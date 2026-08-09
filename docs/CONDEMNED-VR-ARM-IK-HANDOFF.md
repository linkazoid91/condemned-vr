# Condemned VR arm/hand IK porting handoff

## Scope and source revision

This handoff covers the configurable two-bone arm solver, elbow pole tuning,
elbow-continuity memory, exact hand-socket placement, left-hand pose
calibration, and the floating live-tuning controls implemented in F.E.A.R. VR.

Source repository state:

- branch: `integration/all-features`
- audited branch head: `4bcd610d904478a310b0dfc39a612b576115027a`
- primary IK commit: `46f99c64343832d03fc50c564f684bcf2faff39c`
- primary IK parent: `583fb47d6ce2dbeff9734bdbe17dcadbbe08bbd1`
- commit subject: `Add live arm IK tuning`

The exact source diff is:

```powershell
git diff 583fb47d6ce2dbeff9734bdbe17dcadbbe08bbd1 `
         46f99c64343832d03fc50c564f684bcf2faff39c
```

A mail-format patch can be produced with:

```powershell
git format-patch -1 --stdout `
    46f99c64343832d03fc50c564f684bcf2faff39c `
    > fear-vr-arm-ik.patch
```

Do not expect a blind cherry-pick into Condemned VR to compile. The portable
math is isolated, but the engine integration is embedded in F.E.A.R. VR's
`stereo_hook.cpp` and refers to its player-body discovery, tracked hand state,
two-hand weapon state, settings, logging, and floating menu.

Two later commits affect inputs to the IK integration but do not change
`arm_ik.h` or `test_arm_ik.cpp`:

- `ee9d0006382c2f0642fa64f2461eed4ab29d4f27` makes the support hand a rigid
  weapon pivot. Port it only if Condemned VR has equivalent two-hand weapon
  handling.
- `4bcd610d904478a310b0dfc39a612b576115027a` corrects room-scale and tracking
  bases. Condemned must ensure its hand targets and player-body model transform
  are in the same world space, whether or not this exact correction is used.

The newly written solver and integration are covered by this repository's MIT
license. Preserve `LICENSE` and the original commit attribution when porting.

## Relevant files

| File | Porting role |
|---|---|
| [`src/common/arm_ik.h`](../src/common/arm_ik.h) | Engine-independent vector helpers, tuning structure, sanitization, and analytic two-bone elbow solver. This is the principal portable file. |
| [`tests/test_arm_ik.cpp`](../tests/test_arm_ik.cpp) | Portable solver tests. Copy with the solver and adapt only namespaces/build wiring. |
| Donor `src/gameclient_loader/stereo_hook.cpp` | LithTech integration: tracked targets, node/socket discovery, measured bone lengths, node callbacks, continuity state, settings, reset paths, menu text/actions, and arm material handling. Port selected regions rather than the entire file. |
| Donor `src/common/vr_menu_model.h` | `NextVrSteppedValue`, used by fine live-tuning rows with wraparound. |
| Donor `src/common/dev_menu_model.h` | Engine-independent floating-panel ray hit testing. Useful if Condemned VR does not already have an equivalent menu. |
| Donor `tests/test_vr_menu_model.cpp` | Covers stepped values, snapping, invalid input, and wraparound. |
| Donor `tests/test_dev_menu_model.cpp` | Covers tab/row hit testing after the IK tab was added. |
| Donor `tests/CMakeLists.txt` | Registration for `arm_ik`, `vr_menu_model`, and `dev_menu_model`. |
| Donor `docs/OPENXR-INPUT.md` | User-facing description of the IK tab and left-hand correction behaviour. |

Useful symbol locations in the audited donor branch:

- `ArmIkTuning`, `SanitizeArmIkTuning`, `SolveTwoBoneElbow`:
  `src/common/arm_ik.h`
- `HandNodeControlState`: `stereo_hook.cpp:188`
- `EffectiveLeftHandPosition` / `EffectiveLeftHandRotation`:
  `stereo_hook.cpp:570` and `:588`
- settings load/save: `stereo_hook.cpp:1763` and `:1954`
- menu row formatting: `stereo_hook.cpp:4552`
- node solve and callbacks: `stereo_hook.cpp:9190-9461`
- node discovery/installation: `stereo_hook.cpp:9547-9638`
- player-body installation entry: `stereo_hook.cpp:9955`
- menu actions: `stereo_hook.cpp:11086`

Line numbers are for audited head `4bcd610`; use symbol searches after edits.

## Required data contract

The solver itself needs only:

- shoulder world position;
- desired hand/socket world position;
- measured upper-arm and lower-arm lengths;
- a body-relative pole direction converted to world space;
- the previous valid bend direction, if continuity is enabled;
- the current animated upper-arm direction as a last-resort fallback.

The engine integration additionally needs:

- a valid player-body model object;
- upper-arm, forearm, and hand node handles for each side;
- a hand/weapon socket handle for each side;
- model-to-world and node transforms during skeletal evaluation;
- current OpenXR grip position and grip/aim orientation in the same world
  coordinate system as the model;
- a way to register callbacks that run after animation and before rendering.

F.E.A.R. VR uses `ILTModel::AddNodeControlFn`. Condemned VR must locate the
equivalent model-node callback or hook the equivalent post-animation skeleton
update. Applying the solve before the base animation has populated its node
transforms will produce stale or incorrect bone origins.

## F.E.A.R. skeleton names and signs

These names are verified only for F.E.A.R.'s Retail player body. They are a
discovery guide, not values to assume in Condemned.

| Side | Upper arm | Forearm | Hand node | Target socket | Pole side sign |
|---|---|---|---|---|---:|
| Right | `Right_armu` | `Right_arml` | `Right_hand` | `RightHand` | `+1.0` |
| Left | `Left_armu` | `Left_arml` | `Left_hand` | `LeftHand` | `-1.0` |

The room-scale presentation/root node is `null2`, with `translation` as a
fallback. It is not part of the arm chain, but its correction must already be
represented consistently in the model and controller world transforms.

Before implementing Condemned names, dump the player-body model filename,
node names, socket names, piece names, and materials. F.E.A.R. VR's
`LogRetailPlayerBodyGeometry` demonstrates node enumeration, but currently
logs nodes rather than sockets. Add a socket dump in Condemned if its SDK
supports enumeration.

Confirm all of the following in a live Condemned model:

1. Which object is the first-person/full-body player model.
2. The actual upper-arm, lower-arm, hand, and weapon/grip socket names.
3. Whether transforms requested as world transforms include all parents.
4. Callback order is parent before child: upper arm, forearm, then hand.
5. The model is not replaced across weapon switches, cinematics, damage
   states, level changes, or alternate costumes without reinstalling controls.

## Installation and measured geometry

`InstallHandNodeControl` performs the following once per player-body object:

1. Resolve the upper-arm, forearm, hand node, and hand socket.
2. Read their current world transforms.
3. Cache the socket transform relative to the hand node:

   ```text
   socketFromNode = inverse(handNodeWorld) * socketWorld
   ```

4. Measure the upper segment from the relative upper-arm-to-forearm
   translation:

   ```text
   upperVector = translation(inverse(upperWorld) * forearmWorld)
   upperLength = length(upperVector)
   ```

5. Measure the lower segment from the relative forearm-to-socket translation:

   ```text
   lowerVector = translation(inverse(forearmWorld) * socketWorld)
   lowerLength = length(lowerVector)
   ```

6. Reject missing or effectively zero-length geometry.
7. Register callbacks for upper arm, forearm, and hand. If any registration
   fails, remove earlier callbacks and clear the complete hand state.

The cached vectors retain the model's authored local bone axes. Their
magnitudes supply lengths; their rotated directions let the integration rotate
each existing bone toward the result without assuming a canonical bone axis.

## Per-frame solver flow

### 1. Prepare tracked targets

F.E.A.R. VR converts OpenXR poses into LithTech world space during the weapon
manager update. The right target uses the weapon-hand grip position. The left
target comes from `EffectiveLeftHandPosition` and is either:

- the free left controller grip pose; or
- the stored two-hand grip point transformed by the current weapon pose.

Do not feed the left hand's visually corrected target back into weapon-control
logic. The raw controller drives the weapon; the corrected target drives only
the rendered hand/arm. This avoids a hand-follows-weapon-follows-hand feedback
loop.

### 2. Build the body-relative pole

For each upper-arm callback, extract world-space body axes from the model
transform and form:

```text
pole = bodyRight * sideSign * elbowOutward
     - bodyUp                * elbowDown
     - bodyForward           * elbowBack
