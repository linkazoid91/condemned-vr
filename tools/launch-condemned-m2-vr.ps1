<#
.SYNOPSIS
    Launches the isolated Condemned M2 mono OpenXR transport.

.PARAMETER RuntimeManifest
    Optional OpenXR runtime JSON applied only to the new host process. When
    omitted, the system-wide active x64 OpenXR runtime is used.

.PARAMETER ValidateOnly
    Validates the host/runtime/session/swapchains without launching Condemned.

#>
[CmdletBinding()]
param(
    [string]$RuntimeManifest,
    [switch]$ValidateOnly,
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

function Read-LiveLog([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    $stream = New-Object IO.FileStream(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
    try {
        return $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'stage\condemned-m2-mono\m2-mono-deployment.json'))
if (-not (Test-Path -LiteralPath $deploymentPath -PathType Leaf)) {
    throw 'No M2 mono deployment found. Run prepare-condemned-m2-mono-stage.ps1.'
}
$deployment = Get-Content -Raw -LiteralPath $deploymentPath | ConvertFrom-Json

if ((Get-FileSha256 $deployment.ArchiveConfig) -ne
    $deployment.ArchiveConfigSha256) {
    throw 'M2 mono archive configuration changed after preparation.'
}
if ((Get-FileSha256 $deployment.HostExe) -ne $deployment.HostSha256) {
    throw 'M2 mono host changed after preparation.'
}
foreach ($record in $deployment.Files) {
    $path = Join-Path $deployment.ModuleDirectory $record.Name
    if ((Get-FileSha256 $path) -ne $record.Sha256) {
        throw "M2 mono staged module changed: $($record.Name)"
    }
}
foreach ($forbidden in @('d3d9.dll', 'scripts')) {
    $path = Join-Path $deployment.WorkingDirectory $forbidden
    if (Test-Path -LiteralPath $path) {
        throw "M2 isolated runtime contains a forbidden ASI component: $path"
    }
}
if (-not [string]::IsNullOrWhiteSpace($RuntimeManifest)) {
    $RuntimeManifest = [IO.Path]::GetFullPath($RuntimeManifest.Trim('"'))
    if (-not (Test-Path -LiteralPath $RuntimeManifest -PathType Leaf)) {
        throw "OpenXR runtime manifest not found: $RuntimeManifest"
    }
}

$existingGame = @(Get-Process -Name Condemned -ErrorAction SilentlyContinue)
if ($existingGame.Count -gt 0) {
    throw "Condemned.exe is already running (PID $($existingGame.Id -join ', '))."
}
$existingHost = @(Get-Process -Name condemnedvr-host -ErrorAction SilentlyContinue)
if ($existingHost.Count -gt 0) {
    throw "condemnedvr-host.exe is already running (PID $($existingHost.Id -join ', '))."
}

$runLogDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $deployment.LogDirectory (
        'run-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')))
New-Item -ItemType Directory -Force -Path @(
    $runLogDirectory, $deployment.UserDirectory) | Out-Null

$sessionId = [uint64]([DateTime]::UtcNow.Ticks)
$sessionId = $sessionId -bxor ([uint64]$PID -shl 32)
if ($sessionId -eq 0) { $sessionId = 1 }
$sessionText = '0x{0:X16}' -f $sessionId
$hostArguments = @('--log-dir', "`"$runLogDirectory`"")
if ($ValidateOnly) {
    $hostArguments += '--validate-only'
} else {
    $hostArguments += @(
        '--ipc-session', $sessionText,
        '--exit-on-game-disconnect')
}

$previousRuntime = $env:XR_RUNTIME_JSON
try {
    if (-not [string]::IsNullOrWhiteSpace($RuntimeManifest)) {
        $env:XR_RUNTIME_JSON = $RuntimeManifest
    }
    $hostProcess = Start-Process -FilePath $deployment.HostExe `
        -ArgumentList $hostArguments `
        -WorkingDirectory (Split-Path -Parent $deployment.HostExe) `
        -WindowStyle Hidden `
        -PassThru
} finally {
    $env:XR_RUNTIME_JSON = $previousRuntime
}

$hostLog = $null
$hostText = ''
$deadline = (Get-Date).AddSeconds(35)
do {
    Start-Sleep -Milliseconds 200
    $hostProcess.Refresh()
    $hostLog = Get-ChildItem -LiteralPath $runLogDirectory `
        -Filter 'condemnedvr-host-*.log' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -ne $hostLog) {
        $hostText = Read-LiveLog $hostLog.FullName
    }
    if ($hostProcess.HasExited -and
        -not $hostText.Contains('"event":"xr_ready"')) {
        throw "OpenXR host exited before xr_ready (code $($hostProcess.ExitCode))."
    }
} until ($hostText.Contains('"event":"xr_ready"') -or
    (Get-Date) -ge $deadline)
if (-not $hostText.Contains('"event":"xr_ready"')) {
    if (-not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id -Force }
    throw 'OpenXR host did not become ready within 35 seconds.'
}

if ($ValidateOnly) {
    $hostProcess.WaitForExit(10000) | Out-Null
    $hostProcess.Refresh()
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
    }
    Write-Host 'Condemned VR host/runtime validation passed.' -ForegroundColor Green
    Write-Host "Host log: $($hostLog.FullName)"
    exit 0
}

