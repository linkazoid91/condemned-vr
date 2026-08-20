# Third-party notices

This project uses or derives from the components below. Dependencies are
pinned to a commit or tag and are downloaded only during explicit local setup.
No dependency is downloaded while the game is running.

## Source lineage

| Component | Use | Source and pin | License |
|---|---|---|---|
| F.E.A.R. VR | OpenXR host, D3D9 transport, IPC, math, diagnostics and build foundation | [DR-89/fear-vr](https://github.com/DR-89/fear-vr), baseline [`24a6e22f`](https://github.com/DR-89/fear-vr/commit/24a6e22f20a02e64aa0955738f1050357b265400) | MIT |

The original F.E.A.R. VR copyright and permission notice are retained in
[`LICENSE`](LICENSE), and more detailed provenance is recorded in
[`ATTRIBUTION.md`](ATTRIBUTION.md).

## Build dependencies

| Component | Use | Source | License | Pin |
|---|---|---|---|---|
| Khronos OpenXR-SDK | x64 host headers and static loader | [KhronosGroup/OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) | Apache-2.0 | `release-1.1.59`, `e5df31de6c15b4900aee3092273194e51282000d` |
| Khronos OpenXR-SDK-Source | `hello_xr` lifecycle reference only; not built | [KhronosGroup/OpenXR-SDK-Source](https://github.com/KhronosGroup/OpenXR-SDK-Source) | Apache-2.0 | `release-1.1.59`, `04e92820192a6eec490e5eb8ffbd8211bafb0551` |
| MinHook | Guarded x86 hooks | [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | `v1.3.4`, `c3fcafdc10146beb5919319d0683e44e3c30d537` |
| DirectXMath | Pose and projection math | Windows 10/11 SDK | MIT | Supplied by the installed Windows SDK |

`tools/prepare-dependencies.ps1` verifies the exact repository origins and
commit IDs. Local dependency checkouts live under ignored `vendor-local/` and
are never included in a release package.

## Audio assets

The following project-distributed sound effects were supplied through
Freesound under the CC0 1.0 Universal public-domain dedication. Attribution is
not required by CC0; the creator and canonical source are recorded here for
provenance.

| Repository asset | Original sound | Creator and source | License | Repository SHA-256 |
|---|---|---|---|---|
| `sounds/colt45_slide_pull.wav` | `Slide pull.wav` | Nanashi, [Freesound sound 104409](https://freesound.org/people/Nanashi/sounds/104409/) | [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) | `DDC9920E64C99E0F75DAED6B5F3D6B3DDB13933C12A4E0631D77109ECAF1FC42` |
| `sounds/colt45_slide_return.wav` | `Beretta M9 slide release` | vabadus, [Freesound sound 151067](https://freesound.org/people/vabadus/sounds/151067/) | [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) | `028A7976EBC5B629F944C2AF3126296E4CDC19512DE9F09829D41209CEF7485E` |

The repository filenames describe their use by Condemned VR; they do not
claim that the original recordings were made from the in-game Colt model.

## Reference-only projects

- [EchoPatch](https://github.com/Wemino/EchoPatch), GPL-3.0: its description
  of the Jupiter EX redundant HID initialization defect informed the
  investigation. Condemned VR independently verifies and patches the live
  Condemned byte ranges. EchoPatch source and binaries are not copied, built,
  linked or distributed here.
- [FEAR-MORE](https://github.com/SendoTarget/FEAR-MORE): historical build and
  staging reference for the upstream F.E.A.R. VR project. No FEAR-MORE code or
  binaries are incorporated into the Condemned targets.

## Proprietary game components excluded from this repository

The user supplies a legally acquired installation locally. The development
stage may copy verified files into ignored project-local directories, but
these files must never be committed or packaged:

- `Condemned.exe`, `GameClient.dll`, `GameServer.dll`, archives, databases,
  models, textures, audio, video and other *Condemned: Criminal Origins*
  content;
- F.E.A.R. executables, DLLs, archives, Public Tools source or binaries, and
  extracted assets; and
- third-party ASI loaders, widescreen-fix binaries or other mods unless their
  redistribution terms have been reviewed and their inclusion explicitly
  documented.

The repository's staging scripts read and verify local game files; they do
not modify the retail installation. Any future distributable must contain
only project-authored binaries and notices, then locate the user's existing
game installation at install time.

## Release rule

Run `tools/audit-publication.ps1` and manually inspect the release archive
before publishing. If a dependency or asset is added later, record its exact
source, version, license and redistribution basis here before it enters a
release.
