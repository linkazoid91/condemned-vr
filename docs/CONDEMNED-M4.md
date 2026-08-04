# Condemned M4 controller-input evidence

This milestone begins only after the native stereo and full HMD-tracking gate
in [`CONDEMNED-M3.md`](CONDEMNED-M3.md) passed. All observations below apply
to the verified Steam `1.0.314.0` executable and stock client in the isolated
no-ASI stage.

## Read-only OpenXR input transport

The first live gate sampled the shared OpenXR input state during gameplay and
performed no engine or DirectInput writes. It confirmed:

- fresh, focused samples and both active-hand bits;
- independently valid aim and grip poses for both hands;
- signed left and right stick axes;
- the complete 0-1 range for both triggers and squeeze inputs;
- primary, secondary, menu, and stick-click button bits on both hands; and
- correct neutralization and recovery when pose tracking was lost and restored.

The loader log recorded `engine_writes=0` throughout this gate.

## Retail command database discovery

Static analysis of the stock `GameOrig.dll` command-registration constructor
found these exact command IDs:

| Command | ID |
|---|---:|
| Forward / Backward | 0 / 1 |
| ForwardAxis | 2 |
| StrafeLeft / StrafeRight | 3 / 4 |
| StrafeAxis | 5 |
| Run | 16 |
| Fire | 17 |
| Block | 28 |
| ToggleMelee / AmmoCheck | 60 / 61 |
| Taser | 62 |
| Activate | 87 |
| Flashlight | 114 |
| Tools | 116 |

The six basic locomotion IDs match the donor F.E.A.R. command database, but
the later action IDs must remain Condemned-specific. The F.E.A.R.-specific
`CBindMgr::GetBindingValue` discovery signature does not match Condemned and is
not reused. A separate full control-flow match identified Condemned's evaluator
at `GameOrig.dll` RVA `0x000095f0`: it calls
`ILTInput::IsDeviceReady`, calls `GetDeviceObjectValue`, then applies the
binding's scale, offset, and dead zone. Two independent Retail callers resolve
to this function. The `CBindMgr::Update` caller at RVA `0x0000ac33` also proves
that Condemned's binding stride is 60 bytes, command ID is at offset `0x08`,
and command activation range is at offsets `0x24` and `0x28`.

## First write-enabled gate

The first attempt hooked DirectInput `GetDeviceState` and ORed W/A/S/D into
the returned 256-byte keyboard state. Live telemetry proved that the hook
covered Condemned's keyboard device and received every direction and diagonal,
but gameplay did not consume that polled state. Physical W/A/S/D continued to
work, proving that movement is resolved later through `CBindMgr`. This failed
gate was removed rather than extended into buffered or system-wide input.

`-condemnedvr-m4-locomotion` now enables a narrow hook on the verified Retail
binding evaluator. It calls Retail first, then may replace only the returned
value for Forward, Backward, StrafeLeft, or StrafeRight (commands 0, 1, 3, and
4). Retail remains responsible for command-state storage, transitions, and
callbacks. Physical bindings are therefore preserved; the hook performs no
direct command-state writes and never calls `SendInput`.

The overlay is neutral unless all of these conditions hold:

- the OpenXR sample is valid and focused;
- its sample ID changed within the preceding 250 ms;
- the left controller is active;
- Condemned owns the foreground window; and
- the binding command is one of the four verified discrete movement commands.

The gate is disabled by default and exposed by the launcher's
`-LocomotionProbe` switch. The next live test is limited to left-stick movement
and simultaneous physical-keyboard fallback; turning and actions remain
unmapped.

This gate passed live on 31 July 2026. The tester confirmed that the left stick
moved the player and that physical W/A/S/D still worked. Telemetry recorded all
four directions, diagonals, and neutral release through
`m4_binding_locomotion_applied`; the hook remained limited to evaluated Retail
binding values with no direct command-state or system-input writes.

