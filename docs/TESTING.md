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

## M5 physical-melee gates

Physical melee is intentionally incremental. Validate each gate separately:

1. Fresh controller world pose and finite kinematics.
2. Wall-only collision proxy with native actor damage blocked.
3. Visible weapon proxy aligned through the per-weapon grip profile.
4. Weighted follow behavior with safe resets after tracking loss, weapon
   changes, recentering and long frame gaps.
5. Swing adapter only for explicitly mapped weapons; unmapped weapons remain
   disabled.
6. Later native damage handoff only after contact position, normal, speed,
   energy and de-duplication are independently proven.

The detailed design, current fire-axe values and in-headset calibration
controls are in [`CONDEMNED-M5.md`](CONDEMNED-M5.md).

## Evidence collection

Each launch creates a session-specific log directory. Preserve, at minimum:

- executable and stock-client identity results;
- enabled/disabled feature gates and rollback switches;
- OpenXR runtime name, refresh rate and session transitions;
- game FPS, XR submit FPS, image reuse, image/request age and queue/slot drops;
- left/right render, bridge transfer and host copy timing;
- hook/signature failures, tracking loss and focus transitions; and
- physical-melee profile identity, pose validity, sweep speed and accepted
  impact reason.

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
