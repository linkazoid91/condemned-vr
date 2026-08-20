# VR interaction authoring mode

Status: **Phase-1 vertical slice implemented and automated-tested, awaiting
live validation**. The implementation is deliberately limited to authoring
and persisting one magazine-insertion socket. It performs no Retail reload,
weapon-action, ammo, model, collision, or inventory mutation. The common math,
editor, wireframe, command parser, exact-name settings round trip, and menu
surface have portable coverage; neither headset presentation nor live engine
timing is proven.

Implementation entry points are
`src/common/condemned_interaction_authoring.h`, the `AUTHOR` page in
`renderer_probe.cpp`, and `tools/set-condemned-magazine-socket.ps1`.

## Purpose and boundary

The authoring mode extends the existing Grip, 2-Hand, Collider, controller-
wireframe, VR Tools, per-weapon settings, and acknowledged live-command
systems. It gives a headset user a common way to place interaction metadata on
an equipped model without baking new reverse-engineered assumptions into the
runtime.

All authored transforms are model-local. Runtime object pointers are used only
through the existing lifetime-validated visible-model source and are never
persisted. One verified metre remains 100 LithTech units, so a model-local
position or dimension of one unit is displayed as one centimetre. Rotations
are displayed in degrees; runtime composition uses normalized quaternions and
the existing centralized coordinate conversion.

“Read-only” in Phase 1 means read-only with respect to Retail. The mode may
change a mod-owned draft, write a versioned record to the existing player INI,
and draw overlay geometry. It must not:

- dispatch a Retail command or call a native weapon/reload/action function;
- read or write ammo, inventory, animation, collision, or weapon-state fields;
- move a Retail magazine, weapon, hand, socket, bone, or physics object;
- guess a model hierarchy, bone/socket name, object layout, offset, RVA,
  vtable slot, or native ABI; or
- treat an automated test, a drawn ghost, or a successful settings write as
  evidence that a Retail interaction exists.

The existing render-only equipped-model path may supply the already validated
model world transform. Authoring adds no new model transform write. Every
authoring line is projected with the existing per-eye camera and overlay-line
path.

## One primitive vocabulary

The editor presents existing and future settings through a small set of
mod-owned wireframe primitives. This is an authoring facade, not a migration
of the accepted Grip or Collider records into one new monolithic format.

| Interaction | Model-local representation | Existing/potential record |
|---|---|---|
| Primary grip | pose axes plus a small sphere and ghost controller | Adapter over existing `grip` primary position/rotation |
| Support grip | pose axes plus spherical grab volume | Adapter over existing `grip` secondary offset/radius |
| Melee collider | capsule plus axis | Existing `collider` and `block_collider` records |
| Magazine grab | oriented box or capsule plus pose axes, owned by an explicitly identified magazine model or mod ghost | Future `magazine_grab` record |
| Magazine insertion | oriented socket box, seated pose axes, and one constrained approach rail | New `magazine_socket` record; first slice |
| Bolt/pump travel | handle sphere/capsule constrained to a finite rail | Future `weapon_action_rail` record |
| Muzzle | origin sphere, axes, and forward line | Future `muzzle_locator` record; never silently replaces the accepted Retail fire origin |
| Holster | oriented box and snapped pose axes | Future `holster_socket` record |

For the existing support-grip and collider data, the facade must preserve the
current attachment algebra rather than reinterpreting serialized values. With
model-local Grip `G`, controller-local support point `S`, and controller-local
collider frame `T`, the displayed model-local forms are `G * S` and `G * T`.
An authored model-local edit converts back through `inverse(G)`. A Grip change
continues to use the existing rebase logic so the visible attachment stays in
the same place on the model.

New primitives store their pose directly in the local frame of an explicitly
named owner. The first slice supports only `owner=equipped_weapon_model`. A
later magazine grab must use either a separately lifetime-verified magazine
model source or a clearly labelled mod-owned magazine ghost. It may not infer a
Retail magazine child object or bone.

### Common primitive data

The portable common layer should expose allocation-free, finite-value-checked
types along these lines:

```text
ModelLocalPose
  position_cm: x, y, z
  rotation_degrees: pitch, yaw, roll

SpherePrimitive
  pose + radius_cm

CapsulePrimitive
  pose + length_cm + radius_cm

BoxPrimitive
  pose + half_extents_cm

RailPrimitive
  pose                         # local +Z is the rail direction
  approach_length_cm           # approach is -Z; seated is t = 0
  lateral_snap_distance_cm
  angular_snap_tolerance_deg
```

