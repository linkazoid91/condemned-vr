# Condemned VR roomscale investigation and implementation plan

Status: **RS1 live-rejected for performance and rolled back; plan/evidence retained**
Target: verified Steam `Condemned.exe` / `GameOrig.dll` `1.0.314.0`
Date: 13 August 2026

This document defines a safe path from the current camera-only HMD tracking to
roomscale movement that is owned by Condemned's player controller. A read-only
RS1 correlation candidate was implemented, automated-tested, and exercised
live on 13 August 2026. The tester reported that it ran horribly, so the
runtime, launcher switch, and dedicated test were rolled back. The plan and
captured evidence remain; roomscale movement is not implemented or live
verified.

The intended result is:

- physical HMD yaw turns the in-game player as well as the view;
- physical horizontal HMD movement moves the player character through
  Condemned's normal collision and movement path;
- HMD pitch and roll remain head/view motion rather than tilting the player;
- physical height remains a view/stance concern rather than moving the player
  vertically through floors; and
- menus, scripted cameras, tracking loss, focus loss, non-VR play, keyboard and
  mouse, and host-absent behavior retain their existing fallbacks.

## Evidence summary

These labels follow the repository evidence rules. Do not promote one level to
another without its required gate.

| Finding | Evidence level | Boundary |
|---|---|---|
| Relative HMD yaw/pitch/roll drives the stereo camera | **Live verified, partial system** | Responsive tracking and yaw-only recenter passed M3; the intermittent Retail base-pitch report remains unresolved |
| Relative HMD translation drives the stereo camera | **Live verified** | Camera-only, capped at 0.25 m, restored after each eye; it does not move the gameplay player |
| Commands 0/1/3/4 move the Retail player | **Live verified** | Existing left-stick overlay preserves Retail command ownership and keyboard input |
| Command 23 turns the Retail player | **Live verified** | Existing right-stick overlay goes through `GetExtremalCommandValue`; exact roomscale-yaw control is not proven |
| Retail queries continuous ForwardAxis 2 and StrafeAxis 5 | **Static/automated-only investigation** | Verified module bytes contain the calls; signs, scale, game-state behavior, and live player response still need a bounded gate |
| The player-body model follows Retail locomotion and turning | **Live verified** | Arm-IK targets remain body-local during movement; the model is not proof of the authoritative physics root |
| A `CMoveMgr` singleton/player-object candidate exists | **Live-exercised diagnostic, rejected as usable; identity semantics remain a hypothesis** | The attempted trace produced only a transient partial observation, the preserved loader file is not the RS1 stream, the sequence was incomplete, and performance was unacceptable; the implementation was rolled back |
| Native-command roomscale can preserve collision and scripts | **Design hypothesis** | Requires the player-root diagnostic, small command pulse gates, collision tests, and full live regression |

Current evidence lives in
[`CONDEMNED-M3.md`](CONDEMNED-M3.md),
[`CONDEMNED-M4.md`](CONDEMNED-M4.md),
[`CONDEMNED-M5.md`](CONDEMNED-M5.md), and
[`CURRENT_STATE.md`](CURRENT_STATE.md). Coordinate conversion and camera
invariants remain owned by
[`COORDINATE-SYSTEM.md`](COORDINATE-SYSTEM.md).

## Existing behavior and the missing handoff

The x64 host creates an OpenXR `LOCAL` reference space and publishes predicted
eye poses. The x86 loader establishes a yaw-only recenter pose, converts the
relative OpenXR pose centrally, and uses 100 LithTech units per metre.

For translation, the current renderer:

1. computes the centre-HMD displacement from the recenter origin;
2. clamps its total magnitude to 0.25 m;
3. adds it to each temporary eye transform;
4. renders the world eye; and
5. restores the exact Retail camera transform.

Consequently, a player can lean or take a small physical step, but the Retail
player object, collision capsule, triggers, movement direction, body model, and
server/gameplay position do not move with that step.

Rotation has the related ownership gap. The final view currently composes the
untouched Retail camera basis with relative HMD rotation. Physical yaw therefore
changes what the player sees, but it is not itself a Retail player-turn input.

Roomscale must add a player-controller handoff without applying the same motion
twice to the final camera or to controller/weapon poses.

