#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "arm_ik_integration.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "arm_ik.h"
#include "condemned_arm_ik_lifecycle.h"
#include "condemned_physical_melee.h"
#include "head_tracking_math.h"
#include "weapon_settings_store.h"

namespace condemnedvr {
namespace {

static_assert(sizeof(void*) == 4, "Arm IK integration is x86-only.");

using ModelHandle = std::uint32_t;
using ModelResult = std::uint32_t;

constexpr ModelResult kModelOk = 0U;
constexpr ModelHandle kInvalidModelHandle = 0xFFFFFFFFU;
constexpr ULONGLONG kTargetFreshnessMilliseconds = 250U;
constexpr ULONGLONG kCallbackHeartbeatFreshnessMilliseconds = 1000U;
constexpr std::uint64_t kCallbackHeartbeatLogInterval = 600U;

constexpr std::uintptr_t kPlayerBodyManagerInstanceRva = 0x00002EA0U;
constexpr std::uintptr_t kPlayerBodyManagerRva = 0x00167A50U;
constexpr std::size_t kPlayerBodyObjectOffset = 0x10U;
constexpr std::uintptr_t kModelClientGlobalRva = 0x00172EC0U;

// Condemned.exe 1.0.314.0 reverses the two Add/RemoveNodeControlFn overloads
// relative to the available public header. These exact targets were verified
// from the Retail vtable and are required before any callback is registered.
constexpr std::size_t kGetSocketSlot = 1U;
constexpr std::size_t kGetSocketTransformSlot = 2U;
constexpr std::size_t kGetNodeSlot = 11U;
constexpr std::size_t kGetNodeTransformSlot = 13U;
constexpr std::size_t kAddNodeControlSpecificSlot = 22U;
constexpr std::size_t kRemoveNodeControlSpecificSlot = 24U;

constexpr std::uintptr_t kGetSocketExecutableRva = 0x000378E0U;
constexpr std::uintptr_t kGetSocketTransformExecutableRva = 0x000381D0U;
constexpr std::uintptr_t kGetNodeExecutableRva = 0x000379E0U;
constexpr std::uintptr_t kGetNodeTransformExecutableRva = 0x00038040U;
constexpr std::uintptr_t kAddNodeControlSpecificExecutableRva = 0x00037720U;
constexpr std::uintptr_t kRemoveNodeControlSpecificExecutableRva =
    0x000377F0U;

struct ModelTransformAbi {
    float position[3];
    float rotation[4];
    float scale;
};

struct RigidTransformAbi {
    float position[3];
    float rotation[4];
};

struct NodeControlDataAbi {
    const ModelTransformAbi* modelTransform;
    const RigidTransformAbi* parentTransform;
    const RigidTransformAbi* fromParentTransform;
    RigidTransformAbi* nodeTransform;
    void* model;
    ModelHandle node;
};

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

static_assert(sizeof(ModelTransformAbi) == 32U);
static_assert(sizeof(RigidTransformAbi) == 28U);
static_assert(sizeof(NodeControlDataAbi) == 24U);
static_assert(sizeof(InterfaceArrayAbi) == 12U);
static_assert(sizeof(InterfaceDatabaseAbi) == 12U);
static_assert(sizeof(InterfaceNameManagerAbi) == 24U);

using NodeControlFunction = void(__cdecl*)(
    const NodeControlDataAbi&, void*);
using GetSocketFunction = ModelResult(__thiscall*)(
    void*, void*, const char*, ModelHandle&);
using GetSocketTransformFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelTransformAbi&, bool);
using GetNodeFunction = ModelResult(__thiscall*)(
    void*, void*, const char*, ModelHandle&);
using GetNodeTransformFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelTransformAbi&, bool);
using AddNodeControlSpecificFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, NodeControlFunction, void*);
using RemoveNodeControlSpecificFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, NodeControlFunction, void*);

struct RightHandControlState {
    void* playerBody{nullptr};
    ModelHandle upperArmNode{kInvalidModelHandle};
    ModelHandle forearmNode{kInvalidModelHandle};
    ModelHandle handNode{kInvalidModelHandle};
    ModelHandle handSocket{kInvalidModelHandle};
    fearvr::TrackingVector forearmOffsetFromUpperArm{};
    fearvr::TrackingVector handOffsetFromForearm{};
    PhysicalMeleeRigidTransform socketFromNode{};
    float upperLength{0.0F};
    float lowerLength{0.0F};
    fearvr::ArmIkVector previousBendDirection{};
    fearvr::TrackingVector solvedShoulderWorld{};
    fearvr::TrackingVector solvedElbowWorld{};
    PhysicalMeleeRigidTransform lastModelWorld{};
    std::uint64_t solvedSampleId{0U};
    std::uint64_t callbackHeartbeat{0U};
    ULONGLONG lastCallbackTick{0U};
    std::uint32_t lifecycleGeneration{1U};
    ULONGLONG solvedTick{0U};
    bool lastModelWorldValid{false};
    bool previousBendValid{false};
    bool targetClamped{false};
    bool fullArm{false};
    bool installed{false};
};

struct TrackedRightHandTarget {
    PhysicalMeleeRigidTransform socketWorld{};
    PhysicalMeleeRigidTransform socketFromBody{};
    void* referencePlayerBody{nullptr};
    std::uint64_t sampleId{0};
    std::uint64_t timestampNs{0};
    ULONGLONG publishedTick{0};
    bool bodyRelativeValid{false};
    bool valid{false};
};

ArmIkIntegrationLogFunction g_log = nullptr;
void* g_playerBodyManager = nullptr;
void* g_model = nullptr;
const unsigned char* g_modelClientGlobalAddress = nullptr;
GetSocketFunction g_getSocket = nullptr;
GetSocketTransformFunction g_getSocketTransform = nullptr;
GetNodeFunction g_getNode = nullptr;
GetNodeTransformFunction g_getNodeTransform = nullptr;
AddNodeControlSpecificFunction g_addNodeControlSpecific = nullptr;
RemoveNodeControlSpecificFunction g_removeNodeControlSpecific = nullptr;
SRWLOCK g_controlLock = SRWLOCK_INIT;
RightHandControlState g_control{};
SRWLOCK g_targetLock = SRWLOCK_INIT;
TrackedRightHandTarget g_target{};
SRWLOCK g_leftControlLock = SRWLOCK_INIT;
RightHandControlState g_leftControl{};
SRWLOCK g_leftTargetLock = SRWLOCK_INIT;
TrackedRightHandTarget g_leftTarget{};
SRWLOCK g_lifecycleLock = SRWLOCK_INIT;
ArmIkLifecycleState g_lifecycleState{};
volatile LONG g_enabled = 0;
volatile LONG g_fullArmMode = 0;
volatile LONG g_modelGlobalValidated = 0;
volatile LONG g_modelGlobalWaitingLogged = 0;
volatile LONG g_sampleCount = 0;
volatile LONG g_bodyWaitingLogged = 0;
volatile LONG g_callbackActiveLogged = 0;
volatile LONG g_targetWaitingLogged = 0;
volatile LONG g_callbackOrderWaitingLogged = 0;
volatile LONG g_leftCallbackActiveLogged = 0;
volatile LONG g_leftTargetWaitingLogged = 0;
volatile LONG g_leftCallbackOrderWaitingLogged = 0;
void* g_failedPlayerBody = nullptr;
const char* g_installPhase = "idle";
SRWLOCK g_tuningLock = SRWLOCK_INIT;
fearvr::ArmIkTuning g_armIkTuning{};

bool IsLeftControl(const RightHandControlState* control) noexcept {
    return control == &g_leftControl;
}