```

The constant body-relative pole prevents locomotion and weapon animations from
steering the elbow plane every frame.

### 3. Solve the elbow analytically

`SolveTwoBoneElbow`:

1. Validates finite segment lengths greater than `0.01` engine units.
2. Normalizes shoulder-to-target direction.
3. Clamps solve distance to:

   ```text
   minReach = abs(upperLength - lowerLength) + 0.01
   maxReach = upperLength + lowerLength - 0.01
   ```

4. Uses the law of cosines to calculate distance along the reach axis and the
   perpendicular bend height.
5. Projects the pole onto the plane perpendicular to the reach direction.
6. If continuity is enabled, projects the previous bend into the new plane,
   keeps the same hemisphere, and blends toward that remembered plane near the
   pole/reach singularity. Pole influence ramps from zero to full as normalized
   pole strength moves from `0.05` to `0.35`.
7. Falls back to the animated upper-arm direction if no usable pole/history is
   available, then to a deterministic world reference for a straight bind
   pose.
8. Returns elbow position, bend direction, clamped distance, clamp flag, and
   validity.

### 4. Rotate the upper arm

The upper callback computes the current authored upper-bone direction by
rotating `forearmOffsetFromUpperArm`. It creates the shortest rotation from
that direction to `desiredElbow - shoulder`, applies it to the current world
rotation, converts back into model/object space, and writes only the upper-arm
rotation. The solved elbow and bend direction are cached for the hand.

### 5. Rotate the forearm

The forearm callback runs after its parent has moved the elbow. It rotates the
authored forearm-to-socket direction toward `target - currentElbow`, preserving
the current elbow position and changing only lower-arm rotation.

### 6. Place and orient the hand socket exactly

The hand callback solves the hand-node transform from the desired socket pose:

```text
desiredNodeWorld = desiredSocketWorld * inverse(socketFromNode)
desiredNodeObject = inverse(modelWorld) * desiredNodeWorld
```

It writes hand-node position and rotation. Solving against the socket instead
of the hand bone preserves the Retail model's authored hand-axis correction.

Right-hand orientation uses grip position with the weapon/fire aim rotation.
Left-hand orientation uses grip position and grip rotation plus the user
calibration described below.

## Left-hand pose calibration

F.E.A.R. VR originally mixed the left grip position with aim-pose rotation,
which visibly twisted the hand. The corrected path uses grip pose for both
position and rotation.

Translation correction is expressed in the controller's local frame:

```text
localOffset = (rightMeters, upMeters, forwardMeters) * unitsPerMeter
worldPosition = basePosition + rotate(baseRotation, localOffset)
```

Rotation correction is applied after the raw/base grip rotation:

```text
worldRotation = normalize(baseRotation * Euler(pitch, yaw, roll))
```

The exact Euler constructor order is LithTech-specific. Verify Condemned's
quaternion/Euler convention rather than assuming the three constructor
arguments mean the same axes and multiplication order.

Only the left hand has configurable translation/rotation correction in the
current implementation. Generalizing this to a per-hand array is recommended
for Condemned if its right-hand model also needs calibration.

## Configuration fields

All current values are stored in the `[VR]` section of `fearvr.ini`.

| Key | Default | Sanitized range | Menu step | Meaning |
|---|---:|---:|---:|---|
| `IkElbowOutward` | `1.0` | `0.20..2.00` | `0.05` | Body-right component of the pole; multiplied by `+1` right and `-1` left. |
| `IkElbowDown` | `0.45` | `0.00..1.50` | `0.05` | Downward pole component. |
| `IkElbowBack` | `0.15` | `-1.00..1.00` | `0.05` | Backward pole component; negative values bias forward. |
| `IkElbowContinuity` | `1` | Boolean | Toggle | Retain prior bend hemisphere and blend through near-straight singularities. |
| `IkLeftHandRight` | `0.0` m | `-0.20..0.20` m | `0.005` m | Controller-local lateral correction. |
| `IkLeftHandUp` | `0.0` m | `-0.20..0.20` m | `0.005` m | Controller-local vertical correction. |
| `IkLeftHandForward` | `0.0` m | `-0.20..0.20` m | `0.005` m | Controller-local forward correction. |
| `IkLeftHandPitch` | `0.0` deg | `-180..180` deg | `5` deg | Post-grip pitch correction. |
| `IkLeftHandYaw` | `0.0` deg | `-180..180` deg | `5` deg | Post-grip yaw correction. |
| `IkLeftHandRoll` | `0.0` deg | `-180..180` deg | `5` deg | Post-grip roll correction. |

Related but not part of `ArmIkTuning`:

| Key | Default | Purpose |
|---|---:|---|
| `ShowArms` | `0` | Makes complete arms visible for inspection/tuning. F.E.A.R. uses a game-specific material swap. |
| `TwoHandGrip` | `1` | Allows the left rendered hand target to become the stored support point on a weapon. |
| `LeftHanded` | `0` | Mirrors incoming controller state before downstream logic; the internal weapon hand remains the logical right hand. |

`SanitizeArmIkTuning` replaces non-finite values with defaults and clamps all
floats. Re-sanitize after menu edits and after loading external configuration.

Any elbow-pole edit or continuity toggle calls `ResetArmIkBendMemory`. `Reset
All IK` restores `ArmIkTuning{}`, clears bend memory, and releases stored
two-hand placement. Replacing/unloading the player-body object clears the
entire node-control state.

## Floating-menu controls

F.E.A.R. VR opens its world-space panel during gameplay with both grips plus
the right secondary button (`B` on Touch-style layouts). It uses the right
controller/weapon ray. Trigger or right primary (`A`) activates a row; right
stick left/right selects tabs, up/down selects rows, and `B` closes. Gameplay
input is suppressed while open and until the opening controls are released.

The `IK` tab contains seven rows and two pages. Activating row zero toggles
between pages.

Elbows page:

0. `IK PAGE: ELBOWS` -- switch to Left Hand page.
1. `SHOW ARMS` -- toggle arm visibility.
2. `ELBOW OUTWARD` -- advance `0.20..2.00` by `0.05`, then wrap.
3. `ELBOW DOWN` -- advance `0.00..1.50` by `0.05`, then wrap.
4. `ELBOW BACK` -- advance `-1.00..1.00` by `0.05`, then wrap.
5. `ELBOW STABILITY` -- toggle continuity and clear bend memory.
6. `RESET ALL IK` -- restore defaults and release stored two-hand placement.

Left Hand page:

0. `IK PAGE: LEFT HAND` -- switch to Elbows page.
1. `HAND RIGHT` -- advance `-20..20 cm` by `0.5 cm`, then wrap.
2. `HAND UP` -- same range/step.
3. `HAND FORWARD` -- same range/step.
4. `HAND PITCH` -- advance `-180..180 deg` by `5 deg`, then wrap.
5. `HAND YAW` -- same range/step.
6. `HAND ROLL` -- same range/step.

`NextVrSteppedValue` first clamps and snaps to the closest valid step, advances
one step, and wraps from the inclusive maximum back to the minimum. Each row
activation saves settings immediately.

Arm visibility is game-specific. F.E.A.R.'s body combines arms, torso, and
legs in `Body_Group`, so it cannot hide an arm piece independently. It swaps
material slot zero between `chars\\materials\\player_new.Mat00` and a locally
generated alpha-test material `fearvr\\player_body.Mat00`. Condemned must
inspect its pieces/materials and implement its own safe visibility mechanism.

## Tests

### Portable solver tests

`test_arm_ik.cpp` verifies:

- tuning defaults;
- clamping and non-finite fallback;
- a reachable two-segment solve preserves both bone lengths;
- mirrored poles produce mirrored elbows;
- unreachable targets clamp to measured reach;
- crossing a pole-axis singularity retains the prior bend hemisphere;
- invalid lengths and shoulder-equals-target return an invalid solution.

### Menu-model tests

`test_vr_menu_model.cpp` verifies:

- `0.005`-metre position stepping;
- snapping an off-step value before advancing;
- rotation wrap from `+180` to `-180`;
- non-finite fallback to the minimum;
- invalid step leaves the value unchanged.

`test_dev_menu_model.cpp` verifies ray selection of the expanded eight-tab,
seven-row panel geometry. It does not exercise LithTech rendering or text.

### Build/run in F.E.A.R. VR

The tests are registered in the x86 test build:

```powershell
cmake -S . -B build\x86 -A Win32 `
    -DFEARVR_BUILD_PROXY=ON -DFEARVR_BUILD_HOST=OFF
cmake --build build\x86 --config RelWithDebInfo
ctest --test-dir build\x86 -C RelWithDebInfo --output-on-failure
```

