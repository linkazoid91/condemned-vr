#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "arm_ik_discovery.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace condemnedvr {
namespace {

static_assert(sizeof(void*) == 4, "Arm IK discovery is x86-only.");

using ModelHandle = std::uint32_t;
using ModelResult = std::uint32_t;

constexpr ModelResult kModelOk = 0U;
constexpr ModelHandle kInvalidModelHandle = 0xFFFFFFFFU;

// Verified against Condemned: Criminal Origins 1.0.314.0. Retail's
// CPlayerBodyMgr::Instance() returns GameOrig+0x167A50, and the public source
// layout plus live callsites both place m_hPlayerBody at +0x10.
constexpr std::uintptr_t kPlayerBodyManagerInstanceRva = 0x00002EA0U;
constexpr std::uintptr_t kPlayerBodyManagerRva = 0x00167A50U;
constexpr std::size_t kPlayerBodyObjectOffset = 0x10U;

// GameOrig callsites that resolve RightHand/LeftHand sockets load g_pModelLT
// from this address. Requiring it to equal ILTModelClient.Default prevents a
// stale interface-database layout from reaching a virtual call.
constexpr std::uintptr_t kModelClientGlobalRva = 0x00172EC0U;

constexpr std::size_t kGetSocketSlot = 1U;
constexpr std::size_t kGetSocketTransformSlot = 2U;
constexpr std::size_t kGetNodeNameSlot = 12U;
constexpr std::size_t kGetNodeTransformSlot = 13U;
constexpr std::size_t kGetNextNodeSlot = 14U;
constexpr std::size_t kGetParentSlot = 18U;
constexpr std::size_t kGetNumNodesSlot = 19U;

struct ModelTransformAbi {
    float position[3];
    float rotation[4];
    float scale;
};
static_assert(sizeof(ModelTransformAbi) == 32U);

struct InterfaceArrayAbi {
    std::uint32_t count;
    std::uint32_t capacity;
    void** items;
};

struct InterfaceDatabaseAbi {
    void** vtable;
    void* trackedPointers;
    InterfaceArrayAbi* interfaces;
};

struct InterfaceNameManagerAbi {
    void** vtable;
    const char* name;
    std::int32_t version;
    void* implementations;
    void* holders;
    void* currentInterface;
};

static_assert(sizeof(InterfaceArrayAbi) == 12U);
static_assert(sizeof(InterfaceDatabaseAbi) == 12U);
static_assert(sizeof(InterfaceNameManagerAbi) == 24U);

using GetSocketFunction = ModelResult(__thiscall*)(
    void*, void*, const char*, ModelHandle&);
using GetSocketTransformFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelTransformAbi&, bool);
using GetNodeNameFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, char*, std::uint32_t);
using GetNodeTransformFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelTransformAbi&, bool);
using GetNextNodeFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelHandle&);
using GetParentFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelHandle&);
using GetNumNodesFunction = ModelResult(__thiscall*)(
    void*, void*, std::uint32_t&);

ArmIkDiscoveryLogFunction g_log = nullptr;
void* g_playerBodyManager = nullptr;
void* g_model = nullptr;
const unsigned char* g_modelClientGlobalAddress = nullptr;
GetSocketFunction g_getSocket = nullptr;
GetSocketTransformFunction g_getSocketTransform = nullptr;
GetNodeNameFunction g_getNodeName = nullptr;
GetNodeTransformFunction g_getNodeTransform = nullptr;
GetNextNodeFunction g_getNextNode = nullptr;
GetParentFunction g_getParent = nullptr;
GetNumNodesFunction g_getNumNodes = nullptr;
const char* g_readPhase = "idle";
volatile LONG g_enabled = 0;
volatile LONG g_sampleCount = 0;
volatile LONG g_waitingLogged = 0;
volatile LONG g_modelGlobalValidated = 0;
volatile LONG g_modelGlobalWaitingLogged = 0;
void* g_loggedPlayerBody = nullptr;
void* g_failedPlayerBody = nullptr;

constexpr std::size_t kWeaponNodeCapacity = 256U;
constexpr ULONGLONG kWeaponBaselineSettleMilliseconds = 2000U;
constexpr float kWeaponTranslationPeakStepUnits = 0.05F;
constexpr float kWeaponRotationPeakStepDegrees = 0.25F;
constexpr std::uint32_t kWeaponMotionLogCapacity = 1024U;

struct WeaponNodeObservation {
    ModelHandle handle{kInvalidModelHandle};
    ModelHandle parent{kInvalidModelHandle};
    char name[128]{};
    ModelTransformAbi baselineLocal{};
    float peakTranslationUnits{0.0F};
    float peakRotationDegrees{0.0F};
    bool baselineValid{false};
};

volatile LONG g_weaponDiscoveryEnabled = 0;
void* g_weaponModelObject = nullptr;
std::int32_t g_weaponIndex = -1;
std::uint64_t g_weaponSourceGeneration = 0U;
WeaponNodeObservation g_weaponNodes[kWeaponNodeCapacity]{};
std::size_t g_weaponNodeCount = 0U;
ULONGLONG g_weaponBaselineStartTick = 0U;
ULONGLONG g_weaponLastSampleTick = 0U;
std::uint64_t g_weaponSampleSequence = 0U;
std::uint32_t g_weaponMotionLogCount = 0U;
bool g_weaponBaselineReady = false;

