# Current state

Snapshot basis: repository working tree and checked-in evidence reviewed on 20
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

**M5 — Condemned-specific gameplay**, now focused on representative firearm
and combat-behaviour evidence after the live-accepted index-76 direction,
one-handed physical-melee, and forensic-camera checkpoints.
M1 delegation, M2 transport, M3 stereo/tracking, and M4 controller gates have
live acceptance. M0 produced the usable no-ASI baseline, but isolated save-path
persistence and a longer release soak remain unresolved.

Detailed active evidence: [`CONDEMNED-M4.md`](CONDEMNED-M4.md) for forensic
controls and [`CONDEMNED-M5.md`](CONDEMNED-M5.md) for the melee checkpoint.

A parallel interaction-authoring platform is **implemented and
automated-tested, awaiting live validation**. The `-WeaponGripCalibration` /
VR Tools `AUTHOR` tab retains the exact-identity `MAG INSERT SOCKET` editor and
adds a primitive selector for `SLIDE GRAB RAIL`. The slide editor seeds only
the live-observed Colt node/rail facts, while requiring the author to capture
or adjust the physical grab volume and hand pose. Both primitives use
model-local capture, fine/coarse component edits, undo/reset, visible gizmos,
and the exact index-and-case-sensitive-catalog-name settings store. The slide
record has explicit save/unsaved state and backward-compatible schema loading;
the existing magazine auto-save behavior is unchanged.

Live run `run-20260820-091557` exposed and bounded an AUTHOR overlay triangle-
budget regression at 32,766/32,768 vertices. The compacted AUTHOR panel then
passed its live visual recheck in `run-20260820-092549`: the menu remained
visible while navigating and editing the slide primitive, no overlay-failure
event occurred, and an exact Colt slide record was captured and saved. The
worst-case complete-overlay tests retain both magazine and slide coverage.
Intervening runs `run-20260820-092112` and `run-20260820-092228` never reached
Playing state and remain readiness failures rather than interaction evidence.

The index-76 model discovery and name-resolution policy are now **live verified
across multiple model-source lifetimes**:
`SlideJnt`, parent `anim_cult45`, translated from
`(14.1689, 2.8062, -8.7261)` along
`(-0.989379, 0.007748, 0.145151)` for 3.8651 engine units with unchanged
rotation in `run-20260820-082601`. Run `run-20260820-092549` subsequently
resolved `SlideJnt` by name for source generations 1 and 2 after a model
lifetime change. Corrected run `run-20260820-124747` repeated resolution for
source generations 1 and 5 before attachment. Its observed handles and model
pointers remain lifetime-local evidence and are never persisted.

The authored Colt slide-grab/runtime path has a **live-accepted core
interaction with remaining safety/regression gates**. It is a fail-closed
Idle/Candidate/Attached/Released state machine gated by fresh tracking/input,
focus, Playing state, exact weapon identity, source generation, finite
authoring data, volume overlap, activation edge, and per-lifetime `SlideJnt`
name resolution. It projects off-hand displacement onto the observed rail,
clamps it to 3.8651 units, moves the authored hand target with the slide, and
suppresses only the matching VR off-hand Retail command while captured. The
Colt-only opt-in reuses the existing identity-validated `ILTModelClient` node-
control registration/removal boundary. In `run-20260820-092549`, Candidate and
Attached transitions, callback writes/readback, the 3.8651-unit rear clamp,
input-release removal, Retail-ownership restoration, and fresh generation-2
name resolution all occurred; the user confirmed that attachment and slide
motion worked. The same user rejected the attached hand pose. Inspection
showed the slide path bypassed the established LEFT IK wrist calibration even
though AUTHOR captured the raw OpenXR grip pose. Current source applies that
same controller-local correction before publishing the attached hand target
and makes menu teardown telemetry transition-only. Corrected run
`run-20260820-124747` produced 16 attachments and exactly 16 installed-callback
detachments, no overlay failure or source-only detach spam, 3.8651-unit clamps,
and fresh resolution on source generations 1 and 5. The user accepted the
saved pose and slide interaction as correct. No claim is yet made for absence
of unintended Retail action, explicit focus-loss cancellation, or normal post-
release fire/reload animation because those observations were not stated.

