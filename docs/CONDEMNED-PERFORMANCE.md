# Condemned VR performance and frame pacing

Status: request pacing live accepted on 1 August 2026 using
VirtualDesktopXR 1.0.10 at 90 Hz. Representative 90 Hz headroom with the
newer gameplay, IK, and diagnostic workload is under renewed investigation;
the newer observations below do not invalidate the accepted pacing algorithm.

## Jupiter EX HID/FPS correction

Condemned 1.0.314.0 contains the same redundant HID/joystick initialization
pattern that causes FEAR's device-count-dependent frame-rate loss. The
repository-owned correction is deliberately narrower than loading EchoPatch:

- require the verified Condemned executable identity (SHA-256
  `45A1404F213EDBDEAD16168B6E005B245B93105F7345AAF4FB83ECB6A7C5AE02`,
  PE timestamp `0x43FCFF00`);
- verify all 76 live bytes at RVAs `0x82670`, `0x826F6`, and `0x82780`;
- NOP all three ranges only when every identity and byte check passes;
- otherwise leave the running image unchanged and fail the guarded launch;
- never modify `Condemned.exe` on disk.

The correction is applied before the late D3D hooks. A successful run logs
`hid_fps_fix_applied`. Use `-NoHidFpsFix` only for an A/B rollback.

## OpenXR request pacing

The Classic D3D9 compatibility path must read completed staging copies back
to the CPU and upload them to D3D9Ex. Letting the game render freely can put
old completed images ahead of the current OpenXR request, producing visible
double images during fast head or controller motion.

Condemned now uses the latest OpenXR request as its frame clock once native
stereo gameplay has begun:

1. A completed stereo pair records the OpenXR request ID used to render it.
2. If the next world render sees the same request ID, the bridge waits no
   longer than 20 ms for a newer request.
3. During that bounded wait it services completed D3D9 staging work and
   releases output slots.
4. Duplicate request IDs are not captured again. If an output slot is still
   occupied, the new image is discarded instead of queued for stale delivery.
5. A new request already visible to the game is rendered immediately; it does
   not incur another pacing wait.

Startup menus remain free-running until the first native stereo pair. Host
loss always falls back after a bounded wait. `-NoXrFramePacing` restores the
old free-running behavior for A/B diagnostics.

## Live evidence

Before request pacing, run `run-20260731-153729` showed approximately 88 XR
fps and 86.6 imported game fps. Genuine generation-based reuse averaged about
56 of every 300 XR frames, average request age was 2.6 frames, age peaks
reached eight frames, and the capture path accumulated 1,855 occupied-slot
drops and 3,992 duplicate-request drops.

The first Condemned pacing port, `run-20260731-154708`, proved that slot drops
stopped accumulating after stereo began and reduced average image age to one
frame. It also exposed an over-wait: the game waited even when a new request
was already available, leaving stable windows around 44-62 reused images per
300 frames and worsening heavier scenes.

The corrected run, `run-20260731-160143`, waits only for duplicate request
IDs. Stable gameplay windows measured:

- 89.8-90.4 imported game fps at 89.8-90.1 XR fps;
- 14-31 genuinely reused generations per 300 XR frames in the best stable
  windows;
- one frame average request age, normally two to three frames maximum;
- essentially zero duplicate capture drops and no sustained queue growth.

Isolated runtime/`xrEndFrame` stalls can still disturb individual windows.
The user reported that fast-motion presentation was "much much smoother" in
the corrected build.

## Current performance investigation (13 August 2026)

The accepted 31 July run remains the known-good request-pacing reference, but
it is not evidence that every later feature combination sustains a fresh
stereo pair for every 90 Hz display interval. Two later gameplay runs expose
a narrower current boundary:

- `run-20260811-152219` was a five-identity held-alignment acceptance run, not
  a controlled performance run. Its native-stereo windows showed the same
  qualitative pattern as the run below: XR submission stayed close to 90 Hz
  while imported-pair rate, reuse, and request age varied with the scene.
- `run-20260812-124543` exercised the Pipe with physical melee, wall proxy,
  contact damage, collider debug drawing, the visual weapon proxy, weapon-grip
  calibration, and right-arm IK enabled. Sampled native-stereo windows held
  XR submission at 89.6-90.0 Hz while imported-pair rate varied from
  65.0-87.5 fps, reuse varied from 30-83 of every 300 XR frames, and average
  request age varied from two to five frames. Host copy averages were only
  68-113 microseconds and logged `frame_cpu_max_us` values were normally below
  2.1 ms, while the Classic-D3D9 transfer path reported 9.5-13.1 ms maxima in
  several 300-transfer windows.

These are **live observations**, not a confirmed regression or root cause.
The runs used different scenes and feature gates from the 31 July performance
acceptance, and their whole-run counters include menus and mode transitions.
In particular, cumulative slot drops must not be attributed to native stereo
without checking the individual bridge windows. The current evidence supports
a controlled A/B; it does not support relabeling request pacing as failed.

