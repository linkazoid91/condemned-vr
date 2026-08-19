<#
.SYNOPSIS
    Bounded Win32 foreground-window handoff used by the VR launcher.

.DESCRIPTION
    Windows may reject SetForegroundWindow after the launcher has waited for
    OpenXR and game readiness. The normal request is always tried first. At an
    explicit final launch handoff, callers may opt into one AttachThreadInput
    fallback that temporarily joins the launcher, current foreground, and game
    input queues. All attachments are released before this helper returns.
#>

if (-not ('CondemnedVrLauncherFocus' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public sealed class CondemnedVrNativeFocusResult {
    public bool Focused { get; internal set; }
    public bool StandardRequestAccepted { get; internal set; }
    public bool AttachedInputAttempted { get; internal set; }
    public bool AttachedInputSucceeded { get; internal set; }
    public bool AttachedInputReleased { get; internal set; }
    public uint FinalForegroundProcessId { get; internal set; }
    public string Detail { get; internal set; }
}

public static class CondemnedVrLauncherFocus {
    private const int SwRestore = 9;
    private const uint GaRootOwner = 3;
    private const uint PmNoRemove = 0;

    [StructLayout(LayoutKind.Sequential)]
    private struct Point {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Message {
        public IntPtr Window;
        public uint Value;
        public UIntPtr WParam;
        public IntPtr LParam;
        public uint Time;
        public Point Cursor;
        public uint Private;
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsIconic(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShowWindowAsync(IntPtr window, int command);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool BringWindowToTop(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr SetActiveWindow(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr SetFocus(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern IntPtr GetAncestor(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(
        IntPtr window, out uint processId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AttachThreadInput(
        uint attachThreadId, uint attachToThreadId,
        [MarshalAs(UnmanagedType.Bool)] bool attach);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PeekMessage(
        out Message message, IntPtr window, uint minimum, uint maximum,
        uint removeMessage);

    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    private static uint ForegroundProcessId() {
        uint processId;
        GetWindowThreadProcessId(GetForegroundWindow(), out processId);
        return processId;
    }

    private static bool IsTargetForeground(
        IntPtr targetWindow, uint expectedProcessId) {
        IntPtr foregroundWindow = GetForegroundWindow();
        uint foregroundProcessId;
        GetWindowThreadProcessId(foregroundWindow, out foregroundProcessId);
        if (foregroundWindow == IntPtr.Zero ||
            foregroundProcessId != expectedProcessId) {
            return false;
        }

        IntPtr targetRoot = GetAncestor(targetWindow, GaRootOwner);
        IntPtr foregroundRoot = GetAncestor(foregroundWindow, GaRootOwner);
        return targetRoot != IntPtr.Zero && targetRoot == foregroundRoot;
    }

    public static CondemnedVrNativeFocusResult Focus(
        IntPtr window, uint expectedProcessId, bool allowAttachedInput) {
        CondemnedVrNativeFocusResult result =
            new CondemnedVrNativeFocusResult();
        result.Detail = "invalid_target_or_window";
        result.AttachedInputReleased = true;
        result.FinalForegroundProcessId = ForegroundProcessId();
        if (window == IntPtr.Zero || expectedProcessId == 0 ||
            !IsWindow(window)) {
            return result;
        }

        uint targetProcessId;
        uint targetThreadId =
            GetWindowThreadProcessId(window, out targetProcessId);
        if (targetThreadId == 0 || targetProcessId != expectedProcessId) {
            result.Detail = "target_identity_mismatch";
            return result;
        }

        if (IsTargetForeground(window, expectedProcessId)) {
            result.Focused = true;
            result.FinalForegroundProcessId = expectedProcessId;
            result.Detail = "already_foreground";
            return result;
        }

        if (IsIconic(window)) {
            ShowWindowAsync(window, SwRestore);
        }
        result.StandardRequestAccepted = SetForegroundWindow(window);
        result.FinalForegroundProcessId = ForegroundProcessId();
        if (IsTargetForeground(window, expectedProcessId)) {
            result.Focused = true;
            result.FinalForegroundProcessId = expectedProcessId;
            result.Detail = "standard_focus_verified";
            return result;
        }
        if (!allowAttachedInput) {
            result.Detail = result.StandardRequestAccepted ?
                "standard_focus_not_observed" : "standard_focus_refused";
            return result;
        }

        result.AttachedInputAttempted = true;
        IntPtr foregroundWindow = GetForegroundWindow();
        uint foregroundProcessId;
        uint foregroundThreadId = GetWindowThreadProcessId(
            foregroundWindow, out foregroundProcessId);
        uint currentThreadId = GetCurrentThreadId();

        Message unusedMessage;
        PeekMessage(
            out unusedMessage, IntPtr.Zero, 0, 0, PmNoRemove);

        bool foregroundAttachmentNeeded =
            foregroundThreadId != 0 && foregroundThreadId != currentThreadId;
        bool targetAttachmentNeeded =
            targetThreadId != currentThreadId &&
            targetThreadId != foregroundThreadId;
        bool foregroundAttached = false;
        bool targetAttached = false;
        bool foregroundReleased = true;
        bool targetReleased = true;
        try {
            if (foregroundAttachmentNeeded) {
                foregroundAttached = AttachThreadInput(
                    currentThreadId, foregroundThreadId, true);
            }
            if (targetAttachmentNeeded) {
                targetAttached = AttachThreadInput(
                    currentThreadId, targetThreadId, true);
            }
            result.AttachedInputSucceeded =
                (!foregroundAttachmentNeeded || foregroundAttached) &&
                (!targetAttachmentNeeded || targetAttached);

            if (IsIconic(window)) {
                ShowWindowAsync(window, SwRestore);
            }
            BringWindowToTop(window);
            SetForegroundWindow(window);
            SetActiveWindow(window);
            SetFocus(window);
        } finally {
            if (targetAttached) {
                targetReleased = AttachThreadInput(
                    currentThreadId, targetThreadId, false);
            }
            if (foregroundAttached) {
                foregroundReleased = AttachThreadInput(
                    currentThreadId, foregroundThreadId, false);
            }
            result.AttachedInputReleased =
                targetReleased && foregroundReleased;
        }

        result.FinalForegroundProcessId = ForegroundProcessId();
        result.Focused =
            result.AttachedInputReleased &&
            IsTargetForeground(window, expectedProcessId);
        if (!result.AttachedInputReleased) {
            result.Detail = "input_queue_detach_failed";
        } else if (result.Focused) {
            result.FinalForegroundProcessId = expectedProcessId;
            result.Detail = "attached_focus_verified";
        } else if (!result.AttachedInputSucceeded) {
            result.Detail = "input_queue_attach_failed";
        } else {
            result.Detail = "attached_focus_not_observed";
        }
        return result;
    }
}
'@
}

function New-CondemnedVrFocusResult(
    [uint32]$TargetProcessId,
    [string]$Detail) {
    return [pscustomobject][ordered]@{
        Focused = $false
        Attempts = 0
        TargetProcessId = $TargetProcessId
        TargetWindow = '0x0'
        StandardRequestAccepted = $false
        AttachedInputAttempted = $false
        AttachedInputSucceeded = $false
        AttachedInputReleased = $true
        FinalForegroundProcessId = [uint32]0
        Detail = $Detail
    }
}

function ConvertFrom-CondemnedVrNativeFocusResult(
    [uint32]$TargetProcessId,
    [IntPtr]$TargetWindow,
    [int]$Attempts,
    $NativeResult) {
    return [pscustomobject][ordered]@{
        Focused = [bool]$NativeResult.Focused
        Attempts = $Attempts
        TargetProcessId = $TargetProcessId
        TargetWindow = '0x{0:X}' -f $TargetWindow.ToInt64()
        StandardRequestAccepted =
            [bool]$NativeResult.StandardRequestAccepted
        AttachedInputAttempted =
            [bool]$NativeResult.AttachedInputAttempted
        AttachedInputSucceeded =
            [bool]$NativeResult.AttachedInputSucceeded
        AttachedInputReleased =
            [bool]$NativeResult.AttachedInputReleased
        FinalForegroundProcessId =
            [uint32]$NativeResult.FinalForegroundProcessId
        Detail = [string]$NativeResult.Detail
    }
}

function Invoke-CondemnedVrNativeFocus(
    [IntPtr]$Window,
    [uint32]$ExpectedProcessId,
    [bool]$AllowAttachedInput) {
    return [CondemnedVrLauncherFocus]::Focus(
        $Window, $ExpectedProcessId, $AllowAttachedInput)
}

function Set-CondemnedVrForegroundWindow(
    $Process,
    [ValidateRange(0, 5000)][int]$TimeoutMilliseconds = 0,
    [switch]$AllowAttachedInput) {
    if ($null -eq $Process) {
        return New-CondemnedVrFocusResult 0 'process_missing'
    }

    try {
        $targetProcessId = [uint32]$Process.Id
    } catch {
        return New-CondemnedVrFocusResult 0 'process_invalid'
    }
    if ($targetProcessId -eq 0) {
        return New-CondemnedVrFocusResult 0 'process_invalid'
    }
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    $attempts = 0
    $window = [IntPtr]::Zero
    $lastResult = New-CondemnedVrFocusResult (
        $targetProcessId) 'window_unavailable'
    try {
        do {
            $Process.Refresh()
            if ($Process.HasExited) {
                return New-CondemnedVrFocusResult (
                    $targetProcessId) 'process_exited'
            }

            $window = $Process.MainWindowHandle
            if ($window -ne [IntPtr]::Zero) {
                $nativeResult = Invoke-CondemnedVrNativeFocus `
                    $window $targetProcessId $false
                ++$attempts
                $lastResult = ConvertFrom-CondemnedVrNativeFocusResult `
                    $targetProcessId $window $attempts $nativeResult
                if ($lastResult.Focused) {
                    return $lastResult
                }
            }

            if ([DateTime]::UtcNow -ge $deadline) {
                break
            }
            Start-Sleep -Milliseconds 50
        } while ($true)

        if ($AllowAttachedInput -and $window -ne [IntPtr]::Zero) {
            $nativeResult = Invoke-CondemnedVrNativeFocus `
                $window $targetProcessId $true
            ++$attempts
            return ConvertFrom-CondemnedVrNativeFocusResult `
                $targetProcessId $window $attempts $nativeResult
        }
        return $lastResult
    } catch {
        $lastResult.Detail = 'exception: ' + $_.Exception.Message
        return $lastResult
    }
}
