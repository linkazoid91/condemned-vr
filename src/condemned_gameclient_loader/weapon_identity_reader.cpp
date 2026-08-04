#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "weapon_identity_reader.h"

namespace condemnedvr {
namespace {

constexpr wchar_t kGameDatabaseModuleName[] = L"GameDatabase.dll";
constexpr char kRetailDatabaseName[] = "Database\\Dark.Gamdb00p";
constexpr char kGlobalCategoryName[] = "Arsenal/Global";
constexpr char kGlobalRecordName[] = "Global";
constexpr char kPlayerWeaponsAttributeName[] = "PlayerWeapons";
constexpr char kDefaultWeaponDataAttributeName[] = "Default";
constexpr char kAnimationPropertyAttributeName[] = "AnimationProperty";
constexpr std::int32_t kMaximumPlayerWeaponIndex = 255;

// IDatabaseMgr positions verified against Condemned 1.0.314.0's retail
// GameDatabase.dll vtable and callsites. Condemned orders the overloaded
// record/attribute lookup methods differently from the later FEAR 1.08 SDK.
// Only the read calls below are consumed.
constexpr std::size_t kOpenExistingDatabaseSlot = 0U;
constexpr std::size_t kReleaseDatabaseSlot = 6U;
constexpr std::size_t kGetCategorySlot = 7U;
constexpr std::size_t kGetRecordSlot = 14U;
constexpr std::size_t kGetRecordNameSlot = 19U;
constexpr std::size_t kGetAttributeSlot = 22U;
constexpr std::size_t kGetStringSlot = 33U;
constexpr std::size_t kGetRecordLinkSlot = 39U;

using GetDatabaseManagerFunction = void*(__cdecl*)();
using OpenExistingDatabaseFunction =
    void*(__thiscall*)(void*, const char*);
using ReleaseDatabaseFunction = void(__thiscall*)(void*, void*);
using GetCategoryFunction =
    const void*(__thiscall*)(void*, void*, const char*);
using GetRecordFunction =
    const void*(__thiscall*)(void*, const void*, const char*);
using GetRecordNameFunction =
    const char*(__thiscall*)(void*, const void*);
using GetAttributeFunction =
    const void*(__thiscall*)(void*, const void*, const char*);
using GetStringFunction = const char*(__thiscall*)(
    void*, const void*, std::uint32_t, const char*);
using GetRecordLinkFunction = const void*(__thiscall*)(
    void*, const void*, std::uint32_t, const void*);

struct DatabaseReadApi {
    HMODULE module{nullptr};
    void* manager{nullptr};
    OpenExistingDatabaseFunction openExistingDatabase{nullptr};
    ReleaseDatabaseFunction releaseDatabase{nullptr};
    GetCategoryFunction getCategory{nullptr};
    GetRecordFunction getRecord{nullptr};
    GetRecordNameFunction getRecordName{nullptr};
    GetAttributeFunction getAttribute{nullptr};
    GetStringFunction getString{nullptr};
    GetRecordLinkFunction getRecordLink{nullptr};
};

bool IsExecutableModuleAddress(
    const void* address,
    HMODULE module) noexcept {
    if (address == nullptr || module == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(
            address, &information, sizeof(information)) !=
        sizeof(information)) {
        return false;
    }
    const DWORD protection = information.Protect &
        ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    const bool executable = protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    return executable && information.AllocationBase == module;
}

RetailWeaponIdentityReadResult ResolveDatabaseReadApi(
    DatabaseReadApi& api) noexcept {
    api = {};
    api.module = GetModuleHandleW(kGameDatabaseModuleName);
    if (api.module == nullptr) {
        return RetailWeaponIdentityReadResult::
            DatabaseModuleUnavailable;
    }
    const FARPROC exported = GetProcAddress(
        api.module, "GetIDatabaseMgr");
    if (!IsExecutableModuleAddress(exported, api.module)) {
        return RetailWeaponIdentityReadResult::
            DatabaseExportUnavailable;
    }

    __try {
        api.manager =
            reinterpret_cast<GetDatabaseManagerFunction>(exported)();
        if (api.manager == nullptr) {
            return RetailWeaponIdentityReadResult::
                DatabaseManagerUnavailable;
        }
        void** const vtable =
            *reinterpret_cast<void***>(api.manager);
        if (vtable == nullptr) {
            return RetailWeaponIdentityReadResult::
                DatabaseInterfaceMismatch;
        }
        const std::size_t slots[] = {
            kOpenExistingDatabaseSlot,
            kReleaseDatabaseSlot,
            kGetCategorySlot,
            kGetRecordSlot,
            kGetRecordNameSlot,
            kGetAttributeSlot,
            kGetStringSlot,
            kGetRecordLinkSlot};
        for (const std::size_t slot : slots) {
            if (!IsExecutableModuleAddress(
                    vtable[slot], api.module)) {
                return RetailWeaponIdentityReadResult::
                    DatabaseInterfaceMismatch;
            }
        }
        api.openExistingDatabase =
            reinterpret_cast<OpenExistingDatabaseFunction>(
                vtable[kOpenExistingDatabaseSlot]);
        api.releaseDatabase =
            reinterpret_cast<ReleaseDatabaseFunction>(
                vtable[kReleaseDatabaseSlot]);
        api.getCategory = reinterpret_cast<GetCategoryFunction>(
            vtable[kGetCategorySlot]);
        api.getRecord = reinterpret_cast<GetRecordFunction>(
            vtable[kGetRecordSlot]);
        api.getRecordName =
            reinterpret_cast<GetRecordNameFunction>(
                vtable[kGetRecordNameSlot]);
        api.getAttribute =
            reinterpret_cast<GetAttributeFunction>(
                vtable[kGetAttributeSlot]);
        api.getString = reinterpret_cast<GetStringFunction>(
            vtable[kGetStringSlot]);
        api.getRecordLink =
            reinterpret_cast<GetRecordLinkFunction>(
                vtable[kGetRecordLinkSlot]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        api = {};
        return RetailWeaponIdentityReadResult::AccessViolation;
    }
    return RetailWeaponIdentityReadResult::Ok;
}

bool CopyDatabaseString(
    char* destination,
    std::size_t destinationSize,
    const char* source) noexcept {
    if (destination == nullptr || destinationSize == 0U ||
        source == nullptr || source[0] == '\0') {
        return false;
    }
    return strncpy_s(
               destination, destinationSize, source, _TRUNCATE) == 0 &&
        destination[0] != '\0';
}

} // namespace

RetailWeaponIdentityReadResult ReadRetailWeaponIdentity(
    std::int32_t playerWeaponIndex,
    RetailWeaponIdentitySnapshot& snapshot) noexcept {
    snapshot = {};
    snapshot.playerWeaponIndex = playerWeaponIndex;
    if (playerWeaponIndex < 0 ||
        playerWeaponIndex > kMaximumPlayerWeaponIndex) {
        return RetailWeaponIdentityReadResult::InvalidIndex;
    }

    DatabaseReadApi api{};
    RetailWeaponIdentityReadResult result =
        ResolveDatabaseReadApi(api);
    if (result != RetailWeaponIdentityReadResult::Ok) {
        return result;
    }

    void* database = nullptr;
    __try {
        database = api.openExistingDatabase(
            api.manager, kRetailDatabaseName);
        if (database == nullptr) {
            result = RetailWeaponIdentityReadResult::
                DatabaseUnavailable;
        } else {
            do {
                const void* const globalCategory = api.getCategory(
                    api.manager, database, kGlobalCategoryName);
                if (globalCategory == nullptr) {
                    result = RetailWeaponIdentityReadResult::
                        GlobalCategoryUnavailable;
                    break;
                }
                const void* const globalRecord = api.getRecord(
                    api.manager, globalCategory, kGlobalRecordName);
                if (globalRecord == nullptr) {
                    result = RetailWeaponIdentityReadResult::
                        GlobalRecordUnavailable;
                    break;
                }
                const void* const playerWeapons = api.getAttribute(
                    api.manager, globalRecord,
                    kPlayerWeaponsAttributeName);
                if (playerWeapons == nullptr) {
                    result = RetailWeaponIdentityReadResult::
                        PlayerWeaponsUnavailable;
                    break;
                }
                const void* const weaponRecord = api.getRecordLink(
                    api.manager, playerWeapons,
                    static_cast<std::uint32_t>(playerWeaponIndex),
                    nullptr);
                if (weaponRecord == nullptr) {
                    result = RetailWeaponIdentityReadResult::
                        WeaponRecordUnavailable;
                    break;
                }
                snapshot.nameResolved = CopyDatabaseString(
                    snapshot.recordName,
                    sizeof(snapshot.recordName),
                    api.getRecordName(api.manager, weaponRecord));
                if (!snapshot.nameResolved) {
                    result = RetailWeaponIdentityReadResult::
                        WeaponNameUnavailable;
                    break;
                }

                const void* const defaultDataAttribute =
                    api.getAttribute(
                        api.manager, weaponRecord,
                        kDefaultWeaponDataAttributeName);
                const void* const weaponData =
                    defaultDataAttribute == nullptr
                    ? nullptr
                    : api.getRecordLink(
                        api.manager, defaultDataAttribute, 0U,
                        nullptr);
                if (weaponData == nullptr) {
                    result = RetailWeaponIdentityReadResult::
                        WeaponDataUnavailable;
                    break;
                }
                const void* const animationPropertyAttribute =
                    api.getAttribute(
                        api.manager, weaponData,
                        kAnimationPropertyAttributeName);
                if (animationPropertyAttribute == nullptr) {
                    result = RetailWeaponIdentityReadResult::
                        AnimationPropertyUnavailable;
                    break;
                }
                snapshot.animationPropertyResolved =
                    CopyDatabaseString(
                        snapshot.animationProperty,
                        sizeof(snapshot.animationProperty),
                        api.getString(
                            api.manager,
                            animationPropertyAttribute,
                            0U, nullptr));
                if (!snapshot.animationPropertyResolved) {
                    result = RetailWeaponIdentityReadResult::
                        AnimationPropertyUnavailable;
                    break;
                }
                snapshot.poseFamily =
                    ClassifyRetailWeaponAnimationProperty(
                        snapshot.animationProperty);
                result = RetailWeaponIdentityReadResult::Ok;
            } while (false);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = RetailWeaponIdentityReadResult::AccessViolation;
    }

    if (database != nullptr) {
        __try {
            api.releaseDatabase(api.manager, database);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (result == RetailWeaponIdentityReadResult::Ok) {
                result = RetailWeaponIdentityReadResult::
                    AccessViolation;
            }
        }
    }
    return result;
}

RetailWeaponIdentityReadResult ReadRetailWeaponIdentityCatalog(
    RetailWeaponIdentityCatalog& catalog) noexcept {
    catalog = {};
    for (std::int32_t weaponIndex = 0;
         weaponIndex <
             static_cast<std::int32_t>(kRetailWeaponCatalogCapacity);
         ++weaponIndex) {
        RetailWeaponIdentitySnapshot snapshot{};
        const RetailWeaponIdentityReadResult result =
            ReadRetailWeaponIdentity(weaponIndex, snapshot);
        if (result ==
            RetailWeaponIdentityReadResult::WeaponRecordUnavailable) {
            return catalog.count > 0U
                ? RetailWeaponIdentityReadResult::Ok
                : result;
        }
        if (snapshot.nameResolved) {
            catalog.entries[catalog.count++] = snapshot;
            continue;
        }
        if (result != RetailWeaponIdentityReadResult::
                AnimationPropertyUnavailable &&
            result != RetailWeaponIdentityReadResult::
                WeaponDataUnavailable) {
            return result;
        }
    }
    return catalog.count > 0U
        ? RetailWeaponIdentityReadResult::Ok
        : RetailWeaponIdentityReadResult::WeaponRecordUnavailable;
}

const char* RetailWeaponIdentityReadResultName(
    RetailWeaponIdentityReadResult result) noexcept {
    switch (result) {
    case RetailWeaponIdentityReadResult::Ok:
        return "ok";
    case RetailWeaponIdentityReadResult::InvalidIndex:
        return "invalid_index";
    case RetailWeaponIdentityReadResult::DatabaseModuleUnavailable:
        return "database_module_unavailable";
    case RetailWeaponIdentityReadResult::DatabaseExportUnavailable:
        return "database_export_unavailable";
    case RetailWeaponIdentityReadResult::DatabaseManagerUnavailable:
        return "database_manager_unavailable";
    case RetailWeaponIdentityReadResult::DatabaseInterfaceMismatch:
        return "database_interface_mismatch";
    case RetailWeaponIdentityReadResult::DatabaseUnavailable:
        return "database_unavailable";
    case RetailWeaponIdentityReadResult::GlobalCategoryUnavailable:
        return "global_category_unavailable";
    case RetailWeaponIdentityReadResult::GlobalRecordUnavailable:
        return "global_record_unavailable";
    case RetailWeaponIdentityReadResult::PlayerWeaponsUnavailable:
        return "player_weapons_unavailable";
    case RetailWeaponIdentityReadResult::WeaponRecordUnavailable:
        return "weapon_record_unavailable";
    case RetailWeaponIdentityReadResult::WeaponNameUnavailable:
        return "weapon_name_unavailable";
    case RetailWeaponIdentityReadResult::WeaponDataUnavailable:
        return "weapon_data_unavailable";
    case RetailWeaponIdentityReadResult::AnimationPropertyUnavailable:
        return "animation_property_unavailable";
    case RetailWeaponIdentityReadResult::AccessViolation:
        return "access_violation";
    default:
        return "unknown";
    }
}

} // namespace condemnedvr
