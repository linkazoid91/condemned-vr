# Condemned M2 — D3D9 transport

## Passive presentation gate

The first M2 slice was accepted on 31 July 2026. The isolated Condemned stage
loaded the repository-owned `condemnedvr-d3d9.dll` through the verified M1
`GameClient.dll` loader. No ASI module or local `d3d9.dll` was present.

The bridge installed late `IDirect3DDevice9::Reset` and `Present` hooks after
verifying the exact Steam `Condemned.exe` 1.0.314.0 identity. It observed only;
capture and OpenXR were explicitly disabled. The live device reported:

| Property | Observed value |
|---|---|
| Presentation mode | exclusive fullscreen |
| Back buffer and surface | 2560 x 1440 |
| D3D format | 21 (`D3DFMT_A8R8G8B8`) |
| Swap effect | 1 (`D3DSWAPEFFECT_DISCARD`) |
| Multisampling | 4x, quality 0 |
| Presentation interval | `D3DPRESENT_INTERVAL_IMMEDIATE` |
| ASI modules | none |

The hook captures before the original `Present`, as required for a discard
swap effect. Menus, loading, normal gameplay, visual output, and performance
were manually confirmed unchanged. The process remained stable while the
bridge sampled sustained presentation windows.

The ignored live report and JSON-lines log are generated under
`stage/condemned-m2/`. They record `hooks_installed`, the successful reset,
`present_observed`, module paths, and the zero-ASI assertion.

## Mono OpenXR acceptance

The full M2 path was accepted later on 31 July 2026:

- `condemnedvr-host.exe` connected to VirtualDesktopXR 1.0.10 and a Meta
  Quest 3;
- OpenXR selected the NVIDIA GeForce RTX 4090 and created two 3072 x 3264
  swapchains;
- the guarded bridge selected the same adapter and captured the unchanged
  2560 x 1440 `D3DFMT_A8R8G8B8` back buffer;
- classic D3D9 used the donor's asynchronous CPU-to-D3D9Ex compatibility path
  and a three-slot-per-eye shared-texture ring;
- the host imported fresh frames and displayed them identically in both eyes
  on a stable 2.4 x 1.8 metre quad two metres from the viewer;
- the settled host ran at 90 Hz with zero measured image-age frames and no
  sustained long-frame condition;
- the headset image was manually confirmed visible, upright, responsive, and
  correct; and
- after normal game exit, the host detected heartbeat loss, stopped the
  OpenXR session, logged `host_stop`, and exited without intervention.

The game retained its original desktop `Present`, no ASI loaded, and both
processes remained responsive. The isolated run report and logs live beneath
`stage/condemned-m2-mono/logs/` and are intentionally excluded from source
control.

M2 is accepted. M3 may now investigate Condemned-specific renderer interfaces
and camera calls while retaining this mono transport as its fallback.

## Startup panel anchor and desktop focus

The OpenXR host is intentionally started and validated before Condemned. The
original panel logic world-locked the quad on the host's first renderable
frame, so the pose could be captured many seconds before a game image existed.
If the headset moved during game startup, the eventual splash/main-menu image
could consequently appear well above or beside the current view.

The host now treats the first complete image from a connected game as the
definitive startup anchor. On that frame it discards the pre-game pose and
places the panel centre exactly two metres along the current binocular HMD
gaze ray, while retaining the existing level, yaw-only panel orientation. A
game reconnect receives the same one-time behavior. Stereo-to-menu transitions
and explicit right-stick/F9 recentering retain their existing re-anchor paths.

The launcher also restores Condemned's verified main window to the foreground
when the handle first becomes available and once more as its final handoff.
This prevents the launcher's PowerShell window or a diagnostics window from
owning initial keyboard, mouse, and foreground-gated VR input. A refused
Windows foreground request remains non-fatal and is reported explicitly. The
anchor geometry, first-image edge, reconnect reset, and fail-closed inputs are
covered by the cross-architecture `mono_panel_anchor` test.

Before that first game frame, the launcher optionally supplies
`images/title.png` (or an explicit `-StartupImage` path) to the host. Windows
Imaging Component decodes PNG/JPEG data into an immutable sRGB D3D11 texture,
which replaces the red/blue diagnostic eye colors and is drawn identically in
both eyes. The real game image always has priority and replaces it without a
transition. A missing or invalid optional image logs a warning and retains the
diagnostic colors rather than blocking OpenXR startup. The artwork remains a
local, user-supplied file unless its redistribution rights are established; it
is not embedded into the project binary.

This startup slice passed live on 4 August 2026 in run
`run-20260804-093251`. The host recorded `startup_image_loaded` at the exact
1448x1086 source size, `startup_image_displayed`, a first-game-image
`mono_quad_startup_recenter_requested`, and the replacement
`mono_quad_anchored` in the same frame. The launch report independently
recorded `GameWindowFocusRestored: true`. The tester confirmed the title art,
clean game-frame handoff, centred initial panel, and foreground focus all
worked in-headset.

On 13 August 2026 the tester reported that a later fresh launch did not start
focused. This does not invalidate the accepted observation above, but it does
show that the original handoff was not reliable across launches. Code
inspection found two bounded launcher defects: each handoff made one
`SetForegroundWindow` request and checked it immediately, and
`GameWindowFocusRestored` latched an earlier success even though the report was
written before the launcher's actual last focus attempt.

The replacement is **implemented and automated-tested, awaiting live
validation**. Normal focus requests are retried for at most one second. If the
last handoff is still refused, the launcher temporarily attaches its input
queue to the current foreground and target game threads, restores and raises
the game window, requests foreground/active/keyboard focus, and detaches every
queue in a `finally` path. It accepts success only when the exact target
root-owner window and expected game PID own the foreground and every temporary
attachment was released. The final handoff occurs after optional diagnostics
windows have appeared, and `m2-mono-live.json` is written only afterward.
`GameWindowFocus` preserves the initial, readiness, and final attempt details,
including attempt count, standard-request result, attached-input attempt,
cleanup result, final foreground PID, and outcome; the compatibility field
`GameWindowFocusRestored` now describes only the final verified state.

`tools/test-condemned-window-focus.ps1` covers missing, invalid, exited, and
windowless targets plus bounded standard retries and the attached-input
fallback/cleanup contract without launching the game. It cannot prove live
Windows focus policy, keyboard state, or headset input. The required live gate
is a fresh canonical launch followed by first-menu input and the existing
Alt-Tab/return regression.

## OpenXR startup availability

`XR_ERROR_FORM_FACTOR_UNAVAILABLE` is temporary by definition, so the shared
host now retries only that result for up to 15 seconds at 250 ms intervals.
Every other OpenXR initialization error still fails immediately, and a real
headset timeout retains the existing exit code and launcher error.

During the M4 headset-menu acceptance, Virtual Desktop desktop streaming was
connected but VDXR had not switched its streaming source into PCVR mode. The
bounded retry behaved correctly but could not create that mode itself. A
session-scoped SteamVR manifest validated and ran the same host immediately,
without changing the system-wide OpenXR runtime. For a cold VDXR launch, switch
Virtual Desktop into VR mode before starting the host; SteamVR remains a
reversible per-run fallback.
