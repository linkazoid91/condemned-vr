<#
.SYNOPSIS
    Summarizes performance telemetry from a staged Condemned VR run.

.PARAMETER Run
    Run-directory name or full path. Defaults to the newest staged run.

.PARAMETER AsJson
    Emits the report as JSON for comparison or archival.
#>
[CmdletBinding()]
param(
    [string]$Run,
    [switch]$AsJson
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

function Read-Events([string]$Pattern) {
    $file = Get-ChildItem -LiteralPath $runDirectory.FullName `
        -Filter $Pattern -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $file) { return @() }
    return @(
        Get-Content -LiteralPath $file.FullName |
            Where-Object { $_.TrimStart().StartsWith('{') } |
            ForEach-Object { try { $_ | ConvertFrom-Json } catch { } }
    )
}

function Field([string]$Text, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $match = [Text.RegularExpressions.Regex]::Match(
        $Text,
        [Text.RegularExpressions.Regex]::Escape($Key) + '=([^\s]+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

function First-Message($Events, [string]$Name) {
    $entry = $Events | Where-Object { $_.event -eq $Name } |
        Select-Object -First 1
    if ($null -ne $entry) { return $entry.message }
    return $null
}

function Stat($Values) {
    $items = @($Values | Where-Object { $null -ne $_ })
    if ($items.Count -eq 0) { return $null }
    $measured = $items | Measure-Object -Average -Minimum -Maximum
    return [ordered]@{
        average = [math]::Round($measured.Average, 1)
        minimum = [math]::Round($measured.Minimum, 1)
        maximum = [math]::Round($measured.Maximum, 1)
    }
}

$hostEvents = Read-Events 'condemnedvr-host-*.log'
$bridgeEvents = Read-Events 'condemnedvr-bridge-*.log'
if ($hostEvents.Count -eq 0 -or $bridgeEvents.Count -eq 0) {
    throw "Host or bridge telemetry is missing from $($runDirectory.FullName)."
}

$windows = @(
    foreach ($entry in @($hostEvents | Where-Object {
        $_.event -eq 'perf_frame'
    })) {
        $right = [int](Field $entry.message 'render_right_avg_us')
        [pscustomobject]@{
            mode = if ($right -gt 0) { 'stereo' } else { 'flat' }
            xrFps = [double](Field $entry.message 'xr_fps')
            gameFps = [double](Field $entry.message 'game_fps')
            reused = [int](Field $entry.message 'reused')
            imageAgeAverage = [int](Field $entry.message 'image_age_avg_frames')
            imageAgeMaximum = [int](Field $entry.message 'image_age_max_frames')
            leftAverageUs = [int](Field $entry.message 'render_left_avg_us')
            leftMaximumUs = [int](Field $entry.message 'render_left_max_us')
            rightAverageUs = $right
            rightMaximumUs = [int](Field $entry.message 'render_right_max_us')
            copyAverageUs = [int](Field $entry.message 'copy_avg_us')
            copyMaximumUs = [int](Field $entry.message 'copy_max_us')
            frameCpuMaximumUs = [int](Field $entry.message 'frame_cpu_max_us')
            endFrameMaximumUs = [int](Field $entry.message 'endframe_max_us')
            longFrames = [int](Field $entry.message 'long_frames')
            poseFallbacks = [int](Field $entry.message 'pose_fallback')
            handles = [int](Field $entry.message 'handles')
        }
    }
)
$stereo = @($windows | Where-Object { $_.mode -eq 'stereo' })
$flat = @($windows | Where-Object { $_.mode -eq 'flat' })
$pipeline = $bridgeEvents | Where-Object {
    $_.event -eq 'cpu_capture_pipeline'
} | Select-Object -Last 1
$rawPresent = @($bridgeEvents | Where-Object {
    $_.event -eq 'capture_bypass_present_rate'
} | ForEach-Object { [double](Field $_.message 'fps') })

$firstTime = [datetime]$hostEvents[0].time
$lastTime = [datetime]$hostEvents[-1].time
$hostStart = First-Message $hostEvents 'host_start'
$proxyStart = First-Message $bridgeEvents 'proxy_start'
$hidState = if ($bridgeEvents.event -contains 'hid_fps_fix_applied' -or
    $bridgeEvents.event -contains 'hid_fps_fix_already_applied') {
    'active'
} elseif ($bridgeEvents.event -contains 'hid_fps_fix_disabled') {
    'disabled'
} else {
    'not-confirmed'
}
$pacingState = if ($bridgeEvents.event -contains 'xr_frame_pacing_disabled') {
    'disabled'
} elseif ($bridgeEvents.event -contains 'xr_frame_pacing_active') {
    'active'
} else {
    'not-observed'
}

$report = [ordered]@{
    run = $runDirectory.Name
    durationMinutes = [math]::Round(($lastTime - $firstTime).TotalMinutes, 1)
    executableSha256 = $cfg.CriticalFiles['Condemned.exe'].Sha256
    hostVersion = Field $hostStart 'version'
    hostGit = Field $hostStart 'git'
    bridgeVersion = Field $proxyStart 'version'
    bridgeGit = Field $proxyStart 'git'
    runtime = First-Message $hostEvents 'runtime'
    gpu = First-Message $hostEvents 'd3d11_adapter'
    hidFpsFix = $hidState
    xrFramePacing = $pacingState
    performanceWindows = $windows.Count
    stereoWindows = $stereo.Count
    flatWindows = $flat.Count
    xrFps = Stat ($windows | ForEach-Object { $_.xrFps })
    stereoGameFps = Stat ($stereo | ForEach-Object { $_.gameFps })
    flatGameFps = Stat ($flat | ForEach-Object { $_.gameFps })
    reusedPerWindow = Stat ($windows | ForEach-Object { $_.reused })
    imageAgeAverageFrames = Stat (
        $windows | ForEach-Object { $_.imageAgeAverage })
    imageAgeMaximumFrames = Stat (
        $windows | ForEach-Object { $_.imageAgeMaximum })
    leftEyeAverageUs = Stat ($stereo | ForEach-Object { $_.leftAverageUs })
    rightEyeAverageUs = Stat ($stereo | ForEach-Object { $_.rightAverageUs })
    hostCopyAverageUs = Stat ($windows | ForEach-Object { $_.copyAverageUs })
    frameCpuMaximumUs = Stat ($windows | ForEach-Object { $_.frameCpuMaximumUs })
    endFrameMaximumUs = Stat ($windows | ForEach-Object { $_.endFrameMaximumUs })
    rawPresentFps = Stat $rawPresent
    lastPipeline = if ($null -eq $pipeline) { $null } else { [ordered]@{
        queued = [int](Field $pipeline.message 'queued')
        transferred = [int](Field $pipeline.message 'transferred')
        queueDrops = [int](Field $pipeline.message 'queue_drops')
        staleDrops = [int](Field $pipeline.message 'stale_drops')
        slotDrops = [int](Field $pipeline.message 'slot_drops')
        duplicateDrops = [int](Field $pipeline.message 'duplicate_drops')
        transferMaximumUs = [int](Field $pipeline.message 'transfer_max_us')
    }}
}

if ($AsJson) {
    $report | ConvertTo-Json -Depth 6
    return
}

function Row([string]$Name, $Value) {
    if ($null -eq $Value -or "$Value" -eq '') { $Value = 'not captured' }
    Write-Host ('  {0,-28} {1}' -f $Name, $Value)
}

function Rate($Stat) {
    if ($null -eq $Stat) { return $null }
    return "avg $($Stat.average), range $($Stat.minimum)-$($Stat.maximum)"
}

Write-Host "=== Condemned VR performance: $($report.run) ===" `
    -ForegroundColor Cyan
Row 'Duration (minutes)' $report.durationMinutes
Row 'HID/FPS fix' $report.hidFpsFix
Row 'OpenXR request pacing' $report.xrFramePacing
Row 'Runtime' $report.runtime
Row 'GPU' $report.gpu
Row 'Windows (stereo/flat)' "$($report.stereoWindows)/$($report.flatWindows)"
Row 'XR FPS' (Rate $report.xrFps)
Row 'Stereo game FPS' (Rate $report.stereoGameFps)
Row 'Flat game FPS' (Rate $report.flatGameFps)
Row 'Reused / 300 XR frames' (Rate $report.reusedPerWindow)
Row 'Image age avg (frames)' (Rate $report.imageAgeAverageFrames)
Row 'Image age max (frames)' (Rate $report.imageAgeMaximumFrames)
Row 'Left eye avg (us)' (Rate $report.leftEyeAverageUs)
Row 'Right eye avg (us)' (Rate $report.rightEyeAverageUs)
Row 'Host copy avg (us)' (Rate $report.hostCopyAverageUs)
Row 'Frame CPU max (us)' (Rate $report.frameCpuMaximumUs)
Row 'EndFrame max (us)' (Rate $report.endFrameMaximumUs)
Row 'Raw Present FPS' (Rate $report.rawPresentFps)
if ($null -ne $report.lastPipeline) {
    Row 'Pipeline queued/transferred' (
        "$($report.lastPipeline.queued)/$($report.lastPipeline.transferred)")
    Row 'Pipeline drops q/s/slot/dup' (
        "$($report.lastPipeline.queueDrops)/" +
        "$($report.lastPipeline.staleDrops)/" +
        "$($report.lastPipeline.slotDrops)/" +
        "$($report.lastPipeline.duplicateDrops)")
    Row 'Transfer max (us)' $report.lastPipeline.transferMaximumUs
}
