# Test and verification plan

## Automated gate

Prepare the pinned dependencies, build both architectures and run all tests:

```powershell
powershell -ExecutionPolicy Bypass -File tools\prepare-dependencies.ps1
powershell -ExecutionPolicy Bypass -File tools\build-all.ps1
```

The x86 build additionally tests module identity, loader fail-closed behavior
and bridge product guards. The current consolidated gate passes 25/25 x86 and
21/21 x64 CTest cases. Both architectures run the shared protocol, pose,
stereo, input, render-scale, weapon-weight, physical-melee/firearm-muzzle,
interaction-authoring, player-collision, tool-menu, Retail-menu mapping, and
background-render tests.

The standard build also runs these self-contained PowerShell regressions:

```powershell
powershell -ExecutionPolicy Bypass -File `
  tools\test-condemned-launch-profile.ps1
powershell -ExecutionPolicy Bypass -File `
  tools\test-condemned-window-focus.ps1
powershell -ExecutionPolicy Bypass -File `
  tools\capture-condemned-window.ps1 -ValidateOnly
powershell -ExecutionPolicy Bypass -File `
  tools\test-condemned-weapon-diagnostics.ps1
powershell -ExecutionPolicy Bypass -File `
  tools\test-condemned-release-tools.ps1
```

The launch-profile test proves that no feature argument selects `Current`,
that wait/rollback-only options retain it, and that explicit, Pipe, Minimal,
and invalid mixed modes remain distinct. The focus test exercises invalid
targets, bounded retries, and the final fallback/cleanup contract through
deterministic substitutes; it never changes the real foreground window. The
screenshot-helper validation checks its guarded command path without capturing
a live window. The watcher test synthesizes only project-local temporary JSONL
and checks schema v4 hit
classification, per-target intervals, telegraph split, minimum stand-off,
player vitals, command-28 edges, blocked/unblocked health attribution, and the
explicit enemy-health-unobserved boundary. It can be run separately as above
and neither test launches the game or headset. The release-tools test checks
install-root containment, package path traversal rejection, manifest tamper
failure, exact install markers, script parsing, and clickable-wrapper
presence without reading Retail files.

Before a public push or package, also run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\audit-publication.ps1 -RequireClean
```

## End-user package gate

Build the package only after the normal gate passes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-release.ps1 -SkipBuild
```

Require `release-manifest.json` to identify the exact source/configuration,
declare no Retail content, and hash every packaged file. Inspect the generated
folder and ZIP for `Condemned.exe`, `GameOrig.dll`, `GameServer.dll`,
`ClientFx.fxd`, archives, saves, logs, and player settings; none may ship.

Against a disposable target outside both the package and Retail roots, run:

1. first install with automatic Steam discovery and `-NoShortcut`;
2. the same installer again as an in-place update;
3. installed `Play.cmd -VerifyOnly` from a non-default install directory;
4. uninstall without `-Apply` and confirm it is a dry run;
5. uninstall with `-Apply`, preserving userdata; and
6. verify the exact Retail executable/client/server hashes are unchanged.

The 20 August 2026 project-local smoke completed that sequence against the
verified Steam build. It initially exposed Windows PowerShell's
`Remove-Item` junction null-reference failure; the uninstaller now validates
the exact reparse target and removes the junction itself through
`IO.Directory.Delete` before recursively deleting its project-local parent.
The corrected applied uninstall passed and left only the deliberately
preserved empty userdata plus its install marker, which the test then removed.
A subsequent fresh extraction of the generated ZIP repeated first install,
in-place update, installed `Play.cmd -VerifyOnly`, and applied uninstall
successfully. No headset/game launch or desktop shortcut was exercised. A
clean Windows account, default-directory shortcut, both documented OpenXR
runtimes, and release-grade install/play/uninstall acceptance remain required
before marking M6 live accepted.

## Retail integrity gate

`tools/verify-condemned-m0.ps1` reads the local Steam manifest and critical
files, records hashes and PE identity, and performs no writes. Local staging
scripts copy verified inputs only below `stage/`; they never overwrite the
retail installation.

Unknown executable/client hashes, signatures or expected bytes must prevent
the relevant write-enabled gate from activating. A diagnostic should state
the failed identity rather than continuing with an approximate offset.

## Live regression matrix

Run live tests only against the verified `1.0.314.0` Steam build.

| Area | Required observation |
|---|---|
| Stock fallback | Game launches and plays normally without the OpenXR host |
| Stereo | Both eyes show distinct, fuseable views with correct polarity and no edge gaps |
| Tracking | Yaw, pitch and roll are responsive; tracking loss produces no camera jump |
| Frame pacing | Fast head/controller motion does not expose queued stale frames or double images |
| Menu | Pause menu appears in the headset, closes normally and resumes stereo |
| Controls | Move, turn, interact, core actions, menu and recenter produce one bounded game action |
| Focus | A fresh launch ends with Condemned foreground-owned; Alt-Tab releases desktop mouse confinement while headset rendering continues in window mode |
| Haptics | Only confirmed actions pulse; amplitude/duration remain bounded |
| Device reset | Resolution/window changes and return from desktop do not lose the bridge or session |
| Save lifecycle | Load, death/respawn and level transition invalidate stale object pointers safely |
| Exit | Game and host terminate without a hang or persistent system-wide runtime change |

### Default feature-platform launch gate

Run the launcher with no feature-selection parameters:

```powershell
.\tools\launch-condemned-m2-vr.ps1
```

Require the report to record `FeaturePreset=Current`,
`WeaponTestPreset=Pipe`, Retail VR Settings, physical melee/contact damage,
weapon-grip calibration, and full arm IK enabled. Two-hand attachment,
forensic-memory tracing, mutually exclusive A/B probes, and rejected automatic
swing attack must remain disabled or governed by their existing saved/default
gate. Require the exact staged bridge, no ASI modules, successful final focus,
and responsive game/host processes. `-Minimal` must preserve the bare transport;
any explicit feature-selection parameter must resolve to Pipe or Custom rather
than silently inheriting Current.

Run `run-20260820-041148` passes only the no-argument selection and guarded
readiness portion. Headset rendering, input, melee, IK, settings UI, persistence,
performance, and shutdown still require their matching rows and feature gates
below before the combined profile can be called live accepted.

The same run later ended in `Condemned.exe` /
`ClientFx.fxd+0x26EEF` (`0xc0000005`; Windows report ID
`2ea5e501-d7df-4db5-a083-687c6c4a929c`). The host handled the disconnect and
exited, but the stability/shutdown portion therefore fails. Because this exact
fault bucket predates the Current profile and Retail VR Settings work, do not
assign causality without a controlled baseline/profile comparison.

### Startup foreground handoff gate

The bounded retry and attached-input fallback are **automated-tested only**.
They still require a real interactive Windows desktop because a successful API
return, build, or mocked retry cannot prove which window receives keyboard,
mouse, and foreground-gated VR input.

Start one fresh canonical `-WeaponTest Pipe -Wait` session while the launcher
or another ordinary desktop window is foreground. Do not click Condemned during
startup. After the launcher finishes its readiness checks:

1. confirm Condemned is visibly active and accepts keyboard/mouse plus one
   release-gated VR menu action without an initial click or Alt-Tab;
2. require `GameWindowFocusRestored: true` and
   `GameWindowFocus.FinalHandoff.Focused: true` in `m2-mono-live.json`;
3. preserve the initial, readiness, and final attempt counts, attached-input
   attempt/success/release fields, final foreground PID, and outcome detail;
4. with `-PerformanceProbe`, confirm its console appears before the final
   handoff and does not retain foreground ownership; and
5. Alt-Tab away and back, confirming the accepted background-render, free
   desktop cursor, clean focus return, and controller-input gates still work.

Reject a run if the game needs a click, the report records an earlier success
while the final handoff failed, a temporary input-queue attachment is not
released, startup-held input becomes an action, or keyboard/mouse/non-VR
fallback changes. A refused focus request remains non-fatal and must retain its
explicit diagnostic rather than claiming success.

### Room-scale RS1 rejected historical gate

The first RS1 read-only candidate is **live exercised, rejected for
performance, and rolled back**. It supplied no room-scale command and performed
no engine write. Its dedicated runtime, launcher switch, and automated test are
not present in the current tree.

Historical run `run-20260813-131921` used launcher switch
`-RoomscaleProbe` with the existing stereo, locomotion, turning, and
menu-state prerequisites. The tester reported that it ran horribly. Host
telemetry contained extended 17--23 game-FPS sections, heavy frame reuse, and
about 13--16 frames of average image age, with later recovery to roughly
42--43 game FPS. No matched same-scene no-probe control exists, so do not claim
that RS1 caused the timing regression; the candidate nevertheless failed its
live usability gate.

The required correlation sequence was not completed. A transient observation
suggested that the candidate stayed fixed during HMD-only motion and matched
the sampled player-body object, but the file later copied as
`condemnedvr-loader.log` is not the RS1 event stream. That observation cannot
promote the candidate's collision-root semantics. Exact run hashes and the
rollback boundary are retained in
[`CONDEMNED-ROOMSCALE-PLAN.md`](CONDEMNED-ROOMSCALE-PLAN.md).

The historical launch command was:

```powershell
powershell -ExecutionPolicy Bypass -File `
  tools\launch-condemned-m2-vr.ps1 `
  -RoomscaleProbe -DesktopWindow -Wait
```

`-RoomscaleProbe` is intentionally unavailable now. Do not use that command
against the post-rollback tree or relaunch the rejected binary.

Before another RS1 implementation is considered, capture a matched
normal-stereo performance baseline in the same save/scene. A redesigned probe
must make its own cost isolatable, copy its loader stream into the fresh
session directory before any subsequent launch, and remain read-only. A build,
armed event, or numerical correlation alone still cannot establish ownership
of the player's collision capsule.

Retain this bounded sequence for that future probe:

1. Stand still and translate then yaw only the HMD, without either stick.
2. Hold the HMD still and use the left stick for forward/back and both strafe
   directions.
3. Hold the HMD still and turn left/right with the right stick.
4. Walk diagonally, climb available stairs, press into a wall, and use a moving
   platform or elevator if one is readily available.
5. Recenter, open/close the menu, load a save, and exercise death/respawn if
   practical.

For each stage, correlate HMD input, centralized transform, Retail state,
diagnostic decision, candidate/player-body/camera handoff, and resulting
motion. HMD-only motion must not move the candidate root. Native locomotion and
collision must produce coherent root translation; native turning must produce
coherent root yaw. The player-body and untouched Retail camera are comparisons,
not substitutes for the candidate. Manager/object changes must have
explainable acquired/changed/lost and generation events, with no stale pointer
reused. Input query counts/ages, raw stick state, game state, tracking
freshness, focus, and foreground ownership must agree with the performed
action.

Every future read-only sample must explicitly retain zero room-scale command
values for 2/5/23 and `engine_writes=0`. Stop if the candidate follows only
camera or body animation, changes during HMD-only motion, ignores native
collision/turning, crosses a lifecycle boundary without invalidation, or
destabilizes existing stereo, input, menu, keyboard/mouse, focus, or
host-absent behavior. Keep RS2 and all write-enabled room-scale gates blocked
on any ambiguous or performance-rejected result.

Preserve the exact source state, staged loader hash, runtime, launch report,
host log, bridge log, loader event stream, save/location, matched baseline,
expected and actual observations, and rollback. Only a clean,
performance-accepted correlation run may promote the candidate semantics.

### Retail-native VR Settings entry and screenshot gate

The first opt-in row/dispatch candidate has a narrow live pass. Run
`run-20260813-045556` created the Retail control once, displayed it in Options,
and delivered six `0x3A` selections. Its uppercase literal and no-destination
behavior were deliberate limitations of that diagnostic build.

The title-case child-screen follow-up is not accepted as a safe settings host.
`run-20260813-051426` crashed at `GameOrig.dll+0x539AE` before menu
construction. Retry `run-20260813-051528` created `VR Settings`, entered
screen 24 four times, and observed three Back edges, but later crashed at
`ClientFx.fxd+0x26EEF` after extensive input on the dormant Game Options
controls. This correlation does not establish cause, but it is enough to stop
expanding or broadly exercising those controls.

Explicitly approved capture run `run-20260813-054534` entered that page once
without selecting a dormant control. Its exact-foreground screenshot verifies
the title-case Retail design and also shows invalid-string placeholders plus
unrelated Game Options rows. Left open and untouched, the process later
reproduced `ClientFx.fxd+0x26EEF` in the same WER bucket. No Back event was
recorded. A later Windows event-history audit found 59 identical faults before
the Retail-menu work began, so this cannot be used as screen-24 causality. The
original page is still rejected because the capture directly shows unrelated
controls, invalid strings, and dormant settings behavior.

Run `run-20260813-063732` live verifies that the isolated candidate bypasses
those three original routines, builds/focuses through the verified base-screen
paths, renders the title plus exactly four clean category rows, and focuses out
on Back. It also reveals that the same VR-generated Enter edge used to open the
page activated Display before the key-edge dispatch returned. The placeholder
made no settings mutation, but this failed the entry-input gate.

The current candidate brackets the internally generated key edge and suppresses
only a category command produced before that opening Enter's native KeyUp
returns. Later deliberate VR input and physical keyboard/mouse input remain
unchanged. It passes the full headset-free suite and is staged at SHA-256
`A9AD752A4A6D25ACCDFB44AFED9D8539A3FB837280C18BCE733EFEB8446B749A`,
but the suppression behavior initially had no live acceptance.

Explicitly approved run `run-20260813-071147` opened the page twice with
VR-generated accept edges. It built the host once, recorded two active/inactive
base-focus cycles and one explicit VR Back, and produced zero category
selections and zero setting mutations. Because Retail produced no same-edge
category command in either entry, no suppression event was expected or
recorded. This passes the observed entry behavior but leaves the defensive
suppression branch automated-only. Its exact-foreground screenshot has SHA-256
`2223D0C12BCBBE629ED2BC245C267D7D81E462FFD58542E1D6B9BEDAB5E92C8D`
and the preserved loader log has SHA-256
`4031429705448FBF40396F875F33BD0C21F12B4BDF679DB6A1A448A120B20A09`.

No game/headset launch for this gate is automatic. Obtain explicit tester
confirmation immediately before every launch. Build and stage remain
headset-free:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-all.ps1
powershell -ExecutionPolicy Bypass -File tools\prepare-condemned-m2-mono-stage.ps1 -Refresh
```

