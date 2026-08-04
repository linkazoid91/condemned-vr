# Coordinates, projection and camera

OpenXR and LithTech Jupiter EX use different handedness and forward axes.
Mappings must be defined centrally and protected by tests; never repair a
direction error with an isolated sign change at a hook site.

## Conventions

| System | Handedness | Forward | Up | Right | Unit |
|---|---|---|---|---|---|
| OpenXR tracking/view space | Right-handed | `-Z` | `+Y` | `+X` | metres |
| LithTech world/camera | Left-handed | `+Z` | `+Y` | `+X` | 100 game units/metre |

For an identity rotation, `LTRotation::Right/Up/Forward` returns
`+X/+Y/+Z`. The verified mapping is:

```text
OpenXR position -> LithTech:   ( x,  y, -z)
OpenXR quaternion -> LithTech: (-x, -y,  z, w)
```

Reflecting Z gives the quaternion vector the corresponding axial-vector sign
change. `src/common/head_tracking_math.h` owns the mapping;
`src/common/stereo_math.h` owns world scaling and projection helpers.

## Camera rules

1. Establish a neutral HMD basis from the first valid gameplay pose or an
   explicit recenter request.
2. Apply relative tracking-space motion; never inject the absolute OpenXR
   room origin into the game world.
3. Keep body/stick yaw and HMD-relative yaw separate.
4. Derive eye separation from `xrLocateViews`; never hard-code an IPD.
5. Use the exact FOV rendered for each eye in its OpenXR projection view.
6. Keep translation bounded and opt-in until world collision is authoritative.
7. On stale, invalid or non-finite tracking, stop injecting rather than
   extrapolating an unsafe camera pose.

## Weapon and controller frames

The right-hand grip pose is the held-item attachment frame. The right-hand aim
pose is a separate pointing frame used by firearm and diagnostic direction
logic. A weapon profile stores the model-local transform from its authored
object frame to the grip frame. Adjusting that profile must not change the
global OpenXR-to-LithTech conversion.

For two-handed items, the second hand will constrain orientation/leverage; it
must not introduce a second competing world origin. Physical-melee sweeps use
completed, finite weapon poses and measure velocity in tracking space so game
locomotion cannot create a false strike.

## Verification

The automated suite covers axis mapping, quaternion conversion, relative pose
composition, stereo projection and invalid inputs. Live testing has confirmed
correct Condemned head rotation, eye polarity and recenter behavior. A total
hardware tracking-loss test remains a targeted regression whenever tracking
or recenter code changes.