## Bounded static findings

The following was observed read-only in the project-local staged
`GameOrig.dll` whose SHA-256 is the supported
`0AC9798CA460C3E24EFC6D103D5FD258CCA6C921E0BD2A3FD9119D1C7C5228CC`.
It is evidence for diagnostics, not authority for a write path.

### Continuous native movement axes

The already verified `GetExtremalCommandValue` function is called for:

- ForwardAxis command 2 at `GameOrig+0x000329D3`; and
- StrafeAxis command 5 at `GameOrig+0x000329E5`.

Those calls occur in the same player-side control flow. This makes commands 2
and 5 the preferred first movement experiment because they retain Retail's
movement, collision, animation, trigger, and special-state ownership. Static
analysis alone does not establish axis signs, usable range, dead zone, whether
the values are read more than once per update, or their behavior in every game
state.

### Candidate player movement root

Static references strongly suggest a movement-manager singleton pointer at
`GameOrig+0x00168EEC`:

- a constructor-like path assigns its object at `GameOrig+0x00038914`;
- a destruction-like path clears it at `GameOrig+0x00039E47`; and
- the constructor installs `GameOrig+0x00138960` at manager offset `+0x04`;
  and
- the verified callsite beginning at `GameOrig+0x00011FF9` loads the engine
  client, reads the singleton's `+0x10` object, and invokes transform-getter
  vtable slot 21 at `GameOrig+0x00012030`. The accepted slot target is
  `Condemned.exe+0x0000C750`.

The class identity, object-reference field, transform semantics, lifecycle, and
relationship to the authoritative collision capsule remain **hypotheses**.
No candidate field or method may drive roomscale until a bounded read-only live
trace correlates it with actual player movement and rotation. Surrounding-byte
and supported-module identity checks remain mandatory even after correlation.

## Design principles

1. **Retail owns movement.** Prefer commands 2, 5, and 23. Do not teleport a
   camera, body model, or guessed player object with `SetObjectPos`,
   `ForceCurrentObjectPos`, or `SetRigidTransform`.
2. **Measure accepted motion.** Collision, stairs, acceleration, special moves,
   and scripts can make requested and actual movement differ.
3. **Apply tracking once.** Movement consumed by the Retail player must be
   subtracted from eye, HMD aim, controller, weapon, IK, and interaction poses.
4. **Update once per simulation frame.** A roomscale controller may publish one
   stable command snapshot for the next Retail update; it must never integrate
   state once per eye.
5. **Yaw is separate from pitch and roll.** Only horizontal HMD yaw can become
   player yaw. HMD pitch and roll remain in the final view.
6. **Vertical movement is not translation of the player root.** Gravity, steps,
   ladders, elevators, and floor contact remain Retail-owned.
7. **Fail closed.** Stale/non-finite tracking, unknown identity, focus loss,
   unsupported state, transform discontinuity, or failed correlation produces
   neutral roomscale commands and the existing Retail/camera fallback.
8. **The current mode remains available.** Every mutation gate must be
   independently reversible to today's bounded camera-only translation.

## Proposed state model

Roomscale needs two related coordinate frames rather than a single mutable
recenter pose:

- **tracking anchor:** the valid HMD pose chosen at gameplay entry or explicit
  recenter;
- **Retail player anchor:** the verified player-root position and yaw at that
  same generation;
- **consumed translation/yaw:** physical motion that Retail has demonstrably
  applied to the player through the roomscale command path; and
- **residual translation/yaw:** physical motion not yet accepted by Retail.

For horizontal translation, expressed in the recentered player basis:

```text
requested_xz = current_hmd_xz - tracking_anchor_xz
residual_xz  = requested_xz - consumed_player_xz
```

For yaw:

```text
requested_yaw = wrap(current_hmd_yaw - tracking_anchor_yaw)
residual_yaw  = wrap(requested_yaw - consumed_player_yaw)
```

The controller requests native movement from the residual. After Retail
updates, only the verified player-root displacement and yaw actually caused by
that roomscale request advance the consumed values. The eye pose, controllers,
weapons, IK targets, and interaction rays use the residual rather than the full
tracking-space displacement.

Conceptually:

