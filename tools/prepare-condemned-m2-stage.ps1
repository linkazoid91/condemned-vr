<#
.SYNOPSIS
    Prepares the Condemned M2 diagnostic D3D9 stage.

.DESCRIPTION
    Stages the verified M1 loader/original pair plus the project-owned
    condemnedvr-d3d9.dll. This first M2 bridge observes Present and Reset only;
    it does not capture, copy, alter, or submit pixels to OpenXR.
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
    throw 'The verified M0 stock/no-ASI deployment is missing.'
}
$m0 = Get-Content -Raw -LiteralPath $m0DeploymentPath | ConvertFrom-Json
$retailRoot = [IO.Path]::GetFullPath($m0.RetailRoot)

$buildRoot = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'build\condemned-x86-vs\src'))
$loaderSource = Join-Path $buildRoot (
    'condemned_gameclient_loader\RelWithDebInfo\GameClient.dll')
$defaultsSource = Join-Path $buildRoot (
    'condemned_gameclient_loader\RelWithDebInfo\condemnedvr-defaults.ini')
$pullSoundSource = Join-Path $buildRoot (
    'condemned_gameclient_loader\RelWithDebInfo\sounds\colt45_slide_pull.wav')
$returnSoundSource = Join-Path $buildRoot (
    'condemned_gameclient_loader\RelWithDebInfo\sounds\colt45_slide_return.wav')
$bridgeSource = Join-Path $buildRoot (
    'condemned_proxy32\RelWithDebInfo\condemnedvr-d3d9-diagnostic.dll')
$gameClientVerifier = Join-Path $buildRoot (
    'condemned_gameclient_loader\RelWithDebInfo\' +
    'verify-condemned-gameclient.exe')
$executableVerifier = Join-Path $buildRoot (
    'condemned_gameclient_loader\RelWithDebInfo\' +
    'verify-condemned-executable.exe')
$originalSource = Join-Path $retailRoot 'Game\GameClient.dll'
$retailExecutable = Join-Path $retailRoot 'Condemned.exe'
foreach ($required in @(
        $loaderSource,
        $defaultsSource,
        $pullSoundSource,
        $returnSoundSource,
        $bridgeSource,
        $gameClientVerifier,
        $executableVerifier,
        $originalSource,
        $retailExecutable)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required M2 input is missing: $required"
    }
}
& $gameClientVerifier $originalSource
if ($LASTEXITCODE -ne 0) {
    throw 'Compiled GameClient identity verification failed.'
}
& $executableVerifier $retailExecutable
if ($LASTEXITCODE -ne 0) {
    throw 'Compiled Condemned.exe identity verification failed.'
}

$m2Root = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m2')
$moduleDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m2Root 'game-override')
$userDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m2Root 'userdata')
$archiveConfig = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m2Root 'm2.archcfg')
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $m2Root 'm2-deployment.json')

if (Test-Path -LiteralPath $moduleDirectory) {
    $existing = @(Get-ChildItem -LiteralPath $moduleDirectory -Force)
    if ($existing.Count -gt 0) {
        if (-not $Refresh) {
            throw ("M2 module directory is not empty; use -Refresh: " +
                $moduleDirectory)
        }
        $allowedNames = @(
            'GameClient.dll',
            'condemnedvr-defaults.ini',
            'GameOrig.dll',
            'condemnedvr-d3d9.dll',
            'condemnedvr-loader.log',
            'condemnedvr-d3d9.log',
            'sounds'
        )
        $unexpected = @($existing |
            Where-Object { $allowedNames -inotcontains $_.Name })
        if ($unexpected.Count -gt 0) {
            throw ("M2 refresh found unexpected files: " +
                (($unexpected | Select-Object -ExpandProperty Name) -join ', '))
        }
    }
} else {
    New-Item -ItemType Directory -Path $moduleDirectory -Force | Out-Null
}
New-Item -ItemType Directory -Path $userDirectory -Force | Out-Null

$stagedFiles = [ordered]@{
    'GameClient.dll' = $loaderSource
    'condemnedvr-defaults.ini' = $defaultsSource
    'sounds\colt45_slide_pull.wav' = $pullSoundSource
    'sounds\colt45_slide_return.wav' = $returnSoundSource
    'GameOrig.dll' = $originalSource
    'condemnedvr-d3d9.dll' = $bridgeSource
}
$records = New-Object Collections.Generic.List[object]
foreach ($name in $stagedFiles.Keys) {
    $source = $stagedFiles[$name]
    $destination = Join-Path $moduleDirectory $name
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $destination) | Out-Null
    [IO.File]::Copy($source, $destination, [bool]$Refresh)
    $sourceHash = Get-FileSha256 $source
    $destinationHash = Get-FileSha256 $destination
    if ($sourceHash -ne $destinationHash) {
        throw "M2 staged copy failed hash verification: $name"
    }
    $records.Add([pscustomobject][ordered]@{
        Name = $name
        Length = (Get-Item -LiteralPath $destination).Length
        Sha256 = $destinationHash
    })
}

$loaderLog = Join-Path $moduleDirectory 'condemnedvr-loader.log'
$bridgeLog = Join-Path $moduleDirectory 'condemnedvr-d3d9.log'
foreach ($log in @($loaderLog, $bridgeLog)) {
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        [IO.File]::WriteAllText(
            $log, '', (New-Object Text.UTF8Encoding($false)))
    }
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
    Milestone = 'M2-Diagnostic'
    PreparedAtUtc = [DateTime]::UtcNow.ToString('o')
    RuntimeExe = $m0.RuntimeExe
    WorkingDirectory = $m0.StageRoot
    RetailRoot = $retailRoot
    ModuleDirectory = $moduleDirectory
    UserDirectory = $userDirectory
    ArchiveConfig = $archiveConfig
    ArchiveConfigSha256 = Get-FileSha256 $archiveConfig
    LoaderLog = $loaderLog
    BridgeLog = $bridgeLog
    Files = $records
    CaptureEnabled = $false
    OpenXrEnabled = $false
    AsiEnabled = $false
}
[IO.File]::WriteAllText(
    $deploymentPath,
    ($deployment | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host '=== Condemned M2 diagnostic stage prepared ===' `
    -ForegroundColor Green
Write-Host "Runtime:  $($deployment.RuntimeExe)"
Write-Host "Modules:  $moduleDirectory"
Write-Host "ArchCfg:  $archiveConfig"
Write-Host 'Behavior: D3D9 Present/Reset observation only; capture and OpenXR off'
