<#
.SYNOPSIS
    Captures module and window evidence from a running Condemned M0 test.

.PARAMETER Mode
    stock-no-asi verifies the isolated project-local runtime, system D3D9, and
    absence of ASI modules. widescreen-reference records the retail reference
    run without applying the stock assertions.
#>
[CmdletBinding()]
param(
    [ValidateSet('stock-no-asi', 'widescreen-reference')]
    [string]$Mode = 'stock-no-asi',
    [int]$ProcessId = 0
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

if ($ProcessId -eq 0) {
    $process = Get-Process -Name 'Condemned' -ErrorAction Stop |
        Select-Object -First 1
} else {
    $process = Get-Process -Id $ProcessId -ErrorAction Stop
}
$process.Refresh()

$inspectorPowerShell = Join-Path $env:WINDIR (
    'SysWOW64\WindowsPowerShell\v1.0\powershell.exe')
$inspectorScript = Join-Path $PSScriptRoot 'inspect-condemned-process32.ps1'
$inspectorJson = & $inspectorPowerShell `
    -NoProfile `
    -ExecutionPolicy Bypass `
    -File $inspectorScript `
    -ProcessId $process.Id
if ($LASTEXITCODE -ne 0) {
    throw "32-bit module inspector exited with code $LASTEXITCODE."
}
$moduleInspection = $inspectorJson -join [Environment]::NewLine |
    ConvertFrom-Json
$modules = @($moduleInspection.Modules)
$d3d9 = $modules |
    Where-Object { $_.Name -ieq 'd3d9.dll' } |
    Select-Object -First 1
$asiModules = @($modules | Where-Object { $_.Name -like '*.asi' })
if ($null -eq $d3d9) {
    throw 'The running process has no loaded d3d9.dll.'
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CondemnedM0LiveWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
}
'@

$window = $null
$handle = $process.MainWindowHandle
if ($handle -ne 0) {
    $styleValue = [CondemnedM0LiveWindow]::GetWindowLong($handle, -16)
    $styleBits = [BitConverter]::ToUInt32(
        [BitConverter]::GetBytes([int32]$styleValue), 0)
    $popupStyleMask = [Convert]::ToUInt32('80000000', 16)
    $minimizeStyleMask = [Convert]::ToUInt32('20000000', 16)
    $rect = New-Object CondemnedM0LiveWindow+RECT
    if ([CondemnedM0LiveWindow]::GetWindowRect($handle, [ref]$rect)) {
        $window = [pscustomobject][ordered]@{
            Handle = '0x{0:X}' -f $handle.ToInt64()
            Title = $process.MainWindowTitle
            Style = '0x{0:X8}' -f $styleBits
            Left = $rect.Left
            Top = $rect.Top
            Width = $rect.Right - $rect.Left
            Height = $rect.Bottom - $rect.Top
            HasCaption = ($styleValue -band 0x00C00000) -ne 0
            HasThickFrame = ($styleValue -band 0x00040000) -ne 0
            HasPopupStyle = ($styleBits -band $popupStyleMask) -ne 0
            IsMinimized = ($styleBits -band $minimizeStyleMask) -ne 0
        }
    }
}

$actualD3d9 = [IO.Path]::GetFullPath([string]$d3d9.Path)
$systemD3d9Paths = @(
    [IO.Path]::GetFullPath((Join-Path $env:WINDIR 'System32\d3d9.dll')),
    [IO.Path]::GetFullPath((Join-Path $env:WINDIR 'SysWOW64\d3d9.dll'))
)
$systemD3d9Loaded = @($systemD3d9Paths | Where-Object {
    $actualD3d9.Equals($_, [StringComparison]::OrdinalIgnoreCase)
}).Count -gt 0

$report = [pscustomobject][ordered]@{
    SchemaVersion = 1
    CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    Mode = $Mode
    ProcessId = $process.Id
    RuntimeExe = $process.Path
    D3d9 = [pscustomobject][ordered]@{
        Path = $actualD3d9
        AcceptedSystemPaths = $systemD3d9Paths
        IsSystemD3d9 = $systemD3d9Loaded
    }
    AsiModules = $asiModules
    StockModules = @($modules | Where-Object {
        $_.Name -in @('GameClient.dll', 'GameServer.dll', 'ClientFx.fxd')
    })
    Window = $window
}

$reportPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'stage\condemned-m0\{0}-live-{1}.json' -f
        $Mode,
        (Get-Date -Format 'yyyyMMdd-HHmmss')))
[IO.File]::WriteAllText(
    $reportPath,
    ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host "Runtime: $($report.RuntimeExe)"
Write-Host "D3D9:    $actualD3d9"
Write-Host "ASI:     $($asiModules.Count) loaded"
if ($null -ne $window) {
    $windowSummary = ("Window:  {0}x{1} at {2},{3}; style {4}; " +
        "caption={5}, thickframe={6}, popup={7}, minimized={8}") -f
        $window.Width,
        $window.Height,
        $window.Left,
        $window.Top,
        $window.Style,
        $window.HasCaption,
        $window.HasThickFrame,
        $window.HasPopupStyle,
        $window.IsMinimized
    Write-Host $windowSummary
}
Write-Host "Report:  $reportPath"

if ($Mode -eq 'stock-no-asi') {
    $expectedStage = [IO.Path]::GetFullPath(
        (Join-Path $cfg.ProjectRoot 'stage\condemned-m0\stock-no-asi'))
    $actualRuntime = [IO.Path]::GetFullPath($process.Path)
    $stagePrefix = $expectedStage.TrimEnd('\') + '\'
    if (-not $actualRuntime.StartsWith(
            $stagePrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Stock test is not running from the isolated stage: $actualRuntime"
    }
    if (-not $systemD3d9Loaded) {
        throw "Stock test loaded a non-system d3d9.dll: $actualD3d9"
    }
    if ($asiModules.Count -ne 0) {
        throw 'Stock test unexpectedly loaded one or more ASI modules.'
    }
    Write-Host 'Stock/no-ASI live inspection passed.' -ForegroundColor Green
}
