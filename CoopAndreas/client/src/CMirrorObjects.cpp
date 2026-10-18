#include "stdafx.h"
#include "CMirrorObjects.h"
#include "CMirrorObjectsPolicy.h"
#include "CMirrorObjectCopies.h"
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include "CServerTime.h"
#include "CSessionLifecycle.h"
#include "CTrainSync.h"
#include "network/packets/mirror_objects.h"
#include <CPools.h>
#include <CTaskSimpleHoldEntity.h>
#include <CTimer.h>
#include <cassert>
#include <cstdarg>

// Private build: the mission object mirror, see CMirrorObjects.h.
//
// gta_sa.exe 1.0 US used below:
// - create_object 0107 (case 0x46976F) and create_object_no_offset 029B (case 0x47F4F1; cases found through
//   CRunningScript::CommandHandlerTable, read with dumpbin): CObject::Create, ClearSpaceForMissionEntity 0x486B00,
//   CWorld::Add 0x563220, then CPool<CObject>::GetRef 0x465070 on CPools::ms_pObjectPool 0xB7449C and
//   `mov [0A43C78h],eax` (ScriptParams[0]) at 0x469890 / 0x47F603 before CRunningScript::StoreParameters 0x464370. So
//   the handle is in ScriptParams[0] when CRunningScript::Process's command call returns (CMissionMirror's
//   OnCommandExecuted). The cases add the handle to the mission clean-up list after that (0x4698B6, AddEntityToList).
// - A handle is (slot << 8) | the slot's id byte (CPools::GetObjectRef 0x550020); CPools::GetObject 0x550050 returns
//   NULL unless the byte map still holds that id (a freed slot, or one given to a new object, doesn't resolve).
// - CEntity (plugin-sdk CEntity.h, the static_asserts below): the flags dword at +0x1C (bit 0x01 m_bUsesCollision, 0x04
//   m_bIsStatic, 0x80 m_bIsVisible, 0x200 m_bRenderDamaged, 0x400 m_bStreamingDontDelete: the hold task's constructor
//   does `or dword ptr [edx+1Ch],400h` at 0x691434), m_nModelIndex +0x22, m_nAreaCode +0x2F.
// - CPhysical (plugin-sdk CPhysical.h; design 3.3): the physical flags at +0x40 (0x0004 bDisableCollisionForce, 0x2000
//   bDontApplySpeed: freeze_object 0550, case 0x48D621, does `or ecx,2004h` / `and ecx,0FFFFDFFBh` on [eax+40h] at
//   0x48D649 / 0x48D65E; 0x0040 bInfiniteMass), m_vecMoveSpeed +0x44 and m_vecTurnSpeed +0x50 (per 1/50 s),
//   m_pAttachedTo +0xFC, the attach offset +0x100, the Euler attach rotation +0x10C, the attach quaternion +0x118.
//   CPhysical::AttachEntityToEntity 0x54D570 (the Euler form, attach_object_to_car 0681 and the like) writes the offset
//   and the angles and zeroes the quaternion (xor ebx,ebx at 0x54D603, then +0x124/+0x118/+0x11C/+0x120); the
//   quaternion form 0x54D690 writes the quaternion (relative to the fork frame on a forklift). PositionAttachedEntity
//   (0x546FF0) uses the quaternion when its vector part isn't zero, else the angles (gta-reversed Physical.cpp; for an
//   object with bInfiniteMass +0x10C holds a position instead, so no angles are sent for one).
// - CObject (plugin-sdk CObject.h, gta-reversed Object.h): m_nObjectType +0x13C, the object flags at +0x140 (0x0400
//   bIsBroken, 0x2000 bIsLiftable, design 3.4), m_fScale +0x15C (set_object_scale 08D2, case 0x4754B8, writes
//   [eax+15Ch] at 0x4754D9).
// - CPedIntelligence::GetTaskHold 0x600FF0 (plugin-sdk CPedIntelligence.cpp; thiscall, one bool, ret 4; dumpbin): the
//   secondary task in slot 4 (CTaskManager::GetTaskSecondary 0x681810) when its type is 0x133, else, with false, the
//   simplest active task when it is of type 0x134 or 0x135 (pick up / put down, gta-reversed: subclasses of
//   CTaskSimpleHoldEntity). CTaskSimpleHoldEntity's constructor 0x6913A0 (body 0x6913A7, ret 1Ch; dumpbin): held entity
//   +0x08, offset +0x0C..+0x14, bone +0x18, bone flags +0x19, anim id +0x20, anim group +0x24, anim block +0x2C and
//   hierarchy +0x30 (zeroed here, set by the anim-name constructor 0x691470), m_bDisallowDroppingOnAnimEnd +0x36;
//   GetTaskType 0x691460 returns 0x133. The ped pool is walked once per pass (plugin-sdk CPool::GetAt skips free slots).
// - The network identity of a parent or holder comes from the managers' lists, which hold their entities through pool
//   references with generations (CNetworkVehicleManager::GetVehicle, CNetworkPedManager::GetPed and
//   CNetworkPlayerManager::GetPlayer by pointer); an unconfirmed ped or vehicle has none. A train carriage counts only
//   while the train sync runs (as the blips: the other games have no copy otherwise).

static_assert(offsetof(CEntity, m_nModelIndex) == 0x22, "CEntity::m_nModelIndex");
static_assert(offsetof(CEntity, m_nAreaCode) == 0x2F, "CEntity::m_nAreaCode");
static_assert(offsetof(CPhysical, m_nPhysicalFlags) == 0x40, "CPhysical::m_nPhysicalFlags");
static_assert(offsetof(CPhysical, m_vecMoveSpeed) == 0x44, "CPhysical::m_vecMoveSpeed");
static_assert(offsetof(CPhysical, m_vecTurnSpeed) == 0x50, "CPhysical::m_vecTurnSpeed");
static_assert(offsetof(CPhysical, m_pAttachedTo) == 0xFC, "CPhysical::m_pAttachedTo");
static_assert(offsetof(CPhysical, m_vecAttachOffset) == 0x100, "CPhysical::m_vecAttachOffset");
static_assert(offsetof(CPhysical, m_vecAttachedEntityPosn) == 0x10C, "CPhysical attach rotation (Euler)");
static_assert(offsetof(CPhysical, m_qAttachedEntityRotation) == 0x118, "CPhysical attach quaternion");
static_assert(offsetof(CObject, m_nObjectType) == 0x13C, "CObject::m_nObjectType");
static_assert(offsetof(CObject, m_nObjectFlags) == 0x140, "CObject::m_nObjectFlags");
static_assert(offsetof(CObject, m_fScale) == 0x15C, "CObject::m_fScale");
static_assert(MirrorObjectsPolicy::POOL_SIZE <= (MirrorObjectsWire::MISSION_ID_MAX >> 8) + 1, "handles fit the wire");
static_assert(Config::MAX_SERVER_PLAYERS <= 32, "one bit per player in the pending join snapshots");

