<#
.SYNOPSIS
    Launches the Condemned M2 diagnostic D3D9 bridge.
#>
[CmdletBinding()]
param(
    [switch]$ValidateOnly,
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m2\m2-deployment.json')
if (-not (Test-Path -LiteralPath $deploymentPath -PathType Leaf)) {
    throw ('No M2 diagnostic deployment found. Run ' +
        'tools\prepare-condemned-m2-stage.ps1 first.')
}
$deployment = Get-Content -Raw -LiteralPath $deploymentPath | ConvertFrom-Json

$runtimeExe = Assert-UnderCondemnedVrProjectRoot $deployment.RuntimeExe
$workingDirectory = Assert-UnderCondemnedVrProjectRoot (
    $deployment.WorkingDirectory)
$moduleDirectory = Assert-UnderCondemnedVrProjectRoot (
    $deployment.ModuleDirectory)
$userDirectory = Assert-UnderCondemnedVrProjectRoot (
    $deployment.UserDirectory)
$archiveConfig = Assert-UnderCondemnedVrProjectRoot (
    $deployment.ArchiveConfig)
foreach ($record in $deployment.Files) {
    $path = Join-Path $moduleDirectory $record.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileSha256 $path) -ne $record.Sha256) {
        throw "M2 staged module changed: $($record.Name)"
    }
}
if ((Get-FileSha256 $archiveConfig) -ne $deployment.ArchiveConfigSha256) {
    throw 'M2 archive configuration changed after preparation.'
}
foreach ($forbidden in @(
        (Join-Path $workingDirectory 'd3d9.dll'),
        (Join-Path $workingDirectory 'scripts'))) {
    if (Test-Path -LiteralPath $forbidden) {
        throw "M2 isolated runtime contains a forbidden ASI component: $forbidden"
    }
}

Write-Host 'M2 diagnostic stage contract verified.' -ForegroundColor Green
if ($ValidateOnly) {
    exit 0
}

$existing = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    throw "Condemned.exe is already running (PID $($existing.Id -join ', '))."
}
foreach ($log in @($deployment.LoaderLog, $deployment.BridgeLog)) {
    [IO.File]::WriteAllText(
        $log, '', (New-Object Text.UTF8Encoding($false)))
}

$env:SteamAppId = [string]$cfg.SteamAppId
$env:SteamGameId = [string]$cfg.SteamAppId
$arguments = @(
    '-archcfg',
    "`"$archiveConfig`"",
    '-userdirectory',
    "`"$userDirectory`""
)
$process = Start-Process -FilePath $runtimeExe `
    -ArgumentList $arguments `
    -WorkingDirectory $workingDirectory `
    -PassThru
Write-Host "Condemned M2 diagnostic launch started (PID $($process.Id))."

$inspectorPowerShell = Join-Path $env:WINDIR (
    'SysWOW64\WindowsPowerShell\v1.0\powershell.exe')
$inspectorScript = Join-Path $PSScriptRoot 'inspect-condemned-process32.ps1'
$deadline = (Get-Date).AddSeconds(30)
$modules = @()
$loaderLogText = ''
$bridgeLogText = ''
do {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    if ($process.HasExited) {
        throw ("M2 Condemned exited before D3D9 verification " +
            "(exit code $($process.ExitCode)).")
    }

    $inspectionJson = & $inspectorPowerShell `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $inspectorScript `
        -ProcessId $process.Id
    if ($LASTEXITCODE -eq 0) {
        $modules = @(
            ($inspectionJson -join [Environment]::NewLine |
                ConvertFrom-Json).Modules
        )
    }
    $loaderLogText = [IO.File]::ReadAllText($deployment.LoaderLog)
    $bridgeLogText = [IO.File]::ReadAllText($deployment.BridgeLog)
    $loadedBridge = $modules |
        Where-Object { $_.Name -ieq 'condemnedvr-d3d9.dll' } |
        Select-Object -First 1
} until (($null -ne $loadedBridge -and
        $loaderLogText.Contains('"event":"bridge_loaded"') -and
        $bridgeLogText.Contains('"event":"hooks_installed"') -and
        $bridgeLogText.Contains('"event":"present_observed"')) -or
    (Get-Date) -ge $deadline)

if ($loaderLogText.Contains('"event":"bridge_rejected"') -or
    $bridgeLogText.Contains('"event":"bridge_rejected"')) {
    throw ("M2 bridge was rejected. Loader: $loaderLogText Bridge: " +
        $bridgeLogText)
}
if ($null -eq $loadedBridge) {
    throw 'condemnedvr-d3d9.dll was not loaded.'
}
if (-not $bridgeLogText.Contains('"event":"hooks_installed"')) {
    throw "D3D9 hooks were not installed: $bridgeLogText"
}
if (-not $bridgeLogText.Contains('"event":"present_observed"')) {
    throw "No game Present was observed: $bridgeLogText"
}

$asiModules = @($modules | Where-Object { $_.Name -like '*.asi' })
if ($asiModules.Count -ne 0) {
    throw 'M2 diagnostic run unexpectedly loaded an ASI module.'
}
$report = [pscustomobject][ordered]@{
    SchemaVersion = 1
    CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    ProcessId = $process.Id
    RuntimeExe = $process.Path
    Bridge = $loadedBridge
    AsiModules = $asiModules
    LoaderLog = $loaderLogText.Trim()
    BridgeLogLines = @($bridgeLogText -split "`r?`n" |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    CaptureEnabled = $false
    OpenXrEnabled = $false
}
$reportPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'stage\condemned-m2\m2-diagnostic-live-{0}.json' -f
        (Get-Date -Format 'yyyyMMdd-HHmmss')))
[IO.File]::WriteAllText(
    $reportPath,
    ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host "Bridge: $($loadedBridge.Path)"
Write-Host "ASI:    $($asiModules.Count) loaded"
Write-Host "Log:    $($deployment.BridgeLog)"
Write-Host "Report: $reportPath"
Write-Host 'M2 D3D9 observation passed; pixels unchanged, game left running.' `
    -ForegroundColor Green

if ($Wait) {
    $process.WaitForExit()
    Write-Host "Condemned exited with code $($process.ExitCode)."
    exit $process.ExitCode
}
