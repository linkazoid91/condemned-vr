$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-launch-profile.ps1')

function Assert-Equal($Expected, $Actual, [string]$Label) {
    if ($Expected -ne $Actual) {
        throw ('{0} expected {1}, got {2}.' -f $Label, $Expected, $Actual)
    }
}

$current = Resolve-CondemnedVrLaunchProfile @{}
Assert-Equal 'Current' $current.Name 'empty launch profile'
Assert-Equal $true $current.ApplyPipePreset 'current Pipe preset'
Assert-Equal $true $current.EnableRetailVrSettings 'current Retail settings'

$waitOnly = Resolve-CondemnedVrLaunchProfile @{ Wait = $true }
Assert-Equal 'Current' $waitOnly.Name 'wait-only launch profile'

$rollbackOnly = Resolve-CondemnedVrLaunchProfile @{
    NoHidFpsFix = $true
    NoXrFramePacing = $true
    NoBackgroundRender = $true
}
Assert-Equal 'Current' $rollbackOnly.Name 'rollback-only launch profile'

$minimal = Resolve-CondemnedVrLaunchProfile @{ Minimal = $true }
Assert-Equal 'Minimal' $minimal.Name 'minimal launch profile'
Assert-Equal $false $minimal.ApplyPipePreset 'minimal Pipe preset'

$pipe = Resolve-CondemnedVrLaunchProfile @{ WeaponTest = 'Pipe' }
Assert-Equal 'Pipe' $pipe.Name 'explicit Pipe launch profile'
Assert-Equal $false $pipe.EnableRetailVrSettings 'Pipe Retail settings'

$custom = Resolve-CondemnedVrLaunchProfile @{
    StereoTuning = $true
    DesktopWindow = $true
}
Assert-Equal 'Custom' $custom.Name 'custom launch profile'

$pipeExtension = Resolve-CondemnedVrLaunchProfile @{
    WeaponTest = 'Pipe'
    ForensicMemoryProbe = $true
}
Assert-Equal 'Custom' $pipeExtension.Name 'extended Pipe profile'

$conflictRejected = $false
try {
    Resolve-CondemnedVrLaunchProfile @{
        Minimal = $true
        StereoTuning = $true
    } | Out-Null
} catch {
    $conflictRejected = $true
}
Assert-Equal $true $conflictRejected 'minimal/custom conflict rejection'

function Resolve-ActualBoundParameters {
    param([switch]$Wait)
    Resolve-CondemnedVrLaunchProfile $PSBoundParameters
}
$actualEmpty = Resolve-ActualBoundParameters
Assert-Equal 'Current' $actualEmpty.Name 'actual empty bound parameters'
$actualWait = Resolve-ActualBoundParameters -Wait
Assert-Equal 'Current' $actualWait.Name 'actual wait bound parameters'

Write-Host 'Condemned launch-profile tests passed.' -ForegroundColor Green