SRWLOCK& ControlLockFor(bool left) noexcept {
    return left ? g_leftControlLock : g_controlLock;
}

SRWLOCK& TargetLockFor(bool left) noexcept {
    return left ? g_leftTargetLock : g_targetLock;
}

RightHandControlState& ControlFor(bool left) noexcept {
    return left ? g_leftControl : g_control;
}

TrackedRightHandTarget& TargetFor(bool left) noexcept {
    return left ? g_leftTarget : g_target;
}

volatile LONG& CallbackActiveLogFor(bool left) noexcept {
    return left ? g_leftCallbackActiveLogged : g_callbackActiveLogged;
}

volatile LONG& TargetWaitingLogFor(bool left) noexcept {
    return left ? g_leftTargetWaitingLogged : g_targetWaitingLogged;
}

volatile LONG& CallbackOrderWaitingLogFor(bool left) noexcept {
    return left
        ? g_leftCallbackOrderWaitingLogged
        : g_callbackOrderWaitingLogged;
}

fearvr::ArmIkTuning CopyArmIkTuning() noexcept {
    fearvr::ArmIkTuning tuning{};
    AcquireSRWLockShared(&g_tuningLock);
    tuning = g_armIkTuning;
    ReleaseSRWLockShared(&g_tuningLock);
    return fearvr::SanitizeArmIkTuning(tuning);
}

const char* ArmIkModeEvent(
    const char* handOnlyEvent,
    const char* fullArmEvent) noexcept {
    return InterlockedCompareExchange(&g_fullArmMode, 0, 0) != 0
        ? fullArmEvent : handOnlyEvent;
}

ArmIkLifecycleState CopyArmIkLifecycleState() noexcept {
    ArmIkLifecycleState lifecycle{};
    AcquireSRWLockShared(&g_lifecycleLock);
    lifecycle = g_lifecycleState;
    ReleaseSRWLockShared(&g_lifecycleLock);
    return lifecycle;
}

bool CurrentArmIkLifecycleAllowsInstall() noexcept {
    return ArmIkLifecycleAllowsInstall(CopyArmIkLifecycleState());
}

bool IsExecutableAddress(const void* address) noexcept {
    if (address == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) !=
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
            &encodedManagerAddress, functionAddress + 0x35U,
            sizeof(encodedManagerAddress));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        signatureMatches = false;
    }
    if (!signatureMatches || !IsExecutableAddress(functionAddress)) {
        return false;
    }
    manager = gameClientBase + kPlayerBodyManagerRva;
    return encodedManagerAddress == static_cast<std::uint32_t>(
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

bool ModelObjectLooksLive(void* playerBody) noexcept {
    if (playerBody == nullptr) {
        return false;
    }
    bool isModel = false;
    __try {
        isModel = *(static_cast<unsigned char*>(playerBody) + 0x20U) == 1U;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        isModel = false;
    }
    return isModel;
}

bool PlayerBodyIsLive(void* playerBody) noexcept {
    void* current = nullptr;
    return ReadPlayerBody(current) && current == playerBody &&
        ModelObjectLooksLive(playerBody);
}

PhysicalMeleeRigidTransform ToRigidTransform(
    const ModelTransformAbi& value) noexcept {
    return {
        {value.position[0], value.position[1], value.position[2]},
        {value.rotation[0], value.rotation[1],
         value.rotation[2], value.rotation[3]}};
}

PhysicalMeleeRigidTransform ToRigidTransform(
    const RigidTransformAbi& value) noexcept {
    return {
        {value.position[0], value.position[1], value.position[2]},
        {value.rotation[0], value.rotation[1],
         value.rotation[2], value.rotation[3]}};
}

PhysicalMeleeRigidTransform ComposeRigidTransform(
    const PhysicalMeleeRigidTransform& parentWorld,
    const PhysicalMeleeRigidTransform& childFromParent) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(parentWorld) ||
        !PhysicalMeleeRigidTransformIsValid(childFromParent)) {
        return {};
    }
    const fearvr::TrackingQuaternion parentRotation =
        fearvr::Normalize(parentWorld.rotation);
    return {
        PhysicalMeleeAdd(
            parentWorld.positionUnits,
            fearvr::Rotate(
                parentRotation,
                childFromParent.positionUnits)),
        fearvr::Multiply(
            parentRotation,
            fearvr::Normalize(childFromParent.rotation))};
}

PhysicalMeleeRigidTransform RelativeRigidTransform(
    const PhysicalMeleeRigidTransform& parentWorld,
    const PhysicalMeleeRigidTransform& childWorld) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(parentWorld) ||
        !PhysicalMeleeRigidTransformIsValid(childWorld)) {
        return {};
    }
    const fearvr::TrackingQuaternion parentInverse =
        fearvr::Conjugate(fearvr::Normalize(parentWorld.rotation));
    return {
        fearvr::Rotate(
            parentInverse,
            PhysicalMeleeSubtract(
                childWorld.positionUnits,
                parentWorld.positionUnits)),
        fearvr::Multiply(
            parentInverse,
            fearvr::Normalize(childWorld.rotation))};
}

bool ReadFreshTarget(
    bool left, TrackedRightHandTarget& target) noexcept {
    SRWLOCK& lock = TargetLockFor(left);
    AcquireSRWLockShared(&lock);
    target = TargetFor(left);
    ReleaseSRWLockShared(&lock);
    return target.valid && target.sampleId != 0U &&
        target.timestampNs != 0U && target.publishedTick != 0U &&
        GetTickCount64() - target.publishedTick <=
            kTargetFreshnessMilliseconds &&
        PhysicalMeleeRigidTransformIsValid(target.socketWorld);
}

fearvr::ArmIkVector ToArmIkVector(
    const fearvr::TrackingVector& value) noexcept {
    return {value.x, value.y, value.z};
}

fearvr::TrackingVector ToTrackingVector(
    const fearvr::ArmIkVector& value) noexcept {
    return {value.x, value.y, value.z};
}

bool WriteNodeObjectRotationFromWorld(
    const PhysicalMeleeRigidTransform& modelWorld,
    const fearvr::TrackingQuaternion& desiredWorldRotation,
    RigidTransformAbi& nodeTransform) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(modelWorld) ||
        !fearvr::IsFinite(desiredWorldRotation)) {
        return false;
    }
    const fearvr::TrackingQuaternion desiredObjectRotation =
        fearvr::Multiply(
            fearvr::Conjugate(
                fearvr::Normalize(modelWorld.rotation)),
            fearvr::Normalize(desiredWorldRotation));
    if (!fearvr::IsFinite(desiredObjectRotation)) {
        return false;
    }
    nodeTransform.rotation[0] = desiredObjectRotation.x;
    nodeTransform.rotation[1] = desiredObjectRotation.y;
    nodeTransform.rotation[2] = desiredObjectRotation.z;
    nodeTransform.rotation[3] = desiredObjectRotation.w;
    return true;
}

