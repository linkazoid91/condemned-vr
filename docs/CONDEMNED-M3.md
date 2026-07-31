# Condemned M3 — stereo renderer proof

## Verified renderer path

Read-only interface discovery on the verified Steam 1.0.314.0 build found 45
registered interfaces. The relevant live registrations are:

| Interface | Version | Live object |
|---|---:|---|
| `ILTClient.Default` | 104 | present |
| `ILTRenderer.Default` | 0 | present |
| `IClientShell.Default` | 4 | present |

The Condemned renderer layout is not the F.E.A.R. donor layout. Renderer slot
18 is the one-argument player-camera forwarder at executable RVA `0x00102650`.
Its exact verified bytes forward the camera and a null technique override to
slot 20 at RVA `0x00103d20`. Every write remains guarded by the executable
identity, both RVAs, and both code signatures.

An opt-in pass-through replaced only slot 18 and invoked its original function
exactly once. Gameplay entered the hook while menus did not. Sustained calls
were observed with a stable renderer and camera handle, and manual testing
confirmed unchanged gameplay and presentation.

## Eye-separated transport gate

The first bounded stereo diagnostic was accepted on 31 July 2026. After one
normal Retail world render, the bridge copied the unchanged back buffer into a
left/right pair, added a magenta marker to only the right eye, and held the pair
for one second. Telemetry independently confirmed:

- the loader submitted exactly one diagnostic pair;
- the x86 bridge published `stereo_diagnostic_captured`;
- the x64 host selected native stereo projection and matched the image to its
  OpenXR render request; and
- mono transport resumed automatically afterward.

This proves eye ordering and the complete two-image transport. It does not yet
prove geometric depth because both eyes contain the same world render.

## Same-camera double-render gate

The second bounded stereo diagnostic was accepted on 31 July 2026. It called
only the verified slot-20 world-render overload twice for one frame, kept the
same camera transform and FOV for both calls, cleared and captured each eye
independently, held the completed pair for one second, then permanently
returned to mono for that run. Simulation, input, AI, animation update, audio,
particles, and game time were not explicitly repeated by the mod. Live testing
showed the bounded stereo pair and a clean return to normal mono presentation
with no visible game-state damage. This establishes that the world-render path
tolerates one call per eye.

## Camera ABI discovery

The official F.E.A.R. Public Tools headers document a newer `ILTClient`
revision, but Condemned's revision 104 layout diverges and cannot safely reuse
those slot numbers. Direct mapping of the verified executable identified:

- `GetObjectPos`, `SetObjectPos`, and `ForceCurrentObjectPos` at slots 17-19;
- the legacy and rigid transform overload pairs at slots 20-23;
- the compact seven-float rigid-transform getter at slot 21, executable RVA
  `0x0000c750`; and
- the `GetCameraFOV` function-pointer member at object offset `0x5c`, targeting
  executable RVA `0x0000c660`.

The read-only gate was accepted on 31 July 2026. It sampled slot 21 and
`GetCameraFOV` during normal
`RenderCamera` calls. Both targets and their code prefixes are identity-gated.
It writes only to loader-owned stack outputs, never to the engine camera, and
logged position, quaternion, and horizontal/vertical FOV every 600 world
renders. Live gameplay produced valid changing transforms for more than 6,000
world renders, a stable FOV of `(1.682818, 1.123406)` radians, and no probe
failures or exceptions.

## Next bounded gate

The next one-shot diagnostic adds only the OpenXR half-IPD to each eye along
the verified camera quaternion's local right axis. It uses 100 engine units per
meter, matching the donor port's established scale, and does not change camera
rotation. The first live attempt proved that the offset and exact transform
restoration worked, but it exposed a projection mismatch: Condemned retained
approximately `96.4 x 64.4` degrees while the OpenXR pair expected approximately
`80.0 x 88.0` degrees, so the views did not fuse into correct depth.

The revised gate sets the verified camera FOV to the shared conservative
OpenXR FOV for each render. The exact original seven-float rigid transform and
both FOV values are restored and read back after every eye; any failed write,
failed readback, mismatch, or exception discards the stereo pair. A completed
pair is held for three seconds before the run permanently returns to normal
mono rendering.

The FOV-matched live attempt still did not produce correct perceived depth.
The native SDK's `LTRotation::Right()` implementation confirms that the
quaternion-to-axis calculation is exact. The next comparison therefore keeps
the same IPD magnitude, FOV, restoration, and hold time while reversing only
the left/right camera-baseline polarity.

Normal and reversed baseline polarity were equally unfusable in live testing.
The next isolation uses the corrected FOV but a zero camera baseline. This must
appear as a fused flat image if projection and eye-image mapping are sound; a
failure to fuse would place the remaining fault downstream of camera geometry.

At tester request, these restart-bound comparisons are superseded by an
opt-in continuous stereo tuning mode. It keeps the same exact transform/FOV
restoration after every eye and falls back to Retail mono whenever a pair is
incomplete. Controls are edge-triggered while the game window is active:

| Key | Adjustment |
|---|---|
| `F4` | Toggle HMD translation (rotation remains active) |
| `F5` | Recenter HMD yaw and translation origin |
| `F6` | Toggle continuous stereo/mono |
| `F7` | Reverse baseline polarity |
| `F8` / `F9` | Decrease/increase world scale by 10 units per meter |
| `F10` | Set the baseline to zero for a flat-fusion check |
| `Page Down` / `Page Up` | Decrease/increase coupled game/compositor projection scale by 5% (100-150%) |
| `Home` | Reset to 100 units/m, normal polarity, and 130% projection |

Every adjustment is recorded as `m3_stereo_tuning_changed` in the loader log.

The launcher accepts `-RenderScale 100..200`. A live 80% diagnostic produced
the known Jupiter symptom: the scene remained anchored at the top-left with
an empty lower-right region and a mildly warped image. Although the bridge
correctly transformed the viewport and scissor rectangle, a Condemned
internal post-process retained Retail-sized assumptions. Sub-native stereo
targets are therefore rejected; `100` is the supported rollback.

The first continuous session produced comfortable 3D and validated the
per-frame stereo loop. The next revision applies the OpenXR center pose on top
of the Retail camera: full relative HMD rotation, translation limited to 0.25
meters, per-eye poses, and yaw-only recentering. The FOV controls now update the
shared host FOV scale as well as the game camera, matching F.E.A.R.'s coupled
camera/compositor scaling rather than changing only one side of the projection.
Live testing found that 130% almost covered the view during head motion. The
loader previously displayed settings through 150% while the shared protocol
silently clamped them to 130%; the limit is now genuinely 150%, and Condemned
starts at the tested 130% margin.

The native 2560x1440 / 130% configuration was then accepted in live gameplay:
the image was complete, rectangular, correctly fused, and unwarped. During the
accepted interval OpenXR held approximately 90 Hz while new stereo pairs
settled around 43-50 fps, with no projection or capture failures. This passes
the M3 stereo and head-tracking geometry gate; transport latency remains an
M5 performance task.
