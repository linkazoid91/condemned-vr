<#
.SYNOPSIS
    Verifies and starts an installed Condemned VR release.

.PARAMETER Runtime
    active, steamvr, vdxr, or a path to an OpenXR runtime manifest.

.PARAMETER Minimal
    Starts the guarded bare-transport fallback instead of Current.

.PARAMETER RetailHeadBob
    Diagnostic A/B rollback that restores Retail locomotion bob.

.PARAMETER VerifyOnly
    Verifies the complete installation without starting OpenXR or the game.
#>
[CmdletBinding()]
param(
    [string]$InstallDir,
    [string]$Runtime = 'active',
    [switch]$Minimal,
    [switch]$RetailHeadBob,
    [switch]$Wait,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_condemnedvr-release.ps1')
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $selfRoot = Get-NormalizedDirectoryPath (Join-Path $PSScriptRoot '..')
    $selfManifest = Join-Path $selfRoot 'condemnedvr-install.json'
    if (Test-Path -LiteralPath $selfManifest -PathType Leaf) {
        $InstallDir = $selfRoot
    } else {
        $InstallDir = Join-Path $env:USERPROFILE 'CondemnedVR'
    }
}
$InstallDir = Get-NormalizedDirectoryPath $InstallDir
$verified = Test-CondemnedVrInstalledDeployment $InstallDir

Write-Host '=== Condemned VR ===' -ForegroundColor Cyan
Write-Host ('Version: {0}' -f $verified.Install.packageVersion)
Write-Host ('Install: {0}' -f $InstallDir)
Write-Host ('Retail:  {0}' -f $verified.Retail.RetailRoot)

if ($VerifyOnly) {
    Write-Host 'Installation integrity verified.' -ForegroundColor Green
    return
}

$runtimeInfo = Resolve-CondemnedVrOpenXrRuntime $Runtime
$runtimeName = if ([string]::IsNullOrWhiteSpace($runtimeInfo.Name)) {
    'unknown runtime'
} else {
    $runtimeInfo.Name
}
Write-Host ('OpenXR: {0}' -f $runtimeName)

$launcher = Join-Path $InstallDir 'tools\launch-condemned-m2-vr.ps1'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw ('The installed guarded launcher is missing: {0}' -f $launcher)
}
$launchArguments = @{}
if ($runtimeInfo.Override) {
    $launchArguments.RuntimeManifest = $runtimeInfo.Manifest
}
if ($Minimal) {
    $launchArguments.Minimal = $true
}
if ($RetailHeadBob) {
    $launchArguments.RetailHeadBob = $true
}
if ($Wait) {
    $launchArguments.Wait = $true
}
& $launcher @launchArguments