The host field named `game_fps` is also narrower than its name suggests. It is
the number of host-side `CopyResource` imports completed during the 300-frame
window, divided by the window duration. The IPC bridge can import in `Tick`
and again after publishing an OpenXR request, so this value is neither direct
Retail simulation/render time nor a guarantee that every imported generation
was displayed. Host `render_left/right_*_us` values measure CPU call duration
around swapchain acquisition and D3D11 command submission; they do not measure
the corresponding GPU work.

## Code-supported improvement candidates

Everything in this section is a **hypothesis or proposed diagnostic** until an
isolated implementation passes the automated gate and the matching live
regression in `TESTING.md`.

The relevant implementation seams are
`src/condemned_gameclient_loader/dllmain.cpp` for loader logging,
`src/proxy32/bridge.cpp` for Classic-D3D9 capture/publication,
`src/host64/ipc_bridge.cpp` for pair selection/import, and
`src/host64/openxr_host.cpp` plus `src/host64/texture_renderer.cpp` for
OpenXR submission and the final swapchain blit.

### 1. Measure the complete pair pipeline

Add monotonic timestamps and shared frame/generation correlation at these
boundaries:

```text
OpenXR request
  -> left/right world render
  -> game-device completion query
  -> each GetRenderTargetData
  -> row copy
  -> each D3D9Ex UpdateSurface
  -> pair publication query
  -> host import
  -> host swapchain blit
  -> xrEndFrame
```

Report counts plus p50/p95/p99, not only averages and one maximum. Add D3D11
GPU timestamps around the private-texture copy and swapchain blit where the
device supports them. Keep compositor/runtime frame loss or reprojection
metrics separate from application CPU timing. This is the first gate because
the existing telemetry cannot distinguish game rendering, synchronous D3D9
readback, GPU upload, or host GPU contention reliably.

### 2. Remove synchronous loader logging from hot paths

`AppendLoaderEvent` currently resolves the path, opens
`condemnedvr-loader.log`, writes one record, and closes the handle for every
event. The preserved `run-20260812-124543` checkpoint contains 22,555 records
over a session lasting less than three minutes. Many callbacks originate in
game/render-path diagnostics, so this is a concrete frame-pacing hazard even
though its exact cost has not been isolated.

Use a bounded non-blocking queue with one background writer, or at minimum a
persistent handle plus aggressive edge-only/periodic aggregation. Preserve a
dropped-record counter, timestamp records when they are produced, and flush
critical identity/failure records on orderly shutdown. The x64 host's existing
background logger is the local precedent: its comments retain live evidence
that synchronous file and console output caused multi-millisecond frame spikes.

### 3. Make newest-complete import strict and monotonic

`IpcBridge::FindAndClaimPair` sorts READY pairs by `frameId`, claims the newest
candidate visible at that instant, and leaves older READY pairs occupied. It
does not reject a generation at or below `latestGeneration_`. Because frame IDs
can restart across mode changes while publication generation remains the
transport ordering key, the current implementation is not a strict newest-only
policy over time.

A bounded candidate should:

- order complete pairs by publication generation;
- reject or retire generations no newer than the last imported generation;
- claim the newest complete pair and atomically retire older complete pairs;
- coalesce the visible-session import points so at most one selected pair is
  copied for a submitted XR frame; and
- count stale-ready retirement, non-monotonic rejection, and coalesced imports.

This candidate primarily targets presented age, unused copies, and ring-slot
pressure. It must preserve the existing disconnected, menu, mode-transition,
and incomplete-pair behavior.

### 4. Reduce explicit graphics-queue flushes

The host calls `ID3D11DeviceContext::Flush` after starting the private-texture
copy and again before `xrEndFrame`. Microsoft documents that most applications
do not need explicit D3D11 flushes and that unnecessary calls carry significant
overhead:
<https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush>.

Do not simply delete both calls. First measure and A/B a single deliberate
submission point while preserving query progress, slot lifetime, and completed
rendering before swapchain release. On the D3D9Ex publication side, successful
left and right uploads are ordered on one bridge device, so the final-eye event
query is a candidate pair fence. Polling both eyes with
`D3DGETDATA_FLUSH` is therefore a measurable consolidation opportunity.
Direct3D 9 also documents that zero-flag polling alone is not guaranteed to
make an unsubmitted query progress, so the final fence still needs a bounded
submission mechanism:
<https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dquery9-getdata>.

### 5. Do not redraw an unchanged game generation

The host currently acquires, waits, draws, and releases both OpenXR swapchain
images before it increments the generation-reuse counter. Each eye draw runs a
five-tap sharpen/upscale shader. In the recent VDXR runs the source/private
textures were 1920x1080 and the OpenXR swapchains were 3072x3264 per eye, so
redrawing a reused generation performs two large full-screen passes without
creating a newer game image.

OpenXR layers implicitly reference the most recently released image for each
swapchain. After each swapchain has a valid released image, an unchanged game
generation can therefore submit those images again with the original image
pose/FOV instead of acquiring and redrawing them:
<https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html>.

