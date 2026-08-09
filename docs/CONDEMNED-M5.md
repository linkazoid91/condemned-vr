# Condemned M5 physical-combat evidence and design

M5 starts from the live-accepted M3 stereo/head-tracking path and M4
controller path. All version-bound observations below apply only to the
verified Steam `1.0.314.0` client in the isolated no-ASI stage.

## Target melee experience

Melee is controller-driven physical combat, in the style of *The Walking
Dead: Saints & Sinners*. A melee weapon is not an animation that happens to
point toward a controller. It is a separate physical item whose target pose
comes from the weapon hand, whose collision-constrained pose is rendered, and
whose damage depends on actual contact velocity.

The intended path is:

1. Convert the fresh OpenXR grip/aim pose into the verified LithTech world
   basis.
2. Pull a weapon body toward that target pose with a bounded virtual coupling
   so weapon weight and world resistance can produce visible lag.
3. Sweep the weapon volume between completed simulation poses. Never infer a
   hit from a single point sample or from a Retail attack-animation timer.
4. Use contact-point velocity, weapon mass, and impact energy to qualify and
   scale a strike.
5. Feed an accepted contact into Condemned's native impact/damage path so
   material effects, sound, AI reaction, weapon durability, and damage rules
   remain game-owned.
6. Derive haptics from the accepted contact impulse and render the weapon from
   the collision-constrained pose rather than directly from the controller.

Melee must ultimately require no Fire-button edge. Firearms and the taser keep
the simpler right-controller ray/basis path.

## Verified Retail handoff

The Retail melee controller enables collision records at
`GameOrig+0x0001FD00`, updates their physics bodies at
`GameOrig+0x0001FC00`, and builds each body transform through
`GameOrig+0x0000F690`. Its registered collision callback begins at
`GameOrig+0x0001F830`. That callback resolves the other body, contact point,
contact normal, and surface information before making the only direct call to
the impact dispatcher at `GameOrig+0x0001F270`.

The guarded `-AimPathProbe` hook at the dispatcher is pass-through. A live
wall test confirmed finite contact positions and normals alongside fresh
right-controller poses. Redirecting the animation-derived body transform did
change downstream contact data, but the tester still saw Retail's centred
weapon animation. This separates two responsibilities that the physical path
must treat independently: collision authority and visible-weapon pose.

The animation-relative transform experiment is diagnostic scaffolding, not
the final melee implementation.

## Incremental gates

1. **World-pose and kinematics:** publish a coherent controller world pose;
   compute bounded linear/angular motion and weapon-endpoint sweeps with tests.
2. **Wall-only proxy:** drive a verified melee collision proxy from the
   controller pose while all native actor damage remains disabled. Confirm
   contact position, normal, continuity, and tracking-loss fallback.
3. **Visible diagnostic proxy:** continuously observe the verified local
   `CClientWeaponMgr` current-weapon pointer and the primary model's
   engine-owned `LTObjRef`. During both stereo-eye renders, place the model's
   profile-defined local grip frame on the OpenXR right-hand grip position and
   use the right-hand aim rotation as its weapon axis. Restore the exact Retail
   object transform immediately after rendering. Acquisition happens on the
   first gameplay update after equip/load and does not require an attack;
   weapon switches invalidate the old two-level reference automatically. This
   is a test aid, not final weapon physics. The optional live calibration mode
   adjusts that local grip frame in the next stereo frame and retains separate
   values for each equipped weapon/model observed during the run.
4. **Physical contact qualification:** accept impacts from swept volume and
   contact velocity, with per-contact de-duplication and recovery after a
   weapon separates from a surface.
5. **Pipe native damage handoff:** keep the verified collision body active
   after Retail's first seed and pass only speed- and energy-qualified contacts
   through its dispatcher. Retail preserves target, weapon, material, and
   difficulty data; the first guarded slice is pipe-only.
6. **Permanent visible decoupling:** replace the render-only diagnostic with
   a lifetime-safe standalone held item at the collision-constrained physical
   pose. Add configurable grip offsets and weapon geometry profiles.
7. **Resistance and handling:** add virtual-coupling lag, mass profiles,
   two-handed leverage, blocking, impacts against weapons, and bounded haptic
   impulse.

Each gate fails closed on stale tracking, a non-finite pose, excessive sample
time or travel, an unknown weapon profile, a byte-signature mismatch, or loss
of the verified Retail object. World contacts are validated before actor
damage is enabled.

The diagnostic visible proxy deliberately reuses the equipped model instead
of guessing a model filename or creating an untracked duplicate. Static
signatures verify `CClientWeaponMgr::GetCurrentWeapon` at
`GameOrig+0x0002F910`, the manager singleton at `GameOrig+0x00168EBC`, its
current-weapon member at `+0x0C`, and the primary model `LTObjRef` HOBJECT at
`CClientWeapon+0x1C` as consumed by `SetWeaponTransform` at
`GameOrig+0x000255F0`. Both current-weapon and model references must still name
the published objects during every render. Menus and level transitions
invalidate them. The model-local grip-to-object calibration belongs to the
weapon profile; identity is the fallback for assets authored at the Retail
hand socket. The override exists only around the two verified world renders,
is read back, and must be restored exactly; any restore failure disables the
gate. Native impact dispatch remains blocked throughout this diagnostic.

Weapon-specific behavior is data, not another hook. `PhysicalMeleeProfile`
owns the local base/tip geometry, model-local primary grip, optional support
grip offset and grab volume, radius, world scale, mass, impact thresholds,
maximum sweep, contact separation, and sample bounds. A read-only Retail
database lookup now publishes the equipped record name and `AnimationProperty`
to the tool menu and loader log. `pipe_lever` is verified as player-weapon
index 32 with `WEAP_1HandedDebris`; it owns the first provisional one-hand pipe
record. Live catalog probe `run-20260809-110503` resolved 79 stable Retail
records and 27 `WEAP_1HandedDebris` entries. The explicit physical-weapon
allowlist contains 26 of them: indices
`0, 1, 5, 15, 16, 19, 26--36, 43, 44, 49, 50, 59, 62--65`.
Index 61 (`Unarmed`) is deliberately excluded because it has no weapon model.
Index 11 (`crowbar`) is the one extra weapon-specific pose admitted as
verified one-handed melee. This covers the 2x4 family, clothes rack, firearm
melee/broken-melee assets that Retail itself classifies as one-handed debris,
fireplace poker, mannequin arm, meat cleaver, paper cutter, Pipe/pipe variants,
rebar variants, subway handrail, and both Watcher sticks. Ordinary firearm
states, Fire Axe, Shovel, Sledgehammer, two-handed debris, and unknown indices
remain outside the gate.

Until an allowed weapon has its own saved value, its Melee/Weapon, Collider,
Grip, and right-hand IK records read the current `pipe_lever` record as a
temporary baseline. The fallback is read-only: the first edit/save writes only
that Retail index and shadows the corresponding Pipe value thereafter. A
valid per-index record always wins; malformed values still fail closed rather
than being hidden by the fallback.

## Live weapon-grip calibration