namespace
{
using MirrorObjectsPolicy::eApplied;
using MirrorObjectsPolicy::eMotion;
using MirrorObjectsPolicy::SObjectFull;
using MirrorObjectsPolicy::SObjectRecord;
using MirrorObjectsPolicy::SOwnedObject;

// raw fields (offsets above)
constexpr size_t ENTITY_FLAGS = 0x1C;
constexpr size_t PHYSICAL_FLAGS = 0x40;
constexpr size_t OBJECT_FLAGS = 0x140;
constexpr uint32_t ENTITY_USES_COLLISION = 0x01;
constexpr uint32_t ENTITY_STATIC = 0x04;
constexpr uint32_t ENTITY_VISIBLE = 0x80;
constexpr uint32_t ENTITY_RENDER_DAMAGED = 0x200;
constexpr uint32_t PHYSICAL_DISABLE_COLLISION_FORCE = 0x0004;
constexpr uint32_t PHYSICAL_INFINITE_MASS = 0x0040;
constexpr uint32_t PHYSICAL_DONT_APPLY_SPEED = 0x2000;
constexpr uint32_t OBJECT_BROKEN = 0x0400;
constexpr uint32_t OBJECT_LIFTABLE = 0x2000;
// CTaskSimpleHoldEntity
constexpr size_t HOLD_ENTITY = 0x08;
constexpr size_t HOLD_OFFSET = 0x0C;
constexpr size_t HOLD_BONE = 0x18;
constexpr size_t HOLD_BONE_FLAGS = 0x19;
constexpr size_t HOLD_ANIM_ID = 0x20;
constexpr size_t HOLD_ANIM_GROUP = 0x24;
constexpr size_t HOLD_ANIM_HIERARCHY = 0x30;
constexpr size_t HOLD_DISALLOW_DROP = 0x36;

template <typename T>
T ReadField(const void* base, size_t offset)
{
    T value{};
    memcpy(&value, reinterpret_cast<const uint8_t*>(base) + offset, sizeof(T));
    return value;
}

// host: the objects the mission family created
MirrorObjectsPolicy::COwnedObjects ms_owned;
// receivers: the current host's records
MirrorObjectsPolicy::CRecordTable ms_records;

bool ms_bStarted = false;
uint32_t ms_nLastFrame = 0;          // CTimer::m_FrameCounter at the last call (a smaller one = the game restarted)
bool ms_bRestartSeen = false;        // CheckRestart saw a restart in frame ms_nRestartFrame (GAME_RESTARTED follows)
uint32_t ms_nRestartFrame = 0;
bool ms_bBroadcastPending = false;   // host: its whole set with bReset to everyone (after its game restarted)
uint32_t ms_nSnapshotsPending = 0;   // host: join snapshots still to send, one bit per player id

// the peds holding an object, found once per pass
struct SHeld
{
    const CObject* pObject = nullptr;
    CPed* pPed = nullptr;
    const void* pTask = nullptr;
};
constexpr int MAX_HELD = 64;
SHeld ms_held[MAX_HELD];
int ms_nHeld = 0;

// --coopdiag (plain values; the diag thread reads them)
struct SCounters
{
    unsigned int owned = 0;            // host: owned now
    unsigned int ownedTotal = 0;
    unsigned int ownFailures = 0;      // a family's create left no object, or the set is full
    unsigned int sentFull = 0;
    unsigned int sentRest = 0;         // FULLs with a resting transform
    unsigned int sentRemoved = 0;
    unsigned int sentMotion = 0;       // samples
    unsigned int sentPackets = 0;
    unsigned int sentMotionPackets = 0;
    unsigned int snapshots = 0;
    unsigned int broadcasts = 0;
    unsigned int captureFailures = 0;
    unsigned int parentsUnknown = 0;   // captures attached to something without a network identity (world motion)
    unsigned int holdersUnknown = 0;   // captures held by a ped without a network identity (world motion)
    unsigned int records = 0;          // receiver: records now
    unsigned int recvPackets = 0;
    unsigned int recvDropped = 0;      // not from the current host
    unsigned int recvResets = 0;
    unsigned int recvCreated = 0;
    unsigned int recvUpdated = 0;
    unsigned int recvRemoved = 0;
    unsigned int recvOwner = 0;
    unsigned int recvUnknown = 0;      // REMOVED / OWNER for an object this game doesn't have
    unsigned int recvTableFull = 0;
    unsigned int recvMotionPackets = 0;
    unsigned int recvMotion = 0;
    unsigned int recvMotionDropped = 0;  // not the owner, another epoch, or this game is the host
    unsigned int recvMotionUnknown = 0;
    unsigned int recvMotionLate = 0;     // older than the newest sample
    unsigned int recvDisabled = 0;       // packets ignored: mission-objects = false on this game
};
SCounters ms_counters;

// --coopdiag: CoopAndreas_objects.log next to gta_sa.exe, at most 1024 records and 1 MB (the copies' events too)
void LogLineV(const char* format, va_list args)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 1024)
    {
        return;
    }

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    {
        return;
    }
    char* fileName = strrchr(path, '\\');
    if (!fileName)
    {
        return;
    }
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_objects.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char text[512];
        std::vsnprintf(text, sizeof(text), format, args);
        fprintf(file, "t=%u frame=%u %s host=%d hostId=%d owned=%d records=%d\n", GetTickCount(), CTimer::m_FrameCounter,
            text, CSessionLifecycle::IsHost(), CSessionLifecycle::GetHostId(), ms_owned.Count(), ms_records.Count());
        ++records;
    }
    fclose(file);
}

void LogLine(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogLineV(format, args);
    va_end(args);
}