The launcher also exposes `-DesktopWindow`, defaulting to 1920x1080, so the
desktop chat and diagnostics can remain visible during headset testing. It
sets Retail's `Windowed`, `ScreenWidth`, and `ScreenHeight` console variables
at startup. This is intentionally distinct from bridge render scaling:
Condemned allocates all resolution-dependent post-processing at the requested
window resolution rather than receiving an independently resized stereo target.
The 1920x1080 window mode was accepted live. The bridge independently observed
a 1920x1080 source and stereo target at 100% render scale, and the tester
confirmed that the desktop game no longer covered the entire display.

Retail normally handles `WM_ACTIVATEAPP(FALSE)` by shutting down the renderer
and continues calling `SetCursorPos` to centre the mouse. In VR that left the
headset on a stale upward-looking frame after Alt-Tab and made the desktop
mouse unusable. `-DesktopWindow` now enables two version-guarded corrections
for Condemned 1.0.314.0: the verified branch at executable RVA `0x0007C5E3`
returns without renderer shutdown, and the executable's `SetCursorPos` import
is forwarded only while Condemned owns the foreground. Controller/gameplay
overlays remain independently foreground-gated. `-NoBackgroundRender`
restores Retail focus behavior for diagnostics.

This focus path passed live on 1 August 2026 in run
`run-20260801-065035`. The tester confirmed that the world continued updating
smoothly after Alt-Tab, the desktop cursor moved freely, returning to the game
was clean, and right-stick recenter still worked. The bridge recorded both
`background_render_fix_applied` and the existing HID/FPS correction before
the test.

## Right-stick turning and pause-menu gate

Static analysis found that the Retail player update unconditionally queries
`CBindMgr::GetExtremalCommandValue` for YawAccel command 23. The verified
function is at `GameOrig.dll` RVA `0x00009900`. The PC profiles do not contain
a command-23 binding, so the earlier per-binding locomotion hook cannot supply
turning. The new `-TurningProbe` gate calls the extremal-value function first
and may overlay only command 23 with the deadzone-adjusted right-stick X value.
A stronger existing command-23 value is preserved. Retail's separate mouse
command 12 remains untouched and is added later by the original player code.

The pause menu has no usable Retail PC binding either. The reversible menu
gate therefore polls the left-secondary button immediately before the verified
`IClientShell.Default` version-4 `Update` callback (vtable slot 3,
`GameOrig.dll` RVA `0x00051150`). On one release-gated rising edge it calls the
same `OnKeyDown(VK_ESCAPE, 1)` and `OnKeyUp(VK_ESCAPE)` callbacks used by
Retail. A held button cannot oscillate the menu, and startup, stale samples,
tracking loss, focus loss, or foreground loss all require a fresh release
before another toggle. The gate is exposed separately as `-MenuProbe`.

The menu gate now follows Retail's exact game state instead of inferring it
from controller taps. `IClientShell.Default` vtable slot 30 is verified at
`GameOrig.dll` RVA `0x0004A5E0` (`lea eax,[ecx+8]; ret`) and returns the
embedded `CInterfaceMgr`; its state is the dword at manager offset `0x08`.
Relocation-safe guards also verify Retail's own state read and comparisons for
playing, screen, and menu. Synthetic Escape is accepted only for playing (1)
and menu (5), so the controller cannot abort loading, splash, movie, demo,
exit, or modal-screen states.

Retail draws those non-gameplay surfaces after the stereo world-camera pass.
The same verified state signal therefore selects the bridge's existing comfort
panel for every known state except playing. In panel mode the renderer performs
one normal camera call, Present captures the completed desktop backbuffer, and
the bridge publishes it to both eyes with stale stereo state cleared. Returning
to playing clears panel mode and native stereo resumes on the next frame.
Unreadable or out-of-range state fails safely to a non-interactive flat panel.

