<#
.SYNOPSIS
    Prepares the isolated Condemned M2 mono OpenXR transport stage.

.DESCRIPTION
    Stages the verified stock-client loader, the exact stock client renamed to
    GameOrig.dll, and the guarded frame-transport bridge. Retail files are read
    for verification only and are never changed.
#>
[CmdletBinding()]
param([switch]$Refresh)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

$running = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "Condemned.exe is running (PID $($running.Id -join ', '))."
}

$m0Path = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\stock-no-asi-deployment.json')
if (-not (Test-Path -LiteralPath $m0Path -PathType Leaf)) {
    throw 'The verified M0 stock/no-ASI deployment is missing.'
}
$m0 = Get-Content -Raw -LiteralPath $m0Path | ConvertFrom-Json
$retailRoot = [IO.Path]::GetFullPath($m0.RetailRoot)

$x86Root = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'build\condemned-x86-vs\src')
$loaderSource = Join-Path $x86Root (
    'condemned_gameclient_loader\RelWithDebInfo\GameClient.dll')
$defaultsSource = Join-Path $x86Root (
    'condemned_gameclient_loader\RelWithDebInfo\condemnedvr-defaults.ini')
$pullSoundSource = Join-Path $x86Root (
    'condemned_gameclient_loader\RelWithDebInfo\sounds\colt45_slide_pull.wav')
$returnSoundSource = Join-Path $x86Root (
    'condemned_gameclient_loader\RelWithDebInfo\sounds\colt45_slide_return.wav')
$bridgeSource = Join-Path $x86Root (
    'condemned_proxy32\RelWithDebInfo\condemnedvr-d3d9.dll')
$clientVerifier = Join-Path $x86Root (
    'condemned_gameclient_loader\RelWithDebInfo\verify-condemned-gameclient.exe')
$exeVerifier = Join-Path $x86Root (
    'condemned_gameclient_loader\RelWithDebInfo\verify-condemned-executable.exe')
$hostSource = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'build\condemned-x64-vs\src\condemned_host64\RelWithDebInfo\' +
        'condemnedvr-host.exe'))
$originalSource = Join-Path $retailRoot 'Game\GameClient.dll'
$retailExe = Join-Path $retailRoot 'Condemned.exe'
foreach ($required in @(
        $loaderSource, $defaultsSource, $pullSoundSource, $returnSoundSource,
        $bridgeSource, $clientVerifier, $exeVerifier,
        $hostSource, $originalSource, $retailExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required M2 mono input is missing: $required"
    }
}
& $clientVerifier $originalSource
if ($LASTEXITCODE -ne 0) { throw 'Compiled GameClient verification failed.' }
& $exeVerifier $retailExe
if ($LASTEXITCODE -ne 0) { throw 'Compiled executable verification failed.' }

$stageRoot = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m2-mono')
$moduleDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $stageRoot 'game-override')
$userDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $stageRoot 'userdata')
$logDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $stageRoot 'logs')
$archiveConfig = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $stageRoot 'm2-mono.archcfg')
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $stageRoot 'm2-mono-deployment.json')

New-Item -ItemType Directory -Force -Path @(
    $stageRoot, $moduleDirectory, $userDirectory, $logDirectory) | Out-Null
$allowedNames = @(
    'GameClient.dll', 'GameOrig.dll', 'condemnedvr-defaults.ini', 'condemnedvr-d3d9.dll',
    'condemnedvr-loader.log', 'sounds')
$existing = @(Get-ChildItem -LiteralPath $moduleDirectory -Force)
$unexpected = @($existing | Where-Object {
    $allowedNames -inotcontains $_.Name
})
if ($unexpected.Count -gt 0) {
    throw "M2 mono stage contains unexpected files: $($unexpected.Name -join ', ')"
}
if ($existing.Count -gt 0 -and -not $Refresh) {
    throw "M2 mono stage is not empty; use -Refresh: $moduleDirectory"
}

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
    $destination = Join-Path $moduleDirectory $name
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $destination) | Out-Null
    [IO.File]::Copy($stagedFiles[$name], $destination, [bool]$Refresh)
    $sourceHash = Get-FileSha256 $stagedFiles[$name]
    $destinationHash = Get-FileSha256 $destination
    if ($sourceHash -ne $destinationHash) {
        throw "M2 mono staged copy failed hash verification: $name"
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
        $loaderLog, '', (New-Object Text.UTF8Encoding($false)))
}
$archiveLines = @(
    [IO.File]::ReadAllLines((Join-Path $retailRoot 'default.archcfg')) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$archiveLines += $moduleDirectory
[IO.File]::WriteAllLines(
    $archiveConfig, $archiveLines, [Text.Encoding]::ASCII)

$deployment = [pscustomobject][ordered]@{
    SchemaVersion = 1
    Milestone = 'M2-Mono-OpenXR'
    PreparedAtUtc = [DateTime]::UtcNow.ToString('o')
    RuntimeExe = $m0.RuntimeExe
    WorkingDirectory = $m0.StageRoot
    RetailRoot = $retailRoot
    ModuleDirectory = $moduleDirectory
    UserDirectory = $userDirectory
    LogDirectory = $logDirectory
    ArchiveConfig = $archiveConfig
    ArchiveConfigSha256 = Get-FileSha256 $archiveConfig
    LoaderLog = $loaderLog
    HostExe = $hostSource
    HostSha256 = Get-FileSha256 $hostSource
    Files = $records
    CaptureEnabled = $true
    OpenXrEnabled = $true
    StereoEnabled = $false
    AsiEnabled = $false
}
[IO.File]::WriteAllText(
    $deploymentPath,
    ($deployment | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host '=== Condemned M2 mono OpenXR stage prepared ===' -ForegroundColor Green
Write-Host "Runtime: $($deployment.RuntimeExe)"
Write-Host "Modules: $moduleDirectory"
Write-Host "Host:    $hostSource"
Write-Host 'Behavior: mono back-buffer transport; desktop Present remains retail'