Euler angles are a UI/serialization format only. A candidate is converted to a
normalized quaternion, recomposed, and range-checked before it can replace the
current value. Invalid or non-recomposable data leaves the last valid value
authoritative.

## Phase contract

### Phase 1 — visual authoring only

Phase 1 may:

- resolve a stable Retail weapon index, resolved catalog name, live model
  generation, and the already validated displayed-model transform;
- transform a fresh controller grip pose into that model-local frame;
- edit a mod-owned primitive, draw its wireframe, draw a constrained ghost,
  maintain undo/reset state, and persist a per-weapon record; and
- accept an external authoring command through the existing per-run,
  revisioned, PID/weapon-targeted acknowledgement pattern.

It has no gesture state machine and no Retail action output. Projecting a hand
onto a rail for a ghost preview is geometry, not gesture detection.

### Phase 2 — gesture observation

Phase 2 may add state machines for grab entry/exit, rail progress, seated
thresholds, bolt/pump endpoint crossings, and holster entry. It may animate
only mod-owned ghosts and emit structured logs. Every event must state:

```text
phase=2 engine_handoff=none ammo_mutation=0 weapon_state_mutation=0
```

The useful diagnostic stages are:

```text
INPUT -> MODEL-LOCAL TRANSFORM -> PRIMITIVE OVERLAP/RAIL STATE
      -> GESTURE DECISION -> ENGINE HANDOFF: NONE -> RESULT: LOG/GHOST ONLY
```

Focus loss, menu closure, stale tracking, owner identity/generation change,
or a malformed pose cancels the observed gesture without a delayed completion.

### Phase 3 — verified Retail handoff only

Phase 3 remains **blocked** until a separate evidence task identifies and live
accepts a Retail reload/action handoff, including executable/module identity,
surrounding bytes, ABI, ownership, timing, and fallback behavior. The
authoring/gesture code must depend on a narrow capability interface such as
`VerifiedRetailWeaponActionHandoff`, not on an address or object field.

If no verified capability is installed for the exact weapon/action, the only
valid result is `handoff_unavailable`. A later implementation may emit one
bounded edge to that adapter after a qualified gesture. It may not directly
change ammo, chamber, magazine, animation, inventory, or weapon state, and it
may not repeatedly stimulate a command to imitate a missing handoff.

## Authoring interaction

Add one `AUTHOR` tab to the existing VR Tools menu, guarded by a developer
launch option that requires the current tool menu, controller wireframes, and
visible-model source. The first build exposes only `MAGAZINE INSERTION`; later
primitive kinds remain absent rather than disabled placeholders.

The implemented slice deliberately reuses the existing
`-WeaponGripCalibration` gate instead of adding a second overlapping launch
switch. The `AUTHOR` page therefore cannot arm without the accepted
visible-model/calibration prerequisites.

The existing menu chord, capture/release behavior, and controls remain
authoritative:

- left/right trigger changes tabs;
- left stick selects a row;
- right stick left/right changes one discrete value;
- A activates capture, undo, or reset;
- B closes; and
- keyboard controls retain the existing equivalents.

While `AUTHOR` is open, the cyan left-controller wireframe is the authoring
cursor and the right controller continues to identify the held-weapon basis.
The menu already captures gameplay input and releases the support-hand
attachment, so the cursor cannot also run, fire, grab support, or leak an
action into Retail.

The compact page has a constant selectable-row count:

1. `PRIMITIVE` — `MAGAZINE INSERTION` in the first slice.
2. `COMPONENT` — cycles position X/Y/Z, rotation X/Y/Z, box width/height/depth,
   rail length, snap distance, and snap angle.
3. `VALUE` — right-stick adjustment of the selected component.
4. `STEP` — A or right-stick toggles Fine or Coarse.
5. `CAPTURE LEFT GRIP AS SEATED POSE` — creates the first candidate or
   replaces only its pose from a fresh same-sample cursor/model pair.
6. `UNDO` — restores and persists the preceding committed value.
7. `RESET TO BASELINE` — restores the immutable baseline loaded on entry.

