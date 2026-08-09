# Current state

Snapshot basis: repository working tree and checked-in evidence reviewed on 10
August 2026. Detailed proof, run IDs, addresses, and historical failures stay in
the linked milestone documents; re-check against newer evidence before use.

## Status vocabulary

- **PASS** — accepted in a live supported-game/headset run.
- **PARTIAL** — useful live behavior is proven, but an end gate is incomplete.
- **EXPERIMENTAL** — opt-in implementation/scaffold, not the final design.
- **BLOCKED** — intentionally held until an earlier gate produces evidence.
- **NOT STARTED** — no implementation yet satisfies the intended system.

“Automated” never means “live verified.” See [`TESTING.md`](TESTING.md).

## Current milestone

**M5 — Condemned-specific gameplay**, currently narrowed to physical melee.
M1 delegation, M2 transport, M3 stereo/tracking, and M4 controller gates have
live acceptance. M0 produced the usable no-ASI baseline, but isolated save-path
persistence and a longer release soak remain unresolved.

Detailed active evidence: [`CONDEMNED-M5.md`](CONDEMNED-M5.md).

## Current objective

Promote the live-accepted Pipe setup into a temporary, fail-closed baseline for
the verified one-handed majority while preserving per-weapon tuning isolation.
The working tree now maps the 26 modeled `WEAP_1HandedDebris` indices plus
Crowbar, excludes `Unarmed`, normal firearm states, two-handers, and unknown
indices, and lets an unedited mapped weapon read the Pipe's Melee/Weapon,
Collider, Grip, and right-hand IK records. The first save for that weapon
shadows only the corresponding fallback record.

Live run `run-20260809-113629` proves the new path at the engine boundary for
Retail index 29 (`Pipe`, profile `one_handed_debris`): all four records loaded
with `source=pipe_baseline`, the native capsule override consumed
`read_mask=0x7`, and three actor contacts across two targets were accepted and
native-forwarded at 8.630--12.175 m/s. Headset-visible enemy reaction/damage is
awaiting the tester's explicit confirmation. Other newly mapped
collision/damage paths remain automated-only until individually sampled live.
The same run selected index 0 (`2x4`, profile `plank`): its existing
Melee/Weapon record loaded from `source=weapon_record`, while Collider, Grip,
and right-hand IK independently inherited `source=pipe_baseline`. This
live-verifies per-record precedence/isolation, but not 2x4 collision or damage;
the 2x4 body was not left seeded for a bounded actor-hit pass.
The live-tested staged loader SHA-256 is
`00F120FA5A5F4C5F0B0609E9B20BF1371BA2FACC95891CF42B839DD2126B40A0`;
the full RelWithDebInfo gate passes 19/19 x86 and 15/15 x64 tests, and the
PowerShell helper/parser checks pass.

The working tree also adds two independent visibility controls at the top of
the VR Tools `DEBUG` tab: `DRAW MELEE COLLIDER` and `DRAW CONTROLLERS`. They
default on and reset on process restart. The gates are draw-only: collision,
contact damage, controller input, grip/IK calibration, and weapon state continue
underneath a hidden overlay. Automated transition tests prove that the toggles
do not affect one another; the complete RelWithDebInfo gate again passes 19/19
x86 and 15/15 x64. The refreshed staged loader SHA-256 is
`FF89DA8555392B4312972637053124CB7E346BB435D6CB05F54315FA206FDE2D`.
Run `run-20260809-124936` live-accepts the UI change. The headset tester
confirmed both overlays hide and restore correctly, while the ordered menu log
records independent `1/1 -> 0/1 -> 0/0 -> 0/1 -> 1/1` transitions. The
collider was then hidden again before contact began. The first actor contact
was accepted/native-forwarded at 12.139 m/s and the bounded run ended with 32
accepted/native-forwarded damage dispatches, 28 rearms, three multi-target
swings, 532/532 clean reference-vector releases, and zero failures. This
proves collider visibility is independent of collision/damage. Earlier attempts
`run-20260809-120422` and `run-20260809-120452` stopped before game start only
because the HMD was unavailable to VirtualDesktopXR.