void __cdecl ArmNodeControl(
    const NodeControlDataAbi& data,
    void* userData) {
    if ((userData != &g_control && userData != &g_leftControl) ||
        data.model == nullptr ||
        data.modelTransform == nullptr || data.nodeTransform == nullptr) {
        return;
    }

    auto* const controlState =
        static_cast<RightHandControlState*>(userData);
    const bool left = IsLeftControl(controlState);
    SRWLOCK& controlLock = ControlLockFor(left);
    RightHandControlState control{};
    AcquireSRWLockShared(&controlLock);
    control = *controlState;
    ReleaseSRWLockShared(&controlLock);
    const ArmIkLifecycleState lifecycle =
        CopyArmIkLifecycleState();
    const bool registeredNode = data.node == control.handNode ||
        (control.fullArm &&
         (data.node == control.upperArmNode ||
          data.node == control.forearmNode));
    if (!control.installed || !registeredNode ||
        data.model != control.playerBody ||
        control.lifecycleGeneration != lifecycle.generation ||
        !ArmIkLifecycleAllowsInstall(lifecycle) ||
        !PlayerBodyIsLive(control.playerBody)) {
        return;
    }

    TrackedRightHandTarget target{};
    if (!ReadFreshTarget(left, target)) {
        if (InterlockedCompareExchange(
                &TargetWaitingLogFor(left), 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                left
                    ? "arm_ik_left_arm_waiting"
                    : ArmIkModeEvent(
                          "arm_ik_right_hand_proof_waiting",
                          "arm_ik_right_arm_waiting"),
                "reason=tracked_weapon_pose_not_fresh");
        }
        return;
    }
    if (!std::isfinite(data.modelTransform->scale) ||
        data.modelTransform->scale < 0.99F ||
        data.modelTransform->scale > 1.01F) {
        return;
    }

    const PhysicalMeleeRigidTransform modelWorld =
        ToRigidTransform(*data.modelTransform);
    if (!PhysicalMeleeRigidTransformIsValid(modelWorld)) {
        return;
    }
    AcquireSRWLockExclusive(&controlLock);
    if (controlState->installed &&
        controlState->playerBody == control.playerBody) {
        controlState->lastModelWorld = modelWorld;
        controlState->lastModelWorldValid = true;
    }
    ReleaseSRWLockExclusive(&controlLock);
    if (target.bodyRelativeValid &&
        target.referencePlayerBody == control.playerBody) {
        const PhysicalMeleeRigidTransform rebasedSocketWorld =
            ComposeRigidTransform(
                modelWorld, target.socketFromBody);
        if (!PhysicalMeleeRigidTransformIsValid(
                rebasedSocketWorld)) {
            return;
        }
        target.socketWorld = rebasedSocketWorld;
    }
    const PhysicalMeleeVisualProxyTransform desiredNode =
        ResolvePhysicalMeleeHeldModelTransform(
            target.socketWorld,
            control.socketFromNode.positionUnits,
            control.socketFromNode.rotation,
            true);
    if (!desiredNode.active) {
        return;
    }

    if (control.fullArm && data.node == control.upperArmNode) {
        const PhysicalMeleeRigidTransform upperWorld =
            ComposeRigidTransform(
                modelWorld,
                ToRigidTransform(*data.nodeTransform));
        if (!PhysicalMeleeRigidTransformIsValid(upperWorld)) {
            return;
        }

        const fearvr::ArmIkTuning tuning = CopyArmIkTuning();
        const fearvr::TrackingQuaternion bodyRotation =
            fearvr::Normalize(modelWorld.rotation);
        const fearvr::TrackingVector bodyRight = fearvr::Rotate(
            bodyRotation, {1.0F, 0.0F, 0.0F});
        const fearvr::TrackingVector bodyUp = fearvr::Rotate(
            bodyRotation, {0.0F, 1.0F, 0.0F});
        const fearvr::TrackingVector bodyForward = fearvr::Rotate(
            bodyRotation, {0.0F, 0.0F, 1.0F});
        const fearvr::TrackingVector poleDirection =
            PhysicalMeleeAdd(
                PhysicalMeleeScale(
                    bodyRight,
                    tuning.elbowOutward * (left ? -1.0F : 1.0F)),
                PhysicalMeleeAdd(
                    PhysicalMeleeScale(
                        bodyUp, -tuning.elbowDown),
                    PhysicalMeleeScale(
                        bodyForward, -tuning.elbowBack)));
        const fearvr::TrackingVector animatedUpperDirection =
            fearvr::Rotate(
                upperWorld.rotation,
                control.forearmOffsetFromUpperArm);
        const fearvr::TwoBoneElbowSolution solution =
            fearvr::SolveTwoBoneElbow(
                ToArmIkVector(upperWorld.positionUnits),
                ToArmIkVector(
                    desiredNode.objectWorld.positionUnits),
                control.upperLength, control.lowerLength,
                ToArmIkVector(poleDirection),
                control.previousBendDirection,
                tuning.preserveElbowContinuity &&
                    control.previousBendValid,
                ToArmIkVector(animatedUpperDirection));
        if (!solution.valid) {
            return;
        }
        const fearvr::TrackingVector desiredUpperDirection =
            PhysicalMeleeSubtract(
                ToTrackingVector(solution.elbow),
                upperWorld.positionUnits);
        fearvr::TrackingQuaternion deltaRotation{};
        if (!PhysicalMeleeShortestArcRotation(
                animatedUpperDirection,
                desiredUpperDirection,
                deltaRotation)) {
            return;
        }
        const fearvr::TrackingQuaternion desiredUpperWorldRotation =
            fearvr::Multiply(
                deltaRotation,
                fearvr::Normalize(upperWorld.rotation));
        if (!WriteNodeObjectRotationFromWorld(
                modelWorld, desiredUpperWorldRotation,
                *data.nodeTransform)) {
            return;
        }

        AcquireSRWLockExclusive(&controlLock);
        if (controlState->installed && controlState->fullArm &&
            controlState->playerBody == control.playerBody &&
            controlState->upperArmNode == control.upperArmNode) {
            controlState->previousBendDirection =
                solution.bendDirection;
            controlState->previousBendValid =
                tuning.preserveElbowContinuity;
            controlState->solvedShoulderWorld =
                upperWorld.positionUnits;
            controlState->solvedElbowWorld =
                ToTrackingVector(solution.elbow);
            controlState->solvedSampleId = target.sampleId;
            controlState->solvedTick = GetTickCount64();
            controlState->targetClamped = solution.targetClamped;
        }
        ReleaseSRWLockExclusive(&controlLock);
        InterlockedExchange(&CallbackOrderWaitingLogFor(left), 0);
        return;
    }

    if (control.fullArm && data.node == control.forearmNode) {
        if (control.solvedSampleId != target.sampleId ||
            control.solvedTick == 0U ||
            GetTickCount64() - control.solvedTick >
                kTargetFreshnessMilliseconds) {
            if (InterlockedCompareExchange(
                    &CallbackOrderWaitingLogFor(left), 1, 0) == 0 &&
                g_log != nullptr) {
                g_log(
                    left
                        ? "arm_ik_left_arm_callback_order_waiting"
                        : "arm_ik_right_arm_callback_order_waiting",
                    "reason=forearm_ran_without_current_upper_solve");
            }
            return;
        }
        const PhysicalMeleeRigidTransform forearmWorld =
            ComposeRigidTransform(
                modelWorld,
                ToRigidTransform(*data.nodeTransform));
        if (!PhysicalMeleeRigidTransformIsValid(forearmWorld)) {
            return;
        }
        const fearvr::TrackingVector animatedLowerDirection =
            fearvr::Rotate(
                forearmWorld.rotation,
                control.handOffsetFromForearm);
        const fearvr::TrackingVector desiredLowerDirection =
            PhysicalMeleeSubtract(
                desiredNode.objectWorld.positionUnits,
                forearmWorld.positionUnits);
        fearvr::TrackingQuaternion deltaRotation{};
        if (!PhysicalMeleeShortestArcRotation(
                animatedLowerDirection,
                desiredLowerDirection,
                deltaRotation)) {
            return;
        }
        const fearvr::TrackingQuaternion desiredForearmWorldRotation =
            fearvr::Multiply(
                deltaRotation,
                fearvr::Normalize(forearmWorld.rotation));
        WriteNodeObjectRotationFromWorld(
            modelWorld, desiredForearmWorldRotation,
            *data.nodeTransform);
        return;
    }

    const PhysicalMeleeRigidTransform desiredNodeObject =
        RelativeRigidTransform(modelWorld, desiredNode.objectWorld);
    if (!PhysicalMeleeRigidTransformIsValid(desiredNodeObject)) {
        return;
    }

    data.nodeTransform->position[0] =
        desiredNodeObject.positionUnits.x;
    data.nodeTransform->position[1] =
        desiredNodeObject.positionUnits.y;
    data.nodeTransform->position[2] =
        desiredNodeObject.positionUnits.z;
    data.nodeTransform->rotation[0] =
        desiredNodeObject.rotation.x;
    data.nodeTransform->rotation[1] =
        desiredNodeObject.rotation.y;
    data.nodeTransform->rotation[2] =
        desiredNodeObject.rotation.z;
    data.nodeTransform->rotation[3] =
        desiredNodeObject.rotation.w;
    InterlockedExchange(&TargetWaitingLogFor(left), 0);

    std::uint64_t heartbeat = 0U;
    const ULONGLONG callbackTick = GetTickCount64();
    AcquireSRWLockExclusive(&controlLock);
    if (controlState->installed &&
        controlState->playerBody == control.playerBody &&
        controlState->handNode == control.handNode &&
        controlState->lifecycleGeneration ==
            control.lifecycleGeneration) {
        controlState->callbackHeartbeat =
            controlState->callbackHeartbeat ==
                std::numeric_limits<std::uint64_t>::max()
            ? 1U : controlState->callbackHeartbeat + 1U;
        controlState->lastCallbackTick = callbackTick;
        heartbeat = controlState->callbackHeartbeat;
    }
    ReleaseSRWLockExclusive(&controlLock);
    if (heartbeat != 0U &&
        (heartbeat == 1U ||
         heartbeat % kCallbackHeartbeatLogInterval == 0U) &&
        g_log != nullptr) {
        char heartbeatDetail[256]{};
        std::snprintf(
            heartbeatDetail, sizeof(heartbeatDetail),
            "side=%s player_body=%p generation=%lu "
            "heartbeat=%llu sample_id=%llu",
            left ? "left" : "right", control.playerBody,
            static_cast<unsigned long>(
                control.lifecycleGeneration),
            static_cast<unsigned long long>(heartbeat),
            static_cast<unsigned long long>(target.sampleId));
        g_log("arm_ik_callback_heartbeat", heartbeatDetail);
    }

    if (heartbeat != 0U &&
        InterlockedCompareExchange(
            &CallbackActiveLogFor(left), 1, 0) == 0 &&
        g_log != nullptr) {
        char detail[512]{};
        std::snprintf(
            detail, sizeof(detail),
            "player_body=%p node=%lu sample_id=%llu "
            "target_position=(%.4f,%.4f,%.4f) "
            "side=%s mode=%s upper_length=%.3f lower_length=%.3f "
            "upper_solve_current=%u target_clamped=%u "
            "locomotion_anchor=%s generation=%lu heartbeat=%llu",
            control.playerBody,
            static_cast<unsigned long>(control.handNode),
            static_cast<unsigned long long>(target.sampleId),
            target.socketWorld.positionUnits.x,
            target.socketWorld.positionUnits.y,
            target.socketWorld.positionUnits.z,
            left ? "left" : "right",
            control.fullArm
                ? (left ? "full_left_arm" : "full_right_arm")
                : "right_hand_socket_only",
            control.upperLength, control.lowerLength,
            control.fullArm &&
                control.solvedSampleId == target.sampleId ? 1U : 0U,
            control.targetClamped ? 1U : 0U,
            target.bodyRelativeValid
                ? "player_body_local" : "world_fallback",
            static_cast<unsigned long>(
                control.lifecycleGeneration),
            static_cast<unsigned long long>(heartbeat));
        g_log(
            left
                ? "arm_ik_left_arm_active"
                : control.fullArm
                ? "arm_ik_right_arm_active"
                : "arm_ik_right_hand_proof_active",
            detail);
    }
}

