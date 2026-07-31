#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "module_identity.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}
bool WriteAll(HANDLE file, const void* data, DWORD bytes) {
    DWORD written = 0;
    return WriteFile(file, data, bytes, &written, nullptr) != FALSE &&
        written == bytes;
}

} // namespace

int main() {
    wchar_t tempDirectory[MAX_PATH]{};
    wchar_t tempPath[MAX_PATH]{};
    Check(
        GetTempPathW(MAX_PATH, tempDirectory) > 0,
        "GetTempPathW succeeds");
    Check(
        GetTempFileNameW(
            tempDirectory, L"cvr", 0, tempPath) != 0,
        "GetTempFileNameW succeeds");

    unsigned char image[512]{};
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* headers = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + 0x80);
    headers->Signature = IMAGE_NT_SIGNATURE;
    headers->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    headers->FileHeader.TimeDateStamp = 0x12345678;
    headers->FileHeader.SizeOfOptionalHeader =
        sizeof(IMAGE_OPTIONAL_HEADER32);
    headers->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    headers->OptionalHeader.ImageBase = 0x10000000;
    headers->OptionalHeader.SizeOfImage = 0x00200000;

    const HANDLE file = CreateFileW(
        tempPath,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    Check(file != INVALID_HANDLE_VALUE, "fixture opens for writing");
    if (file != INVALID_HANDLE_VALUE) {
        Check(WriteAll(file, image, sizeof(image)), "fixture writes");
        CloseHandle(file);
    }

    condemnedvr::PeIdentity actual{};
    Check(
        condemnedvr::ReadPeIdentity(tempPath, &actual),
        "synthetic PE identity parses");
    Check(actual.machine == IMAGE_FILE_MACHINE_I386, "machine matches");
    Check(actual.timestamp == 0x12345678, "timestamp matches");
    Check(actual.imageBase == 0x10000000, "image base matches");
    Check(actual.sizeOfImage == 0x00200000, "image size matches");
    Check(actual.fileSize == sizeof(image), "file size matches");

    condemnedvr::ExpectedModuleIdentity expected{};
    expected.pe = actual;
    Check(
        condemnedvr::ComputeSha256(tempPath, expected.sha256),
        "SHA-256 computes");
    Check(
        condemnedvr::VerifyModuleIdentity(tempPath, expected) ==
            condemnedvr::ModuleIdentityResult::ok,
        "matching identity verifies");

    condemnedvr::ExpectedModuleIdentity wrongPe = expected;
    ++wrongPe.pe.timestamp;
    Check(
        condemnedvr::VerifyModuleIdentity(tempPath, wrongPe) ==
            condemnedvr::ModuleIdentityResult::peMismatch,
        "PE mismatch rejects");

    condemnedvr::ExpectedModuleIdentity wrongHash = expected;
    wrongHash.sha256[0] ^= 0xFF;
    Check(
        condemnedvr::VerifyModuleIdentity(tempPath, wrongHash) ==
            condemnedvr::ModuleIdentityResult::hashMismatch,
        "hash mismatch rejects");

    Check(
        condemnedvr::VerifyModuleIdentity(
            L"this-file-does-not-exist.dll", expected) ==
            condemnedvr::ModuleIdentityResult::fileOpenFailed,
        "missing file rejects");

    DeleteFileW(tempPath);
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("Condemned module identity tests passed.");
    return 0;
}
