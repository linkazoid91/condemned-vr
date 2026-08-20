# Condemned VR

An experimental, open-source VR mod for the Steam release of
*Condemned: Criminal Origins* (`1.0.314.0`). It adds native stereo rendering,
OpenXR head tracking and motion controls while keeping the original game
installation unchanged.

Developers and AI coding agents should start with [`AGENTS.md`](AGENTS.md),
then read [`docs/CURRENT_STATE.md`](docs/CURRENT_STATE.md) for the active gate.

The project is under active development. M1-M4 have passed their live gates;
the usable M0 baseline retains a few release-oriented checks. M5 is building
controller-driven physical melee, per-weapon handling and an in-headset
calibration/tool menu. See
[`docs/CONDEMNED-PORT-PLAN.md`](docs/CONDEMNED-PORT-PLAN.md) and
[`docs/CONDEMNED-M5.md`](docs/CONDEMNED-M5.md) for the current boundaries.

## Important legal boundary

This repository contains only project-authored source, scripts, tests and
documentation. It does **not** contain or license *Condemned* or *F.E.A.R.*
executables, DLLs, archives, models, textures, audio, extracted assets or SDK
sources. You must provide a legally acquired installation of *Condemned:
Criminal Origins*.

Condemned VR is derived from the MIT-licensed
[F.E.A.R. VR](https://github.com/DR-89/fear-vr) project. Its shared OpenXR,
D3D9, IPC, frame-pacing and math implementation remains intentionally reused.
See [`ATTRIBUTION.md`](ATTRIBUTION.md), [`LICENSE`](LICENSE) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

This is an unofficial fan project. *Condemned*, *F.E.A.R.* and their related
names and assets belong to their respective owners. No affiliation or
endorsement is claimed.

## Confirmed target

- Windows 10/11 x64 host with an x86 game process
- Steam App ID `4720`
- `Condemned.exe` version `1.0.314.0`
- Direct3D 9 game rendering bridged to a separate x64 OpenXR host
- Virtual Desktop VDXR and SteamVR-compatible OpenXR operation

Every version-specific hook verifies the expected executable/module identity
and surrounding bytes. An unknown build fails closed instead of applying
unverified offsets.

## Current capabilities

- Native per-eye stereo with relative HMD tracking
- Runtime-coupled frame pacing and newest-completed-image selection
- Guarded fix for Jupiter EX's redundant HID initialization performance loss
- OpenXR locomotion, turning, interaction, menu and core action controls
- Headset rendering for gameplay and the game's menu states
- Desktop window mode with background VR rendering and released desktop mouse
- Right-controller aiming and head-relative view control
- Experimental controller-driven melee weapon pose, simulated weight,
  fire-axe swing adapter and per-weapon live tuning
- Compact in-headset tool menu and grip-alignment wireframe
- Structured logs and live performance telemetry

## Build

Prerequisites:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.21 or newer
- Git
- Windows 10/11 SDK

Fetch the pinned open-source dependencies and build both architectures:

```powershell
powershell -ExecutionPolicy Bypass -File tools\prepare-dependencies.ps1
powershell -ExecutionPolicy Bypass -File tools\build-all.ps1
```

This is the consolidated developer feature-platform build. It compiles the
complete current source state into one coordinated artifact set: the required
x86 loader/bridges and the x64 OpenXR host. It deliberately does not collapse
the two process architectures into one executable. Experimental diagnostics
and rejected gameplay adapters remain compiled but retain their fail-closed,
opt-in, or default-off gates.

The build produces project-authored binaries under `build/`, runs the x86 and
x64 headset-free suites, and writes artifact hashes to
`stage/condemned-build-manifest.json`. Both `build/` and all locally staged
game files are ignored by Git.

## Local staging and developer launch

The current workflow is intentionally developer-oriented:

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify-condemned-m0.ps1
powershell -ExecutionPolicy Bypass -File tools\prepare-condemned-m0-stock-stage.ps1
powershell -ExecutionPolicy Bypass -File tools\prepare-condemned-m2-mono-stage.ps1
```

Then launch the consolidated current feature platform:

```powershell
.\tools\launch-condemned-m2-vr.ps1
```

No feature parameter is required for the normal developer platform. The
no-argument launch selects the canonical mapped one-handed/Pipe feature set
plus the guarded Retail VR Settings entry. `-Wait` is optional and only keeps
the launcher attached until exit. `-Minimal` retains the bare transport for a
bounded fallback, while any explicit feature-selection switch retains the
existing custom diagnostic behavior. Mutually exclusive A/B probes, high-cost
observers, and headset-rejected behavior remain out of the default.

For the one-handed physical-weapon baseline, equip `pipe_lever` (Retail weapon
index 32) after the normal launch. The former explicit command remains a
compatible alias when a Pipe-only diagnostic run is required:

```powershell
.\tools\launch-condemned-m2-vr.ps1 -WeaponTest Pipe -Wait
```

The preset enables the required controls, physical-melee proxies, grip
calibration, full arm IK, recentering, desktop-window support, and the
pipe-only live contact-damage gate. It does not enable two-hand attachment.
If Retail has not created its collision body yet, make one deliberate swing
to prime it. Later contacts are checked continuously; the Debug tab's Melee
view increments `CALLBACKS` whenever the collision body reports contact and
`HITS` when a fresh, de-duplicated pipe contact is forwarded to Retail's native
impact path. In the current lifecycle-validation build, speed and energy are
diagnostic only; see `CURRENT_STATE.md` before interpreting those counters.

The Pipe preset supports a stereo collider wireframe, hidden by default for
normal play. In VR Tools, `DRAW MELEE COLLIDER` shows it: amber is the
configured swept volume waiting for the first Retail seed, green means the
player-owned collision body is live, and the bright cross is the exact
controller-tip proxy origin. `DRAW CONTROLLERS` independently controls the
calibration wireframes. Both choices save globally and affect drawing only.

Open VR Tools with both grips + Y and select the `COLLIDER` tab to edit the
equipped weapon's local position, pitch/yaw/roll, length, radius, and direction.
Use the left stick to select, the right stick to adjust, and A to toggle
direction or reset. Changes are previewed immediately and saved automatically
by Retail weapon index.

The `GRIP` tab similarly adjusts the equipped weapon model's local position
and rotation. Menu adjustments and reset save automatically in the per-weapon
`grip` record. Its final row starts a guided two-trigger alignment: first make
the visible object and hand look right, pull and release the right trigger,
then move the physical controller to the natural grip pose and pull/release
again. The result saves that Retail item's `grip` and `right_hand_ik` records
together. The melee collider remains a separate controller-local calibration
in the `COLLIDER` tab. When using the continuous both-grips calibration
fallback, press controller Y or keyboard P to save before quitting. Saved
alignment loads automatically the next time the same Retail weapon profile is
equipped.

Release builds copy `config/condemnedvr-defaults.ini` beside the project
`GameClient.dll`. Missing player keys read that project-authored first-level
baseline; changes made in VR Tools write only to
`%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini` and override the package
one key at a time. Keep the packaged file with `GameClient.dll`, but never
distribute a player's writable INI.

## Repository layout

| Path | Purpose |
|---|---|
| `src/condemned_gameclient_loader/` | Verified x86 stock-client loader and Condemned hooks |
| `src/condemned_proxy32/` | Condemned-specific D3D9 bridge entry points |
| `src/condemned_host64/` | Condemned x64 host target |
| `src/host64/` | Shared upstream-derived OpenXR host implementation |
| `src/proxy32/` | Shared upstream-derived D3D9 capture/transport implementation |
| `src/common/` | IPC, controller, pose, stereo and melee logic |
| `tests/` | Headset-free automated tests |
| `tools/` | Build, staging, launch, diagnostics and publication audit scripts |
| `docs/` | Architecture, milestone evidence and performance notes |

The retained `fearvr` namespace, protocol constants and a few internal target
names are compatibility identifiers inherited from the upstream foundation.
They do not indicate a runtime dependency on the F.E.A.R. game.

## Publication safety check

Before committing a release or pushing a new public repository, run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\audit-publication.ps1
```

The audit checks the current index and complete reachable Git history for
forbidden game/binary file types, verifies the required notices and upstream
lineage, and flags common credential patterns. A successful audit does not
replace a human review of the final diff or release archive.

## Contributing

Keep hooks version-bound, byte-signature checked and fail-closed. Never commit
retail files, SDK sources, extracted assets, generated stages, logs, saves or
build outputs. Record live-test evidence and the rollback switch for every new
write-enabled gate.

Bug reports and pull requests are welcome, especially from testers using a
legally acquired copy of the confirmed Steam build.