[IO.File]::WriteAllText(
    $deployment.LoaderLog, '', (New-Object Text.UTF8Encoding($false)))
$env:SteamAppId = [string]$cfg.SteamAppId
$env:SteamGameId = [string]$cfg.SteamAppId
$gameArguments = @(
    '-condemnedvr-session', $sessionText,
    '-condemnedvr-logdir', "`"$runLogDirectory`"",
    '-archcfg', "`"$($deployment.ArchiveConfig)`"",
    '-userdirectory', "`"$($deployment.UserDirectory)`"")
$game = $null
try {
    $game = Start-Process -FilePath $deployment.RuntimeExe `
        -ArgumentList $gameArguments `
        -WorkingDirectory $deployment.WorkingDirectory `
        -PassThru

    $inspectorPowerShell = Join-Path $env:WINDIR (
        'SysWOW64\WindowsPowerShell\v1.0\powershell.exe')
    $inspectorScript = Join-Path $PSScriptRoot 'inspect-condemned-process32.ps1'
    $proxyLog = $null
    $proxyText = ''
    $modules = @()
    $deadline = (Get-Date).AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 250
        $game.Refresh()
        $hostProcess.Refresh()
        if ($game.HasExited) {
            throw "Condemned exited before mono transport verification (code $($game.ExitCode))."
        }
        if ($hostProcess.HasExited) {
            throw "OpenXR host exited during transport verification (code $($hostProcess.ExitCode))."
        }
        $inspectionJson = & $inspectorPowerShell -NoProfile `
            -ExecutionPolicy Bypass -File $inspectorScript -ProcessId $game.Id
        if ($LASTEXITCODE -eq 0) {
            $modules = @((($inspectionJson -join [Environment]::NewLine) |
                ConvertFrom-Json).Modules)
        }
        $proxyLog = Get-ChildItem -LiteralPath $runLogDirectory `
            -Filter 'condemnedvr-bridge-*.log' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
        if ($null -ne $proxyLog) {
            $proxyText = Read-LiveLog $proxyLog.FullName
        }
        $hostText = Read-LiveLog $hostLog.FullName
        if ($proxyText.Contains('"event":"adapter_mismatch"')) {
            throw 'D3D9 and OpenXR selected different GPU adapters.'
        }
        $bridgeReady =
            $proxyText.Contains('"event":"late_hooks_installed"') -and
            $proxyText.Contains('"event":"adapter_match"') -and
            $proxyText.Contains('"event":"shared_resources"') -and
            $proxyText.Contains('"event":"frame_ready"')
        $hostReady =
            $hostText.Contains('"event":"ipc_connected"') -and
            $hostText.Contains('"event":"ipc_frame"') -and
            $hostText.Contains('"event":"mono_quad_layer"')
    } until (($bridgeReady -and $hostReady) -or (Get-Date) -ge $deadline)

    if (-not $bridgeReady -or -not $hostReady) {
        throw 'The mono OpenXR frame path did not become ready within 45 seconds.'
    }
    $loadedBridge = $modules | Where-Object {
        $_.Name -ieq 'condemnedvr-d3d9.dll'
    } | Select-Object -First 1
    $expectedBridge = Join-Path $deployment.ModuleDirectory (
        'condemnedvr-d3d9.dll')
    if ($null -eq $loadedBridge -or
        [IO.Path]::GetFullPath($loadedBridge.Path) -ne
        [IO.Path]::GetFullPath($expectedBridge)) {
        throw 'The guarded transport bridge was not loaded from the M2 stage.'
    }
    $asiModules = @($modules | Where-Object { $_.Name -like '*.asi' })
    if ($asiModules.Count -ne 0) {
        throw 'M2 mono unexpectedly loaded an ASI module.'
    }

    $reportPath = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $runLogDirectory 'm2-mono-live.json')
    $report = [pscustomobject][ordered]@{
        SchemaVersion = 1
        CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
        Session = $sessionText
        GameProcessId = $game.Id
        HostProcessId = $hostProcess.Id
        Bridge = $loadedBridge
        AsiModules = $asiModules
        HostLog = $hostLog.FullName
        ProxyLog = $proxyLog.FullName
        CaptureEnabled = $true
        OpenXrEnabled = $true
        StereoEnabled = $false
    }
    [IO.File]::WriteAllText(
        $reportPath,
        ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false)))

    Write-Host 'Condemned M2 mono OpenXR transport is live.' -ForegroundColor Green
    Write-Host "Game PID: $($game.Id)  Host PID: $($hostProcess.Id)"
    Write-Host "Host log:  $($hostLog.FullName)"
    Write-Host "Proxy log: $($proxyLog.FullName)"
    Write-Host "Report:    $reportPath"
    Write-Host 'The headset should show the normal desktop image on a stable mono quad.'
} catch {
    if ($null -ne $game) {
        $game.Refresh()
        if (-not $game.HasExited) { Stop-Process -Id $game.Id -Force }
    }
    $hostProcess.Refresh()
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
    }
    throw
}

if ($Wait) {
    $game.WaitForExit()
    $hostProcess.WaitForExit(10000) | Out-Null
    $hostProcess.Refresh()
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
    }
    Write-Host 'Condemned and its M2 host exited.'
}