bool IsServerClockSynced()
{
    // packets sent before the first server-time answer carry the PC's uptime as their stamp and wait in the other
    // games' packet buffers (see CWorldStateRegistry); CServerTime::RecalculateOffset sets the offset from 0 on that
    // answer
    return CServerTime::m_serverTimeOffset != 0;
}

bool CanSend()
{
    return CSessionLifecycle::IsAuthenticated() && CSessionLifecycle::IsHost() && IsServerClockSynced();
}

// no other player is in: nothing goes out, the join snapshot brings everything to whoever connects
bool IsAlone()
{
    return CNetworkPlayerManager::m_pPlayers.empty();
}

// ---- the host: capture ----

void FindHolders()
{
    ms_nHeld = 0;
    if (ms_owned.Count() == 0 || CPools::ms_pPedPool == nullptr)
    {
        return;
    }
    for (int i = 0; i < CPools::ms_pPedPool->m_nSize && ms_nHeld < MAX_HELD; i++)
    {
        CPed* pPed = CPools::ms_pPedPool->GetAt(i);
        if (pPed == nullptr || pPed->m_pIntelligence == nullptr)
        {
            continue;
        }
        const CTaskSimpleHoldEntity* pTask = pPed->m_pIntelligence->GetTaskHold(false);
        if (pTask == nullptr)
        {
            continue;
        }
        const CEntity* pHeld = ReadField<const CEntity*>(pTask, HOLD_ENTITY);
        if (pHeld == nullptr || pHeld->m_nType != ENTITY_TYPE_OBJECT)
        {
            continue;
        }
        ms_held[ms_nHeld].pObject = static_cast<const CObject*>(pHeld);
        ms_held[ms_nHeld].pPed = pPed;
        ms_held[ms_nHeld].pTask = pTask;
        ms_nHeld++;
    }
}

// a ped's network identity by membership in the lists: a player's id, or a confirmed CNetworkPed's (a ped still waiting
// for PED_CONFIRM is only in CNetworkPedManager::m_apTempPeds and has none)
bool GetPedIdentity(CPed* pPed, bool& bPlayer, int32_t& id)
{
    if (pPed->IsPlayer())
    {
        bPlayer = true;
        if (pPed == FindPlayerPed(0))
        {
            id = CNetworkPlayerManager::m_nMyId;
            return id >= 0 && id < Config::MAX_SERVER_PLAYERS;
        }
        if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed))
        {
            id = pNetworkPlayer->m_iPlayerId;
            return true;
        }
        return false;
    }
    bPlayer = false;
    if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed))
    {
        id = pNetworkPed->m_nPedId;
        return true;
    }
    return false;
}

// true when the object is attached to something without a network identity (sent as none: world motion)
bool CaptureParent(CObject* pObject, MirrorObjectsWire::SParent& parent)
{
    parent = MirrorObjectsWire::SParent{};
    CPhysical* pParent = pObject->m_pAttachedTo;
    if (pParent == nullptr)
    {
        return false;
    }
    uint8_t kind = MirrorObjectsWire::PARENT_NONE;
    int32_t id = 0;
    switch (pParent->m_nType)
    {
        case ENTITY_TYPE_VEHICLE:
        {
            CVehicle* pVehicle = static_cast<CVehicle*>(pParent);
            if (pVehicle->m_nVehicleSubType != static_cast<unsigned int>(VEHICLE_TRAIN) || CTrainSync::IsEnabled())
            {
                if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
                {
                    kind = MirrorObjectsWire::PARENT_VEHICLE;
                    id = pNetworkVehicle->m_nVehicleId;
                }
            }
            break;
        }
        case ENTITY_TYPE_PED:
        {
            bool bPlayer = false;
            if (GetPedIdentity(static_cast<CPed*>(pParent), bPlayer, id))
            {
                kind = bPlayer ? MirrorObjectsWire::PARENT_PLAYER : MirrorObjectsWire::PARENT_PED;
            }
            break;
        }
        case ENTITY_TYPE_OBJECT:
        {
            const int32_t handle = CPools::GetObjectRef(static_cast<CObject*>(pParent));
            if (ms_owned.Contains(handle))
            {
                kind = MirrorObjectsWire::PARENT_OBJECT;
                id = handle;
            }
            break;
        }
        default:
            break;
    }
    const CVector& offset = pObject->m_vecAttachOffset;
    if (kind == MirrorObjectsWire::PARENT_NONE || !MirrorObjectsWire::IsValidNetworkId(kind, false, id) ||
        !MirrorObjectsPolicy::IsOffsetSendable(offset.x, offset.y, offset.z))
    {
        return true;
    }
    parent.kind = kind;
    parent.id = id;
    parent.offsetX = offset.x;
    parent.offsetY = offset.y;
    parent.offsetZ = offset.z;
    const CQuaternion& quaternion = pObject->m_qAttachedEntityRotation;
    parent.bQuaternion = quaternion.imag.x != 0.0f || quaternion.imag.y != 0.0f || quaternion.imag.z != 0.0f;
    if (parent.bQuaternion)
    {
        parent.rotation.x = quaternion.imag.x;
        parent.rotation.y = quaternion.imag.y;
        parent.rotation.z = quaternion.imag.z;
        parent.rotation.w = quaternion.real;
    }
    else if ((ReadField<uint32_t>(pObject, PHYSICAL_FLAGS) & PHYSICAL_INFINITE_MASS) == 0)
    {
        const CVector& angles = pObject->m_vecAttachedEntityPosn;
        if (std::isfinite(angles.x) && std::isfinite(angles.y) && std::isfinite(angles.z))
        {
            parent.eulerX = angles.x;
            parent.eulerY = angles.y;
            parent.eulerZ = angles.z;
        }
    }
    // (an object with bInfiniteMass keeps its own world rotation while attached in the Euler form: the FULL's rotation,
    // which the receivers give their copy before attaching it, CMirrorObjectCopies.cpp AttachCopy)
    return false;
}

