<#
.SYNOPSIS
    Watches a Condemned VR run and prints compact live performance telemetry.

.PARAMETER Run
    Run-directory name or full path. Defaults to the newest staged run.

.PARAMETER GameProcessId
    Optional Condemned PID. When supplied, the watcher exits with the game.
#>
[CmdletBinding()]
param(
    [string]$Run,
    [int]$GameProcessId = 0,
    [switch]$Once,
    [ValidateRange(100, 5000)]
    [int]$RefreshMilliseconds = 500
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$logRoot = Join-Path $cfg.ProjectRoot 'stage\condemned-m2-mono\logs'

if ([string]::IsNullOrWhiteSpace($Run)) {
    $runDirectory = Get-ChildItem -LiteralPath $logRoot -Directory |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $runDirectory) {
        throw "No Condemned VR run exists under $logRoot."
    }
} elseif (Test-Path -LiteralPath $Run -PathType Container) {
    $runDirectory = Get-Item -LiteralPath $Run
} else {
    $candidate = Join-Path $logRoot $Run
    if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "Condemned VR run not found: $Run"
    }
    $runDirectory = Get-Item -LiteralPath $candidate
}

function Find-Log([string]$Pattern) {
    return Get-ChildItem -LiteralPath $runDirectory.FullName `
        -Filter $Pattern -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
}

$deadline = (Get-Date).AddSeconds(45)
do {
    $hostLog = Find-Log 'condemnedvr-host-*.log'
    $bridgeLog = Find-Log 'condemnedvr-bridge-*.log'
    if ($null -ne $hostLog -and $null -ne $bridgeLog) { break }
    Start-Sleep -Milliseconds 250
} while ((Get-Date) -lt $deadline)

if ($null -eq $hostLog -or $null -eq $bridgeLog) {
    throw "Host or bridge log did not appear in $($runDirectory.FullName)."
}

function Field([string]$Text, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $match = [Text.RegularExpressions.Regex]::Match(
        $Text,
        [Text.RegularExpressions.Regex]::Escape($Key) + '=([^\s]+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

$readState = @{}
function Read-NewLines([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $readState.ContainsKey($fullPath)) {
        $readState[$fullPath] = [pscustomobject]@{
            Offset = [long]0
            Pending = ''
        }
    }
    $state = $readState[$fullPath]
    $stream = New-Object IO.FileStream(
        $fullPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -lt $state.Offset) {
            $state.Offset = 0
            $state.Pending = ''
        }
        [void]$stream.Seek($state.Offset, [IO.SeekOrigin]::Begin)
        $reader = New-Object IO.StreamReader(
            $stream, [Text.Encoding]::UTF8, $true, 4096, $true)
        try {
            $text = $reader.ReadToEnd()
            $state.Offset = $stream.Position
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }

    $combined = $state.Pending + $text
    if ([string]::IsNullOrEmpty($combined)) { return @() }
    $parts = @([Text.RegularExpressions.Regex]::Split($combined, '\r?\n'))
    if (-not $combined.EndsWith("`n")) {
        $state.Pending = $parts[-1]
        if ($parts.Count -eq 1) { return @() }
        $parts = @($parts[0..($parts.Count - 2)])
    } else {
        $state.Pending = ''
    }
    return @($parts | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Show-Event($entry) {
    $stamp = ([datetime]$entry.time).ToLocalTime().ToString('HH:mm:ss')
    switch ($entry.event) {
        'hid_fps_fix_applied' {
            Write-Host "[$stamp] HID/FPS fix verified and active." `
                -ForegroundColor Green
        }
        'xr_frame_pacing_active' {
            Write-Host "[$stamp] OpenXR request pacing active (20 ms bound)." `
                -ForegroundColor Green
        }
        'xr_frame_pacing_disabled' {
            Write-Host "[$stamp] OpenXR request pacing DISABLED for A/B." `
                -ForegroundColor Yellow
        }
        'perf_frame' {
            $right = [int](Field $entry.message 'render_right_avg_us')
            $mode = if ($right -gt 0) { 'STEREO' } else { 'FLAT' }
            $line = "[$stamp] $mode  XR $(Field $entry.message 'xr_fps') fps" +
                " | game $(Field $entry.message 'game_fps') fps" +
                " | reused $(Field $entry.message 'reused')/300" +
                " | age $(Field $entry.message 'image_age_avg_frames')/" +
                    "$(Field $entry.message 'image_age_max_frames') avg/max" +
                " | eyes $(Field $entry.message 'render_left_avg_us')/" +
                    "$(Field $entry.message 'render_right_avg_us') us" +
                " | copy $(Field $entry.message 'copy_avg_us') us" +
                " | EndFrame max $(Field $entry.message 'endframe_max_us') us"
            Write-Host $line -ForegroundColor Cyan
        }
        'cpu_capture_pipeline' {
            $line = "[$stamp] PIPE  transfer max " +
                "$(Field $entry.message 'transfer_max_us') us" +
                " | stale $(Field $entry.message 'stale_drops')" +
                " | slot $(Field $entry.message 'slot_drops')" +
                " | duplicate $(Field $entry.message 'duplicate_drops')" +
                " | queue $(Field $entry.message 'queue_drops')"
            Write-Host $line -ForegroundColor DarkCyan
        }
        'xr_frame_pacing' {
            $line = "[$stamp] PACE  max wait " +
                "$(Field $entry.message 'max_wait_ms') ms" +
                " | timeouts $(Field $entry.message 'timeouts')/300"
            Write-Host $line -ForegroundColor DarkCyan
        }
        'capture_bypass_present_rate' {
            Write-Host (
                "[$stamp] RAW PRESENT  $(Field $entry.message 'fps') fps") `
                -ForegroundColor Magenta
        }
        'host_stop' {
            Write-Host "[$stamp] OpenXR host stopped." -ForegroundColor Yellow
        }
    }
}

Write-Host "=== Condemned VR live performance: $($runDirectory.Name) ===" `
    -ForegroundColor Cyan
Write-Host 'XR/game are rates; age is OpenXR request frames; eyes/copy are microseconds.'
Write-Host 'Press Ctrl+C to stop watching.'

$gameWasObserved = $false
do {
    foreach ($path in @($hostLog.FullName, $bridgeLog.FullName)) {
        foreach ($line in @(Read-NewLines $path)) {
            try { Show-Event ($line | ConvertFrom-Json) } catch { }
        }
    }
    if ($Once) { break }
    if ($GameProcessId -gt 0) {
        $gameAlive = $null -ne (Get-Process -Id $GameProcessId `
            -ErrorAction SilentlyContinue)
        $gameWasObserved = $gameWasObserved -or $gameAlive
        if ($gameWasObserved -and -not $gameAlive) { break }
    }
    Start-Sleep -Milliseconds $RefreshMilliseconds
} while ($true)

if (-not $Once) {
    Write-Host 'Condemned exited; performance watcher stopped.' `
        -ForegroundColor Yellow
}
