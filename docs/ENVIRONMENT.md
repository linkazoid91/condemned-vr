# Development environment

## Supported target

The currently verified game target is the Steam release of *Condemned:
Criminal Origins*:

| Item | Verified value |
|---|---|
| Steam App ID | `4720` |
| Executable | `Condemned.exe` |
| File version | `1.0.314.0` |
| Architecture | PE32 / x86 (`Machine 0x014C`) |
| Executable SHA-256 | `45A1404F213EDBDEAD16168B6E005B245B93105F7345AAF4FB83ECB6A7C5AE02` |
| Executable timestamp | `0x43FCFF00` |
| Stock client SHA-256 | `0AC9798CA460C3E24EFC6D103D5FD258CCA6C921E0BD2A3FD9119D1C7C5228CC` |
| Renderer | Direct3D 9 |
| Engine | LithTech Jupiter EX |

These values identify a compatible local copy; they do not grant permission
to redistribute the corresponding files. Other game builds are unsupported
until independently identified and their hook signatures are verified.

## Required tools

- Windows 10 or 11
- Visual Studio 2022 with Desktop development with C++
- MSVC x86 and x64 tools
- A current Windows 10/11 SDK
- CMake 3.21 or newer
- Git
- An x64 OpenXR runtime and headset for live tests

No tool should be installed system-wide by an automated project script. The
dependency setup script clones only the pinned source dependencies below into
ignored `vendor-local/`.

## Pinned dependencies

| Dependency | Pin |
|---|---|
| OpenXR-SDK | `release-1.1.59` / `e5df31de6c15b4900aee3092273194e51282000d` |
| OpenXR-SDK-Source | `release-1.1.59` / `04e92820192a6eec490e5eb8ffbd8211bafb0551` |
| MinHook | `v1.3.4` / `c3fcafdc10146beb5919319d0683e44e3c30d537` |

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\prepare-dependencies.ps1
```

Use `-VerifyOnly` to check existing local clones without network access.

## Architecture-specific builds

The game-side modules are x86 and the OpenXR host is x64. CMake build trees
must remain separate. The supported one-step command is:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-all.ps1
```

That script configures `build\condemned-x86-vs` and
`build\condemned-x64-vs`, builds them, runs both CTest suites and writes an
ignored artifact manifest under `stage/`.

## OpenXR runtimes

The launcher uses the system active x64 runtime unless an explicit runtime
manifest is supplied. Runtime selection is scoped to the host process through
`XR_RUNTIME_JSON`; project scripts do not replace the machine-wide active
runtime. Virtual Desktop VDXR and SteamVR-backed operation have both been
tested during development.

## Local data boundary

The following directories may contain downloads, generated binaries, retail
copies, saves, logs or captures and are intentionally ignored:

- `vendor-local/`
- `build/`
- `stage/`
- `logs/`
- `dist/`
- `local-runtime/`

Do not attach these directories to issues or upload them to a public release
without inspecting every file.
