# Condemned VR architecture

## Scope

Condemned VR adds OpenXR presentation and input to the verified Steam
`1.0.314.0` build of *Condemned: Criminal Origins*. The retail installation is
read-only. Development copies, logs and user data exist only below ignored
project-local directories.

The architecture originated in the MIT-licensed F.E.A.R. VR project. The port
retains its proven cross-bitness OpenXR/D3D9 transport while replacing every
game-specific identity, offset, signature, hook and control mapping. Detailed
provenance is in [`../ATTRIBUTION.md`](../ATTRIBUTION.md).

## Process layout

```text
OpenXR runtime (x64)
        ^
        | OpenXR + XR_KHR_D3D11_enable
        |
condemnedvr-host.exe (x64, D3D11)
        ^
        | versioned shared memory, events and shared texture handles
        | poses, FOV, controller state, haptics and render requests
        v
Condemned.exe (x86, D3D9)
  + project GameClient.dll loader
  + verified stock client renamed locally to GameOrig.dll
  + condemnedvr-d3d9.dll transport bridge
```

The x64 host owns the OpenXR instance, session, spaces, swapchains, action set
and composition layers. The x86 bridge observes the existing Direct3D 9
device, captures completed eye images and publishes them to the host. The
project GameClient loader preserves the retail ABI, delegates exports to the
verified stock client and installs only version/signature-guarded hooks.

## Source boundaries

| Area | Responsibility | Origin |
|---|---|---|
| `src/condemned_gameclient_loader/` | Condemned module verification, delegation, camera/input/melee gates | Condemned-specific |
| `src/condemned_proxy32/` | Condemned bridge entry points and executable guard | Condemned-specific |
| `src/condemned_host64/` | Condemned host target and product definition | Condemned-specific build glue |
| `src/host64/` | OpenXR lifecycle, rendering, input and host IPC | Adapted from F.E.A.R. VR |
| `src/proxy32/bridge.*`, `system_d3d9.*` | D3D9 capture, transfer and system-loader handling | Adapted from F.E.A.R. VR |
| `src/common/` | Protocol, pose/stereo math and game-neutral state machines | Mixed shared and Condemned-specific |

The legacy F.E.A.R. GameClient, launcher, Public Tools patching and packaging
layers are not part of the current tree. Their history remains available for
provenance and reference.

## Identity and fail-closed behavior

All game-side write paths are tied to the verified Steam executable and stock
client identities. Before enabling a hook, the loader checks the expected PE
identity and live bytes around its target. A mismatch disables the gate or
aborts the guarded developer launch; it never guesses an offset.

OpenXR, Direct3D, IPC, thread creation and hook installation do not run under
the loader lock in `DllMain`. The loader starts deferred work after process
attachment and preserves stock behavior when the host is absent.

## Frame ownership and pacing

OpenXR is the frame clock:

1. The host completes `xrWaitFrame`, locates views and publishes a render
   request containing its ID, pose and FOV.
2. The game renders a left/right pair against that request. Duplicate renders
   for the same request ID are not captured again.
3. The bridge performs bounded Classic D3D9 staging/readback and D3D9Ex upload
   work while waiting for the next published request.
4. If several completed pairs are ready, only the newest survives. If the
   output slot is occupied, the new image is discarded instead of becoming a
   stale queued frame.
5. The host imports the newest completed generation and submits it to the
   matching OpenXR layer. A missing new pair may reuse the most recent image;
   the game thread never waits without a strict timeout.

This removed the old-image queue that caused visible doubling during fast
controller motion. The Classic D3D9 path still includes one bounded
GPU-to-CPU transfer and is therefore a compatibility path, not a zero-copy
design. See [`CONDEMNED-PERFORMANCE.md`](CONDEMNED-PERFORMANCE.md).

## Stereo and tracking

The loader repeats only the verified world camera render for each eye. Game
simulation, AI, particles and time advance once. OpenXR view poses are mapped
to LithTech centrally, and the same FOV used to render an eye is submitted to
OpenXR. Tracking loss or a non-finite/stale pose leaves the last safe retail
camera in control.

Menus and other verified non-gameplay states use a world-locked comfort panel
instead of attempting a second world render. Desktop window mode keeps VR
rendering active during Alt-Tab while releasing Condemned's cursor-centering
behavior whenever it is not foreground.

## Controller input

The host publishes raw OpenXR action state and hand poses. The x86 loader maps
those values into verified Condemned binding queries only while the game is
focused, tracking is fresh and the relevant probe is enabled. Keyboard and
mouse remain available in parallel, except that mouse look is suppressed
while the guarded VR head-aim path owns the camera. Haptic requests travel back
through the same versioned IPC and have bounded amplitude and duration.

## Physical melee direction

M5 treats a held weapon as a profile-driven item rather than a view-centred
attack animation. The architecture separates:

- raw controller target pose;
- weighted/collision-constrained weapon pose;
- visible model placement;
- swept-volume contact detection;
- impact qualification from contact-point velocity and energy; and
- handoff to Condemned's native damage/material response.

Current work includes a render-only model proxy, per-weapon grip calibration,
simulated weight and a temporary speed-to-retail-attack adapter for the
verified fire axe. Native physical-contact damage and a permanent standalone
weapon object remain later gates. Unknown weapons fail closed and never
inherit the axe attack profile. See [`CONDEMNED-M5.md`](CONDEMNED-M5.md).

## Distribution boundary

Build outputs are project-authored, but the local runnable stage necessarily
combines them with verified files copied from the user's game installation.
The stage is ignored and is not redistributable. A future installer must ship
only project binaries and notices, locate an existing legal installation, and
create the combined stage locally.
