<#
.SYNOPSIS
    Applies a collider-alignment change to a running Condemned weapon test.

.DESCRIPTION
    Resolves the newest active WeaponTest run by default, reads the collider
    values last acknowledged by the game, writes one bounded command targeted
    to that exact process and weapon, and waits for a matching applied/rejected
    diagnostic. Absolute values and deltas may be combined.

.PARAMETER Run
    Run-directory name or full path. Defaults to the newest active weapon-test
    run.

.PARAMETER WeaponIndex
    Retail weapon index. The first supported live workflow is pipe_lever (32).

.PARAMETER NoWait
    Writes the command without waiting for the game acknowledgment. Reading
    the current live-alignment snapshot is still required as the safe base.
#>
[CmdletBinding()]
param(
    [string]$Run,
    [ValidateRange(0, 4096)]
    [int]$WeaponIndex = 32,
    [double]$PositionX = [double]::NaN,
    [double]$PositionY = [double]::NaN,
    [double]$PositionZ = [double]::NaN,
    [double]$RotationX = [double]::NaN,
    [double]$RotationY = [double]::NaN,
    [double]$RotationZ = [double]::NaN,
    [double]$Length = [double]::NaN,
    [double]$Radius = [double]::NaN,
    [double]$DeltaPositionX = 0.0,
    [double]$DeltaPositionY = 0.0,
    [double]$DeltaPositionZ = 0.0,
    [double]$DeltaRotationX = 0.0,
    [double]$DeltaRotationY = 0.0,
    [double]$DeltaRotationZ = 0.0,
    [double]$DeltaLength = 0.0,
    [double]$DeltaRadius = 0.0,
    [ValidateSet('Keep', 'Forward', 'Reverse')]
    [string]$Direction = 'Keep',
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
            if ($candidateReport.WeaponTestPreset -eq 'Pipe' -and
                $null -ne (Get-Process `
                    -Id ([int]$candidateReport.GameProcessId) `
                    -ErrorAction SilentlyContinue)) {
                return $candidate
            }
        } catch { }
    }
    throw 'No active Condemned WeaponTest run is available.'
}

