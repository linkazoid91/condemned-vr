# Condemned VR M0 Baseline

This document records the reproducible desktop baseline for the initial Steam
target. M0 is in progress; no VR, renderer, or gameplay hook is enabled.

## Automated verification

Run the quick identity check:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\verify-condemned-m0.ps1 -Quick
```

Capture a complete project-local manifest:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\verify-condemned-m0.ps1
```

The full command hashes every file below the detected retail root and writes
`stage\condemned-m0\retail-manifest.json`. It does not create, change, rename,
or delete any retail file. `stage` is ignored by Git.

The helper parser tests run without a game installation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\test-condemned-m0-tools.ps1
```

## Captured baseline — 31 July 2026

| Property | Result |
|---|---|
| Steam app | `4720`, Condemned: Criminal Origins |
| Steam build | `15838` |
| Installed version | `1.0.314.0` |
| Files captured | 189 |
| Bytes captured | 7,383,254,613 |
| `Condemned.exe` | hash and PE identity verified |
| `Game\GameClient.dll` | hash and PE identity verified |
| `Game\GameServer.dll` | hash and PE32 identity verified |

The first automated run corrected one transcription error in the original port
plan: the verified `GameClient.dll` timestamp is `0x43FCFFDF`, not
`0x43FCFFDE`. The independently read raw PE header and the parser agree, while
the previously recorded SHA-256 remains correct.

## External fix reference

These files are currently present in the retail installation. They are inputs
to M0 comparison tests, not dependencies of the planned mod:

| File | SHA-256 |
|---|---|
| `d3d9.dll` (Ultimate ASI Loader) | `BE0368CDF350CD621C42835E34F01DEB181870D1B171952BFF82B122D3344264` |
| `scripts\Condemned.WidescreenFix.asi` | `93EBCF16A911E081C487F13CB38E799AB0825F89078EEBCA5659DCD2F8EBF7B0` |
| `scripts\Condemned.WidescreenFix.ini` | `13C75B8F7A77ADCB1D2C228C0ED93535F000BF84A8E3C198C94D5B2D4509BB3F` |

The active INI enables five behaviors that must be investigated separately:

- aspect-ratio correction and resolution enumeration;
- menu-background scaling;
- the long-session low-frame-rate correction;
- relocation of settings/save data away from the public documents path;
- borderless windowed mode.

The current retail `default.archcfg` contains only:

```text
Game
Game\CondemnedA.Arch00
Game\CondemnedL.Arch00
```

## Live reference run — Widescreen Fix enabled

User test on 31 July 2026:

- launched successfully through Steam app `4720`;
- the user confirmed that widescreen presentation and gameplay aspect ratio
  appeared correct;
- the presentation appeared fullscreen, but this does not distinguish
  exclusive fullscreen from a borderless window and remains unverified;
- the process was no longer running after the user report.

Menu scaling, save/load, sustained frame pacing, exact window flags, render
dimensions, exit behavior, and the individual contribution of each enabled fix
were not specifically measured in this run.

## Live stock run — isolated stage, no ASI

An isolated stock stage was launched successfully on 31 July 2026. The user
confirmed that the game was running normally. Automated inspection proved:

- runtime executable:
  `stage\condemned-m0\stock-no-asi\Condemned.exe`;
- `d3d9.dll` came from the 32-bit Windows system directory;
- zero `.asi` modules were loaded;
- stock `GameClient.dll`, `GameServer.dll`, and `ClientFx.fxd` loaded through
  the staged `Game` junction;
- neither the Ultimate ASI Loader nor Condemned Widescreen Fix participated in
  this run.

Evidence:
`stage\condemned-m0\stock-no-asi-live-20260731-191854.json`.

A fresh launch captured the active, non-minimized window at `5120x1440`,
position `0,0`, with style `0x14000000`: no caption, thick frame, or
`WS_POPUP`. This proves a frameless display-sized presentation. It is
consistent with the user's earlier impression of fullscreen and does not match
the conventional `WS_POPUP` style normally used for borderless windowed mode,
but exact D3D9 exclusive-mode state still requires presentation-parameter
instrumentation.

User comparison of this no-ASI run:

- the game rendered at 4:3 rather than widescreen, despite the display-sized
  `5120x1440` window;
- loading into gameplay worked;
- flat-screen performance behaved as expected.
- the game closed normally, and the process exited completely.

This proves what the external `FixAspectRatio` changes, but does **not** make
desktop widescreen a VR requirement. Native world rendering will use the two
OpenXR eye projections rather than the desktop aspect ratio. Menus and videos
can retain their original 4:3 composition on a correctly proportioned comfort
panel, and a desktop mirror may be letterboxed. Basic stock startup, archive
loading, ordinary flat-screen performance, and clean shutdown already work
without the external loader.

Initial classification of the five external behaviors:

| Behavior | Initial VR decision |
|---|---|
| `FixAspectRatio` | Not required for native per-eye world projection; desktop widescreen is optional and deferred. |
| `FixMenu` | Do not assume it is needed; preserve original UI proportions on the comfort panel first. |
| `FixLowFramerate` | Implemented in-repo as a byte-verified Jupiter EX HID correction; OpenXR-request pacing handles the separate stale-frame problem. See `CONDEMNED-PERFORMANCE.md`. |
| `FixSavePath` | Prefer the repository-owned isolated `-userdirectory`; verify persistence there. |
| `BorderlessWindowed` | The external implementation is not required. Repository-owned `-DesktopWindow` supplies the test mirror and version-guarded focus/cursor handling needed for headset development. |

Menu behavior, the HID-dependent low-frame-rate behavior, and repository-owned
windowed Alt-Tab handling are live verified. Save/load persistence and a
longer release soak remain unverified.

## Remaining M0 gates

- Verify `FixSavePath` behavior against the repository-owned isolated user
  directory. The VR-relevant `FixLowFramerate` behavior is now measured and
  implemented in-repo.
- Record window style, client/back-buffer dimensions, D3D9 adapter and device
  creation parameters, presentation parameters, active swap chain, `Present`,
  and `Reset`.
- Exercise menus, a playable scene, save/load, and normal exit.
- Confirm the initial behavior classification above and assign only required
  behaviors to a repository-owned implementation and test.

M0 is complete only after those live comparisons pass. M1 stock-client
delegation must not be treated as validated before then.
