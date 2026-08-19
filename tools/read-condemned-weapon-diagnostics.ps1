<#
.SYNOPSIS
    Reads the newest live Condemned weapon-test diagnostic snapshot.

.PARAMETER Run
    Optional run-directory name or full path. Defaults to the newest run with
    a weapon-diagnostics-live.json snapshot.

.PARAMETER Json
    Writes the unchanged machine-readable JSON instead of a compact summary.

.PARAMETER WaitSeconds
    Waits up to this many seconds for a snapshot to appear.
#>
[CmdletBinding()]
param(
    [string]$Run,
    [switch]$Json,
    [ValidateRange(0, 60)]
    [int]$WaitSeconds = 0
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$logRoot = Join-Path $cfg.ProjectRoot 'stage\condemned-m2-mono\logs'

function Resolve-RunDirectory {
    if ([string]::IsNullOrWhiteSpace($Run)) {
        return Get-ChildItem -LiteralPath $logRoot -Directory |
            Where-Object {
                Test-Path (Join-Path $_.FullName (
                    'weapon-diagnostics-live.json')) -PathType Leaf
            } |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
    }
    if (Test-Path -LiteralPath $Run -PathType Container) {
        return Get-Item -LiteralPath $Run
    }
    $candidate = Join-Path $logRoot $Run
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        return Get-Item -LiteralPath $candidate
    }
    return $null
}

$deadline = (Get-Date).AddSeconds($WaitSeconds)
do {
    $runDirectory = Resolve-RunDirectory
    $snapshotPath = if ($null -ne $runDirectory) {
        Join-Path $runDirectory.FullName 'weapon-diagnostics-live.json'
    } else {
        $null
    }
    if ($null -ne $snapshotPath -and
        (Test-Path -LiteralPath $snapshotPath -PathType Leaf)) {
        break
    }
    if ((Get-Date) -ge $deadline) {
        throw 'No weapon diagnostics snapshot is available.'
    }
    Start-Sleep -Milliseconds 250
} while ($true)

$text = Get-Content -Raw -LiteralPath $snapshotPath
if ($Json) {
    Write-Output $text
    return
}

$snapshot = $text | ConvertFrom-Json
$counters = $snapshot.Counters
$vectorClears = $counters.RetailReferenceVectorsCleared
if ($null -eq $vectorClears) {
    $vectorClears = $counters.RetailTargetReferenceClears
}
$vectorFailures = $counters.RetailReferenceVectorFailures
if ($null -eq $vectorFailures) {
    $vectorFailures = $counters.RetailTargetReferenceFailures
}
$referencesCleared = $counters.RetailReferencesCleared
if ($null -eq $referencesCleared) { $referencesCleared = 0 }
$invalidSampleHolds = $counters.InvalidSampleLatchHolds
if ($null -eq $invalidSampleHolds) {
    $invalidSampleHolds = 0
}
$sameTargetBeforeRearm = $counters.SameTargetAcceptedBeforeRearm
if ($null -eq $sameTargetBeforeRearm) {
    $sameTargetBeforeRearm = 0
}
$multiTargetSwings = $counters.MultiTargetSwings
if ($null -eq $multiTargetSwings) {
    $multiTargetSwings = 0
}
Write-Host "=== Weapon diagnostics: $($snapshot.Run) ===" -ForegroundColor Cyan
Write-Host ("Phase: {0}  Healthy: {1}  Game running: {2}" -f
    $snapshot.Phase, $snapshot.Healthy, $snapshot.GameRunning)
Write-Host ("Profile: {0} ({1})  Collider: {2} {3}" -f
    $snapshot.Profile.Name, $snapshot.Profile.WeaponIndex,
    $(if ($snapshot.Collider.Seeded) { 'GREEN' } else { 'AMBER' }),
    $snapshot.Collider.CollisionObject)
