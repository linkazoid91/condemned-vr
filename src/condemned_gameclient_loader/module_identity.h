#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace condemnedvr {

constexpr std::size_t kSha256Bytes = 32;

struct PeIdentity {
    std::uint16_t machine{};
    std::uint16_t optionalHeaderMagic{};
    std::uint32_t timestamp{};
    std::uint32_t imageBase{};
    std::uint32_t sizeOfImage{};
    std::uint64_t fileSize{};
};

struct ExpectedModuleIdentity {
    PeIdentity pe{};
    unsigned char sha256[kSha256Bytes]{};
};

enum class ModuleIdentityResult {
    ok,
    fileOpenFailed,
    fileReadFailed,
    invalidPe,
    peMismatch,
    hashFailed,
    hashMismatch,
};

bool ReadPeIdentity(const wchar_t* path, PeIdentity* identity) noexcept;
bool ComputeSha256(
    const wchar_t* path,
    unsigned char (&hash)[kSha256Bytes]) noexcept;
ModuleIdentityResult VerifyModuleIdentity(
    const wchar_t* path,
    const ExpectedModuleIdentity& expected) noexcept;
ModuleIdentityResult VerifyCondemnedGameClient(
    const wchar_t* path) noexcept;
ModuleIdentityResult VerifyCondemnedExecutable(
    const wchar_t* path) noexcept;
const char* ModuleIdentityResultName(ModuleIdentityResult result) noexcept;

} // namespace condemnedvr