Add `-WeaponGripCalibration` to a launch that already enables
`-PhysicalMeleeVisualProxy`. This arms the Grip tab of the VR tool menu and only
accepts input while Condemned owns the foreground window. The older F11 mode
remains as a keyboard/controller fallback; in that mode, hold both controller
grip/squeeze buttons to capture the setup controls:

- Right stick X/Y adjusts the selected local X/Y axes.
- Left-stick up/down adjusts local Z.
- A selects position mode; B selects model-local XYZ rotation mode.
- X resets only the currently equipped weapon to its profile values.
- Y saves the active weapon's `grip` record and emits a position/quaternion
  snapshot as `m5_weapon_grip_calibration_snapshot` in the loader log.
- Left/right stick click selects a finer/coarser adjustment step.

While the Grip or 2-Hand tab is active, both eyes show always-visible generic
controller wireframes. They are stable setup references rather than models of
a particular controller. Magenta identifies the dominant right controller;
cyan identifies the left support controller. Red/green/blue show grip-local
axes and each aim ray remains separate from its physical grip pose. A support
target on the weapon is green while attached, amber while a free hand is
inside the grab volume, and red while it is too far away. This makes it
possible to distinguish model alignment, controller pose, and support-grip
configuration without removing the headset. F11 hides the wireframes when it
pauses the setup tool.

While both grips are held, the matching locomotion, turning, action, menu, and
recenter injections are suppressed. Releasing either grip returns them to
gameplay immediately. F11 pauses/resumes the setup tool entirely.

The non-numpad keyboard fallback is J/L for X, K/I for Y, U/O for Z, T to
toggle position/rotation, comma/period for finer/coarser steps, R to reset, and
P to save the active weapon. Existing numpad controls remain available when
present.

Both eyes use each updated value immediately. Switching weapons preserves each
stable Retail weapon index independently for the current game process. If the
same weapon is dropped, reacquired, or recreated during a level transition,
its process-local object/model pointers are refreshed while its alignment is
retained.