// true when a ped without a network identity holds the object (sent as none: world motion)
bool CaptureHolder(const CObject* pObject, MirrorObjectsWire::SHolder& holder)
{
    holder = MirrorObjectsWire::SHolder{};
    for (int i = 0; i < ms_nHeld; i++)
    {
        if (ms_held[i].pObject != pObject)
        {
            continue;
        }
        bool bPlayer = false;
        int32_t id = 0;
        const void* pTask = ms_held[i].pTask;
        const CVector offset = ReadField<CVector>(pTask, HOLD_OFFSET);
        if (!GetPedIdentity(ms_held[i].pPed, bPlayer, id) ||
            !MirrorObjectsPolicy::IsOffsetSendable(offset.x, offset.y, offset.z))
        {
            return true;
        }
        holder.kind = bPlayer ? MirrorObjectsWire::HOLDER_PLAYER : MirrorObjectsWire::HOLDER_PED;
        holder.id = id;
        if (!MirrorObjectsWire::IsValidNetworkId(holder.kind, true, id))
        {
            holder = MirrorObjectsWire::SHolder{};
            return true;
        }
        holder.offsetX = offset.x;
        holder.offsetY = offset.y;
        holder.offsetZ = offset.z;
        holder.bone = ReadField<uint8_t>(pTask, HOLD_BONE);
        holder.boneFlags = ReadField<uint8_t>(pTask, HOLD_BONE_FLAGS);
        const int32_t animId = ReadField<int32_t>(pTask, HOLD_ANIM_ID);
        const int32_t animGroup = ReadField<int32_t>(pTask, HOLD_ANIM_GROUP);
        holder.animId = animId >= -1 && animId <= MirrorObjectsWire::ANIM_MAX ? animId : -1;
        holder.animGroup = animGroup >= -1 && animGroup <= MirrorObjectsWire::ANIM_MAX ? animGroup : -1;
        holder.bAnimByName = ReadField<const void*>(pTask, HOLD_ANIM_HIERARCHY) != nullptr;
        holder.bDisallowDrop = ReadField<uint8_t>(pTask, HOLD_DISALLOW_DROP) != 0;
        return false;
    }
    return false;
}

void SetSpeed(MirrorObjectsWire::SSpeed& speed, const CVector& value)
{
    if (std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z))
    {
        speed.x = value.x;
        speed.y = value.y;
        speed.z = value.z;
    }
    else
    {
        speed = MirrorObjectsWire::SSpeed{};
    }
}

// The owned object as the other games should have it; false when it can't be read this frame (nothing goes out for it
// then). bHolders: FindHolders ran in this pass (a new object has no holder).
bool CaptureObject(CObject* pObject, SOwnedObject& owned, bool bHolders, SObjectFull& state)
{
    const uint8_t epoch = owned.epoch;
    state = SObjectFull{};
    if (pObject->m_nModelIndex >= MirrorObjectsWire::MODEL_COUNT)
    {
        return false;
    }
    state.model = pObject->m_nModelIndex;
    state.area = pObject->m_nAreaCode;

    const uint32_t entityFlags = ReadField<uint32_t>(pObject, ENTITY_FLAGS);
    const uint32_t physicalFlags = ReadField<uint32_t>(pObject, PHYSICAL_FLAGS);
    const uint32_t objectFlags = ReadField<uint32_t>(pObject, OBJECT_FLAGS);
    uint16_t flags = 0;
    flags |= (entityFlags & ENTITY_VISIBLE) ? MirrorObjectsWire::FLAG_VISIBLE : 0;
    flags |= (entityFlags & ENTITY_USES_COLLISION) ? MirrorObjectsWire::FLAG_COLLISION : 0;
    flags |= (entityFlags & ENTITY_STATIC) ? MirrorObjectsWire::FLAG_STATIC : 0;
    flags |= (entityFlags & ENTITY_RENDER_DAMAGED) ? MirrorObjectsWire::FLAG_RENDER_DAMAGED : 0;
    flags |= (physicalFlags & PHYSICAL_DISABLE_COLLISION_FORCE) ? MirrorObjectsWire::FLAG_DISABLE_COLLISION_FORCE : 0;
    flags |= (physicalFlags & PHYSICAL_DONT_APPLY_SPEED) ? MirrorObjectsWire::FLAG_DONT_APPLY_SPEED : 0;
    flags |= (objectFlags & OBJECT_LIFTABLE) ? MirrorObjectsWire::FLAG_LIFTABLE : 0;
    flags |= (objectFlags & OBJECT_BROKEN) ? MirrorObjectsWire::FLAG_BROKEN : 0;
    state.flags = flags;

    const float scale = pObject->m_fScale;
    if (std::isfinite(scale) && scale > 0.0f && scale <= MirrorObjectsWire::SCALE_MAX && std::fabs(scale - 1.0f) > 0.0001f)
    {
        state.bScaled = true;
        state.scale = scale;
    }

    const CVector& position = pObject->GetPosition();
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
    {
        return false;
    }
    state.position.x = position.x;
    state.position.y = position.y;
    state.position.z = position.z;
    if (pObject->m_matrix != nullptr)
    {
        MirrorObjectsPolicy::SAxes axes;
        const CVector& right = pObject->m_matrix->GetRight();
        const CVector& forward = pObject->m_matrix->GetForward();
        const CVector& up = pObject->m_matrix->GetUp();
        axes.right[0] = right.x;
        axes.right[1] = right.y;
        axes.right[2] = right.z;
        axes.forward[0] = forward.x;
        axes.forward[1] = forward.y;
        axes.forward[2] = forward.z;
        axes.up[0] = up.x;
        axes.up[1] = up.y;
        axes.up[2] = up.z;
        state.rotation = MirrorObjectsPolicy::QuaternionFromAxes(axes);
    }
    else
    {
        // only a heading (CPlaceable::m_placement): a rotation about z
        const float half = pObject->m_placement.m_fHeading * 0.5f;
        if (std::isfinite(half))
        {
            state.rotation.z = std::sin(half);
            state.rotation.w = std::cos(half);
        }
    }
    SetSpeed(state.moveSpeed, pObject->m_vecMoveSpeed);
    SetSpeed(state.turnSpeed, pObject->m_vecTurnSpeed);

    state.owner.playerId = MirrorObjectsWire::OWNER_HOST;
    state.owner.epoch = epoch;
    // the --coopdiag counters count the starts of an unknown parent or holder, not every capture
    const bool bParentUnknown = CaptureParent(pObject, state.parent);
    if (bParentUnknown && !owned.bParentUnknown)
    {
        ++ms_counters.parentsUnknown;
    }
    owned.bParentUnknown = bParentUnknown;
    if (bHolders)
    {
        const bool bHolderUnknown = CaptureHolder(pObject, state.holder);
        if (bHolderUnknown && !owned.bHolderUnknown)
        {
            ++ms_counters.holdersUnknown;
        }
        owned.bHolderUnknown = bHolderUnknown;
    }
    return true;
}