bool IsExecutableAddress(const void* address) noexcept {
    if (address == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(
            address, &information, sizeof(information)) !=
        sizeof(information)) {
        return false;
    }
    if (information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0U) {
        return false;
    }
    const DWORD protection = information.Protect &
        ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}

void* FindCurrentInterface(
    void* masterDatabase,
    const char* name,
    std::int32_t version) noexcept {
    __try {
        auto* const database =
            static_cast<InterfaceDatabaseAbi*>(masterDatabase);
        InterfaceArrayAbi* const interfaces = database->interfaces;
        if (interfaces == nullptr || interfaces->items == nullptr ||
            interfaces->count > interfaces->capacity ||
            interfaces->count > 4096U) {
            return nullptr;
        }
        for (std::uint32_t index = 0;
             index < interfaces->count; ++index) {
            auto* const manager =
                static_cast<InterfaceNameManagerAbi*>(
                    interfaces->items[index]);
            if (manager != nullptr && manager->name != nullptr &&
                manager->version == version &&
                std::strcmp(manager->name, name) == 0) {
                return manager->currentInterface;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

bool ResolvePlayerBodyManager(
    unsigned char* gameClientBase,
    void*& manager) noexcept {
    manager = nullptr;
    if (gameClientBase == nullptr) {
        return false;
    }
    auto* const functionAddress =
        gameClientBase + kPlayerBodyManagerInstanceRva;
    bool signatureMatches = false;
    std::uint32_t encodedManagerAddress = 0U;
    __try {
        // Relocated absolute operands are intentionally skipped.
        signatureMatches =
            functionAddress[0] == 0x8AU &&
            functionAddress[1] == 0x0DU &&
            functionAddress[6] == 0xB8U &&
            functionAddress[7] == 0x01U &&
            functionAddress[8] == 0x00U &&
            functionAddress[9] == 0x00U &&
            functionAddress[10] == 0x00U &&
            functionAddress[11] == 0x84U &&
            functionAddress[12] == 0xC8U &&
            functionAddress[13] == 0x75U &&
            functionAddress[14] == 0x25U &&
            functionAddress[0x34] == 0xB8U &&
            functionAddress[0x39] == 0xC3U;
        std::memcpy(
            &encodedManagerAddress,
            functionAddress + 0x35U,
            sizeof(encodedManagerAddress));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        signatureMatches = false;
    }
    if (!signatureMatches || !IsExecutableAddress(functionAddress)) {
        return false;
    }
    manager = gameClientBase + kPlayerBodyManagerRva;
    return encodedManagerAddress ==
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(manager));
}

bool ReadPlayerBody(void*& playerBody) noexcept {
    playerBody = nullptr;
    __try {
        std::memcpy(
            &playerBody,
            static_cast<unsigned char*>(g_playerBodyManager) +
                kPlayerBodyObjectOffset,
            sizeof(playerBody));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        playerBody = nullptr;
        return false;
    }
    return true;
}

bool ModelTransformIsFinite(
    const ModelTransformAbi& transform) noexcept {
    for (float component : transform.position) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    for (float component : transform.rotation) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    return std::isfinite(transform.scale);
}

char LowerAscii(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool ContainsAsciiCaseInsensitive(
    const char* text,
    const char* pattern) noexcept {
    if (text == nullptr || pattern == nullptr || pattern[0] == '\0') {
        return false;
    }
    for (const char* start = text; *start != '\0'; ++start) {
        const char* left = start;
        const char* right = pattern;
        while (*left != '\0' && *right != '\0' &&
               LowerAscii(*left) == LowerAscii(*right)) {
            ++left;
            ++right;
        }
        if (*right == '\0') {
            return true;
        }
    }
    return false;
}

bool IsArmRelevantNodeName(const char* name) noexcept {
    constexpr const char* kTerms[] = {
        "arm", "hand", "wrist", "elbow", "shoulder", "clav",
        "fore", "bicep"};
    for (const char* term : kTerms) {
        if (ContainsAsciiCaseInsensitive(name, term)) {
            return true;
        }
    }
    return false;
}

void LogTransform(
    const char* event,
    const char* kind,
    const char* name,
    ModelHandle handle,
    const ModelTransformAbi& transform) noexcept {
    char detail[640]{};
    std::snprintf(
        detail, sizeof(detail),
        "kind=%s name=%s handle=%lu "
        "world_position=(%.4f,%.4f,%.4f) "
        "world_rotation=(%.6f,%.6f,%.6f,%.6f) scale=%.4f",
        kind, name, static_cast<unsigned long>(handle),
        transform.position[0], transform.position[1],
        transform.position[2], transform.rotation[0],
        transform.rotation[1], transform.rotation[2],
        transform.rotation[3], transform.scale);
    g_log(event, detail);
}

void ResetWeaponModelObservation() noexcept {
    g_weaponModelObject = nullptr;
    g_weaponIndex = -1;
    g_weaponSourceGeneration = 0U;
    g_weaponNodeCount = 0U;
    g_weaponBaselineStartTick = 0U;
    g_weaponLastSampleTick = 0U;
    g_weaponSampleSequence = 0U;
    g_weaponMotionLogCount = 0U;
    g_weaponBaselineReady = false;
    for (WeaponNodeObservation& node : g_weaponNodes) {
        node = {};
    }
}

float QuaternionAngularDifferenceDegrees(
    const float (&left)[4],
    const float (&right)[4]) noexcept {
    float leftLengthSquared = 0.0F;
    float rightLengthSquared = 0.0F;
    float dot = 0.0F;
    for (std::size_t component = 0U; component < 4U; ++component) {
        if (!std::isfinite(left[component]) ||
            !std::isfinite(right[component])) {
            return -1.0F;
        }
        leftLengthSquared += left[component] * left[component];
        rightLengthSquared += right[component] * right[component];
        dot += left[component] * right[component];
    }
    if (leftLengthSquared < 0.25F || rightLengthSquared < 0.25F) {
        return -1.0F;
    }
    dot = std::clamp(
        std::fabs(dot) /
            std::sqrt(leftLengthSquared * rightLengthSquared),
        0.0F, 1.0F);
    constexpr float kRadiansToDegrees = 57.29577951308232F;
    return 2.0F * std::acos(dot) * kRadiansToDegrees;
}

bool ReadWeaponNodeLocalTransform(
    void* modelObject,
    ModelHandle node,
    ModelTransformAbi& transform) noexcept {
    transform = {};
    g_readPhase = "weapon_get_node_transform_local";
    const ModelResult result = g_getNodeTransform(
        g_model, modelObject, node, transform, false);
    return result == kModelOk && ModelTransformIsFinite(transform);
}

bool EnumerateWeaponModelUnchecked(
    void* modelObject,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration) noexcept {
    std::uint32_t nodeCount = 0U;
    g_readPhase = "weapon_get_num_nodes";
    const ModelResult nodeCountResult =
        g_getNumNodes(g_model, modelObject, nodeCount);
    if (nodeCountResult != kModelOk || nodeCount == 0U ||
        nodeCount > kWeaponNodeCapacity) {
        char detail[320]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld model_object=%p source_generation=%llu "
            "get_num_nodes_result=%lu node_count=%lu read_only=1",
            static_cast<long>(weaponIndex), modelObject,
            static_cast<unsigned long long>(sourceGeneration),
            static_cast<unsigned long>(nodeCountResult),
            static_cast<unsigned long>(nodeCount));
        g_log("weapon_model_discovery_not_ready", detail);
        return false;
    }

    ResetWeaponModelObservation();
    g_weaponModelObject = modelObject;
    g_weaponIndex = weaponIndex;
    g_weaponSourceGeneration = sourceGeneration;
    g_weaponBaselineStartTick = GetTickCount64();

    char summary[384]{};
    std::snprintf(
        summary, sizeof(summary),
        "weapon_index=%ld model_object=%p source_generation=%llu "
        "nodes=%lu transform_basis=model_local settle_ms=%llu "
        "read_only=1 node_controls_added=0 engine_writes=0",
        static_cast<long>(weaponIndex), modelObject,
        static_cast<unsigned long long>(sourceGeneration),
        static_cast<unsigned long>(nodeCount),
        static_cast<unsigned long long>(
            kWeaponBaselineSettleMilliseconds));
    g_log("weapon_model_discovery_model", summary);

    ModelHandle currentNode = kInvalidModelHandle;
    for (std::uint32_t index = 0U; index < nodeCount; ++index) {
        ModelHandle nextNode = kInvalidModelHandle;
        g_readPhase = "weapon_get_next_node";
        const ModelResult nextResult = g_getNextNode(
            g_model, modelObject, currentNode, nextNode);
        if (nextResult != kModelOk ||
            nextNode == kInvalidModelHandle || nextNode == currentNode) {
            break;
        }
        WeaponNodeObservation& observation =
            g_weaponNodes[g_weaponNodeCount];
        observation.handle = nextNode;
        g_readPhase = "weapon_get_node_name";
        const ModelResult nameResult = g_getNodeName(
            g_model, modelObject, nextNode, observation.name,
            static_cast<std::uint32_t>(sizeof(observation.name)));
        if (nameResult != kModelOk || observation.name[0] == '\0') {
            std::snprintf(
                observation.name, sizeof(observation.name),
                "<unavailable:%lu>",
                static_cast<unsigned long>(nameResult));
        }
        g_readPhase = "weapon_get_parent";
        const ModelResult parentResult = g_getParent(
            g_model, modelObject, nextNode, observation.parent);
        if (parentResult != kModelOk) {
            observation.parent = kInvalidModelHandle;
        }
        observation.baselineValid = ReadWeaponNodeLocalTransform(
            modelObject, nextNode, observation.baselineLocal);

        char detail[768]{};
        if (observation.baselineValid) {
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld model_object=%p source_generation=%llu "
                "index=%lu handle=%lu parent=%lu name=%s "
                "model_local_position=(%.4f,%.4f,%.4f) "
                "model_local_rotation=(%.6f,%.6f,%.6f,%.6f) "
                "scale=%.4f baseline_state=settling read_only=1",
                static_cast<long>(weaponIndex), modelObject,
                static_cast<unsigned long long>(sourceGeneration),
                static_cast<unsigned long>(g_weaponNodeCount),
                static_cast<unsigned long>(nextNode),
                static_cast<unsigned long>(observation.parent),
                observation.name,
                observation.baselineLocal.position[0],
                observation.baselineLocal.position[1],
                observation.baselineLocal.position[2],
                observation.baselineLocal.rotation[0],
                observation.baselineLocal.rotation[1],
                observation.baselineLocal.rotation[2],
                observation.baselineLocal.rotation[3],
                observation.baselineLocal.scale);
        } else {
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld model_object=%p source_generation=%llu "
                "index=%lu handle=%lu parent=%lu name=%s "
                "model_local_transform=unavailable "
                "baseline_state=settling read_only=1",
                static_cast<long>(weaponIndex), modelObject,
                static_cast<unsigned long long>(sourceGeneration),
                static_cast<unsigned long>(g_weaponNodeCount),
                static_cast<unsigned long>(nextNode),
                static_cast<unsigned long>(observation.parent),
                observation.name);
        }
        g_log("weapon_model_discovery_node", detail);
        ++g_weaponNodeCount;
        currentNode = nextNode;
    }

    char complete[384]{};
    std::snprintf(
        complete, sizeof(complete),
        "weapon_index=%ld model_object=%p source_generation=%llu "
        "nodes_reported=%lu expected_nodes=%lu baseline_state=settling "
        "read_only=1 engine_writes=0",
        static_cast<long>(weaponIndex), modelObject,
        static_cast<unsigned long long>(sourceGeneration),
        static_cast<unsigned long>(g_weaponNodeCount),
        static_cast<unsigned long>(nodeCount));
    g_readPhase = "weapon_enumeration_complete";
    g_log("weapon_model_discovery_complete", complete);
    const bool enumerationComplete = g_weaponNodeCount == nodeCount;
    if (!enumerationComplete) {
        ResetWeaponModelObservation();
    }
    return enumerationComplete;
}

bool EnumerateWeaponModel(
    void* modelObject,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration) noexcept {
    __try {
        return EnumerateWeaponModelUnchecked(
            modelObject, weaponIndex, sourceGeneration);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log != nullptr) {
            char detail[320]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld model_object=%p source_generation=%llu "
                "phase=%s exception=0x%08lX read_only=1",
                static_cast<long>(weaponIndex), modelObject,
                static_cast<unsigned long long>(sourceGeneration),
                g_readPhase,
                static_cast<unsigned long>(GetExceptionCode()));
            g_log("weapon_model_discovery_read_failed", detail);
        }
        ResetWeaponModelObservation();
        return false;
    }
}