The session cache is backed by a versioned `grip` record in
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`. GRIP and 2-HAND menu
adjustments, captures, and resets save immediately. Continuous fallback
controller/keyboard adjustment does not write every tracking sample; controller
Y or keyboard P saves that current calibration explicitly. Position, local
rotation correction, support-grip enable/offset, and grab radius share the
record because both tabs edit one calibration slot.

Each record contains the stable Retail weapon index and resolved profile
identity. For an explicitly mapped one-handed weapon, a missing record or a
stale pre-mapping profile identity inherits the matching Pipe record; the
loader reports `source=pipe_baseline`. A malformed value, unsafe range,
excluded identity, or unavailable Pipe record still fails closed to authored
profile values. Those authored base values remain immutable in the live slot,
so RESET restores a known default and persists that reset rather than changing
the profile itself. Successful loads and saves are reported as
`m5_weapon_grip_settings_loaded` and
`m5_weapon_grip_settings_saved`; failures have an explicit
`m5_weapon_grip_settings_save_failed` event.

The first live persistence round trip used Pipe runs `run-20260809-103049`
and `run-20260809-103422`. The first process loaded the recovered
`{2.5, 2.5, -3.5} / {-28, 0, 0}` record, emitted `result=ok` for each
GRIP-menu save, and exited with position `{2.5, 5.0, -3.5}` on disk. The
second process loaded that exact changed value with
`source=local_app_data`. This live-verifies the engine-level
load/edit/save/relaunch path. The tester confirmed in-headset that the Pipe
returned where they left it and accepted Y 5.0 as the current alignment, closing
the perceptual persistence boundary.

## Pipe-first one-hand acceptance slice

One-handed handling is the canonical physical-weapon path. A support hand is
an optional profile capability layered on top of it, not a requirement of the
shared pose, weight, collision, swing, or ownership code. `pipe_lever` at
Retail weapon index 32 is the first acceptance weapon because its model grip
has already been measured and it exercises the majority path without support-
hand attachment state.

### Shared behavior contract for later melee weapons

The pipe is the acceptance reference for every mapped melee weapon, not a
one-off gameplay rule. Later one-handed weapons, and two-handed profiles after
their optional support-hand pose is resolved, must use the same dominant-hand
collision and damage state machine:

1. Resolve the stable Retail weapon identity and its own grip, capsule, mass,
   swing threshold, and optional support-grip profile.
2. Drive the configured capsule from the same weighted controller pose used by
   the visible weapon.
3. On Retail collision-body creation, replace the native local-Y capsule with
   that profile's configured base, tip, and radius; keep it active continuously
   rather than borrowing the Retail animation window.
4. Accept damage only when the native contact is within the configured capsule
   and the continuously sampled swing-speed qualification is true.
5. Dispatch at most once per target during one continuous fast swing and allow
   several different targets in that sweep. Configured tip travel is only the
   first rearm guard; require three consecutive samples below the lower release
   speed before clearing the target set. Keep target/material/difficulty/final
   damage ownership with Retail.

Weapon-specific code should therefore select data and eligibility, not create a
different hit algorithm. Firearms and non-melee tools are outside this contract.
The current implementation admits the explicit one-handed allowlist above to
native replacement and contact damage. Runtime eligibility requires both the
stable Retail index and its resolved profile identity; the native-capsule hook
also retains the local-player collision-owner check. `Unarmed`, normal
firearms, two-handers, enemy-owned instances, and unknown or mismatched
identities therefore remain fail-closed.

This broader mapping is not blanket live acceptance for every asset. Run
`run-20260809-113629` is the first non-`pipe_lever` proof. Retail index 29
(`Pipe`) resolved as `one_handed_debris`; all four settings loaders reported
`result=ok source=pipe_baseline`. Its collision constructor consumed the
configured 10-unit by 2.5-unit-radius capsule with `read_mask=0x7`.
Three actor-candidate contacts across two target pointers qualified at
8.630, 12.175, and 9.081 m/s, were inside the configured capsule, cleared their
Retail reference vectors, and produced `accepted=1 native_forwarded=1`.
This is live engine evidence for inherited settings, native capsule creation,
continuous contact, speed qualification, and native damage handoff on index
29. Headset-visible enemy reaction/damage remains a separate tester
confirmation at the time of this note. All other newly admitted
collision/damage paths are automated-tested but still require a bounded live
asset check; any weapon that takes a different Retail path must be removed or
held fail-closed.
The same session also selected index 0 (`2x4`, profile `plank`). Its existing
per-index Melee/Weapon record correctly won with `source=weapon_record`, while
Collider, Grip, and right-hand IK independently reported
`source=pipe_baseline`. That is live evidence that precedence is per record
family rather than all-or-nothing; editing one category does not freeze or copy
the other three.
The live-tested staged loader SHA-256 is
`00F120FA5A5F4C5F0B0609E9B20BF1371BA2FACC95891CF42B839DD2126B40A0`.
The full RelWithDebInfo gate passes 19/19 x86 and 15/15 x64 tests, including
catalog mapping, persistence fallback/isolation, profile mismatch migration,
and fail-closed exclusions.

After building and refreshing the M2 mono stage, start its guarded headset
configuration with:

```powershell
.\tools\launch-condemned-m2-vr.ps1 -WeaponTest Pipe -Wait
```

The retained `-WeaponTest Pipe` name selects the accepted Pipe baseline, but
the preset now exercises any mapped one-handed weapon. It expands the accepted
M4 controller gates plus physical-melee telemetry, wall and visual proxies,
grip calibration, full arm IK, recentering, desktop-window support, and
one-handed contact damage. It deliberately leaves `-TwoHandedMelee` off.
Confirm the Debug tab reports the intended Retail index/profile before judging
alignment, inertia, sweep speed, or collision behavior.

The damage gate reuses Retail's verified melee collision body, so Retail must
create that body once. If `CONTACT DAMAGE ON` is shown but `CALLBACKS` stays
at zero on contact, make one deliberate swing to seed it. Once seeded, the
hook keeps it active throughout fresh, focused gameplay; collision checks no
longer wait for the attack animation's collision window.

Automatic equip-time seeding is explicitly deferred. The intended later state
machine waits for stable weapon identity/model and a fresh pose, issues one
seed-only Retail pulse with damage blocked, and declares readiness only after
a player-owned body reports native override `read_mask=0x7`. Weapon changes,
death/load transitions, or failed verification reset and retry that state.
Calling `EnableCollisions` directly is not an accepted shortcut until its five
arguments and ownership contract are verified.

`-PhysicalMeleeColliderDebug` draws the configured swept capsule in both eyes
from the same fresh physical-melee frame. Amber is a preview while no fresh
player-owned Retail collision object exists; green means that object is live.
The cross marks the exact controller-tip origin supplied to the collision
transform. This developer overlay is intentionally visible through geometry.
The VR Tools `COLLIDER` tab edits controller-local position, pitch/yaw/roll,
length, radius, and forward/reverse direction with immediate preview. Values
are stored independently per stable Retail weapon index.

The `DEBUG` tab now begins with two independent, session-only visibility
toggles: `DRAW MELEE COLLIDER` and `DRAW CONTROLLERS`. Both default on. Their
gates exist only inside the two overlay-render functions; hiding the capsule
does not stop the collider snapshot or native contact path, and hiding the
controller wireframes does not pause grip calibration, controller input, IK,
or weapon pose publication. Menu-change diagnostics include
`collider_draw=0|1` and `controller_draw=0|1`. The full automated gate passes
19/19 x86 and 15/15 x64 tests with staged loader SHA-256
`FF89DA8555392B4312972637053124CB7E346BB435D6CB05F54315FA206FDE2D`.

Run `run-20260809-124936` live-accepts both controls. The headset tester
confirmed the overlays hide and restore correctly. The ordered menu log proves
independent `1/1 -> 0/1 -> 0/0 -> 0/1 -> 1/1` state transitions. During a
later pass the collider draw remained off while contact processing continued:
the first hidden-draw actor contact was accepted/native-forwarded at 12.139 m/s
with a -0.0117 m capsule-surface gap. The bounded run snapshot ended with 32
accepted/native-forwarded damage dispatches, 28 rearms, three multi-target
swings, 532/532 clean Retail reference-vector releases, and zero failures.

The 9 August working tree exposes actual physical-hit settings in the VR Tools
`MELEE` tab:

- `Require Swing`: on requires the continuously sampled weighted-weapon frame
  to be fast enough; off retains overlap-only diagnosis.
- `Hit Speed`: 0.25--10.00 m/s in 0.25 m/s steps; default 1.25 m/s.
- `Rearm Travel`: 0.02--1.00 m in 0.01 m steps; default 0.12 m. This is
  weighted-tip travel from the first accepted contact, not distance from the
  enemy.
- `Live Speed / Fast Enough`: read-only feedback from the exact physical frame
  used by contact qualification, not the Retail seed-gesture meter.
- `Hit State`: read-only `READY` or `LATCHED` feedback. While latched it
  shows whether travel is complete and the low-speed reset count from 0/3 to
  3/3.

A fixed per-pass set still accepts up to eight distinct targets and blocks
repeat damage during one active swing. Reaching Rearm Travel while still fast
does not clear that set. The release threshold is half Hit Speed, capped at
2.00 m/s and kept below the hit threshold; three consecutive samples at or
below it complete the reset. Impact energy is telemetry-only. Settings persist
per Retail weapon index with version-one migration. Automated checks pass 19/19
x86 and 15/15 x64. The speed gate and original menu are live-accepted; the new
swing-end reset is automated-tested and awaiting headset acceptance. VR Tools
blocks damage while open, so adjust values, observe the indicators, then close
the menu before testing a hit.

### Pipe contact lifecycle findings (2026-08-08)

- Flatscreen collisions were reaching the hook but were suppressed when the
  wall-proxy option was merely enabled without a fresh VR frame. The dispatcher
  now passes straight through to Retail unless the VR proxy is actually active;
  flatscreen enemy damage was confirmed working in-headset testing.
- A Retail attack seeds the reusable weapon collision body. It does not lend
  its animation damage window to later physical contacts. Amber means no body;
  green means the seeded body exists and continues receiving contacts.
- The first physical enemy overlap damaged correctly, but subsequent useful
  contacts stopped until opening the tool menu and reseeding. Logs showed every
  retained-target cleanup attempt failing with
  `retail_target_latch_released=0`.
- The collision callback is asynchronous relative to the collision-update
  hook, so its thread-local `activeCollisionRecord` is no longer available at
  callback time. The first controller-relative fix correctly recovered the
  persistent `+0x60`/`+0xC0` callback slots, but incorrectly treated each slot
  as one `LTObjRef`.
- Live run `run-20260807-162549` falsified that interpretation. One accepted
  callback reported a clear, then the same object produced two
  `contact_latched` callbacks 23--27 ms later whose cleanup raised access
  exceptions. The accepted callback was forwarded once and a 0.12 m rearm was
  observed, but no actor-candidate contact or second accepted dispatch occurred.
- Verified disassembly then established the missing container layer. The
  callback constructs a 16-byte `LTObjRef`, pushes it into the vector at
  collision-record `+0x48` (live controller slot `+0x60`), destroys the local
  copy, and passes the vector header to the dispatcher. The header fields at
  `+4/+8/+C` are begin/end/capacity. The apparent first clear had therefore
  invoked the element reset routine on the vector header and corrupted it;
  the two later exceptions were consequences of that corruption.
- The current implementation verifies the callback's vector setup, exact
  push-back call target, 16-byte element layout, native dispatcher call, and
  element reset routine. It accepts only bounded aligned begin/end/capacity
  spans, resets every live `LTObjRef`, then rewinds vector end to begin. This
  cleanup runs after accepted dispatches and rejected duplicates, while the
  mod-owned per-target latch remains the sole damage de-duplicator.
- The vector-span guards, per-target lifecycle, overlap-only path, and capsule
  distance math pass all automated checks: 19/19 x86 and 15/15 x64. The
  live-tested loader SHA-256 is
  `27CC43313B1E88188685BE5A0763B98C4DB6C9AAE418526531081C6CCFB38655`.
- Live run `run-20260808-060131` reached `repeated_contact_observed`: the first
  512 callbacks contained 42 accepted contacts, 40 native forwards, 470
  blocked held-overlap duplicates, 510 released references, repeated 0.12 m
  rearms, and zero vector failures. This validates the corrected container
  cleanup and continued callback lifecycle. It does not yet prove repeatable
  visible enemy damage because all captured targets were classified as
  world/prop.
- That run also live-validated the distance fields. Accepted contact gaps were
  0.1728--0.9229 m outside the configured pipe capsule; the latest captured
  point was 0.5834 m outside. This is strong evidence that the seeded Retail
  collision body's transform or shape does not match the configured/drawn pipe
  capsule. Preserve a deliberate enemy-contact sample before changing geometry.
- Held-overlap spam exhausted that run's 512-record logging cap. The current
  staged build keeps verbose records bounded but continues emitting compact
  diagnostics for every later accepted contact and any vector-cleanup failure.
  Its SHA-256 is
  `1F9FF7913B953E93E6AA1E6AAFB747022E2DC81938F5C3FEDC9B6410E53F0F45`.
- Live run `run-20260808-062240` verified that accepted contacts continue after
  the verbose cap. It captured seven targets and 1,035 successful reference
  clears with zero failures. Actor-candidate target `0x38CE54F0` produced five
  accepted/native-forwarded contacts, all with vector state `cleared`; their
  capsule gaps were 0.1403--0.8869 m. This proves that Retail's larger native
  shape reaches enemy contacts well before the configured capsule. The headset
  tester also confirmed that the pipe visibly hit and defeated enemies in this
  run, proving repeat damage after one seed. Later configured-capsule gating,
  rather than visual retuning, owns the distance correction.
- The subsequent live-alignment stage is SHA-256
  `35D79A9B96BE2018864197CE4CDD2550A4EC1A5764A79D4FB23593ABE8586E13`.
  It passes 19/19 x86 and 15/15 x64 tests, and the external sender produced the
  exact bounded command and matched its applied revision in disposable runtime
  fixtures. Launch attempts `run-20260808-065304` and `run-20260808-065448`
  timed out safely while VDXR was not visible. With Virtual Desktop active,
  `run-20260808-072314` reached focused gameplay and selected pipe index 32 at
  persisted collider values `(0,12,10)`, rotation `(-50,0,0)`, length 40,
  radius 4.5, forward. External revision `639217706402833475` changed local X
  to 5 and the game acknowledged those exact values without a restart. The
  Retail seed briefly dropped immediately before the save, then the same
  collision object `0x38FBFF68` returned green; event order therefore does not
  implicate the command. Contacts continued afterward, reaching 580 callbacks,
  106 accepted/native-forwarded contacts, 106 rearms, three targets and zero
  reference failures. This live-accepts the command bridge.
- The later revision `639217707924647700` reverted the temporary probe. The
  headset tester confirmed the configured wireframe itself was already correct,
  so visual alignment was ruled out as the distance fault.
- Verified Retail disassembly explains the positive gaps. Collision creation at
  `GameOrig+0x0001FE30` reads `CollisionRadiusScale`, `Radius`,
  `LengthDown`, and `LengthUp` and constructs a native primitive. Update
  code at `GameOrig+0x0001FC00` only applies a transform to that existing
  object. The VR transform hook moves the native database-sized shape to the
  weapon endpoint, while the green wireframe is separately derived from the
  configured base, tip, and radius. The drawing was never the physics shape.
- The configured-capsule gate now measures the callback's verified target
  surface point and rejects gaps above 0.01 m before per-target de-duplication.
  Invalid points fail closed, and rejected callbacks still release Retail's
  reference vector. Automated tests prove that a far callback does not latch the
  target and that the same target is accepted on a later real overlap. The
  tested/staged loader SHA-256 is
  `5CF2BCEADEDA6F236BC6BB28A9389B8658FA029C9E2D34D53DF363956C5E75CE`;
  19/19 x86 and 15/15 x64 tests pass.
- Live run `run-20260808-074159` immediately rejected its first 512 distant
  wall callbacks as `outside_configured_collider`, with zero accepts/forwards,
  512 clean vector clears, and 0.575--0.839 m gaps. The headset tester then
  moved the drawn collider through enemies. A 40-second poll produced no later
  callback; the count stayed frozen at 512. This proves callback gating cannot
  recover true overlap after Retail's oversized body makes its single early
  notification.
- The native-body replacement is now live-accepted. During local-player pipe
  creation only, a guarded database reader supplies `LengthUp=0`,
  `LengthDown=distance(base,tip)`, and the configured radius. The body transform
  maps Retail's local +Y capsule from configured base to tip. Exact constructor
  callsites, property addresses, runtime vtable slot `0x80`, and the reader's
  50-byte body are verified before hooking; every other read retains its Retail
  value.
- Staged loader SHA-256
  `9EE61E2E817F69ED96F59B493A07F83C793E5F5050C88D31CC90534BD44935FD`
  passes 19/19 x86 and 15/15 x64 checks. In `run-20260808-082609`, the seed
  reported `read_mask=0x7`, length 10.0 units, and radius 2.5 units. The
  confirmation snapshot contained 574 callbacks, 126 accepted/forwarded
  contacts, 338 duplicates, 126 rearms, seven targets, and 574 clean reference
  clears with zero failures. Two actor candidates produced ten actor-classified
  accepted forwards; their maximum accepted surface gap was 0.0095 m.
- The headset tester confirmed that the aligned pipe works against enemies.
  Native pipe alignment and visible overlap-only damage are therefore live.
- Live run `run-20260808-165819` accepted the configurable physical-hit menu
  and speed gate. The final pipe record used Require Swing on, Hit Speed
  7.25 m/s, and Rearm Distance 0.12 m. Its 522 compact contact records split
  cleanly at the threshold: 446 `swing_not_qualified` samples topped out at
  7.189 m/s, while 16 accepted/native-forwarded damage dispatches began at
  7.256 m/s and reached 13.865 m/s. It also recorded 26 held-overlap
  duplicates, 34 outside-collider rejections, 16 rearms, four targets, 522
  clean reference-vector clears, and zero failures. The tester reported that
  speed works and the menu is good. Alternate Rearm Distance values were not
  separately feel-tuned in that run.

- Live run `run-20260809-035231` falsified distance-only rearming rather than
  finding a stable distance. At Hit Speed 7.25 m/s, 0.20 m produced
  double/triple same-target forwards; 0.30 m produced a double; and 0.35 m
  produced a triple plus a double. The first 0.40 m three-strike pass was clean,
  but a later hard follow-through re-hit the same target seven tracking samples
  after rearm. At the 1.00 m maximum, a multi-target pass still re-hit the same
  actor 16 samples later. The weighted tip can cover those distances during one
  8--10 m/s swing, so displacement alone cannot identify a new attack.
- The first replacement lifecycle kept accepted targets latched throughout
  continuous fast motion. Once Rearm Travel had been reached, three consecutive
  samples below the derived release speed were required before the target set
  cleared. Any renewed speed reset the partial 1/3 or 2/3 dwell.
- Live run `run-20260809-065827` exercised that build at Rearm Travel 1.00 m,
  not the requested 0.12 m. It recorded 564 callbacks, 66 accepted/native
  forwards, 155 blocked latched callbacks, 113 outside-collider callbacks, 230
  slow callbacks, 55 completed rearms, 12 object pointers, 564 clean Retail
  reference releases, and zero cleanup failures. Every rearm completed at
  `3/3`, no faster than 1.902 m/s against the 2.00 m/s release threshold. Seven
  completed swings accepted two or three distinct object pointers, proving the
  multi-target state path without requiring an intentional two-enemy sweep.
- Automatic replay also found two violations: the same object pointer was
  accepted twice before a `swing_completed` event. The object stayed constant
  while its target node changed. Source audit found that a transient invalid
  kinematic sweep silently called `ResetPhysicalMeleeContactState`; that was the
  only unlogged reset path between those callbacks.
- The corrected lifecycle fails closed on a transient invalid sweep. It keeps
  the target set latched, resets partial release dwell to zero, and emits
  `m5_physical_melee_contact_latch_held`. Explicit tracking loss and profile
  changes remain valid full resets. Automated regression now covers the 1.06 m
  fast follow-through, multiple targets, three-sample release, speed jitter,
  and an invalid sweep arriving during a 2/3 release dwell. Both x86 and x64
  suites pass.
- Live confirmation `run-20260809-095447` used Hit Speed 7.25 m/s and the
  intended Rearm Travel 0.12 m. Six contacts were accepted/native-forwarded,
  34 same-swing callbacks were blocked, all six rearms completed at `3/3` no
  faster than 1.741 m/s against the 2.00 m/s release threshold, and all 60
  Retail reference vectors were released without failure. The watcher reported
  zero `SameTargetAcceptedBeforeRearm`, so the corrected user-facing Pipe
  lifecycle passes live.
- Three invalid-frame callbacks in that confirmation occurred only after an
  earlier swing had already rearmed. `InvalidSampleLatchHolds` was therefore
  zero: the transient fail-closed branch remains regression-proven rather than
  naturally observed live. This caveat does not erase the clean repeated-swing
  acceptance, but it must remain explicit in later evidence.
- The local pipe record now persists Rearm Travel 0.12 m and Hit Speed
  7.25 m/s.

### Live weapon diagnostics

`-WeaponTest Pipe` now starts a hidden diagnostics watcher after the verified
run report is written. The game emits compact `m5_weapon_test_collider_state`
and `m5_weapon_test_contact` events; the watcher turns them into these files in
the session's `stage/condemned-m2-mono/logs/run-*` directory:

- `weapon-diagnostics-live.json`: atomic current-state snapshot intended for a
  developer or Codex to poll during headset testing.
- `weapon-diagnostics-events.jsonl`: concise chronological collider, contact,
  rearm and tracking stream.

Schema-v3 snapshots expose the current phase and recommendation, pipe identity,
seeded collision object, last target/node, actor-candidate classification,
accepted/duplicate/native-forward counts, Retail reference-vector clears and
released element counts, rearm count, invalid-sample latch holds, distinct
targets and the last 24 events. The watcher now computes
`SameTargetAcceptedBeforeRearm` and completed multi-target swing counts directly.
Any same-target reaccept adds a warning, makes the snapshot unhealthy, and sets
phase `same_target_reaccepted_before_rearm`. Rearm events identify
`reason=swing_completed` and carry actual/current and maximum tip displacement,
configured travel, current and release speed, and the completed release-sample
count. `contact_latch_held` events record transient invalid samples that were
prevented from clearing the target latch.
They also expose the per-run alignment command path, the last game-acknowledged
position/rotation/length/radius/direction and revision, plus any command
rejection and the applied/rejected counters.
Important phases include `awaiting_seed`, `ready_waiting_for_contact`,
`first_contact_observed`, `repeated_contact_observed`, `contacts_rejected`, and
`retail_reference_vector_failure`, plus the invariant-failure phase above.

Every compact contact also records the configured weapon capsule's distance to
Retail's target contact point: tip-to-contact, closest axis-to-contact, capsule
radius, capsule surface gap, and the closest-point fraction along the weapon
axis. A gap at or below zero means the reported target contact lies on/inside
the configured capsule; a positive gap measures the mismatch in metres. This
uses the collision callback's actual target surface point rather than a model
origin, which can be far from the struck body part. It also avoids making a new
engine transform call from the collision callback thread.

The watcher exits with the game and leaves a final snapshot. The loader source
is shared and reset by the next launch, so `-Run <run-name> -Once` can rebuild a
completed session only before another launch replaces that source. The derived
per-run snapshot and event stream are the durable records; do not claim a later
replay is archival evidence.
For a read-only human summary use
`tools/read-condemned-weapon-diagnostics.ps1`; pass `-Json` to return the
unchanged snapshot for Codex or another tool.

The Pipe preset gives only its child game process a unique
`weapon-alignment-command.txt` path. The loader polls that file at most 10 Hz
and accepts only a rigid versioned line with a newer revision, the exact current
process ID, active weapon index, finite settings and the existing Collider-tab
bounds. Malformed, stale, cross-process and wrong-weapon commands fail closed.

Codex or a developer can change the running game's collider from another
terminal while the tester keeps moving in headset:

```powershell
.\tools\set-condemned-weapon-collider.ps1 -DeltaPositionX 1
.\tools\set-condemned-weapon-collider.ps1 -RotationX -45 -Length 42
.\tools\set-condemned-weapon-collider.ps1 -Direction Reverse
```

Unspecified fields are based on the last game-acknowledged snapshot, so absolute
and delta edits compose safely. The tool writes atomically and waits for the
matching applied or rejected revision. Accepted values update the collision
proxy and wireframe on the next tracking sample and use the existing per-weapon
settings persistence; they do not require a restart or another Retail seed.

End-to-end validation on run `run-20260807-162549` confirmed that the launcher
started the hidden watcher and both diagnostic files updated while the game was
running. It first reported the fire axe at Retail index 17 with an amber body;
after equipping/seeding the pipe it recorded a green body, three callbacks for
one world/prop-classified target, one accepted/native-forwarded dispatch, two
blocked duplicates, and one 0.12 m rearm. The final two cleanup exceptions
directly exposed the vector-header bug described above. This proves the live
game-to-investigator channel and the old implementation's failure; the
corrected element-wise vector cleanup and new distance fields were then live
verified by `run-20260808-060131`. That second run produced repeated accepted
callbacks and zero vector failures, while its positive 0.1728--0.9229 m contact
gaps exposed Retail's larger database-sized native shape. The later
`run-20260808-062240` captured five clean actor-candidate forwards and the
headset tester confirmed visible pipe hits and enemy defeats. Repeat damage is
therefore live. Run `run-20260808-074159` proves the configured-capsule gate
blocks the old distant wall range, then proves the oversized native body emits
no later callback when the accepted wireframe enters an enemy. Run
`run-20260808-082609` closes that gap: the scoped native constructor override
reported `read_mask=0x7`, actor contacts stayed within 0.0095 m of the configured
surface, and the headset tester confirmed that the aligned pipe works against
enemies.


Retail continues to own target validity, material effects, difficulty rules,
and final damage.

The rest of the pipe acceptance pass covers right-hand ownership and alignment,
weighted one-hand follow, right-arm/socket placement, visual and wall-proxy
motion, and safe reset on menus, tracking loss, recenter, focus loss, and
weapon changes. Rejected local-player contacts stay blocked; enemy and
unrecognised Retail melee continue through the original path. Two-hand behavior
remains available only for profiles that explicitly enable a secondary grip.

## Two-hand axe interaction slice

Add `-TwoHandedMelee` to a launch that already enables
`-PhysicalMeleeVisualProxy`. For setup, also add
`-WeaponGripCalibration`. This is the first reusable support-grip layer; the
fire axe is the first profile to opt in. The mapped `pipe_lever` and unmapped
one-handed weapons continue through the independent one-hand path.

```powershell
.\tools\launch-condemned-m2-vr.ps1 `
  -StereoTuning -RenderScale 100 `
  -LocomotionProbe -TurningProbe -MenuProbe -MenuControlsProbe `
  -InteractionProbe -CoreActionsProbe -HapticsProbe -HeadAimProbe `
  -AimPathProbe -PhysicalMeleeProbe -PhysicalMeleeWallProxy `
  -PhysicalMeleeVisualProxy -WeaponGripCalibration -TwoHandedMelee `
  -DesktopWindow
