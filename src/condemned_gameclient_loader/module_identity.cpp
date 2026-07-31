#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include "module_identity.h"

#include <cstring>

namespace condemnedvr {
namespace {

constexpr ExpectedModuleIdentity kCondemnedGameClientIdentity{
    {
        IMAGE_FILE_MACHINE_I386,
        IMAGE_NT_OPTIONAL_HDR32_MAGIC,
        0x43FCFFDF,
        0x10000000,
        0x00194000,
        1601536,
    },
    {
        0x0A, 0xC9, 0x79, 0x8C, 0xA4, 0x60, 0xC3, 0xE2,
        0x4E, 0xFC, 0x6D, 0x10, 0x3D, 0x5F, 0xD2, 0x58,
        0xCC, 0xA6, 0xC9, 0x21, 0xE0, 0xBD, 0x2A, 0x3F,
        0xD9, 0x11, 0x9D, 0x1C, 0x7C, 0x52, 0x28, 0xCC,
    },
};

constexpr ExpectedModuleIdentity kCondemnedExecutableIdentity{
    {
        IMAGE_FILE_MACHINE_I386,
        IMAGE_NT_OPTIONAL_HDR32_MAGIC,
        0x43FCFF00,
        0x00400000,
        0x0018F000,
        1576960,
    },
    {
        0x45, 0xA1, 0x40, 0x4F, 0x21, 0x3E, 0xDB, 0xDE,
        0xAD, 0x16, 0x16, 0x8B, 0x6E, 0x00, 0x5B, 0x24,
        0x5B, 0x93, 0x10, 0x5F, 0x73, 0x45, 0xAA, 0xF4,
        0xFB, 0x83, 0xEC, 0xB6, 0xA7, 0xC5, 0xAE, 0x02,
    },
};

bool ReadExactAt(
    HANDLE file,
    std::uint64_t offset,
    void* destination,
    DWORD bytesToRead) noexcept {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
        return false;
    }
    DWORD bytesRead = 0;
    return ReadFile(
               file, destination, bytesToRead, &bytesRead, nullptr) != FALSE &&
        bytesRead == bytesToRead;
}

} // namespace

bool ReadPeIdentity(const wchar_t* path, PeIdentity* identity) noexcept {
    if (path == nullptr || identity == nullptr) {
        return false;
    }

    const HANDLE file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool valid = false;
    do {
        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0) {
            break;
        }

        IMAGE_DOS_HEADER dos{};
        if (!ReadExactAt(file, 0, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < static_cast<LONG>(sizeof(dos))) {
            break;
        }

        IMAGE_NT_HEADERS32 headers{};
        if (!ReadExactAt(
                file,
                static_cast<std::uint64_t>(dos.e_lfanew),
                &headers,
                sizeof(headers)) ||
            headers.Signature != IMAGE_NT_SIGNATURE ||
            headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            break;
        }

        identity->machine = headers.FileHeader.Machine;
        identity->optionalHeaderMagic = headers.OptionalHeader.Magic;
        identity->timestamp = headers.FileHeader.TimeDateStamp;
        identity->imageBase = headers.OptionalHeader.ImageBase;
        identity->sizeOfImage = headers.OptionalHeader.SizeOfImage;
        identity->fileSize = static_cast<std::uint64_t>(fileSize.QuadPart);
        valid = true;
    } while (false);

    CloseHandle(file);
    return valid;
}

bool ComputeSha256(
    const wchar_t* path,
    unsigned char (&hash)[kSha256Bytes]) noexcept {
    if (path == nullptr) {
        return false;
    }

    const HANDLE file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    unsigned char* hashObject = nullptr;
    bool succeeded = false;

    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            break;
        }

        DWORD hashObjectBytes = 0;
        DWORD resultBytes = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&hashObjectBytes),
                sizeof(hashObjectBytes),
                &resultBytes,
                0)) ||
            resultBytes != sizeof(hashObjectBytes)) {
            break;
        }

        hashObject = static_cast<unsigned char*>(
            HeapAlloc(GetProcessHeap(), 0, hashObjectBytes));
        if (hashObject == nullptr) {
            break;
        }
        if (!BCRYPT_SUCCESS(BCryptCreateHash(
                algorithm,
                &hashHandle,
                hashObject,
                hashObjectBytes,
                nullptr,
                0,
                0))) {
            break;
        }

        unsigned char buffer[64 * 1024]{};
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(
                    file,
                    buffer,
                    static_cast<DWORD>(sizeof(buffer)),
                    &bytesRead,
                    nullptr)) {
                break;
            }
            if (bytesRead == 0) {
                succeeded = BCRYPT_SUCCESS(BCryptFinishHash(
                    hashHandle,
                    hash,
                    static_cast<ULONG>(kSha256Bytes),
                    0));
                break;
            }
            if (!BCRYPT_SUCCESS(BCryptHashData(
                    hashHandle, buffer, bytesRead, 0))) {
                break;
            }
        }
    } while (false);

    if (hashHandle != nullptr) {
        BCryptDestroyHash(hashHandle);
    }
    if (hashObject != nullptr) {
        HeapFree(GetProcessHeap(), 0, hashObject);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CloseHandle(file);
    return succeeded;
}

ModuleIdentityResult VerifyModuleIdentity(
    const wchar_t* path,
    const ExpectedModuleIdentity& expected) noexcept {
    if (path == nullptr || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return ModuleIdentityResult::fileOpenFailed;
    }

    PeIdentity actual{};
    if (!ReadPeIdentity(path, &actual)) {
        return ModuleIdentityResult::invalidPe;
    }
    if (actual.machine != expected.pe.machine ||
        actual.optionalHeaderMagic != expected.pe.optionalHeaderMagic ||
        actual.timestamp != expected.pe.timestamp ||
        actual.imageBase != expected.pe.imageBase ||
        actual.sizeOfImage != expected.pe.sizeOfImage ||
        actual.fileSize != expected.pe.fileSize) {
        return ModuleIdentityResult::peMismatch;
    }

    unsigned char actualHash[kSha256Bytes]{};
    if (!ComputeSha256(path, actualHash)) {
        return ModuleIdentityResult::hashFailed;
    }
    if (std::memcmp(
            actualHash, expected.sha256, sizeof(actualHash)) != 0) {
        return ModuleIdentityResult::hashMismatch;
    }
    return ModuleIdentityResult::ok;
}

ModuleIdentityResult VerifyCondemnedGameClient(
    const wchar_t* path) noexcept {
    return VerifyModuleIdentity(path, kCondemnedGameClientIdentity);
}

ModuleIdentityResult VerifyCondemnedExecutable(
    const wchar_t* path) noexcept {
    return VerifyModuleIdentity(path, kCondemnedExecutableIdentity);
}

const char* ModuleIdentityResultName(ModuleIdentityResult result) noexcept {
    switch (result) {
        case ModuleIdentityResult::ok:
            return "ok";
        case ModuleIdentityResult::fileOpenFailed:
            return "file_open_failed";
        case ModuleIdentityResult::fileReadFailed:
            return "file_read_failed";
        case ModuleIdentityResult::invalidPe:
            return "invalid_pe";
        case ModuleIdentityResult::peMismatch:
            return "pe_mismatch";
        case ModuleIdentityResult::hashFailed:
            return "hash_failed";
        case ModuleIdentityResult::hashMismatch:
            return "hash_mismatch";
    }
    return "unknown";
}

} // namespace condemnedvr
