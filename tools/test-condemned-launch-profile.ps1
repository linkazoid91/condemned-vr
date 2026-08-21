$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-launch-profile.ps1')

function Assert-Equal($Expected, $Actual, [string]$Label) {
    if ($Expected -ne $Actual) {
        throw ('{0} expected {1}, got {2}.' -f $Label, $Expected, $Actual)
    }
}

function Assert-SequenceEqual(
    [object[]]$Expected,
    [object[]]$Actual,
    [string]$Label
) {
    Assert-Equal $Expected.Count $Actual.Count "$Label count"
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        Assert-Equal $Expected[$index] $Actual[$index] "$Label item $index"
    }
}

$current = Resolve-CondemnedVrLaunchProfile @{}
Assert-Equal 'Current' $current.Name 'empty launch profile'
Assert-Equal $true $current.ApplyPipePreset 'current Pipe preset'
Assert-Equal $true $current.EnableRetailVrSettings 'current Retail settings'
Assert-Equal $true $current.RetailHeadBobSuppressed `
    'current HeadBob suppression'
Assert-SequenceEqual @('+HeadBob', '0', '-condemnedvr-m5-retail-headbob-post-profile-zero') `
    @(Get-CondemnedVrRetailHeadBobArguments $current) `
    'current HeadBob arguments'
Assert-SequenceEqual `
    @('-condemnedvr-test', '+Windowed', '1', '+HeadBob', '0', '-condemnedvr-m5-retail-headbob-post-profile-zero') `
    @(Add-CondemnedVrRetailHeadBobArguments `
        -GameArguments @('-condemnedvr-test', '+Windowed', '1') `
        -LaunchProfile $current) `
    'current HeadBob final argument ordering'

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
Assert-Equal $false $minimal.RetailHeadBobSuppressed `
    'minimal HeadBob suppression'
Assert-Equal $null $minimal.RetailHeadBobCommandValue `
    'minimal HeadBob command value'
Assert-SequenceEqual @() `
    @(Get-CondemnedVrRetailHeadBobArguments $minimal) `
    'minimal HeadBob arguments'
Assert-SequenceEqual @('-condemnedvr-test') `
    @(Add-CondemnedVrRetailHeadBobArguments `
        -GameArguments @('-condemnedvr-test') `
        -LaunchProfile $minimal) `
    'minimal argument preservation'

$pipe = Resolve-CondemnedVrLaunchProfile @{ WeaponTest = 'Pipe' }
Assert-Equal 'Pipe' $pipe.Name 'explicit Pipe launch profile'
Assert-Equal $false $pipe.EnableRetailVrSettings 'Pipe Retail settings'
Assert-SequenceEqual @('+HeadBob', '0', '-condemnedvr-m5-retail-headbob-post-profile-zero') `
    @(Get-CondemnedVrRetailHeadBobArguments $pipe) `
    'Pipe HeadBob arguments'
Assert-SequenceEqual @('-condemnedvr-test', '+HeadBob', '0', '-condemnedvr-m5-retail-headbob-post-profile-zero') `
    @(Add-CondemnedVrRetailHeadBobArguments `
        -GameArguments @('-condemnedvr-test') `
        -LaunchProfile $pipe) `
    'Pipe HeadBob final argument ordering'


$custom = Resolve-CondemnedVrLaunchProfile @{
    StereoTuning = $true
    DesktopWindow = $true
}
Assert-Equal 'Custom' $custom.Name 'custom launch profile'
Assert-SequenceEqual @('+HeadBob', '0', '-condemnedvr-m5-retail-headbob-post-profile-zero') `
    @(Get-CondemnedVrRetailHeadBobArguments $custom) `
    'custom HeadBob arguments'

$retailHeadBob = Resolve-CondemnedVrLaunchProfile @{
    RetailHeadBob = $true
}
Assert-Equal 'Current' $retailHeadBob.Name `
    'HeadBob rollback-only launch profile'
Assert-Equal $false $retailHeadBob.RetailHeadBobSuppressed `
    'HeadBob rollback suppression'
Assert-Equal 1 $retailHeadBob.RetailHeadBobCommandValue `
    'HeadBob rollback command value'
Assert-SequenceEqual @('+HeadBob', '1') `
    @(Get-CondemnedVrRetailHeadBobArguments $retailHeadBob) `
    'HeadBob rollback arguments'
Assert-SequenceEqual @('-condemnedvr-test', '+HeadBob', '1') `
    @(Add-CondemnedVrRetailHeadBobArguments `
        -GameArguments @('-condemnedvr-test') `
        -LaunchProfile $retailHeadBob) `
    'HeadBob rollback final argument ordering'

$customRetailHeadBob = Resolve-CondemnedVrLaunchProfile @{
    StereoTuning = $true
    RetailHeadBob = $true
}
Assert-Equal 'Custom' $customRetailHeadBob.Name `
    'custom HeadBob rollback profile'
Assert-SequenceEqual @('+HeadBob', '1') `
    @(Get-CondemnedVrRetailHeadBobArguments $customRetailHeadBob) `
    'custom HeadBob rollback arguments'

$minimalRetailHeadBob = Resolve-CondemnedVrLaunchProfile @{
    Minimal = $true
    RetailHeadBob = $true
}
Assert-Equal 'Minimal' $minimalRetailHeadBob.Name `
    'minimal HeadBob rollback profile'
Assert-SequenceEqual @('+HeadBob', '1') `
    @(Get-CondemnedVrRetailHeadBobArguments $minimalRetailHeadBob) `
    'minimal HeadBob rollback arguments'

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
    param(
        [switch]$Wait,
        [switch]$RetailHeadBob
    )
    Resolve-CondemnedVrLaunchProfile $PSBoundParameters
}
$actualEmpty = Resolve-ActualBoundParameters
Assert-Equal 'Current' $actualEmpty.Name 'actual empty bound parameters'
$actualWait = Resolve-ActualBoundParameters -Wait
Assert-Equal 'Current' $actualWait.Name 'actual wait bound parameters'
$actualRetailHeadBob = Resolve-ActualBoundParameters -RetailHeadBob
Assert-Equal 'Current' $actualRetailHeadBob.Name `
    'actual HeadBob rollback bound parameters'
Assert-SequenceEqual @('+HeadBob', '1') `
    @(Get-CondemnedVrRetailHeadBobArguments $actualRetailHeadBob) `
    'actual HeadBob rollback arguments'

$headBobDiagnostic = Resolve-CondemnedVrLaunchProfile @{
    HeadBobDiagnostic = $true
}
Assert-Equal 'Current' $headBobDiagnostic.Name `
    'HeadBob diagnostic-only launch profile'
Assert-CondemnedVrHeadBobDiagnosticProfile `
    -LaunchProfile $headBobDiagnostic -HeadBobDiagnostic $true

$minimalHeadBobDiagnosticRejected = $false
try {
    Assert-CondemnedVrHeadBobDiagnosticProfile `
        -LaunchProfile $minimal -HeadBobDiagnostic $true
} catch {
    $minimalHeadBobDiagnosticRejected =
        $_.Exception.Message -like '*cannot be combined*'
}
Assert-Equal $true $minimalHeadBobDiagnosticRejected `
    'minimal HeadBob diagnostic conflict rejection'

Write-Host 'Condemned launch-profile tests passed.' -ForegroundColor Green