ModelResult SafeAddNodeControl(
    void* playerBody,
    ModelHandle node,
    RightHandControlState* controlState) noexcept {
    if (g_addNodeControlSpecific == nullptr ||
        playerBody == nullptr || node == kInvalidModelHandle) {
        return 0xFFFFFFFFU;
    }
    ModelResult result = 0xFFFFFFFFU;
    __try {
        result = g_addNodeControlSpecific(
            g_model, playerBody, node,
            &ArmNodeControl, controlState);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = 0xFFFFFFFFU;
    }
    return result;
}

ModelResult SafeRemoveNodeControl(
    void* playerBody,
    ModelHandle node,
    RightHandControlState* controlState) noexcept {
    if (g_removeNodeControlSpecific == nullptr ||
        playerBody == nullptr || node == kInvalidModelHandle) {
        return 0xFFFFFFFFU;
    }
    ModelResult result = 0xFFFFFFFFU;
    __try {
        result = g_removeNodeControlSpecific(
            g_model, playerBody, node,
            &ArmNodeControl, controlState);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = 0xFFFFFFFFU;
    }
    return result;
}

void RollBackNodeControls(
    void* playerBody,
    const ModelHandle (&nodes)[3],
    std::size_t count,
    RightHandControlState* controlState) noexcept {
    while (count > 0U) {
        --count;
        SafeRemoveNodeControl(
            playerBody, nodes[count], controlState);
    }
}

struct ArmControlReleaseSummary {
    void* playerBody{nullptr};
    std::uint64_t callbackHeartbeat{0U};
    std::uint32_t lifecycleGeneration{1U};
    std::uint32_t removeCount{0U};
    std::uint32_t removeFailures{0U};
    bool installed{false};
    bool removeAttempted{false};
};

ArmControlReleaseSummary ClearArmControl(
    bool left, bool attemptRemove) noexcept {
    SRWLOCK& controlLock = ControlLockFor(left);
    RightHandControlState& controlState = ControlFor(left);
    RightHandControlState oldControl{};
    AcquireSRWLockExclusive(&controlLock);
    oldControl = controlState;
    controlState = {};
    ReleaseSRWLockExclusive(&controlLock);
    InterlockedExchange(&CallbackActiveLogFor(left), 0);
    InterlockedExchange(&TargetWaitingLogFor(left), 0);
    InterlockedExchange(&CallbackOrderWaitingLogFor(left), 0);

    ArmControlReleaseSummary summary{};
    summary.playerBody = oldControl.playerBody;
    summary.callbackHeartbeat = oldControl.callbackHeartbeat;
    summary.lifecycleGeneration = oldControl.lifecycleGeneration;
    summary.installed = oldControl.installed;
    if (attemptRemove && oldControl.installed &&
        g_removeNodeControlSpecific != nullptr &&
        ModelObjectLooksLive(oldControl.playerBody)) {
        summary.removeAttempted = true;
        const ModelHandle nodes[3]{
            oldControl.handNode,
            oldControl.forearmNode,
            oldControl.upperArmNode};
        const std::size_t nodeCount = oldControl.fullArm ? 3U : 1U;
        for (std::size_t index = 0U; index < nodeCount; ++index) {
            ++summary.removeCount;
            if (SafeRemoveNodeControl(
                    oldControl.playerBody, nodes[index],
                    &controlState) != kModelOk) {
                ++summary.removeFailures;
            }
        }
    }
    if (oldControl.installed && g_log != nullptr) {
        char detail[448]{};
        std::snprintf(
            detail, sizeof(detail),
            "player_body=%p mode=%s remove_attempted=%d "
            "remove_count=%lu remove_failures=%lu "
            "generation=%lu heartbeat=%llu",
            oldControl.playerBody,
            oldControl.fullArm
                ? (left ? "full_left_arm" : "full_right_arm")
                : "right_hand_socket_only",
            summary.removeAttempted ? 1 : 0,
            static_cast<unsigned long>(summary.removeCount),
            static_cast<unsigned long>(summary.removeFailures),
            static_cast<unsigned long>(
                oldControl.lifecycleGeneration),
            static_cast<unsigned long long>(
                oldControl.callbackHeartbeat));
        g_log(
            left
                ? "arm_ik_left_arm_released"
                : oldControl.fullArm
                ? "arm_ik_right_arm_released"
                : "arm_ik_right_hand_proof_released",
            detail);
    }
    return summary;
}

