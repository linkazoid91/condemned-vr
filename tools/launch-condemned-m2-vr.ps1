<#
.SYNOPSIS
    Launches the isolated Condemned M2 mono OpenXR transport.

.PARAMETER RuntimeManifest
    Optional OpenXR runtime JSON applied only to the new host process. When
    omitted, the system-wide active x64 OpenXR runtime is used.

.PARAMETER StartupImage
    Optional PNG/JPEG displayed in both eyes until Condemned publishes its
    first frame. When omitted, images\title.png is used if present.

.PARAMETER ValidateOnly
    Validates the host/runtime/session/swapchains without launching Condemned.

.PARAMETER DesktopWindow
    Runs Condemned in a smaller desktop window so other applications remain
    visible. The default window render size is 1920x1080. A verified Retail
    focus patch keeps VR rendering while another desktop window is foreground,
    while suppressing Condemned's cursor-centering outside the game.

.PARAMETER NoBackgroundRender
    Diagnostic rollback for DesktopWindow mode. Restores Retail's behavior of
    shutting down its renderer whenever Condemned loses foreground focus.

.PARAMETER TurningProbe
    Enables the separately guarded M4 right-stick turning gate.

.PARAMETER MenuProbe
    Enables the separately guarded M4 left-secondary pause-menu gate and
    routes verified non-gameplay states to the headset comfort panel.

.PARAMETER MenuControlsProbe
    Enables VR interaction with Retail menus: left stick navigates, A or the
    right trigger accepts, and B goes back. Requires -MenuProbe. Keyboard and
    mouse menu input remain available.

.PARAMETER InteractionProbe
    Enables the separately guarded M4 right-grip Activate command gate.

.PARAMETER CoreActionsProbe
    Enables the guarded M4 run, fire, block, weapon-toggle,
    ammo-check, stun-gun, and flashlight command gate.

.PARAMETER HapticsProbe
    Enables bounded M4 Fire, Block, and Activate confirmation pulses.
    Requires -CoreActionsProbe or -InteractionProbe.

.PARAMETER HeadAimProbe
    Suppresses mouse look while fresh focused HMD tracking is active, aims
    Retail fire from the right controller, and makes the flashlight follow
    head look.
    Requires -StereoTuning and -TurningProbe.

.PARAMETER AimPathProbe
    Enables observation-only M5 tracing for controller attack edges, the
    verified melee collision path, and the existing fire-vector path.
    Requires -HeadAimProbe and -CoreActionsProbe.

.PARAMETER MeleeAimProbe
    Redirects the verified animation-driven melee collision transform toward
    the right controller while preserving Retail's swing curve and timing.
    Requires -AimPathProbe for the initial live-validation gate.

.PARAMETER PhysicalMeleeProbe
    Enables controller world-pose, swept weapon endpoint, velocity, and
    impact-energy telemetry. For the verified fire-axe profile, a qualifying
    tracking-space swing also requests Retail's normal attack command; it does
    not directly apply a collision or damage. Requires -AimPathProbe.

.PARAMETER PhysicalMeleeWallProxy
    Moves Retail's verified melee collision body to the controller-driven
    weapon endpoint for wall-contact testing. Every native melee impact is
    blocked, so this gate cannot damage actors. Requires -PhysicalMeleeProbe
    and cannot be combined with -MeleeAimProbe.

.PARAMETER PhysicalMeleeVisualProxy
    Temporarily moves the equipped Retail melee model during stereo rendering
    so its profile-defined grip sits on the OpenXR right-controller grip and
    its weapon axis follows the controller aim pose. The verified current-
    weapon/model references are observed continuously, so the model starts in
    hand without an attack and automatically follows weapon switches. Retail's
    exact model transform is restored after both eyes. Requires
    -PhysicalMeleeWallProxy.

.PARAMETER WeaponGripCalibration
    Arms the Grip tab of the foreground-gated VR tool menu for the equipped
    weapon model. Both headset eyes update immediately, calibration is retained
    per equipped weapon for the current run, and the Snapshot row logs exact
    profile-ready values. A generic controller wireframe marks the OpenXR grip
    pose and aim direction while the Grip tab is open. F11 retains the legacy
    keyboard/controller calibration mode. Requires -PhysicalMeleeVisualProxy.

.PARAMETER TwoHandedMelee
    Enables profile-driven two-hand melee. The right hand remains the dominant
    weapon anchor; squeezing the left grip near the configured support point
    constrains the shaft direction without scaling the weapon. Release or
    tracking loss returns smoothly to weighted one-hand control. Requires
    -PhysicalMeleeVisualProxy.

.PARAMETER NoHidFpsFix
    Diagnostic rollback that leaves Condemned's redundant Jupiter EX
    HID/joystick initialization unmodified.

