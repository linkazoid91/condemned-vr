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

### VR interaction with Retail menus

`-MenuControlsProbe` extends the verified client-shell key-callback path so
Retail menus can be operated without reaching for the desktop mouse or
keyboard. It requires `-MenuProbe` and maps only these controls:

| VR control | Native Retail menu edge |
|---|---|
| Left stick | Up, Down, Left, or Right arrow |
| Right primary (A) or right trigger | Enter / accept |
| Right secondary (B) | Escape / back |

The stick emits one immediate direction, waits 350 ms, then repeats every
110 ms while held. A dominant-axis rule prevents diagonal input from emitting
two actions. Entering a menu, regaining focus, reacquiring tracking, or losing
either controller requires every mapped control to return to neutral before
input is accepted again. At most one menu action is dispatched per client
update.

Navigation is allowed only while the verified `CInterfaceMgr` state is menu
(5) or screen (6). Live telemetry identifies state 5 as the gameplay pause
menu and state 6 as the front-end/main and options screens; Retail's verified
key callback explicitly handles both. Navigation therefore cannot activate
during gameplay, loading, movies, splash/demo sequences, paused, exiting, or
undefined states. The implementation calls Retail's own
`IClientShell.Default.v4` key callbacks and does not use `SendInput`, write the
command database, move the system cursor, or replace existing keyboard/mouse
handling. The extension passed live headset acceptance on 4 August 2026. In
run `run-20260804-091054`, the tester confirmed navigation and selection in
the pause menu while the loader recorded native key edges in menu state 5.
Run `run-20260804-091433` then confirmed the same controls throughout the
front-end/main menu in screen state 6. The separate launcher gate remains for
diagnostic rollback.

### Retail-native VR Settings entry

Static reverse engineering of the verified Retail `GameOrig.dll` 1.0.314.0
build found a bounded way to extend the PC Options screen without redrawing or
imitating its visual style. This is **static/automated evidence, not a live
menu result**:

- screen ID 4 constructs `CScreenOptions`; its vtable is at RVA
  `0x0014814C`, `Build` is at RVA `0x000CD620`, and `OnCommand` is at
  RVA `0x000CD470`;
- `Build` creates Controls, Display, Sound, and Performance as native
  `CLTGUITextCtrl` objects, then transfers them to `CBaseScreen::AddControl`
  at RVA `0x00009410`;
- the localized and direct-wide-text factories are at RVAs `0x00007160` and
  `0x00006340`. Both accept the same 52-byte control descriptor, so a direct
  wide label can retain Retail's layout, font, focus, sound, and ownership
  path without adding a proprietary string-table entry; and
- stock Options commands `0x37`, `0x38`, `0x39`, and `0x3B` route to
  Display (22), Audio (23), Controls (29), and Performance (25).
  `0x3A` is the sole gap in that table and falls through to the base handler.

The opt-in `-RetailVrSettingsProbe` candidate arms before Retail's
`SetMasterDatabase` lifecycle can construct the screens. It verifies the PE
image size, complete five-slot Options command table, Options vtable entries,
factory/add-control prefixes, Performance-row call site and exact Retail
string anchors. Any mismatch logs
`m6_retail_vr_settings_rejected` and leaves the stock screen untouched.

When the verified Performance construction call is reached, the hook clones
that exact 52-byte descriptor, changes only its command and help fields,
creates the wide label through Retail's native factory, and gives the control
to Retail's own `AddControl` before forwarding Performance creation. Retail
resolves position at construction time, so the intended order is Controls,
Display, Sound, VR Settings, Performance. The command hook accepts `0x3A`
only when the live object still has the exact `CScreenOptions` vtable.

The first candidate used the literal `VR SETTINGS` and deliberately handled
selection without changing screens. Canonical run `run-20260813-045556`
created it once at native control index 7. The tester saw it in Options and
reported the uppercase literal and no destination; six structured selections
confirm that `0x3A` reached the hook. This is a live row/visibility/dispatch
boundary, not a completed settings screen. Its staged loader SHA-256 was
`4F5369A915160522E73C31E080F2AF80B7A6C690DF5A9AA4258AC49D080BA274`;
the preserved loader log SHA-256 is
`6F5E45F2330843C3E7B8BB73A9D4A94004F1517B02C87E28DEF2CE74A068D55C`.

Further static work found an otherwise-unlinked native PC `CScreenGame` at
registered screen ID 24. The screen-manager factory table at RVA `0xCCBC4`
maps ID 24 to branch `0xCCB2B`, factory `0xCBA30`, a `0x1D0`-byte allocation,
constructor `0xC5CB0`, and vtable `0x14732C`; `OnCommand` is `0xC5D20` and
`Build` is `0xC6080`. Build passes `IDS_TITLE_GAME_OPTIONS` to the localized
title path at `0x5F40`. The direct-wide title renderer is `0x5E70`. Its
dormant controls include Crosshair, Subtitles, Gore, Run Lock, Difficulty,
Pickup Message Duration, Head Bob, Auto-switch Weapons, Slow-motion Effects,
and HUD Fade Time.

The remaining lifecycle map explains the safe reuse boundary. `OnFocus` is at
RVA `0xC5E70` and destructor at `0xC5D50`. Comparing the complete callable
portion of the `CScreenGame` vtable at `0x14732C` with the base-screen vtable at
`0x138E14` shows only four differences: destructor, `OnCommand`, `Build`, and
`OnFocus`; slots 2 through 32 otherwise match exactly. The base routines are
`OnCommand` `0x5BC0`, `OnFocus` `0x8370`, `AddControl` `0x9410`, finalizer
`0x94C0`, and `Build` `0x9540`. The original screen Build calls base Build and
then the finalizer with `(true, true, false)`. Its destructor already delegates
directly to the base destructor at `0x92D0`. These are static reverse-
engineering findings, not proof of a safe live replacement.