The 10 August staged build also extends the guarded core controls with Retail's
Tools command 116 on a deliberate right-stick-up gesture (vertical axis at or
above 0.75). Plain Y remains the pause-menu control, and both grips + Y remains
the separate VR Tools chord. The same focus, freshness, playing-state, and
calibration/menu-capture guards apply. The complete RelWithDebInfo gate passes
19/19 x86 and 15/15 x64 tests; the new project-local stage loader is
`C950716B4690A3411C6E0FF18B3CFCCC7FA75D34E307CC65ACB701DD05B6DE94`. This
control extension remains **implemented, staged, and automated-only** until a
crime-scene prompt proves the expected forensic tool behavior in-headset.

The earlier Pipe acceptance evidence remains the reference baseline. Live run
`run-20260808-060131` proved the corrected reference-vector lifecycle at
callback level: its first 512 callbacks contained 42 accepted contacts, 40
native forwards, 510 successfully released live references, repeated 0.12 m
rearms, and zero vector failures. It also exposed a consistent geometry
mismatch: accepted world/prop contact points were 0.1728--0.9229 m outside the
configured pipe capsule.

Run `run-20260808-062240` then captured the deliberate enemy path. Target
`0x38CE54F0` produced five actor-candidate contacts; all five were accepted,
forwarded, and reported vector state `cleared`. The headset tester confirmed
that the pipe hit and defeated enemies during this run, closing the repeat-
damage lifecycle gate. Their configured-capsule gaps were still
0.1403--0.8869 m. Later disassembly proved those callbacks came from Retail's
larger database-sized native body rather than a misplaced drawing. The current
configured-capsule gate blocks them. Speed and energy remain diagnostic until
true-overlap dispatch is live-confirmed and force qualification is restored.

The headset tester confirmed that the configured collider wireframe already
matches the weapon; the temporary live X-axis probe was reverted, so visual
alignment is no longer the active fault. Verified Retail disassembly instead
shows two different shapes. `GameOrig+0x0001FE30` creates Retail's native
collision primitive from database properties `CollisionRadiusScale`, `Radius`,
`LengthDown`, and `LengthUp`. The update at `GameOrig+0x0001FC00` only
applies a transform to that existing primitive. The VR hook changes that
transform, while the green wireframe is independently built from the configured
base, tip, and radius. Adjusting the wireframe therefore never resized Retail's
larger native body.

The staged loader makes the configured capsule authoritative at the
native-dispatch boundary. It measures Retail's target-surface contact point
against the configured capsule and rejects gaps above 0.01 m before target
de-duplication; invalid points fail closed. The live-tested loader is SHA-256
`5CF2BCEADEDA6F236BC6BB28A9389B8658FA029C9E2D34D53DF363956C5E75CE`.
In `run-20260808-074159`, its first 512 distant wall callbacks were all rejected
with `outside_configured_collider`, zero native forwards, 512 clean Retail
reference-vector clears, and measured gaps of 0.575--0.839 m.

The headset tester then moved the drawn collider through enemies. A 40-second
diagnostic poll and final snapshot showed no later callback: the total remained
frozen at 512 with zero accepts/forwards. This falsifies the continuing-callback
hypothesis. A dispatch gate can remove the early false hit, but it cannot create
a new callback when the smaller configured volume later reaches the target.

The native-alignment build is now live-accepted for the pipe. The staged loader
SHA-256 is
`9EE61E2E817F69ED96F59B493A07F83C793E5F5050C88D31CC90534BD44935FD`;
19/19 x86 and 15/15 x64 tests pass. In `run-20260808-082609`, the guarded
database reader armed and the first pipe seed reported `read_mask=0x7`,
`LengthUp=0`, `LengthDown=10.0`, and `Radius=2.5`, proving that Retail consumed
all three configured native dimensions. The transform maps Retail's local +Y
axis from the configured base to tip, with the origin at the tip.