.PARAMETER NoXrFramePacing
    Diagnostic A/B rollback that allows multiple stereo renders to use the
    same OpenXR request again.

.PARAMETER PerformanceProbe
    Opens a separate live telemetry window showing game/XR frame rates,
    image age, reuse, per-eye/copy timing, and pipeline drops.

.PARAMETER RecenterProbe
    Enables release-gated right-stick recentering for tracked gameplay and
    the existing flat headset panel. Requires -StereoTuning.
#>
[CmdletBinding()]
param(
    [string]$RuntimeManifest,
    [string]$StartupImage,
    [switch]$ValidateOnly,
    [switch]$RendererProbe,
    [switch]$RendererPassThrough,
    [switch]$StereoDiagnostic,
    [switch]$DoubleRenderDiagnostic,
    [switch]$CameraReadProbe,
    [switch]$EyeOffsetDiagnostic,
    [switch]$ReverseEyeOffsetDiagnostic,
    [switch]$ZeroEyeOffsetDiagnostic,
    [switch]$StereoTuning,
    [switch]$LocomotionProbe,
    [switch]$TurningProbe,
    [switch]$MenuProbe,
    [switch]$MenuControlsProbe,
    [switch]$InteractionProbe,
    [switch]$CoreActionsProbe,
    [switch]$HapticsProbe,
    [switch]$HeadAimProbe,
    [switch]$AimPathProbe,
    [switch]$MeleeAimProbe,
    [switch]$PhysicalMeleeProbe,
    [switch]$PhysicalMeleeWallProxy,
    [switch]$PhysicalMeleeVisualProxy,
    [switch]$WeaponGripCalibration,
    [switch]$TwoHandedMelee,
    [switch]$NoHidFpsFix,
    [switch]$NoXrFramePacing,
    [switch]$PerformanceProbe,
    [switch]$RecenterProbe,
    [switch]$DesktopWindow,
    [switch]$NoBackgroundRender,
    [ValidateRange(640, 3840)]
    [int]$DesktopWindowWidth = 1920,
    [ValidateRange(480, 2160)]
    [int]$DesktopWindowHeight = 1080,
    [ValidateRange(100, 200)]
    [int]$RenderScale = 100,
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_condemnedvr-env.ps1"
$cfg = Get-CondemnedVrConfig

if ($PSBoundParameters.ContainsKey('StartupImage')) {
    $StartupImage = [IO.Path]::GetFullPath($StartupImage.Trim('"'))
    if (-not (Test-Path -LiteralPath $StartupImage -PathType Leaf)) {
        throw "Startup image not found: $StartupImage"
    }
} else {
    $defaultStartupImage = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $cfg.ProjectRoot 'images\title.png')
    if (Test-Path -LiteralPath $defaultStartupImage -PathType Leaf) {
        $StartupImage = $defaultStartupImage
    } else {
        $StartupImage = $null
    }
}

if ($RecenterProbe -and -not $StereoTuning) {
    throw '-RecenterProbe requires -StereoTuning.'
}
if ($MenuControlsProbe -and -not $MenuProbe) {
    throw '-MenuControlsProbe requires -MenuProbe.'
}
if ($HapticsProbe -and
    -not ($CoreActionsProbe -or $InteractionProbe)) {
    throw '-HapticsProbe requires -CoreActionsProbe or -InteractionProbe.'
}
if ($HeadAimProbe -and
    -not ($StereoTuning -and $TurningProbe)) {
    throw '-HeadAimProbe requires -StereoTuning and -TurningProbe.'
}
if ($AimPathProbe -and
    -not ($HeadAimProbe -and $CoreActionsProbe)) {
    throw '-AimPathProbe requires -HeadAimProbe and -CoreActionsProbe.'
}
if ($MeleeAimProbe -and -not $AimPathProbe) {
    throw '-MeleeAimProbe requires -AimPathProbe.'
}
if ($PhysicalMeleeProbe -and -not $AimPathProbe) {
    throw '-PhysicalMeleeProbe requires -AimPathProbe.'
}
if ($PhysicalMeleeWallProxy -and -not $PhysicalMeleeProbe) {
    throw '-PhysicalMeleeWallProxy requires -PhysicalMeleeProbe.'
}
if ($PhysicalMeleeWallProxy -and $MeleeAimProbe) {
    throw '-PhysicalMeleeWallProxy cannot be combined with -MeleeAimProbe.'
}
if ($PhysicalMeleeVisualProxy -and -not $PhysicalMeleeWallProxy) {
    throw '-PhysicalMeleeVisualProxy requires -PhysicalMeleeWallProxy.'
}
if ($WeaponGripCalibration -and -not $PhysicalMeleeVisualProxy) {
    throw '-WeaponGripCalibration requires -PhysicalMeleeVisualProxy.'
}
if ($TwoHandedMelee -and -not $PhysicalMeleeVisualProxy) {
    throw '-TwoHandedMelee requires -PhysicalMeleeVisualProxy.'
}
if ($NoBackgroundRender -and -not $DesktopWindow) {
    throw '-NoBackgroundRender requires -DesktopWindow.'
}
$backgroundRenderRequired =
    [bool]($DesktopWindow -and -not $NoBackgroundRender)

