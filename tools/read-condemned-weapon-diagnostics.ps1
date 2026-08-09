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
}
Write-Host "Next: $($snapshot.Recommendation)"
