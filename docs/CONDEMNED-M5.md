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

The session cache is backed by a versioned `grip` record. Reads check the
writable `%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini` key first, then
`condemnedvr-defaults.ini` beside `GameClient.dll`. GRIP and 2-HAND menu
adjustments, captures, and resets save immediately to the player file only.
Continuous fallback controller/keyboard adjustment does not write every
tracking sample; controller Y or keyboard P saves that current calibration
explicitly. Position, local rotation correction, support-grip enable/offset,
and grab radius share the record because both tabs edit one calibration slot.

Each record contains the stable Retail weapon index and resolved profile
identity. A valid per-index value from either layer wins. For an explicitly
mapped one-handed weapon, a missing record or stale pre-mapping profile identity
may inherit the matching Pipe record; the loader reports
`source=pipe_baseline`. A malformed player value is not hidden by the
packaged layer, and an unsafe value, excluded identity, or unavailable Pipe
record still fails closed to authored profile values. Those authored base
values remain immutable in the live slot, so RESET restores a known default and
persists that reset rather than changing the profile itself. Successful loads
and saves are reported as `m5_weapon_grip_settings_loaded` and
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

## Packaged first-level release defaults

The release-readiness candidate turns the retained in-game calibrations into a
project-authored, source-controlled `config/condemnedvr-defaults.ini`. CMake
copies it beside the x86 loader, and the M1, M2 diagnostic, and canonical
M2-mono stage builders require, hash, and stage that copy. The file is a
read-only baseline at runtime: every menu save still targets only the player's
LocalAppData INI, so an update cannot overwrite their choices.

The packaged file contains records for indices 0, 3, 4, 17, 30, 32, 46, 76,
and 77 plus global arm IK. This is a delivery statement, not blanket live
acceptance. Pipe index 32 is the live-accepted melee baseline; the forensic
action/aim paths for Item Camera index 3 and Scanner index 46 are live accepted,
as is Colt index-76 direction. The newest pose/hand values for indices 3, 4,
17, and 46 are retained calibrations from the live session, not separate
perceptual acceptance for every value. Index 0 retains applied 2x4 values but
does not have accepted collision/damage, and the retained identities of indices
30 and 77 are not yet evidenced. The remaining records await their own
representative gates.

Global developer visibility is versioned separately as `[debug] draw`.
The packaged value `1,0,0` and the compiled
`ToolMenuDebugDrawSettings` fallback both hide collider and controller
wireframes. Changing either Debug row writes both booleans immediately to the
player INI; failed or malformed loads remain hidden. This changes the current
release candidate from the earlier live-tested default-on, session-only
behavior. The draw controls themselves and collider-hidden damage are live
accepted, and the two-run checkpoint below now live-accepts OFF persistence
across a normal process restart.

The settings-store test covers packaged fallback even when the player file
exists but lacks the requested key, player override precedence, ON and OFF
round trips, and rejection of a malformed nonempty player value instead of
falling through to a valid package value. It also parses every declared record
from the actual copied post-build INI. The complete RelWithDebInfo gate
passes 19/19 x86 and 15/15 x64. Source, build output, and canonical stage share
defaults SHA-256
`04AFF10BC30AFF6B069A613FB0E29FD579F9AA9C21E63A6D8E749186110DC579`;
built and staged `GameClient.dll` share
`C22FAB9AA6DC4C5B4802C3E62B6EBAE81B4888EE0FDB828981A5F8DEBD49A610`.

### Live Debug persistence acceptance

Runs `run-20260811-102206` (game PID 42800) and
`run-20260811-102500` (game PID 33184) are separate canonical Pipe launches.
The later process loaded `collider=0 controller=0` at renderer installation,
then recorded eight successful visibility saves, ending at `0/0`, with zero
`m5_debug_draw_settings_save_failed` events. The writable player record ended
at `[debug] draw=1,0,0`, and the headset tester reported that the choices now
appeared to save across the relaunch.

After the final OFF save, three actor contacts were accepted and native-
forwarded at 12.184, 12.705, and 7.543 m/s. The full run ended healthy with 515
callbacks, 14 accepted/native-forwarded damage dispatches, 11 rearms, two
multi-target swings, 515/515 Retail reference-vector clears, zero reference
failures, and zero same-target accepts before rearm. This live-accepts the
global player save/restart round trip and retains the earlier proof that hidden
drawings do not disable collision or damage.

The preserved later-process checkpoint is
`stage/condemned-m2-mono/logs/run-20260811-102500/condemnedvr-loader-debug-settings-persistence-checkpoint.log`
(2,509,819 bytes; SHA-256
`ECC05757BD7B2DD7EE3A2864FA5E38983551FAB38CC5145DF990B9778C8BB993`).
Its launch-report SHA-256 is
`DD1A86821475D8D55D6EEDDC98B38BD9806E739DDFB7A61F55F6DF8B9DC29636`;
the preceding run report is
`9BD6CF3A514C977A06829730CBF0664B3EACC67C46D1D10A7020019357EF0564`.
Those runs used the preceding staged defaults SHA-256
`02AD2B4073F262E3D320DF797ADEE2F4F12027245448B33432286F03BA9EB64B`.
After the process closed, a read-only comparison found new retained calibration
keys/values for Item Camera index 3 and indices 4, 17, and 46. Promoting them
produced the current 28-key package hash above; the loader binary was unchanged,
and the full automated/package-parse gate passed again. The Debug persistence
claim therefore remains live verified, while those newly packaged calibration
values retain their individual evidence boundaries.