```

The interaction takes *The Walking Dead: Saints & Sinners* as its experience
reference: the weapon remains a heavy object led by the hands rather than a
rigid controller decoration. The current diagnostic implements the parts that
are valid before Condemned has a standalone weapon rigid body:

- The right hand is the dominant owner and primary position anchor.
- The left grip attaches only on an intentional squeeze begun inside the
  configured handle volume. A squeeze begun away from the handle is consumed
  as a missed grab and cannot snap on merely by moving closer.
- The off hand supplies the shaft direction. A shortest-arc solver retains as
  much right-hand twist as possible and never scales the authored weapon to
  span an arbitrary controller distance.
- Separate 0.65 attach and 0.35 release thresholds prevent noisy grip values
  from repeatedly attaching and releasing. Excessive hand separation,
  tracking loss, menus, focus loss, weapon changes, and invalid poses release
  safely.
- Attachment and release do not reset the existing bounded damped-spring
  filter. The axe therefore retains its configured weight and follow-through
  rather than snapping between one- and two-hand poses.
- Opening VR Tools releases the live support-hand selection so its controls
  cannot fight the weapon, but it does not reset the calibrated anchor. The
  per-weapon session cache survives menu reopen, weapon swaps, drops, and
  reacquisition; it fills unused records before applying LRU eviction.
- Left squeeze is withheld from the otherwise conflicting Run binding only
  while it is attached or is making a valid near-handle grab. Keyboard Run,
  locomotion sticks, and unrelated controller actions remain available.
- The same solved shaft direction feeds the OpenXR tracking-space swing meter,
  so a support-hand-led rotation can move the axe head and qualify a swing
  without Retail locomotion or turning being mistaken for weapon motion.

The VR tool menu's **2-Hand** tab edits the per-weapon support offset and grab
radius. Place the cyan left-controller grip where the hand should sit on the
visible handle and activate **Capture Current Left Hand Pose**; the offset is
computed in the dominant right-hand aim frame and updates immediately. Reset
restores and saves the profile values, and **Save Two Hand Snapshot** writes
the combined primary/secondary record and emits the profile-ready values.
After closing the menu, release the left grip once, place it on the handle,
and squeeze to attach.

The accepted fire-axe support offset from the 2026-08-04 headset run is
`{3.114, -30.258, -14.828}` LithTech units with a 0.15-metre grab radius.
It remains the stable profile starting point. Further live changes persist as
per-weapon overrides; deliberately promoting a tested value into profile data
is still a separate source change.

This follows the common XR interaction pattern of allowing multiple selecting
interactors on one held object while retaining an explicit movement/velocity
policy. Unity's XR Interaction Toolkit documents both the multi-select model
and velocity-tracked held-object controls in its
[XR Grab Interactable manual](https://docs.unity3d.com/Packages/com.unity.xr.interaction.toolkit%402.0/manual/xr-grab-interactable.html).
It does not imply that Condemned is using Unity; the source is a public
reference for the interaction policy.

This remains a render-only Retail-model override plus a collision proxy. Full
*Saints & Sinners*-style wall resistance, hand sliding, collision-constrained
two-hand leverage, throwing, and weapon-on-weapon contact require gate 6's
standalone physical item. The current solver is structured as profile data and
a shared select/pose layer so that work carries forward rather than becoming
an axe-only visual exception.

## Arm IK handoff and discovery

The engine-independent two-bone solver from `DR-89/fear-vr` commit
`4bcd610d904478a310b0dfc39a612b576115027a` is now present in
`src/common/arm_ik.h`. Its original clamped-reach, mirrored-pole, invalid-input,
and elbow-continuity tests run on both x86 and x64. This imports only the
portable geometry; no F.E.A.R. skeleton names or engine addresses are treated
as Condemned facts.

Condemned's first read-only discovery gate is enabled with
`-ArmIkDiscovery` on `launch-condemned-m2-vr.ps1`. It requires
`-StereoTuning`, then samples the verified render path until the live player
body exists. Before invoking any model query it verifies all of the following:

- the Retail `CPlayerBodyMgr::Instance` signature and encoded return address
  at `GameOrig+0x2EA0`, without invoking the singleton;
- its expected manager object at `GameOrig+0x167A50` and player-body field at
  offset `0x10`;
- the registered `ILTModelClient.Default` version 0 object;
- equality with Retail's own `g_pModelLT` global once Retail initializes it;
  discovery remains armed but makes no model calls while that global is null;
  and
- executable targets for every model-interface slot used by the dump.

On success the loader emits `arm_ik_discovery_player_body`, node, and socket
records followed by `arm_ik_discovery_complete`. Node records
include pre-order index, handle, parent, and name. To avoid forcing a full
animation reevaluation for every bone in one render frame, world transforms
are sampled only for arm-relevant node names; socket transforms are sampled
for every resolved candidate. The model API has no general socket enumerator,
so the first pass probes known
player/weapon candidates including `RightHand` and `LeftHand`; further names
can be added after the node dump. Retail's node and socket slots match the
public `ILTModel` header. The overloaded piece methods are ordered differently,
and the later filename/material slots also do not match this executable despite
the unchanged interface version. Discovery therefore excludes those
unnecessary calls instead of guessing at their ABI. Any caught query fault
records its exact read phase. The pass is observation-only: it does
not add node controls, hide pieces, replace materials, or alter animation.

The live Retail capture completed with 75 nodes and established these exact
chains:

| Side | Shoulder | Upper arm | Forearm | Hand | Socket |
|---|---|---|---|---|---|
| Left | `Left_shoulder` | `Left_armu` | `Left_arml` | `Left_hand` | `LeftHand` |
| Right | `Right_shoulder` | `Right_armu` | `Right_arml` | `Right_hand` | `RightHand` |

The sampled right upper-arm segment was approximately 29 engine units and the
right forearm-to-socket segment approximately 36 units. Both hand sockets and
all eight arm transforms resolved without a fault.

## Right-hand socket proof

`-ArmIkRightHandProof` is the first opt-in mutation gate. It requires
`-StereoTuning` and `-HeadAimProbe`, installs one callback on `Right_hand`, and
solves the authored `RightHand` socket exactly onto the same fresh, weighted
VR weapon pose already shared by the visible model and collision path. It does
not yet rotate `Right_armu` or `Right_arml`; an expected disconnected-looking
arm is therefore useful evidence during this isolated socket/alignment test.

Before registering the callback, the gate repeats the singleton and model
interface checks above and additionally requires exact Condemned.exe
1.0.314.0 vtable targets for `GetNode`, `GetSocket`, their world-transform
queries, and the node-specific control registration/removal methods. Retail
orders the overloaded methods differently from the public header: the
node-specific `AddNodeControlFn` and `RemoveNodeControlFn` methods are slots 22
and 24. The gate rejects any other layout.

The cached transform is `inverse(handNodeWorld) * handSocketWorld`. During
skeletal evaluation the callback applies
`desiredSocketWorld * inverse(socketFromNode)`, converts that node pose through
the current model-world transform, and writes only the `Right_hand` local
position and rotation. Target snapshots expire after 250 ms, flat/menu frames
invalidate them before Retail renders, and every callback verifies that the
manager still owns the same live player body. Player-body changes clear the
state and remove the old callback when the old model object is still valid.

The second opt-in gate, `-ArmIkRightArm`, now measures the authored local bone
vectors at installation and controls both complete arm chains around their
exact socket targets. The legacy switch name is retained for compatibility.
It is mutually exclusive with `-ArmIkRightHandProof`, retaining the right
hand-only path as a clean A/B and safety fallback.

The full gate resolves both exact verified chains in parent-to-child order. It
measures each upper arm to forearm and each forearm to wrist, then requires all
segment lengths to be finite and between 1 and 200 engine units. It registers
each upper arm, forearm, and hand callback transactionally. Failure on either
side removes every installed callback, so a half-controlled body cannot remain
active. Body replacement clears both histories and removes the old callbacks
in child-to-parent order when the old model remains valid.

During evaluation, each upper callback builds the pole from the player-body
right/up/forward axes and the imported elbow tuning, mirrors the outward sign,
solves the elbow,
and rotates the authored upper-arm vector toward it. The forearm callback only
runs when the upper callback has published a solve for the same fresh OpenXR
sample, then rotates the authored forearm-to-socket vector toward the target.
The hand callback retains the validated exact socket placement and calibrated
per-weapon correction. Live testing validated callback order, wrist placement,
and player-local locomotion anchoring. Elbow-pole tuning is therefore exposed
through its own global tool-menu tab.

The first full-arm run exposed two Condemned-specific corrections. Its
`RightHand` socket is approximately 13.5 units away from the `Right_hand`
wrist node. Solving the arm length and direction to that displaced socket,
then independently placing the wrist from the socket, made controller rotation
swing the unresolved offset around and visibly compress/stretch the wrist.
The arm now solves to the fully resolved desired wrist-node position and
measures the lower segment from forearm to wrist; only the final hand callback
uses `socketFromNode` to retain exact grip-socket placement and orientation.

The skeleton may also evaluate before the latest render-path controller target
is published. Retaining the previous target as an absolute world position made
smooth locomotion move the player body away from its hand for that interval.
Once a live player-body model transform has been observed, each published
socket target is therefore cached in player-body local space. Every callback
reconstructs it through the current body model transform. This is the arm
equivalent of the weapon-weight filter's player-local locomotion frame: body
movement and turning are rigid parent motion, while controller motion remains
the only relative hand motion. A bounded world-space fallback is used only
before the first valid body transform. Each side maintains an independent
body-local target and bend-continuity history.

While free, the left target uses the raw OpenXR grip pose. On two-hand
attachment, its position becomes the authored support point reconstructed from
the final weighted weapon pose, and its current rotation is captured relative
to that same weapon pose. Position and orientation therefore remain one rigid
weapon-relative grip instead of floating or swivelling independently. Release
restores the raw controller pose. Display-only calibration is applied after
either base pose and is never fed back into the weapon solve.

The **Hand IK** tab now provides the live alignment pass for that proven
socket target. The selected weapon has independent local X/Y/Z position and
pitch/yaw/roll corrections. Position is rotated by the current weighted weapon
pose before being added in world space; rotation is composed after the same
pose, so the correction follows the controller instead of becoming a fixed
world offset. The paired adjustment step ranges from 0.10 to 5.00 LithTech
units and 0.25 to 10.00 degrees. Reset restores the zero-offset proof behavior,
and the status row reports whether the node callback is active.

Each changed value is saved immediately under the stable Retail weapon index
as the `right_hand_ik` record in
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`. The record includes the
resolved profile identity and therefore fails closed if that weapon mapping is
later changed. `LOG RIGHT HAND SNAPSHOT` is diagnostic only; adjustment and
reset actions have already persisted before the snapshot is requested.