Both hooks require exact version-bound code and vtable guards and are disabled
by default. The launcher now waits for each requested `*_armed` event and aborts
on a matching `*_rejected` event instead of reporting a successful launch with
an inactive input gate. Neither path writes the command database or injects
system input. Pure input-state tests pass on x86 and x64; live acceptance is
complete for locomotion, right-stick turning, mouse coexistence, and one-edge
pause toggling.

The headset menu path was accepted live on 31 July 2026 through the
session-scoped SteamVR OpenXR runtime. The tester confirmed that the pause menu
appears in the headset, remains usable, closes normally, and immediately
returns to stereo gameplay. The loader recorded the exact Retail transition
`playing (1) -> menu (5) -> playing (1)`; the bridge independently recorded
native stereo disabled on menu entry and eligible to resume on menu exit.
Initial splash, demo, screen, and loading states also selected the flat panel.

Physical Escape and desktop menu controls use the same Retail state publisher,
not a controller-side guessed toggle. Repeating those paths and a full movie
sequence remains release regression coverage rather than a blocker for this
bounded M4 slice.

## Interaction and recenter gate

`-InteractionProbe` maps the right squeeze to Condemned's verified Activate
command 87. It shares the version-bound `CBindMgr::GetBindingValue` hook used
by locomotion, calls Retail first, and preserves a stronger Retail value. The
controller overlay is enabled only while the exact Retail state is playing and
the OpenXR sample is fresh, focused, right-hand active, and foreground-owned.
The squeeze becomes active at 0.65 and returns cleanly to neutral on release;
menus, loading, tracking loss, focus loss, stale transport, and background
execution cannot synthesize Activate.

`-RecenterProbe` maps right-stick click through a release-gated edge detector.
During native stereo gameplay it resets both the yaw reference and translation
origin used by tracked-camera rendering. During a flat menu or screen it
delegates to the OpenXR host's existing comfort-panel reanchor path instead.
Held buttons, startup-held state, stale samples, focus loss, tracking loss, and
foreground loss all require a fresh release before another recenter.

This slice passed live on 1 August 2026. The tester confirmed that interaction
and recenter both work in the headset. Loader telemetry recorded command-87
press and release, native `m4_hmd_recenter_requested` followed by
`m3_hmd_recentered`, and flat-panel delegation; host telemetry independently
recorded the panel reanchor requests. The accepted launch also retained the
previous locomotion, turning, menu, physical keyboard, and mouse paths.
Automated input tests pass 19/19 on x86 and 16/16 on x64.

## Core actions and bounded haptics

`-CoreActionsProbe` extends the same verified Retail binding evaluator without
writing the command database or injecting system input. All overlays remain
limited to fresh, focused, foreground-owned OpenXR input while Retail is in
the playing state, and a stronger physical Retail value is preserved.

| VR control | Condemned command |
|---|---:|
| Left squeeze | Run (16) |
| Right trigger | Fire / attack (17) |
| Left trigger | Block (28) |
| Right primary | Toggle melee weapon (60) |
| Right secondary | Ammo check (61) |
| Left-stick click | Taser (62) |
| Left primary | Flashlight (114) |

Manual crouch was deliberately excluded. Command 14 exists in the inherited
engine command namespace, but the live Condemned binding evaluator never
queries it and Retail exposes no manual crouch control. A diagnostic mapping
produced no command-14 telemetry and was removed; right-stick vertical input
therefore remains free.

`-HapticsProbe` enables short confirmation pulses only when VR wins a rising
Retail binding edge: Fire uses 35 ms at 0.25 on the right hand, Block uses
25 ms at 0.18 on the left, and Activate uses 20 ms at 0.15 on the right.
These are bounded input-confirmation pulses, not weapon recoil. A later
verified game-event hook must drive per-shot or impact feedback so empty fire
and automatic weapons are represented correctly.

The corrected layout passed live on 1 August 2026. The tester confirmed that
all mapped controls work and accepted the haptic behavior. Both launcher
gates armed without rejection, the bridge exported the existing OpenXR haptic
transport, and automated tests pass 19/19 on x86 and 16/16 on x64.