The requested capture utility has an offline compile gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture-condemned-window.ps1 -ValidateOnly
```

After explicit approval for a bounded visual diagnostic, launch the normal
`-WeaponTest Pipe -RetailVrSettingsProbe -Wait` command. Enter the isolated
page once and do not select Display, VR Features, Comfort, or Developer Tools.
Require `m6_retail_vr_settings_page_built` with
`original_build_called=0`, `original_focus_called=0`, and
`original_command_called=0`, plus no
`m6_retail_vr_settings_category_selected`. The normal result is no category
command at all. If Retail reproduces a category command inside the opening VR
Enter edge, require exactly one
`m6_retail_vr_settings_entry_category_suppressed` for Display with
`settings_mutated=0`; only that observation promotes the defensive branch to
live verified. Capture the exact foreground game client from another shell,
using the PID printed by the launcher:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture-condemned-window.ps1 -TargetProcessId <PID>
```

The tool does not focus, move, or resize Condemned. It requires the exact
Condemned root-owner window and PID to own the foreground, then writes a PNG
and JSON identity/rectangle/SHA-256 sidecar to the newest run directory.
`-AllowBackgroundWindow` is diagnostic-only because an occluding window can
otherwise be captured. After capture, press Back once and exit normally.

Accept the hub lifecycle when the image shows the title-case Retail page and
exactly four intended category rows, the loader records active base focus,
zero unsuppressed category selections, and one base-handled Back plus inactive
focus. Absence of a generated category command is a clean behavioral result,
not evidence that the suppression branch executed. Re-entry is permitted as a
bounded regression; deliberate category selection belongs to the next control
gate.

Reject the isolated host on any crash, hang, missing/extra/invalid-string row,
failed Back, original dormant-routine use, unsuppressed category activity,
capture of the wrong window, or non-probe regression.
Preserve the source state, staged hash, launch report, host/bridge/loader logs,
WER fault module/RVA when present, PNG, JSON sidecar, runtime, and observation.

Do not attribute `ClientFx.fxd+0x26EEF` at shutdown to this menu: it is present
in at least 59 pre-menu runs. End-to-end clean shutdown remains a separate open
gate. A matched run with `-RetailVrSettingsProbe` disabled is required before
using that WER bucket as feature-specific evidence.

The hub entry/render/Back lifecycle now has live evidence independent of the
pre-existing shutdown bucket. Display, VR Features, and Comfort must remain
non-mutating placeholders for the next gate.

#### Developer Tools shortcut-toggle gate

`Developer Tools` is one boolean row, not a destination. Its current candidate
is **partially live verified**. It controls whether both grips plus the
controller-specific left secondary button (B on the tester's controller, Y on
Touch) and F12 may open VR Tools; it does not alter overlay features or
debug-draw settings. The full build passes 23/23 x86 and 19/19 x64,
and the staged x86 loader SHA-256 is
`E44FBB97548C24836E4E26A1AF02D0E62E4DE3938F068EED51CB09CDDC6AA8F6`.
No launch is automatic. Obtain fresh explicit tester confirmation immediately
before each launch, including the persistence-reload launch.

Explicitly approved run `run-20260813-132327` completes the displayed-state and
single-mutation portion of this gate. It loaded On with `result=ok`, built and
focused the row with `settings_ready=1`, then saved Off with
`runtime_mutated=1` and recorded
`previous=1 requested=0 saved=1 current=0 label_refreshed=1`. The tester
confirmed the toggle worked, then clarified that VR Tools no longer appeared
when the Off-state controller hotkey was pressed as both grips + B. This
live-verifies controller-chord suppression. F12 suppression, On-state
restoration/open/close/release capture, and fresh-process reload remain
unverified. The preserved loader log SHA-256 is
`1CE76E5407C1F1EAC6F0F0E721E7BA8B1F583E89790302E5EF8BF3EB6FE64C30`.
Shutdown reproduced the known `ClientFx.fxd+0x26EEF` baseline (WER report
`8b491332-38a1-4566-8c4e-c9d2cb7d83ba`), which does not reject this control.

For the first approved run:

1. Require `m6_vr_tool_menu_shortcut_loaded` and
   `m6_retail_vr_settings_page_built` with `settings_ready=1`. Record whether
   the initial native row reads `Developer Tools: On` or `Developer Tools: Off`
   and capture the exact foreground client.
2. Do not activate Display, VR Features, or Comfort. Activate Developer Tools
   exactly once. Require `m6_vr_tool_menu_shortcut_saved` with `result=Ok` and
   `runtime_mutated=1`, followed by
   `m6_retail_vr_settings_developer_tools_changed` with `saved=1`, the inverse
   `current` value, and `label_refreshed=1`. Reject any corresponding
   `*_failed` event or stale visible label.
3. In the Off state, try both grips plus left secondary once after fully
   releasing the controls and try F12 once. VR Tools must not open. The
   disabled chord must not be reported as a tool-menu open/capture transition;
   ordinary Retail behavior remains allowed. The controller portion passes
   live for this run; F12 remains pending.
4. In the On state, verify both grips plus left secondary opens VR Tools, the
   right secondary button closes it, and input remains captured only through
   the normal post-close neutral-release gate. Verify F12 can also open and
   close. Toggle the Retail row as needed so both states are tested, then leave
   it opposite the recorded initial value for the persistence check.
5. Press Back once, require base focus-out, and exit normally. Preserve the
   source/stage identities, all logs, two state screenshots, actual input
   observations, and any WER event without attributing the known ClientFx
   shutdown bucket to this feature.

After a separate fresh confirmation, launch again and require
`m6_vr_tool_menu_shortcut_loaded result=Ok` with the opposite persisted value.
Enter VR Settings and verify the matching row text before any activation.
Repeat the applicable shortcut behavior, then toggle back to the original
value and require a successful save plus immediate label refresh. A third
launch is unnecessary unless the restore save or displayed state is unclear.

Reject the control if the initial setting is unavailable, the wrong row
mutates, a write failure changes runtime state, Off still opens VR Tools, On
cannot open and safely close it, the label disagrees with runtime state,
re-entry loses state, fresh-process reload disagrees with the saved value, or
keyboard/mouse/VR focus and Back behavior regress. Host-absent and non-VR
fallback remain separate follow-up gates before release acceptance. Only after
this control passes should Display, VR Features, or Comfort gain their first
functional controls.

### Arm IK save/restore lifecycle gate

The reported post-load Retail-hand fallback remains a hypothesis about lost
node-control registration, not a verified root cause. A candidate
game-state-generation fix and bounded per-side callback heartbeats are now
**implemented, awaiting live validation**. Hand IK, Left IK, and Elbow `ACTIVE`
now require a recent callback heartbeat for the current lifecycle generation;
the older cached-installation result is no longer used as callback evidence.

With `-ArmIkRightArm` and its required `-MenuProbe` armed, use one fresh
session and preserve the complete launch report, host/bridge logs, and loader
log. Confirm both hands visibly follow their controllers and both callback
heartbeats advance, then perform in order:

1. open and close the ordinary pause menu without loading;
2. load the same save and resume gameplay;
3. trigger one death/respawn cycle; and
4. cross one normal level transition.

For every discontinuity, record the previous and current Retail game state,
player-body pointer, lifecycle generation, cached installation state, per-side
callback heartbeat, removal result, and transactional registration result.
Correlate `arm_ik_lifecycle_invalidated`,
`arm_ik_lifecycle_resume_pending`, both side-specific install/active events, and
`arm_ik_callback_heartbeat`. A same-valued player-body pointer is not proof of
the same callback lifetime.

Accept a candidate lifecycle-generation fix only when both three-node chains
are released or invalidated, reinstall exactly once for the new generation,
resume fresh callbacks, and visibly follow both controllers. Reject duplicate
registration, a one-arm install, callbacks against an old body, a persistent
Retail-hand fallback, or any crash/hang. Also verify that tracking loss and
menus still fall back temporarily, weapon switching retains its calibrated
targets, and host-absent/non-VR play remains Retail-owned.

### Empty-hand right-hand alignment gate

The original empty-right-hand twist and its in-session correction are now
**live verified**. The earlier path combined right-grip position with
right-aim rotation, while the correctly aligned free left hand used its grip
pose for both. The accepted path selects a coherent raw right-grip target when
there is no lifetime-valid held model and keeps the weighted aim/per-weapon
path for an equipped item. The guided solve and player-file write succeeded in
`run-20260811-115639`; restart, forced tracking loss, and equipped/empty
transition checks remain open sub-gates.

Use a fresh canonical headset session with `-ArmIkRightArm` armed and preserve
`m5_right_hand_ik_target_source`, `m5_empty_right_hand_alignment`, and
`m5_empty_right_hand_alignment_loaded`:

1. Reach gameplay with no item equipped. Open **Hand IK** and confirm it shows
   the two-action empty-hand page, while the target reports `empty_grip`.
2. Before calibrating, rotate the right controller through comfortable yaw,
   pitch, palm-up, and palm-down poses. Compare the palm with the free left
   hand and record whether raw grip alone removes the original twist.
3. Select `START GUIDED EMPTY-HAND ALIGNMENT` with A. Make the visible right
   hand look correct, then pull and release the right trigger. Require the menu
   to advance to step 2 and the log to record `reference_captured`; it must
   not change tabs or dispatch a gameplay attack.
4. Move only the physical controller to the pose where it should comfortably
   sit, then pull and release the right trigger again. Require the hand to snap
   to the captured reference, the menu to report applied/saved, and the log to
   record both transforms, the solved controller-local offset, and
   `persistence=ok`.
5. Exercise the calibrated hand through yaw, pitch, roll, reach, and a short
   tracking loss. Tracking loss must return to Retail temporarily and a held
   trigger must not become a delayed capture after tracking resumes.
6. Equip one previously accepted calibrated weapon. Require
   `weapon_weighted_aim`, unchanged per-weapon Hand IK values, and no global
   empty-hand correction on its target. Return to empty hands and require the
   saved correction without a stale weapon pose or visible transition snap.
7. Close and relaunch through the canonical shortcut. Require
   `m5_empty_right_hand_alignment_loaded result=ok` and the same visual
   alignment without reopening the menu.

Accept the candidate only when the right palm follows the physical controller,
the left hand remains unchanged, the two trigger pulls are deterministic, and
the setting survives restart. The equipped/empty transitions must clear bend
and weight history without changing accepted weapon alignment. Invalid,
malformed, stale, or over-range input must leave the last valid calibration
untouched. Do not repair failures with a coordinate sign flip or a persisted
weapon-index `-1`/Unarmed record. `RESET EMPTY-HAND ALIGNMENT` is the
explicit identity rollback; use it only when that rollback is intended.

### Primary one-press held-assembly alignment gate

The immediate one-press interaction retains **live same-sample, persistence,
and repeat-use evidence for indices 76, 46, 4, 3, and 32**, but controller-
facing correctness is contradicted for the Colt. Run `run-20260811-152219`
used staged loader SHA-256
`2D44888071EA3D360E9A7FB822CBC5EDD5BB6C8DBB1E4DD9AE77FCE1D4237A9E`;
all 19 applications were same-sample, `applied`, and persisted Grip, Hand IK,
and Collider as `ok/ok/ok`. The tester accepted the visual result for continued
use. A later 20 August headset report says the Colt did not face the controller
after the action. It has no fresh structured run, but it invalidates treating
the older successful events as weapon-direction proof.

The automatic forward-hand/reset-attachment correction is **implemented and
automated-tested, awaiting live validation**. The full gate passes 25/25 x86
and 21/21 x64; built and
project-local staged x86 loader copies are byte-identical at SHA-256
`74B2169CA45D0A8FA013AAA1ACA857473F04009FCF18FCD5F31981B63CF6AAF6`.
It targets the globally corrected raw-grip hand pose, carries the immutable
authored zero/reset hand-to-model attachment, and rebases the collider. It is
one automatic press with no freeze or trigger capture.

The full gate is still **partial**: restart, forced stale/source-change
rejection, explicit collider/damage observation, forensic optics, and firearm
direction remain open. For that remaining work, use a fresh canonical headset
session with `-WeaponGripCalibration` and `-ArmIkRightArm` armed. Preserve
`m5_align_held_assembly_to_controller`,
`m5_held_object_attachment_applied`, the three settings-save/load events,
`m5_right_hand_ik_target_source`, and the normal launch/host/bridge logs.
Record the exact source state, staged hash, runtime, item index/generation, and
the before/after Grip, Hand IK, and Collider records.