The first live alignment pass produced these accepted starting records:

| Weapon | Retail index | Position offset | Rotation offset |
|---|---:|---|---|
| `pipe_lever` | 32 | `(-5.0, 5.5, 4.5)` units | `(70, -43, -90)` degrees |
| Fire axe | 17 | `(-5.5, 9.5, 1.0)` units | `(-51, -140, 23)` degrees |

Both were observed in the settings file after successful
`m5_right_hand_ik_settings_saved` events. They remain user calibration data,
not hard-coded profile constants.

## VR tool menu

Continuous stereo now includes a simple in-headset tool menu rendered after
each eye. It is a reduced-size head-relative comfort panel, positioned slightly
below eye centre and given crossed per-eye disparity so it converges at a
comfortable finite distance instead of behaving like a full-eye screen-space
overlay. The defaults are 62% scale and 1.50 metres. Open or close it with both
grip/squeeze buttons plus Y. F12 is the keyboard fallback. The menu captures
gameplay controls while open and keeps capturing until the close buttons,
grips, triggers, and sticks have returned to neutral, preventing a menu action
from leaking into Retail gameplay.

The menu has ten tabs:

- **Melee:** for a verified melee profile, enable the temporary
  swing-to-attack adapter and tune trigger speed, re-arm speed, pulse duration,
  and cooldown while seeing live swing speed and trigger count. Unmapped
  weapons show the adapter as unavailable rather than inheriting the axe's
  attack behavior.