The loader does not yet expose which settings layer supplied a successful raw
value, so a clean isolated user path is still required to live-accept packaged
per-weapon fallback.

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
.\tools\launch-condemned-m2-vr.ps1
```

The no-argument `Current` profile selects the accepted Pipe baseline for any
mapped one-handed weapon and also exposes the guarded Retail VR Settings entry.
The retained explicit `-WeaponTest Pipe` name remains a compatible Pipe-only
diagnostic alias. The current profile expands the accepted M4 controller gates
plus physical-melee telemetry, wall and visual proxies, grip calibration, full
arm IK, recentering, desktop-window support, and one-handed contact damage. It
deliberately leaves `-TwoHandedMelee` off.
Confirm the Debug tab reports the intended Retail index/profile before judging
alignment, inertia, sweep speed, or collision behavior.

Run `run-20260820-041148` live-exercised that exact no-argument selection
through launcher readiness. The report records `FeaturePreset=Current`, the
internal Pipe baseline, Retail VR Settings, physical melee/contact damage,
weapon calibration/authoring, and full arm IK enabled; two-hand attachment and
forensic memory tracing remained disabled. The staged bridge was the expected
module, no ASI loaded, foreground restoration passed, and both processes were
responding. This is launcher/stage/hook-readiness evidence only, not perceptual
or gameplay acceptance for the combined feature set.

The session subsequently exercised active headset input and gameplay/menu
paths, then ended after approximately one minute in the known Windows
Application Error bucket `Condemned.exe` / `ClientFx.fxd+0x26EEF`, exception
`0xc0000005`, report ID `2ea5e501-d7df-4db5-a083-687c6c4a929c`. The host
observed the game disconnect and shut down cleanly. Treat this as a failed
combined-profile stability/shutdown gate, not as evidence that Current or the
Retail VR Settings entry caused the fault: the same bucket is documented before
that work. A controlled baseline/profile comparison remains required.

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

`-PhysicalMeleeColliderDebug` enables the configured swept-capsule
diagnostic. When its Debug visibility preference is on, amber is a preview
while no fresh player-owned Retail collision object exists; green means that
object is live. The cross marks the exact controller-tip origin supplied to the
collision transform. The developer overlay is visible through geometry when
enabled. The VR Tools `COLLIDER` tab edits controller-local position,
pitch/yaw/roll, length, radius, and forward/reverse direction with immediate
preview. Values are stored independently per stable Retail weapon index.

The earlier `DEBUG`-visibility candidate began with two independent,
session-only toggles: `DRAW MELEE COLLIDER` and `DRAW CONTROLLERS`, both
defaulting on. Their gates existed only inside the two overlay-render
functions; hiding the capsule did not stop the collider snapshot or native
contact path, and hiding the controller wireframes did not pause grip
calibration, controller input, IK, or weapon pose publication. Menu-change
diagnostics included `collider_draw=0|1` and `controller_draw=0|1`. Its full
automated gate passed 19/19 x86 and 15/15 x64 tests with staged loader SHA-256
`FF89DA8555392B4312972637053124CB7E346BB435D6CB05F54315FA206FDE2D`.
The current packaged-default/persistence behavior supersedes only its initial
visibility state; the draw-only isolation evidence below remains valid.

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

Schema-v4 snapshots expose the current phase and recommendation, pipe identity,
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

Schema v4 additionally keeps the contact's monotonic runtime tick, fresh HMD
full/XZ distance and grip distance to that same Retail surface point, and a
snapshot of whether swing-attack telegraphing is enabled, has triggered for
the current swing, and is still in its pulse window. Per-target records expose
accepted-contact count, first/last runtime ticks, the last accepted interval,
and telegraphed/non-telegraphed/unknown counts. The top-level `Combat` object
separates actor accepted hits from world/prop hits, reports the minimum observed
actor-contact horizontal distance, retains latest read-only player vitals, and
correlates existing command-28 down/up edges with player-health decreases. The
post-run additive fields count all Retail command-17 edges independently from
automatic motion-trigger events. New command-edge records carry a monotonic
loader tick; only that tick, not watcher ingestion UTC, may produce command-28
hold duration.
`EnemyHealthObserved` remains false unless a future verified source exists;
native dispatch count is not presented as target health. Missing fields in an
older log are reported as unknown rather than silently treated as new evidence.

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


### Combat-behaviour diagnostic candidate (12 August 2026)

This candidate is diagnostic-only: it does not change collision, damage, AI,
player spacing, attack commands, or blocking.

- Static analysis on the identity-verified Retail `GameOrig.dll` found the
  player-stats singleton pointer at RVA `0x001702F8`, current health at `+0x04`,
  and maximum health at `+0x0C`. The setter at RVA `0x000A6F60` reads maximum
  then current health and performs the clamp/store. The command handler at
  `0x000A7240` loads the same singleton, and registration at `0x000A949F` maps
  the `Health` string at `0x00145590` to that handler.
- The runtime gate verifies the setter prefix, handler absolute operands/body,
  registration handler/string operands and tail, and the string itself after
  module relocation. Only then, and only with contact damage enabled in Retail
  gameplay, a 100 ms read-only sampler logs initial or changed player health.
  A mismatch emits `m5_combat_player_vitals_rejected` and leaves melee
  unaffected.
- Static analysis of the verified native impact dispatcher at RVA `0x0001F270`
  shows an engine damage message handoff rather than a synchronous enemy-health
  return. Therefore diagnostics count accepted/native-forwarded actor contacts
  but explicitly mark enemy health unobserved. Visible defeat still requires
  tester observation; dispatch count must not be relabeled as damage amount.
- Each compact contact now snapshots fresh HMD and grip world poses against
  Retail's contact point. LithTech +Y is up, so XZ distance is the bounded
  stand-off diagnostic. This is not a discovered player-capsule radius.
- The same record snapshots the existing command-17 swing-attack state. The
  watcher separately consumes automatic swing-trigger events, all command-17
  edges, and command-28 block edges, then associates the latter with changed
  player-health samples. Automatic contact flags do not erase or imply a manual
  Retail trigger.
- Portable tests cover finite/stale proximity and fail-closed vitals bounds.
  The standard build passes 21/21 x86 and 17/17 x64; the separate synthetic
  schema-v4 watcher regression also passes. Built and canonical-stage
  `GameClient.dll` are byte-identical at SHA-256
  `96C17087069654ABA75A321BDCEC4ECA08EBD0BED622A6750DF70B75BE483505`.
- `run-20260812-090345` is a failed launch, not a live combat run. The
  VirtualDesktopXR 1.0.10 host retried HMD acquisition for 15 seconds and
  exited on `XR_ERROR_FORM_FACTOR_UNAVAILABLE`; no game process or loader
  evidence was produced.

`run-20260812-100216` is the first successful live pass for this diagnostic on
VirtualDesktopXR 1.0.10 / Quest 3, using the exact staged loader SHA-256 above.
The read-only vitals gate armed with the expected identity and produced no
rejection or unavailable event. The run ended normally after the game heartbeat
stopped, and the host/bridge logs contain no runtime error or warning.

- Retail produced 513 callback records: 119 actor candidates and 394
  world/prop contacts. Six actor contacts and one world/prop contact qualified,
  were native-forwarded, and completed seven damage dispatches. All 513 Retail
  reference vectors were cleared, with zero failures.
- The latch blocked 44 held-overlap callbacks. Seven rearms completed, there
  was no same-target accept before rearm, and no multi-target swing. One actor
  received two accepted contacts 1,266 ms apart. Another received three, with
  intervals of 2,703 ms and 30,516 ms. This rejects repeated callbacks within
  one swing as the cause of the perceived fast kills. The tester reports that
  some enemies died in one physical hit and others in two. That live-confirms
  high and variable lethality, but enemy health remains unobserved and the run
  has no marker joining each visible death to a specific callback. Five actor
  accepts carried node handle `0x00000006` and one carried `0x00000026`; those
  handles have no verified semantic name here. A head-hit multiplier is
  plausible but remains a hypothesis.
- Every accepted contact had `attack_telegraph_enabled=0`; the loaded Pipe
  record explicitly had `swing_attack=0`, and there were zero automatic
  motion-triggered swing-attack events. Separately, the ordered raw log contains
  five controller-applied command-17 down/up pairs. Because automatic swing
  attack was disabled, these are manual Retail attack-trigger trials. The
  tester reports that enemies reacted only to those Retail-trigger attacks and
  did not react to physical-only swings. This live-correlates AI anticipation
  with the native attack command path. It does not yet identify the downstream
  AI signal or prove that enabling automatic `SWING ATTACK` will preserve
  controller-driven weapon motion without an objectionable Retail animation.
- Accepted actor hits were 0.5218--0.7543 m horizontally from HMD to Retail's
  actual contact surface. The nearest of all actor callbacks was a slow,
  `swing_not_qualified` contact at 0.4729 m with the configured capsule already
  overlapping the surface by 0.0132 m. This proves the configured collider can
  reach an actor at that practical stand-off; it does not identify or measure
  the Retail player capsule.
- Player health was observed from 200/200 and recorded 12 decreases, all while
  command 28 was inactive. Five complete command-28 down/up pairs were present,
  and no decrease appeared between an ordered down/up pair. The tester could
  not get blocking to work while being attacked, so the current block behavior
  fails the headset-visible defensive gate even though command 28 reaches
  Retail. The preserved event stream's UTC values are watcher ingestion times,
  not game-event timestamps; they cannot establish how long each trigger was
  held. Zero sampled decreases while active is not proof of a successful block.

The preserved loader checkpoint is 3,230,134 bytes, SHA-256
`6A2BCEB39037C02C68E6E17CB02B065FE06E3B1C09F3F0E0F144BD854567081B`.
The final schema-v4 snapshot and event stream have SHA-256
`9051FDF65238627E1E06F7EDF461B64E70B3D46161E5ECF76CBDA2811D338F7E`
and
`045158B39C6D429E9F7FC62C69E42EEC696ADC855AFD5A9ABE85F640AA80B2CD`.
The preserved stream contains 11,910 alternating collider-state records for
two addresses. That is watcher noise, not a combat result. The current watcher
now writes only a seeded-state transition or the first occurrence of a collider
address while still updating the live snapshot; it reports observed, recorded,
and suppressed totals. A synthetic five-event A/B/A/B/A fixture records two and
suppresses three, and the focused schema-v4 regression passes. This compaction
is automated-only. The tester-supplied outcome closes the requested visible
observations: variable one/two-hit deaths, AI reaction only with the Retail
attack trigger, and no working block while attacked. Those observations expose
the next questions; they do not accept hit-location scaling, automatic attack
telegraphing, or blocking.

The post-run diagnostic candidate now reports every command-17 edge separately
from automatic motion-trigger events. Command-edge records also carry a loader
`runtime_tick_ms`; only those monotonic ticks may be used to calculate a block
hold duration. The synthetic watcher fixture covers command-17 down/up counts
and a 250 ms command-28 hold. The standard gate passes 21/21 x86 and 17/17 x64
plus the watcher regression. Built and canonical-stage `GameClient.dll` are
byte-identical at SHA-256
`9E4E1B4CC0FC5C569AC8A3A3019A70292CEC4DFF713D9FC6F3BAE9F683741B57`.
This is implemented, tested, and staged, awaiting the next live run.

### Automatic swing-attack A/B setup

For the bounded ON pass, the player override was prepared as follows. Under
`[weapon_32]`, only the third `settings` field changed from zero to one:
`settings=2,1,1,10,0.75,100,450,3,2.75,10,8,4,0.950000107,1,7.25,0.119999997`.
The original and prepared player files have SHA-256
`2095B603D697AD25F9652CE04CB9D8DC44547D5B9E5AF910DBD0AD5A9B979B78`
and `D8796C94A26DF97694BB8E178C092832DCE5D1AFDDB0A6CA3D9084F08B64A1D1`.
Rollback copy
`weapon-settings.before-auto-swing-test-20260812-215524.ini` is beside the
player INI. Grip, hand IK, collider, hit speed, cooldown, rearm, and all other
records are unchanged. Project and staged packaged defaults remain OFF and
byte-identical at SHA-256
`6ADA21EF6DED26FA929FE53B95431568F9C172F7382FA751C0F0C4086CE3F39A`.
The game was not launched during setup; the resulting live evidence is recorded
below.

### Automatic swing-attack ON live result

`run-20260812-124543` is the first live Pipe pass with the player override
reporting `swing_attack=1`. It used the staged `GameClient.dll` SHA-256
`9E4E1B4CC0FC5C569AC8A3A3019A70292CEC4DFF713D9FC6F3BAE9F683741B57`
on VirtualDesktopXR 1.0.10 / Quest 3. The game exited normally, the final
watcher snapshot is healthy, and the host/bridge logs contain no runtime error
or warning. The preserved launch report, snapshot, event stream, and loader
checkpoint have SHA-256
`1107EAF32CA3A8E2F5F7CB3DECD7EE72673222CFAAAB64C2EFE10B46F341FAFD`,
`518F024CC40F3F62F85237988896D72D43B765518CE8BB99CC0BB361B0FD99A9`,
`70D0F348AC9FE1833C605A724734714E1DC2BFD5FD1E81035BD01A797858214E`,
and `37DD60127A4A802056DB62CBFE986E5E1B1D5F36CC44477697452AE0476E061E`.
The loader checkpoint is 4,144,896 bytes.

- Thirty-five motion-triggered swing attacks crossed the unchanged 10.0 m/s
  threshold (10.003--25.206 m/s, mean 12.868 m/s). Each was followed by a
  command-17 down/up pair. There were 36 total command-17 pairs, leaving one
  additional pair not associated with an automatic trigger and therefore
  consistent with one manual trigger trial. All 36 rising edges also requested
  the existing command-17 right-hand haptic. This live-verifies the automatic
  motion-to-Retail-command handoff; headset-visible AI response, animation/pose
  quality, and whether pre-contact haptics are acceptable still require the
  tester's observation.
- Retail produced 515 contact callbacks. Twelve actor contacts across four
  targets qualified and were native-forwarded; no world/prop contact qualified.
  The latch blocked 121 duplicate callbacks, completed 12 rearms, and recorded
  zero same-target accepts before rearm and zero multi-target swings. All 515
  Retail reference vectors were cleared and released with zero failure. This
  confirms the physical forward path retained its contact and per-swing latch
  gates during the ON pass. It does not observe enemy health or exclude damage
  side effects from Retail's own attack animation/window.

- Every accepted actor contact carried `attack_telegraph_enabled=1` and
  `triggered_this_swing=1`. Five occurred while the 100 ms command pulse was
  active and seven after it had ended. Accepted speeds were 7.571--16.485 m/s:
  the automatic 10.0 m/s threshold starts the Retail command, while the
  independently configured physical-contact qualification remains authoritative
  for this proxy path. Actor node handles were `0x04`, `0x06`, `0x18`, `0x19`,
  and `0x26`; none yet has a verified semantic name.
- The closest actor callback was 0.493 m horizontally from the HMD to Retail's
  contact surface. The final accepted contact was already 0.0075 m inside the
  configured capsule. As before, this demonstrates collider reach but does not
  identify the Retail player-capsule size.
- Five command-28 activations lasted only 109--203 ms. Nine health decreases
  were observed, all outside the corresponding ordered down/up intervals; the
  final decrease began 94 ms after the last release. The tester initially
  described these as failed attempts, then clarified after further play that a
  trigger tap was sufficient and many attacks were visibly blocked without
  holding it. Preserve both observations in sequence: visible block efficacy is
  live verified, but this stream cannot correlate it with a native block-state
  lifetime. The current resolver is level-driven, yet command-28 edges describe
  only binding activation. The hypothesis that a tap primes Retail and the
  physical weapon collider subsequently decides spatial interception is not
  instrumented or proven. This run also lacks the raw trigger and eligibility
  reason on release, so it cannot distinguish a short physical press from
  trigger-threshold, hand-active, freshness, foreground, focus, capture, or
  game-state loss.
- One automatic pair violates the configured cooldown: triggers 6 and 7 began
  only 62 ms apart. The first 100 ms pulse was cut short after 47 ms, and the
  second began 15 ms later despite `swing_cooldown_ms=450`. Code inspection shows
  that transient input ineligibility, stale/invalid swing samples, or invalid
  context can reset the whole swing-attack state, including its cooldown. The
  exact reset path is not currently logged, so that explanation remains a
  bounded hypothesis. Add a reset-reason diagnostic or preserve the cooldown
  across transient cancellation before treating the cooldown as live-correct.

The tester reports that fast physical swings did make enemies react, proving
the desired anticipation still follows command 17. However, the Retail attack
animation visibly wound up and delivered its attack after the controller-driven
physical hit had already landed. Eleven of the 12 physical accepts followed
their automatic command edge by only 47--172 ms; the remaining one followed by
844 ms. The later animation strike is a visible headset observation rather than
a separately timestamped engine event. It creates unacceptable timing and a
possible second damage opportunity, although this run does not observe whether
that delayed Retail strike applied additional health damage.

This rejects automatic command 17 as the shipping AI-telegraph solution. Keep
physical contact damage and search downstream for an AI-facing anticipation
signal that does not start Retail's player attack animation. The player Pipe
override was restored to OFF after the result; active SHA-256 is
`2095B603D697AD25F9652CE04CB9D8DC44547D5B9E5AF910DBD0AD5A9B979B78`.
The rejected ON configuration is preserved as
`weapon-settings.auto-swing-rejected-20260812-230136.ini`, SHA-256
`D8796C94A26DF97694BB8E178C092832DCE5D1AFDDB0A6CA3D9084F08B64A1D1`.
Packaged defaults also remain OFF. The cooldown violation remains recorded but
is no longer on the active physical-melee path.

### Captured automatic block pose candidate (12 August 2026)

This working-tree candidate removes the player's manual block-seed step while
retaining the verified Retail command path. It is **implemented and
automated-tested, awaiting headset validation**.

- A dedicated five-control `BLOCK` tab is available for mapped one-handed melee
  identities. `CAPTURE CURRENT GUARD POSE` stores the current weighted weapon
  pose relative to HMD position and yaw, automatically enables it, and writes an
  independent versioned `block_pose` record under the stable Retail weapon
  index. Missing mapped one-handed records may inherit Pipe; malformed records,
  unsupported weapons, and explicit local clears fail closed.
- Position tolerance defaults to 0.18 m and angle tolerance to 25 degrees. Both
  are adjustable in headset. Entry uses the configured values; an active guard
  receives a fixed 0.06 m / 10 degree release margin so tracking noise cannot
  chatter command 28 at the boundary.
- HMD pitch/roll, world locomotion, and a shared body turn do not change the
  saved relationship. The matcher uses the centralized LithTech world basis,
  the exact weighted controller weapon pose shared by model and collision, and
  a yaw-only HMD frame. It does not add a local coordinate sign correction.
- During playing/foreground/fresh-tracking context, entering the saved pose
  automatically holds Retail block command 28; leaving releases it. Menu input,
  captured calibration, stale poses, weapon changes, unsupported identities,
  and disabled/unconfigured records release it. The left-trigger command mapping
  remains an independent fallback. Player input seeding is therefore not
  required.
- The menu previews live match/error even while open, but gameplay block output
  fails closed there. Runtime events report entry/exit, position/angle error,
  tolerances, tracking freshness, stable weapon identity, command 28, automatic
  source, `input_seed_required=0`, and manual-trigger fallback. The watcher
  keeps automatic pose state separate from command-active state and explicitly
  reports that Retail's native block-state lifetime is not observed.
- Portable tests cover exact capture/match, HMD/world yaw invariance,
  locomotion invariance, HMD-pitch tolerance, position/angle rejection,
  entry/release hysteresis, disabled/context/scale/settings failures, menu tab
  bounds, per-weapon registry isolation, settings round-trip/profile mismatch,
  malformed quaternion rejection, Pipe inheritance, firearm exclusion, local
  clear shadowing, and schema-v4 automatic-pose diagnostics. The actual x86
  loader, focused x86 tests, and focused x64 common tests compile and pass. The
  full gate passes 22/22 x86 and 18/18 x64 plus the watcher regression. Tested
  build and canonical VR stage are byte-identical at SHA-256
  `52E11A92CDC685205ECD66A4B1AD6C4CACBE27F440B44BEC34E27456D6642FF4`.

The live gate must distinguish three facts: automatic pose-to-command edges,
visible defense, and spatial interception. Capture Pipe in guard, close the
menu, avoid left trigger, and require an automatic entry/down edge followed by
exit/up. Test one enemy strike with the physical weapon intercepting, one with
it deliberately clear while the pose is otherwise near tolerance, and one
outside the pose. If effective block persists after exit, the candidate does
not yet satisfy pose timing; instrument a verified Retail cancellation path
instead of guessing one. If command edges are correct but interception fails,
investigate native block eligibility/collision state separately. No outcome may
be inferred from health samples solely because command 28 was inactive.

### Automatic block exit latch and native release candidate (13 August 2026)

The first live gate separated command lifetime from native defensive lifetime.
In `run-20260812-140353`, entering the captured Pipe pose emitted
`active=1 entered=1` and command-28 value 1; Retail block sound and haptics
were observed. Leaving emitted `active=0 exited=1` and command-28 value 0, but
the tester remained able to block while outside the pose. This is **live
verified** and rejects the original assumption that releasing the binding also
ends Retail defense. The preserved loader checkpoint has SHA-256
`CBF33EF30D040A45BE77497CB25D9473D7ABB0BABE7B4858D0E346FD2203E317`.

Bounded static analysis of the identity-verified Steam GameClient 1.0.314.0
module explains the latch. `PlayerManager::CommandOff` is GameOrig RVA
`0x000A1B30`; command 28 selects case 3, whose target returns false without
an action. The weapon block path is `CClientWeapon::Block` at RVA
`0x00028DD0`, with `HandlingAnimationStimulus` at `0x00026B80`,
`ActiveAnimationStimulus` at `0x00026C40`, `CS_Block` at
`0x0013B088`, and `CA_BlockCancel` at `0x0014D3F4`. These addresses are
guarded by exact surrounding bytes, vtable targets, call targets, and strings;
unknown identities fail closed. The presence of `CA_BlockCancel` and the
state-aware `CS_Block` dispatch support a second-stimulus cancellation
hypothesis, but do not by themselves prove the live selected action.

The follow-up is **implemented and automated-tested, awaiting live
validation**:

- Automatic pose entry owns native block only when neither controller nor
  original Retail binding input already owns it. Ownership records the stable
  weapon index and exact weapon pointer.
- Pose exit queues one post-Retail-update release. The handoff is consumed only
  for that same readable weapon and only when its verified block vtable slot
  still matches. A weapon change drops the stale request.
- The loader first asks whether that weapon is handling `CS_Block`. Only a
  true result permits one second call through the verified
  `CClientWeapon::Block` path, expected to select `CA_BlockCancel`.
  Already-inactive/rejected entries do not receive a speculative pulse.
- Manual left-trigger or original Retail binding input takes ownership and
  suppresses automatic cancellation. No raw write is made to the observed
  weapon state at `+0x218`.
- Diagnostics distinguish queued, skipped, failed, and
  `m5_physical_melee_block_native_release_handoff` outcomes, including
  before/after handling, active-stimulus, state, and native return values.

The full build passes 22/22 x86 and 18/18 x64 tests plus the schema-v4 watcher.
The built and canonical-stage `GameClient.dll` are byte-identical at SHA-256
`0D2E0068984C9D00CF956F782331AB03C0102C9FEC9015D7850C4514FF47E83F`.
Automated tests prove one-shot lifecycle and ownership guards only. A headset
run must still prove that the handoff actually ends defense, does not start
another visible block animation, and allows repeated pose entry/exit.

### Native-release rejection and collision-lifetime separation (13 August 2026)

The next live gate, `run-20260812-150308`, rejects that same-weapon release
candidate without exercising its speculative engine call. Three independent
pose entries acquired automatic ownership and three exits queued release. On
every exit, `HandlingAnimationStimulus("CS_Block")` and
`ActiveAnimationStimulus` were already false, so the guard emitted
`result=already_inactive_or_entry_rejected` with `engine_handoff=none`. The
tester nevertheless remained able to block outside the pose. This is **live
verified** for staged loader SHA-256
`0D2E0068984C9D00CF956F782331AB03C0102C9FEC9015D7850C4514FF47E83F`.
It proves that the short `CS_Block` animation stimulus is not the authoritative
defensive lifetime. It does not prove that an unconditional second stimulus
would cancel, and that experiment is now disabled rather than broadened.

The preserved run is
`stage/condemned-m2-mono/logs/run-20260812-150308`. Its loader log is 614,659
bytes with SHA-256
`149B66FB47B117E9F4755C3A7173186D516F5A73E1946408CD70B6A6955C1292`;
the launch report SHA-256 is
`254F5DAF5E541AE82275009EDD54733A0A06E195458F004860B4AA1E96828D75`.

That run also provides the decisive block/attack distinction. Immediately
after each automatic pose entry, Retail called `EnableCollisions` on the
player controller with tracker/attack index `0x15`, collider record
`0x001AE3A4`, and fifth argument `1`. A later ordinary player attack seed used
the same tracker, collider record `0x001AE3A5`, and fifth argument `0`. The
identity-verified Condemned function at RVA `0x0001FD00` reads the fifth
argument as `bBlocking`; its exact record selector uses two records beginning
at controller `+0x18` with stride `0x60`, and a blocking record skips the
attack-notifier registration stored at record `+0x44`. The public F.E.A.R. SDK
header/source is compatible ancestral evidence for that signature, but the
Condemned bytes and live arguments are authoritative here.

The prior physical-melee update extended the expiration of every active,
player-owned collision record while contact damage was enabled. It did not
distinguish the live `bBlocking=1` record from the `bBlocking=0` attack record.
Consequently the automatic block seed received the same effectively continuous
lifetime required by physical attack contact, explaining why defense survived
both pose exit and the already-finished animation stimulus.

The correction is **implemented and automated-tested, awaiting live
validation**:

- The hook snapshots the exact record Retail will select, then classifies the
  created player record from the verified fifth argument only after a matching
  active record mutation is observed.
- Classification is bound to controller, slot, record, source object,
  collision object, and tracker index. Weapon/model changes clear it. Unknown
  or mismatched records fail closed to Retail lifetime.
- Only a positively classified non-blocking attack may receive the continuous
  contact-damage lifetime. A classified block remains entirely under Retail's
  finite expiration and teardown.
- Diagnostics report `blocking_argument`, `role`, notifier, lifetime policy,
  and per-update `continuous_lifetime`. The startup gate verifies the record
  selector and both `bBlocking` branches byte-for-byte.
- Pose exit now records
  `m5_physical_melee_block_pose_exit_retail_owned` with
  `engine_handoff=none`; it never calls the rejected second-`CS_Block` path.

The full RelWithDebInfo gate passes 22/22 x86 and 18/18 x64 tests plus the
schema-v4 watcher. Built and canonical-stage `GameClient.dll` are
byte-identical at SHA-256
`C44ABD8267A861E2F55714E31233880A50E25939AC48DB0B3703828F09ADAFDF`.
Automated coverage proves the attack-only decision and fail-closed guards; it
cannot prove live Retail teardown, visible defense ending, or preserved
physical damage. Those remain the next headset gate.

### Dedicated block collider and timing tools (automated only, 13 August 2026)

The follow-on candidate separates player attack and block geometry without
guessing another engine object or replacing Retail collision. It builds on the
already verified `EnableCollisions` signature and `bBlocking` classifier:

- each stable weapon index may store a version-1 `block_collider` record with
  the same bounded controller-local position, rotation, length, radius, and
  direction fields as `collider`;
- when that record is absent or rejected, the runtime reads the weapon's
  current attack collider on every request. Thus the initial block capsule is
  exactly the current attack capsule and continues following attack-collider
  edits until the first explicit Block Col edit creates a dedicated record;
- at `EnableCollisions`, only a player-owned, supported one-handed record with
  `bBlocking=true` substitutes the dedicated capsule dimensions. During
  `UpdateCollision`, the positively classified block role reprojects that
  capsule onto the same fresh weighted grip pose before the existing native
  transform handoff. Attack and unknown roles keep their existing geometry;
- a dedicated block collider is rebased with a later hand-parented held-object
  alignment. An inherited block collider remains inherited and follows the
  rebased attack collider instead of being silently materialised as a new
  record.

The Block tab now adds `CUSTOM BLOCK WINDOW` and `BLOCK WINDOW`. Timing is a
separate version-1 `block_timing` record. Override defaults OFF; the displayed
450 ms value is inert until enabled. Enabled values are bounded to 100--2000 ms
in 25 ms steps. The hook accepts a replacement only for a finite positive
Retail duration no greater than ten seconds, a current player-owned supported
weapon, active gameplay, fresh physical context, and `bBlocking=true`.
`m5_physical_melee_block_window` records the original `retail_ms`, actual
`applied_ms`, and override decision. This uses the verified float duration
argument; it does not invent a weapon-state offset or force early teardown.

Debug visibility is now three independent global preferences:
`DRAW ATTACK COLLIDER`, `DRAW BLOCK COLLIDER`, and `DRAW CONTROLLERS`. Attack
retains amber-preview/green-live. Block uses blue-preview/cyan-live and a
role-specific live collision tick, so a block seed cannot make the attack
capsule look live. `[debug] draw` is version 2 (`attack,block,controllers`);
version 1 still loads with its original attack/controller meaning and block
drawing safely OFF. Compiled and packaged defaults keep all three hidden.

Headset-free coverage proves bounded timing transitions, Retail-default timing,
window resolution, weighted-pose reprojection, attack/block palette separation,
block-collider and timing persistence/profile rejection, missing-record
distinction, version-1 debug migration, version-2 round trips, and the ordinary
fail-closed gates. The normal build passes 22/22 x86 and 18/18 x64 tests plus
launcher-focus and schema-v4 watcher regressions. The unstaged x86 loader built
from the current dirty worktree has SHA-256
`B7C4DE41F9D80D13CF6C7C1D7909A99EFC4EE9B450786C85FBF4759AFF8D055F`.
Per the tester's explicit pause, no game/headset run or runnable-stage DLL
replacement was performed. Native interception, visible timing, debug colors,
and menu ergonomics therefore remain unverified live behavior.

### Tool-menu overlay vertex-cap regression (live accepted, 13 August 2026)

The first later launch, `run-20260813-034938`, reached the canonical OpenXR
transport with Pipe index 32 and all three debug drawings OFF. The tester
reported that one of the two Block-labelled tabs and Debug made the menu
disappear. The input/state log separates this from an accidental close:
`m5_vr_tool_menu_changed` remained `open=1 tab=BLOCK`, immediately followed
by `m5_vr_tool_menu_overlay_failed bridge_draw_rejected=1`. Navigation then
continued through Block Col, Weapon, Debug, Controls, and back while the menu
state remained open. No block-collision acceptance claim comes from this run.

The failure was a deterministic producer/consumer limit mismatch. The menu
owned a 32,768-vertex buffer, but `DrawOverlayTriangles` rejected calls above
24,576 vertices. Headset-free construction of representative complete pages
measures 29,820 vertices for Block and 27,402 for Debug, while Block Col uses
21,876. This reproduces the exact visible/invisible split reported in headset.

The corrective candidate defines one in-process overlay-triangle cap of 32,768
in the shared protocol header and consumes it from both the menu buffer and
D3D9 bridge. A rejected draw now logs its vertex count and limit. The portable
menu regression builds complete Block, Block Col, and Debug pages, requires
them to fit the shared cap, and also requires the fixtures to reproduce the
former Block/Debug-only rejection. The full corrected gate passes 22/22 x86
and 18/18 x64 tests plus launcher-focus and schema-v4 watcher regressions.

The exact corrected stage has SHA-256
`0942BFD65C726FD0A27BB46AF0C2342A7220C3A6196D961114886C79F04D4705`
for `GameClient.dll` and
`D188B8DFB75BB134E6654A94CF6DBD33CC38C982C8E75806DFBACAF7A232BCD7`
for `condemnedvr-d3d9.dll`. Canonical live run
`run-20260813-040058` recorded 18 Block, 15 Block Col, and 16 Debug
navigations, all with `open=1`, plus ten Debug value adjustments and zero
`m5_vr_tool_menu_overlay_failed` events. The tester confirmed all affected
tabs remained visible and interactive. This accepts the shared-cap menu fix;
it does not by itself accept block-collider placement, debug capsule colors,
block timing, or native collision expiration.

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
`-StereoTuning`, `-HeadAimProbe`, and the verified save/load state source from
`-MenuProbe`. It installs one callback on `Right_hand` and solves the authored
`RightHand` socket exactly onto the same fresh, weighted VR weapon pose already
shared by the visible model and collision path. It does not yet rotate
`Right_armu` or `Right_arml`; an expected disconnected-looking arm is therefore
useful evidence during this isolated socket/alignment test.

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

### Save/restore callback-lifecycle hypothesis

**Hypothesis, not yet a verified root cause:** after loading or restoring a
save, both hands can fall back to Retail animation even though controller input,
weapon pose, and per-weapon IK settings resume. The integration captured in the
failing observation sampled the player-body manager only once per 60
world-camera calls and treated the raw `m_hPlayerBody` pointer as the
installed callback lifetime. When both cached arm records are marked installed
and that pointer is unchanged, the sampler returns without registering the six
node controls again.

A working-session loader log supports this diagnosis but has not yet been
preserved as a complete fresh-session evidence package. Both arms installed and
became active on player body `0x0CEC2708`; the game later traversed
`menu -> screen -> loading -> screen -> playing`, with no intervening
`arm_ik_*_released` or second `arm_ik_*_installed` event. Fresh input and the
right-hand IK settings load resumed afterward, while later player-owned native
activity again used `0x0CEC2708`. This is consistent with Retail clearing or
rebuilding the model's node-control state while preserving or reusing the same
object address. It does not yet distinguish an in-place skeleton reset from
destroy-and-recreate address reuse.

At the time of that observation, the Hand IK, Left IK, and Elbow status rows
could not disprove this failure. Their `ACTIVE` result checked only the cached
`installed` flags and non-null player-body pointer; it had no callback
heartbeat. The loader could therefore report an apparently active integration
after Retail stopped invoking its callbacks. The expected menu-time
`tracked_weapon_pose_not_fresh` event is a separate temporary fallback and does
not explain a persistent post-load loss once fresh controller input resumes.

The smallest candidate fix is to make a Retail game-state lifecycle generation,
not pointer inequality, authoritative:

1. On the transition out of gameplay toward a load, invalidate both targets,
   bend histories, and installed arm-control records. Remove the old callbacks
   in child-to-parent order when the old model still passes the existing live
   guard; otherwise clear only mod-owned state and let Retail own destruction.
2. After gameplay resumes and a live player body is stable, resolve the nodes,
   remeasure both chains, and transactionally register all six controls even if
   `m_hPlayerBody` has the same numeric value as before the load.
3. Add a per-side callback heartbeat and lifecycle generation to the bounded
   diagnostics. Menu status should mean that callbacks have executed recently
   for the current generation, not merely that registration once succeeded.
4. Preserve the existing fresh-pose, finite-value, model-identity, and complete
   two-arm installation gates. Any failed reinstallation must leave both arms
   on Retail behavior rather than a partially controlled skeleton.

#### Candidate implementation status (2026-08-11)

**Implemented, awaiting live validation:** the x86 integration now consumes the
already verified `CInterfaceMgr` state from the menu/update hook. The first
witnessed `Loading` state advances an arm-IK lifecycle generation, invalidates
both pose targets, clears both bend/control records, and attempts child-to-parent
removal only while each old model passes the existing live-object guard.
Intermediate `Screen`/menu states cannot reinstall. The next `Playing` state
requests an immediate render-path sample, which re-resolves and remeasures both
chains and permits a transactional six-control registration even when the
player-body address is numerically unchanged.

Each successful hand callback now advances a per-side heartbeat tagged with the
installed generation. The Hand IK, Left IK, and Elbow `ACTIVE` paths require a
recent heartbeat from the current generation instead of cached registration
alone. Bounded `arm_ik_lifecycle_invalidated`,
`arm_ik_lifecycle_resume_pending`, and `arm_ik_callback_heartbeat` events expose
the transition, removal result, and post-install callback proof. The supported
launcher requires `-MenuProbe` for either IK mutation gate, and the loader
rejects a raw IK request if that verified lifecycle observer did not arm.

The portable lifecycle state machine passes x86 and x64 automated tests, and
the modified x86 loader builds. This does not prove the reported headset failure
is fixed. Confirm the root-cause hypothesis and candidate in one fresh headset
session, record callback heartbeats before and after loading the same save, then
repeat death/respawn and a level transition. The failure is confirmed if the
body address and cached installed flags remain stable while callback heartbeats
stop. A candidate fix passes only when every lifecycle discontinuity produces a
new generation, both three-node chains reinstall once, fresh heartbeats resume,
and both hands visibly follow their controllers without stale or duplicate
callbacks. Pause/resume, weapon switching, tracking loss, host absence, and
non-VR Retail behavior remain required regressions.

### Empty-hand right-grip alignment hypothesis

**Reported headset symptom, source-supported hypothesis:** with no item
equipped, the right hand is visibly twisted relative to the right controller;
the free left hand remains correctly aligned. This is distinct from the
save/restore callback-loss report because the right IK callback is still
producing a hand pose, but its orientation basis appears wrong.

The current render path resolves separate right aim and grip poses, then builds
the shared weapon target from `controllerGrip.worldPosition` and
`controllerAim.worldRotation`. The right-hand IK target inherits that weighted
weapon rotation. When no live equipped-model source exists,
`g_physicalMeleeVisualWeaponIndex` is `-1` and the right-hand IK settings path
returns zero offsets, so no per-weapon calibration masks the aim-to-grip
rotation difference. The free left-hand path instead uses
`secondaryGripWorldPosition` and `secondaryGripWorldRotation`; its support-hand
resolver returns the normalized grip rotation while detached.

This matches the donor integration's recorded failure mode: mixing grip
position with aim-pose rotation visibly twisted a hand, while using the grip
pose for both corrected it. It also matches the repository coordinate contract:
the grip pose is the held-item attachment frame and the aim pose is a separate
pointing frame. This is therefore not evidence for changing the centralized
OpenXR-to-LithTech conversion or adding a local sign flip.

The smallest candidate fix is an explicit target-source branch:

1. Classify an equipped hand from a current, lifetime-validated weapon/model
   source rather than from a stale index alone. Treat Retail `Unarmed` and a
   missing current weapon/model as empty-hand states.
2. In the empty-hand state, publish the raw right grip world position and grip
   world rotation directly to the right-hand socket target. Require only a
   fresh, finite right grip pose; do not require aim-pose validity.
3. Bypass weapon-weight filtering and per-weapon Hand IK correction for that
   empty-hand target. Keep `socketFromNode` authoritative for the Retail
   hand-to-`RightHand` socket relationship, and do not create a magic
   weapon-index `-1` calibration record.
4. Preserve the current weighted weapon/aim target plus its per-weapon
   correction while a verified item is equipped. Reset bend continuity and
   weapon-weight state when crossing the equipped/empty basis boundary.

The pre-implementation requirement was a bounded target-source record containing
equipped index, live model-source state, selected source
(`empty_grip` or `weapon_weighted_aim`), right grip and aim quaternions, their
finite angular difference, and final socket target. In headset, exercise right
controller yaw, pitch, and roll while empty, equip one accepted calibrated
weapon, then return to empty hands. The candidate passes when the empty right
palm follows the controller like the free left hand, transitions do not snap or
retain a weapon correction, the equipped weapon retains its accepted aim and
calibration, and stale grip tracking returns the right hand to Retail behavior.

**Implemented and partially live verified (11 August 2026):** the candidate
branch now selects `empty_grip` from a fresh, finite right grip pose whenever
there is no lifetime-valid held weapon/model source or Retail reports
`Unarmed`. It bypasses two-hand solving, weapon weight, aim rotation, and
per-weapon Hand IK correction in that state. A verified held model retains the
existing `weapon_weighted_aim` path. Source/index/generation transitions reset
bend, weight, and support-grip history. Portable tests cover empty/equipped
selection, stale and malformed grip rejection, and basis transitions.

The empty-hand view of the **Hand IK** tab is a separate two-action guided
calibration rather than a weapon-index record. `START GUIDED EMPTY-HAND
ALIGNMENT` release-arms the right trigger. The first pull captures the
currently displayed hand after the player makes it look visually correct; the
second pull captures the raw grip pose after the physical controller is moved
to the desired comfortable pose. The solver computes the controller-local
transform `C` in `controller_pose * C = reference_hand_pose`, applies it in the
same frame, resets bend continuity, and saves it as versioned
`[arm_ik] empty_right_hand` data. `RESET EMPTY-HAND ALIGNMENT` restores and
saves identity. While the mode is active, both trigger tab bindings remain
latched but suppressed, the menu captures gameplay input, A cancels from the
start row, and closing the menu, losing focus, or equipping a weapon discards
only the unfinished capture.

Malformed, stale, non-finite, or over-100-unit solves fail closed and cannot
replace the last valid setting. Tracking loss also requires another complete
trigger release, preventing a held trigger from becoming a delayed capture.
`m5_empty_right_hand_alignment` records start, reference, solve/rejection,
controller/reference transforms, solved offset, and persistence result;
`m5_right_hand_ik_target_source` now distinguishes global empty-hand correction
from per-weapon correction. The automated gate passes 19/19 x86 and 15/15 x64.
Live run `run-20260811-115639` staged loader SHA-256
`62D2BFC48E735177466570C6E71ACE623270AEDF077E82F9DB6CBEA2E2A0B4D5`.
It recorded a deterministic reference capture followed by a completed solve
with `persistence=ok`; the player INI contains position
`(-4.454854, 3.542406, -2.338000)` and quaternion
`(0.476682, -0.571406, -0.385630, 0.545490)`. The headset tester reported
that the resulting empty-hand alignment "works perfectly." This accepts the
raw-grip target, two-pull interaction, immediate application, and live write.
Fresh-process reload, deliberate tracking loss during capture, and the
equipped/empty transition remain narrower regression gates in `TESTING.md`.

### Guided held-object alignment candidate

**Live verified in-session for five held assets; restart and dependent
regressions pending (11 August 2026):** every lifetime-valid held model can use
the same two-capture interaction as the accepted empty hand, without conflating
the two local transforms that make up a held assembly.

The final **Grip** row is `START/CANCEL GUIDED OBJECT + HAND ALIGNMENT`.
The first release-gated right-trigger pull captures the currently displayed
model transform `O` and right-hand target `H` after the player makes the
assembly look right. The second captures the desired raw controller/weapon
basis `D` after the controller is moved to its natural physical grip. The
solver derives:

- absolute model-local grip `G = inverse(O) * D`, preserving
  `O = D * inverse(G)`; and
- per-index right-hand correction `C = inverse(D) * H`, preserving `H = D * C`.

Both are applied together so moving the model cannot leave the rendered hand
behind. The model solution is converted to the existing absolute grip position
plus local Euler correction over the authored base rotation. The hand solution
is converted to the existing per-index Hand-IK position/Euler record. Successful
completion writes `grip` and `right_hand_ik` under the same stable Retail index;
secondary-grip enable/offset/radius remain unchanged, and the global empty-hand
record is untouched. Manual Grip and Hand-IK adjustments and Reset remain
available and continue to save immediately; the former explicit Grip snapshot
was redundant with that auto-save behavior.

While this guided mode is active, the raw grip-position/aim-rotation basis
temporarily bypasses weight and two-hand solving so inertia cannot contaminate
a calibration sample. Both trigger tab bindings and stick adjustments are
suppressed while their latches continue tracking; A cancels, and B/menu close
discard the unfinished state. A trigger capture is accepted only after the
exact render-only model transform was set and read back successfully in that
frame. Fresh tracking, finite transforms, the original Retail index, and the
same live model generation are mandatory. Source change cancels, tracking loss
requires another full trigger release, and over-range or non-recomposable
quaternion/Euler solutions leave the prior settings intact.

The guided write deliberately does not modify the separate per-index
`collider` record. That swept capsule is expressed in the weighted controller's
local space, not the Retail model's Grip transform. After visually realigning a
melee item, use the existing Collider tab and wireframe to verify or tune its
damage volume independently. Model-derived forensic-camera and firearm
direction paths also remain explicit live regression gates rather than being
accepted by the algebra or portable tests.

`m5_guided_held_object_alignment` records start/cancel, both reference poses,
controller pose, source identity/generation, solved grip/hand transforms, and
both persistence results. Portable tests cover model-equation round trip,
ordinary and gimbal quaternion/Euler round trip, bounds rejection, two-pull
release gating, tracking recovery, and source-generation invalidation. The full
gate passes 19/19 x86 and 15/15 x64. Built and staged loader SHA-256 is
`0CF5043043D3D4AF00321F5A718C661D072B91B4588378C2E779ED5050848D79`.

Canonical VDXR run `run-20260811-130418` completed the entire release-gated
two-trigger sequence on five distinct stable Retail identities. Every completion
reported `grip_persistence=ok` and `hand_persistence=ok`; no partial save or
solve rejection occurred. The tester reported, "Its sooooo much easiser to
align weapons now." The persisted results from this run are:

| Index / identity | Grip position / rotation degrees | Right-hand position / rotation degrees |
|---|---|---|
| 46 `Scanner` | `(-0.907,-0.274,-1.555) / (136.186,34.256,117.866)` | `(-0.826,1.788,-0.041) / (-179.056,-53.050,134.878)` |
| 4 `cell_phone` | `(0.258,-4.402,-2.084) / (107.900,69.008,108.788)` | `(-0.423,4.847,-0.332) / (154.657,-82.197,-171.328)` |
| 3 `Camera` | `(-0.317,0.776,0.339) / (178.840,74.685,-178.499)` | `(0.249,-0.792,0.359) / (177.702,-74.660,-177.399)` |
| 76 `colt45_Unbreakable` | `(3.630,1.723,-7.606) / (165.034,70.934,-177.826)` | `(-6.282,0.734,-7.801) / (137.193,-55.241,-137.170)` |
| 77 `colt45_Melee_Unbreakable` | `(0.059,-3.047,-8.049) / (161.840,87.226,-174.080)` | `(-7.545,0.172,-0.120) / (-178.688,-89.355,-172.103)` |

At the user's instruction, these exact five Grip/right-hand pairs were promoted
into `config/condemnedvr-defaults.ini` on 11 August 2026. No handling, secondary
grip, or collider value was promoted from this action. Guided completion emitted
no collider save; indices 3, 4, 46, and 76 retain their existing/default
collider records, index 77 still has no packaged collider override, and the
Pipe index-32 block remains unchanged. Source, generated x86, and staged copies
are byte-identical at SHA-256
`6ADA21EF6DED26FA929FE53B95431568F9C172F7382FA751C0F0C4086CE3F39A`.
The promotion build passed 19/19 x86 and 15/15 x64 tests, and the publication
audit reported no forbidden paths or credential patterns.

The preserved 520,952-byte loader trace is
`stage/condemned-m2-mono/logs/run-20260811-130418/`
`condemnedvr-loader-held-object-alignment-success.log` (SHA-256
`2F9B0E07751AEC790D19B3D322A6F129EF1AEC286154F0746BB13AF6B5AB38F1`).
This live accepts the in-session capture, solve, combined application, two-record
write, and usability for those five identities. Fresh-process reload, deliberate
tracking/source-change cancellation, the forensic camera/photo and firearm
direction/fire regressions under the new values, and all remaining assets stay
open in `TESTING.md`.

### Fresh-process contradiction and hand-parented correction

**Implemented and automated-tested; live validation intentionally held
(12 August 2026):** a later index-76 calibration/restart check contradicted the
assumption that the in-session guided mechanism above had captured a valid
held assembly. After relaunch, the tester reported that the right hand was
again misoriented, the guided tool did not repair its orientation, and the
weapon did not align to the controller.

This was not a missing-load result. The restart trace recorded successful
per-index Grip and right-hand settings loads, an active visual proxy, and a
consistent 60.000-degree grip/aim rotation difference on valid samples. The
loaded index-76 values were Grip position
`(-3.006,-3.300,-16.200)`, rotation
`(-177.438,86.465,-174.250)` degrees, and right-hand position
`(-13.655,1.998,9.694)`, rotation
`(3.339,-5.168,-4.684)` degrees. The preserved artifacts are:

- `stage/condemned-m2-mono/logs/run-20260811-141552/`
  `condemnedvr-loader-held-object-alignment-restart-failure.log`,
  157,399 bytes, SHA-256
  `80D496B4B1E7194F7DCAFF0B94106CF051CD3D532ACA258888491E8487DB088E`;
- the accompanying `weapon-settings-restart-failure.ini`, SHA-256
  `E5F23F0C402FA496B954BCFDCC3CD0A969A2762D0D120E96AA895B1B6BDE294F`.

Reconstructing the two logged captures isolates the interaction error. The
previous accepted index-76 object-to-hand relation was approximately
`(-1.567,0.219,0.855)` units with 9.519 degrees of rotation. The failed
capture stored approximately `(7.381,-0.683,-3.173)` units with 88.192
degrees, a change of about 9.85 units and 94.28 degrees. Capture one had saved
the already-wrong displayed hand and capture two intentionally preserved that
first hand. The model also continued following the controller after capture
one, so the on-screen interaction did not match the claimed freeze. This
explains why two successful saves could faithfully reproduce an incorrect
assembly after restart.

The correction treats the final hand as the attachment parent while preserving
the existing controller-driven renderer. For controller basis `D`,
model-local Grip `G`, and controller-local hand correction `C`:

```text
object O = D * inverse(G)
hand   H = D * C
fixed model-to-hand attachment A = inverse(O) * H = G * C
```

When Hand IK changes from `C0` to `C1`, the candidate computes
`G1 = G0 * C0 * inverse(C1)`. Consequently `G1 * C1 = G0 * C0` and the
weapon follows every hand position/rotation edit instead of separating from
it. The melee collider remains controller-local for runtime contact, but its
model-relative placement is now preserved too. For collider frame `T`, the
candidate computes `T1 = inverse(G1) * G0 * T0`, keeping
`G1 * T1 = G0 * T0`. The visible-firearm reconstruction already consumes
the saved `G`, so no separate firearm sign or axis correction was introduced.

The revised guided interaction now behaves as displayed:

1. Step one lets the player position the weapon and captures/freezes only
   `O`.
2. During step two, the weapon stays frozen while the globally calibrated
   raw-grip right hand follows the controller into the natural model grip.
3. The second trigger solves `G` from the frozen object and current hybrid
   grip-position/aim-rotation basis, solves `C` from the current visible
   hand rather than the discarded first hand, rebases `T`, then applies and
   separately saves Grip, Hand IK, and Collider.
4. Manual per-index Hand IK position/rotation edits use the same coupled
   solve, so the weapon and collider move with the hand immediately.

New diagnostics identify `guided_hand_placement=1` during the second phase
and emit `m5_held_object_attachment_applied` with
`model_to_hand_preserved=1`, `collider_model_relation_preserved=1`, and
all three persistence results. Portable tests deliberately supply a wrong
first hand, require the second/current hand to win, verify
`G0*C0 == G1*C1`, and verify `G0*T0 == G1*T1`. The complete automated gate
passes 20/20 x86 and 16/16 x64. The standard build manifest completed and
the built/staged x86 loader copies are byte-identical at SHA-256
`4863C9E0389A2049AA6CA9DD6376B96588A4596E42F218ED6FF1700635F11DEB`.
The staged defaults remain unchanged at SHA-256
`6ADA21EF6DED26FA929FE53B95431568F9C172F7382FA751C0F0C4086CE3F39A`.
These tests prove the algebra and guards only; they do not prove the render
freeze, live skeleton response, contact volume, camera/fire direction, or
restart behavior. At the tester's request, no live game/headset test has been
launched for this candidate.
### Reset-fit evidence and one-button assembly alignment

**Reset relationship live verified for index 76; replacement action
automated-tested only (12 August 2026):** the follow-up session
`run-20260811-144530` ran the hand-parented build above and was closed before
the one-button replacement was built. The source stayed index 76, its Grip and
Hand IK records loaded successfully, and valid target samples retained the
expected 60.000-degree grip/aim rotation difference. One coupled Hand IK edit
and the later advanced completion each emitted successful Grip, Hand, and
Collider writes. Those events prove the live paths executed; they are not by
themselves visual acceptance.

The decisive reset observation came after the tester reset Grip and Hand IK.
The next reference capture recorded all three displayed/driver transforms at
exactly the same pose:

```text
object O position = (2156.377,-2309.698,2312.275)
hand   H position = (2156.377,-2309.698,2312.275)
driver D position = (2156.377,-2309.698,2312.275)
O quaternion = H quaternion = D quaternion
             = (0.054115,0.758500,0.047660,-0.647671)