1. Confirm the global empty right hand is already aligned. Equip the reversible
   index-76 representative and visually confirm its current gun-in-hand fit
   before changing anything. The accepted reset baseline is `O == H == D`;
   do not deliberately disturb that relationship merely to exercise the tool.
2. Open **Grip**, hold the controller in a comfortable normal pose, and select
   `ALIGN HAND + WEAPON TO CONTROLLER` once with A. No trigger capture or
   manual freehand placement is required.
3. Require `m5_align_held_assembly_to_controller event=applied`,
   `raw_pose_fresh_same_sample=1`, one nonzero sample ID/timestamp, finite raw
   grip and aim transforms, and the measured grip/aim angular difference.
   Require the same stable item index/generation throughout. Require
   `alignment_basis=global_corrected_grip_with_authored_reset_attachment`,
   finite authored grip,
   `hand_target_position_error_units<=0.001`,
   `hand_target_rotation_error_degrees<=0.01`,
   `authored_attachment_position_error_units<=0.001`,
   `authored_attachment_rotation_error_degrees<=0.01`,
   `automatic_hand_forward_aligned=1`,
   `current_model_to_hand_preserved=0`, and
   `authored_reset_attachment_preserved=1`. Index 76 must report the identity
   authored reset attachment; do not infer the same local frame for another
   asset.
4. Require `m5_held_object_attachment_applied` to report
   `relationship=authored_reset_hand_parented`,
   `model_to_hand_preserved=0`,
   `authored_reset_attachment_used=1`,
   `collider_model_relation_preserved=1`, and all three persistence results as
   `ok`. The menu must report `HAND + WEAPON ALIGNED AND SAVED`.
5. Move through yaw, pitch, roll, reach, and a normal swing. The hand must stay
   rigidly in the same natural model grip while the assembly follows the
   physical controller without a weight-lag-dependent saved offset. For index
   76, the hand must point in the same forward direction as the already
   accepted empty hand and the zero/reset Colt must follow it rigidly; neither
   may remain on the prior right-pointing assembly basis.
6. Show the collider wireframe and compare it to the model before and after.
   Its model-relative position must not move. Perform the bounded melee
   contact check; index 76 must not create a distant or detached damage volume.
7. For the handgun representative, fire several mid-range shots through yaw
   and pitch. The visible sights and accepted Flash-socket direction must still
   agree, with no changed Retail fire origin/ammo behavior.
8. Close normally and relaunch. Require all three records to load successfully
   and the same hand/weapon/collider relationship before reopening the menu.
9. In separate negative attempts, invoke the row with stale tracking and force
   an item source-generation change around activation. Require
   `pose_unavailable`, `source_unavailable`, or `apply_rejected` as applicable,
   no partial authoritative update, and no delayed action after recovery.

Reject the candidate for any hand/weapon separation, collider shift, camera or
fire regression, save/reload mismatch, crash/hang, or acceptance from
mismatched input samples. If the model is already wrong inside the virtual hand,
the primary action deliberately preserves that bad authored relationship; stop
and use the advanced gate below rather than redefining the primary behavior.

### Advanced frozen held-object alignment gate

The two-trigger workflow is now an advanced fallback only for an item whose
authored model-to-hand fit is genuinely wrong. Its original interaction is
**live verified in-session for five held assets**, but a later index-76
fresh-process check reproduced a wrong relationship despite successful loads.
Treat that run as usability evidence, not acceptance of the old
first-hand-preserving transform. The revised freeze/current-hand/parented
fallback is **implemented and automated-tested only**: 20/20 x86 and 16/16
x64 pass. Its freeze, skeleton response, collider rebase, camera/fire paths,
and restart persistence still require this gate after the live hold is lifted.

Use a fresh canonical headset session with `-WeaponGripCalibration` and
`-ArmIkRightArm` armed. Start with one reversible representative item, record
the exact source state, staged loader hash, runtime, item identity/generation,
and its current `grip`, `right_hand_ik`, and `collider` records. Preserve
`m5_guided_held_object_alignment`,
`m5_held_object_attachment_applied`,
`m5_right_hand_ik_target_source`, all three settings-save events, and the
normal launch/host/bridge logs:

1. With no held item, first verify that the global empty right hand is already
   aligned to the physical grip pose. Guided phase two deliberately uses this
   calibrated hand as the canonical hand. If it is wrong, stop and complete
   the separate empty-hand gate rather than compensating in a weapon record.
2. Equip the item and open **Grip**. Select
   `ADVANCED: FROZEN TWO-POSE ALIGNMENT`. Position the visible weapon where
   it is easy to inspect; the old per-index hand may be visibly wrong.
3. Pull and fully release the right trigger. Require
   `reference_captured` and the step-two prompt. Move and rotate the
   controller without completing the solve: the weapon must stay visibly
   frozen while the rendered right hand follows the controller. Require
   `guided_hand_placement=1`. Any weapon motion with the controller rejects
   the freeze portion of the candidate.
4. Put the live virtual hand into the frozen weapon's natural grip and pull/
   release the trigger again. Require immediate model/hand agreement, the menu
   to report success, and `m5_held_object_attachment_applied` to report
   `model_to_hand_preserved=1`,
   `collider_model_relation_preserved=1`, and
   `grip_persistence=ok hand_persistence=ok collider_persistence=ok` for the
   same stable index and source generation.
5. Move through yaw, pitch, roll, and reach. The hand/weapon fit must remain
   rigid. In **Hand IK**, make one reversible position or rotation step: the
   hand and weapon must move together, and the collider wireframe must retain
   the same place on the model. Undo or restore the recorded values afterward.
   A Hand-IK edit that moves only the hand rejects the parent attachment.
6. For a melee item, compare the collider wireframe's model-relative placement
   before and after the solve, then land one qualified hit. The serialized
   `collider` transform is expected to change because it is rebased from the
   old Grip to the new Grip; its world/model relationship, length, radius, and
   reverse flag must remain equivalent. Do not require the collider record to
   be byte-identical.
7. Run the item's dependent regression. For either forensic camera, verify
   displayed framing/ray and photo use. For a firearm, verify visible sight
   direction and impact while preserving Retail's native fire origin. These
   results cannot be inferred from the attachment algebra.
8. Start another capture, then test source-generation change, tracking loss
   with the trigger held, and menu/focus loss. Each must cancel or release-gate
   without applying delayed or partial settings. Other item indices, the
   global empty-hand record, and secondary-grip enable/offset/radius must stay
   unchanged.
9. Drop/reacquire the item, then close and relaunch through the canonical
   shortcut. Require successful loads for that index's Grip, Hand IK, and
   Collider, the same visual fit before reopening the menu, the same collider
   placement, and a repeated dependent melee/camera/fire check.

Accept the superseding candidate only when the frozen-object interaction is
visually truthful, the second/current hand wins over any wrong first hand, all
three records persist, manual Hand IK moves the complete assembly, and restart
reproduces the result. Repeat the gate per Retail item identity; one working
model is not evidence that unrelated authored assets share a grip. Any source
mismatch, partial save, malformed/over-range solve, failed visual-override
readback, stale pose, detached collider, or dependent-path regression must
leave or restore the last accepted live and persisted attachment.

### Handgun visible-barrel aim gate

This gate applies only to the stable firearm candidate, Retail index 76
(`colt45_Unbreakable`). Its visible direction is live accepted. It
reconstructs the displayed model, prefers its authored `Breach -> Flash` line
when both transforms exist, and otherwise uses the authored Flash socket's
local +Z axis. Retail fire position is deliberately preserved.

Live run `run-20260810-155025` established that this index-76 asset exposes
`Flash` handle 2 but no `Breach`. All nine calls from the earlier
two-socket-only candidate logged `direction_applied=0` and raw-controller
fallback. That run verifies fallback behavior but does not satisfy this
alignment gate.

Acceptance run `run-20260811-081337` used the staged DLL with SHA-256
`5C385D018E511623E563357F4FCE82BCA689C38D1DB96C7C72405D1698F257F2`.
All four shots recorded `direction_source=Flash_socket_plus_Z`, a valid stable
Flash transform, missing optional Breach, `direction_applied=1`, and no
fallback. The tester confirmed that impacts followed the visible handgun
sights. Preserve
`stage/condemned-m2-mono/logs/run-20260811-081337/condemnedvr-loader-handgun-flash-plus-z-checkpoint.log`
(59,207 bytes; SHA-256
`23791753AFDE1E14B71C7CAE0C627F912CF895866065C2028A122FC471C6CC36`).

That acceptance covers index-76 direction only. It does not accept a moved
muzzle origin, close-range parallax, or any other firearm. Repeat this gate
after changes to fire vectors, tracking/weighting, visible weapon transforms,
grip persistence, socket/model access, weapon identity, or relevant hook
lifetimes.

Stage the exact candidate, then launch the canonical full weapon preset:

```powershell
.\tools\launch-condemned-m2-vr.ps1 -WeaponTest Pipe -Wait
```

The preset name reflects the canonical melee setup; its Head Aim and visible
weapon hooks also supply the guarded handgun source. The launcher must require
`m5_handgun_muzzle_aim_armed` and abort on
`m5_handgun_muzzle_aim_rejected`; a launch that silently uses raw-controller
fallback is no evidence for this gate.

With the known index-76 save:

1. confirm the visible handgun still uses the accepted saved grip and that
   ordinary trigger firing, ammo consumption, sound, and effects work;
2. at a clear wall from a mid-range distance, place the visible barrel/sights
   on a small fixed feature and fire one shot;
3. repeat after obvious comfortable yaw and pitch changes, allowing the
   displayed weapon to settle each time;
4. take one close-range comparison shot without changing calibration; and
5. if practical, switch once to a non-index-76 weapon and confirm its existing
   controller-aim behavior is unchanged.

Every candidate shot must produce an `m5_handgun_muzzle_aim` record with
`result=applied`, `direction_applied=1`,
`direction_source=Flash_socket_plus_Z`, and `fallback=none` for weapon index 76,
current model/source generation, a fresh sample, `Flash_handle=2`,
`Flash_transform_available=1`, and finite, non-degenerate
`Flash_local_rotation`. For this known asset, also require
`Breach_handle=4294967295`, `Breach_socket_available=0`,
`Breach_transform_available=0`, and zero `Breach_to_Flash_units`. Preserve
the Flash local position/rotation, visible barrel direction,
Retail/controller directions and dots, candidate Flash origin, untouched Retail
fire position, and their distance. Any `m5_handgun_muzzle_aim_fallback` must
be correlated with its named gate and cannot count as a candidate shot. If this
verified asset unexpectedly exposes a valid Breach, preserve the record and
investigate identity/state before treating its two-point direction as evidence
for this Flash-only gate.

Accept direction alignment only when repeated mid-range impacts follow the
visible barrel through yaw and pitch with no reversed or stable angular offset.
A mismatch only at close range is a separate origin/parallax finding because
this pass does not move `firePosition`. Reject the candidate for crashes,
weapon/model lifetime errors, changed firing/ammo behavior, non-index-76
changes, or any valid tracked shot that bypasses both candidate and explicit
fallback diagnostics. A consistently reversed or orthogonal applied direction
invalidates the accepted Flash +Z result for that build. Finally verify stale/absent VR
input and a flat-screen launch retain Retail vectors. Preserve the fresh run
directory, launch report, logs, source state, staged DLL hash, saved grip record,
actual observations, and rollback.

### Forensic tool control gate

At a playable crime-scene prompt with `-CoreActionsProbe` armed:

1. Press plain Y: Retail's pause menu must open and no forensic tool may ready.
2. Resume, release every control, then push the right stick fully up once: the
   expected forensic tool must ready exactly once.
3. Release the stick, repeat the gesture, and confirm Retail accepts a later
   tool action rather than treating the first edge as permanently held.
4. Turn fully left and right without a large upward component; the forensic
   tool must not ready. Both grips + Y must still open VR Tools.

Accept the gate only when `m4_binding_core_action_applied` records command 116
with `control=right_stick_up` on press and release, command 116 produces the
headset-visible Retail tool behavior, and both Y paths retain their roles.

`run-20260810-080738` passes the bounded readiness portion: the tester confirmed
that right-stick up pulls out the forensic tool, and telemetry recorded 22
matched command-116 press/release pairs with zero core-action rejections. The
run also recorded 13 ordinary Y pause dispatches. Treat ready-tool selection as
live accepted; retain the ordinary-turn/no-accidental-ready check and the
both-grips + Y regression whenever the next forensic-tool session exercises
those paths.

### Observation-only forensic memory trace

Launch the isolated stage with:

```powershell
.\tools\launch-condemned-m2-vr.ps1 -WeaponTest Pipe -ForensicMemoryProbe
```

The launcher must wait for `m5_forensic_camera_socket_ray_armed`,
`m5_forensic_memory_probe_armed`, and `m5_forensic_observers_armed`. It
must abort on either `m5_forensic_camera_socket_ray_rejected` or
`m5_forensic_memory_probe_rejected`. At a valid crime scene, use right-stick
up for the UV light, find the target, then use right-stick up again for the
camera. Use native left-stick zoom as needed.

