<#
.SYNOPSIS
    Launches and inspects the isolated Condemned M0 stage without an ASI loader.

.DESCRIPTION
    Validates every staged file, starts the project-local Condemned.exe, waits
    for its renderer and main window, then records loaded D3D9/ASI modules and
    Win32 window flags. The game remains open for manual comparison unless
    -Wait is specified.

.PARAMETER ValidateOnly
    Validate the complete stage contract without launching the game.

.PARAMETER Wait
    Wait for Condemned to exit after writing the inspection report.
#>
[CmdletBinding()]
param(
    [switch]$ValidateOnly,
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot 'stage\condemned-m0\stock-no-asi-deployment.json')
if (-not (Test-Path -LiteralPath $deploymentPath -PathType Leaf)) {
    throw ('No stock/no-ASI deployment found. Run ' +
        'tools\prepare-condemned-m0-stock-stage.ps1 first.')
}

$deployment = Get-Content -Raw -LiteralPath $deploymentPath | ConvertFrom-Json
$stageRoot = Assert-UnderCondemnedVrProjectRoot $deployment.StageRoot
$runtimeExe = Assert-UnderCondemnedVrProjectRoot $deployment.RuntimeExe
$userDirectory = Assert-UnderCondemnedVrProjectRoot $deployment.UserDirectory
$archiveConfig = Assert-UnderCondemnedVrProjectRoot $deployment.ArchiveConfig

if (-not (Test-Path -LiteralPath $runtimeExe -PathType Leaf)) {
    throw "Staged Condemned.exe is missing: $runtimeExe"
}
if ((Get-FileSha256 $runtimeExe) -ne
    $cfg.CriticalFiles['Condemned.exe'].Sha256) {
    throw 'Staged Condemned.exe does not match the verified Steam build.'
}
if ((Get-FileSha256 $archiveConfig) -ne $deployment.ArchiveConfigSha256) {
    throw 'Staged default.archcfg changed after preparation.'
}
foreach ($record in $deployment.CopiedFiles) {
    $path = Join-Path $stageRoot $record.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileSha256 $path) -ne $record.Sha256) {
        throw "Staged root file changed after preparation: $($record.Name)"
    }
}
foreach ($forbidden in @('d3d9.dll', 'scripts')) {
    if (Test-Path -LiteralPath (Join-Path $stageRoot $forbidden)) {
        throw "Stock/no-ASI stage contains forbidden component: $forbidden"
    }
}

$gameJunction = Get-Item -LiteralPath $deployment.GameJunction.Path -Force
if (-not ($gameJunction.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Staged Game path is no longer a junction.'
}
foreach ($relativePath in @(
        'GameClient.dll',
        'GameServer.dll',
        'CondemnedA.Arch00',
        'CondemnedL.Arch00')) {
    if (-not (Test-Path -LiteralPath (
            Join-Path $deployment.GameJunction.Path $relativePath) -PathType Leaf)) {
        throw "Required stock Game file is missing: $relativePath"
    }
}

Write-Host 'Stock/no-ASI stage contract verified.' -ForegroundColor Green
Write-Host "Runtime: $runtimeExe"
Write-Host 'Local d3d9.dll: absent'
Write-Host 'Local scripts:  absent'
if ($ValidateOnly) {
    exit 0
}

$existing = @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    throw "Condemned.exe is already running (PID $($existing.Id -join ', '))."
}