This saves only host D3D11 work; it does not remove the game's repeated world
render or the Classic-D3D9 transfer. Reset the reuse state on session,
swapchain, game-mode, source-size, and disconnect transitions, and validate the
path on both VDXR and SteamVR.

### 6. Expose bounded quality and cadence tiers

Keep bridge `RenderScale=100`. The live 80% bridge diagnostic proved that
Condemned post-processing retains Retail-sized assumptions and produces an
incomplete, warped image; sub-native bridge targets remain unsupported.

The separate Retail `DesktopWindow` resolution is a valid A/B because Retail
allocates its resolution-dependent post-processing at the requested window
size. Testing 1600x900 against the accepted 1920x1080 baseline at
`RenderScale=100` would reduce game-render and Classic-D3D9 transfer pixels by
30.6%. This is an **unvalidated quality tier**, not a new default; require live
checks for post-processing, HUD, clarity, eye fusion, fast-motion latency, and
all source/target dimensions.

The host should use the OpenXR runtime's recommended view dimensions by
default and make any sharpening/quality override explicit. Khronos defines the
recommended image rectangle as the optimal runtime-provided size:
<https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html>.
The current Quest 3 minimum does not affect the preserved VDXR runs because
their 3072x3264 recommendation is already higher; changing the default is a
portability correction, not an expected win on that runtime.

If the cleaned-up application can sustain 80 Hz or 72 Hz but not 90 Hz, expose
an opt-in cadence rather than continuously reusing images at 90 Hz. Enumerate
runtime-supported rates before requesting one, and treat the request as a
preference rather than a guarantee:
<https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrRequestDisplayRefreshRateFB.html>.
A lower refresh rate trades temporal resolution and potentially comfort for a
larger frame budget; it is not a throughput fix and requires headset acceptance.

### 7. Keep zero-copy device conversion as high-risk research

At 1920x1080 BGRA, one stereo pair is about 15.8 MiB. The Classic-D3D9 path
performs two GPU-to-CPU readbacks, two row copies, and two D3D9Ex uploads per
stereo pair before the x64 host imports the shared textures. The existing
direct-shared branch is already selected when the game device exposes
`IDirect3DDevice9Ex`; the verified Retail device does not.

A version-guarded experiment that makes the Retail renderer use a D3D9Ex
device could remove the dominant compatibility transfer, but it can change
driver, reset, presentation, lifetime, and hook behavior throughout the game.
Instrument and exhaust the isolated improvements above before attempting it.
Do not replace the intentional x86 game/x64 OpenXR-host split.

## Recommended bounded A/B order

Use the same runtime, refresh rate, save/scene, camera path, interaction script,
warm-up, measurement duration, and source state for every comparison. Change
one unknown at a time:

1. Record 1920x1080, `RenderScale=100`, 90 Hz with the representative gameplay
   features enabled but developer collider/visual drawing disabled.
2. Repeat with the diagnostic drawing and current high-volume logging enabled
   to quantify their cost separately.
3. Add the stage/correlation timestamps without changing transport behavior.
4. A/B asynchronous/aggregated loader logging.
5. A/B strict generation ordering and single-import coalescing.
6. A/B one D3D11 submission policy and one D3D9 pair fence.
7. A/B reuse without swapchain redraw.
8. Only then test 1600x900 at `RenderScale=100`, followed by an enumerated
   80 Hz or 72 Hz mode if the measured cadence supports it.

For every candidate preserve XR and imported-pair rates, p50/p95/p99 stage
times, generation reuse, request age, queue/slot/stale drops, pose fallbacks,
runtime/compositor loss, and the exact feature gates. The live acceptance is
the `TESTING.md` fast-head/controller-motion check: no stale or double images,
plus correct stereo, input, focus, and Retail fallbacks. Compilation or
headset-free tests alone cannot promote any candidate in this section.

## Diagnostics

Launch with the live performance window:

```powershell
.\tools\launch-condemned-m2-vr.ps1 -StereoTuning -RenderScale 100 `
  -LocomotionProbe -TurningProbe -MenuProbe -InteractionProbe `
  -CoreActionsProbe -HapticsProbe -RecenterProbe `
  -DesktopWindow -PerformanceProbe
```

Inspect an active or completed run independently:

```powershell
.\tools\watch-condemned-performance.ps1
.\tools\collect-condemned-performance.ps1 -Run run-20260731-160143
```

The watcher reports XR and imported-game rates, generation-based reuse,
request age, per-eye and host-copy timing, bounded pacing waits, and capture
pipeline drops. Whole-run summaries include startup menus and transitions;
use steady native-stereo windows for pacing comparisons.

On the acceptance machine, VDXR sometimes returned
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` when the launcher was invoked through a
sandboxed automation process while the same command succeeded immediately in
the user's interactive PowerShell. This is a launch-context/runtime issue,
not a Condemned bridge failure.