void SampleWeaponModelMotionUnchecked() noexcept {
    const ULONGLONG now = GetTickCount64();
    // HookRenderCamera may be reached more than once in one presented frame.
    // One sample per millisecond prevents duplicate per-eye/model queries while
    // retaining enough temporal resolution for a fast firearm cycle.
    if (now == g_weaponLastSampleTick) {
        return;
    }
    g_weaponLastSampleTick = now;
    ++g_weaponSampleSequence;
    const bool settling = now - g_weaponBaselineStartTick <
        kWeaponBaselineSettleMilliseconds;

    for (std::size_t index = 0U; index < g_weaponNodeCount; ++index) {
        WeaponNodeObservation& observation = g_weaponNodes[index];
        ModelTransformAbi current{};
        if (!ReadWeaponNodeLocalTransform(
                g_weaponModelObject, observation.handle, current)) {
            continue;
        }
        if (settling || !observation.baselineValid) {
            observation.baselineLocal = current;
            observation.baselineValid = true;
            observation.peakTranslationUnits = 0.0F;
            observation.peakRotationDegrees = 0.0F;
            continue;
        }

        const float delta[3]{
            current.position[0] - observation.baselineLocal.position[0],
            current.position[1] - observation.baselineLocal.position[1],
            current.position[2] - observation.baselineLocal.position[2]};
        const float translation = std::sqrt(
            delta[0] * delta[0] + delta[1] * delta[1] +
            delta[2] * delta[2]);
        const float rotation = QuaternionAngularDifferenceDegrees(
            current.rotation, observation.baselineLocal.rotation);
        if (!std::isfinite(translation) || rotation < 0.0F) {
            continue;
        }
        const bool translationPeak =
            translation >= kWeaponTranslationPeakStepUnits &&
            translation >= observation.peakTranslationUnits +
                kWeaponTranslationPeakStepUnits;
        const bool rotationPeak =
            rotation >= kWeaponRotationPeakStepDegrees &&
            rotation >= observation.peakRotationDegrees +
                kWeaponRotationPeakStepDegrees;
        if (!translationPeak && !rotationPeak) {
            continue;
        }
        observation.peakTranslationUnits = std::max(
            observation.peakTranslationUnits, translation);
        observation.peakRotationDegrees = std::max(
            observation.peakRotationDegrees, rotation);
        if (g_weaponMotionLogCount >= kWeaponMotionLogCapacity) {
            continue;
        }
        ++g_weaponMotionLogCount;
        const float inverseTranslation = translation > 0.00001F
            ? 1.0F / translation : 0.0F;
        char detail[1152]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld model_object=%p source_generation=%llu "
            "sample=%llu index=%lu handle=%lu parent=%lu name=%s "
            "baseline_model_local_position=(%.4f,%.4f,%.4f) "
            "current_model_local_position=(%.4f,%.4f,%.4f) "
            "translation_delta=(%.4f,%.4f,%.4f) "
            "candidate_axis=(%.6f,%.6f,%.6f) "
            "translation_peak_units=%.4f rotation_peak_degrees=%.4f "
            "current_model_local_rotation=(%.6f,%.6f,%.6f,%.6f) "
            "transform_basis=model_local observation=new_peak "
            "read_only=1 node_controls_added=0 engine_writes=0",
            static_cast<long>(g_weaponIndex), g_weaponModelObject,
            static_cast<unsigned long long>(g_weaponSourceGeneration),
            static_cast<unsigned long long>(g_weaponSampleSequence),
            static_cast<unsigned long>(index),
            static_cast<unsigned long>(observation.handle),
            static_cast<unsigned long>(observation.parent),
            observation.name,
            observation.baselineLocal.position[0],
            observation.baselineLocal.position[1],
            observation.baselineLocal.position[2],
            current.position[0], current.position[1], current.position[2],
            delta[0], delta[1], delta[2],
            delta[0] * inverseTranslation,
            delta[1] * inverseTranslation,
            delta[2] * inverseTranslation,
            observation.peakTranslationUnits,
            observation.peakRotationDegrees,
            current.rotation[0], current.rotation[1],
            current.rotation[2], current.rotation[3]);
        g_log("weapon_model_discovery_motion", detail);
    }

    if (!settling && !g_weaponBaselineReady) {
        g_weaponBaselineReady = true;
        char detail[448]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld model_object=%p source_generation=%llu "
            "nodes=%lu settle_ms=%llu transform_basis=model_local "
            "instruction=fire_once_then_reload_once read_only=1 "
            "engine_writes=0",
            static_cast<long>(g_weaponIndex), g_weaponModelObject,
            static_cast<unsigned long long>(g_weaponSourceGeneration),
            static_cast<unsigned long>(g_weaponNodeCount),
            static_cast<unsigned long long>(
                kWeaponBaselineSettleMilliseconds));
        g_log("weapon_model_discovery_baseline_ready", detail);
    }
}

