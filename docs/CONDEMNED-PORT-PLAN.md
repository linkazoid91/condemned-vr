# Condemned VR Porting Plan

## Goal

Build a native VR mod for *Condemned: Criminal Origins* by reusing the proven
OpenXR, D3D9 transport, frame-pacing, and diagnostic architecture from F.E.A.R.
VR while keeping all Condemned-specific hooks behind verified version and code
signatures.

The initial target is the Steam build installed locally as version `1.0.314.0`.
The retail installation must remain recoverable, and no proprietary executable,
DLL, archive, database, or asset may be committed to Git or included in a
release package.

## Verified retail baseline

The completed installation was inspected on 31 July 2026.

| File | Version or PE identity | SHA-256 |
|---|---|---|
| `Condemned.exe` | `1.0.314.0`, PE32, image base `0x00400000`, image size `0x0018f000`, timestamp `0x43fcff00` | `45A1404F213EDBDEAD16168B6E005B245B93105F7345AAF4FB83ECB6A7C5AE02` |
| `Game/GameClient.dll` | `1.0.314.0`, PE32, image base `0x10000000`, image size `0x00194000`, timestamp `0x43fcffdf` | `0AC9798CA460C3E24EFC6D103D5FD258CCA6C921E0BD2A3FD9119D1C7C5228CC` |
| `Game/GameServer.dll` | `1.0.314.0` | `48321A894D47105707020ABF52A2B3CD2049D4366233C2CEB011240961AC26EC` |

The following compatibility facts have also been verified:

- `Condemned.exe` is a 32-bit Direct3D 9 application.
- `GameClient.dll` exports `GetBuildNumber` and `SetMasterDatabase`, matching
  the delegation seam used by the F.E.A.R. VR loader.
- The client contains the Jupiter EX interface names `ILTRenderer.Default` and
  `IClientShell.Default`.
- `default.archcfg` selects the `Game` directory and retail archives, so an
  isolated archive configuration should be investigated first.
- The retail directory currently contains Ultimate ASI Loader as `d3d9.dll`
  and `scripts/Condemned.WidescreenFix.asi`. These are a temporary behavioral
  reference, not a planned dependency. Determine which Widescreen Fix
  corrections are still required and implement those corrections in this
  repository. The isolated VR stage must then run without either third-party
  binary and must never overwrite or remove them from the retail installation.

## Proposed architecture

```text
OpenXR runtime (VDXR or SteamVR, x64)
                    ^
                    | OpenXR + D3D11
                    v
condemnedvr-host.exe (x64)
                    ^
                    | shared memory, events, shared textures
                    v
condemnedvr-d3d9.dll (x86)
                    ^
                    | explicit bridge API and D3D9 hooks
                    v
project GameClient.dll loader (x86)
                    |
                    +--> GameOrig.dll (the user's stock Condemned client)
                    |
                    v
Condemned.exe (x86, Jupiter EX, D3D9)
```

The loading path is a custom isolated `-archcfg` whose `GameClient.dll`
delegates to a locally staged copy of the stock client renamed to
`GameOrig.dll`. The shipped mod must not depend on an ASI loader. If Condemned
does not honor the required isolated launch arguments, investigate and document
another repository-owned loading path instead of silently adopting the retail
ASI chain.

## Reuse boundary

### Reuse with limited renaming

- x64 OpenXR instance, session, view, swapchain, and compositor code
- D3D11 device and texture import
- shared-memory IPC, events, seqlocks, slot ownership, and heartbeats
- OpenXR action handling and haptics
- classic-D3D9 capture and D3D9Ex transfer compatibility path
- frame-request pacing and newest-complete-frame selection
- stereo render scaling
- structured logging and diagnostic probes
- host/runtime selection and most packaging concepts

### Replace or revalidate for Condemned

- executable and module fingerprints
- all binary RVAs, prefixes, object offsets, and vtable slots
- client-shell and renderer interface layouts
- player-camera discovery and `RenderCamera` call path
- menu, video, cutscene, execution, and gameplay-state detection
- camera effects, damage effects, post-processing, and HUD composition
- input command identifiers and controller mappings
- player body, hand, melee weapon, firearm, and interaction transforms
- installer discovery, launch arguments, filenames, and user-data paths

No F.E.A.R. RVA or structure offset is valid merely because the games use the
same engine family.

## Development milestones

### M0 — Reproducible desktop baseline

Current evidence and remaining gates are recorded in
[`CONDEMNED-M0.md`](CONDEMNED-M0.md).

1. Record the complete retail manifest and hashes.
2. Run Condemned without VR and capture clean startup logs.
3. Test once with the Widescreen Fix enabled and once without it, inventory
   every relevant behavioral difference, and identify the corrections that
   must be implemented natively in this repository.
4. Establish the normal window, resolution, D3D device, swap-chain, and
   presentation behavior.

Exit condition: the verified retail build starts and plays normally, and its
existing modifications are understood.

### M1 — Isolated stock-client delegation

Current implementation evidence is recorded in
[`CONDEMNED-M1.md`](CONDEMNED-M1.md).

1. Rename project products and IPC objects from F.E.A.R. VR to Condemned VR.
2. Build a minimal x86 `GameClient.dll` loader.
3. Create an isolated archive configuration and user-data directory.
4. Copy the user's stock client into the stage as `GameOrig.dll`.
5. Delegate `GetBuildNumber` and `SetMasterDatabase` without installing any
   gameplay or renderer hooks.
6. Prove that menus, a new game, save/load, and exit work exactly as retail.