bool InstallArmControlUnchecked(
    void* playerBody, bool left) noexcept {
    const bool fullArm = left || InterlockedCompareExchange(
        &g_fullArmMode, 0, 0) != 0;
    const char* const upperArmName =
        left ? "Left_armu" : "Right_armu";
    const char* const forearmName =
        left ? "Left_arml" : "Right_arml";
    const char* const handName =
        left ? "Left_hand" : "Right_hand";
    const char* const handSocketName =
        left ? "LeftHand" : "RightHand";
    RightHandControlState& controlState = ControlFor(left);
    SRWLOCK& controlLock = ControlLockFor(left);
    ModelHandle upperArmNode = kInvalidModelHandle;
    ModelHandle forearmNode = kInvalidModelHandle;
    ModelHandle handNode = kInvalidModelHandle;
    ModelHandle handSocket = kInvalidModelHandle;
    ModelTransformAbi upperArmWorld{};
    ModelTransformAbi forearmWorld{};
    ModelTransformAbi handWorld{};
    ModelTransformAbi socketWorld{};

    if (fullArm) {
        g_installPhase = left
            ? "get_left_upper_arm_node"
            : "get_right_upper_arm_node";
        if (g_getNode(
                g_model, playerBody, upperArmName, upperArmNode) !=
                kModelOk ||
            upperArmNode == kInvalidModelHandle) {
            return false;
        }
        g_installPhase = left
            ? "get_left_forearm_node"
            : "get_right_forearm_node";
        if (g_getNode(
                g_model, playerBody, forearmName, forearmNode) !=
                kModelOk ||
            forearmNode == kInvalidModelHandle) {
            return false;
        }
    }
    g_installPhase = left
        ? "get_left_hand_node" : "get_right_hand_node";
    if (g_getNode(
            g_model, playerBody, handName, handNode) != kModelOk ||
        handNode == kInvalidModelHandle) {
        return false;
    }
    g_installPhase = left
        ? "get_left_hand_socket" : "get_right_hand_socket";
    if (g_getSocket(
            g_model, playerBody, handSocketName, handSocket) != kModelOk ||
        handSocket == kInvalidModelHandle) {
        return false;
    }
    if (fullArm) {
        g_installPhase = left
            ? "get_left_upper_arm_transform"
            : "get_right_upper_arm_transform";
        if (g_getNodeTransform(
                g_model, playerBody, upperArmNode,
                upperArmWorld, true) != kModelOk) {
            return false;
        }
        g_installPhase = left
            ? "get_left_forearm_transform"
            : "get_right_forearm_transform";
        if (g_getNodeTransform(
                g_model, playerBody, forearmNode,
                forearmWorld, true) != kModelOk) {
            return false;
        }
    }
    g_installPhase = left
        ? "get_left_hand_node_transform"
        : "get_right_hand_node_transform";
    if (g_getNodeTransform(
            g_model, playerBody, handNode, handWorld, true) != kModelOk) {
        return false;
    }
    g_installPhase = left
        ? "get_left_hand_socket_transform"
        : "get_right_hand_socket_transform";
    if (g_getSocketTransform(
            g_model, playerBody, handSocket,
            socketWorld, true) != kModelOk) {
        return false;
    }
    if ((fullArm &&
         (!std::isfinite(upperArmWorld.scale) ||
          !std::isfinite(forearmWorld.scale) ||
          upperArmWorld.scale < 0.99F ||
          upperArmWorld.scale > 1.01F ||
          forearmWorld.scale < 0.99F ||
          forearmWorld.scale > 1.01F)) ||
        !std::isfinite(handWorld.scale) ||
        !std::isfinite(socketWorld.scale) ||
        handWorld.scale < 0.99F || handWorld.scale > 1.01F ||
        socketWorld.scale < 0.99F || socketWorld.scale > 1.01F) {
        return false;
    }

    const PhysicalMeleeRigidTransform hand =
        ToRigidTransform(handWorld);
    const PhysicalMeleeRigidTransform socket =
        ToRigidTransform(socketWorld);
    if (!PhysicalMeleeRigidTransformIsValid(hand) ||
        !PhysicalMeleeRigidTransformIsValid(socket)) {
        return false;
    }
    const PhysicalMeleeRigidTransform socketFromNode =
        RelativeRigidTransform(hand, socket);
    if (!PhysicalMeleeRigidTransformIsValid(socketFromNode) ||
        PhysicalMeleeLength(socketFromNode.positionUnits) > 100.0F) {
        return false;
    }

    const ArmIkLifecycleState lifecycle =
        CopyArmIkLifecycleState();
    if (!ArmIkLifecycleAllowsInstall(lifecycle)) {
        g_installPhase = "lifecycle_not_playing";
        return false;
    }

    RightHandControlState next{};
    next.playerBody = playerBody;
    next.upperArmNode = upperArmNode;
    next.forearmNode = forearmNode;
    next.handNode = handNode;
    next.handSocket = handSocket;
    next.socketFromNode = socketFromNode;
    next.fullArm = fullArm;
    next.lifecycleGeneration = lifecycle.generation;

    if (fullArm) {
        const PhysicalMeleeRigidTransform upperArm =
            ToRigidTransform(upperArmWorld);
        const PhysicalMeleeRigidTransform forearm =
            ToRigidTransform(forearmWorld);
        if (!PhysicalMeleeRigidTransformIsValid(upperArm) ||
            !PhysicalMeleeRigidTransformIsValid(forearm)) {
            return false;
        }
        const PhysicalMeleeRigidTransform forearmFromUpperArm =
            RelativeRigidTransform(upperArm, forearm);
        const PhysicalMeleeRigidTransform handFromForearm =
            RelativeRigidTransform(forearm, hand);
        next.forearmOffsetFromUpperArm =
            forearmFromUpperArm.positionUnits;
        next.handOffsetFromForearm =
            handFromForearm.positionUnits;
        next.upperLength = PhysicalMeleeLength(
            next.forearmOffsetFromUpperArm);
        next.lowerLength = PhysicalMeleeLength(
            next.handOffsetFromForearm);
        if (!PhysicalMeleeRigidTransformIsValid(
                forearmFromUpperArm) ||
            !PhysicalMeleeRigidTransformIsValid(
                handFromForearm) ||
            !std::isfinite(next.upperLength) ||
            !std::isfinite(next.lowerLength) ||
            next.upperLength < 1.0F || next.upperLength > 200.0F ||
            next.lowerLength < 1.0F || next.lowerLength > 200.0F) {
            return false;
        }
    }

    ModelHandle registeredNodes[3]{
        kInvalidModelHandle,
        kInvalidModelHandle,
        kInvalidModelHandle};
    std::size_t registeredCount = 0U;
    if (fullArm) {
        g_installPhase = left
            ? "add_left_upper_arm_node_control"
            : "add_right_upper_arm_node_control";
        if (SafeAddNodeControl(
                playerBody, upperArmNode, &controlState) != kModelOk) {
            return false;
        }
        registeredNodes[registeredCount++] = upperArmNode;
        g_installPhase = left
            ? "add_left_forearm_node_control"
            : "add_right_forearm_node_control";
        if (SafeAddNodeControl(
                playerBody, forearmNode, &controlState) != kModelOk) {
            RollBackNodeControls(
                playerBody, registeredNodes, registeredCount,
                &controlState);
            return false;
        }
        registeredNodes[registeredCount++] = forearmNode;
    }
    g_installPhase = left
        ? "add_left_hand_node_control"
        : "add_right_hand_node_control";
    if (SafeAddNodeControl(
            playerBody, handNode, &controlState) != kModelOk) {
        RollBackNodeControls(
            playerBody, registeredNodes, registeredCount,
            &controlState);
        return false;
    }
    registeredNodes[registeredCount++] = handNode;

    const ArmIkLifecycleState lifecycleAfterRegistration =
        CopyArmIkLifecycleState();
    if (!ArmIkLifecycleAllowsInstall(lifecycleAfterRegistration) ||
        lifecycleAfterRegistration.generation !=
            next.lifecycleGeneration) {
        g_installPhase = "lifecycle_changed_during_install";
        RollBackNodeControls(
            playerBody, registeredNodes, registeredCount, &controlState);
        return false;
    }
    next.installed = true;
    AcquireSRWLockExclusive(&controlLock);
    controlState = next;
    ReleaseSRWLockExclusive(&controlLock);
    g_installPhase = "complete";

    if (g_log != nullptr) {
        char detail[896]{};
        std::snprintf(
            detail, sizeof(detail),
            "player_body=%p mode=%s upper_arm_node=%lu "
            "forearm_node=%lu hand_node=%lu hand_socket=%lu "
            "upper_length=%.4f lower_length=%.4f "
            "forearm_from_upper_position=(%.4f,%.4f,%.4f) "
            "hand_from_forearm_position=(%.4f,%.4f,%.4f) "
            "socket_from_node_position=(%.4f,%.4f,%.4f) "
            "socket_from_node_rotation=(%.6f,%.6f,%.6f,%.6f) "
            "node_control_slot=%lu registered_nodes=%lu "
            "generation=%lu",
            playerBody,
            fullArm
                ? (left ? "full_left_arm" : "full_right_arm")
                : "right_hand_socket_only",
            static_cast<unsigned long>(upperArmNode),
            static_cast<unsigned long>(forearmNode),
            static_cast<unsigned long>(handNode),
            static_cast<unsigned long>(handSocket),
            next.upperLength, next.lowerLength,
            next.forearmOffsetFromUpperArm.x,
            next.forearmOffsetFromUpperArm.y,
            next.forearmOffsetFromUpperArm.z,
            next.handOffsetFromForearm.x,
            next.handOffsetFromForearm.y,
            next.handOffsetFromForearm.z,
            socketFromNode.positionUnits.x,
            socketFromNode.positionUnits.y,
            socketFromNode.positionUnits.z,
            socketFromNode.rotation.x,
            socketFromNode.rotation.y,
            socketFromNode.rotation.z,
            socketFromNode.rotation.w,
            static_cast<unsigned long>(kAddNodeControlSpecificSlot),
            static_cast<unsigned long>(registeredCount),
            static_cast<unsigned long>(
                next.lifecycleGeneration));
        g_log(
            left
                ? "arm_ik_left_arm_installed"
                : fullArm
                ? "arm_ik_right_arm_installed"
                : "arm_ik_right_hand_proof_installed",
            detail);
    }
    return true;
}