void SampleWeaponModelMotion() noexcept {
    __try {
        SampleWeaponModelMotionUnchecked();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log != nullptr) {
            char detail[320]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld model_object=%p source_generation=%llu "
                "phase=%s exception=0x%08lX read_only=1",
                static_cast<long>(g_weaponIndex), g_weaponModelObject,
                static_cast<unsigned long long>(g_weaponSourceGeneration),
                g_readPhase,
                static_cast<unsigned long>(GetExceptionCode()));
            g_log("weapon_model_discovery_read_failed", detail);
        }
        ResetWeaponModelObservation();
    }
}

bool DumpPlayerBodyUnchecked(void* playerBody) noexcept {
    std::uint32_t nodeCount = 0U;
    g_readPhase = "get_num_nodes";
    const ModelResult nodeCountResult =
        g_getNumNodes(g_model, playerBody, nodeCount);
    if (nodeCountResult != kModelOk || nodeCount == 0U ||
        nodeCount > 512U) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "player_body=%p get_num_nodes_result=%lu node_count=%lu",
            playerBody, static_cast<unsigned long>(nodeCountResult),
            static_cast<unsigned long>(nodeCount));
        g_log("arm_ik_discovery_body_not_ready", detail);
        return false;
    }

    char summary[512]{};
    std::snprintf(
        summary, sizeof(summary),
        "player_body=%p nodes=%lu "
        "model_interface=%p read_only=1 node_controls_added=0",
        playerBody, static_cast<unsigned long>(nodeCount),
        g_model);
    g_log("arm_ik_discovery_player_body", summary);

    ModelHandle currentNode = kInvalidModelHandle;
    std::uint32_t loggedNodes = 0U;
    std::uint32_t loggedNodeTransforms = 0U;
    for (; loggedNodes < nodeCount && loggedNodes < 256U;
         ++loggedNodes) {
        ModelHandle nextNode = kInvalidModelHandle;
        g_readPhase = "get_next_node";
        const ModelResult nextResult = g_getNextNode(
            g_model, playerBody, currentNode, nextNode);
        if (nextResult != kModelOk ||
            nextNode == kInvalidModelHandle ||
            nextNode == currentNode) {
            break;
        }
        char nodeName[128]{};
        g_readPhase = "get_node_name";
        const ModelResult nameResult = g_getNodeName(
            g_model, playerBody, nextNode, nodeName,
            static_cast<std::uint32_t>(sizeof(nodeName)));
        if (nameResult != kModelOk || nodeName[0] == '\0') {
            std::snprintf(
                nodeName, sizeof(nodeName), "<unavailable:%lu>",
                static_cast<unsigned long>(nameResult));
        }
        ModelHandle parent = kInvalidModelHandle;
        g_readPhase = "get_parent";
        const ModelResult parentResult = g_getParent(
            g_model, playerBody, nextNode, parent);
        const bool sampleTransform = IsArmRelevantNodeName(nodeName);
        ModelTransformAbi transform{};
        g_readPhase = sampleTransform
            ? "get_node_transform" : "format_node";
        const ModelResult transformResult = sampleTransform
            ? g_getNodeTransform(
                  g_model, playerBody, nextNode, transform, true)
            : 0xFFFFFFFFU;
        char detail[640]{};
        if (sampleTransform && transformResult == kModelOk &&
            ModelTransformIsFinite(transform)) {
            std::snprintf(
                detail, sizeof(detail),
                "index=%lu handle=%lu parent=%lu parent_result=%lu "
                "name=%s world_position=(%.4f,%.4f,%.4f) "
                "world_rotation=(%.6f,%.6f,%.6f,%.6f) scale=%.4f",
                static_cast<unsigned long>(loggedNodes),
                static_cast<unsigned long>(nextNode),
                static_cast<unsigned long>(parent),
                static_cast<unsigned long>(parentResult), nodeName,
                transform.position[0], transform.position[1],
                transform.position[2], transform.rotation[0],
                transform.rotation[1], transform.rotation[2],
                transform.rotation[3], transform.scale);
            ++loggedNodeTransforms;
        } else if (!sampleTransform) {
            std::snprintf(
                detail, sizeof(detail),
                "index=%lu handle=%lu parent=%lu parent_result=%lu "
                "name=%s transform=not_sampled",
                static_cast<unsigned long>(loggedNodes),
                static_cast<unsigned long>(nextNode),
                static_cast<unsigned long>(parent),
                static_cast<unsigned long>(parentResult), nodeName);
        } else {
            std::snprintf(
                detail, sizeof(detail),
                "index=%lu handle=%lu parent=%lu parent_result=%lu "
                "name=%s transform_result=%lu",
                static_cast<unsigned long>(loggedNodes),
                static_cast<unsigned long>(nextNode),
                static_cast<unsigned long>(parent),
                static_cast<unsigned long>(parentResult), nodeName,
                static_cast<unsigned long>(transformResult));
        }
        g_log("arm_ik_discovery_node", detail);
        currentNode = nextNode;
    }

    constexpr const char* kSocketCandidates[] = {
        "RightHand", "LeftHand", "RightHandWeapon", "LeftHandWeapon",
        "Weapon", "Muzzle", "Flash", "Breach", "CriticalHitSocket",
        "Upper_Torso", "Pelvis"};
    std::uint32_t loggedSockets = 0U;
    for (const char* socketName : kSocketCandidates) {
        ModelHandle socket = kInvalidModelHandle;
        g_readPhase = "get_socket";
        if (g_getSocket(
                g_model, playerBody, socketName, socket) != kModelOk ||
            socket == kInvalidModelHandle) {
            continue;
        }
        ModelTransformAbi transform{};
        g_readPhase = "get_socket_transform";
        const ModelResult transformResult = g_getSocketTransform(
            g_model, playerBody, socket, transform, true);
        if (transformResult == kModelOk &&
            ModelTransformIsFinite(transform)) {
            LogTransform(
                "arm_ik_discovery_socket", "socket", socketName,
                socket, transform);
        } else {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "kind=socket name=%s handle=%lu transform_result=%lu",
                socketName, static_cast<unsigned long>(socket),
                static_cast<unsigned long>(transformResult));
            g_log("arm_ik_discovery_socket", detail);
        }
        ++loggedSockets;
    }

    char complete[320]{};
    std::snprintf(
        complete, sizeof(complete),
        "player_body=%p nodes_logged=%lu arm_transforms_logged=%lu "
        "known_sockets_resolved=%lu",
        playerBody, static_cast<unsigned long>(loggedNodes),
        static_cast<unsigned long>(loggedNodeTransforms),
        static_cast<unsigned long>(loggedSockets));
    g_readPhase = "complete";
    g_log("arm_ik_discovery_complete", complete);
    return loggedNodes > 0U;
}

