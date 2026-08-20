# Publishing Condemned VR

This repository intentionally preserves its F.E.A.R. VR Git ancestry. Do not
squash or recreate the source tree when publishing it: the existing history,
`LICENSE` and `ATTRIBUTION.md` together provide clear provenance.

## Before the first public push

1. Run `tools/audit-publication.ps1 -RequireClean`.
2. Review `git status`, the complete staged diff and the destination remote.
3. Confirm that `LICENSE`, `ATTRIBUTION.md` and
   `THIRD_PARTY_NOTICES.md` are included at the repository root.
4. Build and run both test suites with `tools/build-all.ps1`.
5. Create the package with `tools/make-release.ps1 -SkipBuild`. Do not use a
   package whose manifest reports a dirty working tree for publication.
6. Inspect the release folder, `release-manifest.json`, and ZIP manually. They
   may contain project-authored
   binaries, `condemnedvr-defaults.ini`, and notices only. The defaults file
   must sit beside the project `GameClient.dll`.
7. From the extracted package, exercise first install, update,
   `Play.cmd -VerifyOnly`, uninstall dry-run, and uninstall `-Apply` against a
   disposable isolated target. Confirm the Retail critical hashes remain
   unchanged and the verified Game junction is removed without traversal.
8. Create the public repository from this Git history. Do not upload the
   ignored `stage/`, `vendor-local/`, `build/`, `logs/`, `dist/` or
   `local-runtime/` directories through the web interface.

## Never publish

- `Condemned.exe`, retail `GameClient.dll`, `GameServer.dll`, archives,
  databases or extracted game assets;
- saves, profiles, screenshots or captured frame data from `stage/` or
  `logs/`;
- `%LOCALAPPDATA%\CondemnedVR\weapon-settings.ini` or any other player's
  writable settings/profile file;
- F.E.A.R. retail/Public Tools files retained on a development machine;
- OpenXR-SDK or MinHook checkouts from `vendor-local/`; or
- third-party mod binaries without a separate license and redistribution
  review.

The release builder must reject `Condemned.exe`, `GameOrig.dll`,
`GameServer.dll`, and `ClientFx.fxd` anywhere in the generated package. The
installer may create local copies from the end user's verified Steam install;
those generated install directories remain non-redistributable.

## Suggested repository description

> Experimental OpenXR VR mod for Condemned: Criminal Origins, derived from
> the MIT-licensed F.E.A.R. VR project.

## New assets and dependencies

Custom project-owned assets are allowed, but record their author and license
before committing them. For third-party code or assets, add the exact source,
version and redistribution terms to `THIRD_PARTY_NOTICES.md`. Never treat a
downloadable game asset as redistributable merely because it is accessible.

`config/condemnedvr-defaults.ini` is a project-authored numeric calibration
asset governed with this source tree. It must contain no Retail data, save
state, logs, captured runtime objects, or machine-specific paths. Release
tooling may copy that exact source-controlled file beside the project loader;
it must never promote a developer's LocalAppData INI automatically. The
project-local `stage/` remains non-redistributable even when it contains this
allowed file, because the same stage also contains legally installed Retail
files.
