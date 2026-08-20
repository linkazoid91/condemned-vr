<#
.SYNOPSIS
    Runs headset-free regressions for release-package safety helpers.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-env.ps1')
. (Join-Path $PSScriptRoot 'release\_condemnedvr-release.ps1')
$cfg = Get-CondemnedVrConfig

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Expect-Throw([scriptblock]$Action, [string]$Pattern) {
    $threw = $false
    try {
        & $Action
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch $Pattern) {
            throw (
                'Unexpected error. Expected /{0}/, received: {1}' -f
                $Pattern, $_.Exception.Message)
        }
    }
    if (-not $threw) {
        throw ('Expected an error matching /{0}/.' -f $Pattern)
    }
}

$testParent = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\release-tool-tests')
$testRoot = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $testParent ([Guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

try {
    $installRoot = Join-Path $testRoot 'install'
    $retailRoot = Join-Path $testRoot 'retail'
    $packageRoot = Join-Path $testRoot 'package'
    foreach ($directory in @($installRoot, $retailRoot, $packageRoot)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }

    Assert-True (Test-PathInside $installRoot $testRoot) (
        'Install root should be inside the test root.')
    Assert-True (-not (Test-PathInside $testRoot $installRoot)) (
        'Test root must not be inside the install root.')
    $safe = Assert-SafeCondemnedVrInstallRoot (
        $installRoot) $retailRoot $packageRoot
    Assert-True ($safe -eq (Get-NormalizedDirectoryPath $installRoot)) (
        'Safe install-root normalization failed.')
    Expect-Throw {
        Assert-SafeCondemnedVrInstallRoot (
            (Join-Path $retailRoot 'mod')) $retailRoot $packageRoot
    } 'must not overlap'
    Expect-Throw {
        Resolve-SafeRelativePath $packageRoot '..\escape.txt'
    } 'escapes'

    $payload = Join-Path $packageRoot 'payload.txt'
    [IO.File]::WriteAllText(
        $payload, 'package-payload', (New-Object Text.UTF8Encoding($false)))
    $manifest = [ordered]@{
        schemaVersion = 1
        product = 'Condemned VR'
        version = 'test'
        gitCommit = 'test'
        containsRetailContent = $false
        files = @([ordered]@{
            path = 'payload.txt'
            sha256 = Get-FileSha256 $payload
            bytes = (Get-Item -LiteralPath $payload).Length
        })
    }
    [IO.File]::WriteAllText(
        (Join-Path $packageRoot 'release-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 5),
        (New-Object Text.UTF8Encoding($false)))
    $verified = Test-CondemnedVrReleasePackage $packageRoot
    Assert-True ($verified.version -eq 'test') (
        'Valid package manifest was not returned.')
    [IO.File]::AppendAllText($payload, '-tampered')
    Expect-Throw {
        Test-CondemnedVrReleasePackage $packageRoot
    } 'missing or changed'

    $installManifest = [ordered]@{
        schemaVersion = 1
        productId = 'CondemnedVR'
        installRoot = (Get-NormalizedDirectoryPath $installRoot)
    }
    [IO.File]::WriteAllText(
        (Join-Path $installRoot 'condemnedvr-install.json'),
        ($installManifest | ConvertTo-Json -Depth 3),
        (New-Object Text.UTF8Encoding($false)))
    $readBack = Read-CondemnedVrInstallManifest $installRoot
    Assert-True ($readBack.productId -eq 'CondemnedVR') (
        'Valid install marker was not returned.')

    foreach ($script in @(
            'release\_condemnedvr-release.ps1',
            'release\install.ps1',
            'release\play.ps1',
            'release\uninstall.ps1',
            'make-release.ps1'
        )) {
        $tokens = $null
        $errors = $null
        [Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $PSScriptRoot $script),
            [ref]$tokens,
            [ref]$errors) | Out-Null
        Assert-True ($errors.Count -eq 0) (
            'PowerShell parser rejected {0}: {1}' -f
            $script, ($errors.Message -join '; '))
    }

    foreach ($wrapper in @('Install.cmd', 'Play.cmd', 'Uninstall.cmd')) {
        Assert-True (Test-Path -LiteralPath (
            Join-Path $PSScriptRoot ('release\' + $wrapper)) -PathType Leaf) (
            'Release wrapper is missing: {0}' -f $wrapper)
    }

    foreach ($expectation in @(
            @{
                Path = 'make-release.ps1'
                Text = @(
                    'bin\x86\sounds\colt45_slide_pull.wav',
                    'bin\x86\sounds\colt45_slide_return.wav')
            },
            @{
                Path = 'release\install.ps1'
                Text = @(
                    'sounds\colt45_slide_pull.wav',
                    'sounds\colt45_slide_return.wav')
            })) {
        $scriptText = Get-Content -Raw -LiteralPath (
            Join-Path $PSScriptRoot $expectation.Path)
        foreach ($requiredText in $expectation.Text) {
            Assert-True $scriptText.Contains($requiredText) (
                '{0} is missing licensed slide-audio payload: {1}' -f
                $expectation.Path, $requiredText)
        }
    }
} finally {
    $validatedRoot = Assert-UnderCondemnedVrProjectRoot $testRoot
    if (Test-Path -LiteralPath $validatedRoot -PathType Container) {
        Remove-Item -LiteralPath $validatedRoot -Recurse -Force
    }
}

Write-Host 'Condemned VR release-tool regressions passed.' -ForegroundColor Green