bool InstallArmControl(void* playerBody, bool left) noexcept {
    __try {
        return InstallArmControlUnchecked(playerBody, left);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log != nullptr) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "player_body=%p phase=%s exception=0x%08lX",
                playerBody, g_installPhase,
                static_cast<unsigned long>(GetExceptionCode()));
            g_log(
                left
                    ? "arm_ik_left_arm_install_failed"
                    : InterlockedCompareExchange(
                    &g_fullArmMode, 0, 0) != 0
                    ? "arm_ik_right_arm_install_failed"
                    : "arm_ik_right_hand_proof_install_failed",
                detail);
        }
        return false;
    }
}

bool ValidateModelGlobal() noexcept {
    if (InterlockedCompareExchange(
            &g_modelGlobalValidated, 0, 0) != 0) {
        return true;
    }
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
        (retailModelGlobal != nullptr && retailModelGlobal != g_model)) {
        if (g_log != nullptr) {
            char detail[192]{};
            std::snprintf(
                detail, sizeof(detail),
                "reason=model_interface_global_mismatch "
                "registered=%p retail_global=%p read_succeeded=%d",
                g_model, retailModelGlobal,
                readSucceeded ? 1 : 0);
            g_log(
                ArmIkModeEvent(
                    "arm_ik_right_hand_proof_rejected",
                    "arm_ik_right_arm_rejected"),
                detail);
        }
        InterlockedExchange(&g_enabled, 0);
        return false;
    }
    if (retailModelGlobal == nullptr) {
        if (InterlockedCompareExchange(
                &g_modelGlobalWaitingLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                ArmIkModeEvent(
                    "arm_ik_right_hand_proof_waiting",
                    "arm_ik_right_arm_waiting"),
                "reason=model_interface_global_not_initialized");
        }
        return false;
    }
    InterlockedExchange(&g_modelGlobalValidated, 1);
    InterlockedExchange(&g_modelGlobalWaitingLogged, 0);
    if (g_log != nullptr) {
        char detail[128]{};
        std::snprintf(
            detail, sizeof(detail),
            "registered=%p retail_global=%p",
            g_model, retailModelGlobal);
        g_log(
            ArmIkModeEvent(
                "arm_ik_right_hand_proof_model_interface_validated",
                "arm_ik_right_arm_model_interface_validated"),
            detail);
    }
    return true;
}

} // namespace