Use the right controller to move the forensic assembly, but judge optical aim
by the white alignment arrows and live image on the camera screen. Those cues
are authoritative; neither raw controller-forward nor the body mesh alone
defines the photographed direction. Do not press Fire until Retail beeps and
the camera light remains green. Hold that pose and press the right trigger
exactly once. Do not hammer Fire or switch to flatscreen. If it fails, keep
the scene open so the socket/query diagnostics preserve the evidence.

Accept the diagnostic capture only when:

- Tool and Fire command edges have unique trace IDs and matching release
  records; Activate is optional unless the scene uses it;
- all observation-only records report `engine_writes=0`, with no probe
  rejection or unreadable tracked span;
- `m5_forensic_camera_socket_pose` identifies `display_kind=scanner`,
  `socket=Camera`, `result=LT_OK`, and a finite world pose/forward;
- every `m5_forensic_camera_socket_ray_query` references a recent socket
  sequence with `socket_age_ms` at most 250, `query_restored=1`,
  `retail_filter_preserved=1`, and `engine_state_writes=0`; only its
  temporary caller-stack start/end prefix may differ;
- `m5_forensic_display_state` resolves the actual Scanner or DigitalCamera
  object to its verified vtable and records six Scanner bytes or four
  DigitalCamera bytes;
- `m5_forensic_command_dispatch` records the same bytes immediately before and
  after the exact Retail command-on/off callback, including Scanner
  `target_hit`, `framing_ok`, and final `can_photo`; and
- the headset-visible beep/green-light cue and photo outcome are correlated
  with that exact Fire edge.

Memory telemetry can identify native state changes but cannot prove the photo
advanced the scene. Record the headset-visible outcome separately. Treat an
OpenXR failure before the game starts, a missing crime-scene prompt, or a run
without both armed events as **no evidence**, not a failed forensic result.

`run-20260810-103612` is the historical successful-mouse capture. Sixteen VR
Fire downs at output `1` failed, while a later physical mouse Fire at `128`
took the photo after the tester switched to flatscreen and realigned. That run
raised a magnitude hypothesis but did not compare identical aim validity. It
also rejected the assumption that `weapon+0x90` directly owns the known
Scanner or DigitalCamera subclass vtable.

`run-20260810-105904` falsifies the magnitude hypothesis. VR Fire at the
temporary value `128` and two physical mouse Fire edges at `128` all failed in
the same unchanged Scanner state. Later, VR Fire operated the Colt nine times.
The native Fire binding range is `(0.100,99999.000)`, so value `1` was already
active. The temporary magnitude adaptation is removed.

`run-20260810-112423` live-arms the first actual-object observer. Scanner index
46 resolves vtable RVA `0x0014AB44`. Static analysis and a bounded read-only
live sample classify Scanner `+0x1DB` as target hit, `+0x1DC` as framing valid,
and `+0x1DD` as final `can_photo`. A controller sweep produced five windows
where all three were one; the tester confirmed those windows match Retail's
beep and green camera light. Head aim does not drive the Scanner.

The same run contains the successful physical control: trace 20 Fire `128`
changed the Scanner's first four bytes to `0x01010101`, then `0x01010100`, and
index 46 advanced to phone 4 before trace 21. The old observer omitted the
last two gates on that exact edge. Its final log SHA-256 is
`7350A928463DCFED3937447790ACAA55796301A6DF51F63C01D556BDB10900B9`.

`run-20260810-115015` is the controlled valid-gate VR failure. One right
trigger down reached Retail with controller output `1` while target hit,
framing, and `can_photo` were all one; release retained all three. The tester
received the Fire haptic but no flash or photo, and Scanner remained index 46
through 256 frames. Do not classify this as aim or eligibility failure. Its
preserved loader snapshot SHA-256 is
`C32359EA48CB5B0C7F59CDA18586A25DFD5926AF5C918CAC33A781B8BF1B97A2`.

`run-20260810-121200` proves valid VR Fire reaches PlayerMgr: 24 Scanner-46
dispatches retained all three gates at one. None entered generic
`CClientWeapon::Fire`. Do not classify that absence as a PlayerMgr rejection:
static verification shows Scanner type `0x15` can instead call the special
collection action at `GameOrig+0x000E8F00` from callsite `+0x000A1351`, then
return before generic Fire. The run's one physical mouse-`128` edge had all
three gates zero and is not a valid comparison. Its loader snapshot SHA-256 is
`925715B6773C396CB58518F80BF5414E8DA904BC0CFF57B97AA946DEFB35F52F`.

`run-20260810-123242` is the controller-context baseline: five valid
Scanner-46 PlayerMgr callbacks had all UI gates one, but neither dispatch route
ran. A read-only live sample showed PlayerMgr's separate cached activation
result at kind zero with a null target. The Scanner UI ray and the Retail
action-target ray were conclusively different.

Its preserved loader snapshot SHA-256 is
`5F3E15664DAEC3D14EA0C32FFA7FF690B1DBA8F559AD1683EFDFB56C5DEB2B77`.


Scanner acceptance requires all of:

- `m5_forensic_camera_socket_ray_armed`;
- a fresh Scanner `m5_forensic_camera_socket_pose` captured from Retail's
  `Camera` socket;
- at least one `m5_forensic_camera_socket_ray_query` from each exercised
  Retail callsite, with a recent matching socket sequence, preserved range,
  and restored query;
- `m5_forensic_player_fire_branch_input` with Scanner 46, readable target
  cache, kind three, and a non-null cached target;
- `m5_forensic_player_command_dispatch` with all three gates one;
- `m5_forensic_collection_action_dispatch` with a non-null target; and
- a headset-visible flash/photo plus Scanner-to-phone or scene progression.

The collection action is the expected Scanner route; generic weapon `Fire`
need not appear. A missing/stale socket fallback localizes capture timing.
If fresh Camera-socket queries never intersect while the arrows are green,
verify the socket-local forward axis. If intersections occur but PlayerMgr
still has no route, investigate Retail filter/classification and cache update
timing. If collection action runs without a photo, trace downstream from
`GameOrig+0x000E8F00`. The geometry helper and full build pass 19/19 x86
plus 15/15 x64 tests.

`run-20260810-143142` passes the full Scanner acceptance gate. On the
successful VR-trigger edge, PlayerMgr read cached kind three with non-null
target `0x38EEF848`, the native collection action returned `handled=1`, and
the tester confirmed the visible photo fired. Preserve the decisive checkpoint
at
`stage/condemned-m2-mono/logs/run-20260810-143142/condemnedvr-loader-photo-success-checkpoint.log`
(431,677 bytes; SHA-256
`3F29DCB159E7F9504E2F1E1E375C62603EA90CDE5B8474924168E294DB62AD25`).

The following tool is stable catalog index 3, `Camera` / `WEAP_Camera`.
Scanner-only `run-20260810-143142` proved the baseline: DigitalCamera
published fresh successful `Camera`-socket poses, but the hook selected stale
Scanner data and PlayerMgr remained kind zero.

The mapped candidate kept index 46 on Scanner, mapped only index 3 to
DigitalCamera, and excluded index 6/unknown identities. It passed 19/19 x86
and 15/15 x64 tests at x86 SHA-256
`FD2311E189650AC3DD79FB7A887D0DBDCA40487064180CD385E69FF36655F638`.

`run-20260810-145113` passes the complete index-3 gate. The successful query
used `weapon_index=3 pose_display_kind=digital_camera`; the Fire edge had
DigitalCamera state `0x01010101`, PlayerMgr cached kind three with non-null
target `0x38B4F9A0`, the native collection action returned `handled=1`, and
the tester confirmed the visible Item Camera result. Preserve
`stage/condemned-m2-mono/logs/run-20260810-145113/condemnedvr-loader-item-camera-success-checkpoint.log`
(1,393,735 bytes; SHA-256
`1DC321F8DDA0C61BC7FEE84D14BE47C530636CC4A782E7A1CD68F0610A870047`).

Scanner index 46 and Item Camera index 3 are both live accepted. Retain one
regression edge for each whenever the shared Camera-socket hook changes.

`run-20260810-134726` conclusively falsifies raw controller-forward as the
Scanner optical axis. The completed snapshot contains 209 bounded query
records, 167 intersections, zero restoration failures, 89 Scanner-ready
PlayerMgr branch records all at cached kind zero, zero kind-three caches, and
zero collection actions. Scanner 46's saved grip is
`position=(-0.15,-0.90,0.65), rotation=(20.0,85.5,25.5)` degrees; the visible
model applies the grip correction while the failed ray did not. Preserve this
baseline at
`stage/condemned-m2-mono/logs/run-20260810-134726/condemnedvr-loader-snapshot.log`
(1,412,180 bytes; SHA-256
`7881DF6FCA814885DA78288CEF1751744061B261FCD5D4259D9BC66BBAE42ACC`).

The white alignment-arrow layer and camera body/screen are separate, and the
embedded image follows the arrows. Both forensic display types share Retail
vtable slot `+0x24` (`GameOrig+0x000F4CB0`) for the named `Camera`-socket
world transform. Capture that successful output directly; do not substitute
hand-IK rotation or infer the optical axis solely from the camera body. The
headset-accepted Scanner build SHA-256 is
`91B60E0EFEA049599EDF221A40D28957C2DC997D655C7ED6731966530CD02512`;
the tested index-3 candidate SHA-256 is
`FD2311E189650AC3DD79FB7A887D0DBDCA40487064180CD385E69FF36655F638`.

## M5 physical-melee gates

Physical melee is intentionally incremental. Validate each gate separately:

1. Fresh controller world pose and finite kinematics.
2. Wall-only collision proxy with native actor damage blocked.
3. Visible weapon proxy aligned through the per-weapon grip profile.
4. Weighted follow behavior with safe resets after tracking loss, weapon
   changes, recentering and long frame gaps.
5. Swing adapter only for explicitly mapped weapons; unmapped weapons remain
   disabled.
6. Pipe-only contact damage after the first Retail collision seed: native
   handoff, overlap de-duplication, reference-vector cleanup, configurable
   tip travel plus swing-end rearm, and speed-only Require Swing must agree.
   Bounds, persistence, qualification, one-metre fast-follow-through rejection,
   release hysteresis, jitter reset, and transient-invalid-sample latch hold
   pass automated tests;
   `run-20260808-165819` live-accepts the speed threshold, repeated dispatch,
   cleanup, and original Melee UI. `run-20260809-035231` falsified distance-only
   reset; `run-20260809-065827` validated most swing-end behavior but exposed
   two transient-sample latch clears. `run-20260809-095447` live-accepts the
   corrected Pipe lifecycle at 0.12 m with zero same-target reaccepts. The
   transient-invalid hold itself remains regression-tested because the live
   counter was zero.
7. The explicit mapped one-handed set now enters the same shared state machine.
   Index 29 (`Pipe`) has live engine proof for four-record Pipe-baseline
   inheritance, constructor `read_mask=0x7`, configured overlap, speed
   qualification, clean Retail references, and native impact forwarding.
   Treat every other newly mapped asset as implemented/automated-only until it
   has at least one bounded live pass. Require accurate slow/fast overlap,
   de-duplication, rearm, clean Retail references, and visible damage; one
   weapon's pass is architectural evidence, not acceptance for every model.

For the fastest headset loop, use:

```powershell
.\tools\launch-condemned-m2-vr.ps1 -WeaponTest Pipe -Wait
```

The retained `Pipe` preset name identifies the accepted baseline; it enables
the mapped one-handed allowlist and deliberately leaves two-handed attachment
off. The preset automatically starts a hidden weapon-diagnostics watcher. Poll
`weapon-diagnostics-live.json` in the newest session directory while the user
remains in headset. It gives a direct phase, recommendation, last contact,
reference-vector result, capsule-to-target contact distance and cumulative
counters without mining the raw loader log. The adjacent
`weapon-diagnostics-events.jsonl` is the compact timeline.
Before another launch resets the shared loader log, rebuild a completed or
interrupted session's snapshot with:

```powershell
.\tools\watch-condemned-weapon-diagnostics.ps1 `
  -Run run-YYYYMMDD-HHMMSS -Once
