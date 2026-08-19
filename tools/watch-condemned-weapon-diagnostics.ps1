<#
.SYNOPSIS
    Builds a compact live weapon-test status file from the loader event stream.

.DESCRIPTION
    WeaponTest writes detailed reverse-engineering events to the loader log.
    This watcher converts the relevant events into a stable JSON snapshot and
    a concise JSONL timeline that Codex or a developer can poll while the user
    remains in headset.

.PARAMETER Run
    Run-directory name or full path. Defaults to the newest staged run.

.PARAMETER GameProcessId
    Optional Condemned PID. When supplied, the watcher finalizes after exit.
#>
[CmdletBinding()]
param(
    [string]$Run,
    [int]$GameProcessId = 0,
    [switch]$Once,
    [ValidateRange(100, 5000)]
    [int]$RefreshMilliseconds = 250
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$logRoot = Join-Path $cfg.ProjectRoot 'stage\condemned-m2-mono\logs'

if ([string]::IsNullOrWhiteSpace($Run)) {
    $runDirectory = Get-ChildItem -LiteralPath $logRoot -Directory |
        Where-Object {
            Test-Path (Join-Path $_.FullName 'm2-mono-live.json') -PathType Leaf
        } |
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

$liveReportPath = Join-Path $runDirectory.FullName 'm2-mono-live.json'
$deadline = (Get-Date).AddSeconds(45)
while (-not (Test-Path -LiteralPath $liveReportPath -PathType Leaf) -and
       (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
}
if (-not (Test-Path -LiteralPath $liveReportPath -PathType Leaf)) {
    throw "Live report did not appear: $liveReportPath"
}
$liveReport = Get-Content -Raw -LiteralPath $liveReportPath | ConvertFrom-Json
if ($GameProcessId -le 0 -and $null -ne $liveReport.GameProcessId) {
    $GameProcessId = [int]$liveReport.GameProcessId
}
$loaderLog = [IO.Path]::GetFullPath([string]$liveReport.LoaderLog)
if (-not (Test-Path -LiteralPath $loaderLog -PathType Leaf)) {
    throw "Loader log does not exist: $loaderLog"
}

$alignmentCommandPath = [string]$liveReport.WeaponAlignmentCommand
$statusPath = Join-Path $runDirectory.FullName 'weapon-diagnostics-live.json'
$eventPath = Join-Path $runDirectory.FullName 'weapon-diagnostics-events.jsonl'
[IO.File]::WriteAllText(
    $eventPath, '', (New-Object Text.UTF8Encoding($false)))

function Field([string]$Text, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $match = [Text.RegularExpressions.Regex]::Match(
        $Text,
        [Text.RegularExpressions.Regex]::Escape($Key) + '=([^\s]+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

function Number([string]$Value, [double]$Fallback = 0.0) {
    $parsed = 0.0
    if ([double]::TryParse(
            $Value,
            [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$parsed)) {
        return $parsed
    }
    return $Fallback
}

function Integer([string]$Value, [long]$Fallback = 0) {
    $parsed = [long]0
    if ([long]::TryParse(
            $Value,
            [Globalization.NumberStyles]::Integer,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$parsed)) {
        return $parsed
    }
    return $Fallback
}

$offset = [long]0
$pending = ''
function Read-NewLoaderLines {
    $stream = New-Object IO.FileStream(
        $loaderLog,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -lt $script:offset) {
            $script:offset = 0
            $script:pending = ''
        }
        [void]$stream.Seek($script:offset, [IO.SeekOrigin]::Begin)
        $reader = New-Object IO.StreamReader(
            $stream, [Text.Encoding]::UTF8, $true, 4096, $true)
        try {
            $text = $reader.ReadToEnd()
            $script:offset = $stream.Position
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
    $combined = $script:pending + $text
    if ([string]::IsNullOrEmpty($combined)) { return @() }
    $parts = @([Text.RegularExpressions.Regex]::Split($combined, '\r?\n'))
    if (-not $combined.EndsWith("`n")) {
        $script:pending = $parts[-1]
        if ($parts.Count -eq 1) { return @() }
        $parts = @($parts[0..($parts.Count - 2)])
    } else {
        $script:pending = ''
    }
    return @($parts | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

$startedAtUtc = [DateTime]::UtcNow
$profile = ''
$weaponIndex = -1
$colliderSeeded = $false
$collisionObject = ''
$colliderStateEventCount = 0
$colliderStateRecordedCount = 0
$colliderStateSuppressedCount = 0
$lastColliderTimelineSeeded = $null
$seenColliderTimelineObjects = @{}
$automaticSeedArmed = $false
$automaticSeedCandidate = $false
$automaticSeedPhase = 'inactive'
$automaticSeedAttempts = 0
$automaticSeedStartedCount = 0
$automaticSeedConfirmedCount = 0
$automaticSeedReadyCount = 0
$automaticSeedFailureCount = 0
$automaticSeedImpactBlockedCount = 0
$automaticSeedLastSource = ''
$callbackCount = 0
$acceptedCount = 0
$forwardedCount = 0
$duplicateCount = 0
$actorContactCount = 0
$worldContactCount = 0
$targetReferenceClearCount = 0
$targetReferenceElementClearCount = 0
$targetReferenceFailureCount = 0
$rearmCount = 0
$sameTargetBeforeRearmCount = 0
$multiTargetSwingCount = 0
$invalidSampleHoldCount = 0
$trackingLossCount = 0
$damageDispatchCount = 0
$acceptedActorHitCount = 0
$acceptedWorldHitCount = 0
$attackTelegraphTriggerCount = 0
$acceptedWithAttackTelegraphCount = 0
$acceptedWithoutAttackTelegraphCount = 0
$acceptedAttackTelegraphUnknownCount = 0
$retailAttackCommandDownCount = 0
$retailAttackCommandUpCount = 0
$minimumActorHeadHorizontalDistanceMeters = $null
$enemyHealthObserved = $false
$playerVitals = $null
$playerHealthDecreaseCount = 0
$blockCommandActive = $false
$blockCommandDownCount = 0
$blockCommandUpCount = 0
$blockedHealthDecreaseCount = 0
$unblockedHealthDecreaseCount = 0
$blockCommandDownRuntimeTick = 0
$lastBlockHoldDurationMilliseconds = $null
$maximumBlockHoldDurationMilliseconds = $null
$blockPoseConfigured = $false
$blockPoseActive = $false
$blockPoseActivationCount = 0
$blockPoseExitCount = 0
$blockPosePositionErrorMeters = $null
$blockPoseAngleErrorDegrees = $null
$blockPosePositionToleranceMeters = $null
$blockPoseAngleToleranceDegrees = $null
$blockPoseLastReason = ''
$blockPoseTrackingFresh = $false

$lastContact = $null
$timeline = New-Object Collections.Generic.List[object]
$targets = @{}
$swingTargets = @{}
$warnings = @{}
$liveAlignment = $null
$liveAlignmentAppliedCount = 0
$liveAlignmentRejectedCount = 0
$lastLiveAlignmentError = $null

function Add-Warning([string]$Code, [string]$Message) {
    $warnings[$Code] = $Message
}

function Add-Timeline([object]$Item) {
    $timeline.Add($Item)
    while ($timeline.Count -gt 24) { $timeline.RemoveAt(0) }
    [IO.File]::AppendAllText(
        $eventPath,
        ($Item | ConvertTo-Json -Compress -Depth 6) + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false)))
}


function New-LiveAlignmentSnapshot(
    [string]$Detail,
    [string]$Source,
    [string]$ObservedAtUtc) {
    return [pscustomobject][ordered]@{
        Available = $true
        Revision = [long](Integer (Field $Detail 'revision'))
        Source = $Source
        ObservedAtUtc = $ObservedAtUtc
        ProcessId = [int](Integer (Field $Detail 'process_id'))
        WeaponIndex = [int](Integer (Field $Detail 'weapon_index') -1)
        PositionUnits = [pscustomobject][ordered]@{
            X = Number (Field $Detail 'position_x')
            Y = Number (Field $Detail 'position_y')
            Z = Number (Field $Detail 'position_z')
        }
        RotationDegrees = [pscustomobject][ordered]@{
            X = Number (Field $Detail 'rotation_x')
            Y = Number (Field $Detail 'rotation_y')
            Z = Number (Field $Detail 'rotation_z')
        }
        LengthUnits = Number (Field $Detail 'length')
        RadiusUnits = Number (Field $Detail 'radius')
        Reversed = (Field $Detail 'reversed') -eq '1'
        Result = [string](Field $Detail 'result')
    }
}
function Observe-Entry([object]$Entry) {
    $observedAt = [DateTime]::UtcNow.ToString('o')
    $detail = [string]$Entry.detail
    switch ([string]$Entry.event) {
        'm5_physical_melee_profile_selected' {
            $script:profile = [string](Field $detail 'profile')
            $script:weaponIndex = [int](Integer (Field $detail 'weapon_index') -1)
            $script:swingTargets = @{}
            $script:lastColliderTimelineSeeded = $null
            $script:seenColliderTimelineObjects = @{}
            $script:automaticSeedCandidate = $false
            $script:automaticSeedPhase = 'inactive'
            $script:automaticSeedAttempts = 0
            $script:automaticSeedLastSource = ''
        }
        'm5_physical_melee_auto_seed_armed' {
            $script:automaticSeedArmed = $true
        }
        'm5_physical_melee_auto_seed_candidate' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = [string](Field $detail 'phase')
            if ([string]::IsNullOrWhiteSpace(
                    $script:automaticSeedPhase)) {
                $script:automaticSeedPhase = 'stabilizing'
            }
            $script:automaticSeedAttempts = [int](Integer (
                Field $detail 'attempts'))
            $script:automaticSeedLastSource = ''
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_candidate'
                WeaponIndex = [int](Integer (
                    Field $detail 'weapon_index') -1)
                Phase = $script:automaticSeedPhase
            })
        }
        'm5_physical_melee_auto_seed_reset' {
            $script:automaticSeedCandidate = $false
            $script:automaticSeedPhase = 'inactive'
            $script:automaticSeedAttempts = 0
            $script:automaticSeedLastSource = ''
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_reset'
            })
        }
        'm5_physical_melee_auto_seed_started' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = 'pulse'
            $script:automaticSeedAttempts = [int](Integer (
                Field $detail 'attempt'))
            $script:automaticSeedStartedCount++
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_started'
                Attempt = $script:automaticSeedAttempts
                DamageBlocked = $true
                HapticBlocked = $true
            })
        }
        'm5_physical_melee_auto_seed_verification_pending' {
            $script:automaticSeedPhase = 'awaiting_confirmation'
        }
        'm5_physical_melee_auto_seed_retry' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = 'retry_wait'
            $script:automaticSeedAttempts = [int](Integer (
                Field $detail 'attempt'))
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_retry'
                Attempt = $script:automaticSeedAttempts
                Reason = [string](Field $detail 'reason')
            })
        }
        'm5_physical_melee_auto_seed_failed' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = 'failed'
            $script:automaticSeedAttempts = [int](Integer (
                Field $detail 'attempt'))
            $script:automaticSeedFailureCount++
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_failed'
                Attempt = $script:automaticSeedAttempts
                Reason = [string](Field $detail 'reason')
            })
        }
        'm5_physical_melee_auto_seed_body_lost' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = [string](Field $detail 'phase')
            if ([string]::IsNullOrWhiteSpace(
                    $script:automaticSeedPhase)) {
                $script:automaticSeedPhase = 'stabilizing'
            }
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_body_lost'
                Phase = $script:automaticSeedPhase
            })
        }
        'm5_physical_melee_auto_seed_confirmed' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = [string](Field $detail 'phase')
            $script:automaticSeedAttempts = [int](Integer (
                Field $detail 'attempt'))
            $script:automaticSeedConfirmedCount++
            $script:automaticSeedLastSource = [string](Field $detail 'source')
            $confirmedObject = [string](Field $detail 'collision_object')
            if (-not [string]::IsNullOrWhiteSpace($confirmedObject) -and
                $confirmedObject -ne '0x00000000') {
                $script:colliderSeeded = $true
                $script:collisionObject = $confirmedObject
            }
            if ((Field $detail 'ready_immediately') -eq '1') {
                $script:automaticSeedPhase = 'ready'
                $script:automaticSeedReadyCount++
            }
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_confirmed'
                Attempt = $script:automaticSeedAttempts
                Source = $script:automaticSeedLastSource
                CollisionObject = $confirmedObject
                ReadyImmediately =
                    (Field $detail 'ready_immediately') -eq '1'
            })
        }
        'm5_physical_melee_auto_seed_ready' {
            $script:automaticSeedCandidate = $true
            $script:automaticSeedPhase = 'ready'
            $script:automaticSeedAttempts = [int](Integer (
                Field $detail 'attempt'))
            $script:automaticSeedReadyCount++
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_seed_ready'
                Attempt = $script:automaticSeedAttempts
            })
        }
        'm5_physical_melee_auto_seed_impact_blocked' {
            $reportedBlockedCount = [int](Integer (
                Field $detail 'count'))
            if ($reportedBlockedCount -gt
                    $script:automaticSeedImpactBlockedCount) {
                $script:automaticSeedImpactBlockedCount =
                    $reportedBlockedCount
            } else {
                $script:automaticSeedImpactBlockedCount++
            }
        }
        'm5_aim_path_melee_collision_update' {
            $object = [string](Field $detail 'collision_object')
            if ((Field $detail 'player_owned') -eq '1' -and
                -not [string]::IsNullOrWhiteSpace($object) -and
                $object -ne '0x00000000') {
                $script:colliderSeeded = $true
                $script:collisionObject = $object
            }
        }
        'm5_live_collider_alignment_armed' {
            $script:liveAlignment = New-LiveAlignmentSnapshot (
                $detail) 'ready' $observedAt
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'live_alignment_ready'
                Revision = $script:liveAlignment.Revision
                WeaponIndex = $script:liveAlignment.WeaponIndex
            })
        }
        'm5_live_collider_alignment_applied' {
            $script:liveAlignment = New-LiveAlignmentSnapshot (
                $detail) 'external_command' $observedAt
            $script:liveAlignmentAppliedCount++
            $script:lastLiveAlignmentError = $null
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'live_alignment_applied'
                Revision = $script:liveAlignment.Revision
                WeaponIndex = $script:liveAlignment.WeaponIndex
                PositionUnits = $script:liveAlignment.PositionUnits
                RotationDegrees = $script:liveAlignment.RotationDegrees
                LengthUnits = $script:liveAlignment.LengthUnits
                RadiusUnits = $script:liveAlignment.RadiusUnits
                Reversed = $script:liveAlignment.Reversed
            })
        }
        'm5_live_collider_alignment_rejected' {
            $script:liveAlignmentRejectedCount++
            $script:lastLiveAlignmentError =
                [pscustomobject][ordered]@{
                    ObservedAtUtc = $observedAt
                    Revision = [long](Integer (Field $detail 'revision'))
                    Reason = [string](Field $detail 'reason')
                    TargetProcessId = [int](Integer (
                        Field $detail 'target_process_id'))
                    TargetWeaponIndex = [int](Integer (
                        Field $detail 'target_weapon_index') -1)
                    ActiveWeaponIndex = [int](Integer (
                        Field $detail 'active_weapon_index') -1)
                }
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'live_alignment_rejected'
                Revision = $script:lastLiveAlignmentError.Revision
                Reason = $script:lastLiveAlignmentError.Reason
            })
        }
        'm5_collider_settings_saved' {
            if ($null -ne (Field $detail 'position_x')) {
                $savedAlignment = New-LiveAlignmentSnapshot (
                    $detail) 'tool_menu_or_command' $observedAt
                $savedAlignment.ProcessId = $GameProcessId
                if ($null -ne $script:liveAlignment) {
                    $savedAlignment.Revision =
                        [long]$script:liveAlignment.Revision
                }
                $script:liveAlignment = $savedAlignment
            }
        }
        'm5_weapon_test_collider_state' {
            $script:colliderSeeded = (Field $detail 'seeded') -eq '1'
            $script:collisionObject = [string](Field $detail 'collision_object')
            $script:colliderStateEventCount++

            $stateChanged =
                $null -eq $script:lastColliderTimelineSeeded -or
                $script:lastColliderTimelineSeeded -ne $script:colliderSeeded
            $newObject = $script:colliderSeeded -and
                -not [string]::IsNullOrWhiteSpace($script:collisionObject) -and
                -not $script:seenColliderTimelineObjects.ContainsKey(
                    $script:collisionObject)
            if ($newObject) {
                $script:seenColliderTimelineObjects[
                    $script:collisionObject] = $true
            }

            if ($stateChanged -or $newObject) {
                $script:colliderStateRecordedCount++
                Add-Timeline ([pscustomobject][ordered]@{
                    ObservedAtUtc = $observedAt
                    Event = 'collider_state'
                    Seeded = $script:colliderSeeded
                    CollisionObject = $script:collisionObject
                })
            } else {
                $script:colliderStateSuppressedCount++
            }
            $script:lastColliderTimelineSeeded = $script:colliderSeeded
        }
        'm5_aim_path_command_edge' {
            $command = Integer (Field $detail 'command')
            $runtimeTick = Integer (Field $detail 'runtime_tick_ms')
            $edgeDown = (Field $detail 'edge') -eq 'down'
            if ($command -eq 17) {
                if ($edgeDown) {
                    $script:retailAttackCommandDownCount++
                } else {
                    $script:retailAttackCommandUpCount++
                }
                Add-Timeline ([pscustomobject][ordered]@{
                    ObservedAtUtc = $observedAt
                    RuntimeTickMilliseconds = $runtimeTick
                    Event = 'retail_attack_command_edge'
                    Active = $edgeDown
                    ControllerApplied =
                        (Field $detail 'controller_applied') -eq '1'
                    OutputValue = Number (Field $detail 'output_value')
                })
            } elseif ($command -eq 28) {
                $script:blockCommandActive =
                    $edgeDown
                if ($script:blockCommandActive) {
                    $script:blockCommandDownCount++
                    $script:blockCommandDownRuntimeTick = $runtimeTick
                } else {
                    $script:blockCommandUpCount++
                    if ($runtimeTick -gt 0 -and
                        $script:blockCommandDownRuntimeTick -gt 0 -and
                        $runtimeTick -ge $script:blockCommandDownRuntimeTick) {
                        $duration =
                            $runtimeTick - $script:blockCommandDownRuntimeTick
                        $script:lastBlockHoldDurationMilliseconds = $duration
                        if ($null -eq
                                $script:maximumBlockHoldDurationMilliseconds -or
                            $duration -gt
                                $script:maximumBlockHoldDurationMilliseconds) {
                            $script:maximumBlockHoldDurationMilliseconds =
                                $duration
                        }
                    }
                    $script:blockCommandDownRuntimeTick = 0
                }
                Add-Timeline ([pscustomobject][ordered]@{
                    ObservedAtUtc = $observedAt
                    RuntimeTickMilliseconds = $runtimeTick
                    Event = 'block_command_edge'
                    Active = $script:blockCommandActive
                    ControllerApplied =
                        (Field $detail 'controller_applied') -eq '1'
                    OutputValue = Number (Field $detail 'output_value')
                })
            }
        }

        'm5_physical_melee_swing_attack_triggered' {
            $script:attackTelegraphTriggerCount++
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'attack_telegraph_triggered'
                TriggerCount = $script:attackTelegraphTriggerCount
                SpeedMetersPerSecond = Number (Field $detail 'speed_mps')
                ThresholdMetersPerSecond = Number (
                    Field $detail 'threshold_mps')
                PulseMilliseconds = [int](Integer (Field $detail 'pulse_ms'))
                CooldownMilliseconds = [int](Integer (
                    Field $detail 'cooldown_ms'))
            })
        }
        'm5_physical_melee_block_pose_state' {
            $script:blockPoseActive =
                (Field $detail 'active') -eq '1'
            $entered = (Field $detail 'entered') -eq '1'
            $exited = (Field $detail 'exited') -eq '1'
            if ($entered) {
                $script:blockPoseActivationCount++
            }
            if ($exited) {
                $script:blockPoseExitCount++
            }
            $script:blockPoseConfigured = $true
            $script:blockPoseTrackingFresh =
                (Field $detail 'tracking_fresh') -eq '1'
            $script:blockPosePositionErrorMeters = Number (
                Field $detail 'position_error_m')
            $script:blockPoseAngleErrorDegrees = Number (
                Field $detail 'angle_error_deg')
            $script:blockPosePositionToleranceMeters = Number (
                Field $detail 'position_tolerance_m')
            $script:blockPoseAngleToleranceDegrees = Number (
                Field $detail 'angle_tolerance_deg')
            $script:blockPoseLastReason = [string](
                Field $detail 'reason')
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'automatic_block_pose_state'
                WeaponIndex = [int](Integer (
                    Field $detail 'weapon_index') -1)
                Active = $script:blockPoseActive
                Entered = $entered
                Exited = $exited
                TrackingFresh = $script:blockPoseTrackingFresh
                PositionErrorMeters =
                    $script:blockPosePositionErrorMeters
                AngleErrorDegrees =
                    $script:blockPoseAngleErrorDegrees
                Reason = $script:blockPoseLastReason
            })
        }
        'm5_combat_player_vitals' {
            $delta = [long](Integer (Field $detail 'delta'))
            if ($delta -lt 0) {
                $script:playerHealthDecreaseCount++
                if ($script:blockCommandActive) {
                    $script:blockedHealthDecreaseCount++
                } else {
                    $script:unblockedHealthDecreaseCount++
                }
            }
            $script:playerVitals = [pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                RuntimeTickMilliseconds = [long](Integer (
                    Field $detail 'runtime_tick_ms'))
                Current = [long](Integer (Field $detail 'current'))
                Maximum = [long](Integer (Field $detail 'maximum'))
                Fraction = Number (Field $detail 'fraction')
                Delta = $delta
                BlockCommandActive = $script:blockCommandActive
                Initial = (Field $detail 'initial') -eq '1'
                Cause = [string](Field $detail 'cause')
                ReadOnly = $true
            }
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'player_vitals'
                Current = $script:playerVitals.Current
                Maximum = $script:playerVitals.Maximum
                Delta = $script:playerVitals.Delta
                BlockCommandActive = $script:playerVitals.BlockCommandActive
            })
        }

        'm5_weapon_test_contact' {
            $script:callbackCount++
            $accepted = (Field $detail 'accepted') -eq '1'
            $forwarded = (Field $detail 'native_forwarded') -eq '1'
            $clearValue = [string](Field $detail 'retail_ref_vector_clear_ok')
            if ([string]::IsNullOrWhiteSpace($clearValue)) {
                $clearValue = [string](Field $detail 'retail_target_ref_clear_ok')
            }
            $clearOk = $clearValue -eq '1'
            $clearState = [string](Field $detail 'retail_ref_vector_state')
            if ([string]::IsNullOrWhiteSpace($clearState)) {
                $clearState = [string](Field $detail 'retail_target_ref_state')
            }
            $vectorBegin = [string](Field $detail 'retail_ref_vector_begin')
            $vectorEndBefore = [string](
                Field $detail 'retail_ref_vector_end_before')
            $vectorEndAfter = [string](
                Field $detail 'retail_ref_vector_end_after')
            $vectorCapacity = [string](
                Field $detail 'retail_ref_vector_capacity')
            $referencesBefore = [int](Integer (
                Field $detail 'retail_refs_before'))
            $referencesCleared = [int](Integer (
                Field $detail 'retail_refs_cleared'))
            $contactDamageActive = (Field $detail 'contact_damage_active') -eq '1'
            $reason = [string](Field $detail 'reason')
            $kind = [string](Field $detail 'target_kind')
            $target = [string](Field $detail 'target')
            $runtimeTick = [long](Integer (Field $detail 'runtime_tick_ms'))
            $headPoseValid = (Field $detail 'head_pose_valid') -eq '1'
            $headToContact = Number (
                Field $detail 'head_to_target_contact_m')
            $headHorizontalToContact = Number (
                Field $detail 'head_horizontal_to_target_contact_m')
            $gripPoseValid = (Field $detail 'grip_pose_valid') -eq '1'
            $gripToContact = Number (
                Field $detail 'grip_to_target_contact_m')
            $telegraphTriggeredValue = [string](Field $detail (
                'attack_telegraph_triggered_this_swing'))
            $attackTelegraphObserved = -not [string]::IsNullOrWhiteSpace(
                $telegraphTriggeredValue)
            $attackTelegraphEnabled =
                (Field $detail 'attack_telegraph_enabled') -eq '1'
            $attackTelegraphTriggered = $telegraphTriggeredValue -eq '1'
            $attackTelegraphPulseActive =
                (Field $detail 'attack_telegraph_pulse_active') -eq '1'
            if ((Field $detail 'enemy_health_observed') -eq '1') {
                $script:enemyHealthObserved = $true
            }
            $contactPositionValid =
                (Field $detail 'contact_position_valid') -eq '1'
            $distanceValid =
                (Field $detail 'contact_distance_valid') -eq '1'
            $tipToContact = Number (
                Field $detail 'weapon_tip_to_target_contact_m')
            $axisToContact = Number (
                Field $detail 'weapon_axis_to_target_contact_m')
            $capsuleGap = Number (
                Field $detail 'weapon_capsule_to_target_gap_m')
            $capsuleRadius = Number (
                Field $detail 'weapon_capsule_radius_m')
            $contactAxis = Number (Field $detail 'contact_axis_t')
            $distanceAssessment = if (-not $distanceValid) {
                'unavailable'
            } elseif ($capsuleGap -le 0.0) {
                'touching_or_inside'
            } elseif ($capsuleGap -le 0.02) {
                'within_2cm'
            } else {
                'outside_configured_capsule'
            }
            $distance = [pscustomobject][ordered]@{
                ContactPositionValid = $contactPositionValid
                Valid = $distanceValid
                Assessment = $distanceAssessment
                TipToTargetContactMeters = $tipToContact
                AxisToTargetContactMeters = $axisToContact
                CapsuleToTargetGapMeters = $capsuleGap
                CapsuleRadiusMeters = $capsuleRadius
                ContactAxisFraction = $contactAxis
                HeadPoseValid = $headPoseValid
                HeadToTargetContactMeters = $headToContact
                HeadHorizontalToTargetContactMeters =
                    $headHorizontalToContact
                GripPoseValid = $gripPoseValid
                GripToTargetContactMeters = $gripToContact
            }
            if ($accepted) { $script:acceptedCount++ }
            if ($forwarded) { $script:forwardedCount++ }
            if ($reason -eq 'contact_latched') { $script:duplicateCount++ }
            if ($kind -eq 'actor_candidate') {
                $script:actorContactCount++
            } else {
                $script:worldContactCount++
            }
            if ($kind -eq 'actor_candidate' -and $headPoseValid -and
                ($null -eq $script:minimumActorHeadHorizontalDistanceMeters -or
                 $headHorizontalToContact -lt
                    $script:minimumActorHeadHorizontalDistanceMeters)) {
                $script:minimumActorHeadHorizontalDistanceMeters =
                    $headHorizontalToContact
            }
            if ($accepted) {
                if ($kind -eq 'actor_candidate') {
                    $script:acceptedActorHitCount++
                } else {
                    $script:acceptedWorldHitCount++
                }
                if (-not $attackTelegraphObserved) {
                    $script:acceptedAttackTelegraphUnknownCount++
                } elseif ($attackTelegraphTriggered) {
                    $script:acceptedWithAttackTelegraphCount++
                } else {
                    $script:acceptedWithoutAttackTelegraphCount++
                }
            }
            if ($clearOk) {
                $script:targetReferenceClearCount++
                $script:targetReferenceElementClearCount +=
                    $referencesCleared
            } elseif ($contactDamageActive) {
                $script:targetReferenceFailureCount++
                Add-Warning 'retail_reference_vector' (
                    'Retail reference-vector cleanup failed; repeated contacts may stop.')
            }
            $script:damageDispatchCount = [int](Integer (
                Field $detail 'damage_dispatch_count') $script:damageDispatchCount)
            if ($accepted) {
                if ($script:swingTargets.ContainsKey($target)) {
                    $script:sameTargetBeforeRearmCount++
                    Add-Warning 'same_target_before_rearm' (
                        'A target was accepted twice before a completed swing reset.')
                } else {
                    $script:swingTargets[$target] = $true
                }
                if (-not $targets.ContainsKey($target)) {
                    $targets[$target] = [pscustomobject][ordered]@{
                        Target = $target
                        Kind = $kind
                        AcceptedContacts = 0
                        AcceptedWithAttackTelegraph = 0
                        AcceptedWithoutAttackTelegraph = 0
                        AcceptedAttackTelegraphUnknown = 0
                        FirstAcceptedRuntimeTickMilliseconds = 0
                        LastAcceptedRuntimeTickMilliseconds = 0
                        LastAcceptedIntervalMilliseconds = $null
                        LastObservedAtUtc = $observedAt
                    }
                }
                $targets[$target].AcceptedContacts++
                $targetState = $targets[$target]
                if ($runtimeTick -gt 0) {
                    if ($targetState.FirstAcceptedRuntimeTickMilliseconds -eq 0) {
                        $targetState.FirstAcceptedRuntimeTickMilliseconds =
                            $runtimeTick
                    }
                    if ($targetState.LastAcceptedRuntimeTickMilliseconds -gt 0 -and
                        $runtimeTick -ge
                            $targetState.LastAcceptedRuntimeTickMilliseconds) {
                        $targetState.LastAcceptedIntervalMilliseconds =
                            $runtimeTick -
                            $targetState.LastAcceptedRuntimeTickMilliseconds
                    }
                    $targetState.LastAcceptedRuntimeTickMilliseconds = $runtimeTick
                }
                if (-not $attackTelegraphObserved) {
                    $targetState.AcceptedAttackTelegraphUnknown++
                } elseif ($attackTelegraphTriggered) {
                    $targetState.AcceptedWithAttackTelegraph++
                } else {
                    $targetState.AcceptedWithoutAttackTelegraph++
                }
                $targets[$target].LastObservedAtUtc = $observedAt
            }
            $script:lastContact = [pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Target = $target
                TargetNode = [string](Field $detail 'target_node')
                Kind = $kind
                Accepted = $accepted
                Reason = $reason
                NativeForwarded = $forwarded
                RuntimeTickMilliseconds = $runtimeTick
                AttackTelegraph = [pscustomobject][ordered]@{
                    Observed = $attackTelegraphObserved
                    Enabled = $attackTelegraphEnabled
                    TriggeredThisSwing = $attackTelegraphTriggered
                    PulseActive = $attackTelegraphPulseActive
                }
                EnemyHealthObserved =
                    (Field $detail 'enemy_health_observed') -eq '1'
                RetailReferenceVectorClearOk = $clearOk
                RetailReferenceVectorState = $clearState
                RetailReferenceVector = [pscustomobject][ordered]@{
                    Begin = $vectorBegin
                    EndBefore = $vectorEndBefore
                    EndAfter = $vectorEndAfter
                    Capacity = $vectorCapacity
                    ReferencesBefore = $referencesBefore
                    ReferencesCleared = $referencesCleared
                    TargetSlot = [int](Integer (
                        Field $detail 'target_slot') -1)
                }
                Distance = $distance
                PassTargetCount = [int](Integer (
                    Field $detail 'pass_target_count'))
                SpeedMetersPerSecond = Number (Field $detail 'speed_mps')
                EnergyJoules = Number (Field $detail 'energy_j')
            }
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'contact'
                Target = $target
                Kind = $kind
                Accepted = $accepted
                Reason = $reason
                Forwarded = $forwarded
                RetailReferenceVectorClearOk = $clearOk
                RetailReferenceVectorState = $clearState
                WeaponCapsuleToTargetGapMeters = $capsuleGap
                DistanceAssessment = $distanceAssessment
                RuntimeTickMilliseconds = $runtimeTick
                HeadPoseValid = $headPoseValid
                HeadHorizontalToTargetContactMeters =
                    $headHorizontalToContact
                AttackTelegraphObserved = $attackTelegraphObserved
                AttackTelegraphTriggeredThisSwing =
                    $attackTelegraphTriggered
            })
        }
        'm5_physical_melee_contact_rearmed' {
            $script:rearmCount++
            if ($script:swingTargets.Count -gt 1) {
                $script:multiTargetSwingCount++
            }
            $script:swingTargets = @{}
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'contact_rearmed'
                RearmCount = $script:rearmCount
                Reason = Field $detail 'reason'
                TipDisplacementMeters = Number (
                    Field $detail 'tip_displacement_m')
                MaximumTipDisplacementMeters = Number (
                    Field $detail 'max_tip_displacement_m')
                RequiredTipTravelMeters = Number (
                    Field $detail 'required_tip_travel_m')
                SpeedMetersPerSecond = Number (Field $detail 'speed_mps')
                ReleaseSpeedMetersPerSecond = Number (
                    Field $detail 'release_speed_mps')
                ReleaseSamples = Field $detail 'release_samples'
            })
        }
        'm5_physical_melee_contact_latch_held' {
            $script:invalidSampleHoldCount++
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'contact_latch_held'
                HoldCount = $script:invalidSampleHoldCount
                Reason = Field $detail 'reason'
                ResetReason = Field $detail 'reset'
                ReleaseDwellReset =
                    (Field $detail 'release_dwell_reset') -eq '1'
            })
        }
        'm5_physical_melee_retail_latch_release_failed' {
            Add-Warning 'retail_reference_vector' (
                'Retail reference-vector cleanup failed; repeated contacts may stop.')
        }
        'm5_physical_melee_tracking_lost' {
            $script:trackingLossCount++
            $script:swingTargets = @{}
            Add-Timeline ([pscustomobject][ordered]@{
                ObservedAtUtc = $observedAt
                Event = 'tracking_lost'
                Count = $script:trackingLossCount
            })
        }
    }
}

