<#
.SYNOPSIS
    Captures the verified Condemned game-window client area to a PNG.

.DESCRIPTION
    Resolves one live Condemned.exe process (or an explicit PID), verifies
    that its root-owner window belongs to that PID, and captures only the
    visible client area. By default the exact game root-owner window must own
    the foreground; the tool fails instead of focusing or moving any window.

    The PNG and a JSON sidecar are written to the newest project-local live
    run directory unless -OutputPath is supplied. The sidecar records the
    process/window identity, screen rectangle, foreground state, and SHA-256.

.PARAMETER TargetProcessId
    Optional exact Condemned.exe PID. When omitted, exactly one live
    Condemned process must exist.

.PARAMETER OutputPath
    Optional PNG path. Relative paths are resolved from the current directory.

.PARAMETER DelayMilliseconds
    Optional bounded delay after window validation and before capture.

.PARAMETER AllowBackgroundWindow
    Allows capture when Condemned is not the foreground root-owner window.
    CopyFromScreen can then capture an occluding window, so this is intended
    only for controlled diagnostics.

.PARAMETER ValidateOnly
    Compiles the bounded Win32 capture helper and exits without requiring a
    game process or writing a screenshot.
#>
[CmdletBinding()]
param(
    [ValidateRange(0, 2147483647)]
    [int]$TargetProcessId = 0,

    [string]$OutputPath,

    [ValidateRange(0, 10000)]
    [int]$DelayMilliseconds = 0,

    [switch]$AllowBackgroundWindow,

    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

if (-not ('CondemnedVrWindowCaptureNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public sealed class CondemnedVrWindowCaptureTarget {
    public IntPtr Window { get; internal set; }
    public uint ProcessId { get; internal set; }
    public bool Foreground { get; internal set; }
    public uint ForegroundProcessId { get; internal set; }
    public int X { get; internal set; }
    public int Y { get; internal set; }
    public int Width { get; internal set; }
    public int Height { get; internal set; }
    public string Detail { get; internal set; }
}

public static class CondemnedVrWindowCaptureNative {
    private const uint GaRootOwner = 3;

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Point {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsIconic(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr GetAncestor(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(
        IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetClientRect(IntPtr window, out Rect rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClientToScreen(IntPtr window, ref Point point);

    public static CondemnedVrWindowCaptureTarget Inspect(
        IntPtr window, uint expectedProcessId) {
        CondemnedVrWindowCaptureTarget result =
            new CondemnedVrWindowCaptureTarget();
        result.Detail = "invalid_target_or_window";
        if (window == IntPtr.Zero || expectedProcessId == 0 ||
            !IsWindow(window)) {
            return result;
        }

        IntPtr root = GetAncestor(window, GaRootOwner);
        if (root == IntPtr.Zero) {
            root = window;
        }
        uint processId;
        GetWindowThreadProcessId(root, out processId);
        result.Window = root;
        result.ProcessId = processId;
        if (processId != expectedProcessId) {
            result.Detail = "root_owner_identity_mismatch";
            return result;
        }
        if (!IsWindowVisible(root)) {
            result.Detail = "window_not_visible";
            return result;
        }
        if (IsIconic(root)) {
            result.Detail = "window_minimized";
            return result;
        }

        Rect client;
        Point origin = new Point();
        if (!GetClientRect(root, out client) ||
            !ClientToScreen(root, ref origin)) {
            result.Detail = "client_rectangle_unavailable";
            return result;
        }
        int width = client.Right - client.Left;
        int height = client.Bottom - client.Top;
        if (width <= 0 || height <= 0) {
            result.Detail = "empty_client_rectangle";
            return result;
        }

        IntPtr foreground = GetForegroundWindow();
        IntPtr foregroundRoot = GetAncestor(foreground, GaRootOwner);
        if (foregroundRoot == IntPtr.Zero) {
            foregroundRoot = foreground;
        }
        uint foregroundProcessId;
        GetWindowThreadProcessId(foregroundRoot, out foregroundProcessId);

        result.ForegroundProcessId = foregroundProcessId;
        result.Foreground = foregroundRoot == root &&
            foregroundProcessId == expectedProcessId;
        result.X = origin.X;
        result.Y = origin.Y;
        result.Width = width;
        result.Height = height;
        result.Detail = "ok";
        return result;
    }
}
'@
}

if ($ValidateOnly) {
    Write-Host 'Condemned window capture helper validation passed.' `
        -ForegroundColor Green
    return
}

$processes = if ($TargetProcessId -gt 0) {
    @(Get-Process -Id $TargetProcessId -ErrorAction Stop)
} else {
    @(Get-Process -Name 'Condemned' -ErrorAction SilentlyContinue)
}

if ($processes.Count -ne 1) {
    throw (
        'Expected exactly one live Condemned process; found {0}. ' +
        'Pass -TargetProcessId to disambiguate.' -f $processes.Count)
}

$gameProcess = $processes[0]
if ($gameProcess.ProcessName -ne 'Condemned' -or $gameProcess.HasExited) {
    throw "PID $($gameProcess.Id) is not a live Condemned process."
}
$gameProcess.Refresh()
$window = $gameProcess.MainWindowHandle
$target = [CondemnedVrWindowCaptureNative]::Inspect(
    $window, [uint32]$gameProcess.Id)
if ($target.Detail -ne 'ok') {
    throw (
        'Condemned window validation failed: {0} ' +
        '(PID={1}, HWND=0x{2:X}).' -f
        $target.Detail, $gameProcess.Id, $window.ToInt64())
}
if (-not $target.Foreground -and -not $AllowBackgroundWindow) {
    throw (
        'Condemned is not the foreground root-owner window ' +
        '(game PID={0}, foreground PID={1}).' -f
        $gameProcess.Id, $target.ForegroundProcessId)
}

if ($DelayMilliseconds -gt 0) {
    Start-Sleep -Milliseconds $DelayMilliseconds
    $gameProcess.Refresh()
    if ($gameProcess.HasExited) {
        throw 'Condemned exited before capture.'
    }
    $target = [CondemnedVrWindowCaptureNative]::Inspect(
        $gameProcess.MainWindowHandle, [uint32]$gameProcess.Id)
    if ($target.Detail -ne 'ok' -or
        (-not $target.Foreground -and -not $AllowBackgroundWindow)) {
        throw 'Condemned window state changed before capture.'
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $logsRoot = Join-Path $repositoryRoot 'stage\condemned-m2-mono\logs'
    $runDirectory = Get-ChildItem -LiteralPath $logsRoot -Directory `
        -Filter 'run-*' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -eq $runDirectory) {
        $captureDirectory = Join-Path $logsRoot 'manual-captures'
    } else {
        $captureDirectory = $runDirectory.FullName
    }
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff')
    $OutputPath = Join-Path $captureDirectory (
        "condemned-window-$stamp.png")
} elseif (-not [IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = [IO.Path]::GetFullPath((
        Join-Path (Get-Location).Path $OutputPath))
} else {
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)
}

if ([IO.Path]::GetExtension($OutputPath) -ne '.png') {
    throw '-OutputPath must use a .png extension.'
}
$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$bitmap = New-Object System.Drawing.Bitmap(
    $target.Width, $target.Height,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen(
        $target.X, $target.Y, 0, 0,
        ([System.Drawing.Size]::new($target.Width, $target.Height)),
        [System.Drawing.CopyPixelOperation]::SourceCopy)
    $bitmap.Save(
        $OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

$imageHash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash
$metadataPath = [IO.Path]::ChangeExtension($OutputPath, '.json')
$metadata = [pscustomobject][ordered]@{
    SchemaVersion = 1
    CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    ProcessId = [int]$gameProcess.Id
    ProcessName = $gameProcess.ProcessName
    Window = ('0x{0:X}' -f $target.Window.ToInt64())
    WindowTitle = $gameProcess.MainWindowTitle
    Foreground = [bool]$target.Foreground
    ForegroundProcessId = [uint32]$target.ForegroundProcessId
    ClientRectangle = [pscustomobject][ordered]@{
        X = $target.X
        Y = $target.Y
        Width = $target.Width
        Height = $target.Height
    }
    ImagePath = $OutputPath
    ImageSha256 = $imageHash
}
[IO.File]::WriteAllText(
    $metadataPath,
    ($metadata | ConvertTo-Json -Depth 4),
    (New-Object Text.UTF8Encoding($false)))

Write-Host "Condemned window captured: $OutputPath" -ForegroundColor Green
Write-Host "Metadata: $metadataPath" -ForegroundColor Cyan
$metadata
