<#
.SYNOPSIS
    Launches the current Condemned VR feature platform.

.DESCRIPTION
    With no feature-selection parameters, launches the consolidated Current
    profile: the canonical mapped one-handed/Pipe platform plus the guarded
    Retail VR Settings entry. Explicit feature-selection parameters preserve
    the existing custom diagnostic behavior. Use -Minimal for the legacy bare
    transport launch.

.PARAMETER RuntimeManifest
    Optional OpenXR runtime JSON applied only to the new host process. When
    omitted, the system-wide active x64 OpenXR runtime is used.

.PARAMETER StartupImage
    Optional PNG/JPEG displayed in both eyes until Condemned publishes its
    first frame. When omitted, images\title.png is used if present.

.PARAMETER ValidateOnly
    Validates the host/runtime/session/swapchains without launching Condemned.

.PARAMETER Minimal
    Disables the implicit Current feature profile and launches only the bare
    transport. It cannot be combined with a feature-selection parameter.

.PARAMETER RetailHeadBob
    Diagnostic A/B rollback that explicitly requests Retail's default
    `+HeadBob 1`. Every non-Minimal launch otherwise requests `+HeadBob 0`;
    Minimal without this switch leaves Retail's HeadBob value untouched.

.PARAMETER HeadBobDiagnostic
    Enables a guarded read-only sample of Retail's effective HeadBob and
    IdleBreathing console values plus the existing Retail camera transform
    sample. The switch itself does not select another gameplay profile or
    write either console variable; normal non-Minimal suppression can still
    restore HeadBob through the separately guarded Retail console setter.

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

.PARAMETER RetailVrSettingsProbe
    Adds an opt-in `VR Settings` row to Retail's Options screen through the
    native CLTGUI control factory. The current isolated base-screen host
    bypasses the unsuitable dormant Game Options content and requires live
    acceptance before its diagnostic category rows can mutate settings.
    Requires -MenuProbe and -MenuControlsProbe.

.PARAMETER InteractionProbe
    Enables the separately guarded M4 right-grip Activate command gate.

.PARAMETER CoreActionsProbe
    Enables the guarded M4 run, fire, block, weapon-toggle, ammo-check,
    stun-gun, flashlight, and forensic-tool command gate.
    Includes the signature-checked forensic target ray for verified Scanner
    index 46 and Item Camera index 3, each sourced from its matching fresh
    Retail Camera socket used by the white arrows/live preview. Missing/stale
    matching poses and every unmapped index fall back to Retail.

.PARAMETER ForensicMemoryProbe
    Enables an observation-only Scanner/DigitalCamera memory trace around
    Tool, Fire, and Activate edges. Requires -CoreActionsProbe and -MenuProbe.
    No game-memory writes or video capture are performed.

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
    weapon endpoint. Native impact from the player's equipped weapon remains
    blocked unless the separate pipe-only -PhysicalMeleeContactDamage gate is
    enabled; enemy and unrecognised Retail melee remains untouched. Requires
    -PhysicalMeleeProbe and cannot be combined with -MeleeAimProbe.

.PARAMETER PhysicalMeleeContactDamage
    Enables the mapped one-handed live damage acceptance gate. A guarded
    equip-time Retail pulse automatically requests and verifies its native
    collision body; one manual Retail attack remains a bounded fallback. The
    verified body is then checked continuously. The current
    lifecycle-validation build forwards one fresh, de-duplicated Retail-valid
    overlap, then rearms only after separation; speed and energy are diagnostic
    until that lifecycle passes live. Requires -PhysicalMeleeWallProxy and fails
    closed for other weapons, menus, focus loss, stale tracking, and weapon
    changes.

.PARAMETER PhysicalMeleeColliderDebug
    Draws the configured swept melee volume in real time in both headset eyes.
    Amber is the predicted proxy before Retail creates its collision body;
    green means that player-owned body is live. The exact proxy origin is
    marked with a cross. Requires -PhysicalMeleeWallProxy.
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
    and persisted per equipped Retail weapon, and the Snapshot row saves and
    logs exact profile-ready values. Grip/2-Hand menu edits auto-save; the
    continuous fallback saves on controller Y or keyboard P. A generic
    controller wireframe marks the OpenXR grip
    pose and aim direction while the Grip tab is open. F11 retains the legacy
    keyboard/controller calibration mode. Requires -PhysicalMeleeVisualProxy.

.PARAMETER WeaponCatalogProbe
    Logs the stable Retail index, database name, and animation property for
    every player weapon. This is an opt-in developer discovery pass and is
    not used by gameplay frames.

.PARAMETER WeaponModelDiscovery
    Enumerates the lifetime-validated equipped model's complete node hierarchy
    and records model-local motion peaks after a two-second idle baseline. Use
    it to identify a Colt slide/bolt node and its travel axis without assuming
    a name, object offset, or layout. The diagnostic is read-only. Requires
    -StereoTuning and -PhysicalMeleeVisualProxy.

