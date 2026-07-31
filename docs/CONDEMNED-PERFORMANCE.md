# Condemned VR performance and frame pacing

Status: live accepted on 1 August 2026 using VirtualDesktopXR 1.0.10 at
90 Hz.

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