bool InstallArmIkMode(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkIntegrationLogFunction log,
    bool fullArm) noexcept {
    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        log == nullptr) {
        return false;
    }
    if (InterlockedCompareExchange(&g_enabled, 0, 0) != 0) {
        return (InterlockedCompareExchange(
            &g_fullArmMode, 0, 0) != 0) == fullArm;
    }
    InterlockedExchange(&g_fullArmMode, fullArm ? 1 : 0);
    const char* const rejectedEvent = fullArm
        ? "arm_ik_right_arm_rejected"
        : "arm_ik_right_hand_proof_rejected";

    auto* const gameClientBase =
        static_cast<unsigned char*>(gameClientModule);
    void* playerBodyManager = nullptr;
    if (!ResolvePlayerBodyManager(
            gameClientBase, playerBodyManager)) {
        log(
            rejectedEvent,
            "reason=player_body_manager_signature_or_instance_mismatch");
        return false;
    }

    void* const model = FindCurrentInterface(
        masterDatabase, "ILTModelClient.Default", 0);
    if (model == nullptr) {
        log(
            rejectedEvent,
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
        log(rejectedEvent, detail);
        return false;
    }

    HMODULE const executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) {
        log(
            rejectedEvent,
            "reason=retail_executable_module_missing");
        return false;
    }
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executable);
    struct SlotExpectation {
        std::size_t slot;
        std::uintptr_t executableRva;
    };
    constexpr SlotExpectation kExpectations[] = {
        {kGetSocketSlot, kGetSocketExecutableRva},
        {kGetSocketTransformSlot, kGetSocketTransformExecutableRva},
        {kGetNodeSlot, kGetNodeExecutableRva},
        {kGetNodeTransformSlot, kGetNodeTransformExecutableRva},
        {kAddNodeControlSpecificSlot,
         kAddNodeControlSpecificExecutableRva},
        {kRemoveNodeControlSpecificSlot,
         kRemoveNodeControlSpecificExecutableRva}};
    for (const SlotExpectation& expectation : kExpectations) {
        void* target = nullptr;
        __try {
            target = vtable[expectation.slot];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            target = nullptr;
        }
        void* const expected =
            executableBase + expectation.executableRva;
        if (target != expected || !IsExecutableAddress(target)) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "reason=model_vtable_slot_mismatch slot=%lu "
                "target=%p expected=%p",
                static_cast<unsigned long>(expectation.slot),
                target, expected);
            log(rejectedEvent, detail);
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
    g_getNode = reinterpret_cast<GetNodeFunction>(vtable[kGetNodeSlot]);
    g_getNodeTransform = reinterpret_cast<GetNodeTransformFunction>(
        vtable[kGetNodeTransformSlot]);
    g_addNodeControlSpecific =
        reinterpret_cast<AddNodeControlSpecificFunction>(
            vtable[kAddNodeControlSpecificSlot]);
    g_removeNodeControlSpecific =
        reinterpret_cast<RemoveNodeControlSpecificFunction>(
            vtable[kRemoveNodeControlSpecificSlot]);
    g_failedPlayerBody = nullptr;
    AcquireSRWLockExclusive(&g_lifecycleLock);
    g_lifecycleState = {};
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    fearvr::ArmIkTuning loadedTuning{};
    const WeaponSettingsStoreResult tuningLoadResult =
        LoadArmIkTuning(loadedTuning);
    if (tuningLoadResult == WeaponSettingsStoreResult::Ok) {
        AcquireSRWLockExclusive(&g_tuningLock);
        g_armIkTuning = fearvr::SanitizeArmIkTuning(loadedTuning);
        ReleaseSRWLockExclusive(&g_tuningLock);
    }
    {
        const fearvr::ArmIkTuning tuning = CopyArmIkTuning();
        char tuningDetail[224]{};
        std::snprintf(
            tuningDetail, sizeof(tuningDetail),
            "result=%s outward=%.3f down=%.3f back=%.3f continuity=%u",
            WeaponSettingsStoreResultName(tuningLoadResult),
            tuning.elbowOutward, tuning.elbowDown, tuning.elbowBack,
            tuning.preserveElbowContinuity ? 1U : 0U);
        log("arm_ik_elbow_tuning_loaded", tuningDetail);
    }
    InterlockedExchange(&g_modelGlobalValidated,
        retailModelGlobal == model ? 1 : 0);
    InterlockedExchange(&g_modelGlobalWaitingLogged, 0);
    InterlockedExchange(&g_sampleCount, 0);
    InterlockedExchange(&g_bodyWaitingLogged, 0);
    InterlockedExchange(&g_callbackActiveLogged, 0);
    InterlockedExchange(&g_targetWaitingLogged, 0);
    InterlockedExchange(&g_callbackOrderWaitingLogged, 0);
    InterlockedExchange(&g_leftCallbackActiveLogged, 0);
    InterlockedExchange(&g_leftTargetWaitingLogged, 0);
    InterlockedExchange(&g_leftCallbackOrderWaitingLogged, 0);
    InterlockedExchange(&g_enabled, 1);

    char detail[384]{};
    std::snprintf(
        detail, sizeof(detail),
        "model_interface=%p player_body_manager=%p "
        "model_global=%p model_global_ready=%d "
        "add_slot=%lu remove_slot=%lu "
        "mode=%s mutation=opt_in "
        "lifecycle=retail_loading_generation "
        "active_status=fresh_callback_heartbeat",
        model, playerBodyManager, retailModelGlobal,
        retailModelGlobal == model ? 1 : 0,
        static_cast<unsigned long>(kAddNodeControlSpecificSlot),
        static_cast<unsigned long>(kRemoveNodeControlSpecificSlot),
        fullArm ? "full_both_arms" : "right_hand_socket_only");
    log(
        fullArm
            ? "arm_ik_right_arm_armed"
            : "arm_ik_right_hand_proof_armed",
        detail);
    return true;
}

bool InstallArmIkRightHandProof(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkIntegrationLogFunction log) noexcept {
    return InstallArmIkMode(
        masterDatabase, gameClientModule, log, false);
}

bool InstallArmIkRightArm(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkIntegrationLogFunction log) noexcept {
    return InstallArmIkMode(
        masterDatabase, gameClientModule, log, true);
}

void SampleArmIkRightHandProof() noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0) {
        return;
    }
    if (!CurrentArmIkLifecycleAllowsInstall()) {
        return;
    }
    const LONG sample = InterlockedIncrement(&g_sampleCount);
    if (sample != 1 && sample % 60 != 0) {
        return;
    }
    if (!ValidateModelGlobal()) {
        return;
    }

    void* playerBody = nullptr;
    if (!ReadPlayerBody(playerBody)) {
        if (InterlockedCompareExchange(
                &g_bodyWaitingLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                ArmIkModeEvent(
                    "arm_ik_right_hand_proof_waiting",
                    "arm_ik_right_arm_waiting"),
                "reason=player_body_manager_read_failed");
        }
        return;
    }

    RightHandControlState control{};
    AcquireSRWLockShared(&g_controlLock);
    control = g_control;
    ReleaseSRWLockShared(&g_controlLock);
    RightHandControlState leftControl{};
    AcquireSRWLockShared(&g_leftControlLock);
    leftControl = g_leftControl;
    ReleaseSRWLockShared(&g_leftControlLock);
    if (control.installed && control.playerBody != playerBody) {
        ClearArmControl(false, true);
        ClearArmControl(true, true);
        g_failedPlayerBody = nullptr;
    } else if (leftControl.installed &&
               leftControl.playerBody != playerBody) {
        ClearArmControl(true, true);
        ClearArmControl(false, true);
        g_failedPlayerBody = nullptr;
    }
    if (playerBody == nullptr) {
        if (InterlockedCompareExchange(
                &g_bodyWaitingLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                ArmIkModeEvent(
                    "arm_ik_right_hand_proof_waiting",
                    "arm_ik_right_arm_waiting"),
                "reason=player_body_not_created");
        }
        return;
    }
    const bool fullArm = InterlockedCompareExchange(
        &g_fullArmMode, 0, 0) != 0;
    if (control.installed && control.playerBody == playerBody &&
        (!fullArm ||
         (leftControl.installed &&
          leftControl.playerBody == playerBody))) {
        return;
    }
    if (playerBody == g_failedPlayerBody && sample % 600 != 0) {
        return;
    }

    InterlockedExchange(&g_bodyWaitingLogged, 0);
    bool installed = control.installed &&
        control.playerBody == playerBody;
    if (!installed) {
        installed = InstallArmControl(playerBody, false);
    }
    if (installed && fullArm &&
        !(leftControl.installed &&
          leftControl.playerBody == playerBody)) {
        installed = InstallArmControl(playerBody, true);
        if (!installed) {
            ClearArmControl(true, true);
            ClearArmControl(false, true);
        }
    }
    if (installed) {
        g_failedPlayerBody = nullptr;
    } else {
        g_failedPlayerBody = playerBody;
        if (g_log != nullptr) {
            char detail[224]{};
            std::snprintf(
                detail, sizeof(detail),
                "player_body=%p phase=%s retry_frames=600",
                playerBody, g_installPhase);
            g_log(
                ArmIkModeEvent(
                    "arm_ik_right_hand_proof_body_not_ready",
                    "arm_ik_right_arm_body_not_ready"),
                detail);
        }
    }
}