void FillFull(MirrorObjectsWire::SObjectEntry& entry, int32_t handle, const SObjectFull& state)
{
    entry.idKind = MirrorObjectsWire::ID_MISSION;
    entry.id = handle;
    entry.op = MirrorObjectsWire::OP_FULL;
    entry.full = state;
}

// The packet a batch fills: one static packet (kept off the stack) for the batch in use; a batch made while another of
// the same kind is in use (none does today) gets its own on the heap instead of emptying the other's.
template <typename TPacket>
class CBatchPacket
{
public:
    CBatchPacket() : m_bOwn(ms_bInUse)
    {
        assert(!ms_bInUse && "mission object batches don't nest");
        if (m_bOwn)
        {
            m_pPacket = new TPacket{};
            LogLine("event=batch-nested (own packet)");
        }
        else
        {
            static TPacket shared{};
            m_pPacket = &shared;
            ms_bInUse = true;
        }
        m_pPacket->count = 0;
    }

    ~CBatchPacket()
    {
        if (m_bOwn)
        {
            delete m_pPacket;
        }
        else
        {
            ms_bInUse = false;
        }
    }

    CBatchPacket(const CBatchPacket&) = delete;
    CBatchPacket& operator=(const CBatchPacket&) = delete;

    TPacket& Get() { return *m_pPacket; }

private:
    static inline bool ms_bInUse = false;
    TPacket* m_pPacket = nullptr;
    bool m_bOwn;
};

// MISSION_OBJECTS packets of one target, filled entry by entry; a full packet goes out and the next one continues
// (bReset only on the first)
class CBatch
{
public:
    CBatch(int targetPlayerId, bool bReset) : m_nTarget(targetPlayerId), m_bReset(bReset) {}

    MirrorObjectsWire::SObjectEntry& Next()
    {
        Packets::Mirror::MirrorObjects& packet = m_packet.Get();
        if (packet.count >= MirrorObjectsWire::MAX_ENTRIES)
        {
            Send();
        }
        MirrorObjectsWire::SObjectEntry& entry = packet.entries[packet.count++];
        entry = MirrorObjectsWire::SObjectEntry{};
        return entry;
    }

    // bEvenEmpty: a reset goes out even without entries
    void Flush(bool bEvenEmpty)
    {
        if (m_packet.Get().count > 0 || (bEvenEmpty && !m_bSentAny))
        {
            Send();
        }
    }

private:
    void Send()
    {
        Packets::Mirror::MirrorObjects& packet = m_packet.Get();
        packet.targetPlayerId = m_nTarget;
        packet.bReset = m_bReset && !m_bSentAny;
        GetPacketFactory().Send(packet);
        ++ms_counters.sentPackets;
        m_bSentAny = true;
        packet.count = 0;
    }

    CBatchPacket<Packets::Mirror::MirrorObjects> m_packet;
    int m_nTarget;
    bool m_bReset;
    bool m_bSentAny = false;
};

// MISSION_OBJECT_MOTION, filled sample by sample
class CMotionBatch
{
public:
    MirrorObjectsWire::SMotionEntry& Next()
    {
        Packets::Mirror::MirrorObjectMotion& packet = m_packet.Get();
        if (packet.count >= MirrorObjectsWire::MAX_MOTION_ENTRIES)
        {
            Send();
        }
        MirrorObjectsWire::SMotionEntry& entry = packet.entries[packet.count++];
        entry = MirrorObjectsWire::SMotionEntry{};
        return entry;
    }

    void Flush()
    {
        if (m_packet.Get().count > 0)
        {
            Send();
        }
    }

private:
    void Send()
    {
        Packets::Mirror::MirrorObjectMotion& packet = m_packet.Get();
        GetPacketFactory().Send(packet);
        ++ms_counters.sentMotionPackets;
        packet.count = 0;
    }

    CBatchPacket<Packets::Mirror::MirrorObjectMotion> m_packet;
};

// The whole owned set with bReset (design 5.10): to a player who joined, or to everyone (-1) after this game restarted.
// What everyone got becomes the base of the next differences. FindHolders has run.
void SendSnapshot(int targetPlayerId)
{
    const uint32_t now = GetTickCount();
    CBatch batch(targetPlayerId, true);
    int count = 0;
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        SOwnedObject& owned = ms_owned.At(i);
        CObject* pObject = CPools::GetObject(owned.handle);
        SObjectFull state;
        if (pObject == nullptr || !CaptureObject(pObject, owned, true, state))
        {
            continue;  // the next script frame removes it, or sends it once it can be read
        }
        FillFull(batch.Next(), owned.handle, state);
        ++count;
        if (targetPlayerId < 0)
        {
            MirrorObjectsPolicy::OnFullSent(owned, state, now);
        }
    }
    batch.Flush(true);
    ms_counters.sentFull += static_cast<unsigned int>(count);
    if (targetPlayerId < 0)
    {
        ++ms_counters.broadcasts;
    }
    else
    {
        ++ms_counters.snapshots;
    }
    LogLine("event=snapshot target=%d objects=%d", targetPlayerId, count);
}

void SendPendingSnapshots()
{
    if (ms_bBroadcastPending)
    {
        ms_bBroadcastPending = false;
        if (!IsAlone())
        {
            SendSnapshot(-1);
        }
    }
    for (int playerId = 0; playerId < Config::MAX_SERVER_PLAYERS && ms_nSnapshotsPending != 0; playerId++)
    {
        const uint32_t bit = 1u << playerId;
        if ((ms_nSnapshotsPending & bit) == 0)
        {
            continue;
        }
        ms_nSnapshotsPending &= ~bit;
        if (CNetworkPlayerManager::GetPlayer(playerId) != nullptr)
        {
            SendSnapshot(playerId);
        }
    }
}