- **Weapon:** tune impact mass, hand inertia, positional/rotational follow,
  catch-up, and damping for the currently equipped weapon's own profile.
  Impact mass is reserved for contact energy; hand inertia and the follow
  parameters control the visible virtual-weight response.
  Melee and Weapon tab changes are persisted by Retail weapon index in
  `%LOCALAPPDATA%\\CondemnedVR\\weapon-settings.ini`; profile identity is
  stored with each record so stale values fail closed if a mapping changes.
- **Grip:** adjust the equipped model's local XYZ position and rotation,
  adjustment step, reset, and save a grip snapshot. Adjustments and reset
  auto-save the equipped Retail weapon's `grip` record. This tab requires
  the `-WeaponGripCalibration` launch option and shows the controller
  wireframe.
- **2-Hand:** enable the profile's support grip, edit its local offset and grab
  radius, capture the current left-hand pose, reset, and save a combined
  snapshot in the same `grip` record. All changes auto-save. Live attachment
  distance/error remains visible on the tab.
- **Hand IK:** adjust the rendered dominant hand's per-weapon local XYZ socket
  target and pitch/yaw/roll while seeing the result immediately. The tab has
  independent fine/coarse steps, zero-offset reset, callback status, and a
  diagnostic snapshot action. Its corrections save immediately and do not
  modify the weapon model's Grip-tab calibration or physical handling.
