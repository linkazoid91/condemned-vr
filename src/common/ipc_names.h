#pragma once

#include <cstdint>
#include <cwchar>
#include <string>

namespace fearvr {

inline std::wstring MakeIpcObjectName(std::uint64_t sessionId,
                                      const wchar_t* suffix) {
    wchar_t sessionText[17]{};
    std::swprintf(sessionText, sizeof(sessionText) / sizeof(sessionText[0]),
                  L"%016llX", static_cast<unsigned long long>(sessionId));
#if defined(CONDEMNEDVR_PRODUCT)
    std::wstring name = L"Local\\CondemnedVr.M2.";
#else
    std::wstring name = L"Local\\FearVr.M2.";
#endif
    name += sessionText;
    name += L'.';
    name += suffix;
    return name;
}

} // namespace fearvr