Two user-supplied CC0 WAVs are now wired into the
slide interaction. A project-owned Windows `PlaySoundW` adapter avoids any
unverified Retail sound-manager call. It emits the pull cue after rearward
travel reaches the authored rear threshold (3.50 of the Colt's 3.8651 units),
then rearms after the attached slide moves at least 0.25 units forward of that
threshold so another complete pull can cue without releasing grip. Endpoint
jitter inside that hysteresis band remains silent. The return cue is emitted
only for an ordinary input release while the slide remains displaced and
node-control removal succeeds; safety cancellation stops pending playback.
Structured
`m5_slide_grab_sound` evidence separates state, decision, Windows audio
handoff, and result. The local M2 stages copy and hash the WAVs under the
loader's `sounds` directory. Their Freesound source URLs, creators, CC0 1.0
terms, and repository hashes are recorded in `THIRD_PARTY_NOTICES.md`, so the
audio assets are eligible for source and release packaging. Automated tests
and a successful `PlaySoundW` return value are not live auditory proof.

Live run `run-20260820-133700` exercised the first sound build. Forty-four
attachments produced 44 pull handoffs, 35 displaced releases produced 35
return handoffs, and nine already-closed releases produced stop/no-return;
all assets were available, all Windows handoffs reported success, and all 44
callbacks detached with Retail ownership restored. The user rejected the pull
cue timing because that build fired at the initial 0.10-unit movement
threshold. Current source instead fires on `projection.rearReached` and is
implemented/automated-tested/staged, awaiting a corrected live timing check.

Corrected run `run-20260820-134612` supplies the process-side timing recheck:
all 12 pull cues occurred between 3.5050 and 3.8651 units, with none below the
authored 3.50-unit rear threshold. Eleven displaced releases produced return
cues and one already-closed release produced stop/no-return. All 24 Windows
handoffs succeeded with required assets available, and all 12 installed
callbacks detached with Retail ownership restored. The user explicitly
accepted the perceived pull/return timing and content as correct. This accepts
the single-pull live audio slice; the later repeat-pull rearm extension remains
implemented and automated-tested with the process-side live gate now complete.

Repeat-cycle run `run-20260820-135900` loaded staged x86 loader SHA-256
`A5B79C8741CAA3E4F09833B17B7D9D218F434F0B794E19536D9FA77E78E16036`.
Eleven successful attachments produced 24 pull cues, including two continuous
attachments that each advanced monotonically through `pull_cycle=1..6`
without an input release. Every pull handoff occurred at 3.5123-3.8651 units,
at or beyond the 3.5000-unit rear threshold. Nine displaced releases emitted
return cues after ownership restoration, two closed releases emitted stop/no-
return, all 11 installed callbacks detached with Retail ownership restored,
and no failed sound/control handoff or AUTHOR overlay failure was recorded.
This is live process-side evidence for rearming and bounded cue dispatch; the
user explicitly confirmed that repeated audibility and feel were perfect.
The repeat-pull audio extension is therefore **live accepted** for the tested
Colt path.

The current 20 August RelWithDebInfo gate passed 26/26 x86 and 22/22 x64
CTest cases plus all normal PowerShell validation suites. The dirty-tree x86
loader SHA-256 is
`A5B79C8741CAA3E4F09833B17B7D9D218F434F0B794E19536D9FA77E78E16036`;
this proves compilation, wiring, hand-target calibration math, slide-sound cue
policy including repeat rearming/hysteresis, and headset-free regressions. Run
`run-20260820-124747` separately
supplies the live acceptance for the corrected hand pose and transition-only
telemetry described above; it predates the sound adapter and supplies no audio
acceptance.

The current source state is also the **consolidated M5 developer feature
platform**. The normal build compiles the established loader, D3D9/OpenXR,
stereo, tracking, input, UI, firearm/forensic, melee, weight, IK, settings, and
diagnostic paths together with the newer player-collision and Phase-1
authoring slices. The required x86 game side and x64 host remain separate
artifacts in one coordinated build; unsafe diagnostics remain opt-in and the
headset-rejected automatic swing attack remains OFF. The 20 August
RelWithDebInfo gate passed 26/26 x86 and 22/22 x64 CTest cases plus the
launch-profile, foreground-handoff, screenshot-helper, schema-v4 weapon
watcher, and new release-tool PowerShell regressions.
The x86 loader SHA-256 is
`708EBD9DDFE21A3E1BDA7AEBAB8393C0977C44DA5B8F977A2389770C1B1343F1`.
The manifest correctly records base commit
`503fe3150012762403ce157d136ef047a0e687f8` plus a dirty working tree, so this
is an **automated-only local platform build**, identified by artifact hashes
rather than by the commit alone.

M6 end-user packaging is now **implemented and automated/integration-tested,
awaiting clean-machine and live acceptance**. `tools/make-release.ps1` creates
a project-authored release folder/ZIP with a hash manifest, guarded launcher,
clickable install/play/uninstall wrappers, defaults, title image, and notices.
The package contains no Retail files. On the end-user machine, `Install.cmd`
strictly verifies Steam `1.0.314.0`, constructs the established isolated
runtime/module/archive-config layout, and creates a self-contained install
whose normal `Play.cmd` uses the no-argument Current profile. Updates replace
only manifest-owned files and preserve userdata/settings; uninstall is a dry
run until `-Apply`, verifies the exact install marker and Game junction, and
preserves user data by default.

The 20 August project-local integration smoke used generated package
`condemned-vr-0.5.0-dev+da0e429` with 25 manifest-owned files. Automatic Steam
discovery, strict identities, first install, in-place update, custom-directory
`Play.cmd -VerifyOnly`, uninstall dry-run, and applied uninstall passed against
the verified Retail copy without changing its critical hashes. The first
applied-uninstall attempt exposed Windows PowerShell's junction-removal
null-reference bug before any deletion; the corrected path validates the
reparse target and removes the junction itself with `IO.Directory.Delete`.
The generated ZIP was then freshly extracted and repeated strict first
install, in-place update, installed `Play.cmd -VerifyOnly`, and applied
uninstall successfully. No headset/game launch, desktop shortcut, or clean
Windows account has yet accepted the end-user flow. The current dirty-source
package is diagnostic and must not be published.

The no-feature-parameter launcher is **live exercised through the guarded
readiness boundary**. Run `run-20260820-041148` invoked exactly
`tools\launch-condemned-m2-vr.ps1` with no feature arguments. Its report
(SHA-256
`40166256749D74DFAA7A8A24631A8A7533456883BA89F5C422D9FDAD599DC9CB`)
records `FeaturePreset=Current`, internal `WeaponTestPreset=Pipe`, Retail VR
Settings, physical melee/contact damage, weapon authoring, and full arm IK ON;
two-hand attachment and forensic memory tracing remain OFF. The staged bridge
loaded with no ASI module, final foreground restoration passed, and both game
and host were responding at the readiness sample. This verifies default-profile
resolution, staging, identity/hook readiness, and process handoff only. It is
not in-headset acceptance of the combined feature behavior; `-Minimal` and
explicit custom-profile fallbacks remain headset-free tested only.

That same session then reached active headset input and gameplay/menu paths,
but ended after approximately one minute in Windows Application Error
`Condemned.exe` / `ClientFx.fxd+0x26EEF`, exception `0xc0000005`, report ID
`2ea5e501-d7df-4db5-a083-687c6c4a929c`. The host detected the lost game
heartbeat and exited cleanly. This fails the combined profile's stability and
shutdown gate. It does not identify the default profile or Retail VR Settings
as the cause: the identical longstanding fault bucket predates those changes
and requires a controlled baseline comparison. Keep the combined profile
**implemented and readiness-live-exercised, awaiting live acceptance**.

## Current objective

The active player-collision objective is **partially live verified and still
experimental**. Run `run-20260819-115942` exercised the identity/signature-
gated `CMoveMgr` dimension handoff on the exact local player. At 100%, Retail's
requested and observed dimensions were `(40,95,40)`. Every successful reduction
reported exact requested/actual agreement with native result zero; Y stayed
95 units and the implementation did not write any enemy object.

The headset tester then reduced width to the initial 50% floor. The engine
accepted `(20,95,20)`, but the tester reported no material improvement in
stick-locomotion approach distance. Two later accepted melee contacts recorded
HMD-XZ-to-contact-point distances of 0.7137 m and 0.7939 m; these are contact-
point observations, not nearest-body-distance measurements. This rejects the
50% player-width hypothesis as a sufficient fix, not the native dimension
handoff itself.

Run `run-20260819-122852` live-exercised the 10% floor with staged x86 loader
SHA-256
`2710B2B87F14B8FD0DCCA0F0B31ACDC2D0CC4A43D47DE880C40B50FFBD8FF3C8`.
The settings change succeeded at `(4,95,4)`, but three later accepted actor
contacts on the same player HOBJECT read player dimensions `(40,95,40)` while
the configured scale remained 10%. Target reads were `(40,95,40)`,
`(39.022,91.3,39.022)`, and `(39,91,39)`, with `mutation=none`. The tester
reported that distance felt unchanged. This is **live rejection of the
one-shot persistence**, not a clean rejection of a continuously retained 10%
player width: the engine had restored full player dimensions before contact.
No native-handoff or retry event covered that restoration.

Read-only static inspection of the verified stock client found a supported
flag-`0x20` manager-update path at `GameOrig+0x000344E0` that bypasses the
existing `+0x00031BA0` hook and calls `ILTClientPhysics::SetObjectDims`
directly at `+0x000346BC`, `+0x0003476C`, and retry `+0x00034787`. It may use
the `+0x40C/+0x410/+0x414` triple for its primary/retry request. A separate
nearby triple begins at `+0x418`, but `+0x344E0` does not read it. The
triples' runtime meanings and whether this path caused the live restoration
remain hypotheses.

The 19 August read-only audit candidate samples the exact local player
immediately before and after Retail's client-shell update and performs a
labelled readback after pending processing. It does not mutate the engine; only
the existing mod-added one-shot setter adds a dimensions write. Its full
headset-free gate completed at
2026-08-19T13:30:55Z with 24/24 x86 and 20/20 x64 CTest cases plus the three
PowerShell validation suites passing. The x86 loader SHA-256 is
`D780B2DB6BC9C8A79824B4E24E0DF8E5C21E75A0F7234638B987CD954BEF8A3C`;
the manifest records base commit
`9732e0a867ffc3b80bfe909be6411a68367c457e` with a dirty working tree.

Run `run-20260819-133507` **live verified the audit and located the
restoration at a Retail-update boundary**. On player `0CC25FC0`, the pending
setter returned zero and a forced readback confirmed `(4,95,4)`. The next
changed sample, after a later Retail update and with no stick input, was
`(40,95,40)`, `pending=0`, and `drift=1`. A controlled 15%-to-10%
sequence again confirmed `(4,95,4)`; immediately after the menu closed, the
first `directions=0x5` stick edge was followed by a
`phase=post_retail_update` sample at `(40,95,40)`. At that sample,
manager `+0x1C=(1,1,1)`, `+0x40C=(24,31.5,24)`, and
`+0x418=(40,95,40)`; the last equality is correlation, not writer
attribution. Two accepted contacts later still read the player at full width
and read enemy dimensions with `mutation=none`. The run ended with a
successful saved 100% setting and actual `(40,95,40)`, followed by a clean
game/host exit.

This proves that the player enters a Retail update reduced and exits it full,
including on the first tested stick frame. It does not identify the exact
writer, prove that movement collision consumed either size inside the update,
or establish a proximity/performance improvement; the tester supplied no
subjective result for this run. Do not force an unconditional post-update
reapply or mutate enemy objects.

Static inspection of verified `Condemned.exe` SHA-256
`45A1404F213EDBDEAD16168B6E005B245B93105F7345AAF4FB83ECB6A7C5AE02`
identifies the `ILTClientPhysics` vtable at executable RVA `+0x0014ADE0`,
slot 8 `GetObjectDims` at `+0x00064530`, and slot 9 `SetObjectDims` at
`+0x00007FD0`. The slot-9 body has the verified x86 three-argument/
`ret 0x0C` ABI and may modify its dimensions buffer on native failure.

The 20 August working tree adds an executable-identity/vtable/body/callsite-
gated MinHook observer at slot 9. Every native call is forwarded exactly once
with the same object, in/out request pointer, and raw slot-9 flags, and its
exact result is returned. Bounded telemetry is emitted only while Playing for
the fresh exact local-player HOBJECT when width is reduced or a reapply is
pending. It records request input/output separately, actual dimensions
before/after, read-only manager candidates, and verified GameOrig return/
callsite classification. Unrecognised image callers retain module/return-RVA
observations, while unresolved callers retain only the raw return address,
under independent budgets. The observer adds no
`SetObjectDims` call or engine-state write of its own.
Both collider detours stay native-pass-through until enable succeeds; an
uncertain owned-hook rollback suppresses retry and leaves the retained
trampoline operational only for native forwarding.

The exact setter observer is **automated-only**: the normal headset-free gate
passed before the Collision X-ray work began. No event yet identifies the
restoring callsite, proves the `+0x344E0` hypothesis, establishes which
dimensions movement consumed, or accepts its runtime cost.

The working tree also adds a session-only, read-only `COLLISION X-RAY`. Its
separately identity/vtable/body-gated slot-11 `SetVelocity` observer forwards
the exact local-player call once and labels it a velocity handoff, not a
collision result. Pre/post Retail-update samples correlate the player object's
actual dimensions and rigid-transform origin with a fresh HMD origin and a
fresh actor-contact target. Magenta player and orange target boxes are always
labelled **diagnostic proxies** because `GetObjectDims` has not been proven to
describe the true physics geometry; cyan HMD-to-player distance is an origin
offset measurement, never a player radius. The X-ray does not persist, mutate
enemies, call `SetObjectDims`, or write any engine object. It is
**automated-only**: the full gate passes 25/25 x86 and 21/21 x64 tests plus
the launcher-focus, screenshot-helper, and schema-v4 watcher suites. Live
validation remains pending.

Room-scale RS1 is **live exercised, rejected for performance, and rolled back
from the runtime**. Run `run-20260813-131921` armed the read-only candidate;
the tester reported that it ran horribly. Host telemetry recorded extended
sections around 17--23 game FPS with heavy reuse and roughly 13--16-frame
average image age, although later windows recovered to about 42--43 game FPS.
There is no matched same-scene no-probe control, so causation is not isolated,
but the candidate failed its usability/timing gate.

The attempted diagnostic never supplied a room-scale movement command or
engine transform write. Its dedicated helper/probe files, binding and renderer
sampling, loader/launcher switch `-RoomscaleProbe`, and dedicated automated
test have been removed. The plan and historical 24/24 x86, 20/20 x64 candidate
test result remain in
[`CONDEMNED-ROOMSCALE-PLAN.md`](CONDEMNED-ROOMSCALE-PLAN.md). The required
live correlation sequence was incomplete, and the later copied loader file is
not the RS1 event stream, so `GameOrig+0x00168EEC` / manager `+0x10` remains
only a static candidate. RS2 and every write-enabled room-scale gate remain
blocked pending a redesigned, lower-cost read-only probe and matched
performance baseline. The post-rollback full gate passes 23/23 x86 and 19/19
x64 tests; the refreshed project-local stage uses x86 loader SHA-256
`6E624F4ADFC690167B663CD79A9234BC563F2EBB134586A8ECE2D1E581210058`.
No post-rollback game/headset run has been started.

Automatic equip-time melee collision seeding is **implemented and
headset-free tested, awaiting live validation**. For a positively identified
mapped one-handed pickup, the runtime waits 250 ms for stable weapon/model
identity and a fresh tracked pose, overlays one 100 ms Retail Fire-command
pulse, and accepts readiness only after the existing native collision hook
observes a player-owned Attack record with exact overridden `read_mask=0x7`
and a non-null body. Native impact forwarding and the action haptic are blocked
during this seed-only transaction; the contact latch is not mutated. A
confirmed body settles for one second before becoming ready. Confirmation is
bounded to two seconds, retries wait 750 ms, and only three attempts are
allowed per equip. Weapon/model changes reset the transaction. One ordinary
Retail attack remains a manual fallback after terminal automatic failure.

This is deliberately separate from the headset-rejected motion-triggered
`SWING ATTACK` adapter, which remains OFF for accepted physical melee. It does
not call `EnableCollisions` directly or invent its five-argument ownership
contract. The final headset-free gate passes 23/23 x86 and 19/19 x64 tests
plus launcher-focus, screenshot-helper, and schema-v4 watcher regressions. The
refreshed project-local stage uses x86 loader SHA-256
`08F10AE3C302D6C7D616DE2B10C01D3F44389B9F362FCCD2EC717AF55BE346E4`.
No game/headset run has yet established pickup readiness, absence of seed
damage, or whether Retail exposes an unacceptable one-time attack animation.

Startup-focus reliability has a new **implemented and automated-tested,
awaiting live validation** candidate. A 13 August tester report showed that a
fresh launch can still leave another desktop window in the foreground. The
launcher had issued only one immediate `SetForegroundWindow` request at each
handoff, latched any early success into `GameWindowFocusRestored`, and wrote
the report before its actual last focus attempt. It now retries normal requests
for one bounded second, performs one cleanup-checked attached-input fallback
only at the final handoff, and verifies the exact game root-owner window plus
PID. That handoff runs after optional diagnostics windows appear; only then is
the report written. `GameWindowFocus` records the initial, ready, and final
attempts, while the legacy boolean reflects only the final result. The
headset-free launcher-focus regression passes; a canonical live launch plus
keyboard/mouse, VR-input, and Alt-Tab checks remain required.

The Retail-menu extension seam now has a narrow **live-verified row and
dispatch boundary**. Canonical run `run-20260813-045556` used staged x86
loader SHA-256
`4F5369A915160522E73C31E080F2AF80B7A6C690DF5A9AA4258AC49D080BA274`.
Retail created the injected control once at native index 7, and six selections
reached the reserved command `0x3A`; the preserved loader log is SHA-256
`6F5E45F2330843C3E7B8BB73A9D4A94004F1517B02C87E28DEF2CE74A068D55C`.
The tester saw the row in Options and reported two deliberate shortcomings:
the literal was all uppercase and selection opened no destination. The latter
was the diagnostic-only behavior of that build. This proves construction,
visibility, and dispatch, but not a finished page or every focus/style
sub-gate.

The first title-case child candidate is **live exercised and rejected as a
safe settings host**. It routed command `0x3A` to registered screen 24, an
otherwise-unlinked native `CScreenGame`, and retained that dormant screen's
original Game Options controls and lifecycle. `run-20260813-051426` crashed
before menu construction at `GameOrig.dll+0x539AE` and supplies no page
evidence. Retry `run-20260813-051528` created the title-case row/page, recorded
four entries and three Back edges during extensive menu input, then crashed at
`ClientFx.fxd+0x26EEF`; its preserved loader log SHA-256 is
`AB67C0C3045B30DC2B3368741AD8CA8965490D70B9EE17651471B0E35DA6D73C`.

Explicitly approved capture run `run-20260813-054534` then entered the page
once and captured the exact foreground Condemned client before any dormant
control was activated. The image live verifies a normal-case `VR Settings`
title and Retail's parchment, pinned-note, font, focus-row, and column design.
It also exposes the unsuitable dormant content: Crosshair, Subtitles, Blood,
Always Run, Difficulty, Message Duration, Head Bob, Auto-switch Weapons, HUD
Fade Speed, and visible invalid-string placeholders. While the page was simply
left open, with no Back event recorded, the process later reproduced
`ClientFx.fxd+0x26EEF` in the same WER bucket. A later Windows event-history
audit found 59 identical faults from 7 August through 13 August 14:18, before
the Retail-menu work began. The fault therefore does not establish menu or
screen-24 causality. The original page remains rejected because its controls
are unrelated/incomplete, contain invalid strings, and invoke unaccepted
dormant settings behavior. Preserved SHA-256
values are loader log
`8579639CECEF66DB40A5A07CE50D801881C1323747D74D11A798180BF58CB6C7`,
PNG `E50ABCB564976B524852C5A0A3EC79BE1F2D8CF26854CC7ED221AAF808913C1D`,
and JSON sidecar
`8EC6E5F94A5E4C34CA891C585B55D2EFBF1769F7BE40E3A47DE8EA8F7FC1CC78`.

The replacement is **partially live verified; its Developer Tools controller
shortcut suppression now passes live, while keyboard suppression, re-enable,
and restart persistence remain pending**. It still uses
the registered Retail object/base shell, but exact
vtable comparison proves that screen 24 overrides only destructor,
`OnCommand`, `Build`, and `OnFocus`; the destructor already delegates to the
base owner, and hooks now completely bypass the other three. The replacement
sets the title through Retail's wide-title path, creates Display, VR Features,
Comfort, and Developer Tools rows from the captured Retail descriptor, and
calls only the verified base Build/finalize/focus/command routines. Display,
VR Features, and Comfort remain non-mutating placeholders. Developer Tools is
now a single `On`/`Off` row controlling whether both grips plus the
controller-specific left secondary button (reported as B by this tester; Y on
Touch) and F12 may open the existing VR Tools overlay. Exact image,
inherited-vtable, factory,
text-setter, per-control vtable, signature, and call-target guards fail closed.
The full gate passes 23/23 x86 and 19/19 x64.

Explicitly approved run `run-20260813-063732` used staged loader SHA-256
`B1B860D25C06DAD0725AA87E530D89F09B1FFB2E8CACD178742A979516DE598D`.
It built and focused the isolated host exactly once with original Build,
OnFocus, and OnCommand all bypassed. Exact-foreground screenshot SHA-256
`2223D0C12BCBBE629ED2BC245C267D7D81E462FFD58542E1D6B9BEDAB5E92C8D`
shows the title-case heading and exactly Display, VR Features, Comfort, and
Developer Tools with no invalid strings. Back completed base focus-out and
returned from the page. However, the opening VR Enter edge also selected
Display before the injected key-edge call returned; the placeholder explicitly
made no settings mutation. This is a failed entry-input sub-gate.

The new candidate brackets internally generated menu key edges and suppresses
only a category command produced by the same VR Enter edge that opens the
page. Later VR selections and physical keyboard/mouse input remain untouched.
The full automated gate still passes 23/23 x86 and 19/19 x64; refreshed staged
loader SHA-256 is
`A9AD752A4A6D25ACCDFB44AFED9D8539A3FB837280C18BCE733EFEB8446B749A`.

Explicitly approved validation run `run-20260813-071147` used that staged
loader. Two VR-generated accept edges opened the isolated page without any
`m6_retail_vr_settings_category_selected` event or setting mutation. The page
built once, retained its four clean rows across re-entry, and recorded two
active/inactive base-focus cycles; the first return also has an explicit VR
Back edge. No `m6_retail_vr_settings_entry_category_suppressed` event was
needed, so the observed behavior passes while the defensive suppression branch
itself remains automated-only. The exact-foreground 1930x1090 screenshot is
pixel-identical to the prior accepted page (SHA-256
`2223D0C12BCBBE629ED2BC245C267D7D81E462FFD58542E1D6B9BEDAB5E92C8D`);
its new JSON sidecar SHA-256 is
`B5AE68BD3F7CA9B003467E4423BA6BF11AAC88AFA8BBAD55E151A7EC5B20F46F`.
Preserved loader-log SHA-256 is
`4031429705448FBF40396F875F33BD0C21F12B4BDF679DB6A1A448A120B20A09`.
Shutdown again produced the longstanding `ClientFx.fxd+0x26EEF` WER bucket
(report `cc156935-3775-472a-97af-d2e8c686fa9e`), which remains a separate
baseline rather than menu-causality evidence.

`tools/capture-condemned-window.ps1` verifies the exact PID/root-owner/
foreground client rectangle, writes a PNG plus JSON identity/hash sidecar,
never changes focus, and passes `-ValidateOnly`. Per tester instruction, no
game/headset launch is automatic. Hub entry, native rendering, re-entry, and
one explicit Back now have live evidence.

The first functional row is **partially live verified**. A new
versioned `[developer] tool_menu_shortcut` preference lives in the existing
global user INI; packaged and missing-setting behavior preserves the established
enabled shortcut, while a malformed record fails closed to disabled. Runtime
writes save before changing the atomic preference. Disabling it does not close
an already-open overlay or bypass release capture, so the close path remains
safe. The refreshed project-local stage carries x86 loader SHA-256
`E44FBB97548C24836E4E26A1AF02D0E62E4DE3938F068EED51CB09CDDC6AA8F6`.

Explicitly approved run `run-20260813-132327` loaded the preference with
`result=ok enabled=1 capability_available=1`, built the native fourth row with
`settings_ready=1`, and refreshed its label on focus. One deliberate activation
then recorded `previous=1 requested=0 saved=1 current=0 label_refreshed=1`; the
tester confirmed that the toggle worked, then clarified that with the row Off
VR Tools no longer appeared when pressing both grips + B, their physical label
for the left-secondary controller chord. This live verifies initial display,
command dispatch, save success, runtime mutation, immediate native-label
refresh for `On -> Off`, and controller-chord suppression. It does not yet
verify F12 suppression, an On-state restore/open/close/release-capture pass, or
that Off reloads in a fresh process.
The preserved loader-log SHA-256 is
`1CE76E5407C1F1EAC6F0F0E721E7BA8B1F583E89790302E5EF8BF3EB6FE64C30`;
run-metadata, host-log, and bridge-log SHA-256 values are respectively
`56C0E88D55B4EAB8A55EB1DCECA262394EF21DFE7574E325228714D993190525`,
`3839C43794BCF43044F3B262E64DC9A88D1D62DA40823E13A2B296435D870F61`,
and `77A7F8F19F58CFB0CAAEA42ACE1111DF011C1533BC0CD4081367547D2B506265`.
The launcher-monitor wrapper timed out after startup, so this run has live
metadata rather than a finalized launch report. Shutdown reproduced the known
`ClientFx.fxd+0x26EEF` baseline (WER report
`8b491332-38a1-4566-8c4e-c9d2cb7d83ba`) and is not attributed to the toggle.
The next step is the remaining keyboard/restore/release-capture behavior and
fresh-process persistence gate, after fresh explicit launch confirmation.

The preceding accepted candidate addresses the reported empty-right-hand twist.
A lifetime-validated held model keeps the weighted weapon/aim target; no live
model or Retail `Unarmed` selects a fresh raw right-grip target and bypasses
weapon weight plus per-weapon correction. The empty-hand view of **Hand IK**
now exposes only `START/CANCEL GUIDED EMPTY-HAND ALIGNMENT` and
`RESET EMPTY-HAND ALIGNMENT`. Two release-gated right-trigger captures solve
and immediately apply a global controller-local correction, then save it as
`[arm_ik] empty_right_hand`; weapon-specific Hand IK records remain separate.
Focus/menu loss, equipping a weapon, stale tracking, malformed transforms, and
over-range solves fail closed. Dedicated diagnostics cover source selection,
both capture transforms, solve result, and persistence.

Live run `run-20260811-115639` used the canonical `-WeaponTest Pipe` launch
and staged x86 loader SHA-256
`62D2BFC48E735177466570C6E71ACE623270AEDF077E82F9DB6CBEA2E2A0B4D5`.
The two right-trigger pulls recorded `reference_captured` followed by
`completed` with `persistence=ok`. The accepted controller-local correction
was position `(-4.455, 3.542, -2.338)` units and quaternion
`(0.476682, -0.571406, -0.385630, 0.545490)`, and the same value was present
in the writable player INI. The headset tester reported that the alignment
"works perfectly." This **live verifies the in-session empty-hand target,
guided capture, solve, immediate application, and write**. A fresh-process
reload, deliberate tracking-loss capture, and equipped/empty transition remain
separate live sub-gates; their guards and persistence parsing are automated.

The 11 August guided held-object alignment is **live verified in-session for five held assets**.
Its original behavior is retained here as historical evidence and is superseded
by the fresh-process contradiction and hand-parented candidate below. That
candidate's final **Grip** row starts/cancels a two-trigger capture. Capture one
freezes both displayed transforms: model `O` and right hand `H`. Capture two
records the desired raw controller/weapon basis `D`; the candidate solves
`G = inverse(O) * D` for the absolute model-local grip and
`C = inverse(D) * H` for the per-index hand correction. Applying both keeps
the object and hand together rather than repairing one by misaligning the other.
Transient weight and two-hand solving are bypassed only while capture is active.
A pose can become authoritative only after the render override set/readback
succeeds for that frame. Focus/menu loss, source index/generation change, stale
tracking, malformed transforms, and bounded solve failures preserve prior values.
Success immediately updates and separately saves the existing per-index `grip`
and `right_hand_ik` records; secondary-grip data and all manual sliders remain.
The action is available to every lifetime-valid held model, but alignment is
still calibrated and accepted per Retail item index rather than shared across
unrelated assets. It deliberately leaves the independent per-index `collider`
record untouched: that swept capsule is expressed in weighted-controller space
and must be checked or tuned separately after visually realigning a melee item.
The automated gate passes 19/19 x86 and 15/15 x64. Canonical live run
`run-20260811-130418` used staged loader SHA-256
`0CF5043043D3D4AF00321F5A718C661D072B91B4588378C2E779ED5050848D79`.
Scanner index 46, `cell_phone` index 4, Camera index 3,
`colt45_Unbreakable` index 76, and `colt45_Melee_Unbreakable` index 77 each
completed start, reference capture, controller capture, immediate application,
and separate `grip_persistence=ok` / `hand_persistence=ok` writes. The tester
reported that alignment is now "sooooo much easier." This live accepts the
guided in-session mechanism and usability for those five item identities.
At the user's instruction, those exact five Grip/right-hand pairs are now
source-controlled in `config/condemnedvr-defaults.ini`; collider values remain
unchanged. Source, generated x86, and canonical-stage copies are byte-identical
at SHA-256 `6ADA21EF6DED26FA929FE53B95431568F9C172F7382FA751C0F0C4086CE3F39A`.
Fresh-process reload, forced tracking/source-change cancellation, each item's
camera/fire regression, and all remaining assets remain separate live gates.

A later index-76 fresh-process check contradicted the assumption that the
in-session two-capture result was a complete held-assembly calibration. The
tester reported that the right hand was again misoriented and that the weapon
did not align with the controller. The preserved restart trace loaded the
per-index Grip and Hand-IK records successfully, activated the visual proxy,
and repeatedly measured the expected 60-degree grip/aim basis difference; this
is therefore not evidence of a missing save. The failed capture had preserved
an already-wrong first hand pose, and the model continued following the
controller after capture one instead of visibly freezing. The contradictory
evidence is preserved under
`stage/condemned-m2-mono/logs/run-20260811-141552/`; detailed transforms and
hashes are in `CONDEMNED-M5.md`.

The current hand-parented correction is **implemented and automated-tested,
awaiting live validation**. With model-local Grip `G`, controller-local hand
correction `C`, and collider frame `T`, it preserves the model-relative
attachments `A = G * C` and `B = G * T`. A Hand-IK edit computes a matching
new `G`, so rotating or translating the hand moves the weapon with it, and
rebases `T` so the collider remains attached to the same model location.
The firearm reconstruction already consumes `G`, so its visible barrel frame
follows the same weapon pose. Guided capture now freezes only the model after
the first trigger. During step two the globally calibrated raw-grip hand
follows the controller into that frozen model; the second trigger solves the
fresh `G` and `C`, rebases the collider, and saves all three per-index
records. The full headset-free gate passes 20/20 x86 and 16/16 x64. The
built/staged x86 loader is SHA-256
`4863C9E0389A2049AA6CA9DD6376B96588A4596E42F218ED6FF1700635F11DEB`;
the staged defaults were intentionally left unchanged. Per the tester's
instruction, no game/headset run has been started for this candidate, so
alignment, damage-volume, camera, firearm, and restart behavior remain live
gates.
A subsequent index-76 session establishes a narrower **live-verified reset-fit
boundary**. In `run-20260811-144530`, after Grip and Hand IK were reset, the
guided reference trace recorded object `O`, hand `H`, and controller driver `D`
at the identical position `(2156.377,-2309.698,2312.275)` and quaternion
`(0.054115,0.758500,0.047660,-0.647671)`. The tester independently reported
that the reset gun already sat perfectly in the hand. This verifies the
authored reset gun-to-hand relationship for index 76 and shows that a second
freehand placement is unnecessary for that asset; it does not live-validate
the replacement button. The preserved loader trace is 244,316 bytes, SHA-256
`78810AA7F35FAED1FBE5EFF05DE6F7199450FB8832F6E64158200CEDE0DB99E5`;
the accompanying settings snapshot is SHA-256
`66709385464EC4AE5A4C613027B1B0FD28F6C5120D70B47D8000BF27162CBACC`.

The simplified **Grip** action `ALIGN HAND + WEAPON TO CONTROLLER` is now
**implemented and automated-tested, awaiting live validation**. One press
samples raw grip position/rotation and raw aim rotation from the same fresh
OpenXR input frame, deliberately excluding weapon-weight lag. It aligns the
hand to the globally corrected grip target, preserves the current
`A = G * C` gun-in-hand relationship, rebases the collider to preserve
`B = G * T`, and separately saves Grip, Hand IK, and Collider. The previous
frozen two-pose workflow remains as an advanced fallback for a genuinely bad
authored model-to-hand fit. The full gate passes 20/20 x86 and 16/16 x64;
the tested/staged x86 loader is SHA-256
`2D44888071EA3D360E9A7FB822CBC5EDD5BB6C8DBB1E4DD9AE77FCE1D4237A9E`.
The source and staged defaults remain byte-identical at SHA-256
`6ADA21EF6DED26FA929FE53B95431568F9C172F7382FA751C0F0C4086CE3F39A`.
No game/headset run has been made with this new binary.

Live run `run-20260811-152219` now **accepts the one-press interaction
in-session for five held identities** on VirtualDesktopXR 1.0.10 / Quest 3.
With staged loader SHA-256
`2D44888071EA3D360E9A7FB822CBC5EDD5BB6C8DBB1E4DD9AE77FCE1D4237A9E`,
the tester exercised index 76 eight times, Scanner 46 three times,
`cell_phone` 4 five times, Item Camera 3 twice, and Pipe 32 once. All 19
`m5_align_held_assembly_to_controller` events were `applied`, used a fresh
same-sample raw grip/aim pair, measured the expected 60.000-degree basis
difference, and reported Grip/Hand/Collider persistence `ok/ok/ok`. The tester
accepted the result as good enough to use until a concrete issue appears.
This closes the immediate application, repeat-use, five-identity usability,
and write sub-gates. Restart reload, forced stale/source-change rejection,
post-alignment collider/damage checks, camera/fire regressions, and all other
identities remain open. The preserved loader checkpoint is 1,090,914 bytes,
SHA-256
`90D6F718892EAC82C4E76E9971C989AB4A7A668805568AB983DD9D8FCDD101A9`;
the final player-settings snapshot is SHA-256
`07B4F52D6ED87265F83CB23285B6DF4289920063279E8E157243A7E2A3097CDC`.

A 20 August headset tester report now supplies the concrete issue that the
earlier broad usability judgment did not test: after the one-press action, the
Colt remained attached to the hand but did not face the controller direction.
No fresh structured run accompanies the report, so the prior same-sample,
write, and repeat-use evidence remains valid; weapon-to-controller direction
correctness does not. Inspection confirmed that the old solver could preserve
a stale model-local grip `G` while successfully aligning the hand and
preserving `G*C`. The tester then clarified that zero Grip and Hand IK values
place the Colt and hand perfectly relative to each other while both point in
the wrong direction, and explicitly requested automatic rather than frozen
alignment. This establishes the authored reset attachment as the known-good
relationship and moves the fault to the whole assembly's controller-local
basis.

The working tree contains an **implemented and automated-tested, awaiting live
validation** automatic correction. It targets the already calibrated global
raw-grip hand pose, carries each weapon's immutable zero/reset hand-to-model
attachment, and rebases the collider from the prior model grip. Stale current
Grip/Hand IK values are intentionally superseded. For index 76 the authored
attachment is identity, so hand and Colt resolve together onto the corrected
hand direction rather than the 60-degree-displaced aim driver. The action
requires no freeze or trigger capture and rejects before persistence unless
both the hand target and authored attachment recompose within 0.001 unit and
0.01 degree. The full gate passes 25/25 x86 and 21/21 x64 CTest cases plus all
five normal PowerShell regression suites. Built and project-local staged x86
loader copies are byte-identical at SHA-256
`74B2169CA45D0A8FA013AAA1ACA857473F04009FCF18FCD5F31981B63CF6AAF6`;
the manifest records base commit
`503fe3150012762403ce157d136ef047a0e687f8` plus a dirty working tree. No
headset run has yet accepted Colt/hand forward direction, reset-fit
preservation, collider placement, fire direction, or restart persistence for
this correction.

The current combat-investigation diagnostic is **implemented, automated-tested,
and live exercised**. Its instrumentation is usable; the automatic command-17
telegraph candidate is headset-rejected because of its delayed Retail attack
animation. The diagnostic layer does not
alter damage, collision, AI, spacing, or block behavior. Compact contact records
add a runtime tick, fresh HMD/grip distance to Retail's contact point, and the
per-swing native attack-telegraph state. An optional read-only player-vitals
sample is armed only when the supported Retail `Health` command, setter,
singleton, and field signatures all match. Enemy health remains deliberately
unobserved because the native melee dispatcher sends an engine damage message
rather than returning target health; actor accepted/native-forward counts are
reported without relabeling them as health loss. The schema-v4 watcher also
correlates existing command-28 block edges with observed player-health
decreases. The full gate passes 21/21 x86 and 17/17 x64, plus the synthetic
schema-v4 watcher regression; tested and staged `GameClient.dll` SHA-256 is
`96C17087069654ABA75A321BDCEC4ECA08EBD0BED622A6750DF70B75BE483505`.
Launch attempt `run-20260812-090345` never started the game and supplies no live
combat evidence.

Successful run `run-20260812-100216` armed the verified read-only vitals path
and recorded six accepted/native-forwarded actor hits plus one world/prop hit,
seven rearms, 44 duplicate blocks, zero same-target accepts before rearm, and
513/513 Retail reference-vector clears. Actor accepts for repeated targets were
separated by at least 1,266 ms, rejecting duplicate callbacks within one swing
as the fast-kill cause. The tester observed some enemies die in one hit and
others in two; a head-hit multiplier is plausible but unproven because enemy
health and semantic node names are not observed. Five actor accepts used node
handle `0x00000006` and one used `0x00000026`.

All seven accepts had automatic swing attack disabled because the loaded Pipe
record had `swing_attack=0`, but the raw log separately contains five manual
controller-applied command-17 down/up pairs. The tester reports that enemies
reacted only to the Retail attack trigger and not to physical-only swings. This
live-correlates AI anticipation with the native attack-command path while
leaving the downstream signal and animation-free integration unresolved. The
closest actor surface contact was 0.4729 m horizontally from the HMD and already
overlapped the configured capsule, so the measurement does not support blaming
the weapon collider for the remaining stand-off.

Five command-28 down/up pairs and 12 health decreases were observed; all
decreases were outside an ordered active pair. The tester initially reported
that block had not worked, but later clarified that a trigger tap was enough to
produce many visible blocks and did not need to remain held. The live-visible
blocks are verified; the proposed explanation that command 28 primes Retail's
native state and the physical weapon collider then supplies the spatial
interception remains a hypothesis. Command-active intervals therefore do not
measure native block-state lifetime. Watcher UTC values are ingestion times and
do not measure the original activation. The post-run diagnostic candidate adds monotonic
loader ticks, automatic/manual command-17 separation, and authoritative block-
command activation duration. The full gate passes 21/21 x86 and 17/17 x64 plus the focused
watcher regression. Built and canonical-stage `GameClient.dll` are
byte-identical at SHA-256
`9E4E1B4CC0FC5C569AC8A3A3019A70292CEC4DFF713D9FC6F3BAE9F683741B57`;
that exact diagnostic build is live exercised below.

Live ON run `run-20260812-124543` used that exact staged loader and loaded the
Pipe player override with `swing_attack=1`. Thirty-five motion threshold
crossings produced 35 matched automatic command-17 pairs; one additional pair
is consistent with a manual trigger. Twelve contacts across four actors were
accepted/native-forwarded, 121 held-overlap callbacks were blocked, 12 rearms
completed, and no target was accepted twice before rearm. All 515 Retail
reference vectors were released cleanly. Five accepts occurred during the
100 ms command pulse and seven after it, confirming that the physical proxy's
native forwards remain independently contact-gated. This does not exclude
Retail attack-window side effects or establish visible AI response.

The run exposed one automatic cooldown violation: two pulses began 62 ms apart
despite the configured 450 ms cooldown, with the first pulse ending after
47 ms. Reset-on-transient-invalid paths can erase the state, but the responsible
reset is not yet observable. Five command-28 activations lasted only 109--203 ms;
all nine health decreases were outside their active intervals. The tester
first described these as unsuccessful attempts, then clarified after further
play that a short activation visibly enabled repeated spatial blocks without
holding the trigger. The snapshot observes command edges, not Retail's native
block state, so those health correlations cannot classify the later blocks as
active or inactive. Raw trigger/eligibility loss is also not distinguishable
from a short physical press. The final snapshot, event stream, and preserved
loader checkpoint have SHA-256
`518F024CC40F3F62F85237988896D72D43B765518CE8BB99CC0BB361B0FD99A9`,
`70D0F348AC9FE1833C605A724734714E1DC2BFD5FD1E81035BD01A797858214E`,
and `37DD60127A4A802056DB62CBFE986E5E1B1D5F36CC44477697452AE0476E061E`.

The tester confirms that automatic command 17 made enemies react, but also
started Retail's unwanted attack animation. Its wind-up completed after the
physical controller hit had already landed, creating unacceptable delayed
timing and a possible second damage opportunity. Additional Retail-animation
damage is not observed, so it remains a risk rather than a confirmed cause of
the earlier fast kills. The A/B therefore rejects automatic command 17 as the
shipping telegraph. The player Pipe override has been restored to
`swing_attack=0`, SHA-256
`2095B603D697AD25F9652CE04CB9D8DC44547D5B9E5AF910DBD0AD5A9B979B78`;
the rejected ON file is preserved separately and packaged defaults remain OFF.

Automatic guard-pose entry is **live verified**, but the first exit path failed
the effective-defense gate. In `run-20260812-140353`, entering the saved pose
produced the automatic command-28 down edge plus Retail block sound and
haptics. Leaving produced the matching pose exit and command-28 up edge, yet
the tester remained in Retail's native defensive state. The preserved loader
checkpoint has SHA-256
`CBF33EF30D040A45BE77497CB25D9473D7ABB0BABE7B4858D0E346FD2203E317`.
Identity-verified disassembly now establishes why: command 28 reaches
`PlayerManager::CommandOff` at GameOrig RVA `0x000A1B30`, whose selector-3
case returns without cancelling block. The up edge is therefore a command-level
release, not proof that native defense ended.

The guarded second-`CS_Block` exit hypothesis is now **live rejected**. In
`run-20260812-150308`, three automatic exits queued the same-weapon check, but
`HandlingAnimationStimulus("CS_Block")` and the active stimulus were already
false every time, so the fail-closed guard made no engine handoff. The tester
still blocked outside the pose. The preserved loader log has SHA-256
`149B66FB47B117E9F4755C3A7173186D516F5A73E1946408CD70B6A6955C1292`.
This proves the short animation stimulus is not the authoritative defensive
lifetime; it does not prove an unconditional second stimulus would cancel.

The same run exposed the stronger root cause. Every automatic entry called the
verified Retail `EnableCollisions` path with argument five set to one, while a
later ordinary attack seed used zero. Identity-verified disassembly establishes
that argument as `bBlocking`: block records skip the attack notifier. The
generic physical-melee update had nevertheless replaced every player-owned
record's expiration with the continuous contact-damage lifetime, including the
block record. A new candidate is **implemented and automated only**: exact
record-selection and `bBlocking` byte layouts are verified, only positively
classified attack records may receive the continuous lifetime, and block or
unknown records remain Retail-window-owned. The failed second-stimulus handoff
is disabled. The full gate passes 22/22 x86 and 18/18 x64 plus schema-v4; built
and staged loader SHA-256 is
`C44ABD8267A861E2F55714E31233880A50E25939AC48DB0B3703828F09ADAFDF`.

A follow-on dedicated-block-collider candidate is **implemented and automated-
tested, awaiting live validation**. A missing per-weapon `block_collider`
record follows that weapon's current attack collider, including later attack-
collider edits, until the first explicit **Block Col** edit creates an
independent record. Positively classified `bBlocking=true` records now use that
geometry both when Retail reads native capsule dimensions and while its update
path builds the capsule transform; attack records retain the existing attack
geometry. Explicit block geometry is rebased with held-object alignment, while
an inherited block capsule continues following the rebased attack collider.

The **Block** tab also has a per-weapon `CUSTOM BLOCK WINDOW` toggle and bounded
100--2000 ms duration in 25 ms steps. The toggle defaults OFF, so existing
weapons preserve Retail's authored duration; diagnostics report Retail and
applied values separately. **Debug** now has independent attack-collider,
block-collider, and controller toggles. Block preview/live capsules are
blue/cyan, use a role-specific live flag, and all three drawings default OFF.
The global debug record is version 2 with version-1 read compatibility. The
normal headset-free gate passes 22/22 x86 and 18/18 x64 plus launcher-focus and
schema-v4 watcher tests. The unstaged x86 loader built from the current dirty
worktree has SHA-256
`B7C4DE41F9D80D13CF6C7C1D7909A99EFC4EE9B450786C85FBF4759AFF8D055F`.
That initial candidate was not live-run. The first staged launch,
`run-20260813-034938`, then exposed a bounded menu-rendering regression before
block behavior could be validated: navigation remained `open=1`, but entering
`BLOCK` emitted `m5_vr_tool_menu_overlay_failed bridge_draw_rejected=1`.
Representative complete pages reproduce the exact split: Block uses 29,820
triangle vertices and Debug 27,402, both above the bridge's former 24,576
limit, while the visible Block Col page uses 21,876. The accepted fix gives the
menu producer and D3D9 bridge one shared 32,768-vertex cap, logs the rejected
count/limit, and covers Block, Block Col, and Debug page budgets headset-free.
The full gate passes 22/22 x86 and 18/18 x64 plus launcher-focus and schema-v4.
Live run `run-20260813-040058` recorded 18 Block, 15 Block Col, and 16 Debug
navigations plus ten Debug adjustments with the menu still open and zero
`m5_vr_tool_menu_overlay_failed` events; the tester confirmed all three tabs
remain visible and interactive. This is **live verified tool-menu rendering**.
Exact staged SHA-256 values are
`0942BFD65C726FD0A27BB46AF0C2342A7220C3A6196D961114886C79F04D4705`
for `GameClient.dll` and
`D188B8DFB75BB134E6654A94CF6DBD33CC38C982C8E75806DFBACAF7A232BCD7`
for `condemnedvr-d3d9.dll`. Collision interception, timing, colors, and
native block expiration remain unverified live.



The Retail `Camera`-socket target-query context switch is live accepted for
both mapped forensic displays. Scanner index 46 succeeded in
`run-20260810-143142`; Item Camera index 3 succeeded in
`run-20260810-145113`. Both keep Retail command 116 for tool selection,
command 17 for use, and Retail ownership of range, filtering, classification,
cache, and collection dispatch.

Visible-handgun direction was live accepted for stable index 76
(`colt45_Unbreakable`) using the then-packaged grip position `(0,0,-6)` and
local rotation `(-8,100,16)` degrees. The later guided-alignment run produced
position `(3.630,1.723,-7.606)` and rotation
`(165.034,70.934,-177.826)` degrees; the user instructed that this new pair be
promoted into the packaged defaults. The prior sight-direction result therefore
requires a fresh fire regression under the new release pose. The first
direction-only candidate
required authored `Breach -> Flash` geometry; `run-20260810-155025` found
`Flash` handle 2 but no `Breach`, so all nine calls safely retained
raw-controller aim and provided no candidate alignment evidence.

Verified Retail static evidence shows that `Breach` is optional. The follow-up
index-76 candidate reconstructs the same visible model pose used by the stereo
weapon override, prefers `Breach -> Flash` when both transforms exist, and
otherwise uses the authored `Flash` socket's local +Z axis. Retail's original
fire position remains unchanged; transformed `Flash` position is diagnostic
only. Current weapon/model references, saved grip generation, fresh tracking,
finite transforms, and exact model-interface slots fail closed to raw-controller
aim, then Retail on stale tracking. Other weapon indices retain the existing
controller basis.

Live run `run-20260811-081337` used VirtualDesktopXR 1.0.10 on a Quest 3 and
the canonical `-WeaponTest Pipe -Wait` launch. All four index-76 shots logged
`result=applied`, `direction_source=Flash_socket_plus_Z`, a valid stable
Flash transform, unavailable optional Breach, and `fallback=none`. The tester
confirmed that impacts followed the visible handgun sights. This is **live
verified direction alignment for index 76**.

The claim does not move or accept the muzzle origin: Retail `firePosition`
remains native, and the observed diagnostic Flash-to-Retail-origin separation
was 64.570--78.185 units. Close-range parallax and every other firearm model
remain outside the acceptance. The full build passes 19/19 x86 and 15/15 x64
tests; the accepted staged x86 SHA-256 is
`5C385D018E511623E563357F4FCE82BCA689C38D1DB96C7C72405D1698F257F2`.

The current release-readiness candidate adds a source-controlled
`config/condemnedvr-defaults.ini` and stages it beside `GameClient.dll`.
Settings reads now use a per-key precedence of the writable player file first,
then that packaged file; menu writes continue to modify only
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`. The package contains the
retained first-level weapon, collider, grip, hand-IK, and arm-IK calibrations,
including the live-authored Item Camera index-3 records and the newest retained
pose/hand records for indices 4, 17, and 46. These additions are retained
calibrations, not blanket perceptual acceptance for every record. All 28
current player-setting keys now have packaged counterparts; only the intended
legacy-v1 to packaged-v2 record normalization differs. `[debug] draw=1,0,0`
keeps both developer wireframes hidden for a fresh player. The Debug-tab rows
now save their global visibility preference immediately, including OFF. The
full automated gate, including parsing every packaged record, passes 19/19 x86
and 15/15 x64; source, build output, and canonical stage all share defaults
SHA-256
`04AFF10BC30AFF6B069A613FB0E29FD579F9AA9C21E63A6D8E749186110DC579`,
and built/staged candidate `GameClient.dll` share
`C22FAB9AA6DC4C5B4802C3E62B6EBAE81B4888EE0FDB828981A5F8DEBD49A610`.
Runs `run-20260811-102206` and `run-20260811-102500` now live-accept
the Debug player override/restart boundary. The later process loaded `0/0`,
recorded eight successful saves ending at `0/0` with zero save failures, and
left `[debug] draw=1,0,0` in the writable player INI; the tester confirmed the
choices appeared to save. Three actor contacts after the final OFF save were
accepted/native-forwarded, so hidden drawing still did not disable damage. The
2,509,819-byte loader checkpoint SHA-256 is
`ECC05757BD7B2DD7EE3A2864FA5E38983551FAB38CC5145DF990B9778C8BB993`.
An isolated empty-player-path run is still needed to live-identify packaged
per-weapon fallback; that path currently remains automated-only.

The preceding pushed checkpoint promoted the live-accepted Pipe setup into a
temporary fail-closed baseline for the verified one-handed majority. The
repository maps the 26 modeled `WEAP_1HandedDebris` indices plus
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

The earlier visibility candidate added two independent controls at the top of
the VR Tools `DEBUG` tab: `DRAW MELEE COLLIDER` and `DRAW CONTROLLERS`. That
live-tested build defaulted them on and reset them on process restart. The
current release-readiness candidate instead defaults both hidden and persists
their global user preference. The gates remain draw-only: collision, contact
damage, controller input, grip/IK calibration, and weapon state continue
underneath a hidden overlay. Automated transition and settings-store tests
prove that the toggles do not affect one another and that OFF survives a
save/load round trip. The earlier live-tested staged loader SHA-256 was
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
`C950716B4690A3411C6E0FF18B3CFCCC7FA75D34E307CC65ACB701DD05B6DE94`.
Run `run-20260810-080738` **live-accepts ready-tool selection**: the headset
tester confirmed that right-stick up pulls out the forensic tool. The armed
gate recorded 22 matched command-116 press/release pairs at `game_state=1`,
each with `control=right_stick_up`, and zero core-action rejections; it also
recorded 13 ordinary Y pause dispatches. Ready selection is therefore a live
PASS. Actual forensic use/scanning, contextual actions, tool pose/aim, prompts,
stow/reselection, crime-scene variants, and transition behavior remain the
active gameplay work.

A bounded, observation-only forensic memory trace is implemented behind
`-ForensicMemoryProbe`. It signature-checks the Retail weapon-display and
command paths, then records bounded Tool, Fire, and Activate observations
without game-memory writes or video capture. Run `run-20260810-103612`
live-armed the first version and recorded 44 command edges, 44 edge snapshots,
163 samples, 15 root transitions, and four complete traces, all with
`engine_writes=0`.

That run raised, but did not prove, a Fire-magnitude hypothesis. Sixteen VR
right-trigger Fire presses reached command 17 with output `1` without taking
the photo. After switching to flatscreen and realigning, one mouse Fire edge at
Retail value `128` took the photo and Scanner index 46 changed to `cell_phone`
index 4. The preserved loader trace is
`stage/condemned-m2-mono/logs/run-20260810-103612/condemnedvr-loader-snapshot.log`
(SHA-256
`F9EADA3039F70B826E2C5CEC9912063E465D146B68AF2D0B8D1E77F8CE85C24D`).

The same trace rejected the initial object-layout assumption: the
`weapon+0x90` product remained `0x029DDEB0` across different equipped objects,
did not match the known Scanner or DigitalCamera vtables, and yielded no
aligned word diff. Static factory/constructor facts remain valid, but that live
pointer must not be treated as the final subclass object.

Run `run-20260810-105904` falsified the magnitude hypothesis. VR Fire at the
experimental value `128` did not take the photo, and two physical mouse Fire
edges at `128` also failed in the same unchanged Scanner state. Later, the same
VR trigger fired `colt45_Unbreakable` nine times; observed weapon fields
changed 9 to 0 and 0 to 9 while the Retail fire-vector path ran. Command 17's
native binding declares range `(0.100,99999.000)`, so the original value `1`
was already active. The magnitude experiment has been removed. The preserved
loader trace is
`stage/condemned-m2-mono/logs/run-20260810-105904/condemnedvr-loader-snapshot.log`
(SHA-256
`67BB633813E5562FD01972706EB3976DB1487B9BF2C2587C883AAB4CA49439E8`).

The observer hooks the verified Scanner and DigitalCamera updates to capture
their actual live `this` pointers, plus Retail command on/off callbacks. Static
analysis now classifies Scanner `+0x1DB` as target hit, `+0x1DC` as framing
valid, and `+0x1DD` as the final `can_photo` conjunction consumed by its
vtable eligibility function at `GameOrig+0x000F46F0`.

Loader `F242036E6D975389B7394B014FE37628CABD72C874C1736BB67800FDFD6A9E77`
live-armed both observer layers in `run-20260810-112423`. Scanner index 46
resolved the actual object `0x0CC8A098` and expected vtable RVA `0x0014AB44`;
DigitalCamera was not instantiated in this scene. Two VR Fire edges reached
Retail while only target hit was observed as one and did not take a photo; one
physical mouse Fire edge later reached Retail with target hit zero and also
failed.

A bounded read-only sample of Scanner `+0x1D8..+0x1DF` then recorded five
`target_hit=1, framing_ok=1, can_photo=1` windows during a 20-second controller
sweep. The tester confirmed that controller movement drives forensic aim, head
aim does not, and the final state matches Retail's beep plus green camera
light.

The same run contains a successful flatscreen reference: physical Fire `128`
at trace 20 changed the Scanner's first four state bytes from `0x01000001` to
`0x01010101`, then `0x01010100`, before index 46 advanced to phone index 4.
The final preserved log SHA-256 is
`7350A928463DCFED3937447790ACAA55796301A6DF51F63C01D556BDB10900B9`.

The expanded six-byte loader
`4413F08686DDBC9AD24FF5790DC60D1311BA8B01EC94DA0044B603C1DB56FA14`
live-armed in `run-20260810-115015`. One VR Fire edge had
`controller_active=1`, output `1`, and target hit, framing, and final
`can_photo` all equal to one at the exact Retail command callback. It produced
only the expected controller haptic: no flash, no photo, and no Scanner-46
transition through 256 sampled frames. This falsifies eligibility or missed
aim as the explanation for that failure. Its final preserved loader log is
`stage/condemned-m2-mono/logs/run-20260810-115015/condemnedvr-loader-snapshot.log`
(SHA-256
`C32359EA48CB5B0C7F59CDA18586A25DFD5926AF5C918CAC33A781B8BF1B97A2`).

Bounded reverse engineering maps PlayerMgr command-on to
`GameOrig+0x000A0C30`, `CClientWeapon::Fire` to `+0x00024D90`, and their
verified command-17 callsite to `+0x000A13C7`. The live Scanner weapon uses
vtable RVA `0x0013B46C`, type `0x15`, and has its weapon `+0x303` Fire-ready
byte set. The same branch also has a Scanner/collection-tool special callsite
at `+0x000A1351`, targeting `GameOrig+0x000E8F00`; that path returns before
generic `Fire` and therefore makes ?no weapon Fire? ambiguous by itself.

The route-marker loader
`26360CAC206D32972EE8012B3AC49044430FEBDBD0F138F062269684CEDA8EBE`
live-armed in `run-20260810-121200`. It recorded 32 PlayerMgr Fire dispatches,
including 24 Scanner-46 dispatches with target hit, framing, and `can_photo`
all held at one. None entered generic `CClientWeapon::Fire`. The one physical
mouse-`128` edge in this run occurred with all three Scanner gates zero, so it
is not a valid flat/VR route comparison. The completed loader trace is
`stage/condemned-m2-mono/logs/run-20260810-121200/condemnedvr-loader-snapshot.log`
(SHA-256
`925715B6773C396CB58518F80BF5414E8DA904BC0CFF57B97AA946DEFB35F52F`).

The branch/collection observer build passes 19/19 x86 and 15/15 x64
tests; SHA-256
`4B1B05040114790C0E4DD389048CAD46EC001082295B16C6AF1EA492C717841F`.

Live `run-20260810-123242` recorded five Scanner-46 PlayerMgr Fire callbacks.
Every branch input had `player_mode=1`, weapon type `0x15`, state
`+0x218=1`, Fire-ready one, owner null, and the same all-three-green Scanner
state. All five returned without either
`m5_forensic_collection_action_dispatch` or
`m5_forensic_weapon_fire_dispatch`. A read-only live check then resolved
PlayerMgr's target-query object at `0x02CCF388`, vtable RVA `0x00149A70`,
and cached result at object `+0xB4`: its classification byte `+0x1C` was
zero and target reference `+0x2C` was null. This is the decisive mismatch:
the Scanner display sees the right-controller target, while the activation
branch sees no target from its separate desktop-camera query.

The completed baseline loader trace is
`stage/condemned-m2-mono/logs/run-20260810-123242/condemnedvr-loader-snapshot.log`
(SHA-256 `5F3E15664DAEC3D14EA0C32FFA7FF690B1DBA8F559AD1683EFDFB56C5DEB2B77`).


The cached-result getter is `GameOrig+0x000EA500`; updater
`+0x000EA010` calls acquisition routine `+0x000E98D0`. That routine obtains
the desktop camera transform through the engine-client interface, builds a
Retail segment, and calls `IntersectSegment` from return RVAs
`+0x000E9BCE` and `+0x000E9BEE`. The live engine method resolves to
`Condemned.exe+0x000095C0`.

The superseded first implementation temporarily replaced only the query
start/end with fresh OpenXR right-controller origin/+Z, called Retail, then
restored the caller-stack prefix. It preserved range, flags, filter, result
classification, and action ownership. Its automated tests passed, but live
`run-20260810-134726` falsifies its optical axis. The completed snapshot has
209 bounded query records, 167 engine intersections, and zero restoration
failures. All 89 Scanner-ready PlayerMgr branch records retained cached
`kind=0`; none reached kind three, collection action, or generic weapon
`Fire`. The saved Scanner grip is position `(-0.15,-0.90,0.65)` with local
rotation `(20.0,85.5,25.5)` degrees, while the raw ray ignored that visible
camera/controller-forward disagreement.

The preserved completed trace is
`stage/condemned-m2-mono/logs/run-20260810-134726/condemnedvr-loader-snapshot.log`
(1,412,180 bytes; SHA-256
`7881DF6FCA814885DA78288CEF1751744061B261FCD5D4259D9BC66BBAE42ACC`).

The forensic camera is presented as separate white alignment-arrow graphics
and a camera body/screen; the embedded view follows the arrows. Scanner and
DigitalCamera share vtable slot `+0x24` at `GameOrig+0x000F4CB0`. Scanner
calls it at `+0x000FC0C0`, and DigitalCamera at `+0x000FC6CD`. The shared
function resolves the model-socket name stored at display `+0x1AC` and asks
Retail's model interface for its world transform. A bounded live read of
Scanner display `0x0CC1A098` confirmed the literal socket name `Camera`.

The current replacement hooks the shared socket function only after its
invariant code, both display vtable entries, and both virtual-call sites
match. Each successful finite `LT_OK` output publishes a coherent per-display
pose. The target-query hook additionally requires forensic type `0x15` and
maps only stable catalog index 46 to Scanner or index 3 to DigitalCamera.
Index 6 (`CollectionToolBase`) and unknown indices are compile-time asserted
to receive no override. A matching pose must be no older than 250 ms.

The candidate segment uses socket-local +Z while retaining Retail's original
range and every non-geometry field, then restores the query immediately after
Retail returns. `m5_forensic_camera_socket_pose`,
`m5_forensic_camera_socket_ray_query`, and
`m5_forensic_camera_socket_ray_fallback` expose display kind, expected kind,
source sequence, age, geometry, result, and fallback reason without persistent
engine writes. The index-3 candidate passes 19/19 x86 and 15/15 x64 tests;
built x86 SHA-256 is
`FD2311E189650AC3DD79FB7A887D0DBDCA40487064180CD385E69FF36655F638`.

Live `run-20260810-143142` headset-accepts the Scanner mapping. Immediately
before the successful VR Fire edge, PlayerMgr's cache was `kind=3` with
non-null target `0x38EEF848`; Retail's collection action ran with `handled=1`,
and the tester confirmed the visible photo. The frozen checkpoint is
`stage/condemned-m2-mono/logs/run-20260810-143142/condemnedvr-loader-photo-success-checkpoint.log`
(431,677 bytes; SHA-256
`3F29DCB159E7F9504E2F1E1E375C62603EA90CDE5B8474924168E294DB62AD25`).

The Scanner-only run established the index-3 baseline: `Camera` /
`WEAP_Camera` stayed forensic type `0x15` and published fresh DigitalCamera
poses, but stale-Scanner fallback left PlayerMgr at kind zero.

Candidate run `run-20260810-145113` is now headset accepted. Index 3 used
`pose_display_kind=digital_camera`; the successful Fire edge saw all four
DigitalCamera state bytes at one, PlayerMgr cached kind three with non-null
target `0x38B4F9A0`, and Retail's collection action returned `handled=1`. The
tester confirmed the visible Item Camera result. Preserve the checkpoint at
`stage/condemned-m2-mono/logs/run-20260810-145113/condemnedvr-loader-item-camera-success-checkpoint.log`
(1,393,735 bytes; SHA-256
`1DC321F8DDA0C61BC7FEE84D14BE47C530636CC4A782E7A1CD68F0610A870047`).

The forensic Camera-socket route is accepted for both mapped displays. The
first firearm candidate found `Flash` but no `Breach` on index 76 and
safely applied no candidate direction. The revised Flash-socket +Z direction
crossed four native fire calls without fallback in `run-20260811-081337`;
the tester confirmed impacts followed the visible handgun sights. Index-76
direction is live accepted while Retail origin/parallax and every other firearm
remain unchanged and unclaimed.

Live attempts `run-20260810-101312` and `run-20260810-101822` both stopped
before game launch: VDXR loaded, but `xrGetSystem(HMD)` remained unavailable
for the bounded 15-second retry and ended with
`XR_ERROR_FORM_FACTOR_UNAVAILABLE`. No loader/probe armed event exists for
either run, so both are explicitly **no forensic behavior evidence**; retry
after the headset session is awake.

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

Weapon model alignment is persistent rather than process-local. A versioned
per-weapon `grip` record stores the absolute local grip position, local rotation
correction, and optional secondary-grip enable/offset/radius. Reads prefer
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini`, then the packaged module-
sibling defaults; writes target only the player file. Stored profile identity,
mapped one-hand Pipe fallback, and authored defaults remain the fail-closed
lower layers. GRIP and 2-HAND menu changes and resets auto-save, while
continuous fallback calibration saves on controller Y or keyboard P. Authored
base values remain separate so Reset still has a stable target.

The x86 settings-store tests cover packaged fallback, player precedence, Debug
ON/OFF persistence, grip round-trip, profile mismatch, out-of-range save
rejection, malformed-record rejection, and every declared record in the actual
copied post-build defaults file. The full
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
| Frame pacing | **PASS (pacing) / PARTIAL (headroom)** | Request-clock pacing is live accepted near 90 Hz; later feature-heavy runs kept XR submission near 90 Hz but imported 65-87.5 pairs/s with two-to-five-frame average age, so representative 90 Hz headroom remains open; see [performance evidence](CONDEMNED-PERFORMANCE.md) |
| Native stereo | **PASS** | M3 live at 100% render scale and 130% coupled FOV; distinct fuseable eyes, state restoration, no repeated simulation |
| HMD rotation | **PARTIAL** | M3 live accepts responsive relative yaw/pitch/roll and yaw-only recenter; an 11 August report says the horizon can intermittently inherit a downward pitch. Source inspection supports a Retail camera-base composition hypothesis, but no correlated orientation trace exists yet |
| HMD translation | **PASS** | M3 live bounded relative translation; current path limits unsafe travel and falls back on stale tracking |
| Controller transport/input | **PASS** | M4 base controls and right-stick-up forensic ready selection pass live; the complete forensic lifecycle remains M5 work |
| Locomotion | **PASS** | M4 live left-stick movement and right-stick turning with keyboard/mouse coexistence |
| Weapon/fire aiming | **PARTIAL** | HMD flashlight, mapped Scanner/Item Camera aim/action, and the earlier index-76 visible-barrel direction pass live; a later Colt controller-facing report contradicts direction correctness after one-press alignment, and the automatic forward-hand/reset-attachment correction plus Flash/fire regression still need live validation, while origin/parallax, recoil, and complete coverage remain |
| Visible weapon pose | **PARTIAL** | Render-only override/restoration remains exact; the prior one-press run retains same-sample/write/repeat evidence for indices 76, 46, 4, 3, and 32, but a later Colt-facing report rejects direction correctness and the automated-only forward-hand/reset-attachment correction still needs headset, collider/camera/fire, and restart validation |
| Simulated weapon weight | **EXPERIMENTAL** | Bounded player-local damped-spring solver is automated-tested and drives visible/collision pose; it is not collision-constrained physics |
| Melee collision | **PARTIAL** | Pipe reference path and index-29 inherited native geometry/continuous speed-qualified overlap pass live; the other mapped one-handed assets remain unverified |
| Actor contact qualification | **EXPERIMENTAL** | Pipe ownership, 1 cm overlap, 7.25 m/s Hit Speed, and 0.12 m travel-plus-swing-end rearm pass live with zero same-target reaccepts; the transient-invalid hold is regression-tested but was not naturally invoked in the confirmation run |
| Native damage handoff | **PARTIAL** | Pipe damage is live-accepted; index 29 produced three clean actor native-forwards across two targets, with explicit headset-visible damage confirmation pending; remaining assets are unproven |
| Arm/hand IK | **PARTIAL** | Initial chains, callback order, wrist placement, locomotion anchoring, empty-hand correction, and one-press hand/weapon propagation have live evidence; Colt controller-facing correctness is contradicted, while the automatic forward-hand/reset-attachment correction, restart/load-state generation, and explicit visual collider preservation remain open |
| Haptics | **PARTIAL** | M4 bounded input-confirmation pulses passed live; melee-impact impulse haptics and verified weapon-event feedback are not implemented |
| HUD/UI | **PARTIAL** | Retail menu/screen comfort panel and controller navigation passed live; the title-case VR Settings hub, four-row styling, two clean entries/focus-outs, and one explicit Back pass live. The prior opening-Enter leak did not recur, so its defensive suppression branch remains automated-only. Display, VR Features, and Comfort are placeholders; Developer Tools `On -> Off`, save, runtime mutation, immediate native-label refresh, and controller-hotkey suppression pass live, while F12 suppression, On restoration/release capture, and fresh-process persistence remain pending; VR Tools remains developer UI |
| Installer/release | **EXPERIMENTAL** | Retail-free folder/ZIP builder, strict install/update, integrity-only Play, dry-run-first uninstall, ownership manifest, and preserved userdata pass automated plus project-local Retail integration smoke; shortcut, clean-account, extracted-ZIP, runtime/headset, and release acceptance remain |

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
        Retail melee collision-body transform                [TEMPORARY; automatic seed live gate pending]
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

- **Retail VR Settings isolated host:** adds one native Options row and reuses
  the registered screen-24 base shell while bypassing its unsuitable
  dormant Build/OnFocus/OnCommand path. Four Retail-native category rows are
  diagnostic-only. Hub rendering, two clean entries/focus-outs, and one
  explicit Back have live evidence; the same-entry-edge suppression remains
  automated-only because its branch was not exercised. The next scaffold is
  one guarded category destination with one mod-owned persisted boolean. The
  pre-existing ClientFx shutdown fault remains a separate gate.
- **Swing-to-Retail-attack adapter:** a tracking-space 3.00 m/s gesture can
  emit a bounded command-17 pulse for mapped profiles, but its delayed Retail
  animation was headset-rejected and it remains OFF for accepted physical
  melee. The fire-axe bridge remains experimental. Equip-time seeding below is
  a distinct one-shot compatibility transaction, not motion-triggered damage.
- **Render-only Retail weapon override:** proves acquisition, grip calibration,
  alignment, weight, and stereo rendering while restoring the engine object.
  Replace it with a standalone lifetime-safe held item at the constrained pose.
- **Retail collision-body/wall proxy:** redirects the animation-derived physics
  transform. The native body still has to be created once, so this remains a
  bridge to a permanent physical weapon/collision solution.
- **Automatic equip-time seed candidate:** implemented and automated-tested,
  but not live accepted. Stable mapped pickups receive one guarded seed-only
  command-17 pulse. Damage dispatch, contact-latch mutation, and the action
  haptic are blocked until exact player Attack classification and overridden
  `read_mask=0x7` confirm the body, followed by a bounded settle. Three timed
  attempts are allowed; a manual Retail attack is exposed only as the terminal
  fallback. A silent direct `EnableCollisions` call remains off-limits until
  its five arguments and ownership contract are verified; do not guess that
  ABI. A truly seedless system still requires the standalone item below.
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
- **Calibration/debug helpers:** collider/controller wireframes, 2-Hand snapshot
  controls, discovery probes, and the hand-only IK proof remain bounded aids for
  establishing profiles/layouts; the live-accepted guided Grip interaction is
  no longer classified as temporary diagnostic scaffolding.
- **M4 confirmation haptics:** prove transport only; final recoil/impact haptics
  must come from verified game/contact events.
- **M2-named stage/launcher:** the internal name remains for compatibility;
  the M6 package deliberately generates that proven layout behind end-user
  `Install.cmd` and `Play.cmd` rather than exposing milestone setup steps.

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

- `tools\build-all.ps1` builds x86/x64 and currently runs 25/21 CTest tests
  covering the protocol/ABI, state/math, input/UI, weight/melee/IK,
  interaction authoring, player-collision diagnostics, logging, identity,
  fail-closed loader/bridge, settings, and background-render guards. The x86
  settings test covers packaged fallback, per-player precedence, Debug ON/OFF
  persistence, and malformed-player-override rejection.
- `tools\test-condemned-launch-profile.ps1` proves no-argument/Wait-only
  `Current`, explicit Pipe/custom, `-Minimal`, rollback-only, and invalid mixed
  profile resolution using both hashtables and PowerShell's real bound-parameter
  dictionary.
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
performance tools. Gaps: the loader envelope has only `event` and key/value
`detail` and is not inherently per-session; current contact, vitals, and command-
edge records carry a monotonic runtime tick, but no shared stage/correlation ID.
Contact records still lack stable semantic actor/material/node identity; stages
are not joined in one record; and impact haptics do not exist. Improve only as
an experiment needs.

## Next experiment

For the active player-collision objective, the normal headset-free gate is
complete. Do not deploy or launch without an explicit tester request. The next
bounded live run must require `m5_player_collider_writer_trace_armed` and
`m5_player_collision_xray_armed`, establish `(4,95,4)`, then correlate
`m5_player_collider_setdims_observed` sequence/thread, caller module and
return/call RVA, request input/output, raw slot-9 flags, native result, context
stability, and actual before/after with
`m5_player_collision_xray_velocity_handoff` and the pre/post update X-ray
record.
The raw slot-9 flags are not proof of the outer manager flag `0x20`.

A known call changing `(4,95,4)` to `(40,95,40)` would identify that native
handoff as the restoration. It still would not prove whether movement
collision ran before or after it. Boundary restoration without a qualifying,
uncapped observer event means only that the change was not observed through
`Condemned.exe+0x00007FD0`; it does not prove no writer exists.

Reject the diagnostic if its local-player identity changes, calls are not
forwarded exactly once, a proxy is presented as verified geometry, telemetry
cannot separate HMD/player origin offset from object separation, or measurable
runtime cost appears. Rollback is the session toggle OFF; no saved setting or
engine state needs restoration. Keep unconditional post-update reapply and
enemy mutation absent.

Keep automatic `SWING ATTACK` OFF. Live-validate the attack-only collision-
lifetime and dedicated-block-collider candidate without using the left trigger.
Start with block timing override OFF and the block collider still reporting its
attack-collider fallback. Pose-to-command entry and Retail feedback are already
live verified; the required gate is that the classified block record remains
finite, uses the independently visible block capsule, and defense no longer
persists outside the pose, while a later classified attack record still uses
the attack capsule and supports physical hits.

The automatic-command A/B is complete and rejected. It proved enemy reaction,
but Retail's wind-up attacked only after the physical hit. The player and
packaged settings are back to OFF. Do not re-enable command 17 automatically to
solve anticipation.

- **Established baseline:** the corrected latch accepted six actor hits with a
  completed rearm between every accept and blocked 44 duplicates. Fast visible
  kills are not explained by repeated callbacks in a single swing. The nearest
  slow actor contact was 0.4729 m HMD-XZ to the Retail surface and overlapped
  the configured capsule. Physical-only swings did not cause visible AI
  anticipation; five manual Retail attack-command trials did. Enemies visibly
  died in one or two physical hits. A later tester clarification establishes
  that a short command-28 activation visibly enabled many blocks without a
  continuous trigger hold. Whether the weapon collider alone decided those
  blocks remains a hypothesis.
- **Rejected A/B:** with all Pipe damage/collider/weight values fixed, 35
  automatic threshold crossings reached command 17 and 12 physical contacts
  reached the native dispatcher. Enemies visibly reacted, but the unwanted
  Retail wind-up attacked after the physical hit. This fails timing and risks a
  second attack; it does not prove a second damage application.
- **Minimal diagnostic:** trace the verified command-17 path by pipeline stage
  from player attack-state transition to AI observation and animation/window
  activation. Instrument a bounded candidate only after identity and surrounding
  bytes establish it; do not guess an AI object, offset, callback, or vtable
  slot. The first pass must be read-only.
- **Cooldown follow-up:** defer the observed 62 ms/450 ms violation while the
  feature remains OFF. Preserve the evidence if the swing state is reused.
- **Damage follow-up:** before tuning strength, add a bounded read-only semantic
  name for Retail's collision node and a tester marker that joins a deliberate
  head/body strike to visible defeat. Raw handles `0x06`/`0x26` and native
  dispatch count do not prove a head multiplier.
- **Block follow-up:** equip Pipe, close the menu, and avoid left trigger. First
  confirm `BLOCK COL` reports attack fallback and the blue block preview exactly
  overlaps the attack capsule; then make one visible block-collider adjustment
  and confirm only the block capsule moves. Enter the saved pose and require a
  classified `blocking_argument=1` / `role=block` record whose updates report
  `continuous_lifetime=0`; the block capsule must turn cyan while the attack
  capsule remains role-independent. Leave, wait beyond the displayed finite
  Retail window, and take an enemy strike outside the pose. Repeat entry/exit,
  then perform a normal physical hit and require a separate
  `blocking_argument=0` / `role=attack` record with `continuous_lifetime=1`.
  Only after that baseline, enable a single conservative custom-window value
  and verify `retail_ms` versus `applied_ms`. Record visible defense, health,
  command edges, pose errors, collision role, both capsule positions, and
  timing independently.
- **Block success:** defense does not remain latched outside the pose, re-entry
  works, every block record stays Retail-window-owned, and attack-only lifetime
  classification preserves normal physical hits.
- **Telegraph success:** a qualified fast swing can expose anticipation to AI without a
  Retail player animation, delayed attack window, or pre-contact action haptic;
  damage still requires accepted physical contact.
- **Failure interpretation:** if AI anticipation cannot be isolated from the
  Retail attack state, keep automatic swing attack OFF rather than accepting a
  delayed second attack. If the automatic pose produces correct command edges
  but no visible defense, move block investigation into Retail state/eligibility
  or the spatial interception hypothesis. If a finite classified block record
  disappears but defense still remains, instrument the distinct Retail
  `BLOCKWINDOW`/server state; do not add repeated stimuli or write the weapon
  state directly.
- **Regression:** collision, native dispatch, AI, player movement, firearm,
  forensic, keyboard/mouse, and host-absent fallbacks remain unchanged.
- **Rollback:** already complete for the rejected ON override. Restore the
  preceding staged DLL only if new diagnostic plumbing disturbs runtime behavior.
- **Headset required:** yes.

The isolated packaged-fallback smoke and representative ordinary-firearm
inventory remain queued after this combat gate.

## Verified facts and known unknowns

Verified facts include the supported PE identities; x86/x64 split; renderer
slots/camera ABI; coordinate conversion and accepted 100 units/m scale; M4
command IDs and binding paths; Retail melee enable/update/transform/callback/
dispatcher path; pipe index 32, fire-axe index 17, and recorded 2x4 indices;
Condemned arm chains/sockets; and the callback-relative reference-vector slot
locations. Exact evidence is in M0–M5, not repeated here.

Still unresolved:

- representative 90 Hz performance headroom under the current gameplay, IK,
  and diagnostic workload: the 1 August request-pacing acceptance remains
  valid, while 11-12 August feature-heavy runs show lower imported-pair rate
  and higher image age. A controlled one-change-at-a-time A/B must separate
  diagnostic I/O/drawing, game render time, Classic-D3D9 transfer, host import,
  and host GPU work; see [`CONDEMNED-PERFORMANCE.md`](CONDEMNED-PERFORMANCE.md);
- an intermittent reported HMD horizon pitch offset: the current renderer
  composes the complete untouched Retail camera rotation with relative HMD
  rotation and suppresses native mouse pitch while the head snapshot is fresh.
  Residual Retail base pitch is the leading source-supported hypothesis, but a
  bounded pre/post-F6 trace must separate Retail base, HMD-relative, and final
  eye orientation before any fix;
- representative headset validation and asset-specific grip/collider tuning
  across the newly mapped one-handed allowlist; only index 29 has crossed the
  shared constructor/contact/native-forward boundary so far;
- headset acceptance and final tuning for Hit Speed/Rearm Travel plus future
  impact-energy and per-material scaling;
- live acceptance of the automatic equip-time collision-body seed candidate,
  including pickup animation, nearby-target damage suppression, haptic
  suppression, retry/reset behavior, and immediate post-ready physical hits;
- a truly seedless collision lifecycle independent of Retail attack creation;
- a safe standalone weapon object's creation, ownership, collision constraints,
  resistance, throwing, blocking, and destruction lifecycle;
- permanent visible weapon/body/hand integration across every load, death,
  cutscene, execution, and weapon transition; the arm-IK game-state generation,
  transactional reinstall, and per-side heartbeat candidate is implemented and
  automated-tested but still awaits the bounded live lifecycle gate;
  the empty-right-hand raw-grip target and guided global correction are
  implemented and automated-tested, but still require headset alignment,
  transition, tracking-loss, weapon-isolation, and restart acceptance;
- forensic ready selection plus mapped Scanner and Item Camera target/action
  paths are live accepted; remaining contextual transitions, prompts, stow, and
  broader scene-lifecycle behavior remain;
- complete weapon catalog profiles, representative firearm behavior, impact
  haptics, HUD/full-screen effect classification, and comfort treatment;
- isolated save persistence, long-session/release regression, clean-account
  installer/shortcut/runtime acceptance, and support for any game binary
  other than verified Steam 1.0.314.0.

## Safety invariants

Identity plus surrounding-byte checks precede every version-bound write; an
unknown build or layout disables the gate. Fresh finite tracking, foreground,
Retail gameplay state, live object ownership, and mapped weapon identity are
required before injection. Host loss and waits are bounded. Incomplete eye
pairs are discarded; camera/model state is restored exactly. Unknown weapons
cannot inherit mapped swing/damage behavior. Enemy and unrecognized Retail
melee bypass the local-player gate. Retail files stay read-only, and
proprietary/generated stages, logs, binaries, saves, and assets stay out of Git.