- **Left IK:** adjust the support hand in its controller-local frame by up to
  20 cm along right/up/forward and by pitch/yaw/roll in five-degree steps.
  These global display-only corrections apply to both the free grip and the
  attached weapon-support target. Reset and snapshot actions are provided,
  and edits save immediately in the versioned `[arm_ik] elbow` record.
- **Elbow:** tune the full-arm solver's body-relative outward, downward, and
  backward pole components in 0.05 increments. Elbow continuity keeps the
  remembered bend hemisphere stable as the hand crosses difficult poses; it
  can be disabled for diagnosis. Adjustments reset bend memory immediately,
  include a defaults action and diagnostic snapshot, and persist globally as
  `[arm_ik] elbow` in `weapon-settings.ini` rather than following a weapon.
- **Display:** tune FOV scale, world scale, menu size, and menu convergence
  distance; toggle HMD translation, eye polarity, and stereo; recenter; or
  restore display defaults.
- **Controls:** shows the complete controller and keyboard menu mapping.
- **Debug:** shows current weapon/tracking state, live swing telemetry, and
  proxy/two-hand state.

Use the left/right triggers to change tabs, left-stick up/down to choose a row,
right-stick left/right to change a value, A to activate a row, and B to close.
The keyboard equivalents are left/right square brackets, arrow keys, Enter,
and F12. All values take effect immediately. Melee, Weapon, Grip, 2-Hand, Hand
IK, Left IK, and Elbow menu values are persisted automatically. The Grip and
2-Hand snapshot rows force an additional save and diagnostic snapshot. The
continuous fallback calibration path uses controller Y or keyboard P to avoid
writing on every tracking sample. Promoting a tested override into authored
profile data remains a deliberate source change.