.PARAMETER SlideControlTest
    Arms the explicitly guarded Colt-only SlideJnt callback proof and authored
    slide-grab runtime. It requires the verified full two-arm node-control
    boundary, exact held-model identity, weapon authoring, and core-action
    capture. Omit this switch for immediate rollback.

.PARAMETER ArmIkDiscovery
    Runs the observation-only player-body pass needed before arm IK. It logs
    the live node hierarchy and arm transforms plus known hand sockets. It
    never installs node controls or changes animation.
    Requires -StereoTuning so sampling occurs on the verified render path.

.PARAMETER ArmIkRightHandProof
    Enables the first guarded arm-IK mutation gate. It installs one callback
    on Right_hand and solves the authored RightHand socket onto the same
    weighted VR weapon pose used for rendering and collision. Upper arm and
    forearm animation remain untouched. Requires -StereoTuning,
    -HeadAimProbe, and -MenuProbe for save/load lifecycle observation.

.PARAMETER ArmIkRightArm
    Enables the second guarded arm-IK mutation gate. It retains exact
    RightHand socket placement and additionally rotates Right_armu and
    Right_arml with the measured two-bone chain, then mirrors the complete
    measured solve onto Left_armu, Left_arml, Left_hand, and LeftHand. The
    legacy switch name is retained for launch compatibility. Requires
    -StereoTuning, -HeadAimProbe, and -MenuProbe; the latter supplies the
    verified Retail save/load lifecycle. It is mutually exclusive with
    -ArmIkRightHandProof so the hand-only build remains an explicit A/B
    fallback.

.PARAMETER TwoHandedMelee
    Enables profile-driven two-hand melee. The right hand remains the dominant
    weapon anchor; squeezing the left grip near the configured support point
    constrains the shaft direction without scaling the weapon. Release or
    tracking loss returns smoothly to weighted one-hand control. Requires
    -PhysicalMeleeVisualProxy.

.PARAMETER WeaponTest
    Applies the complete guarded one-handed melee headset-test preset. The
    retained `Pipe` switch name identifies the accepted baseline/configuration;
    mapped one-handed weapons inherit that baseline until they receive their
    own per-index save. The preset enables the accepted M4 controls, physical-
    melee proxy, live grip calibration, full arm IK, recentering, and desktop-
    window workflow. It deliberately leaves two-hand attachment disabled.

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
    [ValidateSet('Pipe')]
    [string]$WeaponTest,
    [switch]$ValidateOnly,
    [switch]$Minimal,
    [switch]$RetailHeadBob,
    [switch]$HeadBobDiagnostic,
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
    [switch]$RetailVrSettingsProbe,
    [switch]$InteractionProbe,
    [switch]$CoreActionsProbe,
    [switch]$ForensicMemoryProbe,
    [switch]$HapticsProbe,
    [switch]$HeadAimProbe,
    [switch]$AimPathProbe,
    [switch]$MeleeAimProbe,
    [switch]$PhysicalMeleeProbe,
    [switch]$PhysicalMeleeWallProxy,
    [switch]$PhysicalMeleeContactDamage,
    [switch]$PhysicalMeleeColliderDebug,
    [switch]$PhysicalMeleeVisualProxy,
    [switch]$WeaponGripCalibration,
    [switch]$WeaponCatalogProbe,
    [switch]$WeaponModelDiscovery,
    [switch]$SlideControlTest,
    [switch]$ArmIkDiscovery,
    [switch]$ArmIkRightHandProof,
    [switch]$ArmIkRightArm,
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

. (Join-Path $PSScriptRoot '_condemnedvr-launch-profile.ps1')
$featurePlatformProfile =
    Resolve-CondemnedVrLaunchProfile $PSBoundParameters
Assert-CondemnedVrHeadBobDiagnosticProfile `
    -LaunchProfile $featurePlatformProfile `
    -HeadBobDiagnostic ([bool]$HeadBobDiagnostic)
if ($HeadBobDiagnostic) {
    $CameraReadProbe = $true
}
if ($featurePlatformProfile.ApplyPipePreset) {
    $WeaponTest = 'Pipe'
}
if ($featurePlatformProfile.EnableRetailVrSettings) {
    $RetailVrSettingsProbe = $true
}

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