// once per script frame: removals and changed fields
void ProcessHost(uint32_t now)
{
    const bool bCanSend = CanSend();
    const bool bAlone = IsAlone();
    FindHolders();
    if (bCanSend)
    {
        SendPendingSnapshots();
    }

    CBatch batch(-1, false);
    for (int i = 0; i < ms_owned.Count();)
    {
        SOwnedObject& owned = ms_owned.At(i);
        CObject* pObject = CPools::GetObject(owned.handle);
        if (pObject == nullptr)
        {
            // deleted (delete_object, the population's clean-up of a released one, a restart) or its slot reused
            const bool bSend = owned.bSent && bCanSend && !bAlone;
            if (bSend)
            {
                MirrorObjectsWire::SObjectEntry& entry = batch.Next();
                entry.idKind = MirrorObjectsWire::ID_MISSION;
                entry.id = owned.handle;
                entry.op = MirrorObjectsWire::OP_REMOVED;
                ++ms_counters.sentRemoved;
            }
            LogLine("event=removed handle=%05X sent=%d", owned.handle, bSend);
            ms_owned.RemoveAt(i);
            continue;
        }

        SObjectFull state;
        if (!CaptureObject(pObject, owned, true, state))
        {
            ++ms_counters.captureFailures;
            i++;
            continue;
        }
        if (bAlone)
        {
            MirrorObjectsPolicy::OnFullSent(owned, state, now);  // nobody to tell; a joiner gets the snapshot
        }
        else if (bCanSend && (!owned.bSent || !MirrorObjectsPolicy::SameFields(owned.sent, state)))
        {
            if (owned.bSent)
            {
                LogLine("event=changed handle=%05X model=%d flags=%04X parent=%u/%d holder=%u/%d", owned.handle,
                    state.model, state.flags, state.parent.kind, state.parent.id, state.holder.kind, state.holder.id);
            }
            FillFull(batch.Next(), owned.handle, state);
            MirrorObjectsPolicy::OnFullSent(owned, state, now);
            ++ms_counters.sentFull;
        }
        i++;
    }
    batch.Flush(false);
}

// once per frame after CGame::Process (physics done): motion samples and resting FULLs
void ProcessHostMotion(uint32_t now)
{
    if (ms_owned.Count() == 0)
    {
        return;
    }
    const bool bSend = CanSend() && !IsAlone();
    FindHolders();
    CBatch full(-1, false);
    CMotionBatch motion;
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        SOwnedObject& owned = ms_owned.At(i);
        CObject* pObject = CPools::GetObject(owned.handle);
        SObjectFull state;
        if (pObject == nullptr || !CaptureObject(pObject, owned, true, state))
        {
            continue;  // the script frame removes it
        }

        const float speed = MirrorObjectsPolicy::Length(state.moveSpeed.x, state.moveSpeed.y, state.moveSpeed.z) *
            MirrorObjectsPolicy::SPEED_UNITS_PER_SECOND;
        const float turnRate = MirrorObjectsPolicy::Length(state.turnSpeed.x, state.turnSpeed.y, state.turnSpeed.z) *
            MirrorObjectsPolicy::SPEED_UNITS_PER_SECOND;
        owned.movement.Update(state.position, state.rotation, speed, turnRate, now);

        if (!bSend || !owned.bSent)
        {
            continue;  // the script frame sends its FULL first
        }
        const bool bStatic = (state.flags & MirrorObjectsWire::FLAG_STATIC) != 0;
        MirrorObjectsPolicy::SMotionInput input;
        input.bFollowsParent = MirrorObjectsPolicy::FollowsParent(state);
        input.bBecameStatic = bStatic && !owned.bSampleStatic;
        input.bMotionActive = owned.bMotionActive;
        input.speed = speed;
        input.turnRate = turnRate;
        input.movedSinceSample = MirrorObjectsPolicy::Distance(owned.samplePosition, state.position);
        input.turnedSinceSample = MirrorObjectsPolicy::AngleBetweenDegrees(owned.sampleRotation, state.rotation);
        input.msSinceSample = now - owned.sampledAt;
        input.msStill = now - owned.movement.lastMovingAt;
        switch (MirrorObjectsPolicy::DecideMotion(input))
        {
            case eMotion::SAMPLE:
            {
                MirrorObjectsWire::SMotionEntry& entry = motion.Next();
                entry.idKind = MirrorObjectsWire::ID_MISSION;
                entry.id = owned.handle;
                entry.epoch = owned.epoch;
                entry.position = state.position;
                entry.rotation = state.rotation;
                entry.moveSpeed = state.moveSpeed;
                entry.turnSpeed = state.turnSpeed;
                MirrorObjectsPolicy::OnSampleSent(owned, state.position, state.rotation, bStatic, now);
                ++ms_counters.sentMotion;
                break;
            }
            case eMotion::REST_FULL:
                FillFull(full.Next(), owned.handle, state);
                MirrorObjectsPolicy::OnFullSent(owned, state, now);
                ++ms_counters.sentFull;
                ++ms_counters.sentRest;
                break;
            default:
                break;
        }
    }
    full.Flush(false);
    motion.Flush();
}

void ForgetOwned(const char* why)
{
    if (ms_owned.Count() > 0)
    {
        LogLine("event=forget-owned why=%s count=%d", why, ms_owned.Count());
    }
    ms_owned.Clear();
}

// ---- the other games ----

void DropRecords(const char* why)
{
    if (ms_records.Count() == 0)
    {
        return;
    }
    LogLine("event=drop-records why=%s count=%d", why, ms_records.Count());
    ms_records.Clear();
    CMirrorObjectCopies::OnRecordsCleared(why);
}

// ---- both ----

// This game restarted: on the host its objects were deleted (CWorld::ClearForRestart, design 3.2), so nothing it owned
// is there any more and the others drop what they have of it (its whole set, empty now, with bReset); on the others the
// copies were deleted, the records stay.
void OnLocalRestart(const char* why)
{
    ForgetOwned(why);
    if (CSessionLifecycle::IsHost())
    {
        ms_bBroadcastPending = true;
    }
    else
    {
        LogLine("event=local-restart why=%s", why);
        CMirrorObjectCopies::OnLocalRestart();
    }
}

// the frame counter starts over with a new game or a load (CTimer::Initialise 0x5617E0 zeroes CTimer::m_FrameCounter
// 0xB7CB4C; CSessionPolicy.h). Checked at every entry point, before the GAME_RESTARTED event of the same frame.
void CheckRestart()
{
    const uint32_t frame = CTimer::m_FrameCounter;
    if (frame < ms_nLastFrame)
    {
        OnLocalRestart("frame-counter");
        ms_bRestartSeen = true;
        ms_nRestartFrame = frame;
    }
    ms_nLastFrame = frame;
}