if ($null -ne $snapshot.Collider.StateEventsObserved) {
    Write-Host ("Collider telemetry: observed {0}, recorded {1}, suppressed {2}" -f
        $snapshot.Collider.StateEventsObserved,
        $snapshot.Collider.StateEventsRecorded,
        $snapshot.Collider.StateEventsSuppressed)
}
$automaticSeed = $snapshot.Collider.AutomaticSeed
if ($null -ne $automaticSeed) {
    Write-Host ((
        'Automatic seed: phase={0}, attempt {1}/{2}; ' +
        'starts/confirmed/ready/failed {3}/{4}/{5}/{6}; ' +
        'seed impacts blocked {7}') -f
        $automaticSeed.Phase,
        $automaticSeed.Attempts,
        $automaticSeed.MaximumAttempts,
        $automaticSeed.StartsObserved,
        $automaticSeed.ConfirmationsObserved,
        $automaticSeed.ReadyEventsObserved,
        $automaticSeed.FailuresObserved,
        $automaticSeed.SeedImpactsBlocked)
    if ($automaticSeed.ManualAttackFallback) {
        Write-Host (
            'Automatic seed retries were exhausted; one manual Retail ' +
            'attack remains the fallback.') -ForegroundColor Yellow
    }
}
$alignment = $snapshot.Collider.LiveAlignment
if ($null -ne $alignment -and $alignment.Available) {
    $direction = if ($alignment.Reversed) { 'REVERSE' } else { 'FORWARD' }
    Write-Host ((
        "Alignment r{0} ({1}): pos ({2:F3}, {3:F3}, {4:F3}), " +
        "rot ({5:F3}, {6:F3}, {7:F3}), length {8:F3}, radius {9:F3}, {10}") -f
        [long]$alignment.Revision, $alignment.Source,
        [double]$alignment.PositionUnits.X,
        [double]$alignment.PositionUnits.Y,
        [double]$alignment.PositionUnits.Z,
        [double]$alignment.RotationDegrees.X,
        [double]$alignment.RotationDegrees.Y,
        [double]$alignment.RotationDegrees.Z,
        [double]$alignment.LengthUnits,
        [double]$alignment.RadiusUnits,
        $direction)
}
$alignmentError = $snapshot.Collider.LastAlignmentError
if ($null -ne $alignmentError) {
    Write-Host ("Last alignment rejection r{0}: {1}" -f
        [long]$alignmentError.Revision, $alignmentError.Reason) `
        -ForegroundColor Yellow
}
Write-Host ("Contacts: {0} callbacks, {1} accepted, {2} forwarded, {3} duplicate" -f
    $counters.Callbacks, $counters.AcceptedContacts,
    $counters.NativeForwards, $counters.DuplicateCallbacksBlocked)
$combat = $snapshot.Combat
if ($null -ne $combat) {
    $automaticSwingAttackTriggers = $combat.AutomaticSwingAttackTriggers
    if ($null -eq $automaticSwingAttackTriggers) {
        $automaticSwingAttackTriggers = $combat.AttackTelegraphTriggers
    }
    Write-Host ((
        'Combat: {0} actor hits, {1} world/prop hits; ' +
        '{2} automatic swing-attack triggers; accepted with/without/unknown {3}/{4}/{5}') -f
        $combat.AcceptedActorHits, $combat.AcceptedWorldOrPropHits,
        $automaticSwingAttackTriggers,
        $combat.AcceptedWithAttackTelegraph,
        $combat.AcceptedWithoutAttackTelegraph,
        $combat.AcceptedAttackTelegraphUnknown)
    if ($null -ne $combat.RetailAttackCommandDownEdges) {
        Write-Host ('Retail attack command: down/up edges {0}/{1}' -f
            $combat.RetailAttackCommandDownEdges,
            $combat.RetailAttackCommandUpEdges)
    }
    if ($null -ne $combat.MinimumActorHeadHorizontalDistanceMeters) {
        Write-Host (
            'Minimum HMD-to-actor-contact horizontal distance: {0:F3} m' -f
            [double]$combat.MinimumActorHeadHorizontalDistanceMeters)
    }
    if ($null -ne $combat.PlayerVitals) {
        $healthWhileCommandActive =
            $combat.HealthDecreasesWhileBlockCommandActive
        if ($null -eq $healthWhileCommandActive) {
            $healthWhileCommandActive = $combat.HealthDecreasesWhileBlocked
        }
        $healthWhileCommandInactive =
            $combat.HealthDecreasesWhileBlockCommandInactive
        if ($null -eq $healthWhileCommandInactive) {
            $healthWhileCommandInactive =
                $combat.HealthDecreasesWhileNotBlocked
        }
        $lastActivationDuration =
            $combat.LastBlockCommandActivationDurationMilliseconds
        if ($null -eq $lastActivationDuration) {
            $lastActivationDuration =
                $combat.LastBlockHoldDurationMilliseconds
        }
        $maximumActivationDuration =
            $combat.MaximumBlockCommandActivationDurationMilliseconds
        if ($null -eq $maximumActivationDuration) {
            $maximumActivationDuration =
                $combat.MaximumBlockHoldDurationMilliseconds
        }
        Write-Host (
            'Player health: {0}/{1} ({2:P0}), delta {3}; decreases {4}' -f
            [long]$combat.PlayerVitals.Current,
            [long]$combat.PlayerVitals.Maximum,
            [double]$combat.PlayerVitals.Fraction,
            [long]$combat.PlayerVitals.Delta,
            [long]$combat.PlayerHealthDecreaseEvents)
        Write-Host (("Block command: active={0}, down/up edges {1}/{2}; " +
            "health decreases while command active/inactive {3}/{4}") -f
            $combat.BlockCommandActive, $combat.BlockCommandDownEdges,
            $combat.BlockCommandUpEdges,
            $healthWhileCommandActive,
            $healthWhileCommandInactive)
        if ($null -ne $maximumActivationDuration) {
            Write-Host (
                'Block command activation: last {0} ms, maximum {1} ms' -f
                $lastActivationDuration,
                $maximumActivationDuration)
        }
        if ($null -ne $combat.AutomaticBlockPose) {
            $pose = $combat.AutomaticBlockPose
            Write-Host ((
                'Automatic block pose: configured={0}, active={1}, ' +
                'entries/exits {2}/{3}, tracking fresh={4}') -f
                $pose.Configured, $pose.Active,
                $pose.Activations, $pose.Exits,
                $pose.TrackingFresh)
            if ($null -ne $pose.PositionErrorMeters) {
                Write-Host ((
                    'Guard error: {0:F3}/{1:F3} m, ' +
                    '{2:F1}/{3:F1} deg; reason={4}; seed required={5}') -f
                    [double]$pose.PositionErrorMeters,
                    [double]$pose.PositionToleranceMeters,
                    [double]$pose.AngleErrorDegrees,
                    [double]$pose.AngleToleranceDegrees,
                    $pose.LastReason, $pose.InputSeedRequired)
            }
        }
        if ($null -ne $combat.BlockStateNote) {
            Write-Host ('Block evidence note: {0}' -f
                $combat.BlockStateNote) -ForegroundColor DarkYellow
        }
    }
}
Write-Host ("Retail vectors: {0} clear, {1} failed, {2} refs released  Rearms: {3}  Invalid holds: {4}  Targets: {5}" -f
    $vectorClears, $vectorFailures, $referencesCleared,
    $counters.Rearms, $invalidSampleHolds, @($snapshot.Targets).Count)
if ($null -ne $snapshot.LastContact) {
Write-Host ("Swing invariant: {0} same-target reaccepts before reset; {1} multi-target swings" -f
    $sameTargetBeforeRearm, $multiTargetSwings)
    $referenceState = $snapshot.LastContact.RetailReferenceVectorState
    if ([string]::IsNullOrWhiteSpace([string]$referenceState)) {
        $referenceState =
            $snapshot.LastContact.RetailTargetReferenceState
    }
    Write-Host ("Last: {0} {1} accepted={2} reason={3} vector={4}" -f
        $snapshot.LastContact.Kind, $snapshot.LastContact.Target,
        $snapshot.LastContact.Accepted, $snapshot.LastContact.Reason,
        $referenceState)
    $distance = $snapshot.LastContact.Distance
    if ($null -ne $distance -and $distance.Valid) {
        Write-Host ((
            "Distance to target contact: {0}; capsule gap {1:F4} m, " +
            "axis {2:F4} m, tip {3:F4} m, radius {4:F4} m") -f
            $distance.Assessment,
            [double]$distance.CapsuleToTargetGapMeters,
            [double]$distance.AxisToTargetContactMeters,
            [double]$distance.TipToTargetContactMeters,
            [double]$distance.CapsuleRadiusMeters)
    } elseif ($null -ne $distance) {
        Write-Host 'Distance to target contact: unavailable'
    }
    if ($null -ne $distance -and $distance.HeadPoseValid) {
        Write-Host (
            'HMD to contact: {0:F3} m horizontal, {1:F3} m full' -f
            [double]$distance.HeadHorizontalToTargetContactMeters,
            [double]$distance.HeadToTargetContactMeters)
    }
    $telegraph = $snapshot.LastContact.AttackTelegraph
    if ($null -ne $telegraph -and $telegraph.Observed) {
        Write-Host (
            'Automatic swing attack: enabled={0} triggered-this-swing={1} pulse={2}' -f
            $telegraph.Enabled, $telegraph.TriggeredThisSwing,
            $telegraph.PulseActive)
    }
}
Write-Host "Next: $($snapshot.Recommendation)"
