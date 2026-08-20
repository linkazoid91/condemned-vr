# Condemned VR

Experimental OpenXR VR mod for the Steam release of Condemned: Criminal
Origins. This package targets only Condemned.exe 1.0.314.0, Steam App ID 4720.

## Requirements

- A legally acquired Steam installation of Condemned: Criminal Origins
- Windows 10 or 11, 64-bit
- A headset and an x64 OpenXR runtime such as SteamVR or Virtual Desktop VDXR

This archive contains no game files. The installer reads and verifies the
required files from your own Steam installation.

## Install

Extract the ZIP, then double-click Install.cmd.

The installer finds the Steam library, verifies the exact supported game
build, and creates an isolated installation at:

    %USERPROFILE%\CondemnedVR

The Steam directory is never modified. If automatic detection fails:

~~~powershell
Install.cmd -RetailRoot 'D:\SteamLibrary\steamapps\common\Condemned Criminal Origins'
~~~

Choose another isolated target with:

~~~powershell
Install.cmd -InstallDir 'D:\Games\CondemnedVR'
~~~

## Update

Close the game and run Install.cmd from the newly downloaded package. The
installer replaces package-owned files, refreshes the verified local stage,
and preserves saves, profiles, logs, and weapon settings.

## Play

Use the Condemned VR desktop shortcut or double-click Play.cmd in the install
directory. No feature parameters are required: the normal launch selects the
Current profile.

Optional runtime selection:

~~~powershell
Play.cmd -Runtime steamvr
Play.cmd -Runtime vdxr
~~~

Verify the complete installation without starting the game:

~~~powershell
Play.cmd -VerifyOnly
~~~

The guarded bare-transport fallback remains available:

~~~powershell
Play.cmd -Minimal
~~~

## Uninstall

Uninstall is a dry run unless Apply is explicitly supplied:

~~~powershell
Uninstall.cmd
Uninstall.cmd -Apply
~~~

The default keeps the isolated userdata directory and the per-user weapon
settings under LocalAppData. To remove those as well:

~~~powershell
Uninstall.cmd -Apply -IncludeUserData
~~~

The uninstaller verifies its exact installation marker and Game junction
before deletion. It never removes or changes the Steam installation.

## Project status

Condemned VR remains experimental. CURRENT_STATE.md and TESTING.md in the docs
folder describe the exact automated and live evidence boundaries for this
build.

## License

Project-authored code is provided under the MIT license. See LICENSE,
ATTRIBUTION.md, and THIRD_PARTY_NOTICES.md.