void OnSessionEvent(const SSessionEvent& event)
{
    CheckRestart();
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            if (CMirrorObjects::IsEnabled() && CSessionLifecycle::IsHost() && event.playerId >= 0 &&
                event.playerId < Config::MAX_SERVER_PLAYERS)
            {
                // the takeover's events come after the first server-time answer; a join before it waits for it
                ms_nSnapshotsPending |= 1u << event.playerId;
                if (CanSend())
                {
                    FindHolders();
                    SendPendingSnapshots();
                }
            }
            break;
        case eSessionEvent::PLAYER_LEFT:
            if (event.bWasHost)
            {
                DropRecords("host-left");
            }
            break;
        case eSessionEvent::HOST_CHANGED:
            // a new host starts empty; this game, if it is the new host, drops the old host's records
            DropRecords("host-changed");
            if (!event.bIsLocal)
            {
                ForgetOwned("host-changed");
            }
            ms_nSnapshotsPending = 0;
            ms_bBroadcastPending = false;
            break;
        case eSessionEvent::DISCONNECTED:
            DropRecords("disconnected");
            ForgetOwned("disconnected");
            ms_nSnapshotsPending = 0;
            ms_bBroadcastPending = false;
            break;
        case eSessionEvent::GAME_RESTARTED:
            // the restart CheckRestart saw in this frame already, or a load right after start-up that didn't move the
            // frame counter back
            if (!ms_bRestartSeen || ms_nRestartFrame != CTimer::m_FrameCounter)
            {
                OnLocalRestart(event.bLoaded ? "game-loaded" : "new-game");
            }
            ms_bRestartSeen = false;
            break;
        default:
            break;
    }
}

// Subscribes on first use (a script command, the first script frame or a packet), not from CCore::Init, which runs
// during the DLL's static initialization (as CWorldStateRegistry does).
void EnsureStarted()
{
    if (ms_bStarted)
    {
        return;
    }
    ms_bStarted = true;
    ms_nLastFrame = CTimer::m_FrameCounter;
    CSessionLifecycle::Subscribe(OnSessionEvent);
    LogLine("event=config mission-objects=%s", CMirrorObjects::IsEnabled() ? "true" : "false");
}
}  // namespace

void CMirrorObjects::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    if (!MirrorObjectsPolicy::IsCreatingCommand(opcode) || !IsEnabled() || !CMissionMirror::IsFamilyScript(pScript))
    {
        return;
    }
    EnsureStarted();
    CheckRestart();

    const int32_t handle = static_cast<int32_t>(ScriptParams[0]);
    CObject* pObject = MirrorObjectsPolicy::IsObjectHandle(handle) ? CPools::GetObject(handle) : nullptr;
    if (pObject == nullptr || !ms_owned.Add(handle))
    {
        if (!ms_owned.Contains(handle))
        {
            ++ms_counters.ownFailures;
            LogLine("event=own-failed handle=%08X opcode=%04X script=%.8s", static_cast<uint32_t>(handle), opcode,
                pScript->m_szName);
        }
        return;
    }
    ++ms_counters.ownedTotal;
    ms_counters.owned = static_cast<unsigned int>(ms_owned.Count());

    // the creation goes out at once, before any later command of this frame names the object (design 5.3)
    SOwnedObject& owned = ms_owned.At(ms_owned.IndexOf(handle));
    SObjectFull state;
    const bool bCaptured = CaptureObject(pObject, owned, false, state);
    const bool bAlone = IsAlone();
    bool bSent = false;
    if (bCaptured && bAlone)
    {
        MirrorObjectsPolicy::OnFullSent(owned, state, GetTickCount());
    }
    else if (bCaptured && CanSend())
    {
        CBatch batch(-1, false);
        FillFull(batch.Next(), handle, state);
        batch.Flush(false);
        MirrorObjectsPolicy::OnFullSent(owned, state, GetTickCount());
        ++ms_counters.sentFull;
        bSent = true;
    }
    LogLine("event=own handle=%05X opcode=%04X script=%.8s model=%d sent=%d", handle, opcode, pScript->m_szName,
        state.model, bSent);
}

void CMirrorObjects::Process()
{
    EnsureStarted();
    CheckRestart();

    const uint32_t now = GetTickCount();
    if (!IsEnabled())
    {
        // the kill switch: nothing owned, sent, kept or copied (it is read once at start-up, so this stays empty)
        DropRecords("disabled");
        ForgetOwned("disabled");
        ms_nSnapshotsPending = 0;
        ms_bBroadcastPending = false;
    }
    else if (CSessionLifecycle::IsHost())
    {
        DropRecords("this-game-is-host");  // (HOST_CHANGED has done it)
        ProcessHost(now);
    }
    else
    {
        ForgetOwned("not-host");  // (HOST_CHANGED has done it)
        // before CWorld::Process: a holder ped deleted since the last frame (a packet, ProcessPeds, a stand-in) flagged
        // the copy it held for deletion there (see CMirrorObjectCopies.cpp)
        CMirrorObjectCopies::ClearRemoveFlags();
    }
    ms_counters.owned = static_cast<unsigned int>(ms_owned.Count());
    ms_counters.records = static_cast<unsigned int>(ms_records.Count());
}

bool CMirrorObjects::IsEnabled()
{
    return CConfigLoader::ms_bMissionObjects;
}

void CMirrorObjects::ProcessMotion()
{
    EnsureStarted();
    CheckRestart();

    if (!IsEnabled())
    {
        return;
    }
    if (CSessionLifecycle::IsHost())
    {
        ProcessHostMotion(GetTickCount());
    }
    else
    {
        CMirrorObjectCopies::Process();
    }
}