```text
OpenXR HMD/controller motion
          |
          v
relative pose in recentered tracking frame
          |
          +-------------------------> requested player XZ/yaw
          |                                      |
          |                                      v
          |                         native commands 2/5/23
          |                                      |
          |                                      v
          |                         Retail movement + collision
          |                                      |
          |                                      v
          +---- minus accepted player motion <---+
          |
          v
residual eye/controller/weapon pose -> stereo render and gameplay consumers
```

This is the central invariant:

```text
accepted Retail player motion + rendered residual = requested physical motion
```

### Translation behavior

The first implementation should use a small hysteretic head-only zone so
tracking noise and ordinary leaning do not continually walk the capsule. Past
that zone, a bounded proportional/damped controller supplies ForwardAxis and
StrafeAxis. Final thresholds and maximum axis values are live-tuning results,
not facts to copy from another game.

The controller must use actual player displacement, not camera displacement.
Retail camera bob, landing motion, height smoothing, damage motion, and scripted
camera offsets are not accepted body movement.

Initial safety behavior when the user also operates left-stick or keyboard
movement should be conservative:

- suspend roomscale translation commands;
- rebase the Retail player anchor only after intentional movement settles; and
- preserve or smoothly resolve the current visual residual without a snap.

Simultaneous stick plus roomscale translation is a later gate. Collision makes
it unsafe to apportion one measured player displacement between two independent
movement requests without evidence.

### Rotation behavior

Player yaw should follow HMD yaw through command 23, using measured accepted
player yaw to remove the consumed part from the rendered HMD yaw. Otherwise the
Retail turn and HMD rotation would be applied twice.

Two user modes are reasonable after the exact-follow proof:

- **Head locked:** player yaw continuously follows HMD yaw.
- **Free-look follow:** a configurable yaw cone permits small head turns; the
  player follows when the head leaves the cone.

The first live mutation gate should prove head-locked behavior because it is
easier to measure. Free-look hysteresis belongs in portable tested math after
the native handoff is known-good.

Right-stick turning must not be misclassified as consumed physical yaw. The
first gate should suspend the head-yaw servo while a stronger Retail/VR turn is
active, then rebase the game-yaw anchor without changing physical pitch/roll.
Simultaneous composition can follow only after telemetry distinguishes both
inputs.

The exact quaternion composition must wait for the existing bounded M3
orientation trace. The unresolved intermittent Retail base pitch makes the
camera quaternion an unsafe proxy for player yaw. The roomscale path needs a
verified yaw-only player basis and must not hide that issue with another local
sign or Euler-angle correction.

### Vertical behavior and physical crouch

HMD Y initially remains bounded camera translation. It must not move the
player object upward or downward, disable gravity, or alter floor contact.
Retail continues to own stairs, falls, elevators, ladders, and camera-height
effects.

Physical crouch is a separate feature. Condemned's inherited command 14 was
already tested and removed because the live binding evaluator did not query it
and Retail exposes no normal manual crouch control. A future crouch animation or
capsule-height handoff therefore requires its own read-only state/path
investigation; it is not part of the first roomscale write gate.

## Pose consumers that must change together

Moving only the camera and player root would double-displace other VR systems.
One shared roomscale-relative pose helper must serve every consumer:

- both eye positions and eye yaw;
- tracked HMD world pose and flashlight basis;
- right and left controller aim/grip world positions;
- visible held model placement;
- firearm and forensic camera rays;
- physical-melee collider pose and swing qualification;
- weapon-weight reference frame;
- both arm-IK socket targets and bend memory;
- block-pose head-relative comparison;
- HUD/tool-menu world anchors where applicable; and
- future interaction reach and haptic contact sources.

Controller positions currently use the untouched Retail camera position plus
the full controller displacement from the tracking recenter. After the Retail
player consumes a roomscale step, that consumed displacement must be removed
before adding the controller offset or the hand/weapon moves twice.

Physical-melee speed needs extra care. Current tracking-space velocity excludes
stick locomotion, but a physical roomscale step is itself tracking-space motion.
Swing qualification should use controller motion relative to the roomscale
body/HMD floor frame so walking into an actor with a stationary hand cannot
become a false high-speed swing. Contact may still exist; native damage
qualification remains a separate decision.