```
After a later launch, preserve and use the existing per-run snapshot/event files
rather than treating a replay from the replaced source as archival evidence.

Treat `retail_reference_vector_failure` as a concrete lifecycle failure.
`first_contact_observed` should progress to `repeated_contact_observed` after a
withdrawal/recontact; `contacts_rejected` identifies the last rejection reason.
Read the newest snapshot without modifying it with:

```powershell
.\tools\read-condemned-weapon-diagnostics.ps1
.\tools\read-condemned-weapon-diagnostics.ps1 -Json
```

### Combat-behaviour diagnostic pass

This pass observes four independent questions without changing combat:
accepted-hit/defeat cadence, command-17 attack telegraphing, closest practical
stand-off, and command-28 block/health behavior.

1. Record the exact staged loader hash and use `-WeaponTest Pipe -Wait`.
   Require `m5_combat_player_vitals_armed` for the health sub-test. A
   `m5_combat_player_vitals_rejected` event makes only player vitals
   unavailable; it must say `behavior=melee_unaffected`.
2. Load gameplay, equip Pipe, and leave the current Melee/Weapon settings
   unchanged. Require an initial `m5_combat_player_vitals` sample when the
   verified stats singleton becomes available.
3. Land three deliberate actor hits, completing the release/rearm phase between
   swings. Record visible dodge/weave and the hit on which the actor is visibly
   defeated. Do not infer enemy health from dispatch count.
4. Let an enemy attack once with left trigger released. Then hold left trigger
   long enough to record command 28 `edge=down` and let the enemy attack once
   while it remains active; release and require `edge=up`.
5. Move as close as Retail permits. Make one slow contact and one normal swing
   through the actor so the contact stream samples the practical boundary.
6. Exit normally and preserve the final schema-v4 snapshot/event stream plus
   host, bridge, loader, launch report, source state, and staged hashes.

Evaluate these fields independently:

- `Combat.AcceptedActorHits`, per-target accepted count and runtime interval,
  duplicate blocks, rearms, and visible defeat observation establish cadence.
- `AutomaticSwingAttackTriggers` plus each contact's `TriggeredThisSwing`
  describe only the motion-triggered feature. `RetailAttackCommandDownEdges`
  and `RetailAttackCommandUpEdges` count every command-17 transition, including
  manual trigger use. Keep those sources separate when correlating visible AI
  reaction; lack of reaction without either command does not falsify Retail AI.
- `MinimumActorHeadHorizontalDistanceMeters` is HMD XZ-to-Retail-contact
  distance. It is not a player-capsule radius and cannot by itself authorize a
  collision-size change.
- `PlayerVitals`, block down/up edges, and health decreases while command 28 is
  active versus inactive measure only the binding-command path. They do not
  observe Retail's native block-state lifetime. Automatic pose entry/exit is a
  separate stream; a single comparison is diagnostic, not defensive acceptance.

Fail the regression if diagnostic hooks change collision/native-forward counts,
cause a signature-verified path to stop arming, affect keyboard/mouse fallback,
or disturb player movement, AI, firearm, forensic, focus, or host-loss behavior.

Live diagnostic `run-20260812-100216` used VirtualDesktopXR 1.0.10 / Quest 3
and staged `GameClient.dll` SHA-256
`96C17087069654ABA75A321BDCEC4ECA08EBD0BED622A6750DF70B75BE483505`.
It recorded six actor accepts, one world/prop accept, seven rearms, 44 duplicate
blocks, zero same-target accepts before rearm, and 513/513 clean Retail-vector
releases. Repeated actor accepts were at least 1,266 ms apart. All accepts were
non-telegraphed under the saved `swing_attack=0` Pipe setting, so there were zero
automatic swing-attack triggers. The raw log separately contains five manual
controller-applied command-17 down/up pairs. The tester reported enemy reaction
only during Retail-trigger attacks; physical-only swings produced no visible
anticipation. Enemies visibly died in one or two hits. Five actor accepts used
node handle `0x00000006` and one used `0x00000026`, but their semantic names and
per-death correlation are unavailable, so head-hit scaling remains a hypothesis.

The nearest actor callback was a slow rejected contact at 0.4729 m HMD-XZ to
the Retail surface; the configured capsule was already overlapping by 0.0132 m.
Five command-28 down/up pairs were ordered around no health decrease, while all
12 decreases appeared outside an active pair. The tester nevertheless could not
get blocking to work while attacked, failing the visible defensive gate. The
preserved watcher's UTC fields are ingestion times and must not be used as hold
durations. This live-validates diagnostic plumbing and single-dispatch cadence,
but not enemy health/damage amount, semantic hit location, Retail player-capsule
size, automatic AI telegraph integration, or blocking efficacy. The preserved
event stream exposed 11,910 alternating collider-state records. The watcher now
compacts those to state transitions and first-seen addresses while exposing
observed/recorded/suppressed totals; the synthetic A/B/A/B/A regression passes.
It also counts all command-17 edges independently of automatic swing triggers.
New loader command-edge records carry `runtime_tick_ms`, and block duration is
derived only from those monotonic ticks. Synthetic command-17 down/up and 250 ms
block-hold assertions pass. The complete gate passes 21/21 x86 and 17/17 x64
plus the watcher regression; built and canonical-stage `GameClient.dll` share
SHA-256
`9E4E1B4CC0FC5C569AC8A3A3019A70292CEC4DFF713D9FC6F3BAE9F683741B57`.
These tooling refinements were subsequently exercised by
`run-20260812-124543`, described below.

For the bounded automatic-swing-attack ON pass, change only the player override;
packaged defaults remain OFF. Launch the canonical Pipe shortcut and require
the loaded profile to report `swing_attack=1`. The retained automatic threshold
is 10.0 m/s; the preceding live actor hits reached only 7.342--9.304 m/s. Use
the Melee live-speed indicator and deliberately exceed 10.0 m/s once. A run
without that threshold crossing supplies no automatic-trigger evidence. First
make that qualified near miss, then one above-threshold physical hit. Require an automatic
`m5_physical_melee_swing_attack_triggered` record and matching command-17 edges,
but still require damage only after accepted physical contact. Record whether
the enemy dodges/weaves and whether the Retail attack animation causes visible
arm, weapon, controller, or timing mismatch. Do not change hit speed, collider,
weight, cooldown, or rearm during this A/B. After assessing the pass, either
promote the result explicitly or return `SWING ATTACK` to OFF/restore
`weapon-settings.before-auto-swing-test-20260812-215524.ini` before treating the
old baseline as active again.

Live ON result `run-20260812-124543` loaded `swing_attack=1` with staged
`GameClient.dll` SHA-256
`9E4E1B4CC0FC5C569AC8A3A3019A70292CEC4DFF713D9FC6F3BAE9F683741B57`.
It recorded 35 automatic threshold crossings at 10.003--25.206 m/s and 35
matching command-17 down/up pairs. One additional unmatched pair is consistent
with a manual trigger. All 36 command-17 rising edges requested the existing
right-hand action haptic. Twelve actor contacts across four targets qualified
and were native-forwarded, with 121 duplicate callbacks blocked, 12 rearms,
zero same-target accepts before rearm, zero multi-target swings, and 515/515
clean Retail-vector releases. Five accepts occurred during the automatic pulse
and seven after it. This accepts the motion-to-command and physical contact-
lifecycle portions of the test, but tester observation is still required for AI
dodge/weave, Retail animation/pose quality, visible damage behavior, and the
acceptability of pre-contact haptics.

The tester subsequently confirmed that enemies did dodge/react to those
automatic pulses, but the Retail animation visibly wound up and attacked after
the controller-driven physical hit had already landed. This fails the visible
timing gate and introduces a possible delayed second damage opportunity. Do not
claim that extra damage occurred without a separate engine/result observation.
Automatic command 17 is rejected as the shipping telegraph path. The active
player Pipe record was restored to `swing_attack=0`, SHA-256
`2095B603D697AD25F9652CE04CB9D8DC44547D5B9E5AF910DBD0AD5A9B979B78`.
The rejected ON record is preserved as
`weapon-settings.auto-swing-rejected-20260812-230136.ini`, SHA-256
`D8796C94A26DF97694BB8E178C092832DCE5D1AFDDB0A6CA3D9084F08B64A1D1`;
packaged defaults remain OFF.

Do not accept the configured cooldown from this run. Automatic triggers 6 and
7 began 62 ms apart even though the Pipe record specified 450 ms; the first
pulse ended after 47 ms and the next began 15 ms later. A transient state reset
is the current code-supported hypothesis, but the reset reason was not logged.
Do not infer block efficacy from the five 109--203 ms command-28 intervals or
the nine health decreases outside them. The tester first described those
attempts as failed, then clarified that a trigger tap visibly enabled many
blocks without remaining held. This verifies visible blocking but not the
native state lifetime or the hypothesis that the physical weapon collider
decided interception. The edge records also lack raw trigger and eligibility-
reason fields. Use the automatic guard-pose gate below rather than the obsolete
two-second trigger-hold test. The preserved snapshot, event stream, and loader checkpoint
have SHA-256
`518F024CC40F3F62F85237988896D72D43B765518CE8BB99CC0BB361B0FD99A9`,
`70D0F348AC9FE1833C605A724734714E1DC2BFD5FD1E81035BD01A797858214E`,
and `37DD60127A4A802056DB62CBFE986E5E1B1D5F36CC44477697452AE0476E061E`.

### Automatic guard-pose live gate

Two live candidates define the current boundary. `run-20260812-140353` proved
pose entry/down and exit/up but defense remained latched. In
`run-20260812-150308`, all three guarded same-weapon exit checks found
`CS_Block` already inactive and made no engine handoff, yet defense still
remained outside the pose. The latter run correlated every automatic entry with
`EnableCollisions(..., bBlocking=true)` and exposed that the generic physical-
melee lifetime hold was also extending that block record. The second-stimulus
release experiment is rejected and disabled.

The current candidate keeps only positively classified attack collisions alive;
block and unknown records retain Retail lifetime. The full gate passes 22/22
x86 and 18/18 x64 plus the schema-v4 watcher. The tested and canonical-stage
loader SHA-256 is
`C44ABD8267A861E2F55714E31233880A50E25939AC48DB0B3703828F09ADAFDF`.
Use Pipe first and keep `SWING ATTACK` off.

1. Launch the canonical `-WeaponTest Pipe -Wait` stage and require
   `m5_physical_melee_block_pose_armed` to report
   `exit=retail_finite_block_window`,
   `second_CS_Block_release=disabled_after_live_rejection`, and
   `collision_lifetime=classified_block_retail_window`. Preserve the source,
   staged hash, launch report, and fresh run directory.
2. Close VR Tools and do not touch left trigger. Enter the saved Pipe pose.
   Require exactly one automatic pose entry and command-28 down edge plus the
   normal Retail sound/haptic feedback.
3. Require one `m5_physical_melee_collision_role_classified` record for that
   seed with `blocking_argument=1`, `role=block`, and
   `lifetime_policy=retail_window`. Its collision updates must report
   `role=block continuous_lifetime=0`; an unknown role is a fail-closed
   diagnostic result, not a pass.
4. Leave far enough to cross release hysteresis. Require one pose exit, one
   command-28 up edge, and
   `m5_physical_melee_block_pose_exit_retail_owned` with
   `engine_handoff=none`. No native-release-handoff event or second visible
   block animation is expected. Wait at least one second, longer than the
   approximately 0.43-second duration observed on the prior block seed.
5. While clearly outside the pose, allow a deliberate enemy strike. Defense
   must not remain latched. Record visible block/no-block, health before/after,
   pose errors, collision object lifetime, and weapon interception separately;
   a command-28 up edge alone is not evidence of defense release.
6. Re-enter and leave the pose at least three times. Each entry must create a
   newly classified finite block record and reacquire defense; every exit must
   return to a non-blocking state without another engine handoff.
7. Perform a normal fast physical Pipe hit. Require a distinct classifier event
   with `blocking_argument=0`, `role=attack`, and
   `lifetime_policy=continuous_contact_damage_candidate`, followed by updates
   with `continuous_lifetime=1`. Confirm the collider still contacts and damages
   an enemy. This is the regression proving the latch fix did not disable the
   physical attack path.
8. Repeat once with manual left trigger and once across a weapon switch or menu
   transition if practical. Manual input remains Retail-owned; stale role state
   must not transfer to another weapon. Keyboard, mouse, firearms, two-hand
   weapons, forensic tools, movement, collision, physical damage, and
   host-absent fallbacks remain unchanged.

Accept only when defense no longer persists outside the pose, repeated pose
entry works, every block record stays `continuous_lifetime=0`, and a separate
attack record still reaches `continuous_lifetime=1` and damages an enemy. If a
classified finite block collision object disappears but defense remains, move
the next diagnostic to Retail's separate `BLOCKWINDOW`/server state. If the
block collision itself outlives its Retail duration, instrument expiration and
teardown around `UpdateCollision`. Do not reintroduce repeated `CS_Block`
stimuli or write an inferred weapon state directly.

### Dedicated block-collider and block-window live gate

The dedicated block-collider candidate is automated-tested but has not been
staged into a runnable game or exercised in a headset. Keep the per-weapon
block-window override `OFF` for the first pass so Retail remains authoritative.

1. Equip the Pipe and open `BLOCK COL`. With no `block_collider` weapon record,
   require `SOURCE: ATTACK FALLBACK`. Confirm its displayed dimensions and pose
   match the current attack collider, including any earlier attack-collider
   calibration.
2. Enable `DRAW ATTACK COLLIDER` and `DRAW BLOCK COLLIDER` independently. At
   rest, require an amber attack capsule and a blue block capsule occupying the
   same calibrated pose; neither visibility control may affect collision.
3. Change exactly one `BLOCK COL` component and save. Require
   `SOURCE: WEAPON RECORD`, then confirm only the blue block capsule moves.
   `COPY CURRENT ATTACK COLLIDER` must restore the current attack geometry as a
   dedicated block record; it does not return the weapon to inherited fallback.
4. Enter the calibrated guard pose during a real enemy strike. Require a
   player-owned native capsule record with `role=block`,
   `blocking_argument=1`, and `continuous_lifetime=0`. The block capsule should
   turn cyan only while its own Retail collision body is live; the attack
   capsule must not turn green merely because blocking is active. Preserve the
   verified `0x7` Retail read mask and one `CS_Block` entry per pose edge.
5. With the custom timing toggle still `OFF`, require the block-window record
   to report `override=0` and identical finite positive `retail_ms` and
   `applied_ms`. Correlate an accepted defense with both the guard-pose interval
   and weapon/enemy-attack overlap; do not accept pose entry alone as proof of
   spatial blocking.
6. Leave the pose and wait past the observed Retail window. Confirm defense and
   cyan liveness both end, then repeat the pose and block a second strike.
7. Only after that baseline, enable one conservative custom block window in the
   `BLOCK` tab. Require `override=1`, preserve the reported Retail duration, and
   confirm the applied duration matches the selected 100--2000 ms value. Test
   one shorter and one longer bounded value without changing collider geometry.
8. Perform an ordinary physical attack. Require `role=attack`,
   `blocking_argument=0`, the attack capsule turning green, and no false block
   liveness. Confirm the hit still uses Retail damage/reaction dispatch.
9. Turn all three debug drawings off, restart normally, and require them to
   remain hidden while attack and block collision continue to work.

Accept only when attack and block roles have independent geometry and liveness,
the inherited fallback and first-edit transition are observable, the Retail
duration remains unchanged while override is off, and guard exit reliably ends
defense. Treat menu rendering, live capsule placement, block overlap, timing,
and native expiration as awaiting live verification until this gate passes.

Before this live gate, the headset-free menu test must construct complete
`BLOCK`, `BLOCK COL`, and `DEBUG` pages and prove each fits the exact
triangle-vertex cap shared by the producer and D3D9 bridge. This covers live
regression `run-20260813-034938`: the menu state stayed open, but the former
24,576-vertex bridge cap rejected representative Block (29,820) and Debug
(27,402) pages while Block Col (21,876) remained visible. On relaunch, navigate
repeatedly through all three pages, require no
`m5_vr_tool_menu_overlay_failed`, and confirm selected rows remain visible
and interactive. A draw rejection must report both `vertices` and `limit`.

Live run `run-20260813-040058` passes this menu-rendering gate. It recorded
18 Block, 15 Block Col, and 16 Debug navigations plus ten Debug adjustments,
all while open, with zero overlay-failure events. The tester confirmed all
three pages remained visible and interactive. The exact staged loader and
bridge hashes are recorded in `CONDEMNED-M5.md`. This result does not replace
the separate block-collision and timing gate above.


Equip the intended mapped one-handed weapon, then open the Debug tab's Melee
view. Confirm the exact Retail index/profile and `CONTACT DAMAGE ON`.
`Unarmed`, ordinary firearm states, two-handers, and unknown indices must
remain excluded. Confirm the wireframe follows the held weapon: amber means it
is still waiting for the first Retail collision seed, while green means the
collision body is live.
The cross at the tip is the exact proxy origin. The overlay is intentionally
visible through geometry.

The first three selectable rows in `DEBUG` are visibility-only developer
controls:

- `DRAW ATTACK COLLIDER` hides or restores the amber/green attack capsule.
- `DRAW BLOCK COLLIDER` hides or restores the blue/cyan block capsule.
- `DRAW CONTROLLERS` hides or restores the controller/grip calibration
  wireframes when a calibration tab is active.

All three default to `OFF` in the compiled fallback and packaged release
settings. They are a versioned global player preference under `[debug] draw`,
independent of the equipped weapon, and every successful change saves
immediately to `%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`. Version 2 stores
`attack,block,controllers`; a legacy version-1 record migrates with block drawing
off. Turning any row off must not disable the native collider, contact damage,
controller input, grip/IK calibration, or per-weapon settings.

The automated gate must prove hidden defaults, three independent row
transitions, version-1 migration, version-2 ON/OFF save/load round trips,
player-over-package precedence, and malformed player data failing closed rather
than exposing any drawing. It must parse every declared record from the actual
copied post-build INI. The live
restart gate is:

1. Preserve the player's existing INI and launch the exact staged candidate.
2. Confirm all three drawings begin hidden. Toggle each on independently, then
   leave all three off; require `m5_debug_draw_settings_saved result=ok` for
   every change.
3. Land one physical hit with the collider hidden.
4. Exit normally and relaunch. Before changing the menu, require
   `m5_debug_draw_settings_loaded result=ok collider=0 block_collider=0
   controller=0` and no visible debug geometry.

Live runs `run-20260811-102206` and `run-20260811-102500` pass the
save/restart portion. They are separate game processes. The later loader
started at `collider=0 controller=0`, recorded eight successful saves ending
at `0/0`, and recorded zero save failures; the player INI ended at
`[debug] draw=1,0,0`, matching the headset tester's observation. Three
actor contacts after the final OFF save were accepted/native-forwarded, and
the completed run had 515/515 clean Retail reference-vector releases. The
preserved loader checkpoint SHA-256 is
`ECC05757BD7B2DD7EE3A2864FA5E38983551FAB38CC5145DF990B9778C8BB993`.
This accepts player override persistence; use the isolated-path procedure below
before claiming live packaged per-weapon fallback. These runs predate the
version-2 block-draw row and are not evidence for its rendering or persistence.

Historical live baseline `run-20260809-124936` used the earlier default-on,
session-only implementation. It accepted the draw controls and their
independence: the ordered loader log records
`1/1 -> 0/1 -> 0/0 -> 0/1 -> 1/1`. A later collider-off pass still produced
32 accepted/native-forwarded damage dispatches, 28 rearms, three multi-target
swings, 532/532 clean Retail reference-vector releases, and zero reference
failures. Do not treat that run as proof of the new restart persistence.

To validate shipped weapon defaults without touching the real profile, launch
one bounded run with `CONDEMNEDVR_SETTINGS_PATH` pointing to an isolated,
nonexistent writable INI. Keep the staged `condemnedvr-defaults.ini` beside
`GameClient.dll`; require the calibrated Pipe records to load and verify its
accepted pose, collider, Hit Speed, and Rearm Travel. A missing packaged file
must fall back safely rather than creating or copying a player calibration.

For physical-hit tuning, select `MELEE`. These are per-weapon, auto-saved
settings. A newly mapped one-handed index initially reports
`source=pipe_baseline` for Melee/Weapon, Collider, Grip, and right-hand IK.
Editing it creates only that index's record; relaunch and confirm
`source=weapon_record` for the edited record while the Pipe remains unchanged.

- `Require Swing`: toggle with A or the right stick. Off allows overlap-only
  contact; on requires the physical weapon to clear Hit Speed.
- `Hit Speed`: 0.25--10.00 m/s, adjusted in 0.25 m/s steps.
- `Rearm Travel`: 0.02--1.00 m, adjusted in 0.01 m steps. It measures
  weighted-tip travel from the accepted contact and does not mean distance from
  the enemy.
- `Live Speed / Fast Enough`: read-only feedback from the exact weighted
  collider frame used by the hit gate.
- `Hit State`: read-only READY/LATCHED feedback. LATCHED also shows whether
  travel is complete and the low-speed reset count from 0/3 through 3/3.

Use this acceptance sequence:

1. While the menu is open, move the held weapon and confirm Live Speed changes and
   Fast Enough flips at the configured threshold.
2. Close VR Tools before contacting an enemy; damage intentionally fails closed
   while the menu is open.
3. With Require Swing off, a slow overlap should dispatch exactly once.
4. Turn Require Swing on, close the menu, and repeat slowly. The callback must
   be blocked as `swing_not_qualified` with no visible hit.
5. Swing through the same target faster than Hit Speed. Require one accepted
   native forward and one visible reaction.
6. Continue that same fast follow-through well beyond Rearm Travel. The target
   must remain LATCHED and no second damage forward may occur.
7. Finish the swing naturally. Once travel is complete, require three
   consecutive samples at or below the displayed reset speed before Hit State
   becomes READY. A one-frame slowdown followed by renewed speed must reset the
   partial count rather than rearm.
8. Perform a new fast swing for exactly one additional hit. Also sweep through
   two nearby enemies once: each distinct target may be hit once, but neither
   may repeat before READY.

Validate automatic collider readiness on a fresh mapped one-handed pickup
before treating the physical-hit sequence as usable:

1. Do not press Fire. Hold the newly acquired weapon/controller still with the
   game focused, tracking fresh, and VR Tools closed.
2. Require one ordered `m5_physical_melee_auto_seed_candidate` -> `started` ->
   `confirmed` -> `ready` sequence. The start must produce exactly one bounded
   command-17 down/up pair for that attempt. Confirmation must report
   `source=automatic_equip_pulse`, `player_attack_classified=1`, and exact
   `native_read_mask=0x7`; amber may turn green at confirmation, but combat is
   not ready until the one-second settling phase completes.
3. Keep a harmless target or wall overlap available during seeding. Any seed
   callback must report `automatic_seed_damage_blocked`, clear its Retail
   reference vector, leave the contact latch unmodified, and produce no native
   damage forward, visible target reaction, or action haptic.
4. Immediately after `ready`, perform one ordinary physical swing without a
   manual Fire input. Require the normal physical contact gate and native
   result. This is the user-facing ready-on-pickup criterion.
5. Drop/switch the weapon and reacquire it. Require the old transaction to
   reset and a new stable identity to start from attempt 1. Opening VR Tools,
   losing focus/tracking, stale pose, unknown weapon identity, or active manual
   Fire must prevent/cancel the pulse without leaking input or damage.
6. If confirmation is absent, require two-second timeouts, 750 ms retry delays,
   and no more than three attempts for that equip. Only after
   `m5_physical_melee_auto_seed_failed` should the watcher recommend one manual
   Retail attack; that attack must still require exact body confirmation.
7. Record whether the equip-time pulse produces any visible weapon/arm attack
   animation or delayed native attack. A conspicuous or delayed second attack
   fails the usability gate even when damage suppression and body creation are
   correct.

The current automatic-seed implementation is headset-free tested only. Until
the sequence above passes, the historical one-manual-attack procedure remains
the live baseline and must not be described as automatic pickup readiness.

If it is misaligned, open VR Tools with both grips + Y and select `COLLIDER`.
Use the left stick to choose position, pitch/yaw/roll, length, radius, direction,
or reset; use the right stick to adjust and A to activate direction/reset.
Changes preview immediately and auto-save for the equipped Retail index.
Never assume another one-handed model shares the Pipe's visual alignment.

Verify weapon-model alignment persistence separately from collider alignment:

1. Open the `GRIP` tab and confirm the Pipe initially shows the last saved
   position/rotation. The loader must report
   `m5_weapon_grip_settings_loaded` with `result=ok` and either
   `source=weapon_record` for its own saved value or
   `source=pipe_baseline` for an unedited mapped one-handed weapon.
2. Make one reversible position or rotation adjustment. Require
   `m5_weapon_grip_settings_saved` with `result=ok` and the exact displayed
   values. The menu path auto-saves; do not start the guided object/hand action
   during this persistence-only check.
3. Return the adjustment to its intended value, quit normally, and relaunch.
   Require the same values before touching the menu and another successful load
   event. A stale profile ID or malformed record must instead log the fallback
   result and use profile defaults.
4. For the continuous both-grips fallback, move the weapon and press controller
   Y or keyboard P before quitting. Those controls save explicitly; continuous
   axis samples intentionally do not write at tracking frequency.
5. Treat RESET as persistent. Do not select it merely to inspect the row.

Live baseline: `run-20260809-103049` loaded the recovered Pipe grip, saved
each menu adjustment with `result=ok`, and ended at
`{2.5, 5.0, -3.5} / {-28, 0, 0}`. After process exit,
`run-20260809-103422` loaded that exact record into a new calibration slot.
The headset tester confirmed that the visible Pipe returned where they left it
and accepted Y 5.0. This is the live-accepted Grip-persistence baseline.

For an external live-alignment pass, leave the game running and keep the
wireframe visible while the headset tester moves the pipe. Codex or a developer
can send small acknowledged adjustments from another terminal, for example:

```powershell
.\tools\set-condemned-weapon-collider.ps1 -DeltaPositionY 1
.\tools\set-condemned-weapon-collider.ps1 -DeltaRotationX -5
```

The script resolves the newest active Pipe run, bases unspecified fields on the
last values acknowledged by that game, targets the exact PID and Retail index
32, enforces the Collider-tab bounds, and waits for an applied/rejected revision.
The proxy transform, drawing, and saved settings update without relaunching.
Retail fixes native length and radius when it creates the primitive, so reseed
after changing either dimension. Change one axis at a time and preserve the last
visually better set.

- Do not retune a wireframe that already matches the pipe. In the old gate-only
  build, green meant only that Retail's body was live. In the native-alignment
  build, require `m5_physical_melee_native_capsule_override` with
  `read_mask=0x7` before treating green as the aligned physics body.
- With the body seeded, hold the configured volume short of a wall at the old
  false-hit range. No wall effect and ideally no callback should occur. Any
  distant callback must remain blocked as `outside_configured_collider`.
- Move the green volume itself through the wall. Exactly one contact may be
  accepted only when `LastContact.Distance.CapsuleSurfaceGapMeters` is at most
  0.01 m; independently confirm that the wall effect occurs at that moment.
- Repeat against a new enemy. Exactly one accepted/native-forwarded contact and
  a visible reaction or health change are required; forwarding alone is not
  proof of damage.
- Every callback, accepted or rejected, must report vector state `cleared`,
  release its live references, and leave vector failures at zero.
- Keep the pipe overlapping and continue the fast stroke past the configured
  Rearm Travel: the accepted counter must not repeat. End the swing, observe
  three low-speed samples and READY, then recontact during a new fast swing for
  one further accepted dispatch.
- Baselines `run-20260808-060131` and `run-20260808-062240` reported
  0.1403--0.9229 m positive gaps and previously forwarded them. Verified
  disassembly identifies those as Retail's larger database-sized native shape,
  not a reason to move the already-correct drawing.
- In `run-20260808-074159`, the gate rejected 512 distant wall callbacks with
  zero accepts/forwards and clean reference-vector release. The tester then
  moved the drawn collider through enemies, but a 40-second poll showed zero
  later callbacks. Preserve this as proof that a dispatch gate alone cannot
  synthesize the missing true-overlap contact.
- `run-20260808-082609` is the accepted native-alignment baseline. Its seed
  reported `read_mask=0x7`, length 10.0 units, and radius 2.5 units. The
  confirmation snapshot contained 574 callbacks, 126 accepted/forwarded
  contacts, 338 duplicates, 126 rearms, seven targets, 574 clean reference
  clears, and zero failures. Two actor candidates produced ten actor-classified
  forwards with maximum accepted gap 0.0095 m; the headset tester confirmed the
  aligned pipe works against enemies.
- `run-20260808-165819` is the accepted physical-speed baseline. Require Swing
  was on with Hit Speed 7.25 m/s and Rearm Distance 0.12 m. All 446
  `swing_not_qualified` records were 0.083--7.189 m/s; all 16 accepted/native-
  forwarded damage dispatches were 7.256--13.865 m/s. The run also recorded
  26 held-overlap duplicates, 34 outside-collider rejections, 16 rearms, four
  targets, 522 clean reference-vector clears, and zero failures. The headset
  tester confirmed that speed works and the menu is good.
- `run-20260809-035231` is the distance-only rearm failure baseline. With Hit
  Speed held at 7.25 m/s, 0.20 m produced double/triple forwards, 0.30 m
  produced a double, and 0.35 m produced a triple plus a double against the same
  target. The first 0.40 m trial was clean, but its stress pass re-hit the same
  target seven tracking samples after reset. At 1.00 m, a multi-target pass
  still re-hit one actor 16 samples later. Do not interpret a large travel value
  as enemy separation.
- `run-20260809-065827` is the first swing-end-lifecycle live pass. At the
  unchanged 1.00 m Rearm Travel it blocked 155 duplicate callbacks, completed
  55 valid low-speed rearms, cleanly released all 564 Retail reference vectors,
  and exercised seven multi-target object sets. Automatic watcher replay found
  two same-object acceptances without an intervening reset, tracing a silent
  clear to transient invalid sweep handling. This is partial evidence, not an
  accepted lifecycle run.
- `run-20260809-095447` is the accepted corrected Pipe confirmation. Hit Speed
  was 7.25 m/s and Rearm Travel was 0.12 m. Six contacts were accepted and
  native-forwarded, 34 same-swing callbacks were blocked, six rearms completed
  at `release_samples=3/3` no faster than 1.741 m/s, and all 60 Retail reference
  vectors were released cleanly. `SameTargetAcceptedBeforeRearm` was zero.
  Three invalid-frame callbacks occurred after a prior rearm, so
  `InvalidSampleLatchHolds` remained zero; retain the direct automated
  transient-sample regression as that branch's current evidence.
- Use the Pipe values and invariant counts above as the regression baseline for
  every additional one-handed weapon. A new profile does not pass merely
  because it can damage: it must also block follow-through duplicates, rearm at
  `3/3`, and release every Retail reference cleanly.
- Preserve this geometry and speed baseline during later tuning. Require Swing
  off must retain overlap-only behavior; with it on, contacts below Hit Speed
  must remain `swing_not_qualified`, while faster overlaps produce one visible
  hit.
- Treat impact energy as evidence only. The first configurable gate is based on
  the Live Speed / Fast Enough result displayed in the Melee tab.
- Let an enemy strike the player: enemy damage must remain unaffected.
- Switch away from Retail index 32, open a menu, or remove headset focus:
  local physical contact damage must fail closed.

The detailed design, current fire-axe values and in-headset calibration
controls are in [`CONDEMNED-M5.md`](CONDEMNED-M5.md).


## Player locomotion collider / enemy-approach live gate

Evidence status: run `run-20260819-133507` used x86 loader SHA-256
`D780B2DB6BC9C8A79824B4E24E0DF8E5C21E75A0F7234638B987CD954BEF8A3C`
from base commit `9732e0a867ffc3b80bfe909be6411a68367c457e` plus the
recorded dirty working tree. The preserved loader log SHA-256 is
`69B71F6F38D097DF1595BACA4C099A983EA431D6F9D9C7344E685ED5AF4BC984`.

The run **live verified the attempted-slot-9/post-pending route and the
Retail-update boundary audit**:

- the probe armed with both event caps and loaded 10%;
- a successful reapply and forced readback established `(4,95,4)` on the
  exact local player;
- the next changed boundary was `phase=post_retail_update` at
  `(40,95,40)`, `pending=0`, and `drift=1`, first with no stick input;
- a controlled 15%-to-10% reapply again established `(4,95,4)`, and the
  first `directions=0x5` stick edge after menu close was immediately followed
  by the same post-Retail restoration;
- two accepted contacts read the player at full width and enemy dimensions
  read-only with `mutation=none`; and
- the final saved 100% setting reported requested/actual `(40,95,40)`,
  `pending=0`, and `drift=0`, followed by clean game and host shutdown.

This locates restoration inside a Retail client update but does not identify
its exact writer or show which dimensions movement collision consumed.
`post_pending_noop`, the native-result-unknown handoff route, cap exhaustion,
the reset-row action, menu warning presentation, and subjective
proximity/performance remain unvalidated. The tester supplied no subjective
result for this run.

The exact setter extension is **automated-only**. It observes the exact
verified `Condemned.exe+0x00007FD0`
slot-9 target, forwards native arguments/results unchanged, and bounds
read-only telemetry to the fresh exact local player while Playing and reduced
or pending. Its portable return-RVA and forwarding tests and normal headset-
free gate pass. No staged hash, arm event, native request/result observation,
or performance evidence exists for this observer. The prior boundary evidence
remains live; the exact restoration writer remains unknown.

Use the following procedure as a regression gate for the boundary diagnostic:

1. Stage and launch the built candidate only after the tester requests each
   action. Preserve the exact source state, staged loader hash, launch report,
   host/bridge logs, and loader log.
2. Launch a fresh supported headset run. Require
   `m5_player_collider_armed`, `m5_player_collider_drift_probe_armed`,
   `m5_player_collider_writer_trace_armed`, and a successful settings-load
   event. Both collider arm records must report
   `fail_closed_operational_gate=1`. The probe arm records must report
   `boundary_event_cap=128 post_pending_event_cap=32` and writer budgets
   `known_event_cap=64 unknown_gameorig_event_cap=64`,
   `executable_local_event_cap=64 external_local_event_cap=32`, and
   `unresolved_local_event_cap=32`. The local profile has a latest preserved
   save of 100%; record that load before
   changing it.
3. In clear open space, select `RESET TO RETAIL WIDTH`. Require requested and
   actual `(40,95,40)`, `succeeded=1`, and preserved height. Then reduce to
   10% and require requested/actual `(4,95,4)`, preserved Y,
   `enemy_objects_changed=0`, and exactly classified evidence through one of
   these routes:

   - `m5_player_collider_native_handoff` with
     `native_call_attempted=1 native_result_valid=0 succeeded=1`, followed by
     a `stream=boundary phase=post_retail_update` observation at `(4,95,4)`;
   - `m5_player_collider_reapply_attempted` with
     `native_call_attempted=1 native_result_valid=1 native_result=0x00000000`,
     followed by `stream=post_pending phase=post_mod_setdims_attempt` at
     `(4,95,4)`; or
   - `m5_player_collider_reapply_not_needed` with
     `native_call_attempted=0 native_result_valid=0 succeeded=1`, followed by
     `stream=post_pending phase=post_pending_noop` at `(4,95,4)`.

4. Close VR Tools and make one straight forward stick approach to a live
   enemy. This candidate is diagnostic; do not expect it to force dimensions
   after a native restoration. Note any frame-time or headset-performance
   regression because the probe queries dimensions twice per update while the
   reduced setting is active, plus once after each processed pending request;
   each in-budget exact-local setter observation also makes bounded before/
   after dimension queries.
5. Use the accepted observation from step 3 to establish `(4,95,4)`, then
   require the first later `stream=boundary` change at
   `phase=pre_retail_update` or
   `phase=post_retail_update`. It must report the exact player HOBJECT,
   locomotion direction mask, current scale, actual dimensions, manager
   `+0x1C`, the manager-`+0x40C` source candidate, and the separate adjacent
   candidate at `+0x418`. A post-Retail change is visible after Retail and
   before pending processing but does not identify the writer; a pre-Retail
   change occurred since the preceding post sample. Do not use absent drift to
   exclude a write-and-restore inside one update. Boundary samples emit the
   initial value and changes; `stream=post_pending` samples are forced and
   must use `post_mod_setdims_attempt` or `post_pending_noop` consistently
   with their matching event. Every observation record must say
   `mutation=none`.
6. If practical, make one accepted physical strike. Require
   `m5_enemy_collider_observed` with valid player/target dimensions and
   `mutation=none`; treat its contact-point distance separately from
   nearest-body or object-centre separation.
7. Move clear of obstacles and reset to 100% before exit. If
   `native_result_valid=1 native_result=0x00000001` leaves smaller actual
   dimensions in place, require the pending retry to reach `(40,95,40)` once
   unobstructed.
8. Validate the slot-9 observer before any write-enabled retention candidate.
   After an accepted `(4,95,4)` readback, require completed
   `m5_player_collider_setdims_observed` records to carry sequence/thread,
   exact manager/player/physics, raw caller address, caller module and return-
   RVA validity, a verified call RVA only for known mappings, request input/
   output readability and values, raw slot-9 `flags`, native result, context
   stability, actual before/after, locomotion mask, and read-only manager
   candidates. Every record must say
   `native_setdims_executed=1`,
   `request_pointer_forwarded_unchanged=1`,
   `flags_forwarded_unchanged=1`,
   `native_result_preserved=1`,
   `observer_added_engine_state_writes=0`, and
   `observer_setdims_calls_added=0`.

   Correlate the record by sequence/thread and update timing with the boundary
   drift. A verified GameOrig call that bridges `(4,95,4)` to `(40,95,40)`
   identifies that native handoff as the restoration. The raw slot-9 flags do
   not prove the outer manager flag `0x20`. An unclassified, other-module, or
   unresolved record requires surrounding-code/module inspection before
   attribution.
   Boundary restoration without a qualifying event means only that it was not
   observed through `Condemned.exe+0x00007FD0` when all relevant caps
   remained below their limits. Do not shrink or de-solidify enemies, and do
   not use an
   unconditional post-update `SETDIMS_PUSHOBJECTS` loop as the corrective
   gate.

9. Enable the session-only `COLLISION X-RAY`. Require
   `m5_player_collision_xray_armed` to name verified slot 11
   `Condemned+0x00007CD0`, exact-local/Playing/foreground gates,
   `native_call_count=1`, `enemy_mutation=0`, and
   `true_physics_geometry_verified=0`. The menu and render log must call every
   box a **diagnostic proxy**. No X-ray state may be written to settings.
10. Make one accepted actor contact, then repeat one straight approach within
    the two-second target-freshness window. Correlate bounded
    `m5_player_collision_xray_velocity_handoff` and
    `m5_player_collision_xray_update` records. Require requested velocity,
    exact player/physics identity, pre/post player origin and actual
    dimensions, native result, locomotion mask, fresh HMD origin, fresh target
    origin/dimensions/contact when available, player-to-target origin distance,
    proxy horizontal gap, and HMD-to-player horizontal origin offset. Treat
    neither proxy gap nor HMD/contact distance as a verified radius or nearest-
    body separation.
11. Interpret only the bounded alternatives: full dimensions before the
    velocity/update sample support restoration before movement; retained
    reduced dimensions plus a stopped player with nonzero velocity and a
    positive proxy gap support another movement volume/rule; target dimensions
    or contact/proximity correlation support target geometry/push behavior;
    stable HMD-to-player origin offset explains a misleading headset-based
    measurement. These are diagnostic discriminators, not live facts until
    observed.
12. Success requires stable identity, exact-once native forwarding, finite
    fresh records, no enemy/object mutation, and no material performance or
    locomotion regression. Reject on any gate violation, ambiguous proxy
    labeling, unexplained target reuse, or cost. Roll back by toggling X-ray
    OFF; because it is session-only and read-only, no settings or engine-state
    restoration is required.

## Phase-1 magazine insertion authoring live gate

Evidence status: **implemented and headset-free tested, awaiting live
validation**. The slice is mod-owned visual authoring only. No test result,
persisted socket, or convincing ghost proves a Retail magazine interaction.

1. Build and stage the exact source state, then launch the smallest canonical
   headset configuration with `-WeaponGripCalibration`. Manually equip a
   positively identified held firearm; do not add a launcher preset, model
   offset, bone, or socket assumption to force the representative.
2. Open VR Tools and select `AUTHOR`. Require
   `m5_live_magazine_socket_armed` for the exact game PID, stable Retail
   index, resolved catalog name, and current live source generation. Unarmed,
   unknown identity, a missing visible model, or stale off-hand tracking must
   remain unavailable.
3. With no saved record, require `NOT CONFIGURED` and no socket wireframe.
   Place the left grip at the intended seated pose and activate capture. Verify
   the model-local box, RGB axes, constrained rail, raw/snapped ghost, and
   centimetre/degree readouts in both eyes. The controller and held weapon must
   not move because of the authoring geometry.
4. Exercise Fine (0.1 cm / 0.25 degree) and Coarse (1 cm / 5 degree)
   component edits, all position/rotation/half-extent/rail/tolerance
   components, one undo, and reset to the loaded baseline. A failed or
   out-of-range edit must leave the prior runtime and persisted value
   authoritative.
5. During menu navigation, capture, snapping, undo, reset, and close, require
   no Retail Fire, Activate, reload, block, inventory, ammo, animation, or
   weapon-action edge, including no delayed edge after the menu closes. Every
   authoring event must report `phase=1 engine_handoff=none ammo_mutation=0
   weapon_state_mutation=0 retail_state_mutation=0`.
6. Drop/reacquire the weapon and start a fresh process. The record may reload
   only for the exact same stable index and case-sensitive catalog name; a
   malformed player record or name mismatch must fail closed without using a
   packaged or another-weapon fallback.
7. Use `tools/set-condemned-magazine-socket.ps1` once with a valid edit, then
   deliberately exercise stale-base, wrong-PID/index/name, and invalid-value
   commands. Require the matching `m5_live_magazine_socket_applied` or
   `_rejected` revision and verify the INI/runtime value changes only after a
   successful save.
8. Regress Grip, 2-Hand, Collider, Hand IK, firearm aim/fire, tool-menu input,
   keyboard/mouse, host-absent fallback, exact model restoration, both-eye
   overlay presentation, and frame timing. Preserve the launch report,
   host/bridge/loader logs, source state, staged hashes, exact weapon identity,
   before/after player INI, expected observation, actual observation, and
   rollback.

## Colt equipped-model slide discovery live gate

Evidence status: **implemented and automated-tested, awaiting live
validation**. This
is an observation-only model-node diagnostic. It neither proves that the slide
is a node nor authorizes a node control or weapon-state handoff.

1. Build and stage the exact source state. Launch the smallest canonical
   index-76 firearm configuration with `-StereoTuning`,
   `-PhysicalMeleeVisualProxy`, and `-WeaponModelDiscovery`. Preserve the
   launch report and all ordinary logs.

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\launch-condemned-m2-vr.ps1 `
       -StereoTuning -TurningProbe -CoreActionsProbe -HeadAimProbe `
       -AimPathProbe -PhysicalMeleeProbe -PhysicalMeleeWallProxy `
       -PhysicalMeleeVisualProxy -WeaponModelDiscovery -DesktopWindow -Wait
   ```
2. Equip positively identified `colt45_Unbreakable` index 76, finish the equip
   animation, and hold the weapon/controller still. Require one
   `weapon_model_discovery_model`, a complete node list, and
   `weapon_model_discovery_baseline_ready` for the same nonzero model pointer
   and source generation. Do not fire before the baseline-ready event.
3. Fire exactly once, pause, then perform one ordinary Retail reload. Preserve
   every `weapon_model_discovery_motion` record. Each must state model-local
   basis, read-only operation, no node controls, and no engine writes.
4. Identify candidates by repeatable translation peaks, not by a presumed
   `Slide`, `Breach`, or `Bolt` name. Compare node parentage so a moving child
   subtree is not mistaken for several independent objects. Require finite
   closed/current positions, displacement, normalized candidate axis, travel,
   and rotation.
5. Repeat one fire/reload cycle after drop/reacquire. A candidate is useful
   only when its semantic node/name or hierarchy role and motion axis/travel
   repeat for the new lifetime-valid model generation. Pointer or raw handle
   values are process-local evidence and must never be persisted.
6. Reject the node hypothesis if no stable model-local candidate moves, motion
   is only whole-animation recoil, the baseline was captured during an equip
   transition, reads fault, the source identity changes mid-sample, or frame
   timing materially regresses. The next diagnostic is bounded
   attachment/model-piece discovery; do not infer an object offset.
7. Roll back by omitting `-WeaponModelDiscovery`. Verify ordinary Colt aim,
   fire, reload, visible-model restoration, hand IK, controller input,
   keyboard/mouse, host-absent fallback, and frame pacing remain unchanged.

The first model lifetime in
`stage\condemned-m2-mono\logs\run-20260820-082601\condemnedvr-loader.log`
has now satisfied the discovery portion for index 76: `SlideJnt`, parent
`anim_cult45`, translated from `(14.1689, 2.8062, -8.7261)` to
`(10.3449, 2.8362, -8.1651)` along
`(-0.989379, 0.007748, 0.145151)` for 3.8651 units with unchanged rotation.
A new model lifetime must still repeat name resolution; handle 3 and the
observed model pointer are not reusable evidence.

## Authored Colt slide-grab and node-control live gate

Evidence status: **core interaction live accepted; remaining safety/regression
observations open**. Run `run-20260820-092549` proved exact Colt capture/save,
per-generation `SlideJnt` name resolution, inside-volume Candidate/Attached,
position-only rail control, the 3.8651-unit clamp, callback removal, and Retail-
ownership restoration. The user confirmed attachment and slide motion but
rejected the attached hand pose. Corrected run `run-20260820-124747` then
produced 16 successful attachments and exactly 16 installed-callback detaches,
no AUTHOR overlay failure or source-only detach spam, repeated 3.8651-unit
clamps, and fresh `SlideJnt` resolution for source generations 1 and 5. The
user accepted the saved pose and slide interaction as correct. Absence of an
unintended Retail action, explicit focus-loss cancellation, and ordinary post-
release fire/reload animation remain open because they were not explicitly
observed. The later project-owned pull/return sound adapter is automated-only
and was not present in that run. Sound run `run-20260820-133700` proved asset
availability, Windows handoff, one-shot counts, already-closed suppression,
and callback restoration, but the user rejected its pull timing: requests
occurred at 0.1009-0.4762 units rather than near the rear endpoint. The
corrected build uses the authored 3.50-unit rear threshold. Corrected run
`run-20260820-134612` then produced
12/12 pull handoffs between 3.5050 and 3.8651 units with none below threshold,
11 displaced-release return handoffs, and one already-closed stop/no-return;
all 24 Windows handoffs and all 12 callback removals succeeded. The user
explicitly accepted that pull/return timing and content. The subsequent repeat-
pull extension, which rearms after forward motion while grip remains held, is
automated-tested and staged. Repeat-cycle run `run-20260820-135900` completed
the process-side live gate: 11 attachments produced 24 pull handoffs, including
two uninterrupted `pull_cycle=1..6` sequences; every cue occurred at
3.5123-3.8651 units against the 3.5000 threshold. Nine displaced releases
returned after ownership restoration, two closed releases produced stop/no-
return, and all 11 callbacks restored Retail ownership with no failed handoff.
The user explicitly judged the repeated audibility, timing, and absence of
unwanted duplicates perfect. This completes the repeat-cycle live gate for the
tested Colt path.

Live `run-20260820-091557` reached AUTHOR but the panel failed closed at
32,766/32,768 triangle vertices before slide capture. The compacted AUTHOR rows
have both worst-case automated coverage and a successful live visual recheck
in `run-20260820-092549`. Corrected run `run-20260820-124747` retained the
visible panel, accepted attached hand pose, and bounded detach records one-for-
one with installed callbacks. Continue with the still-open safety observations
in steps 4, 6, 7, and 8 rather than repeating the accepted presentation gate.

1. Build and stage the exact source state. Author and explicitly save a finite
   index-76 `SLIDE GRAB RAIL` volume and hand pose in VR Tools. Preserve the
   player settings file, exact staged binary hashes, and the complete authoring
   log. Then run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\launch-condemned-m2-vr.ps1 `
       -StereoTuning -TurningProbe -MenuProbe -CoreActionsProbe -HeadAimProbe `
       -AimPathProbe -PhysicalMeleeProbe -PhysicalMeleeWallProxy `
       -PhysicalMeleeVisualProxy -WeaponGripCalibration -ArmIkRightArm `
       -SlideControlTest -DesktopWindow -Wait
   ```
