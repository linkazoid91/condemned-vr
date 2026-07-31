# Condemned VR M1 — Stock-client delegation

M1 introduces the smallest project-owned runtime seam: an x86
`GameClient.dll` that verifies and delegates to the user's stock client renamed
to `GameOrig.dll`. It contains no renderer, D3D, OpenXR, input, or gameplay
hook.

## Build

```powershell
cmake -S . -B build\condemned-x86-vs `
  -G "Visual Studio 17 2022" -A Win32 `
  -DCONDEMNEDVR_BUILD_M1_LOADER=ON `
  -DFEARVR_BUILD_PROXY=OFF `
  -DFEARVR_BUILD_HOST=OFF `
  -DFEARVR_BUILD_TESTS=ON

cmake --build build\condemned-x86-vs `
  --config RelWithDebInfo --parallel

ctest --test-dir build\condemned-x86-vs `
  -C RelWithDebInfo --output-on-failure
```

Result on 31 July 2026: 18/18 tests passed. The two M1-specific tests cover
synthetic PE/hash matching and mismatch rejection, plus safe export behavior
when `GameOrig.dll` is unavailable.

The build also produces `verify-condemned-gameclient.exe`. Stage preparation
runs this compiled verifier against the local stock client, preventing drift
between PowerShell validation and the identity enforced inside the loader.

## Stage and launch

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\prepare-condemned-m1-stage.ps1

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\launch-condemned-m1.ps1
```

The M1 archive configuration retains the stock loose `Game` directory and
archives, then adds `stage\condemned-m1\game-override` as the final layer. That
directory contains only:

- project `GameClient.dll`;
- verified stock `GameOrig.dll`;
- the generated loader event log after launch.

The runtime remains the M0 copied executable with system D3D9. The retail
installation is not modified.

## Live proof

Run `stage\condemned-m1\m1-live-20260731-193329.json` proved:

- project `GameClient.dll` loaded from the M1 override directory;
- verified stock `GameOrig.dll` loaded beside it;
- loader event `original_loaded` recorded identity `1.0.314.0`;
- Windows system `d3d9.dll` loaded;
- zero ASI modules loaded;
- hooks remained disabled.

The first diagnostic launch correctly failed closed with the game's
“Invalid shell DLL” message. The loader log reported `hash_mismatch`: the final
six SHA-256 bytes had been transcribed incorrectly in the compiled descriptor.
After correction, the independent compiled verifier accepted the retail DLL,
all tests passed, and the live delegation proof above succeeded. Future stage
preparation now runs that verifier before copying the loader.

## M1 acceptance

The user completed the live regression on 31 July 2026 and reported that the
game worked perfectly through the project loader. Menus, loading into a
playable scene, normal gameplay behavior, and clean exit passed. The process
was independently confirmed to have exited completely.

M1 is complete. Save-file location and persistence remain part of the broader
installer/user-directory regression, but do not block the proven stock-client
delegation seam.
