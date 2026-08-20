#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <intrin.h>

#include <MinHook.h>

#include "arm_ik_integration.h"
#include "binding_input.h"
#include "condemned_block_pose.h"
#include "condemned_combat_diagnostics.h"
#include "condemned_controller_input.h"
#include "condemned_forensic_trace.h"
#include "condemned_locomotion.h"
#include "condemned_menu_input.h"
#include "condemned_physical_melee.h"
#include "condemned_player_collision.h"
#include "head_tracking_math.h"
#include "module_identity.h"
#include "protocol.h"
#include "retail_menu_integration.h"
#include "weapon_identity_reader.h"

namespace condemnedvr {
namespace {

struct RetailBinding {
    std::uint32_t device;
    std::uint32_t object;
    std::uint32_t command;
    float defaultValue;
    float offset;
    float scale;
    float deadzoneMin;
    float deadzoneMax;
    float deadzoneValue;
    float commandMin;
    float commandMax;
    float condemnedState[4];
};
static_assert(
    sizeof(RetailBinding) == 60,
    "Condemned CBindMgr binding layout changed.");

using GetBindingValueFunction =
    float(__thiscall*)(const void*, const RetailBinding*);
using GetExtremalCommandValueFunction =
    float(__thiscall*)(const void*, std::uint32_t);
using GetInputStateFunction = BOOL(__cdecl*)(FearVrInputState*);
using SubmitHapticRequestFunction =
    BOOL(__cdecl*)(const FearVrHapticRequest*);
struct VectorAbi {
    float x;
    float y;
    float z;
};
static_assert(sizeof(VectorAbi) == 12);
struct QuaternionAbi {
    float x;
    float y;
    float z;
    float w;
};
static_assert(sizeof(QuaternionAbi) == 16);
struct RigidTransformAbi {
    VectorAbi position;
    QuaternionAbi rotation;
};
static_assert(sizeof(RigidTransformAbi) == 28);

using ModelHandle = std::uint32_t;
using ModelResult = std::uint32_t;
constexpr ModelResult kModelOk = 0U;
constexpr ModelHandle kInvalidModelHandle = 0xFFFFFFFFU;
struct ModelTransformAbi {
    VectorAbi position;
    QuaternionAbi rotation;
    float scale;
};
static_assert(sizeof(ModelTransformAbi) == 32);
using GetModelSocketFunction = ModelResult(__thiscall*)(
    void*, void*, const char*, ModelHandle&);
using GetModelSocketTransformFunction = ModelResult(__thiscall*)(
    void*, void*, ModelHandle, ModelTransformAbi&, bool);

using GetFireVectorsFunction = bool(__thiscall*)(
    const void*, VectorAbi&, VectorAbi&, VectorAbi&, VectorAbi&);
using MeleeEnableCollisionsFunction = std::uintptr_t(__thiscall*)(
    void*, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t);
using MeleeUpdateCollisionFunction = void(__thiscall*)(void*, void*);
using BuildRigidTransformFunction = void*(__thiscall*)(
    void*, const VectorAbi*, const QuaternionAbi*);
using DatabaseFloatReaderFunction = float(__thiscall*)(
    void*, const void*, std::uint32_t, float);
using MeleeImpactDispatchFunction = std::uintptr_t(__thiscall*)(
    void*, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, std::uintptr_t);
using ResetMeleeTargetReferenceFunction =
    void(__thiscall*)(void*);
using SetMenuActiveFunction = void(__cdecl*)(BOOL);
using PlayerSetDimensionsFunction =
    void(__thiscall*)(void*, const VectorAbi*);
using GetObjectDimensionsFunction =
    std::uint32_t(__thiscall*)(void*, void*, VectorAbi*);
using SetObjectDimensionsFunction =
    std::uint32_t(__thiscall*)(
        void*, void*, VectorAbi*, std::uint32_t);
using SetVelocityFunction =
    std::uint32_t(__thiscall*)(void*, void*, const VectorAbi*);
using ClientShellUpdateFunction = void(__thiscall*)(void*);
using ClientShellKeyUpFunction = void(__thiscall*)(void*, int);
using ClientShellKeyDownFunction =
    void(__thiscall*)(void*, int, int);
using ClientShellCommandFunction =
    void(__thiscall*)(void*, int);
using ForensicDisplayUpdateFunction =
    void(__thiscall*)(void*, float);
using ForensicCameraSocketTransformFunction =
    std::uint32_t(__thiscall*)(void*, RigidTransformAbi*);
using PlayerManagerCommandFunction =
    bool(__thiscall*)(void*, int);
using ClientWeaponFireFunction =
    void(__thiscall*)(void*);
using ForensicCollectionActionFunction =
    bool(__thiscall*)(void*, void*);
using ClientShellGetInterfaceManagerFunction =
    void*(__thiscall*)(void*);
using EngineIntersectSegmentFunction =
    bool(__thiscall*)(void*, void*, void*);

struct ForensicIntersectQueryPrefixAbi {
    VectorAbi start;
    VectorAbi end;
};
static_assert(sizeof(ForensicIntersectQueryPrefixAbi) == 24);

struct PhysicalMeleeCurrentWeaponSnapshot {
    std::int32_t index{-1};
    void* weapon{nullptr};
    bool readable{false};
};

enum class PhysicalMeleeCollisionRole : std::uint8_t {
    Unknown = 0U,
    Attack,
    Block,
};

struct PhysicalMeleeCollisionClassification {
    void* controller{nullptr};
    void* record{nullptr};
    std::uintptr_t sourceObject{0U};
    std::uintptr_t collisionObject{0U};
    unsigned int attackIndex{0U};
    PhysicalMeleeCollisionRole role{
        PhysicalMeleeCollisionRole::Unknown};
};


constexpr std::uintptr_t kGetBindingValueRva = 0x000095F0U;
constexpr std::uintptr_t kGetExtremalCommandValueRva = 0x00009900U;
constexpr std::uintptr_t kGetFireVectorsRva = 0x0002AF70U;
// Retail's weapon-display path resolves the authored right-hand Flash and
// Breach sockets through ILTModelClient.Default. The first live candidate is
// deliberately limited to the current save's verified unbreakable Colt.
constexpr std::uintptr_t kModelClientGlobalRva = 0x00172EC0U;
constexpr std::size_t kGetModelSocketSlot = 1U;
constexpr std::size_t kGetModelSocketTransformSlot = 2U;
constexpr std::uintptr_t kGetModelSocketExecutableRva = 0x000378E0U;
constexpr std::uintptr_t kGetModelSocketTransformExecutableRva =
    0x000381D0U;
constexpr std::uintptr_t kRetailSocketTransformHelperRva = 0x0004C0A0U;
constexpr std::uintptr_t kFlashSocketNameRva = 0x0013DDF0U;
constexpr std::uintptr_t kBreachSocketNameRva = 0x0014B1A8U;
constexpr std::int32_t kHandgunMuzzleAcceptanceWeaponIndex = 76;

constexpr std::uintptr_t kMeleeEnableCollisionsRva = 0x0001FD00U;
constexpr std::uintptr_t kMeleeUpdateCollisionRva = 0x0001FC00U;
constexpr std::uintptr_t kBuildRigidTransformRva = 0x0000F690U;
constexpr std::uintptr_t kMeleeBuildRigidTransformReturnRva =
    0x0001FCDEU;
constexpr std::uintptr_t kMeleeNativeLengthUpReadReturnRva =
    0x0002000DU;
constexpr std::uintptr_t kMeleeNativeLengthDownReadReturnRva =
    0x00020033U;
constexpr std::uintptr_t kMeleeNativeRadiusReadReturnRva =
    0x00020059U;
constexpr std::uintptr_t kMasterDatabaseGlobalRva = 0x00172EB8U;
constexpr std::size_t kMasterDatabaseFloatReaderSlot = 0x80U;
constexpr std::uintptr_t kMeleeLengthUpPropertyNameRva = 0x0013A684U;
constexpr std::uintptr_t kMeleeLengthDownPropertyNameRva = 0x0013A678U;
constexpr std::uintptr_t kMeleeRadiusPropertyNameRva = 0x0013A670U;
constexpr std::size_t kMeleeLengthUpPropertyPushOffset = 0x2F5U;
constexpr std::size_t kMeleeLengthDownPropertyPushOffset = 0x31DU;
constexpr std::size_t kMeleeRadiusPropertyPushOffset = 0x343U;
constexpr std::uintptr_t kMeleeImpactDispatchRva = 0x0001F270U;
constexpr std::uintptr_t kMeleeImpactDispatchReturnRva =
    0x0001FBC8U;
constexpr std::uintptr_t kMeleeCollisionCallbackRva = 0x0001F830U;
constexpr std::uintptr_t kMeleeImpactDispatchCallRva = 0x0001FBC3U;
constexpr std::uintptr_t kMeleeTargetReferenceVectorPushRva = 0x000155C0U;
constexpr std::uintptr_t kMeleeTargetReferenceVectorPushCallRva = 0x0001FB87U;
constexpr std::uintptr_t kMeleeCollisionLimitTextRva = 0x0013A6B8U;
constexpr std::uintptr_t kMeleeClientGlobalRva = 0x00168EECU;
constexpr std::uintptr_t kResetMeleeTargetReferenceRva = 0x00102B80U;
constexpr std::uintptr_t kPlayerSetDimensionsRva = 0x00031BA0U;
constexpr std::uintptr_t kClientPhysicsGlobalRva = 0x00172EC4U;
constexpr std::uintptr_t kClientPhysicsVtableExecutableRva =
    0x0014ADE0U;
constexpr std::uintptr_t kGetObjectDimensionsExecutableRva =
    0x00064530U;
constexpr std::uintptr_t kSetObjectDimensionsExecutableRva =
    0x00007FD0U;
constexpr std::uintptr_t kSetVelocityExecutableRva = 0x00007CD0U;
constexpr std::size_t kSetVelocityVtableSlot = 0x2CU / 4U;
constexpr std::uintptr_t kSetVelocityLogTextRva = 0x0014ACD4U;
constexpr LONG kPlayerCollisionVelocityEventCap = 128;
constexpr LONG kPlayerCollisionUpdateEventCap = 256;
constexpr ULONGLONG kPlayerCollisionTargetFreshnessMilliseconds = 2000U;
constexpr ULONGLONG kPlayerCollisionUpdateLogIntervalMilliseconds = 100U;
constexpr std::uintptr_t kPlayerColliderAdjacentRoutineRva =
    0x00031CF0U;
constexpr std::uintptr_t kPlayerColliderWriterRoutineRva =
    0x000344E0U;
constexpr std::uintptr_t kPlayerColliderWriterCallerBranchRva =
    0x00037FE3U;
constexpr std::uintptr_t kPlayerColliderWriterCallerReturnRva =
    0x00037FF4U;
constexpr std::uintptr_t kSetObjectDimensionsFirstLogTextRva =
    0x0014AD78U;
constexpr std::uintptr_t kSetObjectDimensionsSecondLogTextRva =
    0x0014AD5CU;
constexpr LONG kPlayerColliderWriterKnownEventCap = 64;
constexpr LONG kPlayerColliderWriterUnknownGameEventCap = 64;
constexpr LONG kPlayerColliderWriterExecutableEventCap = 64;
constexpr LONG kPlayerColliderWriterExternalEventCap = 32;
constexpr LONG kPlayerColliderWriterUnresolvedEventCap = 32;
constexpr std::size_t kPlayerObjectOffset = 0x10U;
constexpr std::size_t kPlayerRequestedDimensionsOffset = 0x1CU;
// The verified flag-0x20 manager routine can source its primary/retry
// ILTClientPhysics::SetObjectDims request from the +0x40C triple. The +0x418
// triple is populated nearby but is not read by that routine. Both runtime
// meanings remain hypotheses; the drift probe reads them only under neutral
// labels and never mutates them.
constexpr std::size_t kPlayerManager40cSourceCandidateOffset =
    0x40CU;
constexpr std::size_t kPlayerAdjacentDimensionsCandidateOffset = 0x418U;
enum class PlayerColliderPendingProcessResult {
    NotProcessed,
    AlreadyMatched,
    NativeSetDimsAttempted,
};
constexpr std::size_t kGetObjectDimensionsVtableSlot = 0x20U / 4U;
constexpr std::size_t kSetObjectDimensionsVtableSlot = 0x24U / 4U;
constexpr std::uint32_t kLithTechOk = 0U;
constexpr std::uint32_t kSetDimensionsPushObjects = 1U;
constexpr LONG kPlayerColliderScaleBasisPointsDefault = 10000;
constexpr ULONGLONG kPlayerColliderReapplyIntervalMilliseconds = 250U;
constexpr double kContinuousMeleeCollisionExpiration = 1.0e300;
constexpr std::size_t kMeleeCollisionRecordOffset = 0x18U;
constexpr std::size_t kMeleeCollisionRecordStride = 0x60U;
constexpr std::size_t kMeleeCollisionRecordCount = 2U;
constexpr std::size_t kMeleeCollisionNotifierOffset = 0x44U;
// Verified local-player weapon lifecycle. CClientWeaponMgr::GetCurrentWeapon
// at +0x2F910 returns m_pCurrentWeapon (+0x0C) only when its index (+0x08)
// is valid. CClientWeapon::SetWeaponTransform at +0x255F0 reads the primary
// engine-owned model HOBJECT from the LTObjRef field at +0x1C.
constexpr std::uintptr_t kWeaponManagerGlobalRva = 0x00168EBCU;
constexpr std::uintptr_t kGetCurrentWeaponRva = 0x0002F910U;
constexpr std::uintptr_t kSetWeaponTransformRva = 0x000255F0U;
constexpr std::uintptr_t
    kClientWeaponHandlingAnimationStimulusRva = 0x00026B80U;
constexpr std::uintptr_t
    kClientWeaponActiveAnimationStimulusRva = 0x00026C40U;
constexpr std::uintptr_t kClientWeaponBlockRva = 0x00028DD0U;
constexpr std::uintptr_t kClientWeaponVtableRva = 0x0013B46CU;
constexpr std::size_t kClientWeaponBlockVtableSlot = 0x30U / 4U;
constexpr std::uintptr_t kClientWeaponBlockStimulusNameRva =
    0x0013B088U;
constexpr std::uintptr_t kBlockCancelAnimationPropertyNameRva =
    0x0014D3F4U;
constexpr std::size_t kCurrentWeaponIndexOffset = 0x08U;
constexpr std::size_t kCurrentWeaponOffset = 0x0CU;
constexpr std::size_t kRightWeaponModelObjectOffset = 0x1CU;
// CClientWeapon::Init calls the verified weapon-display factory, then stores
// its result at +0x90. Scanner and DigitalCamera constructors allocate 0x640
// and 0x230 bytes and install the vtables below. Only their derived tails are
// sampled; the shared animated display base remains outside the trace.
constexpr std::size_t kWeaponDisplayObjectOffset = 0x90U;
constexpr std::uintptr_t kWeaponDisplayFactoryCallRva = 0x00028220U;
constexpr std::uintptr_t kWeaponDisplayFactoryRva = 0x000FB670U;
constexpr std::uintptr_t kScannerDisplayFactoryRva = 0x000FA100U;
constexpr std::uintptr_t kDigitalCameraDisplayFactoryRva = 0x000FA0E0U;
constexpr std::uintptr_t kScannerDisplayConstructorRva = 0x000F7D80U;
constexpr std::uintptr_t kDigitalCameraDisplayConstructorRva = 0x000F9890U;
constexpr std::uintptr_t kScannerDisplayVtableRva = 0x0014AB44U;
constexpr std::uintptr_t kDigitalCameraDisplayVtableRva = 0x0014AB80U;
constexpr std::uintptr_t kScannerDisplayUpdateRva = 0x000FC000U;
constexpr std::uintptr_t kDigitalCameraDisplayUpdateRva = 0x000FC640U;
constexpr std::uintptr_t kForensicCameraSocketTransformRva =
    0x000F4CB0U;
constexpr std::size_t kForensicCameraSocketTransformVtableSlot =
    0x24U / sizeof(void*);
constexpr std::uintptr_t kScannerCameraSocketTransformCallRva =
    0x000FC0C0U;
constexpr std::uintptr_t kDigitalCameraSocketTransformCallRva =
    0x000FC6CDU;
constexpr std::uintptr_t kClientShellCommandOnRva = 0x0004AD00U;
constexpr std::uintptr_t kClientShellCommandOffRva = 0x0004AD60U;
constexpr std::uintptr_t kPlayerManagerCommandOnRva = 0x000A0C30U;
constexpr std::uintptr_t kPlayerManagerCommandOffRva = 0x000A1B30U;
constexpr std::uintptr_t kPlayerManagerCommandOnVtableRva =
    0x001453DCU;
constexpr std::size_t kPlayerManagerCommandOnSlot = 9U;
constexpr std::size_t kPlayerManagerCommandOffSlot = 10U;
constexpr std::uintptr_t kClientWeaponFireRva = 0x00024D90U;
constexpr std::uintptr_t kPlayerManagerFireCallRva = 0x000A13C7U;
constexpr std::size_t kScannerDisplayStateOffset = 0x1D8U;
constexpr std::uintptr_t kForensicCollectionActionRva =
    0x000E8F00U;
constexpr std::uintptr_t kPlayerManagerCollectionActionCallRva =
    0x000A1351U;
// CTargetMgr::UpdateTarget calls +0xE98D0 to build a desktop-camera
// intersection segment. Scanner command 17 later copies that cached result.
// The two return RVAs below are the only Retail calls whose query geometry is
// redirected to the fresh Retail Camera-socket pose used by the white
// alignment arrows and live preview.
constexpr std::uintptr_t kForensicTargetAcquireRva = 0x000E98D0U;
constexpr std::uintptr_t kForensicIntersectFirstReturnRva =
    0x000E9BCEU;
constexpr std::uintptr_t kForensicIntersectSecondReturnRva =
    0x000E9BEEU;
constexpr std::uintptr_t kEngineClientGlobalRva = 0x00169EB8U;
constexpr std::size_t kEngineIntersectSegmentVtableSlot = 0x7CU / 4U;
constexpr std::uintptr_t kRetailIntersectSegmentThunkRva =
    0x000095C0U;
constexpr std::uintptr_t kRetailIntersectDispatcherGlobalRva =
    0x00168E8CU;
constexpr unsigned int kForensicCameraToolWeaponType = 0x15U;
constexpr std::int32_t kForensicItemCameraWeaponIndex = 3;
constexpr std::int32_t kForensicCollectionToolBaseWeaponIndex = 6;
constexpr std::int32_t kForensicScannerWeaponIndex = 46;
constexpr std::size_t kPlayerManagerTargetQueryOffset = 0x20U;
constexpr std::size_t kForensicTargetCacheOffset = 0xB4U;
constexpr std::size_t kForensicTargetCacheKindOffset = 0x1CU;
constexpr std::size_t kForensicTargetCacheReferenceOffset = 0x2CU;


constexpr std::size_t kDigitalCameraDisplayStateOffset = 0x208U;
constexpr std::size_t kScannerDisplayStateSize = 6U;
constexpr std::size_t kDigitalCameraDisplayStateSize = 4U;
constexpr std::size_t kForensicDisplayStateCapacity =
    sizeof(std::uint64_t);
static_assert(kScannerDisplayStateSize <=
    kForensicDisplayStateCapacity);
static_assert(kDigitalCameraDisplayStateSize <=
    kForensicDisplayStateCapacity);
constexpr std::size_t kForensicDisplayDerivedOffset = 0x1D0U;
constexpr std::size_t kScannerDisplaySize = 0x640U;
constexpr std::size_t kDigitalCameraDisplaySize = 0x230U;
constexpr std::size_t kForensicWeaponTraceOffset = 0x80U;
constexpr std::size_t kForensicWeaponTraceSize = 0x80U;
constexpr std::size_t kForensicDisplayTraceCapacity =
    kScannerDisplaySize - kForensicDisplayDerivedOffset;
constexpr std::uintptr_t kRetailGameImageSize = 0x00194000U;
// Retail console registration maps "Health" to +0xA7240. That handler reads
// the stats singleton at +0x1702F8 and calls the verified setter at +0xA6F60,
// whose current/max fields are +0x04/+0x0C. This read-only diagnostic remains
// optional and signature-gated independently from melee dispatch.
constexpr std::uintptr_t kPlayerStatsGlobalRva = 0x001702F8U;
constexpr std::uintptr_t kPlayerHealthSetterRva = 0x000A6F60U;
constexpr std::uintptr_t kPlayerHealthCommandHandlerRva = 0x000A7240U;
constexpr std::uintptr_t kPlayerHealthCommandRegistrationRva = 0x000A949FU;
constexpr std::uintptr_t kPlayerHealthNameRva = 0x00145590U;
constexpr std::size_t kPlayerCurrentHealthOffset = 0x04U;
constexpr std::size_t kPlayerMaximumHealthOffset = 0x0CU;
constexpr std::uintptr_t kClientShellVtableRva = 0x0013E714U;
constexpr std::uintptr_t kClientShellKeyUpRva = 0x0004AD90U;
constexpr std::uintptr_t kClientShellKeyDownRva = 0x0004CC00U;
constexpr std::uintptr_t kClientShellUpdateRva = 0x00051150U;
constexpr std::uintptr_t kClientShellGetInterfaceManagerRva =
    0x0004A5E0U;
constexpr std::uintptr_t kInterfaceManagerVtableRva = 0x00142594U;
constexpr std::uintptr_t kInterfaceManagerSingletonRva = 0x0016F388U;
constexpr std::uintptr_t kClientShellKeyUpStateReadRva = 0x0004ADB5U;
constexpr std::size_t kClientShellKeyUpSlot = 16U;
constexpr std::size_t kClientShellKeyDownSlot = 17U;
constexpr std::size_t kClientShellUpdateSlot = 3U;
constexpr std::size_t kClientShellGetInterfaceManagerSlot = 30U;
constexpr std::size_t kInterfaceManagerStateOffset = 8U;
constexpr ULONGLONG kInputFreshnessMilliseconds = 250;
constexpr ULONGLONG kForensicCameraSocketFreshnessMilliseconds =
    250;
constexpr LONG kUnknownRetailGameState = -1;
constexpr LONG kUnpublishedRetailGameState = -2;

constexpr unsigned char kGetBindingValuePrefix[] = {
    0x51, 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x8B,
    0x06, 0x83, 0xF8, 0xFF, 0x74, 0x3A};
constexpr unsigned char kIsDeviceReadySequence[] = {
    0x8B, 0x11, 0x57, 0x8D, 0x7C, 0x24,
    0x10, 0x57, 0x50, 0xFF, 0x52, 0x18};
constexpr unsigned char kGetDeviceObjectValueSequence[] = {
    0x8B, 0x01, 0x8D, 0x54, 0x24, 0x04, 0x52, 0x8B,
    0x56, 0x04, 0x52, 0x8B, 0x16, 0x52, 0xFF, 0x50, 0x2C};
constexpr unsigned char kDefaultReturnSequence[] = {
    0xD9, 0x46, 0x0C, 0x5E, 0x59, 0xC2, 0x04, 0x00};
constexpr unsigned char kGetExtremalCommandValuePrefix[] = {
    0x51, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x04,
    0x3B, 0x77, 0x08, 0xC7, 0x44, 0x24, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x74, 0x4E, 0x53, 0x8B, 0x5C,
    0x24, 0x14};
constexpr unsigned char kGetExtremalCommandValueLoop[] = {
    0x39, 0x5E, 0x08, 0x75, 0x33, 0x8A, 0x47, 0x4C,
    0x84, 0xC0, 0x75, 0x05, 0xD9, 0x46, 0x0C};
constexpr unsigned char kGetExtremalBindingCall[] = {
    0x56, 0x8B, 0xCF, 0xE8, 0xB7, 0xFC, 0xFF, 0xFF};
constexpr unsigned char kGetExtremalBindingStride[] = {
    0x8B, 0x47, 0x08, 0x83, 0xC6, 0x3C, 0x3B, 0xF0,
    0x75, 0xBE};
constexpr unsigned char kGetExtremalCommandValueTail[] = {
    0xD9, 0x44, 0x24, 0x08, 0x5F, 0x5E, 0x59, 0xC2,
    0x04, 0x00};
constexpr unsigned char kGetFireVectorsPrefix[] = {
    0x83, 0xEC, 0x58};
constexpr unsigned char kGetFireVectorsStackInit[] = {
    0x53, 0x55, 0xC7, 0x44, 0x24, 0x30,
    0x00, 0x00, 0x00, 0x00};
constexpr unsigned char kGetFireVectorsCameraProbe[] = {
    0x8B, 0xE9, 0x8B, 0x48, 0x28,
    0x8B, 0x81, 0x18, 0x01, 0x00, 0x00,
    0x85, 0xC0, 0x56, 0x57};
constexpr unsigned char kMeleeEnableCollisionsPrefix[] = {
    0x81, 0xEC, 0x6C, 0x01, 0x00, 0x00, 0xA1};
constexpr unsigned char kMeleeEnableCollisionsBodyPrefix[] = {
    0x8B, 0x50, 0x10, 0x53, 0x55, 0x8B, 0xE9};
constexpr unsigned char kMeleeNativeFloatReadCall[] = {
    0xFF, 0x93, 0x80, 0x00, 0x00, 0x00};
constexpr unsigned char kMeleeNativeLengthUpStore[] = {
    0xD9, 0x5C, 0x24, 0x24};
constexpr unsigned char kMeleeNativeLengthDownStore[] = {
    0xD9, 0x5C, 0x24, 0x1C};
constexpr unsigned char kMeleeNativeRadiusStore[] = {
    0xD9, 0x5C, 0x24, 0x14};
constexpr unsigned char kMasterDatabaseFloatReaderBody[] = {
    0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x23,
    0x80, 0x78, 0x0A, 0x02, 0x75, 0x1D, 0x0F, 0xB7,
    0x50, 0x08, 0x8B, 0x4C, 0x24, 0x08, 0x3B, 0xCA,
    0x73, 0x11, 0x8B, 0x40, 0x0C, 0x8B, 0x0C, 0x88,
    0x89, 0x4C, 0x24, 0x04, 0xD9, 0x44, 0x24, 0x04,
    0xC2, 0x0C, 0x00, 0xD9, 0x44, 0x24, 0x0C, 0xC2,
    0x0C, 0x00};
constexpr unsigned char kMeleeCollisionLimitTextReferencePrefix[] = {
    0x8B, 0x94, 0x24, 0x88, 0x01, 0x00, 0x00, 0x52, 0x68};
constexpr std::size_t kMeleeCollisionRecordSelectionOffset = 0x0E3U;
constexpr unsigned char kMeleeCollisionRecordSelection[] = {
    0x33, 0xC0, 0x8D, 0x4D, 0x5C, 0x83, 0x39, 0x00,
    0x74, 0x0B, 0x40, 0x83, 0xC1, 0x60, 0x83, 0xF8,
    0x02, 0x72, 0xF2};
constexpr std::size_t kMeleeBlockingArgumentBranchOffset = 0x3BAU;
constexpr unsigned char kMeleeBlockingArgumentBranch[] = {
    0x8A, 0x84, 0x24, 0x90, 0x01, 0x00, 0x00, 0x84,
    0xC0, 0x74, 0x07, 0xB8, 0x10, 0x00, 0x00, 0x00};
constexpr std::size_t kMeleeBlockingNotifierBranchOffset = 0x4BCU;
constexpr unsigned char kMeleeBlockingNotifierBranch[] = {
    0x8A, 0x9C, 0x24, 0x90, 0x01, 0x00, 0x00, 0x84,
    0xDB, 0x88, 0x46, 0x28, 0x75, 0x26, 0x8B, 0x4E,
    0x40};
constexpr unsigned char kMeleeUpdateCollisionPrefix[] = {
    0x83, 0xEC, 0x3C, 0x56, 0x8B, 0x74, 0x24, 0x44,
    0x8B, 0x46, 0x40, 0x85, 0xC0, 0x57, 0x8B, 0xF9};
constexpr unsigned char kMeleeUpdateCollisionNodeQuery[] = {
    0x8B, 0x46, 0x38, 0x8B, 0x0D};
constexpr unsigned char kMeleeUpdateCollisionSetTransform[] = {
    0x50, 0x8B, 0xCF, 0xFF, 0x93, 0xC4, 0x00, 0x00, 0x00};
constexpr unsigned char kBuildRigidTransformPrefix[] = {
    0x8B, 0xC1, 0xC7, 0x40, 0x18, 0x00, 0x00, 0x80,
    0x3F, 0x33, 0xC9, 0x89, 0x48, 0x0C, 0x89, 0x48,
    0x10, 0x89, 0x48, 0x14};
constexpr unsigned char kBuildRigidTransformTail[] = {
    0x8B, 0x49, 0x0C, 0x89, 0x48, 0x18, 0xC2, 0x08, 0x00};
constexpr unsigned char kMeleeImpactDispatchPrefix[] = {
    0x8B, 0x44, 0x24, 0x0C, 0x83, 0xEC, 0x4C, 0x83,
    0xF8, 0x10, 0x53, 0x56, 0x8B, 0xD9, 0x75, 0x50};
constexpr unsigned char kMeleeImpactDispatchCallsitePrefix[] = {
    0x8B, 0x46, 0x24, 0x8B, 0x56, 0x38, 0x50, 0x33,
    0xC9, 0x8A, 0x4E, 0x28, 0x57, 0x8D, 0x44, 0x24,
    0x44, 0x51, 0x52};
constexpr unsigned char kMeleeTargetReferenceVectorSetup[] = {
    0x8D, 0x54, 0x24, 0x2C, 0x8D, 0x7E, 0x48, 0x52, 0x8B, 0xCF};
constexpr unsigned char kMeleeTargetReferenceVectorPushPrefix[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x56, 0x04, 0x85, 0xD2,
    0x75, 0x04, 0x33, 0xC9, 0xEB, 0x08, 0x8B, 0x4E,
    0x08, 0x2B, 0xCA, 0xC1, 0xF9, 0x04, 0x85, 0xD2,
    0x74, 0x31, 0x8B, 0x46, 0x0C, 0x2B, 0xC2, 0xC1,
    0xF8, 0x04};
constexpr unsigned char kResetMeleeTargetReferenceBody[] = {
    0x8B, 0x51, 0x04, 0x8D, 0x41, 0x04, 0x56, 0x8B,
    0x70, 0x04, 0x89, 0x72, 0x04, 0x8B, 0x30, 0x8B,
    0x50, 0x04, 0x89, 0x32, 0x89, 0x40, 0x04, 0x89,
    0x00, 0xC7, 0x41, 0x0C, 0x00, 0x00, 0x00, 0x00,
    0x5E, 0xC3};

constexpr unsigned char kGetCurrentWeaponBody[] = {
    0x83, 0x79, 0x08, 0xFF, 0x75, 0x03, 0x33,
    0xC0, 0xC3, 0x8B, 0x41, 0x0C, 0xC3};
constexpr unsigned char kSetWeaponTransformPrefix[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x1C, 0x85,
    0xC0, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x74, 0x0D};
constexpr unsigned char kSetWeaponTransformSecondModel[] = {
    0x8B, 0x86, 0xEC, 0x00, 0x00, 0x00,
    0x85, 0xC0, 0x74, 0x0D};
constexpr unsigned char
    kClientWeaponHandlingAnimationStimulusPrefix[] = {
        0x53, 0x56, 0x57, 0x8B, 0xF9,
        0x8B, 0xB7, 0x70, 0x03, 0x00, 0x00,
        0x3B, 0xB7, 0x74, 0x03, 0x00, 0x00,
        0x74, 0x1D};
constexpr unsigned char
    kClientWeaponHandlingAnimationStimulusCall[] = {
        0x8B, 0x0E, 0x53, 0xE8,
        0x81, 0x4F, 0x01, 0x00,
        0x84, 0xC0, 0x75, 0x15};
constexpr unsigned char kClientWeaponActiveAnimationStimulusPrefix[] = {
    0x56, 0x57, 0x8B, 0xF9,
    0x8B, 0xB7, 0x70, 0x03, 0x00, 0x00,
    0x3B, 0xB7, 0x74, 0x03, 0x00, 0x00,
    0x74, 0x18};
constexpr unsigned char kClientWeaponBlockPrefix[] = {
    0x57, 0x8B, 0xF9,
    0x8A, 0x87, 0x53, 0x02, 0x00, 0x00,
    0x84, 0xC0, 0x74, 0x06,
    0x32, 0xC0, 0x5F, 0xC2, 0x04, 0x00};
constexpr unsigned char kClientWeaponBlockStimulusDispatch[] = {
    0x8B, 0x0E, 0x68};
constexpr unsigned char kClientWeaponBlockActiveTail[] = {
    0x8B, 0xCF, 0xE8};
constexpr unsigned char kPlayerManagerCommandOffPrefix[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x30,
    0x85, 0xC0, 0x0F, 0x84, 0x8F, 0x00, 0x00, 0x00};
constexpr unsigned char kPlayerManagerCommandOffUnhandledTail[] = {
    0x5F, 0x32, 0xC0, 0x5E, 0xC2, 0x04, 0x00};
constexpr unsigned char kClientShellKeyUpPrefix[] = {
    0x83, 0xEC, 0x28, 0x55, 0x8B, 0x6C, 0x24, 0x30,
    0x83, 0xFD, 0x77, 0x57, 0x8B, 0xF9, 0x0F, 0x84};
constexpr unsigned char kClientShellKeyDownPrefix[] = {
    0x81, 0xEC, 0x08, 0x02, 0x00, 0x00, 0x56, 0x57,
    0x8B, 0xBC, 0x24, 0x14, 0x02, 0x00, 0x00, 0x81,
    0xFF, 0xFF, 0x00, 0x00, 0x00};
constexpr unsigned char kClientShellUpdatePrefix[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC,
    0x1C};
constexpr unsigned char kClientShellUpdateBody[] = {
    0x55, 0x56, 0x57, 0x8B, 0xF1, 0xFF, 0x50, 0x4C,
    0xDC, 0x9E, 0x10, 0x4F, 0x00, 0x00, 0xDF, 0xE0,
    0xF6, 0xC4, 0x41, 0x75, 0x26};
constexpr unsigned char kClientShellBindingUpdateSequence[] = {
    0xE8, 0x42, 0x98, 0xFB, 0xFF, 0x8B, 0xC8, 0xE8,
    0xBB, 0x99, 0xFB, 0xFF};
constexpr unsigned char kClientShellGetInterfaceManagerBody[] = {
    0x8D, 0x41, 0x08, 0xC3};
constexpr unsigned char kClientShellKeyUpStateReadTail[] = {
    0x56, 0x8B, 0x70, 0x08};
constexpr unsigned char kClientShellKeyUpPlayingCompare[] = {
    0x83, 0xFE, 0x01};
constexpr unsigned char kClientShellKeyUpScreenCompare[] = {
    0x83, 0xFE, 0x06};
constexpr unsigned char kClientShellKeyUpMenuCompare[] = {
    0x83, 0xFE, 0x05};
constexpr unsigned char kPlayerHealthSetterPrefix[] = {
    0x8B, 0x41, 0x0C, 0x8B, 0x54, 0x24, 0x04, 0x3B,
    0xD0, 0x76, 0x02, 0x8B, 0xD0, 0x8B, 0x41, 0x04,
    0x3B, 0xD0, 0x74, 0x24};
constexpr unsigned char kPlayerHealthHandlerBody[] = {
    0x85, 0xC0, 0x74, 0x29, 0x83, 0x7C, 0x24, 0x04,
    0x01, 0x7C, 0x22};
constexpr unsigned char kPlayerHealthRegistrationTail[] = {
    0xFF, 0x90, 0x10, 0x01, 0x00, 0x00};
constexpr ULONGLONG kPlayerVitalsSampleIntervalMilliseconds = 100U;
constexpr unsigned char kPlayerSetDimensionsPrefix[] = {
    0x83, 0xEC, 0x64, 0x56, 0x57, 0x8B, 0xF1, 0x8B, 0x0D};
constexpr unsigned char kPlayerSetDimensionsAfterPhysicsGlobal[] = {
    0x8B, 0x01, 0x8D, 0x54, 0x24, 0x2C, 0x52,
    0x8B, 0x56, 0x10, 0x52, 0xFF, 0x50, 0x20};
constexpr unsigned char kPlayerSetDimensionsDesiredRead[] = {
    0x8D, 0x46, 0x1C, 0x8B, 0x08, 0x8B, 0x50,
    0x04, 0x8B, 0x40, 0x08, 0x6A, 0x01};
constexpr unsigned char kPlayerSetDimensionsNativeCall[] = {
    0x8B, 0x11, 0x50, 0xFF, 0x52, 0x24,
    0x85, 0xC0, 0x74, 0x32};
constexpr unsigned char kPlayerSetDimensionsFallbackAfterPhysicsGlobal[] = {
    0x8B, 0x01, 0x6A, 0x00, 0x8D, 0x54, 0x24, 0x30,
    0x52, 0x8B, 0x56, 0x10, 0x52, 0xFF, 0x50, 0x24};
constexpr unsigned char kPlayerSetDimensionsReturnTail[] = {
    0x5F, 0x5E, 0x83, 0xC4, 0x64, 0xC2, 0x04, 0x00};

constexpr unsigned char kGetObjectDimensionsExecutablePrefix[] = {
    0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0x74, 0x21,
    0x8B, 0x44, 0x24, 0x08, 0x85, 0xC0, 0x74, 0x19,
    0x8B, 0x51, 0x78};
constexpr unsigned char kSetObjectDimensionsExecutablePrefix[] = {
    0x83, 0xEC, 0x68, 0x56, 0x8B, 0x74, 0x24, 0x70,
    0x85, 0xF6, 0x57, 0x0F, 0x84, 0xF7, 0x00, 0x00,
    0x00, 0x8B, 0x7C, 0x24, 0x78, 0x85, 0xFF, 0x0F,
    0x84, 0xEB, 0x00, 0x00, 0x00};
constexpr unsigned char kSetObjectDimensionsSuccessTail[] = {
    0x5F, 0x33, 0xC0, 0x5E, 0x83, 0xC4, 0x68, 0xC2,
    0x0C, 0x00};
constexpr unsigned char kSetObjectDimensionsAdjustedTail[] = {
    0x5F, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x5E, 0x83,
    0xC4, 0x68, 0xC2, 0x0C, 0x00};
constexpr unsigned char kSetObjectDimensionsInvalidTail[] = {
    0x5F, 0xB8, 0x3C, 0x00, 0x00, 0x00, 0x5E, 0x83,
    0xC4, 0x68, 0xC2, 0x0C, 0x00};
constexpr unsigned char kPlayerColliderAdjacentPrefix[] = {
    0x83, 0xEC, 0x24, 0x56, 0x8B, 0xF1, 0x8B, 0x0D};
constexpr unsigned char kPlayerColliderAdjacentAfterPhysicsGlobal[] = {
    0x8B, 0x01, 0x8D, 0x54, 0x24, 0x04, 0x52,
    0x8B, 0x56, 0x10, 0x52, 0xFF, 0x50, 0x20};
constexpr unsigned char kPlayerColliderAdjacentFirstRequest[] = {
    0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x8E, 0x1C, 0x04,
    0x00, 0x00, 0x8B, 0x54, 0x24, 0x14, 0x6A, 0x01,
    0x89, 0x44, 0x24, 0x1C, 0x8B, 0x46, 0x10, 0x89,
    0x4C, 0x24, 0x20, 0x8B, 0x0D};
constexpr unsigned char kPlayerColliderAdjacentFirstCallSuffix[] = {
    0x8D, 0x7C, 0x24, 0x1C, 0x57, 0x89, 0x54, 0x24,
    0x28, 0x8B, 0x11, 0x50, 0xFF, 0x52, 0x24};
constexpr unsigned char kPlayerColliderAdjacentRetrySuffix[] = {
    0x8B, 0xD8, 0x8B, 0x01, 0x6A, 0x00, 0x8D, 0x54,
    0x24, 0x10, 0x52, 0x8B, 0x56, 0x10, 0xF7, 0xDB,
    0x1A, 0xDB, 0x52, 0xFE, 0xC3, 0xFF, 0x50, 0x24};
constexpr unsigned char kPlayerColliderWriterEntry[] = {
    0x83, 0xEC, 0x58, 0x53, 0x56, 0x57, 0x8B, 0x7C,
    0x24, 0x68, 0x33, 0xDB, 0x3B, 0xFB, 0x8B, 0xF1,
    0x0F, 0x84, 0x17, 0x03, 0x00, 0x00, 0x8B, 0x07};
constexpr unsigned char kPlayerColliderWriterTail[] = {
    0x5F, 0x5E, 0x5B, 0x83, 0xC4, 0x58, 0xC2, 0x04,
    0x00};
constexpr unsigned char kPlayerColliderWriterCallerBranch[] = {
    0x53, 0x8A, 0x5C, 0x24, 0x34, 0xF6, 0xC3, 0x20,
    0x55, 0x74, 0x08, 0x56, 0xE8, 0xEC, 0xC4, 0xFF,
    0xFF, 0xEB, 0x2D};
constexpr unsigned char kPlayerColliderWriterLiteralSuffix[] = {
    0x53, 0x8D, 0x54, 0x24, 0x4C, 0x52, 0x8B, 0x56,
    0x10, 0xC7, 0x44, 0x24, 0x50, 0x00, 0x00, 0x00,
    0x3F, 0xC7, 0x44, 0x24, 0x54, 0x00, 0x00, 0x00,
    0x3F, 0xC7, 0x44, 0x24, 0x58, 0x00, 0x00, 0x00,
    0x3F, 0x8B, 0x01, 0x52, 0xFF, 0x50, 0x24};
constexpr unsigned char kPlayerColliderWriterPrimarySuffix[] = {
    0x8B, 0x11, 0xF6, 0xD8, 0x1A, 0xC0, 0xFE, 0xC0,
    0x88, 0x44, 0x24, 0x68, 0x8B, 0x46, 0x10, 0x89,
    0x5C, 0x24, 0x68, 0xBB, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x95, 0xC3, 0x8D, 0x7C, 0x24, 0x18, 0x53,
    0x57, 0x50, 0xFF, 0x52, 0x24};
constexpr unsigned char kPlayerColliderWriterRetrySuffix[] = {
    0x8B, 0x01, 0x53, 0x8B, 0xD7, 0x52, 0x8B, 0x56,
    0x10, 0x52, 0xFF, 0x50, 0x24};
constexpr unsigned char kSetVelocityPrefix[] = {
    0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x75, 0x39,
    0x6A, 0x3C, 0xE8, 0xC1, 0x6B, 0x07, 0x00, 0xA1};
constexpr unsigned char kSetVelocityTail[] = {
    0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x11,
    0x89, 0x90, 0xAC, 0x00, 0x00, 0x00,
    0x8B, 0x51, 0x04, 0x89, 0x90, 0xB0, 0x00, 0x00, 0x00,
    0x8B, 0x49, 0x08, 0x89, 0x88, 0xB4, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x5A, 0x40, 0x33, 0xC0, 0xC2, 0x08, 0x00};

SRWLOCK g_bindingLock = SRWLOCK_INIT;
SRWLOCK g_playerColliderHookInstallLock = SRWLOCK_INIT;
PlayerSetDimensionsFunction g_originalPlayerSetDimensions = nullptr;
SetObjectDimensionsFunction
    g_originalPlayerSetObjectDimensionsTrace = nullptr;
SetVelocityFunction g_originalPlayerCollisionSetVelocity = nullptr;
GetBindingValueFunction g_originalGetBindingValue = nullptr;
GetExtremalCommandValueFunction g_originalGetExtremalCommandValue = nullptr;
GetFireVectorsFunction g_originalGetFireVectors = nullptr;
MeleeEnableCollisionsFunction g_originalMeleeEnableCollisions = nullptr;
MeleeUpdateCollisionFunction g_originalMeleeUpdateCollision = nullptr;
BuildRigidTransformFunction g_originalBuildRigidTransform = nullptr;
MeleeImpactDispatchFunction g_originalMeleeImpactDispatch = nullptr;
DatabaseFloatReaderFunction g_originalMasterDatabaseFloatReader = nullptr;
ResetMeleeTargetReferenceFunction g_resetMeleeTargetReference = nullptr;
GetInputStateFunction g_getInputState = nullptr;
SubmitHapticRequestFunction g_submitHapticRequest = nullptr;
SetMenuActiveFunction g_setMenuActive = nullptr;
ClientShellUpdateFunction g_originalClientShellUpdate = nullptr;
ClientShellKeyUpFunction g_clientShellKeyUp = nullptr;
ClientShellKeyDownFunction g_clientShellKeyDown = nullptr;
ClientShellCommandFunction g_originalForensicCommandOn = nullptr;
ClientShellCommandFunction g_originalForensicCommandOff = nullptr;
ForensicDisplayUpdateFunction g_originalScannerDisplayUpdate = nullptr;
ForensicDisplayUpdateFunction g_originalDigitalCameraDisplayUpdate = nullptr;
ForensicCameraSocketTransformFunction
    g_originalForensicCameraSocketTransform = nullptr;
PlayerManagerCommandFunction
    g_originalForensicPlayerManagerCommandOn = nullptr;
ClientWeaponFireFunction
    g_originalForensicClientWeaponFire = nullptr;
RendererProbeLogFunction g_log = nullptr;
void* g_bindingValueHookTarget = nullptr;
void* g_playerSetDimensionsHookTarget = nullptr;
void* g_playerSetObjectDimensionsTraceHookTarget = nullptr;
void* g_playerCollisionSetVelocityHookTarget = nullptr;
HMODULE g_playerColliderTraceExecutable = nullptr;
ForensicCollectionActionFunction
    g_originalForensicCollectionAction = nullptr;
EngineIntersectSegmentFunction
    g_originalForensicIntersectSegment = nullptr;
void* g_turningHookTarget = nullptr;
void* g_fireVectorsHookTarget = nullptr;
void* g_meleeEnableCollisionsHookTarget = nullptr;
void* g_meleeUpdateCollisionHookTarget = nullptr;
void* g_buildRigidTransformHookTarget = nullptr;
void* g_meleeImpactDispatchHookTarget = nullptr;
void* g_masterDatabaseFloatReaderHookTarget = nullptr;
void* g_menuHookTarget = nullptr;
void* g_forensicCommandOnHookTarget = nullptr;
void* g_forensicCommandOffHookTarget = nullptr;
void* g_scannerDisplayUpdateHookTarget = nullptr;
void* g_digitalCameraDisplayUpdateHookTarget = nullptr;
void* g_forensicCameraSocketTransformHookTarget = nullptr;
void* g_forensicPlayerManagerCommandOnHookTarget = nullptr;
void* g_forensicClientWeaponFireHookTarget = nullptr;
void* g_clientShell = nullptr;
void* g_interfaceManager = nullptr;
volatile LONG g_menuUpdateObserved = 0;
void* g_forensicCollectionActionHookTarget = nullptr;
void* g_forensicIntersectSegmentHookTarget = nullptr;
volatile LONG g_lastPublishedRetailGameState =
    kUnpublishedRetailGameState;
volatile LONG g_menuRenderPublishFailed = 0;
volatile LONG g_menuControlsEnabled = 0;
std::uint64_t g_lastSampleId = 0;
ULONGLONG g_lastSampleTick = 0;
std::uint32_t g_lastDirectionMask = 0;
int g_lastTurnDirection = 0;
volatile LONG g_locomotionEnabled = 0;
volatile LONG g_playerColliderScaleBasisPoints =
    kPlayerColliderScaleBasisPointsDefault;
volatile LONG g_playerColliderReapplyPending = 1;
volatile LONG g_interactionEnabled = 0;
volatile LONG g_coreActionsEnabled = 0;
volatile LONG g_lastInteractionActive = 0;
volatile LONG g_lastCoreActionActive[8]{};
alignas(8) volatile LONG64 g_hapticRequestId = 0;
volatile LONG g_hapticsEnabled = 0;
volatile LONG g_hapticFailureReported = 0;
volatile LONG g_headAimInputEnabled = 0;
volatile LONG g_mouseLookSuppressionLogged = 0;
volatile LONG g_controllerFireAimLogged = 0;
void* g_firearmMuzzleModel = nullptr;
const unsigned char* g_firearmMuzzleModelClientGlobalAddress = nullptr;
GetModelSocketFunction g_getFirearmModelSocket = nullptr;
GetModelSocketTransformFunction g_getFirearmModelSocketTransform = nullptr;
volatile LONG g_handgunMuzzleAimCalls = 0;
volatile LONG g_handgunMuzzleAimApplied = 0;
volatile LONG g_handgunMuzzleAimFallbacks = 0;
volatile LONG g_handgunMuzzleAimActiveLogged = 0;
volatile LONG g_aimPathProbeEnabled = 0;
volatile LONG g_aimPathFireVectorCalls = 0;
volatile LONG g_aimPathMeleeCalls = 0;
volatile LONG g_aimPathMeleeUpdateCalls = 0;
volatile LONG g_aimPathMeleeTransformCalls = 0;
volatile LONG g_aimPathMeleeImpactCalls = 0;
volatile LONG g_controllerMeleeAimEnabled = 0;
volatile LONG g_controllerMeleeAimLogged = 0;
SRWLOCK g_physicalMeleeLock = SRWLOCK_INIT;
SRWLOCK g_physicalMeleeBlockPoseLock = SRWLOCK_INIT;
PhysicalMeleeKinematicsState g_physicalMeleeState{};
PhysicalMeleeKinematicsState g_physicalMeleeSwingKinematicsState{};
PhysicalMeleeFrame g_physicalMeleeFrame{};
PhysicalMeleeProfile g_physicalMeleeProfile{};
std::int32_t g_physicalMeleeProfileWeaponIndex = -1;
RetailWeaponIdentitySnapshot g_equippedWeaponIdentity{};
PhysicalMeleeContactState g_physicalMeleeContactState{};
PhysicalMeleeSwingAttackState g_physicalMeleeSwingAttackState{};
PhysicalMeleeAutomaticSeedState g_physicalMeleeAutomaticSeedState{};
PhysicalMeleeBlockPoseState g_physicalMeleeBlockPoseState{};
PhysicalMeleeBlockPoseResult g_physicalMeleeBlockPoseResult{};
PhysicalMeleeBlockNativeLifecycleState
    g_physicalMeleeBlockNativeLifecycleState{};
std::int32_t g_physicalMeleeBlockPoseWeaponIndex = -1;
bool g_physicalMeleeBlockPoseTrackingFresh = false;
std::uint64_t g_physicalMeleeSampleId = 0;
ULONGLONG g_physicalMeleeSampleTick = 0;
std::uint64_t g_physicalMeleeSwingSampleId = 0;
ULONGLONG g_physicalMeleeSwingSampleTick = 0;
float g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
volatile LONG g_physicalMeleeProbeEnabled = 0;
volatile LONG g_physicalMeleeSampleCalls = 0;
volatile LONG g_physicalMeleeDamageQualified = 0;
volatile LONG g_physicalMeleeSwingAttackTriggered = 0;
volatile LONG g_physicalMeleeAutomaticSeedEnabled = 0;
volatile LONG g_physicalMeleeAutomaticSeedStarted = 0;
volatile LONG g_physicalMeleeAutomaticSeedConfirmed = 0;
volatile LONG g_physicalMeleeAutomaticSeedReady = 0;
volatile LONG g_physicalMeleeAutomaticSeedFailed = 0;
volatile LONG g_physicalMeleeAutomaticSeedImpactsBlocked = 0;
volatile LONG g_physicalMeleeBlockPoseActivations = 0;
volatile LONG g_physicalMeleeBlockNativeReleaseEnabled = 0;
volatile LONG g_physicalMeleeBlockNativeReleaseQueued = 0;
volatile LONG g_physicalMeleeBlockNativeReleaseDispatched = 0;
volatile LONG g_physicalMeleeBlockNativeReleaseSkipped = 0;
volatile LONG g_physicalMeleeWallProxyEnabled = 0;
volatile LONG g_physicalMeleeColliderDebugEnabled = 0;
volatile LONG g_physicalMeleeVisualProxyEnabled = 0;
volatile LONG g_physicalMeleeContactDamageEnabled = 0;
volatile LONG g_physicalMeleeDamageDispatched = 0;
volatile LONG g_physicalMeleeContinuousCollisionHeld = 0;
volatile LONG g_physicalMeleeContinuousCollisionReleased = 0;
volatile LONG g_physicalMeleeCollisionRoleClassified = 0;
volatile LONG g_physicalMeleeCollisionRoleUnclassified = 0;
volatile LONG g_physicalMeleeRetailLatchReleased = 0;
volatile LONG g_physicalMeleeRetailLatchReleaseFailedLogged = 0;
volatile LONG g_physicalMeleeWallProxyAppliedLogged = 0;
volatile LONG g_physicalMeleeContactAccepted = 0;
volatile LONG g_physicalMeleeContactRearmed = 0;
volatile LONG g_physicalMeleeContactInvalidSampleHeld = 0;
volatile LONG g_physicalMeleeNativeCapsuleOverrides = 0;
volatile LONG g_physicalMeleeBlockWindowSamples = 0;
std::uintptr_t g_physicalMeleePlayerWeaponModelObject = 0;
std::uintptr_t g_physicalMeleePlayerCollisionObject = 0;
ULONGLONG g_physicalMeleePlayerCollisionTick = 0;
std::uintptr_t g_physicalMeleePlayerBlockCollisionObject = 0;
ULONGLONG g_physicalMeleePlayerBlockCollisionTick = 0;
float g_physicalMeleeLastRetailBlockWindowSeconds = 0.0F;
float g_physicalMeleeLastAppliedBlockWindowSeconds = 0.0F;
bool g_physicalMeleeLastBlockWindowOverrideApplied = false;
void* g_physicalMeleePlayerCollisionController = nullptr;
void* g_physicalMeleeAutomaticSeedImpactController = nullptr;
ULONGLONG g_physicalMeleeAutomaticSeedImpactBlockUntil = 0U;
std::array<PhysicalMeleeCollisionClassification,
           kMeleeCollisionRecordCount>
    g_physicalMeleePlayerCollisionClassifications{};
thread_local bool g_physicalMeleePlayerCollisionUpdate = false;
thread_local PhysicalMeleeCollisionRole
    g_physicalMeleeActiveCollisionRole =
        PhysicalMeleeCollisionRole::Unknown;
thread_local PhysicalMeleeNativeCapsuleShape
    g_physicalMeleeNativeCapsuleOverride{};
thread_local std::uint32_t g_physicalMeleeNativeCapsuleReadMask = 0U;
thread_local void* g_physicalMeleeActiveCollisionRecord = nullptr;
volatile LONG g_weaponCatalogProbeState = 0;
unsigned char* g_gameClientBase = nullptr;
SRWLOCK g_playerColliderLock = SRWLOCK_INIT;
PlayerColliderTelemetry g_playerColliderTelemetry{};
PlayerColliderDimensions g_playerColliderRetailBaseline{};
PlayerColliderDimensions g_playerColliderLastObservedDimensions{};
std::uintptr_t g_playerColliderBaselineObject = 0U;
std::uintptr_t g_playerColliderLastObservedObject = 0U;
bool g_playerColliderLastObservedDimensionsValid = false;
ULONGLONG g_playerColliderNextReapplyTick = 0U;
volatile LONG g_playerColliderHandoffEvents = 0;
volatile LONG g_playerColliderDirectApplyEvents = 0;
volatile LONG g_playerColliderDimensionObservationEvents = 0;
volatile LONG g_playerColliderPostPendingObservationEvents = 0;
volatile LONG g_playerColliderWriterKnownEvents = 0;
volatile LONG g_playerColliderWriterUnknownGameEvents = 0;
volatile LONG g_playerColliderWriterExecutableEvents = 0;
volatile LONG g_playerColliderWriterExternalEvents = 0;
volatile LONG g_playerColliderWriterUnresolvedEvents = 0;
alignas(8) volatile LONG64 g_playerColliderWriterNextSequence = 0;
volatile LONG g_playerColliderManagerHookOperational = 0;
volatile LONG g_playerColliderManagerInstallPoisoned = 0;
volatile LONG g_playerColliderWriterHookOperational = 0;
volatile LONG g_playerColliderWriterInstallPoisoned = 0;
volatile LONG g_enemyColliderObservationEvents = 0;
SRWLOCK g_playerCollisionXrayLock = SRWLOCK_INIT;
PlayerCollisionXraySnapshot g_playerCollisionXraySnapshot{};
PlayerCollisionXraySnapshot g_playerCollisionXrayPreUpdate{};
std::uintptr_t g_playerCollisionXrayTargetObject = 0U;
PlayerCollisionDiagnosticPoint g_playerCollisionXrayContactPoint{};
bool g_playerCollisionXrayContactValid = false;
ULONGLONG g_playerCollisionXrayTargetTick = 0U;
ULONGLONG g_playerCollisionXrayLastUpdateLogTick = 0U;
volatile LONG g_playerCollisionXrayEnabled = 0;
volatile LONG g_playerCollisionVelocityHookOperational = 0;
volatile LONG g_playerCollisionVelocityInstallPoisoned = 0;
volatile LONG g_playerCollisionVelocityEvents = 0;
volatile LONG g_playerCollisionUpdateEvents = 0;
alignas(8) volatile LONG64 g_playerCollisionTimelineSequence = 0;

volatile LONG g_combatPlayerVitalsEnabled = 0;
volatile LONG g_combatPlayerVitalsUnavailableLogged = 0;
ULONGLONG g_combatPlayerVitalsLastSampleTick = 0U;
std::uint32_t g_combatPlayerVitalsLastHealth = 0U;
std::uint32_t g_combatPlayerVitalsLastMaximum = 0U;
bool g_combatPlayerVitalsHaveSample = false;
volatile LONG g_forensicCameraSocketRayEnabled = 0;
volatile LONG g_forensicCameraSocketRayCalls = 0;
volatile LONG g_forensicCameraSocketRayOverrides = 0;
volatile LONG g_forensicCameraSocketRayFallbacks = 0;
volatile LONG g_forensicCameraSocketRayLastResult[2] = {-1, -1};

volatile LONG g_forensicMemoryProbeEnabled = 0;
PVOID volatile g_forensicScannerDisplay = nullptr;
PVOID volatile g_forensicDigitalCameraDisplay = nullptr;
alignas(8) volatile LONG64 g_forensicScannerState = -1;
alignas(8) volatile LONG64 g_forensicDigitalCameraState = -1;
volatile LONG g_forensicScannerStateEvents = 0;
volatile LONG g_forensicDigitalCameraStateEvents = 0;
volatile LONG g_forensicMemoryCommandActive[3]{};
alignas(8) volatile LONG64 g_forensicMemoryNextTraceId = 0;
SRWLOCK g_forensicMemoryLock = SRWLOCK_INIT;
enum class ForensicDisplayKind : std::uint32_t {
    none = 0U,
    scanner = 1U,
    digitalCamera = 2U,
    other = 3U
};

ForensicDisplayKind ResolveForensicDisplayKind(
    std::uintptr_t vtable) noexcept;

struct ForensicCameraSocketPoseSnapshot {
    ForensicRayVector origin{};
    fearvr::TrackingQuaternion rotation{};
    ForensicRayVector forward{};
    std::uintptr_t display{0U};
    ForensicDisplayKind displayKind{ForensicDisplayKind::none};
    std::uint64_t sequence{0U};
    ULONGLONG capturedTick{0U};
    bool valid{false};
};

SRWLOCK g_forensicCameraSocketPoseLock = SRWLOCK_INIT;
std::array<ForensicCameraSocketPoseSnapshot, 2U>
    g_forensicCameraSocketPoses{};
std::uint64_t g_forensicCameraSocketPoseSequence = 0U;
volatile LONG g_forensicCameraSocketPoseCaptures[2]{};

struct ForensicMemorySnapshot {
    std::int32_t gameState{kUnknownRetailGameState};
    std::int32_t weaponIndex{-1};
    std::uintptr_t weaponManager{0U};
    std::uintptr_t currentWeapon{0U};
    std::uintptr_t modelObject{0U};
    std::uintptr_t weaponDisplay{0U};
    std::uintptr_t displayVtable{0U};
    std::uint32_t displayVtableRva{0U};
    ForensicDisplayKind displayKind{ForensicDisplayKind::none};
    std::size_t displayBytesCaptured{0U};
    std::array<unsigned char, kForensicWeaponTraceSize> weaponBytes{};
    std::array<unsigned char, kForensicDisplayTraceCapacity> displayBytes{};
    char weaponName[kRetailWeaponNameCapacity]{};
    bool rootsReadable{false};
    bool weaponBytesReadable{false};
    bool displayBytesReadable{false};
};

struct ForensicMemoryTraceState {
    std::uint64_t traceId{0U};
    std::uint32_t command{0U};
    std::uint32_t frame{0U};
    std::size_t nextSampleIndex{0U};
    ForensicMemorySnapshot baseline{};
    ForensicMemorySnapshot lastObservation{};
    bool active{false};
    bool lastObservationValid{false};
};

ForensicMemoryTraceState g_forensicMemoryTraceState{};
MenuToggleLatch g_menuToggleLatch;
MenuNavigationState g_menuNavigationState;
ForensicMemorySnapshot CaptureForensicMemorySnapshot() noexcept;


int ReadRetailGameState(void* interfaceManager) noexcept;

void ObserveForensicMemoryCommandTransition(
    const RetailBinding& binding,
    std::uint32_t command, LONG active, bool controllerApplied,
    float retailValue, float outputValue,
    int retailGameState) noexcept;
void SampleForensicMemoryAfterRetailUpdate() noexcept;

struct MeleeCollisionRecordSnapshot {
    std::uintptr_t sourceObject{0};
    std::uintptr_t sourceNode{0};
    std::uintptr_t collisionObject{0};
    std::uintptr_t collisionNotifier{0};
    double expirationTime{0.0};
    unsigned int attackIndex{0};
    unsigned int collisionFinished{0};
    bool readable{false};
};

struct MeleeEnableCollisionCandidate {
    void* record{nullptr};
    MeleeCollisionRecordSnapshot before{};
    int slot{-1};
};

MeleeCollisionRecordSnapshot ReadMeleeCollisionRecord(
    void* record) noexcept {
    MeleeCollisionRecordSnapshot snapshot{};
    if (record == nullptr) {
        return snapshot;
    }
    __try {
        auto* const bytes = static_cast<unsigned char*>(record);
        std::memcpy(
            &snapshot.sourceObject, bytes + 0x38,
            sizeof(snapshot.sourceObject));
        std::memcpy(
            &snapshot.sourceNode, bytes + 0x3C,
            sizeof(snapshot.sourceNode));
        std::memcpy(
            &snapshot.collisionObject, bytes + 0x40,
            sizeof(snapshot.collisionObject));
        std::memcpy(
            &snapshot.collisionNotifier,
            bytes + kMeleeCollisionNotifierOffset,
            sizeof(snapshot.collisionNotifier));
        std::memcpy(
            &snapshot.expirationTime, bytes + 0x18,
            sizeof(snapshot.expirationTime));
        snapshot.attackIndex = bytes[0x10];
        snapshot.collisionFinished = bytes[0x58];
        snapshot.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snapshot = {};
    }
    return snapshot;
}

const char* PhysicalMeleeCollisionRoleName(
    PhysicalMeleeCollisionRole role) noexcept {
    switch (role) {
    case PhysicalMeleeCollisionRole::Attack:
        return "attack";
    case PhysicalMeleeCollisionRole::Block:
        return "block";
    default:
        return "unknown";
    }
}

int ResolveMeleeCollisionRecordSlot(
    void* controller, void* record) noexcept {
    const auto controllerAddress =
        reinterpret_cast<std::uintptr_t>(controller);
    const auto recordAddress =
        reinterpret_cast<std::uintptr_t>(record);
    if (controllerAddress == 0U || recordAddress <
            controllerAddress + kMeleeCollisionRecordOffset) {
        return -1;
    }
    const std::uintptr_t delta = recordAddress -
        (controllerAddress + kMeleeCollisionRecordOffset);
    if (delta % kMeleeCollisionRecordStride != 0U) {
        return -1;
    }
    const std::size_t slot = static_cast<std::size_t>(
        delta / kMeleeCollisionRecordStride);
    return slot < kMeleeCollisionRecordCount
        ? static_cast<int>(slot) : -1;
}

MeleeEnableCollisionCandidate FindMeleeEnableCollisionCandidate(
    void* controller) noexcept {
    MeleeEnableCollisionCandidate candidate{};
    if (controller == nullptr) {
        return candidate;
    }
    auto* const bytes = static_cast<unsigned char*>(controller);
    for (std::size_t slot = 0U;
         slot < kMeleeCollisionRecordCount; ++slot) {
        void* const record = bytes + kMeleeCollisionRecordOffset +
            slot * kMeleeCollisionRecordStride;
        const MeleeCollisionRecordSnapshot snapshot =
            ReadMeleeCollisionRecord(record);
        if (snapshot.readable && snapshot.collisionNotifier == 0U) {
            candidate.record = record;
            candidate.before = snapshot;
            candidate.slot = static_cast<int>(slot);
            return candidate;
        }
    }
    return candidate;
}

bool MeleeCollisionRecordMutationObserved(
    const MeleeCollisionRecordSnapshot& before,
    const MeleeCollisionRecordSnapshot& after) noexcept {
    return before.sourceObject != after.sourceObject ||
        before.sourceNode != after.sourceNode ||
        before.collisionObject != after.collisionObject ||
        before.collisionNotifier != after.collisionNotifier ||
        before.expirationTime != after.expirationTime ||
        before.attackIndex != after.attackIndex ||
        before.collisionFinished != after.collisionFinished;
}

bool PublishPhysicalMeleeCollisionClassification(
    void* controller,
    const MeleeEnableCollisionCandidate& candidate,
    std::uintptr_t ownerObject,
    std::uintptr_t attackIndex,
    bool blocking) noexcept {
    if (candidate.slot < 0 || candidate.record == nullptr) {
        return false;
    }
    const MeleeCollisionRecordSnapshot after =
        ReadMeleeCollisionRecord(candidate.record);
    const PhysicalMeleeCollisionRole role = blocking
        ? PhysicalMeleeCollisionRole::Block
        : PhysicalMeleeCollisionRole::Attack;
    bool classified = false;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    const bool playerModelMatches = after.readable &&
        PhysicalMeleeCollisionBelongsToEquippedWeapon(
            after.sourceObject,
            g_physicalMeleePlayerWeaponModelObject);
    const bool recordCreated = playerModelMatches &&
        after.collisionObject != 0U &&
        after.attackIndex ==
            static_cast<unsigned int>(attackIndex & 0xFFU) &&
        MeleeCollisionRecordMutationObserved(
            candidate.before, after);
    auto& classification =
        g_physicalMeleePlayerCollisionClassifications[
            static_cast<std::size_t>(candidate.slot)];
    if (recordCreated) {
        classification.controller = controller;
        classification.record = candidate.record;
        classification.sourceObject = after.sourceObject;
        classification.collisionObject = after.collisionObject;
        classification.attackIndex = after.attackIndex;
        classification.role = role;
        classified = true;
    } else if (classification.controller == controller &&
               classification.record == candidate.record) {
        classification = {};
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);

    const LONG count = InterlockedIncrement(
        classified
            ? &g_physicalMeleeCollisionRoleClassified
            : &g_physicalMeleeCollisionRoleUnclassified);
    if (g_log != nullptr && count <= 128) {
        char detail[512]{};
        std::snprintf(
            detail, sizeof(detail),
            "count=%ld slot=%d controller=%p record=%p "
            "owner=0x%08lX attack_index=%lu blocking_argument=%u "
            "collision_object=0x%08lX collision_notifier=0x%08lX "
            "role=%s classified=%u lifetime_policy=%s",
            count, candidate.slot, controller, candidate.record,
            static_cast<unsigned long>(ownerObject),
            static_cast<unsigned long>(attackIndex & 0xFFU),
            blocking ? 1U : 0U,
            static_cast<unsigned long>(after.collisionObject),
            static_cast<unsigned long>(after.collisionNotifier),
            classified
                ? PhysicalMeleeCollisionRoleName(role) : "unknown",
            classified ? 1U : 0U,
            classified && role == PhysicalMeleeCollisionRole::Attack
                ? "continuous_contact_damage_candidate"
                : "retail_window");
        g_log(
            classified
                ? "m5_physical_melee_collision_role_classified"
                : "m5_physical_melee_collision_role_unclassified",
            detail);
    }
    return classified;
}

PhysicalMeleeCollisionRole ReadPhysicalMeleeCollisionRole(
    void* controller, void* record,
    const MeleeCollisionRecordSnapshot& snapshot) noexcept {
    const int slot = ResolveMeleeCollisionRecordSlot(
        controller, record);
    if (slot < 0 || !snapshot.readable ||
        snapshot.collisionObject == 0U) {
        return PhysicalMeleeCollisionRole::Unknown;
    }
    PhysicalMeleeCollisionRole role =
        PhysicalMeleeCollisionRole::Unknown;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    const auto& classification =
        g_physicalMeleePlayerCollisionClassifications[
            static_cast<std::size_t>(slot)];
    if (classification.controller == controller &&
        classification.record == record &&
        classification.sourceObject == snapshot.sourceObject &&
        classification.collisionObject == snapshot.collisionObject &&
        classification.attackIndex == snapshot.attackIndex) {
        role = classification.role;
    }
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    return role;
}

bool SetPhysicalMeleeCollisionLifetime(
    void* record,
    bool maintainContinuously) noexcept {
    if (record == nullptr) {
        return false;
    }
    bool transitioned = false;
    __try {
        auto* const bytes = static_cast<unsigned char*>(record);
        double expiration = 0.0;
        std::memcpy(
            &expiration, bytes + 0x18, sizeof(expiration));
        const bool wasContinuous =
            expiration == kContinuousMeleeCollisionExpiration;
        if (maintainContinuously) {
            std::memcpy(
                bytes + 0x18, &kContinuousMeleeCollisionExpiration,
                sizeof(kContinuousMeleeCollisionExpiration));
            bytes[0x58] = 0U;
            transitioned = !wasContinuous;
        } else if (wasContinuous) {
            // Retail's next collision update owns teardown of the physics
            // body, listener, and retained target references.
            bytes[0x58] = 1U;
            transitioned = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (transitioned) {
        InterlockedIncrement(
            maintainContinuously
                ? &g_physicalMeleeContinuousCollisionHeld
                : &g_physicalMeleeContinuousCollisionReleased);
    }
    return transitioned;
}

struct RetailMeleeTargetReferenceVectorRelease {
    std::uintptr_t beginBefore{0U};
    std::uintptr_t endBefore{0U};
    std::uintptr_t capacityBefore{0U};
    std::uintptr_t endAfter{0U};
    std::uintptr_t firstTargetBefore{0U};
    std::size_t referencesBefore{0U};
    std::size_t referencesCleared{0U};
    const char* state{"not_attempted"};
    bool ok{false};
};

RetailMeleeTargetReferenceVectorRelease
ReleaseRetailMeleeTargetReferenceVector(
    void* impactController,
    std::uintptr_t targetReferenceVector) noexcept {
    RetailMeleeTargetReferenceVectorRelease result{};
    if (impactController == nullptr) {
        result.state = "missing_controller";
        return result;
    }
    if (g_resetMeleeTargetReference == nullptr) {
        result.state = "missing_reset_function";
        return result;
    }
    if (targetReferenceVector == 0U) {
        result.state = "missing_target_vector";
        return result;
    }
    const std::uintptr_t controllerAddress =
        reinterpret_cast<std::uintptr_t>(impactController);
    const bool verifiedSlot =
        targetReferenceVector == controllerAddress + 0x60U ||
        targetReferenceVector == controllerAddress + 0xC0U;
    if (!verifiedSlot) {
        result.state = "invalid_slot";
        return result;
    }
    __try {
        auto* const vectorBytes =
            reinterpret_cast<unsigned char*>(targetReferenceVector);
        std::memcpy(
            &result.beginBefore, vectorBytes + 0x04,
            sizeof(result.beginBefore));
        std::memcpy(
            &result.endBefore, vectorBytes + 0x08,
            sizeof(result.endBefore));
        std::memcpy(
            &result.capacityBefore, vectorBytes + 0x0C,
            sizeof(result.capacityBefore));
        const RetailMeleeTargetReferenceVectorSpan span =
            ResolveRetailMeleeTargetReferenceVectorSpan(
                result.beginBefore, result.endBefore,
                result.capacityBefore);
        if (!span.valid) {
            result.state = "invalid_vector";
            return result;
        }
        result.referencesBefore = span.count;
        if (span.count == 0U) {
            result.endAfter = span.end;
            result.state = "empty";
            result.ok = true;
            return result;
        }

        constexpr std::uintptr_t kTargetReferenceSize = 16U;
        constexpr std::uintptr_t kTargetOffset = 0x0CU;
        for (std::size_t index = 0U; index < span.count; ++index) {
            const std::uintptr_t targetReference =
                span.begin + index * kTargetReferenceSize;
            std::uintptr_t targetBefore = 0U;
            std::memcpy(
                &targetBefore,
                reinterpret_cast<const unsigned char*>(targetReference) +
                    kTargetOffset,
                sizeof(targetBefore));
            if (index == 0U) {
                result.firstTargetBefore = targetBefore;
            }
            if (targetBefore == 0U) {
                continue;
            }
            g_resetMeleeTargetReference(
                reinterpret_cast<void*>(targetReference));
            std::uintptr_t targetAfter = 0U;
            std::memcpy(
                &targetAfter,
                reinterpret_cast<const unsigned char*>(targetReference) +
                    kTargetOffset,
                sizeof(targetAfter));
            if (targetAfter != 0U) {
                result.state = "target_still_set";
                return result;
            }
            ++result.referencesCleared;
        }

        std::memcpy(
            vectorBytes + 0x08, &span.begin, sizeof(span.begin));
        std::memcpy(
            &result.endAfter, vectorBytes + 0x08,
            sizeof(result.endAfter));
        if (result.endAfter != span.begin) {
            result.state = "end_not_rewound";
            return result;
        }
        InterlockedIncrement(&g_physicalMeleeRetailLatchReleased);
        result.state = "cleared";
        result.ok = true;
        return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result.state = "exception";
        return result;
    }
}

void PublishPhysicalMeleePlayerWeaponModel(
    void* modelObject) noexcept {
    const auto value = reinterpret_cast<std::uintptr_t>(modelObject);
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (value != g_physicalMeleePlayerWeaponModelObject) {
        g_physicalMeleePlayerWeaponModelObject = value;
        ResetPhysicalMeleeAutomaticSeed(
            g_physicalMeleeAutomaticSeedState);
        g_physicalMeleePlayerCollisionController = nullptr;
        g_physicalMeleePlayerCollisionObject = 0U;
        g_physicalMeleePlayerCollisionTick = 0U;
        g_physicalMeleePlayerBlockCollisionObject = 0U;
        g_physicalMeleePlayerBlockCollisionTick = 0U;
        g_physicalMeleeLastRetailBlockWindowSeconds = 0.0F;
        g_physicalMeleeLastAppliedBlockWindowSeconds = 0.0F;
        g_physicalMeleeLastBlockWindowOverrideApplied = false;
        for (auto& classification :
             g_physicalMeleePlayerCollisionClassifications) {
            classification = {};
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
}

void PublishPhysicalMeleePlayerCollisionObject(
    bool playerOwned,
    std::uintptr_t collisionObject,
    PhysicalMeleeCollisionRole role) noexcept {
    if (!playerOwned ||
        (role != PhysicalMeleeCollisionRole::Attack &&
         role != PhysicalMeleeCollisionRole::Block)) {
        return;
    }
    std::uintptr_t previousCollisionObject = 0U;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (role == PhysicalMeleeCollisionRole::Block) {
        previousCollisionObject =
            g_physicalMeleePlayerBlockCollisionObject;
        g_physicalMeleePlayerBlockCollisionObject = collisionObject;
        g_physicalMeleePlayerBlockCollisionTick = collisionObject != 0U
            ? GetTickCount64() : 0U;
    } else {
        previousCollisionObject = g_physicalMeleePlayerCollisionObject;
        g_physicalMeleePlayerCollisionObject = collisionObject;
        g_physicalMeleePlayerCollisionTick = collisionObject != 0U
            ? GetTickCount64() : 0U;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    if (previousCollisionObject != collisionObject && g_log != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "seeded=%u collision_object=0x%08lX "
            "previous_object=0x%08lX role=%s",
            collisionObject != 0U ? 1U : 0U,
            static_cast<unsigned long>(collisionObject),
            static_cast<unsigned long>(previousCollisionObject),
            PhysicalMeleeCollisionRoleName(role));
        g_log("m5_weapon_test_collider_state", detail);
    }
}


bool MarkPhysicalMeleeCollisionControllerOwnership(
    void* controller,
    std::uintptr_t sourceObject) noexcept {
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    const bool playerOwned =
        PhysicalMeleeCollisionBelongsToEquippedWeapon(
            sourceObject,
            g_physicalMeleePlayerWeaponModelObject);
    if (playerOwned) {
        g_physicalMeleePlayerCollisionController = controller;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return playerOwned;
}

bool PhysicalMeleeImpactControllerIsPlayerOwned(
    void* controller) noexcept {
    if (g_physicalMeleePlayerCollisionUpdate) {
        return true;
    }
    AcquireSRWLockShared(&g_physicalMeleeLock);
    const bool playerOwned =
        g_physicalMeleePlayerWeaponModelObject != 0U &&
        controller != nullptr &&
        controller == g_physicalMeleePlayerCollisionController;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    return playerOwned;
}

bool PhysicalMeleeEnableCollisionsIsPlayerOwned(
    std::uintptr_t ownerObject) noexcept {
    if (ownerObject == 0U || g_gameClientBase == nullptr) {
        return false;
    }
    __try {
        std::uintptr_t client = 0U;
        std::memcpy(
            &client,
            g_gameClientBase + kMeleeClientGlobalRva,
            sizeof(client));
        if (client == 0U) {
            return false;
        }
        std::uintptr_t playerObject = 0U;
        std::memcpy(
            &playerObject,
            reinterpret_cast<const unsigned char*>(client) + 0x10U,
            sizeof(playerObject));
        return PhysicalMeleeCollisionBelongsToEquippedWeapon(
            ownerObject, playerObject);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


bool CommandLineContains(const wchar_t* option) noexcept {
    const wchar_t* const commandLine = GetCommandLineW();
    return commandLine != nullptr && option != nullptr &&
        std::wcsstr(commandLine, option) != nullptr;
}

void TryLogRetailWeaponCatalog() noexcept {
    if (!CommandLineContains(
            L"-condemnedvr-m5-weapon-catalog-probe") ||
        g_log == nullptr ||
        InterlockedCompareExchange(
            &g_weaponCatalogProbeState, 1, 0) != 0) {
        return;
    }
    RetailWeaponIdentityCatalog catalog{};
    const RetailWeaponIdentityReadResult result =
        ReadRetailWeaponIdentityCatalog(catalog);
    char summary[160]{};
    std::snprintf(
        summary, sizeof(summary), "result=%s count=%u",
        RetailWeaponIdentityReadResultName(result), catalog.count);
    g_log("m5_weapon_catalog_probe", summary);
    if (result != RetailWeaponIdentityReadResult::Ok) {
        InterlockedExchange(&g_weaponCatalogProbeState, 0);
        return;
    }
    for (std::uint32_t index = 0U; index < catalog.count; ++index) {
        const RetailWeaponIdentitySnapshot& entry =
            catalog.entries[index];
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld weapon_name=%s animation_property=%s "
            "pose_family=%s",
            static_cast<long>(entry.playerWeaponIndex),
            entry.recordName,
            entry.animationPropertyResolved
                ? entry.animationProperty : "UNKNOWN",
            RetailWeaponPoseFamilyLabel(entry.poseFamily));
        g_log("m5_weapon_catalog_entry", detail);
    }
    InterlockedExchange(&g_weaponCatalogProbeState, 2);
}

bool ProcessOwnsForegroundWindow() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

bool SampleIsFresh(
    std::uint64_t sampleId, ULONGLONG now) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (sampleId != 0 && sampleId != g_lastSampleId) {
        g_lastSampleId = sampleId;
        g_lastSampleTick = now;
    }
    const bool fresh = g_lastSampleTick != 0 &&
        now - g_lastSampleTick <= kInputFreshnessMilliseconds;
    ReleaseSRWLockExclusive(&g_bindingLock);
    return fresh;
}

bool ReadUsableControllerInput(
    FearVrInputState& input) noexcept {
    input = {};
    if (g_getInputState == nullptr ||
        g_getInputState(&input) == FALSE) {
        return false;
    }
    return SampleIsFresh(input.sampleId, GetTickCount64()) &&
        ProcessOwnsForegroundWindow();
}

bool WeaponGripCalibrationCapturesInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    return VrToolMenuCapturesControllerInput(input, sampleFresh) ||
        (WeaponGripCalibrationAcceptsControllerInput() &&
         ResolveWeaponGripCalibrationControls(
             input, sampleFresh).captured);
}

void FormatGameClientStack(
    char* output, std::size_t outputSize) noexcept {
    if (output == nullptr || outputSize == 0) {
        return;
    }
    output[0] = '\0';
    void* frames[16]{};
    const USHORT count = CaptureStackBackTrace(
        0, static_cast<DWORD>(
            sizeof(frames) / sizeof(frames[0])), frames, nullptr);
    std::size_t used = 0;
    for (USHORT index = 0; index < count; ++index) {
        auto* const address = static_cast<unsigned char*>(frames[index]);
        if (g_gameClientBase == nullptr ||
            address < g_gameClientBase ||
            address >= g_gameClientBase + kRetailGameImageSize) {
            continue;
        }
        const auto rva = static_cast<unsigned long>(
            address - g_gameClientBase);
        const int written = std::snprintf(
            output + used, outputSize - used,
            used == 0 ? "0x%08lX" : ",0x%08lX", rva);
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= outputSize - used) {
            output[outputSize - 1] = '\0';
            break;
        }
        used += static_cast<std::size_t>(written);
    }
    if (used == 0) {
        std::snprintf(output, outputSize, "none");
    }
}

bool ReadControllerForward(VectorAbi& forward) noexcept {
    float rotation[4]{};
    if (!ReadTrackedControllerAimRotation(rotation)) {
        return false;
    }
    const fearvr::TrackingQuaternion controller = fearvr::Normalize({
        rotation[0], rotation[1], rotation[2], rotation[3]});
    if (!fearvr::IsFinite(controller)) {
        return false;
    }
    const fearvr::TrackingVector value =
        fearvr::Rotate(controller, {0.0F, 0.0F, 1.0F});
    if (!fearvr::IsFinite(value)) {
        return false;
    }
    forward = {value.x, value.y, value.z};
    return true;
}

bool ReadControllerSwingPose(
    fearvr::TrackingVector& gripPositionMeters,
    fearvr::TrackingQuaternion& aimRotation,
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    gripPositionMeters = {};
    aimRotation = {};
    sampleId = 0;
    timestampNs = 0;
    if (g_getInputState == nullptr) {
        return false;
    }
    FearVrInputState input{};
    if (g_getInputState(&input) == FALSE ||
        !fearvr::IsInputStateUsable(input, true) ||
        (input.activeHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        (input.gripPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        (input.aimPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        input.sampleId == 0 || input.predictedDisplayTimeNs == 0 ||
        !fearvr::IsValidPose(
            input.handGripPose[FEARVR_HAND_RIGHT]) ||
        !fearvr::IsValidPose(
            input.handAimPose[FEARVR_HAND_RIGHT])) {
        return false;
    }

    // Tracking-space motion excludes Retail camera translation and turning,
    // so walking or snap-turning cannot masquerade as a hand swing. The
    // OpenXR-to-LithTech conversion is orthonormal and preserves speed.
    gripPositionMeters = fearvr::OpenXrToLithTech(
        fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_RIGHT]));
    aimRotation = fearvr::OpenXrToLithTech(
        fearvr::PoseRotation(
            input.handAimPose[FEARVR_HAND_RIGHT]));
    ResolvePhysicalMeleeTrackedTwoHandPose(
        input, gripPositionMeters, aimRotation);
    if (!fearvr::IsFinite(gripPositionMeters) ||
        !fearvr::IsFinite(aimRotation)) {
        gripPositionMeters = {};
        aimRotation = {};
        return false;
    }
    sampleId = input.sampleId;
    timestampNs = input.predictedDisplayTimeNs;
    return true;
}

bool ReadVectorCandidate(
    std::uintptr_t address,
    VectorAbi& value) noexcept {
    value = {};
    if (address == 0) {
        return false;
    }
    __try {
        std::memcpy(
            &value, reinterpret_cast<const void*>(address),
            sizeof(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = {};
        return false;
    }
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

const char* PhysicalMeleeResetReasonName(
    PhysicalMeleeResetReason reason) noexcept {
    switch (reason) {
    case PhysicalMeleeResetReason::None:
        return "none";
    case PhysicalMeleeResetReason::FirstPose:
        return "first_pose";
    case PhysicalMeleeResetReason::TrackingLost:
        return "tracking_lost";
    case PhysicalMeleeResetReason::TrackingReacquired:
        return "tracking_reacquired";
    case PhysicalMeleeResetReason::InvalidPose:
        return "invalid_pose";
    case PhysicalMeleeResetReason::InvalidProfile:
        return "invalid_profile";
    case PhysicalMeleeResetReason::NonPositiveTime:
        return "non_positive_time";
    case PhysicalMeleeResetReason::InsufficientSampleInterval:
        return "insufficient_sample_interval";
    case PhysicalMeleeResetReason::ExcessiveSampleGap:
        return "excessive_sample_gap";
    case PhysicalMeleeResetReason::ExcessiveTravel:
        return "excessive_travel";
    default:
        return "unknown";
    }
}

const char* PhysicalMeleeContactReasonName(
    PhysicalMeleeContactReason reason) noexcept {
    switch (reason) {
    case PhysicalMeleeContactReason::None:
        return "none";
    case PhysicalMeleeContactReason::Accepted:
        return "accepted";
    case PhysicalMeleeContactReason::InvalidProfile:
        return "invalid_profile";
    case PhysicalMeleeContactReason::MissingTarget:
        return "missing_target";
    case PhysicalMeleeContactReason::InvalidContact:
        return "invalid_contact";
    case PhysicalMeleeContactReason::InvalidFrame:
        return "invalid_frame";
    case PhysicalMeleeContactReason::OutsideConfiguredCollider:
        return "outside_configured_collider";
    case PhysicalMeleeContactReason::SwingNotQualified:
        return "swing_not_qualified";
    case PhysicalMeleeContactReason::ContactLatched:
        return "contact_latched";
    case PhysicalMeleeContactReason::AutomaticSeedSuppressed:
        return "automatic_seed_suppressed";
    default:
        return "unknown";
    }
}

bool CopyLatestPhysicalMeleeColliderSource(
    PhysicalMeleeFrame& frame,
    std::uint64_t& sampleId,
    PhysicalMeleeProfile& profile,
    std::int32_t& weaponIndex) noexcept {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_physicalMeleeLock);
    frame = g_physicalMeleeFrame;
    sampleId = g_physicalMeleeSampleId;
    profile = g_physicalMeleeProfile;
    weaponIndex = g_physicalMeleeProfileWeaponIndex;
    const bool available = sampleId != 0 && frame.poseValid &&
        g_physicalMeleeSampleTick != 0 &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    return available && ProcessOwnsForegroundWindow();
}

bool CopyLatestPhysicalMeleeFrame(
    PhysicalMeleeFrame& frame,
    std::uint64_t& sampleId) noexcept {
    PhysicalMeleeProfile profile{};
    std::int32_t weaponIndex = -1;
    return CopyLatestPhysicalMeleeColliderSource(
        frame, sampleId, profile, weaponIndex);
}

bool PhysicalMeleeContactDamageContextActive() noexcept {
    if (InterlockedCompareExchange(
            &g_physicalMeleeContactDamageEnabled, 0, 0) == 0 ||
        VrToolMenuIsOpen() ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return false;
    }
    PhysicalMeleeFrame frame{};
    std::uint64_t sampleId = 0U;
    if (!CopyLatestPhysicalMeleeFrame(frame, sampleId)) {
        return false;
    }
    AcquireSRWLockShared(&g_physicalMeleeLock);
    const bool supportedOneHandedEquipped =
        PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            g_physicalMeleeProfileWeaponIndex,
            g_physicalMeleeProfile.id);
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    return supportedOneHandedEquipped;
}

bool CapturePhysicalMeleeNativeCapsuleOverride(
    std::uintptr_t ownerObject,
    bool blocking,
    PhysicalMeleeNativeCapsuleShape& shape) noexcept {
    shape = {};
    if (InterlockedCompareExchange(
            &g_physicalMeleeContactDamageEnabled, 0, 0) == 0 ||
        !PhysicalMeleeEnableCollisionsIsPlayerOwned(ownerObject) ||
        VrToolMenuIsOpen() ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return false;
    }
    PhysicalMeleeFrame frame{};
    std::uint64_t sampleId = 0U;
    PhysicalMeleeProfile profile{};
    std::int32_t weaponIndex = -1;
    if (!CopyLatestPhysicalMeleeColliderSource(
            frame, sampleId, profile, weaponIndex) ||
        !PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            weaponIndex, profile.id)) {
        return false;
    }
    if (blocking) {
        const ToolMenuColliderSettings blockSettings =
            ReadVrToolMenuBlockColliderSettings(weaponIndex);
        PhysicalMeleeFrame blockFrame{};
        if (!ResolveToolMenuColliderFrameAtCurrentPose(
                frame, profile, blockSettings, blockFrame)) {
            return false;
        }
        frame = blockFrame;
    }
    shape = ResolvePhysicalMeleeNativeCapsuleShape(frame, true);
    return shape.valid;
}

struct PhysicalMeleeBlockWindowResolution {
    std::uintptr_t appliedArgument{0U};
    float retailSeconds{0.0F};
    float appliedSeconds{0.0F};
    bool observed{false};
    bool overrideApplied{false};
};

PhysicalMeleeBlockWindowResolution ResolvePhysicalMeleeBlockWindow(
    std::uintptr_t ownerObject,
    bool blocking,
    std::uintptr_t retailArgument) noexcept {
    PhysicalMeleeBlockWindowResolution result{};
    result.appliedArgument = retailArgument;
    if (!blocking ||
        InterlockedCompareExchange(
            &g_physicalMeleeContactDamageEnabled, 0, 0) == 0 ||
        !PhysicalMeleeEnableCollisionsIsPlayerOwned(ownerObject) ||
        VrToolMenuIsOpen() ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return result;
    }

    std::uint32_t retailBits =
        static_cast<std::uint32_t>(retailArgument);
    std::memcpy(
        &result.retailSeconds, &retailBits,
        sizeof(result.retailSeconds));
    if (!std::isfinite(result.retailSeconds) ||
        result.retailSeconds <= 0.0F ||
        result.retailSeconds > 10.0F) {
        return result;
    }

    std::int32_t weaponIndex = -1;
    PhysicalMeleeProfileId profileId =
        PhysicalMeleeProfileId::GenericOneHanded;
    bool supported = false;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    weaponIndex = g_physicalMeleeProfileWeaponIndex;
    profileId = g_physicalMeleeProfile.id;
    supported = PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
        weaponIndex, profileId);
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    if (!supported) {
        return result;
    }

    const ToolMenuBlockTimingSettings settings =
        ReadVrToolMenuBlockTimingSettings(weaponIndex);
    result.appliedSeconds = ResolveToolMenuBlockWindowSeconds(
        settings, result.retailSeconds,
        result.overrideApplied);
    if (result.overrideApplied) {
        std::uint32_t appliedBits = 0U;
        std::memcpy(
            &appliedBits, &result.appliedSeconds,
            sizeof(appliedBits));
        result.appliedArgument =
            static_cast<std::uintptr_t>(appliedBits);
    }
    result.observed = true;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    g_physicalMeleeLastRetailBlockWindowSeconds =
        result.retailSeconds;
    g_physicalMeleeLastAppliedBlockWindowSeconds =
        result.appliedSeconds;
    g_physicalMeleeLastBlockWindowOverrideApplied =
        result.overrideApplied;
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return result;
}

void PublishPhysicalMeleeAutomaticSeedCandidate(
    std::int32_t weaponIndex,
    void* weapon,
    void* modelObject) noexcept {
    const bool enabled = InterlockedCompareExchange(
        &g_physicalMeleeAutomaticSeedEnabled, 0, 0) != 0;
    const std::uintptr_t weaponToken =
        reinterpret_cast<std::uintptr_t>(weapon);
    const std::uintptr_t modelToken =
        reinterpret_cast<std::uintptr_t>(modelObject);
    bool candidateValid = false;
    bool changed = false;
    PhysicalMeleeAutomaticSeedPhase phase =
        PhysicalMeleeAutomaticSeedPhase::Inactive;
    char weaponName[kRetailWeaponNameCapacity]{};
    char animationProperty[
        kRetailWeaponAnimationPropertyCapacity]{};
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    candidateValid = enabled &&
        weaponIndex == g_physicalMeleeProfileWeaponIndex &&
        PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            weaponIndex, g_physicalMeleeProfile.id) &&
        g_equippedWeaponIdentity.playerWeaponIndex == weaponIndex &&
        g_equippedWeaponIdentity.nameResolved &&
        g_equippedWeaponIdentity.animationPropertyResolved &&
        modelToken != 0U &&
        modelToken == g_physicalMeleePlayerWeaponModelObject;
    std::memcpy(
        weaponName, g_equippedWeaponIdentity.recordName,
        sizeof(weaponName));
    std::memcpy(
        animationProperty,
        g_equippedWeaponIdentity.animationProperty,
        sizeof(animationProperty));
    changed = ObservePhysicalMeleeAutomaticSeedEquip(
        g_physicalMeleeAutomaticSeedState,
        weaponIndex, weaponToken, modelToken,
        candidateValid);
    phase = g_physicalMeleeAutomaticSeedState.phase;
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    if (!changed || g_log == nullptr) {
        return;
    }
    char detail[512]{};
    std::snprintf(
        detail, sizeof(detail),
        "weapon_index=%ld weapon=0x%08lX model=0x%08lX "
        "weapon_name=%s animation_property=%s candidate_valid=%u "
        "phase=%s attempts=0 collision_body_ready=0",
        static_cast<long>(weaponIndex),
        static_cast<unsigned long>(weaponToken),
        static_cast<unsigned long>(modelToken),
        weaponName[0] != '\0' ? weaponName : "UNKNOWN",
        animationProperty[0] != '\0'
            ? animationProperty : "UNKNOWN",
        candidateValid ? 1U : 0U,
        PhysicalMeleeAutomaticSeedPhaseName(phase));
    g_log(
        candidateValid
            ? "m5_physical_melee_auto_seed_candidate"
            : "m5_physical_melee_auto_seed_reset",
        detail);
}

PhysicalMeleeAutomaticSeedResult
UpdatePhysicalMeleeAutomaticSeedRuntime(
    bool safeContext,
    bool attackInputIdle) noexcept {
    PhysicalMeleeAutomaticSeedResult result{};
    if (InterlockedCompareExchange(
            &g_physicalMeleeAutomaticSeedEnabled, 0, 0) == 0) {
        return result;
    }
    const ULONGLONG now = GetTickCount64();
    std::int32_t weaponIndex = -1;
    std::uintptr_t weaponToken = 0U;
    std::uintptr_t modelToken = 0U;
    ULONGLONG blockUntil = 0U;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    const bool poseFresh =
        g_physicalMeleeSampleId != 0U &&
        g_physicalMeleeSampleTick != 0U &&
        g_physicalMeleeFrame.poseValid &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds;
    const bool collisionBodyLive =
        g_physicalMeleePlayerCollisionObject != 0U &&
        g_physicalMeleePlayerCollisionTick != 0U &&
        now - g_physicalMeleePlayerCollisionTick <=
            kInputFreshnessMilliseconds;
    result = UpdatePhysicalMeleeAutomaticSeed(
        g_physicalMeleeAutomaticSeedState,
        static_cast<std::uint64_t>(now),
        safeContext && poseFresh,
        attackInputIdle, collisionBodyLive);
    weaponIndex =
        g_physicalMeleeAutomaticSeedState.weaponIndex;
    weaponToken =
        g_physicalMeleeAutomaticSeedState.weaponToken;
    modelToken =
        g_physicalMeleeAutomaticSeedState.modelToken;
    if (result.started) {
        ResetPhysicalMeleeContactState(
            g_physicalMeleeContactState);
        g_physicalMeleeAutomaticSeedImpactController = nullptr;
        g_physicalMeleeAutomaticSeedImpactBlockUntil =
            g_physicalMeleeAutomaticSeedState
                .damageBlockEndMilliseconds;
    } else if (result.becameReady) {
        g_physicalMeleeAutomaticSeedImpactController = nullptr;
        g_physicalMeleeAutomaticSeedImpactBlockUntil = 0U;
    }
    blockUntil =
        g_physicalMeleeAutomaticSeedImpactBlockUntil;
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);

    if (g_log == nullptr) {
        return result;
    }
    if (result.started) {
        const LONG count = InterlockedIncrement(
            &g_physicalMeleeAutomaticSeedStarted);
        char detail[512]{};
        std::snprintf(
            detail, sizeof(detail),
            "count=%ld weapon_index=%ld weapon=0x%08lX "
            "model=0x%08lX attempt=%u/%u "
            "stable_ms=%llu pulse_ms=%llu confirm_ms=%llu "
            "damage_block_until_ms=%llu "
            "output=retail_fire_command_17 haptic=blocked "
            "native_impact_dispatch=blocked",
            count, static_cast<long>(weaponIndex),
            static_cast<unsigned long>(weaponToken),
            static_cast<unsigned long>(modelToken),
            result.attempts,
            kPhysicalMeleeAutomaticSeedMaximumAttempts,
            static_cast<unsigned long long>(
                kPhysicalMeleeAutomaticSeedStableMilliseconds),
            static_cast<unsigned long long>(
                kPhysicalMeleeAutomaticSeedPulseMilliseconds),
            static_cast<unsigned long long>(
                kPhysicalMeleeAutomaticSeedConfirmationMilliseconds),
            static_cast<unsigned long long>(blockUntil));
        g_log("m5_physical_melee_auto_seed_started", detail);
    }
    if (result.retryScheduled || result.terminalFailure) {
        if (result.terminalFailure) {
            InterlockedIncrement(
                &g_physicalMeleeAutomaticSeedFailed);
        }
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld attempt=%u/%u phase=%s "
            "reason=%s retry=%u manual_attack_fallback=1 "
            "direct_EnableCollisions_call=0",
            static_cast<long>(weaponIndex), result.attempts,
            kPhysicalMeleeAutomaticSeedMaximumAttempts,
            PhysicalMeleeAutomaticSeedPhaseName(result.phase),
            result.timedOut
                ? "confirmation_timeout"
                : result.bodyLost
                    ? "collision_body_lost"
                    : "unsafe_context",
            result.retryScheduled ? 1U : 0U);
        g_log(
            result.terminalFailure
                ? "m5_physical_melee_auto_seed_failed"
                : "m5_physical_melee_auto_seed_retry",
            detail);
    } else if (result.bodyLost) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld phase=%s reason=collision_body_lost "
            "retry_after_stable_context=1",
            static_cast<long>(weaponIndex),
            PhysicalMeleeAutomaticSeedPhaseName(result.phase));
        g_log("m5_physical_melee_auto_seed_body_lost", detail);
    }
    if (result.becameReady) {
        const LONG count = InterlockedIncrement(
            &g_physicalMeleeAutomaticSeedReady);
        char detail[320]{};
        std::snprintf(
            detail, sizeof(detail),
            "count=%ld weapon_index=%ld attempt=%u "
            "phase=ready collision_body_live=1 "
            "native_read_mask=0x7 settle_ms=%llu "
            "native_impact_dispatch=physical_contact_gate_owned",
            count, static_cast<long>(weaponIndex),
            result.attempts,
            static_cast<unsigned long long>(
                kPhysicalMeleeAutomaticSeedSettleMilliseconds));
        g_log("m5_physical_melee_auto_seed_ready", detail);
    }
    return result;
}

bool PhysicalMeleeAutomaticSeedImpactIsBlocked(
    void* impactController) noexcept {
    if (impactController == nullptr) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (g_physicalMeleeAutomaticSeedImpactBlockUntil != 0U &&
        now >= g_physicalMeleeAutomaticSeedImpactBlockUntil) {
        g_physicalMeleeAutomaticSeedImpactController = nullptr;
        g_physicalMeleeAutomaticSeedImpactBlockUntil = 0U;
    }
    const bool blocked =
        g_physicalMeleeAutomaticSeedImpactController ==
            impactController &&
        g_physicalMeleeAutomaticSeedImpactBlockUntil != 0U &&
        now < g_physicalMeleeAutomaticSeedImpactBlockUntil;
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return blocked;
}

void BindPhysicalMeleeAutomaticSeedImpactController(
    void* controller) noexcept {
    if (controller == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (g_physicalMeleeAutomaticSeedImpactBlockUntil != 0U &&
        now < g_physicalMeleeAutomaticSeedImpactBlockUntil) {
        g_physicalMeleeAutomaticSeedImpactController = controller;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
}

void ObservePhysicalMeleeAutomaticSeedCollision(
    void* controller,
    std::uint32_t nativeReadMask,
    bool playerAttackClassified,
    std::uintptr_t collisionObject) noexcept {
    const ULONGLONG now = GetTickCount64();
    PhysicalMeleeAutomaticSeedConfirmation confirmation{};
    bool transactionBlocked = false;
    std::int32_t weaponIndex = -1;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    transactionBlocked =
        g_physicalMeleeAutomaticSeedImpactBlockUntil != 0U &&
        now < g_physicalMeleeAutomaticSeedImpactBlockUntil;
    if (transactionBlocked) {
        g_physicalMeleeAutomaticSeedImpactController = controller;
    }
    confirmation = ConfirmPhysicalMeleeAutomaticSeed(
        g_physicalMeleeAutomaticSeedState,
        static_cast<std::uint64_t>(now),
        nativeReadMask, playerAttackClassified,
        collisionObject);
    weaponIndex =
        g_physicalMeleeAutomaticSeedState.weaponIndex;
    if (confirmation.accepted &&
        confirmation.automaticTransaction) {
        g_physicalMeleeAutomaticSeedImpactController = controller;
        g_physicalMeleeAutomaticSeedImpactBlockUntil =
            confirmation.damageBlockEndMilliseconds;
    } else if (confirmation.accepted &&
               confirmation.readyImmediately &&
               !transactionBlocked) {
        g_physicalMeleeAutomaticSeedImpactController = nullptr;
        g_physicalMeleeAutomaticSeedImpactBlockUntil = 0U;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);

    if (confirmation.accepted) {
        // Classification plus read_mask=0x7 proves the body before its first
        // ordinary UpdateCollision tick. Publish that bounded freshness now;
        // the verified update hook must keep refreshing it through settle.
        PublishPhysicalMeleePlayerCollisionObject(
            true, collisionObject,
            PhysicalMeleeCollisionRole::Attack);
    }
    if (g_log == nullptr) {
        return;
    }
    if (confirmation.accepted) {
        const LONG count = InterlockedIncrement(
            &g_physicalMeleeAutomaticSeedConfirmed);
        if (confirmation.readyImmediately) {
            InterlockedIncrement(
                &g_physicalMeleeAutomaticSeedReady);
        }
        char detail[512]{};
        std::snprintf(
            detail, sizeof(detail),
            "count=%ld weapon_index=%ld controller=%p "
            "collision_object=0x%08lX attempt=%u "
            "source=%s player_attack_classified=1 "
            "native_read_mask=0x%X expected_read_mask=0x7 "
            "phase=%s ready_immediately=%u "
            "damage_block_until_ms=%llu",
            count, static_cast<long>(weaponIndex), controller,
            static_cast<unsigned long>(collisionObject),
            confirmation.attempts,
            confirmation.automaticTransaction
                ? "automatic_equip_pulse"
                : "manual_retail_attack",
            nativeReadMask,
            PhysicalMeleeAutomaticSeedPhaseName(
                confirmation.phase),
            confirmation.readyImmediately ? 1U : 0U,
            static_cast<unsigned long long>(
                confirmation.damageBlockEndMilliseconds));
        g_log("m5_physical_melee_auto_seed_confirmed", detail);
    } else if (transactionBlocked) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld controller=%p "
            "collision_object=0x%08lX "
            "player_attack_classified=%u native_read_mask=0x%X "
            "expected_read_mask=0x7 result=not_ready "
            "native_impact_dispatch=blocked",
            static_cast<long>(weaponIndex), controller,
            static_cast<unsigned long>(collisionObject),
            playerAttackClassified ? 1U : 0U,
            nativeReadMask);
        g_log(
            "m5_physical_melee_auto_seed_verification_pending",
            detail);
    }
}


bool ReadPhysicalMeleeSwingAttackActive(
    bool inputEligible) noexcept {
    const ULONGLONG now = GetTickCount64();
    const bool probeEnabled = InterlockedCompareExchange(
        &g_physicalMeleeProbeEnabled, 0, 0) != 0;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    const bool sampleFresh = g_physicalMeleeSampleId != 0 &&
        g_physicalMeleeSampleTick != 0 &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds &&
        g_physicalMeleeSwingSampleId != 0 &&
        g_physicalMeleeSwingSampleTick != 0 &&
        now - g_physicalMeleeSwingSampleTick <=
            kInputFreshnessMilliseconds;
    if (!inputEligible || !probeEnabled || !sampleFresh ||
        !g_physicalMeleeProfile.swingAttackEnabled) {
        ResetPhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState);
        ReleaseSRWLockExclusive(&g_physicalMeleeLock);
        return false;
    }
    const bool active = PhysicalMeleeSwingAttackPulseIsActive(
        g_physicalMeleeSwingAttackState,
        static_cast<std::uint64_t>(now));
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return active;
}

PhysicalMeleeCurrentWeaponSnapshot
ReadPhysicalMeleeCurrentWeaponSnapshot() noexcept {
    PhysicalMeleeCurrentWeaponSnapshot snapshot{};
    if (g_gameClientBase == nullptr) {
        return snapshot;
    }
    __try {
        void* manager = nullptr;
        std::memcpy(
            &manager,
            g_gameClientBase + kWeaponManagerGlobalRva,
            sizeof(manager));
        if (manager == nullptr) {
            return snapshot;
        }
        const auto* const bytes =
            static_cast<const unsigned char*>(manager);
        std::memcpy(
            &snapshot.index,
            bytes + kCurrentWeaponIndexOffset,
            sizeof(snapshot.index));
        std::memcpy(
            &snapshot.weapon,
            bytes + kCurrentWeaponOffset,
            sizeof(snapshot.weapon));
        snapshot.readable =
            snapshot.index >= 0 && snapshot.weapon != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snapshot = {};
    }
    return snapshot;
}

void ProcessPhysicalMeleeBlockNativeRelease() noexcept {
    if (InterlockedCompareExchange(
            &g_physicalMeleeBlockNativeReleaseEnabled,
            0, 0) == 0) {
        return;
    }

    const int gameState = ReadRetailGameState(g_interfaceManager);
    if (gameState != kCondemnedGameStatePlaying) {
        return;
    }

    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    const bool manualBlockActive = ResolveCoreActionValue(
        input, usable && !calibrationCaptured,
        kCondemnedBlockCommand).active;
    if (manualBlockActive) {
        bool cleared = false;
        AcquireSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
        if (g_physicalMeleeBlockNativeLifecycleState.releasePending) {
            g_physicalMeleeBlockNativeLifecycleState = {};
            cleared = true;
        }
        ReleaseSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
        if (cleared) {
            InterlockedIncrement(
                &g_physicalMeleeBlockNativeReleaseSkipped);
            if (g_log != nullptr) {
                g_log(
                    "m5_physical_melee_block_native_release_skipped",
                    "reason=manual_left_trigger_took_ownership "
                    "engine_handoff=none");
            }
        }
        return;
    }

    const PhysicalMeleeCurrentWeaponSnapshot current =
        ReadPhysicalMeleeCurrentWeaponSnapshot();
    PhysicalMeleeBlockNativeReleaseDecision decision =
        PhysicalMeleeBlockNativeReleaseDecision::None;
    AcquireSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
    decision = ConsumePhysicalMeleeBlockNativeRelease(
        current.readable, current.index,
        reinterpret_cast<std::uintptr_t>(current.weapon),
        g_physicalMeleeBlockNativeLifecycleState);
    ReleaseSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
    if (decision ==
        PhysicalMeleeBlockNativeReleaseDecision::None ||
        decision ==
        PhysicalMeleeBlockNativeReleaseDecision::WaitForWeapon) {
        return;
    }
    if (decision ==
        PhysicalMeleeBlockNativeReleaseDecision::DropWeaponChanged) {
        InterlockedIncrement(
            &g_physicalMeleeBlockNativeReleaseSkipped);
        if (g_log != nullptr) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "reason=weapon_changed current_weapon_index=%ld "
                "current_weapon=0x%08lX engine_handoff=none",
                static_cast<long>(current.index),
                static_cast<unsigned long>(
                    reinterpret_cast<std::uintptr_t>(
                        current.weapon)));
            g_log(
                "m5_physical_melee_block_native_release_skipped",
                detail);
        }
        return;
    }

    InterlockedIncrement(
        &g_physicalMeleeBlockNativeReleaseSkipped);
    if (g_log != nullptr) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld weapon=0x%08lX "
            "reason=second_CS_Block_release_candidate_live_rejected "
            "exit_owner=retail_finite_block_window "
            "collision_lifetime=classified_block_retail_window "
            "engine_handoff=none",
            static_cast<long>(current.index),
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(current.weapon)));
        g_log(
            "m5_physical_melee_block_pose_exit_retail_owned",
            detail);
    }
}

bool ReadPhysicalMeleeBlockPoseActive(
    bool inputEligible,
    bool manualControllerActive,
    bool retailBindingActive) noexcept {
    const bool probeEnabled = InterlockedCompareExchange(
        &g_physicalMeleeProbeEnabled, 0, 0) != 0;
    std::int32_t weaponIndex = -1;
    PhysicalMeleeProfile profile{};
    AcquireSRWLockShared(&g_physicalMeleeLock);
    weaponIndex = g_physicalMeleeProfileWeaponIndex;
    profile = g_physicalMeleeProfile;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    const bool supportedOneHanded =
        PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            weaponIndex, profile.id);
    const PhysicalMeleeCurrentWeaponSnapshot currentWeapon =
        ReadPhysicalMeleeCurrentWeaponSnapshot();
    const bool nativeReleaseReady =
        InterlockedCompareExchange(
            &g_physicalMeleeBlockNativeReleaseEnabled,
            0, 0) != 0 &&
        currentWeapon.readable &&
        currentWeapon.index == weaponIndex;
    PhysicalMeleeBlockPoseSettings settings{};
    if (supportedOneHanded) {
        settings = ReadVrToolMenuBlockPoseSettings(weaponIndex);
    }

    float headPosition[3]{};
    float headRotation[4]{};
    float weaponPosition[3]{};
    float weaponRotation[4]{};
    std::uint64_t sampleId = 0U;
    std::uint64_t timestampNs = 0U;
    const bool trackingFresh =
        ReadTrackedHeadWorldPose(headPosition, headRotation) &&
        ReadTrackedControllerWorldPose(
            weaponPosition, weaponRotation,
            sampleId, timestampNs);
    const PhysicalMeleeBlockWorldPose head{
        {headPosition[0], headPosition[1], headPosition[2]},
        {headRotation[0], headRotation[1],
         headRotation[2], headRotation[3]}};
    const PhysicalMeleeBlockWorldPose weapon{
        {weaponPosition[0], weaponPosition[1], weaponPosition[2]},
        {weaponRotation[0], weaponRotation[1],
         weaponRotation[2], weaponRotation[3]}};

    PhysicalMeleeBlockPoseResult result{};
    AcquireSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
    if (weaponIndex != g_physicalMeleeBlockPoseWeaponIndex) {
        g_physicalMeleeBlockPoseState = {};
        g_physicalMeleeBlockNativeLifecycleState = {};
        g_physicalMeleeBlockPoseWeaponIndex = weaponIndex;
    }
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, weapon, profile.unitsPerMeter,
        inputEligible && probeEnabled && supportedOneHanded &&
            trackingFresh && nativeReleaseReady,
        g_physicalMeleeBlockPoseState);
    const PhysicalMeleeBlockNativeLifecycleTransition
        lifecycleTransition =
            ObservePhysicalMeleeBlockNativeLifecycle(
                result, manualControllerActive,
                retailBindingActive,
                currentWeapon.index,
                reinterpret_cast<std::uintptr_t>(
                    currentWeapon.weapon),
                g_physicalMeleeBlockNativeLifecycleState);
    const bool automaticOwned =
        g_physicalMeleeBlockNativeLifecycleState.automaticOwned;
    const bool releasePending =
        g_physicalMeleeBlockNativeLifecycleState.releasePending;
    g_physicalMeleeBlockPoseResult = result;
    g_physicalMeleeBlockPoseTrackingFresh = trackingFresh;
    ReleaseSRWLockExclusive(&g_physicalMeleeBlockPoseLock);

    if (result.entered) {
        InterlockedIncrement(&g_physicalMeleeBlockPoseActivations);
    }
    if (lifecycleTransition.releaseQueued) {
        InterlockedIncrement(
            &g_physicalMeleeBlockNativeReleaseQueued);
    }
    if ((result.entered || result.exited) && g_log != nullptr) {
        char detail[640]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld active=%u entered=%u exited=%u "
            "tracking_fresh=%u position_error_m=%.3f "
            "angle_error_deg=%.1f position_tolerance_m=%.3f "
            "angle_tolerance_deg=%.1f reason=%s command=28 "
            "source=automatic_guard_pose input_seed_required=0 "
            "manual_trigger_fallback=1 manual_controller_active=%u "
            "retail_binding_active=%u native_release_ready=%u "
            "automatic_native_owned=%u native_release_pending=%u "
            "native_release_queued=%u",
            static_cast<long>(weaponIndex),
            result.active ? 1U : 0U,
            result.entered ? 1U : 0U,
            result.exited ? 1U : 0U,
            trackingFresh ? 1U : 0U,
            result.positionErrorMeters,
            result.angleErrorDegrees,
            settings.positionToleranceMeters,
            settings.angleToleranceDegrees,
            PhysicalMeleeBlockPoseReasonName(result.reason),
            manualControllerActive ? 1U : 0U,
            retailBindingActive ? 1U : 0U,
            nativeReleaseReady ? 1U : 0U,
            automaticOwned ? 1U : 0U,
            releasePending ? 1U : 0U,
            lifecycleTransition.releaseQueued ? 1U : 0U);
        g_log("m5_physical_melee_block_pose_state", detail);
    }
    return result.active;
}

bool EvaluatePhysicalMeleeContact(
    std::uintptr_t targetId,
    bool contactPositionValid,
    const fearvr::TrackingVector& contactPositionUnits,
    PhysicalMeleeFrame& frame,
    std::uint64_t& sampleId,
    PhysicalMeleeContactQualification& qualification) noexcept {
    const ULONGLONG now = GetTickCount64();
    const bool foreground = ProcessOwnsForegroundWindow();
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    frame = g_physicalMeleeFrame;
    sampleId = g_physicalMeleeSampleId;
    const bool available = foreground && sampleId != 0 &&
        frame.poseValid && g_physicalMeleeSampleTick != 0 &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds;
    if (available) {
        const PhysicalMeleeContactDistance distance =
            contactPositionValid
                ? MeasurePhysicalMeleeContactDistance(
                      frame, contactPositionUnits,
                      g_physicalMeleeProfile.unitsPerMeter)
                : PhysicalMeleeContactDistance{};
        qualification = QualifyPhysicalMeleeContactAtDistance(
            g_physicalMeleeContactState, targetId, frame, sampleId,
            distance, g_physicalMeleeProfile,
            g_physicalMeleeProfile.requireSwingForContactDamage);
    } else {
        qualification = {};
        qualification.reason = PhysicalMeleeContactReason::InvalidFrame;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return available;
}

void UpdatePhysicalMeleeProbe() noexcept {
    if (InterlockedCompareExchange(
            &g_physicalMeleeProbeEnabled, 0, 0) == 0) {
        return;
    }

    float position[3]{};
    float rotation[4]{};
    std::uint64_t sampleId = 0;
    std::uint64_t timestampNs = 0;
    const bool fresh = ReadTrackedControllerWorldPose(
        position, rotation, sampleId, timestampNs);
    fearvr::TrackingVector swingGripPositionMeters{};
    fearvr::TrackingQuaternion swingAimRotation{};
    std::uint64_t swingSampleId = 0;
    std::uint64_t swingTimestampNs = 0;
    const bool swingPoseFresh = ReadControllerSwingPose(
        swingGripPositionMeters, swingAimRotation,
        swingSampleId, swingTimestampNs);
    if (!fresh) {
        bool trackingWasActive = false;
        AcquireSRWLockExclusive(&g_physicalMeleeLock);
        trackingWasActive = g_physicalMeleeState.havePose;
        if (trackingWasActive) {
            ResetPhysicalMeleeKinematics(
                g_physicalMeleeState,
                PhysicalMeleeResetReason::TrackingLost);
        }
        ResetPhysicalMeleeKinematics(
            g_physicalMeleeSwingKinematicsState,
            PhysicalMeleeResetReason::TrackingLost);
        ResetPhysicalMeleeContactState(g_physicalMeleeContactState);
        ResetPhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState);
        g_physicalMeleeFrame = {};
        g_physicalMeleeFrame.resetReason =
            PhysicalMeleeResetReason::TrackingLost;
        g_physicalMeleeSampleId = 0;
        g_physicalMeleeSampleTick = 0;
        g_physicalMeleeSwingSampleId = 0;
        g_physicalMeleeSwingSampleTick = 0;
        g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
        ReleaseSRWLockExclusive(&g_physicalMeleeLock);
        InterlockedExchange(&g_physicalMeleeDamageQualified, 0);
        if (trackingWasActive && g_log != nullptr) {
            g_log(
                "m5_physical_melee_tracking_lost",
                "history_cleared=1 engine_writes=0");
        }
        return;
    }

    PhysicalMeleeFrame frame{};
    PhysicalMeleeFrame swingFrame{};
    PhysicalMeleeSwingAttackResult swingAttack{};
    PhysicalMeleeProfile sampledProfile{};
    bool newSample = false;
    bool newSwingSample = false;
    PhysicalMeleeContactRearmUpdate contactRearm{};
    const ULONGLONG now = GetTickCount64();
    const bool swingAttackContext = ProcessOwnsForegroundWindow() &&
        !VrToolMenuIsOpen() &&
        ReadRetailGameState(g_interfaceManager) ==
            kCondemnedGameStatePlaying;
    std::int32_t toolSettingsWeaponIndex = -1;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    toolSettingsWeaponIndex = g_physicalMeleeProfileWeaponIndex;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    const ToolMenuMeleeSettings toolSettings =
        ReadVrToolMenuMeleeSettings(toolSettingsWeaponIndex);
    const ToolMenuColliderSettings colliderSettings =
        ReadVrToolMenuColliderSettings(toolSettingsWeaponIndex);
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (toolSettingsWeaponIndex ==
        g_physicalMeleeProfileWeaponIndex) {
        ApplyToolMenuMeleeSettings(
            toolSettings, g_physicalMeleeProfile);
        ApplyToolMenuColliderSettings(
            colliderSettings, g_physicalMeleeProfile);
    }
    if (sampleId != g_physicalMeleeSampleId) {
        const PhysicalMeleePose pose{
            {position[0], position[1], position[2]},
            {rotation[0], rotation[1], rotation[2], rotation[3]}};
        frame = UpdatePhysicalMeleeKinematics(
            g_physicalMeleeState, pose, true, timestampNs,
            g_physicalMeleeProfile);
        g_physicalMeleeFrame = frame;
        g_physicalMeleeSampleId = sampleId;
        g_physicalMeleeSampleTick = now;
        contactRearm = UpdatePhysicalMeleeContactRearm(
            g_physicalMeleeContactState,
            frame, frame.poseValid,
            g_physicalMeleeProfile);
        sampledProfile = g_physicalMeleeProfile;
        newSample = true;
    }
    if (swingPoseFresh &&
        swingSampleId != g_physicalMeleeSwingSampleId) {
        const PhysicalMeleePose swingPose{
            PhysicalMeleeScale(
                swingGripPositionMeters,
                g_physicalMeleeProfile.unitsPerMeter),
            swingAimRotation};
        swingFrame = UpdatePhysicalMeleeKinematics(
            g_physicalMeleeSwingKinematicsState,
            swingPose, true, swingTimestampNs,
            g_physicalMeleeProfile);
        g_physicalMeleeSwingSampleId = swingSampleId;
        g_physicalMeleeSwingSampleTick = now;
        g_physicalMeleeSwingSpeedMetersPerSecond =
            swingFrame.sweepValid
                ? swingFrame.impactSpeedMetersPerSecond
                : 0.0F;
        swingAttack = UpdatePhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState, swingFrame,
            static_cast<std::uint64_t>(now),
            swingAttackContext, g_physicalMeleeProfile);
        sampledProfile = g_physicalMeleeProfile;
        newSwingSample = true;
    } else if (!swingPoseFresh) {
        ResetPhysicalMeleeKinematics(
            g_physicalMeleeSwingKinematicsState,
            PhysicalMeleeResetReason::TrackingLost);
        ResetPhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState);
        g_physicalMeleeSwingSampleId = 0;
        g_physicalMeleeSwingSampleTick = 0;
        g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    if ((!newSample && !newSwingSample) || g_log == nullptr) {
        return;
    }

    if (swingAttack.triggered) {
        const LONG trigger = InterlockedIncrement(
            &g_physicalMeleeSwingAttackTriggered);
        if (trigger <= 512) {
            char triggerDetail[384]{};
            std::snprintf(
                triggerDetail, sizeof(triggerDetail),
                "trigger=%ld sample_id=%llu profile=%s "
                "speed_mps=%.3f threshold_mps=%.3f "
                "pulse_ms=%u cooldown_ms=%u "
                "motion_space=openxr_tracking "
                "output=retail_fire_command_17",
                trigger,
                static_cast<unsigned long long>(swingSampleId),
                PhysicalMeleeProfileName(sampledProfile.id),
                swingFrame.impactSpeedMetersPerSecond,
                sampledProfile
                    .swingAttackTriggerSpeedMetersPerSecond,
                sampledProfile.swingAttackPulseMilliseconds,
                sampledProfile.swingAttackCooldownMilliseconds);
            g_log(
                "m5_physical_melee_swing_attack_triggered",
                triggerDetail);
        }
    }

    if (!newSample) {
        return;
    }

    if (contactRearm.invalidSampleHeld) {
        const LONG hold = InterlockedIncrement(
            &g_physicalMeleeContactInvalidSampleHeld);
        if (hold <= 512) {
            char holdDetail[384]{};
            std::snprintf(
                holdDetail, sizeof(holdDetail),
                "hold=%ld sample_id=%llu "
                "reason=transient_invalid_sample reset=%s "
                "release_dwell_reset=1 "
                "native_impact_dispatch=blocked",
                hold,
                static_cast<unsigned long long>(sampleId),
                PhysicalMeleeResetReasonName(frame.resetReason));
            g_log(
                "m5_physical_melee_contact_latch_held",
                holdDetail);
        }
    }

    if (contactRearm.rearmed) {
        const LONG rearm = InterlockedIncrement(
            &g_physicalMeleeContactRearmed);
        if (rearm <= 512) {
            char rearmDetail[512]{};
            std::snprintf(
                rearmDetail, sizeof(rearmDetail),
                "rearm=%ld sample_id=%llu reason=swing_completed "
                "tip_displacement_m=%.3f max_tip_displacement_m=%.3f "
                "required_tip_travel_m=%.3f speed_mps=%.3f "
                "release_speed_mps=%.3f release_samples=%u/%u "
                "native_impact_dispatch=blocked",
                rearm,
                static_cast<unsigned long long>(sampleId),
                contactRearm.tipDisplacementMeters,
                contactRearm.maximumTipDisplacementMeters,
                sampledProfile.contactRearmSeparationMeters,
                contactRearm.speedMetersPerSecond,
                contactRearm.releaseSpeedMetersPerSecond,
                contactRearm.releaseSampleCount,
                kPhysicalMeleeContactReleaseSampleCount);
            g_log(
                "m5_physical_melee_contact_rearmed",
                rearmDetail);
        }
    }

    const LONG sampleCall = InterlockedIncrement(
        &g_physicalMeleeSampleCalls);
    const LONG wasDamageQualified = InterlockedExchange(
        &g_physicalMeleeDamageQualified,
        frame.damageQualified ? 1 : 0);
    const bool logSample = sampleCall <= 4 ||
        (frame.damageQualified && wasDamageQualified == 0) ||
        frame.resetReason ==
            PhysicalMeleeResetReason::InsufficientSampleInterval ||
        frame.resetReason ==
            PhysicalMeleeResetReason::ExcessiveSampleGap ||
        frame.resetReason ==
            PhysicalMeleeResetReason::ExcessiveTravel;
    if (!logSample || sampleCall > 512) {
        return;
    }
    char detail[896]{};
    std::snprintf(
        detail, sizeof(detail),
        "sample_call=%ld sample_id=%llu timestamp_ns=%llu "
        "base=(%.3f,%.3f,%.3f) tip=(%.3f,%.3f,%.3f) "
        "sweep_valid=%u sweep_m=%.4f speed_mps=%.3f "
        "energy_j=%.3f damage_qualified=%u reset=%s "
        "profile=%s engine_writes=0",
        sampleCall,
        static_cast<unsigned long long>(sampleId),
        static_cast<unsigned long long>(timestampNs),
        frame.currentBaseUnits.x, frame.currentBaseUnits.y,
        frame.currentBaseUnits.z, frame.currentTipUnits.x,
        frame.currentTipUnits.y, frame.currentTipUnits.z,
        frame.sweepValid ? 1U : 0U,
        frame.sweepDistanceMeters,
        frame.impactSpeedMetersPerSecond,
        frame.impactEnergyJoules,
        frame.damageQualified ? 1U : 0U,
        PhysicalMeleeResetReasonName(frame.resetReason),
        PhysicalMeleeProfileName(sampledProfile.id));
    g_log("m5_physical_melee_sample", detail);
}

void SelectPhysicalMeleeProfileForWeaponIndex(
    std::int32_t weaponIndex) noexcept {
    AcquireSRWLockShared(&g_physicalMeleeLock);
    const bool alreadySelected =
        weaponIndex == g_physicalMeleeProfileWeaponIndex;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    if (alreadySelected) {
        return;
    }
    PhysicalMeleeProfile selected =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    ApplyToolMenuMeleeSettings(
        ReadVrToolMenuMeleeSettings(weaponIndex), selected);
    ApplyToolMenuColliderSettings(
        ReadVrToolMenuColliderSettings(weaponIndex), selected);
    RetailWeaponIdentitySnapshot identity{};
    const RetailWeaponIdentityReadResult identityResult =
        weaponIndex >= 0
        ? ReadRetailWeaponIdentity(weaponIndex, identity)
        : RetailWeaponIdentityReadResult::InvalidIndex;
    if (identityResult == RetailWeaponIdentityReadResult::Ok) {
        TryLogRetailWeaponCatalog();
    }
    if (!identity.nameResolved) {
        std::snprintf(
            identity.recordName, sizeof(identity.recordName), "%s",
            weaponIndex >= 0
                ? ToolMenuWeaponProfileLabel(selected.id)
                : "NO WEAPON");
    }
    if (!identity.animationPropertyResolved) {
        std::snprintf(
            identity.animationProperty,
            sizeof(identity.animationProperty), "UNKNOWN");
        identity.poseFamily = RetailWeaponPoseFamily::Unknown;
    }
    bool changed = false;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (weaponIndex != g_physicalMeleeProfileWeaponIndex) {
        g_physicalMeleeProfileWeaponIndex = weaponIndex;
        g_physicalMeleeProfile = selected;
        g_equippedWeaponIdentity = identity;
        g_physicalMeleeState = {};
        g_physicalMeleeSwingKinematicsState = {};
        g_physicalMeleeFrame = {};
        g_physicalMeleeContactState = {};
        g_physicalMeleeSwingAttackState = {};
        g_physicalMeleeAutomaticSeedState = {};
        g_physicalMeleeSampleId = 0;
        g_physicalMeleeSampleTick = 0;
        g_physicalMeleeSwingSampleId = 0;
        g_physicalMeleeSwingSampleTick = 0;
        g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
        changed = true;
        g_physicalMeleePlayerCollisionObject = 0U;
        g_physicalMeleePlayerCollisionTick = 0U;
        g_physicalMeleePlayerBlockCollisionObject = 0U;
        g_physicalMeleePlayerBlockCollisionTick = 0U;
        g_physicalMeleeLastRetailBlockWindowSeconds = 0.0F;
        g_physicalMeleeLastAppliedBlockWindowSeconds = 0.0F;
        g_physicalMeleeLastBlockWindowOverrideApplied = false;
        for (auto& classification :
             g_physicalMeleePlayerCollisionClassifications) {
            classification = {};
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    if (!changed) {
        return;
    }
    InterlockedExchange(&g_physicalMeleeDamageQualified, 0);
    if (g_log != nullptr) {
        char detail[896]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s "
            "weapon_name=%s animation_property=%s pose_family=%s "
            "identity_result=%s identity_name_resolved=%u "
            "identity_animation_resolved=%u "
            "mass_kg=%.2f "
            "handling_weight=%.2f positional_follow=%.2f "
            "rotational_follow=%.2f catch_up=%.2f damping_ratio=%.2f "
            "swing_attack=%u swing_trigger_mps=%.2f "
            "swing_rearm_mps=%.2f swing_pulse_ms=%u "
            "swing_cooldown_ms=%u "
            "collider_base=(%.2f,%.2f,%.2f) "
            "collider_tip=(%.2f,%.2f,%.2f) "
            "collider_radius=%.2f "
            "grip_position=(%.3f,%.3f,%.3f) "
            "grip_rotation=(%.6f,%.6f,%.6f,%.6f) "
            "secondary_grip=%u "
            "secondary_offset=(%.3f,%.3f,%.3f) "
            "secondary_grab_radius_m=%.3f "
            "kinematics_reset=1",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(selected.id),
            identity.recordName, identity.animationProperty,
            RetailWeaponPoseFamilyLabel(identity.poseFamily),
            RetailWeaponIdentityReadResultName(identityResult),
            identity.nameResolved ? 1U : 0U,
            identity.animationPropertyResolved ? 1U : 0U,
            selected.massKilograms, selected.handlingWeight,
            selected.positionalFollow, selected.rotationalFollow,
            selected.catchUpStrength, selected.dampingRatio,
            selected.swingAttackEnabled ? 1U : 0U,
            selected.swingAttackTriggerSpeedMetersPerSecond,
            selected.swingAttackRearmSpeedMetersPerSecond,
            selected.swingAttackPulseMilliseconds,
            selected.swingAttackCooldownMilliseconds,
            selected.localBaseOffsetUnits.x,
            selected.localBaseOffsetUnits.y,
            selected.localBaseOffsetUnits.z,
            selected.localTipOffsetUnits.x,
            selected.localTipOffsetUnits.y,
            selected.localTipOffsetUnits.z,
            selected.radiusUnits,
            selected.modelLocalGripPositionUnits.x,
            selected.modelLocalGripPositionUnits.y,
            selected.modelLocalGripPositionUnits.z,
            selected.modelLocalGripRotation.x,
            selected.modelLocalGripRotation.y,
            selected.modelLocalGripRotation.z,
            selected.modelLocalGripRotation.w,
            selected.secondaryGripEnabled ? 1U : 0U,
            selected.secondaryGripOffsetUnits.x,
            selected.secondaryGripOffsetUnits.y,
            selected.secondaryGripOffsetUnits.z,
            selected.secondaryGripGrabRadiusMeters);
        g_log("m5_physical_melee_profile_selected", detail);
    }
}

void UpdateEquippedWeaponVisualSource() noexcept {
    if (g_gameClientBase == nullptr) {
        return;
    }
    const bool visualProxyEnabled = InterlockedCompareExchange(
        &g_physicalMeleeVisualProxyEnabled, 0, 0) != 0;
    const bool ownershipRequired = visualProxyEnabled ||
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyEnabled, 0, 0) != 0 ||
        InterlockedCompareExchange(
            &g_physicalMeleeContactDamageEnabled, 0, 0) != 0 ||
        InterlockedCompareExchange(
            &g_controllerMeleeAimEnabled, 0, 0) != 0;
    if (g_interfaceManager != nullptr &&
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        PublishPhysicalMeleePlayerWeaponModel(nullptr);
        SelectPhysicalMeleeProfileForWeaponIndex(-1);
        PublishPhysicalMeleeAutomaticSeedCandidate(
            -1, nullptr, nullptr);
        if (visualProxyEnabled) {
            InvalidatePhysicalMeleeVisualProxySource();
        }
        return;
    }

    void* weaponManager = nullptr;
    std::int32_t currentWeaponIndex = -1;
    void* const* currentWeaponReference = nullptr;
    void* currentWeapon = nullptr;
    void* const* modelObjectReference = nullptr;
    void* modelObject = nullptr;
    bool readable = false;
    __try {
        std::memcpy(
            &weaponManager,
            g_gameClientBase + kWeaponManagerGlobalRva,
            sizeof(weaponManager));
        if (weaponManager != nullptr) {
            currentWeaponIndex =
                *reinterpret_cast<const std::int32_t*>(
                    static_cast<unsigned char*>(weaponManager) +
                    kCurrentWeaponIndexOffset);
        }
        if (weaponManager != nullptr && currentWeaponIndex != -1) {
            currentWeaponReference =
                reinterpret_cast<void* const*>(
                    static_cast<unsigned char*>(weaponManager) +
                    kCurrentWeaponOffset);
            std::memcpy(
                &currentWeapon, currentWeaponReference,
                sizeof(currentWeapon));
        }
        if (ownershipRequired && currentWeapon != nullptr) {
            modelObjectReference =
                reinterpret_cast<void* const*>(
                    static_cast<unsigned char*>(currentWeapon) +
                    kRightWeaponModelObjectOffset);
            std::memcpy(
                &modelObject, modelObjectReference,
                sizeof(modelObject));
        }
        readable = weaponManager != nullptr &&
            currentWeaponIndex != -1 &&
            currentWeaponReference != nullptr &&
            currentWeapon != nullptr;
        if (ownershipRequired) {
            readable = readable &&
                modelObjectReference != nullptr &&
                modelObject != nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    if (!readable) {
        PublishPhysicalMeleePlayerWeaponModel(nullptr);
        SelectPhysicalMeleeProfileForWeaponIndex(-1);
        PublishPhysicalMeleeAutomaticSeedCandidate(
            -1, nullptr, nullptr);
        if (visualProxyEnabled) {
            InvalidatePhysicalMeleeVisualProxySource();
        }
        return;
    }

    SelectPhysicalMeleeProfileForWeaponIndex(currentWeaponIndex);
    PublishPhysicalMeleePlayerWeaponModel(
        ownershipRequired ? modelObject : nullptr);
    PublishPhysicalMeleeAutomaticSeedCandidate(
        currentWeaponIndex, currentWeapon,
        ownershipRequired ? modelObject : nullptr);
    if (!visualProxyEnabled) {
        return;
    }

    float localGripPosition[3]{};
    float localGripRotation[4]{};
    AcquireSRWLockShared(&g_physicalMeleeLock);
    localGripPosition[0] =
        g_physicalMeleeProfile.modelLocalGripPositionUnits.x;
    localGripPosition[1] =
        g_physicalMeleeProfile.modelLocalGripPositionUnits.y;
    localGripPosition[2] =
        g_physicalMeleeProfile.modelLocalGripPositionUnits.z;
    localGripRotation[0] =
        g_physicalMeleeProfile.modelLocalGripRotation.x;
    localGripRotation[1] =
        g_physicalMeleeProfile.modelLocalGripRotation.y;
    localGripRotation[2] =
        g_physicalMeleeProfile.modelLocalGripRotation.z;
    localGripRotation[3] =
        g_physicalMeleeProfile.modelLocalGripRotation.w;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    PublishEquippedWeaponVisualProxySource(
        currentWeaponReference, currentWeapon,
        currentWeaponIndex,
        modelObjectReference, modelObject,
        localGripPosition, localGripRotation);
}

bool AimPathCommand(std::uint32_t command) noexcept {
    return command == kCondemnedFireCommand ||
        command == kCondemnedBlockCommand ||
        command == kCondemnedToggleMeleeCommand ||
        command == kCondemnedStunGunCommand;
}

void LogAimPathCommandEdge(
    std::uint32_t command,
    LONG active,
    bool controllerApplied,
    float retailValue,
    float outputValue) noexcept {
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0 ||
        !AimPathCommand(command) || g_log == nullptr) {
        return;
    }
    const ULONGLONG runtimeTick = GetTickCount64();
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    char stack[192]{};
    FormatGameClientStack(stack, sizeof(stack));
    char detail[512]{};
    std::snprintf(
        detail, sizeof(detail),
        "command=%u edge=%s controller_applied=%u "
        "retail_value=%.3f output_value=%.3f runtime_tick_ms=%llu "
        "controller_aim_valid=%u controller_forward=(%.4f,%.4f,%.4f) "
        "gameorig_stack_rvas=%s",
        command, active != 0 ? "down" : "up",
        controllerApplied ? 1U : 0U, retailValue, outputValue,
        static_cast<unsigned long long>(runtimeTick),
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y, controllerForward.z,
        stack);
    g_log("m5_aim_path_command_edge", detail);
}

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

static_assert(sizeof(InterfaceArrayAbi) == 12);
static_assert(sizeof(InterfaceDatabaseAbi) == 12);
static_assert(sizeof(InterfaceNameManagerAbi) == 24);

void* FindCurrentInterface(
    void* masterDatabase,
    const char* name,
    std::int32_t version) noexcept {
    __try {
        auto* const database =
            static_cast<InterfaceDatabaseAbi*>(masterDatabase);
        InterfaceArrayAbi* const array = database->interfaces;
        if (array == nullptr || array->count > array->capacity ||
            array->count > 4096U || array->items == nullptr) {
            return nullptr;
        }
        for (std::uint32_t index = 0; index < array->count; ++index) {
            auto* const manager =
                static_cast<InterfaceNameManagerAbi*>(
                    array->items[index]);
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

bool IsExecutableAddress(const void* address) noexcept {
    if (address == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(
            address, &information, sizeof(information)) !=
        sizeof(information) ||
        information.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protection = information.Protect &
        ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}


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
    DWORD protection = information.Protect &
        ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    const bool executable =
        protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    return executable && information.AllocationBase == module;
}

std::uint32_t DirectionMask(
    const LocomotionDirections& directions) noexcept {
    return (directions.forward ? 0x1U : 0U) |
           (directions.backward ? 0x2U : 0U) |
           (directions.left ? 0x4U : 0U) |
           (directions.right ? 0x8U : 0U);
}

void ReportDirectionTransition(std::uint32_t mask) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (mask == g_lastDirectionMask) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return;
    }
    g_lastDirectionMask = mask;
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (g_log != nullptr) {
        char detail[128]{};
        std::snprintf(
            detail, sizeof(detail),
            "directions=0x%X path=retail_binding_value "
            "direct_command_writes=0 system_input=0",
            mask);
        g_log("m4_binding_locomotion_applied", detail);
    }
}

void ReportTurnTransition(
    const TurningValue& turning,
    float retailValue,
    float outputValue) noexcept {
    const bool applied = turning.active &&
        std::isfinite(turning.value) &&
        std::isfinite(retailValue) &&
        std::fabs(turning.value) > std::fabs(retailValue);
    const int direction = !applied
        ? 0
        : turning.value < 0.0F ? -1 : 1;
    AcquireSRWLockExclusive(&g_bindingLock);
    if (direction == g_lastTurnDirection) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return;
    }
    g_lastTurnDirection = direction;
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (g_log != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=23 applied=%u direction=%d vr_value=%.3f "
            "retail_value=%.3f "
            "output_value=%.3f path=retail_extremal_value "
            "direct_command_writes=0 system_input=0",
            applied ? 1U : 0U, direction, turning.value,
            retailValue, outputValue);
        g_log("m4_binding_turning_applied", detail);
    }
}

void RequestCoreActionHaptic(
    std::uint32_t command,
    bool controllerApplied) noexcept {
    if (!controllerApplied || g_submitHapticRequest == nullptr ||
        InterlockedCompareExchange(&g_hapticsEnabled, 0, 0) == 0) {
        return;
    }
    const CoreActionHapticPulse pulse =
        ResolveCoreActionHapticPulse(command);
    if (!pulse.active) {
        return;
    }

    FearVrHapticRequest request{};
    request.requestId = static_cast<std::uint64_t>(
        InterlockedIncrement64(&g_hapticRequestId));
    request.durationNs = pulse.durationNs;
    request.amplitude = pulse.amplitude;
    request.frequency = 0.0F;
    request.handMask = pulse.handMask;
    request.flags = FEARVR_HF_VALID;
    BOOL submitted = FALSE;
    __try {
        submitted = g_submitHapticRequest(&request);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        submitted = FALSE;
    }
    if (submitted == FALSE) {
        if (InterlockedCompareExchange(
                &g_hapticFailureReported, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m4_controller_haptic_failed",
                "CondemnedVr_SubmitHapticRequest_failed");
        }
        return;
    }
    if (g_log != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=%u request_id=%llu hand_mask=0x%X "
            "duration_ns=%llu amplitude=%.2f "
            "source=vr_binding_rising_edge",
            command,
            static_cast<unsigned long long>(request.requestId),
            request.handMask,
            static_cast<unsigned long long>(request.durationNs),
            request.amplitude);
        g_log("m4_controller_haptic_requested", detail);
    }
}

void ReportInteractionTransition(
    const RetailBinding& binding,
    const ActivateValue& activate,
    float retailValue,
    float outputValue,
    int retailGameState) noexcept {
    const LONG active = activate.active ? 1 : 0;
    const bool controllerApplied = activate.active &&
        std::isfinite(activate.value) && std::isfinite(retailValue) &&
        std::fabs(activate.value) > std::fabs(retailValue);
    ObserveForensicMemoryCommandTransition(
        binding, kCondemnedActivateCommand, active, controllerApplied,
        retailValue, outputValue, retailGameState);
    if (InterlockedExchange(
            &g_lastInteractionActive, active) == active) {
        return;
    }
    RequestCoreActionHaptic(
        kCondemnedActivateCommand, controllerApplied);
    if (g_log != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=87 controller_active=%ld retail_value=%.3f "
            "output_value=%.3f game_state=%d "
            "button=right_squeeze path=retail_binding_value "
            "direct_command_writes=0 system_input=0",
            active, retailValue, outputValue, retailGameState);
        g_log("m4_binding_interaction_applied", detail);
    }
}

void ReportCoreActionTransition(
    const RetailBinding& binding,
    std::uint32_t command,
    const CoreActionValue& action,
    float retailValue,
    float outputValue,
    int retailGameState,
    bool automaticBlockPoseActive,
    bool automaticAttackSeedActive,
    bool automaticAttackSeedOnly) noexcept {
    const int index = CondemnedCoreActionIndex(command);
    if (index < 0) {
        return;
    }
    const LONG active = action.active ? 1 : 0;
    const bool controllerApplied = action.active &&
        std::isfinite(action.value) && std::isfinite(retailValue) &&
        std::fabs(action.value) > std::fabs(retailValue);
    ObserveForensicMemoryCommandTransition(
        binding, command, active, controllerApplied,
        retailValue, outputValue, retailGameState);
    if (InterlockedExchange(
            &g_lastCoreActionActive[index], active) == active) {
        return;
    }
    LogAimPathCommandEdge(
        command, active, controllerApplied, retailValue, outputValue);
    RequestCoreActionHaptic(
        command,
        controllerApplied && !automaticAttackSeedOnly);
    if (g_log != nullptr) {
        char detail[448]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=%u controller_active=%ld retail_value=%.3f "
            "output_value=%.3f game_state=%d control=%s "
            "automatic_block_pose=%u "
            "automatic_attack_seed=%u automatic_seed_only=%u "
            "automatic_seed_haptic_blocked=%u "
            "path=retail_binding_value direct_command_writes=0 "
            "system_input=0",
            command, active, retailValue, outputValue,
            retailGameState,
            CondemnedCoreActionControlName(command),
            automaticBlockPoseActive ? 1U : 0U,
            automaticAttackSeedActive ? 1U : 0U,
            automaticAttackSeedOnly ? 1U : 0U,
            automaticAttackSeedOnly ? 1U : 0U);
        g_log("m4_binding_core_action_applied", detail);
    }
}

float ActiveBindingValue(const RetailBinding& binding) noexcept {
    if (!std::isfinite(binding.commandMin) ||
        !std::isfinite(binding.commandMax) ||
        binding.commandMin > binding.commandMax) {
        return 1.0F;
    }
    return std::clamp(1.0F, binding.commandMin, binding.commandMax);
}

bool DirectionActive(
    std::uint32_t command,
    const LocomotionDirections& directions) noexcept {
    switch (command) {
    case 0:
        return directions.forward;
    case 1:
        return directions.backward;
    case 3:
        return directions.left;
    case 4:
        return directions.right;
    default:
        return false;
    }
}

thread_local bool g_playerColliderSetDimensionsActive = false;
thread_local bool
    g_playerColliderSetObjectDimensionsTraceActive = false;

PlayerColliderSettings CurrentPlayerColliderSettings() noexcept {
    PlayerColliderSettings settings{};
    settings.widthScale =
        static_cast<float>(InterlockedCompareExchange(
            &g_playerColliderScaleBasisPoints, 0, 0)) /
        10000.0F;
    if (!PlayerColliderSettingsAreValid(settings)) {
        settings = {};
    }
    return settings;
}

PlayerColliderDimensions ToPlayerColliderDimensions(
    const VectorAbi& dimensions) noexcept {
    return {dimensions.x, dimensions.y, dimensions.z};
}

VectorAbi ToVectorAbi(
    const PlayerColliderDimensions& dimensions) noexcept {
    return {dimensions.x, dimensions.y, dimensions.z};
}

bool ReadPlayerColliderManagerDimensions(
    void* moveManager,
    std::size_t offset,
    PlayerColliderDimensions& dimensions) noexcept {
    dimensions = {};
    if (moveManager == nullptr) {
        return false;
    }

    VectorAbi raw{};
    bool readable = false;
    __try {
        std::memcpy(
            &raw,
            static_cast<unsigned char*>(moveManager) + offset,
            sizeof(raw));
        readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    const PlayerColliderDimensions observed =
        ToPlayerColliderDimensions(raw);
    if (!readable ||
        !PlayerColliderDimensionsAreValid(observed)) {
        return false;
    }
    dimensions = observed;
    return true;
}

bool ResolvePlayerColliderContext(
    void*& moveManager,
    void*& playerObject,
    void*& physics,
    GetObjectDimensionsFunction& getDimensions,
    SetObjectDimensionsFunction& setDimensions) noexcept {
    moveManager = nullptr;
    playerObject = nullptr;
    physics = nullptr;
    getDimensions = nullptr;
    setDimensions = nullptr;
    if (g_gameClientBase == nullptr) {
        return false;
    }

    void** vtable = nullptr;
    __try {
        std::memcpy(
            &moveManager,
            g_gameClientBase + kMeleeClientGlobalRva,
            sizeof(moveManager));
        std::memcpy(
            &physics,
            g_gameClientBase + kClientPhysicsGlobalRva,
            sizeof(physics));
        if (moveManager != nullptr) {
            std::memcpy(
                &playerObject,
                static_cast<unsigned char*>(moveManager) +
                    kPlayerObjectOffset,
                sizeof(playerObject));
        }
        if (physics != nullptr) {
            vtable = *static_cast<void***>(physics);
        }
        if (vtable != nullptr) {
            getDimensions =
                reinterpret_cast<GetObjectDimensionsFunction>(
                    vtable[kGetObjectDimensionsVtableSlot]);
            setDimensions =
                reinterpret_cast<SetObjectDimensionsFunction>(
                    vtable[kSetObjectDimensionsVtableSlot]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    HMODULE const executable = GetModuleHandleW(nullptr);
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executable);
    void** const expectedVtable = executableBase != nullptr
        ? reinterpret_cast<void**>(
              executableBase +
              kClientPhysicsVtableExecutableRva)
        : nullptr;
    return moveManager != nullptr && playerObject != nullptr &&
        physics != nullptr && vtable != nullptr &&
        vtable == expectedVtable &&
        reinterpret_cast<void*>(getDimensions) ==
            executableBase + kGetObjectDimensionsExecutableRva &&
        reinterpret_cast<void*>(setDimensions) ==
            executableBase + kSetObjectDimensionsExecutableRva &&
        IsExecutableModuleAddress(getDimensions, executable) &&
        IsExecutableModuleAddress(setDimensions, executable);
}

bool ReadPlayerColliderActualDimensions(
    void* physics,
    void* playerObject,
    GetObjectDimensionsFunction getDimensions,
    PlayerColliderDimensions& dimensions) noexcept {
    dimensions = {};
    if (physics == nullptr || playerObject == nullptr ||
        getDimensions == nullptr) {
        return false;
    }

    VectorAbi actual{};
    std::uint32_t result = ~kLithTechOk;
    __try {
        result = getDimensions(physics, playerObject, &actual);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    const PlayerColliderDimensions observed =
        ToPlayerColliderDimensions(actual);
    if (result != kLithTechOk ||
        !PlayerColliderDimensionsAreValid(observed)) {
        return false;
    }
    dimensions = observed;
    return true;
}

struct PlayerColliderWriterCaller {
    void* moduleBase{nullptr};
    std::uintptr_t returnRva{0U};
    PlayerColliderSetDimensionsCallsite callsite{
        PlayerColliderSetDimensionsCallsite::Unknown};
    bool resolved{false};
};

bool ResolvePlayerColliderWriterCaller(
    void* returnAddress,
    PlayerColliderWriterCaller& caller) noexcept {
    caller = {};
    if (returnAddress == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(
            returnAddress, &information, sizeof(information)) !=
            sizeof(information) ||
        information.AllocationBase == nullptr ||
        information.Type != MEM_IMAGE) {
        return false;
    }
    caller.moduleBase = information.AllocationBase;
    const std::uintptr_t returnValue =
        reinterpret_cast<std::uintptr_t>(returnAddress);
    const std::uintptr_t moduleValue =
        reinterpret_cast<std::uintptr_t>(caller.moduleBase);
    if (returnValue < moduleValue) {
        caller = {};
        return false;
    }
    caller.returnRva = returnValue - moduleValue;
    if (caller.moduleBase == g_gameClientBase) {
        caller.callsite =
            ClassifyPlayerColliderSetDimensionsReturnRva(
                caller.returnRva);
    }
    caller.resolved = true;
    return true;
}

bool ReservePlayerColliderWriterEvent(
    const PlayerColliderWriterCaller& caller,
    LONG& eventIndex,
    const char*& stream) noexcept {
    volatile LONG* counter = nullptr;
    LONG limit = 0;
    if (!caller.resolved) {
        counter = &g_playerColliderWriterUnresolvedEvents;
        limit = kPlayerColliderWriterUnresolvedEventCap;
        stream = "unresolved_local";
    } else if (caller.moduleBase == g_gameClientBase) {
        if (caller.callsite !=
                PlayerColliderSetDimensionsCallsite::Unknown) {
            counter = &g_playerColliderWriterKnownEvents;
            limit = kPlayerColliderWriterKnownEventCap;
            stream = "known_gameorig";
        } else {
            counter = &g_playerColliderWriterUnknownGameEvents;
            limit = kPlayerColliderWriterUnknownGameEventCap;
            stream = "unclassified_gameorig";
        }
    } else if (caller.moduleBase ==
            g_playerColliderTraceExecutable) {
        counter = &g_playerColliderWriterExecutableEvents;
        limit = kPlayerColliderWriterExecutableEventCap;
        stream = "unclassified_executable";
    } else {
        counter = &g_playerColliderWriterExternalEvents;
        limit = kPlayerColliderWriterExternalEventCap;
        stream = "external_local";
    }
    eventIndex = InterlockedIncrement(counter);
    return eventIndex <= limit;
}

bool ReadPlayerColliderTraceDimensions(
    const VectorAbi* source,
    PlayerColliderDimensions& dimensions,
    bool& finite) noexcept {
    dimensions = {};
    finite = false;
    if (source == nullptr) {
        return false;
    }
    VectorAbi raw{};
    __try {
        std::memcpy(&raw, source, sizeof(raw));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    dimensions = ToPlayerColliderDimensions(raw);
    finite = std::isfinite(dimensions.x) &&
        std::isfinite(dimensions.y) &&
        std::isfinite(dimensions.z);
    return true;
}

bool ReadFiniteDiagnosticPoint(
    const float (&position)[3],
    PlayerCollisionDiagnosticPoint& point) noexcept {
    point = {position[0], position[1], position[2]};
    return std::isfinite(point.x) && std::isfinite(point.y) &&
        std::isfinite(point.z);
}

void PublishPlayerCollisionXrayTarget(
    std::uintptr_t object,
    const VectorAbi* contact) noexcept {
    if (InterlockedCompareExchange(
            &g_playerCollisionXrayEnabled, 0, 0) == 0 ||
        object == 0U) {
        return;
    }
    PlayerCollisionDiagnosticPoint point{};
    const bool contactValid = contact != nullptr &&
        std::isfinite(contact->x) && std::isfinite(contact->y) &&
        std::isfinite(contact->z);
    if (contactValid) {
        point = {contact->x, contact->y, contact->z};
    }
    AcquireSRWLockExclusive(&g_playerCollisionXrayLock);
    g_playerCollisionXrayTargetObject = object;
    g_playerCollisionXrayContactPoint = point;
    g_playerCollisionXrayContactValid = contactValid;
    g_playerCollisionXrayTargetTick = GetTickCount64();
    g_playerCollisionXraySnapshot.contactPoint = point;
    g_playerCollisionXraySnapshot.contactValid = contactValid;
    ReleaseSRWLockExclusive(&g_playerCollisionXrayLock);
}

bool SamplePlayerCollisionXray(
    PlayerCollisionXraySnapshot& snapshot) noexcept {
    snapshot = {};
    snapshot.enabled = InterlockedCompareExchange(
        &g_playerCollisionXrayEnabled, 0, 0) != 0;
    snapshot.movementTraceReady = InterlockedCompareExchange(
        &g_playerCollisionVelocityHookOperational, 0, 0) != 0;
    if (!snapshot.enabled || !snapshot.movementTraceReady ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying ||
        !ProcessOwnsForegroundWindow()) {
        return false;
    }

    void* moveManager = nullptr;
    void* playerObject = nullptr;
    void* physics = nullptr;
    GetObjectDimensionsFunction getDimensions = nullptr;
    SetObjectDimensionsFunction setDimensions = nullptr;
    if (!ResolvePlayerColliderContext(
            moveManager, playerObject, physics,
            getDimensions, setDimensions)) {
        return false;
    }
    (void)moveManager;
    (void)setDimensions;
    snapshot.playerObject =
        reinterpret_cast<std::uintptr_t>(playerObject);
    snapshot.playerValid = ReadPlayerColliderActualDimensions(
        physics, playerObject, getDimensions,
        snapshot.playerDimensions);
    float playerPosition[3]{};
    float playerRotation[4]{};
    PlayerCollisionDiagnosticPoint playerPoint{};
    snapshot.playerValid = snapshot.playerValid &&
        ReadDiagnosticObjectRigidTransform(
            playerObject, playerPosition, playerRotation) &&
        ReadFiniteDiagnosticPoint(playerPosition, playerPoint);
    if (snapshot.playerValid) {
        snapshot.playerOrigin = playerPoint;
    }

    float headPosition[3]{};
    float headRotation[4]{};
    PlayerCollisionDiagnosticPoint headPoint{};
    snapshot.headValid = ReadTrackedHeadWorldPose(
            headPosition, headRotation) &&
        ReadFiniteDiagnosticPoint(headPosition, headPoint);
    if (snapshot.headValid) {
        snapshot.headOrigin = headPoint;
    }

    const ULONGLONG now = GetTickCount64();
    std::uintptr_t targetObject = 0U;
    ULONGLONG targetTick = 0U;
    AcquireSRWLockShared(&g_playerCollisionXrayLock);
    targetObject = g_playerCollisionXrayTargetObject;
    targetTick = g_playerCollisionXrayTargetTick;
    snapshot.contactPoint = g_playerCollisionXrayContactPoint;
    const bool storedContactValid =
        g_playerCollisionXrayContactValid;
    ReleaseSRWLockShared(&g_playerCollisionXrayLock);
    if (targetObject != 0U && targetObject != snapshot.playerObject &&
        targetTick != 0U && now >= targetTick && now - targetTick <=
            kPlayerCollisionTargetFreshnessMilliseconds) {
        snapshot.contactValid = storedContactValid;
        snapshot.targetAgeMilliseconds =
            static_cast<std::uint64_t>(now - targetTick);
        float targetPosition[3]{};
        float targetRotation[4]{};
        PlayerCollisionDiagnosticPoint targetPoint{};
        snapshot.targetObject = targetObject;
        snapshot.targetValid = ReadPlayerColliderActualDimensions(
                physics, reinterpret_cast<void*>(targetObject),
                getDimensions, snapshot.targetDimensions) &&
            ReadDiagnosticObjectRigidTransform(
                reinterpret_cast<void*>(targetObject),
                targetPosition, targetRotation) &&
            ReadFiniteDiagnosticPoint(targetPosition, targetPoint);
        if (snapshot.targetValid) {
            snapshot.targetOrigin = targetPoint;
        }
    }
    AcquireSRWLockShared(&g_bindingLock);
    snapshot.locomotionDirectionMask = g_lastDirectionMask;
    ReleaseSRWLockShared(&g_bindingLock);
    snapshot.tickMilliseconds = now;
    snapshot.updateSequence = static_cast<std::uint64_t>(
        InterlockedIncrement64(&g_playerCollisionTimelineSequence));
    if (snapshot.playerValid && snapshot.headValid) {
        snapshot.headToPlayerHorizontalUnits =
            PlayerCollisionDiagnosticHorizontalDistance(
                snapshot.playerOrigin, snapshot.headOrigin);
    }
    if (snapshot.playerValid && snapshot.targetValid) {
        snapshot.playerToTargetHorizontalUnits =
            PlayerCollisionDiagnosticHorizontalDistance(
                snapshot.playerOrigin, snapshot.targetOrigin);
        snapshot.diagnosticProxyHorizontalGapUnits =
            PlayerCollisionDiagnosticProxyHorizontalGap(
                BuildPlayerCollisionDiagnosticProxy(
                    snapshot.playerOrigin, snapshot.playerDimensions),
                BuildPlayerCollisionDiagnosticProxy(
                    snapshot.targetOrigin, snapshot.targetDimensions));
    }
    return snapshot.playerValid;
}

void ObservePlayerCollisionXrayUpdate(bool beforeRetail) noexcept {
    PlayerCollisionXraySnapshot sample{};
    if (!SamplePlayerCollisionXray(sample)) {
        return;
    }
    if (beforeRetail) {
        AcquireSRWLockExclusive(&g_playerCollisionXrayLock);
        g_playerCollisionXrayPreUpdate = sample;
        ReleaseSRWLockExclusive(&g_playerCollisionXrayLock);
        return;
    }

    PlayerCollisionXraySnapshot before{};
    AcquireSRWLockExclusive(&g_playerCollisionXrayLock);
    before = g_playerCollisionXrayPreUpdate;
    g_playerCollisionXraySnapshot = sample;
    ReleaseSRWLockExclusive(&g_playerCollisionXrayLock);
    const ULONGLONG now = sample.tickMilliseconds;
    if (g_log == nullptr ||
        (sample.locomotionDirectionMask == 0U && !sample.targetValid) ||
        now - g_playerCollisionXrayLastUpdateLogTick <
            kPlayerCollisionUpdateLogIntervalMilliseconds ||
        InterlockedIncrement(&g_playerCollisionUpdateEvents) >
            kPlayerCollisionUpdateEventCap) {
        return;
    }
    g_playerCollisionXrayLastUpdateLogTick = now;
    const float displacement = before.playerValid
        ? PlayerCollisionDiagnosticHorizontalDistance(
              before.playerOrigin, sample.playerOrigin)
        : -1.0F;
    char detail[2048]{};
    std::snprintf(
        detail, sizeof(detail),
        "sequence=%llu tick=%llu player=0x%08lX target=0x%08lX "
        "locomotion_direction_mask=0x%X "
        "pre_valid=%u pre_origin=(%.3f,%.3f,%.3f) "
        "pre_dims=(%.3f,%.3f,%.3f) "
        "post_origin=(%.3f,%.3f,%.3f) post_dims=(%.3f,%.3f,%.3f) "
        "post_displacement_horizontal=%.3f "
        "head_valid=%u head_origin=(%.3f,%.3f,%.3f) "
        "head_to_player_horizontal=%.3f "
        "target_valid=%u target_age_ms=%llu "
        "target_origin=(%.3f,%.3f,%.3f) "
        "target_dims=(%.3f,%.3f,%.3f) "
        "player_to_target_horizontal=%.3f "
        "diagnostic_proxy_horizontal_gap=%.3f contact_valid=%u "
        "contact=(%.3f,%.3f,%.3f) "
        "geometry=diagnostic_proxy true_physics_geometry_verified=0 "
        "hmd_distance_interpreted_as_player_radius=0 mutation=none",
        static_cast<unsigned long long>(sample.updateSequence),
        static_cast<unsigned long long>(sample.tickMilliseconds),
        static_cast<unsigned long>(sample.playerObject),
        static_cast<unsigned long>(sample.targetObject),
        static_cast<unsigned int>(sample.locomotionDirectionMask),
        before.playerValid ? 1U : 0U,
        before.playerOrigin.x, before.playerOrigin.y,
        before.playerOrigin.z, before.playerDimensions.x,
        before.playerDimensions.y, before.playerDimensions.z,
        sample.playerOrigin.x, sample.playerOrigin.y,
        sample.playerOrigin.z, sample.playerDimensions.x,
        sample.playerDimensions.y, sample.playerDimensions.z,
        displacement, sample.headValid ? 1U : 0U,
        sample.headOrigin.x, sample.headOrigin.y, sample.headOrigin.z,
        sample.headToPlayerHorizontalUnits,
        sample.targetValid ? 1U : 0U,
        static_cast<unsigned long long>(
            sample.targetAgeMilliseconds),
        sample.targetOrigin.x, sample.targetOrigin.y,
        sample.targetOrigin.z, sample.targetDimensions.x,
        sample.targetDimensions.y, sample.targetDimensions.z,
        sample.playerToTargetHorizontalUnits,
        sample.diagnosticProxyHorizontalGapUnits,
        sample.contactValid ? 1U : 0U,
        sample.contactPoint.x, sample.contactPoint.y,
        sample.contactPoint.z);
    g_log("m5_player_collision_xray_update", detail);
}

std::uint32_t __fastcall HookPlayerSetObjectDimensionsTrace(
    void* physics,
    void* ignoredEdx,
    void* object,
    VectorAbi* dimensions,
    std::uint32_t flags) {
    void* const returnAddress = _ReturnAddress();
    (void)ignoredEdx;
    const bool operational = InterlockedCompareExchange(
        &g_playerColliderWriterHookOperational, 0, 0) != 0;
    SetObjectDimensionsFunction const original =
        g_originalPlayerSetObjectDimensionsTrace;
    if (!operational) {
        return ForwardPlayerColliderSetDimensionsExactlyOnce(
            original, physics, object, dimensions, flags);
    }
    if (g_playerColliderSetObjectDimensionsTraceActive) {
        return ForwardPlayerColliderSetDimensionsExactlyOnce(
            original, physics, object, dimensions, flags);
    }

    const PlayerColliderSettings settings =
        CurrentPlayerColliderSettings();
    const bool pending = InterlockedCompareExchange(
        &g_playerColliderReapplyPending, 0, 0) != 0;
    if (!PlayerColliderDimensionAuditRequired(settings, pending) ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return ForwardPlayerColliderSetDimensionsExactlyOnce(
            original, physics, object, dimensions, flags);
    }

    void* moveManager = nullptr;
    void* playerObject = nullptr;
    void* resolvedPhysics = nullptr;
    GetObjectDimensionsFunction getDimensions = nullptr;
    SetObjectDimensionsFunction setDimensions = nullptr;
    if (!ResolvePlayerColliderContext(
            moveManager, playerObject, resolvedPhysics,
            getDimensions, setDimensions) ||
        physics != resolvedPhysics || object != playerObject ||
        reinterpret_cast<void*>(setDimensions) !=
            reinterpret_cast<unsigned char*>(
                g_playerColliderTraceExecutable) +
                kSetObjectDimensionsExecutableRva) {
        return ForwardPlayerColliderSetDimensionsExactlyOnce(
            original, physics, object, dimensions, flags);
    }

    PlayerColliderWriterCaller caller{};
    (void)ResolvePlayerColliderWriterCaller(
        returnAddress, caller);
    LONG eventIndex = 0;
    const char* eventStream = "unknown";
    if (!ReservePlayerColliderWriterEvent(
            caller, eventIndex, eventStream)) {
        return ForwardPlayerColliderSetDimensionsExactlyOnce(
            original, physics, object, dimensions, flags);
    }

    const LONG64 sequence = InterlockedIncrement64(
        &g_playerColliderWriterNextSequence);
    const LONG64 collisionTimelineSequence = InterlockedIncrement64(
        &g_playerCollisionTimelineSequence);
    const DWORD threadId = GetCurrentThreadId();
    const ULONGLONG tick = GetTickCount64();
    std::uint32_t directionMask = 0U;
    AcquireSRWLockShared(&g_bindingLock);
    directionMask = g_lastDirectionMask;
    ReleaseSRWLockShared(&g_bindingLock);

    PlayerColliderDimensions requestIn{};
    bool requestInFinite = false;
    const bool requestInReadable =
        ReadPlayerColliderTraceDimensions(
            dimensions, requestIn, requestInFinite);
    const bool requestInValid = requestInReadable &&
        PlayerColliderDimensionsAreValid(requestIn);
    PlayerColliderDimensions actualBefore{};
    const bool actualBeforeValid =
        ReadPlayerColliderActualDimensions(
            physics, object, getDimensions, actualBefore);
    PlayerColliderDimensions managerRequested{};
    PlayerColliderDimensions manager40cSourceCandidate{};
    PlayerColliderDimensions adjacentDimensionsCandidate{};
    const bool managerRequestedValid =
        ReadPlayerColliderManagerDimensions(
            moveManager, kPlayerRequestedDimensionsOffset,
            managerRequested);
    const bool manager40cSourceCandidateValid =
        ReadPlayerColliderManagerDimensions(
            moveManager,
            kPlayerManager40cSourceCandidateOffset,
            manager40cSourceCandidate);
    const bool adjacentDimensionsCandidateValid =
        ReadPlayerColliderManagerDimensions(
            moveManager, kPlayerAdjacentDimensionsCandidateOffset,
            adjacentDimensionsCandidate);

    std::uint32_t nativeResult = 0U;
    g_playerColliderSetObjectDimensionsTraceActive = true;
    __try {
        nativeResult =
            ForwardPlayerColliderSetDimensionsExactlyOnce(
                original, physics, object, dimensions, flags);
    } __finally {
        g_playerColliderSetObjectDimensionsTraceActive = false;
    }

    PlayerColliderDimensions requestOut{};
    bool requestOutFinite = false;
    const bool requestOutReadable =
        ReadPlayerColliderTraceDimensions(
            dimensions, requestOut, requestOutFinite);
    const bool requestOutValid = requestOutReadable &&
        PlayerColliderDimensionsAreValid(requestOut);

    void* afterManager = nullptr;
    void* afterPlayerObject = nullptr;
    void* afterPhysics = nullptr;
    GetObjectDimensionsFunction afterGetDimensions = nullptr;
    SetObjectDimensionsFunction afterSetDimensions = nullptr;
    const bool contextStable =
        ResolvePlayerColliderContext(
            afterManager, afterPlayerObject, afterPhysics,
            afterGetDimensions, afterSetDimensions) &&
        afterManager == moveManager &&
        afterPlayerObject == playerObject &&
        afterPhysics == physics &&
        reinterpret_cast<void*>(afterSetDimensions) ==
            reinterpret_cast<unsigned char*>(
                g_playerColliderTraceExecutable) +
                kSetObjectDimensionsExecutableRva;
    PlayerColliderDimensions actualAfter{};
    const bool actualAfterValid = contextStable &&
        ReadPlayerColliderActualDimensions(
            afterPhysics, afterPlayerObject,
            afterGetDimensions, actualAfter);
    const bool actualChangedBeyondTolerance =
        actualBeforeValid && actualAfterValid &&
        !PlayerColliderDimensionsMatch(
            actualBefore, actualAfter);

    const bool callerIsGameOrig =
        caller.moduleBase == g_gameClientBase;
    const bool callerIsExecutable =
        caller.moduleBase == g_playerColliderTraceExecutable;
    const char* const callerModule = !caller.resolved
        ? "unresolved"
        : callerIsGameOrig
            ? "GameOrig"
            : callerIsExecutable ? "Condemned" : "other";
    const std::uintptr_t callRva =
        callerIsGameOrig
        ? PlayerColliderSetDimensionsCallInstructionRva(
              caller.callsite)
        : 0U;
    const bool callRvaValid = callRva != 0U;
    const bool writerControlPath =
        caller.callsite ==
            PlayerColliderSetDimensionsCallsite::WriterLiteralHalf ||
        caller.callsite ==
            PlayerColliderSetDimensionsCallsite::WriterPrimary ||
        caller.callsite ==
            PlayerColliderSetDimensionsCallsite::WriterRetry;

    if (g_log != nullptr) {
        char detail[3072]{};
        std::snprintf(
            detail, sizeof(detail),
            "stream=%s count=%ld sequence=%lld "
            "collision_timeline_sequence=%lld thread=%lu tick=%llu "
            "caller_module=%s caller_module_base=%p "
            "caller_return_address=%p return_rva_valid=%u "
            "return_rva=0x%08lX "
            "call_rva_valid=%u call_rva=0x%08lX "
            "callsite=%s control_path=%s "
            "manager=%p player=%p physics=%p scale=%.2f pending=%u "
            "locomotion_direction_mask=0x%X "
            "request_in_readable=%u request_in_finite=%u "
            "request_in_valid=%u request_in=(%.3f,%.3f,%.3f) "
            "flags=0x%08X "
            "actual_before_valid=%u actual_before=(%.3f,%.3f,%.3f) "
            "manager_requested_valid=%u "
            "manager_requested=(%.3f,%.3f,%.3f) "
            "manager_40c_source_candidate_valid=%u "
            "manager_40c_source_candidate=(%.3f,%.3f,%.3f) "
            "adjacent_dimensions_candidate_valid=%u "
            "adjacent_dimensions_candidate=(%.3f,%.3f,%.3f) "
            "native_result=0x%08X "
            "request_out_readable=%u request_out_finite=%u "
            "request_out_valid=%u request_out=(%.3f,%.3f,%.3f) "
            "context_stable=%u actual_after_valid=%u "
            "actual_after=(%.3f,%.3f,%.3f) "
            "actual_changed_beyond_tolerance=%u actual_change_tolerance=0.050 "
            "native_setdims_executed=1 "
            "request_pointer_forwarded_unchanged=1 "
            "flags_forwarded_unchanged=1 "
            "native_result_preserved=1 observer_added_engine_state_writes=0 "
            "observer_setdims_calls_added=0",
            eventStream, static_cast<long>(eventIndex),
            static_cast<long long>(sequence),
            static_cast<long long>(collisionTimelineSequence),
            static_cast<unsigned long>(threadId),
            static_cast<unsigned long long>(tick),
            callerModule, caller.moduleBase,
            returnAddress,
            caller.resolved ? 1U : 0U,
            static_cast<unsigned long>(caller.returnRva),
            callRvaValid ? 1U : 0U,
            static_cast<unsigned long>(callRva),
            PlayerColliderSetDimensionsCallsiteName(
                caller.callsite),
            writerControlPath
                ? "verified_344e0_internal" : "other",
            moveManager, playerObject, physics,
            settings.widthScale, pending ? 1U : 0U,
            static_cast<unsigned int>(directionMask),
            requestInReadable ? 1U : 0U,
            requestInFinite ? 1U : 0U,
            requestInValid ? 1U : 0U,
            requestIn.x, requestIn.y, requestIn.z,
            static_cast<unsigned int>(flags),
            actualBeforeValid ? 1U : 0U,
            actualBefore.x, actualBefore.y, actualBefore.z,
            managerRequestedValid ? 1U : 0U,
            managerRequested.x, managerRequested.y,
            managerRequested.z,
            manager40cSourceCandidateValid ? 1U : 0U,
            manager40cSourceCandidate.x,
            manager40cSourceCandidate.y,
            manager40cSourceCandidate.z,
            adjacentDimensionsCandidateValid ? 1U : 0U,
            adjacentDimensionsCandidate.x,
            adjacentDimensionsCandidate.y,
            adjacentDimensionsCandidate.z,
            static_cast<unsigned int>(nativeResult),
            requestOutReadable ? 1U : 0U,
            requestOutFinite ? 1U : 0U,
            requestOutValid ? 1U : 0U,
            requestOut.x, requestOut.y, requestOut.z,
            contextStable ? 1U : 0U,
            actualAfterValid ? 1U : 0U,
            actualAfter.x, actualAfter.y, actualAfter.z,
            actualChangedBeyondTolerance ? 1U : 0U);
        g_log(
            "m5_player_collider_setdims_observed",
            detail);
    }
    return nativeResult;
}

std::uint32_t __fastcall HookPlayerCollisionSetVelocity(
    void* physics,
    void* ignoredEdx,
    void* object,
    const VectorAbi* velocity) {
    (void)ignoredEdx;
    SetVelocityFunction const original =
        g_originalPlayerCollisionSetVelocity;
    const bool observing = InterlockedCompareExchange(
            &g_playerCollisionVelocityHookOperational, 0, 0) != 0 &&
        InterlockedCompareExchange(
            &g_playerCollisionXrayEnabled, 0, 0) != 0 &&
        ReadRetailGameState(g_interfaceManager) ==
            kCondemnedGameStatePlaying &&
        ProcessOwnsForegroundWindow();
    if (!observing) {
        return ForwardPlayerCollisionSetVelocityExactlyOnce(
            original, physics, object, velocity);
    }

    void* moveManager = nullptr;
    void* playerObject = nullptr;
    void* resolvedPhysics = nullptr;
    GetObjectDimensionsFunction getDimensions = nullptr;
    SetObjectDimensionsFunction setDimensions = nullptr;
    if (!ResolvePlayerColliderContext(
            moveManager, playerObject, resolvedPhysics,
            getDimensions, setDimensions) ||
        physics != resolvedPhysics || object != playerObject) {
        return ForwardPlayerCollisionSetVelocityExactlyOnce(
            original, physics, object, velocity);
    }
    (void)moveManager;
    (void)getDimensions;
    (void)setDimensions;
    VectorAbi requested{};
    bool velocityReadable = false;
    __try {
        if (velocity != nullptr) {
            requested = *velocity;
            velocityReadable = std::isfinite(requested.x) &&
                std::isfinite(requested.y) &&
                std::isfinite(requested.z);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        velocityReadable = false;
    }
    if (!velocityReadable) {
        return ForwardPlayerCollisionSetVelocityExactlyOnce(
            original, physics, object, velocity);
    }

    PlayerCollisionXraySnapshot before{};
    (void)SamplePlayerCollisionXray(before);
    const std::uint32_t result =
        ForwardPlayerCollisionSetVelocityExactlyOnce(
            original, physics, object, velocity);
    PlayerCollisionXraySnapshot after{};
    (void)SamplePlayerCollisionXray(after);
    AcquireSRWLockExclusive(&g_playerCollisionXrayLock);
    g_playerCollisionXraySnapshot = after;
    ReleaseSRWLockExclusive(&g_playerCollisionXrayLock);

    const LONG eventIndex = InterlockedIncrement(
        &g_playerCollisionVelocityEvents);
    if (eventIndex <= kPlayerCollisionVelocityEventCap &&
        g_log != nullptr) {
        char detail[1536]{};
        std::snprintf(
            detail, sizeof(detail),
            "count=%ld sequence=%llu thread=%lu tick=%llu "
            "target=Condemned+0x00007CD0 player=%p physics=%p "
            "requested_velocity=(%.3f,%.3f,%.3f) "
            "before_valid=%u before_origin=(%.3f,%.3f,%.3f) "
            "before_dims=(%.3f,%.3f,%.3f) "
            "after_valid=%u after_origin=(%.3f,%.3f,%.3f) "
            "after_dims=(%.3f,%.3f,%.3f) native_result=0x%08X "
            "object_and_pointer_forwarded_unchanged=1 "
            "native_call_count=1 collision_result_observed=0 "
            "movement_semantics=velocity_handoff_not_collision_result "
            "mutation=none",
            static_cast<long>(eventIndex),
            static_cast<unsigned long long>(after.updateSequence),
            static_cast<unsigned long>(GetCurrentThreadId()),
            static_cast<unsigned long long>(GetTickCount64()),
            object, physics, requested.x, requested.y, requested.z,
            before.playerValid ? 1U : 0U,
            before.playerOrigin.x, before.playerOrigin.y,
            before.playerOrigin.z, before.playerDimensions.x,
            before.playerDimensions.y, before.playerDimensions.z,
            after.playerValid ? 1U : 0U,
            after.playerOrigin.x, after.playerOrigin.y,
            after.playerOrigin.z, after.playerDimensions.x,
            after.playerDimensions.y, after.playerDimensions.z,
            static_cast<unsigned int>(result));
        g_log("m5_player_collision_xray_velocity_handoff", detail);
    }
    return result;
}

void ResetPlayerColliderDimensionObservation() noexcept {
    AcquireSRWLockExclusive(&g_playerColliderLock);
    if (g_playerColliderLastObservedDimensionsValid) {
        g_playerColliderLastObservedDimensions = {};
        g_playerColliderLastObservedObject = 0U;
        g_playerColliderLastObservedDimensionsValid = false;
    }
    ReleaseSRWLockExclusive(&g_playerColliderLock);
}

void ObservePlayerColliderDimensionTransition(
    const char* phase,
    bool forceEvent) noexcept {
    if (g_log == nullptr) {
        return;
    }

    const PlayerColliderSettings settings =
        CurrentPlayerColliderSettings();
    const bool reapplyPending =
        InterlockedCompareExchange(
            &g_playerColliderReapplyPending, 0, 0) != 0;
    if ((!forceEvent &&
            !PlayerColliderDimensionAuditRequired(
                settings, reapplyPending)) ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        ResetPlayerColliderDimensionObservation();
        return;
    }

    void* moveManager = nullptr;
    void* playerObject = nullptr;
    void* physics = nullptr;
    GetObjectDimensionsFunction getDimensions = nullptr;
    SetObjectDimensionsFunction setDimensions = nullptr;
    if (!ResolvePlayerColliderContext(
            moveManager, playerObject, physics,
            getDimensions, setDimensions)) {
        return;
    }

    PlayerColliderDimensions actual{};
    if (!ReadPlayerColliderActualDimensions(
            physics, playerObject, getDimensions, actual)) {
        return;
    }

    PlayerColliderDimensions managerRequested{};
    PlayerColliderDimensions manager40cSourceCandidate{};
    PlayerColliderDimensions adjacentDimensionsCandidate{};
    const bool managerRequestedValid =
        ReadPlayerColliderManagerDimensions(
            moveManager, kPlayerRequestedDimensionsOffset,
            managerRequested);
    const bool manager40cSourceCandidateValid =
        ReadPlayerColliderManagerDimensions(
            moveManager,
            kPlayerManager40cSourceCandidateOffset,
            manager40cSourceCandidate);
    const bool adjacentDimensionsCandidateValid =
        ReadPlayerColliderManagerDimensions(
            moveManager, kPlayerAdjacentDimensionsCandidateOffset,
            adjacentDimensionsCandidate);

    PlayerColliderDimensions expectedBaseline{};
    AcquireSRWLockShared(&g_playerColliderLock);
    if (g_playerColliderBaselineObject ==
            reinterpret_cast<std::uintptr_t>(playerObject) &&
        PlayerColliderDimensionsAreValid(
            g_playerColliderRetailBaseline)) {
        expectedBaseline = g_playerColliderRetailBaseline;
    }
    ReleaseSRWLockShared(&g_playerColliderLock);
    expectedBaseline.y = actual.y;
    PlayerColliderDimensions expected{};
    const bool expectedValid =
        ResolvePlayerColliderDimensions(
            expectedBaseline, settings, expected);
    const bool drifted = expectedValid &&
        !PlayerColliderDimensionsMatch(actual, expected);
    const char* const expectedSource =
        expectedValid ? "cached_retail_baseline" : "none";

    bool changed = false;
    const std::uintptr_t playerValue =
        reinterpret_cast<std::uintptr_t>(playerObject);
    AcquireSRWLockExclusive(&g_playerColliderLock);
    changed = forceEvent ||
        !g_playerColliderLastObservedDimensionsValid ||
        g_playerColliderLastObservedObject != playerValue ||
        !PlayerColliderDimensionsMatch(
            g_playerColliderLastObservedDimensions, actual);
    if (changed) {
        g_playerColliderLastObservedDimensions = actual;
        g_playerColliderLastObservedObject = playerValue;
        g_playerColliderLastObservedDimensionsValid = true;
        g_playerColliderTelemetry.actualDimensions = actual;
        g_playerColliderTelemetry.actualDimensionsValid = true;
        g_playerColliderTelemetry.playerObject = playerValue;
        g_playerColliderTelemetry.widthScale = settings.widthScale;
        g_playerColliderTelemetry.runtimeDriftObserved = drifted;
    }
    ReleaseSRWLockExclusive(&g_playerColliderLock);
    if (!changed) {
        return;
    }

    volatile LONG* const observationCounter = forceEvent
        ? &g_playerColliderPostPendingObservationEvents
        : &g_playerColliderDimensionObservationEvents;
    const LONG observation = InterlockedIncrement(
        observationCounter);
    const LONG observationLimit = forceEvent ? 32 : 128;
    if (observation > observationLimit) {
        return;
    }
    const char* const observationStream =
        forceEvent ? "post_pending" : "boundary";
    std::uint32_t directionMask = 0U;
    AcquireSRWLockShared(&g_bindingLock);
    directionMask = g_lastDirectionMask;
    ReleaseSRWLockShared(&g_bindingLock);
    char detail[1024]{};
    std::snprintf(
        detail, sizeof(detail),
        "stream=%s count=%ld phase=%s player=%p scale=%.2f pending=%u "
        "locomotion_direction_mask=0x%X "
        "actual=(%.3f,%.3f,%.3f) "
        "manager_requested_valid=%u manager_requested=(%.3f,%.3f,%.3f) "
        "manager_40c_source_candidate_valid=%u "
        "manager_40c_source_candidate=(%.3f,%.3f,%.3f) "
        "adjacent_dimensions_candidate_valid=%u "
        "adjacent_dimensions_candidate=(%.3f,%.3f,%.3f) "
        "expected_valid=%u expected_source=%s "
        "expected=(%.3f,%.3f,%.3f) drift=%u "
        "query=ILTClientPhysics.GetObjectDims "
        "manager_reads=read_only mutation=none",
        observationStream,
        static_cast<long>(observation),
        phase != nullptr ? phase : "unknown",
        playerObject, settings.widthScale,
        reapplyPending ? 1U : 0U,
        static_cast<unsigned int>(directionMask),
        actual.x, actual.y, actual.z,
        managerRequestedValid ? 1U : 0U,
        managerRequested.x, managerRequested.y, managerRequested.z,
        manager40cSourceCandidateValid ? 1U : 0U,
        manager40cSourceCandidate.x,
        manager40cSourceCandidate.y,
        manager40cSourceCandidate.z,
        adjacentDimensionsCandidateValid ? 1U : 0U,
        adjacentDimensionsCandidate.x,
        adjacentDimensionsCandidate.y,
        adjacentDimensionsCandidate.z,
        expectedValid ? 1U : 0U,
        expectedSource,
        expected.x, expected.y, expected.z,
        drifted ? 1U : 0U);
    g_log("m5_player_collider_dimensions_observed", detail);
}

void RecordPlayerColliderHandoff(
    const char* source,
    void* playerObject,
    const PlayerColliderSettings& settings,
    const PlayerColliderDimensions& retailDimensions,
    const PlayerColliderDimensions& requestedDimensions,
    const PlayerColliderDimensions& actualDimensions,
    bool actualDimensionsValid,
    bool succeeded,
    std::uint32_t nativeResult,
    bool nativeCallAttempted,
    bool nativeResultValid,
    bool directApply) noexcept {
    std::uint32_t handoffCount = 0U;
    AcquireSRWLockExclusive(&g_playerColliderLock);
    g_playerColliderTelemetry.retailDimensions = retailDimensions;
    g_playerColliderTelemetry.requestedDimensions =
        requestedDimensions;
    g_playerColliderTelemetry.actualDimensions =
        actualDimensionsValid ? actualDimensions :
            PlayerColliderDimensions{};
    g_playerColliderTelemetry.widthScale = settings.widthScale;
    g_playerColliderTelemetry.playerObject =
        reinterpret_cast<std::uintptr_t>(playerObject);
    if (nativeCallAttempted) {
        ++g_playerColliderTelemetry.nativeHandoffCount;
    }
    handoffCount = g_playerColliderTelemetry.nativeHandoffCount;
    g_playerColliderTelemetry.retailDimensionsValid =
        PlayerColliderDimensionsAreValid(retailDimensions);
    g_playerColliderTelemetry.actualDimensionsValid =
        actualDimensionsValid;
    g_playerColliderTelemetry.lastRequestSatisfied = succeeded;
    if (succeeded) {
        g_playerColliderTelemetry.runtimeDriftObserved = false;
    }
    g_playerColliderTelemetry.reapplyPending =
        InterlockedCompareExchange(
            &g_playerColliderReapplyPending, 0, 0) != 0;
    ReleaseSRWLockExclusive(&g_playerColliderLock);

    volatile LONG* const eventCounter = directApply
        ? &g_playerColliderDirectApplyEvents
        : &g_playerColliderHandoffEvents;
    const LONG eventIndex = InterlockedIncrement(eventCounter);
    if (g_log == nullptr || eventIndex > 128) {
        return;
    }
    char detail[512]{};
    std::snprintf(
        detail, sizeof(detail),
        "source=%s handoff=%u player=%p scale=%.2f "
        "retail=(%.3f,%.3f,%.3f) "
        "requested=(%.3f,%.3f,%.3f) "
        "actual_valid=%u actual=(%.3f,%.3f,%.3f) "
        "native_call_attempted=%u native_result_valid=%u "
        "native_result=0x%08X succeeded=%u "
        "height_preserved=%u enemy_objects_changed=0",
        source != nullptr ? source : "unknown",
        handoffCount, playerObject, settings.widthScale,
        retailDimensions.x, retailDimensions.y,
        retailDimensions.z,
        requestedDimensions.x, requestedDimensions.y,
        requestedDimensions.z,
        actualDimensionsValid ? 1U : 0U,
        actualDimensions.x, actualDimensions.y,
        actualDimensions.z,
        nativeCallAttempted ? 1U : 0U,
        nativeResultValid ? 1U : 0U,
        nativeResult, succeeded ? 1U : 0U,
        requestedDimensions.y == retailDimensions.y ? 1U : 0U);
    g_log(
        !directApply
            ? "m5_player_collider_native_handoff"
            : nativeCallAttempted
                ? "m5_player_collider_reapply_attempted"
                : "m5_player_collider_reapply_not_needed",
        detail);
}

void __fastcall HookPlayerSetDimensions(
    void* moveManager,
    void* ignoredEdx,
    const VectorAbi* positionOffset) {
    (void)ignoredEdx;
    const bool operational = InterlockedCompareExchange(
        &g_playerColliderManagerHookOperational, 0, 0) != 0;
    PlayerSetDimensionsFunction const original =
        g_originalPlayerSetDimensions;
    if (original == nullptr) {
        return;
    }
    if (!operational) {
        original(moveManager, positionOffset);
        return;
    }
    if (g_playerColliderSetDimensionsActive) {
        original(moveManager, positionOffset);
        return;
    }

    void* expectedManager = nullptr;
    void* playerObject = nullptr;
    void* physics = nullptr;
    GetObjectDimensionsFunction getDimensions = nullptr;
    SetObjectDimensionsFunction setDimensions = nullptr;
    if (!ResolvePlayerColliderContext(
            expectedManager, playerObject, physics,
            getDimensions, setDimensions) ||
        moveManager != expectedManager) {
        original(moveManager, positionOffset);
        return;
    }

    VectorAbi retailAbi{};
    bool retailReadable = false;
    __try {
        std::memcpy(
            &retailAbi,
            static_cast<unsigned char*>(moveManager) +
                kPlayerRequestedDimensionsOffset,
            sizeof(retailAbi));
        retailReadable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        retailReadable = false;
    }
    const PlayerColliderDimensions retailDimensions =
        ToPlayerColliderDimensions(retailAbi);
    const PlayerColliderSettings settings =
        CurrentPlayerColliderSettings();
    PlayerColliderDimensions requestedDimensions{};
    if (!retailReadable ||
        !ResolvePlayerColliderDimensions(
            retailDimensions, settings, requestedDimensions)) {
        original(moveManager, positionOffset);
        return;
    }

    AcquireSRWLockExclusive(&g_playerColliderLock);
    g_playerColliderRetailBaseline = retailDimensions;
    g_playerColliderBaselineObject =
        reinterpret_cast<std::uintptr_t>(playerObject);
    ReleaseSRWLockExclusive(&g_playerColliderLock);

    const VectorAbi requestedAbi =
        ToVectorAbi(requestedDimensions);
    bool dimensionsReplaced = false;
    g_playerColliderSetDimensionsActive = true;
    __try {
        if (!PlayerColliderDimensionsMatch(
                retailDimensions, requestedDimensions)) {
            std::memcpy(
                static_cast<unsigned char*>(moveManager) +
                    kPlayerRequestedDimensionsOffset,
                &requestedAbi, sizeof(requestedAbi));
            dimensionsReplaced = true;
        }
        original(moveManager, positionOffset);
    } __finally {
        if (dimensionsReplaced) {
            std::memcpy(
                static_cast<unsigned char*>(moveManager) +
                    kPlayerRequestedDimensionsOffset,
                &retailAbi, sizeof(retailAbi));
        }
        g_playerColliderSetDimensionsActive = false;
    }

    PlayerColliderDimensions actualDimensions{};
    const bool actualValid =
        ReadPlayerColliderActualDimensions(
            physics, playerObject, getDimensions,
            actualDimensions);
    const bool succeeded = actualValid &&
        PlayerColliderDimensionsMatch(
            actualDimensions, requestedDimensions);
    InterlockedExchange(
        &g_playerColliderReapplyPending, succeeded ? 0 : 1);
    RecordPlayerColliderHandoff(
        "retail_cmove_mgr", playerObject, settings,
        retailDimensions, requestedDimensions,
        actualDimensions, actualValid, succeeded,
        kLithTechOk, true, false, false);
}

PlayerColliderPendingProcessResult
ProcessPendingPlayerColliderReapply() noexcept {
    if (g_originalPlayerSetDimensions == nullptr ||
        InterlockedCompareExchange(
            &g_playerColliderManagerHookOperational, 0, 0) == 0 ||
        InterlockedCompareExchange(
            &g_playerColliderReapplyPending, 0, 0) == 0 ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return PlayerColliderPendingProcessResult::NotProcessed;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_playerColliderNextReapplyTick) {
        return PlayerColliderPendingProcessResult::NotProcessed;
    }
    g_playerColliderNextReapplyTick =
        now + kPlayerColliderReapplyIntervalMilliseconds;

    void* moveManager = nullptr;
    void* playerObject = nullptr;
    void* physics = nullptr;
    GetObjectDimensionsFunction getDimensions = nullptr;
    SetObjectDimensionsFunction setDimensions = nullptr;
    if (!ResolvePlayerColliderContext(
            moveManager, playerObject, physics,
            getDimensions, setDimensions)) {
        return PlayerColliderPendingProcessResult::NotProcessed;
    }

    PlayerColliderDimensions actualBefore{};
    if (!ReadPlayerColliderActualDimensions(
            physics, playerObject, getDimensions,
            actualBefore)) {
        return PlayerColliderPendingProcessResult::NotProcessed;
    }

    PlayerColliderDimensions baseline{};
    const std::uintptr_t objectValue =
        reinterpret_cast<std::uintptr_t>(playerObject);
    AcquireSRWLockExclusive(&g_playerColliderLock);
    if (g_playerColliderBaselineObject == objectValue &&
        PlayerColliderDimensionsAreValid(
            g_playerColliderRetailBaseline)) {
        baseline = g_playerColliderRetailBaseline;
    } else {
        baseline = actualBefore;
        g_playerColliderRetailBaseline = baseline;
        g_playerColliderBaselineObject = objectValue;
    }
    ReleaseSRWLockExclusive(&g_playerColliderLock);

    // Current Y comes from Retail so standing, crouching, and other native
    // posture transitions remain untouched by a horizontal-width change.
    baseline.y = actualBefore.y;
    const PlayerColliderSettings settings =
        CurrentPlayerColliderSettings();
    PlayerColliderDimensions requested{};
    if (!ResolvePlayerColliderDimensions(
            baseline, settings, requested)) {
        return PlayerColliderPendingProcessResult::NotProcessed;
    }

    std::uint32_t nativeResult = kLithTechOk;
    bool nativeCallAttempted = false;
    bool nativeResultValid = false;
    if (!PlayerColliderDimensionsMatch(actualBefore, requested)) {
        nativeCallAttempted = true;
        VectorAbi requestedAbi = ToVectorAbi(requested);
        __try {
            nativeResult = setDimensions(
                physics, playerObject, &requestedAbi,
                kSetDimensionsPushObjects);
            nativeResultValid = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            nativeResult = ~kLithTechOk;
        }
    }

    PlayerColliderDimensions actualAfter{};
    const bool actualValid =
        ReadPlayerColliderActualDimensions(
            physics, playerObject, getDimensions,
            actualAfter);
    const bool succeeded =
        actualValid &&
        PlayerColliderDimensionsMatch(actualAfter, requested) &&
        (!nativeCallAttempted ||
         (nativeResultValid && nativeResult == kLithTechOk));
    InterlockedExchange(
        &g_playerColliderReapplyPending, succeeded ? 0 : 1);
    RecordPlayerColliderHandoff(
        "settings_change", playerObject, settings,
        baseline, requested, actualAfter, actualValid,
        succeeded, nativeResult, nativeCallAttempted,
        nativeResultValid, true);
    return nativeCallAttempted
        ? PlayerColliderPendingProcessResult::NativeSetDimsAttempted
        : PlayerColliderPendingProcessResult::AlreadyMatched;
}

float __fastcall HookGetBindingValue(
    const void* bindManager,
    void* ignoredEdx,
    const RetailBinding* binding) {
    (void)ignoredEdx;
    const float original =
        g_originalGetBindingValue(bindManager, binding);
    if (binding == nullptr || g_getInputState == nullptr) {
        return original;
    }

    const bool locomotionCommand =
        binding->command <= 4U && binding->command != 2U;
    if (locomotionCommand && InterlockedCompareExchange(
            &g_locomotionEnabled, 0, 0) != 0) {
        FearVrInputState input{};
        const bool usable = ReadUsableControllerInput(input);
        const bool calibrationCaptured =
            WeaponGripCalibrationCapturesInput(input, usable);
        const LocomotionDirections directions =
            ResolveLocomotionDirections(
                input, usable && !calibrationCaptured);
        ReportDirectionTransition(DirectionMask(directions));
        if (DirectionActive(binding->command, directions)) {
            return ActiveBindingValue(*binding);
        }
    }

    if (binding->command == kCondemnedActivateCommand &&
        InterlockedCompareExchange(
            &g_interactionEnabled, 0, 0) != 0) {
        FearVrInputState input{};
        const int retailGameState =
            ReadRetailGameState(g_interfaceManager);
        const bool usable = ReadUsableControllerInput(input) &&
            retailGameState == kCondemnedGameStatePlaying;
        const bool calibrationCaptured =
            WeaponGripCalibrationCapturesInput(input, usable);
        const ActivateValue activate =
            ResolveActivateValue(
                input, usable && !calibrationCaptured);
        const float outputValue =
            MergeActivateWithRetail(original, activate);
        ReportInteractionTransition(
            *binding, activate, original, outputValue, retailGameState);
        return outputValue;
    }

    if (CondemnedCoreActionIndex(binding->command) >= 0 &&
        InterlockedCompareExchange(
            &g_coreActionsEnabled, 0, 0) != 0) {
        FearVrInputState input{};
        const int retailGameState =
            ReadRetailGameState(g_interfaceManager);
        const bool usable = ReadUsableControllerInput(input) &&
            retailGameState == kCondemnedGameStatePlaying;
        const bool calibrationCaptured =
            WeaponGripCalibrationCapturesInput(input, usable);
        const bool secondaryGripCaptured =
            binding->command == kCondemnedRunCommand &&
            PhysicalMeleeSecondaryGripCapturesInput(input, usable);
        const bool slideGrabCaptured =
            (binding->command == kCondemnedRunCommand &&
             SlideGrabCapturesOffHandInput(
                 input, usable, true)) ||
            (binding->command == kCondemnedBlockCommand &&
             SlideGrabCapturesOffHandInput(
                 input, usable, false));
        CoreActionValue action = ResolveCoreActionValue(
            input,
            usable && !calibrationCaptured &&
                !secondaryGripCaptured && !slideGrabCaptured,
            binding->command);
        bool automaticBlockPoseActive = false;
        bool automaticAttackSeedActive = false;
        bool automaticAttackSeedOnly = false;
        if (binding->command == kCondemnedBlockCommand) {
            const bool manualBlockActive = action.active;
            const bool retailBlockActive =
                std::isfinite(original) &&
                std::fabs(original) > 0.001F;
            automaticBlockPoseActive =
                ReadPhysicalMeleeBlockPoseActive(
                    usable && !calibrationCaptured,
                    manualBlockActive,
                    retailBlockActive);
            if (automaticBlockPoseActive) {
                action = {ActiveBindingValue(*binding), true};
            }
        }
        if (binding->command == kCondemnedFireCommand) {
            const bool manualFireActive = action.active;
            const bool retailFireActive =
                std::isfinite(original) &&
                std::fabs(original) > 0.001F;
            const PhysicalMeleeAutomaticSeedResult seed =
                UpdatePhysicalMeleeAutomaticSeedRuntime(
                    usable && !calibrationCaptured &&
                        ProcessOwnsForegroundWindow() &&
                        !VrToolMenuIsOpen(),
                    !manualFireActive && !retailFireActive);
            automaticAttackSeedActive = seed.pulseActive;
            const bool swingAttackActive =
                ReadPhysicalMeleeSwingAttackActive(
                    usable && !calibrationCaptured);
            if (automaticAttackSeedActive || swingAttackActive) {
                action = {ActiveBindingValue(*binding), true};
            }
            automaticAttackSeedOnly =
                automaticAttackSeedActive &&
                !manualFireActive && !retailFireActive &&
                !swingAttackActive;
        }
        const float outputValue = MergeCoreActionWithRetail(
            original, action);
        ReportCoreActionTransition(
            *binding, binding->command, action, original, outputValue,
            retailGameState, automaticBlockPoseActive,
            automaticAttackSeedActive,
            automaticAttackSeedOnly);
        return outputValue;
    }

    return original;
}

float __fastcall HookGetExtremalCommandValue(
    const void* bindManager,
    void* ignoredEdx,
    std::uint32_t command) {
    (void)ignoredEdx;
    const float retailValue =
        g_originalGetExtremalCommandValue(bindManager, command);
    UpdatePhysicalMeleeProbe();
    if (command == kCondemnedYawAccelCommand) {
        UpdateEquippedWeaponVisualSource();
    }
    if ((command == kCondemnedPitchCommand ||
         command == kCondemnedYawCommand) &&
        InterlockedCompareExchange(
            &g_headAimInputEnabled, 0, 0) != 0 &&
        TrackedHeadAimIsFresh()) {
        if (retailValue != 0.0F &&
            InterlockedCompareExchange(
                &g_mouseLookSuppressionLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m5_vr_mouse_look_suppressed",
                "commands=11,12 replacement=0 "
                "condition=fresh_focused_hmd_look "
                "keyboard_mouse_fallback_on_stale=1");
        }
        return 0.0F;
    }
    if (command != kCondemnedYawAccelCommand ||
        g_getInputState == nullptr) {
        return retailValue;
    }

    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    const TurningValue turning = ResolveTurningValue(
        input, usable && !calibrationCaptured);
    const float outputValue = MergeTurningWithRetail(
        retailValue, turning);
    ReportTurnTransition(turning, retailValue, outputValue);
    return outputValue;
}

enum class HandgunMuzzleAimResult : std::uint32_t {
    Applied = 0U,
    VisualSourceUnavailable,
    WeaponIdentityMismatch,
    TrackedWeaponPoseStale,
    ModelInterfaceUnavailable,
    FlashSocketUnavailable,
    BreachSocketUnavailable,
    FlashTransformUnavailable,
    BreachTransformUnavailable,
    VisibleModelSolveFailed,
    SocketGeometryInvalid
};

const char* HandgunMuzzleAimResultName(
    HandgunMuzzleAimResult result) noexcept {
    switch (result) {
    case HandgunMuzzleAimResult::Applied:
        return "applied";
    case HandgunMuzzleAimResult::VisualSourceUnavailable:
        return "visual_source_unavailable";
    case HandgunMuzzleAimResult::WeaponIdentityMismatch:
        return "weapon_identity_mismatch";
    case HandgunMuzzleAimResult::TrackedWeaponPoseStale:
        return "tracked_weapon_pose_stale";
    case HandgunMuzzleAimResult::ModelInterfaceUnavailable:
        return "model_interface_unavailable";
    case HandgunMuzzleAimResult::FlashSocketUnavailable:
        return "Flash_socket_unavailable";
    case HandgunMuzzleAimResult::BreachSocketUnavailable:
        return "Breach_socket_unavailable";
    case HandgunMuzzleAimResult::FlashTransformUnavailable:
        return "Flash_transform_unavailable";
    case HandgunMuzzleAimResult::BreachTransformUnavailable:
        return "Breach_transform_unavailable";
    case HandgunMuzzleAimResult::VisibleModelSolveFailed:
        return "visible_model_solve_failed";
    case HandgunMuzzleAimResult::SocketGeometryInvalid:
        return "socket_geometry_invalid";
    }
    return "unknown";
}

struct HandgunMuzzleAimContext {
    HandgunMuzzleAimResult result{
        HandgunMuzzleAimResult::VisualSourceUnavailable};
    std::int32_t weaponIndex{-1};
    void* modelObject{nullptr};
    std::uint64_t sourceGeneration{0U};
    std::uint64_t sampleId{0U};
    std::uint64_t timestampNs{0U};
    float modelLocalGripPositionUnits[3]{};
    float modelLocalGripRotation[4]{};
    PhysicalMeleeRigidTransform desiredGripWorld{};
    PhysicalMeleeVisualProxyTransform visibleModel{};
    ModelHandle flashSocket{kInvalidModelHandle};
    ModelHandle breachSocket{kInvalidModelHandle};
    ModelTransformAbi flashLocal{};
    ModelTransformAbi breachLocal{};
    bool flashTransformAvailable{false};
    bool breachSocketAvailable{false};
    bool breachTransformAvailable{false};
    PhysicalFirearmMuzzleFrame muzzle{};
};

bool HandgunMuzzleAcceptanceWeaponSelected() noexcept {
    AcquireSRWLockShared(&g_physicalMeleeLock);
    const bool selected =
        g_physicalMeleeProfileWeaponIndex ==
        kHandgunMuzzleAcceptanceWeaponIndex;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    return selected;
}

bool FirearmMuzzleModelInterfaceIsCurrent() noexcept {
    if (g_firearmMuzzleModel == nullptr ||
        g_firearmMuzzleModelClientGlobalAddress == nullptr ||
        g_getFirearmModelSocket == nullptr ||
        g_getFirearmModelSocketTransform == nullptr) {
        return false;
    }
    void* currentModel = nullptr;
    __try {
        std::memcpy(
            &currentModel,
            g_firearmMuzzleModelClientGlobalAddress,
            sizeof(currentModel));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return currentModel == g_firearmMuzzleModel;
}

PhysicalMeleeRigidTransform ToPhysicalMeleeRigidTransform(
    const ModelTransformAbi& transform) noexcept {
    return {
        {transform.position.x, transform.position.y, transform.position.z},
        {transform.rotation.x, transform.rotation.y,
         transform.rotation.z, transform.rotation.w}};
}

bool FirearmSocketTransformIsValid(
    const ModelTransformAbi& transform) noexcept {
    return std::isfinite(transform.scale) &&
        transform.scale >= 0.99F && transform.scale <= 1.01F &&
        PhysicalMeleeRigidTransformIsValid(
            ToPhysicalMeleeRigidTransform(transform));
}

bool ResolveHandgunMuzzleAim(
    const void* weapon,
    HandgunMuzzleAimContext& context) noexcept {
    context = {};
    if (!ReadEquippedWeaponVisualSourceForFire(
            weapon,
            context.weaponIndex,
            context.modelObject,
            context.modelLocalGripPositionUnits,
            context.modelLocalGripRotation,
            context.sourceGeneration)) {
        context.result =
            HandgunMuzzleAimResult::VisualSourceUnavailable;
        return false;
    }
    if (context.weaponIndex !=
        kHandgunMuzzleAcceptanceWeaponIndex) {
        context.result =
            HandgunMuzzleAimResult::WeaponIdentityMismatch;
        return false;
    }

    float gripPosition[3]{};
    float gripRotation[4]{};
    if (!ReadTrackedControllerWorldPose(
            gripPosition, gripRotation,
            context.sampleId, context.timestampNs)) {
        context.result =
            HandgunMuzzleAimResult::TrackedWeaponPoseStale;
        return false;
    }
    context.desiredGripWorld = {
        {gripPosition[0], gripPosition[1], gripPosition[2]},
        fearvr::Normalize({
            gripRotation[0], gripRotation[1],
            gripRotation[2], gripRotation[3]})};
    if (!PhysicalMeleeRigidTransformIsValid(
            context.desiredGripWorld)) {
        context.result =
            HandgunMuzzleAimResult::TrackedWeaponPoseStale;
        return false;
    }
    if (!FirearmMuzzleModelInterfaceIsCurrent()) {
        context.result =
            HandgunMuzzleAimResult::ModelInterfaceUnavailable;
        return false;
    }

    ModelResult socketResult = ~kModelOk;
    __try {
        socketResult = g_getFirearmModelSocket(
            g_firearmMuzzleModel, context.modelObject,
            "Flash", context.flashSocket);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        socketResult = ~kModelOk;
    }
    if (socketResult != kModelOk ||
        context.flashSocket == kInvalidModelHandle) {
        context.result =
            HandgunMuzzleAimResult::FlashSocketUnavailable;
        return false;
    }
    __try {
        socketResult = g_getFirearmModelSocketTransform(
            g_firearmMuzzleModel, context.modelObject,
            context.flashSocket, context.flashLocal, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        socketResult = ~kModelOk;
    }
    context.flashTransformAvailable =
        socketResult == kModelOk &&
        FirearmSocketTransformIsValid(context.flashLocal);
    if (!context.flashTransformAvailable) {
        context.result =
            HandgunMuzzleAimResult::FlashTransformUnavailable;
        return false;
    }

    // Retail's own display code treats Breach as optional. The live index-76
    // Colt exposes Flash handle 2 but returns LT_NOTFOUND for Breach, so retain
    // the two-point barrel when available and use authored Flash +Z otherwise.
    __try {
        socketResult = g_getFirearmModelSocket(
            g_firearmMuzzleModel, context.modelObject,
            "Breach", context.breachSocket);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        socketResult = ~kModelOk;
    }
    context.breachSocketAvailable =
        socketResult == kModelOk &&
        context.breachSocket != kInvalidModelHandle;
    if (context.breachSocketAvailable) {
        __try {
            socketResult = g_getFirearmModelSocketTransform(
                g_firearmMuzzleModel, context.modelObject,
                context.breachSocket, context.breachLocal, false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            socketResult = ~kModelOk;
        }
        context.breachTransformAvailable =
            socketResult == kModelOk &&
            FirearmSocketTransformIsValid(context.breachLocal);
    }

    const fearvr::TrackingVector localGripPosition{
        context.modelLocalGripPositionUnits[0],
        context.modelLocalGripPositionUnits[1],
        context.modelLocalGripPositionUnits[2]};
    const fearvr::TrackingQuaternion localGripRotation{
        context.modelLocalGripRotation[0],
        context.modelLocalGripRotation[1],
        context.modelLocalGripRotation[2],
        context.modelLocalGripRotation[3]};
    context.visibleModel =
        ResolvePhysicalMeleeHeldModelTransform(
            context.desiredGripWorld,
            localGripPosition, localGripRotation, true);
    if (!context.visibleModel.active) {
        context.result =
            HandgunMuzzleAimResult::VisibleModelSolveFailed;
        return false;
    }
    if (context.breachTransformAvailable) {
        context.muzzle = ResolvePhysicalFirearmMuzzleFrame(
            context.visibleModel.objectWorld,
            ToPhysicalMeleeRigidTransform(context.flashLocal),
            ToPhysicalMeleeRigidTransform(context.breachLocal),
            true);
    }
    if (!context.muzzle.active) {
        context.muzzle =
            ResolvePhysicalFirearmMuzzleFrameFromFlashSocket(
                context.visibleModel.objectWorld,
                ToPhysicalMeleeRigidTransform(context.flashLocal),
                true);
    }
    if (!context.muzzle.active) {
        context.result =
            HandgunMuzzleAimResult::SocketGeometryInvalid;
        return false;
    }
    context.result = HandgunMuzzleAimResult::Applied;
    return true;
}

void LogHandgunMuzzleAim(
    LONG call,
    const void* weapon,
    const HandgunMuzzleAimContext& context,
    const VectorAbi& retailForward,
    const VectorAbi& retailFirePosition,
    bool controllerAimValid,
    const VectorAbi& controllerForward,
    bool directionApplied) noexcept {
    if (g_log == nullptr || call > 128) {
        return;
    }
    const fearvr::TrackingVector retailDirection{
        retailForward.x, retailForward.y, retailForward.z};
    const fearvr::TrackingVector controllerDirection{
        controllerForward.x,
        controllerForward.y,
        controllerForward.z};
    fearvr::TrackingVector retailDirectionUnit{};
    fearvr::TrackingVector controllerDirectionUnit{};
    const bool retailDirectionValid =
        PhysicalMeleeNormalizeVector(
            retailDirection, retailDirectionUnit);
    const bool controllerDirectionNormalized =
        controllerAimValid &&
        PhysicalMeleeNormalizeVector(
            controllerDirection, controllerDirectionUnit);
    const fearvr::TrackingVector originDelta =
        context.muzzle.active
        ? PhysicalMeleeSubtract(
              context.muzzle.originUnits,
              {retailFirePosition.x,
               retailFirePosition.y,
               retailFirePosition.z})
        : fearvr::TrackingVector{};
    const float muzzleRetailDot =
        context.muzzle.active && retailDirectionValid
        ? PhysicalMeleeDot(
              context.muzzle.forward, retailDirectionUnit)
        : 0.0F;
    const float muzzleControllerDot =
        context.muzzle.active && controllerDirectionNormalized
        ? PhysicalMeleeDot(
              context.muzzle.forward, controllerDirectionUnit)
        : 0.0F;
    char detail[2560]{};
    std::snprintf(
        detail, sizeof(detail),
        "call=%ld result=%s direction_applied=%u direction_source=%s "
        "fallback=%s weapon=%p weapon_index=%ld "
        "model_object=%p source_generation=%llu "
        "sample_id=%llu timestamp_ns=%llu "
        "local_grip_position=(%.3f,%.3f,%.3f) "
        "local_grip_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "desired_grip_position=(%.3f,%.3f,%.3f) "
        "desired_grip_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "Flash_handle=%u Flash_transform_available=%u "
        "Flash_local=(%.3f,%.3f,%.3f) "
        "Flash_local_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "Breach_handle=%u Breach_socket_available=%u "
        "Breach_transform_available=%u "
        "Breach_local=(%.3f,%.3f,%.3f) "
        "Breach_local_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "Breach_to_Flash_units=%.3f "
        "visible_muzzle_origin=(%.3f,%.3f,%.3f) "
        "visible_barrel_forward=(%.5f,%.5f,%.5f) "
        "retail_fire_position=(%.3f,%.3f,%.3f) "
        "retail_forward=(%.5f,%.5f,%.5f) "
        "controller_aim_valid=%u "
        "controller_forward=(%.5f,%.5f,%.5f) "
        "muzzle_retail_dot=%.5f muzzle_controller_dot=%.5f "
        "muzzle_to_retail_origin_units=(%.3f,%.3f,%.3f) "
        "muzzle_to_retail_origin_distance_units=%.3f "
        "fire_position_policy=retail_preserved",
        call, HandgunMuzzleAimResultName(context.result),
        directionApplied ? 1U : 0U,
        PhysicalFirearmMuzzleDirectionSourceName(
            context.muzzle.directionSource),
        directionApplied ? "none" :
            (controllerAimValid ? "raw_controller" : "retail"),
        weapon, static_cast<long>(context.weaponIndex),
        context.modelObject,
        static_cast<unsigned long long>(
            context.sourceGeneration),
        static_cast<unsigned long long>(context.sampleId),
        static_cast<unsigned long long>(context.timestampNs),
        context.modelLocalGripPositionUnits[0],
        context.modelLocalGripPositionUnits[1],
        context.modelLocalGripPositionUnits[2],
        context.modelLocalGripRotation[0],
        context.modelLocalGripRotation[1],
        context.modelLocalGripRotation[2],
        context.modelLocalGripRotation[3],
        context.desiredGripWorld.positionUnits.x,
        context.desiredGripWorld.positionUnits.y,
        context.desiredGripWorld.positionUnits.z,
        context.desiredGripWorld.rotation.x,
        context.desiredGripWorld.rotation.y,
        context.desiredGripWorld.rotation.z,
        context.desiredGripWorld.rotation.w,
        static_cast<unsigned int>(context.flashSocket),
        context.flashTransformAvailable ? 1U : 0U,
        context.flashLocal.position.x,
        context.flashLocal.position.y,
        context.flashLocal.position.z,
        context.flashLocal.rotation.x,
        context.flashLocal.rotation.y,
        context.flashLocal.rotation.z,
        context.flashLocal.rotation.w,
        static_cast<unsigned int>(context.breachSocket),
        context.breachSocketAvailable ? 1U : 0U,
        context.breachTransformAvailable ? 1U : 0U,
        context.breachLocal.position.x,
        context.breachLocal.position.y,
        context.breachLocal.position.z,
        context.breachLocal.rotation.x,
        context.breachLocal.rotation.y,
        context.breachLocal.rotation.z,
        context.breachLocal.rotation.w,
        context.muzzle.breachToFlashUnits,
        context.muzzle.originUnits.x,
        context.muzzle.originUnits.y,
        context.muzzle.originUnits.z,
        context.muzzle.forward.x,
        context.muzzle.forward.y,
        context.muzzle.forward.z,
        retailFirePosition.x,
        retailFirePosition.y,
        retailFirePosition.z,
        retailForward.x,
        retailForward.y,
        retailForward.z,
        controllerAimValid ? 1U : 0U,
        controllerForward.x,
        controllerForward.y,
        controllerForward.z,
        muzzleRetailDot, muzzleControllerDot,
        originDelta.x, originDelta.y, originDelta.z,
        PhysicalMeleeLength(originDelta));
    g_log(
        directionApplied
            ? "m5_handgun_muzzle_aim"
            : "m5_handgun_muzzle_aim_fallback",
        detail);
}

bool __fastcall HookGetFireVectors(
    const void* weapon,
    void* ignoredEdx,
    VectorAbi& right,
    VectorAbi& up,
    VectorAbi& forward,
    VectorAbi& firePosition) {
    (void)ignoredEdx;
    const bool result = g_originalGetFireVectors(
        weapon, right, up, forward, firePosition);
    const VectorAbi retailForward = forward;
    const VectorAbi retailFirePosition = firePosition;
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) != 0 &&
        g_log != nullptr) {
        const LONG call = InterlockedIncrement(
            &g_aimPathFireVectorCalls);
        if (call <= 512) {
            VectorAbi controllerForward{};
            const bool controllerAim =
                ReadControllerForward(controllerForward);
            char stack[192]{};
            FormatGameClientStack(stack, sizeof(stack));
            char detail[640]{};
            std::snprintf(
                detail, sizeof(detail),
                "call=%ld result=%u weapon=%p "
                "retail_forward=(%.4f,%.4f,%.4f) "
                "retail_fire_position=(%.3f,%.3f,%.3f) "
                "controller_aim_valid=%u "
                "controller_forward=(%.4f,%.4f,%.4f) "
                "gameorig_stack_rvas=%s",
                call, result ? 1U : 0U, weapon,
                retailForward.x, retailForward.y, retailForward.z,
                retailFirePosition.x, retailFirePosition.y,
                retailFirePosition.z,
                controllerAim ? 1U : 0U,
                controllerForward.x, controllerForward.y,
                controllerForward.z, stack);
            g_log("m5_aim_path_fire_vectors", detail);
        }
    }

    const bool handgunCandidate =
        result && HandgunMuzzleAcceptanceWeaponSelected();
    HandgunMuzzleAimContext handgunContext{};
    LONG handgunCall = 0;
    if (handgunCandidate) {
        handgunCall = InterlockedIncrement(
            &g_handgunMuzzleAimCalls);
        if (ResolveHandgunMuzzleAim(
                weapon, handgunContext)) {
            right = {
                handgunContext.muzzle.right.x,
                handgunContext.muzzle.right.y,
                handgunContext.muzzle.right.z};
            up = {
                handgunContext.muzzle.up.x,
                handgunContext.muzzle.up.y,
                handgunContext.muzzle.up.z};
            forward = {
                handgunContext.muzzle.forward.x,
                handgunContext.muzzle.forward.y,
                handgunContext.muzzle.forward.z};
            InterlockedIncrement(&g_handgunMuzzleAimApplied);
            VectorAbi controllerForward{};
            const bool controllerAim =
                ReadControllerForward(controllerForward);
            LogHandgunMuzzleAim(
                handgunCall, weapon, handgunContext,
                retailForward, retailFirePosition,
                controllerAim, controllerForward, true);
            if (InterlockedCompareExchange(
                    &g_handgunMuzzleAimActiveLogged, 1, 0) == 0 &&
                g_log != nullptr) {
                g_log(
                    "m5_handgun_muzzle_aim_active",
                    "weapon_index=76 model_frame=saved_visible_grip "
                    "barrel_direction=prefer_Breach_to_Flash_else_"
                    "Flash_socket_plus_Z "
                    "basis_roll=Flash_socket "
                    "fire_position=retail_preserved "
                    "fallback=raw_controller_then_retail");
            }
            return result;
        }
    }

    float rotation[4]{};
    bool controllerBasisValid =
        result && ReadTrackedControllerAimRotation(rotation);
    fearvr::TrackingVector controllerRight{};
    fearvr::TrackingVector controllerUp{};
    fearvr::TrackingVector controllerForward{};
    if (controllerBasisValid) {
        const fearvr::TrackingQuaternion controller =
            fearvr::Normalize({
                rotation[0], rotation[1],
                rotation[2], rotation[3]});
        controllerBasisValid = fearvr::IsFinite(controller);
        if (controllerBasisValid) {
            controllerRight =
                fearvr::Rotate(controller, {1.0F, 0.0F, 0.0F});
            controllerUp =
                fearvr::Rotate(controller, {0.0F, 1.0F, 0.0F});
            controllerForward =
                fearvr::Rotate(controller, {0.0F, 0.0F, 1.0F});
            controllerBasisValid =
                fearvr::IsFinite(controllerRight) &&
                fearvr::IsFinite(controllerUp) &&
                fearvr::IsFinite(controllerForward);
        }
    }

    if (handgunCandidate) {
        InterlockedIncrement(&g_handgunMuzzleAimFallbacks);
        const VectorAbi controllerForwardAbi{
            controllerForward.x,
            controllerForward.y,
            controllerForward.z};
        LogHandgunMuzzleAim(
            handgunCall, weapon, handgunContext,
            retailForward, retailFirePosition,
            controllerBasisValid, controllerForwardAbi, false);
    }
    if (!controllerBasisValid) {
        return result;
    }

    right = {
        controllerRight.x, controllerRight.y, controllerRight.z};
    up = {controllerUp.x, controllerUp.y, controllerUp.z};
    forward = {
        controllerForward.x,
        controllerForward.y,
        controllerForward.z};
    if (InterlockedCompareExchange(
            &g_controllerFireAimLogged, 1, 0) == 0 &&
        g_log != nullptr) {
        g_log(
            "m5_controller_fire_vectors_active",
            "target=GameOrig+0x0002AF70 "
            "direction=right_controller_world_basis "
            "scope=non_index76_or_verified_muzzle_fallback "
            "retail_fire_position_preserved=1 stale_fallback=retail");
    }
    return result;
}

float __fastcall HookMasterDatabaseFloatReader(
    void* database,
    void* ignoredEdx,
    const void* attribute,
    std::uint32_t index,
    float defaultValue) {
    (void)ignoredEdx;
    const auto* const caller = static_cast<const unsigned char*>(
        _ReturnAddress());
    const float retailValue =
        g_originalMasterDatabaseFloatReader != nullptr
        ? g_originalMasterDatabaseFloatReader(
              database, attribute, index, defaultValue)
        : defaultValue;
    if (!g_physicalMeleeNativeCapsuleOverride.valid ||
        g_gameClientBase == nullptr || caller == nullptr) {
        return retailValue;
    }

    PhysicalMeleeNativeCapsuleProperty property =
        PhysicalMeleeNativeCapsuleProperty::Retail;
    std::uint32_t readBit = 0U;
    if (caller == g_gameClientBase +
            kMeleeNativeLengthUpReadReturnRva) {
        property = PhysicalMeleeNativeCapsuleProperty::LengthUp;
        readBit = 0x1U;
    } else if (caller == g_gameClientBase +
            kMeleeNativeLengthDownReadReturnRva) {
        property = PhysicalMeleeNativeCapsuleProperty::LengthDown;
        readBit = 0x2U;
    } else if (caller == g_gameClientBase +
            kMeleeNativeRadiusReadReturnRva) {
        property = PhysicalMeleeNativeCapsuleProperty::Radius;
        readBit = 0x4U;
    }
    if (readBit != 0U) {
        g_physicalMeleeNativeCapsuleReadMask |= readBit;
    }
    return ResolvePhysicalMeleeNativeCapsuleProperty(
        g_physicalMeleeNativeCapsuleOverride,
        property, retailValue);
}


std::uintptr_t __fastcall HookMeleeEnableCollisions(
    void* controller,
    void* ignoredEdx,
    std::uintptr_t argument1,
    std::uintptr_t argument2,
    std::uintptr_t argument3,
    std::uintptr_t argument4,
    std::uintptr_t argument5) {
    (void)ignoredEdx;
    const bool blocking = argument5 != 0U;
    const bool classifyPlayerCollision =
        InterlockedCompareExchange(
            &g_physicalMeleeContactDamageEnabled, 0, 0) != 0 &&
        PhysicalMeleeEnableCollisionsIsPlayerOwned(argument1);
    const MeleeEnableCollisionCandidate collisionCandidate =
        classifyPlayerCollision
        ? FindMeleeEnableCollisionCandidate(controller)
        : MeleeEnableCollisionCandidate{};
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) != 0 &&
        g_log != nullptr) {
        const LONG call = InterlockedIncrement(&g_aimPathMeleeCalls);
        if (call <= 512) {
            VectorAbi controllerForward{};
            const bool controllerAim =
                ReadControllerForward(controllerForward);
            char stack[192]{};
            FormatGameClientStack(stack, sizeof(stack));
            char detail[704]{};
            std::snprintf(
                detail, sizeof(detail),
                "call=%ld controller=%p "
                "args=(0x%08lX,0x%08lX,0x%08lX,0x%08lX,0x%08lX) "
                "controller_aim_valid=%u "
                "controller_forward=(%.4f,%.4f,%.4f) "
                "gameorig_stack_rvas=%s behavior=pass_through",
                call, controller,
                static_cast<unsigned long>(argument1),
                static_cast<unsigned long>(argument2),
                static_cast<unsigned long>(argument3),
                static_cast<unsigned long>(argument4),
                static_cast<unsigned long>(argument5),
                controllerAim ? 1U : 0U,
                controllerForward.x, controllerForward.y,
                controllerForward.z, stack);
            g_log("m5_aim_path_melee_collision_enable", detail);
        }
    }
    const PhysicalMeleeNativeCapsuleShape previousOverride =
        g_physicalMeleeNativeCapsuleOverride;
    const std::uint32_t previousReadMask =
        g_physicalMeleeNativeCapsuleReadMask;
    PhysicalMeleeNativeCapsuleShape requestedOverride{};
    const bool overrideActive =
        CapturePhysicalMeleeNativeCapsuleOverride(
            argument1, blocking, requestedOverride);
    const PhysicalMeleeBlockWindowResolution blockWindow =
        ResolvePhysicalMeleeBlockWindow(
            argument1, blocking, argument4);
    if (classifyPlayerCollision && !blocking) {
        // Bind before the Retail call so even a synchronous creation-contact
        // callback cannot escape the seed-only damage suppression window.
        BindPhysicalMeleeAutomaticSeedImpactController(
            controller);
    }
    if (overrideActive) {
        g_physicalMeleeNativeCapsuleOverride = requestedOverride;
        g_physicalMeleeNativeCapsuleReadMask = 0U;
    }

    std::uintptr_t result = 0U;
    std::uint32_t appliedReadMask = 0U;
    __try {
        result = g_originalMeleeEnableCollisions(
            controller, argument1, argument2, argument3,
            blockWindow.appliedArgument, argument5);
        appliedReadMask = g_physicalMeleeNativeCapsuleReadMask;
    } __finally {
        g_physicalMeleeNativeCapsuleOverride = previousOverride;
        g_physicalMeleeNativeCapsuleReadMask = previousReadMask;
    }
    if (overrideActive) {
        const LONG overrideCount = InterlockedIncrement(
            &g_physicalMeleeNativeCapsuleOverrides);
        if (g_log != nullptr && overrideCount <= 64) {
            char detail[640]{};
            std::snprintf(
                detail, sizeof(detail),
                "count=%ld owner=0x%08lX "
                "origin_tip=(%.3f,%.3f,%.3f) "
                "length_up=%.3f length_down=%.3f radius=%.3f "
                "read_mask=0x%X expected_read_mask=0x7 result=0x%08lX "
                "endpoints=configured_base_tip role=%s",
                overrideCount,
                static_cast<unsigned long>(argument1),
                requestedOverride.transform.positionUnits.x,
                requestedOverride.transform.positionUnits.y,
                requestedOverride.transform.positionUnits.z,
                requestedOverride.lengthUpUnits,
                requestedOverride.lengthDownUnits,
                requestedOverride.radiusUnits,
                appliedReadMask,
                static_cast<unsigned long>(result),
                blocking ? "block" : "attack");
            g_log(
                "m5_physical_melee_native_capsule_override",
                detail);
        }
    }
    if (blockWindow.observed) {
        const LONG windowCount = InterlockedIncrement(
            &g_physicalMeleeBlockWindowSamples);
        if (g_log != nullptr && windowCount <= 64) {
            char detail[320]{};
            std::snprintf(
                detail, sizeof(detail),
                "count=%ld owner=0x%08lX retail_ms=%.1f "
                "applied_ms=%.1f override=%u "
                "scope=player_block_only",
                windowCount,
                static_cast<unsigned long>(argument1),
                blockWindow.retailSeconds * 1000.0F,
                blockWindow.appliedSeconds * 1000.0F,
                blockWindow.overrideApplied ? 1U : 0U);
            g_log("m5_physical_melee_block_window", detail);
        }
    }
    bool playerCollisionClassified = false;
    if (classifyPlayerCollision) {
        playerCollisionClassified =
            PublishPhysicalMeleeCollisionClassification(
            controller, collisionCandidate, argument1, argument2,
            blocking);
    }
    if (classifyPlayerCollision && !blocking) {
        const MeleeCollisionRecordSnapshot confirmedRecord =
            ReadMeleeCollisionRecord(collisionCandidate.record);
        ObservePhysicalMeleeAutomaticSeedCollision(
            controller, appliedReadMask,
            playerCollisionClassified,
            confirmedRecord.readable
                ? confirmedRecord.collisionObject : 0U);
    }
    return result;
}

void __fastcall HookMeleeUpdateCollision(
    void* controller,
    void* ignoredEdx,
    void* record) {
    (void)ignoredEdx;
    MeleeCollisionRecordSnapshot snapshot =
        ReadMeleeCollisionRecord(record);
    const bool playerOwned = snapshot.readable &&
        MarkPhysicalMeleeCollisionControllerOwnership(
            controller, snapshot.sourceObject);
    const bool contactDamageEnabled =
        InterlockedCompareExchange(
            &g_physicalMeleeContactDamageEnabled, 0, 0) != 0;
    const bool gameplayContextActive =
        PhysicalMeleeContactDamageContextActive();
    const PhysicalMeleeCollisionRole collisionRole =
        playerOwned
        ? ReadPhysicalMeleeCollisionRole(
              controller, record, snapshot)
        : PhysicalMeleeCollisionRole::Unknown;
    const bool attackCollision =
        collisionRole == PhysicalMeleeCollisionRole::Attack;
    const bool maintainContinuously =
        ShouldMaintainPhysicalMeleeCollision(
            contactDamageEnabled, playerOwned,
            snapshot.collisionObject != 0U,
            gameplayContextActive, attackCollision);
    SetPhysicalMeleeCollisionLifetime(
        record, maintainContinuously);
    void* const previousActiveCollisionRecord =
        g_physicalMeleeActiveCollisionRecord;
    const bool previousPlayerCollisionUpdate =
        g_physicalMeleePlayerCollisionUpdate;
    const PhysicalMeleeCollisionRole previousCollisionRole =
        g_physicalMeleeActiveCollisionRole;
    g_physicalMeleeActiveCollisionRecord = record;
    g_physicalMeleePlayerCollisionUpdate = playerOwned;
    g_physicalMeleeActiveCollisionRole = collisionRole;
    __try {
        g_originalMeleeUpdateCollision(controller, record);
    } __finally {
        g_physicalMeleeActiveCollisionRecord =
            previousActiveCollisionRecord;
        g_physicalMeleePlayerCollisionUpdate =
            previousPlayerCollisionUpdate;
        g_physicalMeleeActiveCollisionRole =
            previousCollisionRole;
    }
    snapshot = ReadMeleeCollisionRecord(record);
    PublishPhysicalMeleePlayerCollisionObject(
        playerOwned,
        snapshot.readable ? snapshot.collisionObject : 0U,
        collisionRole);
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0 ||
        g_log == nullptr || record == nullptr) {
        return;
    }
    snapshot = ReadMeleeCollisionRecord(record);
    if (!snapshot.readable || snapshot.collisionObject == 0) {
        return;
    }

    const LONG call = InterlockedIncrement(
        &g_aimPathMeleeUpdateCalls);
    if (call > 512) {
        return;
    }
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    const int slot = ResolveMeleeCollisionRecordSlot(
        controller, record);
    char stack[192]{};
    FormatGameClientStack(stack, sizeof(stack));
    char detail[896]{};
    std::snprintf(
        detail, sizeof(detail),
        "call=%ld slot=%d controller=%p record=%p attack_index=%u "
        "source_object=0x%08lX source_node=0x%08lX "
        "collision_object=0x%08lX collision_notifier=0x%08lX "
        "collision_finished=%u player_owned=%u role=%s "
        "continuous_lifetime=%u "
        "controller_aim_valid=%u "
        "controller_forward=(%.4f,%.4f,%.4f) "
        "gameorig_stack_rvas=%s behavior=pass_through",
        call, slot, controller, record, snapshot.attackIndex,
        static_cast<unsigned long>(snapshot.sourceObject),
        static_cast<unsigned long>(snapshot.sourceNode),
        static_cast<unsigned long>(snapshot.collisionObject),
        static_cast<unsigned long>(snapshot.collisionNotifier),
        snapshot.collisionFinished,
        playerOwned ? 1U : 0U,
        PhysicalMeleeCollisionRoleName(collisionRole),
        maintainContinuously ? 1U : 0U,
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y,
        controllerForward.z, stack);
    g_log("m5_aim_path_melee_collision_update", detail);
}

void* __fastcall HookBuildRigidTransform(
    void* destination,
    void* ignoredEdx,
    const VectorAbi* position,
    const QuaternionAbi* rotation) {
    (void)ignoredEdx;
    const auto* const caller = static_cast<const unsigned char*>(
        _ReturnAddress());
    const bool meleeTransformCall = g_gameClientBase != nullptr &&
        caller == g_gameClientBase +
            kMeleeBuildRigidTransformReturnRva;
    if (!meleeTransformCall || position == nullptr ||
        rotation == nullptr) {
        return g_originalBuildRigidTransform(
            destination, position, rotation);
    }

    VectorAbi retailPosition{};
    QuaternionAbi retailRotation{};
    bool readable = false;
    __try {
        retailPosition = *position;
        retailRotation = *rotation;
        readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    if (!readable ||
        !std::isfinite(retailPosition.x) ||
        !std::isfinite(retailPosition.y) ||
        !std::isfinite(retailPosition.z) ||
        !std::isfinite(retailRotation.x) ||
        !std::isfinite(retailRotation.y) ||
        !std::isfinite(retailRotation.z) ||
        !std::isfinite(retailRotation.w)) {
        return g_originalBuildRigidTransform(
            destination, position, rotation);
    }

    VectorAbi appliedPosition = retailPosition;
    QuaternionAbi appliedRotation = retailRotation;
    bool physicalWallProxyApplied = false;
    std::uint64_t physicalSampleId = 0;
    const bool physicalWallProxyRequested =
        ShouldApplyPhysicalMeleePlayerOverride(
            InterlockedCompareExchange(
                &g_physicalMeleeWallProxyEnabled, 0, 0) != 0,
            g_physicalMeleePlayerCollisionUpdate);
    if (physicalWallProxyRequested) {
        PhysicalMeleeFrame frame{};
        PhysicalMeleeProfile profile{};
        std::int32_t weaponIndex = -1;
        if (CopyLatestPhysicalMeleeColliderSource(
                frame, physicalSampleId, profile, weaponIndex)) {
            if (g_physicalMeleeActiveCollisionRole ==
                PhysicalMeleeCollisionRole::Block) {
                const ToolMenuColliderSettings blockSettings =
                    ReadVrToolMenuBlockColliderSettings(weaponIndex);
                PhysicalMeleeFrame blockFrame{};
                if (ResolveToolMenuColliderFrameAtCurrentPose(
                        frame, profile, blockSettings, blockFrame)) {
                    frame = blockFrame;
                } else {
                    frame.poseValid = false;
                }
            }
            const PhysicalMeleeWallProxyTransform proxy =
                ResolvePhysicalMeleeWallProxyTransform(frame, true);
            if (proxy.active) {
                appliedPosition = {
                    proxy.positionUnits.x,
                    proxy.positionUnits.y,
                    proxy.positionUnits.z};
                appliedRotation = {
                    proxy.rotation.x,
                    proxy.rotation.y,
                    proxy.rotation.z,
                    proxy.rotation.w};
                physicalWallProxyApplied = true;
            }
        }
    }
    bool meleeAimApplied = false;
    if (!physicalWallProxyRequested &&
        g_physicalMeleePlayerCollisionUpdate &&
        InterlockedCompareExchange(
            &g_controllerMeleeAimEnabled, 0, 0) != 0) {
        float pivot[3]{};
        float baseRotation[4]{};
        float controllerRotation[4]{};
        const bool basisFresh = ReadTrackedMeleeAimBasis(
            pivot, baseRotation, controllerRotation);
        const auto resolved =
            ResolveControllerRelativeMeleeTransform(
                {retailPosition.x, retailPosition.y, retailPosition.z},
                {retailRotation.x, retailRotation.y,
                 retailRotation.z, retailRotation.w},
                {pivot[0], pivot[1], pivot[2]},
                {baseRotation[0], baseRotation[1],
                 baseRotation[2], baseRotation[3]},
                {controllerRotation[0], controllerRotation[1],
                 controllerRotation[2], controllerRotation[3]},
                basisFresh);
        if (resolved.active) {
            appliedPosition = {
                resolved.position.x,
                resolved.position.y,
                resolved.position.z};
            appliedRotation = {
                resolved.rotation.x,
                resolved.rotation.y,
                resolved.rotation.z,
                resolved.rotation.w};
            meleeAimApplied = true;
        }
    }

    void* const result = g_originalBuildRigidTransform(
        destination, &appliedPosition, &appliedRotation);
    if (physicalWallProxyApplied &&
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyAppliedLogged, 1, 0) == 0 &&
        g_log != nullptr) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "target=GameOrig+0x0000F690 sample_id=%llu "
            "source=controller_weapon_tip role_specific_geometry=1 "
            "native_impact_dispatch=contact_gate_owned "
            "actor_damage=qualified_pipe_contact_only "
            "stale_fallback=retail_transform",
            static_cast<unsigned long long>(physicalSampleId));
        g_log("m5_physical_melee_wall_proxy_active", detail);
    }
    if (meleeAimApplied &&
        InterlockedCompareExchange(
            &g_controllerMeleeAimLogged, 1, 0) == 0 &&
        g_log != nullptr) {
        g_log(
            "m5_controller_melee_aim_active",
            "target=GameOrig+0x0000F690 "
            "source=melee_node_transform "
            "operation=controller_delta_about_camera_pivot "
            "retail_swing_timing_and_shape_preserved=1 "
            "stale_fallback=retail");
    }
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0 ||
        g_log == nullptr) {
        return result;
    }
    const LONG call = InterlockedIncrement(
        &g_aimPathMeleeTransformCalls);
    if (call > 512) {
        return result;
    }
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    char detail[960]{};
    std::snprintf(
        detail, sizeof(detail),
        "call=%ld retail_position=(%.3f,%.3f,%.3f) "
        "applied_position=(%.3f,%.3f,%.3f) "
        "retail_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "applied_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "melee_aim_applied=%u physical_wall_proxy_applied=%u "
        "physical_sample_id=%llu "
        "controller_aim_valid=%u "
        "controller_forward=(%.4f,%.4f,%.4f) "
        "source=melee_node_transform",
        call, retailPosition.x, retailPosition.y, retailPosition.z,
        appliedPosition.x, appliedPosition.y, appliedPosition.z,
        retailRotation.x, retailRotation.y, retailRotation.z,
        retailRotation.w,
        appliedRotation.x, appliedRotation.y, appliedRotation.z,
        appliedRotation.w, meleeAimApplied ? 1U : 0U,
        physicalWallProxyApplied ? 1U : 0U,
        static_cast<unsigned long long>(physicalSampleId),
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y,
        controllerForward.z);
    g_log("m5_aim_path_melee_physics_transform", detail);
    return result;
}

std::uintptr_t __fastcall HookMeleeImpactDispatch(
    void* impactController,
    void* ignoredEdx,
    std::uintptr_t argument1,
    std::uintptr_t argument2,
    std::uintptr_t argument3,
    std::uintptr_t argument4,
    std::uintptr_t argument5,
    std::uintptr_t argument6,
    std::uintptr_t argument7,
    std::uintptr_t argument8,
    std::uintptr_t argument9) {
    (void)ignoredEdx;
    const auto* const caller = static_cast<const unsigned char*>(
        _ReturnAddress());
    const bool verifiedMeleeCallback = g_gameClientBase != nullptr &&
        caller == g_gameClientBase +
            kMeleeImpactDispatchReturnRva;
    if (!verifiedMeleeCallback ||
        InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0) {
        return g_originalMeleeImpactDispatch(
            impactController, argument1, argument2, argument3,
            argument4, argument5, argument6, argument7,
            argument8, argument9);
    }
    const bool automaticSeedImpactBlocked =
        PhysicalMeleeAutomaticSeedImpactIsBlocked(
            impactController);
    const bool playerOwnedCollision =
        PhysicalMeleeImpactControllerIsPlayerOwned(
            impactController) ||
        automaticSeedImpactBlocked;
    if (!playerOwnedCollision) {
        // Enemy and unrecognised Retail melee must remain completely
        // untouched by the local player's physical-proxy safety gate.
        return g_originalMeleeImpactDispatch(
            impactController, argument1, argument2, argument3,
            argument4, argument5, argument6, argument7,
            argument8, argument9);
    }

    VectorAbi contactPosition{};
    VectorAbi contactNormal{};
    const bool contactPositionValid = ReadVectorCandidate(
        argument4, contactPosition);
    const bool contactNormalValid = ReadVectorCandidate(
        argument5, contactNormal);
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    PhysicalMeleeFrame physicalFrame{};
    std::uint64_t physicalSampleId = 0;
    const bool physicalWallProxyEnabled =
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyEnabled, 0, 0) != 0;
    PhysicalMeleeContactQualification physicalContact{};
    bool physicalFrameAvailable = false;
    if (InterlockedCompareExchange(
            &g_physicalMeleeProbeEnabled, 0, 0) != 0) {
        if (automaticSeedImpactBlocked) {
            // The compatibility pulse may create contacts while Retail is
            // constructing its body. Never qualify/latch those targets.
            physicalContact.reason =
                PhysicalMeleeContactReason::AutomaticSeedSuppressed;
            physicalFrameAvailable =
                CopyLatestPhysicalMeleeFrame(
                    physicalFrame, physicalSampleId);
        } else {
            physicalFrameAvailable = physicalWallProxyEnabled
                ? EvaluatePhysicalMeleeContact(
                      argument1, contactPositionValid,
                      {contactPosition.x, contactPosition.y,
                       contactPosition.z},
                      physicalFrame, physicalSampleId,
                      physicalContact)
                : CopyLatestPhysicalMeleeFrame(
                      physicalFrame, physicalSampleId);
        }
    }
    // Enabling the wall-proxy feature must not claim Retail's impact path by
    // itself. With no fresh controller frame, no physical transform is being
    // applied, so flatscreen and stale-tracking callbacks remain Retail-owned.
    const bool physicalWallProxyActive =
        physicalWallProxyEnabled && physicalFrameAvailable;
    float physicalUnitsPerMeter = 0.0F;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    physicalUnitsPerMeter = g_physicalMeleeProfile.unitsPerMeter;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    const PhysicalMeleeContactDistance contactDistance =
        contactPositionValid && physicalFrameAvailable
            ? MeasurePhysicalMeleeContactDistance(
                  physicalFrame,
                  {contactPosition.x, contactPosition.y,
                   contactPosition.z},
                  physicalUnitsPerMeter)
            : PhysicalMeleeContactDistance{};
    float headPosition[3]{};
    float headRotation[4]{};
    const bool headPoseFresh = ReadTrackedHeadWorldPose(
        headPosition, headRotation);
    float gripPosition[3]{};
    float gripRotation[4]{};
    std::uint64_t gripSampleId = 0U;
    std::uint64_t gripTimestampNs = 0U;
    const bool gripPoseFresh = ReadTrackedControllerWorldPose(
        gripPosition, gripRotation,
        gripSampleId, gripTimestampNs);
    const CombatContactProximity contactProximity =
        MeasureCombatContactProximity(
            {headPosition[0], headPosition[1], headPosition[2]},
            headPoseFresh,
            {gripPosition[0], gripPosition[1], gripPosition[2]},
            gripPoseFresh,
            {contactPosition.x, contactPosition.y, contactPosition.z},
            contactPositionValid, physicalUnitsPerMeter);
    const ULONGLONG contactRuntimeTick = GetTickCount64();
    bool attackTelegraphEnabled = false;
    bool attackTelegraphTriggeredThisSwing = false;
    bool attackTelegraphPulseActive = false;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    attackTelegraphEnabled =
        g_physicalMeleeProfile.swingAttackEnabled;
    attackTelegraphTriggeredThisSwing =
        attackTelegraphEnabled &&
        !g_physicalMeleeSwingAttackState.armed;
    attackTelegraphPulseActive =
        attackTelegraphEnabled &&
        PhysicalMeleeSwingAttackPulseIsActive(
            g_physicalMeleeSwingAttackState,
            static_cast<std::uint64_t>(contactRuntimeTick));
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    if (physicalContact.accepted) {
        InterlockedIncrement(&g_physicalMeleeContactAccepted);
    }
    const bool contactDamageActive =
        PhysicalMeleeContactDamageContextActive();
    const bool nativeImpactForwarded =
        ShouldDispatchPhysicalMeleeNativeImpact(
            physicalWallProxyActive, playerOwnedCollision,
            contactDamageActive, physicalContact.accepted,
            automaticSeedImpactBlocked);
    const std::uintptr_t result = nativeImpactForwarded
        ? g_originalMeleeImpactDispatch(
              impactController, argument1, argument2, argument3,
              argument4, argument5, argument6, argument7,
              argument8, argument9)
        : 0U;
    RetailMeleeTargetReferenceVectorRelease retailTargetVector{};
    const bool retailCleanupRequired =
        contactDamageActive || automaticSeedImpactBlocked;
    if (retailCleanupRequired) {
        // argument8 is the verified vector<LTObjRef> header at controller
        // +0x60/+0xC0, not an LTObjRef element. The callback appends before
        // dispatch; clear every live element and rewind end after either an
        // accepted dispatch or a de-duplicated callback.
        retailTargetVector = ReleaseRetailMeleeTargetReferenceVector(
            impactController, argument8);
        if (!retailTargetVector.ok &&
            InterlockedCompareExchange(
                &g_physicalMeleeRetailLatchReleaseFailedLogged,
                1, 0) == 0 &&
            g_log != nullptr) {
            char failureDetail[640]{};
            std::snprintf(
                failureDetail, sizeof(failureDetail),
                "target_reference_vector_clear_failed=1 state=%s "
                "begin=0x%08lX end_before=0x%08lX "
                "end_after=0x%08lX capacity=0x%08lX "
                "references_before=%zu references_cleared=%zu",
                retailTargetVector.state,
                static_cast<unsigned long>(
                    retailTargetVector.beginBefore),
                static_cast<unsigned long>(
                    retailTargetVector.endBefore),
                static_cast<unsigned long>(
                    retailTargetVector.endAfter),
                static_cast<unsigned long>(
                    retailTargetVector.capacityBefore),
                retailTargetVector.referencesBefore,
                retailTargetVector.referencesCleared);
            g_log("m5_physical_melee_retail_latch_release_failed",
                  failureDetail);
        }
    }
    LONG damageDispatchCount = InterlockedCompareExchange(
        &g_physicalMeleeDamageDispatched, 0, 0);
    if (nativeImpactForwarded && contactDamageActive &&
        physicalContact.accepted) {
        damageDispatchCount = InterlockedIncrement(
            &g_physicalMeleeDamageDispatched);
    }
    if (automaticSeedImpactBlocked) {
        const LONG blocked = InterlockedIncrement(
            &g_physicalMeleeAutomaticSeedImpactsBlocked);
        if (g_log != nullptr && blocked <= 64) {
            char detail[448]{};
            std::snprintf(
                detail, sizeof(detail),
                "count=%ld impact_controller=%p "
                "target=0x%08lX target_node=0x%08lX "
                "retail_ref_vector_clear_ok=%u "
                "retail_refs_cleared=%zu "
                "contact_latch_mutated=0 native_forwarded=0",
                blocked, impactController,
                static_cast<unsigned long>(argument1),
                static_cast<unsigned long>(argument2),
                retailTargetVector.ok ? 1U : 0U,
                retailTargetVector.referencesCleared);
            g_log(
                "m5_physical_melee_auto_seed_impact_blocked",
                detail);
        }
    }
    if (g_log == nullptr) {
        return result;
    }
    const LONG call = InterlockedIncrement(
        &g_aimPathMeleeImpactCalls);
    const bool verboseContactDiagnostic = call <= 512;
    const bool retailCleanupFailed =
        retailCleanupRequired && !retailTargetVector.ok;
    const bool compactContactDiagnostic =
        verboseContactDiagnostic || physicalContact.accepted ||
        retailCleanupFailed;
    if (!compactContactDiagnostic) {
        return result;
    }
    std::size_t passTargetCount = 0U;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    passTargetCount = g_physicalMeleeContactState.targetCount;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    const std::uintptr_t controllerAddress =
        reinterpret_cast<std::uintptr_t>(impactController);
    const int targetSlot = argument8 == controllerAddress + 0x60U
        ? 0
        : (argument8 == controllerAddress + 0xC0U ? 1 : -1);
    const bool actorCandidate = argument3 == 9U &&
        argument2 != static_cast<std::uintptr_t>(-1);
    if (actorCandidate && physicalContact.accepted) {
        PublishPlayerCollisionXrayTarget(
            argument1,
            contactPositionValid ? &contactPosition : nullptr);
        const LONG observation = InterlockedIncrement(
            &g_enemyColliderObservationEvents);
        if (observation <= 64) {
            void* moveManager = nullptr;
            void* playerObject = nullptr;
            void* physics = nullptr;
            GetObjectDimensionsFunction getDimensions = nullptr;
            SetObjectDimensionsFunction setDimensions = nullptr;
            const bool contextValid = ResolvePlayerColliderContext(
                moveManager, playerObject, physics,
                getDimensions, setDimensions);
            PlayerColliderDimensions playerDimensions{};
            PlayerColliderDimensions targetDimensions{};
            const bool playerDimensionsValid = contextValid &&
                ReadPlayerColliderActualDimensions(
                    physics, playerObject, getDimensions,
                    playerDimensions);
            const bool targetDimensionsValid = contextValid &&
                argument1 != 0U &&
                ReadPlayerColliderActualDimensions(
                    physics, reinterpret_cast<void*>(argument1),
                    getDimensions, targetDimensions);
            const PlayerColliderSettings playerSettings =
                CurrentPlayerColliderSettings();
            char observationDetail[768]{};
            std::snprintf(
                observationDetail, sizeof(observationDetail),
                "count=%ld target=0x%08lX target_node=0x%08lX "
                "player=%p width_scale=%.2f "
                "player_dims_valid=%u player_dims=(%.3f,%.3f,%.3f) "
                "target_dims_valid=%u target_dims=(%.3f,%.3f,%.3f) "
                "head_pose_valid=%u "
                "head_horizontal_to_contact_m=%.4f "
                "query=ILTClientPhysics.GetObjectDims mutation=none",
                static_cast<long>(observation),
                static_cast<unsigned long>(argument1),
                static_cast<unsigned long>(argument2),
                playerObject, playerSettings.widthScale,
                playerDimensionsValid ? 1U : 0U,
                playerDimensions.x, playerDimensions.y,
                playerDimensions.z,
                targetDimensionsValid ? 1U : 0U,
                targetDimensions.x, targetDimensions.y,
                targetDimensions.z,
                contactProximity.headValid ? 1U : 0U,
                contactProximity.headHorizontalToContactMeters);
            g_log(
                "m5_enemy_collider_observed",
                observationDetail);
        }
    }
    char contactDiagnostic[2048]{};
    std::snprintf(
        contactDiagnostic, sizeof(contactDiagnostic),
        "call=%ld target=0x%08lX target_node=0x%08lX "
        "target_kind=%s accepted=%u reason=%s native_forwarded=%u "
        "retail_ref_vector_clear_ok=%u retail_ref_vector_state=%s "
        "retail_ref_vector_begin=0x%08lX "
        "retail_ref_vector_end_before=0x%08lX "
        "retail_ref_vector_end_after=0x%08lX "
        "retail_ref_vector_capacity=0x%08lX "
        "retail_refs_before=%zu retail_refs_cleared=%zu "
        "target_slot=%d pass_target_count=%zu "
        "damage_dispatch_count=%ld proxy_active=%u "
        "contact_damage_active=%u automatic_seed_damage_blocked=%u "
        "speed_mps=%.3f energy_j=%.3f "
        "contact_position_valid=%u contact_distance_valid=%u "
        "weapon_tip_to_target_contact_m=%.4f "
        "weapon_axis_to_target_contact_m=%.4f "
        "weapon_capsule_to_target_gap_m=%.4f "
        "weapon_capsule_radius_m=%.4f contact_axis_t=%.3f "
        "runtime_tick_ms=%llu "
        "head_pose_valid=%u head_to_target_contact_m=%.4f "
        "head_horizontal_to_target_contact_m=%.4f "
        "grip_pose_valid=%u grip_to_target_contact_m=%.4f "
        "attack_telegraph_enabled=%u "
        "attack_telegraph_triggered_this_swing=%u "
        "attack_telegraph_pulse_active=%u enemy_health_observed=0",
        call, static_cast<unsigned long>(argument1),
        static_cast<unsigned long>(argument2),
        actorCandidate ? "actor_candidate" : "world_or_prop",
        physicalContact.accepted ? 1U : 0U,
        PhysicalMeleeContactReasonName(physicalContact.reason),
        nativeImpactForwarded ? 1U : 0U,
        retailTargetVector.ok ? 1U : 0U,
        retailTargetVector.state,
        static_cast<unsigned long>(retailTargetVector.beginBefore),
        static_cast<unsigned long>(retailTargetVector.endBefore),
        static_cast<unsigned long>(retailTargetVector.endAfter),
        static_cast<unsigned long>(retailTargetVector.capacityBefore),
        retailTargetVector.referencesBefore,
        retailTargetVector.referencesCleared,
        targetSlot, passTargetCount,
        static_cast<long>(damageDispatchCount),
        physicalWallProxyActive ? 1U : 0U,
        contactDamageActive ? 1U : 0U,
        automaticSeedImpactBlocked ? 1U : 0U,
        physicalContact.swingSpeedMetersPerSecond,
        physicalContact.swingEnergyJoules,
        contactPositionValid ? 1U : 0U,
        contactDistance.valid ? 1U : 0U,
        contactDistance.tipToContactMeters,
        contactDistance.centerlineToContactMeters,
        contactDistance.capsuleSurfaceGapMeters,
        contactDistance.capsuleRadiusMeters,
        contactDistance.axisFraction,
        static_cast<unsigned long long>(contactRuntimeTick),
        contactProximity.headValid ? 1U : 0U,
        contactProximity.headToContactMeters,
        contactProximity.headHorizontalToContactMeters,
        contactProximity.gripValid ? 1U : 0U,
        contactProximity.gripToContactMeters,
        attackTelegraphEnabled ? 1U : 0U,
        attackTelegraphTriggeredThisSwing ? 1U : 0U,
        attackTelegraphPulseActive ? 1U : 0U);
    g_log("m5_weapon_test_contact", contactDiagnostic);
    char detail[2048]{};
    if (!verboseContactDiagnostic) {
        // Held-overlap callbacks can consume the verbose budget in seconds.
        // Preserve every later accepted contact and cleanup failure for the
        // live watcher without allowing duplicate detail to grow unbounded.
        return result;
    }

    std::snprintf(
        detail, sizeof(detail),
        "call=%ld impact_controller=%p "
        "args=(0x%08lX,0x%08lX,0x%08lX,0x%08lX,0x%08lX,"
        "0x%08lX,0x%08lX,0x%08lX,0x%08lX) "
        "contact_position_valid=%u "
        "contact_position=(%.3f,%.3f,%.3f) "
        "contact_distance_valid=%u "
        "weapon_tip_to_target_contact_m=%.4f "
        "weapon_axis_to_target_contact_m=%.4f "
        "weapon_capsule_to_target_gap_m=%.4f "
        "weapon_capsule_radius_m=%.4f contact_axis_t=%.3f "
        "contact_normal_valid=%u "
        "contact_normal=(%.4f,%.4f,%.4f) "
        "controller_aim_valid=%u "
        "controller_forward=(%.4f,%.4f,%.4f) "
        "physical_sample_valid=%u physical_sample_id=%llu "
        "physical_base=(%.3f,%.3f,%.3f) "
        "physical_tip=(%.3f,%.3f,%.3f) "
        "physical_sweep_valid=%u physical_speed_mps=%.3f "
        "physical_energy_j=%.3f physical_damage_qualified=%u "
        "physical_wall_proxy_enabled=%u physical_wall_proxy_active=%u "
        "contact_damage_active=%u automatic_seed_damage_blocked=%u "
        "native_impact_forwarded=%u retail_ref_vector_cleared=%u "
        "damage_dispatch_count=%ld "
        "physical_contact_accepted=%u contact_reason=%s "
        "swing_speed_mps=%.3f swing_energy_j=%.3f "
        "result=0x%08lX source=melee_collision_callback "
        "behavior=%s",
        call, impactController,
        static_cast<unsigned long>(argument1),
        static_cast<unsigned long>(argument2),
        static_cast<unsigned long>(argument3),
        static_cast<unsigned long>(argument4),
        static_cast<unsigned long>(argument5),
        static_cast<unsigned long>(argument6),
        static_cast<unsigned long>(argument7),
        static_cast<unsigned long>(argument8),
        static_cast<unsigned long>(argument9),
        contactPositionValid ? 1U : 0U,
        contactPosition.x, contactPosition.y, contactPosition.z,
        contactDistance.valid ? 1U : 0U,
        contactDistance.tipToContactMeters,
        contactDistance.centerlineToContactMeters,
        contactDistance.capsuleSurfaceGapMeters,
        contactDistance.capsuleRadiusMeters,
        contactDistance.axisFraction,
        contactNormalValid ? 1U : 0U,
        contactNormal.x, contactNormal.y, contactNormal.z,
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y,
        controllerForward.z,
        physicalFrameAvailable ? 1U : 0U,
        static_cast<unsigned long long>(physicalSampleId),
        physicalFrame.currentBaseUnits.x,
        physicalFrame.currentBaseUnits.y,
        physicalFrame.currentBaseUnits.z,
        physicalFrame.currentTipUnits.x,
        physicalFrame.currentTipUnits.y,
        physicalFrame.currentTipUnits.z,
        physicalFrame.sweepValid ? 1U : 0U,
        physicalFrame.impactSpeedMetersPerSecond,
        physicalFrame.impactEnergyJoules,
        physicalFrame.damageQualified ? 1U : 0U,
        physicalWallProxyEnabled ? 1U : 0U,
        physicalWallProxyActive ? 1U : 0U,
        contactDamageActive ? 1U : 0U,
        automaticSeedImpactBlocked ? 1U : 0U,
        nativeImpactForwarded ? 1U : 0U,
        retailTargetVector.ok ? 1U : 0U,
        static_cast<long>(damageDispatchCount),
        physicalContact.accepted ? 1U : 0U,
        PhysicalMeleeContactReasonName(physicalContact.reason),
        physicalContact.swingSpeedMetersPerSecond,
        physicalContact.swingEnergyJoules,
        static_cast<unsigned long>(result),
        automaticSeedImpactBlocked
            ? "automatic_seed_damage_blocked"
            : nativeImpactForwarded
                ? (contactDamageActive
                       ? "qualified_contact_damage"
                       : "pass_through")
                : "physical_contact_blocked");
    g_log("m5_aim_path_melee_impact_dispatch", detail);
    return result;
}

const char* RetailGameStateName(int state) noexcept {
    switch (state) {
    case kCondemnedGameStateUndefined:
        return "undefined";
    case kCondemnedGameStatePlaying:
        return "playing";
    case kCondemnedGameStateExiting:
        return "exiting";
    case kCondemnedGameStateLoading:
        return "loading";
    case kCondemnedGameStateSplash:
        return "splash";
    case kCondemnedGameStateMenu:
        return "menu";
    case kCondemnedGameStateScreen:
        return "screen";
    case kCondemnedGameStatePaused:
        return "paused";
    case kCondemnedGameStateDemo:
        return "demo";
    case kCondemnedGameStateMovie:
        return "movie";
    default:
        return "unknown";
    }
}

int ReadRetailGameState(void* interfaceManager) noexcept {
    if (interfaceManager == nullptr) {
        return kUnknownRetailGameState;
    }
    __try {
        const int state = *reinterpret_cast<const int*>(
            static_cast<const unsigned char*>(interfaceManager) +
            kInterfaceManagerStateOffset);
        return IsKnownCondemnedGameState(state)
            ? state
            : kUnknownRetailGameState;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kUnknownRetailGameState;
    }
}

RetailPlayerVitals ReadRetailPlayerVitals() noexcept {
    if (InterlockedCompareExchange(
            &g_combatPlayerVitalsEnabled, 0, 0) == 0 ||
        g_gameClientBase == nullptr) {
        return {};
    }
    void* stats = nullptr;
    std::uint32_t currentHealth = 0U;
    std::uint32_t maximumHealth = 0U;
    bool readable = false;
    __try {
        std::memcpy(
            &stats, g_gameClientBase + kPlayerStatsGlobalRva,
            sizeof(stats));
        if (stats != nullptr) {
            const auto* const bytes =
                static_cast<const unsigned char*>(stats);
            std::memcpy(
                &currentHealth,
                bytes + kPlayerCurrentHealthOffset,
                sizeof(currentHealth));
            std::memcpy(
                &maximumHealth,
                bytes + kPlayerMaximumHealthOffset,
                sizeof(maximumHealth));
            readable = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    return ResolveRetailPlayerVitals(
        currentHealth, maximumHealth, readable);
}

void SampleRetailPlayerVitals() noexcept {
    if (InterlockedCompareExchange(
            &g_combatPlayerVitalsEnabled, 0, 0) == 0 ||
        g_log == nullptr ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_combatPlayerVitalsLastSampleTick != 0U &&
        now - g_combatPlayerVitalsLastSampleTick <
            kPlayerVitalsSampleIntervalMilliseconds) {
        return;
    }
    g_combatPlayerVitalsLastSampleTick = now;
    const RetailPlayerVitals vitals = ReadRetailPlayerVitals();
    if (!vitals.valid) {
        if (InterlockedCompareExchange(
                &g_combatPlayerVitalsUnavailableLogged, 1, 0) == 0) {
            g_log(
                "m5_combat_player_vitals_unavailable",
                "source=GameOrig+0x001702F8 "
                "reason=null_unreadable_or_implausible "
                "behavior=diagnostic_only");
        }
        return;
    }
    if (g_combatPlayerVitalsHaveSample &&
        vitals.currentHealth == g_combatPlayerVitalsLastHealth &&
        vitals.maximumHealth == g_combatPlayerVitalsLastMaximum) {
        return;
    }
    const long long delta = g_combatPlayerVitalsHaveSample
        ? static_cast<long long>(vitals.currentHealth) -
              static_cast<long long>(g_combatPlayerVitalsLastHealth)
        : 0LL;
    char detail[384]{};
    std::snprintf(
        detail, sizeof(detail),
        "runtime_tick_ms=%llu current=%u maximum=%u fraction=%.4f "
        "delta=%lld initial=%u source=GameOrig+0x001702F8_fields_04_0C "
        "behavior=read_only cause=unattributed",
        static_cast<unsigned long long>(now),
        vitals.currentHealth, vitals.maximumHealth,
        vitals.healthFraction, delta,
        g_combatPlayerVitalsHaveSample ? 0U : 1U);
    g_log("m5_combat_player_vitals", detail);
    g_combatPlayerVitalsLastHealth = vitals.currentHealth;
    g_combatPlayerVitalsLastMaximum = vitals.maximumHealth;
    g_combatPlayerVitalsHaveSample = true;
}

const char* ForensicDisplayKindName(
    ForensicDisplayKind kind) noexcept {
    switch (kind) {
    case ForensicDisplayKind::scanner:
        return "scanner";
    case ForensicDisplayKind::digitalCamera:
        return "digital_camera";
    case ForensicDisplayKind::other:
        return "other";
    default:
        return "none";
    }
}

bool ReadForensicMemoryRange(
    const void* source,
    std::size_t size,
    unsigned char* destination) noexcept {
    if (source == nullptr || destination == nullptr || size == 0U) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(source, &information, sizeof(information)) !=
            sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0U) {
        return false;
    }
    const DWORD protection = information.Protect &
        ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    const bool readable = protection == PAGE_READONLY ||
        protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    if (!readable) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(source);
    const auto regionStart =
        reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    const auto regionEnd = regionStart + information.RegionSize;
    if (regionEnd < regionStart || address < regionStart ||
        address > regionEnd || size > regionEnd - address) {
        return false;
    }
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct ForensicLiveDisplayState {
    std::uintptr_t object{0U};
    std::uintptr_t vtable{0U};
    std::uint64_t state{0U};
    bool stateReadable{false};
};

ForensicLiveDisplayState CaptureForensicLiveDisplayState(
    PVOID volatile* trackedObject,
    std::size_t stateOffset,
    std::size_t stateSize) noexcept {
    ForensicLiveDisplayState snapshot{};
    if (trackedObject == nullptr || stateSize == 0U ||
        stateSize > sizeof(snapshot.state)) {
        return snapshot;
    }
    void* const object = InterlockedCompareExchangePointer(
        trackedObject, nullptr, nullptr);
    snapshot.object = reinterpret_cast<std::uintptr_t>(object);
    if (object == nullptr) {
        return snapshot;
    }
    ReadForensicMemoryRange(
        object, sizeof(snapshot.vtable),
        reinterpret_cast<unsigned char*>(&snapshot.vtable));
    snapshot.stateReadable = ReadForensicMemoryRange(
        static_cast<const unsigned char*>(object) + stateOffset,
        stateSize,
        reinterpret_cast<unsigned char*>(&snapshot.state));
    return snapshot;
}

void ObserveForensicDisplayState(
    const char* displayKind,
    void* display,
    std::size_t stateOffset,
    std::size_t stateSize,
    PVOID volatile* trackedObject,
    volatile LONG64* lastState,
    volatile LONG* eventCount,
    float updateArgument) noexcept {
    if (display == nullptr || trackedObject == nullptr ||
        lastState == nullptr || eventCount == nullptr) {
        return;
    }
    void* const priorObject = InterlockedExchangePointer(
        trackedObject, display);
    if (priorObject != display) {
        InterlockedExchange64(lastState, -1);
    }
    const ForensicLiveDisplayState snapshot =
        CaptureForensicLiveDisplayState(
            trackedObject, stateOffset, stateSize);
    const LONG64 stateValue = snapshot.stateReadable
        ? static_cast<LONG64>(snapshot.state)
        : -1;
    const LONG64 priorState = InterlockedExchange64(
        lastState, stateValue);
    if (priorObject == display && priorState == stateValue) {
        return;
    }
    const LONG eventIndex = InterlockedIncrement(eventCount);
    if (eventIndex > 256 || g_log == nullptr) {
        return;
    }
    std::uint32_t vtableRva = 0U;
    if (g_gameClientBase != nullptr &&
        snapshot.vtable >= reinterpret_cast<std::uintptr_t>(
            g_gameClientBase) &&
        snapshot.vtable < reinterpret_cast<std::uintptr_t>(
            g_gameClientBase) + kRetailGameImageSize) {
        vtableRva = static_cast<std::uint32_t>(
            snapshot.vtable -
            reinterpret_cast<std::uintptr_t>(g_gameClientBase));
    }
    const auto state = static_cast<unsigned long long>(
        snapshot.state);
    char detail[768]{};
    if (displayKind != nullptr &&
        std::strcmp(displayKind, "scanner") == 0) {
        std::snprintf(
            detail, sizeof(detail),
            "event_index=%ld display_kind=%s object=0x%08lX "
            "vtable=0x%08lX vtable_rva=0x%08X "
            "state_offset=0x%03X state_bytes=%u state_readable=%u "
            "state=0x%012llX flags=(%u,%u,%u,%u,%u,%u) "
            "target_hit=%u framing_ok=%u can_photo=%u "
            "update_argument=%.6f source=verified_display_update "
            "engine_writes=0",
            static_cast<long>(eventIndex), displayKind,
            static_cast<unsigned long>(snapshot.object),
            static_cast<unsigned long>(snapshot.vtable),
            vtableRva, static_cast<unsigned int>(stateOffset),
            static_cast<unsigned int>(stateSize),
            snapshot.stateReadable ? 1U : 0U, state,
            static_cast<unsigned int>(snapshot.state & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 8U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 16U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 24U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 32U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 40U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 24U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 32U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 40U) & 0xFFU),
            updateArgument);
    } else {
        std::snprintf(
            detail, sizeof(detail),
            "event_index=%ld display_kind=%s object=0x%08lX "
            "vtable=0x%08lX vtable_rva=0x%08X "
            "state_offset=0x%03X state_bytes=%u state_readable=%u "
            "state=0x%08llX flags=(%u,%u,%u,%u) "
            "update_argument=%.6f source=verified_display_update "
            "engine_writes=0",
            static_cast<long>(eventIndex),
            displayKind == nullptr ? "unknown" : displayKind,
            static_cast<unsigned long>(snapshot.object),
            static_cast<unsigned long>(snapshot.vtable),
            vtableRva, static_cast<unsigned int>(stateOffset),
            static_cast<unsigned int>(stateSize),
            snapshot.stateReadable ? 1U : 0U, state,
            static_cast<unsigned int>(snapshot.state & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 8U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 16U) & 0xFFU),
            static_cast<unsigned int>((snapshot.state >> 24U) & 0xFFU),
            updateArgument);
    }
    g_log("m5_forensic_display_state", detail);
}

int ForensicCameraSocketPoseIndex(
    ForensicDisplayKind kind) noexcept {
    switch (kind) {
    case ForensicDisplayKind::scanner:
        return 0;
    case ForensicDisplayKind::digitalCamera:
        return 1;
    default:
        return -1;
    }
}

bool ResolveForensicCameraSocketForward(
    const QuaternionAbi& rotation,
    fearvr::TrackingQuaternion& normalizedRotation,
    ForensicRayVector& forward) noexcept {
    const fearvr::TrackingQuaternion rawRotation{
        rotation.x, rotation.y, rotation.z, rotation.w};
    const float lengthSquared =
        rawRotation.x * rawRotation.x +
        rawRotation.y * rawRotation.y +
        rawRotation.z * rawRotation.z +
        rawRotation.w * rawRotation.w;
    if (!fearvr::IsFinite(rawRotation) ||
        !std::isfinite(lengthSquared) ||
        lengthSquared <= 0.000001F) {
        normalizedRotation = {};
        forward = {};
        return false;
    }
    normalizedRotation = fearvr::Normalize(rawRotation);
    const fearvr::TrackingVector resolvedForward = fearvr::Rotate(
        normalizedRotation, {0.0F, 0.0F, 1.0F});
    if (!fearvr::IsFinite(resolvedForward)) {
        normalizedRotation = {};
        forward = {};
        return false;
    }
    forward = {
        resolvedForward.x, resolvedForward.y,
        resolvedForward.z};
    return true;
}

std::uint32_t __fastcall HookForensicCameraSocketTransform(
    void* display,
    void* ignoredEdx,
    RigidTransformAbi* transform) {
    (void)ignoredEdx;
    if (g_originalForensicCameraSocketTransform == nullptr) {
        return 1U;
    }
    const std::uint32_t result =
        g_originalForensicCameraSocketTransform(
            display, transform);
    if (result != 0U || display == nullptr ||
        transform == nullptr ||
        InterlockedCompareExchange(
            &g_forensicCameraSocketRayEnabled, 0, 0) == 0) {
        return result;
    }

    std::uintptr_t vtable = 0U;
    RigidTransformAbi captured{};
    if (!ReadForensicMemoryRange(
            display, sizeof(vtable),
            reinterpret_cast<unsigned char*>(&vtable)) ||
        !ReadForensicMemoryRange(
            transform, sizeof(captured),
            reinterpret_cast<unsigned char*>(&captured))) {
        return result;
    }
    const ForensicDisplayKind kind =
        ResolveForensicDisplayKind(vtable);
    const int poseIndex =
        ForensicCameraSocketPoseIndex(kind);
    if (poseIndex < 0 ||
        !std::isfinite(captured.position.x) ||
        !std::isfinite(captured.position.y) ||
        !std::isfinite(captured.position.z)) {
        return result;
    }

    fearvr::TrackingQuaternion normalizedRotation{};
    ForensicRayVector forward{};
    if (!ResolveForensicCameraSocketForward(
            captured.rotation, normalizedRotation, forward)) {
        return result;
    }

    ForensicCameraSocketPoseSnapshot snapshot{};
    snapshot.origin = {
        captured.position.x, captured.position.y,
        captured.position.z};
    snapshot.rotation = normalizedRotation;
    snapshot.forward = forward;
    snapshot.display =
        reinterpret_cast<std::uintptr_t>(display);
    snapshot.displayKind = kind;
    snapshot.capturedTick = GetTickCount64();
    snapshot.valid = true;
    const std::size_t snapshotIndex =
        static_cast<std::size_t>(poseIndex);
    AcquireSRWLockExclusive(&g_forensicCameraSocketPoseLock);
    snapshot.sequence =
        ++g_forensicCameraSocketPoseSequence;
    g_forensicCameraSocketPoses[snapshotIndex] = snapshot;
    ReleaseSRWLockExclusive(&g_forensicCameraSocketPoseLock);

    const LONG capture = InterlockedIncrement(
        &g_forensicCameraSocketPoseCaptures[snapshotIndex]);
    if (g_log != nullptr &&
        (capture <= 16 || (capture % 240) == 0)) {
        char detail[768]{};
        std::snprintf(
            detail, sizeof(detail),
            "capture=%ld display_kind=%s display=%p "
            "sequence=%llu socket=Camera "
            "position=(%.3f,%.3f,%.3f) "
            "rotation=(%.6f,%.6f,%.6f,%.6f) "
            "forward=(%.5f,%.5f,%.5f) "
            "source=Retail_vslot_0x24_GameOrig+0x%08X "
            "result=LT_OK engine_writes=0",
            capture, ForensicDisplayKindName(kind), display,
            static_cast<unsigned long long>(
                snapshot.sequence),
            snapshot.origin.x, snapshot.origin.y,
            snapshot.origin.z, snapshot.rotation.x,
            snapshot.rotation.y, snapshot.rotation.z,
            snapshot.rotation.w, snapshot.forward.x,
            snapshot.forward.y, snapshot.forward.z,
            static_cast<unsigned int>(
                kForensicCameraSocketTransformRva));
        g_log(
            "m5_forensic_camera_socket_pose", detail);
    }
    return result;
}

constexpr ForensicDisplayKind
ResolveForensicCameraSocketDisplayKindForWeaponIndex(
    std::int32_t weaponIndex) noexcept {
    switch (weaponIndex) {
    case kForensicScannerWeaponIndex:
        return ForensicDisplayKind::scanner;
    case kForensicItemCameraWeaponIndex:
        return ForensicDisplayKind::digitalCamera;
    default:
        return ForensicDisplayKind::none;
    }
}

static_assert(
    ResolveForensicCameraSocketDisplayKindForWeaponIndex(
        kForensicScannerWeaponIndex) == ForensicDisplayKind::scanner);
static_assert(
    ResolveForensicCameraSocketDisplayKindForWeaponIndex(
        kForensicItemCameraWeaponIndex) ==
        ForensicDisplayKind::digitalCamera);
static_assert(
    ResolveForensicCameraSocketDisplayKindForWeaponIndex(
        kForensicCollectionToolBaseWeaponIndex) == ForensicDisplayKind::none);
bool ReadFreshForensicCameraSocketPose(
    ForensicDisplayKind expectedDisplayKind,
    ForensicCameraSocketPoseSnapshot& snapshot,
    ULONGLONG& ageMilliseconds,
    bool& posePublished) noexcept {
    snapshot = {};
    ageMilliseconds = ~static_cast<ULONGLONG>(0U);
    posePublished = false;
    const int poseIndex = ForensicCameraSocketPoseIndex(
        expectedDisplayKind);
    if (poseIndex < 0) {
        return false;
    }
    AcquireSRWLockShared(&g_forensicCameraSocketPoseLock);
    const ForensicCameraSocketPoseSnapshot captured =
        g_forensicCameraSocketPoses[
            static_cast<std::size_t>(poseIndex)];
    ReleaseSRWLockShared(&g_forensicCameraSocketPoseLock);
    posePublished = captured.valid;
    if (!captured.valid ||
        captured.displayKind != expectedDisplayKind) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < captured.capturedTick) {
        return false;
    }
    ageMilliseconds = now - captured.capturedTick;
    if (ageMilliseconds >
        kForensicCameraSocketFreshnessMilliseconds) {
        return false;
    }
    snapshot = captured;
    return true;
}

void __fastcall HookScannerDisplayUpdate(
    void* display,
    void* ignoredEdx,
    float updateArgument) {
    (void)ignoredEdx;
    g_originalScannerDisplayUpdate(display, updateArgument);
    ObserveForensicDisplayState(
        "scanner", display, kScannerDisplayStateOffset,
        kScannerDisplayStateSize, &g_forensicScannerDisplay,
        &g_forensicScannerState, &g_forensicScannerStateEvents,
        updateArgument);
}

void __fastcall HookDigitalCameraDisplayUpdate(
    void* display,
    void* ignoredEdx,
    float updateArgument) {
    (void)ignoredEdx;
    g_originalDigitalCameraDisplayUpdate(display, updateArgument);
    ObserveForensicDisplayState(
        "digital_camera", display,
        kDigitalCameraDisplayStateOffset,
        kDigitalCameraDisplayStateSize,
        &g_forensicDigitalCameraDisplay,
        &g_forensicDigitalCameraState,
        &g_forensicDigitalCameraStateEvents, updateArgument);
}

bool ReadEquippedForensicCameraTool(
    std::int32_t& weaponIndex,
    unsigned int& weaponType,
    ForensicDisplayKind& expectedDisplayKind) noexcept {
    weaponIndex = -1;
    weaponType = 0U;
    expectedDisplayKind = ForensicDisplayKind::none;
    if (g_gameClientBase == nullptr ||
        InterlockedCompareExchange(
            &g_coreActionsEnabled, 0, 0) == 0 ||
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        return false;
    }

    __try {
        std::uintptr_t weaponManager = 0U;
        std::memcpy(
            &weaponManager,
            g_gameClientBase + kWeaponManagerGlobalRva,
            sizeof(weaponManager));
        if (weaponManager == 0U) {
            return false;
        }
        const auto* const manager =
            reinterpret_cast<const unsigned char*>(
                weaponManager);
        std::uintptr_t weapon = 0U;
        std::memcpy(
            &weaponIndex,
            manager + kCurrentWeaponIndexOffset,
            sizeof(weaponIndex));
        std::memcpy(
            &weapon,
            manager + kCurrentWeaponOffset,
            sizeof(weapon));
        if (weapon == 0U) {
            return false;
        }
        weaponType =
            reinterpret_cast<const unsigned char*>(
                weapon)[0x34DU];
        expectedDisplayKind =
            ResolveForensicCameraSocketDisplayKindForWeaponIndex(
                weaponIndex);
        return weaponType == kForensicCameraToolWeaponType &&
            expectedDisplayKind != ForensicDisplayKind::none;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        weaponIndex = -1;
        weaponType = 0U;
        expectedDisplayKind = ForensicDisplayKind::none;
        return false;
    }
}

int ForensicIntersectCallsiteIndex(
    const void* returnAddress) noexcept {
    if (g_gameClientBase == nullptr ||
        returnAddress == nullptr) {
        return -1;
    }
    const auto* const caller =
        static_cast<const unsigned char*>(returnAddress);
    if (caller ==
        g_gameClientBase +
            kForensicIntersectFirstReturnRva) {
        return 0;
    }
    if (caller ==
        g_gameClientBase +
            kForensicIntersectSecondReturnRva) {
        return 1;
    }
    return -1;
}

bool __fastcall HookForensicIntersectSegment(
    void* engineClient,
    void* ignoredEdx,
    void* query,
    void* resultBuffer) {
    (void)ignoredEdx;
    if (g_originalForensicIntersectSegment == nullptr) {
        return false;
    }

    const int callsite =
        ForensicIntersectCallsiteIndex(_ReturnAddress());
    if (callsite < 0 ||
        InterlockedCompareExchange(
            &g_forensicCameraSocketRayEnabled, 0, 0) == 0 ||
        query == nullptr) {
        return g_originalForensicIntersectSegment(
            engineClient, query, resultBuffer);
    }

    std::int32_t weaponIndex = -1;
    unsigned int weaponType = 0U;
    ForensicDisplayKind expectedDisplayKind =
        ForensicDisplayKind::none;
    if (!ReadEquippedForensicCameraTool(
            weaponIndex, weaponType,
            expectedDisplayKind)) {
        return g_originalForensicIntersectSegment(
            engineClient, query, resultBuffer);
    }

    ForensicCameraSocketPoseSnapshot socketPose{};
    ULONGLONG socketPoseAgeMilliseconds = 0U;
    bool socketPosePublished = false;
    if (!ReadFreshForensicCameraSocketPose(
            expectedDisplayKind, socketPose,
            socketPoseAgeMilliseconds,
            socketPosePublished)) {
        const LONG fallback = InterlockedIncrement(
            &g_forensicCameraSocketRayFallbacks);
        if (g_log != nullptr &&
            (fallback <= 16 || (fallback % 240) == 0)) {
            char detail[512]{};
            std::snprintf(
                detail, sizeof(detail),
                "fallback=%ld reason=%s pose_published=%u "
                "pose_age_ms=%llu freshness_limit_ms=%llu "
                "weapon_index=%d weapon_type=0x%02X "
                "expected_display_kind=%s "
                "fallback_path=untouched_retail_query "
                "engine_state_writes=0",
                fallback,
                socketPosePublished ? "stale_pose" :
                                      "missing_pose",
                socketPosePublished ? 1U : 0U,
                static_cast<unsigned long long>(
                    socketPoseAgeMilliseconds),
                static_cast<unsigned long long>(
                    kForensicCameraSocketFreshnessMilliseconds),
                weaponIndex, weaponType,
                ForensicDisplayKindName(
                    expectedDisplayKind));
            g_log(
                "m5_forensic_camera_socket_ray_fallback",
                detail);
        }
        return g_originalForensicIntersectSegment(
            engineClient, query, resultBuffer);
    }

    ForensicIntersectQueryPrefixAbi originalQuery{};
    ForensicControllerRay socketRay{};
    bool overridden = false;
    __try {
        std::memcpy(
            &originalQuery, query,
            sizeof(originalQuery));
        socketRay = BuildForensicControllerRay(
            {originalQuery.start.x, originalQuery.start.y,
             originalQuery.start.z},
            {originalQuery.end.x, originalQuery.end.y,
             originalQuery.end.z},
            socketPose.origin, socketPose.forward);
        if (socketRay.valid) {
            auto* const liveQuery =
                static_cast<ForensicIntersectQueryPrefixAbi*>(
                    query);
            liveQuery->start = {
                socketRay.start.x,
                socketRay.start.y,
                socketRay.start.z};
            liveQuery->end = {
                socketRay.end.x,
                socketRay.end.y,
                socketRay.end.z};
            overridden = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        overridden = false;
    }
    if (!overridden) {
        return g_originalForensicIntersectSegment(
            engineClient, query, resultBuffer);
    }

    const LONG call = InterlockedIncrement(
        &g_forensicCameraSocketRayCalls);
    const LONG overrideCount = InterlockedIncrement(
        &g_forensicCameraSocketRayOverrides);
    const bool hit = g_originalForensicIntersectSegment(
        engineClient, query, resultBuffer);

    bool queryRestored = false;
    __try {
        std::memcpy(
            query, &originalQuery,
            sizeof(originalQuery));
        queryRestored = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        queryRestored = false;
    }

    const LONG hitValue = hit ? 1 : 0;
    const LONG previousHit = InterlockedExchange(
        &g_forensicCameraSocketRayLastResult[callsite],
        hitValue);
    const bool logSample =
        call <= 16 || previousHit != hitValue ||
        (overrideCount % 240) == 0 || !queryRestored;
    if (logSample && g_log != nullptr) {
        char detail[1280]{};
        std::snprintf(
            detail, sizeof(detail),
            "call=%ld override=%ld callsite=%d "
            "return_rva=0x%08X weapon_index=%d "
            "weapon_type=0x%02X pose_display_kind=%s "
            "socket_sequence=%llu socket_age_ms=%llu "
            "retail_start=(%.3f,%.3f,%.3f) "
            "retail_end=(%.3f,%.3f,%.3f) "
            "socket_start=(%.3f,%.3f,%.3f) "
            "socket_forward=(%.5f,%.5f,%.5f) "
            "socket_end=(%.3f,%.3f,%.3f) "
            "range_units=%.3f intersected=%u "
            "query_restored=%u retail_filter_preserved=1 "
            "engine_state_writes=0",
            call, overrideCount, callsite,
            static_cast<unsigned int>(
                callsite == 0
                    ? kForensicIntersectFirstReturnRva
                    : kForensicIntersectSecondReturnRva),
            weaponIndex, weaponType,
            ForensicDisplayKindName(
                socketPose.displayKind),
            static_cast<unsigned long long>(
                socketPose.sequence),
            static_cast<unsigned long long>(
                socketPoseAgeMilliseconds),
            originalQuery.start.x, originalQuery.start.y,
            originalQuery.start.z,
            originalQuery.end.x, originalQuery.end.y,
            originalQuery.end.z,
            socketRay.start.x, socketRay.start.y,
            socketRay.start.z,
            socketPose.forward.x, socketPose.forward.y,
            socketPose.forward.z,
            socketRay.end.x, socketRay.end.y,
            socketRay.end.z,
            socketRay.rangeUnits, hit ? 1U : 0U,
            queryRestored ? 1U : 0U);
        g_log(
            "m5_forensic_camera_socket_ray_query",
            detail);
    }
    return hit;
}

bool ForensicControllerCommandActive(
    std::uint32_t command,
    int gameState) noexcept {
    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input) &&
        gameState == kCondemnedGameStatePlaying;
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    const bool allowed = usable && !calibrationCaptured;
    if (command == kCondemnedActivateCommand) {
        return ResolveActivateValue(input, allowed).active;
    }
    return ResolveCoreActionValue(
        input, allowed, command).active;
}

void TraceForensicCommandDispatch(
    ClientShellCommandFunction original,
    void* clientShell,
    std::uint32_t command,
    const char* edge) {
    if (original == nullptr) {
        return;
    }
    if (CondemnedForensicTraceCommandIndex(command) < 0 ||
        InterlockedCompareExchange(
            &g_forensicMemoryProbeEnabled, 0, 0) == 0) {
        original(clientShell, static_cast<int>(command));
        return;
    }
    const int gameState = ReadRetailGameState(g_interfaceManager);
    const bool controllerActive =
        ForensicControllerCommandActive(command, gameState);
    const ForensicLiveDisplayState scannerBefore =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    const ForensicLiveDisplayState cameraBefore =
        CaptureForensicLiveDisplayState(
            &g_forensicDigitalCameraDisplay,
            kDigitalCameraDisplayStateOffset,
            kDigitalCameraDisplayStateSize);
    original(clientShell, static_cast<int>(command));
    const ForensicLiveDisplayState scannerAfter =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    const ForensicLiveDisplayState cameraAfter =
        CaptureForensicLiveDisplayState(
            &g_forensicDigitalCameraDisplay,
            kDigitalCameraDisplayStateOffset,
            kDigitalCameraDisplayStateSize);
    if (g_log == nullptr) {
        return;
    }
    char detail[1280]{};
    std::snprintf(
        detail, sizeof(detail),
        "command=%u command_name=%s edge=%s "
        "controller_active=%u game_state=%d "
        "scanner_object=0x%08lX scanner_vtable=0x%08lX "
        "scanner_readable=%u scanner_state=0x%012llX>0x%012llX "
        "scanner_target_hit=%u>%u scanner_framing_ok=%u>%u "
        "scanner_can_photo=%u>%u "
        "camera_object=0x%08lX camera_vtable=0x%08lX "
        "camera_readable=%u camera_state=0x%08llX>0x%08llX "
        "source=ClientShell_OnCommand engine_writes=0",
        command, CondemnedForensicTraceCommandName(command),
        edge == nullptr ? "unknown" : edge,
        controllerActive ? 1U : 0U, gameState,
        static_cast<unsigned long>(scannerAfter.object),
        static_cast<unsigned long>(scannerAfter.vtable),
        scannerBefore.stateReadable && scannerAfter.stateReadable
            ? 1U : 0U,
        static_cast<unsigned long long>(scannerBefore.state),
        static_cast<unsigned long long>(scannerAfter.state),
        static_cast<unsigned int>(
            (scannerBefore.state >> 24U) & 0xFFU),
        static_cast<unsigned int>(
            (scannerAfter.state >> 24U) & 0xFFU),
        static_cast<unsigned int>(
            (scannerBefore.state >> 32U) & 0xFFU),
        static_cast<unsigned int>(
            (scannerAfter.state >> 32U) & 0xFFU),
        static_cast<unsigned int>(
            (scannerBefore.state >> 40U) & 0xFFU),
        static_cast<unsigned int>(
            (scannerAfter.state >> 40U) & 0xFFU),
        static_cast<unsigned long>(cameraAfter.object),
        static_cast<unsigned long>(cameraAfter.vtable),
        cameraBefore.stateReadable && cameraAfter.stateReadable
            ? 1U : 0U,
        static_cast<unsigned long long>(cameraBefore.state),
        static_cast<unsigned long long>(cameraAfter.state));
    g_log("m5_forensic_command_dispatch", detail);
}

void __fastcall HookForensicCommandOn(
    void* clientShell,
    void* ignoredEdx,
    int command) {
    (void)ignoredEdx;
    TraceForensicCommandDispatch(
        g_originalForensicCommandOn, clientShell,
        static_cast<std::uint32_t>(command), "down");
}

void __fastcall HookForensicCommandOff(
    void* clientShell,
    void* ignoredEdx,
    int command) {
    (void)ignoredEdx;
    TraceForensicCommandDispatch(
        g_originalForensicCommandOff, clientShell,
        static_cast<std::uint32_t>(command), "up");
}

struct ForensicTargetCacheSnapshot {
    std::uintptr_t queryObject{0U};
    std::uintptr_t queryVtable{0U};
    std::uint32_t queryVtableRva{0U};
    unsigned int kind{0U};
    std::uintptr_t target{0U};
    bool readable{false};
};

ForensicTargetCacheSnapshot CaptureForensicTargetCache(
    void* playerManager) noexcept {
    ForensicTargetCacheSnapshot snapshot{};
    if (playerManager == nullptr) {
        return snapshot;
    }
    __try {
        const auto* const playerBytes =
            static_cast<const unsigned char*>(
                playerManager);
        std::memcpy(
            &snapshot.queryObject,
            playerBytes + kPlayerManagerTargetQueryOffset,
            sizeof(snapshot.queryObject));
        if (snapshot.queryObject == 0U) {
            return snapshot;
        }
        const auto* const queryBytes =
            reinterpret_cast<const unsigned char*>(
                snapshot.queryObject);
        std::memcpy(
            &snapshot.queryVtable, queryBytes,
            sizeof(snapshot.queryVtable));
        const auto* const cache =
            queryBytes + kForensicTargetCacheOffset;
        snapshot.kind =
            cache[kForensicTargetCacheKindOffset];
        std::memcpy(
            &snapshot.target,
            cache + kForensicTargetCacheReferenceOffset,
            sizeof(snapshot.target));
        if (g_gameClientBase != nullptr &&
            snapshot.queryVtable >=
                reinterpret_cast<std::uintptr_t>(
                    g_gameClientBase) &&
            snapshot.queryVtable <
                reinterpret_cast<std::uintptr_t>(
                    g_gameClientBase) +
                    kRetailGameImageSize) {
            snapshot.queryVtableRva =
                static_cast<std::uint32_t>(
                    snapshot.queryVtable -
                    reinterpret_cast<std::uintptr_t>(
                        g_gameClientBase));
        }
        snapshot.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snapshot = {};
    }
    return snapshot;
}

bool __fastcall HookForensicPlayerManagerCommandOn(
    void* playerManager,
    void* ignoredEdx,
    int command) {
    (void)ignoredEdx;
    if (g_originalForensicPlayerManagerCommandOn == nullptr) {
        return false;
    }
    if (command != static_cast<int>(kCondemnedFireCommand) ||
        InterlockedCompareExchange(
            &g_forensicMemoryProbeEnabled, 0, 0) == 0) {
        return g_originalForensicPlayerManagerCommandOn(
            playerManager, command);
    }

    const ForensicMemorySnapshot before =
        CaptureForensicMemorySnapshot();
    const ForensicLiveDisplayState scannerBefore =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    const ForensicTargetCacheSnapshot targetCacheBefore =
        CaptureForensicTargetCache(playerManager);
    std::uint32_t playerMode = 0U;
    std::uintptr_t weaponRecord = 0U;
    std::uint32_t weaponState218 = 0U;
    std::uintptr_t weaponManagerOwner = 0U;
    unsigned int weaponType = 0U;
    unsigned int fireReady = 0U;
    bool branchFieldsReadable = false;
    __try {
        if (playerManager != nullptr &&
            before.currentWeapon != 0U &&
            before.weaponManager != 0U) {
            const auto* const playerBytes =
                static_cast<const unsigned char*>(
                    playerManager);
            const auto* const weaponBytes =
                reinterpret_cast<const unsigned char*>(
                    before.currentWeapon);
            const auto* const managerBytes =
                reinterpret_cast<const unsigned char*>(
                    before.weaponManager);
            std::memcpy(
                &playerMode, playerBytes + 0x30U,
                sizeof(playerMode));
            std::memcpy(
                &weaponRecord, weaponBytes + 0x1D4U,
                sizeof(weaponRecord));
            std::memcpy(
                &weaponState218, weaponBytes + 0x218U,
                sizeof(weaponState218));
            std::memcpy(
                &weaponManagerOwner, managerBytes + 0x1CU,
                sizeof(weaponManagerOwner));
            weaponType = weaponBytes[0x34DU];
            fireReady =
                weaponBytes[0x303U] != 0U ? 1U : 0U;
            branchFieldsReadable = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        branchFieldsReadable = false;
    }
    if (g_log != nullptr) {
        const bool weaponVslotB0Predicted =
            weaponState218 == 8U || weaponState218 == 9U ||
            (weaponManagerOwner != 0U &&
             weaponManagerOwner != weaponRecord);
        char branchDetail[896]{};
        std::snprintf(
            branchDetail, sizeof(branchDetail),
            "command=17 fields_readable=%u player_mode=%u "
            "weapon_index=%d weapon=0x%08lX weapon_type=0x%02X "
            "weapon_record=0x%08lX weapon_state_218=%u "
            "fire_ready=%u weapon_manager_owner=0x%08lX "
            "weapon_vslot_b0_predicted=%u target_cache_readable=%u "
            "target_query=0x%08lX target_query_vtable_rva=0x%08X "
            "target_cache_kind=%u target_cache_target=0x%08lX "
            "engine_writes=0",
            branchFieldsReadable ? 1U : 0U, playerMode,
            before.weaponIndex,
            static_cast<unsigned long>(before.currentWeapon),
            weaponType,
            static_cast<unsigned long>(weaponRecord),
            weaponState218, fireReady,
            static_cast<unsigned long>(weaponManagerOwner),
            weaponVslotB0Predicted ? 1U : 0U,
            targetCacheBefore.readable ? 1U : 0U,
            static_cast<unsigned long>(
                targetCacheBefore.queryObject),
            targetCacheBefore.queryVtableRva,
            targetCacheBefore.kind,
            static_cast<unsigned long>(targetCacheBefore.target));
        g_log("m5_forensic_player_fire_branch_input",
              branchDetail);
    }
    const bool handled =
        g_originalForensicPlayerManagerCommandOn(
            playerManager, command);
    const ForensicMemorySnapshot after =
        CaptureForensicMemorySnapshot();
    const ForensicLiveDisplayState scannerAfter =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    const ForensicTargetCacheSnapshot targetCacheAfter =
        CaptureForensicTargetCache(playerManager);
    if (g_log != nullptr) {
        char detail[1024]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=17 player_manager=0x%08lX handled=%u "
            "weapon_index=%d>%d weapon=0x%08lX>0x%08lX "
            "scanner_target_hit=%u>%u "
            "scanner_framing_ok=%u>%u "
            "scanner_can_photo=%u>%u "
            "target_cache_readable=%u>%u target_cache_kind=%u>%u "
            "target_cache_target=0x%08lX>0x%08lX "
            "source=PlayerMgr_OnCommandOn engine_writes=0",
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(
                    playerManager)),
            handled ? 1U : 0U,
            before.weaponIndex, after.weaponIndex,
            static_cast<unsigned long>(before.currentWeapon),
            static_cast<unsigned long>(after.currentWeapon),
            static_cast<unsigned int>(
                (scannerBefore.state >> 24U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 24U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerBefore.state >> 32U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 32U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerBefore.state >> 40U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 40U) & 0xFFU),
            targetCacheBefore.readable ? 1U : 0U,
            targetCacheAfter.readable ? 1U : 0U,
            targetCacheBefore.kind, targetCacheAfter.kind,
            static_cast<unsigned long>(
                targetCacheBefore.target),
            static_cast<unsigned long>(targetCacheAfter.target));
        g_log("m5_forensic_player_command_dispatch", detail);
    }
    return handled;
}

void __fastcall HookForensicClientWeaponFire(
    void* weapon,
    void* ignoredEdx) {
    (void)ignoredEdx;
    if (g_originalForensicClientWeaponFire == nullptr) {
        return;
    }
    if (InterlockedCompareExchange(
            &g_forensicMemoryProbeEnabled, 0, 0) == 0) {
        g_originalForensicClientWeaponFire(weapon);
        return;
    }

    const ForensicMemorySnapshot before =
        CaptureForensicMemorySnapshot();
    const ForensicLiveDisplayState scannerBefore =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    unsigned int weaponType = 0U;
    unsigned int fireReady = 0U;
    bool fieldsReadable = false;
    __try {
        if (weapon != nullptr) {
            const auto* const bytes =
                static_cast<const unsigned char*>(weapon);
            weaponType = bytes[0x34DU];
            fireReady = bytes[0x303U] != 0U ? 1U : 0U;
            fieldsReadable = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fieldsReadable = false;
    }
    g_originalForensicClientWeaponFire(weapon);
    const ForensicMemorySnapshot after =
        CaptureForensicMemorySnapshot();
    const ForensicLiveDisplayState scannerAfter =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    if (g_log != nullptr) {
        char detail[896]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon=0x%08lX current_weapon=0x%08lX>0x%08lX "
            "weapon_index=%d>%d weapon_matches_current=%u "
            "weapon_fields_readable=%u weapon_type=0x%02X "
            "fire_ready=%u scanner_target_hit=%u>%u "
            "scanner_framing_ok=%u>%u scanner_can_photo=%u>%u "
            "source=CClientWeapon_Fire engine_writes=0",
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(weapon)),
            static_cast<unsigned long>(before.currentWeapon),
            static_cast<unsigned long>(after.currentWeapon),
            before.weaponIndex, after.weaponIndex,
            reinterpret_cast<std::uintptr_t>(weapon) ==
                    before.currentWeapon
                ? 1U : 0U,
            fieldsReadable ? 1U : 0U,
            weaponType, fireReady,
            static_cast<unsigned int>(
                (scannerBefore.state >> 24U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 24U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerBefore.state >> 32U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 32U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerBefore.state >> 40U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 40U) & 0xFFU));
        g_log("m5_forensic_weapon_fire_dispatch", detail);
    }
}

bool __fastcall HookForensicCollectionAction(
    void* collectionManager,
    void* ignoredEdx,
    void* target) {
    (void)ignoredEdx;
    if (g_originalForensicCollectionAction == nullptr) {
        return false;
    }
    if (InterlockedCompareExchange(
            &g_forensicMemoryProbeEnabled, 0, 0) == 0) {
        return g_originalForensicCollectionAction(
            collectionManager, target);
    }

    const ForensicLiveDisplayState scannerBefore =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    std::uintptr_t selectedBefore = 0U;
    std::uintptr_t selectedAfter = 0U;
    std::uintptr_t targetVtable = 0U;
    std::uintptr_t targetField38 = 0U;
    unsigned int activeBefore = 0U;
    unsigned int activeAfter = 0U;
    unsigned int disabledBefore = 0U;
    bool managerReadable = false;
    bool targetReadable = false;
    __try {
        if (collectionManager != nullptr) {
            const auto* const managerBytes =
                static_cast<const unsigned char*>(
                    collectionManager);
            std::memcpy(
                &selectedBefore, managerBytes,
                sizeof(selectedBefore));
            activeBefore =
                managerBytes[0x04U] != 0U ? 1U : 0U;
            disabledBefore =
                managerBytes[0xB0U] != 0U ? 1U : 0U;
            managerReadable = true;
        }
        if (target != nullptr) {
            const auto* const targetBytes =
                static_cast<const unsigned char*>(target);
            std::memcpy(
                &targetVtable, targetBytes,
                sizeof(targetVtable));
            std::memcpy(
                &targetField38, targetBytes + 0x38U,
                sizeof(targetField38));
            targetReadable = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        managerReadable = false;
        targetReadable = false;
    }

    const bool handled =
        g_originalForensicCollectionAction(
            collectionManager, target);
    __try {
        if (collectionManager != nullptr) {
            const auto* const managerBytes =
                static_cast<const unsigned char*>(
                    collectionManager);
            std::memcpy(
                &selectedAfter, managerBytes,
                sizeof(selectedAfter));
            activeAfter =
                managerBytes[0x04U] != 0U ? 1U : 0U;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        managerReadable = false;
    }
    const ForensicLiveDisplayState scannerAfter =
        CaptureForensicLiveDisplayState(
            &g_forensicScannerDisplay,
            kScannerDisplayStateOffset,
            kScannerDisplayStateSize);
    std::uint32_t targetVtableRva = 0U;
    if (g_gameClientBase != nullptr &&
        targetVtable >= reinterpret_cast<std::uintptr_t>(
            g_gameClientBase) &&
        targetVtable <
            reinterpret_cast<std::uintptr_t>(g_gameClientBase) +
                kRetailGameImageSize) {
        targetVtableRva = static_cast<std::uint32_t>(
            targetVtable -
            reinterpret_cast<std::uintptr_t>(g_gameClientBase));
    }
    if (g_log != nullptr) {
        char detail[1024]{};
        std::snprintf(
            detail, sizeof(detail),
            "manager=0x%08lX target=0x%08lX handled=%u "
            "manager_readable=%u selected=0x%08lX>0x%08lX "
            "active=%u>%u disabled=%u target_readable=%u "
            "target_vtable=0x%08lX target_vtable_rva=0x%08X "
            "target_field_38=0x%08lX "
            "scanner_target_hit=%u>%u "
            "scanner_framing_ok=%u>%u "
            "scanner_can_photo=%u>%u "
            "source=forensic_collection_action engine_writes=0",
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(
                    collectionManager)),
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(target)),
            handled ? 1U : 0U,
            managerReadable ? 1U : 0U,
            static_cast<unsigned long>(selectedBefore),
            static_cast<unsigned long>(selectedAfter),
            activeBefore, activeAfter, disabledBefore,
            targetReadable ? 1U : 0U,
            static_cast<unsigned long>(targetVtable),
            targetVtableRva,
            static_cast<unsigned long>(targetField38),
            static_cast<unsigned int>(
                (scannerBefore.state >> 24U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 24U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerBefore.state >> 32U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 32U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerBefore.state >> 40U) & 0xFFU),
            static_cast<unsigned int>(
                (scannerAfter.state >> 40U) & 0xFFU));
        g_log("m5_forensic_collection_action_dispatch",
              detail);
    }
    return handled;
}

ForensicDisplayKind ResolveForensicDisplayKind(
    std::uintptr_t vtable) noexcept {
    if (g_gameClientBase == nullptr || vtable == 0U) {
        return ForensicDisplayKind::none;
    }
    const auto base =
        reinterpret_cast<std::uintptr_t>(g_gameClientBase);
    if (vtable == base + kScannerDisplayVtableRva) {
        return ForensicDisplayKind::scanner;
    }
    if (vtable == base + kDigitalCameraDisplayVtableRva) {
        return ForensicDisplayKind::digitalCamera;
    }
    return ForensicDisplayKind::other;
}

std::size_t ForensicDisplayTrackedSize(
    ForensicDisplayKind kind) noexcept {
    switch (kind) {
    case ForensicDisplayKind::scanner:
        return kScannerDisplaySize -
            kForensicDisplayDerivedOffset;
    case ForensicDisplayKind::digitalCamera:
        return kDigitalCameraDisplaySize -
            kForensicDisplayDerivedOffset;
    default:
        return 0U;
    }
}

ForensicMemorySnapshot CaptureForensicMemorySnapshot() noexcept {
    ForensicMemorySnapshot snapshot{};
    snapshot.gameState = ReadRetailGameState(g_interfaceManager);
    if (g_gameClientBase == nullptr) {
        std::snprintf(
            snapshot.weaponName, sizeof(snapshot.weaponName),
            "UNAVAILABLE");
        return snapshot;
    }

    __try {
        std::memcpy(
            &snapshot.weaponManager,
            g_gameClientBase + kWeaponManagerGlobalRva,
            sizeof(snapshot.weaponManager));
        if (snapshot.weaponManager != 0U) {
            const auto* const manager =
                reinterpret_cast<const unsigned char*>(
                    snapshot.weaponManager);
            std::memcpy(
                &snapshot.weaponIndex,
                manager + kCurrentWeaponIndexOffset,
                sizeof(snapshot.weaponIndex));
            std::memcpy(
                &snapshot.currentWeapon,
                manager + kCurrentWeaponOffset,
                sizeof(snapshot.currentWeapon));
            snapshot.rootsReadable = true;
        }
        if (snapshot.currentWeapon != 0U) {
            const auto* const weapon =
                reinterpret_cast<const unsigned char*>(
                    snapshot.currentWeapon);
            std::memcpy(
                &snapshot.modelObject,
                weapon + kRightWeaponModelObjectOffset,
                sizeof(snapshot.modelObject));
            std::memcpy(
                &snapshot.weaponDisplay,
                weapon + kWeaponDisplayObjectOffset,
                sizeof(snapshot.weaponDisplay));
        }
        if (snapshot.weaponDisplay != 0U) {
            std::memcpy(
                &snapshot.displayVtable,
                reinterpret_cast<const void*>(
                    snapshot.weaponDisplay),
                sizeof(snapshot.displayVtable));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snapshot = {};
        snapshot.gameState =
            ReadRetailGameState(g_interfaceManager);
    }

    if (snapshot.currentWeapon != 0U) {
        snapshot.weaponBytesReadable = ReadForensicMemoryRange(
            reinterpret_cast<const unsigned char*>(
                snapshot.currentWeapon) +
                kForensicWeaponTraceOffset,
            snapshot.weaponBytes.size(),
            snapshot.weaponBytes.data());
    }

    snapshot.displayKind = ResolveForensicDisplayKind(
        snapshot.displayVtable);
    if (snapshot.displayVtable != 0U) {
        const auto base =
            reinterpret_cast<std::uintptr_t>(g_gameClientBase);
        if (snapshot.displayVtable >= base &&
            snapshot.displayVtable < base + kRetailGameImageSize) {
            snapshot.displayVtableRva =
                static_cast<std::uint32_t>(
                    snapshot.displayVtable - base);
        }
    }
    snapshot.displayBytesCaptured =
        ForensicDisplayTrackedSize(snapshot.displayKind);
    if (snapshot.weaponDisplay != 0U &&
        snapshot.displayBytesCaptured != 0U) {
        snapshot.displayBytesReadable = ReadForensicMemoryRange(
            reinterpret_cast<const unsigned char*>(
                snapshot.weaponDisplay) +
                kForensicDisplayDerivedOffset,
            snapshot.displayBytesCaptured,
            snapshot.displayBytes.data());
    }

    if (snapshot.weaponIndex < 0) {
        std::snprintf(
            snapshot.weaponName, sizeof(snapshot.weaponName),
            "NO_WEAPON");
        return snapshot;
    }
    AcquireSRWLockShared(&g_physicalMeleeLock);
    if (snapshot.weaponIndex ==
            g_physicalMeleeProfileWeaponIndex &&
        g_equippedWeaponIdentity.nameResolved) {
        strncpy_s(
            snapshot.weaponName, sizeof(snapshot.weaponName),
            g_equippedWeaponIdentity.recordName, _TRUNCATE);
    }
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    if (snapshot.weaponName[0] == '\0') {
        std::snprintf(
            snapshot.weaponName, sizeof(snapshot.weaponName),
            "INDEX_%ld",
            static_cast<long>(snapshot.weaponIndex));
    }
    return snapshot;
}

void LogForensicMemorySnapshot(
    const char* event,
    const char* phase,
    std::uint64_t traceId,
    std::uint32_t command,
    std::uint32_t frame,
    const ForensicMemorySnapshot& snapshot) noexcept {
    if (g_log == nullptr) {
        return;
    }
    const std::uint32_t weaponHash =
        snapshot.weaponBytesReadable
        ? ForensicMemoryFnv1a(
            snapshot.weaponBytes.data(),
            snapshot.weaponBytes.size())
        : 0U;
    const std::uint32_t displayHash =
        snapshot.displayBytesReadable
        ? ForensicMemoryFnv1a(
            snapshot.displayBytes.data(),
            snapshot.displayBytesCaptured)
        : 0U;
    char detail[1024]{};
    std::snprintf(
        detail, sizeof(detail),
        "trace_id=%llu command=%u command_name=%s phase=%s "
        "frame=%u game_state=%ld weapon_index=%ld weapon_name=%s "
        "weapon_manager=0x%08lX weapon=0x%08lX model=0x%08lX "
        "display=0x%08lX display_vtable=0x%08lX "
        "display_vtable_rva=0x%08X "
        "display_kind=%s roots_readable=%u "
        "weapon_span=0x%03X+0x%03X weapon_readable=%u "
        "weapon_hash=0x%08X "
        "display_span=0x%03X+0x%03X display_readable=%u "
        "display_hash=0x%08X engine_writes=0",
        static_cast<unsigned long long>(traceId),
        command, CondemnedForensicTraceCommandName(command),
        phase == nullptr ? "unknown" : phase,
        frame, static_cast<long>(snapshot.gameState),
        static_cast<long>(snapshot.weaponIndex),
        snapshot.weaponName,
        static_cast<unsigned long>(snapshot.weaponManager),
        static_cast<unsigned long>(snapshot.currentWeapon),
        static_cast<unsigned long>(snapshot.modelObject),
        static_cast<unsigned long>(snapshot.weaponDisplay),
        static_cast<unsigned long>(snapshot.displayVtable),
        snapshot.displayVtableRva,
        ForensicDisplayKindName(snapshot.displayKind),
        snapshot.rootsReadable ? 1U : 0U,
        static_cast<unsigned int>(kForensicWeaponTraceOffset),
        static_cast<unsigned int>(snapshot.weaponBytes.size()),
        snapshot.weaponBytesReadable ? 1U : 0U,
        weaponHash,
        static_cast<unsigned int>(
            kForensicDisplayDerivedOffset),
        static_cast<unsigned int>(
            snapshot.displayBytesCaptured),
        snapshot.displayBytesReadable ? 1U : 0U,
        displayHash);
    g_log(event, detail);
}

void LogForensicMemoryWordDiffs(
    std::uint64_t traceId,
    std::uint32_t command,
    const char* phase,
    std::uint32_t frame,
    const char* root,
    std::uintptr_t pointer,
    std::size_t objectOffset,
    const unsigned char* before,
    const unsigned char* after,
    std::size_t size) noexcept {
    if (g_log == nullptr || root == nullptr ||
        before == nullptr || after == nullptr ||
        size == 0U || (size % sizeof(std::uint32_t)) != 0U) {
        return;
    }
    std::size_t totalChanges = 0U;
    for (std::size_t offset = 0U;
         offset < size; offset += sizeof(std::uint32_t)) {
        std::uint32_t beforeValue = 0U;
        std::uint32_t afterValue = 0U;
        std::memcpy(&beforeValue, before + offset, sizeof(beforeValue));
        std::memcpy(&afterValue, after + offset, sizeof(afterValue));
        if (beforeValue != afterValue) {
            ++totalChanges;
        }
    }
    if (totalChanges == 0U) {
        return;
    }

    constexpr std::size_t kChangesPerRecord = 12U;
    const std::size_t chunkCount =
        (totalChanges + kChangesPerRecord - 1U) /
        kChangesPerRecord;
    std::size_t changesSkipped = 0U;
    for (std::size_t chunk = 0U;
         chunk < chunkCount; ++chunk) {
        char words[768]{};
        std::size_t used = 0U;
        std::size_t skipped = 0U;
        std::size_t emitted = 0U;
        for (std::size_t offset = 0U;
             offset < size; offset += sizeof(std::uint32_t)) {
            std::uint32_t beforeValue = 0U;
            std::uint32_t afterValue = 0U;
            std::memcpy(
                &beforeValue, before + offset,
                sizeof(beforeValue));
            std::memcpy(
                &afterValue, after + offset,
                sizeof(afterValue));
            if (beforeValue == afterValue) {
                continue;
            }
            if (skipped < changesSkipped) {
                ++skipped;
                continue;
            }
            if (emitted >= kChangesPerRecord) {
                break;
            }
            const int written = std::snprintf(
                words + used, sizeof(words) - used,
                "%s0x%03lX:%08lX>%08lX",
                emitted == 0U ? "" : ",",
                static_cast<unsigned long>(
                    objectOffset + offset),
                static_cast<unsigned long>(beforeValue),
                static_cast<unsigned long>(afterValue));
            if (written <= 0 ||
                static_cast<std::size_t>(written) >=
                    sizeof(words) - used) {
                break;
            }
            used += static_cast<std::size_t>(written);
            ++emitted;
        }
        if (emitted == 0U) {
            break;
        }
        changesSkipped += emitted;
        char detail[1280]{};
        std::snprintf(
            detail, sizeof(detail),
            "trace_id=%llu command=%u command_name=%s phase=%s "
            "frame=%u root=%s pointer=0x%08lX "
            "changes_total=%u chunk=%u/%u words=%s "
            "format=object_offset:before_u32>after_u32 "
            "engine_writes=0",
            static_cast<unsigned long long>(traceId),
            command,
            CondemnedForensicTraceCommandName(command),
            phase == nullptr ? "unknown" : phase,
            frame, root, static_cast<unsigned long>(pointer),
            static_cast<unsigned int>(totalChanges),
            static_cast<unsigned int>(chunk + 1U),
            static_cast<unsigned int>(chunkCount), words);
        g_log("m5_forensic_memory_diff", detail);
    }
}

void LogForensicMemoryComparison(
    std::uint64_t traceId,
    std::uint32_t command,
    const char* phase,
    std::uint32_t frame,
    const ForensicMemorySnapshot& before,
    const ForensicMemorySnapshot& after) noexcept {
    const bool rootsChanged =
        before.weaponIndex != after.weaponIndex ||
        before.currentWeapon != after.currentWeapon ||
        before.modelObject != after.modelObject ||
        before.weaponDisplay != after.weaponDisplay ||
        before.displayVtable != after.displayVtable;
    if (rootsChanged && g_log != nullptr) {
        char detail[768]{};
        std::snprintf(
            detail, sizeof(detail),
            "trace_id=%llu command=%u command_name=%s phase=%s "
            "frame=%u weapon_index=%ld>%ld "
            "weapon=0x%08lX>0x%08lX model=0x%08lX>0x%08lX "
            "display=0x%08lX>0x%08lX "
            "display_vtable=0x%08lX>0x%08lX "
            "display_vtable_rva=0x%08X>0x%08X "
            "display_kind=%s>%s engine_writes=0",
            static_cast<unsigned long long>(traceId),
            command,
            CondemnedForensicTraceCommandName(command),
            phase == nullptr ? "unknown" : phase,
            frame,
            static_cast<long>(before.weaponIndex),
            static_cast<long>(after.weaponIndex),
            static_cast<unsigned long>(before.currentWeapon),
            static_cast<unsigned long>(after.currentWeapon),
            static_cast<unsigned long>(before.modelObject),
            static_cast<unsigned long>(after.modelObject),
            static_cast<unsigned long>(before.weaponDisplay),
            static_cast<unsigned long>(after.weaponDisplay),
            static_cast<unsigned long>(before.displayVtable),
            static_cast<unsigned long>(after.displayVtable),
            before.displayVtableRva,
            after.displayVtableRva,
            ForensicDisplayKindName(before.displayKind),
            ForensicDisplayKindName(after.displayKind));
        g_log("m5_forensic_memory_root_transition", detail);
    }

    if (before.currentWeapon != 0U &&
        before.currentWeapon == after.currentWeapon &&
        before.weaponBytesReadable &&
        after.weaponBytesReadable) {
        LogForensicMemoryWordDiffs(
            traceId, command, phase, frame, "weapon",
            after.currentWeapon, kForensicWeaponTraceOffset,
            before.weaponBytes.data(), after.weaponBytes.data(),
            before.weaponBytes.size());
    }
    if (before.weaponDisplay != 0U &&
        before.weaponDisplay == after.weaponDisplay &&
        before.displayVtable == after.displayVtable &&
        before.displayBytesReadable &&
        after.displayBytesReadable &&
        before.displayBytesCaptured ==
            after.displayBytesCaptured) {
        LogForensicMemoryWordDiffs(
            traceId, command, phase, frame, "display",
            after.weaponDisplay, kForensicDisplayDerivedOffset,
            before.displayBytes.data(), after.displayBytes.data(),
            before.displayBytesCaptured);
    }
}

void ObserveForensicMemoryCommandTransition(
    const RetailBinding& binding,
    std::uint32_t command,
    LONG controllerActive,
    bool controllerApplied,
    float retailValue,
    float outputValue,
    int retailGameState) noexcept {
    if (InterlockedCompareExchange(
            &g_forensicMemoryProbeEnabled, 0, 0) == 0) {
        return;
    }
    const int commandIndex =
        CondemnedForensicTraceCommandIndex(command);
    if (commandIndex < 0) {
        return;
    }
    const LONG outputActive =
        ForensicTraceCommandValueActive(outputValue) ? 1 : 0;
    if (InterlockedExchange(
            &g_forensicMemoryCommandActive[commandIndex],
            outputActive) == outputActive) {
        return;
    }

    const ForensicMemorySnapshot current =
        CaptureForensicMemorySnapshot();
    std::uint64_t traceId = 0U;
    ForensicMemorySnapshot prior{};
    bool comparePrior = false;
    if (outputActive != 0) {
        traceId = static_cast<std::uint64_t>(
            InterlockedIncrement64(
                &g_forensicMemoryNextTraceId));
        AcquireSRWLockExclusive(&g_forensicMemoryLock);
        if (g_forensicMemoryTraceState.lastObservationValid) {
            prior =
                g_forensicMemoryTraceState.lastObservation;
            comparePrior = true;
        }
        g_forensicMemoryTraceState.traceId = traceId;
        g_forensicMemoryTraceState.command = command;
        g_forensicMemoryTraceState.frame = 0U;
        g_forensicMemoryTraceState.nextSampleIndex = 0U;
        g_forensicMemoryTraceState.baseline = current;
        g_forensicMemoryTraceState.active = true;
        ReleaseSRWLockExclusive(&g_forensicMemoryLock);
    } else {
        AcquireSRWLockShared(&g_forensicMemoryLock);
        traceId = g_forensicMemoryTraceState.traceId;
        ReleaseSRWLockShared(&g_forensicMemoryLock);
    }

    if (comparePrior) {
        LogForensicMemoryComparison(
            traceId, command, "between_edges", 0U,
            prior, current);
    }
    LogForensicMemorySnapshot(
        "m5_forensic_memory_edge",
        outputActive != 0 ? "pre_retail_down" : "release",
        traceId, command, 0U, current);
    if (g_log != nullptr) {
        char detail[768]{};
        std::snprintf(
            detail, sizeof(detail),
            "trace_id=%llu command=%u command_name=%s "
            "edge=%s controller_active=%ld "
            "controller_applied=%u retail_value=%.3f "
            "output_value=%.3f game_state=%d "
            "binding_device=%u binding_object=%u "
            "binding_default=%.3f binding_offset=%.3f "
            "binding_scale=%.3f command_range=(%.3f,%.3f) "
            "trigger=retail_binding_value engine_writes=0",
            static_cast<unsigned long long>(traceId),
            command,
            CondemnedForensicTraceCommandName(command),
            outputActive != 0 ? "down" : "up",
            controllerActive,
            controllerApplied ? 1U : 0U,
            retailValue, outputValue, retailGameState,
            binding.device, binding.object,
            binding.defaultValue, binding.offset, binding.scale,
            binding.commandMin, binding.commandMax);
        g_log("m5_forensic_memory_command_edge", detail);
    }
}

void SampleForensicMemoryAfterRetailUpdate() noexcept {
    if (InterlockedCompareExchange(
            &g_forensicMemoryProbeEnabled, 0, 0) == 0) {
        return;
    }

    std::uint64_t traceId = 0U;
    std::uint32_t command = 0U;
    std::uint32_t frame = 0U;
    bool sample = false;
    bool finalSample = false;
    AcquireSRWLockExclusive(&g_forensicMemoryLock);
    if (g_forensicMemoryTraceState.active) {
        traceId = g_forensicMemoryTraceState.traceId;
        command = g_forensicMemoryTraceState.command;
        frame = g_forensicMemoryTraceState.frame++;
        sample = ConsumeForensicMemoryTraceSampleFrame(
            frame,
            g_forensicMemoryTraceState.nextSampleIndex);
        finalSample = sample &&
            g_forensicMemoryTraceState.nextSampleIndex >=
                kForensicMemoryTraceSampleFrames.size();
    }
    ReleaseSRWLockExclusive(&g_forensicMemoryLock);
    if (!sample) {
        return;
    }

    const ForensicMemorySnapshot current =
        CaptureForensicMemorySnapshot();
    ForensicMemorySnapshot baseline{};
    bool stillCurrent = false;
    AcquireSRWLockExclusive(&g_forensicMemoryLock);
    if (g_forensicMemoryTraceState.active &&
        g_forensicMemoryTraceState.traceId == traceId) {
        baseline = g_forensicMemoryTraceState.baseline;
        g_forensicMemoryTraceState.lastObservation = current;
        g_forensicMemoryTraceState.lastObservationValid = true;
        if (finalSample) {
            g_forensicMemoryTraceState.active = false;
        }
        stillCurrent = true;
    }
    ReleaseSRWLockExclusive(&g_forensicMemoryLock);
    if (!stillCurrent) {
        return;
    }

    LogForensicMemorySnapshot(
        "m5_forensic_memory_sample", "post_retail",
        traceId, command, frame, current);
    LogForensicMemoryComparison(
        traceId, command, "post_retail", frame,
        baseline, current);
    if (finalSample && g_log != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "trace_id=%llu command=%u command_name=%s "
            "final_frame=%u samples=%u "
            "last_observation_retained=1 engine_writes=0",
            static_cast<unsigned long long>(traceId),
            command,
            CondemnedForensicTraceCommandName(command),
            frame,
            static_cast<unsigned int>(
                kForensicMemoryTraceSampleFrames.size()));
        g_log("m5_forensic_memory_trace_complete", detail);
    }
}

int PublishMenuRenderState() noexcept {
    const int state = ReadRetailGameState(g_interfaceManager);
    const LONG publishedState = IsKnownCondemnedGameState(state)
        ? static_cast<LONG>(state)
        : kUnknownRetailGameState;
    const LONG previous = InterlockedExchange(
        &g_lastPublishedRetailGameState, publishedState);
    NotifyArmIkRetailGameState(static_cast<int>(publishedState));
    if (previous == kCondemnedGameStatePlaying &&
        publishedState != kCondemnedGameStatePlaying) {
        PublishPhysicalMeleePlayerWeaponModel(nullptr);
        InvalidatePhysicalMeleeVisualProxySource();
    }
    if (previous == publishedState) {
        return state;
    }

    const bool flatPanel = CondemnedGameStateUsesFlatPanel(state);
    bool published = false;
    if (g_setMenuActive != nullptr) {
        __try {
            g_setMenuActive(flatPanel ? TRUE : FALSE);
            published = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            published = false;
        }
    }
    if (!published) {
        InterlockedExchange(
            &g_lastPublishedRetailGameState,
            kUnpublishedRetailGameState);
        if (InterlockedCompareExchange(
                &g_menuRenderPublishFailed, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m4_menu_render_state_failed",
                "CondemnedVr_SetMenuActive_callback_failed");
        }
        return state;
    }

    if (g_log != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "state=%d state_name=%s state_known=%u playing=%u "
            "flat_panel=%u source=CInterfaceMgr_plus_0x08",
            state, RetailGameStateName(state),
            IsKnownCondemnedGameState(state) ? 1U : 0U,
            state == kCondemnedGameStatePlaying ? 1U : 0U,
            flatPanel ? 1U : 0U);
        g_log("m4_menu_render_state", detail);
    }
    return state;
}

bool PollMenuToggle(
    void* clientShell,
    int retailGameState) noexcept {
    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input) &&
        CondemnedGameStateAllowsMenuToggle(retailGameState);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    if (!ConsumeMenuTogglePress(
            g_menuToggleLatch, input,
            usable && !calibrationCaptured)) {
        return false;
    }

    __try {
        g_clientShellKeyDown(clientShell, VK_ESCAPE, 1);
        g_clientShellKeyUp(clientShell, VK_ESCAPE);
        if (g_log != nullptr) {
            g_log(
                "m4_menu_toggle_dispatched",
                "button=left_secondary path=IClientShell_v4_escape_edge "
                "result=escape_edge_dispatched direct_command_writes=0 "
                "system_input=0");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log != nullptr) {
            g_log(
                "m4_menu_toggle_failed",
                "IClientShell_v4_escape_callback_exception");
        }
    }
    return true;
}

int MenuNavigationVirtualKey(
    MenuNavigationAction action) noexcept {
    switch (action) {
    case MenuNavigationAction::up:
        return VK_UP;
    case MenuNavigationAction::down:
        return VK_DOWN;
    case MenuNavigationAction::left:
        return VK_LEFT;
    case MenuNavigationAction::right:
        return VK_RIGHT;
    case MenuNavigationAction::accept:
        return VK_RETURN;
    case MenuNavigationAction::back:
        return VK_ESCAPE;
    default:
        return 0;
    }
}

const char* MenuNavigationControlName(
    MenuNavigationAction action) noexcept {
    switch (action) {
    case MenuNavigationAction::up:
    case MenuNavigationAction::down:
    case MenuNavigationAction::left:
    case MenuNavigationAction::right:
        return "left_stick";
    case MenuNavigationAction::accept:
        return "right_primary_or_trigger";
    case MenuNavigationAction::back:
        return "right_secondary";
    default:
        return "none";
    }
}

void PollMenuNavigation(
    void* clientShell,
    int retailGameState) noexcept {
    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    const MenuNavigationAction action = UpdateMenuNavigation(
        g_menuNavigationState,
        input,
        usable && !calibrationCaptured,
        CondemnedGameStateAllowsMenuNavigation(retailGameState),
        GetTickCount64());
    const int virtualKey = MenuNavigationVirtualKey(action);
    if (virtualKey == 0) {
        return;
    }

    bool dispatched = false;
    BeginRetailVrSettingsMenuKeyEdge(virtualKey);
    __try {
        g_clientShellKeyDown(clientShell, virtualKey, 1);
        g_clientShellKeyUp(clientShell, virtualKey);
        dispatched = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dispatched = false;
    }
    EndRetailVrSettingsMenuKeyEdge(virtualKey);
    if (g_log == nullptr) {
        return;
    }

    char detail[256]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s key=0x%02X control=%s game_state=%d "
        "path=IClientShell_v4_key_edge direct_command_writes=0 "
        "system_input=0",
        MenuNavigationActionName(action), virtualKey,
        MenuNavigationControlName(action), retailGameState);
    g_log(
        dispatched
            ? "m6_menu_control_dispatched"
            : "m6_menu_control_failed",
        detail);
}

void __fastcall HookClientShellUpdate(
    void* clientShell,
    void* ignoredEdx) {
    (void)ignoredEdx;
    if (clientShell == g_clientShell &&
        g_clientShellKeyDown != nullptr &&
        g_clientShellKeyUp != nullptr) {
        if (InterlockedCompareExchange(
                &g_menuUpdateObserved, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m4_menu_update_hook_called",
                "interface=IClientShell.Default.v4 update_slot=3 "
                "poll_before_retail=1 render_state_pre_post=1");
        }
        const int stateBeforeInput = PublishMenuRenderState();
        if (PollMenuToggle(clientShell, stateBeforeInput)) {
            RequireMenuNavigationRelease(g_menuNavigationState);
        } else if (InterlockedCompareExchange(
                       &g_menuControlsEnabled, 0, 0) != 0) {
            PollMenuNavigation(clientShell, stateBeforeInput);
        }
        PublishMenuRenderState();
    }
    if (clientShell == g_clientShell) {
        ObservePlayerCollisionXrayUpdate(true);
        ObservePlayerColliderDimensionTransition("pre_retail_update", false);
    }
    g_originalClientShellUpdate(clientShell);
    if (clientShell == g_clientShell) {
        ObservePlayerColliderDimensionTransition("post_retail_update", false);
        ObservePlayerCollisionXrayUpdate(false);
        ProcessPhysicalMeleeBlockNativeRelease();
        const PlayerColliderPendingProcessResult colliderResult =
            ProcessPendingPlayerColliderReapply();
        if (colliderResult ==
                PlayerColliderPendingProcessResult::NativeSetDimsAttempted) {
            ObservePlayerColliderDimensionTransition(
                "post_mod_setdims_attempt", true);
        } else if (colliderResult ==
                PlayerColliderPendingProcessResult::AlreadyMatched) {
            ObservePlayerColliderDimensionTransition(
                "post_pending_noop", true);
        }
        PublishMenuRenderState();
        SampleForensicMemoryAfterRetailUpdate();
        SampleRetailPlayerVitals();
    }
}

bool LocomotionTargetMatches(const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kGetBindingValuePrefix,
                   sizeof(kGetBindingValuePrefix)) == 0 &&
               std::memcmp(
                   target + 0x13, kIsDeviceReadySequence,
                   sizeof(kIsDeviceReadySequence)) == 0 &&
               std::memcmp(
                   target + 0x32, kGetDeviceObjectValueSequence,
                   sizeof(kGetDeviceObjectValueSequence)) == 0 &&
               std::memcmp(
                   target + 0x47, kDefaultReturnSequence,
                   sizeof(kDefaultReturnSequence)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool PlayerSetDimensionsTargetMatches(
    HMODULE gameClientModule,
    const unsigned char* target) noexcept {
    if (gameClientModule == nullptr || target == nullptr) {
        return false;
    }
    auto* const base =
        reinterpret_cast<unsigned char*>(gameClientModule);
    __try {
        std::uintptr_t firstPhysicsGlobal = 0U;
        std::uintptr_t secondPhysicsGlobal = 0U;
        std::memcpy(
            &firstPhysicsGlobal, target + 0x09,
            sizeof(firstPhysicsGlobal));
        std::memcpy(
            &secondPhysicsGlobal, target + 0x42,
            sizeof(secondPhysicsGlobal));
        return IsExecutableModuleAddress(
                   target, gameClientModule) &&
            firstPhysicsGlobal ==
                reinterpret_cast<std::uintptr_t>(
                    base + kClientPhysicsGlobalRva) &&
            secondPhysicsGlobal ==
                reinterpret_cast<std::uintptr_t>(
                    base + kClientPhysicsGlobalRva) &&
            std::memcmp(
                target, kPlayerSetDimensionsPrefix,
                sizeof(kPlayerSetDimensionsPrefix)) == 0 &&
            std::memcmp(
                target + 0x0D,
                kPlayerSetDimensionsAfterPhysicsGlobal,
                sizeof(kPlayerSetDimensionsAfterPhysicsGlobal)) == 0 &&
            std::memcmp(
                target + 0x2F,
                kPlayerSetDimensionsDesiredRead,
                sizeof(kPlayerSetDimensionsDesiredRead)) == 0 &&
            std::memcmp(
                target + 0x56,
                kPlayerSetDimensionsNativeCall,
                sizeof(kPlayerSetDimensionsNativeCall)) == 0 &&
            std::memcmp(
                target + 0x8A,
                kPlayerSetDimensionsReturnTail,
                sizeof(kPlayerSetDimensionsReturnTail)) == 0 &&
            std::memcmp(
                target + 0x13E,
                kPlayerSetDimensionsReturnTail,
                sizeof(kPlayerSetDimensionsReturnTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PlayerCollisionVelocityTraceTargetMatches(
    HMODULE executableModule,
    const unsigned char* target) noexcept {
    if (executableModule == nullptr || target == nullptr ||
        g_gameClientBase == nullptr) {
        return false;
    }
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executableModule);
    void** const expectedVtable = reinterpret_cast<void**>(
        executableBase + kClientPhysicsVtableExecutableRva);
    __try {
        void* runtimePhysics = nullptr;
        void** runtimeVtable = nullptr;
        void* staticSetVelocity = nullptr;
        std::uintptr_t logText = 0U;
        std::memcpy(
            &runtimePhysics,
            g_gameClientBase + kClientPhysicsGlobalRva,
            sizeof(runtimePhysics));
        if (runtimePhysics != nullptr) {
            std::memcpy(
                &runtimeVtable, runtimePhysics,
                sizeof(runtimeVtable));
        }
        std::memcpy(
            &staticSetVelocity,
            &expectedVtable[kSetVelocityVtableSlot],
            sizeof(staticSetVelocity));
        std::memcpy(&logText, target + 0x2CU, sizeof(logText));
        return target == executableBase + kSetVelocityExecutableRva &&
            runtimePhysics != nullptr &&
            runtimeVtable == expectedVtable &&
            staticSetVelocity == target &&
            IsExecutableModuleAddress(target, executableModule) &&
            std::memcmp(target, kSetVelocityPrefix,
                        sizeof(kSetVelocityPrefix)) == 0 &&
            target[0x2BU] == 0x68U &&
            logText == reinterpret_cast<std::uintptr_t>(
                executableBase + kSetVelocityLogTextRva) &&
            std::memcmp(target + 0x41U, kSetVelocityTail,
                        sizeof(kSetVelocityTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PlayerColliderWriterTraceTargetsMatch(
    HMODULE gameClientModule,
    HMODULE executableModule,
    const unsigned char* setObjectDimensionsTarget) noexcept {
    if (gameClientModule == nullptr ||
        executableModule == nullptr ||
        setObjectDimensionsTarget == nullptr) {
        return false;
    }
    auto* const gameBase =
        reinterpret_cast<unsigned char*>(gameClientModule);
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executableModule);
    auto* const getObjectDimensionsTarget =
        executableBase + kGetObjectDimensionsExecutableRva;
    auto* const expectedSetObjectDimensionsTarget =
        executableBase + kSetObjectDimensionsExecutableRva;
    void** const expectedVtable = reinterpret_cast<void**>(
        executableBase + kClientPhysicsVtableExecutableRva);
    const std::uintptr_t expectedPhysicsGlobal =
        reinterpret_cast<std::uintptr_t>(
            gameBase + kClientPhysicsGlobalRva);

    __try {
        void* runtimePhysics = nullptr;
        void** runtimeVtable = nullptr;
        void* staticGetObjectDimensions = nullptr;
        void* staticSetObjectDimensions = nullptr;
        std::memcpy(
            &runtimePhysics,
            gameBase + kClientPhysicsGlobalRva,
            sizeof(runtimePhysics));
        if (runtimePhysics != nullptr) {
            std::memcpy(
                &runtimeVtable, runtimePhysics,
                sizeof(runtimeVtable));
        }
        std::memcpy(
            &staticGetObjectDimensions,
            &expectedVtable[kGetObjectDimensionsVtableSlot],
            sizeof(staticGetObjectDimensions));
        std::memcpy(
            &staticSetObjectDimensions,
            &expectedVtable[kSetObjectDimensionsVtableSlot],
            sizeof(staticSetObjectDimensions));

        std::uintptr_t moveManagerPrimaryPhysicsGlobal = 0U;
        std::uintptr_t moveManagerFallbackPhysicsGlobal = 0U;
        std::uintptr_t adjacentInitialPhysicsGlobal = 0U;
        std::uintptr_t adjacentFirstPhysicsGlobal = 0U;
        std::uintptr_t adjacentRetryPhysicsGlobal = 0U;
        std::uintptr_t writerLiteralPhysicsGlobal = 0U;
        std::uintptr_t writerPrimaryPhysicsGlobal = 0U;
        std::uintptr_t writerRetryPhysicsGlobal = 0U;
        std::memcpy(
            &moveManagerPrimaryPhysicsGlobal,
            gameBase + kPlayerSetDimensionsRva + 0x42U,
            sizeof(moveManagerPrimaryPhysicsGlobal));
        std::memcpy(
            &moveManagerFallbackPhysicsGlobal,
            gameBase + kPlayerSetDimensionsRva + 0x62U,
            sizeof(moveManagerFallbackPhysicsGlobal));
        std::memcpy(
            &adjacentInitialPhysicsGlobal,
            gameBase + kPlayerColliderAdjacentRoutineRva + 0x08U,
            sizeof(adjacentInitialPhysicsGlobal));
        std::memcpy(
            &adjacentFirstPhysicsGlobal,
            gameBase + 0x00031D55U,
            sizeof(adjacentFirstPhysicsGlobal));
        std::memcpy(
            &adjacentRetryPhysicsGlobal,
            gameBase + 0x00031D6AU,
            sizeof(adjacentRetryPhysicsGlobal));
        std::memcpy(
            &writerLiteralPhysicsGlobal,
            gameBase + 0x00034694U,
            sizeof(writerLiteralPhysicsGlobal));
        std::memcpy(
            &writerPrimaryPhysicsGlobal,
            gameBase + 0x00034746U,
            sizeof(writerPrimaryPhysicsGlobal));
        std::memcpy(
            &writerRetryPhysicsGlobal,
            gameBase + 0x00034779U,
            sizeof(writerRetryPhysicsGlobal));

        std::uintptr_t firstLogText = 0U;
        std::uintptr_t secondLogText = 0U;
        std::memcpy(
            &firstLogText,
            setObjectDimensionsTarget + 0x55U,
            sizeof(firstLogText));
        std::memcpy(
            &secondLogText,
            setObjectDimensionsTarget + 0x12DU,
            sizeof(secondLogText));

        std::int32_t writerCallerDisplacement = 0;
        std::memcpy(
            &writerCallerDisplacement,
            gameBase + 0x00037FF0U,
            sizeof(writerCallerDisplacement));
        const std::intptr_t writerCallerTarget =
            reinterpret_cast<std::intptr_t>(
                gameBase +
                kPlayerColliderWriterCallerReturnRva) +
            writerCallerDisplacement;

        return
            setObjectDimensionsTarget ==
                expectedSetObjectDimensionsTarget &&
            runtimePhysics != nullptr &&
            runtimeVtable == expectedVtable &&
            staticGetObjectDimensions ==
                getObjectDimensionsTarget &&
            staticSetObjectDimensions ==
                expectedSetObjectDimensionsTarget &&
            IsExecutableModuleAddress(
                getObjectDimensionsTarget, executableModule) &&
            IsExecutableModuleAddress(
                setObjectDimensionsTarget, executableModule) &&
            moveManagerPrimaryPhysicsGlobal ==
                expectedPhysicsGlobal &&
            moveManagerFallbackPhysicsGlobal ==
                expectedPhysicsGlobal &&
            adjacentInitialPhysicsGlobal ==
                expectedPhysicsGlobal &&
            adjacentFirstPhysicsGlobal ==
                expectedPhysicsGlobal &&
            adjacentRetryPhysicsGlobal ==
                expectedPhysicsGlobal &&
            writerLiteralPhysicsGlobal ==
                expectedPhysicsGlobal &&
            writerPrimaryPhysicsGlobal ==
                expectedPhysicsGlobal &&
            writerRetryPhysicsGlobal ==
                expectedPhysicsGlobal &&
            firstLogText ==
                reinterpret_cast<std::uintptr_t>(
                    executableBase +
                    kSetObjectDimensionsFirstLogTextRva) &&
            secondLogText ==
                reinterpret_cast<std::uintptr_t>(
                    executableBase +
                    kSetObjectDimensionsSecondLogTextRva) &&
            writerCallerTarget ==
                reinterpret_cast<std::intptr_t>(
                    gameBase +
                    kPlayerColliderWriterRoutineRva) &&
            std::memcmp(
                getObjectDimensionsTarget,
                kGetObjectDimensionsExecutablePrefix,
                sizeof(kGetObjectDimensionsExecutablePrefix)) == 0 &&
            std::memcmp(
                setObjectDimensionsTarget,
                kSetObjectDimensionsExecutablePrefix,
                sizeof(kSetObjectDimensionsExecutablePrefix)) == 0 &&
            setObjectDimensionsTarget[0x54U] == 0x68U &&
            setObjectDimensionsTarget[0x12CU] == 0x68U &&
            std::memcmp(
                setObjectDimensionsTarget + 0xDDU,
                kSetObjectDimensionsSuccessTail,
                sizeof(kSetObjectDimensionsSuccessTail)) == 0 &&
            std::memcmp(
                setObjectDimensionsTarget + 0xFBU,
                kSetObjectDimensionsAdjustedTail,
                sizeof(kSetObjectDimensionsAdjustedTail)) == 0 &&
            std::memcmp(
                setObjectDimensionsTarget + 0x13AU,
                kSetObjectDimensionsInvalidTail,
                sizeof(kSetObjectDimensionsInvalidTail)) == 0 &&
            std::memcmp(
                gameBase + kPlayerSetDimensionsRva + 0x2FU,
                kPlayerSetDimensionsDesiredRead,
                sizeof(kPlayerSetDimensionsDesiredRead)) == 0 &&
            std::memcmp(
                gameBase + kPlayerSetDimensionsRva + 0x56U,
                kPlayerSetDimensionsNativeCall,
                sizeof(kPlayerSetDimensionsNativeCall)) == 0 &&
            std::memcmp(
                gameBase + kPlayerSetDimensionsRva + 0x66U,
                kPlayerSetDimensionsFallbackAfterPhysicsGlobal,
                sizeof(
                    kPlayerSetDimensionsFallbackAfterPhysicsGlobal)) == 0 &&
            std::memcmp(
                gameBase + kPlayerColliderAdjacentRoutineRva,
                kPlayerColliderAdjacentPrefix,
                sizeof(kPlayerColliderAdjacentPrefix)) == 0 &&
            std::memcmp(
                gameBase + 0x00031CFCU,
                kPlayerColliderAdjacentAfterPhysicsGlobal,
                sizeof(
                    kPlayerColliderAdjacentAfterPhysicsGlobal)) == 0 &&
            std::memcmp(
                gameBase + 0x00031D38U,
                kPlayerColliderAdjacentFirstRequest,
                sizeof(kPlayerColliderAdjacentFirstRequest)) == 0 &&
            std::memcmp(
                gameBase + 0x00031D59U,
                kPlayerColliderAdjacentFirstCallSuffix,
                sizeof(
                    kPlayerColliderAdjacentFirstCallSuffix)) == 0 &&
            std::memcmp(
                gameBase + 0x00031D6EU,
                kPlayerColliderAdjacentRetrySuffix,
                sizeof(kPlayerColliderAdjacentRetrySuffix)) == 0 &&
            std::memcmp(
                gameBase + kPlayerColliderWriterRoutineRva,
                kPlayerColliderWriterEntry,
                sizeof(kPlayerColliderWriterEntry)) == 0 &&
            std::memcmp(
                gameBase + 0x0003480DU,
                kPlayerColliderWriterTail,
                sizeof(kPlayerColliderWriterTail)) == 0 &&
            std::memcmp(
                gameBase +
                    kPlayerColliderWriterCallerBranchRva,
                kPlayerColliderWriterCallerBranch,
                sizeof(kPlayerColliderWriterCallerBranch)) == 0 &&
            std::memcmp(
                gameBase + 0x00034698U,
                kPlayerColliderWriterLiteralSuffix,
                sizeof(kPlayerColliderWriterLiteralSuffix)) == 0 &&
            std::memcmp(
                gameBase + 0x0003474AU,
                kPlayerColliderWriterPrimarySuffix,
                sizeof(kPlayerColliderWriterPrimarySuffix)) == 0 &&
            std::memcmp(
                gameBase + 0x0003477DU,
                kPlayerColliderWriterRetrySuffix,
                sizeof(kPlayerColliderWriterRetrySuffix)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


bool TurningTargetMatches(const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kGetExtremalCommandValuePrefix,
                   sizeof(kGetExtremalCommandValuePrefix)) == 0 &&
               std::memcmp(
                   target + 0x20, kGetExtremalCommandValueLoop,
                   sizeof(kGetExtremalCommandValueLoop)) == 0 &&
               std::memcmp(
                   target + 0x31, kGetExtremalBindingCall,
                   sizeof(kGetExtremalBindingCall)) == 0 &&
               std::memcmp(
                   target + 0x58, kGetExtremalBindingStride,
                   sizeof(kGetExtremalBindingStride)) == 0 &&
               std::memcmp(
                   target + 0x63, kGetExtremalCommandValueTail,
                   sizeof(kGetExtremalCommandValueTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool FireVectorsTargetMatches(const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kGetFireVectorsPrefix,
                   sizeof(kGetFireVectorsPrefix)) == 0 &&
               std::memcmp(
                   target + 0x08, kGetFireVectorsStackInit,
                   sizeof(kGetFireVectorsStackInit)) == 0 &&
               std::memcmp(
                   target + 0x2A, kGetFireVectorsCameraProbe,
                   sizeof(kGetFireVectorsCameraProbe)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool FirearmMuzzleSocketLayoutMatches(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return false;
    }
    auto* const base =
        reinterpret_cast<unsigned char*>(gameClientModule);
    auto* const helper =
        base + kRetailSocketTransformHelperRva;
    constexpr unsigned char kGetSocketCall[] = {
        0x8B, 0x01, 0x56, 0xFF, 0x50, 0x04};
    constexpr unsigned char kGetSocketTransformCall[] = {
        0x8B, 0x01, 0x56, 0xFF, 0x50, 0x08};
    __try {
        return IsExecutableModuleAddress(
                   helper, gameClientModule) &&
            std::strcmp(
                reinterpret_cast<const char*>(
                    base + kFlashSocketNameRva),
                "Flash") == 0 &&
            std::strcmp(
                reinterpret_cast<const char*>(
                    base + kBreachSocketNameRva),
                "Breach") == 0 &&
            std::memcmp(
                helper + 0x3AU,
                kGetSocketCall,
                sizeof(kGetSocketCall)) == 0 &&
            std::memcmp(
                helper + 0x6AU,
                kGetSocketTransformCall,
                sizeof(kGetSocketTransformCall)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ResolveVerifiedFirearmMuzzleModelInterface(
    void* masterDatabase,
    HMODULE gameClientModule,
    void*& model,
    GetModelSocketFunction& getSocket,
    GetModelSocketTransformFunction& getSocketTransform) noexcept {
    model = nullptr;
    getSocket = nullptr;
    getSocketTransform = nullptr;
    if (masterDatabase == nullptr ||
        gameClientModule == nullptr) {
        return false;
    }

    void* const candidate = FindCurrentInterface(
        masterDatabase, "ILTModelClient.Default", 0);
    if (candidate == nullptr) {
        return false;
    }
    auto* const gameClientBase =
        reinterpret_cast<unsigned char*>(gameClientModule);
    HMODULE const executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) {
        return false;
    }
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executable);
    void* retailModelGlobal = nullptr;
    void** vtable = nullptr;
    void* socketTarget = nullptr;
    void* socketTransformTarget = nullptr;
    __try {
        std::memcpy(
            &retailModelGlobal,
            gameClientBase + kModelClientGlobalRva,
            sizeof(retailModelGlobal));
        vtable = *static_cast<void***>(candidate);
        if (vtable != nullptr) {
            socketTarget = vtable[kGetModelSocketSlot];
            socketTransformTarget =
                vtable[kGetModelSocketTransformSlot];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    void* const expectedSocket =
        executableBase + kGetModelSocketExecutableRva;
    void* const expectedSocketTransform =
        executableBase +
        kGetModelSocketTransformExecutableRva;
    if (vtable == nullptr ||
        (retailModelGlobal != nullptr &&
         retailModelGlobal != candidate) ||
        socketTarget != expectedSocket ||
        socketTransformTarget != expectedSocketTransform ||
        !IsExecutableAddress(socketTarget) ||
        !IsExecutableAddress(socketTransformTarget)) {
        return false;
    }

    model = candidate;
    getSocket =
        reinterpret_cast<GetModelSocketFunction>(socketTarget);
    getSocketTransform =
        reinterpret_cast<GetModelSocketTransformFunction>(
            socketTransformTarget);
    return true;
}

bool MeleeEnableCollisionsTargetMatches(
    HMODULE gameClientModule,
    const unsigned char* target) noexcept {
    if (gameClientModule == nullptr || target == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    __try {
        std::uint32_t clientGlobal = 0;
        std::uint32_t limitText = 0;
        std::memcpy(&clientGlobal, target + 7, sizeof(clientGlobal));
        std::memcpy(&limitText, target + 0x10F, sizeof(limitText));
        return std::memcmp(
                   target, kMeleeEnableCollisionsPrefix,
                   sizeof(kMeleeEnableCollisionsPrefix)) == 0 &&
               clientGlobal == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeClientGlobalRva)) &&
               std::memcmp(
                   target + 0x0B, kMeleeEnableCollisionsBodyPrefix,
                   sizeof(kMeleeEnableCollisionsBodyPrefix)) == 0 &&
               std::memcmp(
                   target + 0x106,
                   kMeleeCollisionLimitTextReferencePrefix,
                   sizeof(kMeleeCollisionLimitTextReferencePrefix)) == 0 &&
               limitText == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeCollisionLimitTextRva));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MeleeCollisionRoleLayoutMatches(
    const unsigned char* enableCollisionsTarget) noexcept {
    if (enableCollisionsTarget == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   enableCollisionsTarget +
                       kMeleeCollisionRecordSelectionOffset,
                   kMeleeCollisionRecordSelection,
                   sizeof(kMeleeCollisionRecordSelection)) == 0 &&
               std::memcmp(
                   enableCollisionsTarget +
                       kMeleeBlockingArgumentBranchOffset,
                   kMeleeBlockingArgumentBranch,
                   sizeof(kMeleeBlockingArgumentBranch)) == 0 &&
               std::memcmp(
                   enableCollisionsTarget +
                       kMeleeBlockingNotifierBranchOffset,
                   kMeleeBlockingNotifierBranch,
                   sizeof(kMeleeBlockingNotifierBranch)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MeleeNativeCapsulePropertyCallsitesMatch(
    HMODULE gameClientModule,
    const unsigned char* enableCollisionsTarget) noexcept {
    if (gameClientModule == nullptr ||
        enableCollisionsTarget == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const lengthUpReturn =
        base + kMeleeNativeLengthUpReadReturnRva;
    auto* const lengthDownReturn =
        base + kMeleeNativeLengthDownReadReturnRva;
    auto* const radiusReturn =
        base + kMeleeNativeRadiusReadReturnRva;
    __try {
        std::uint32_t lengthUpName = 0U;
        std::uint32_t lengthDownName = 0U;
        std::uint32_t radiusName = 0U;
        std::memcpy(
            &lengthUpName,
            enableCollisionsTarget +
                kMeleeLengthUpPropertyPushOffset + 1U,
            sizeof(lengthUpName));
        std::memcpy(
            &lengthDownName,
            enableCollisionsTarget +
                kMeleeLengthDownPropertyPushOffset + 1U,
            sizeof(lengthDownName));
        std::memcpy(
            &radiusName,
            enableCollisionsTarget +
                kMeleeRadiusPropertyPushOffset + 1U,
            sizeof(radiusName));
        return enableCollisionsTarget[
                   kMeleeLengthUpPropertyPushOffset] == 0x68 &&
               enableCollisionsTarget[
                   kMeleeLengthDownPropertyPushOffset] == 0x68 &&
               enableCollisionsTarget[
                   kMeleeRadiusPropertyPushOffset] == 0x68 &&
               lengthUpName == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeLengthUpPropertyNameRva)) &&
               lengthDownName == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeLengthDownPropertyNameRva)) &&
               radiusName == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeRadiusPropertyNameRva)) &&
               std::memcmp(
                   lengthUpReturn -
                       sizeof(kMeleeNativeFloatReadCall),
                   kMeleeNativeFloatReadCall,
                   sizeof(kMeleeNativeFloatReadCall)) == 0 &&
               std::memcmp(
                   lengthUpReturn, kMeleeNativeLengthUpStore,
                   sizeof(kMeleeNativeLengthUpStore)) == 0 &&
               std::memcmp(
                   lengthDownReturn -
                       sizeof(kMeleeNativeFloatReadCall),
                   kMeleeNativeFloatReadCall,
                   sizeof(kMeleeNativeFloatReadCall)) == 0 &&
               std::memcmp(
                   lengthDownReturn, kMeleeNativeLengthDownStore,
                   sizeof(kMeleeNativeLengthDownStore)) == 0 &&
               std::memcmp(
                   radiusReturn -
                       sizeof(kMeleeNativeFloatReadCall),
                   kMeleeNativeFloatReadCall,
                   sizeof(kMeleeNativeFloatReadCall)) == 0 &&
               std::memcmp(
                   radiusReturn, kMeleeNativeRadiusStore,
                   sizeof(kMeleeNativeRadiusStore)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DatabaseFloatReaderFunction ResolveMasterDatabaseFloatReader(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return nullptr;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    __try {
        void* masterDatabase = nullptr;
        std::memcpy(
            &masterDatabase, base + kMasterDatabaseGlobalRva,
            sizeof(masterDatabase));
        if (masterDatabase == nullptr) {
            return nullptr;
        }
        void** vtable = nullptr;
        std::memcpy(&vtable, masterDatabase, sizeof(vtable));
        if (vtable == nullptr) {
            return nullptr;
        }
        void* target = nullptr;
        std::memcpy(
            &target,
            reinterpret_cast<const unsigned char*>(vtable) +
                kMasterDatabaseFloatReaderSlot,
            sizeof(target));
        if (!IsExecutableAddress(target) ||
            std::memcmp(
                target, kMasterDatabaseFloatReaderBody,
                sizeof(kMasterDatabaseFloatReaderBody)) != 0) {
            return nullptr;
        }
        return reinterpret_cast<DatabaseFloatReaderFunction>(
            target);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool MeleeUpdateCollisionTargetMatches(
    const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kMeleeUpdateCollisionPrefix,
                   sizeof(kMeleeUpdateCollisionPrefix)) == 0 &&
               std::memcmp(
                   target + 0x7B, kMeleeUpdateCollisionNodeQuery,
                   sizeof(kMeleeUpdateCollisionNodeQuery)) == 0 &&
               std::memcmp(
                   target + 0xE2, kMeleeUpdateCollisionSetTransform,
                   sizeof(kMeleeUpdateCollisionSetTransform)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BuildRigidTransformTargetMatches(
    const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kBuildRigidTransformPrefix,
                   sizeof(kBuildRigidTransformPrefix)) == 0 &&
               std::memcmp(
                   target + 0x3D, kBuildRigidTransformTail,
                   sizeof(kBuildRigidTransformTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MeleeImpactDispatchTargetMatches(
    HMODULE gameClientModule,
    const unsigned char* target) noexcept {
    if (gameClientModule == nullptr || target == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const vectorSetup =
        base + kMeleeCollisionCallbackRva + 0x34DU;
    auto* const vectorPushCall =
        base + kMeleeTargetReferenceVectorPushCallRva;
    auto* const vectorPush =
        base + kMeleeTargetReferenceVectorPushRva;
    auto* const callbackCallsite =
        base + kMeleeCollisionCallbackRva + 0x36DU;
    auto* const dispatchCall =
        base + kMeleeImpactDispatchCallRva;
    __try {
        std::int32_t vectorPushRelativeTarget = 0;
        std::memcpy(
            &vectorPushRelativeTarget, vectorPushCall + 1,
            sizeof(vectorPushRelativeTarget));
        const auto resolvedVectorPush =
            reinterpret_cast<std::uintptr_t>(vectorPushCall + 5) +
            static_cast<std::intptr_t>(vectorPushRelativeTarget);
        std::int32_t dispatchRelativeTarget = 0;
        std::memcpy(
            &dispatchRelativeTarget, dispatchCall + 1,
            sizeof(dispatchRelativeTarget));
        const auto resolvedDispatch =
            reinterpret_cast<std::uintptr_t>(dispatchCall + 5) +
            static_cast<std::intptr_t>(dispatchRelativeTarget);
        return std::memcmp(
                   target, kMeleeImpactDispatchPrefix,
                   sizeof(kMeleeImpactDispatchPrefix)) == 0 &&
               std::memcmp(
                   vectorSetup,
                   kMeleeTargetReferenceVectorSetup,
                   sizeof(kMeleeTargetReferenceVectorSetup)) == 0 &&
               vectorPushCall[0] == 0xE8 &&
               resolvedVectorPush ==
                   reinterpret_cast<std::uintptr_t>(vectorPush) &&
               std::memcmp(
                   vectorPush,
                   kMeleeTargetReferenceVectorPushPrefix,
                   sizeof(kMeleeTargetReferenceVectorPushPrefix)) == 0 &&
               std::memcmp(
                   callbackCallsite,
                   kMeleeImpactDispatchCallsitePrefix,
                   sizeof(kMeleeImpactDispatchCallsitePrefix)) == 0 &&
               dispatchCall[0] == 0xE8 &&
               resolvedDispatch ==
                   reinterpret_cast<std::uintptr_t>(target);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ResetMeleeTargetReferenceTargetMatches(
    const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kResetMeleeTargetReferenceBody,
                   sizeof(kResetMeleeTargetReferenceBody)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RetailPlayerVitalsLayoutMatches(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const setter = base + kPlayerHealthSetterRva;
    auto* const handler = base + kPlayerHealthCommandHandlerRva;
    auto* const registration =
        base + kPlayerHealthCommandRegistrationRva;
    auto* const healthName = base + kPlayerHealthNameRva;
    __try {
        std::uint32_t handlerClientShellGlobal = 0U;
        std::uint32_t handlerStatsGlobal = 0U;
        std::uint32_t registeredHandler = 0U;
        std::uint32_t registeredName = 0U;
        std::memcpy(
            &handlerClientShellGlobal, handler + 2U,
            sizeof(handlerClientShellGlobal));
        std::memcpy(
            &handlerStatsGlobal, handler + 22U,
            sizeof(handlerStatsGlobal));
        std::memcpy(
            &registeredHandler, registration + 1U,
            sizeof(registeredHandler));
        std::memcpy(
            &registeredName, registration + 6U,
            sizeof(registeredName));
        const auto Address = [](const unsigned char* value) noexcept {
            return static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(value));
        };
        return std::memcmp(
                   setter, kPlayerHealthSetterPrefix,
                   sizeof(kPlayerHealthSetterPrefix)) == 0 &&
               handler[0] == 0x8B && handler[1] == 0x0D &&
               handlerClientShellGlobal ==
                   Address(base + 0x001694A8U) &&
               handler[21] == 0xA1 &&
               handlerStatsGlobal ==
                   Address(base + kPlayerStatsGlobalRva) &&
               std::memcmp(
                   handler + 26U, kPlayerHealthHandlerBody,
                   sizeof(kPlayerHealthHandlerBody)) == 0 &&
               registration[0] == 0x68 &&
               registeredHandler ==
                   Address(base + kPlayerHealthCommandHandlerRva) &&
               registration[5] == 0x68 &&
               registeredName == Address(healthName) &&
               std::memcmp(
                   registration + 10U,
                   kPlayerHealthRegistrationTail,
                   sizeof(kPlayerHealthRegistrationTail)) == 0 &&
               std::memcmp(healthName, "Health", 7U) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool EquippedWeaponLayoutMatches(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const getCurrentWeapon =
        base + kGetCurrentWeaponRva;
    auto* const setWeaponTransform =
        base + kSetWeaponTransformRva;
    __try {
        return std::memcmp(
                   getCurrentWeapon, kGetCurrentWeaponBody,
                   sizeof(kGetCurrentWeaponBody)) == 0 &&
               std::memcmp(
                   setWeaponTransform, kSetWeaponTransformPrefix,
                   sizeof(kSetWeaponTransformPrefix)) == 0 &&
               std::memcmp(
                   setWeaponTransform + 0x1C,
                   kSetWeaponTransformSecondModel,
                   sizeof(kSetWeaponTransformSecondModel)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PhysicalMeleeBlockNativeReleaseTargetsMatch(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr ||
        !EquippedWeaponLayoutMatches(gameClientModule)) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const handling = base +
        kClientWeaponHandlingAnimationStimulusRva;
    auto* const active = base +
        kClientWeaponActiveAnimationStimulusRva;
    auto* const block = base + kClientWeaponBlockRva;
    auto* const commandOff = base + kPlayerManagerCommandOffRva;
    __try {
        std::uintptr_t blockVtableTarget = 0U;
        std::uintptr_t commandOffVtableTarget = 0U;
        std::uintptr_t blockStimulusOperand = 0U;
        std::int32_t activeRelativeTarget = 0;
        std::uintptr_t unhandledJumpTarget = 0U;
        std::memcpy(
            &blockVtableTarget,
            base + kClientWeaponVtableRva +
                kClientWeaponBlockVtableSlot * sizeof(void*),
            sizeof(blockVtableTarget));
        std::memcpy(
            &commandOffVtableTarget,
            base + kPlayerManagerCommandOnVtableRva +
                kPlayerManagerCommandOffSlot * sizeof(void*),
            sizeof(commandOffVtableTarget));
        std::memcpy(
            &blockStimulusOperand, block + 0x53U,
            sizeof(blockStimulusOperand));
        std::memcpy(
            &activeRelativeTarget, block + 0x6CU,
            sizeof(activeRelativeTarget));
        std::memcpy(
            &unhandledJumpTarget, commandOff + 0xB0U,
            sizeof(unhandledJumpTarget));
        const auto resolvedActiveTarget =
            reinterpret_cast<std::uintptr_t>(block + 0x70U) +
            static_cast<std::intptr_t>(activeRelativeTarget);
        return std::memcmp(
                   handling,
                   kClientWeaponHandlingAnimationStimulusPrefix,
                   sizeof(
                       kClientWeaponHandlingAnimationStimulusPrefix)) == 0 &&
               std::memcmp(
                   handling + 0x17U,
                   kClientWeaponHandlingAnimationStimulusCall,
                   sizeof(
                       kClientWeaponHandlingAnimationStimulusCall)) == 0 &&
               std::memcmp(
                   active,
                   kClientWeaponActiveAnimationStimulusPrefix,
                   sizeof(kClientWeaponActiveAnimationStimulusPrefix)) == 0 &&
               std::memcmp(
                   block, kClientWeaponBlockPrefix,
                   sizeof(kClientWeaponBlockPrefix)) == 0 &&
               std::memcmp(
                   block + 0x50U,
                   kClientWeaponBlockStimulusDispatch,
                   sizeof(kClientWeaponBlockStimulusDispatch)) == 0 &&
               std::memcmp(
                   block + 0x69U,
                   kClientWeaponBlockActiveTail,
                   sizeof(kClientWeaponBlockActiveTail)) == 0 &&
               blockVtableTarget ==
                   reinterpret_cast<std::uintptr_t>(block) &&
               blockStimulusOperand ==
                   reinterpret_cast<std::uintptr_t>(
                       base + kClientWeaponBlockStimulusNameRva) &&
               resolvedActiveTarget ==
                   reinterpret_cast<std::uintptr_t>(active) &&
               std::memcmp(
                   base + kClientWeaponBlockStimulusNameRva,
                   "CS_Block", 9U) == 0 &&
               std::memcmp(
                   base + kBlockCancelAnimationPropertyNameRva,
                   "CA_BlockCancel", 15U) == 0 &&
               std::memcmp(
                   commandOff, kPlayerManagerCommandOffPrefix,
                   sizeof(kPlayerManagerCommandOffPrefix)) == 0 &&
               commandOffVtableTarget ==
                   reinterpret_cast<std::uintptr_t>(commandOff) &&
               commandOff[0xCAU] == 0x03U &&
               unhandledJumpTarget ==
                   reinterpret_cast<std::uintptr_t>(
                       commandOff + 0x96U) &&
               std::memcmp(
                   commandOff + 0x96U,
                   kPlayerManagerCommandOffUnhandledTail,
                   sizeof(kPlayerManagerCommandOffUnhandledTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ResolveVerifiedForensicCameraSocketTransform(
    HMODULE gameClientModule,
    void*& target) noexcept {
    target = nullptr;
    if (gameClientModule == nullptr) {
        return false;
    }

    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const expectedTarget =
        base + kForensicCameraSocketTransformRva;
    auto* const scannerVtable =
        base + kScannerDisplayVtableRva;
    auto* const digitalCameraVtable =
        base + kDigitalCameraDisplayVtableRva;
    auto* const scannerCall =
        base + kScannerCameraSocketTransformCallRva;
    auto* const digitalCameraCall =
        base + kDigitalCameraSocketTransformCallRva;

    constexpr unsigned char kFunctionPrefix[] = {
        0x51, 0x53, 0x56, 0x8B, 0xF1};
    constexpr unsigned char kNamedSocketBody[] = {
        0x8B, 0x46, 0x04, 0x57,
        0xC7, 0x44, 0x24, 0x0C,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x8B, 0x19, 0x8B, 0x78, 0x0C,
        0x8D, 0x54, 0x24, 0x0C, 0x52,
        0x8D, 0x8E, 0xAC, 0x01, 0x00, 0x00};
    constexpr unsigned char kResolvedSocketTail[] = {
        0x50, 0x57, 0xFF, 0x53, 0x04,
        0x85, 0xC0, 0x75, 0x29};
    constexpr unsigned char kGetWorldTransformBody[] = {
        0x8B, 0x11, 0x6A, 0x01, 0x56,
        0x8B, 0x74, 0x24, 0x14, 0x56,
        0x50, 0xFF, 0x52, 0x08,
        0x85, 0xC0, 0x75, 0x07};
    constexpr unsigned char kSuccessTail[] = {
        0x5F, 0x5E, 0x5B, 0x59, 0xC2, 0x04, 0x00};
    constexpr unsigned char kScannerVirtualCall[] = {
        0xFF, 0x50, 0x24};
    constexpr unsigned char kDigitalCameraVirtualCall[] = {
        0xFF, 0x52, 0x24};

    __try {
        void* scannerTarget = nullptr;
        void* digitalCameraTarget = nullptr;
        std::memcpy(
            &scannerTarget,
            scannerVtable +
                kForensicCameraSocketTransformVtableSlot *
                    sizeof(void*),
            sizeof(scannerTarget));
        std::memcpy(
            &digitalCameraTarget,
            digitalCameraVtable +
                kForensicCameraSocketTransformVtableSlot *
                    sizeof(void*),
            sizeof(digitalCameraTarget));
        if (scannerTarget != expectedTarget ||
            digitalCameraTarget != expectedTarget ||
            std::memcmp(
                expectedTarget, kFunctionPrefix,
                sizeof(kFunctionPrefix)) != 0 ||
            std::memcmp(
                expectedTarget + 0x0BU, kNamedSocketBody,
                sizeof(kNamedSocketBody)) != 0 ||
            std::memcmp(
                expectedTarget + 0x33U, kResolvedSocketTail,
                sizeof(kResolvedSocketTail)) != 0 ||
            std::memcmp(
                expectedTarget + 0x4CU,
                kGetWorldTransformBody,
                sizeof(kGetWorldTransformBody)) != 0 ||
            std::memcmp(
                expectedTarget + 0x5EU, kSuccessTail,
                sizeof(kSuccessTail)) != 0 ||
            std::memcmp(
                scannerCall, kScannerVirtualCall,
                sizeof(kScannerVirtualCall)) != 0 ||
            std::memcmp(
                digitalCameraCall,
                kDigitalCameraVirtualCall,
                sizeof(kDigitalCameraVirtualCall)) != 0) {
            return false;
        }
        target = expectedTarget;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        target = nullptr;
        return false;
    }
}

bool ResolveVerifiedForensicIntersectSegment(
    HMODULE gameClientModule,
    void*& target) noexcept {
    target = nullptr;
    if (gameClientModule == nullptr) {
        return false;
    }
    HMODULE const executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) {
        return false;
    }

    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executable);
    auto* const targetAcquire =
        base + kForensicTargetAcquireRva;
    auto* const firstIntersectCall =
        base + kForensicIntersectFirstReturnRva - 6U;
    auto* const secondIntersectCall =
        base + kForensicIntersectSecondReturnRva - 6U;
    void* const expectedTarget =
        executableBase + kRetailIntersectSegmentThunkRva;
    constexpr unsigned char kTargetAcquirePrefix[] = {
        0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00,
        0x53, 0x55, 0x56, 0x8B, 0xF1};
    constexpr unsigned char kIntersectCall[] = {
        0x8B, 0x01, 0x52, 0xFF, 0x50, 0x7C};
    constexpr unsigned char kThunkPrefix[] = {
        0x8B, 0x0D};
    constexpr unsigned char kThunkTail[] = {
        0x8B, 0x01, 0xFF, 0x60, 0x0C};

    __try {
        void* engineClient = nullptr;
        std::memcpy(
            &engineClient,
            base + kEngineClientGlobalRva,
            sizeof(engineClient));
        if (engineClient == nullptr) {
            return false;
        }
        void** const vtable =
            *static_cast<void***>(engineClient);
        if (vtable == nullptr) {
            return false;
        }
        void* const resolvedTarget =
            vtable[kEngineIntersectSegmentVtableSlot];
        std::uint32_t dispatcherGlobal = 0U;
        std::memcpy(
            &dispatcherGlobal,
            static_cast<unsigned char*>(
                expectedTarget) + 2U,
            sizeof(dispatcherGlobal));
        const std::uint32_t expectedDispatcherGlobal =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(
                    executableBase +
                    kRetailIntersectDispatcherGlobalRva));
        if (resolvedTarget != expectedTarget ||
            std::memcmp(
                targetAcquire, kTargetAcquirePrefix,
                sizeof(kTargetAcquirePrefix)) != 0 ||
            std::memcmp(
                firstIntersectCall, kIntersectCall,
                sizeof(kIntersectCall)) != 0 ||
            std::memcmp(
                secondIntersectCall, kIntersectCall,
                sizeof(kIntersectCall)) != 0 ||
            std::memcmp(
                expectedTarget, kThunkPrefix,
                sizeof(kThunkPrefix)) != 0 ||
            std::memcmp(
                static_cast<unsigned char*>(
                    expectedTarget) + 6U,
                kThunkTail, sizeof(kThunkTail)) != 0 ||
            dispatcherGlobal != expectedDispatcherGlobal) {
            return false;
        }
        target = resolvedTarget;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        target = nullptr;
        return false;
    }
}

bool InstallForensicCameraSocketRay(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept {
    if (gameClientModule == nullptr || log == nullptr) {
        return false;
    }

    AcquireSRWLockShared(&g_bindingLock);
    const bool alreadyInstalled =
        g_forensicCameraSocketTransformHookTarget != nullptr &&
        g_originalForensicCameraSocketTransform != nullptr &&
        g_forensicIntersectSegmentHookTarget != nullptr &&
        g_originalForensicIntersectSegment != nullptr;
    ReleaseSRWLockShared(&g_bindingLock);
    if (alreadyInstalled) {
        InterlockedExchange(
            &g_forensicCameraSocketRayEnabled, 1);
        return true;
    }

    void* cameraSocketTarget = nullptr;
    if (!ResolveVerifiedForensicCameraSocketTransform(
            gameClientModule, cameraSocketTarget)) {
        log(
            "m5_forensic_camera_socket_ray_rejected",
            "GameOrig_Camera_socket_function_vtables_or_"
            "callsites_signature_mismatch fallback=retail");
        return false;
    }

    void* intersectTarget = nullptr;
    if (!ResolveVerifiedForensicIntersectSegment(
            gameClientModule, intersectTarget)) {
        log(
            "m5_forensic_camera_socket_ray_rejected",
            "GameOrig_target_acquire_or_Retail_intersect_"
            "signature_mismatch fallback=retail");
        return false;
    }

    InterlockedExchange(
        &g_forensicCameraSocketRayEnabled, 0);
    AcquireSRWLockExclusive(
        &g_forensicCameraSocketPoseLock);
    g_forensicCameraSocketPoses = {};
    g_forensicCameraSocketPoseSequence = 0U;
    ReleaseSRWLockExclusive(
        &g_forensicCameraSocketPoseLock);
    InterlockedExchange(
        &g_forensicCameraSocketPoseCaptures[0], 0);
    InterlockedExchange(
        &g_forensicCameraSocketPoseCaptures[1], 0);
    InterlockedExchange(
        &g_forensicCameraSocketRayCalls, 0);
    InterlockedExchange(
        &g_forensicCameraSocketRayOverrides, 0);
    InterlockedExchange(
        &g_forensicCameraSocketRayFallbacks, 0);
    InterlockedExchange(
        &g_forensicCameraSocketRayLastResult[0], -1);
    InterlockedExchange(
        &g_forensicCameraSocketRayLastResult[1], -1);

    const char* hookStep = "camera_socket_create";
    MH_STATUS status = MH_CreateHook(
        cameraSocketTarget,
        reinterpret_cast<void*>(
            &HookForensicCameraSocketTransform),
        reinterpret_cast<void**>(
            &g_originalForensicCameraSocketTransform));
    if (status == MH_OK) {
        hookStep = "intersect_create";
        status = MH_CreateHook(
            intersectTarget,
            reinterpret_cast<void*>(
                &HookForensicIntersectSegment),
            reinterpret_cast<void**>(
                &g_originalForensicIntersectSegment));
    }
    if (status == MH_OK) {
        hookStep = "camera_socket_enable";
        status = MH_EnableHook(cameraSocketTarget);
    }
    if (status == MH_OK) {
        hookStep = "intersect_enable";
        status = MH_EnableHook(intersectTarget);
    }
    if (status != MH_OK) {
        MH_DisableHook(intersectTarget);
        MH_DisableHook(cameraSocketTarget);
        MH_RemoveHook(intersectTarget);
        MH_RemoveHook(cameraSocketTarget);
        g_originalForensicIntersectSegment = nullptr;
        g_originalForensicCameraSocketTransform = nullptr;
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "step=%s status=%s fallback=retail",
            hookStep, MH_StatusToString(status));
        log(
            "m5_forensic_camera_socket_ray_rejected",
            detail);
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_forensicCameraSocketTransformHookTarget =
        cameraSocketTarget;
    g_forensicIntersectSegmentHookTarget =
        intersectTarget;
    ReleaseSRWLockExclusive(&g_bindingLock);
    InterlockedExchange(
        &g_forensicCameraSocketRayEnabled, 1);

    char detail[1280]{};
    std::snprintf(
        detail, sizeof(detail),
        "camera_socket_target=GameOrig+0x%08X "
        "intersect_target=Condemned.exe+0x%08X "
        "camera_socket_vtable_slot=0x24 "
        "camera_socket_callsites=GameOrig+0x%08X,"
        "GameOrig+0x%08X "
        "target_acquire=GameOrig+0x%08X "
        "intersect_callsites=GameOrig+0x%08X,"
        "GameOrig+0x%08X "
        "gate=playing_and_weapon_type_0x%02X "
        "mapped_indices=%d:scanner,%d:digital_camera "
        "ray=Retail_arrow_preview_Camera_socket_plus_Z "
        "socket_pose_freshness_ms=%llu "
        "range=retail_preserved "
        "retail_flags_filter_result_and_classification_preserved=1 "
        "missing_or_stale_socket_pose_fallback=untouched_retail "
        "query_mutation=caller_stack_start_end_only "
        "persistent_engine_state_writes=0",
        static_cast<unsigned int>(
            kForensicCameraSocketTransformRva),
        static_cast<unsigned int>(
            kRetailIntersectSegmentThunkRva),
        static_cast<unsigned int>(
            kScannerCameraSocketTransformCallRva),
        static_cast<unsigned int>(
            kDigitalCameraSocketTransformCallRva),
        static_cast<unsigned int>(
            kForensicTargetAcquireRva),
        static_cast<unsigned int>(
            kForensicIntersectFirstReturnRva),
        static_cast<unsigned int>(
            kForensicIntersectSecondReturnRva),
        kForensicCameraToolWeaponType,
        kForensicScannerWeaponIndex,
        kForensicItemCameraWeaponIndex,
        static_cast<unsigned long long>(
            kForensicCameraSocketFreshnessMilliseconds));
    log(
        "m5_forensic_camera_socket_ray_armed",
        detail);
    return true;
}

bool ForensicObserverTargetsMatch(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const commandOn =
        base + kClientShellCommandOnRva;
    auto* const commandOff =
        base + kClientShellCommandOffRva;
    auto* const scannerUpdate =
        base + kScannerDisplayUpdateRva;
    auto* const cameraUpdate =
        base + kDigitalCameraDisplayUpdateRva;
    auto* const playerCommandOn =
        base + kPlayerManagerCommandOnRva;
    auto* const clientWeaponFire =
        base + kClientWeaponFireRva;
    auto* const playerFireCall =
        base + kPlayerManagerFireCallRva;
    auto* const collectionAction =
        base + kForensicCollectionActionRva;
    auto* const collectionActionCall =
        base + kPlayerManagerCollectionActionCallRva;
    constexpr unsigned char kCommandOnPrefix[] = {
        0x56, 0x8B, 0xF1, 0x8B, 0x06,
        0xFF, 0x50, 0x7C, 0x8B, 0xC8};
    constexpr unsigned char kCommandOnBody[] = {
        0x84, 0xC0, 0x74, 0x3E, 0x57};
    constexpr unsigned char kCommandOffPrefix[] = {
        0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x57,
        0xFF, 0x50, 0x78, 0x8B, 0x7C, 0x24, 0x0C};
    constexpr unsigned char kScannerUpdatePrefix[] = {
        0x83, 0xEC, 0x2C, 0x56, 0x8B, 0xF1,
        0x8A, 0x86, 0xD8, 0x01, 0x00, 0x00,
        0x84, 0xC0};
    constexpr unsigned char kCameraUpdatePrefix[] = {
        0x81, 0xEC, 0x0C, 0x01, 0x00, 0x00,
        0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x86,
        0x08, 0x02, 0x00, 0x00};
    constexpr unsigned char kPlayerCommandOnPrefix[] = {
        0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00,
        0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x30};
    constexpr unsigned char kClientWeaponFirePrefix[] = {
        0x53, 0x56, 0x8B, 0xF1, 0x57,
        0xC6, 0x86, 0x11, 0x02, 0x00, 0x00, 0x00};
    constexpr unsigned char kForensicCollectionActionPrefix[] = {
        0x83, 0xEC, 0x34, 0x56, 0x57, 0x8B, 0x7C,
        0x24, 0x40, 0x85, 0xFF, 0x8B, 0xF1};
    __try {
        std::uintptr_t scannerVtableUpdate = 0U;
        std::uintptr_t cameraVtableUpdate = 0U;
        std::uintptr_t playerVtableCommandOn = 0U;
        std::int32_t playerFireRelativeTarget = 0;
        std::int32_t collectionActionRelativeTarget = 0;
        std::memcpy(
            &scannerVtableUpdate,
            base + kScannerDisplayVtableRva + 0x14,
            sizeof(scannerVtableUpdate));
        std::memcpy(
            &cameraVtableUpdate,
            base + kDigitalCameraDisplayVtableRva + 0x14,
            sizeof(cameraVtableUpdate));
        std::memcpy(
            &playerVtableCommandOn,
            base + kPlayerManagerCommandOnVtableRva +
                kPlayerManagerCommandOnSlot * sizeof(void*),
            sizeof(playerVtableCommandOn));
        std::memcpy(
            &playerFireRelativeTarget, playerFireCall + 1,
            sizeof(playerFireRelativeTarget));
        std::memcpy(
            &collectionActionRelativeTarget,
            collectionActionCall + 1,
            sizeof(collectionActionRelativeTarget));
        const auto resolvedPlayerFire =
            reinterpret_cast<std::uintptr_t>(playerFireCall + 5) +
            static_cast<std::intptr_t>(
                playerFireRelativeTarget);
        const auto resolvedCollectionAction =
            reinterpret_cast<std::uintptr_t>(
                collectionActionCall + 5) +
            static_cast<std::intptr_t>(
                collectionActionRelativeTarget);
        return std::memcmp(
                   commandOn, kCommandOnPrefix,
                   sizeof(kCommandOnPrefix)) == 0 &&
               std::memcmp(
                   commandOn + 0x0F, kCommandOnBody,
                   sizeof(kCommandOnBody)) == 0 &&
               std::memcmp(
                   commandOff, kCommandOffPrefix,
                   sizeof(kCommandOffPrefix)) == 0 &&
               std::memcmp(
                   scannerUpdate, kScannerUpdatePrefix,
                   sizeof(kScannerUpdatePrefix)) == 0 &&
               std::memcmp(
                   cameraUpdate, kCameraUpdatePrefix,
                   sizeof(kCameraUpdatePrefix)) == 0 &&
               scannerVtableUpdate ==
                   reinterpret_cast<std::uintptr_t>(
                       scannerUpdate) &&
               cameraVtableUpdate ==
                   reinterpret_cast<std::uintptr_t>(
                       cameraUpdate) &&
               std::memcmp(
                   playerCommandOn, kPlayerCommandOnPrefix,
                   sizeof(kPlayerCommandOnPrefix)) == 0 &&
               playerVtableCommandOn ==
                   reinterpret_cast<std::uintptr_t>(
                       playerCommandOn) &&
               std::memcmp(
                   clientWeaponFire, kClientWeaponFirePrefix,
                   sizeof(kClientWeaponFirePrefix)) == 0 &&
               playerFireCall[0] == 0xE8 &&
               resolvedPlayerFire ==
                   reinterpret_cast<std::uintptr_t>(
                       clientWeaponFire) &&
               std::memcmp(
                   collectionAction,
                   kForensicCollectionActionPrefix,
                   sizeof(kForensicCollectionActionPrefix)) == 0 &&
               collectionActionCall[0] == 0xE8 &&
               resolvedCollectionAction ==
                   reinterpret_cast<std::uintptr_t>(
                       collectionAction);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ForensicDisplayLayoutMatches(
    HMODULE gameClientModule) noexcept {
    if (!EquippedWeaponLayoutMatches(gameClientModule)) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const factoryCall =
        base + kWeaponDisplayFactoryCallRva;
    auto* const scannerFactory =
        base + kScannerDisplayFactoryRva;
    auto* const cameraFactory =
        base + kDigitalCameraDisplayFactoryRva;
    auto* const scannerConstructor =
        base + kScannerDisplayConstructorRva;
    auto* const cameraConstructor =
        base + kDigitalCameraDisplayConstructorRva;
    constexpr unsigned char kFactoryStore[] = {
        0x83, 0xC4, 0x08, 0x89, 0x86, 0x90, 0x00, 0x00, 0x00};
    constexpr unsigned char kWrapperTail[] = {
        0x83, 0xC4, 0x04, 0x85, 0xC0, 0x74, 0x07, 0x8B, 0xC8};
    constexpr unsigned char kScannerConstructorPrefix[] = {
        0x51, 0x53, 0x55, 0x56, 0x57, 0x8B, 0xF1, 0xE8};
    constexpr unsigned char kCameraConstructorPrefix[] = {
        0x56, 0x8B, 0xF1, 0xE8};
    constexpr unsigned char kNullReturn[] = {
        0x33, 0xC0, 0xC3};
    __try {
        std::int32_t factoryRelativeTarget = 0;
        std::int32_t scannerRelativeTarget = 0;
        std::int32_t cameraRelativeTarget = 0;
        std::uint32_t scannerAllocation = 0U;
        std::uint32_t cameraAllocation = 0U;
        std::uint32_t scannerVtable = 0U;
        std::uint32_t cameraVtable = 0U;
        std::memcpy(
            &factoryRelativeTarget, factoryCall + 1,
            sizeof(factoryRelativeTarget));
        std::memcpy(
            &scannerRelativeTarget, scannerFactory + 0x14,
            sizeof(scannerRelativeTarget));
        std::memcpy(
            &cameraRelativeTarget, cameraFactory + 0x14,
            sizeof(cameraRelativeTarget));
        std::memcpy(
            &scannerAllocation, scannerFactory + 1,
            sizeof(scannerAllocation));
        std::memcpy(
            &cameraAllocation, cameraFactory + 1,
            sizeof(cameraAllocation));
        std::memcpy(
            &scannerVtable, scannerConstructor + 0x10,
            sizeof(scannerVtable));
        std::memcpy(
            &cameraVtable, cameraConstructor + 0x12,
            sizeof(cameraVtable));
        const auto resolvedFactory =
            reinterpret_cast<std::uintptr_t>(factoryCall + 5) +
            static_cast<std::intptr_t>(factoryRelativeTarget);
        const auto resolvedScannerConstructor =
            reinterpret_cast<std::uintptr_t>(scannerFactory + 0x18) +
            static_cast<std::intptr_t>(scannerRelativeTarget);
        const auto resolvedCameraConstructor =
            reinterpret_cast<std::uintptr_t>(cameraFactory + 0x18) +
            static_cast<std::intptr_t>(cameraRelativeTarget);
        return factoryCall[0] == 0xE8 &&
               resolvedFactory ==
                   reinterpret_cast<std::uintptr_t>(
                       base + kWeaponDisplayFactoryRva) &&
               std::memcmp(
                   factoryCall + 5, kFactoryStore,
                   sizeof(kFactoryStore)) == 0 &&
               scannerFactory[0] == 0x68 &&
               scannerAllocation == kScannerDisplaySize &&
               scannerFactory[5] == 0xE8 &&
               std::memcmp(
                   scannerFactory + 0x0A, kWrapperTail,
                   sizeof(kWrapperTail)) == 0 &&
               scannerFactory[0x13] == 0xE9 &&
               resolvedScannerConstructor ==
                   reinterpret_cast<std::uintptr_t>(
                       scannerConstructor) &&
               std::memcmp(
                   scannerFactory + 0x18, kNullReturn,
                   sizeof(kNullReturn)) == 0 &&
               cameraFactory[0] == 0x68 &&
               cameraAllocation == kDigitalCameraDisplaySize &&
               cameraFactory[5] == 0xE8 &&
               std::memcmp(
                   cameraFactory + 0x0A, kWrapperTail,
                   sizeof(kWrapperTail)) == 0 &&
               cameraFactory[0x13] == 0xE9 &&
               resolvedCameraConstructor ==
                   reinterpret_cast<std::uintptr_t>(
                       cameraConstructor) &&
               std::memcmp(
                   cameraFactory + 0x18, kNullReturn,
                   sizeof(kNullReturn)) == 0 &&
               std::memcmp(
                   scannerConstructor, kScannerConstructorPrefix,
                   sizeof(kScannerConstructorPrefix)) == 0 &&
               scannerConstructor[0x0E] == 0xC7 &&
               scannerConstructor[0x0F] == 0x06 &&
               scannerVtable ==
                   static_cast<std::uint32_t>(
                       reinterpret_cast<std::uintptr_t>(
                           base + kScannerDisplayVtableRva)) &&
               std::memcmp(
                   cameraConstructor, kCameraConstructorPrefix,
                   sizeof(kCameraConstructorPrefix)) == 0 &&
               cameraConstructor[0x10] == 0xC7 &&
               cameraConstructor[0x11] == 0x06 &&
               cameraVtable ==
                   static_cast<std::uint32_t>(
                       reinterpret_cast<std::uintptr_t>(
                           base + kDigitalCameraDisplayVtableRva));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InstallForensicObservers(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept {
    if (gameClientModule == nullptr || log == nullptr) {
        return false;
    }
    AcquireSRWLockShared(&g_bindingLock);
    const bool alreadyInstalled =
        g_forensicCommandOnHookTarget != nullptr &&
        g_forensicCommandOffHookTarget != nullptr &&
        g_scannerDisplayUpdateHookTarget != nullptr &&
        g_digitalCameraDisplayUpdateHookTarget != nullptr &&
        g_forensicPlayerManagerCommandOnHookTarget != nullptr &&
        g_forensicClientWeaponFireHookTarget != nullptr &&
        g_forensicCollectionActionHookTarget != nullptr &&
        g_originalForensicCommandOn != nullptr &&
        g_originalForensicCommandOff != nullptr &&
        g_originalScannerDisplayUpdate != nullptr &&
        g_originalDigitalCameraDisplayUpdate != nullptr &&
        g_originalForensicPlayerManagerCommandOn != nullptr &&
        g_originalForensicClientWeaponFire != nullptr &&
        g_originalForensicCollectionAction != nullptr;
    ReleaseSRWLockShared(&g_bindingLock);
    if (alreadyInstalled) {
        return true;
    }
    if (!ForensicObserverTargetsMatch(gameClientModule)) {
        return false;
    }

    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    void* const commandOn =
        base + kClientShellCommandOnRva;
    void* const commandOff =
        base + kClientShellCommandOffRva;
    void* const scannerUpdate =
        base + kScannerDisplayUpdateRva;
    void* const cameraUpdate =
        base + kDigitalCameraDisplayUpdateRva;
    void* const playerCommandOn =
        base + kPlayerManagerCommandOnRva;
    void* const clientWeaponFire =
        base + kClientWeaponFireRva;
    void* const collectionAction =
        base + kForensicCollectionActionRva;

    MH_STATUS status = MH_CreateHook(
        commandOn,
        reinterpret_cast<void*>(&HookForensicCommandOn),
        reinterpret_cast<void**>(
            &g_originalForensicCommandOn));
    if (status == MH_OK) {
        status = MH_CreateHook(
            commandOff,
            reinterpret_cast<void*>(&HookForensicCommandOff),
            reinterpret_cast<void**>(
                &g_originalForensicCommandOff));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            scannerUpdate,
            reinterpret_cast<void*>(&HookScannerDisplayUpdate),
            reinterpret_cast<void**>(
                &g_originalScannerDisplayUpdate));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            cameraUpdate,
            reinterpret_cast<void*>(
                &HookDigitalCameraDisplayUpdate),
            reinterpret_cast<void**>(
                &g_originalDigitalCameraDisplayUpdate));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            playerCommandOn,
            reinterpret_cast<void*>(
                &HookForensicPlayerManagerCommandOn),
            reinterpret_cast<void**>(
                &g_originalForensicPlayerManagerCommandOn));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            clientWeaponFire,
            reinterpret_cast<void*>(
                &HookForensicClientWeaponFire),
            reinterpret_cast<void**>(
                &g_originalForensicClientWeaponFire));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            collectionAction,
            reinterpret_cast<void*>(
                &HookForensicCollectionAction),
            reinterpret_cast<void**>(
                &g_originalForensicCollectionAction));
    }
    if (status == MH_OK) {
        status = MH_EnableHook(commandOn);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(commandOff);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(scannerUpdate);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(cameraUpdate);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(playerCommandOn);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(clientWeaponFire);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(collectionAction);
    }
    if (status != MH_OK) {
        MH_DisableHook(commandOn);
        MH_DisableHook(commandOff);
        MH_DisableHook(scannerUpdate);
        MH_DisableHook(cameraUpdate);
        MH_DisableHook(playerCommandOn);
        MH_DisableHook(clientWeaponFire);
        MH_DisableHook(collectionAction);
        MH_RemoveHook(commandOn);
        MH_RemoveHook(commandOff);
        MH_RemoveHook(scannerUpdate);
        MH_RemoveHook(cameraUpdate);
        MH_RemoveHook(playerCommandOn);
        MH_RemoveHook(clientWeaponFire);
        MH_RemoveHook(collectionAction);
        g_originalForensicCommandOn = nullptr;
        g_originalForensicCommandOff = nullptr;
        g_originalScannerDisplayUpdate = nullptr;
        g_originalDigitalCameraDisplayUpdate = nullptr;
        g_originalForensicPlayerManagerCommandOn = nullptr;
        g_originalForensicClientWeaponFire = nullptr;
        g_originalForensicCollectionAction = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_forensicCommandOnHookTarget = commandOn;
    g_forensicCommandOffHookTarget = commandOff;
    g_scannerDisplayUpdateHookTarget = scannerUpdate;
    g_digitalCameraDisplayUpdateHookTarget = cameraUpdate;
    g_forensicPlayerManagerCommandOnHookTarget =
        playerCommandOn;
    g_forensicClientWeaponFireHookTarget =
        clientWeaponFire;
    g_forensicCollectionActionHookTarget =
        collectionAction;
    ReleaseSRWLockExclusive(&g_bindingLock);

    char detail[768]{};
    std::snprintf(
        detail, sizeof(detail),
        "gameorig_base=0x%08lX "
        "command_on=GameOrig+0x%08X "
        "command_off=GameOrig+0x%08X "
        "scanner_update=GameOrig+0x%08X "
        "digital_camera_update=GameOrig+0x%08X "
        "player_command_on=GameOrig+0x%08X "
        "client_weapon_fire=GameOrig+0x%08X "
        "collection_action=GameOrig+0x%08X "
        "state_offsets=scanner+0x%03X,camera+0x%03X "
        "state_bytes=scanner:6,camera:4 "
        "scanner_semantics=+1DB_target_hit,+1DC_framing_ok,"
        "+1DD_can_photo engine_writes=0",
        static_cast<unsigned long>(
            reinterpret_cast<std::uintptr_t>(base)),
        static_cast<unsigned int>(kClientShellCommandOnRva),
        static_cast<unsigned int>(kClientShellCommandOffRva),
        static_cast<unsigned int>(kScannerDisplayUpdateRva),
        static_cast<unsigned int>(
            kDigitalCameraDisplayUpdateRva),
        static_cast<unsigned int>(kPlayerManagerCommandOnRva),
        static_cast<unsigned int>(kClientWeaponFireRva),
        static_cast<unsigned int>(kForensicCollectionActionRva),
        static_cast<unsigned int>(kScannerDisplayStateOffset),
        static_cast<unsigned int>(
            kDigitalCameraDisplayStateOffset));
    log("m5_forensic_observers_armed", detail);
    return true;
}

bool MenuTargetsMatch(
    HMODULE gameClientModule,
    void* clientShell) noexcept {
    if (gameClientModule == nullptr || clientShell == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    void** vtable = nullptr;
    __try {
        vtable = *static_cast<void***>(clientShell);
        if (vtable != reinterpret_cast<void**>(
                base + kClientShellVtableRva) ||
            vtable[kClientShellKeyUpSlot] !=
                base + kClientShellKeyUpRva ||
            vtable[kClientShellKeyDownSlot] !=
                base + kClientShellKeyDownRva ||
            vtable[kClientShellUpdateSlot] !=
                base + kClientShellUpdateRva ||
            vtable[kClientShellGetInterfaceManagerSlot] !=
                base + kClientShellGetInterfaceManagerRva) {
            return false;
        }
        auto* const stateRead =
            base + kClientShellKeyUpStateReadRva;
        std::uint32_t singletonOperand = 0;
        std::memcpy(
            &singletonOperand, stateRead + 1,
            sizeof(singletonOperand));
        const auto expectedSingletonOperand =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(
                    base + kInterfaceManagerSingletonRva));
        return IsExecutableModuleAddress(
                   vtable[kClientShellKeyUpSlot], gameClientModule) &&
               IsExecutableModuleAddress(
                   vtable[kClientShellKeyDownSlot], gameClientModule) &&
               IsExecutableModuleAddress(
                   vtable[kClientShellUpdateSlot], gameClientModule) &&
               IsExecutableModuleAddress(
                   vtable[kClientShellGetInterfaceManagerSlot],
                   gameClientModule) &&
               std::memcmp(
                   vtable[kClientShellKeyUpSlot],
                   kClientShellKeyUpPrefix,
                   sizeof(kClientShellKeyUpPrefix)) == 0 &&
               std::memcmp(
                   vtable[kClientShellKeyDownSlot],
                   kClientShellKeyDownPrefix,
                   sizeof(kClientShellKeyDownPrefix)) == 0 &&
               std::memcmp(
                   vtable[kClientShellUpdateSlot],
                   kClientShellUpdatePrefix,
                   sizeof(kClientShellUpdatePrefix)) == 0 &&
               std::memcmp(
                   static_cast<unsigned char*>(
                       vtable[kClientShellUpdateSlot]) + 0x0E,
                   kClientShellUpdateBody,
                   sizeof(kClientShellUpdateBody)) == 0 &&
               std::memcmp(
                   static_cast<unsigned char*>(
                       vtable[kClientShellUpdateSlot]) + 0x49,
                   kClientShellBindingUpdateSequence,
                   sizeof(kClientShellBindingUpdateSequence)) == 0 &&
               std::memcmp(
                   vtable[kClientShellGetInterfaceManagerSlot],
                   kClientShellGetInterfaceManagerBody,
                   sizeof(kClientShellGetInterfaceManagerBody)) == 0 &&
               stateRead[0] == 0xA1 &&
               singletonOperand == expectedSingletonOperand &&
               std::memcmp(
                   stateRead + 5,
                   kClientShellKeyUpStateReadTail,
                   sizeof(kClientShellKeyUpStateReadTail)) == 0 &&
               std::memcmp(
                   stateRead + 0x0F,
                   kClientShellKeyUpPlayingCompare,
                   sizeof(kClientShellKeyUpPlayingCompare)) == 0 &&
               std::memcmp(
                   stateRead + 0x14,
                   kClientShellKeyUpScreenCompare,
                   sizeof(kClientShellKeyUpScreenCompare)) == 0 &&
               std::memcmp(
                   stateRead + 0x19,
                   kClientShellKeyUpMenuCompare,
                   sizeof(kClientShellKeyUpMenuCompare)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ResolveVerifiedInterfaceManager(
    HMODULE gameClientModule,
    void* clientShell) noexcept {
    if (gameClientModule == nullptr || clientShell == nullptr) {
        return nullptr;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    __try {
        void** const vtable = *static_cast<void***>(clientShell);
        const auto getInterfaceManager =
            reinterpret_cast<ClientShellGetInterfaceManagerFunction>(
                vtable[kClientShellGetInterfaceManagerSlot]);
        void* const interfaceManager =
            getInterfaceManager(clientShell);
        if (interfaceManager !=
                static_cast<unsigned char*>(clientShell) + 8 ||
            *static_cast<void***>(interfaceManager) !=
                reinterpret_cast<void**>(
                    base + kInterfaceManagerVtableRva) ||
            *reinterpret_cast<void**>(
                base + kInterfaceManagerSingletonRva) !=
                interfaceManager) {
            return nullptr;
        }
        return interfaceManager;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool EnsureBindingValueHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    const char* rejectionEvent) noexcept {
    if (gameClientModule == nullptr || bridgeModule == nullptr ||
        log == nullptr || rejectionEvent == nullptr) {
        return false;
    }
    const auto getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_GetInputState"));
    auto* const target =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kGetBindingValueRva;
    if (getInputState == nullptr) {
        log(
            rejectionEvent,
            "controller_transport_export_missing");
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    const bool alreadyInstalled =
        g_bindingValueHookTarget == target &&
        g_originalGetBindingValue != nullptr;
    ReleaseSRWLockExclusive(&g_bindingLock);
    if (alreadyInstalled) {
        g_getInputState = getInputState;
        g_log = log;
        return true;
    }
    if (!LocomotionTargetMatches(target)) {
        log(
            rejectionEvent,
            "GameOrig_rva_000095f0_signature_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(rejectionEvent, MH_StatusToString(initialize));
        return false;
    }

    g_getInputState = getInputState;
    g_log = log;
    MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookGetBindingValue),
        reinterpret_cast<void**>(&g_originalGetBindingValue));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log(rejectionEvent, MH_StatusToString(status));
        g_originalGetBindingValue = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_bindingValueHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    return true;
}

class ScopedPlayerColliderHookInstallLock {
public:
    ScopedPlayerColliderHookInstallLock() noexcept {
        AcquireSRWLockExclusive(
            &g_playerColliderHookInstallLock);
    }

    ~ScopedPlayerColliderHookInstallLock() noexcept {
        ReleaseSRWLockExclusive(
            &g_playerColliderHookInstallLock);
    }

    ScopedPlayerColliderHookInstallLock(
        const ScopedPlayerColliderHookInstallLock&) = delete;
    ScopedPlayerColliderHookInstallLock& operator=(
        const ScopedPlayerColliderHookInstallLock&) = delete;
};

bool EnsurePlayerColliderHook(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept {
    ScopedPlayerColliderHookInstallLock installLock;
    (void)installLock;
    if (gameClientModule == nullptr || log == nullptr) {
        InterlockedExchange(
            &g_playerColliderManagerHookOperational, 0);
        return false;
    }
    if (InterlockedCompareExchange(
            &g_playerColliderManagerInstallPoisoned, 0, 0) != 0) {
        InterlockedExchange(
            &g_playerColliderManagerHookOperational, 0);
        log(
            "m5_player_collider_rejected",
            "reason=prior_owned_hook_rollback_uncertain "
            "retry_suppressed=1 trampoline_retained=1");
        return false;
    }
    auto* const base =
        reinterpret_cast<unsigned char*>(gameClientModule);
    auto* const target = base + kPlayerSetDimensionsRva;

    AcquireSRWLockExclusive(&g_bindingLock);
    const bool alreadyInstalled =
        g_playerSetDimensionsHookTarget == target &&
        g_originalPlayerSetDimensions != nullptr &&
        InterlockedCompareExchange(
            &g_playerColliderManagerHookOperational, 0, 0) != 0;
    ReleaseSRWLockExclusive(&g_bindingLock);
    if (alreadyInstalled) {
        g_gameClientBase = base;
        g_log = log;
        return true;
    }
    InterlockedExchange(
        &g_playerColliderManagerHookOperational, 0);
    if (!PlayerSetDimensionsTargetMatches(
            gameClientModule, target)) {
        log(
            "m5_player_collider_rejected",
            "GameOrig_rva_00031ba0_signature_mismatch "
            "runtime_mutation=0");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(
            "m5_player_collider_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    g_gameClientBase = base;
    g_log = log;
    PlayerSetDimensionsFunction trampoline = nullptr;
    MH_STATUS status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookPlayerSetDimensions),
        reinterpret_cast<void**>(&trampoline));
    if (status != MH_OK) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "step=create status=%s hook_installed=0 "
            "owned_hook_removed=0 runtime_mutation=0",
            MH_StatusToString(status));
        log("m5_player_collider_rejected", detail);
        return false;
    }

    g_originalPlayerSetDimensions = trampoline;
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        InterlockedExchange(
            &g_playerColliderManagerHookOperational, 0);
        const MH_STATUS removeStatus = MH_RemoveHook(target);
        const bool removed = removeStatus == MH_OK;
        if (removed) {
            g_originalPlayerSetDimensions = nullptr;
        } else {
            InterlockedExchange(
                &g_playerColliderManagerInstallPoisoned, 1);
        }
        AcquireSRWLockExclusive(&g_playerColliderLock);
        g_playerColliderTelemetry.hookReady = false;
        ReleaseSRWLockExclusive(&g_playerColliderLock);
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "step=enable status=%s hook_installed=%s "
            "remove_status=%s owned_hook_removed=%u "
            "hook_state_unknown=%u trampoline_retained=%u "
            "runtime_mutation=%s",
            MH_StatusToString(status),
            removed ? "0" : "unknown",
            MH_StatusToString(removeStatus),
            removed ? 1U : 0U,
            removed ? 0U : 1U,
            removed ? 0U : 1U,
            removed ? "0" : "unknown");
        log("m5_player_collider_rejected", detail);
        return false;
    }

    InterlockedExchange(
        &g_playerColliderManagerHookOperational, 1);
    AcquireSRWLockExclusive(&g_bindingLock);
    g_playerSetDimensionsHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    AcquireSRWLockExclusive(&g_playerColliderLock);
    g_playerColliderTelemetry.hookReady = true;
    g_playerColliderTelemetry.widthScale =
        CurrentPlayerColliderSettings().widthScale;
    ReleaseSRWLockExclusive(&g_playerColliderLock);
    InterlockedExchange(
        &g_playerColliderReapplyPending, 1);
    log(
        "m5_player_collider_armed",
        "target=GameOrig+0x00031BA0 "
        "manager=GameOrig+0x00168EEC player_object_offset=0x10 "
        "physics=GameOrig+0x00172EC4 "
        "GetObjectDims=slot8 SetObjectDims=slot9 "
        "scope=local_player_only axes=x,z y=retail_preserved "
        "scale_range=0.10-1.00 default=1.00 "
        "push_objects=1 enemy_objects_changed=0 "
        "signature_and_module_identity_gated=1 "
        "fail_closed_operational_gate=1");
    log(
        "m5_player_collider_drift_probe_armed",
        "phases=pre_retail_update,post_retail_update,"
        "post_mod_setdims_attempt,post_pending_noop "
        "post_retail_before_pending_process=1 "
        "boundary_emission=initial_plus_change "
        "post_pending_emission=forced "
        "boundary_event_cap=128 post_pending_event_cap=32 "
        "local_player_only=1 "
        "manager_requested_offset=0x1C "
        "manager_40c_source_candidate=0x40C "
        "adjacent_dimensions_candidate=0x418 mutation=none");
    return true;
}


bool EnsurePlayerColliderWriterTrace(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept {
    ScopedPlayerColliderHookInstallLock installLock;
    (void)installLock;
    if (gameClientModule == nullptr || log == nullptr) {
        InterlockedExchange(
            &g_playerColliderWriterHookOperational, 0);
        return false;
    }
    if (InterlockedCompareExchange(
            &g_playerColliderWriterInstallPoisoned, 0, 0) != 0) {
        InterlockedExchange(
            &g_playerColliderWriterHookOperational, 0);
        log(
            "m5_player_collider_writer_trace_rejected",
            "reason=prior_owned_hook_rollback_uncertain "
            "retry_suppressed=1 trampoline_retained=1");
        return false;
    }
    HMODULE const executableModule = GetModuleHandleW(nullptr);
    if (executableModule == nullptr) {
        InterlockedExchange(
            &g_playerColliderWriterHookOperational, 0);
        log(
            "m5_player_collider_writer_trace_rejected",
            "reason=executable_module_missing hook_installed=0 "
            "runtime_mutation=0");
        return false;
    }
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executableModule);
    auto* const target =
        executableBase + kSetObjectDimensionsExecutableRva;

    AcquireSRWLockShared(&g_bindingLock);
    const bool managerHookReady =
        g_playerSetDimensionsHookTarget ==
            reinterpret_cast<unsigned char*>(
                gameClientModule) +
                kPlayerSetDimensionsRva &&
        g_originalPlayerSetDimensions != nullptr &&
        InterlockedCompareExchange(
            &g_playerColliderManagerHookOperational, 0, 0) != 0;
    const bool alreadyInstalled =
        g_playerSetObjectDimensionsTraceHookTarget == target &&
        g_originalPlayerSetObjectDimensionsTrace != nullptr &&
        g_playerColliderTraceExecutable == executableModule &&
        InterlockedCompareExchange(
            &g_playerColliderWriterHookOperational, 0, 0) != 0;
    ReleaseSRWLockShared(&g_bindingLock);
    if (alreadyInstalled) {
        g_log = log;
        return true;
    }
    InterlockedExchange(
        &g_playerColliderWriterHookOperational, 0);
    if (!managerHookReady) {
        log(
            "m5_player_collider_writer_trace_rejected",
            "reason=player_collider_manager_hook_unavailable "
            "hook_installed=0 runtime_mutation=0");
        return false;
    }

    wchar_t executablePath[MAX_PATH]{};
    const DWORD executablePathLength = GetModuleFileNameW(
        nullptr, executablePath, MAX_PATH);
    if (executablePathLength == 0U ||
        executablePathLength >= MAX_PATH) {
        log(
            "m5_player_collider_writer_trace_rejected",
            "reason=executable_path_unavailable hook_installed=0 "
            "runtime_mutation=0");
        return false;
    }
    const ModuleIdentityResult executableIdentity =
        VerifyCondemnedExecutable(executablePath);
    if (executableIdentity != ModuleIdentityResult::ok) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "reason=executable_identity_%s hook_installed=0 "
            "runtime_mutation=0",
            ModuleIdentityResultName(executableIdentity));
        log(
            "m5_player_collider_writer_trace_rejected",
            detail);
        return false;
    }
    if (!PlayerColliderWriterTraceTargetsMatch(
            gameClientModule, executableModule, target)) {
        log(
            "m5_player_collider_writer_trace_rejected",
            "reason=executable_vtable_or_callsite_signature_mismatch "
            "hook_installed=0 runtime_mutation=0");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "step=initialize status=%s hook_installed=0 "
            "runtime_mutation=0",
            MH_StatusToString(initialize));
        log(
            "m5_player_collider_writer_trace_rejected",
            detail);
        return false;
    }

    g_originalPlayerSetObjectDimensionsTrace = nullptr;
    MH_STATUS status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(
            &HookPlayerSetObjectDimensionsTrace),
        reinterpret_cast<void**>(
            &g_originalPlayerSetObjectDimensionsTrace));
    if (status != MH_OK) {
        g_originalPlayerSetObjectDimensionsTrace = nullptr;
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "step=create status=%s hook_installed=0 "
            "owned_hook_removed=0 runtime_mutation=0",
            MH_StatusToString(status));
        log(
            "m5_player_collider_writer_trace_rejected",
            detail);
        return false;
    }

    g_playerColliderTraceExecutable = executableModule;
    InterlockedExchange(
        &g_playerColliderWriterKnownEvents, 0);
    InterlockedExchange(
        &g_playerColliderWriterUnknownGameEvents, 0);
    InterlockedExchange(
        &g_playerColliderWriterExecutableEvents, 0);
    InterlockedExchange(
        &g_playerColliderWriterExternalEvents, 0);
    InterlockedExchange(
        &g_playerColliderWriterUnresolvedEvents, 0);
    InterlockedExchange64(
        &g_playerColliderWriterNextSequence, 0);
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        InterlockedExchange(
            &g_playerColliderWriterHookOperational, 0);
        const MH_STATUS removeStatus =
            MH_RemoveHook(target);
        const bool removed = removeStatus == MH_OK;
        if (removed) {
            g_originalPlayerSetObjectDimensionsTrace = nullptr;
            g_playerColliderTraceExecutable = nullptr;
        } else {
            InterlockedExchange(
                &g_playerColliderWriterInstallPoisoned, 1);
        }
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "step=enable status=%s hook_installed=%s "
            "remove_status=%s owned_hook_removed=%u "
            "hook_state_unknown=%u trampoline_retained=%u "
            "runtime_mutation=%s",
            MH_StatusToString(status),
            removed ? "0" : "unknown",
            MH_StatusToString(removeStatus),
            removed ? 1U : 0U,
            removed ? 0U : 1U,
            removed ? 0U : 1U,
            removed ? "0" : "unknown");
        log(
            "m5_player_collider_writer_trace_rejected",
            detail);
        return false;
    }

    InterlockedExchange(
        &g_playerColliderWriterHookOperational, 1);
    AcquireSRWLockExclusive(&g_bindingLock);
    g_playerSetObjectDimensionsTraceHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    log(
        "m5_player_collider_writer_trace_armed",
        "target=Condemned+0x00007FD0 "
        "exe_identity=sha256_verified "
        "physics_vtable=Condemned+0x0014ADE0 "
        "GetObjectDims=Condemned+0x00064530 "
        "SetObjectDims=Condemned+0x00007FD0 "
        "known_returns=GameOrig+0x00031BFC,0x00031C16,"
        "0x00031D68,0x00031D86,0x000346BF,0x0003476F,"
        "0x0003478A "
        "known_event_cap=64 unknown_gameorig_event_cap=64 "
        "executable_local_event_cap=64 external_local_event_cap=32 "
        "unresolved_local_event_cap=32 "
        "detour_scope=all_setobjectdims_calls "
        "telemetry_scope=exact_local_player_reduced_or_pending "
        "playing_only=1 nonlocal_forwarded_without_observation=1 "
        "request_pointer_forwarded_unchanged=1 "
        "flags_forwarded_unchanged=1 native_result_preserved=1 "
        "observer_added_engine_state_writes=0 "
        "observer_setdims_calls_added=0 "
        "fail_closed_operational_gate=1");
    return true;
}

bool EnsurePlayerCollisionVelocityTrace(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept {
    ScopedPlayerColliderHookInstallLock installLock;
    (void)installLock;
    InterlockedExchange(
        &g_playerCollisionVelocityHookOperational, 0);
    if (gameClientModule == nullptr || log == nullptr ||
        InterlockedCompareExchange(
            &g_playerCollisionVelocityInstallPoisoned, 0, 0) != 0) {
        return false;
    }
    HMODULE const executableModule = GetModuleHandleW(nullptr);
    auto* const executableBase =
        reinterpret_cast<unsigned char*>(executableModule);
    auto* const target = executableBase != nullptr
        ? executableBase + kSetVelocityExecutableRva
        : nullptr;
    AcquireSRWLockShared(&g_bindingLock);
    const bool alreadyInstalled =
        g_playerCollisionSetVelocityHookTarget == target &&
        g_originalPlayerCollisionSetVelocity != nullptr;
    ReleaseSRWLockShared(&g_bindingLock);
    if (alreadyInstalled) {
        InterlockedExchange(
            &g_playerCollisionVelocityHookOperational, 1);
        return true;
    }

    wchar_t executablePath[MAX_PATH]{};
    const DWORD pathLength = GetModuleFileNameW(
        nullptr, executablePath, MAX_PATH);
    if (pathLength == 0U || pathLength >= MAX_PATH) {
        log("m5_player_collision_xray_rejected",
            "reason=executable_path_unavailable hook_installed=0 runtime_mutation=0");
        return false;
    }
    const ModuleIdentityResult identity =
        VerifyCondemnedExecutable(executablePath);
    if (identity != ModuleIdentityResult::ok ||
        !PlayerCollisionVelocityTraceTargetMatches(
            executableModule, target)) {
        char detail[320]{};
        std::snprintf(
            detail, sizeof(detail),
            "reason=%s hook_installed=0 runtime_mutation=0",
            identity != ModuleIdentityResult::ok
                ? ModuleIdentityResultName(identity)
                : "executable_vtable_or_signature_mismatch");
        log("m5_player_collision_xray_rejected", detail);
        return false;
    }
    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log("m5_player_collision_xray_rejected",
            "reason=minhook_initialize hook_installed=0 runtime_mutation=0");
        return false;
    }
    g_originalPlayerCollisionSetVelocity = nullptr;
    MH_STATUS status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookPlayerCollisionSetVelocity),
        reinterpret_cast<void**>(
            &g_originalPlayerCollisionSetVelocity));
    if (status != MH_OK) {
        g_originalPlayerCollisionSetVelocity = nullptr;
        log("m5_player_collision_xray_rejected",
            "reason=minhook_create hook_installed=0 runtime_mutation=0");
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        const MH_STATUS removeStatus = MH_RemoveHook(target);
        if (removeStatus == MH_OK) {
            g_originalPlayerCollisionSetVelocity = nullptr;
        } else {
            InterlockedExchange(
                &g_playerCollisionVelocityInstallPoisoned, 1);
        }
        log("m5_player_collision_xray_rejected",
            removeStatus == MH_OK
                ? "reason=minhook_enable owned_hook_removed=1 runtime_mutation=0"
                : "reason=minhook_enable rollback_uncertain=1 runtime_mutation=unknown");
        return false;
    }
    AcquireSRWLockExclusive(&g_bindingLock);
    g_playerCollisionSetVelocityHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    InterlockedExchange(&g_playerCollisionVelocityEvents, 0);
    InterlockedExchange(&g_playerCollisionUpdateEvents, 0);
    InterlockedExchange64(&g_playerCollisionTimelineSequence, 0);
    InterlockedExchange(
        &g_playerCollisionVelocityHookOperational, 1);
    log(
        "m5_player_collision_xray_armed",
        "SetVelocity=Condemned+0x00007CD0 "
        "physics_vtable=Condemned+0x0014ADE0 slot=11 "
        "exe_identity=sha256_verified exact_local_player_only=1 "
        "playing_foreground_freshness_gated=1 "
        "movement_semantics=velocity_handoff_not_collision_result "
        "dimensions_and_origins=read_only "
        "render_geometry=diagnostic_proxy "
        "true_physics_geometry_verified=0 enemy_mutation=0 "
        "observer_engine_state_writes=0 native_call_count=1 "
        "fallback=retail fail_closed_operational_gate=1");
    return true;
}

} // namespace
bool ConfigurePlayerColliderSettings(
    const PlayerColliderSettings& settings) noexcept {
    if (!PlayerColliderSettingsAreValid(settings)) {
        return false;
    }
    const LONG basisPoints = static_cast<LONG>(
        std::lround(settings.widthScale * 10000.0F));
    const LONG previous = InterlockedExchange(
        &g_playerColliderScaleBasisPoints, basisPoints);
    if (previous != basisPoints) {
        InterlockedExchange(
            &g_playerColliderReapplyPending, 1);
    }
    AcquireSRWLockExclusive(&g_playerColliderLock);
    g_playerColliderTelemetry.widthScale =
        static_cast<float>(basisPoints) / 10000.0F;
    g_playerColliderTelemetry.reapplyPending =
        InterlockedCompareExchange(
            &g_playerColliderReapplyPending, 0, 0) != 0;
    ReleaseSRWLockExclusive(&g_playerColliderLock);
    return true;
}

PlayerColliderSettings ReadPlayerColliderSettings() noexcept {
    return CurrentPlayerColliderSettings();
}

void ReadPlayerColliderTelemetry(
    PlayerColliderTelemetry& telemetry) noexcept {
    AcquireSRWLockShared(&g_playerColliderLock);
    telemetry = g_playerColliderTelemetry;
    ReleaseSRWLockShared(&g_playerColliderLock);
    telemetry.widthScale =
        CurrentPlayerColliderSettings().widthScale;
    telemetry.reapplyPending =
        InterlockedCompareExchange(
            &g_playerColliderReapplyPending, 0, 0) != 0;
}

void SetPlayerCollisionXrayEnabled(bool enabled) noexcept {
    InterlockedExchange(
        &g_playerCollisionXrayEnabled, enabled ? 1 : 0);
    AcquireSRWLockExclusive(&g_playerCollisionXrayLock);
    g_playerCollisionXraySnapshot = {};
    g_playerCollisionXraySnapshot.enabled = enabled;
    g_playerCollisionXraySnapshot.movementTraceReady =
        InterlockedCompareExchange(
            &g_playerCollisionVelocityHookOperational, 0, 0) != 0;
    g_playerCollisionXrayPreUpdate = {};
    g_playerCollisionXrayTargetObject = 0U;
    g_playerCollisionXrayContactPoint = {};
    g_playerCollisionXrayContactValid = false;
    g_playerCollisionXrayTargetTick = 0U;
    g_playerCollisionXrayLastUpdateLogTick = 0U;
    ReleaseSRWLockExclusive(&g_playerCollisionXrayLock);
    InterlockedExchange(&g_playerCollisionVelocityEvents, 0);
    InterlockedExchange(&g_playerCollisionUpdateEvents, 0);
    if (g_log != nullptr) {
        g_log(
            "m5_player_collision_xray_state",
            enabled
                ? "enabled=1 persistence=session_only mutation=none geometry=diagnostic_proxy true_physics_geometry_verified=0"
                : "enabled=0 persistence=session_only mutation=none");
    }
}

bool PlayerCollisionXrayEnabled() noexcept {
    return InterlockedCompareExchange(
        &g_playerCollisionXrayEnabled, 0, 0) != 0;
}

bool ReadPlayerCollisionXraySnapshot(
    PlayerCollisionXraySnapshot& snapshot) noexcept {
    AcquireSRWLockShared(&g_playerCollisionXrayLock);
    snapshot = g_playerCollisionXraySnapshot;
    ReleaseSRWLockShared(&g_playerCollisionXrayLock);
    snapshot.enabled = PlayerCollisionXrayEnabled();
    snapshot.movementTraceReady = InterlockedCompareExchange(
        &g_playerCollisionVelocityHookOperational, 0, 0) != 0;
    return snapshot.enabled && snapshot.playerValid &&
        snapshot.tickMilliseconds != 0U &&
        GetTickCount64() >= snapshot.tickMilliseconds &&
        GetTickCount64() - snapshot.tickMilliseconds <=
            kInputFreshnessMilliseconds &&
        ProcessOwnsForegroundWindow();
}


bool InstallBindingLocomotionHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (!EnsureBindingValueHook(
            gameClientModule, bridgeModule, log,
            "m4_binding_locomotion_rejected")) {
        return false;
    }
    const bool playerColliderReady = EnsurePlayerColliderHook(
        gameClientModule, log);
    if (playerColliderReady) {
        (void)EnsurePlayerColliderWriterTrace(
            gameClientModule, log);
        (void)EnsurePlayerCollisionVelocityTrace(
            gameClientModule, log);
    }
    InterlockedExchange(&g_locomotionEnabled, 1);
    log(
        "m4_binding_locomotion_armed",
        "target=GameOrig+0x000095F0 commands=0,1,3,4 "
        "binding_size=60 direct_command_writes=0 system_input=0");
    return true;
}

bool InstallBindingInteractionHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    if (clientShell == nullptr ||
        !MenuTargetsMatch(gameClientModule, clientShell)) {
        log(
            "m4_binding_interaction_rejected",
            clientShell == nullptr
                ? "IClientShell_Default_v4_missing"
                : "IClientShell_Default_v4_state_guard_mismatch");
        return false;
    }
    void* const interfaceManager = ResolveVerifiedInterfaceManager(
        gameClientModule, clientShell);
    if (interfaceManager == nullptr) {
        log(
            "m4_binding_interaction_rejected",
            "CInterfaceMgr_state_source_mismatch");
        return false;
    }
    if (!EnsureBindingValueHook(
            gameClientModule, bridgeModule, log,
            "m4_binding_interaction_rejected")) {
        return false;
    }

    g_interfaceManager = interfaceManager;
    InterlockedExchange(&g_interactionEnabled, 1);
    log(
        "m4_binding_interaction_armed",
        "target=GameOrig+0x000095F0 command=87 "
        "button=right_squeeze threshold=0.65 "
        "state=playing path=retail_binding_value "
        "binding_size=60 direct_command_writes=0 system_input=0");
    return true;
}

bool InstallBindingCoreActionsHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    bool forensicMemoryProbe) noexcept {
    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    if (clientShell == nullptr ||
        !MenuTargetsMatch(gameClientModule, clientShell)) {
        log(
            "m4_binding_core_actions_rejected",
            clientShell == nullptr
                ? "IClientShell_Default_v4_missing"
                : "IClientShell_Default_v4_state_guard_mismatch");
        return false;
    }
    void* const interfaceManager = ResolveVerifiedInterfaceManager(
        gameClientModule, clientShell);
    if (interfaceManager == nullptr) {
        log(
            "m4_binding_core_actions_rejected",
            "CInterfaceMgr_state_source_mismatch");
        return false;
    }
    if (!EnsureBindingValueHook(
            gameClientModule, bridgeModule, log,
            "m4_binding_core_actions_rejected")) {
        return false;
    }

    g_interfaceManager = interfaceManager;
    g_gameClientBase = reinterpret_cast<unsigned char*>(
        gameClientModule);
    (void)InstallForensicCameraSocketRay(
        gameClientModule, log);
    for (auto& state : g_lastCoreActionActive) {
        InterlockedExchange(&state, 0);
    }
    InterlockedExchange(&g_coreActionsEnabled, 1);
    log(
        "m4_binding_core_actions_armed",
        "target=GameOrig+0x000095F0 "
        "commands=16,17,28,60,61,62,114,116 "
        "controls=left_squeeze,right_trigger,left_trigger,"
        "right_primary,right_secondary,left_stick,"
        "left_primary,right_stick_up state=playing "
        "path=retail_binding_value "
        "binding_size=60 direct_command_writes=0 system_input=0");
    if (forensicMemoryProbe) {
        if (!ForensicDisplayLayoutMatches(gameClientModule) ||
            !ForensicObserverTargetsMatch(gameClientModule)) {
            InterlockedExchange(&g_forensicMemoryProbeEnabled, 0);
            log(
                "m5_forensic_memory_probe_rejected",
                "GameOrig_forensic_layout_or_observer_signature_mismatch "
                "engine_writes=0");
            return true;
        }
        g_gameClientBase = reinterpret_cast<unsigned char*>(
            gameClientModule);
        InterlockedExchangePointer(
            &g_forensicScannerDisplay, nullptr);
        InterlockedExchangePointer(
            &g_forensicDigitalCameraDisplay, nullptr);
        InterlockedExchange64(&g_forensicScannerState, -1);
        InterlockedExchange64(&g_forensicDigitalCameraState, -1);
        InterlockedExchange(&g_forensicScannerStateEvents, 0);
        InterlockedExchange(
            &g_forensicDigitalCameraStateEvents, 0);
        for (auto& state : g_forensicMemoryCommandActive) {
            InterlockedExchange(&state, 0);
        }
        InterlockedExchange64(&g_forensicMemoryNextTraceId, 0);
        AcquireSRWLockExclusive(&g_forensicMemoryLock);
        g_forensicMemoryTraceState = {};
        ReleaseSRWLockExclusive(&g_forensicMemoryLock);
        InterlockedExchange(&g_forensicMemoryProbeEnabled, 1);
        if (!InstallForensicObservers(
                gameClientModule, log)) {
            InterlockedExchange(
                &g_forensicMemoryProbeEnabled, 0);
            log(
                "m5_forensic_memory_probe_rejected",
                "forensic_observer_hook_install_failed "
                "engine_writes=0");
            return true;
        }
        log(
            "m5_forensic_memory_probe_armed",
            "commands=116,17,87 command_names=tools,fire,activate "
            "triggers=retail_binding_edge,ClientShell_OnCommand "
            "samples_after_retail_frames=0,1,2,4,8,16,32,64,128,256 "
            "weapon_root=GameOrig+0x00168EBC "
            "weapon_manager_fields=index+0x08,current+0x0C "
            "legacy_display_field=weapon+0x90 "
            "weapon_span=+0x080:0x080 "
            "live_display_sources=scanner_update,digital_camera_update "
            "live_state_offsets=scanner+0x1D8,camera+0x208 "
            "state_flag_semantics=unclassified "
            "engine_writes=0");
    }
    return true;
}

bool InstallControllerHaptics(
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    if (InterlockedCompareExchange(
            &g_coreActionsEnabled, 0, 0) == 0 &&
        InterlockedCompareExchange(
            &g_interactionEnabled, 0, 0) == 0) {
        log(
            "m4_controller_haptics_rejected",
            "core_action_or_interaction_gate_required");
        return false;
    }
    const auto submit =
        reinterpret_cast<SubmitHapticRequestFunction>(
            GetProcAddress(
                bridgeModule,
                "CondemnedVr_SubmitHapticRequest"));
    if (submit == nullptr) {
        log(
            "m4_controller_haptics_rejected",
            "haptic_transport_export_missing");
        return false;
    }

    g_submitHapticRequest = submit;
    InterlockedExchange64(&g_hapticRequestId, 0);
    InterlockedExchange(&g_hapticFailureReported, 0);
    InterlockedExchange(&g_hapticsEnabled, 1);
    log(
        "m4_controller_haptics_armed",
        "commands=17,28,87 edge=rising "
        "pulses=fire_right_35ms_0.25,block_left_25ms_0.18,"
        "activate_right_20ms_0.15 transport=openxr_haptic "
        "weapon_event_haptics=0");
    return true;
}

bool InstallBindingTurningHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (g_turningHookTarget != nullptr) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return true;
    }
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (gameClientModule == nullptr || bridgeModule == nullptr ||
        log == nullptr) {
        return false;
    }
    const auto getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_GetInputState"));
    auto* const target =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kGetExtremalCommandValueRva;
    if (getInputState == nullptr || !TurningTargetMatches(target)) {
        log(
            "m4_binding_turning_rejected",
            getInputState == nullptr
                ? "controller_transport_export_missing"
                : "GameOrig_rva_00009900_signature_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(
            "m4_binding_turning_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    g_getInputState = getInputState;
    g_log = log;
    MH_STATUS status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookGetExtremalCommandValue),
        reinterpret_cast<void**>(
            &g_originalGetExtremalCommandValue));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log(
            "m4_binding_turning_rejected",
            MH_StatusToString(status));
        g_originalGetExtremalCommandValue = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_turningHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    log(
        "m4_binding_turning_armed",
        "target=GameOrig+0x00009900 command=23 "
        "path=retail_extremal_value deadzone=0.22 "
        "direct_command_writes=0 system_input=0");
    return true;
}

bool InstallHeadAimHooks(
    void* masterDatabase,
    HMODULE gameClientModule,
    RendererProbeLogFunction log,
    bool aimPathProbe,
    bool controllerMeleeAim,
    bool physicalMeleeProbe,
    bool physicalMeleeWallProxy,
    bool physicalMeleeColliderDebug,
    bool physicalMeleeContactDamage,
    bool physicalMeleeVisualProxy,
    bool weaponGripCalibration,
    bool twoHandedMelee) noexcept {
    if (masterDatabase == nullptr ||
        gameClientModule == nullptr || log == nullptr) {
        return false;
    }
    if (g_turningHookTarget == nullptr ||
        g_originalGetExtremalCommandValue == nullptr) {
        log(
            "m5_head_aim_rejected",
            "verified_extremal_binding_hook_required");
        return false;
    }
    if (controllerMeleeAim && !aimPathProbe) {
        log(
            "m5_controller_melee_aim_rejected",
            "aim_path_probe_required_for_initial_live_gate");
        return false;
    }
    if (physicalMeleeProbe && !aimPathProbe) {
        log(
            "m5_physical_melee_probe_rejected",
            "aim_path_probe_required_for_native_impact_trace");
        return false;
    }
    if (physicalMeleeWallProxy && !physicalMeleeProbe) {
        log(
            "m5_physical_melee_wall_proxy_rejected",
            "physical_melee_probe_required");
        return false;
    }
    if (physicalMeleeContactDamage &&
        !physicalMeleeWallProxy) {
        log(
            "m5_physical_melee_contact_damage_rejected",
            "physical_melee_wall_proxy_required");
        return false;
    }
    if (physicalMeleeColliderDebug && !physicalMeleeWallProxy) {
        log(
            "m5_physical_melee_collider_debug_rejected",
            "physical_melee_wall_proxy_required");
        return false;
    }
    if (physicalMeleeWallProxy && controllerMeleeAim) {
        log(
            "m5_physical_melee_wall_proxy_rejected",
            "controller_melee_aim_conflicts_with_physical_proxy");
        return false;
    }
    if (physicalMeleeVisualProxy && !physicalMeleeWallProxy) {
        log(
            "m5_physical_melee_visual_proxy_rejected",
            "physical_melee_wall_proxy_required");
        return false;
    }
    if (weaponGripCalibration && !physicalMeleeVisualProxy) {
        log(
            "m5_weapon_grip_calibration_rejected",
            "physical_melee_visual_proxy_required");
        return false;
    }
    if (twoHandedMelee && !physicalMeleeVisualProxy) {
        log(
            "m5_two_handed_melee_rejected",
            "physical_melee_visual_proxy_required");
        return false;
    }
    auto* const fireVectors =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kGetFireVectorsRva;
    auto* const meleeEnableCollisions =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kMeleeEnableCollisionsRva;
    auto* const meleeUpdateCollision =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kMeleeUpdateCollisionRva;
    auto* const buildRigidTransform =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kBuildRigidTransformRva;
    auto* const meleeImpactDispatch =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kMeleeImpactDispatchRva;
    auto* const resetMeleeTargetReference =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kResetMeleeTargetReferenceRva;
    DatabaseFloatReaderFunction masterDatabaseFloatReader = nullptr;
    if (!FireVectorsTargetMatches(fireVectors)) {
        log(
            "m5_head_aim_rejected",
            "GameOrig_rva_0002af70_fire_vector_signature_mismatch");
        return false;
    }
    void* firearmMuzzleModel = nullptr;
    GetModelSocketFunction getFirearmModelSocket = nullptr;
    GetModelSocketTransformFunction
        getFirearmModelSocketTransform = nullptr;
    const bool firearmSocketLayoutVerified =
        FirearmMuzzleSocketLayoutMatches(gameClientModule);
    const bool handgunMuzzleAimVerified =
        firearmSocketLayoutVerified &&
        ResolveVerifiedFirearmMuzzleModelInterface(
            masterDatabase, gameClientModule,
            firearmMuzzleModel,
            getFirearmModelSocket,
            getFirearmModelSocketTransform);
    if (!handgunMuzzleAimVerified) {
        log(
            "m5_handgun_muzzle_aim_rejected",
            firearmSocketLayoutVerified
                ? "reason=ILTModelClient_Default_slots_1_2_or_global_mismatch "
                  "fallback=raw_controller"
                : "reason=GameOrig_Flash_Breach_socket_helper_layout_mismatch "
                  "fallback=raw_controller");
    }
    g_firearmMuzzleModel = handgunMuzzleAimVerified
        ? firearmMuzzleModel : nullptr;
    g_firearmMuzzleModelClientGlobalAddress =
        handgunMuzzleAimVerified
        ? reinterpret_cast<unsigned char*>(gameClientModule) +
              kModelClientGlobalRva
        : nullptr;
    g_getFirearmModelSocket = handgunMuzzleAimVerified
        ? getFirearmModelSocket : nullptr;
    g_getFirearmModelSocketTransform =
        handgunMuzzleAimVerified
        ? getFirearmModelSocketTransform : nullptr;

    if (aimPathProbe && !MeleeEnableCollisionsTargetMatches(
            gameClientModule, meleeEnableCollisions)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0001fd00_melee_collision_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !MeleeUpdateCollisionTargetMatches(
            meleeUpdateCollision)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0001fc00_melee_update_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !BuildRigidTransformTargetMatches(
            buildRigidTransform)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0000f690_transform_builder_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !MeleeImpactDispatchTargetMatches(
            gameClientModule, meleeImpactDispatch)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0001f270_melee_impact_signature_mismatch");
        return false;
    }
    if (physicalMeleeContactDamage &&
        !ResetMeleeTargetReferenceTargetMatches(
            resetMeleeTargetReference)) {
        log(
            "m5_physical_melee_contact_damage_rejected",
            "GameOrig_rva_00102b80_target_reference_signature_mismatch");
        return false;
    }
    if (physicalMeleeContactDamage &&
        !MeleeNativeCapsulePropertyCallsitesMatch(
            gameClientModule, meleeEnableCollisions)) {
        log(
            "m5_physical_melee_native_capsule_rejected",
            "GameOrig_rva_0001fd00_native_capsule_property_callsites_mismatch");
        return false;
    }
    if (physicalMeleeContactDamage &&
        !MeleeCollisionRoleLayoutMatches(meleeEnableCollisions)) {
        log(
            "m5_physical_melee_contact_damage_rejected",
            "GameOrig_rva_0001fd00_block_argument_or_record_layout_mismatch");
        return false;
    }
    if (physicalMeleeContactDamage) {
        masterDatabaseFloatReader =
            ResolveMasterDatabaseFloatReader(gameClientModule);
        if (masterDatabaseFloatReader == nullptr) {
            log(
                "m5_physical_melee_native_capsule_rejected",
                "master_database_float_reader_slot_0x80_signature_mismatch");
            return false;
        }
    }
    if (physicalMeleeVisualProxy &&
        !EquippedWeaponLayoutMatches(gameClientModule)) {
        log(
            "m5_physical_melee_visual_proxy_rejected",
            "GameOrig_current_weapon_or_model_layout_mismatch");
        return false;
    }
    const bool blockNativeReleaseVerified =
        physicalMeleeProbe &&
        PhysicalMeleeBlockNativeReleaseTargetsMatch(
            gameClientModule);
    if (physicalMeleeProbe && !blockNativeReleaseVerified) {
        log(
            "m5_physical_melee_block_pose_rejected",
            "reason=GameOrig_block_stimulus_or_command_off_signature_mismatch "
            "automatic_pose_output=disabled "
            "manual_left_trigger_fallback=retail");
    }
    const bool combatPlayerVitalsVerified =
        physicalMeleeContactDamage &&
        RetailPlayerVitalsLayoutMatches(gameClientModule);
    if (physicalMeleeContactDamage &&
        !combatPlayerVitalsVerified) {
        log(
            "m5_combat_player_vitals_rejected",
            "reason=Health_command_or_setter_signature_mismatch "
            "behavior=melee_unaffected");
    }


    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log("m5_head_aim_rejected", MH_StatusToString(initialize));
        return false;
    }
    MH_STATUS status = MH_CreateHook(
        fireVectors,
        reinterpret_cast<void*>(&HookGetFireVectors),
        reinterpret_cast<void**>(&g_originalGetFireVectors));
    if (status == MH_OK) {
        status = MH_EnableHook(fireVectors);
    }
    if (status != MH_OK) {
        MH_RemoveHook(fireVectors);
        g_originalGetFireVectors = nullptr;
        log("m5_head_aim_rejected", MH_StatusToString(status));
        return false;
    }
    if (aimPathProbe) {
        status = MH_CreateHook(
            meleeEnableCollisions,
            reinterpret_cast<void*>(&HookMeleeEnableCollisions),
            reinterpret_cast<void**>(
                &g_originalMeleeEnableCollisions));
        if (status == MH_OK) {
            status = MH_EnableHook(meleeEnableCollisions);
        }
        if (status != MH_OK) {
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        status = MH_CreateHook(
            meleeUpdateCollision,
            reinterpret_cast<void*>(&HookMeleeUpdateCollision),
            reinterpret_cast<void**>(
                &g_originalMeleeUpdateCollision));
        if (status == MH_OK) {
            status = MH_EnableHook(meleeUpdateCollision);
        }
        if (status != MH_OK) {
            MH_RemoveHook(meleeUpdateCollision);
            g_originalMeleeUpdateCollision = nullptr;
            MH_DisableHook(meleeEnableCollisions);
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        status = MH_CreateHook(
            buildRigidTransform,
            reinterpret_cast<void*>(&HookBuildRigidTransform),
            reinterpret_cast<void**>(
                &g_originalBuildRigidTransform));
        if (status == MH_OK) {
            status = MH_EnableHook(buildRigidTransform);
        }
        if (status != MH_OK) {
            MH_RemoveHook(buildRigidTransform);
            g_originalBuildRigidTransform = nullptr;
            MH_DisableHook(meleeUpdateCollision);
            MH_RemoveHook(meleeUpdateCollision);
            g_originalMeleeUpdateCollision = nullptr;
            MH_DisableHook(meleeEnableCollisions);
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        status = MH_CreateHook(
            meleeImpactDispatch,
            reinterpret_cast<void*>(&HookMeleeImpactDispatch),
            reinterpret_cast<void**>(
                &g_originalMeleeImpactDispatch));
        if (status == MH_OK) {
            status = MH_EnableHook(meleeImpactDispatch);
        }
        if (status != MH_OK) {
            MH_RemoveHook(meleeImpactDispatch);
            g_originalMeleeImpactDispatch = nullptr;
            MH_DisableHook(buildRigidTransform);
            MH_RemoveHook(buildRigidTransform);
            g_originalBuildRigidTransform = nullptr;
            MH_DisableHook(meleeUpdateCollision);
            MH_RemoveHook(meleeUpdateCollision);
            g_originalMeleeUpdateCollision = nullptr;
            MH_DisableHook(meleeEnableCollisions);
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        if (physicalMeleeContactDamage) {
            void* const databaseFloatReaderTarget =
                reinterpret_cast<void*>(masterDatabaseFloatReader);
            status = MH_CreateHook(
                databaseFloatReaderTarget,
                reinterpret_cast<void*>(
                    &HookMasterDatabaseFloatReader),
                reinterpret_cast<void**>(
                    &g_originalMasterDatabaseFloatReader));
            if (status == MH_OK) {
                status = MH_EnableHook(
                    databaseFloatReaderTarget);
            }
            if (status != MH_OK) {
                MH_RemoveHook(databaseFloatReaderTarget);
                g_originalMasterDatabaseFloatReader = nullptr;
                MH_DisableHook(meleeImpactDispatch);
                MH_RemoveHook(meleeImpactDispatch);
                g_originalMeleeImpactDispatch = nullptr;
                MH_DisableHook(buildRigidTransform);
                MH_RemoveHook(buildRigidTransform);
                g_originalBuildRigidTransform = nullptr;
                MH_DisableHook(meleeUpdateCollision);
                MH_RemoveHook(meleeUpdateCollision);
                g_originalMeleeUpdateCollision = nullptr;
                MH_DisableHook(meleeEnableCollisions);
                MH_RemoveHook(meleeEnableCollisions);
                g_originalMeleeEnableCollisions = nullptr;
                MH_DisableHook(fireVectors);
                MH_RemoveHook(fireVectors);
                g_originalGetFireVectors = nullptr;
                log(
                    "m5_physical_melee_native_capsule_rejected",
                    MH_StatusToString(status));
                return false;
            }
        }
    }

    g_fireVectorsHookTarget = fireVectors;
    g_meleeEnableCollisionsHookTarget = aimPathProbe
        ? meleeEnableCollisions
        : nullptr;
    g_meleeUpdateCollisionHookTarget = aimPathProbe
        ? meleeUpdateCollision
        : nullptr;
    g_buildRigidTransformHookTarget = aimPathProbe
        ? buildRigidTransform
        : nullptr;
    g_meleeImpactDispatchHookTarget = aimPathProbe
        ? meleeImpactDispatch
        : nullptr;
    g_masterDatabaseFloatReaderHookTarget =
        physicalMeleeContactDamage
        ? reinterpret_cast<void*>(masterDatabaseFloatReader)
        : nullptr;
    g_gameClientBase = reinterpret_cast<unsigned char*>(
        gameClientModule);
    g_log = log;
    InterlockedExchange(&g_mouseLookSuppressionLogged, 0);
    InterlockedExchange(
        &g_combatPlayerVitalsEnabled,
        combatPlayerVitalsVerified ? 1 : 0);
    InterlockedExchange(
        &g_combatPlayerVitalsUnavailableLogged, 0);
    g_combatPlayerVitalsLastSampleTick = 0U;
    g_combatPlayerVitalsLastHealth = 0U;
    g_combatPlayerVitalsLastMaximum = 0U;
    g_combatPlayerVitalsHaveSample = false;

    InterlockedExchange(&g_controllerFireAimLogged, 0);
    InterlockedExchange(&g_handgunMuzzleAimCalls, 0);
    InterlockedExchange(&g_handgunMuzzleAimApplied, 0);
    InterlockedExchange(&g_handgunMuzzleAimFallbacks, 0);
    InterlockedExchange(&g_handgunMuzzleAimActiveLogged, 0);
    InterlockedExchange(&g_aimPathFireVectorCalls, 0);
    InterlockedExchange(&g_aimPathMeleeCalls, 0);
    InterlockedExchange(&g_aimPathMeleeUpdateCalls, 0);
    InterlockedExchange(&g_aimPathMeleeTransformCalls, 0);
    InterlockedExchange(&g_aimPathMeleeImpactCalls, 0);
    InterlockedExchange(&g_controllerMeleeAimLogged, 0);
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    g_physicalMeleeState = {};
    g_physicalMeleeSwingKinematicsState = {};
    g_physicalMeleeFrame = {};
    g_physicalMeleeProfile = {};
    g_physicalMeleeProfileWeaponIndex = -1;
    g_equippedWeaponIdentity = {};
    g_physicalMeleeContactState = {};
    g_physicalMeleeSwingAttackState = {};
    g_physicalMeleeAutomaticSeedState = {};
    g_physicalMeleeSampleId = 0;
    g_physicalMeleeSampleTick = 0;
    g_physicalMeleeSwingSampleId = 0;
    g_physicalMeleeSwingSampleTick = 0;
    g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
    g_physicalMeleePlayerWeaponModelObject = 0U;
    g_physicalMeleePlayerCollisionController = nullptr;
    g_physicalMeleeAutomaticSeedImpactController = nullptr;
    g_physicalMeleeAutomaticSeedImpactBlockUntil = 0U;
    g_physicalMeleePlayerCollisionObject = 0U;
    g_physicalMeleePlayerCollisionTick = 0U;
    g_physicalMeleePlayerBlockCollisionObject = 0U;
    g_physicalMeleePlayerBlockCollisionTick = 0U;
    g_physicalMeleeLastRetailBlockWindowSeconds = 0.0F;
    g_physicalMeleeLastAppliedBlockWindowSeconds = 0.0F;
    g_physicalMeleeLastBlockWindowOverrideApplied = false;
    for (auto& classification :
         g_physicalMeleePlayerCollisionClassifications) {
        classification = {};
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    AcquireSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
    g_physicalMeleeBlockPoseState = {};
    g_physicalMeleeBlockPoseResult = {};
    g_physicalMeleeBlockNativeLifecycleState = {};
    g_physicalMeleeBlockPoseWeaponIndex = -1;
    g_physicalMeleeBlockPoseTrackingFresh = false;
    ReleaseSRWLockExclusive(&g_physicalMeleeBlockPoseLock);
    g_physicalMeleeActiveCollisionRecord = nullptr;
    g_physicalMeleeActiveCollisionRole =
        PhysicalMeleeCollisionRole::Unknown;
    g_physicalMeleeNativeCapsuleOverride = {};
    g_physicalMeleeNativeCapsuleReadMask = 0U;
    g_resetMeleeTargetReference = physicalMeleeContactDamage
        ? reinterpret_cast<ResetMeleeTargetReferenceFunction>(
              resetMeleeTargetReference)
        : nullptr;
    InterlockedExchange(&g_physicalMeleeSampleCalls, 0);
    InterlockedExchange(&g_physicalMeleeDamageQualified, 0);
    InterlockedExchange(&g_physicalMeleeSwingAttackTriggered, 0);
    InterlockedExchange(&g_physicalMeleeAutomaticSeedStarted, 0);
    InterlockedExchange(&g_physicalMeleeAutomaticSeedConfirmed, 0);
    InterlockedExchange(&g_physicalMeleeAutomaticSeedReady, 0);
    InterlockedExchange(&g_physicalMeleeAutomaticSeedFailed, 0);
    InterlockedExchange(
        &g_physicalMeleeAutomaticSeedImpactsBlocked, 0);
    InterlockedExchange(&g_physicalMeleeBlockPoseActivations, 0);
    InterlockedExchange(
        &g_physicalMeleeBlockNativeReleaseEnabled,
        blockNativeReleaseVerified ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeBlockNativeReleaseQueued, 0);
    InterlockedExchange(
        &g_physicalMeleeBlockNativeReleaseDispatched, 0);
    InterlockedExchange(
        &g_physicalMeleeBlockNativeReleaseSkipped, 0);
    InterlockedExchange(&g_physicalMeleeWallProxyAppliedLogged, 0);
    InterlockedExchange(&g_physicalMeleeContactAccepted, 0);
    InterlockedExchange(&g_physicalMeleeContactRearmed, 0);
    InterlockedExchange(&g_physicalMeleeNativeCapsuleOverrides, 0);
    InterlockedExchange(&g_physicalMeleeBlockWindowSamples, 0);
    InterlockedExchange(&g_physicalMeleeDamageDispatched, 0);
    InterlockedExchange(
        &g_physicalMeleeContactInvalidSampleHeld, 0);
    InterlockedExchange(&g_physicalMeleeContinuousCollisionHeld, 0);
    InterlockedExchange(&g_physicalMeleeContinuousCollisionReleased, 0);
    InterlockedExchange(&g_physicalMeleeCollisionRoleClassified, 0);
    InterlockedExchange(&g_physicalMeleeCollisionRoleUnclassified, 0);
    InterlockedExchange(&g_physicalMeleeRetailLatchReleased, 0);
    InterlockedExchange(
        &g_physicalMeleeRetailLatchReleaseFailedLogged, 0);
    InterlockedExchange(
        &g_physicalMeleeWallProxyEnabled,
        physicalMeleeWallProxy ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeColliderDebugEnabled,
        physicalMeleeColliderDebug ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeVisualProxyEnabled,
        physicalMeleeVisualProxy ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeContactDamageEnabled,
        physicalMeleeContactDamage ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeAutomaticSeedEnabled,
        physicalMeleeContactDamage &&
                physicalMeleeProbe &&
                physicalMeleeWallProxy
            ? 1 : 0);
    SetPhysicalMeleeVisualProxyEnabled(
        physicalMeleeVisualProxy);
    SetWeaponGripCalibrationEnabled(
        weaponGripCalibration);
    SetTwoHandedMeleeEnabled(twoHandedMelee);
    InterlockedExchange(
        &g_physicalMeleeProbeEnabled,
        physicalMeleeProbe ? 1 : 0);
    InterlockedExchange(
        &g_controllerMeleeAimEnabled,
        controllerMeleeAim ? 1 : 0);
    InterlockedExchange(
        &g_aimPathProbeEnabled, aimPathProbe ? 1 : 0);
    InterlockedExchange(&g_headAimInputEnabled, 1);
    log(
        "m5_head_aim_armed",
        "mouse_commands=11,12 suppression=fresh_hmd_look_only "
        "fire_vectors=GameOrig+0x0002AF70 "
        "index76_direction=visible_socket_barrel_when_armed "
        "socket_policy=prefer_Breach_to_Flash_else_Flash_plus_Z "
        "other_direction=right_controller_world_basis "
        "fire_position=retail stale_and_flat_fallback=retail");
    if (handgunMuzzleAimVerified &&
        physicalMeleeVisualProxy) {
        log(
            "m5_handgun_muzzle_aim_armed",
            "weapon_index=76 weapon=colt45_Unbreakable "
            "model_interface=ILTModelClient.Default_v0 "
            "GetSocket=slot1_CondemnedExe+0x000378E0 "
            "GetSocketTransform=slot2_CondemnedExe+0x000381D0 "
            "socket_names=Flash,optional_Breach "
            "direction=prefer_Breach_to_Flash_else_Flash_socket_plus_Z "
            "visible_model_pose=saved_grip_calibration "
            "fire_position=retail_preserved "
            "diagnostic_muzzle_origin=Flash "
            "fallback=raw_controller_then_retail event_cap=128");
    } else if (handgunMuzzleAimVerified) {
        log(
            "m5_handgun_muzzle_aim_rejected",
            "reason=physical_melee_visual_proxy_required "
            "fallback=raw_controller");
    }
    if (aimPathProbe) {
        log(
            "m5_aim_path_probe_armed",
            "behavior=observation_only command_edges=17,28,60,62 "
            "fire_vectors=GameOrig+0x0002AF70 "
            "melee_collision_enable=GameOrig+0x0001FD00 "
            "melee_collision_update=GameOrig+0x0001FC00 "
            "melee_transform_builder=GameOrig+0x0000F690 "
            "melee_impact_dispatch=GameOrig+0x0001F270 "
            "controller_forward_samples=1 gameorig_stack_rvas=1 "
            "event_cap_per_path=512");
    }
    if (controllerMeleeAim) {
        log(
            "m5_controller_melee_aim_armed",
            "target=GameOrig+0x0000F690 "
            "operation=controller_delta_about_camera_pivot "
            "retail_animation_curve_timing_damage_and_collision_rules=1 "
            "freshness_ms=250 stale_and_flat_fallback=retail");
    }
    if (physicalMeleeProbe) {
        log(
            "m5_physical_melee_probe_armed",
            "source=right_controller_weapon_pose "
            "position=openxr_grip rotation=openxr_aim "
            "swing_motion_space=openxr_tracking "
            "retail_locomotion_and_turning_excluded=1 "
            "profile=generic_one_handed_fallback "
            "catalog=pipe,crowbar,fire_axe,plank "
            "2x4_retail_indices=0,1,64,65 "
            "2x4_pose=WEAP_1HandedDebris "
            "pipe_lever_retail_index=32 pipe_lever_mass_kg=1.75 "
            "pipe_lever_handling_weight=1.75 "
            "pipe_lever_pose=WEAP_1HandedDebris "
            "fire_axe_retail_index=17 fire_axe_mass_kg=4.5 "
            "fire_axe_handling_weight=4.0 "
            "mapped_swing_attack=2x4_family,pipe_lever,fire_axe "
            "retail_fire_command=17 "
            "swing_trigger_mps=3.00 swing_rearm_mps=0.75 "
            "swing_pulse_ms=100 swing_cooldown_ms=450 "
            "length_m=0.75 radius_m=0.04 mass_kg=1.5 "
            "sweep=base_and_tip speed_gate_mps=1.25 "
            "energy_gate_j=1.0 native_impact_writes=qualified_contact_only");
        if (blockNativeReleaseVerified) {
            log(
                "m5_physical_melee_block_pose_armed",
                "scope=mapped_one_handed_weapons "
                "capture=head_yaw_relative_weighted_weapon_pose "
                "entry=retail_block_command_28 "
                "exit=retail_finite_block_window "
                "second_CS_Block_release=disabled_after_live_rejection "
                "collision_lifetime=classified_block_retail_window "
                "command_off_28=verified_unhandled "
                "input_seed_required=0 manual_left_trigger_fallback=1 "
                "position_tolerance_m=menu_saved "
                "angle_tolerance_deg=menu_saved freshness_ms=250 "
                "menu_focus_tracking_weapon_change=fail_closed "
                "live_exit_result=awaiting_attack_only_lifetime_gate");
        }
    }
    if (physicalMeleeWallProxy) {
        log(
            "m5_physical_melee_wall_proxy_armed",
            "target=GameOrig+0x0000F690 "
            "proxy_origin=controller_weapon_tip length_m=0.75 "
            "collision_lifetime=classified_attack_hold_or_retail_window "
            "block_collision_lifetime=retail_window_only "
            "contact_gate=fresh_overlap "
            "duplicate_latch=tip_displacement_0.12m "
            "native_impact_dispatch=blocked_unless_new_contact "
            "stale_and_background_fallback=retail_transform");
    }
    if (physicalMeleeColliderDebug) {
        log(
            "m5_physical_melee_collider_debug_armed",
            "shape=configured_swept_capsule "
            "origin=exact_controller_weapon_tip_proxy "
            "green=retail_collision_body_live "
            "amber=preview_waiting_for_automatic_equip_seed "
            "projection=verified_per_eye_camera "
            "overlay_depth=always_visible");
    }
    if (physicalMeleeContactDamage) {
        log(
            "m5_physical_melee_native_capsule_armed",
            "reader=verified_master_database_vtable_slot_0x80 "
            "scope=mapped_one_handed_player_creation "
            "native_axis=local_y length_up=0 "
            "length_down=configured_base_to_tip radius=configured "
            "transform_origin=configured_tip "
            "transform_axis=configured_base_to_tip "
            "expected_read_mask=0x7 fallback=retail");
    }
    if (physicalMeleeContactDamage) {
        log(
            "m5_physical_melee_contact_damage_armed",
            "weapon=mapped_one_handed_allowlist "
            "collision_check=continuous_after_classified_attack_seed "
            "classification=EnableCollisions_bBlocking_argument "
            "block_collision=retail_window_only unknown=fail_closed "
            "swing_speed_bool=diagnostic_only "
            "contact_gate=fresh_overlap "
            "one_hit_per_contact=1 "
            "repeat_rearm=tip_displacement_0.12m "
            "target_and_damage_dispatch=retail_owned "
            "tracking_menu_focus_weapon_change=fail_closed");
        if (physicalMeleeProbe && physicalMeleeWallProxy) {
            log(
                "m5_physical_melee_auto_seed_armed",
                "scope=verified_mapped_one_handed_equip "
                "stable_equip_ms=250 pulse_ms=100 "
                "confirmation_ms=2000 settle_ms=1000 "
                "maximum_attempts=3 "
                "output=retail_fire_command_17 "
                "haptic=blocked seed_impacts=blocked "
                "confirmation=player_attack_role_plus_read_mask_0x7 "
                "weapon_model_tracking_focus_menu=fail_closed "
                "manual_attack_fallback=1 "
                "direct_EnableCollisions_call=0 "
                "swing_attack_setting=independent");
        }
    }
    if (combatPlayerVitalsVerified) {
        log(
            "m5_combat_player_vitals_armed",
            "source=GameOrig+0x001702F8 current_offset=0x04 "
            "maximum_offset=0x0C "
            "identity=Health_command_plus_setter_signatures "
            "sampling_ms=100 behavior=read_only");
    }
    if (physicalMeleeVisualProxy) {
        log(
            "m5_physical_melee_visual_proxy_armed",
            "source=CClientWeaponMgr_current_weapon_model "
            "manager=GameOrig+0x00168EBC current_weapon_offset=0x0C "
            "model_LTObjRef_offset=0x1C acquisition=gameplay_update "
            "alignment=model_local_grip_to_openxr_right_grip "
            "rotation=openxr_right_aim profile_driven=1 "
            "heavy_profiles=bounded_damped_spring_visible_inertia "
            "attack_required=0 weapon_switch_auto_release=1 "
            "render_override_only=1 exact_transform_restore=1 "
            "placeholder_model=0 native_impact_dispatch=contact_gate_owned");
    }
    if (weaponGripCalibration) {
        log(
            "m5_weapon_grip_calibration_armed",
            "live_render_update=1 session_cache=per_weapon_index_pointer_model "
            "start_mode=position start_active=1 "
            "controller_capture=both_squeezes "
            "controller_axes=right_stick_xy,left_stick_y_z "
            "controller_buttons=a_position,b_rotation,x_reset,y_snapshot,"
            "left_stick_finer,right_stick_coarser "
            "visual_reference=generic_controller_wireframe "
            "visual_grip_pose=openxr_right_grip "
            "visual_aim_ray=openxr_right_aim "
            "gameplay_input_suppressed_during_capture=1 "
            "keyboard=j_l_x,k_i_y,u_o_z,t_mode,comma_period_step,"
            "r_reset,p_snapshot,f11_pause "
            "position_units=lithtech rotation_axes=model_local_xyz "
            "foreground_only=1 retail_transform_restore=exact");
    }
    if (twoHandedMelee) {
        log(
            "m5_two_handed_melee_armed",
            "dominant_hand=right support_hand=left "
            "attach=left_squeeze_near_profile_handle "
            "attach_threshold=0.65 release_threshold=0.35 "
            "remote_snap_grab=0 authored_weapon_scaling=0 "
            "solver=dominant_anchor_shortest_arc_twist_preserving "
            "weight_filter=existing_bounded_damped_spring "
            "release_momentum_reset=0 tracking_loss_fail_closed=1 "
            "conflicting_left_squeeze_run_action=captured "
            "supported_profile=fire_axe retail_index=17");
    }
    return true;
}

bool InstallMenuToggleHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    bool menuControls) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (g_menuHookTarget != nullptr) {
        InterlockedExchange(
            &g_menuControlsEnabled, menuControls ? 1 : 0);
        RequireMenuNavigationRelease(g_menuNavigationState);
        ReleaseSRWLockExclusive(&g_bindingLock);
        return true;
    }
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    const auto getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_GetInputState"));
    const auto setMenuActive = reinterpret_cast<SetMenuActiveFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_SetMenuActive"));
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    if (getInputState == nullptr || setMenuActive == nullptr ||
        clientShell == nullptr ||
        !MenuTargetsMatch(gameClientModule, clientShell)) {
        const char* reason =
            getInputState == nullptr
            ? "controller_transport_export_missing"
            : setMenuActive == nullptr
            ? "menu_render_transport_export_missing"
            : clientShell == nullptr
            ? "IClientShell_Default_v4_missing"
            : "IClientShell_Default_v4_target_mismatch";
        log("m4_menu_toggle_rejected", reason);
        return false;
    }
    void* const interfaceManager = ResolveVerifiedInterfaceManager(
        gameClientModule, clientShell);
    if (interfaceManager == nullptr ||
        !IsKnownCondemnedGameState(
            ReadRetailGameState(interfaceManager))) {
        log(
            "m4_menu_toggle_rejected",
            "CInterfaceMgr_state_layout_mismatch");
        return false;
    }

    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    void** const vtable = *static_cast<void***>(clientShell);
    void* const target = vtable[kClientShellUpdateSlot];
    const auto keyUp = reinterpret_cast<ClientShellKeyUpFunction>(
        base + kClientShellKeyUpRva);
    const auto keyDown = reinterpret_cast<ClientShellKeyDownFunction>(
        base + kClientShellKeyDownRva);

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(
            "m4_menu_toggle_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    g_getInputState = getInputState;
    g_setMenuActive = setMenuActive;
    g_log = log;
    g_clientShell = clientShell;
    g_interfaceManager = interfaceManager;
    g_clientShellKeyUp = keyUp;
    g_clientShellKeyDown = keyDown;
    g_menuToggleLatch = {};
    g_menuNavigationState = {};
    InterlockedExchange(
        &g_menuControlsEnabled, menuControls ? 1 : 0);
    InterlockedExchange(&g_menuUpdateObserved, 0);
    InterlockedExchange(
        &g_lastPublishedRetailGameState,
        kUnpublishedRetailGameState);
    InterlockedExchange(&g_menuRenderPublishFailed, 0);
    MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookClientShellUpdate),
        reinterpret_cast<void**>(&g_originalClientShellUpdate));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log("m4_menu_toggle_rejected", MH_StatusToString(status));
        g_originalClientShellUpdate = nullptr;
        g_clientShellKeyUp = nullptr;
        g_clientShellKeyDown = nullptr;
        g_setMenuActive = nullptr;
        g_interfaceManager = nullptr;
        g_clientShell = nullptr;
        InterlockedExchange(&g_menuControlsEnabled, 0);
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_menuHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    log(
        "m4_menu_toggle_armed",
        "target=GameOrig+0x00051150 "
        "interface=IClientShell.Default.v4 update_slot=3 "
        "button=left_secondary path=escape_callbacks "
        "state_source=CInterfaceMgr+0x08 flat_panel_nonplaying=1 "
        "escape_states=playing,menu "
        "direct_command_writes=0 system_input=0");
    if (menuControls) {
        log(
            "m6_menu_controls_armed",
            "states=menu,screen left_stick=arrow_keys "
            "right_primary_or_trigger=enter right_secondary=escape "
            "initial_repeat_ms=350 repeat_ms=110 "
            "neutral_on_entry=1 both_hands_required=1 "
            "path=IClientShell_v4_key_edges mouse_keyboard_unchanged=1 "
            "direct_command_writes=0 system_input=0");
    }
    PublishMenuRenderState();
    return true;
}

void ReadPhysicalMeleeToolTelemetry(
    ToolMenuMeleeTelemetry& telemetry) noexcept {
    telemetry = {};
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_physicalMeleeLock);
    telemetry.weaponIndex = g_physicalMeleeProfileWeaponIndex;
    std::memcpy(
        telemetry.weaponName,
        g_equippedWeaponIdentity.recordName,
        sizeof(telemetry.weaponName));
    std::memcpy(
        telemetry.weaponAnimationProperty,
        g_equippedWeaponIdentity.animationProperty,
        sizeof(telemetry.weaponAnimationProperty));
    telemetry.weaponPoseFamily =
        g_equippedWeaponIdentity.poseFamily;
    telemetry.weaponNameResolved =
        g_equippedWeaponIdentity.nameResolved;
    telemetry.weaponAnimationPropertyResolved =
        g_equippedWeaponIdentity.animationPropertyResolved;
    telemetry.contactTrackingFresh =
        g_physicalMeleeSampleId != 0 &&
        g_physicalMeleeSampleTick != 0 &&
        g_physicalMeleeFrame.poseValid &&
        g_physicalMeleeFrame.sweepValid &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds;
    telemetry.contactSpeedMetersPerSecond =
        telemetry.contactTrackingFresh
        ? g_physicalMeleeFrame.impactSpeedMetersPerSecond
        : 0.0F;
    telemetry.contactFastEnough =
        telemetry.contactTrackingFresh &&
        g_physicalMeleeFrame.damageQualified;
    telemetry.contactReleaseSpeedMetersPerSecond =
        PhysicalMeleeContactReleaseSpeedMetersPerSecond(
            g_physicalMeleeProfile);
    telemetry.contactReleaseSampleCount =
        g_physicalMeleeContactState.releaseSampleCount;
    telemetry.contactLatched =
        g_physicalMeleeContactState.haveContact &&
        !g_physicalMeleeContactState.armed;
    telemetry.contactRearmTravelReady =
        g_physicalMeleeContactState.rearmDistanceReached;
    telemetry.trackingFresh =
        g_physicalMeleeSwingSampleId != 0 &&
        g_physicalMeleeSwingSampleTick != 0 &&
        now - g_physicalMeleeSwingSampleTick <=
            kInputFreshnessMilliseconds;
    telemetry.swingSpeedMetersPerSecond = telemetry.trackingFresh
        ? g_physicalMeleeSwingSpeedMetersPerSecond
        : 0.0F;
    telemetry.lastRetailBlockWindowMilliseconds =
        g_physicalMeleeLastRetailBlockWindowSeconds * 1000.0F;
    telemetry.lastAppliedBlockWindowMilliseconds =
        g_physicalMeleeLastAppliedBlockWindowSeconds * 1000.0F;
    telemetry.blockWindowOverrideApplied =
        g_physicalMeleeLastBlockWindowOverrideApplied;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    AcquireSRWLockShared(&g_physicalMeleeBlockPoseLock);
    telemetry.blockPoseTrackingFresh =
        g_physicalMeleeBlockPoseTrackingFresh;
    telemetry.blockPoseActive =
        g_physicalMeleeBlockPoseResult.active;
    telemetry.blockPosePositionErrorMeters =
        g_physicalMeleeBlockPoseResult.positionErrorMeters;
    telemetry.blockPoseAngleErrorDegrees =
        g_physicalMeleeBlockPoseResult.angleErrorDegrees;
    ReleaseSRWLockShared(&g_physicalMeleeBlockPoseLock);
    telemetry.blockPoseActivationCount =
        static_cast<std::uint32_t>(std::max<LONG>(
            0, InterlockedCompareExchange(
                   &g_physicalMeleeBlockPoseActivations, 0, 0)));
    telemetry.triggerCount = static_cast<std::uint32_t>(
        std::max<LONG>(
            0, InterlockedCompareExchange(
                   &g_physicalMeleeSwingAttackTriggered, 0, 0)));
    telemetry.contactCallbackCount = static_cast<std::uint32_t>(
        std::max<LONG>(
            0, InterlockedCompareExchange(
                   &g_aimPathMeleeImpactCalls, 0, 0)));
    telemetry.damageDispatchCount =
        static_cast<std::uint32_t>(
            std::max<LONG>(
                0, InterlockedCompareExchange(
                       &g_physicalMeleeDamageDispatched, 0, 0)));
    telemetry.contactDamageEnabled = InterlockedCompareExchange(
        &g_physicalMeleeContactDamageEnabled, 0, 0) != 0;
    telemetry.wallProxyEnabled = InterlockedCompareExchange(
        &g_physicalMeleeWallProxyEnabled, 0, 0) != 0;
    telemetry.visualProxyEnabled = InterlockedCompareExchange(
        &g_physicalMeleeVisualProxyEnabled, 0, 0) != 0;
    telemetry.colliderDebugEnabled = InterlockedCompareExchange(
        &g_physicalMeleeColliderDebugEnabled, 0, 0) != 0;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    telemetry.collisionBodyLive =
        g_physicalMeleePlayerCollisionObject != 0U &&
        g_physicalMeleePlayerCollisionTick != 0U &&
        now - g_physicalMeleePlayerCollisionTick <=
            kInputFreshnessMilliseconds;
    telemetry.blockCollisionBodyLive =
        g_physicalMeleePlayerBlockCollisionObject != 0U &&
        g_physicalMeleePlayerBlockCollisionTick != 0U &&
        now - g_physicalMeleePlayerBlockCollisionTick <=
            kInputFreshnessMilliseconds;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    ReadPhysicalMeleeTwoHandTelemetry(telemetry);
}

bool ReadPhysicalMeleeColliderDebugSnapshot(
    PhysicalMeleeColliderDebugSnapshot& snapshot) noexcept {
    snapshot = {};
    snapshot.enabled = InterlockedCompareExchange(
        &g_physicalMeleeColliderDebugEnabled, 0, 0) != 0;
    if (!snapshot.enabled ||
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyEnabled, 0, 0) == 0) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    PhysicalMeleeFrame frame{};
    PhysicalMeleeProfile profile{};
    ULONGLONG sampleTick = 0U;
    std::uintptr_t collisionObject = 0U;
    ULONGLONG collisionTick = 0U;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    frame = g_physicalMeleeFrame;
    profile = g_physicalMeleeProfile;
    snapshot.sampleId = g_physicalMeleeSampleId;
    sampleTick = g_physicalMeleeSampleTick;
    collisionObject = g_physicalMeleePlayerCollisionObject;
    collisionTick = g_physicalMeleePlayerCollisionTick;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    snapshot.trackingFresh = snapshot.sampleId != 0U &&
        frame.poseValid && sampleTick != 0U &&
        now - sampleTick <= kInputFreshnessMilliseconds &&
        ProcessOwnsForegroundWindow();
    if (!snapshot.trackingFresh) {
        return false;
    }
    const PhysicalMeleeWallProxyTransform proxy =
        ResolvePhysicalMeleeWallProxyTransform(frame, true);
    if (!proxy.active) {
        return false;
    }
    snapshot.baseUnits = frame.currentBaseUnits;
    snapshot.tipUnits = frame.currentTipUnits;
    snapshot.collisionOriginUnits = proxy.positionUnits;
    snapshot.radiusUnits = profile.radiusUnits;
    snapshot.collisionBodyLive = collisionObject != 0U &&
        collisionTick != 0U &&
        now - collisionTick <= kInputFreshnessMilliseconds;
    return true;
}

bool ReadPhysicalMeleeBlockColliderDebugSnapshot(
    PhysicalMeleeColliderDebugSnapshot& snapshot) noexcept {
    snapshot = {};
    snapshot.enabled = InterlockedCompareExchange(
        &g_physicalMeleeColliderDebugEnabled, 0, 0) != 0;
    if (!snapshot.enabled ||
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyEnabled, 0, 0) == 0) {
        return false;
    }

    PhysicalMeleeFrame sourceFrame{};
    PhysicalMeleeProfile sourceProfile{};
    std::int32_t weaponIndex = -1;
    if (!CopyLatestPhysicalMeleeColliderSource(
            sourceFrame, snapshot.sampleId,
            sourceProfile, weaponIndex) ||
        !PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            weaponIndex, sourceProfile.id)) {
        return false;
    }
    const ToolMenuColliderSettings blockSettings =
        ReadVrToolMenuBlockColliderSettings(weaponIndex);
    PhysicalMeleeFrame blockFrame{};
    if (!ResolveToolMenuColliderFrameAtCurrentPose(
            sourceFrame, sourceProfile,
            blockSettings, blockFrame)) {
        return false;
    }
    const PhysicalMeleeNativeCapsuleShape shape =
        ResolvePhysicalMeleeNativeCapsuleShape(blockFrame, true);
    if (!shape.valid) {
        return false;
    }

    const ULONGLONG now = GetTickCount64();
    std::uintptr_t collisionObject = 0U;
    ULONGLONG collisionTick = 0U;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    collisionObject = g_physicalMeleePlayerBlockCollisionObject;
    collisionTick = g_physicalMeleePlayerBlockCollisionTick;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    snapshot.trackingFresh = true;
    snapshot.baseUnits = blockFrame.currentBaseUnits;
    snapshot.tipUnits = blockFrame.currentTipUnits;
    snapshot.collisionOriginUnits =
        shape.transform.positionUnits;
    snapshot.radiusUnits = blockFrame.radiusUnits;
    snapshot.collisionBodyLive = collisionObject != 0U &&
        collisionTick != 0U &&
        now - collisionTick <= kInputFreshnessMilliseconds;
    return true;
}

bool BindingInputAllowsSlideGrab() noexcept {
    return g_interfaceManager != nullptr &&
        ReadRetailGameState(g_interfaceManager) ==
            kCondemnedGameStatePlaying &&
        ProcessOwnsForegroundWindow();
}

} // namespace condemnedvr