2. Equip exact index 76 / `colt45_Unbreakable`. Require
   `m5_slide_node_control_armed`, then one successful
   `m5_slide_node_resolved` for `SlideJnt` and the current nonzero source
   generation. Confirm the authoring display shows CONFIGURED, the oriented
   box is on the physical slide contact region rather than merely at the node
   pivot, and the closed/rear rail gizmo matches the weapon.
3. Move the fresh tracked left controller outside and through every side of
   the authored box. Outside activation must not attach. Entry must produce
   Candidate only for an inside oriented-box result and log controller
   model-local position, exact identity/generation, controller selection,
   configured input, and overlap result.
4. From inside, press each non-configured input and require no attachment;
   press the configured GRIP, TRIGGER, or either input edge and require one
   Attached transition. The left hand must snap to the authored pose while the
   dominant weapon hand remains unchanged. Confirm that this edge produces no
   Retail gunshot, attack, block, reload, activate, or delayed action.
5. Pull forward, rearward, sideways, and beyond both endpoints. Require bounded
   before/requested/after node evidence: only the projection along
   `(-0.989379, 0.007748, 0.145151)` changes, rotation remains Retail-owned,
   sideways motion contributes no travel, and requested/clamped travel remains
   in `[0, 3.8651]` units. The authored hand target must translate with the
   same clamped slide displacement. For the sound-enabled build, attachment at
   zero and partial travel must be silent. Crossing the authored rear threshold
   (3.50 units for the Colt seed, near its 3.8651-unit maximum) must emit
   exactly one audible pull cue and one `m5_slide_grab_sound cue=pull
   pull_cycle=1` record. Holding or jittering within 0.25 units forward of the
   rear threshold must not retrigger it. Without releasing the configured
   input, move the slide farther forward than that hysteresis band and pull it
   back across the threshold; require exactly one second audible pull cue and
   `pull_cycle=2`. Repeat once more to prove each complete forward/rear cycle
   cues once while attachment persists. Require every pull record to have
   `travel >= rear_threshold`, with no cue during forward rearming motion.