```

The tester separately observed that with all values reset the gun already sat
perfectly in the hand. This is live evidence that index 76's authored/reset
model-to-hand attachment is correct and that the second freehand placement
adds unnecessary degrees of freedom for this normal case. It does not prove
that every item has the same authored relationship. Preserve:

- `stage/condemned-m2-mono/logs/run-20260811-144530/`
  `condemnedvr-loader-reset-fit-and-guided-alignment.log`, 244,316 bytes,
  SHA-256
  `78810AA7F35FAED1FBE5EFF05DE6F7199450FB8832F6E64158200CEDE0DB99E5`;
- `weapon-settings-after-reset-fit-session.ini`, SHA-256
  `66709385464EC4AE5A4C613027B1B0FD28F6C5120D70B47D8000BF27162CBACC`.

The new primary **Grip** action therefore uses one coherent prior-frame input
sample. Let `R` be the raw right-grip world pose, `E` the accepted global
empty-hand correction, and `D` the unweighted hybrid driver made from raw grip
position plus raw aim rotation. It computes:

```text
desired hand Hc = R * E
C1 = inverse(D) * Hc
G1 = G0 * C0 * inverse(C1)
T1 = inverse(G1) * G0 * T0
```

`G1*C1 = G0*C0` preserves the current gun-in-hand attachment, while
`G1*T1 = G0*T0` preserves the collider's model-relative placement. The raw
grip and aim caches must be fresh, finite, nonzero, and carry the same sample
ID and predicted-display timestamp. The action never samples the
weight-smoothed weapon pose, so spring lag cannot become persistent alignment.
Current index and source generation are rechecked while applying. Success
updates and separately saves Grip, Hand IK, and Collider and emits
`m5_align_held_assembly_to_controller` plus
`m5_held_object_attachment_applied`. Any failed gate leaves prior values
authoritative.

The selectable row is `ALIGN HAND + WEAPON TO CONTROLLER`. The two-trigger
freeze interaction remains `ADVANCED: FROZEN TWO-POSE ALIGNMENT` for an item
whose authored model-to-hand relationship is genuinely wrong. Portable tests
cover reset `G=C=identity` with a 60-degree grip/aim disagreement, a
non-identity `G0*C0` attachment, invalid global correction rejection, and the
expanded menu/overlay budget. The full build passes 20/20 x86 and 16/16 x64.
The project-local stage was refreshed from branch
`feature/two-handed-weapons`, base commit
`93dab5242e52c4c4d56453b3464001a256ff9262`; the manifest correctly marks
the source dirty. Built and staged x86 loaders are SHA-256
`2D44888071EA3D360E9A7FB822CBC5EDD5BB6C8DBB1E4DD9AE77FCE1D4237A9E`.
Source and staged defaults remain SHA-256
`6ADA21EF6DED26FA929FE53B95431568F9C172F7382FA751C0F0C4086CE3F39A`.
Per the tester's instruction, the game and host remain closed and all live
validation of this binary is on hold.
### One-press five-identity live acceptance

**Live verified in-session (12 August 2026):** after the hold was explicitly
lifted, canonical run `run-20260811-152219` launched the Pipe weapon-test
preset with staged x86 loader SHA-256
`2D44888071EA3D360E9A7FB822CBC5EDD5BB6C8DBB1E4DD9AE77FCE1D4237A9E`.
The runtime was VirtualDesktopXR 1.0.10 on a Meta Quest 3. Game PID 22708 and
host PID 48108 both exited before evidence preservation.

The tester exercised the primary row across all five previously calibrated
representatives:

| Retail index | Identity | One-press applications |
|---:|---|---:|
| 76 | `colt45_Unbreakable` | 8 |
| 46 | Scanner | 3 |
| 4 | `cell_phone` | 5 |
| 3 | Item Camera | 2 |
| 32 | Pipe | 1 |

All 19 `m5_align_held_assembly_to_controller` events reported `applied`,
`raw_pose_fresh_same_sample=1`, nonzero sample/timestamp values, and the
expected 60.000-degree raw grip-to-aim difference. Every paired
`m5_held_object_attachment_applied` event retained
`model_to_hand_preserved=1` and
`collider_model_relation_preserved=1`; Grip, Hand IK, and Collider persistence
were `ok/ok/ok` in every case. Repeated presses without an intervening edit
resolved back to the existing values rather than accumulating pose drift.

The tester judged the result good enough to keep using until a concrete issue
appears. This live accepts the row's immediate visual usability, repeat use,
same-frame sampling, source-lifetime application, and three-record write for
these five identities. Pipe was subsequently adjusted with the advanced
two-pose mode, so its final settings snapshot is not an isolated one-press
result. This run does not yet accept restart reload, deliberately stale input,
source-generation cancellation, the collider's visible/damage location after
alignment, forensic optical regressions, index-76 firing, or any untested
identity.

Preserved checkpoint artifacts:

- `condemnedvr-loader-one-press-alignment-accepted.log`, 1,090,914 bytes,
  SHA-256
  `90D6F718892EAC82C4E76E9971C989AB4A7A668805568AB983DD9D8FCDD101A9`;
- `weapon-settings-after-one-press-alignment.ini`, 2,504 bytes, SHA-256
  `07B4F52D6ED87265F83CB23285B6DF4289920063279E8E157243A7E2A3097CDC`;
- `m2-mono-live.json`, SHA-256
  `908F43B0A64CC6E86A6F5AC22791673D1BA2850CA4C3A265337F4D1C68F93F39`;
- host log SHA-256
  `F7EEEABEC1EF410CBB1DCE3B92202A4A171DCE1EB30D1D270392C01A99228A77`;
- bridge log SHA-256
  `AEFB303731ECB2EA05EE38D0055DE8F86A9A47B919EA1C01B7D7614035AD8951`.

All live files are under
`stage/condemned-m2-mono/logs/run-20260811-152219/`.

### Controller-facing contradiction and automatic forward-hand correction

**Implemented and automated-tested; awaiting live validation (20 August
2026):** a later headset tester report supplied the concrete issue left open
by the earlier broad usability judgment: after selecting `ALIGN HAND + WEAPON
TO CONTROLLER`, the Colt remained rigidly attached to the hand but did not
face the controller direction. No fresh run directory or structured event
stream accompanies that report, so it does not invalidate the 12 August
same-sample, persistence, or repeat-use evidence. It does invalidate treating
those successful events as proof that the weapon frame itself was constrained
to controller aim.

The tester then supplied the decisive reset observation: with Grip and Hand IK
both zero, the Colt and hand are perfectly aligned to each other, but both
point in the wrong direction. The tester explicitly requires automatic
alignment, not another frozen/manual workflow. This proves the authored reset
hand-to-gun relationship is the known-good constraint; the whole assembly
needs a controller-local direction correction.

Two incomplete automatic formulations are therefore rejected. Preserving the
current `A = G*C` can retain stale guided values, while forcing the authored
grip directly onto the aim-based driver `D` leaves an identity-reset Colt and
hand on the same wrong basis. The correction retains the fresh same-sample
raw grip/aim and source-lifetime gates, targets the already accepted global
empty-hand correction `E`, and carries the immutable authored/reset attachment
with it. For raw grip `R`, authored grip `Gb`, and controller driver `D`:

```text
desired hand Hc = R * E
authored reset attachment Areset = Gb * identity = Gb
C1 = inverse(D) * Hc
G1 = Gb * inverse(C1)
G1 * C1 = Gb
T1 = inverse(G1) * G0 * T0
```

For index 76, `Gb` is identity, so resolved object and hand both land on `Hc`:
the hand points in its globally calibrated forward direction and the Colt
follows it with the zero/reset fit intact. Non-identity assets carry their own
authored reset attachment instead of inheriting a Colt assumption. The action
does not start a freeze mode or require trigger capture.

`m5_align_held_assembly_to_controller` now records the authored grip and the
recomposed hand-target and authored-attachment position/rotation errors. The
action fails closed before applying or saving unless
`automatic_hand_forward_aligned=1` and both relationships reach 0.001 unit
and 0.01 degree. The paired attachment event declares
`model_to_hand_preserved=0` and `authored_reset_attachment_used=1`; stale
current attachment values are intentionally superseded. Portable coverage
requires an identity Colt-like reset assembly to follow corrected raw grip
rather than the 60-degree-displaced aim driver, covers a non-identity authored
reset fit plus global hand correction, rejects invalid input, and proves
repeat application is idempotent.

The 20 August full gate passes 25/25 x86 and 21/21 x64 CTest cases plus the
launch-profile, focus-handoff, screenshot-helper, schema-v4 diagnostics
watcher, and release-tool PowerShell regressions. Built and project-local
staged x86 loader copies are byte-identical at SHA-256
`74B2169CA45D0A8FA013AAA1ACA857473F04009FCF18FCD5F31981B63CF6AAF6`.
The manifest records base commit
`503fe3150012762403ce157d136ef047a0e687f8` with a dirty working tree. This
proves the algebra, guards, compilation, and persistence handoff only. Colt
forward-hand/weapon alignment, collider placement, firing direction, and
restart persistence remain live gates.



While free, the left target uses the raw OpenXR grip pose. On two-hand
attachment, its position becomes the authored support point reconstructed from
the final weighted weapon pose, and its current rotation is captured relative
to that same weapon pose. Position and orientation therefore remain one rigid
weapon-relative grip instead of floating or swivelling independently. Release
restores the raw controller pose. Display-only calibration is applied after
either base pose and is never fed back into the weapon solve.

When a held model is present, **Hand IK** provides the live alignment pass for
that proven socket target. The selected weapon has independent local X/Y/Z
position and
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
  adjustment step, and reset. Adjustments and reset auto-save the equipped
  Retail weapon's `grip` record. The final row starts/cancels the two-trigger
  guided object-plus-right-hand alignment. The hand-parented candidate freezes
  the weapon between captures, then saves the resulting per-index `grip` and
  `right_hand_ik` plus a rebased `collider` that preserves its location on
  the model. This tab requires the `-WeaponGripCalibration` launch option and
  shows the controller wireframe.
- **2-Hand:** enable the profile's support grip, edit its local offset and grab
  radius, capture the current left-hand pose, reset, and save a combined
  snapshot in the same `grip` record. All changes auto-save. Live attachment
  distance/error remains visible on the tab.
- **Hand IK:** adjust the rendered dominant hand's per-weapon local XYZ socket
  target and pitch/yaw/roll while seeing the result immediately. The tab has
  independent fine/coarse steps, zero-offset reset, callback status, and a
  diagnostic snapshot action. For a live held model, each edit now updates and
  saves the matching Grip and rebased Collider too, so hand, weapon, and damage
  volume remain one attachment. Physical weight/handling values are unchanged.
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
  proxy/two-hand state. Its first two rows independently show/hide the melee
  collider and controller calibration wireframes; both are global user
  preferences and save immediately.

Use the left/right triggers to change tabs, left-stick up/down to choose a row,
right-stick left/right to change a value, A to activate a row, and B to close.
The keyboard equivalents are left/right square brackets, arrow keys, Enter,
and F12. All values take effect immediately. Melee, Weapon, Grip, 2-Hand, Hand
IK, Left IK, Elbow, and Debug values are persisted automatically. The 2-Hand
snapshot row forces an additional save and diagnostic snapshot. The primary
Grip alignment row performs the one-press assembly solve; its advanced row
retains the frozen two-pose fallback. The continuous fallback
calibration path uses controller Y or keyboard P to avoid writing on every
tracking sample. Promoting a tested override into authored profile data remains
a deliberate source change.

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

## Handgun visible-barrel fire direction

The firearm candidate reuses the accepted held-model relationship instead of
aiming along raw controller +Z. Stable index 76 reconstructs the visible gun
from the current weighted weapon pose and its saved model-local grip. This is
the same `desiredGripWorld * inverse(modelLocalGrip)` relationship used by the
temporary stereo model override; it does not add a second weapon pose or a
weapon-specific sign correction.

The initial candidate required authored `Breach -> Flash` positions. Live run
`run-20260810-155025` found `Flash` handle 2 but no `Breach` on
`colt45_Unbreakable`; all nine calls safely used raw-controller fallback and
none applied the candidate. Static Retail evidence confirms that `Breach` is
optional. That run verifies the missing socket and fallback only, not handgun
alignment.

The revised candidate validates `Flash` first, prefers `Breach -> Flash`
when both transforms exist, and otherwise uses transformed authored Flash
socket +Z for direction and +Y for roll. In `run-20260811-081337`, all four
Colt shots applied that Flash +Z source with a valid stable transform and no
fallback. The tester confirmed that impacts followed the visible handgun
sights. Index-76 direction is therefore **live verified**.

This pass changes direction only. Condemned's original fire position, ammo,
fire dispatch, effects, and damage ownership remain native. The transformed
`Flash` point is diagnostic so a later live result can distinguish angular
misalignment from close-range origin/parallax. Exact current weapon/model
references, saved-source generation, tracking freshness, model-interface
identity, a finite Flash transform, and optional two-point socket geometry all
fail closed. Non-index-76 weapons retain the controller basis, and stale or
flat-screen input retains Retail behavior.

The accepted built and staged x86 DLL SHA-256 is
`5C385D018E511623E563357F4FCE82BCA689C38D1DB96C7C72405D1698F257F2`.
The direction claim remains limited to `colt45_Unbreakable`. Retail fire
origin was not moved, its 64.570--78.185-unit diagnostic separation from the
Flash point was not close-range accepted, and other firearms remain unchanged.
The detailed Retail socket evidence, failed first-candidate checkpoint,
accepted-run checkpoint, event schema, and regression procedure are recorded in
`CONDEMNED-M4.md` and `TESTING.md`.

## Automatic equip-time collision seed candidate (14 August 2026)

The current Retail collision proxy inherits one unavoidable lifecycle fact:
the player attack body does not exist until Retail creates it. Historical
headset runs used one manual attack for that creation. Calling
`EnableCollisions` directly remains unsafe because the complete five-argument
acquisition/ownership contract is not established. A genuinely seedless
lifecycle therefore belongs to the later standalone physical-item
architecture, not to an unverified direct native call.

The working tree now automates the already verified Retail creation path as a
bounded equip-time compatibility transaction:

- only an exact mapped one-handed weapon index with resolved Retail name and
  `AnimationProperty`, a stable weapon/model identity, current model token, and
  fresh tracked physical-melee pose becomes a candidate;
- after 250 ms of stable context and while ordinary Fire is idle, one 100 ms
  command-17 pulse is overlaid through the verified Retail binding-value hook;
- the pulse is seed-only. Its action haptic is withheld, its impact controller
  is bound before Retail's `EnableCollisions` call can synchronously dispatch,
  native impact forwarding is blocked, Retail target references are still
  cleared, and the physical contact latch is not mutated;
- readiness requires the existing enable hook to observe exact
  `read_mask=0x7`, positive player-owned Attack classification, and a non-zero
  collision object. Partial masks, block/unknown records, and null objects do
  not confirm the transaction;
- a confirmed body remains damage-blocked for a one-second settle, then must
  still be fresh before the phase becomes `ready`;
- confirmation times out after two seconds. Retry is delayed 750 ms and capped
  at three attempts per equip. Weapon/model changes reset the state. A manual
  Retail attack can still confirm readiness immediately, including after
  automatic retry exhaustion.

This does not re-enable the motion-triggered `SWING ATTACK` experiment. That
adapter was live-rejected because the delayed Retail animation produced an
unwanted second attack after the physical hit. Automatic seeding occurs once
after a stable pickup, before physical combat is declared ready, and remains
independent of the per-weapon swing-attack setting. Whether even that one-shot
pickup pulse exposes an unacceptable visible Retail animation is a live
usability gate, not an automated claim.

Portable policy coverage exercises stability dwell, pulse bounds, exact-mask
confirmation, settle/readiness, body loss, transient unsafe context, three
attempts, manual fallback, damage-dispatch suppression, and weapon/model
lifecycle reset on x86 and x64. The final normal gate passes 23/23 x86 and
19/19 x64 tests plus launcher-focus, screenshot-helper, and schema-v4 watcher
regressions. The refreshed project-local stage has matching build/stage x86
loader SHA-256
`08F10AE3C302D6C7D616DE2B10C01D3F44389B9F362FCCD2EC717AF55BE346E4`.
This is **implemented, awaiting live validation**. No game/headset run has yet
proven automatic pickup readiness or perception.

## Player locomotion collision-width probe (19 August 2026)

Stick locomotion stopped the HMD too far from enemies, so weapon `COLLIDER`
and `BLOCK COL` geometry were excluded first: neither owns the character body.
Static evidence in the supported Retail `GameOrig.dll` established the local
`CMoveMgr` singleton at `GameOrig+0x00168EEC`, its player HOBJECT at manager
`+0x10`, the desired dimensions at `+0x1C`, and the identity/signature-gated
dimension handoff at `GameOrig+0x00031BA0`. The engine interface uses verified
`ILTClientPhysics` slots 8/9 for `GetObjectDims`/`SetObjectDims`;
`SETDIMS_PUSHOBJECTS` remains native.

The first candidate added a global `PLAYER COL` menu page. It scales only
Retail X/Z, preserves Retail/current stance Y, defaults and resets to exact
100%, filters on the exact local manager/player object, and restores the
manager request field after the native call. Settings changes are reapplied on
the game thread and remain pending after native failure. Enemy and other
objects are never passed to `SetObjectDims`. The pre-live full gate passed
24/24 x86 and 20/20 x64 tests.

Run `run-20260819-115942` supplied the first live evidence:

- the hook armed on the supported binary and loaded a 100% default;
- Retail requested and reported `(40,95,40)`, with native result zero;
- every successful step down reported exact requested/actual agreement;
- at the initial 50% floor, native actual dimensions were `(20,95,20)`, Y
  remained 95, and telemetry reported `enemy_objects_changed=0`;
- attempts to expand while obstructed returned native result 1 and retained
  `(20,95,20)` until the player moved clear, after which expansion succeeded;
- after returning to 50%, accepted actor contacts recorded HMD-XZ distances
  of 0.7137 m and 0.7939 m to their contact points. Those values are not a
  nearest-body or object-centre distance; and
- the headset tester reported that 50% did not materially improve the
  stick-locomotion approach gap.

This is **live verification of the local-player dimension handoff**, but a
**live rejection of 50% width as a sufficient proximity fix**. It does not
establish that enemy dimensions or an AI/server push rule owns the remainder.

The next diagnostic checkpoint lowered only the positive floor from 50% to
10%, which requests `(4,95,4)` for the observed Retail body. On an accepted
actor contact it also reads, but does not mutate, the local-player and target
dimensions through the same verified `GetObjectDims` interface and emits
`m5_enemy_collider_observed` with `mutation=none`. Retail 100% remains the
default/reset. The full headset-free gate on 2026-08-19 passed 24/24 x86 and
20/20 x64 CTest cases plus the launcher-focus, screenshot-helper, and
schema-v4 diagnostics-watcher checks. At that checkpoint the follow-up was
**automated only, awaiting live validation**. The built x86 loader SHA-256 is
`2710B2B87F14B8FD0DCCA0F0B31ACDC2D0CC4A43D47DE880C40B50FFBD8FF3C8`;
the build manifest records a dirty working tree, so that hash rather than a
commit identifies this candidate. The exact candidate was staged at
2026-08-19T12:27:03Z in `stage/condemned-m2-mono` after both compiled Retail
identity verifiers passed.

Run `run-20260819-122852` supplied the second live gate with the staged loader
above:

- the persisted 50% setting first reapplied `(20,95,20)` successfully;
- menu steps down to 10% each reported exact native requested/actual
  agreement, ending at `(4,95,4)` with result zero and preserved Y;
- the tester then reported that enemy approach distance felt unchanged;
- three accepted actor contacts on the same player HOBJECT later read the
  player at `(40,95,40)` while the configured scale still reported 10%;
- the corresponding target reads were `(40,95,40)`,
  `(39.022,91.3,39.022)`, and `(39,91,39)`; and
- no `m5_player_collider_native_handoff` or retry/reapply event explained the
  transition back to full width.

This is **live verification that the one-shot 10% handoff did not persist**.
The tester's unchanged-distance report therefore does not yet reject a
continuously retained 10% player width; by contact time the live player body
was back at Retail dimensions. Enemy dimensions were observed read-only and
remain unmodified.

Read-only GNU objdump inspection of the same verified stock client SHA-256
`0AC9798CA460C3E24EFC6D103D5FD258CCA6C921E0BD2A3FD9119D1C7C5228CC`
found a concrete bypass candidate. The flag-`0x20` branch at
`GameOrig+0x00037FE8` calls manager routine `+0x000344E0`, which writes the
same manager `+0x10` HOBJECT through physics slot 9 at `+0x000346BC`,
`+0x0003476C`, and retry `+0x00034787`. A branch can source dimensions from
manager triple `+0x40C/+0x410/+0x414`. A separately populated triple starts
at `+0x418`, but `+0x344E0` does not read it. These code facts are static
verified. Whether that branch fired in the live run, and the runtime semantics
of either triple, remain hypotheses.

The current working tree adds `GetObjectDims` observations immediately before
and after the verified client-shell update, emitting the initial boundary
value and later changes. A processed pending request gets a forced readback.
The probe reads only the exact local HOBJECT and labels manager `+0x1C`, the
manager-`+0x40C` source candidate, and the separate adjacent candidate at
`+0x418`. The post-Retail sample precedes pending processing.
`post_mod_setdims_attempt` means a slot-9 call was attempted, and
`native_result_valid` says whether it returned; `post_pending_noop` means
the requested dimensions already matched and no setter was called. The audit
itself performs no new engine mutation, cannot identify which writer caused a
boundary change, and cannot reveal a write-and-restore wholly inside one
Retail update. Its two queries per update while reduced plus one per processed
pending request remain a live performance gate. Independent event caps keep
repeated retries from consuming the boundary stream. It also fixes the stale
armed range text. The full headset-free gate completed at
2026-08-19T13:30:55Z with 24/24 x86 and 20/20 x64 CTest cases passing, plus
the launcher-focus, screenshot-helper, and schema-v4 diagnostics-watcher
checks. The x86 loader SHA-256 is
`D780B2DB6BC9C8A79824B4E24E0DF8E5C21E75A0F7234638B987CD954BEF8A3C`;
the manifest records base commit
`9732e0a867ffc3b80bfe909be6411a68367c457e` with a dirty working tree.

### Live boundary-classification run

Run `run-20260819-133507` exercised that exact loader with
`-WeaponTest Pipe -Wait` on VirtualDesktopXR 1.0.10 and Meta Quest 3. The
preserved loader log SHA-256 is
`69B71F6F38D097DF1595BACA4C099A983EA431D6F9D9C7344E685ED5AF4BC984`.

- the 10% setting loaded successfully on player HOBJECT `0CC25FC0`;
- the first pending setter returned zero, reported requested/actual
  `(4,95,4)`, and the forced `post_mod_setdims_attempt` readback confirmed
  `(4,95,4)` with `pending=0` and `drift=0`;
- the next changed boundary was `phase=post_retail_update` at
  `(40,95,40)`, `pending=0`, and `drift=1`, with locomotion mask zero and no
  intervening collider handoff, reapply, rejection, or failure;
- a controlled 15%-to-10% sequence again confirmed `(4,95,4)`. After the
  menu closed, the first `directions=0x5` stick edge was immediately followed
  by a post-Retail sample at `(40,95,40)`; the following direction was
  straight forward (`0x1`);
- across the controlled restoration, manager `+0x1C=(1,1,1)`, the
  `+0x40C` candidate was `(24,31.5,24)`, and the `+0x418` candidate was
  `(40,95,40)`. The actual/candidate equality does not attribute the writer;
- two accepted, native-forwarded contacts observed the player at full
  `(40,95,40)` while the setting remained 10%. Their read-only target
  dimensions were `(39.022,91.3,39.022)` and `(39,91,39)`, with
  contact-point distances 0.5989 m and 0.4548 m and `mutation=none`; and
- all 18 emitted reapply attempts returned zero. The run ended with a saved
  100% setting, requested/actual `(40,95,40)`, `pending=0`, and `drift=0`,
  followed by a clean game exit and OpenXR host shutdown.

This is **live verification of the attempted-slot-9/post-pending route, the
boundary audit, and restoration becoming visible inside a Retail client
update**. It does not identify the exact writer or prove which dimensions
movement collision consumed inside that update. The no-op and
native-result-unknown routes, cap exhaustion, menu warning presentation, and
subjective proximity/performance remain unvalidated; the tester supplied no
subjective result for this run.

Forced per-frame post-update reapply remains intentionally absent. It could
run after movement collision has already consumed full dimensions and would
invoke `SETDIMS_PUSHOBJECTS` every frame.

### Exact `SetObjectDims` observer (20 August 2026)

Static inspection of verified `Condemned.exe` SHA-256
`45A1404F213EDBDEAD16168B6E005B245B93105F7345AAF4FB83ECB6A7C5AE02`
identified the `CLTPhysicsClient` vtable at executable RVA `+0x0014ADE0`,
slot 8 `GetObjectDims` at `+0x00064530`, and slot 9 `SetObjectDims` at
`+0x00007FD0`. The setter has the verified x86
`uint32_t __thiscall(physics, object, Vector*, flags)` ABI, ends in
`ret 0x0C`, and may copy adjusted values back into its dimensions buffer on
native failure.

The working tree now adds a MinHook observer at the exact slot-9 target.
Installation requires the full executable identity, exact vtable/slot
pointers, anchored getter/setter bodies and return tails, both embedded setter
diagnostic-string pointers, and verified GameOrig writer/caller/callsite byte
windows. Relocated absolute operands are decoded and compared with their
module-base-plus-RVA targets; no scan or approximate fallback exists. A
mismatch rejects only this diagnostic and leaves locomotion and the existing
`+0x31BA0` collider hook available.
Both collider detours remain native-pass-through until their respective enable
operations succeed. An uncertain owned-hook rollback poisons future retries
but retains the trampoline for native-only forwarding.

The detour executes for every native `SetObjectDims` call but forwards the
same physics/object pointers, in/out request pointer, and raw slot-9 flags
exactly once and returns the raw native result. Calls outside Playing, outside
reduced-or-pending audit state, or outside the fresh exact local-player
HOBJECT are forwarded without observation. Bounded exact-local telemetry
records thread/sequence and raw caller address; module/return RVA is valid only
when the caller resolves to a loaded image. It also records a verified call RVA
when known, request input/output, raw slot-9 flags, native result, actual
dimensions before/after, context stability, and read-only manager candidates.

Verified GameOrig mappings are calls/returns
`+0x31BF9/+0x31BFC`, `+0x31C13/+0x31C16`,
`+0x31D65/+0x31D68`, `+0x31D83/+0x31D86`,
`+0x346BC/+0x346BF`, `+0x3476C/+0x3476F`, and
`+0x34787/+0x3478A`. The three `+0x344E0` internal returns identify that
verified routine, not a runtime copy of the outer flag byte. Known GameOrig,
unknown GameOrig, unknown executable, other-module local, and unresolved local
calls use independent caps of 64, 64, 64, 32, and 32. Cap exhaustion remains
inconclusive, and an other-module or unresolved record must be resolved before
attribution to an authority writer.

This candidate is **automated-only**. The portable exact-return classifier and
exact-once forwarding tests pass, and the normal headset-free gate passed
before the next diagnostic was added. The x86 hook's live filters, event
stream, runtime cost, and native in/out behavior remain unverified. No staged
observer run exists, and `+0x344E0` remains only a restoration candidate.

### Read-only Collision X-ray (20 August 2026)

The smallest follow-up is a session-only `PLAYER COL` toggle. It adds a
separately executable-identity/vtable/body-gated observer at verified
`ILTClientPhysics` slot 11, `Condemned.exe+0x00007CD0`. The detour observes
only the exact resolved local-player physics/object pair while Playing and in
the foreground, forwards the original physics/object/velocity pointers once,
and returns the raw result. Telemetry explicitly calls this a velocity handoff,
not a collision result.

At the existing client-update boundary, read-only samples capture actual
player dimensions and object origin before and after Retail, fresh HMD origin,
locomotion mask, and a fresh local-player-owned actor-contact target's
dimensions, origin, and contact point. The target expires after two seconds;
invalid or non-finite data fails closed. The display draws the player in
magenta, target in orange, HMD in cyan, and contact in yellow. Every box is a
**diagnostic proxy** built from object origin plus/minus `GetObjectDims`; true
physics geometry and orientation remain unverified. HMD-to-player horizontal
distance measures origin offset only and is never interpreted as radius.

The diagnostic adds no `SetObjectDims`, post-update correction, enemy write,
offset, or persisted setting. It is **automated-only**: the full headset-free
gate passes 25/25 x86 and 21/21 x64 tests plus all three PowerShell validation
suites. Live validation remains pending. Success requires a
single trace to separate update-boundary restoration, the object/dimensions
receiving movement, target separation/proximity behavior, and HMD/player
origin offset. Reject on identity mismatch, non-exact forwarding, proxy
mislabeling, unbounded/stale data, or performance regression; rollback is the
session toggle OFF.

## Phase-1 magazine insertion authoring slice (20 August 2026)

The working tree now has one deliberately narrow interaction-authoring
vertical slice. The existing `-WeaponGripCalibration` / VR Tools boundary
adds an `AUTHOR` page for only a model-local magazine insertion socket. A
fresh left-grip pose can explicitly capture the seated pose; selectable
components then edit position, rotation, box half-extents, approach-rail
length, snap distance, and snap angle with 0.1/1 cm and 0.25/5 degree steps.
A 32-entry snapshot stack supplies undo, and reset restores the immutable
record loaded for that source generation.

The overlay reuses both generic controller wireframes. The socket is an
oriented wire box with RGB axes and a finite local rail. The off-hand cursor is
transformed through the existing validated displayed-model pose; projection
onto the rail changes only a mod-owned ghost. No Retail magazine object, child,
bone, socket, layout, offset, RVA, vtable slot, or native action function is
read or inferred.

Persistence adds an independent `magazine_socket` key under the existing
`[weapon_<index>]` section. Load/save requires both the stable index and
case-sensitive resolved catalog name. It has no Pipe, profile, or cross-weapon
fallback; the existing player-over-package precedence can supply only an exact
same-identity record. A missing record is unconfigured; the neutral primitive
dimensions become visible only after explicit capture. Malformed player data
or an identity mismatch fails closed.

The existing per-run alignment file preserves Collider version 1 and now
accepts a separately parsed version-2 socket command with revision,
base-revision, PID, exact index/name, configured bit, and all numeric fields.
The runtime persists before publishing, rolls back failed saves, and emits an
applied/rejected acknowledgement. The helper
`tools/set-condemned-magazine-socket.ps1` resolves the active exact state and
waits on that acknowledgement. Every Phase-1 event explicitly reports
`engine_handoff=none ammo_mutation=0 weapon_state_mutation=0
retail_state_mutation=0`.

Headset-free evidence is complete for this source state. The normal
`tools\build-all.ps1` gate at 2026-08-20T03:21:53Z passed 25/25 x86 and
21/21 x64 CTest cases plus launcher-focus, screenshot-helper, and
weapon-diagnostics schema-v4 PowerShell suites. The focused settings test
covers exact-name round trip, mismatch, missing-record non-mutation, and
malformed fail-closed behavior. The common authoring test covers bounds,
fine/coarse edits, undo/reset, model-local transform, rail snapping, line
budget, and strict version-2 parsing. The new sender also passes a PowerShell
syntax parse. The dirty-tree x86 loader SHA-256 is
`1E317E2FCE5C3AADC10775BBA13821982C44985BED0463159C6D096ADD6A49D4`
at base commit `9732e0a867ffc3b80bfe909be6411a68367c457e`.

This is **implemented and automated-tested, awaiting live validation**. No
headset/game run has proven both-eye presentation, capture timing, input
isolation, persistence across a fresh live process, acknowledged external
edits, performance, or absence of a perceived gameplay regression. Phase 2
gesture logging remains unimplemented. Phase 3 remains blocked until a
separately verified Retail reload/action handoff exists.

## Colt equipped-model slide discovery (20 August 2026)

The working tree now includes a separate observation-only diagnostic for the
next firearm-interaction uncertainty. `-WeaponModelDiscovery` requires the
established stereo render path and lifetime-validated equipped-model source.
It reuses the already verified `ILTModelClient.Default` node count, traversal,
name, parent, and transform slots; it does not add a new offset, RVA, vtable
slot, model write, or node control.

For every equipped source generation the pass enumerates at most 256 nodes and
logs each name, handle, parent, and model-local transform. It continually
refreshes the baseline during a two-second settle window so the normal weapon
equip transition is less likely to become the closed reference pose. After
`weapon_model_discovery_baseline_ready`, each node is sampled in model-local
space and only increasing motion peaks are logged. A motion record includes
the baseline and current positions, displacement, normalized candidate axis,
peak travel in engine units, peak rotation, exact weapon index/model/generation,
and the node hierarchy identifiers. The log is bounded to 1024 motion records
per source generation.

This is **implemented and automated-tested, awaiting live validation**. It
supplies a way to distinguish a translating Colt slide/bolt node from whole-weapon
controller motion without assuming that the missing optional `Breach` socket
is the slide. A canonical index-76 run must wait for the baseline, fire once,
and reload once. A credible candidate should show repeatable model-local
translation on a stable node/subtree with a consistent axis and travel. If no
suitable node moves, the result does not authorize an invented node: a
separate attachment/model-piece discovery task is required.

The diagnostic does not implement the requested grab, hand lock, constrained
pull, endpoint, release, slide return, chamber, or ammo behaviour. Those
remain gated in order: live-identify the moving owner and closed/rear endpoint
first; author a `weapon_action_rail`; observe the controller gesture without
Retail mutation; then prove a narrow model-node control and native weapon
action handoff independently before connecting the gesture.

The normal 20 August RelWithDebInfo gate passed 25/25 x86 and 21/21 x64
CTest cases plus the launch-profile, focus-handoff, screenshot-helper,
weapon-diagnostics, and release-tool PowerShell suites. The built dirty-tree
x86 loader SHA-256 is
`EF161A5A124A7021969E82E7CD4E3395B210FF5A0D57D47185D11DA2085E888B`.
Those results prove compilation and guarded integration only; no portable test
can identify the live Colt hierarchy or animation.

The first requested live attempt, `run-20260820-081943`, stopped at launcher
readiness. Its loader log proves the game reached active index 76 and captured
Colt model object `38FBF090`, but the game had loaded the older staged x86
loader SHA-256
`74B2169CA45D0A8FA013AAA1ACA857473F04009FCF18FCD5F31981B63CF6AAF6`.
That binary predates `weapon_model_discovery_armed`, so the missing event was a
stale-stage failure rather than a rejected model query and the run supplies no
slide/node evidence. The M2 mono stage was refreshed at
2026-08-20T08:21:45Z and its `GameClient.dll` now matches the built diagnostic
at SHA-256
`EF161A5A124A7021969E82E7CD4E3395B210FF5A0D57D47185D11DA2085E888B`.
The launcher timeout now names every missing readiness capability and suggests
checking the prepared stage instead of reporting only a generic controller-
hook failure.

Because the launcher failed before its normal evidence-copy completion, the
loader log remained only in `game-override` and was replaced when the stage was
refreshed. The run directory retains its bridge and host logs, but not the full
loader log or a loader-log hash. The old staged binary hash and quoted readiness,
index, model, and source fields above were inspected before refresh; treat this
as an incomplete failed-launch record, not canonical runtime evidence.
The launcher now copies the module-local loader log into the session directory
on any caught failure and after a `-Wait` run exits, so a later stage refresh
does not repeat this evidence loss.

## Authored Colt slide-grab rail (20 August 2026)

The subsequent canonical discovery run,
`stage\condemned-m2-mono\logs\run-20260820-082601\condemnedvr-loader.log`,
provides **live-verified observation evidence for one Colt model lifetime**.
Exact Retail weapon index 76 exposed node `SlideJnt`, parent
`anim_cult45`. Its model-local closed position was
`(14.1689, 2.8062, -8.7261)`, rear position was
`(10.3449, 2.8362, -8.1651)`, normalized closed-to-rear axis was
`(-0.989379, 0.007748, 0.145151)`, and maximum translation was 3.8651
engine units. The node rotation did not change during the observed Retail
animation. The observed node handle 3 and model pointer `0x38C50930` are
lifetime-local evidence only. The implementation resolves `SlideJnt` by name
again for every validated source generation and never stores either value.

The AUTHOR tool menu now selects between the retained `MAG INSERT SOCKET`
primitive and a new `SLIDE GRAB RAIL` primitive. An exact index-76 Colt record
may seed the observed node name, closed point, normalized rail axis, maximum
travel, and a rear threshold. It deliberately remains unconfigured until an
author captures or adjusts the off-hand model-local grab-box pose and authored
hand pose; the animation pivot is not treated as a contact point. The editor
provides oriented-box position/rotation/half-extents, hand position/rotation,
closed position, axis, travel, threshold, and GRIP/TRIGGER/EITHER activation
components, fine/coarse adjustment, capture, undo, reset-to-loaded, explicit
save, and box/rail/endpoints/hand-pose gizmos. Status distinguishes configured,
not configured, invalid, unavailable, and unsaved. Magazine authoring retains
its previous automatic persistence behavior.

Slide records are stored independently as `slide_grab_rail` in the existing
exact index plus case-sensitive catalog-name store. Version 2 persists every
field. Version-1 records migrate with activation `EITHER`; malformed,
non-finite, non-normalized, out-of-range, wrong-name, or incomplete records
fail closed without replacing the caller's current settings.

The runtime path is an explicit Idle/Candidate/Attached/Released machine.
Candidate and attachment require Playing state, foreground focus, fresh
off-hand tracking and input, exact index/name identity, the same lifetime-valid
model and source generation, finite transforms/settings, oriented-box overlap,
per-lifetime name resolution, and the configured activation edge. The attached
controller displacement is transformed into held-model local space, projected
onto the observed axis, and clamped to `[0, maximumTravel]`. The registered
node control changes only position along that rail, leaves the incoming Retail
rotation intact, and moves the authored left-hand IK target by the same
translation. The dominant weapon-hand path is unchanged. Only the matching VR
off-hand Run/Block source is consumed while candidate/attached, preventing the
grab edge from becoming a Retail command while preserving keyboard, mouse,
other controller, non-VR, and host-absent behavior.

The engine-write boundary adds no RVA, object offset, signature, vtable slot,
or node API. The Colt-only `-SlideControlTest` path reuses the already
identity-validated `ILTModelClient.Default` GetNode/GetNodeTransform and
specific AddNodeControl/RemoveNodeControl mechanism established by the arm-IK
work. It prepares against the current exact model/generation, resolves by name,
records the before transform, registers one pass-through-by-default callback,
and removes it on every detach. If the incoming Retail position differs from
the closed rail by more than 0.15 units along or off the rail, the callback
classifies an incompatible Retail fire/reload animation, performs no write,
and requests deterministic immediate detach/removal. Release, focus loss,
stale tracking/input, game-state change, identity/model/generation change,
invalid transform, menu opening, resolution/control failure, and incompatible
Retail animation use the same rollback path. Even a callback-removal failure
first disables its write, so Retail transforms pass through.

Bounded `m5_slide_grab_transition`, `m5_slide_node_resolved`,
`m5_slide_node_control_sample`, and detach/removal events expose
`INPUT -> TRANSFORM -> STATE -> DECISION -> ENGINE HANDOFF -> RESULT` fields:
weapon identity, generation, node resolution, controller/input, overlap,
controller model-local position, projected/clamped travel, hand target,
before/requested/after transform, node-control result, detach reason, and
Retail-ownership restoration. Samples are restricted to transition/endpoint
evidence rather than emitted every frame.

This slice is **implemented and automated-tested, awaiting live validation**.
Portable tests prove the math, validation, persistence/migration, editor
semantics, activation choices, and fail-closed state transitions. They do not
prove that the live Colt accepts the callback write, the authored hand aligns
perceptually, the Retail command is isolated, or callback removal restores
normal firing/reload animation. The exact live gate is in
`docs/TESTING.md`; do not promote this feature to live verified before that
evidence is captured.

### First authored-interaction live attempt

Live run `run-20260820-091557` used the guarded slide-control command and
current exact Colt identity. It armed the verified node-control boundary,
loaded the index-76 `SlideJnt` seed for source generation 1, and correctly
reported `configured=0`, `load=not_found`, and no engine handoff. Both arm
callbacks installed and became active. The user then opened VR Tools and
navigated to AUTHOR, but the tool panel disappeared. The preserved loader log
identifies the cause:
`m5_vr_tool_menu_overlay_failed triangle_buffer_overflow=1
vertices=32766 limit=32768`. No slide capture, save, node resolution,
attachment, or node-control write occurred, so this run supplies no slide
interaction acceptance.

The smallest correction removes redundant non-interactive magazine diagnostics
and compacts slide rail/status text. All eight AUTHOR controls remain, magazine
auto-save is unchanged, and slide configured/invalid/unavailable/unsaved,
load/save, node, input, rail, overlap, and guarded-control states remain
visible. New complete-menu fixtures render worst-case magazine and slide
AUTHOR strings through the same glyph, panel-transform, and 32,768-vertex
boundary. The full gate passes 26/26 x86 and 22/22 x64 CTest cases plus the
normal PowerShell suites. The corrected x86 loader SHA-256 is
`708EBD9DDFE21A3E1BDA7AEBAB8393C0977C44DA5B8F977A2389770C1B1343F1`.

Two immediate correction rechecks, `run-20260820-092112` and
`run-20260820-092228`, did not enter Playing state. Their host and bridge
connected and exchanged frames, while the loader remained in
`splash -> demo -> screen`; the guarded launcher rolled each session back at
its 45-second gameplay-camera readiness timeout. They neither reproduce nor
clear the AUTHOR failure. The compacted AUTHOR panel and all later slide gates
remain **implemented and automated-tested, awaiting live validation**.

### First live attachment and rejected hand-pose presentation

Live run `run-20260820-092549` cleared the AUTHOR overlay failure. The panel
remained visible through navigation and editing, emitted no
`m5_vr_tool_menu_overlay_failed`, captured a physical off-hand grab/hand pose,
and saved the exact index-76 / case-sensitive `colt45_Unbreakable` record.
`SlideJnt` was resolved by name for source generation 1 and again for source
generation 2 after the equipped-model lifetime changed; no persisted handle or
pointer was reused.

For generation 2, the controller produced inside-volume Candidate transitions
and several activation-edge Attachments. The guarded callback reported
position-only writes with unchanged Retail rotation. Bounded samples reached
the verified rear endpoint `(10.3449, 2.8361, -8.1651)`: projected values above
the endpoint were clamped to exactly 3.8651 units. Release removed the specific
callback and reported `retail_ownership_restored=1`. The user explicitly
confirmed that attachment and slide motion worked. This is live evidence for
the Colt node-control rail, per-lifetime name resolution, clamp, and release
path, but not blanket feature acceptance.

The attached hand presentation failed the live gate: the user reported that
the hand pose was incorrect. The runtime had composed the authored model-local
pose into world space and published it directly, while the normal free/support
left-hand path applies `ResolveToolMenuLeftHandIkTarget`. AUTHOR captures the
raw physical OpenXR grip pose, so bypassing the established controller-local
wrist translation/rotation was a pose-convention error. The corrected path now
applies the same LEFT IK calibration after model-to-world composition and logs
the final calibrated target basis. The run also exposed repeated source-only
`m5_slide_node_control_detached` records while the menu was open. Menu teardown
is now executed only on open/close transitions, and `EndSlideNodeControl` logs
a detach only when a callback was actually installed.

The post-correction full gate passes 26/26 x86 and 22/22 x64 CTest cases plus
the launch-profile, focus-handoff, screenshot-helper, schema-v4 diagnostics,
and release-tool PowerShell suites. The corrected x86 loader SHA-256 is
`C75C8C570D313A982D27753BD1F3045A6DC65DBF9684E933228E6FA838B2C4E0`.
These two corrections are automated-only until a fresh staged run confirms the
authored hand visually and the absence of per-frame detach records. The run did
not explicitly accept absence of unintended Retail action, focus-loss reason,
or normal post-release firing/reload, so those gates remain open.

The first corrected staging attempt, `run-20260820-094432`, loaded the exact
corrected hash and armed slide control, but remained in
`splash -> demo -> screen` until the 45-second gameplay readiness timeout. It
contains no Colt source, AUTHOR, attachment, or corrected-hand evidence and is
classified only as a guarded readiness failure.

### Corrected authored-hand live acceptance

Corrected run `run-20260820-124747` loaded staged x86 loader SHA-256
`C75C8C570D313A982D27753BD1F3045A6DC65DBF9684E933228E6FA838B2C4E0`.
The exact saved Colt record entered Candidate only on authored-volume overlap
and published attached hand targets with
`hand_target_basis=authored_grip_plus_left_ik_calibration`. Across the run,
16 callback installations have exactly 16 corresponding installed-callback
detachments; there is no `m5_vr_tool_menu_overlay_failed` event and no source-
only per-frame detach volume. Endpoint samples repeatedly clamp projected
travel to 3.8651 units with unchanged rotation and matching requested/after
positions, while every release reports successful callback removal and
`retail_ownership_restored=1`.

The source lifetime advanced from generation 1 to generation 5. `SlideJnt`
was resolved by name again for generation 5 before attachment even though the
allocator reused the same numeric model address; the runtime did not reuse a
persisted node handle. The user accepted the result explicitly: the slide
worked, the pose was saved, and the interaction was visually accepted as
correct. This promotes the authored pose, hand attachment, rail motion, rear
clamp, release restoration, transition-only logging, and per-generation name-
resolution portions to **live verified** for the Colt configuration exercised
in this run.

The response did not explicitly state whether an attachment produced a
gunshot/other Retail action, whether normal fire/reload animation resumed after
release, or whether focus loss detached with its dedicated reason. Those gates
remain open and are not inferred from the positive presentation judgment.

### User-supplied slide pull and return sounds

Static inspection of the verified Retail archive first established that the
Colt equip rack uses the database alias `colt45_select`, mapped to
`global\weapons\colt45\snd\colt45_select.wav`. That 44.1-kHz, 16-bit mono
355-ms file contains two strong mechanical regions separated by a low-energy
boundary near 156 ms, so Retail bakes pullback and spring return into one
compound recording. `colt45_deselect.wav` exists in the archive but has no
corresponding Colt deselect alias in `database\Dark.Gamdb00p`; it is not
evidence for a standalone return sound. The Retail extraction remained under
the ignored local analysis stage and is not a redistributable input.

The user subsequently supplied two Creative Commons Zero assets:

- `sounds\colt45_slide_pull.wav`: 44.1-kHz 16-bit stereo PCM, 415 ms,
  SHA-256
  `DDC9920E64C99E0F75DAED6B5F3D6B3DDB13933C12A4E0631D77109ECAF1FC42`;
- `sounds\colt45_slide_return.wav`: 44.1-kHz 16-bit mono PCM, 257 ms,
  SHA-256
  `028A7976EBC5B629F944C2AF3126296E4CDC19512DE9F09829D41209CEF7485E`.

The pull recording is Nanashi's `Slide pull.wav`, Freesound sound 104409. The
return recording is vabadus's `Beretta M9 slide release`, Freesound sound
151067. Both source pages identify the files as CC0; their canonical URLs,
creators, CC0 1.0 terms, and repository hashes are recorded in
`THIRD_PARTY_NOTICES.md`. Attribution is not required, but provenance is
retained. The files are eligible for source and release packaging.

The implementation uses the documented Windows `PlaySoundW` filename API and
links `winmm`; it does not call a guessed Retail sound-manager RVA or ABI. The
loader resolves only module-relative `sounds\colt45_slide_pull.wav` and
`sounds\colt45_slide_return.wav`. The pull cue fires after attached travel
reaches the authored `rearThresholdUnits`; the Colt seed uses 3.50 of the
verified 3.8651-unit maximum. While attachment is retained, moving forward to
at least 0.25 units below the rear threshold rearms the cue, and the next rear
threshold crossing fires another numbered pull cycle. This hysteresis prevents
endpoint jitter from causing duplicates. The return cue is one-shot only when the
configured input is normally released while the most recent travel remains at
least 0.10 units and `EndSlideNodeControl` reports that Retail ownership was
restored. Returning the slide to closed while still attached,
node-control failure, focus/tracking/game-state loss, menu opening, identity or
generation change, model loss, and incompatible Retail animation produce no
false return cue and stop pending project playback after a pull.

`m5_slide_grab_sound` records
`STATE -> DECISION -> WINDOWS_AUDIO_HANDOFF -> RESULT`, exact weapon identity,
source generation, cue/action, numbered pull cycle, travel, rear threshold,
relative asset, availability, handoff
request/result, detach reason, and Retail-ownership restoration. CMake copies
the assets beside the built x86 loader and both M2 preparation scripts stage
and hash them under `game-override\sounds` without changing Retail files.

The full automated gate passes 26/26 x86 and 22/22 x64 CTest cases plus all
five PowerShell suites. Focused tests cover pull only on the authored rear
threshold, no duplicate within the endpoint hysteresis band, forward-motion
rearm and a second pull without input release, one-shot input-release return,
cancellation without return, already-closed release, and rejected node-control
attachment. The staged x86
loader is
`A5B79C8741CAA3E4F09833B17B7D9D218F434F0B794E19536D9FA77E78E16036`.
This sound slice is **implemented and automated-tested, awaiting live auditory
validation**; neither compilation nor `PlaySoundW` initiation proves that the
headset receives the cue at the intended timing and level.

### First slide-sound live timing result

Run `run-20260820-133700` loaded the initial sound-enabled loader
`C9146F6685689C73B5F75F819F2299EC51DAF753F3A8E3CAE4EA87FE4F11B5D1`.
It recorded 44 installed attachments and 44 installed-callback detachments
across source generations 1, 3, and 5. Exactly 44 pull handoffs occurred, 35
displaced input releases produced return handoffs after
`retail_ownership_restored=1`, and nine releases after the slide had returned
to zero produced stop/no-return. Every requested asset was available and all
88 Windows handoffs returned success. There was no AUTHOR overlay failure.

The user supplied the decisive perceptual correction: pullback audio should
trigger at maximum pull length or close to it. The initial build triggered on
the first frame at or above 0.10 units; observed pull request travel ranged
from 0.1009 to 0.4762 units, so the timing was correctly rejected. Current
source replaces that early threshold with the already-authored
`projection.rearReached` result, 3.50 units for the saved Colt seed, and adds
`rear_threshold` to sound telemetry. Return-on-release policy is unchanged.
The corrected hash above is staged and awaits a live recheck; do not treat the
first run as acceptance of corrected pull timing or of the return sound.

Corrected run `run-20260820-134612` loaded
`F4648986D7EECC31FD013603AAA75DD095DEB3D7FC38CA9FA524CCB0948C91B4`.
Across 12 installed attachments, pull cues occurred at travel values from
3.5050 through 3.8651 units; none occurred below the logged 3.5000-unit rear
threshold. Eleven input releases while displaced produced return cues after
`retail_ownership_restored=1`. One attachment was pulled beyond the rear
threshold, then returned to closed while still held; its release correctly
produced `action=stop travel=0.0000` and no return cue. All 24 requested
Windows handoffs returned success, all required assets were available, and
all 12 installed callbacks detached. Fresh name-resolution evidence spans
source generations 1, 3, and 5, with no AUTHOR overlay failure.

This accepts the corrected **live cue-policy and handoff timing evidence**.
The user subsequently judged the pull/return timing and content perfection,
so the tested single-pull audio behavior is live accepted. The later repeat-
pull rearm extension was not present in this run and remains implemented and
automated-tested, awaiting live validation.

### Attached repeat-pull extension live handoff result

The current staged loader
`A5B79C8741CAA3E4F09833B17B7D9D218F434F0B794E19536D9FA77E78E16036`
allows another pull cue during the same attachment after the slide first
crosses the rear threshold, moves forward beyond a 0.25-unit hysteresis band,
and crosses the rear threshold again. Telemetry adds `pull_cycle`; automated
coverage requires cycles 1 and 2, and proves that jitter only 0.10 units below
the threshold does not rearm. This is not live evidence that repeated cues are
audible or perceptually correct.

Repeat-cycle run `run-20260820-135900` loaded that exact staged hash. Eleven
successful attachments produced 24 pull handoffs. Two continuous attachments
each advanced monotonically from `pull_cycle=1` through `pull_cycle=6` before
input release, directly proving that forward motion rearmed the cue while the
attachment remained active. All pull requests occurred from 3.5123 through
3.8651 units and therefore met the logged 3.5000-unit rear threshold. Nine
displaced input releases emitted return cues only after
`retail_ownership_restored=1`; two releases at closed emitted stop/no-return.
All 11 installed callbacks detached with Retail ownership restored, all audio
handoffs succeeded, and no sound/control failure or AUTHOR overlay failure was
recorded. Node `SlideJnt` was freshly resolved by name for source generation 3.

This accepts the **live repeat-cycle state, decision, engine-handoff, and
result evidence**. The user then explicitly judged the repeated behavior
perfect, accepting audibility, timing, and absence of unwanted duplicates for
the tested Colt path. The attached repeat-pull audio extension is live
accepted.

## Guarded Retail HeadBob suppression (21 August 2026)

Static evidence from the verified Retail `GameOrig.dll` identifies the
supported console variable `HeadBob` with default value 1.0.
`CUserProfile` exposes the dormant Head Bob control as integer 0--10 and
converts between that control and the console value with a 0.1 scale. The
Retail subsystem owns distinct `CameraOffset`/`CameraRotation` and
`WeaponOffset`/`WeaponRotation` channels under the same HeadBob system.
These findings support one Retail-owned console override; they do not justify
an IK, grip, weight, controller, model-node, or stereo-camera compensation.
The rejected dormant `CScreenGame` controls remain disabled.

The launch policy in `tools/_condemnedvr-launch-profile.ps1` now requests
`+HeadBob 0` for Current, Pipe, and every other non-Minimal custom/diagnostic
profile. Plain `-Minimal` emits no HeadBob pair and remains the bare
Retail-behaviour baseline. The rollback-only `-RetailHeadBob` switch does not
select a different feature profile and explicitly requests `+HeadBob 1`;
the installed `Play.cmd` path forwards the same switch. The launch report
adds `RetailHeadBobSuppressed` and `RetailHeadBobCommandValue`. Those fields
prove only the requested launch arguments, not that Retail consumed them or
that live visual behavior passed.

The pure launch-profile regression covers default Current, explicit Pipe,
custom diagnostics, plain Minimal, rollback-only Current, custom plus rollback,
Minimal plus explicit rollback, real bound-parameter dictionaries, and the
existing Minimal/feature conflict. The 21 August full RelWithDebInfo gate
passed 26/26 x86 and 22/22 x64 CTest cases plus launch-profile,
foreground-handoff, screenshot-helper, schema-v4 weapon-diagnostics, and
release-tool PowerShell suites.

Before and after that headset-free gate, the project-local staged
`autoexec.cfg` was exactly 889 bytes with SHA-256
`3FC7560991888E866FAE3BA42291826BAC43D523C29E602EE31C57DFFC2C49C8`
and retained the exact CRLF line `"HeadBob" "1.000000"`. The automated
gate did not launch Condemned, so this proves only that the implementation and
tests did not edit the staged file; it is not evidence about Retail persistence
after a live command-line override. No Retail installation file was changed.

The first headset A/B rejected the initial argument construction. Suppression
run `run-20260820-143707` requested value 0 and rollback run
`run-20260820-143941` requested value 1, but the user reported visible bob in
the first run and then judged the bob identical in the rollback run. Both runs
exited cleanly and retained their launch report, host log, bridge log, and
loader evidence. This proves a live visual non-difference for that candidate;
it does not prove either effective in-process console value.

The initial candidate placed `+HeadBob` before subsequent project-specific
switches. Because the verified Retail `+Windowed` pairs are emitted at the end,
the smallest bounded follow-up moved the HeadBob pair to the final two process
arguments. Pure tests assert that final ordering. Run
`run-20260820-144458` requested value 0 and exited without a recorded
perceptual result. The requested rerun `run-20260820-150441` also requested
value 0, and the user reported that bob was still present. This rejects the
final-position hypothesis.

A bounded static trace then identified Retail's effective-value boundary.
`GameOrig+0x00023CA0` is the console-float getter. The profile-apply routine
at `GameOrig+0x000AED80` reads the integer at `CUserProfile+0x9C`,
multiplies it by the verified 0.1 constant, and calls the console-float setter
at `GameOrig+0x00023D50` for `HeadBob`. The update at
`GameOrig+0x000567D0` selects `HeadBob` for non-idle records or
`IdleBreathing` for the exact Idle record, generates the twelve
camera/weapon offset/rotation channels, and multiplies each final channel by
the selected effective console value. These facts make post-command-line
profile application the static overwrite candidate; they do not by themselves
prove its live timing.

The guarded `-HeadBobDiagnostic` is observation-only. It signature-checks
that getter, setter, profile writer, 0.1 conversion, HeadBob update, final
channel multiplier, and both names before sampling after Retail's client
update. It also enables the existing read-only Retail camera transform sample.
It performs no console, object, camera, IK, grip, weapon, or Retail-file write,
does not select another gameplay profile, and is rejected with `-Minimal`.

Diagnostic run `run-20260820-152254` is live evidence that the launch request
was not the effective runtime value. Its report records Current,
`RetailHeadBobSuppressed=true`, command value 0, and the diagnostic enabled.
All 90 post-Retail-update samples, from update read 1 through 10,680, reported
`effective_headbob=1.000000`. The separate `IdleBreathing` query returned
the diagnostic fallback -1.0 throughout, so this run provides no accepted
live IdleBreathing value and does not add an override. The user's preceding
suppression run still showed bob. Therefore the observed locomotion motion
remains compatible with Retail HeadBob running at 1.0; it is not evidence that
an animation source survives an effective HeadBob value of zero.

The run used Git HEAD
`ebe8f3b0bceed3c54bbd86e1d07689c4b97a2538` plus the documented dirty
working tree. Staged SHA-256 values were
`638D56CD96DB38F667CD4CD0ABD6A580DEE61F86C014AC5D8E62B572CD634D1D`
for the loader,
`60FE9A5F2AC6537A31449B7019E615A3D7496B2A7FE7AFCC3A03060DB5237AE7`
for the bridge, and
`EFBFF77BCEEEC5FA62A88C19486FB42784170C21BB9077353DE32EBF2156C489`
for the host. Preserved run hashes are
`D108862DEFD016355F7161A2B802212C8A3EC6E1A285312395560FCD09F178B6`
(report),
`DDBD481C622FF845931DA528D542A23FEC637C189D6F16556F109487CABE0F09`
(loader log),
`C44BD2C62A63E20F6EAF79D576EBAAA002A457329C24186A850191D0FD183759`
(host log), and
`F3C4EAF63FD1A5E6995DC988CB83226841B5700F32E5613C3D343734A6EE96E9`
(bridge log).

The command-line-only suppression candidate is now **live rejected**.
Suppression is not implemented successfully, and no headset A/B has observed
an effective zero. A post-profile console application, profile persistence
change, or another engine intervention would exceed the stated
command-line-only implementation boundary and requires a separate explicit
decision before implementation. No compensation was added anywhere else.

Across the automated and live runs, the project-local staged source remained
exactly 889 bytes at SHA-256
`3FC7560991888E866FAE3BA42291826BAC43D523C29E602EE31C57DFFC2C49C8`
and retained `"HeadBob" "1.000000"`; Retail did not persist either requested
command value there. `IdleBreathing` remains untouched.

### Guarded post-profile enforcement and live discriminator

After the failed command-line-only candidate and with explicit approval for a
bounded follow-up, the non-Minimal launch policy retained `+HeadBob 0` and
added a private post-profile-zero arm flag. The loader first verifies the
known Retail console getter and setter, the profile writer and 0.1 conversion,
the HeadBob update and final channel multiplier, and both console-variable
names. After the original Retail client update, it reads `HeadBob` and calls
Retail's verified console setter only when the effective value differs from
the requested zero. This neither patches `GameOrig.dll` nor writes the profile
object, head-bob records, camera, weapon, IK, grip, weight, controller, model
nodes, or Retail/project config files. `IdleBreathing` remains untouched.

The rollback-only `-RetailHeadBob` path continues to request `+HeadBob 1`,
does not arm post-profile enforcement, and does not change the Current profile.
Plain `-Minimal` still sends neither override nor enforcement flag. The normal
21 August gate passed 26/26 x86 and 22/22 x64 CTest cases plus all five
PowerShell suites. The staged loader SHA-256 was
`DDF8D322A24BCAFF222FDF73525899C52534D058D41B9830FA1C9A8BFE2A49CB`;
the bridge and host retained
`60FE9A5F2AC6537A31449B7019E615A3D7496B2A7FE7AFCC3A03060DB5237AE7`
and
`EFBFF77BCEEEC5FA62A88C19486FB42784170C21BB9077353DE32EBF2156C489`.
The source basis was Git HEAD
`ebe8f3b0bceed3c54bbd86e1d07689c4b97a2538` plus the documented dirty
working tree.

Suppression run `run-20260821-032521` used Current on VirtualDesktopXR
1.0.10. Its report recorded `RetailHeadBobSuppressed=true`, command value 0,
and the diagnostic enabled. The first post-Retail-update read observed 1.0;
one guarded setter call requested 0.0, and all 83 effective samples then read
0.0. The user reported that locomotion bob was heavily reduced. Other movement
directions showed no residual; only a small amount of hand movement remained
during forward locomotion. This run therefore proves that the dominant motion
was Retail HeadBob and rejects character animation as its primary source. It
does not identify the small forward-only hand residual.

The same run identified the held firearm as Colt Retail index 76 and selected
the existing `weapon_weighted_aim` right-hand source with the bounded damped
weapon-weight spring active. Intentional filtered weapon inertia is therefore
the leading bounded hypothesis for that residual, but the run did not record a
simultaneous raw-controller versus weighted-target delta. It is not yet a
verified cause and does not authorize a weapon-weight or IK change.

Rollback run `run-20260821-032811` retained Current, recorded
`RetailHeadBobSuppressed=false` and command value 1, armed no post-profile
setter, and all 30 effective samples read 1.0. On the same comparison route,
the user reported that the large head bob returned. Both runs ended through
the normal game-heartbeat disconnect path. Preserved suppression hashes are
`51B56D91F82832BDCA06B0C3625D072E173A32081F0AAE3CBED772B1CD411515`
for the report,
`8CB5322C9EA0CA2436DEF726F7D668861B3DBC7A9E364C22D10DDF89D309D2D9`
for the loader log,
`1BE3C376E7415795C40A60F247015E3F1BA12A125FA7572EF8AB870DB71989FE`
for the host log, and
`C43B2125987009D3E6BAA4F1E93360648B68C278E99188AFDD2A9D94C05867EE`
for the bridge log. Rollback hashes in the same order are
`A8D57BF9B371A4BDA23D9A2FCF2CFAC55FDDCBA92B335BD3AB9519E336FD25B5`,
`BC8F06E5F1CDD8B42DFC70641ECE238DC8224C847388C663AD2E5E7B0556F06C`,
`9E14C2926FCB8A23DFD414A1FC5D93E12FBC2F75B9EC2F8EB1DD041A05122819`,
and
`443C11728BB5F818DDE65F08EF6F5678CDD432D5161F36D77C487FB5B6640FC7`.

Before staging, after the suppression exit, and after the rollback exit, the
project-local staged `autoexec.cfg` remained exactly 889 bytes at SHA-256
`3FC7560991888E866FAE3BA42291826BAC43D523C29E602EE31C57DFFC2C49C8`
and retained its original HeadBob value 1.0. Retail did not persist either
live console value to that file, and no source Retail file was altered.

The main locomotion HeadBob suppression and deterministic rollback now have a
**live-verified core A/B**. The small forward-only hand movement remains a
separate bounded hypothesis and must be diagnosed by observing input,
controller-world transform, desired hand/weapon pose, IK handoff, and rendered
result independently. No compensation is authorized in those systems. The
one-firearm, one-mapped-melee, landing, damage, scripted-camera,
keyboard/mouse, attachment, calibration, and complete pacing regression is
still incomplete, so the whole feature is not yet described as fully live
accepted.