$steam = Get-Process -Name 'steam' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $steam) {
    $steamExe = Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'
    if (-not (Test-Path -LiteralPath $steamExe -PathType Leaf)) {
        throw 'Steam is not running and steam.exe was not found.'
    }
    Start-Process -FilePath $steamExe -WorkingDirectory (
        Split-Path -Parent $steamExe) -WindowStyle Hidden | Out-Null
    $steamDeadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 250
        $steam = Get-Process -Name 'steam' -ErrorAction SilentlyContinue |
            Select-Object -First 1
    } until ($null -ne $steam -or (Get-Date) -ge $steamDeadline)
    if ($null -eq $steam) {
        throw 'Steam did not start within 20 seconds.'
    }
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CondemnedM0Window {
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

$env:SteamAppId = [string]$cfg.SteamAppId
$env:SteamGameId = [string]$cfg.SteamAppId
$arguments = @(
    '-archcfg',
    "`"$archiveConfig`"",
    '-userdirectory',
    "`"$userDirectory`""
)
$process = Start-Process -FilePath $runtimeExe `
    -ArgumentList $arguments `
    -WorkingDirectory $stageRoot `
    -PassThru
Write-Host "Condemned stock/no-ASI launch started (PID $($process.Id))."

$deadline = (Get-Date).AddSeconds(30)
$d3d9Module = $null
$asiModules = @()
$moduleReadError = $null
$inspectorPowerShell = Join-Path $env:WINDIR (
    'SysWOW64\WindowsPowerShell\v1.0\powershell.exe')
$inspectorScript = Join-Path $PSScriptRoot 'inspect-condemned-process32.ps1'
if (-not (Test-Path -LiteralPath $inspectorPowerShell -PathType Leaf)) {
    throw "32-bit Windows PowerShell is missing: $inspectorPowerShell"
}
if (-not (Test-Path -LiteralPath $inspectorScript -PathType Leaf)) {
    throw "32-bit module inspector is missing: $inspectorScript"
}
do {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    if ($process.HasExited) {
        throw ("Staged Condemned exited before inspection " +
            "(exit code $($process.ExitCode)).")
    }
    try {
        $inspectorJson = & $inspectorPowerShell `
            -NoProfile `
            -ExecutionPolicy Bypass `
            -File $inspectorScript `
            -ProcessId $process.Id
        if ($LASTEXITCODE -ne 0) {
            throw "32-bit inspector exited with code $LASTEXITCODE."
        }
        $loadedModules = @(
            ($inspectorJson -join [Environment]::NewLine |
                ConvertFrom-Json).Modules
        )
        $d3d9Module = $loadedModules |
            Where-Object { $_.Name -ieq 'd3d9.dll' } |
            Select-Object -First 1
        $asiModules = @($loadedModules |
            Where-Object { $_.Name -like '*.asi' })
        $moduleReadError = $null
    } catch {
        $moduleReadError = $_.Exception.Message
    }
} until (($null -ne $d3d9Module -and $process.MainWindowHandle -ne 0) -or
    (Get-Date) -ge $deadline)

if ($null -ne $moduleReadError) {
    throw "Could not inspect loaded modules: $moduleReadError"
}
if ($null -eq $d3d9Module) {
    throw 'The staged game did not load D3D9 within 30 seconds.'
}

$process.Refresh()
$windowHandle = $process.MainWindowHandle
$style = $null
$window = $null
if ($windowHandle -ne 0) {
    $styleValue = [CondemnedM0Window]::GetWindowLong($windowHandle, -16)
    $styleBits = [BitConverter]::ToUInt32(
        [BitConverter]::GetBytes([int32]$styleValue), 0)
    $popupStyleMask = [Convert]::ToUInt32('80000000', 16)
    $minimizeStyleMask = [Convert]::ToUInt32('20000000', 16)
    $rect = New-Object CondemnedM0Window+RECT
    if ([CondemnedM0Window]::GetWindowRect($windowHandle, [ref]$rect)) {
        $window = [pscustomobject][ordered]@{
            Handle = '0x{0:X}' -f $windowHandle.ToInt64()
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

$systemD3d9Paths = @(
    [IO.Path]::GetFullPath((Join-Path $env:WINDIR 'System32\d3d9.dll')),
    [IO.Path]::GetFullPath((Join-Path $env:WINDIR 'SysWOW64\d3d9.dll'))
)
$actualD3d9 = [IO.Path]::GetFullPath($d3d9Module.Path)
$systemD3d9Loaded = @($systemD3d9Paths | Where-Object {
    $actualD3d9.Equals($_, [StringComparison]::OrdinalIgnoreCase)
}).Count -gt 0
$inspection = [pscustomobject][ordered]@{
    SchemaVersion = 1
    CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    ProcessId = $process.Id
    RuntimeExe = $process.MainModule.FileName
    D3d9 = [pscustomobject][ordered]@{
        Path = $actualD3d9
        AcceptedSystemPaths = $systemD3d9Paths
        IsSystemD3d9 = $systemD3d9Loaded
    }
    AsiModules = @($asiModules | ForEach-Object {
        [pscustomobject][ordered]@{
            Name = $_.Name
            Path = $_.Path
        }
    })
    Window = $window
}
$reportPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'stage\condemned-m0\stock-no-asi-live-{0}.json' -f
        (Get-Date -Format 'yyyyMMdd-HHmmss')))
[IO.File]::WriteAllText(
    $reportPath,
    ($inspection | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    (New-Object Text.UTF8Encoding($false)))

Write-Host "D3D9:  $actualD3d9"
Write-Host "ASI:   $($asiModules.Count) loaded"
if ($null -ne $window) {
    $windowSummary = ("Window: {0}x{1} at {2},{3}; style {4}; " +
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
} else {
    Write-Host 'Window: no main window handle captured'
}
Write-Host "Report: $reportPath"

if (-not $systemD3d9Loaded) {
    throw "No-ASI launch loaded an unexpected d3d9.dll: $actualD3d9"
}
if ($asiModules.Count -ne 0) {
    throw 'No-ASI launch unexpectedly loaded one or more ASI modules.'
}

Write-Host 'No-ASI module inspection passed; game left running.' `
    -ForegroundColor Green
if ($Wait) {
    $process.WaitForExit()
    Write-Host "Condemned exited with code $($process.ExitCode)."
    exit $process.ExitCode
}
