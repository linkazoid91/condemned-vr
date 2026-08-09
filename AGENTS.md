# Condemned VR agent guide

This file applies to the entire repository. Read it before changing code or
documentation.

## Project summary

Condemned VR is an experimental OpenXR mod for the verified Steam
`Condemned.exe` 1.0.314.0 build. The x86 game keeps its Direct3D 9 renderer and
loads a project `GameClient.dll`; a separate x64 process owns OpenXR and D3D11.
Versioned shared memory, events, and shared textures carry render requests,
completed eye images, tracking/input, and haptics between them.

The project is reverse-engineering-heavy. Existing milestone documents are the
detailed evidence record. [`docs/CURRENT_STATE.md`](docs/CURRENT_STATE.md) is
the short, current navigation layer; it is not a replacement for that record.

## Establish the evidence level

Never collapse these into one claim:

- **Live verified:** observed on the supported Retail build with a headset/game
  run and recorded evidence.
- **Automated only:** implemented and covered by a headset-free test, but not
  yet proven against live engine/runtime behavior.
- **Implemented, awaiting validation:** present in the working tree or staged
  binary, with its required live gate still pending.
- **Hypothesis:** inferred layout, behavior, or design direction that still
  needs a bounded diagnostic.

Working-tree code and the newest explicit evidence win over an older plan or
summary. Do not rewrite historical milestone observations to match a later
implementation; add the newer result to the relevant evidence document and
update `CURRENT_STATE.md` if the present boundary changed.

## Required reading order

1. `AGENTS.md`
2. [`docs/CURRENT_STATE.md`](docs/CURRENT_STATE.md)
3. The task-specific evidence:
   - physical melee, weapon pose/weight, arm IK, or calibration:
     [`docs/CONDEMNED-M5.md`](docs/CONDEMNED-M5.md), then
     [`docs/TESTING.md`](docs/TESTING.md)
   - renderer, IPC, OpenXR, or frame pacing:
     [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),
     [`docs/CONDEMNED-PERFORMANCE.md`](docs/CONDEMNED-PERFORMANCE.md), and the
     relevant M2/M3 milestone
   - coordinates, camera, controller, or aiming:
     [`docs/COORDINATE-SYSTEM.md`](docs/COORDINATE-SYSTEM.md) and the relevant
     M3/M4/M5 milestone
   - binary identity, hooks, loading, or staging:
     [`docs/ENVIRONMENT.md`](docs/ENVIRONMENT.md), M0/M1, and
     [`docs/CONDEMNED-PORT-PLAN.md`](docs/CONDEMNED-PORT-PLAN.md)
   - arm-IK donor rationale or hazards:
     [`docs/CONDEMNED-VR-ARM-IK-HANDOFF.md`](docs/CONDEMNED-VR-ARM-IK-HANDOFF.md)
   - packaging/publication: [`docs/PUBLISHING.md`](docs/PUBLISHING.md)
4. Relevant source, tests, and launch/tool scripts. Treat prose as a map, not a
   substitute for inspecting the current implementation.

## Development philosophy

- Preserve known-good systems and make the smallest isolated change that can
  distinguish the current hypothesis.
- Instrument uncertain runtime behavior before proposing a speculative rewrite.
- Diagnose input, transform, state, decision, engine handoff, and result as
  independent pipeline stages.
- Change one unknown system at a time. Rendering, timing, hooks, physics, and
  input can fail far from the edited code.
- Never invent an RVA, object offset, signature, vtable slot, skeleton name, or
  layout. Mark an inference as a hypothesis until bounded evidence verifies it.
- Unknown executable/module identities and signature mismatches fail closed.
- Headset/game behavior is not proven by unit tests, compilation, launcher
  readiness events, or a flat-screen run.

## Settled boundaries

Do not reopen these without new contradictory evidence:

- The x86 game / x64 OpenXR-host split is intentional.
- Only the verified world-camera render is repeated per eye. Simulation, AI,
  animation update, input, audio, particles, and game time advance once.
- Native 100% render scale is the live-accepted baseline. Sub-native targets
  are unsupported because Condemned post-processing retains Retail-sized
  assumptions; treat unaccepted supersampling values as a separate live gate.
- OpenXR-to-LithTech conversion is centralized and tested. Do not repair a
  local direction error with an isolated sign flip.
- Qualified physical melee should hand off to Condemned's native impact/damage
  path so the game retains damage, material effects, AI response, sound, and
  durability rules.
