<#
.SYNOPSIS
    Applies one acknowledged Phase-1 magazine-socket authoring edit.

.DESCRIPTION
    Resolves the newest active Weapon Grip Calibration run, reads the last
    exact weapon/PID/revision and model-local socket values acknowledged by
    GameClient.dll, writes a version-2 command to that run's existing live
    alignment command file, and waits for an applied or rejected log event.

    This edits only Condemned VR's per-weapon settings. It does not invoke a
    reload, touch ammunition, or mutate any Retail weapon state.
#>
[CmdletBinding()]
param(
    [string]$Run,
    [int]$WeaponIndex = -1,
    [string]$WeaponName,
    [ValidateSet('Keep', 'On', 'Off')]
    [string]$Configured = 'Keep',
    [double]$PositionX = [double]::NaN,
    [double]$PositionY = [double]::NaN,
    [double]$PositionZ = [double]::NaN,
    [double]$RotationX = [double]::NaN,
    [double]$RotationY = [double]::NaN,
    [double]$RotationZ = [double]::NaN,
    [double]$HalfExtentX = [double]::NaN,
    [double]$HalfExtentY = [double]::NaN,
    [double]$HalfExtentZ = [double]::NaN,
    [double]$RailLength = [double]::NaN,
    [double]$SnapDistance = [double]::NaN,
    [double]$SnapAngle = [double]::NaN,
    [double]$DeltaPositionX = 0.0,
    [double]$DeltaPositionY = 0.0,
    [double]$DeltaPositionZ = 0.0,
    [double]$DeltaRotationX = 0.0,
    [double]$DeltaRotationY = 0.0,
    [double]$DeltaRotationZ = 0.0,
    [double]$DeltaHalfExtentX = 0.0,
    [double]$DeltaHalfExtentY = 0.0,
    [double]$DeltaHalfExtentZ = 0.0,
    [double]$DeltaRailLength = 0.0,
    [double]$DeltaSnapDistance = 0.0,
    [double]$DeltaSnapAngle = 0.0,
    [ValidateRange(1, 15)]
    [int]$WaitSeconds = 5,
    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$logRoot = Join-Path $cfg.ProjectRoot 'stage\condemned-m2-mono\logs'
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Read-JsonFile([string]$Path) {
    for ($attempt = 0; $attempt -lt 8; $attempt++) {
        try {
            return [IO.File]::ReadAllText($Path) | ConvertFrom-Json
        } catch {
            if ($attempt -eq 7) { throw }
            Start-Sleep -Milliseconds 50
        }
    }
}

function Resolve-RunDirectory {
    if (-not [string]::IsNullOrWhiteSpace($Run)) {
        if (Test-Path -LiteralPath $Run -PathType Container) {
            return Get-Item -LiteralPath $Run
        }
        $candidate = Join-Path $logRoot $Run
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return Get-Item -LiteralPath $candidate
        }
        throw "Condemned VR run not found: $Run"
    }
    foreach ($candidate in @(
        Get-ChildItem -LiteralPath $logRoot -Directory |
            Where-Object {
                Test-Path (Join-Path $_.FullName 'm2-mono-live.json') `
                    -PathType Leaf
            } |
            Sort-Object LastWriteTimeUtc -Descending)) {
        try {
            $candidateReport = Read-JsonFile (
                Join-Path $candidate.FullName 'm2-mono-live.json')
            if ([bool]$candidateReport.M4Input.WeaponGripCalibration -and
                $null -ne (Get-Process `
                    -Id ([int]$candidateReport.GameProcessId) `
                    -ErrorAction SilentlyContinue)) {
                return $candidate
            }
        } catch { }
    }
    throw 'No active Weapon Grip Calibration run is available.'
}

function Assert-RunFile([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description is absent; relaunch with -WeaponGripCalibration."
    }
    $fullPath = Assert-UnderCondemnedVrProjectRoot $Path
    $runRoot = $runDirectory.FullName.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith(
            $runRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Safety stop: $Description is outside the selected run."
    }
    return $fullPath
}

function Match-Required([string]$Text, [string]$Pattern, [string]$Name) {
    $match = [regex]::Match($Text, $Pattern)
    if (-not $match.Success) {
        throw "The acknowledged authoring state has no valid $Name field."
    }
    return $match
}

function Select-Value(
    [double]$Current,
    [double]$Absolute,
    [double]$Delta) {
    $base = if ([double]::IsNaN($Absolute)) { $Current } else { $Absolute }
    return $base + $Delta
}

function Assert-BoundedFinite(
    [string]$Name,
    [double]$Value,
    [double]$Minimum,
    [double]$Maximum) {
    if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value) -or
        $Value -lt $Minimum -or $Value -gt $Maximum) {
        throw "$Name must be finite and within [$Minimum, $Maximum]; got $Value."
    }
}

function Format-Number([double]$Value) {
    return $Value.ToString('0.#########', $invariant)
}

$runDirectory = Resolve-RunDirectory
$report = Read-JsonFile (
    Join-Path $runDirectory.FullName 'm2-mono-live.json')
$gameProcessId = [int]$report.GameProcessId
if ($gameProcessId -le 0 -or
    $null -eq (Get-Process -Id $gameProcessId -ErrorAction SilentlyContinue)) {
    throw "Condemned process $gameProcessId is not running."
}
if (-not [bool]$report.M4Input.WeaponGripCalibration) {
    throw 'The selected run did not enable Weapon Grip Calibration.'
}
$commandPath = Assert-RunFile (
    [string]$report.WeaponAlignmentCommand) 'WeaponAlignmentCommand'
$loaderLog = [string]$report.LoaderLog
if ([string]::IsNullOrWhiteSpace($loaderLog) -or
    -not (Test-Path -LiteralPath $loaderLog -PathType Leaf)) {
    throw 'The selected run has no readable loader log.'
}

$stateDeadline = (Get-Date).AddSeconds($WaitSeconds)
$stateLine = $null
do {
    $lines = @(Get-Content -LiteralPath $loaderLog -ErrorAction SilentlyContinue)
    $stateLine = $lines |
        Where-Object {
            $_ -match 'm5_(live_magazine_socket_armed|magazine_socket_authoring_applied|live_magazine_socket_applied)' -and
            $_ -match "process_id=$gameProcessId(?:\s|$)"
        } |
        Select-Object -Last 1
    if ($null -ne $stateLine) { break }
    if ((Get-Date) -ge $stateDeadline) { break }
    Start-Sleep -Milliseconds 100
} while ($true)
if ($null -eq $stateLine) {
    throw 'Open the in-headset AUTHOR tab; no active magazine-socket state was acknowledged.'
}

$number = '([-+0-9.eE]+)'
$revisionMatch = Match-Required $stateLine 'revision=([0-9]+)' 'revision'
$indexMatch = Match-Required $stateLine 'weapon_index=(-?[0-9]+)' 'weapon index'
$nameMatch = Match-Required $stateLine 'weapon_name=([A-Za-z0-9_.-]+)' 'weapon name'
$configuredMatch = Match-Required $stateLine 'configured=([01])' 'configured state'
$positionMatch = Match-Required $stateLine "position_cm=\($number,$number,$number\)" 'position'
$rotationMatch = Match-Required $stateLine "rotation_deg=\($number,$number,$number\)" 'rotation'
$halfMatch = Match-Required $stateLine "half_extents_cm=\($number,$number,$number\)" 'half extents'
$railMatch = Match-Required $stateLine "rail_cm=$number" 'rail length'
$snapDistanceMatch = Match-Required $stateLine "snap_cm=$number" 'snap distance'
$snapAngleMatch = Match-Required $stateLine "snap_deg=$number" 'snap angle'

$baseRevision = [long]$revisionMatch.Groups[1].Value
$activeWeaponIndex = [int]$indexMatch.Groups[1].Value
$activeWeaponName = $nameMatch.Groups[1].Value
if ($WeaponIndex -ge 0 -and $WeaponIndex -ne $activeWeaponIndex) {
    throw "Active authoring weapon index is $activeWeaponIndex; requested $WeaponIndex."
}
if (-not [string]::IsNullOrWhiteSpace($WeaponName) -and
    $WeaponName -cne $activeWeaponName) {
    throw "Active authoring weapon name is '$activeWeaponName'; requested '$WeaponName'."
}

$positionXValue = Select-Value ([double]$positionMatch.Groups[1].Value) $PositionX $DeltaPositionX
$positionYValue = Select-Value ([double]$positionMatch.Groups[2].Value) $PositionY $DeltaPositionY
$positionZValue = Select-Value ([double]$positionMatch.Groups[3].Value) $PositionZ $DeltaPositionZ
$rotationXValue = Select-Value ([double]$rotationMatch.Groups[1].Value) $RotationX $DeltaRotationX
$rotationYValue = Select-Value ([double]$rotationMatch.Groups[2].Value) $RotationY $DeltaRotationY
$rotationZValue = Select-Value ([double]$rotationMatch.Groups[3].Value) $RotationZ $DeltaRotationZ
$halfXValue = Select-Value ([double]$halfMatch.Groups[1].Value) $HalfExtentX $DeltaHalfExtentX
$halfYValue = Select-Value ([double]$halfMatch.Groups[2].Value) $HalfExtentY $DeltaHalfExtentY
$halfZValue = Select-Value ([double]$halfMatch.Groups[3].Value) $HalfExtentZ $DeltaHalfExtentZ
$railValue = Select-Value ([double]$railMatch.Groups[1].Value) $RailLength $DeltaRailLength
$snapDistanceValue = Select-Value ([double]$snapDistanceMatch.Groups[1].Value) $SnapDistance $DeltaSnapDistance
$snapAngleValue = Select-Value ([double]$snapAngleMatch.Groups[1].Value) $SnapAngle $DeltaSnapAngle
$configuredValue = switch ($Configured) {
    'On' { 1 }
    'Off' { 0 }
    default { [int]$configuredMatch.Groups[1].Value }
}

Assert-BoundedFinite 'PositionX' $positionXValue -300.0 300.0
Assert-BoundedFinite 'PositionY' $positionYValue -300.0 300.0
Assert-BoundedFinite 'PositionZ' $positionZValue -300.0 300.0
Assert-BoundedFinite 'RotationX' $rotationXValue -180.0 180.0
Assert-BoundedFinite 'RotationY' $rotationYValue -180.0 180.0
Assert-BoundedFinite 'RotationZ' $rotationZValue -180.0 180.0
Assert-BoundedFinite 'HalfExtentX' $halfXValue 0.25 50.0
Assert-BoundedFinite 'HalfExtentY' $halfYValue 0.25 50.0
Assert-BoundedFinite 'HalfExtentZ' $halfZValue 0.25 50.0
Assert-BoundedFinite 'RailLength' $railValue 1.0 150.0
Assert-BoundedFinite 'SnapDistance' $snapDistanceValue 0.25 30.0
Assert-BoundedFinite 'SnapAngle' $snapAngleValue 1.0 90.0

$revision = [long][DateTime]::UtcNow.Ticks
if ($revision -le $baseRevision) {
    $revision = $baseRevision + 1
}
$command = (
    'version=2 revision={0} base_revision={1} pid={2} ' +
    'weapon_index={3} weapon_name={4} configured={5} ' +
    'pos={6},{7},{8} rot={9},{10},{11} half={12},{13},{14} ' +
    'rail={15} snap_distance={16} snap_angle={17}') -f
    $revision, $baseRevision, $gameProcessId,
    $activeWeaponIndex, $activeWeaponName, $configuredValue,
    (Format-Number $positionXValue),
    (Format-Number $positionYValue),
    (Format-Number $positionZValue),
    (Format-Number $rotationXValue),
    (Format-Number $rotationYValue),
    (Format-Number $rotationZValue),
    (Format-Number $halfXValue),
    (Format-Number $halfYValue),
    (Format-Number $halfZValue),
    (Format-Number $railValue),
    (Format-Number $snapDistanceValue),
    (Format-Number $snapAngleValue)

$temporaryPath = $commandPath + '.tmp-' + $PID
[IO.File]::WriteAllText(
    $temporaryPath, $command + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))
Move-Item -LiteralPath $temporaryPath -Destination $commandPath -Force

if ($NoWait) {
    Write-Output ([pscustomobject][ordered]@{
        Result = 'written'
        Run = $runDirectory.Name
        Revision = $revision
        BaseRevision = $baseRevision
        ProcessId = $gameProcessId
        WeaponIndex = $activeWeaponIndex
        WeaponName = $activeWeaponName
        CommandPath = $commandPath
    })
    return
}

$ackDeadline = (Get-Date).AddSeconds($WaitSeconds)
do {
    if ($null -eq (Get-Process -Id $gameProcessId `
            -ErrorAction SilentlyContinue)) {
        throw 'The game exited before acknowledging the authoring command.'
    }
    $recentLines = @(Get-Content -LiteralPath $loaderLog `
        -Tail 300 -ErrorAction SilentlyContinue)
    $revisionPattern = [regex]::Escape([string]$revision)
    $applied = $recentLines | Where-Object {
        $_ -match 'm5_live_magazine_socket_applied' -and
        $_ -match "revision=$revisionPattern(?:\s|$)" -and
        $_ -match "process_id=$gameProcessId(?:\s|$)"
    } | Select-Object -Last 1
    if ($null -ne $applied) {
        Write-Output ([pscustomobject][ordered]@{
            Result = 'applied'
            Run = $runDirectory.Name
            Revision = $revision
            BaseRevision = $baseRevision
            ProcessId = $gameProcessId
            WeaponIndex = $activeWeaponIndex
            WeaponName = $activeWeaponName
            RetailStateMutation = $false
        })
        return
    }
    $rejected = $recentLines | Where-Object {
        $_ -match 'm5_live_magazine_socket_rejected' -and
        $_ -match "revision=$revisionPattern(?:\s|$)"
    } | Select-Object -Last 1
    if ($null -ne $rejected) {
        $reason = [regex]::Match($rejected, 'reason=([^ ]+)')
        throw "Game rejected authoring revision $revision`: $($reason.Groups[1].Value)."
    }
    Start-Sleep -Milliseconds 100
} while ((Get-Date) -lt $ackDeadline)

throw "Timed out waiting for authoring revision $revision acknowledgment."