The user confirmed in-headset that it works against enemies. At the confirmation
snapshot, diagnostics contained 574 callbacks, 126 accepted/native-forwarded
contacts, 338 held-contact duplicates, 126 rearms, seven targets, 574 clean
reference-vector releases, and zero failures. Two actor candidates produced ten
actor-classified accepted forwards; every one was inside the configured gate,
with the largest accepted surface gap 0.0095 m. This closes native pipe-collider
alignment and visible contact damage. That live-accepted binary kept
speed and energy diagnostic-only. The 9 August working tree now implements a
speed-only physical contact gate and exposes its actual controls in VR Tools:
`Require Swing`, `Hit Speed`, and `Rearm Travel`. `Live Speed` and `Fast
Enough` are read from the same weighted-weapon frame used by contact
qualification, not from the transitional Retail attack gesture.

Defaults are Require Swing on, 1.25 m/s Hit Speed, and 0.12 m Rearm
Travel. Require Swing off preserves overlap-only diagnosis. Settings persist
per Retail weapon index; version-one records migrate without discarding older
alignment/handling values. Impact energy remains telemetry only. Both x86 and
x64 builds pass 19/19 and 15/15 automated checks, respectively. The refreshed
stage loader SHA-256 is
`4FAC4881981E3D9E0395710A10262C057F8AB7A47E08BEB3E76669CF506E303B`.
Live run `run-20260808-165819` closes the pipe speed-gate and Melee-menu
acceptance boundary. The tester confirmed that speed behavior works and that
the menu is good.

The final persisted pipe values were Require Swing on, Hit Speed 7.25 m/s,
and Rearm Distance 0.12 m. Across 522 compact contact records, all 446
`swing_not_qualified` samples were at 0.083--7.189 m/s, while all 16 accepted
contacts were at 7.256--13.865 m/s. Every accepted contact was native-forwarded
and counted as a damage dispatch. The same run recorded 26 held-overlap
duplicates, 34 outside-collider rejections, 16 rearms, four targets, 522 clean
Retail reference-vector clears, and zero cleanup failures. This live-accepts
the physical Live Speed/Fast Enough display and speed-qualified pipe dispatch.
That run did not stress alternate rearm travel; the later run below does.

Live run `run-20260809-035231` falsified distance-only rearming. With Hit Speed
held at 7.25 m/s, 0.20 m produced double/triple same-target forwards, 0.30 m
produced a double, and 0.35 m produced a triple plus a double. An initial
three-strike 0.40 m pass was clean, but its stress pass re-hit the same target
only seven tracking samples after rearming. Raising the setting to the 1.00 m
maximum still re-hit one actor 16 samples after a multi-target pass. The travel
test was operating as implemented: at roughly 8--10 m/s the weighted pipe tip
can traverse a metre during one follow-through. It did not prove enemy exit or
the end of a swing.

The first replacement build treated travel only as a secondary guard. A first
accepted contact latched each target until configured travel had been reached
and the weighted tip remained below a lower release threshold for three valid
samples. Live run `run-20260809-065827` exercised that design at the still-saved
1.00 m Rearm Travel: 564 callbacks produced 66 accepted/native-forwarded
contacts, 155 `contact_latched` rejections, 113 outside-collider rejections,
230 `swing_not_qualified` rejections, 55 completed rearms, 12 distinct object
pointers, 564 clean Retail reference-vector releases, and zero cleanup failures.
All rearms completed at `3/3` below the 2.00 m/s release threshold; seven
completed target sets contained two or three distinct object pointers, so the
multi-target path was exercised even though the tester did not intentionally
complete a two-enemy sweep.

That run was not a full acceptance. Automatic timeline auditing found two cases
where the same object pointer was accepted twice without an intervening
`swing_completed` event. The raw callbacks changed target node but retained the
same target object. Source audit traced the only silent clear in that interval
to `UpdatePhysicalMeleeContactRearm` resetting the entire latch on a transient
invalid sweep sample.

The current working tree now fails closed across those samples: it preserves
all target latches, cancels partial release dwell, and logs
`m5_physical_melee_contact_latch_held`. Explicit tracking loss and weapon
changes still reset state. A 1.06 m fast-follow-through regression,
speed-jitter tests, and a new transient-invalid-sample regression pass in both
architectures.