Non-selectable rows show the complete position in centimetres, rotation in
degrees, box dimensions, rail length, snap tolerances, current persistence
result, and ghost errors. Fine/coarse defaults should reuse the established
Grip scale family: 0.1/1.0 cm for translation and dimensions, and 0.25/5.0
degrees for rotation. Bounds are mod authoring safety limits, not claims about
Retail geometry.

Undo is a bounded, allocation-free stack of 32 complete primitive snapshots.
Each successful discrete edit is one entry; held-repeat input may coalesce
until neutral. A failed validation or settings write creates no undo entry and
does not change the authoritative runtime value. Reset itself is undoable.
The immutable baseline is the exact player/packaged record loaded on entry, or
explicitly **unconfigured** when none exists. First capture is undoable but does
not replace that baseline. No weapon-specific identity/default socket is
invented.

Capture supplies only the controller-derived pose and then exposes neutral,
weapon-agnostic authoring values (one-centimetre box half-extents, 10 cm rail,
2 cm snap distance, 15-degree tolerance). These visible placeholders are not
embedded index-76 geometry or claims about a Retail magazine.

## Ghost and snap visualization

The magazine-socket slice draws only wireframes:

- a white/cyan oriented box for the authored socket opening;
- RGB axes at the fully seated pose;
- a yellow constrained rail from `t=-approach_length` to `t=0`;
- the existing cyan left-controller wireframe; and
- an amber magazine-proxy box at the raw cursor, a cyan ghost at the projected
  rail pose when both lateral and angular tolerances pass, and a green ghost
  when the projected pose reaches the seated endpoint.

Let `O` be the lifetime-validated displayed weapon-model world transform and
`H` the fresh left-grip world transform. The raw model-local cursor is
`L = inverse(O) * H`. The closest rail parameter is the clamped dot product of
`L.position - socket.position` with the socket's local +Z axis. Snapping may
change only the ghost transform. The authored record changes only after an
explicit menu edit or capture.

The page reports:

```text
RAIL  7.4 / 12.0 CM
LATERAL ERROR  1.2 CM
ANGLE ERROR  6.5 DEG
GHOST  SNAPPED | FREE | SEATED
```

`SEATED` is a visual classification at the `t=0` endpoint. It is not a reload,
an ammo observation, or proof of a Retail magazine state.

## Persistence and live acknowledgement

Reuse `weapon_settings_store` path resolution and precedence. The new record
lives under the stable weapon section, is written only to
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`, and may later receive a
project-authored packaged default. Player-over-package precedence, malformed
player-value rejection, and no writes to packaged/Retail files remain
unchanged.

The record is independent of Grip, Collider, Melee, Hand IK, and Block data so
an authoring schema change cannot invalidate accepted tuning. A concrete v1
payload contains:

```text
magazine_socket = version,exact_catalog_name,
                  configured,
                  position_xyz_cm,rotation_xyz_deg,
                  half_extents_xyz_cm,approach_length_cm,
                  snap_distance_cm,snap_angle_deg
```

The weapon index is already part of `[weapon_<index>]`. Load and save still
require the current stable index, an exact resolved catalog name matching the
stored name, a live source generation, finite values, and bounded dimensions.
An unknown name, mismatch, malformed player override, or unsupported owner
fails closed to `UNAVAILABLE`; the record never inherits Pipe or another
weapon's socket.

Follow the settings-first mutation pattern: validate the candidate, write it,
then publish it to the runtime slot only when the write returns `Ok`. This
keeps menu state, ghost state, and disk state from silently diverging.

The existing per-run command file remains the transport. Preserve version-1
Collider commands unchanged and add a separately parsed version-2 authoring
grammar with:

```text
version=2 revision=<n> base_revision=<n> pid=<pid>
weapon_index=<index> weapon_name=<exact_name> configured=<0|1>
pos=x,y,z rot=x,y,z half=x,y,z
rail=<cm> snap_distance=<cm> snap_angle=<degrees>
```

The game polls at the existing bounded rate. It rejects an old revision,
wrong PID/index/name, stale base revision, invalid value,
unavailable model generation, or failed persistence. A successful command
updates the mod-owned record and publishes an acknowledgement containing the
exact applied values. The first-slice sender reads the latest exact active
state and waits for the matching applied/rejected event in the loader log;
extending the diagnostics watcher schema remains future work.

Implemented events are:

- `m5_live_magazine_socket_armed`;
- `m5_magazine_socket_settings_loaded` / `_load_rejected`;
- `m5_magazine_socket_authoring_applied` / `_save_failed`;
- `m5_magazine_socket_authoring_gizmo_active` / `_failed`; and
- `m5_live_magazine_socket_applied` / `_rejected`.

Every Phase-1 event includes `phase=1`, `engine_handoff=none`,
`ammo_mutation=0`, `weapon_state_mutation=0`,
`retail_state_mutation=0`, the exact weapon index/name/source generation, and the
record revision.

## First vertical slice: magazine insertion socket

Implement only this sequence:

```text
stable equipped weapon/model
  -> open AUTHOR / MAGAZINE INSERTION
  -> capture left grip as seated model-local pose
  -> adjust one component with fine/coarse steps
  -> see box + rail + snapped ghost and cm/degree errors in both eyes
  -> undo, adjust again, reset, then make the intended edit
  -> persist exact per-weapon record
  -> drop/reacquire and relaunch
  -> load and draw the same record