if ($WeaponTest -eq 'Pipe') {
    if ($TwoHandedMelee) {
        throw '-WeaponTest Pipe cannot be combined with -TwoHandedMelee.'
    }
    if ($MeleeAimProbe) {
        throw '-WeaponTest Pipe cannot be combined with -MeleeAimProbe.'
    }
    if ($ArmIkRightHandProof) {
        throw '-WeaponTest Pipe cannot be combined with -ArmIkRightHandProof.'
    }

    # One-handed is the canonical weapon path. The support grip remains a
    # profile capability and is intentionally absent from this preset.
    $StereoTuning = $true
    $LocomotionProbe = $true
    $TurningProbe = $true
    $MenuProbe = $true
    $MenuControlsProbe = $true
    $InteractionProbe = $true
    $CoreActionsProbe = $true
    $HapticsProbe = $true
    $HeadAimProbe = $true
    $AimPathProbe = $true
    $PhysicalMeleeProbe = $true
    $PhysicalMeleeWallProxy = $true
    $PhysicalMeleeContactDamage = $true
    $PhysicalMeleeColliderDebug = $true
    $PhysicalMeleeVisualProxy = $true
    $WeaponGripCalibration = $true
    $ArmIkRightArm = $true
    $RecenterProbe = $true
    $DesktopWindow = $true
}

if ($ForensicMemoryProbe) {
    $WeaponCatalogProbe = $true
}

