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