void CMirrorObjects::HandlePacket(const Packets::Mirror::MirrorObjects& packet)
{
    EnsureStarted();
    CheckRestart();
    ++ms_counters.recvPackets;

    if (!IsEnabled())
    {
        ++ms_counters.recvDisabled;
        return;
    }
    const int sender = packet.playerid.value;
    if (!MirrorObjectsPolicy::AcceptFrom(
            CSessionLifecycle::IsHost(), CSessionLifecycle::HasHost(), CSessionLifecycle::GetHostId(), sender))
    {
        if (ms_counters.recvDropped++ < 16)
        {
            LogLine("event=drop-packet sender=%d reset=%d count=%d (not from the current host)", sender, packet.bReset,
                packet.count);
        }
        return;
    }

    if (packet.bReset)
    {
        DropRecords("reset");
        ++ms_counters.recvResets;
        LogLine("event=reset sender=%d target=%d count=%d", sender, packet.targetPlayerId, packet.count);
    }
    for (int i = 0; i < packet.count && i < MirrorObjectsWire::MAX_ENTRIES; i++)
    {
        const MirrorObjectsWire::SObjectEntry& entry = packet.entries[i];
        SObjectRecord affected;
        switch (ms_records.Apply(entry, affected))
        {
            case eApplied::CREATED:
                ++ms_counters.recvCreated;
                if (SObjectRecord* pRecord = ms_records.Find(entry.idKind, entry.id))
                {
                    pRecord->fullServerTime = packet.serverTime;  // the host's clock, as the motion samples' stamps
                    CMirrorObjectCopies::OnRecordFull(*pRecord, true);
                }
                break;
            case eApplied::UPDATED:
                ++ms_counters.recvUpdated;
                if (SObjectRecord* pRecord = ms_records.Find(entry.idKind, entry.id))
                {
                    pRecord->fullServerTime = packet.serverTime;
                    CMirrorObjectCopies::OnRecordFull(*pRecord, false);
                }
                break;
            case eApplied::REMOVED:
                ++ms_counters.recvRemoved;
                CMirrorObjectCopies::OnRecordRemoved(affected);
                break;
            case eApplied::OWNER_CHANGED:
                ++ms_counters.recvOwner;
                if (const SObjectRecord* pRecord = ms_records.Find(entry.idKind, entry.id))
                {
                    CMirrorObjectCopies::OnOwnerChanged(*pRecord);
                }
                break;
            case eApplied::OWNER_SAME:
                ++ms_counters.recvOwner;
                break;
            case eApplied::UNKNOWN:
                ++ms_counters.recvUnknown;
                break;
            case eApplied::TABLE_FULL:
                if (ms_counters.recvTableFull++ == 0)
                {
                    LogLine("event=table-full id=%u/%05X", entry.idKind, entry.id);
                }
                break;
        }
    }
    ms_counters.records = static_cast<unsigned int>(ms_records.Count());
}

void CMirrorObjects::HandleMotion(const Packets::Mirror::MirrorObjectMotion& packet)
{
    EnsureStarted();
    CheckRestart();
    ++ms_counters.recvMotionPackets;

    if (!IsEnabled())
    {
        ++ms_counters.recvDisabled;
        return;
    }
    const int sender = packet.playerid.value;
    const bool bIsHost = CSessionLifecycle::IsHost();
    const bool bHasHost = CSessionLifecycle::HasHost();
    const int hostId = CSessionLifecycle::GetHostId();
    for (int i = 0; i < packet.count && i < MirrorObjectsWire::MAX_MOTION_ENTRIES; i++)
    {
        const MirrorObjectsWire::SMotionEntry& entry = packet.entries[i];
        SObjectRecord* pRecord = ms_records.Find(entry.idKind, entry.id);
        if (pRecord == nullptr && !bIsHost)
        {
            ++ms_counters.recvMotionUnknown;
            continue;
        }
        if (!MirrorObjectsPolicy::AcceptMotion(bIsHost, bHasHost, hostId, pRecord, sender, entry.epoch))
        {
            ++ms_counters.recvMotionDropped;
            continue;
        }
        MirrorObjectsPolicy::SMotionSample sample;
        sample.serverTime = packet.serverTime;
        sample.position = entry.position;
        sample.rotation = entry.rotation;
        sample.moveSpeed = entry.moveSpeed;
        sample.turnSpeed = entry.turnSpeed;
        if (!MirrorObjectsPolicy::AddSample(*pRecord, sample))
        {
            ++ms_counters.recvMotionLate;
            continue;
        }
        ++ms_counters.recvMotion;
        CMirrorObjectCopies::OnMotion(*pRecord);
    }
}

bool CMirrorObjects::IsOwnedObject(int32_t hostHandle)
{
    return CSessionLifecycle::IsHost() && ms_owned.Contains(hostHandle);
}

CObject* CMirrorObjects::ResolveObjectCopy(int32_t hostHandle)
{
    return CMirrorObjectCopies::FindCopy(hostHandle);
}

void CMirrorObjects::Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogLineV(format, args);
    va_end(args);
}

const MirrorObjectsPolicy::CRecordTable& CMirrorObjects::GetRecords()
{
    return ms_records;
}

const MirrorObjectsPolicy::SObjectRecord* CMirrorObjects::FindRecord(uint8_t idKind, int32_t id)
{
    return ms_records.Find(idKind, id);
}

void CMirrorObjects::WriteDiagSummary(FILE* file)
{
    const SCounters& c = ms_counters;
    fprintf(file,
        "  objects: enabled=%d owned=%u total=%u ownFail=%u sent full=%u rest=%u removed=%u motion=%u packets=%u "
        "motionPackets=%u snapshots=%u broadcasts=%u captureFail=%u parentUnknown=%u holderUnknown=%u | recv records=%u "
        "packets=%u dropped=%u disabled=%u resets=%u created=%u updated=%u removed=%u owner=%u unknown=%u tableFull=%u "
        "motionPackets=%u motion=%u motionDropped=%u motionUnknown=%u motionLate=%u",
        CConfigLoader::ms_bMissionObjects ? 1 : 0, c.owned, c.ownedTotal, c.ownFailures, c.sentFull, c.sentRest,
        c.sentRemoved, c.sentMotion, c.sentPackets, c.sentMotionPackets, c.snapshots, c.broadcasts, c.captureFailures,
        c.parentsUnknown, c.holdersUnknown, c.records, c.recvPackets, c.recvDropped, c.recvDisabled, c.recvResets,
        c.recvCreated, c.recvUpdated, c.recvRemoved, c.recvOwner, c.recvUnknown, c.recvTableFull, c.recvMotionPackets,
        c.recvMotion, c.recvMotionDropped, c.recvMotionUnknown, c.recvMotionLate);
    CMirrorObjectCopies::WriteDiagSummary(file);  // ends the line
}
