#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "renderer_probe.h"

namespace condemnedvr {

// Installs the opt-in first live gate for a Retail-native VR Settings entry.
// The entry is created by Retail's own CLTGUI factory and command 0x3A is
// intercepted only on the verified CScreenOptions vtable. Registered screen
// 24 supplies the Retail-owned CBaseScreen shell, but its unrelated/incomplete
// dormant Build/OnFocus/OnCommand implementations are bypassed. The isolated
// base-only host lifecycle is live verified. Display, VR Features, and Comfort
// remain non-mutating placeholders; Developer Tools is a persisted toggle for
// the VR tool-menu opening shortcut and still requires its focused live gate.
bool InstallRetailVrSettingsMenuProbe(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept;

// Brackets one internally generated Retail menu key edge. The integration
// uses this only to distinguish the Enter edge that opens VR Settings from a
// later deliberate category selection; physical keyboard/mouse input is not
// marked and remains Retail-owned.
void BeginRetailVrSettingsMenuKeyEdge(int virtualKey) noexcept;
void EndRetailVrSettingsMenuKeyEdge(int virtualKey) noexcept;

} // namespace condemnedvr