## Collision and blocked movement

Native player movement should provide capsule collision, step handling, ledge
behavior, movement scripts, and trigger traversal. It does not by itself keep a
residual camera/head volume out of a nearby wall.

The collision plan has two layers:

1. **Player layer:** commands 2/5 move the normal Retail controller; accepted
   root displacement is measured after collision.
2. **Head layer:** constrain the remaining camera residual against world
   geometry, with a safety margin and temporal hysteresis. If no safe custom
   sweep has been verified, clamp the residual conservatively and fade/vignette
   while the player controller is stalled.

The repository has a verified engine `IntersectSegment` target for the forensic
camera's existing Retail calls, but only that callsite and the query fields it
modifies are authoritative. A new roomscale query requires a dedicated ABI,
flags, result-layout, player-ignore-filter, and call-safety proof. Do not turn
the forensic hook's partial layout into an assumed general collision API.

Stall handling must:

- detect requested motion with negligible accepted player progress;
- stop continuously pressing the player into a wall;
- retain a constrained residual without oscillation or tangential wall jitter;
- retry only after the physical target or collision situation changes; and
- return smoothly when the HMD moves back toward the player.

Historical upstream context reinforces this requirement. Repository history
contains an engine-independent, tested axis-follow solver, while its associated
runtime documentation records that body-follow was disabled after feedback,
oscillation, and body-lag problems. That is negative design evidence only; no
F.E.A.R.-specific address, layout, or tuning constant is transferable to
Condemned.

## Lifecycle and discontinuity handling

Roomscale commands are eligible only while all of these are true:

- verified executable and client identities;
- Retail game state is Playing;
- normal player-control eligibility has been identified and is active;
- stereo gameplay, not a flat/menu panel;
- OpenXR pose is valid, finite, focused, and fresh;
- Condemned owns the foreground window;
- the player-root identity and generation are current;
- no roomscale diagnostic/tool capture owns the same controls; and
- no large unexplained transform discontinuity is pending classification.

Neutralize commands and reset or advance the roomscale generation for:

- explicit VR recenter;
- OpenXR reference-space change;
- tracking loss and reacquisition;
- focus or foreground loss;
- loading, menu, movie, splash, demo, death, and respawn transitions;
- player-root destruction or replacement;
- teleport, execution, scripted camera, knockdown, ladder, special move, or
  other state that does not use normal player locomotion;
- large frame-time gaps; and
- non-finite or implausible player/HMD deltas.

The host currently does not publish an explicit OpenXR reference-space-change
generation. Add handling for `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`
and transport a generation counter before production roomscale. A large-pose
discontinuity detector remains a fallback, not a substitute for the event.

Moving platforms and elevators require special attribution. Platform motion
must advance the Retail world anchor without being counted as physical HMD
motion. Until a platform/parent signal is verified, a sustained unexplained
player displacement should suspend and re-anchor roomscale rather than pull the
player back toward the old world position.

## Implementation gates

Each gate has an independent launch switch and rollback. Do not combine gates
to save a live run.

### RS0 — Documented design

Status: **complete with this document**.

- Preserve the current evidence boundary.
- No runtime or protocol changes.
- No claim beyond feasibility and bounded static candidates.

### RS1 — Read-only player-root correlation

Status: **live-rejected for performance; runtime rolled back**.

The rolled-back RS1 candidate provided an opt-in `-RoomscaleProbe` that
performed no engine writes and emitted a bounded sample at a reduced
diagnostic cadence. That launcher switch and its runtime implementation are
intentionally no longer present.

Resolve and validate the candidate movement manager only after:

- supported module identity passes;
- the constructor/destructor/global-reference surrounding bytes match;
- the singleton pointer is readable and its object/vtable ownership is sane;
- any candidate player object passes existing engine-object validity checks;
  and
- the read uses a verified engine transform getter only.

The rejected candidate added `-condemnedvr-roomscale-probe`, exposed by
launcher switch `-RoomscaleProbe`. Its launcher enabled the already accepted
stereo, locomotion, turning, and Retail-state prerequisites. At startup, it
verified both module identities, the exact
constructor/assignment/destructor/callsite bytes, the manager `+0x04` marker,
and the executable slot-21 target. Every runtime object read was range-,
type-, finite-value-, and lifetime-checked.

