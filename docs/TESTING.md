# Test and verification plan

## Automated gate

Prepare the pinned dependencies, build both architectures and run all tests:

```powershell
powershell -ExecutionPolicy Bypass -File tools\prepare-dependencies.ps1
powershell -ExecutionPolicy Bypass -File tools\build-all.ps1
```

The x86 build additionally tests module identity, loader fail-closed behavior
and bridge product guards. Both architectures run the shared protocol, pose,
stereo, input, render-scale, weapon-weight, physical-melee, tool-menu and
background-render tests.

Before a public push or package, also run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\audit-publication.ps1 -RequireClean
```

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
| Focus | Alt-Tab releases desktop mouse confinement while headset rendering continues in window mode |
| Haptics | Only confirmed actions pulse; amplitude/duration remain bounded |
| Device reset | Resolution/window changes and return from desktop do not lose the bridge or session |
| Save lifecycle | Load, death/respawn and level transition invalidate stale object pointers safely |
| Exit | Game and host terminate without a hang or persistent system-wide runtime change |

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


Equip the intended mapped one-handed weapon, then open the Debug tab's Melee
view. Confirm the exact Retail index/profile and `CONTACT DAMAGE ON`.
`Unarmed`, ordinary firearm states, two-handers, and unknown indices must
remain excluded. Confirm the wireframe follows the held weapon: amber means it
is still waiting for the first Retail collision seed, while green means the
collision body is live.
The cross at the tip is the exact proxy origin. The overlay is intentionally
visible through geometry.

The first two selectable rows in `DEBUG` are visibility-only developer
controls:

- `DRAW MELEE COLLIDER` hides or restores the amber/green capsule.
- `DRAW CONTROLLERS` hides or restores the controller/grip calibration
  wireframes when a calibration tab is active.

Both default to `ON` for each process and are intentionally session-only.
Turning either row off must not disable the native collider, contact damage,
controller input, grip/IK calibration, or per-weapon settings. For headset
acceptance, hide each overlay independently, confirm the other remains
unchanged, then restore both and land one physical hit with the collider hidden.

Live baseline `run-20260809-124936` completed that checklist. The tester
confirmed the draw controls work in headset; the ordered loader log records
independent `1/1 -> 0/1 -> 0/0 -> 0/1 -> 1/1` transitions. A later collider-off
pass still produced 32 accepted/native-forwarded damage dispatches, 28 rearms,
three multi-target swings, 532/532 clean Retail reference-vector releases, and
zero reference failures.

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

If the collider is amber, make one deliberate Retail attack to create its
native body. Automatic pickup seeding is recorded as deferred work.

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
   values. The menu path auto-saves; `SAVE GRIP SNAPSHOT` forces another save.
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