Live confirmation `run-20260809-095447` accepts the corrected Pipe lifecycle at
the intended 0.12 m Rearm Travel and 7.25 m/s Hit Speed. Six contacts were
accepted/native-forwarded, 34 same-swing callbacks were blocked, all six rearms
completed at `3/3` no faster than 1.741 m/s against the 2.00 m/s release
threshold, and all 60 Retail reference vectors were released with zero
failures. The automatic invariant reported zero same-target acceptances before
reset. Three invalid-frame contacts occurred only after an earlier swing had
already rearmed, so `InvalidSampleLatchHolds` remained zero: the fail-closed
branch retains direct automated coverage but was not naturally exercised in
this short live run.

Weapon model alignment is now persistent rather than process-local. A versioned
per-weapon `grip` record stores the absolute local grip position, local rotation
correction, and optional secondary-grip enable/offset/radius in
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`. New calibration slots load
that record only when its stored profile identity matches; invalid, stale, or
missing data falls back to authored profile defaults. GRIP and 2-HAND menu
changes and resets auto-save, while continuous fallback calibration saves on
controller Y or keyboard P. Authored base values remain separate so Reset still
has a stable target.

The x86 settings-store tests now cover grip round-trip, profile mismatch,
out-of-range save rejection, and malformed-record rejection. The full
RelWithDebInfo suite passes 19/19 x86 and 15/15 x64. The refreshed staged loader
SHA-256 is
`9D789BCDCFD5FD8372B786BB67B74250B42F5CD68E3906BD275DFAB2F2A4B5E6`.
The final prior Pipe calibration event was recovered into its first record at
position `{2.5, 2.5, -3.5}` and local rotation `{-28, 0, 0}`.

Live run `run-20260809-103049` loaded that record with `result=ok` and
`source=local_app_data`. GRIP-menu adjustments then emitted successful save
events for every step and left the disk record at position
`{2.5, 5.0, -3.5}` with the same rotation. After the game process exited,
`run-20260809-103422` loaded exactly those adjusted values into a new weapon
calibration slot. This closes the live engine save/relaunch/load round trip.
The headset tester confirmed that the visible Pipe returned where they left it
and accepted the current Y value of 5.0. Per-weapon Grip persistence is
therefore live-accepted for the Pipe.


The pipe is the reference implementation for the shared melee path, not an
intended pipe-specific behavior. Every mapped melee weapon should ultimately
use the same sequence: profile-defined capsule follows the weighted controller
pose, Retail creates that native capsule once, collision stays continuous
outside animation windows, fresh overlap plus the shared swing-speed boolean
qualifies damage, the per-target latch stays closed for the complete fast swing,
configured tip travel plus a three-sample low-speed reset rearms it, and Retail
performs the final target/material/damage handoff.
Only profile data should vary: weapon identity, grip, capsule base/tip/radius,
mass/thresholds, and optional support-hand settings.

The executable gate now admits only the explicit mapped one-handed
index/profile pairs. Native replacement still requires the collision owner to
be the local player's weapon, so enemy-owned instances cannot borrow the
current player's profile. Index 29 proves that a second identity reaches the
shared constructor/contact/native-dispatch path; it does not prove the visual
alignment or Retail lifecycle of every mapped asset. Each remaining identity
still needs a bounded live sample. If one takes a different Retail path, it
must be removed or held fail-closed rather than silently retaining the Pipe
baseline.

## Known-good and current systems

| System | Status | Evidence boundary |
|---|---|---|
| Game loading/delegation | **PASS** | M1 live: verified project loader delegated to `GameOrig.dll`; menus, gameplay, and exit passed; Retail install unchanged |
| D3D9 capture | **PASS** | M2 live: pre-`Present` capture of the actual discard swap chain; mono and stereo transport accepted |
| Versioned IPC/shared textures | **PASS** | M2 live: x86 bridge/x64 host connected, transferred completed frames, handled heartbeat loss and clean shutdown |
| OpenXR host | **PASS** | M2 live on VDXR; session-scoped SteamVR also used successfully; startup retry is bounded |
| Frame pacing | **PASS** | 1 Aug live: OpenXR-request clock, newest-complete image policy, near-90 Hz accepted; see performance doc |
| Native stereo | **PASS** | M3 live at 100% render scale and 130% coupled FOV; distinct fuseable eyes, state restoration, no repeated simulation |
| HMD rotation | **PASS** | M3 live full relative yaw/pitch/roll and recenter |
| HMD translation | **PASS** | M3 live bounded relative translation; current path limits unsafe travel and falls back on stale tracking |
| Controller transport/input | **PASS** | M4 base controls pass live; right-stick-up forensic command 116 is staged and automated-only pending its focused live gate |
| Locomotion | **PASS** | M4 live left-stick movement and right-stick turning with keyboard/mouse coexistence |
| Weapon/fire aiming | **PARTIAL** | Right-controller fire-vector path and HMD-relative view/flashlight exist; representative firearms, recoil, tools, and complete gameplay coverage remain M5 work |
| Visible weapon pose | **EXPERIMENTAL** | Profile-calibrated Retail model is overridden only during eye renders and restored exactly; no standalone held object yet |
| Simulated weapon weight | **EXPERIMENTAL** | Bounded player-local damped-spring solver is automated-tested and drives visible/collision pose; it is not collision-constrained physics |
| Melee collision | **PARTIAL** | Pipe reference path and index-29 inherited native geometry/continuous speed-qualified overlap pass live; the other mapped one-handed assets remain unverified |
| Actor contact qualification | **EXPERIMENTAL** | Pipe ownership, 1 cm overlap, 7.25 m/s Hit Speed, and 0.12 m travel-plus-swing-end rearm pass live with zero same-target reaccepts; the transient-invalid hold is regression-tested but was not naturally invoked in the confirmation run |
| Native damage handoff | **PARTIAL** | Pipe damage is live-accepted; index 29 produced three clean actor native-forwards across two targets, with explicit headset-visible damage confirmation pending; remaining assets are unproven |
| Arm/hand IK | **PARTIAL** | Condemned skeleton/chains, callback order, wrist placement, locomotion anchoring, and initial calibrations have live evidence; broad lifecycle/pose regression remains |
| Haptics | **PARTIAL** | M4 bounded input-confirmation pulses passed live; melee-impact impulse haptics and verified weapon-event feedback are not implemented |
| HUD/UI | **PARTIAL** | Retail menu/screen comfort panel and controller menu navigation passed live; VR Tools is developer UI; gameplay HUD/effects/cutscene classification lacks representative acceptance |
| Installer/release | **NOT STARTED** | Safe developer staging and publication audit exist; no install/update/uninstall package satisfies M6 |

## Current pipeline

Authoritative pipe path in the current working tree:

```text
OpenXR right grip position + right aim rotation              [PASS transport]
        |
        v