The candidate sampled the verified outer world-render path before any
temporary eye or model transform was applied, at a bounded 100 ms log cadence.
Its events reported HMD pose, freshness/focus, Retail state, candidate root,
independent player-body and untouched camera transforms, root
deltas/generation, raw stick input, and the observed original/final values for
commands 2/5/23. All RS1 records explicitly reported
`roomscale_cmd2=0`, `roomscale_cmd5=0`, `roomscale_cmd23=0`, and
`engine_writes=0`; no solver or body-follow handoff existed. Lifecycle events
covered recenter, state/focus/tracking loss, manager/object replacement, and
flat/stereo suspension.

The historical candidate passed 24/24 x86 and 20/20 x64 headset-free tests,
including its portable pose/yaw/root-generation helper, and the x86 loader
built with the probe. Those counts describe the rejected candidate, not the
current post-rollback tree. They proved only algorithms, guards, and
integration at compile time; they did not establish that the candidate was the
authoritative collision root or promote any roomscale movement gate.

#### 13 August 2026 live result and rollback

Run `run-20260813-131921` used VirtualDesktopXR with game PID 36028 and host
PID 34928. The launcher report recorded `RoomscaleProbe=true`, existing
locomotion/turning/menu prerequisites enabled, foreground handoff success, and
no write-enabled roomscale mode. The tester's direct result was: **it runs
horribly**.

The host log corroborates a rejected timing experience. During extended
sections, game publication was about 17--23 FPS while the host remained near
90 FPS, roughly 221--242 of each 300 host frames reused an older game image,
and average image age was about 13--16 frames with maxima commonly 17--24
frames. Later windows recovered to about 42--43 game FPS. There is no matched
same-scene run without RS1, so these records do not prove that the read-only
probe caused the regression; they do prove that this candidate/run failed its
live usability and timing gate.

A transient inspection suggested that the candidate root stayed fixed during
the observed HMD-only interval and matched the sampled player-body object, but
the required locomotion/turn/collision/lifecycle sequence was not completed.
Moreover, the file copied later as `condemnedvr-loader.log` does not contain
the RS1 event stream, so that observation is not promotable evidence. The
candidate's collision-root semantics remain a hypothesis.

Preserved session files are under
`stage/condemned-m2-mono/logs/run-20260813-131921`:

- launch report SHA-256
  `D4B78761DEDECF66C6710C14579AE01786307368A2D1EC3026F450881C3286AC`;
- host log SHA-256
  `24F88CA88AB84FB09B536D35430E9E0250FF8D2B60313BB83A0C6E0A7A43B187`;
- bridge log SHA-256
  `752F4CD4CC0D16649EDF3B8F5ADF37DF20047C6A799A4224112B8CEB2431444E`;
  and
- the later copied, non-RS1 loader log SHA-256
  `1CE76E5407C1F1EAC6F0F0E721E7BA8B1F583E89790302E5EF8BF3EB6FE64C30`.

The staged RS1 x86 `GameClient.dll` was SHA-256
`F3EE14C0533E74261856B54C0453156C63ED25BC638E6EF4FE22202262EC8D09`.
After the report, the dedicated common/helper files, binding observations,
renderer sampling, loader/launcher gates, and dedicated test were removed.
The post-rollback full gate passes 23/23 x86 and 19/19 x64 tests. The refreshed
project-local stage uses x86 loader SHA-256
`6E624F4ADFC690167B663CD79A9234BC563F2EBB134586A8ECE2D1E581210058`.
No post-rollback game/headset run was made.

Correlate these independent stages:

```text
INPUT      HMD centre pose, controller/stick state, freshness/focus
TRANSFORM  recentered HMD XZ/yaw and centralized LithTech mapping
STATE      Retail game/player state and candidate manager/object lifetime
DECISION   diagnostic classification only; engine_writes=0
HANDOFF    candidate player transform + player-body model + Retail camera
RESULT     measured deltas during physical motion, stick move, and stick turn
```

Required live sequence:

1. Stand still; translate and yaw the HMD without stick input.
2. Keep the HMD still; walk forward/back and strafe with the left stick.
3. Keep the HMD still; turn left/right with the right stick.
4. Walk diagonally, use stairs, touch a wall, and ride any available moving
   platform/elevator.
5. Recenter, open/close the menu, load a save, die/respawn if practical, and
   verify pointer/generation transitions.

Success means one candidate transform:

- remains stable during HMD-only movement;
- translates coherently with native locomotion and collision;
- rotates coherently with native player turning;
- has a stable relationship to the player-body model and camera excluding
  documented camera offsets; and
- has explainable lifetime transitions.

If the candidate fails, keep the gate read-only and trace verified Retail
transform callsites to find the actual controller object. Do not scan for a
plausible position field and write it.

### RS2 — Portable roomscale solver

Status: **blocked on RS1 identity semantics**.

Add engine-independent state/math, preferably isolated in
`src/common/roomscale_math.h`, with tests on x86 and x64. The solver consumes
validated HMD/player samples and produces only a desired command snapshot plus
camera/controller residuals.

Automated coverage must include:

- X/Z conversion and world-scale invariance;
- yaw wrap at plus/minus pi;
- accepted motion reducing residual without double application;
- partial progress, acceleration, overshoot, reverse motion, and settling;
- dead zone and hysteresis;
- wall stall and release without oscillation;
- intentional stick/keyboard movement suspension and re-anchor;
- stick turn and physical yaw separation;
- recenter and reference-space generation changes;
- player teleport/platform-like discontinuities;
- tracking/focus/game-state loss and reacquisition;
- zero, negative, excessive, and non-finite delta time;
- non-finite poses and implausible displacement;
- vertical HMD movement excluded from player commands;
- controller/eye residual sharing; and
- one stable output when a command is queried repeatedly in the same frame.

Tests prove the state machine and guards only. They cannot prove Retail axis
semantics, collision, perception, or live callback timing.

### RS3 — Yaw-only native handoff

Status: **blocked on RS1 and the M3 orientation trace**.

Add `-RoomscaleYaw` after the read-only gate identifies a player yaw source.
The existing command-23 hook may consume a bounded roomscale-yaw snapshot only
while ordinary turning is neutral and all lifecycle gates pass.

Requirements:

- requested physical yaw drives command 23, not a direct rotation write;
- accepted player yaw is measured from the verified player root;
- accepted yaw is removed from the HMD yaw used for the final view;
- pitch and roll remain physical HMD orientation;
- command output is bounded and becomes exactly neutral on every failed gate;
- stronger Retail/VR turn input wins and causes a safe rebase; and
- recenter changes the tracking/body relationship without rotating or
  teleporting the player immediately.

The first live proof should use small, deliberate yaw targets in an empty area,
then 45/90-degree turns, return-to-centre, right-stick coexistence, recenter,
tracking loss, and focus loss.

### RS4 — Continuous-axis proof

Status: **blocked on RS1**.

Before connecting HMD translation, add a separate bounded proof for commands 2
and 5 through the already verified extremal-value hook:

- one axis at a time;
- low magnitude and short duration;
- empty, level area first;
- explicit neutral release;
- record requested value, Retail value, resulting root delta, animation, and
  collision; and
- repeat all signs only after the first direction is understood.

Success requires normal player/capsule movement with no direct command-state or
object writes, no movement after release, and unchanged keyboard/left-stick
fallback. If the axis path affects only camera/animation or bypasses collision,
stop here and investigate a verified native movement-manager method.

### RS5 — Horizontal body follow

Status: **blocked on RS2 and RS4**.

Add `-RoomscaleTranslation` as a separate opt-in gate:

- derive ForwardAxis/StrafeAxis from residual X/Z;
- publish one bounded snapshot for the next Retail update;
- measure accepted player-root displacement after Retail moves;
- subtract only accepted roomscale displacement from all tracked poses;
- suspend/re-anchor around intentional normal locomotion;
- detect stall, overshoot, teleport, and long-frame gaps; and
- preserve current 0.25 m camera-only behavior when disabled or invalid.

Open-area proof precedes wall collision. Test forward, back, left, right,
diagonals, return to the physical origin, then increasing physical distances.

### RS6 — Head collision and blocked-motion comfort

Status: **blocked on RS5**.

