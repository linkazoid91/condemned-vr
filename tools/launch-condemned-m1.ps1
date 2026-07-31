<#
.SYNOPSIS
    Launches Condemned through the M1 pass-through GameClient loader.

.DESCRIPTION
    Validates the M1 deployment, starts the isolated runtime, and uses the
    32-bit module inspector plus the loader log to prove that project
    GameClient.dll delegated to the verified stock GameOrig.dll.
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
    Join-Path $cfg.ProjectRoot 'stage\condemned-m1\m1-deployment.json')
if (-not (Test-Path -LiteralPath $deploymentPath -PathType Leaf)) {
    throw ('No M1 deployment found. Run ' +
        'tools\prepare-condemned-m1-stage.ps1 first.')
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
        throw "M1 staged module changed: $($record.Name)"
    }
}
if ((Get-FileSha256 (Join-Path $moduleDirectory 'GameOrig.dll')) -ne
    $cfg.CriticalFiles['Game\GameClient.dll'].Sha256) {
    throw 'M1 GameOrig.dll is not the verified Condemned 1.0.314.0 client.'
}
if ((Get-FileSha256 $archiveConfig) -ne $deployment.ArchiveConfigSha256) {
    throw 'M1 archive configuration changed after preparation.'
}
foreach ($forbidden in @(
        (Join-Path $workingDirectory 'd3d9.dll'),
        (Join-Path $workingDirectory 'scripts'),
        (Join-Path $moduleDirectory 'condemnedvr-d3d9.dll'))) {
    if (Test-Path -LiteralPath $forbidden) {
        throw "M1 pass-through stage contains a forbidden hook component: $forbidden"
    }
}
Write-Host 'M1 pass-through stage contract verified.' -ForegroundColor Green
if ($ValidateOnly) {
    exit 0
}

$existing = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    throw "Condemned.exe is already running (PID $($existing.Id -join ', '))."
}
if (Test-Path -LiteralPath $deployment.LoaderLog -PathType Leaf) {
    [IO.File]::WriteAllText(
        $deployment.LoaderLog,
        '',
        (New-Object Text.UTF8Encoding($false)))
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
Write-Host "Condemned M1 launch started (PID $($process.Id))."

$inspectorPowerShell = Join-Path $env:WINDIR (
    'SysWOW64\WindowsPowerShell\v1.0\powershell.exe')
$inspectorScript = Join-Path $PSScriptRoot 'inspect-condemned-process32.ps1'
$deadline = (Get-Date).AddSeconds(30)
$modules = @()
$loaderLogText = ''
do {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    if ($process.HasExited) {
        throw ("M1 Condemned exited before delegation verification " +
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
    if (Test-Path -LiteralPath $deployment.LoaderLog -PathType Leaf) {
        $loaderLogText = [IO.File]::ReadAllText($deployment.LoaderLog)
    }

    $loadedLoader = $modules |
        Where-Object { $_.Name -ieq 'GameClient.dll' } |
        Select-Object -First 1
    $loadedOriginal = $modules |
        Where-Object { $_.Name -ieq 'GameOrig.dll' } |
        Select-Object -First 1
} until (($null -ne $loadedLoader -and
        $null -ne $loadedOriginal -and
        $loaderLogText.Contains('"event":"original_loaded"')) -or
    (Get-Date) -ge $deadline)

if ($null -eq $loadedLoader) {
    throw 'Project GameClient.dll was not loaded through the M1 archive layer.'
}
if ($null -eq $loadedOriginal) {
    throw ("Verified GameOrig.dll was not loaded. Loader log: " +
        $loaderLogText.Trim())
}
if (-not $loaderLogText.Contains('"event":"original_loaded"')) {
    throw "Loader did not record verified delegation: $loaderLogText"
}

$expectedLoader = [IO.Path]::GetFullPath(
    (Join-Path $moduleDirectory 'GameClient.dll'))
$expectedOriginal = [IO.Path]::GetFullPath(
    (Join-Path $moduleDirectory 'GameOrig.dll'))
$actualLoader = [IO.Path]::GetFullPath([string]$loadedLoader.Path)
$actualOriginal = [IO.Path]::GetFullPath([string]$loadedOriginal.Path)
if (-not $actualLoader.Equals(
        $expectedLoader,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Wrong GameClient.dll loaded: $actualLoader"
}
if (-not $actualOriginal.Equals(
        $expectedOriginal,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Wrong GameOrig.dll loaded: $actualOriginal"
}

$d3d9 = $modules |
    Where-Object { $_.Name -ieq 'd3d9.dll' } |
    Select-Object -First 1
$asiModules = @($modules | Where-Object { $_.Name -like '*.asi' })
$report = [pscustomobject][ordered]@{
    SchemaVersion = 1
    CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    ProcessId = $process.Id
    RuntimeExe = $process.Path
    Loader = $loadedLoader
    Original = $loadedOriginal
    D3d9 = $d3d9
    AsiModules = $asiModules
    LoaderLog = $loaderLogText.Trim()
    HooksEnabled = $false
}
$reportPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'stage\condemned-m1\m1-live-{0}.json' -f
        (Get-Date -Format 'yyyyMMdd-HHmmss')))
[IO.File]::WriteAllText(
    $reportPath,
    ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host "Loader:   $actualLoader"
Write-Host "Original: $actualOriginal"
Write-Host "D3D9:     $($d3d9.Path)"
Write-Host "ASI:      $($asiModules.Count) loaded"
Write-Host "Report:   $reportPath"
Write-Host 'M1 verified stock-client delegation passed; game left running.' `
    -ForegroundColor Green

if ($Wait) {
    $process.WaitForExit()
    Write-Host "Condemned exited with code $($process.ExitCode)."
    exit $process.ExitCode
}
