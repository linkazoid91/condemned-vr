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
5. **Native damage handoff:** allow qualified actor contacts through the
   verified Retail dispatcher while preserving target, weapon, material, and
   difficulty data. Start with one pipe/axe profile.
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
maximum sweep, contact separation, and sample bounds. The current generic
one-handed pipe profile is the fallback until a stable Retail model/weapon
identity lookup is verified. Pipe, crowbar, fire axe, plank, and later
two-handed profiles will select different records while sharing the same pose,
visual calibration, collision, qualification, damage, and haptic adapters.

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
- Y emits a profile-ready position/quaternion snapshot as
  `m5_weapon_grip_calibration_snapshot` in the loader log.
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
P to save a snapshot. Existing numpad controls remain available when present.

Both eyes use each updated value immediately. Switching weapons preserves each
stable Retail weapon index independently for the current game process. If the
same weapon is dropped, reacquired, or recreated during a level transition,
its process-local object/model pointers are refreshed while its alignment is
retained. These values are deliberately session-local: the snapshot is
reviewed and assigned to a stable Retail weapon identity before it becomes
permanent profile data.

## Two-hand axe interaction slice

Add `-TwoHandedMelee` to a launch that already enables
`-PhysicalMeleeVisualProxy`. For setup, also add
`-WeaponGripCalibration`. This is the first reusable support-grip layer; the
fire axe is the first profile to opt in, while unmapped and one-handed weapons
continue through the unchanged one-hand path.

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
restores the profile values, and **Log Two Hand Snapshot** emits the primary
and secondary profile-ready values together. After closing the menu, release
the left grip once, place it on the handle, and squeeze to attach.

The accepted fire-axe support offset from the 2026-08-04 headset run is
`{3.114, -30.258, -14.828}` LithTech units with a 0.15-metre grab radius.
It is now the stable profile starting point. Further live changes remain
session-local until another tested snapshot is deliberately promoted.

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

The menu has seven tabs:

- **Melee:** for a verified melee profile, enable the temporary
  swing-to-attack adapter and tune trigger speed, re-arm speed, pulse duration,
  and cooldown while seeing live swing speed and trigger count. Unmapped
  weapons show the adapter as unavailable rather than inheriting the axe's
  attack behavior.
- **Weapon:** tune mass, handling weight, positional/rotational follow,
  catch-up, and damping for the currently equipped weapon's own profile.
- **Grip:** adjust the equipped model's local XYZ position and rotation,
  adjustment step, reset, and log a profile snapshot. This tab requires the
  `-WeaponGripCalibration` launch option and shows the controller wireframe.
- **2-Hand:** enable the profile's support grip, edit its local offset and grab
  radius, capture the current left-hand pose, reset, and log a combined
  profile snapshot. Live attachment distance/error remains visible on the
  tab.
- **Display:** tune FOV scale, world scale, menu size, and menu convergence
  distance; toggle HMD translation, eye polarity, and stereo; recenter; or
  restore display defaults.
- **Controls:** shows the complete controller and keyboard menu mapping.
- **Debug:** shows current weapon/tracking state, live swing telemetry, and
  proxy/two-hand state.

Use the left/right triggers to change tabs, left-stick up/down to choose a row,
right-stick left/right to change a value, A to activate a row, and B to close.
The keyboard equivalents are left/right square brackets, arrow keys, Enter,
and F12. All values take effect immediately but remain session-local until a
tested value is deliberately promoted into a profile.

The cyan `EQUIPPED ... INDEX ...` banner identifies exactly which weapon is
being edited. Settings use the stable Retail weapon index rather than runtime
pointers. A fixed 64-slot allocation-free registry keeps each observed
weapon's Melee and Weapon values isolated: switching away and back restores
that weapon's values. Known catalog entries start from their authored defaults
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

## Fire-axe swing attack bridge

Until native physical-contact damage is enabled, the fire axe uses its
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

This is deliberately a transitional adapter, not the final damage authority.
It does not claim a hit merely because the swing was fast: Retail still owns
the attack animation and its existing attack/collision window, while native
physical impact dispatch remains blocked. The adapter is enabled only for the
measured fire-axe profile. Tracking loss, an invalid sweep, menus, focus loss,
weapon changes, stale input, and captured grip-calibration controls cancel the
pulse and reset its latch. Each accepted gesture is logged as
`m5_physical_melee_swing_attack_triggered` with the measured speed and timing
parameters.

The 3.00 m/s value replaces the initial, over-sensitive 1.80 m/s test value.
It can be adjusted live from 0.50 to 10.00 m/s in the menu's Melee tab without
restarting the game.