function Read-LiveLog([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    $stream = New-Object IO.FileStream(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
    try {
        return $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

if (-not ('CondemnedVrLauncherWindow' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CondemnedVrLauncherWindow {
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
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(
        IntPtr window, out uint processId);

    public static bool Focus(IntPtr window, uint expectedProcessId) {
        if (window == IntPtr.Zero || expectedProcessId == 0) {
            return false;
        }
        if (IsIconic(window)) {
            ShowWindowAsync(window, 9); // SW_RESTORE
        }
        SetForegroundWindow(window);
        uint foregroundProcessId;
        GetWindowThreadProcessId(
            GetForegroundWindow(), out foregroundProcessId);
        return foregroundProcessId == expectedProcessId;
    }
}
'@
}

function Set-CondemnedVrForegroundWindow(
    [Diagnostics.Process]$Process) {
    if ($null -eq $Process) {
        return $false
    }
    try {
        $Process.Refresh()
        if ($Process.HasExited -or $Process.MainWindowHandle -eq 0) {
            return $false
        }
        return [CondemnedVrLauncherWindow]::Focus(
            $Process.MainWindowHandle,
            [uint32]$Process.Id)
    } catch {
        return $false
    }
}
$deploymentPath = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $cfg.ProjectRoot (
        'stage\condemned-m2-mono\m2-mono-deployment.json'))
if (-not (Test-Path -LiteralPath $deploymentPath -PathType Leaf)) {
    throw 'No M2 mono deployment found. Run prepare-condemned-m2-mono-stage.ps1.'
}
$deployment = Get-Content -Raw -LiteralPath $deploymentPath | ConvertFrom-Json

if ((Get-FileSha256 $deployment.ArchiveConfig) -ne
    $deployment.ArchiveConfigSha256) {
    throw 'M2 mono archive configuration changed after preparation.'
}
if ((Get-FileSha256 $deployment.HostExe) -ne $deployment.HostSha256) {
    throw 'M2 mono host changed after preparation.'
}
foreach ($record in $deployment.Files) {
    $path = Join-Path $deployment.ModuleDirectory $record.Name
    if ((Get-FileSha256 $path) -ne $record.Sha256) {
        throw "M2 mono staged module changed: $($record.Name)"
    }
}
foreach ($forbidden in @('d3d9.dll', 'scripts')) {
    $path = Join-Path $deployment.WorkingDirectory $forbidden
    if (Test-Path -LiteralPath $path) {
        throw "M2 isolated runtime contains a forbidden ASI component: $path"
    }
}
if (-not [string]::IsNullOrWhiteSpace($RuntimeManifest)) {
    $RuntimeManifest = [IO.Path]::GetFullPath($RuntimeManifest.Trim('"'))
    if (-not (Test-Path -LiteralPath $RuntimeManifest -PathType Leaf)) {
        throw "OpenXR runtime manifest not found: $RuntimeManifest"
    }
}

$existingGame = @(Get-Process -Name Condemned -ErrorAction SilentlyContinue)
if ($existingGame.Count -gt 0) {
    throw "Condemned.exe is already running (PID $($existingGame.Id -join ', '))."
}
$existingHost = @(Get-Process -Name condemnedvr-host -ErrorAction SilentlyContinue)
if ($existingHost.Count -gt 0) {
    throw "condemnedvr-host.exe is already running (PID $($existingHost.Id -join ', '))."
}

$runLogDirectory = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $deployment.LogDirectory (
        'run-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')))
New-Item -ItemType Directory -Force -Path @(
    $runLogDirectory, $deployment.UserDirectory) | Out-Null

$sessionId = [uint64]([DateTime]::UtcNow.Ticks)
$sessionId = $sessionId -bxor ([uint64]$PID -shl 32)
if ($sessionId -eq 0) { $sessionId = 1 }
$sessionText = '0x{0:X16}' -f $sessionId
$hostArguments = @('--log-dir', "`"$runLogDirectory`"")
if (-not [string]::IsNullOrWhiteSpace($StartupImage)) {
    $hostArguments += @('--startup-image', "`"$StartupImage`"")
}
if ($ValidateOnly) {
    $hostArguments += '--validate-only'
} else {
    $hostArguments += @(
        '--ipc-session', $sessionText,
        '--exit-on-game-disconnect')
}

$previousRuntime = $env:XR_RUNTIME_JSON
try {
    if (-not [string]::IsNullOrWhiteSpace($RuntimeManifest)) {
        $env:XR_RUNTIME_JSON = $RuntimeManifest
    }
    $hostProcess = Start-Process -FilePath $deployment.HostExe `
        -ArgumentList $hostArguments `
        -WorkingDirectory (Split-Path -Parent $deployment.HostExe) `
        -WindowStyle Hidden `
        -PassThru
} finally {
    $env:XR_RUNTIME_JSON = $previousRuntime
}

$hostLog = $null
$hostText = ''
$deadline = (Get-Date).AddSeconds(35)
do {
    Start-Sleep -Milliseconds 200
    $hostProcess.Refresh()
    $hostLog = Get-ChildItem -LiteralPath $runLogDirectory `
        -Filter 'condemnedvr-host-*.log' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -ne $hostLog) {
        $hostText = Read-LiveLog $hostLog.FullName
    }
    if ($hostProcess.HasExited -and
        -not $hostText.Contains('"event":"xr_ready"')) {
        throw "OpenXR host exited before xr_ready (code $($hostProcess.ExitCode))."
    }
} until ($hostText.Contains('"event":"xr_ready"') -or
    (Get-Date) -ge $deadline)
if (-not $hostText.Contains('"event":"xr_ready"')) {
    if (-not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id -Force }
    throw 'OpenXR host did not become ready within 35 seconds.'
}

if ($ValidateOnly) {
    $hostProcess.WaitForExit(10000) | Out-Null
    $hostProcess.Refresh()
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
    }
    Write-Host 'Condemned VR host/runtime validation passed.' -ForegroundColor Green
    Write-Host "Host log: $($hostLog.FullName)"
    exit 0
}

[IO.File]::WriteAllText(
    $deployment.LoaderLog, '', (New-Object Text.UTF8Encoding($false)))
$env:SteamAppId = [string]$cfg.SteamAppId
$env:SteamGameId = [string]$cfg.SteamAppId
$gameArguments = @(
    '-condemnedvr-session', $sessionText,
    '-condemnedvr-logdir', "`"$runLogDirectory`"",
    '-archcfg', "`"$($deployment.ArchiveConfig)`"",
    '-userdirectory', "`"$($deployment.UserDirectory)`"",
    '-fearvr-render-scale',
    $RenderScale.ToString([Globalization.CultureInfo]::InvariantCulture))
if ($RendererProbe) {
    $gameArguments += '-condemnedvr-m3-probe'
}
if ($RendererPassThrough) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through')
}
if ($StereoDiagnostic) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-stereo-diagnostic')
}
if ($DoubleRenderDiagnostic) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-double-render-diagnostic')
}
if ($CameraReadProbe) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-camera-read-probe')
}
if ($EyeOffsetDiagnostic) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-eye-offset-diagnostic')
}
if ($ReverseEyeOffsetDiagnostic) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-eye-offset-diagnostic',
        '-condemnedvr-m3-reverse-eye-offset-diagnostic')
}
if ($ZeroEyeOffsetDiagnostic) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-eye-offset-diagnostic',
        '-condemnedvr-m3-zero-eye-offset-diagnostic')
}
if ($StereoTuning) {
    $gameArguments += @(
        '-condemnedvr-m3-probe',
        '-condemnedvr-m3-pass-through',
        '-condemnedvr-m3-stereo-tuning')
}
if ($LocomotionProbe) {
    $gameArguments += '-condemnedvr-m4-locomotion'
}
if ($InteractionProbe) {
    $gameArguments += '-condemnedvr-m4-interaction'
}
if ($CoreActionsProbe) {
    $gameArguments += '-condemnedvr-m4-core-actions'
}
if ($HapticsProbe) {
    $gameArguments += '-condemnedvr-m4-haptics'
}
if ($HeadAimProbe) {
    $gameArguments += '-condemnedvr-m5-head-aim'
}
if ($AimPathProbe) {
    $gameArguments += '-condemnedvr-m5-aim-path-probe'
}
if ($MeleeAimProbe) {
    $gameArguments += '-condemnedvr-m5-controller-melee-aim'
}
if ($PhysicalMeleeProbe) {
    $gameArguments += '-condemnedvr-m5-physical-melee-probe'
}
if ($PhysicalMeleeWallProxy) {
    $gameArguments += '-condemnedvr-m5-physical-melee-wall-proxy'
}
if ($PhysicalMeleeVisualProxy) {
    $gameArguments += '-condemnedvr-m5-physical-melee-visual-proxy'
}
if ($WeaponGripCalibration) {
    $gameArguments += '-condemnedvr-m5-weapon-grip-calibration'
}
if ($TwoHandedMelee) {
    $gameArguments += '-condemnedvr-m5-two-handed-melee'
}
if ($NoHidFpsFix) {
    $gameArguments += '-condemnedvr-no-hid-fps-fix'
}
if ($NoXrFramePacing) {
    $gameArguments += '-condemnedvr-no-xr-frame-pacing'
}
if ($TurningProbe) {
    $gameArguments += '-condemnedvr-m4-turning'
}
if ($MenuProbe) {
    $gameArguments += '-condemnedvr-m4-menu'
}
if ($MenuControlsProbe) {
    $gameArguments += '-condemnedvr-m6-menu-controls'
}
if ($RecenterProbe) {
    $gameArguments += '-condemnedvr-m4-recenter'
}
if ($DesktopWindow) {
    if ($backgroundRenderRequired) {
        $gameArguments += '-condemnedvr-background-render'
    }
    $gameArguments += @(
        '+Windowed', '1',
        '+ScreenWidth',
        $DesktopWindowWidth.ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '+ScreenHeight',
        $DesktopWindowHeight.ToString(
            [Globalization.CultureInfo]::InvariantCulture))
}
$game = $null
try {
    $game = Start-Process -FilePath $deployment.RuntimeExe `
        -ArgumentList $gameArguments `
        -WorkingDirectory $deployment.WorkingDirectory `
        -PassThru
    $gameFocusAttempted = $false
    $gameFocusRestored = $false

    $inspectorPowerShell = Join-Path $env:WINDIR (
        'SysWOW64\WindowsPowerShell\v1.0\powershell.exe')
    $inspectorScript = Join-Path $PSScriptRoot 'inspect-condemned-process32.ps1'
    $proxyLog = $null
    $proxyText = ''
    $loaderText = ''
    $modules = @()
    $deadline = (Get-Date).AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 250
        $game.Refresh()
        $hostProcess.Refresh()
        if ($game.HasExited) {
            throw "Condemned exited before mono transport verification (code $($game.ExitCode))."
        }
        if ($hostProcess.HasExited) {
            throw "OpenXR host exited during transport verification (code $($hostProcess.ExitCode))."
        }
        if (-not $gameFocusAttempted -and
            $game.MainWindowHandle -ne 0) {
            $gameFocusAttempted = $true
            $gameFocusRestored =
                Set-CondemnedVrForegroundWindow $game
        }
        $inspectionJson = & $inspectorPowerShell -NoProfile `
            -ExecutionPolicy Bypass -File $inspectorScript -ProcessId $game.Id
        if ($LASTEXITCODE -eq 0) {
            $modules = @((($inspectionJson -join [Environment]::NewLine) |
                ConvertFrom-Json).Modules)
        }
        $proxyLog = Get-ChildItem -LiteralPath $runLogDirectory `
            -Filter 'condemnedvr-bridge-*.log' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
        if ($null -ne $proxyLog) {
            $proxyText = Read-LiveLog $proxyLog.FullName
        }
        $loaderText = Read-LiveLog $deployment.LoaderLog
        $hostText = Read-LiveLog $hostLog.FullName
        if ($proxyText.Contains('"event":"adapter_mismatch"')) {
            throw 'D3D9 and OpenXR selected different GPU adapters.'
        }
        if (-not $NoHidFpsFix -and
            ($proxyText.Contains(
                 '"event":"hid_fps_fix_unsupported_executable"') -or
             $proxyText.Contains(
                 '"event":"hid_fps_fix_byte_mismatch"') -or
             $proxyText.Contains(
                 '"event":"hid_fps_fix_protect_failed"'))) {
            throw 'The guarded Condemned HID/FPS fix was rejected.'
        }
        if ($backgroundRenderRequired -and
            ($proxyText.Contains(
                 '"event":"background_render_fix_unsupported_executable"') -or
             $proxyText.Contains(
                 '"event":"background_render_fix_byte_mismatch"') -or
             $proxyText.Contains(
                 '"event":"background_render_fix_protect_failed"') -or
             $proxyText.Contains(
                 '"event":"background_render_fix_cursor_hook_failed"'))) {
            throw 'The guarded Condemned background-render fix was rejected.'
        }
        if ($LocomotionProbe -and
            $loaderText.Contains(
                '"event":"m4_binding_locomotion_rejected"')) {
            throw 'The guarded M4 locomotion hook was rejected.'
        }
        if ($TurningProbe -and
            $loaderText.Contains(
                '"event":"m4_binding_turning_rejected"')) {
            throw 'The guarded M4 turning hook was rejected.'
        }
        if ($InteractionProbe -and
            $loaderText.Contains(
                '"event":"m4_binding_interaction_rejected"')) {
            throw 'The guarded M4 interaction hook was rejected.'
        }
        if ($CoreActionsProbe -and
            $loaderText.Contains(
                '"event":"m4_binding_core_actions_rejected"')) {
            throw 'The guarded M4 core-action hook was rejected.'
        }
        if ($HapticsProbe -and
            $loaderText.Contains(
                '"event":"m4_controller_haptics_rejected"')) {
            throw 'The guarded M4 haptic path was rejected.'
        }
        if ($HapticsProbe -and
            $loaderText.Contains(
                '"event":"m4_controller_haptic_failed"')) {
            throw 'The guarded M4 haptic transport failed.'
        }
        if ($HeadAimProbe -and
            $loaderText.Contains(
                '"event":"m5_head_aim_rejected"')) {
            throw 'The guarded M5 head-aim path was rejected.'
        }
        if ($HeadAimProbe -and
            $loaderText.Contains(
                '"event":"m5_head_camera_transform_rejected"')) {
            throw 'The guarded M5 head-camera transform was rejected.'
        }
        if ($AimPathProbe -and
            $loaderText.Contains(
                '"event":"m5_aim_path_rejected"')) {
            throw 'The guarded M5 aim-path diagnostic was rejected.'
        }
        if ($MeleeAimProbe -and
            $loaderText.Contains(
                '"event":"m5_controller_melee_aim_rejected"')) {
            throw 'The guarded M5 controller-melee path was rejected.'
        }
        if ($PhysicalMeleeProbe -and
            $loaderText.Contains(
                '"event":"m5_physical_melee_probe_rejected"')) {
            throw 'The guarded M5 physical-melee probe was rejected.'
        }
        if ($PhysicalMeleeWallProxy -and
            $loaderText.Contains(
                '"event":"m5_physical_melee_wall_proxy_rejected"')) {
            throw 'The guarded M5 physical-melee wall proxy was rejected.'
        }
        if ($PhysicalMeleeVisualProxy -and
            $loaderText.Contains(
                '"event":"m5_physical_melee_visual_proxy_rejected"')) {
            throw 'The guarded M5 visible melee proxy was rejected.'
        }
        if ($PhysicalMeleeVisualProxy -and
            $loaderText.Contains(
                '"event":"m5_physical_melee_visual_proxy_restore_failed"')) {
            throw 'The visible M5 melee proxy could not restore the Retail model.'
        }
        if ($WeaponGripCalibration -and
            $loaderText.Contains(
                '"event":"m5_weapon_grip_calibration_rejected"')) {
            throw 'The guarded M5 weapon-grip calibration mode was rejected.'
        }
        if ($TwoHandedMelee -and
            $loaderText.Contains(
                '"event":"m5_two_handed_melee_rejected"')) {
            throw 'The guarded M5 two-handed melee mode was rejected.'
        }
        if ($RecenterProbe -and
            $loaderText.Contains(
                '"event":"m4_hmd_recenter_rejected"')) {
            throw 'The guarded M4 recenter path was rejected.'
        }
        if ($MenuProbe -and
            $loaderText.Contains(
                 '"event":"m4_menu_toggle_rejected"')) {
            throw 'The guarded M4 menu hook was rejected.'
        }
        if ($MenuProbe -and
            $loaderText.Contains(
                '"event":"m4_menu_render_state_failed"')) {
            throw 'The guarded M4 headset menu-state publisher failed.'
        }
        $bridgeReady =
            $proxyText.Contains('"event":"late_hooks_installed"') -and
            $proxyText.Contains('"event":"adapter_match"') -and
            $proxyText.Contains('"event":"shared_resources"') -and
            $proxyText.Contains('"event":"frame_ready"')
        $hidFpsFixReady = $NoHidFpsFix -or
            $proxyText.Contains('"event":"hid_fps_fix_applied"') -or
            $proxyText.Contains(
                '"event":"hid_fps_fix_already_applied"')
        $backgroundRenderReady = -not $backgroundRenderRequired -or
            $proxyText.Contains(
                '"event":"background_render_fix_applied"') -or
            $proxyText.Contains(
                '"event":"background_render_fix_already_applied"')
        $hostReady =
            $hostText.Contains('"event":"ipc_connected"') -and
            $hostText.Contains('"event":"ipc_frame"') -and
            $hostText.Contains('"event":"mono_quad_layer"')
        $locomotionReady = -not $LocomotionProbe -or
            $loaderText.Contains(
                '"event":"m4_binding_locomotion_armed"')
        $turningReady = -not $TurningProbe -or
            $loaderText.Contains(
                '"event":"m4_binding_turning_armed"')
        $interactionReady = -not $InteractionProbe -or
            $loaderText.Contains(
                '"event":"m4_binding_interaction_armed"')
        $coreActionsReady = -not $CoreActionsProbe -or
            $loaderText.Contains(
                '"event":"m4_binding_core_actions_armed"')
        $hapticsReady = -not $HapticsProbe -or
            $loaderText.Contains(
                '"event":"m4_controller_haptics_armed"')
        $headAimReady = -not $HeadAimProbe -or
            ($loaderText.Contains(
                 '"event":"m5_head_aim_armed"') -and
             $loaderText.Contains(
                 '"event":"m5_head_camera_transform_armed"'))
        $aimPathReady = -not $AimPathProbe -or
            $loaderText.Contains(
                '"event":"m5_aim_path_probe_armed"')
        $meleeAimReady = -not $MeleeAimProbe -or
            $loaderText.Contains(
                '"event":"m5_controller_melee_aim_armed"')
        $physicalMeleeReady = -not $PhysicalMeleeProbe -or
            $loaderText.Contains(
                '"event":"m5_physical_melee_probe_armed"')
        $physicalMeleeWallProxyReady = -not $PhysicalMeleeWallProxy -or
            $loaderText.Contains(
                '"event":"m5_physical_melee_wall_proxy_armed"')
        $physicalMeleeVisualProxyReady = -not $PhysicalMeleeVisualProxy -or
            $loaderText.Contains(
                '"event":"m5_physical_melee_visual_proxy_armed"')
        $weaponGripCalibrationReady = -not $WeaponGripCalibration -or
            $loaderText.Contains(
                '"event":"m5_weapon_grip_calibration_armed"')
        $twoHandedMeleeReady = -not $TwoHandedMelee -or
            $loaderText.Contains(
                '"event":"m5_two_handed_melee_armed"')
        $recenterReady = -not $RecenterProbe -or
            $loaderText.Contains(
                '"event":"m4_hmd_recenter_armed"')
        $menuReady = -not $MenuProbe -or
            ($loaderText.Contains(
                 '"event":"m4_menu_toggle_armed"') -and
             $loaderText.Contains(
                 '"event":"m4_menu_update_hook_called"') -and
             $loaderText.Contains(
                 '"event":"m4_menu_render_state"'))
        $menuControlsReady = -not $MenuControlsProbe -or
            $loaderText.Contains(
                '"event":"m6_menu_controls_armed"')
        $inputHooksReady =
            $locomotionReady -and $turningReady -and $menuReady -and
            $menuControlsReady -and
            $interactionReady -and $coreActionsReady -and $hapticsReady -and
            $headAimReady -and $aimPathReady -and $meleeAimReady -and
            $physicalMeleeReady -and $physicalMeleeWallProxyReady -and
            $physicalMeleeVisualProxyReady -and
            $weaponGripCalibrationReady -and
            $twoHandedMeleeReady -and
            $recenterReady
    } until (($bridgeReady -and $hostReady -and $hidFpsFixReady -and
              $backgroundRenderReady -and $inputHooksReady) -or
        (Get-Date) -ge $deadline)

    if (-not $bridgeReady -or -not $hostReady -or
        -not $hidFpsFixReady -or -not $backgroundRenderReady) {
        throw 'The mono OpenXR frame path did not become ready within 45 seconds.'
    }
    if (-not $inputHooksReady) {
        throw 'A requested guarded controller input hook did not arm within 45 seconds.'
    }
    $loadedBridge = $modules | Where-Object {
        $_.Name -ieq 'condemnedvr-d3d9.dll'
    } | Select-Object -First 1
    $expectedBridge = Join-Path $deployment.ModuleDirectory (
        'condemnedvr-d3d9.dll')
    if ($null -eq $loadedBridge -or
        [IO.Path]::GetFullPath($loadedBridge.Path) -ne
        [IO.Path]::GetFullPath($expectedBridge)) {
        throw 'The guarded transport bridge was not loaded from the M2 stage.'
    }
    $asiModules = @($modules | Where-Object { $_.Name -like '*.asi' })
    if ($asiModules.Count -ne 0) {
        throw 'M2 mono unexpectedly loaded an ASI module.'
    }
    $gameFocusRestored =
        (Set-CondemnedVrForegroundWindow $game) -or
        $gameFocusRestored

    $reportPath = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $runLogDirectory 'm2-mono-live.json')
    $report = [pscustomobject][ordered]@{
        SchemaVersion = 1
        CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
        Session = $sessionText
        GameProcessId = $game.Id
        HostProcessId = $hostProcess.Id
        Bridge = $loadedBridge
        AsiModules = $asiModules
        HostLog = $hostLog.FullName
        ProxyLog = $proxyLog.FullName
        LoaderLog = $deployment.LoaderLog
        M4Input = [pscustomobject][ordered]@{
            Locomotion = [bool]$LocomotionProbe
            Turning = [bool]$TurningProbe
            Menu = [bool]$MenuProbe
            MenuControls = [bool]$MenuControlsProbe
            Interaction = [bool]$InteractionProbe
            CoreActions = [bool]$CoreActionsProbe
            Haptics = [bool]$HapticsProbe
            HeadAim = [bool]$HeadAimProbe
            AimPath = [bool]$AimPathProbe
            MeleeAim = [bool]$MeleeAimProbe
            PhysicalMelee = [bool]$PhysicalMeleeProbe
            PhysicalMeleeWallProxy = [bool]$PhysicalMeleeWallProxy
            PhysicalMeleeVisualProxy = [bool]$PhysicalMeleeVisualProxy
            WeaponGripCalibration = [bool]$WeaponGripCalibration
            TwoHandedMelee = [bool]$TwoHandedMelee
            Recenter = [bool]$RecenterProbe
        }
        CaptureEnabled = $true
        HidFpsFixEnabled = -not [bool]$NoHidFpsFix
        BackgroundRenderingEnabled = $backgroundRenderRequired
        XrFramePacingEnabled = -not [bool]$NoXrFramePacing
        PerformanceProbe = [bool]$PerformanceProbe
        GameWindowFocusRestored = [bool]$gameFocusRestored
        StartupImage = $StartupImage
        OpenXrEnabled = $true
        StereoEnabled = $false
    }
    [IO.File]::WriteAllText(
        $reportPath,
        ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false)))

    Write-Host 'Condemned M2 mono OpenXR transport is live.' -ForegroundColor Green
    Write-Host "Game PID: $($game.Id)  Host PID: $($hostProcess.Id)"
    Write-Host "Host log:  $($hostLog.FullName)"
    Write-Host "Proxy log: $($proxyLog.FullName)"
    Write-Host "Report:    $reportPath"
    if (-not [string]::IsNullOrWhiteSpace($StartupImage)) {
        Write-Host "Startup:   $StartupImage"
    }
    Write-Host 'The headset should show the normal desktop image on a stable mono quad.'
    if ($MenuControlsProbe) {
        Write-Host 'VR menu controls: left stick = navigate; A/trigger = accept; B = back.' `
            -ForegroundColor Cyan
    }
    if ($WeaponGripCalibration) {
        Write-Host 'Live weapon-grip calibration is active:' `
            -ForegroundColor Cyan
        Write-Host '  Hold BOTH grips: right stick = X/Y; left-stick up/down = Z'
        Write-Host '  A = position; B = rotation; X = reset; Y = save snapshot'
        Write-Host '  Left/right stick click = finer/coarser; release a grip = gameplay'
        Write-Host '  Keyboard fallback: J/L X, K/I Y, U/O Z, T mode, ,/. step, R reset, P save'
        Write-Host '  Wireframe = grip pose; magenta = grip centre; RGB = local axes; yellow = aim'
        Write-Host '  F11 pauses/resumes the setup tool.'
    }
    if ($TwoHandedMelee) {
        Write-Host 'Two-hand axe: place the left controller on the handle, release, then squeeze left grip.' `
            -ForegroundColor Cyan
        Write-Host '  VR Tools > 2-HAND can capture the current left-hand pose and tune the grab radius.'
        Write-Host '  Magenta = right controller; cyan = left; green/amber/red = support target state.'
    }
    if ($PerformanceProbe) {
        $watcherScript = Join-Path $PSScriptRoot (
            'watch-condemned-performance.ps1')
        $watcherArguments = @(
            '-NoLogo',
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f $watcherScript),
            '-Run', ('"{0}"' -f $runLogDirectory),
            '-GameProcessId', $game.Id.ToString(
                [Globalization.CultureInfo]::InvariantCulture))
        Start-Process -FilePath 'powershell.exe' `
            -ArgumentList $watcherArguments | Out-Null
        Write-Host 'Live performance telemetry opened in a separate window.' `
            -ForegroundColor Cyan
    }
    if (-not (Set-CondemnedVrForegroundWindow $game)) {
        Write-Warning 'Condemned was ready, but Windows refused the foreground focus handoff.'
    }
} catch {
    if ($null -ne $game) {
        $game.Refresh()
        if (-not $game.HasExited) { Stop-Process -Id $game.Id -Force }
    }
    $hostProcess.Refresh()
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
    }
    throw
}

if ($Wait) {
    $game.WaitForExit()
    $hostProcess.WaitForExit(10000) | Out-Null
    $hostProcess.Refresh()
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
    }
    Write-Host 'Condemned and its M2 host exited.'
}