```

The slice does not author primary/support grips again, magazine grab, a
magazine object, bolt/pump travel, muzzle, holster, gesture detection, reload,
or action dispatch. Those remain vocabulary/design targets only.

Choose the representative only from an already stable, positively identified
held firearm such as the live-evidenced index-76 model. The code must not embed
a presumed magazine socket, magazine dimensions, bone, or child object for
that index. The first pose comes from the tester's explicit controller capture.

### Headset-free gate

Add portable tests for:

- model/world transform round trips and centralized 100-units-per-metre
  centimetre presentation;
- box/rail finite-value and range validation;
- rail projection, clamping, lateral error, angular error, snapped/free/seated
  classification, and stale-pose rejection;
- wireframe construction and per-eye projection within existing line/triangle
  caps;
- fine/coarse component adjustment, 32-entry undo, coalescing, reset, and
  failed-save non-mutation;
- menu navigation and input capture without Fire/Activate leakage;
- player/package precedence, exact index/name matching, round trip, malformed
  player override rejection, and no cross-weapon fallback; and
- version-1 Collider-command compatibility plus version-2 authoring command
  parsing, target/base-revision rejection, and applied acknowledgement.

These tests establish only mod-owned math, UI, serialization, and guards.

### Phase-1 live gate

Run the smallest canonical headset configuration with `-WeaponGripCalibration`
and preserve the ordinary launch report, host/bridge logs, loader log, source
state, staged hashes, exact weapon index/name, and before/after player INI.

Accept only when:

1. the mode arms for the exact live weapon/model generation and remains
   unavailable for Unarmed, unknown identity, stale tracking, or a missing
   model;
2. capture, fine/coarse edit, ghost snapping, centimetre/degree readouts,
   undo, and reset are visually correct in both eyes;
3. menu input produces no Retail Fire, Activate, reload, block, inventory, or
   other command edge and no delayed edge after close;
4. every record states `phase=1 engine_handoff=none ammo_mutation=0
   weapon_state_mutation=0 retail_state_mutation=0`;
5. a successful save reloads after drop/reacquire and a fresh process for only
   that exact weapon identity;
6. an external revision is acknowledged with the exact applied values and a
   deliberately stale/wrong-weapon revision is rejected without mutation;
7. Grip, 2-Hand, Collider, Hand IK, firearm direction/fire, controller input,
   keyboard/mouse, host-absent behavior, and exact model restoration remain
   unchanged; and
8. frame timing remains acceptable with the authoring drawings visible.

A persisted socket and convincing snapped ghost are still only **Phase 1 live
verified**. They do not promote magazine grabbing, insertion detection,
reload, chambering, ammo mutation, or any native handoff.

## Promotion order

After the slice passes, extend one uncertainty at a time:

1. Add Phase-2 read-only magazine grab/insertion gesture logs using the
   accepted socket and a mod-owned ghost.
2. Separately investigate a Retail reload/action handoff with bounded,
   observation-first evidence. Do not put that investigation inside the
   authoring editor.
3. Only after that handoff is independently live accepted may Phase 3 connect
   one qualified insertion edge to it.
4. Add one further primitive—preferably bolt/pump rail or muzzle locator—using
   the same editor, settings, visualization, undo, identity, and acknowledgement
   machinery. Do not implement the entire interaction catalog in one change.
