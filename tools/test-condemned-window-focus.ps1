<#
.SYNOPSIS
    Headset-free tests for the bounded launcher focus handoff.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-window-focus.ps1"

function Assert-Equal($Expected, $Actual, [string]$Label) {
    if ($Expected -ne $Actual) {
        throw "$Label expected '$Expected', got '$Actual'."
    }
}

function Assert-True([bool]$Condition, [string]$Label) {
    if (-not $Condition) {
        throw $Label
    }
}

function New-FakeProcess(
    [int]$Id,
    [IntPtr]$Window,
    [bool]$HasExited = $false) {
    $process = [pscustomobject]@{
        Id = $Id
        MainWindowHandle = $Window
        HasExited = $HasExited
    }
    $process | Add-Member -MemberType ScriptMethod -Name Refresh -Value {}
    return $process
}

function New-NativeResult(
    [bool]$Focused,
    [bool]$AttachedInputAttempted,
    [string]$Detail) {
    $finalForegroundProcessId = if ($Focused) { 4242 } else { 7 }
    return [pscustomobject]@{
        Focused = $Focused
        StandardRequestAccepted = $Focused
        AttachedInputAttempted = $AttachedInputAttempted
        AttachedInputSucceeded = $AttachedInputAttempted
        AttachedInputReleased = $true
        FinalForegroundProcessId = [uint32]$finalForegroundProcessId
        Detail = $Detail
    }
}

$missing = Set-CondemnedVrForegroundWindow $null
Assert-Equal $false $missing.Focused 'missing process focus'
Assert-Equal 0 $missing.Attempts 'missing process attempts'
Assert-Equal 'process_missing' $missing.Detail 'missing process detail'

$invalid = Set-CondemnedVrForegroundWindow ([pscustomobject]@{})
Assert-Equal $false $invalid.Focused 'invalid process focus'
Assert-Equal 'process_invalid' $invalid.Detail 'invalid process detail'

$exited = Set-CondemnedVrForegroundWindow (
    New-FakeProcess 4242 ([IntPtr]123) $true)
Assert-Equal $false $exited.Focused 'exited process focus'
Assert-Equal 0 $exited.Attempts 'exited process attempts'
Assert-Equal 'process_exited' $exited.Detail 'exited process detail'

$windowless = Set-CondemnedVrForegroundWindow (
    New-FakeProcess 4242 ([IntPtr]::Zero)) -AllowAttachedInput
Assert-Equal $false $windowless.Focused 'windowless process focus'
Assert-Equal 0 $windowless.Attempts 'windowless process attempts'
Assert-Equal $false $windowless.AttachedInputAttempted (
    'windowless attached-input attempt')
Assert-Equal $true $windowless.AttachedInputReleased (
    'windowless attached-input release invariant')
Assert-Equal 'window_unavailable' $windowless.Detail (
    'windowless process detail')

$script:standardCalls = 0
function Invoke-CondemnedVrNativeFocus(
    [IntPtr]$Window,
    [uint32]$ExpectedProcessId,
    [bool]$AllowAttachedInput) {
    ++$script:standardCalls
    if ($AllowAttachedInput) {
        throw 'Attached-input fallback should not be needed.'
    }
    if ($script:standardCalls -ge 3) {
        return New-NativeResult $true $false 'standard_focus_verified'
    }
    return New-NativeResult $false $false 'standard_focus_refused'
}

$retried = Set-CondemnedVrForegroundWindow (
    New-FakeProcess 4242 ([IntPtr]123)) -TimeoutMilliseconds 250
Assert-Equal $true $retried.Focused 'retried focus'
Assert-Equal 3 $retried.Attempts 'standard retry attempts'
Assert-Equal $false $retried.AttachedInputAttempted (
    'standard retry attached-input attempt')
Assert-Equal 'standard_focus_verified' $retried.Detail (
    'standard retry detail')

$script:fallbackCalls = 0
function Invoke-CondemnedVrNativeFocus(
    [IntPtr]$Window,
    [uint32]$ExpectedProcessId,
    [bool]$AllowAttachedInput) {
    ++$script:fallbackCalls
    if ($AllowAttachedInput) {
        return New-NativeResult $true $true 'attached_focus_verified'
    }
    return New-NativeResult $false $false 'standard_focus_refused'
}

$fallback = Set-CondemnedVrForegroundWindow (
    New-FakeProcess 4242 ([IntPtr]123)) `
    -TimeoutMilliseconds 0 `
    -AllowAttachedInput
Assert-Equal $true $fallback.Focused 'attached-input fallback focus'
Assert-Equal 2 $fallback.Attempts 'attached-input fallback attempts'
Assert-Equal $true $fallback.AttachedInputAttempted (
    'attached-input fallback attempted')
Assert-Equal $true $fallback.AttachedInputSucceeded (
    'attached-input fallback succeeded')
Assert-Equal $true $fallback.AttachedInputReleased (
    'attached-input fallback released')
Assert-Equal 'attached_focus_verified' $fallback.Detail (
    'attached-input fallback detail')

$nativeInvalid = [CondemnedVrLauncherFocus]::Focus(
    [IntPtr]::Zero, 4242, $true)
Assert-Equal $false $nativeInvalid.Focused 'native invalid-window focus'
Assert-Equal $false $nativeInvalid.AttachedInputAttempted (
    'native invalid-window attached-input attempt')
Assert-Equal 'invalid_target_or_window' $nativeInvalid.Detail (
    'native invalid-window detail')

$launcherPath = Join-Path $PSScriptRoot 'launch-condemned-m2-vr.ps1'
$launcherText = Get-Content -Raw -LiteralPath $launcherPath
$finalHandoffIndex = $launcherText.IndexOf(
    '$finalGameFocus = Set-CondemnedVrForegroundWindow')
$legacyResultIndex = $launcherText.IndexOf(
    '$report.GameWindowFocusRestored = [bool]$finalGameFocus.Focused')
$reportWrites = [regex]::Matches(
    $launcherText,
    '\[IO\.File\]::WriteAllText\(\s*\$reportPath,')
Assert-True ($finalHandoffIndex -ge 0) 'final focus handoff is absent'
Assert-True ($legacyResultIndex -gt $finalHandoffIndex) (
    'legacy focus result is not derived from the final handoff')
Assert-Equal 1 $reportWrites.Count 'live report write count'
Assert-True ($reportWrites[0].Index -gt $legacyResultIndex) (
    'live report is written before the final focus result')

Write-Host 'Condemned launcher focus tests passed.' -ForegroundColor Green
