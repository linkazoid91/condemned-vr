# Condemned M4 controller-input evidence

This milestone begins only after the native stereo and full HMD-tracking gate
in [`CONDEMNED-M3.md`](CONDEMNED-M3.md) passed. All observations below apply
to the verified Steam `1.0.314.0` executable and stock client in the isolated
no-ASI stage.

## Read-only OpenXR input transport

The first live gate sampled the shared OpenXR input state during gameplay and
performed no engine or DirectInput writes. It confirmed:

- fresh, focused samples and both active-hand bits;
- independently valid aim and grip poses for both hands;
- signed left and right stick axes;
- the complete 0-1 range for both triggers and squeeze inputs;
- primary, secondary, menu, and stick-click button bits on both hands; and
- correct neutralization and recovery when pose tracking was lost and restored.

The loader log recorded `engine_writes=0` throughout this gate.

## Retail command database discovery

Static analysis of the stock `GameOrig.dll` command-registration constructor
found these exact command IDs:

| Command | ID |
|---|---:|
| Forward / Backward | 0 / 1 |
| ForwardAxis | 2 |
| StrafeLeft / StrafeRight | 3 / 4 |
| StrafeAxis | 5 |
| Run | 16 |
| Fire | 17 |
| Block | 28 |
| ToggleMelee / AmmoCheck | 60 / 61 |
| Taser | 62 |
| Activate | 87 |
| Flashlight | 114 |
| Tools | 116 |

The six basic locomotion IDs match the donor F.E.A.R. command database, but
the later action IDs must remain Condemned-specific. The F.E.A.R.-specific
`CBindMgr::GetBindingValue` discovery signature does not match Condemned and is
not reused. A separate full control-flow match identified Condemned's evaluator
at `GameOrig.dll` RVA `0x000095f0`: it calls
`ILTInput::IsDeviceReady`, calls `GetDeviceObjectValue`, then applies the
binding's scale, offset, and dead zone. Two independent Retail callers resolve
to this function. The `CBindMgr::Update` caller at RVA `0x0000ac33` also proves
that Condemned's binding stride is 60 bytes, command ID is at offset `0x08`,
and command activation range is at offsets `0x24` and `0x28`.

## First write-enabled gate

The first attempt hooked DirectInput `GetDeviceState` and ORed W/A/S/D into
the returned 256-byte keyboard state. Live telemetry proved that the hook
covered Condemned's keyboard device and received every direction and diagonal,
but gameplay did not consume that polled state. Physical W/A/S/D continued to
work, proving that movement is resolved later through `CBindMgr`. This failed
gate was removed rather than extended into buffered or system-wide input.

`-condemnedvr-m4-locomotion` now enables a narrow hook on the verified Retail
binding evaluator. It calls Retail first, then may replace only the returned
value for Forward, Backward, StrafeLeft, or StrafeRight (commands 0, 1, 3, and
4). Retail remains responsible for command-state storage, transitions, and
callbacks. Physical bindings are therefore preserved; the hook performs no
direct command-state writes and never calls `SendInput`.

The overlay is neutral unless all of these conditions hold:

- the OpenXR sample is valid and focused;
- its sample ID changed within the preceding 250 ms;
- the left controller is active;
- Condemned owns the foreground window; and
- the binding command is one of the four verified discrete movement commands.

The gate is disabled by default and exposed by the launcher's
`-LocomotionProbe` switch. The next live test is limited to left-stick movement
and simultaneous physical-keyboard fallback; turning and actions remain
unmapped.

This gate passed live on 31 July 2026. The tester confirmed that the left stick
moved the player and that physical W/A/S/D still worked. Telemetry recorded all
four directions, diagonals, and neutral release through
`m4_binding_locomotion_applied`; the hook remained limited to evaluated Retail
binding values with no direct command-state or system-input writes.

The launcher also exposes `-DesktopWindow`, defaulting to 1920x1080, so the
desktop chat and diagnostics can remain visible during headset testing. It
sets Retail's `Windowed`, `ScreenWidth`, and `ScreenHeight` console variables
at startup. This is intentionally distinct from bridge render scaling:
Condemned allocates all resolution-dependent post-processing at the requested
window resolution rather than receiving an independently resized stereo target.
The 1920x1080 window mode was accepted live. The bridge independently observed
a 1920x1080 source and stereo target at 100% render scale, and the tester
confirmed that the desktop game no longer covered the entire display.