6. Release the configured input at closed, middle, and rear positions. Require
   immediate Released/Idle transition, node-control removal, and
   `retail_ownership_restored=1`. After each release, ordinary Retail fire
   and reload must animate the slide normally and produce exactly the expected
   Retail action. A release while still displaced must emit exactly one audible
   return cue only after successful Retail-ownership restoration. A release at
   closed must emit no return cue. Require the relative asset, availability,
   handoff request/result, travel, and detach reason in
   `m5_slide_grab_sound`.
7. While attached, separately cause foreground focus loss, stale controller
   tracking/input, VR Tools opening, weapon drop, and weapon identity change.
   Each must detach immediately, neutralize the captured VR off-hand command,
   remove or disable the node override, and name the exact cancellation reason.
   Reacquire the Colt and require a new source generation and fresh
   `SlideJnt` name-resolution event rather than reuse of a handle. These safety
   cancellations must not emit a return cue; if a pull cue was active, require
   an `action=stop` sound event.
8. Attempt an ordinary Retail fire/reload animation while attached only if it
   can be done safely without defeating input isolation. If incoming Retail
   node position deviates from the closed rail tolerance, require
   `RetailAnimationStarted`, no callback write for that sample, immediate
   detach/removal, and normal completion of the Retail animation. No ammo,
   chamber, reload, firing, durability, or weapon-state behavior may be
   synthesized by the mod.