void PublishArmIkTarget(
    bool left,
    const float (&position)[3],
    const float (&rotation)[4],
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0) {
        return;
    }
    TrackedRightHandTarget target{};
    target.socketWorld = {
        {position[0], position[1], position[2]},
        {rotation[0], rotation[1], rotation[2], rotation[3]}};
    target.sampleId = sampleId;
    target.timestampNs = timestampNs;
    target.publishedTick = GetTickCount64();
    target.valid = sampleId != 0U && timestampNs != 0U &&
        PhysicalMeleeRigidTransformIsValid(target.socketWorld);
    RightHandControlState control{};
    SRWLOCK& controlLock = ControlLockFor(left);
    AcquireSRWLockShared(&controlLock);
    control = ControlFor(left);
    ReleaseSRWLockShared(&controlLock);
    if (target.valid && control.installed &&
        control.lastModelWorldValid &&
        PhysicalMeleeRigidTransformIsValid(
            control.lastModelWorld)) {
        target.socketFromBody = RelativeRigidTransform(
            control.lastModelWorld, target.socketWorld);
        target.referencePlayerBody = control.playerBody;
        target.bodyRelativeValid =
            PhysicalMeleeRigidTransformIsValid(
                target.socketFromBody) &&
            PhysicalMeleeLength(
                target.socketFromBody.positionUnits) <= 500.0F;
    }
    SRWLOCK& targetLock = TargetLockFor(left);
    AcquireSRWLockExclusive(&targetLock);
    TargetFor(left) = target;
    ReleaseSRWLockExclusive(&targetLock);
}

void InvalidateArmIkTarget(bool left) noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0) {
        return;
    }
    SRWLOCK& targetLock = TargetLockFor(left);
    AcquireSRWLockExclusive(&targetLock);
    TargetFor(left) = {};
    ReleaseSRWLockExclusive(&targetLock);
}

void PublishArmIkRightHandProofTarget(
    const float (&position)[3],
    const float (&rotation)[4],
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept {
    PublishArmIkTarget(
        false, position, rotation, sampleId, timestampNs);
}

void InvalidateArmIkRightHandProofTarget() noexcept {
    InvalidateArmIkTarget(false);
}

void PublishArmIkLeftHandTarget(
    const float (&position)[3],
    const float (&rotation)[4],
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept {
    PublishArmIkTarget(
        true, position, rotation, sampleId, timestampNs);
}

void InvalidateArmIkLeftHandTarget() noexcept {
    InvalidateArmIkTarget(true);
}

void NotifyArmIkRetailGameState(int gameState) noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0) {
        return;
    }

    ArmIkLifecycleTransition transition{};
    AcquireSRWLockExclusive(&g_lifecycleLock);
    transition = ObserveArmIkGameState(
        g_lifecycleState, gameState);
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    if (transition.action == ArmIkLifecycleAction::None) {
        return;
    }

    g_failedPlayerBody = nullptr;
    InterlockedExchange(&g_sampleCount, 0);
    if (transition.action ==
        ArmIkLifecycleAction::ResumeAfterLoad) {
        if (g_log != nullptr) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "previous_state=%d current_state=%d generation=%lu "
                "reinstall=next_render_sample same_body_address_allowed=1",
                transition.previousGameState,
                transition.currentGameState,
                static_cast<unsigned long>(
                    transition.generation));
            g_log("arm_ik_lifecycle_resume_pending", detail);
        }
        return;
    }

    InvalidateArmIkTarget(false);
    InvalidateArmIkTarget(true);
    const ArmControlReleaseSummary right =
        ClearArmControl(false, true);
    const ArmControlReleaseSummary left =
        ClearArmControl(true, true);
    if (g_log != nullptr) {
        char detail[896]{};
        std::snprintf(
            detail, sizeof(detail),
            "previous_state=%d current_state=%d generation=%lu "
            "right_body=%p right_installed=%u "
            "right_generation=%lu right_heartbeat=%llu "
            "right_remove_attempted=%u right_remove_count=%lu "
            "right_remove_failures=%lu "
            "left_body=%p left_installed=%u "
            "left_generation=%lu left_heartbeat=%llu "
            "left_remove_attempted=%u left_remove_count=%lu "
            "left_remove_failures=%lu targets_invalidated=1",
            transition.previousGameState,
            transition.currentGameState,
            static_cast<unsigned long>(transition.generation),
            right.playerBody, right.installed ? 1U : 0U,
            static_cast<unsigned long>(
                right.lifecycleGeneration),
            static_cast<unsigned long long>(
                right.callbackHeartbeat),
            right.removeAttempted ? 1U : 0U,
            static_cast<unsigned long>(right.removeCount),
            static_cast<unsigned long>(right.removeFailures),
            left.playerBody, left.installed ? 1U : 0U,
            static_cast<unsigned long>(
                left.lifecycleGeneration),
            static_cast<unsigned long long>(
                left.callbackHeartbeat),
            left.removeAttempted ? 1U : 0U,
            static_cast<unsigned long>(left.removeCount),
            static_cast<unsigned long>(left.removeFailures));
        g_log("arm_ik_lifecycle_invalidated", detail);
    }
}

namespace {

bool ArmControlHasFreshHeartbeat(
    bool left, bool requireFullArm) noexcept {
    const ArmIkLifecycleState lifecycle =
        CopyArmIkLifecycleState();
    if (!ArmIkLifecycleAllowsInstall(lifecycle)) {
        return false;
    }
    RightHandControlState control{};
    SRWLOCK& controlLock = ControlLockFor(left);
    AcquireSRWLockShared(&controlLock);
    control = ControlFor(left);
    ReleaseSRWLockShared(&controlLock);
    return control.installed && control.playerBody != nullptr &&
        (!requireFullArm || control.fullArm) &&
        control.lifecycleGeneration == lifecycle.generation &&
        control.callbackHeartbeat != 0U &&
        control.lastCallbackTick != 0U &&
        GetTickCount64() - control.lastCallbackTick <=
            kCallbackHeartbeatFreshnessMilliseconds;
}

} // namespace

bool ArmIkRightHandProofIsActive() noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0) {
        return false;
    }
    return ArmControlHasFreshHeartbeat(false, false);
}

bool ArmIkLeftHandIsActive() noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_fullArmMode, 0, 0) == 0) {
        return false;
    }
    return ArmControlHasFreshHeartbeat(true, true);
}

fearvr::ArmIkTuning ReadArmIkTuning() noexcept {
    return CopyArmIkTuning();
}

bool ApplyArmIkTuning(const fearvr::ArmIkTuning& tuning) noexcept {
    const fearvr::ArmIkTuning sanitized =
        fearvr::SanitizeArmIkTuning(tuning);
    AcquireSRWLockExclusive(&g_tuningLock);
    g_armIkTuning = sanitized;
    ReleaseSRWLockExclusive(&g_tuningLock);

    ResetArmIkBendMemory();
    return true;
}

void ResetArmIkBendMemory() noexcept {
    AcquireSRWLockExclusive(&g_controlLock);
    g_control.previousBendDirection = {};
    g_control.previousBendValid = false;
    g_control.solvedSampleId = 0U;
    g_control.solvedTick = 0U;
    ReleaseSRWLockExclusive(&g_controlLock);
    AcquireSRWLockExclusive(&g_leftControlLock);
    g_leftControl.previousBendDirection = {};
    g_leftControl.previousBendValid = false;
    g_leftControl.solvedSampleId = 0U;
    g_leftControl.solvedTick = 0U;
    ReleaseSRWLockExclusive(&g_leftControlLock);
}

bool ArmIkRightArmIsActive() noexcept {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_fullArmMode, 0, 0) == 0) {
        return false;
    }
    return ArmControlHasFreshHeartbeat(false, true) &&
        ArmIkLeftHandIsActive();
}

} // namespace condemnedvr