if ($RecenterProbe -and -not $StereoTuning) {
    throw '-RecenterProbe requires -StereoTuning.'
}
if ($ArmIkDiscovery -and -not $StereoTuning) {
    throw '-ArmIkDiscovery requires -StereoTuning.'
}
if ($WeaponModelDiscovery -and
    -not ($StereoTuning -and $PhysicalMeleeVisualProxy)) {
    throw ('-WeaponModelDiscovery requires -StereoTuning and ' +
        '-PhysicalMeleeVisualProxy.')
}
if ($SlideControlTest -and -not (
    $StereoTuning -and $PhysicalMeleeVisualProxy -and
    $WeaponGripCalibration -and $ArmIkRightArm -and
    $CoreActionsProbe)) {
    throw ('-SlideControlTest requires -StereoTuning, ' +
        '-PhysicalMeleeVisualProxy, -WeaponGripCalibration, ' +
        '-ArmIkRightArm, and -CoreActionsProbe.')
}
if ($ArmIkRightHandProof -and
    -not ($StereoTuning -and $HeadAimProbe -and $MenuProbe)) {
    throw ('-ArmIkRightHandProof requires -StereoTuning, -HeadAimProbe, ' +
        'and -MenuProbe.')
}
if ($ArmIkRightArm -and
    -not ($StereoTuning -and $HeadAimProbe -and $MenuProbe)) {
    throw ('-ArmIkRightArm requires -StereoTuning, -HeadAimProbe, ' +
        'and -MenuProbe.')
}
if ($ArmIkRightArm -and $ArmIkRightHandProof) {
    throw '-ArmIkRightArm and -ArmIkRightHandProof are mutually exclusive.'
}
if ($MenuControlsProbe -and -not $MenuProbe) {
    throw '-MenuControlsProbe requires -MenuProbe.'
}
if ($RetailVrSettingsProbe -and
    -not ($MenuProbe -and $MenuControlsProbe)) {
    throw ('-RetailVrSettingsProbe requires -MenuProbe and ' +
        '-MenuControlsProbe.')
}
if ($ForensicMemoryProbe -and
    -not ($CoreActionsProbe -and $MenuProbe)) {
    throw '-ForensicMemoryProbe requires -CoreActionsProbe and -MenuProbe.'
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
if ($PhysicalMeleeContactDamage -and -not $PhysicalMeleeWallProxy) {
    throw '-PhysicalMeleeContactDamage requires -PhysicalMeleeWallProxy.'
}
if ($PhysicalMeleeColliderDebug -and -not $PhysicalMeleeWallProxy) {
    throw '-PhysicalMeleeColliderDebug requires -PhysicalMeleeWallProxy.'
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

. (Join-Path $PSScriptRoot '_condemnedvr-window-focus.ps1')
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
$sessionLoaderLog = Assert-UnderCondemnedVrProjectRoot (
    Join-Path $runLogDirectory 'condemnedvr-loader.log')
$liveColliderCommandPath = $null
if ($WeaponTest -eq 'Pipe' -or $WeaponGripCalibration) {
    $liveColliderCommandPath = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $runLogDirectory 'weapon-alignment-command.txt')
}


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
if ($HeadBobDiagnostic) {
    $gameArguments += '-condemnedvr-m5-headbob-diagnostic'
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
if ($ForensicMemoryProbe) {
    $gameArguments += '-condemnedvr-m5-forensic-memory-probe'
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
if ($PhysicalMeleeContactDamage) {
    $gameArguments += '-condemnedvr-m5-physical-melee-contact-damage'
}
if ($PhysicalMeleeColliderDebug) {
    $gameArguments += '-condemnedvr-m5-physical-melee-collider-debug'
}
if ($PhysicalMeleeVisualProxy) {
    $gameArguments += '-condemnedvr-m5-physical-melee-visual-proxy'
}
if ($WeaponGripCalibration) {
    $gameArguments += '-condemnedvr-m5-weapon-grip-calibration'
}
if ($WeaponCatalogProbe) {
    $gameArguments += '-condemnedvr-m5-weapon-catalog-probe'
}
if ($WeaponModelDiscovery) {
    $gameArguments += '-condemnedvr-m5-weapon-model-discovery'
}
if ($SlideControlTest) {
    $gameArguments += '-condemnedvr-m5-slide-control-test'
}
if ($ArmIkDiscovery) {
    $gameArguments += '-condemnedvr-arm-ik-discovery'
}
if ($ArmIkRightHandProof) {
    $gameArguments += '-condemnedvr-arm-ik-right-hand-proof'
}
if ($ArmIkRightArm) {
    $gameArguments += '-condemnedvr-arm-ik-right-arm'
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
if ($RetailVrSettingsProbe) {
    $gameArguments += '-condemnedvr-m6-retail-vr-settings'
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
# Keep Retail console-variable pairs after every project-specific switch.
$gameArguments = @(
    Add-CondemnedVrRetailHeadBobArguments `
        -GameArguments $gameArguments `
        -LaunchProfile $featurePlatformProfile)
$game = $null
$previousLiveColliderCommandPath =
    $env:CONDEMNEDVR_LIVE_COLLIDER_COMMAND_PATH
try {
    try {
        $env:CONDEMNEDVR_LIVE_COLLIDER_COMMAND_PATH =
            $liveColliderCommandPath
        $game = Start-Process -FilePath $deployment.RuntimeExe `
            -ArgumentList $gameArguments `
            -WorkingDirectory $deployment.WorkingDirectory `
            -PassThru
    } finally {
        $env:CONDEMNEDVR_LIVE_COLLIDER_COMMAND_PATH =
            $previousLiveColliderCommandPath
    }
    $gameFocusAttempted = $false
    $initialGameFocus = $null

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
            $initialGameFocus =
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
        if ($CoreActionsProbe -and
            $loaderText.Contains(
                '"event":"m5_forensic_camera_socket_ray_rejected"')) {
            throw 'The guarded M5 forensic Camera-socket ray hook was rejected.'
        }
        if ($HeadBobDiagnostic -and
            $loaderText.Contains(
                '"event":"m5_retail_headbob_diagnostic_rejected"')) {
            throw 'The guarded Retail HeadBob diagnostic was rejected.'
        }
        if ($ForensicMemoryProbe -and
            $loaderText.Contains(
                '"event":"m5_forensic_memory_probe_rejected"')) {
            throw 'The guarded M5 forensic memory probe was rejected.'
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
        if ($HeadAimProbe -and $PhysicalMeleeVisualProxy -and
            $loaderText.Contains(
                '"event":"m5_handgun_muzzle_aim_rejected"')) {
            throw 'The guarded M5 handgun muzzle-aim path was rejected.'
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
        if ($PhysicalMeleeContactDamage -and
            $loaderText.Contains(
                '"event":"m5_physical_melee_contact_damage_rejected"')) {
            throw 'The guarded M5 pipe contact-damage gate was rejected.'
        }
        if ($PhysicalMeleeColliderDebug -and
            $loaderText.Contains(
                '"event":"m5_physical_melee_collider_debug_rejected"')) {
            throw 'The guarded M5 melee collider visualizer was rejected.'
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
        if ($ArmIkDiscovery -and
            $loaderText.Contains(
                '"event":"arm_ik_discovery_rejected"')) {
            throw 'The guarded arm-IK discovery pass was rejected.'
        }
        if ($WeaponModelDiscovery -and
            $loaderText.Contains(
                '"event":"weapon_model_discovery_rejected"')) {
            throw 'The guarded equipped-weapon model discovery pass was rejected.'
        }
        if ($SlideControlTest -and
            $loaderText.Contains(
                '"event":"m5_slide_node_control_rejected"')) {
            throw 'The guarded Colt slide-control proof was rejected.'
        }
        if ($ArmIkRightHandProof -and
            $loaderText.Contains(
                '"event":"arm_ik_right_hand_proof_rejected"')) {
            throw 'The guarded right-hand arm-IK proof was rejected.'
        }
        if ($ArmIkRightArm -and
            $loaderText.Contains(
                '"event":"arm_ik_right_arm_rejected"')) {
            throw 'The guarded full two-arm IK gate was rejected.'
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
        if ($RetailVrSettingsProbe -and
            $loaderText.Contains(
                '"event":"m6_retail_vr_settings_rejected"')) {
            throw 'The guarded Retail VR Settings menu probe was rejected.'
        }
        if ($RetailVrSettingsProbe -and
            $loaderText.Contains(
                '"event":"m6_retail_vr_settings_row_failed"')) {
            throw 'Retail could not create or own the VR Settings menu row.'
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
        $headBobDiagnosticReady = -not $HeadBobDiagnostic -or
            ($loaderText.Contains(
                 '"event":"m5_retail_headbob_diagnostic_armed"') -and
             $loaderText.Contains(
                 '"event":"m5_retail_headbob_effective_sample"'))
        $headBobPostProfileReady = -not $featurePlatformProfile.RetailHeadBobSuppressed -or
            $loaderText.Contains(
                 '"event":"m5_retail_headbob_post_profile_armed"')
        $forensicCameraSocketRayReady = -not $CoreActionsProbe -or
            $loaderText.Contains(
                '"event":"m5_forensic_camera_socket_ray_armed"')
        $forensicMemoryReady = -not $ForensicMemoryProbe -or
            ($loaderText.Contains(
                 '"event":"m5_forensic_memory_probe_armed"') -and
             $loaderText.Contains(
                 '"event":"m5_forensic_observers_armed"'))
        $hapticsReady = -not $HapticsProbe -or
            $loaderText.Contains(
                '"event":"m4_controller_haptics_armed"')
        $headAimReady = -not $HeadAimProbe -or
            ($loaderText.Contains(
                 '"event":"m5_head_aim_armed"') -and
             $loaderText.Contains(
                 '"event":"m5_head_camera_transform_armed"'))
        $handgunMuzzleAimReady =
            -not ($HeadAimProbe -and $PhysicalMeleeVisualProxy) -or
            $loaderText.Contains(
                '"event":"m5_handgun_muzzle_aim_armed"')
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
        $physicalMeleeContactDamageReady =
            -not $PhysicalMeleeContactDamage -or
            $loaderText.Contains(
                '"event":"m5_physical_melee_contact_damage_armed"')
        $physicalMeleeColliderDebugReady =
            -not $PhysicalMeleeColliderDebug -or
            $loaderText.Contains(
                '"event":"m5_physical_melee_collider_debug_armed"')
        $physicalMeleeVisualProxyReady = -not $PhysicalMeleeVisualProxy -or
            $loaderText.Contains(
                '"event":"m5_physical_melee_visual_proxy_armed"')
        $weaponGripCalibrationReady = -not $WeaponGripCalibration -or
            $loaderText.Contains(
                '"event":"m5_weapon_grip_calibration_armed"')
        $twoHandedMeleeReady = -not $TwoHandedMelee -or
            $loaderText.Contains(
                '"event":"m5_two_handed_melee_armed"')
        $armIkDiscoveryReady = -not $ArmIkDiscovery -or
            $loaderText.Contains(
                '"event":"arm_ik_discovery_armed"')
        $weaponModelDiscoveryReady = -not $WeaponModelDiscovery -or
            $loaderText.Contains(
                '"event":"weapon_model_discovery_armed"')
        $slideControlTestReady = -not $SlideControlTest -or
            $loaderText.Contains(
                '"event":"m5_slide_node_control_armed"')
        $armIkRightHandProofReady = -not $ArmIkRightHandProof -or
            $loaderText.Contains(
                '"event":"arm_ik_right_hand_proof_armed"')
        $armIkRightArmReady = -not $ArmIkRightArm -or
            $loaderText.Contains(
                '"event":"arm_ik_right_arm_armed"')
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
        $retailVrSettingsReady = -not $RetailVrSettingsProbe -or
            $loaderText.Contains(
                '"event":"m6_retail_vr_settings_armed"')
        $inputHooksReady =
            $locomotionReady -and $turningReady -and $menuReady -and
            $menuControlsReady -and $retailVrSettingsReady -and
            $interactionReady -and $coreActionsReady -and
            $headBobDiagnosticReady -and
            $headBobPostProfileReady -and
            $forensicCameraSocketRayReady -and
            $forensicMemoryReady -and $hapticsReady -and
            $headAimReady -and $handgunMuzzleAimReady -and
            $aimPathReady -and $meleeAimReady -and
            $physicalMeleeReady -and $physicalMeleeWallProxyReady -and
            $physicalMeleeContactDamageReady -and
            $physicalMeleeColliderDebugReady -and
            $physicalMeleeVisualProxyReady -and
            $weaponGripCalibrationReady -and
            $twoHandedMeleeReady -and
            $armIkDiscoveryReady -and
            $weaponModelDiscoveryReady -and
            $slideControlTestReady -and
            $armIkRightHandProofReady -and
            $armIkRightArmReady -and
            $recenterReady
    } until (($bridgeReady -and $hostReady -and $hidFpsFixReady -and
              $backgroundRenderReady -and $inputHooksReady) -or
        (Get-Date) -ge $deadline)

    if (-not $bridgeReady -or -not $hostReady -or
        -not $hidFpsFixReady -or -not $backgroundRenderReady) {
        throw 'The mono OpenXR frame path did not become ready within 45 seconds.'
    }
    if (-not $inputHooksReady) {
        $missingReadiness = @()
        if (-not $locomotionReady) { $missingReadiness += 'Locomotion' }
        if (-not $turningReady) { $missingReadiness += 'Turning' }
        if (-not $menuReady) { $missingReadiness += 'Menu' }
        if (-not $menuControlsReady) { $missingReadiness += 'MenuControls' }
        if (-not $retailVrSettingsReady) { $missingReadiness += 'RetailVrSettings' }
        if (-not $interactionReady) { $missingReadiness += 'Interaction' }
        if (-not $coreActionsReady) { $missingReadiness += 'CoreActions' }
        if (-not $headBobDiagnosticReady) { $missingReadiness += 'HeadBobDiagnostic' }
        if (-not $headBobPostProfileReady) { $missingReadiness += 'HeadBobPostProfile' }
        if (-not $forensicCameraSocketRayReady) { $missingReadiness += 'ForensicCameraSocketRay' }
        if (-not $forensicMemoryReady) { $missingReadiness += 'ForensicMemory' }
        if (-not $hapticsReady) { $missingReadiness += 'Haptics' }
        if (-not $headAimReady) { $missingReadiness += 'HeadAim' }
        if (-not $handgunMuzzleAimReady) { $missingReadiness += 'HandgunMuzzleAim' }
        if (-not $aimPathReady) { $missingReadiness += 'AimPath' }
        if (-not $meleeAimReady) { $missingReadiness += 'MeleeAim' }
        if (-not $physicalMeleeReady) { $missingReadiness += 'PhysicalMelee' }
        if (-not $physicalMeleeWallProxyReady) { $missingReadiness += 'PhysicalMeleeWallProxy' }
        if (-not $physicalMeleeContactDamageReady) { $missingReadiness += 'PhysicalMeleeContactDamage' }
        if (-not $physicalMeleeColliderDebugReady) { $missingReadiness += 'PhysicalMeleeColliderDebug' }
        if (-not $physicalMeleeVisualProxyReady) { $missingReadiness += 'PhysicalMeleeVisualProxy' }
        if (-not $weaponGripCalibrationReady) { $missingReadiness += 'WeaponGripCalibration' }
        if (-not $twoHandedMeleeReady) { $missingReadiness += 'TwoHandedMelee' }
        if (-not $armIkDiscoveryReady) { $missingReadiness += 'ArmIkDiscovery' }
        if (-not $weaponModelDiscoveryReady) { $missingReadiness += 'WeaponModelDiscovery' }
        if (-not $slideControlTestReady) { $missingReadiness += 'SlideControlTest' }
        if (-not $armIkRightHandProofReady) { $missingReadiness += 'ArmIkRightHandProof' }
        if (-not $armIkRightArmReady) { $missingReadiness += 'ArmIkRightArm' }
        if (-not $recenterReady) { $missingReadiness += 'Recenter' }
        throw ('Requested guarded readiness did not arm within 45 seconds: ' +
            ($missingReadiness -join ', ') +
            '. Verify that the prepared M2 stage contains the current build.')
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
    $readyGameFocus = Set-CondemnedVrForegroundWindow $game `
        -TimeoutMilliseconds 1000

    $reportPath = Assert-UnderCondemnedVrProjectRoot (
        Join-Path $runLogDirectory 'm2-mono-live.json')
    $weaponDiagnosticsPath = $null
    if ($WeaponTest -eq 'Pipe') {
        $weaponDiagnosticsPath = Assert-UnderCondemnedVrProjectRoot (
            Join-Path $runLogDirectory 'weapon-diagnostics-live.json')
    }
    $report = [pscustomobject][ordered]@{
        SchemaVersion = 1
        CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
        Session = $sessionText
        FeaturePreset = $featurePlatformProfile.Name
        WeaponTestPreset = $WeaponTest
        RetailHeadBobSuppressed =
            [bool]$featurePlatformProfile.RetailHeadBobSuppressed
        RetailHeadBobCommandValue =
            $featurePlatformProfile.RetailHeadBobCommandValue
        RetailHeadBobDiagnostic = [bool]$HeadBobDiagnostic
        GameProcessId = $game.Id
        HostProcessId = $hostProcess.Id
        Bridge = $loadedBridge
        AsiModules = $asiModules
        HostLog = $hostLog.FullName
        WeaponAlignmentCommand = $liveColliderCommandPath
        ProxyLog = $proxyLog.FullName
        LoaderLog = $deployment.LoaderLog
        WeaponDiagnostics = $weaponDiagnosticsPath
        M4Input = [pscustomobject][ordered]@{
            Locomotion = [bool]$LocomotionProbe
            Turning = [bool]$TurningProbe
            Menu = [bool]$MenuProbe
            MenuControls = [bool]$MenuControlsProbe
            RetailVrSettings = [bool]$RetailVrSettingsProbe
            Interaction = [bool]$InteractionProbe
            CoreActions = [bool]$CoreActionsProbe
            ForensicMemory = [bool]$ForensicMemoryProbe
            Haptics = [bool]$HapticsProbe
            HeadAim = [bool]$HeadAimProbe
            AimPath = [bool]$AimPathProbe
            MeleeAim = [bool]$MeleeAimProbe
            PhysicalMelee = [bool]$PhysicalMeleeProbe
            PhysicalMeleeWallProxy = [bool]$PhysicalMeleeWallProxy
            PhysicalMeleeContactDamage = [bool]$PhysicalMeleeContactDamage
            PhysicalMeleeColliderDebug = [bool]$PhysicalMeleeColliderDebug
            PhysicalMeleeVisualProxy = [bool]$PhysicalMeleeVisualProxy
            WeaponGripCalibration = [bool]$WeaponGripCalibration
            WeaponModelDiscovery = [bool]$WeaponModelDiscovery
            SlideControlTest = [bool]$SlideControlTest
            TwoHandedMelee = [bool]$TwoHandedMelee
            ArmIkDiscovery = [bool]$ArmIkDiscovery
            ArmIkRightHandProof = [bool]$ArmIkRightHandProof
            ArmIkRightArm = [bool]$ArmIkRightArm
            Recenter = [bool]$RecenterProbe
        }
        CaptureEnabled = $true
        HidFpsFixEnabled = -not [bool]$NoHidFpsFix
        BackgroundRenderingEnabled = $backgroundRenderRequired
        XrFramePacingEnabled = -not [bool]$NoXrFramePacing
        PerformanceProbe = [bool]$PerformanceProbe
        GameWindowFocusRestored = $false
        GameWindowFocus = [pscustomobject][ordered]@{
            InitialAttempt = $initialGameFocus
            ReadinessHandoff = $readyGameFocus
            FinalHandoff = $null
        }
        StartupImage = $StartupImage
        OpenXrEnabled = $true
        StereoEnabled = $false
    }
    Write-Host 'Condemned M2 mono OpenXR transport is live.' -ForegroundColor Green
    Write-Host ('Feature preset: {0}' -f $featurePlatformProfile.Name)
    Write-Host "Game PID: $($game.Id)  Host PID: $($hostProcess.Id)"
    Write-Host "Host log:  $($hostLog.FullName)"
    Write-Host "Proxy log: $($proxyLog.FullName)"
    Write-Host "Report:    $reportPath"
    if (-not [string]::IsNullOrWhiteSpace($StartupImage)) {
        Write-Host "Startup:   $StartupImage"
    }
    if ($WeaponTest -eq 'Pipe') {
        Write-Host ('One-handed combat test (Pipe baseline): equip any mapped ' +
            'one-handed melee weapon; live contact damage is ON; two-hand ' +
            'attachment is OFF.') -ForegroundColor Cyan
        Write-Host ('  Unarmed, ordinary firearms, two-handers, and unknown ' +
            'weapon indices remain excluded.') -ForegroundColor Cyan
        Write-Host ('  A stable mapped pickup automatically requests and verifies ' +
            'its reusable Retail collider.') -ForegroundColor Cyan
        Write-Host ('  Seed damage and action haptics are blocked; one manual ' +
            'Retail attack remains only as a failed-retry fallback.') `
            -ForegroundColor Cyan
        Write-Host ('  Collider wireframe: AMBER = automatic seed pending; ' +
            'GREEN = the Retail collision body is live.') -ForegroundColor Cyan
        Write-Host ('  Configure it in VR Tools > COLLIDER: left stick selects, ' +
            'right stick adjusts, A toggles direction/reset; changes auto-save.') `
            -ForegroundColor Cyan
        $diagnosticWatcherScript = Join-Path $PSScriptRoot (
            'watch-condemned-weapon-diagnostics.ps1')
        $diagnosticWatcherArguments = @(
            '-NoLogo',
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f $diagnosticWatcherScript),
            '-Run', ('"{0}"' -f $runLogDirectory),
            '-GameProcessId', $game.Id.ToString(
                [Globalization.CultureInfo]::InvariantCulture))
        Start-Process -FilePath 'powershell.exe' `
            -ArgumentList $diagnosticWatcherArguments `
            -WindowStyle Hidden | Out-Null
        Write-Host "  Live weapon diagnostics: $weaponDiagnosticsPath" `
            -ForegroundColor Cyan
        Write-Host "  Live alignment commands: $liveColliderCommandPath" `
            -ForegroundColor Cyan
    }
    Write-Host 'The headset should show the normal desktop image on a stable mono quad.'
    if ($MenuControlsProbe) {
        Write-Host 'VR menu controls: left stick = navigate; A/trigger = accept; B = back.' `
            -ForegroundColor Cyan
    }
    if ($RetailVrSettingsProbe) {
        Write-Host ('Retail Options probe: verify VR Settings appears between ' +
            'SOUND and PERFORMANCE, then select it once.') `
            -ForegroundColor Cyan
        Write-Host ('  It should open a Retail-native VR Settings page; ' +
            'verify B/Escape returns to Options.') `
            -ForegroundColor Cyan
        Write-Host ('  Display, VR Features, Comfort, and Developer Tools are ' +
            'diagnostic-only in this lifecycle gate; do not select them.') `
            -ForegroundColor Yellow
    }
    if ($CoreActionsProbe) {
        Write-Host 'Forensic tool: push the right stick fully UP (75% threshold).' `
            -ForegroundColor Cyan
    }
    if ($ForensicMemoryProbe) {
        Write-Host ('Forensic state observers are active: no video capture and ' +
            'no game-memory writes.') -ForegroundColor Cyan
        Write-Host ('  Scanner: reach the UV target. Item Camera index 3: ' +
            'continue from the shown camera stage.') `
            -ForegroundColor Cyan
        Write-Host '  Aim using the white alignment arrows and live camera screen.' `
            -ForegroundColor Cyan
        Write-Host ('  Wait for the green light/beep, then pull the VR trigger ' +
            'once. The target trace now follows Retail''s Camera socket.') `
            -ForegroundColor Cyan
        Write-Host ('  Stay in VR if it fails; the new socket/query diagnostics ' +
            'will preserve the evidence automatically.') `
            -ForegroundColor Cyan
    }
    if ($MenuProbe) {
        Write-Host 'Pause menu: Y. VR Tools remains BOTH grips + Y.' `
            -ForegroundColor Cyan
    }
    if ($WeaponGripCalibration) {
        if ($WeaponTest -ne 'Pipe') {
            Write-Host (
                "Live authoring commands: $liveColliderCommandPath") `
                -ForegroundColor Cyan
        }
        Write-Host 'Live weapon-grip calibration is active:' `
            -ForegroundColor Cyan
        Write-Host '  Hold BOTH grips: right stick = X/Y; left-stick up/down = Z'
        Write-Host '  A = position; B = rotation; X = reset; Y = save snapshot'
        Write-Host '  GRIP / 2-HAND menu adjustments and resets auto-save'
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
    if ($ArmIkDiscovery) {
        Write-Host 'Arm-IK discovery is read-only; enter gameplay once so the player-body geometry can be logged.' `
            -ForegroundColor Cyan
    }
    if ($WeaponModelDiscovery) {
        Write-Host 'Weapon-model discovery is read-only: equip the Colt and hold it still until weapon_model_discovery_baseline_ready is logged.' `
            -ForegroundColor Cyan
        Write-Host '  Then fire once and reload once. Motion records report the node name, closed/current model-local positions, peak travel, and candidate axis.'
    }
    if ($SlideControlTest) {
        Write-Host 'Colt slide control is armed: author and SAVE SLIDE GRAB RAIL, close VR Tools, enter the box with the left hand, then press the configured Grip/Trigger.' -ForegroundColor Cyan
        Write-Host '  Release, focus loss, weapon/model generation change, or Retail slide animation removes the callback and restores Retail ownership.'
    }
    if ($ArmIkRightHandProof) {
        Write-Host 'Right-hand IK proof is active: RightHand follows the weighted VR weapon pose; arm and forearm remain Retail.' `
            -ForegroundColor Cyan
    }
    if ($ArmIkRightArm) {
        Write-Host 'Full two-arm IK is armed: the right chain follows the weighted weapon and the mirrored left chain follows the free/support grip target.' `
            -ForegroundColor Cyan
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
        $performanceWatcher = Start-Process -FilePath 'powershell.exe' `
            -ArgumentList $watcherArguments `
            -PassThru
        $performanceWatcherDeadline = [DateTime]::UtcNow.AddSeconds(1)
        do {
            $performanceWatcher.Refresh()
            if ($performanceWatcher.HasExited -or
                $performanceWatcher.MainWindowHandle -ne 0) {
                break
            }
            Start-Sleep -Milliseconds 50
        } while ([DateTime]::UtcNow -lt $performanceWatcherDeadline)
        Write-Host 'Live performance telemetry opened in a separate window.' `
            -ForegroundColor Cyan
    }
    $finalGameFocus = Set-CondemnedVrForegroundWindow $game `
        -TimeoutMilliseconds 1000 `
        -AllowAttachedInput
    $report.GameWindowFocusRestored = [bool]$finalGameFocus.Focused
    $report.GameWindowFocus.FinalHandoff = $finalGameFocus
    [IO.File]::WriteAllText(
        $reportPath,
        ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false)))
    if (-not $finalGameFocus.Focused) {
        Write-Warning (
            'Condemned was ready, but Windows refused the foreground focus ' +
            ('handoff ({0}).' -f $finalGameFocus.Detail))
    }
} catch {
    if (Test-Path -LiteralPath $deployment.LoaderLog -PathType Leaf) {
        Copy-Item -LiteralPath $deployment.LoaderLog `
            -Destination $sessionLoaderLog -Force `
            -ErrorAction SilentlyContinue
    }
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
    if (Test-Path -LiteralPath $deployment.LoaderLog -PathType Leaf) {
        Copy-Item -LiteralPath $deployment.LoaderLog `
            -Destination $sessionLoaderLog -Force
    }
    Write-Host 'Condemned and its M2 host exited.'
}