function Write-Snapshot([bool]$GameRunning) {
    $oneHandedProfiles = @(
        'pipe', 'crowbar', 'plank', 'one_handed_debris')
    $oneHandedEquipped = $oneHandedProfiles -contains $script:profile
    $phase = 'equip_one_handed_melee'
    $recommendation = 'Equip any mapped one-handed melee weapon.'
    if ($oneHandedEquipped -and
        $script:automaticSeedPhase -eq 'settling') {
        $phase = 'automatic_seed_settling'
        $recommendation = (
            'The native collider is verified; wait for automatic seed ' +
            'damage suppression to settle before striking.')
    } elseif ($oneHandedEquipped -and -not $script:colliderSeeded -and
        $script:automaticSeedPhase -eq 'failed') {
        $phase = 'automatic_seed_failed'
        $recommendation = (
            'Automatic collider verification exhausted its retries; ' +
            'one manual Retail attack remains the fallback.')
    } elseif ($oneHandedEquipped -and -not $script:colliderSeeded -and
              ($script:automaticSeedArmed -or
               $script:automaticSeedCandidate)) {
        $phase = 'awaiting_automatic_seed'
        $recommendation = (
            'Keep the picked-up weapon and tracked controller stable while ' +
            'the automatic collider seed is verified.')
    } elseif ($oneHandedEquipped -and -not $script:colliderSeeded) {
        $phase = 'awaiting_seed'
        $recommendation = (
            'This run has no automatic-seed telemetry; one manual Retail ' +
            'attack creates the reusable collider.')
    } elseif ($script:targetReferenceFailureCount -gt 0 -or
              $warnings.ContainsKey('retail_reference_vector')) {
        $phase = 'retail_reference_vector_failure'
        $recommendation = 'Inspect LastContact.RetailReferenceVector.'
    } elseif ($script:sameTargetBeforeRearmCount -gt 0) {
        $phase = 'same_target_reaccepted_before_rearm'
        $recommendation = 'Inspect the swing timeline and latch-reset events.'
    } elseif ($script:colliderSeeded -and $script:callbackCount -eq 0) {
        $phase = 'ready_waiting_for_contact'
        $recommendation = 'Move the green collider through an enemy.'
    } elseif ($script:acceptedCount -eq 1) {
        $phase = 'first_contact_observed'
        $recommendation = 'Finish the swing, slow below reset speed, then strike again.'
    } elseif ($script:acceptedCount -gt 1 -and $script:rearmCount -gt 0) {
        $phase = 'repeated_contact_observed'
        $recommendation = 'Repeated-contact lifecycle is producing accepted dispatches.'
    } elseif ($script:callbackCount -gt 0 -and $script:acceptedCount -eq 0) {
        $phase = 'contacts_rejected'
        $recommendation = 'Inspect LastContact.Reason and proxy/contact state.'
    }

    $targetSnapshot = New-Object Collections.Generic.List[object]
    foreach ($targetValue in $targets.Values) {
        $targetSnapshot.Add($targetValue)
    }
    $warningSnapshot = New-Object Collections.Generic.List[object]
    foreach ($warningValue in $warnings.GetEnumerator()) {
        $warningSnapshot.Add([pscustomobject][ordered]@{
            Code = $warningValue.Key
            Message = $warningValue.Value
        })
    }
    $snapshot = [pscustomobject][ordered]@{
        SchemaVersion = 4
        UpdatedAtUtc = [DateTime]::UtcNow.ToString('o')
        StartedAtUtc = $startedAtUtc.ToString('o')
        Run = $runDirectory.Name
        GameProcessId = $GameProcessId
        GameRunning = $GameRunning
        Phase = $phase
        Recommendation = $recommendation
        Healthy = $warnings.Count -eq 0
        Profile = [pscustomobject][ordered]@{
            Name = $script:profile
            WeaponIndex = $script:weaponIndex
        }
        Collider = [pscustomobject][ordered]@{
            Seeded = $script:colliderSeeded
            CollisionObject = $script:collisionObject
            StateEventsObserved = $script:colliderStateEventCount
            StateEventsRecorded = $script:colliderStateRecordedCount
            StateEventsSuppressed = $script:colliderStateSuppressedCount
            AlignmentCommandPath = $alignmentCommandPath
            LiveAlignment = $script:liveAlignment
            LastAlignmentError = $script:lastLiveAlignmentError
            AutomaticSeed = [pscustomobject][ordered]@{
                Armed = $script:automaticSeedArmed
                Candidate = $script:automaticSeedCandidate
                Phase = $script:automaticSeedPhase
                Attempts = $script:automaticSeedAttempts
                MaximumAttempts = 3
                StartsObserved = $script:automaticSeedStartedCount
                ConfirmationsObserved =
                    $script:automaticSeedConfirmedCount
                ReadyEventsObserved = $script:automaticSeedReadyCount
                FailuresObserved = $script:automaticSeedFailureCount
                SeedImpactsBlocked =
                    $script:automaticSeedImpactBlockedCount
                LastConfirmationSource =
                    $script:automaticSeedLastSource
                ManualAttackFallback =
                    $script:automaticSeedPhase -eq 'failed'
            }
        }
        Combat = [pscustomobject][ordered]@{
            EnemyHealthObserved = $script:enemyHealthObserved
            EnemyHealthNote = (
                'Native melee dispatch sends an engine damage message; ' +
                'it does not return enemy health.')
            AcceptedActorHits = $script:acceptedActorHitCount
            AcceptedWorldOrPropHits = $script:acceptedWorldHitCount
            AttackTelegraphTriggers = $script:attackTelegraphTriggerCount
            AutomaticSwingAttackTriggers =
                $script:attackTelegraphTriggerCount
            RetailAttackCommandDownEdges =
                $script:retailAttackCommandDownCount
            RetailAttackCommandUpEdges =
                $script:retailAttackCommandUpCount
            AcceptedWithAttackTelegraph =
                $script:acceptedWithAttackTelegraphCount
            AcceptedWithoutAttackTelegraph =
                $script:acceptedWithoutAttackTelegraphCount
            AcceptedAttackTelegraphUnknown =
                $script:acceptedAttackTelegraphUnknownCount
            MinimumActorHeadHorizontalDistanceMeters =
                $script:minimumActorHeadHorizontalDistanceMeters
            StandOffNote = (
                'Distance is HMD XZ position to Retail contact point; ' +
                'it is diagnostic evidence, not a player-capsule radius.')
            PlayerVitals = $script:playerVitals
            PlayerHealthDecreaseEvents = $script:playerHealthDecreaseCount
            BlockCommandActive = $script:blockCommandActive
            BlockCommandDownEdges = $script:blockCommandDownCount
            BlockCommandUpEdges = $script:blockCommandUpCount
            AutomaticBlockPose = [pscustomobject][ordered]@{
                Configured = $script:blockPoseConfigured
                Active = $script:blockPoseActive
                Activations = $script:blockPoseActivationCount
                Exits = $script:blockPoseExitCount
                TrackingFresh = $script:blockPoseTrackingFresh
                PositionErrorMeters =
                    $script:blockPosePositionErrorMeters
                AngleErrorDegrees =
                    $script:blockPoseAngleErrorDegrees
                PositionToleranceMeters =
                    $script:blockPosePositionToleranceMeters
                AngleToleranceDegrees =
                    $script:blockPoseAngleToleranceDegrees
                LastReason = $script:blockPoseLastReason
                InputSeedRequired = $false
                ManualTriggerFallback = $true
            }
            LastBlockHoldDurationMilliseconds =
                $script:lastBlockHoldDurationMilliseconds
            MaximumBlockHoldDurationMilliseconds =
                $script:maximumBlockHoldDurationMilliseconds
            LastBlockCommandActivationDurationMilliseconds =
                $script:lastBlockHoldDurationMilliseconds
            MaximumBlockCommandActivationDurationMilliseconds =
                $script:maximumBlockHoldDurationMilliseconds
            CommandEdgeTimingNote =
                'Durations require runtime_tick_ms from the loader; watcher ingestion timestamps are not timing evidence.'
            NativeBlockStateObserved = $false
            BlockStateNote = (
                'Command 28 edges describe controller input activation, not ' +
                'the lifetime of Retail block state. A released command may ' +
                'leave a native spatial block window active.')
            HealthDecreasesWhileBlockCommandActive =
                $script:blockedHealthDecreaseCount
            HealthDecreasesWhileBlockCommandInactive =
                $script:unblockedHealthDecreaseCount
            LegacyBlockHealthFieldNote = (
                'HealthDecreasesWhileBlocked/NotBlocked are retained for ' +
                'schema-v4 compatibility; read them as command active/inactive.')
            HealthDecreasesWhileBlocked =
                $script:blockedHealthDecreaseCount
            HealthDecreasesWhileNotBlocked =
                $script:unblockedHealthDecreaseCount
        }
        Counters = [pscustomobject][ordered]@{
            Callbacks = $script:callbackCount
            AcceptedContacts = $script:acceptedCount
            NativeForwards = $script:forwardedCount
            DamageDispatches = $script:damageDispatchCount
            AcceptedActorHits = $script:acceptedActorHitCount
            AcceptedWorldOrPropHits = $script:acceptedWorldHitCount
            AttackTelegraphTriggers = $script:attackTelegraphTriggerCount
            AutomaticSwingAttackTriggers =
                $script:attackTelegraphTriggerCount
            RetailAttackCommandDownEdges =
                $script:retailAttackCommandDownCount
            RetailAttackCommandUpEdges =
                $script:retailAttackCommandUpCount
            AcceptedWithAttackTelegraph =
                $script:acceptedWithAttackTelegraphCount
            AcceptedWithoutAttackTelegraph =
                $script:acceptedWithoutAttackTelegraphCount
            AcceptedAttackTelegraphUnknown =
                $script:acceptedAttackTelegraphUnknownCount
            PlayerHealthDecreaseEvents =
                $script:playerHealthDecreaseCount
            BlockCommandDownEdges = $script:blockCommandDownCount
            BlockCommandUpEdges = $script:blockCommandUpCount
            HealthDecreasesWhileBlocked =
                $script:blockedHealthDecreaseCount
            HealthDecreasesWhileNotBlocked =
                $script:unblockedHealthDecreaseCount
            DuplicateCallbacksBlocked = $script:duplicateCount
            ActorCandidateContacts = $script:actorContactCount
            WorldOrPropContacts = $script:worldContactCount
            RetailReferenceVectorsCleared = $script:targetReferenceClearCount
            RetailReferenceVectorFailures = $script:targetReferenceFailureCount
            RetailReferencesCleared = $script:targetReferenceElementClearCount
            RetailTargetReferenceClears = $script:targetReferenceClearCount
            RetailTargetReferenceFailures = $script:targetReferenceFailureCount
            InvalidSampleLatchHolds = $script:invalidSampleHoldCount
            Rearms = $script:rearmCount
            TrackingLosses = $script:trackingLossCount
            LiveAlignmentApplied = $script:liveAlignmentAppliedCount
            LiveAlignmentRejected = $script:liveAlignmentRejectedCount
            AutomaticSeedStarts = $script:automaticSeedStartedCount
            AutomaticSeedConfirmations =
                $script:automaticSeedConfirmedCount
            AutomaticSeedReadyEvents = $script:automaticSeedReadyCount
            AutomaticSeedFailures = $script:automaticSeedFailureCount
            AutomaticSeedImpactsBlocked =
                $script:automaticSeedImpactBlockedCount
            SameTargetAcceptedBeforeRearm =
                $script:sameTargetBeforeRearmCount
            MultiTargetSwings = $script:multiTargetSwingCount
        }
        LastContact = $script:lastContact
        Targets = $targetSnapshot.ToArray()
        Warnings = $warningSnapshot.ToArray()
        Timeline = $timeline.ToArray()
        SourceLoaderLog = $loaderLog
        EventStream = $eventPath
    }
    $temporaryPath = $statusPath + '.tmp'
    [IO.File]::WriteAllText(
        $temporaryPath,
        ($snapshot | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $temporaryPath -Destination $statusPath -Force
}

$gameWasObserved = $false
do {
    foreach ($line in @(Read-NewLoaderLines)) {
        try { Observe-Entry ($line | ConvertFrom-Json) } catch { }
    }
    $gameRunning = $true
    if ($GameProcessId -gt 0) {
        $gameRunning = $null -ne (Get-Process -Id $GameProcessId `
            -ErrorAction SilentlyContinue)
        $gameWasObserved = $gameWasObserved -or $gameRunning
    }
    Write-Snapshot $gameRunning
    if ($Once -or ($gameWasObserved -and -not $gameRunning)) { break }
    Start-Sleep -Milliseconds $RefreshMilliseconds
} while ($true)

Write-Output $statusPath