For a focused run:

```powershell
ctest --test-dir build\x86 -C RelWithDebInfo `
    -R "arm_ik|vr_menu_model|dev_menu_model" --output-on-failure
```

Condemned VR should retain the portable tests and add integration tests or
diagnostics for its actual skeleton names, callback order, segment lengths,
coordinate conversion, and pose validity loss/recovery.

## Known limitations and porting hazards

1. **Skeleton names are not portable.** All six F.E.A.R. node names and both
   sockets must be discovered again in Condemned.
2. **The model callback is engine-specific.** The solver assumes animated
   transforms already exist and that parent controls affect child transforms
   evaluated afterward.
3. **No clavicle, shoulder translation, torso, or spine solve.** Only upper
   arm, forearm, and hand are controlled. Extreme cross-body and overhead
   poses can look unnatural.
4. **No anatomical joint or twist limits.** The analytic solve preserves
   segment lengths mathematically but does not constrain elbow flexion,
   forearm roll, wrist twist, or self-intersection.
5. **Unreachable hand targets can visually stretch/disconnect.** The elbow
   calculation clamps reach, but the final hand callback still pins the hand
   socket exactly to the controller. The current system prioritizes controller
   contact over anatomical reach. Condemned may prefer to clamp the visible
   hand target too, or add shoulder motion.
6. **Continuity memory is spatial, not temporal smoothing.** It prevents a
   hemisphere flip near a singularity but does not filter controller jitter.
   Reset it on player-model replacement, recenter, handedness changes,
   prolonged tracking loss, and any coordinate-basis discontinuity.
7. **Pole tuning is global and symmetric.** Outward uses a side sign; down and
   back are shared. There are no per-side, per-weapon, stance, or body-model
   profiles.
8. **Only left-hand alignment is configurable.** Generalize to per-hand
   correction if Condemned needs right-hand model calibration.
9. **Euler correction conventions may differ.** Confirm axis order,
   handedness, units, quaternion multiplication order, and model forward axis.
10. **Arm lengths are captured when controls install.** Reinstall if Condemned
    swaps skeletons, scales models, or changes proportions dynamically.
11. **The hand target must share the model's world basis.** Room-scale body
    presentation offsets, recentering, player yaw, and left-handed input
    mirroring must occur consistently before IK.
12. **Two-hand placement is an optional dependency.** Use raw left grip when
    not supporting a weapon. Never feed corrected display pose back into the
    weapon solve.
13. **Arm visibility is unrelated to IK correctness.** F.E.A.R.'s material
    paths and piece-mask workaround must not be copied to Condemned.
14. **Callbacks need lifecycle cleanup.** Remove every registered callback
    before a model is destroyed or replaced; fail closed if only part of a
    three-node chain installs.
15. **Threading assumptions must be rechecked.** F.E.A.R. updates target state
    and evaluates node controls on its game/render path. If Condemned evaluates
    skeletons concurrently, use a coherent per-frame pose snapshot rather than
    mutable globals.

## Recommended Condemned VR port sequence

1. Copy `arm_ik.h` and `test_arm_ik.cpp`; rename only the namespace if needed.
2. Wire and run the portable solver tests before touching game memory.
3. Add diagnostic enumeration for the Condemned player body, nodes, sockets,
   pieces, materials, and model replacement events.
4. Prove one hand socket can be positioned from an OpenXR grip pose without
   the arm callbacks. Verify position, axis, and multiplication order.
5. Resolve and measure one upper/forearm chain; log lengths and reject zero or
   implausible values.
6. Install upper, forearm, then hand controls for one side. Test reachable,
   full-extension, cross-body, overhead, and tracking-loss poses.
7. Add the mirrored side sign and second arm.
8. Add bend memory and reset it on all tracking/model/basis discontinuities.
9. Add per-hand local calibration. Prefer a two-element array even if defaults
   expose only the left hand initially.
10. Add menu rows and persistence only after live transforms are stable.
11. Implement Condemned-specific arm visibility separately.
12. Integrate optional two-hand weapon placement last, keeping raw controller
    input separate from corrected rendered-hand targets.

Acceptance should include: no elbow flips through straight extension, no
stale callbacks after level/model changes, stable results while walking and
turning, correct hand/socket axes, no feedback loop during two-hand weapon
handling, and graceful fallback when a model or tracked pose is unavailable.