Verify a general world-query ABI and object filter before adding a multi-sample
head sweep. Until then, use a conservative residual cap and an explicit
blocked-motion fade/vignette rather than allowing the camera to continue
through a wall.

Live tests include perpendicular walls, glancing angles, corners, door frames,
low ceilings, stairs, ledges, and moving platforms. Body collision, camera
collision, accepted movement, and visual comfort are recorded separately.

### RS7 — Full VR-system integration

Status: **blocked on RS5/RS6**.

Route the common residual/generation through every pose consumer. Re-run:

- head flashlight and HMD aim;
- controller aim/grip and interaction;
- forensic Scanner/Camera alignment and capture;
- firearm visible-barrel direction and firing;
- visible weapon pose and weight;
- arm IK during physical and stick locomotion;
- melee collision, swing speed, native damage, and block pose;
- menu/tool overlays and recenter; and
- haptics, focus, host-loss, keyboard, mouse, and non-VR fallbacks.

Roomscale must not create attacks, interaction edges, haptics, or controller
velocity merely because the body followed the HMD.

### RS8 — Production settings and soak

Status: **blocked on all earlier gates**.

Only after representative live acceptance:

- expose `Roomscale: OFF / HEAD ONLY / BODY FOLLOW`;
- optionally add `HEAD LOCKED / FREE-LOOK FOLLOW` yaw behavior;
- persist safe user-level settings without changing packaged defaults during
  diagnostics;
- retain an immediate in-headset disable/recenter action;
- run save/load, death, execution, ladder, cutscene, elevator, combat, and
  long-session regression; and
- update `CURRENT_STATE.md`, `TESTING.md`, and the relevant milestone evidence
  with exact run IDs, hashes, switches, observations, and rollback.

## Proposed source ownership

The rolled-back RS1 candidate used the probe, binding, renderer, launcher, and
testing ownership below. These rows are retained as a possible decomposition
for a redesigned lower-cost probe; none describes an active roomscale runtime.
Solver, protocol, host-event, IK-residual, and write-enabled rows remain future
gates rather than current architecture.

| Area | Proposed responsibility |
|---|---|
| `src/common/roomscale_math.h` | Pure residual, yaw-wrap, controller, stall, generation, and re-anchor logic |
| `tests/test_roomscale_math.cpp` | Cross-bitness automated state/math coverage |
| `src/condemned_gameclient_loader/roomscale_probe.*` | Condemned-specific player-root identity, transform sampling, and bounded telemetry |
| `binding_input.cpp` | RS1 read-only observations for 2/5/23; later stable per-frame overlays, with no integration inside repeated queries |
| `renderer_probe.cpp` | RS1 once-per-outer-frame HMD/player sampling; later shared residual use for eyes/pose publishers |
| `arm_ik_integration.cpp` | Consume shared roomscale generation/residual; preserve body-local targets |
| `src/host64/openxr_host.cpp` | OpenXR reference-space-change event handling |
| `src/common/protocol.h` | Versioned reference-space generation only if transport is required |
| launcher/tool-menu files | Independent diagnostic switches first; user setting only after acceptance |
| `docs/TESTING.md` | Canonical RS1 correlation gate and later write-enabled live matrix |

Prefer a small isolated controller and snapshot interface over more persistent
state inside the already large renderer and binding hook files.

## Telemetry contract

Use transition logs plus bounded periodic samples rather than logging every eye
or every command query. A useful roomscale sample contains:

- source revision, staged loader hash, supported module identity, and run ID;
- frame/request/sample IDs and timestamps;
- game state, stereo/panel state, focus, foreground, and tracking validity;
- tracking and roomscale generation;
- HMD requested X/Y/Z and yaw in the recentered frame;
- candidate player-root position/yaw and per-frame delta until RS1 passes;
- player-body model and Retail camera transform as independent comparisons;
- consumed and residual X/Z/yaw;
- original/final observed values plus explicit roomscale values for 2/5/23;
- intentional locomotion/turning activity;
- solver state: idle, following, settling, stalled, suspended, or reset;
- reset/suspension reason;
- head-collision result/scale when that gate exists; and
- final camera/controller residual used for the frame.