9. Reject on a node-resolution/control failure, non-finite transform, incorrect
   identity/generation, write outside the rail, rotation change, unintended
   Retail action, missing detach, stale-handle reuse, failure to restore Retail
   animation, material performance regression, or misleading per-frame log
   volume. Preserve launch report, host/bridge/loader logs, source state,
   settings, weapon/model generation, expected/actual observation, and
   rollback.
   Also reject the sound slice on missing/inaudible audio, duplicate cues,
   pull sound before meaningful motion, return sound at closed, return sound on
   a safety cancellation, incorrect output device, or a cue that masks or
   delays Retail weapon audio.
10. Roll back by omitting `-SlideControlTest`. The authoring record may remain
    saved, but no Colt node control, off-hand Retail-input suppression, or
    slide-hand override may arm. Regress magazine authoring, Grip, 2-Hand,
    Collider, Hand IK, firearm aim/fire/reload, tool-menu input,
    keyboard/mouse, host-absent fallback, model restoration, both-eye overlay,
    and frame timing.

## Evidence collection

Each launch creates a session-specific log directory. Preserve, at minimum:

- executable and stock-client identity results;
- enabled/disabled feature gates and rollback switches;
- OpenXR runtime name, refresh rate and session transitions;
- game FPS, XR submit FPS, image reuse, image/request age and queue/slot drops;
- left/right render, bridge transfer and host copy timing;
- hook/signature failures, tracking loss and focus transitions;
- physical-melee profile identity, pose validity, sweep speed and accepted
  impact reason; and
- the final weapon diagnostics snapshot and compact event stream.

Use `tools/collect-condemned-performance.ps1` for a completed run or
`tools/watch-condemned-performance.ps1` during a performance-probe launch.
Logs may contain local paths and runtime diagnostics; inspect them before
sharing publicly.

## Acceptance history

Milestone-specific live evidence is retained in `CONDEMNED-M0.md` through
`CONDEMNED-M5.md`. Frame-pacing and HID performance evidence is in
[`CONDEMNED-PERFORMANCE.md`](CONDEMNED-PERFORMANCE.md). Those records explain
what was actually observed; they are not substitutes for running the current
automated suite after a code change.