The cyan `EQUIPPED ... INDEX ...` banner identifies exactly which weapon is
being edited. Settings use the stable Retail weapon index rather than runtime
pointers. A fixed 64-slot allocation-free registry keeps each observed
weapon's Melee, Weapon, and Hand IK values isolated: switching away and back
restores that weapon's values. Known catalog entries start from their authored defaults
(weapon 17 therefore retains the heavy fire-axe setup); unmapped indices start
from conservative one-handed handling with swing attack disabled. Grip
alignment follows the same per-index lifetime rule.

## Persistent fire-axe profile

The live run `run-20260801-095026` captured the tester-identified fire axe at
Retail weapon index 17. That index now selects a persistent profile on every
launch. Its final controller snapshot is:

```text
modelLocalGripPositionUnits = {-0.117, -3.053, -6.982}
modelLocalGripRotation      = {-0.052973, 0.840891, 0.248921, 0.477635}
localRotationDegrees        = {-41.240, 123.937, -15.447}
secondaryGripOffsetUnits    = {3.114, -30.258, -14.828}
secondaryGripGrabRadiusM    = 0.150
```

The position and quaternion are used directly as the model-local grip. The
degree values are retained only to make later setup changes understandable.
Pointer values from the run are intentionally not used because they are
process-local.

The axe profile uses an 82-unit reach, 7-unit collision radius, and a
deliberately heavy 4.5 kg mass. Its handling weight is the maximum supported
value of 4.0, with positional/rotational follow values of 10/8, bounded
catch-up strength 0.80, and damping ratio 0.55. The bounded damped-spring
filter makes the visible weapon and collision pose lag hand-relative movement
and retain mild follow-through after the hand stops, while preventing an
undamped or permanently oscillating response. The filter runs in the player's
local locomotion frame: smooth movement and turning carry the held weapon
rigidly with the player and are not interpreted as gravity or hand
acceleration. It snaps on weapon changes, tracking loss, long frame gaps, and
recenter events. This is an initial weight response; later
collision-constrained virtual coupling will replace it when the weapon becomes
a standalone physical object.

The provisional `pipe_lever` profile uses the same 10/8 follow-stiffness
family and 0.80 bounded catch-up as the axe, but a lower 1.75 hand-inertia
value and higher 0.65 damping ratio. This makes changes to hand inertia
perceptible while keeping the pipe quicker and less prone to follow-through
than the axe. Its 1.75 kg impact mass remains independent because it describes
contact energy rather than the current render-pose approximation.

## Fire-axe swing attack bridge

The fire axe continues to use its
tracking-space hand/endpoint sweep speed to request Retail's ordinary
Fire/attack command 17. Keeping this meter in OpenXR tracking space excludes
Retail locomotion and turning from the gesture. While the support grip is
attached, its solved shaft direction drives the endpoint used by that meter.
A valid focused-gameplay sweep
at or above 3.00 m/s starts a bounded 100 ms command pulse. The gesture must
then slow to 0.75 m/s or below to re-arm, and a 450 ms cooldown prevents
repeated attacks from one continuous fast motion.
The right trigger remains available and is merged with this pulse through the
same verified binding-value hook.

For the fire axe, this remains a transitional adapter rather than damage
authority. A fast swing requests an attack, but Retail still owns the attack
animation and collision window, and physical impact dispatch remains blocked.

The pipe preset retains the same pulse as a collision-seed and compatibility
bridge. After Retail's verified collision body exists, the separate pipe-only
contact gate checks it continuously and does not use animation or pulse timing
to decide damage.

For both profiles, tracking loss, an invalid sweep, menus, focus loss, weapon
changes, stale input, and captured grip-calibration controls cancel the pulse
and reset its latch. Each accepted gesture is logged as
`m5_physical_melee_swing_attack_triggered` with the measured speed and timing
parameters.

The 3.00 m/s value replaces the initial, over-sensitive 1.80 m/s test value.
Its legacy trigger/rearm/pulse/cooldown values remain persisted for the
temporary seed and fire-axe bridges, but are intentionally no longer exposed
as the main Melee tab. That tab now owns only physical contact behavior.