The rolled-back RS1 format reported the three roomscale command values as zero
and marked consumed, residual, solver, reference-space generation, and
head-collision fields unavailable where those later gates did not yet exist.

Recommended event families:

```text
roomscale_probe_armed / rejected
roomscale_player_root_acquired / changed / lost
roomscale_generation_reset
roomscale_state_changed
roomscale_command_applied / neutralized
roomscale_stall_entered / released
roomscale_reference_space_changed
roomscale_sample
```

Telemetry must make `INPUT -> TRANSFORM -> STATE -> DECISION -> ENGINE HANDOFF
-> RESULT` reconstructable without treating a requested command as proof of
movement.

## Live acceptance matrix

Every write-enabled gate uses a fresh session directory and records the exact
source state, staged hashes, runtime, switches, expected observation, actual
observation, and rollback.

| Area | Required observation |
|---|---|
| Idle | No player drift, camera jitter, command chatter, or body oscillation |
| HMD yaw | View remains one-to-one while verified player/body yaw follows without double rotation |
| HMD X/Z | Verified player/capsule follows in all horizontal directions and returns smoothly |
| Wall | Capsule stops, residual view remains constrained, no clipping or tangential jitter |
| Stairs/ledge | Retail step/fall behavior remains authoritative; no vertical player writes |
| Stick movement | Existing movement works; roomscale suspends/rebases without snapping |
| Stick turn | Existing turn works; physical yaw and body yaw do not double or unwind unexpectedly |
| Recenter | New origin is stable; no player teleport, attack, or delayed command |
| Tracking loss | Commands neutralize immediately; reacquisition starts a fresh generation without a jump |
| Focus/Alt-Tab | Commands neutralize; desktop behavior and clean focus return remain intact |
| Menu/load/death | No roomscale mutation outside eligible gameplay; player-root lifecycle recovers safely |
| Scripted states | Cutscenes, executions, ladders, knockdowns, and special moves retain Retail ownership |
| Moving platform | Platform displacement is not interpreted as physical room movement |
| Weapons/IK | Hands, weapons, colliders, rays, and body remain singly transformed and aligned |
| Melee | Physical walking alone does not qualify as a swing or duplicate native damage |
| Fallback | Roomscale OFF, host absent, stale tracking, keyboard/mouse, and non-VR match existing behavior |
| Timing | Simulation still advances once and roomscale work does not introduce a new pacing regression |

Initial quantitative diagnostics may target a settled body-follow error below
5 cm and yaw error below 2 degrees in an open area, but those are candidate
acceptance thresholds, not live-verified tuning values. Perceptual acceptance
and collision correctness remain mandatory even if numeric error is small.

## Rollback and no-go conditions

Every stage defaults off until its own live gate passes. Disabling a gate must
restore the existing bounded HMD-camera path without requiring a new save,
configuration reset, or Retail-file change.

Stop and return to the prior gate if:

- the player-root source remains ambiguous;
- a candidate transform follows the camera/body model but not the collision
  controller;
- commands 2/5 do not move through normal Retail collision;
- accepted movement cannot be separated from camera bob or scripts;
- yaw cannot be consumed without worsening the existing horizon issue;
- a wall stall produces oscillation, sliding, or camera penetration;
- scripted states cannot be identified conservatively;
- roomscale motion creates false melee, fire, interaction, or haptic events;
- save/load or player-object replacement leaves a stale controller active; or
- keyboard, mouse, non-VR, focus, or host-loss fallback changes.

Direct player-object teleportation is not the fallback for a failed native-axis
gate. The fallback is another bounded read-only investigation of Condemned's
native movement/controller handoff.

## Recommended next action

Do not relaunch the rejected RS1 binary or advance to a write-enabled gate.
First capture a matched normal-stereo performance baseline in the same scene
without roomscale instrumentation, then design a cheaper bounded correlation
probe whose sampling and logging cost can be isolated independently. A future
probe must preserve its loader stream inside the fresh session directory
before any later launch can overwrite the stage-level log. Only a clean,
performance-accepted correlation run can decide whether commands 2/5/23 plus
measured accepted motion are sufficient or whether a deeper native
`CMoveMgr` investigation is required. Do not implement or stage RS2-RS8
movement before that evidence exists.