bool DumpPlayerBody(void* playerBody) noexcept {
    __try {
        return DumpPlayerBodyUnchecked(playerBody);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log != nullptr) {
            char detail[160]{};
            std::snprintf(
                detail, sizeof(detail),
                "player_body=%p phase=%s exception=0x%08lX",
                playerBody, g_readPhase,
                static_cast<unsigned long>(GetExceptionCode()));
            g_log("arm_ik_discovery_read_failed", detail);
        }
        return false;
    }
}

} // namespace

bool InstallArmIkDiscovery(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkDiscoveryLogFunction log) noexcept {
    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        log == nullptr) {
        return false;
    }
    if (InterlockedCompareExchange(&g_enabled, 0, 0) != 0) {
        return true;
    }

    auto* const gameClientBase =
        static_cast<unsigned char*>(gameClientModule);
    void* playerBodyManager = nullptr;
    if (!ResolvePlayerBodyManager(
            gameClientBase, playerBodyManager)) {
        log(
            "arm_ik_discovery_rejected",
            "reason=player_body_manager_signature_or_instance_mismatch");
        return false;
    }

    void* const model = FindCurrentInterface(
        masterDatabase, "ILTModelClient.Default", 0);
    if (model == nullptr) {
        log(
            "arm_ik_discovery_rejected",
            "reason=ILTModelClient_Default_v0_missing");
        return false;
    }

    void* retailModelGlobal = nullptr;
    void** vtable = nullptr;
    __try {
        std::memcpy(
            &retailModelGlobal,
            gameClientBase + kModelClientGlobalRva,
            sizeof(retailModelGlobal));
        vtable = *static_cast<void***>(model);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        retailModelGlobal = nullptr;
        vtable = nullptr;
    }
    if (vtable == nullptr ||
        (retailModelGlobal != nullptr && retailModelGlobal != model)) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "reason=model_interface_global_mismatch "
            "registered=%p retail_global=%p",
            model, retailModelGlobal);
        log("arm_ik_discovery_rejected", detail);
        return false;
    }

    const std::size_t requiredSlots[] = {
        kGetSocketSlot, kGetSocketTransformSlot, kGetNodeNameSlot,
        kGetNodeTransformSlot, kGetNextNodeSlot, kGetParentSlot,
        kGetNumNodesSlot};
    for (const std::size_t slot : requiredSlots) {
        void* target = nullptr;
        __try {
            target = vtable[slot];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            target = nullptr;
        }
        if (!IsExecutableAddress(target)) {
            char detail[192]{};
            std::snprintf(
                detail, sizeof(detail),
                "reason=model_vtable_slot_invalid slot=%lu target=%p",
                static_cast<unsigned long>(slot), target);
            log("arm_ik_discovery_rejected", detail);
            return false;
        }
    }

    g_log = log;
    g_playerBodyManager = playerBodyManager;
    g_model = model;
    g_modelClientGlobalAddress =
        gameClientBase + kModelClientGlobalRva;
    g_getSocket = reinterpret_cast<GetSocketFunction>(
        vtable[kGetSocketSlot]);
    g_getSocketTransform = reinterpret_cast<GetSocketTransformFunction>(
        vtable[kGetSocketTransformSlot]);
    g_getNodeName = reinterpret_cast<GetNodeNameFunction>(
        vtable[kGetNodeNameSlot]);
    g_getNodeTransform = reinterpret_cast<GetNodeTransformFunction>(
        vtable[kGetNodeTransformSlot]);
    g_getNextNode = reinterpret_cast<GetNextNodeFunction>(
        vtable[kGetNextNodeSlot]);
    g_getParent = reinterpret_cast<GetParentFunction>(
        vtable[kGetParentSlot]);
    g_getNumNodes = reinterpret_cast<GetNumNodesFunction>(
        vtable[kGetNumNodesSlot]);
    g_readPhase = "idle";
    g_loggedPlayerBody = nullptr;
    g_failedPlayerBody = nullptr;
    InterlockedExchange(&g_sampleCount, 0);
    InterlockedExchange(&g_waitingLogged, 0);
    InterlockedExchange(
        &g_modelGlobalValidated,
        retailModelGlobal == model ? 1 : 0);
    InterlockedExchange(&g_modelGlobalWaitingLogged, 0);
    InterlockedExchange(&g_enabled, 1);

    char detail[384]{};
    std::snprintf(
        detail, sizeof(detail),
        "model_interface=%p player_body_manager=%p "
        "manager_rva=0x%08lX object_offset=0x%02lX "
        "model_global=%p model_global_ready=%d "
        "mode=read_only node_controls_added=0",
        model, playerBodyManager,
        static_cast<unsigned long>(kPlayerBodyManagerRva),
        static_cast<unsigned long>(kPlayerBodyObjectOffset),
        retailModelGlobal,
        retailModelGlobal == model ? 1 : 0);
    log("arm_ik_discovery_armed", detail);
    return true;
}