LithTech world pose + mapped weapon profile                 [IMPLEMENTED]
        |
        +--> optional support-hand orientation               [EXPERIMENTAL; Pipe preset OFF]
        |
        v
player-local bounded weapon-weight solver                   [EXPERIMENTAL]
        |
        +--> Retail equipped-model render override           [TEMPORARY]
        |       -> render left/right world eyes
        |       -> restore exact Retail model transform
        |
        +--> base/tip kinematics and sweep telemetry          [IMPLEMENTED; automated]
                |
                v
        Retail melee collision-body transform                [TEMPORARY; seed required]
                |
                v
        Retail registered collision callback                 [VERIFIED / observed live]
                |
                v
        player-owned + mapped one-hand context + per-swing target de-dupe
        + configurable tip travel                             [IMPLEMENTED; automated]
        + 3-sample low-speed release                           [PASS; pipe live]
        + optional Require Swing / Hit Speed gate              [PASS; pipe + index 29 live]
                |
                v
        Condemned native impact dispatcher                   [PARTIAL; pipe + index 29 forwards live]
                |
                v
        game-owned damage/material effects/AI/sound          [Retail-owned]
                |
                v
        impact-derived VR haptics                            [NOT STARTED]
```

The end state keeps the native dispatcher but replaces both temporary branches
with a lifetime-safe, collision-constrained item rendered from its physical pose.

## Temporary scaffolding

- **Swing-to-Retail-attack adapter:** a tracking-space 3.00 m/s gesture emits a
  bounded command-17 pulse for mapped profiles. For the pipe it seeds the
  Retail collision body; for the fire axe it remains a play-test bridge. Final
  physical melee must not require this Fire edge or Retail animation timing.
- **Render-only Retail weapon override:** proves acquisition, grip calibration,
  alignment, weight, and stereo rendering while restoring the engine object.
  Replace it with a standalone lifetime-safe held item at the constrained pose.
- **Retail collision-body/wall proxy:** redirects the animation-derived physics
  transform and currently requires a Retail attack to create the body. It is a
  bridge to a permanent physical weapon/collision solution.
- **Deferred pickup auto-seeding:** the native body still has to be created once.
  Later work should detect equip, wait for a stable identity/model and fresh
  pose, issue one internal seed-only Retail pulse with damage blocked, and
  require a player-owned body plus native override `read_mask=0x7` before
  declaring the weapon ready. Reset and retry on weapon lifecycle changes.
  A silent direct `EnableCollisions` call remains off-limits until its five
  arguments and ownership contract are verified; do not guess that ABI.
- **Physical-hit qualification:** the working tree now gates contact with
  configurable `Require Swing`, speed-only `Hit Speed`, and tip-displacement
  `Rearm Travel`. Impact energy remains diagnostic. Travel no longer rearms
  during a continuous fast follow-through: three consecutive samples below the
  derived release speed are also required. Transient invalid sweep samples now
  retain the target latch and reset partial release dwell rather than silently
  reopening damage. Geometry, Hit Speed, 0.12 m Rearm Travel, target latching,
  and six repeated swing-end resets pass live for the Pipe. The transient hold
  remains directly automated-tested because it did not fire in the short run.
- **`MeleeAimProbe`:** animation-relative collision steering is a diagnostic
  A/B path, is mutually exclusive with the physical wall proxy, and is not the
  final melee architecture.
- **Calibration/debug helpers:** collider/controller wireframes, Grip/2-Hand
  snapshot controls, discovery probes, and the hand-only IK proof exist to
  establish profiles/layouts. Do not optimize them as final player UX.
- **M4 confirmation haptics:** prove transport only; final recoil/impact haptics
  must come from verified game/contact events.
- **M2-named local stage/launcher:** it is the current M5 developer harness,
  not an installer or release design.

## Do not work on unless required

While this gate is active, avoid unrelated refactors of:

- loader verification/delegation, staging, protocol, D3D9 capture, x64 host,
  shared-texture ownership, request pacing, newest-image selection, and HID fix;
- world-only stereo, camera/FOV restoration, eye polarity, and coordinate maps;
- accepted M4 input/menu/recenter/focus and keyboard/mouse fallbacks; and
- verified RVAs/signatures, weapon indices, skeleton chains, or layouts absent
  specific contradictory evidence.

These are regression dependencies, not cleanup opportunities.

## Testing boundary

Headset-free:

- `tools\build-all.ps1` builds x86/x64 and runs 19/15 CTest tests covering the
  protocol/ABI, state/math, input/UI, weight/melee/IK, logging, identity,
  fail-closed loader/bridge, settings, and background-render guards.
- `tools\test-condemned-m0-tools.ps1` tests the M0 PowerShell parsers.
- `tools\verify-condemned-m0.ps1` and compiled module verifiers need a legal
  game install but no headset; they do not prove hooks in a running process.
- `tools\audit-publication.ps1` checks publication boundaries.

Live testing is required for rendering/projection, perceived stereo/latency,
pacing/OpenXR lifecycle, focus/reset, input/haptics, model callbacks/lifetimes,
collision/native effects, and changed hooks. “Armed” proves installation only.

## Runtime evidence format

Prefer one correlation/sample ID and structured fields through:

```text
INPUT -> TRANSFORM -> STATE -> DECISION -> ENGINE HANDOFF -> RESULT
```

For physical melee, capture at least: tracking/focus/game-state validity,
stable weapon identity/profile, raw grip and aim pose, weighted physical pose,
base/tip and contact-point velocity, sweep validity/distance, target identity,
contact position/normal, configured capsule-to-target surface gap, qualification
result and rejection reason, reference-vector span/clear result, native
dispatcher invocation/result, de-duplication/rearm state, and emitted haptic.
Also retain executable/client identity, enabled/disabled gates, source/binary
hash, OpenXR runtime, and launch switches.

Sources are the launch report, session host/bridge JSONL under
`stage/condemned-m2-mono/logs/run-*`, loader `condemnedvr-loader.log`, and the
performance tools. Gaps: loader records have only `event` and key/value `detail`
(no timestamp/stage/correlation ID) and are not inherently per-session; contact
records lack stable semantic actor/material identity; stages are not joined in
one record; and impact haptics do not exist. Improve only as an experiment needs.

## Next experiment

**Hypothesis:** index 29 proves the shared architecture, but a visually
different common one-handed asset will need only per-index geometry/grip
correction rather than another damage implementation.

- **Baseline:** `run-20260809-113629` loaded all four Pipe-baseline records for
  index 29, consumed native `read_mask=0x7`, and produced three clean actor
  native forwards across two targets. The accepted Pipe reference remains
  `run-20260809-095447`.
- **Minimal change:** select a different form factor such as
  `meat_cleaver` (27), `mannequin_arm` (26), or a 2x4; inspect the inherited
  pose/collider before saving only that index's required corrections.
- **Headset procedure:** equip and seed it, verify the inherited settings are
  visible, align only if needed, then check slow rejection, one fast hit,
  continuous-follow-through latching, and a later rearmed swing. Relaunch once
  after any edit.
- **Diagnostics:** first require four `source=pipe_baseline` events, then
  `read_mask=0x7`, clean Retail reference release and native forwarding. After
  an edit/relaunch, require `source=weapon_record` for that record while index
  32 remains byte-for-byte unchanged.
- **Success:** the asset aligns and damages each target at most once per swing,
  repeats only after a valid reset, and keeps its correction isolated from the
  Pipe and other one-handed weapons.
- **Failure interpretation:** a fit problem belongs to that index's profile;
  a missing constructor/callback path means that identity must be removed or
  held fail-closed. A shared lifecycle invariant failure must also be regressed
  against the Pipe.
- **Rollback:** remove only the failing index from the explicit allowlist and
  retain the accepted Pipe/index-29 paths.
- **Headset required:** yes.

Do not retune the accepted Pipe collider or 7.25/0.12 hit baseline while
promoting another weapon. Changing any native capsule length or radius after
seeding requires another seed because Retail fixes those dimensions when it
creates the primitive.

## Verified facts and known unknowns

Verified facts include the supported PE identities; x86/x64 split; renderer
slots/camera ABI; coordinate conversion and accepted 100 units/m scale; M4
command IDs and binding paths; Retail melee enable/update/transform/callback/
dispatcher path; pipe index 32, fire-axe index 17, and recorded 2x4 indices;
Condemned arm chains/sockets; and the callback-relative reference-vector slot
locations. Exact evidence is in M0–M5, not repeated here.

Still unresolved:

- representative headset validation and asset-specific grip/collider tuning
  across the newly mapped one-handed allowlist; only index 29 has crossed the
  shared constructor/contact/native-forward boundary so far;
- headset acceptance and final tuning for Hit Speed/Rearm Travel plus future
  impact-energy and per-material scaling;
- automatic equip-time collision-body seeding with a verified safe lifecycle;
- a safe standalone weapon object's creation, ownership, collision constraints,
  resistance, throwing, blocking, and destruction lifecycle;
- permanent visible weapon/body/hand integration across every load, death,
  cutscene, execution, and weapon transition;
- complete weapon catalog profiles, firearm/forensic-tool behavior, impact
  haptics, HUD/full-screen effect classification, and comfort treatment;
- isolated save persistence, long-session/release regression, installer flow,
  and support for any game binary other than verified Steam 1.0.314.0.

## Safety invariants

Identity plus surrounding-byte checks precede every version-bound write; an
unknown build or layout disables the gate. Fresh finite tracking, foreground,
Retail gameplay state, live object ownership, and mapped weapon identity are
required before injection. Host loss and waits are bounded. Incomplete eye
pairs are discarded; camera/model state is restored exactly. Unknown weapons
cannot inherit mapped swing/damage behavior. Enemy and unrecognized Retail
melee bypass the local-player gate. Retail files stay read-only, and
proprietary/generated stages, logs, binaries, saves, and assets stay out of Git.