- Retail installation files are read-only. Runnable combined stages remain
  project-local, ignored, and non-redistributable.

## Source map

| Path | Ownership |
|---|---|
| `src/condemned_gameclient_loader/` | x86 stock-client delegation, identity checks, Condemned-specific camera/input/melee/IK hooks, weapon identity/settings |
| `src/condemned_proxy32/` | Condemned x86 D3D9 bridge entry points, product guard, passive diagnostics |
| `src/proxy32/` | Shared D3D9 capture, IPC client, frame pacing, eye publication, HUD/overlay composition |
| `src/condemned_host64/` | Condemned x64 host build/product glue |
| `src/host64/` | OpenXR lifecycle, D3D11 import/composition, input, haptics, host telemetry |
| `src/common/` | Cross-bitness protocol and headset-free pose, stereo, input, melee, weight, IK, and menu logic |
| `tests/` | Portable x86/x64 automated tests; no headset or Retail process |
| `tools/` | Dependency, build, identity, staging, launch, telemetry, and publication workflows |
| `docs/CONDEMNED-M*.md` | Historical milestone evidence and detailed current research |

Retained `fearvr` names in protocol/targets are upstream compatibility
identifiers, not permission to reuse F.E.A.R.-specific game addresses.

## Normal workflow

```text
inspect existing evidence
  -> form one falsifiable hypothesis
  -> add/adjust bounded instrumentation if needed
  -> implement the minimal change
  -> build
  -> run headset-free tests
  -> stage verified artifacts
  -> run the smallest required live test
  -> inspect structured evidence by pipeline stage
  -> update CURRENT_STATE.md only if current knowledge changed
```

Normal full build/test command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-all.ps1
```

It builds separate x86 and x64 trees and runs CTest in both. Focused portable
tests may be run with `ctest --test-dir build\condemned-x86-vs -C
RelWithDebInfo -R <name> --output-on-failure` and the corresponding x64 tree.
`tools/test-condemned-m0-tools.ps1` and publication checks are also
headset-free. Identity/staging checks may require a legal local game install,
but not a headset.

Any change to rendering, frame pacing, OpenXR/IPC, tracking, controller input,
focus handling, game hooks, object lifetimes, simulation timing, collision,
native dispatch, haptics, or in-headset UI requires the matching live
regression from `docs/TESTING.md`. Automated tests validate algorithms and
guards; they cannot validate perception, engine callback timing, live object
layouts, or headset/runtime behavior.

## Agent rules

- Do not reopen a settled architectural decision without new evidence.
- Do not treat temporary diagnostic scaffolding as final architecture.
- Do not silently replace native game systems when a verified native handoff
  exists.
- Do not reuse F.E.A.R. offsets or object layouts in Condemned without fresh
  verification.
- Do not make a reverse-engineered address authoritative without the required
  executable/module identity and surrounding-byte checks.
- Prefer an observable diagnostic over guessing when runtime behavior is
  uncertain.
- Preserve keyboard, mouse, non-VR, host-absent, and Retail fallbacks unless a
  task explicitly changes one of them.
- Preserve freshness, focus, game-state, weapon-identity, and finite-value
  gates. Stale/unknown input must neutralize or return to Retail behavior.
- Preserve exact camera/model restoration and discard incomplete stereo pairs.
- Keep enemy and unrecognized Retail melee outside the local-player physical
  proxy gate.
- Changes to rendering, frame pacing, IPC, simulation timing, collision, or
  hooks require extra care because regressions can appear in another subsystem.
- If a task is ambiguous, inspect the existing evidence before proposing a new
  architecture.
- Do not commit build trees, stages, logs, saves, game/SDK files, extracted
  assets, or other proprietary/generated artifacts.

## Runtime evidence and knowledge updates

Use a fresh session directory and preserve the launch report, host log, bridge
log, and `condemnedvr-loader.log`. Record the exact source state, staged binary
hash, runtime, launch switches, game/weapon identity, expected observation,
actual observation, and rollback used. For complex changes, emit enough data
to separate `INPUT -> TRANSFORM -> STATE -> DECISION -> ENGINE HANDOFF ->
RESULT`; the current preferred fields and known gaps are in `CURRENT_STATE.md`.

Keep `CURRENT_STATE.md` short. Update it when a milestone, immediate objective,
known-good boundary, active scaffold, verified fact, or next experiment changes.
Put raw discoveries, run IDs, offsets, signatures, and historical detail in the
relevant milestone/research document and link to it.