bool InstallWeaponModelDiscovery(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkDiscoveryLogFunction log) noexcept {
    if (!InstallArmIkDiscovery(
            masterDatabase, gameClientModule, log)) {
        if (log != nullptr) {
            log(
                "weapon_model_discovery_rejected",
                "reason=verified_model_interface_install_failed");
        }
        return false;
    }
    ResetWeaponModelObservation();
    InterlockedExchange(&g_weaponDiscoveryEnabled, 1);
    if (log != nullptr) {
        log(
            "weapon_model_discovery_armed",
            "mode=read_only source=lifetime_validated_equipped_model "
            "transform_basis=model_local node_names_assumed=0 "
            "object_offsets_assumed=0 node_controls_added=0 engine_writes=0");
    }
    return true;
}

void SampleArmIkDiscovery() noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0) {
        return;
    }
    const LONG sample = InterlockedIncrement(&g_sampleCount);
    if (sample != 1 && sample % 60 != 0) {
        return;
    }

    if (InterlockedCompareExchange(
            &g_modelGlobalValidated, 0, 0) == 0) {
        void* retailModelGlobal = nullptr;
        bool readSucceeded = false;
        __try {
            std::memcpy(
                &retailModelGlobal, g_modelClientGlobalAddress,
                sizeof(retailModelGlobal));
            readSucceeded = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            readSucceeded = false;
        }
        if (!readSucceeded ||
            (retailModelGlobal != nullptr &&
             retailModelGlobal != g_model)) {
            if (g_log != nullptr) {
                char detail[192]{};
                std::snprintf(
                    detail, sizeof(detail),
                    "reason=model_interface_global_mismatch "
                    "registered=%p retail_global=%p read_succeeded=%d",
                    g_model, retailModelGlobal,
                    readSucceeded ? 1 : 0);
                g_log("arm_ik_discovery_rejected", detail);
            }
            InterlockedExchange(&g_enabled, 0);
            return;
        }
        if (retailModelGlobal == nullptr) {
            if (InterlockedCompareExchange(
                    &g_modelGlobalWaitingLogged, 1, 0) == 0 &&
                g_log != nullptr) {
                g_log(
                    "arm_ik_discovery_waiting",
                    "reason=model_interface_global_not_initialized");
            }
            return;
        }
        InterlockedExchange(&g_modelGlobalValidated, 1);
        InterlockedExchange(&g_modelGlobalWaitingLogged, 0);
        if (g_log != nullptr) {
            char detail[128]{};
            std::snprintf(
                detail, sizeof(detail),
                "registered=%p retail_global=%p",
                g_model, retailModelGlobal);
            g_log("arm_ik_discovery_model_interface_validated", detail);
        }
    }

    void* playerBody = nullptr;
    if (!ReadPlayerBody(playerBody)) {
        if (InterlockedCompareExchange(&g_waitingLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "arm_ik_discovery_waiting",
                "reason=player_body_manager_read_failed");
        }
        return;
    }
    if (playerBody == nullptr) {
        if (g_loggedPlayerBody != nullptr || g_failedPlayerBody != nullptr) {
            g_loggedPlayerBody = nullptr;
            g_failedPlayerBody = nullptr;
            if (g_log != nullptr) {
                g_log(
                    "arm_ik_discovery_lifecycle_reset",
                    "reason=player_body_released");
            }
        }
        if (InterlockedCompareExchange(&g_waitingLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "arm_ik_discovery_waiting",
                "reason=player_body_not_created");
        }
        return;
    }

    InterlockedExchange(&g_waitingLogged, 0);
    if (playerBody == g_loggedPlayerBody) {
        return;
    }
    // Retry a body that was observed before its model finished loading only
    // at a bounded, infrequent interval.
    if (playerBody == g_failedPlayerBody && sample % 600 != 0) {
        return;
    }
    if (DumpPlayerBody(playerBody)) {
        g_loggedPlayerBody = playerBody;
        g_failedPlayerBody = nullptr;
    } else {
        g_failedPlayerBody = playerBody;
    }
}

