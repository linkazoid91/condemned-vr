<#
.SYNOPSIS
    Prepares the isolated Condemned M1 stock-client delegation stage.

.DESCRIPTION
    Adds the project GameClient.dll and a verified stock GameOrig.dll as the
    final loose archive layer over the working M0 no-ASI stage. No renderer,
    input, OpenXR, or gameplay hook is included.
#>
[CmdletBinding()]
param(
    [switch]$Refresh
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

$running = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "Condemned.exe is running (PID $($running.Id -join ', '))."
}

$m0DeploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\stock-no-asi-deployment.json')
if (-not (Test-Path -LiteralPath $m0DeploymentPath -PathType Leaf)) {
    throw ('The M0 stock/no-ASI stage is missing. Run ' +
        'tools\prepare-condemned-m0-stock-stage.ps1 first.')
}
& "$PSScriptRoot\launch-condemned-m0-stock.ps1" -ValidateOnly
if ($LASTEXITCODE -ne 0) {
    throw 'The M0 stock/no-ASI stage contract failed validation.'
}

$m0 = Get-Content -Raw -LiteralPath $m0DeploymentPath | ConvertFrom-Json
$retailRoot = [IO.Path]::GetFullPath($m0.RetailRoot)
$loaderSource = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'build\condemned-x86-vs\src\condemned_gameclient_loader\' +
        'RelWithDebInfo\GameClient.dll'))
$identityVerifier = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'build\condemned-x86-vs\src\condemned_gameclient_loader\' +
        'RelWithDebInfo\verify-condemned-gameclient.exe'))
$originalSource = Join-Path $retailRoot 'Game\GameClient.dll'
foreach ($required in @($loaderSource, $identityVerifier, $originalSource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required M1 input is missing: $required"
    }
}
if ((Get-FileSha256 $originalSource) -ne
    $cfg.CriticalFiles['Game\GameClient.dll'].Sha256) {
    throw 'Retail GameClient.dll no longer matches Condemned 1.0.314.0.'
}
& $identityVerifier $originalSource
if ($LASTEXITCODE -ne 0) {
    throw 'The compiled M1 identity descriptor rejected retail GameClient.dll.'
}

$m1Root = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m1')
$moduleDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m1Root 'game-override')
$userDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m1Root 'userdata')
$archiveConfig = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m1Root 'm1.archcfg')
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m1Root 'm1-deployment.json')

if (Test-Path -LiteralPath $moduleDirectory) {
    $existing = @(Get-ChildItem -LiteralPath $moduleDirectory -Force)
    if ($existing.Count -gt 0) {
        if (-not $Refresh) {
            throw ("M1 module directory is not empty and will not be reused " +
                "without -Refresh: $moduleDirectory")
        }
        $allowedNames = @(
            'GameClient.dll',
            'GameOrig.dll',
            'condemnedvr-loader.log'
        )
        $unexpected = @($existing |
            Where-Object { $allowedNames -inotcontains $_.Name })
        if ($unexpected.Count -gt 0) {
            throw ("M1 refresh found unexpected files: " +
                (($unexpected | Select-Object -ExpandProperty Name) -join ', '))
        }
    }
} else {
    New-Item -ItemType Directory -Path $moduleDirectory -Force | Out-Null
}
New-Item -ItemType Directory -Path $userDirectory -Force | Out-Null

$stagedFiles = [ordered]@{
    'GameClient.dll' = $loaderSource
    'GameOrig.dll' = $originalSource
}
$records = New-Object Collections.Generic.List[object]
foreach ($name in $stagedFiles.Keys) {
    $source = $stagedFiles[$name]
    $destination = Join-Path $moduleDirectory $name
    [IO.File]::Copy($source, $destination, [bool]$Refresh)
    $sourceHash = Get-FileSha256 $source
    $destinationHash = Get-FileSha256 $destination
    if ($sourceHash -ne $destinationHash) {
        throw "M1 staged copy failed hash verification: $name"
    }
    $records.Add([pscustomobject][ordered]@{
        Name = $name
        Length = (Get-Item -LiteralPath $destination).Length
        Sha256 = $destinationHash
    })
}
$loaderLog = Join-Path $moduleDirectory 'condemnedvr-loader.log'
if (Test-Path -LiteralPath $loaderLog -PathType Leaf) {
    [IO.File]::WriteAllText(
        $loaderLog,
        '',
        (New-Object Text.UTF8Encoding($false)))
}

$retailArchiveConfig = Join-Path $retailRoot 'default.archcfg'
$archiveLines = @(
    [IO.File]::ReadAllLines($retailArchiveConfig) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
$archiveLines += $moduleDirectory
[IO.File]::WriteAllLines(
    $archiveConfig, $archiveLines, [Text.Encoding]::ASCII)

$deployment = [pscustomobject][ordered]@{
    SchemaVersion = 1
    Milestone = 'M1'
    PreparedAtUtc = [DateTime]::UtcNow.ToString('o')
    RuntimeExe = $m0.RuntimeExe
    WorkingDirectory = $m0.StageRoot
    RetailRoot = $retailRoot
    ModuleDirectory = $moduleDirectory
    UserDirectory = $userDirectory
    ArchiveConfig = $archiveConfig
    ArchiveConfigSha256 = Get-FileSha256 $archiveConfig
    LoaderLog = $loaderLog
    Files = $records
    HooksEnabled = $false
    AsiEnabled = $false
}
[IO.File]::WriteAllText(
    $deploymentPath,
    ($deployment | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host '=== Condemned M1 delegation stage prepared ===' `
    -ForegroundColor Green
Write-Host "Runtime:  $($deployment.RuntimeExe)"
Write-Host "Modules:  $moduleDirectory"
Write-Host "ArchCfg:  $archiveConfig"
Write-Host "Userdata: $userDirectory"
Write-Host 'Behavior: verified pass-through only; no hooks, D3D bridge, ASI, or VR'
