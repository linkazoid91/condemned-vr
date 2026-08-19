# Legacy DirectX VR stereo handoff

This is a game-neutral research playbook for carrying the stereo lessons from
Condemned VR into an older DirectX title. DirectX 6 and the Dungeon Keeper 2
dxwrapper fork are the concrete starting point.

It is not a claim that another game, DK2, or that wrapper already has working
VR. It separates live-proven Condemned invariants, source-verified wrapper
capabilities, and hypotheses that must be tested again in the target.

## The rule that matters most

Advance the game once, obtain one coherent OpenXR render request, and repeat
only a verified world-render operation for the left and right eyes. Apply the
matching eye pose and projection immediately before each render, capture both
images, restore every changed game and graphics state exactly, and publish the
pair only if both eyes and every restoration step succeeded.

Do not run the whole frame twice. That can double simulation, AI, animation,
particles, input, audio, and game time while still producing bad stereo.

## Evidence boundary

| Claim | Evidence level |
|---|---|
| A world-only render can be called twice while simulation advances once | Live verified in Condemned 1.0.314.0; must be rediscovered in the target |
| Matched eye camera/FOV, exact restoration, complete-pair publication, and newest-frame pacing produce accepted stereo | Live verified in Condemned; the invariants transfer, the implementation does not |
| The referenced DK2 fork translates DK2's IDirectDraw4/IDirect3D3 path through dxwrapper's Dd7to9 path | Source and release verified at v1.0.0; not live-tested here |
| The fork converts selected D3DFVF_XYZRHW draws back to XYZ and emits them through D3D9 | Source verified at v1.0.0; built for RTX Remix, not stereo |
| The fork can be combined with OpenXR without target changes | Hypothesis requiring bounded integration tests |
| A Condemned scale, FOV, camera layout, hook slot, or address applies elsewhere | Unsupported until independently measured |

Condemned's 100 units/metre, 130% projection tuning, camera ABI, hook
signatures, and addresses are game-specific. Do not copy them.

## Recommended architecture

Use dxwrapper as an API normalizer, then place a game-neutral version of the
Condemned transport after normalization:

```text
32-bit DirectX 6 game
  -> ddraw.dll stub
  -> dxwrapper.dll: DirectDraw/Direct3D -> D3D9
  -> D3D9 eye capture and transport
  -> versioned request/events/complete-pair slots
  -> 64-bit D3D11 OpenXR host
  -> OpenXR projection views
```

Keep camera ownership on the game side. The host publishes one immutable
request containing a frame ID, predicted display time, and both eye poses and
FOVs. The game side decides whether a verified world render is safe, renders
the pair, and returns only a complete generation.

OpenXR's standard Windows graphics bindings cover D3D11 and D3D12, not legacy
D3D6/D3D9 swapchains. A modern host or compositor boundary is still needed
after dxwrapper makes the game render through D3D9.

## How the DK2 release helps

Pin the supplied reference before modifying it:

- repository:
  [mencelot/dk2-dxwrapper-with-path-tracing-support](https://github.com/mencelot/dk2-dxwrapper-with-path-tracing-support);
- release:
  [v1.0.0](https://github.com/mencelot/dk2-dxwrapper-with-path-tracing-support/releases/tag/v1.0.0);
- tag commit: `c0b3441850f2da044c3aef7833c155984af39d65`;
- release ZIP SHA-256 published by GitHub:
  `73A09584A7151FE1679E327BCCDBD194CC18E0E5FCC2089F508EC0090E0FDAF0`.

Useful parts of the fork:

- an established DirectDraw/Direct3D compatibility boundary;
- conversion through Dd7to9;
- interception at BeginScene, DrawPrimitive, DrawIndexedPrimitive, surface
  operations, and the D3D9 endpoint;
- optional reverse projection of CPU-pretransformed XYZRHW vertices;
- an auditable location for view/projection state before draw calls; and
- source and patch notes rather than an opaque binary only.

RTX-specific parts are not VR-ready:

- the included d3d9.dll is the RTX Remix NV Bridge, not an OpenXR bridge;
- its fixed 60-degree elevated isometric camera is a DK2/Remix assumption, not
  a measured per-eye camera;
- it deliberately retains synthetic matrices for Remix camera detection,
  whereas VR must restore borrowed state at its ownership boundary;
- reconstructing XYZ does not itself render two distinct eyes; and
- texture-atlas tracing is unrelated to stereo.

There can be only one local d3d9.dll endpoint. For VR, choose one controlled
chain:

1. integrate eye capture into the dxwrapper fork after Dd7to9; or
2. make dxwrapper load an intentional VR D3D9 proxy, which loads system D3D9.

The first option has fewer loader-order and export-forwarding variables. Keep
a pass-through switch that returns to unmodified dxwrapper behavior.

The repository root is MIT-licensed, while the bundled dxwrapper tree carries
a multi-component license notice. Preserve all notices and audit the exact
files redistributed.

## DirectX 6 discovery before stereo

Do not begin by editing projection matrices. Produce a read-only render trace
for the exact target executable first.

### 1. Freeze identity

Record executable hash and PE timestamp, imported graphics DLLs, display mode,
surface format, and loaded wrappers. Verify the actual COM interfaces; do not
infer DirectX 6 from the release year.

For DK2, the fork reports DirectDrawCreate, IDirectDraw4, and IDirect3D3.
Recheck those facts against the exact game build.

### 2. Find the actual presentation path

Trace:

- primary, back-buffer, render-target, texture, and Z-surface creation;
- Flip, Blt, and BltFast;
- BeginScene/EndScene;
- render-target and Z-buffer attachments;
- display-mode/cooperative-level changes; and
- surface loss and restoration.

After Dd7to9, also observe D3D9 device creation, Reset, and Present. The old
front-end Flip and normalized D3D9 Present are different stages. Logging both
prevents a hook from being placed on the wrong frame.

### 3. Classify the geometry

| Observed path | Stereo route |
|---|---|
| XYZ plus meaningful world/view/projection transforms | Hook the engine camera or transform handoff, then repeat the world render |
| XYZRHW with usable depth and a stable inverse projection | Reconstruct XYZ, then rerender/replay world draws with distinct eye matrices |
| XYZRHW produced by a discoverable CPU camera routine | Prefer changing the engine camera before CPU projection |
| Sprites, text, cursor, movies, or depthless XYZRHW | Treat as 2D and compose once on a panel/layer |
| No world-only boundary and no reversible geometry | Stop at mono/flat OpenXR until new evidence appears |

Changing a downstream view matrix cannot create stereo from vertices already
projected to final screen coordinates. The DK2 fork's inverse conversion is
valuable, but its depth, camera, UI classification, and indexed/non-indexed
coverage must be verified before relying on it.

### 4. Separate the frame stages

```text
UPDATE -> WORLD CAMERA -> WORLD DRAWS -> EFFECTS -> HUD/UI -> PRESENT
```

The repeatable operation must begin after update and end before one-copy UI or
presentation work. A hook that wraps the whole sequence is too high-level for
safe stereo.

### 5. Find the camera authority

Trace position, orientation, projection/FOV, near/far planes, viewport, aspect
ratio, handedness, units, and the point where CPU projection occurs. Use
read-only samples and bounded A/B diagnostics. Do not make an address, vtable
slot, matrix layout, or scale authoritative without identity and code evidence.

## The stereo transaction

```text
request = read_one_coherent_xr_request()
if request is stale/invalid or mode is not verified gameplay:
    render_retail_once()
    return

saved_game = snapshot_camera_and_projection()
saved_graphics = snapshot_every_state_the_mod_changes()

for eye in [left, right]:
    bind_eye_target_and_matching_depth()
    apply_eye_pose_relative_to_retail_camera(request.eye[eye])
    apply_projection_matching_submitted_fov(request.eye[eye])
    clear_eye_target()
    call_verified_world_render_only()
    capture_eye()
    restore_and_verify_game_state()
    restore_and_verify_graphics_state()

if both eyes and all restores succeeded:
    publish_complete_pair(request.frame_id)
else:
    discard_partial_pair()
```

Guard against recursion and concurrent attempts. Call a verified lower-level
world-render operation from the hook, not the hooked entry again.

DirectX 6 has no D3D9 all-state block to depend on. Maintain an explicit ledger
for every mutated state, including as applicable:

- render target, back-buffer relationship, and Z attachment;
- viewport and clipping;
- world/view/projection transforms;
- render, light, material, texture, palette, fog, and texture-stage state;
- scene nesting and wrapper-owned synthetic matrices; and
- surface/resource generation.

Verify restoration by readback where possible. An exception, failed setter,
failed readback, lost surface, or missing eye invalidates the pair.

## Projection and pose rules

- Read both views from the same xrLocateViews result and predicted display time.
- Derive eye separation from the poses; never hard-code an IPD.
- Apply tracking relative to a recentered game-camera basis, not the absolute
  room origin.
- Determine handedness, axes, quaternion/matrix convention, and units once in a
  tested module. Do not fix a local error with an isolated sign flip.
- Use asymmetric per-eye projection when the engine accepts a matrix. If it
  exposes only symmetric FOV, derive one intentionally and submit exactly the
  FOV that was rendered.
- Start at native game dimensions. Scaling and supersampling are later gates.
- Invalid, stale, or non-finite tracking returns to the Retail/flat path.

## Prove transport before depth

1. Capture one completed game frame to a world-locked OpenXR quad.
2. Copy it to both eye slots and add a marker to one eye only.
3. Require the marker in the correct headset eye.
4. Render the same camera twice into separate targets.
5. Use matched XR FOV with a zero eye baseline; it must fuse as a flat image.
6. Only then add eye offsets, polarity diagnostics, and head tracking.

For native DX6, first try two compatible off-screen DirectDraw surfaces with
matching Z storage. If target switching is unreliable, render to the normal
back buffer and copy each eye before the next render. If Dd7to9 is stable,
prefer D3D9 targets after normalization.

Zero-copy is not required for the proof. A bounded CPU readback/upload path can
establish correctness. Optimize only after telemetry identifies the bottleneck.

## Frame pacing and pair ownership

1. The host waits for XR, locates both views, and publishes a monotonically
   increasing request ID.
2. The game renders at most one pair for that ID.
3. A duplicate request may cause only a short measured wait while capture work
   continues.
4. Newest complete wins; never queue stale head poses.
5. If an output slot is occupied, discard instead of blocking indefinitely.
6. The host may reuse the last complete pair when no new pair is ready.
7. Host loss, game exit, or heartbeat timeout releases all waits.

Condemned's 20 ms bound and three slots per eye are references, not universal
settings.

## UI and lifecycle

- Repeat only the verified world pass during gameplay.
- Render HUD/UI once after the world, or classify it into an OpenXR panel.
- Use the completed 2D frame on a comfort panel for menus, loading, movies, and
  unknown modes.
- Invalidate partial pairs and cached camera/object pointers on mode changes,
  save/load, level changes, lost surfaces, and device recreation.
- Preserve keyboard, mouse, non-VR, host-absent, and desktop output.

For an XYZRHW game, UI classification is mandatory: reverse projection can
incorrectly turn sprites or text into world geometry.

## Bounded research gates

| Gate | Experiment | Required evidence |
|---|---|---|
| G0 pass-through | Load with VR writes disabled | Exact interfaces logged; menus/gameplay/present/focus/exit unchanged |
| G1 Dd7to9 | Normalize only | Stable D3D9 device/present; DirectDraw surfaces and resets recover |
| G2 mono XR | Final image on a panel | Upright complete image; bounded disconnect/exit |
| G3 eye transport | Same image, one-eye marker | Correct eye order and one matched pair ID |
| G4 world boundary | Same-camera world render twice for one frame | Two world calls, one update, exact restore, automatic fallback |
| G5 zero baseline | Matched FOV, identical eye positions | Fused flat image |
| G6 eye geometry | Measured offsets with polarity toggle | One polarity gives stable correctly fused depth |
| G7 head pose | Relative rotation, then bounded translation | Correct yaw/pitch/roll; no jump on recenter/tracking loss |
| G8 modes | Pause/load/movie/return | Readable flat UI; invalidation; stereo resumes once |
| G9 pacing | Fast head motion with age/drop telemetry | No stale queue, doubling, unbounded wait, or growing image age |
| G10 lifecycle | Alt-Tab, mode change, lost surface, save/load, exit | Resources rebuild once; fallback works; no hang |

Automated tests can prove math, finite-value gates, protocol layout, pair state,
resource generations, and timeouts. They cannot prove perceived scale, fusion,
polarity, live callback ownership, or latency.

## Minimum diagnostics

Carry one correlation ID through:

```text
XR REQUEST -> GAME MODE -> CAMERA -> TRANSFORM -> WORLD RENDER
  -> EYE CAPTURE -> PAIR DECISION -> TRANSPORT -> XR SUBMIT -> RESTORE
```

Record executable/wrapper hashes, interface versions, request ID/time, eye
pose/FOV validity, game mode, update/world-render counts, camera and projection
hashes before/during/after, target/depth identity and generation, capture and
discard reasons, request/image age, reuse/drops/waits, runtime/adapter/import
result, surface loss/recreation, focus, fallback, and exit.

## First experiment with the supplied fork

1. Preserve the working DK2/RTX setup and make a reversible project-local VR
   stage.
2. Keep the DirectDraw front end and Dd7to9.
3. Remove the RTX runtime/NV Bridge from the VR stage, or replace that endpoint
   with one explicit VR-to-system-D3D9 chain.
4. Disable the fixed synthetic camera and path-tracing-only behavior by default.
5. Log interfaces, surfaces, scene boundaries, XYZ versus XYZRHW draw counts,
   transforms, and both front-end and D3D9 presentation.
6. Confirm whether world, UI, cursor, and movies can be distinguished without
   changing output.
7. Produce a mono OpenXR panel from the normalized image.
8. Then instrument the game update/camera boundary and try a one-frame
   same-camera double render.

For DK2, the decisive early question is whether the engine can project the
world twice, or dxwrapper must reconstruct and replay buffered world draws for
the second eye. Answer that with call counts and markers before committing to
a final renderer.

## Reuse boundary

Safe concepts to carry from Condemned:

- coherent versioned render requests;
- x86 game/x64 D3D11 OpenXR separation;
- complete-pair generations and newest-image selection;
- bounded waits, heartbeat loss, and stock fallback;
- centralized pose/projection math;
- exact per-eye state restoration; and
- the transport, double-render, zero-baseline, polarity, tracking, UI, pacing,
  and lifecycle gates above.

Re-prove all target identities, addresses, interfaces, matrices, coordinates,
scale, FOV, state ownership, and resource behavior. Condemned's current
product guards and retained fearvr compatibility names do not support another
executable.

Condemned evidence and implementation anchors:

- [ARCHITECTURE.md](ARCHITECTURE.md)
- [CONDEMNED-M2.md](CONDEMNED-M2.md)
- [CONDEMNED-M3.md](CONDEMNED-M3.md)
- [CONDEMNED-PERFORMANCE.md](CONDEMNED-PERFORMANCE.md)
- [COORDINATE-SYSTEM.md](COORDINATE-SYSTEM.md)
- [TESTING.md](TESTING.md)
- `src/condemned_gameclient_loader/renderer_probe.cpp`
- `src/proxy32/bridge.cpp`
- `src/host64/ipc_bridge.cpp`
- `src/common/protocol.h`

External primary references:

- [DK2 dxwrapper source](https://github.com/mencelot/dk2-dxwrapper-with-path-tracing-support)
- [DK2 dxwrapper v1.0.0](https://github.com/mencelot/dk2-dxwrapper-with-path-tracing-support/releases/tag/v1.0.0)
- [upstream dxwrapper](https://github.com/elishacloud/dxwrapper)
- [OpenXR registry](https://registry.khronos.org/OpenXR/)
- [XR_KHR_D3D11_enable](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XR_KHR_D3D11_enable.html)
- [Microsoft DirectDraw surface creation](https://learn.microsoft.com/en-us/previous-versions/ms785028%28v%3Dvs.85%29)
- [Microsoft DirectDraw surface and Flip reference](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nn-ddraw-idirectdrawsurface7)