void SampleWeaponModelDiscovery(
    void* modelObject,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration) noexcept {
    if (InterlockedCompareExchange(
            &g_weaponDiscoveryEnabled, 0, 0) == 0) {
        return;
    }
    if (modelObject == nullptr || weaponIndex < 0 ||
        sourceGeneration == 0U ||
        InterlockedCompareExchange(
            &g_modelGlobalValidated, 0, 0) == 0) {
        if (g_weaponModelObject != nullptr) {
            if (g_log != nullptr) {
                char detail[320]{};
                std::snprintf(
                    detail, sizeof(detail),
                    "weapon_index=%ld model_object=%p source_generation=%llu "
                    "reason=equipped_model_unavailable_or_changed "
                    "read_only=1 engine_writes=0",
                    static_cast<long>(g_weaponIndex), g_weaponModelObject,
                    static_cast<unsigned long long>(
                        g_weaponSourceGeneration));
                g_log("weapon_model_discovery_lifecycle_reset", detail);
            }
            ResetWeaponModelObservation();
        }
        return;
    }
    const bool sourceChanged =
        modelObject != g_weaponModelObject ||
        weaponIndex != g_weaponIndex ||
        sourceGeneration != g_weaponSourceGeneration;
    if (sourceChanged &&
        !EnumerateWeaponModel(
            modelObject, weaponIndex, sourceGeneration)) {
        return;
    }
    if (modelObject == g_weaponModelObject &&
        weaponIndex == g_weaponIndex &&
        sourceGeneration == g_weaponSourceGeneration) {
        SampleWeaponModelMotion();
    }
}

} // namespace condemnedvr