The follow-up guards that exact factory/constructor/vtable/title chain, changes
both literals to `VR Settings`, and routes `0x3A` through the live screen
manager's switch slot at vtable offset `0x40`. The target must remain an
executable `MEM_IMAGE` page owned by the verified GameOrig module. The full
headset-free gate passes 23/23 x86 and 19/19 x64; staged loader SHA-256 is
`CFA47C01411F9B01AAEFC5D97B84D14D65330805AE2370B4D1751A32D39DBFD2`.

That original native child candidate is **not accepted as a safe final host**.
`run-20260813-051426` armed successfully but crashed before menu construction
at `GameOrig.dll+0x539AE`. Retry `run-20260813-051528` created the row and
title, recorded four screen-24 entries and three Back edges during extensive
menu input, then crashed at `ClientFx.fxd+0x26EEF`. The retry loader log is
preserved at SHA-256
`AB67C0C3045B30DC2B3368741AD8CA8965490D70B9EE17651471B0E35DA6D73C`.
The different first fault and lack of a direct screen/control trace leave
causality unresolved, but the second crash was enough to stop broad use of the
dormant controls.

The requested `tools/capture-condemned-window.ps1` now provides the missing
visual evidence path. It fails closed on PID, root-owner, foreground, minimized
or empty-client mismatches; captures the visible client through
`CopyFromScreen`; and writes a PNG plus JSON rectangle/identity/hash sidecar
to the newest run. Offline `-ValidateOnly` passes.

After explicit tester confirmation, `run-20260813-054534` entered screen 24
once and captured the exact foreground 1930x1090 Condemned client before any
dormant control was selected. The screenshot live verifies a title-case
`VR Settings` heading and the native parchment, pinned-note, font, selected-row,
and two-column visual language. It also shows why the dormant content cannot be
shipped: Crosshair, Subtitles, Blood, Always Run, Difficulty, Message Duration,
Head Bob, Auto-switch Weapons, HUD Fade Speed, and invalid-string placeholders
are visible. The page was then left untouched. No Back event was recorded, and
the process later reproduced `ClientFx.fxd+0x26EEF` in the same WER bucket as
the retry (WER report `dd2eb7c3-f8e7-4e5f-ac43-0824df5173e3`). At that point
the repetition was treated as lifecycle correlation. A later 13 August audit
of Windows Application Error history found 59 identical
`ClientFx.fxd+0x26EEF` faults from 7 August through 13 August 14:18, all before
this menu work. The fault is therefore a pre-existing shutdown baseline/
confounder and cannot establish screen-24 causality. The original page remains
rejected on direct visual evidence: unrelated/incomplete controls, invalid
strings, and unaccepted dormant settings behavior. Preserved SHA-256 values:

- loader log:
  `8579639CECEF66DB40A5A07CE50D801881C1323747D74D11A798180BF58CB6C7`;
- PNG:
  `E50ABCB564976B524852C5A0A3EC79BE1F2D8CF26854CC7ED221AAF808913C1D`;
- JSON sidecar:
  `8EC6E5F94A5E4C34CA891C585B55D2EFBF1769F7BE40E3A47DE8EA8F7FC1CC78`.

The replacement candidate is **partially live verified; its entry-input fix is
implemented and automated-tested, awaiting live validation**. It keeps the
registered screen-24 object so Retail owns the
base shell, back stack, control vector, drawing, sounds, and destructor, but
hooks and completely bypasses the unsuitable original `Build`, `OnFocus`,
and `OnCommand`. The replacement:

- guards the PE image, full inherited vtable, factory/constructor, original
  call chain, and every direct base routine before installing;
- captures the verified Performance row's 52-byte descriptor before injection;
- sets `VR Settings` through Retail's direct-wide title path;
- creates Display, VR Features, Comfort, and Developer Tools through Retail's
  direct-wide control factory and native `AddControl` ownership handoff;
- calls only base Build, the verified `(true, true, false)` finalizer, base
  focus, and base navigation command handling; and
- treats the four category commands as handled diagnostics with no settings
  reads or writes.

Explicitly approved run `run-20260813-063732` used loader SHA-256
`B1B860D25C06DAD0725AA87E530D89F09B1FFB2E8CACD178742A979516DE598D`.
The isolated page built and focused once with controls at indices 0 through 3
and all three original routines bypassed. Exact-foreground screenshot
`condemned-window-20260813-064001485.png` has SHA-256
`2223D0C12BCBBE629ED2BC245C267D7D81E462FFD58542E1D6B9BEDAB5E92C8D`;
its JSON sidecar has SHA-256
`D3B8332499B03C3D69910E399CACFA9DCCA8D1F87FC0CF27B4CEA3797931913A`.
The image shows the normal-case title, exactly four intended rows, native
Retail styling, and no invalid strings. One Back edge caused base focus-out,
so the entry, rendering, and Back boundary is live verified.