function Assert-RunFile([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description is absent; relaunch with -WeaponTest Pipe."
    }
    $fullPath = Assert-UnderCondemnedVrProjectRoot $Path
    $runRoot = $runDirectory.FullName.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith(
            $runRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Safety stop: $Description is outside the selected run."
    }
    return $fullPath
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
$reportPath = Join-Path $runDirectory.FullName 'm2-mono-live.json'
$report = Read-JsonFile $reportPath
$gameProcessId = [int]$report.GameProcessId
if ($gameProcessId -le 0 -or
    $null -eq (Get-Process -Id $gameProcessId -ErrorAction SilentlyContinue)) {
    throw "Condemned process $gameProcessId is not running."
}
if ($report.WeaponTestPreset -ne 'Pipe') {
    throw 'Live collider alignment currently requires -WeaponTest Pipe.'
}

$commandPath = Assert-RunFile (
    [string]$report.WeaponAlignmentCommand) 'WeaponAlignmentCommand'
$statusPathValue = [string]$report.WeaponDiagnostics
if ([string]::IsNullOrWhiteSpace($statusPathValue)) {
    $statusPathValue = Join-Path $runDirectory.FullName (
        'weapon-diagnostics-live.json')
}
$statusPath = Assert-RunFile $statusPathValue 'WeaponDiagnostics'

$snapshotDeadline = (Get-Date).AddSeconds($WaitSeconds)
$snapshot = $null
do {
    if (Test-Path -LiteralPath $statusPath -PathType Leaf) {
        try {
            $candidateSnapshot = Read-JsonFile $statusPath
            if ([int]$candidateSnapshot.SchemaVersion -ge 3 -and
                [int]$candidateSnapshot.GameProcessId -eq $gameProcessId -and
                $null -ne $candidateSnapshot.Collider.LiveAlignment) {
                $snapshot = $candidateSnapshot
                break
            }
        } catch { }
    }
    if ((Get-Date) -ge $snapshotDeadline) { break }
    Start-Sleep -Milliseconds 100
} while ($true)
if ($null -eq $snapshot) {
    throw 'The game has not published a schema-v3 live-alignment snapshot.'
}

$alignment = $snapshot.Collider.LiveAlignment
if ([int]$alignment.ProcessId -ne $gameProcessId -or
    [int]$alignment.WeaponIndex -ne $WeaponIndex) {
    throw ("Active alignment targets process {0}, weapon {1}; requested {2}, {3}." -f
        $alignment.ProcessId, $alignment.WeaponIndex,
        $gameProcessId, $WeaponIndex)
}

$positionXValue = Select-Value `
    ([double]$alignment.PositionUnits.X) $PositionX $DeltaPositionX
$positionYValue = Select-Value `
    ([double]$alignment.PositionUnits.Y) $PositionY $DeltaPositionY
$positionZValue = Select-Value `
    ([double]$alignment.PositionUnits.Z) $PositionZ $DeltaPositionZ
$rotationXValue = Select-Value `
    ([double]$alignment.RotationDegrees.X) $RotationX $DeltaRotationX
$rotationYValue = Select-Value `
    ([double]$alignment.RotationDegrees.Y) $RotationY $DeltaRotationY
$rotationZValue = Select-Value `
    ([double]$alignment.RotationDegrees.Z) $RotationZ $DeltaRotationZ
$lengthValue = Select-Value `
    ([double]$alignment.LengthUnits) $Length $DeltaLength
$radiusValue = Select-Value `
    ([double]$alignment.RadiusUnits) $Radius $DeltaRadius
$reversed = switch ($Direction) {
    'Forward' { $false }
    'Reverse' { $true }
    default { [bool]$alignment.Reversed }
}

Assert-BoundedFinite 'PositionX' $positionXValue -200.0 200.0
Assert-BoundedFinite 'PositionY' $positionYValue -200.0 200.0
Assert-BoundedFinite 'PositionZ' $positionZValue -200.0 200.0
Assert-BoundedFinite 'RotationX' $rotationXValue -180.0 180.0
Assert-BoundedFinite 'RotationY' $rotationYValue -180.0 180.0
Assert-BoundedFinite 'RotationZ' $rotationZValue -180.0 180.0
Assert-BoundedFinite 'Length' $lengthValue 5.0 250.0
Assert-BoundedFinite 'Radius' $radiusValue 0.5 25.0

$revision = [long][DateTime]::UtcNow.Ticks
if ($revision -le [long]$alignment.Revision) {
    $revision = [long]$alignment.Revision + 1
}
$command = (
    'version=1 revision={0} process_id={1} weapon_index={2} ' +
    'position_x={3} position_y={4} position_z={5} ' +
    'rotation_x={6} rotation_y={7} rotation_z={8} ' +
    'length={9} radius={10} reversed={11}') -f
    $revision, $gameProcessId, $WeaponIndex,
    (Format-Number $positionXValue),
    (Format-Number $positionYValue),
    (Format-Number $positionZValue),
    (Format-Number $rotationXValue),
    (Format-Number $rotationYValue),
    (Format-Number $rotationZValue),
    (Format-Number $lengthValue),
    (Format-Number $radiusValue),
    $(if ($reversed) { 1 } else { 0 })

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
        ProcessId = $gameProcessId
        WeaponIndex = $WeaponIndex
        CommandPath = $commandPath
    })
    return
}

$ackDeadline = (Get-Date).AddSeconds($WaitSeconds)
do {
    if ($null -eq (Get-Process -Id $gameProcessId `
            -ErrorAction SilentlyContinue)) {
        throw 'The game exited before acknowledging the alignment command.'
    }
    if (Test-Path -LiteralPath $statusPath -PathType Leaf) {
        try {
            $ackSnapshot = Read-JsonFile $statusPath
            $applied = $ackSnapshot.Collider.LiveAlignment
            if ($null -ne $applied -and
                [long]$applied.Revision -eq $revision -and
                [int]$applied.ProcessId -eq $gameProcessId -and
                [int]$applied.WeaponIndex -eq $WeaponIndex -and
                $applied.Result -eq 'applied') {
                Write-Output ([pscustomobject][ordered]@{
                    Result = 'applied'
                    Run = $runDirectory.Name
                    Revision = $revision
                    ProcessId = $gameProcessId
                    WeaponIndex = $WeaponIndex
                    PositionUnits = $applied.PositionUnits
                    RotationDegrees = $applied.RotationDegrees
                    LengthUnits = $applied.LengthUnits
                    RadiusUnits = $applied.RadiusUnits
                    Reversed = $applied.Reversed
                })
                return
            }
            $rejected = $ackSnapshot.Collider.LastAlignmentError
            if ($null -ne $rejected -and
                [long]$rejected.Revision -eq $revision) {
                throw "Game rejected alignment revision $revision`: $($rejected.Reason)."
            }
        } catch {
            if ($_.Exception.Message.StartsWith(
                    'Game rejected alignment')) {
                throw
            }
        }
    }
    Start-Sleep -Milliseconds 100
} while ((Get-Date) -lt $ackDeadline)

throw "Timed out waiting for alignment revision $revision acknowledgment."