Exit condition: Condemned runs through the project loader with no VR behavior
and no retail files overwritten.

### M2 — Mono OpenXR transport

The passive D3D9 presentation gate and its exact live parameters are recorded
in [`CONDEMNED-M2.md`](CONDEMNED-M2.md).

1. Start the reusable x64 OpenXR host before the game.
2. Load the renamed D3D9 bridge explicitly from the client loader.
3. Hook Direct3D creation, the actual presenting swap chain, `Present`, and
   `Reset`.
4. Capture immediately before presentation and publish the completed frame.
5. Display menus and gameplay on a stable OpenXR quad.
6. Validate both VDXR and SteamVR where practical.

Exit condition: the exact desktop frame is visible in the headset with stable
frame delivery, clean shutdown, and desktop fallback when the host is absent.

### M3 — Stereo renderer proof

Current interface, renderer-slot, pass-through, and eye-separated transport
evidence is recorded in [`CONDEMNED-M3.md`](CONDEMNED-M3.md).

1. Locate the renderer interface and player-camera render path.
2. Validate candidate vtable slots and function prefixes against
   `GameClient.dll` version `1.0.314.0`.
3. Demonstrate left/right eye separation using an obvious one-frame diagnostic.
4. Repeat only the world-render call; never repeat simulation, AI, animation
   updates, input, audio, particles, or game time.
5. Restore camera and D3D state after each eye.
6. Keep menus, videos, and unsafe camera sequences on the mono comfort quad.

Exit condition: gameplay renders as a stable stereo pair with correct eye
ordering, scale, FOV, and no doubled game behavior.

Status: passed live at native 2560x1440 render scale and 130% coupled FOV.
Stereo depth, full HMD rotation/translation, restoration, and the complete
unwarped image were accepted by the tester. Sub-native render targets are not
supported because Condemned retains Retail-sized internal post-processing.

### M4 — Head tracking and controller input

Current controller-transport, command-ID, and first locomotion-gate evidence
is recorded in [`CONDEMNED-M4.md`](CONDEMNED-M4.md).

1. Determine Condemned's coordinate and world-unit mapping empirically.
2. Apply rotational tracking, then positional tracking.
3. Map basic locomotion and menu actions through verified game commands.
4. Add snap/smooth turning, recentering, crouch, interaction, and haptics.
5. Preserve keyboard, mouse, and flat-screen fallback.

Exit condition: a level can be navigated and basic interactions completed
entirely with VR controllers.

### M5 — Condemned-specific gameplay

1. Discover player-body, hand, weapon, block, shove, and interaction paths.
2. Implement melee alignment and collision without changing hit timing or
   damage rules.
3. Handle firearms and forensic tools.
4. Separate HUD from world rendering.
5. Classify executions, scripted cameras, damage effects, and cutscenes as
   native stereo or comfort-screen sequences.
6. Add VR settings, performance controls, diagnostics, and safe rollback
   switches.

Exit condition: representative melee, firearm, investigation, cutscene,
save/load, and level-transition scenarios work in a sustained VR session.

### M6 — Packaging and release validation

1. Build an installer that discovers the user's legal retail installation.
2. Copy required stock modules into an isolated stage at install time.
3. Exclude all retail and third-party binaries from Git and release archives.
4. Record commit, configuration, hashes, and runtime choice per launch.
5. Run automated x86/x64 tests and a complete manual regression checklist.

Exit condition: a clean package installs, updates, launches, and uninstalls
without depending on the Widescreen Fix or Ultimate ASI Loader and without
damaging either the retail installation or any third-party files left there.

## First implementation checklist

- [ ] Rename CMake targets, namespaces exposed to the OS, executable names,
      configuration files, and log event prefixes.
- [x] Add a Condemned `1.0.314.0` version descriptor using the verified hashes
      and PE identities above.
- [x] Create the minimal stock-client loader with no hooks enabled.
- [x] Prototype isolated `-archcfg` and `-userdirectory` launch arguments.
- [ ] Inventory the Widescreen Fix behavior and record which corrections the
      repository must provide before the external ASI is excluded from testing.
      Its aspect-ratio effect is confirmed, but desktop widescreen is not an
      initial VR requirement: native world rendering will use OpenXR per-eye
      projections and flat UI may remain on a proportioned 4:3 comfort panel.
- [x] Verify stock-client delegation through menus and one playable scene.
- [x] Add a no-capture D3D9 bridge load/unload smoke test.
- [x] Add exact presenting-swap-chain logging.
- [x] Produce the first mono OpenXR frame.

## Safety and engineering rules

- Unknown executable or client versions disable all version-bound hooks.
- Every hard-coded address requires module identity and byte-signature checks.
- Retail calls reached through uncertain layouts are protected by narrow
  failure boundaries, with a normal retail fallback.
- OpenXR, D3D, IPC, logging threads, and file work never start inside
  `DllMain`.
- Capture occurs before the original `Present` when discard swap effects are
  used.
- Failed D3D calls are errors, not black-frame pixel samples.
- Only completed stereo pairs may be published.
- The OpenXR host may disappear at any time without trapping the game.
- The initial implementation optimizes for diagnostic certainty before
  performance.

## Immediate next step

Commit the accepted M4 right-stick turning and Retail-state headset menu slice.
Then extend the verified controller path to recentering and the first basic
interaction command while preserving mouse, keyboard, and flat-panel fallback.
Keep physical-Escape, desktop menu controls, and a full movie sequence in the
release regression pass.