The run failed a distinct input sub-gate. Ordered telemetry recorded Display
command `0x70` before `m6_menu_control_dispatched` reported completion of the
Enter edge that opened the page. This localizes the activation to the same
internally generated key-down/key-up call rather than a later deliberate
selection. The diagnostic placeholder handled it with
`behavior=placeholder_no_state_mutation`, so no setting changed. Preserved
loader-log SHA-256 is
`E97CBEBB28678D45BB2A6DDEBE8CE9EA9AF39765D4A46F541AE28E69FC795D1B`.
Shutdown again recorded the longstanding ClientFx fault (WER report
`2024f02f-6e3f-436f-a83c-574d4e26aadf`); it remains unassignable to this
feature without a matched no-menu-probe control.

The follow-up brackets only the mod's internally generated menu key edges. If
Options opens VR Settings during a VR Enter edge, a category command occurring
before that edge's native KeyUp returns is handled and logged as
`m6_retail_vr_settings_entry_category_suppressed` with
`settings_mutated=0`. The bracket then clears; later deliberate VR selection
and physical keyboard/mouse input remain Retail-owned. The full headset-free
gate passes 23/23 x86 and 19/19 x64, and the project-local stage now carries
loader SHA-256
`A9AD752A4A6D25ACCDFB44AFED9D8539A3FB837280C18BCE733EFEB8446B749A`.

Explicitly approved validation run `run-20260813-071147` used that staged
loader. Its source identity was Git HEAD
`93dab5242e52c4c4d56453b3464001a256ff9262` plus working-tree menu sources
identified by SHA-256: `retail_menu_integration.cpp`
`B0F40F122D72899FA0CA393B24696206A63207D3A932F28D3253D26496AE5F97`,
`retail_menu_integration.h`
`255DCFB3B252832B0F98F333501AAA7300AD6EBDD8EB052B8197D7CABB0FD0AE`,
`binding_input.cpp`
`4C90AE0057408A83066798738977CD10F92F1DA03FA4442C6F2A4C2882290082`,
`condemned_retail_menu.h`
`851276259480CB2BB0A01B5A64B50D4C475B7204108C88B00ADC5A370FCEB0EF`,
and `test_condemned_retail_menu.cpp`
`8F2A9D95E36BBD9DFC68495E9D5C0A0455A5FA2D6968FA53A40DB33DB4C17619`.

The page built once and retained the same object across two VR-generated
entries. It recorded two `m6_retail_vr_settings_selected` events, active and
inactive base-focus counts 1 through 4, zero category selections, zero
same-entry-edge suppression events, and zero settings mutations. The first
focus-out followed an explicit VR Back edge; after the tester confirmed the
second page was visible, the second focus-out occurred during the requested
exit. This live verifies clean behavior for those two entries, but it does not
live-exercise the defensive suppression branch. That branch remains
implemented and automated-tested rather than live verified.

Exact-foreground screenshot `condemned-window-20260813-071346302.png` captured
the 1930x1090 client and is pixel-identical to the prior accepted image, with
SHA-256
`2223D0C12BCBBE629ED2BC245C267D7D81E462FFD58542E1D6B9BEDAB5E92C8D`.
Its JSON sidecar SHA-256 is
`B5AE68BD3F7CA9B003467E4423BA6BF11AAC88AFA8BBAD55E151A7EC5B20F46F`.
Preserved loader-log SHA-256 is
`4031429705448FBF40396F875F33BD0C21F12B4BDF679DB6A1A448A120B20A09`;
launch-report, host-log, and bridge-log SHA-256 values are respectively
`3A92BC11803CD3E78BB700A67280FB642A737AF95419DA1DDAE41A0410B9870A`,
`BFED36F9161F483FB7C74EDA82BEB88DCCB30E759C52093BC3AC41165751CAE6`,
and `4AEDAD67EFE17127262802E124650D26123D4506AFA474E43F10BB12EE35969C`.
Shutdown reproduced `ClientFx.fxd+0x26EEF` (WER report
`cc156935-3775-472a-97af-d2e8c686fa9e`), which remains part of the 59-plus
pre-menu baseline and is not attributed to this feature.

The tester then narrowed the Developer Tools requirement: it is not a category
destination or debug-draw submenu. It is one Retail-native boolean row that
enables or disables the existing VR Tools opening shortcut. That correction is
now **partially live verified**:

- the fourth row reads `Developer Tools: On` or `Developer Tools: Off` and
  command `0x73` toggles only both grips plus the controller-specific left
  secondary button (B on the tester's controller, Y on Touch) and its F12
  keyboard fallback;
- renderer capability remains a separate gate. Disabling the preference does
  not close an already-open overlay and does not cancel its release capture, so
  the user retains a safe close/neutralization path;
- `[developer] tool_menu_shortcut=1,0|1` is stored in the existing global user
  INI. The packaged value is enabled to preserve established behavior, a
  missing value also preserves enabled behavior, and malformed data fails
  closed to disabled;
- writes are transactional at this boundary: persistence succeeds before the
  live atomic preference changes; and
- Display, VR Features, and Comfort remain handled, non-mutating placeholders.

Static analysis of the verified Retail image identified the exact existing
`CLTGUITextCtrl` label path instead of introducing a speculative control ABI.
The text-control vtable is RVA `0x150AE0`; slot `0xD8` points to the wrapper at
RVA `0x12BA40`, which adds the verified embedded-text offset `0x54` and
tail-jumps to the wide-string setter at RVA `0x1330A0`. Installation guards the
wrapper bytes, jump target, setter prefix, and vtable slot. Each created row
must also retain that exact text-control vtable before its label can change.

Git HEAD remains `93dab5242e52c4c4d56453b3464001a256ff9262` with a dirty working
tree. Relevant post-change source SHA-256 values are
`retail_menu_integration.cpp`
`D2C78FA4D88E546D9D52E79340CD38D627C9A0D2C87E7D665A69B588E9134567`,
`renderer_probe.cpp`
`4D3AEEAC5BACDCFC35768B69D626B303B255C1F0FB99F1AAC710FA1E0A2B2D7C`,
`weapon_settings_store.cpp`
`969E12200259A68C29E82DB155FB9ADCFCA153B6B17C0E588BB759277D670BB3`,
`condemned_retail_menu.h`
`E1490999BF7DC96FF127DF118B7F2FA09B871D5C7DB79511BAE8FEE0728DEDCC`,
and `condemnedvr-defaults.ini`
`CB878DEA217206B2D47D42EF0F9F6C0FB822353CC3E17F2AFE84D762C30F4B5A`.
The normal full gate passes 23/23 x86 and 19/19 x64 plus the launcher-focus,
screenshot-helper, and diagnostics-watcher checks. The refreshed project-local
stage loader SHA-256 is
`E44FBB97548C24836E4E26A1AF02D0E62E4DE3938F068EED51CB09CDDC6AA8F6`.
No game/headset launch had been performed at that staging boundary.

The tester then explicitly approved launch `run-20260813-132327`. Run metadata
records game PID 4572, host PID 49268, the Pipe preset, Retail VR Settings
enabled, and successful foreground ownership at all three handoffs. Startup
loaded `tool_menu_shortcut` with
`result=ok enabled=1 capability_available=1 fallback=stored_or_packaged`.
The isolated page built with four native controls at indices 0 through 3,
`developer_tools_enabled=1`, and `settings_ready=1`; focus refreshed the native
label successfully. One deliberate command `0x73` produced the ordered events:

```text
m6_vr_tool_menu_shortcut_saved
  result=ok enabled=0 runtime_mutated=1
m6_retail_vr_settings_developer_tools_changed
  previous=1 requested=0 saved=1 current=0 label_refreshed=1
```

The tester confirmed that the toggle worked, then clarified that with the row
Off VR Tools no longer appeared when pressing both grips + B. The code gates
the left-secondary OpenXR action; its physical label is controller-profile
specific (B for this observation, Y on Touch). This is **live verification** of
the initial On state, native control dispatch, successful persistence write,
live atomic change, immediate Retail-label refresh to Off, and suppression of
the controller opening chord. It is not proof that Off blocks F12, that On
restores open/close and neutral-release capture, or that the saved Off value
reloads in a new process.

The launcher-monitor command timed out after successful startup, while the game
and host continued normally, so no finalized launch report exists. The run's
`m2-mono-live.json` SHA-256 is
`56C0E88D55B4EAB8A55EB1DCECA262394EF21DFE7574E325228714D993190525`.
The manually preserved loader log SHA-256 is
`1CE76E5407C1F1EAC6F0F0E721E7BA8B1F583E89790302E5EF8BF3EB6FE64C30`;
host and bridge log SHA-256 values are
`3839C43794BCF43044F3B262E64DC9A88D1D62DA40823E13A2B296435D870F61`
and `77A7F8F19F58CFB0CAAEA42ACE1111DF011C1533BC0CD4081367547D2B506265`.
Shutdown again reproduced the longstanding `ClientFx.fxd+0x26EEF` fault (WER
report `8b491332-38a1-4566-8c4e-c9d2cb7d83ba`), which remains baseline evidence
rather than toggle causality.

The next plan is bounded and every live launch still requires fresh explicit
confirmation:

1. capture the already-live-verified row state in both visual states if a
   retained visual record is still required;
2. prove Off prevents F12 from opening VR Tools, then prove On restores the
   controller and keyboard open/close paths plus neutral-release capture;
3. with separate confirmation for a later launch, prove fresh-process reload
   and restore the tester's original preference; and
4. only then add one low-risk control at a time to the other three categories.

The longstanding ClientFx shutdown fault is tracked separately. A paired run
with this probe disabled is required before any shutdown WER event can be used
to accept or reject the Retail-menu implementation.

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
produced no command-14 telemetry and was removed. That gate left right-stick
vertical input free.

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

### Forensic tools extension

Static analysis of the same Retail control-registration constructor confirms
that `IDS_CONTROL_TOOLS` is command 116. The 10 August 2026 working tree maps
a deliberate right-stick-up gesture (at least 0.75 on the vertical axis) to
that command through the existing guarded binding evaluator. This preserves
Retail's keyboard binding and stronger Retail input; it performs no command
writes and no system-key injection.

Plain Y remains the separately release-gated pause-menu control. Both grips +
Y remains the VR Tools chord. Horizontal right-stick turning cannot activate
command 116, and stale, unfocused, non-playing, calibration-captured, or
tool-menu-captured samples remain neutral through the existing gates.

Run `run-20260810-080738` **live-accepts forensic readiness** on the staged
loader `C950716B4690A3411C6E0FF18B3CFCCC7FA75D34E307CC65ACB701DD05B6DE94`.
The headset tester confirmed that pushing the right stick up pulls out the
forensic tool. The core-action gate armed with command 116 and recorded 22
press plus 22 release transitions at `game_state=1`, each with
`control=right_stick_up`, `retail_value=0`, and the expected 0/1 output. There
were no core-action rejections. The same run recorded 13 ordinary Y pause-menu
dispatches through the existing release-gated path.

The tester further described the live contextual sequence: the first Tool
press deploys a left-hand UV light, which already works well; finding its target
prompts another Tool press, which changes to a right-hand camera; native
left-stick zoom also works. Retail then expects Fire/click to take the photo.
VR already maps right trigger to Fire command 17. Live run
`run-20260810-103612` initially raised a digital-value hypothesis: sixteen VR
Fire presses reached command 17 with `controller_applied=1` and
`output_value=1` without taking the photo, while a later physical mouse edge at
Retail value `128` took it and was followed by Scanner index 46 changing to
`cell_phone` index 4. The tester had switched to flatscreen and realigned with
the mouse before that successful edge, however, so this was not a controlled
same-aim comparison.

Run `run-20260810-105904` falsified the value hypothesis. Active VR Fire was
temporarily emitted as `128`, yet it still did not take the photo. Two physical
mouse Fire edges on the same native binding also failed while the Scanner
state remained unchanged. Later in that run the same VR trigger successfully
fired `colt45_Unbreakable` nine times: the observed weapon fields changed from
9 to 0 and 0 to 9 while Retail's fire-vector path ran. The binding declares a
minimum command value of `0.1`, so the original VR value `1` was already active.
The temporary magnitude adaptation has therefore been removed. Command 17,
controller transport, and release behavior work; the remaining forensic issue
is camera target/aim eligibility.

#### Observation-only forensic memory trace

Static analysis bounds the diagnostic instead of dumping the process.
`CClientWeapon::Init` calls the weapon-display factory at
`GameOrig+0x00028220` and stores its result at `CClientWeapon+0x90`. The
verified Scanner wrapper at `+0x000FA100` allocates `0x640` bytes and reaches
the constructor at `+0x000F7D80`, whose vtable is `+0x0014AB44`. The verified
DigitalCamera wrapper at `+0x000FA0E0` allocates `0x230` bytes and reaches the
constructor at `+0x000F9890`, whose vtable is `+0x0014AB80`. Those facts verify
factory implementations in the executable; they do not prove that the live
pointer at `weapon+0x90` is the final Scanner or DigitalCamera subclass object.
The probe observes the bounded current-weapon span `+0x80..+0xFF`, the verified
weapon-manager roots, and a derived tail only when the live vtable actually
matches one of those known classes.

The opt-in launcher switch `-ForensicMemoryProbe` requires
`-CoreActionsProbe` and `-MenuProbe` and automatically enables the read-only
weapon catalog. Exact factory-call, allocation, constructor, vtable-immediate,
current-weapon, display-update, and command-dispatch code signatures must match
before `m5_forensic_memory_probe_armed` is emitted. A mismatch emits
`m5_forensic_memory_probe_rejected`; normal core controls remain armed, but the
memory trace stays disabled.

The diagnostic hooks the verified Scanner and DigitalCamera update functions
solely to discover their actual live `this` pointers. It observes Retail's
`ClientShell::OnCommandOn` and `OnCommandOff` callbacks and snapshots state
immediately before and after Tool, Fire, or Activate dispatch. Scanner static
analysis classifies `+0x1DB` as target hit, `+0x1DC` as the separate framing
test, and `+0x1DD` as their final `can_photo` conjunction. Scanner's vtable
function at `GameOrig+0x000F46F0` refuses normal use while `+0x1DD` is zero.
The working observer therefore records six bytes from Scanner `+0x1D8` and
four bytes from DigitalCamera `+0x208`, including the three classified Scanner
values on every command edge.

Each Tool (116), Fire (17), or Activate (87) edge records a pre-Retail
snapshot, then samples after Retail update frames 0, 1, 2, 4, 8, 16, 32, 64,
128, and 256. Root transitions, snapshot hashes, and changed aligned 32-bit
words are logged with a trace ID. The last settled observation is also compared
with the next edge, covering state changes while the UV light is searching.
Reads are range-checked with `VirtualQuery` and exception guards. This path
performs no engine calls, object mutation, whole-process dump, system input, or
video capture; every event explicitly records `engine_writes=0`.

Run `run-20260810-103612` live-armed this read-only trace and recorded 44 command
edges, 44 edge snapshots, 163 post-Retail samples, 15 root transitions, and
four completed traces with `engine_writes=0`. It recorded no aligned word diff.
The `weapon+0x90` product stayed at `0x029DDEB0` across distinct equipped
objects and did not resolve to either expected GameOrig vtable, so the earlier
direct-subclass/derived-tail assumption is **rejected by live evidence**. The
next trace logs the raw vtable address and binding metadata rather than treating
that failed assumption as an acceptance requirement.

The preserved `run-20260810-105904` loader trace is
`stage/condemned-m2-mono/logs/run-20260810-105904/condemnedvr-loader-snapshot.log`
(SHA-256
`67BB633813E5562FD01972706EB3976DB1487B9BF2C2587C883AAB4CA49439E8`).
Loader `F242036E6D975389B7394B014FE37628CABD72C874C1736BB67800FDFD6A9E77`
live-armed both observers in `run-20260810-112423`. The verified Scanner update
resolved the actual object at `0x0CC8A098`, with vtable RVA `0x0014AB44`; the
scene used equipped Scanner index 46 and did not instantiate DigitalCamera.
Two earlier VR Fire edges reached Retail while only the target-hit byte was
known to be one and did not take the photo. A physical mouse edge later reached
Retail while target hit was zero and also did nothing.

A bounded read-only sample of Scanner `+0x1D8..+0x1DF` then exposed the full
gate. During a 20-second sweep, target hit became one and the framing plus
`can_photo` bytes toggled together from zero to one five times. The headset
tester confirmed that controller movement drives this forensic aim, head aim
does not, and `can_photo=1` corresponds exactly to Retail's beep and green
camera light.

The same run preserves a complete successful flatscreen reference. Trace 20
delivered physical Fire `128` with the four logged Scanner bytes at
`0x01000001`; the next verified update changed them to `0x01010101`, then
`0x01010100`, and Scanner index 46 changed to `cell_phone` index 4 before
trace 21. Trace 20 therefore took the photo; trace 21 was later post-photo
input. The older command observer omitted `+0x1DC..+0x1DD`, so this proves the
Retail outcome but not all three gates at the exact mouse edge. The final
preserved log is
`stage/condemned-m2-mono/logs/run-20260810-112423/condemnedvr-loader-snapshot.log`
(SHA-256
`7350A928463DCFED3937447790ACAA55796301A6DF51F63C01D556BDB10900B9`).

The expanded six-byte loader
`4413F08686DDBC9AD24FF5790DC60D1311BA8B01EC94DA0044B603C1DB56FA14`
live-armed in `run-20260810-115015`. One right-trigger Fire down reached
Retail with `controller_active=1`, output `1`, and all three Scanner gates
equal to one before and after `ClientShell::OnCommandOn`. Its matched release
also retained all three gates. Scanner remained index 46 through 256 sampled
frames, and the tester observed no flash or photo, only the expected VR Fire
haptic. The eligibility-gate hypothesis is therefore **falsified**: target,
framing, final `can_photo`, command transport, and release timing were all
valid in the same failed attempt. The final loader snapshot is
`stage/condemned-m2-mono/logs/run-20260810-115015/condemnedvr-loader-snapshot.log`
(SHA-256
`C32359EA48CB5B0C7F59CDA18586A25DFD5926AF5C918CAC33A781B8BF1B97A2`).

Static analysis maps PlayerMgr command-on to `GameOrig+0x000A0C30`. Its
command-17 branch uses Scanner vtable RVA `0x0013B46C`, forensic type `0x15`,
and the nonzero Fire-ready byte at weapon `+0x303`. It can reach generic
`CClientWeapon::Fire` at `+0x00024D90` through callsite `+0x000A13C7`, but it
also has a collection-tool special callsite at `+0x000A1351` targeting
`GameOrig+0x000E8F00`. The special path returns before generic `Fire`, so a
missing generic Fire marker does not by itself prove rejection.

The route-marker loader
`26360CAC206D32972EE8012B3AC49044430FEBDBD0F138F062269684CEDA8EBE`
live-armed in `run-20260810-121200`. It captured 32 PlayerMgr Fire callbacks;
24 were Scanner-46 callbacks with target hit, framing, and `can_photo` all one.
None entered generic `CClientWeapon::Fire`. The sole physical mouse-`128`
edge had all three Scanner gates zero and is not a valid route control. The
completed loader snapshot is
`stage/condemned-m2-mono/logs/run-20260810-121200/condemnedvr-loader-snapshot.log`
(SHA-256
`925715B6773C396CB58518F80BF5414E8DA904BC0CFF57B97AA946DEFB35F52F`).

The branch/collection observer build logs raw PlayerMgr fields plus special
collection-action entry/result/state without writes. It passes 19/19 x86 and
15/15 x64 tests; SHA-256
`4B1B05040114790C0E4DD389048CAD46EC001082295B16C6AF1EA492C717841F`.

Live `run-20260810-123242` recorded five right-trigger Scanner-46 PlayerMgr
callbacks. Every branch input had `player_mode=1`, type `0x15`, weapon state
`+0x218=1`, Fire-ready one, owner null, and all three Scanner display gates
one. None entered the collection action or generic weapon `Fire`. This moves
the stop ahead of both dispatches.

The command-17 branch calls the PlayerMgr `+0x20` target-query object's
vtable slot `+0x1C` to copy a cached target result. It requires result kind
three at copied `+0x1C`, a non-null target reference at `+0x2C`, then
additional target-class checks before calling `GameOrig+0x000E8F00`. In the
live process, PlayerMgr was `0x089EE180`; its query object was `0x02CCF388`
with vtable RVA `0x00149A70`. Slot `+0x1C` resolved to the copy getter
`GameOrig+0x000EA500`. A read-only sample of its cache at query `+0xB4`
showed kind zero and target null immediately after the failed green triggers.
The Scanner display and activation branch therefore had different aim
contexts.

The completed baseline loader trace is
`stage/condemned-m2-mono/logs/run-20260810-123242/condemnedvr-loader-snapshot.log`
(SHA-256 `5F3E15664DAEC3D14EA0C32FFA7FF690B1DBA8F559AD1683EFDFB56C5DEB2B77`).


The query updater at `GameOrig+0x000EA010` calls acquisition routine
`+0x000E98D0`. That routine clears the cache, gets a transform through the
engine-client global at `+0x00169EB8` and vtable slot `+0x50`, constructs a
desktop-camera segment, and invokes the normal engine intersection method from
return RVAs `+0x000E9BCE` and `+0x000E9BEE`. The live vtable slot
`+0x7C` resolves to `Condemned.exe+0x000095C0`. Retail then performs its
own filter, result conversion, target classification, and cache ownership.

The exact-callsite query hook verifies the GameOrig target routine, both
intersection callsites, the live engine vtable, executable thunk, and
dispatcher global before it can arm. It redirects only those two calls, only
in playing state with equipped weapon type `0x15`. The first experiment fed
fresh OpenXR right-controller origin/+Z into that hook. It preserved Retail's
segment length, flags, filter, result buffer, target classification, and
action dispatch, then restored the first 24 caller-stack bytes immediately
after Retail returned. No persistent engine state was fabricated.

Completed live `run-20260810-134726` falsifies raw controller-forward as the
Scanner optical axis. Its preserved snapshot contains 209 bounded query
records, 167 engine intersections, and zero query-restoration failures.
All 89 Scanner-ready PlayerMgr branch records still held cached kind zero;
none reached kind three, collection action, or generic weapon `Fire`. The
persisted Scanner grip record is position `(-0.15,-0.90,0.65)` and local
rotation `(20.0,85.5,25.5)` degrees. The visible model solves
`desired-controller-grip * inverse(model-local-grip)`, whereas the failed ray
ignored that optical correction. The tester independently observed that the
camera and raw controller forward directions disagree.

The completed trace is
`stage/condemned-m2-mono/logs/run-20260810-134726/condemnedvr-loader-snapshot.log`
(1,412,180 bytes; SHA-256
`7881DF6FCA814885DA78288CEF1751744061B261FCD5D4259D9BC66BBAE42ACC`).

The forensic camera is presented as white alignment-arrow graphics plus a
separate camera body/screen, and the embedded view follows the arrows.
Scanner and DigitalCamera vtables both map slot `+0x24` to
`GameOrig+0x000F4CB0`. Scanner calls that slot at `+0x000FC0C0` before
writing target-hit state; DigitalCamera calls it at `+0x000FC6CD`. The shared
function resolves the inline socket name at display `+0x1AC`, then requests
its world transform through Retail's model interface. A bounded live read of
Scanner display `0x0CC1A098` confirmed that the socket name is `Camera`.

The implementation adds a second fail-closed hook at
`GameOrig+0x000F4CB0`. It verifies the invariant named-socket/world-transform
code, both display vtable entries, and both virtual-call encodings. Only
successful finite `LT_OK` outputs are captured, with separate coherent Scanner
and DigitalCamera snapshots under a reader/writer lock.

The target-query hook requires forensic type `0x15`, then selects by the
verified stock catalog identity: index 46 maps to Scanner, index 3 maps to
DigitalCamera, and transitional index 6 (`CollectionToolBase`) or any unknown
index receives no override. Compile-time assertions lock those mappings. The
selected pose must be no older than 250 ms. The candidate segment uses socket
position and socket-local +Z; Retail's range and all non-geometry fields remain
unchanged, and the caller query is restored after intersection. Missing/stale
matching socket data takes the untouched desktop-query path.

`m5_forensic_camera_socket_pose` records display kind, socket sequence, world
pose, and candidate forward. `m5_forensic_camera_socket_ray_query` now also
records `pose_display_kind`; the fallback records `expected_display_kind`.
The paired hooks arm as one unit or are both removed.

The Scanner-only build passed 19/19 x86 plus 15/15 x64 tests at x86 SHA-256
`91B60E0EFEA049599EDF221A40D28957C2DC997D655C7ED6731966530CD02512`.
Live `run-20260810-143142` headset-accepts it: PlayerMgr read cached kind
three with non-null target `0x38EEF848`, the native collection action ran with
`handled=1`, and the tester confirmed the visible photo. The frozen checkpoint
is
`stage/condemned-m2-mono/logs/run-20260810-143142/condemnedvr-loader-photo-success-checkpoint.log`
(431,677 bytes; SHA-256
`3F29DCB159E7F9504E2F1E1E375C62603EA90CDE5B8474924168E294DB62AD25`).

The Scanner-only run then selected stable index 3, `Camera` / `WEAP_Camera`,
published fresh DigitalCamera socket poses, but used stale-Scanner fallback and
left the cache at kind zero. The identity-to-snapshot candidate passed 19/19
x86 and 15/15 x64 tests at SHA-256
`FD2311E189650AC3DD79FB7A887D0DBDCA40487064180CD385E69FF36655F638`.

Live `run-20260810-145113` headset-accepts index 3. Its query records identify
`pose_display_kind=digital_camera`; the successful edge had DigitalCamera state
`0x01010101`, PlayerMgr cached kind three with target `0x38B4F9A0`, and the
native collection action returned `handled=1`. The tester confirmed the
visible result. Preserve
`stage/condemned-m2-mono/logs/run-20260810-145113/condemnedvr-loader-item-camera-success-checkpoint.log`
(1,393,735 bytes; SHA-256
`1DC321F8DDA0C61BC7FEE84D14BE47C530636CC4A782E7A1CD68F0610A870047`).

#### Handgun visible-barrel direction candidate

The next isolated defect is handgun aim. In the accepted Item Camera session,
the tester equipped stable index 76, `colt45_Unbreakable` (weapon
`0x0CBEF960`, model `0x38BBE120`), and reported that bullets did not follow
the visible handgun. Its persisted visible grip is position `(0,0,-6)`, local
rotation `(-8,100,16)` degrees, quaternion
`(-0.150755,0.750501,0.142157,0.627545)`. Existing
`HookGetFireVectors` records at caller `GameOrig+0x0002D1E6` showed the
cause: the hook replaced Retail right/up/forward with raw controller axes while
the rendered gun used the separately calibrated model-local grip.

Static analysis of the verified Retail client found the asset-defined path.
`GameOrig+0x0004C0A0` resolves a named model socket through the interface
global at `+0x00172EC0`, calling model vtable slot 1 for the handle and slot 2
for its transform. The helper's call encodings are verified at offsets
`+0x3A` and `+0x6A`; the stock strings `Flash` and `Breach` live at
`+0x0013DDF0` and `+0x0014B1A8`. Retail's right-hand display path selects
`Flash` near `+0x000FD6BF` and `Breach` near `+0x000FD7DD`.
`ILTModelClient.Default` slots 1 and 2 must still resolve exactly to
`Condemned.exe+0x000378E0` and `+0x000381D0`, matching the already verified
Arm IK model ABI.

The first candidate was deliberately direction-only and limited to index 76.
It reconstructed the visible model transform as
`desiredGripWorld * inverse(modelLocalGrip)`, required local `Flash` and
`Breach` transforms, and derived forward from their world-space
`Breach -> Flash` positions. `Flash` socket roll supplied the orthonormal
right/up basis. Retail's `firePosition` remained untouched; the transformed
`Flash` position was diagnostic only.

The runtime gate requires the exact current weapon and model references, current
saved-grip generation, fresh finite weighted weapon pose, current verified model
interface, and successful finite unit-scale socket transforms. A two-point
source additionally requires plausible 0.1--200 unit separation. Failure uses
raw controller aim, then untouched Retail vectors when tracking is stale.
Non-index-76 weapons do not enter the candidate.

The first candidate binary had x86 `GameClient.dll` SHA-256
`EF482E911088E3DBDBF708C66A8462CA7E3AE120F1C4AB6B47E1849A5A699B3B`.
Live run `run-20260810-155025` armed that binary and observed nine index-76
fire-vector calls. Every record had `Flash_handle=2`,
`Breach_handle=4294967295`, `result=Breach_socket_unavailable`,
`direction_applied=0`, and `fallback=raw_controller`. The first resolver
returned before querying the Flash transform, so its zero `Flash_local` fields
are not evidence that the Flash transform is zero or invalid. There was no
`m5_handgun_muzzle_aim result=applied` record: the shots prove native firing
and fail-closed fallback, but provide **no candidate alignment evidence**.
Preserve
`stage/condemned-m2-mono/logs/run-20260810-155025/condemnedvr-loader-handgun-breach-fallback-checkpoint.log`
(68,817 bytes; SHA-256
`E92AEB8D5440B28DB691B60955024136E4558D2A651B3FCC048BEA715524B995`).

Follow-up static analysis explains the result. `CClientWeapon::Init` at
`GameOrig+0x0002EDC0` reads the database attributes named by
`MuzzleSocket` and `BreachSocket` at `+0x0013B444` and `+0x0013B454`
and stores the resolved right-weapon handles at approximately `+0x40` and
`+0x3C`. Retail display paths near `+0x000FD650` and `+0x000FD700`
continue through a fallback position when `Breach` cannot be resolved.
`Breach` is therefore optional Retail asset data rather than a required
index-76 invariant.

The revised candidate queries and validates `Flash` first. When a valid
`Breach` also exists it retains the two-point `Breach -> Flash` source;
otherwise it rotates the authored Flash socket's local +Z and +Y axes through
the reconstructed visible-model pose for forward and roll. Before its live gate,
the +Z convention was an explicit hypothesis supported by the authored socket
convention used by the accepted forensic Camera path. Retail fire position
remains untouched.

`m5_handgun_muzzle_aim_armed`, `m5_handgun_muzzle_aim`, and
`m5_handgun_muzzle_aim_fallback` now expose `direction_source`, Flash
handle/transform/position/rotation, optional Breach availability and transform,
visible barrel, Retail/controller directions and dots, diagnostic Flash origin,
preserved Retail origin, and their distance. The 11 August revised build passes
19/19 x86 and 15/15 x64 tests, including two-point, Flash-only, rotated, stale,
and invalid-transform cases. Its built and staged x86 `GameClient.dll`
SHA-256 is
`5C385D018E511623E563357F4FCE82BCA689C38D1DB96C7C72405D1698F257F2`.

Live acceptance run `run-20260811-081337` used the dirty working tree at
HEAD `93dab52`, the exact staged DLL above, VirtualDesktopXR 1.0.10, a Meta
Quest 3, and
`launch-condemned-m2-vr.ps1 -WeaponTest Pipe -Wait`. Four native index-76
fire calls all recorded `result=applied`, `direction_applied=1`,
`direction_source=Flash_socket_plus_Z`, and `fallback=none`; no handgun
fallback record occurred. Each call had source generation 1, `Flash_handle=2`,
`Flash_transform_available=1`, local position
`(20.675,5.009,-9.411)`, local rotation
`(0.05224,0.75488,0.04003,0.65255)`,
`Breach_handle=4294967295`, and both Breach availability fields at zero.

The four directions covered visibly different controller orientations.
Candidate-to-Retail direction dots ranged 0.94429--0.99619 and
candidate-to-controller dots ranged 0.88496--0.94690, demonstrating that the
socket path was active rather than numerically identical to either old source.
The tester confirmed that bullet impacts followed the visible handgun sights.
This is **live-verified visible direction alignment for index 76**.

Preserve
`stage/condemned-m2-mono/logs/run-20260811-081337/condemnedvr-loader-handgun-flash-plus-z-checkpoint.log`
(59,207 bytes; SHA-256
`23791753AFDE1E14B71C7CAE0C627F912CF895866065C2028A122FC471C6CC36`)
with that run's host log, bridge log, and `m2-mono-live.json`.

This acceptance is direction-only and asset-specific. Retail fire position was
preserved; diagnostic Flash-to-Retail-origin distance ranged
64.570--78.185 units, so close-range origin/parallax remains unaccepted.
Other firearm indices retain controller aim. The repeatable regression gate is
in `TESTING.md`.
