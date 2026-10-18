#include "stdafx.h"
#include "CMirrorObjectCopies.h"
#include "CMirrorObjectCopiesPolicy.h"
#include "CMirrorObjects.h"
#include "CServerTime.h"
#include "CSessionLifecycle.h"
#include <CObject.h>
#include <CPools.h>
#include <CTaskSimpleHoldEntity.h>
#include <CTimer.h>
#include <ePedBones.h>

// Private build: the receivers' copies of the host's mission objects, see CMirrorObjectCopies.h.
//
// gta_sa.exe 1.0 US used below (bytes read with dumpbin unless named otherwise):
// - Creation repeats create_object 0107 (case 0x46976F) without its ground search and its clear-space call: the model
//   info's alpha +0x0C = 255 (0x469797), CObject::Create(model, 0) 0x5A1F60 (cdecl, two arguments: push 0 / push eax at
//   0x469794; plugin-sdk's wrapper passes one), the type +0x13C (6 = OBJECT_MISSION2, the type 0107 gives an external
//   script's objects: the population never deletes it, CObject::CanBeDeleted 0x59F120; SaveObjectPool 0x5D4940 saves
//   only type 2; DeleteAllMissionObjects 0x5A1910, called only from CWorld::ClearForRestart, deletes it), the position
//   (0x420B80) and orientation (0x439A80), CEntity::UpdateRwMatrix 0x446F90 and UpdateRwFrame 0x532B00, the model
//   info's vtable +0x0C (AsLodAtomicModelInfoPtr, plugin-sdk slot 3) deciding CEntity::SetupBigBuilding 0x533150, then
//   CWorld::Add 0x563220. No CTheScripts::ClearSpaceForMissionEntity 0x486B00 (it could delete this game's vehicles and
//   peds). CObject::Create's pool can fail (gta-reversed Object.cpp: a null second `new`), so a copy is only made while
//   the pool has room (CopiesPolicy::CanTakePoolSlot).
// - Deletion repeats delete_object 0108 (case 0x4698C2): CWorld::Remove 0x563280, CWorld::RemoveReferencesToDeletedObject
//   0x565510, then the deleting destructor (vtable slot 0 with 1: plugin-sdk's virtual ~CPlaceable makes `delete` call
//   it). ~CObject 0x59F660 clears the object's radar blip itself (CRadar::ClearBlipForEntity(3, ref) 0x587C60, call at
//   0x59F735), so CMirrorBlips finds no trace and builds its fallback again. Its ResolveReferences nulls every
//   registered pointer to it: an attachment's m_pAttachedTo, a hold task's entity.
// - Placing a copy repeats CObject::Teleport 0x5A17B0 (the vtable +0x38 call set_object_coordinates 01BC makes, case
//   0x47D5C9 at 0x47D65F; gta-reversed Object.cpp): CWorld::Remove, the new matrix, CWorld::Add (which updates the RW
//   matrix and frame, gta-reversed World.cpp).
// - Static and dynamic: SetIsStatic (vtable +0x10) with AddToMovingList 0x542800 / RemoveFromMovingList 0x542860, as
//   make_object_dynamic 0392 (case 0x4841E3, design 3.3).
// - Attachments: CPhysical::AttachEntityToEntity 0x54D570 (Euler: offset, angles; zeroes the quaternion) and 0x54D690
//   (offset pointer, quaternion pointer; relative to the fork frame on a forklift), plugin-sdk CPhysical.cpp; both
//   register a reference to the parent (gta-reversed Physical.cpp). Neither the engine's detach 0x5442F0 nor
//   DettachAutoAttachedEntity 0x5446A0 removes it, so a copy's own detach first calls CEntity::CleanUpOldReference
//   0x571A00 on the parent (removes the first matching reference, gta-reversed Entity.cpp), then
//   DettachAutoAttachedEntity (plugin-sdk; reads the parent, so only while it is set).
// - Hold task: CTask::operator new 0x61A5A0 (plugin-sdk CTask.cpp), CTaskSimpleHoldEntity's constructor 0x6913A0 (a
//   stub; body 0x6913A7, thiscall, 7 arguments, ret 1Ch: entity +0x08, offset +0x0C, bone +0x18, bone flags +0x19, anim
//   id +0x20, group +0x24, disallow-drop +0x36; it sets the entity's +0x1C bit 0x400 and registers a reference to it at
//   0x69143E), the type 0x133 (GetTaskType 0x691460), CTaskManager::SetTaskSecondary 0x681B60 (deletes the slot's old
//   task through its vtable slot 0, 0x681B7D) in slot 4 (TASK_SECONDARY_PARTIAL_ANIM, plugin-sdk CTaskManager.h; the
//   slot CPedIntelligence::GetTaskHold 0x600FF0 reads). ~CTaskSimpleHoldEntity (body 0x691630) clears the held
//   entity's +0x1C bit 0x400 and sets its m_bRemoveFromWorld (bit 0x800) once the task ran, and CWorld::Process deletes
//   a moving non-ped entity with that flag (gta-reversed World.cpp). So our own task is ended with
//   CTaskSimpleHoldEntity::ReleaseEntity 0x6916E0 first (CleanUpOldReference 0x571A00 at 0x6916EB, then the pointer is
//   zeroed at 0x6916F0; it leaves bit 0x400, which is cleared here), then MakeAbortable (vtable slot 6, plugin-sdk
//   CTask.h; URGENT blends the animation out) and SetTaskSecondary(nullptr, 4). A holder ped that goes away deletes its
//   task with it, so every copy's flag is cleared twice a frame: in CMirrorObjects::Process (the end of the script
//   frame, before CWorld::Process, for peds deleted by packets, CNetworkPlayerManager::ProcessPeds or a stand-in since
//   the last frame) and in Process (after CGame::Process).
// - CPedIntelligence::FlushImmediately 0x601640 (CNetworkPed/CNetworkPlayer warps, CDepartedPlayerPeds, CPed::Teleport
//   for players) clones a slot-4 task of type 0x133 (vtable +4), flushes the original and puts the clone in the slot:
//   any type-0x133 task in the holder's slot 4 whose entity (+0x08) is the copy counts as ours (adopted).
// - Breaks repeat break_object 0723 (case 0x46D754): a glass object (CGlass::IsObjectGlass 0x46A760, cdecl) gets
//   CGlass::WindowRespondsToCollision 0x71BC40 (cdecl: entity, 99999.9 (push 47C34FF3h at 0x46D7D1), zero speed, its
//   position, false; add esp,24h) and bit 0x400 of +0x140; any other gets BreakManager_c::Add 0x59E9B0 (thiscall on
//   g_breakMan 0xBB4240: object, &objectInfo->m_vecBreakVelocity (+0x38), m_fBreakVelocityRand (+0x44), smash; the
//   script's second parameter, sent as false here: the wire doesn't carry it), loses collision and visibility
//   (and edx,0FFFFFF7Eh at 0x46D846), is taken off the moving list, gets physical flag 0x800000, the broken bit 0x400,
//   the static bit, zero speeds and its RwObject deleted (vtable +0x20, DeleteRwObject, at 0x46D89D).
// - The hand fallback: CPed::GetTransformedBonePosition 0x5E01C0 (plugin-sdk CPed.cpp) at BONE_RIGHTHAND 25 (plugin-sdk
//   ePedBones.h).
// - A hold task's anim: CAnimManager::ms_aAnimAssocGroups is a pointer at 0xB4EA34 (CAnimManager::GetAnimAssociation
//   0x4D3A60: mov edx,[0B4EA34h], then group * 0x14; plugin-sdk declares the array at that address instead), the
//   group count ms_numAnimAssocDefinitions at 0xB4EA28 (CreateAnimAssocGroups 0x4D3CC3 loops to it); a group
//   (CAnimBlendAssocGroup, 0x14 bytes, plugin-sdk) has its associations at +0x04, their count at +0x08 and its first
//   anim id at +0x0C (CAnimBlendAssocGroup::GetAnimation 0x4CE090: [ecx+4] + (id - [ecx+0Ch]) * 0x14, unchecked).
//   The associations exist only while the group's anim block is loaded (gta-reversed CreateAnimAssocGroups,
//   RemoveAnimBlock). CTaskSimpleHoldEntity::StartAnim 0x692FF0 (bytes) for a group other than 0 without a block yet:
//   the block is the group's +0x00 (mov edi,[ecx+edx*4] at 0x69303D), else GetAnimationBlock(GetAnimBlockName(group))
//   (cdecl 0x4D3A30, then cdecl 0x4D3940 by name); its index = (block - 0xB5D4A0) >> 5 (CAnimBlock is 0x20 bytes,
//   0x69305A); loaded = byte +0x10; not loaded: CStreaming::RequestModel(index + 0x63E7 (IFPToModelId), 8 =
//   KEEP_IN_MEMORY) 0x4087E0 and return (0x6930CC-0x6930D4), loaded: AddAnimBlockRef 0x4D3FB0 (the task's destructor
//   removes it). Nothing in the task releases KEEP_IN_MEMORY, so the copies request the block the same way and don't
//   either; the task we give then takes and drops its own block reference.
// - Clone 0x6929B0 of a hold task made with an anim id passes disallow-drop false (push 0 at 0x692A43): an adopted
//   clone is replaced by our own task when the host's has it set.
// - A forklift or dumper can attach a copy on its own (CObject::ProcessControl, gta-reversed Object.cpp): outside the
//   attach link a copy is detached again.
//
// Closest-object queries (design 5.11): this game's scripts never get a copy's handle (no script creates them, no story
// mission runs here), but the engine's own searches can find one: "closest object of model" commands (0363, 0716,
// 07F0, 088D, 0985) and GetClosestStealableObject (0866, the burglary side mission). A copy found that way behaves as
// the host's object would (it can even be picked up: the engine's own pick-up, FindObjectToSteal 0x56DBD0, sees its
// liftable flag); a hand-over is OB-3. A copy this game's player holds is left to the hold (no motion, no put-back).

static_assert(offsetof(CObjectInfo, m_vecBreakVelocity) == 0x38, "CObjectInfo::m_vecBreakVelocity");
static_assert(offsetof(CObjectInfo, m_fBreakVelocityRand) == 0x44, "CObjectInfo::m_fBreakVelocityRand");
static_assert(offsetof(CBaseModelInfo, m_nAlpha) == 0x0C, "CBaseModelInfo::m_nAlpha");

namespace
{
namespace CopiesPolicy = MirrorObjectCopiesPolicy;
using CopiesPolicy::eLink;
using MirrorObjectsPolicy::SObjectRecord;
using MirrorObjectsWire::SHolder;
using MirrorObjectsWire::SParent;
using MirrorObjectsWire::SPosition;
using MirrorObjectsWire::SRotation;

// raw fields, as CMirrorObjects reads them (its comment has the sources)
constexpr size_t ENTITY_FLAGS = 0x1C;
constexpr size_t PHYSICAL_FLAGS = 0x40;
constexpr size_t OBJECT_FLAGS = 0x140;
constexpr uint32_t ENTITY_USES_COLLISION = 0x01;
constexpr uint32_t ENTITY_STATIC = 0x04;
constexpr uint32_t ENTITY_VISIBLE = 0x80;
constexpr uint32_t ENTITY_RENDER_DAMAGED = 0x200;
constexpr uint32_t ENTITY_STREAMING_DONT_DELETE = 0x400;  // set by the hold task's constructor (0x691434)
constexpr uint32_t PHYSICAL_DISABLE_COLLISION_FORCE = 0x0004;
constexpr uint32_t PHYSICAL_INFINITE_MASS = 0x0040;
constexpr uint32_t PHYSICAL_DONT_APPLY_SPEED = 0x2000;
constexpr uint32_t PHYSICAL_BREAK_FLAG = 0x800000;  // set by 0723's break (or ecx,800000h at 0x46D86D)
constexpr uint32_t OBJECT_BROKEN = 0x0400;
constexpr uint32_t OBJECT_LIFTABLE = 0x2000;
constexpr size_t HOLD_ENTITY = 0x08;
constexpr int HOLD_TASK_SIZE = 0x3C;     // 070A's operator new size (design 3.4)
constexpr int HOLD_TASK_TYPE = 0x133;    // TASK_SIMPLE_HOLD_ENTITY (GetTaskType 0x691460: mov eax,133h)
constexpr int HOLD_SLOT = 4;             // TASK_SECONDARY_PARTIAL_ANIM
constexpr float GLASS_DAMAGE = 99999.9f; // 0x47C34FF3
constexpr uintptr_t BREAK_MANAGER = 0xBB4240;
// the anim groups (see the comment at the top)
constexpr uintptr_t ANIM_ASSOC_GROUPS = 0xB4EA34;
constexpr uintptr_t ANIM_ASSOC_GROUP_COUNT = 0xB4EA28;
constexpr size_t ANIM_GROUP_SIZE = 0x14;
constexpr size_t ANIM_GROUP_ASSOCIATIONS = 0x04;
constexpr size_t ANIM_GROUP_NUM_ANIMS = 0x08;
constexpr size_t ANIM_GROUP_FIRST_ID = 0x0C;
constexpr size_t ANIM_GROUP_BLOCK = 0x00;
constexpr uintptr_t ANIM_BLOCKS = 0xB5D4A0;       // CAnimManager::ms_aAnimBlocks
constexpr uintptr_t ANIM_BLOCK_COUNT = 0xB4EA30;  // CAnimManager::ms_numAnimBlocks
constexpr int ANIM_BLOCK_SIZE_SHIFT = 5;          // 0x20 bytes each
constexpr size_t ANIM_BLOCK_LOADED = 0x10;
constexpr int IFP_MODEL_BASE = 0x63E7;            // IFPToModelId
constexpr int STREAMING_KEEP_IN_MEMORY = 8;
constexpr size_t HOLD_DISALLOW_DROP = 0x36;

template <typename T>
T ReadField(const void* base, size_t offset)
{
    T value{};
    memcpy(&value, reinterpret_cast<const uint8_t*>(base) + offset, sizeof(T));
    return value;
}

void SetBits(void* base, size_t offset, uint32_t mask, bool bOn)
{
    uint32_t value = ReadField<uint32_t>(base, offset);
    value = bOn ? (value | mask) : (value & ~mask);
    memcpy(reinterpret_cast<uint8_t*>(base) + offset, &value, sizeof(value));
}

bool HasBits(const void* base, size_t offset, uint32_t mask)
{
    return (ReadField<uint32_t>(base, offset) & mask) != 0;
}

// One copy: this game's object for one of the host's records, and how it follows it.
struct SCopy
{
    bool bUsed = false;
    uint8_t idKind = MirrorObjectsWire::ID_MISSION;
    int32_t id = 0;

    CObject* pObject = nullptr;   // checked through objectRef (the pool slot's id byte) before every use
    int objectRef = -1;
    int model = -1;
    CopiesPolicy::CRetryThrottle createThrottle;
    unsigned createAttempts = 0;  // since the last success
    bool bBroken = false;         // the copy is broken (the break was replayed, or it was made broken)

    // motion
    bool bFullPending = true;     // put it at the record's FULL (a FULL arrived, or it came back to its own transform)
    uint32_t lastSampleTime = 0;  // the sample a dynamic copy took last
    bool bSampleApplied = false;
    CopiesPolicy::CRestWatch rest;
    bool bLocalHold = false;      // this game's player holds it (the engine's pick-up)

    // parent and holder
    eLink link = eLink::WORLD;
    CEntity* pParent = nullptr;   // what it was attached to (compared with the parent resolved now, never read)
    SParent attachedTo{};         // the record's parent then
    CopiesPolicy::CRetryThrottle attachThrottle;
    CPed* pHolder = nullptr;      // the ped our hold task was given to, checked through holderRef
    int holderRef = -1;
    CTask* pHoldTask = nullptr;   // our task (compared with the ped's slot, read only while it is there)
    SHolder heldAs{};             // the record's holder then
    CopiesPolicy::CRetryThrottle holdThrottle;
    SHolder throttledFor{};       // the record's holder the throttle counts for (a new one starts it over)
    bool bAnimRequested = false;  // the hold anim's block was requested (at animRequestedAt)
    uint32_t animRequestedAt = 0;
    bool bHandFlags = false;      // in the hand: collision off and static until it leaves
};

constexpr int MAX_COPIES = MirrorObjectsPolicy::MAX_RECORDS;
SCopy ms_copies[MAX_COPIES];

// --coopdiag (plain values; the diag thread reads them)
struct SCounters
{
    unsigned int copies = 0;          // live copies now
    unsigned int created = 0;
    unsigned int createFailed = 0;
    unsigned int retries = 0;         // creations after a failure or a lost copy
    unsigned int removed = 0;
    unsigned int lost = 0;            // this game's engine deleted one (found by the pool check)
    unsigned int snapped = 0;
    unsigned int putBack = 0;
    unsigned int attached = 0;
    unsigned int reattached = 0;
    unsigned int attachFallback = 0;
    unsigned int holdGiven = 0;
    unsigned int holdLost = 0;
    unsigned int holdFallback = 0;
    unsigned int holdAdopted = 0;     // the engine's clone of our task (FlushImmediately) taken as ours
    unsigned int breakReplayed = 0;
    unsigned int localHold = 0;
    unsigned int autoDetached = 0;    // attached by this game's engine outside the attach link (a forklift)
};
SCounters ms_counters;

// a few lines of a repeating event, then every 64th
bool ShouldLog(unsigned int count)
{
    return count <= 16 || (count % 64) == 0;
}

// ---- the copy and its pool slot ----

// the copy, or nullptr once the pool slot holds another object (or none): the engine deleted it
CObject* GetLiveCopy(SCopy& copy)
{
    if (copy.pObject == nullptr)
    {
        return nullptr;
    }
    if (copy.objectRef >= 0 && CPools::GetObject(copy.objectRef) == copy.pObject && copy.pObject->IsVTableValid())
    {
        return copy.pObject;
    }
    ++ms_counters.lost;
    CMirrorObjects::Log("event=copy-lost id=%05X ref=%08X link=%s", copy.id, static_cast<uint32_t>(copy.objectRef),
        CopiesPolicy::LinkName(copy.link));
    copy.pObject = nullptr;
    copy.objectRef = -1;
    copy.pParent = nullptr;
    copy.pHoldTask = nullptr;  // its ped may still be there; the task lost its entity with the copy (ResolveReferences)
    copy.pHolder = nullptr;
    copy.holderRef = -1;
    copy.link = eLink::WORLD;
    copy.bHandFlags = false;
    copy.bFullPending = true;
    return nullptr;
}

SCopy* FindEntry(uint8_t idKind, int32_t id)
{
    for (SCopy& copy : ms_copies)
    {
        if (copy.bUsed && copy.id == id && copy.idKind == idKind)
        {
            return &copy;
        }
    }
    return nullptr;
}

SCopy* AddEntry(uint8_t idKind, int32_t id)
{
    for (SCopy& copy : ms_copies)
    {
        if (!copy.bUsed)
        {
            copy = SCopy{};
            copy.bUsed = true;
            copy.idKind = idKind;
            copy.id = id;
            return &copy;
        }
    }
    return nullptr;
}

CPed* GetLiveHolder(SCopy& copy)
{
    if (copy.pHolder == nullptr)
    {
        return nullptr;
    }
    if (copy.holderRef >= 0 && CPools::GetPed(copy.holderRef) == copy.pHolder && copy.pHolder->IsVTableValid())
    {
        return copy.pHolder;
    }
    copy.pHolder = nullptr;
    copy.holderRef = -1;
    copy.pHoldTask = nullptr;  // deleted with the ped's intelligence
    return nullptr;
}

// ---- transforms ----

void ReadPose(const CObject* pObject, SPosition& position, SRotation& rotation)
{
    const CVector& at = pObject->GetPosition();
    position.x = at.x;
    position.y = at.y;
    position.z = at.z;
    rotation = SRotation{};
    if (pObject->m_matrix != nullptr)
    {
        MirrorObjectsPolicy::SAxes axes;
        const CMatrix& matrix = *pObject->m_matrix;
        axes.right[0] = matrix.right.x;
        axes.right[1] = matrix.right.y;
        axes.right[2] = matrix.right.z;
        axes.forward[0] = matrix.up.x;
        axes.forward[1] = matrix.up.y;
        axes.forward[2] = matrix.up.z;
        axes.up[0] = matrix.at.x;
        axes.up[1] = matrix.at.y;
        axes.up[2] = matrix.at.z;
        rotation = MirrorObjectsPolicy::QuaternionFromAxes(axes);
    }
}

bool IsFinitePose(const SPosition& position, const SRotation& rotation)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
        std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z) && std::isfinite(rotation.w);
}

void WriteMatrix(CObject* pObject, const SPosition& position, const SRotation& rotation)
{
    const MirrorObjectsPolicy::SAxes axes = MirrorObjectsPolicy::AxesFromQuaternion(rotation);
    CMatrix& matrix = *pObject->m_matrix;
    matrix.right = CVector(axes.right[0], axes.right[1], axes.right[2]);
    matrix.up = CVector(axes.forward[0], axes.forward[1], axes.forward[2]);
    matrix.at = CVector(axes.up[0], axes.up[1], axes.up[2]);
    matrix.pos = CVector(position.x, position.y, position.z);
}

// as CObject::Teleport: out of the world, the new matrix, back in (CWorld::Add updates the RW matrix and frame)
void SetPose(CObject* pObject, const SPosition& position, const SRotation& rotation)
{
    if (pObject->m_matrix == nullptr || !IsFinitePose(position, rotation))
    {
        return;
    }
    CWorld::Remove(pObject);
    WriteMatrix(pObject, position, rotation);
    CWorld::Add(pObject);
}

void SetSpeeds(CObject* pObject, const MirrorObjectsWire::SSpeed& move, const MirrorObjectsWire::SSpeed& turn)
{
    pObject->m_vecMoveSpeed = CVector(move.x, move.y, move.z);
    pObject->m_vecTurnSpeed = CVector(turn.x, turn.y, turn.z);
}

void ZeroSpeeds(CObject* pObject)
{
    pObject->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    pObject->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
}

// As make_object_dynamic 0392. The moving list is set whatever the static bit was: an attachment or the engine's
// detach (which never touch the list, see CMirrorObjects' facts) can leave a dynamic object off it.
// AddToMovingList 0x542800 adds only while +0xB4 (m_pMovingList) is empty and bit 0x40000 of +0x1C is clear,
// RemoveFromMovingList 0x542860 only while +0xB4 is set (bytes), so both are no-ops when already done.
void SetStatic(CObject* pObject, bool bStatic)
{
    if (HasBits(pObject, ENTITY_FLAGS, ENTITY_STATIC) != bStatic)
    {
        pObject->SetIsStatic(bStatic);
    }
    if (bStatic)
    {
        pObject->RemoveFromMovingList();
    }
    else
    {
        pObject->AddToMovingList();
    }
}

// ---- state ----

bool IsHeldLink(eLink link)
{
    return link == eLink::HOLD_TASK || link == eLink::HAND;
}

// the host's flags, area and scale; collision stays off while a ped holds the copy (bHeld: as the hold task leaves it)
void ApplyFields(CObject* pObject, const SObjectRecord& record, bool bHeld)
{
    const uint16_t flags = record.state.flags;
    SetBits(pObject, ENTITY_FLAGS, ENTITY_VISIBLE, (flags & MirrorObjectsWire::FLAG_VISIBLE) != 0);
    if (!bHeld)
    {
        SetBits(pObject, ENTITY_FLAGS, ENTITY_USES_COLLISION, (flags & MirrorObjectsWire::FLAG_COLLISION) != 0);
    }
    SetBits(pObject, ENTITY_FLAGS, ENTITY_RENDER_DAMAGED, (flags & MirrorObjectsWire::FLAG_RENDER_DAMAGED) != 0);
    SetBits(pObject, PHYSICAL_FLAGS, PHYSICAL_DISABLE_COLLISION_FORCE,
        (flags & MirrorObjectsWire::FLAG_DISABLE_COLLISION_FORCE) != 0);
    SetBits(pObject, PHYSICAL_FLAGS, PHYSICAL_DONT_APPLY_SPEED, (flags & MirrorObjectsWire::FLAG_DONT_APPLY_SPEED) != 0);
    SetBits(pObject, OBJECT_FLAGS, OBJECT_LIFTABLE, (flags & MirrorObjectsWire::FLAG_LIFTABLE) != 0);
    if ((flags & MirrorObjectsWire::FLAG_BROKEN) != 0)
    {
        SetBits(pObject, OBJECT_FLAGS, OBJECT_BROKEN, true);
    }
    pObject->m_nAreaCode = record.state.area;
    pObject->m_fScale = record.state.bScaled ? record.state.scale : 1.0f;
}

// break_object 0723's effect on the copy while it is still visible (the FULL's flags hide it right after)
void ReplayBreak(SCopy& copy, CObject* pObject)
{
    const bool bGlass = plugin::CallAndReturn<bool, 0x46A760, CEntity*>(pObject);
    if (bGlass)
    {
        const CVector position = pObject->GetPosition();
        plugin::Call<0x71BC40, CEntity*, float, CVector, CVector, bool>(pObject, GLASS_DAMAGE, CVector(0.0f, 0.0f, 0.0f),
            position, false);
        SetBits(pObject, OBJECT_FLAGS, OBJECT_BROKEN, true);
    }
    else
    {
        if (CObjectInfo* pInfo = pObject->m_pObjectInfo)
        {
            CVector velocity = pInfo->m_vecBreakVelocity;
            plugin::CallMethodAndReturn<bool, 0x59E9B0, void*, CObject*, CVector*, float, bool>(
                reinterpret_cast<void*>(BREAK_MANAGER), pObject, &velocity, pInfo->m_fBreakVelocityRand, false);
        }
        SetBits(pObject, ENTITY_FLAGS, ENTITY_VISIBLE | ENTITY_USES_COLLISION, false);
        pObject->RemoveFromMovingList();
        SetBits(pObject, PHYSICAL_FLAGS, PHYSICAL_BREAK_FLAG, true);
        SetBits(pObject, OBJECT_FLAGS, OBJECT_BROKEN, true);
        SetBits(pObject, ENTITY_FLAGS, ENTITY_STATIC, true);
        ZeroSpeeds(pObject);
        pObject->DeleteRwObject();
    }
    copy.bBroken = true;
    ++ms_counters.breakReplayed;
    CMirrorObjects::Log("event=break-replayed id=%05X model=%d glass=%d", copy.id, copy.model, bGlass);
}

// ---- parents and holders ----

// this game's entity for the record's parent, or nullptr (never the copy itself or something attached to it)
CEntity* ResolveParent(const SParent& parent, const CObject* pCopy)
{
    CEntity* pEntity = nullptr;
    switch (parent.kind)
    {
        case MirrorObjectsWire::PARENT_VEHICLE:
            // a train carriage is a CNetworkVehicle bound to its carriage copy (CNetworkVehicleManager::BindTrainCopy)
            if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(parent.id))
            {
                pEntity = pNetworkVehicle->GetLiveVehicle();
            }
            break;
        case MirrorObjectsWire::PARENT_PED:
            if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(parent.id))
            {
                pEntity = pNetworkPed->GetLivePed();
            }
            break;
        case MirrorObjectsWire::PARENT_PLAYER:
            if (parent.id == CNetworkPlayerManager::m_nMyId)
            {
                pEntity = FindPlayerPed(0);
            }
            else if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(static_cast<int>(parent.id)))
            {
                pEntity = pNetworkPlayer->GetLivePed();
            }
            break;
        case MirrorObjectsWire::PARENT_OBJECT:
        {
            CObject* pParentCopy = CMirrorObjectCopies::FindCopy(parent.id);
            if (pParentCopy != nullptr && pParentCopy != pCopy && pParentCopy->m_pAttachedTo != pCopy)
            {
                pEntity = pParentCopy;
            }
            break;
        }
        default:
            break;
    }
    return pEntity;
}

// this game's ped for the record's holder: live, with its clump and intelligence
CPed* ResolveHolder(const SHolder& holder, bool& bLocal)
{
    bLocal = false;
    switch (holder.kind)
    {
        case MirrorObjectsWire::HOLDER_PED:
            if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(holder.id))
            {
                if (pNetworkPed->IsModelReady())
                {
                    return pNetworkPed->GetLivePed();
                }
            }
            return nullptr;
        case MirrorObjectsWire::HOLDER_PLAYER:
            if (holder.id == CNetworkPlayerManager::m_nMyId)
            {
                CPlayerPed* pPed = FindPlayerPed(0);
                bLocal = true;
                return pPed != nullptr && pPed->m_pRwClump != nullptr && pPed->m_pIntelligence != nullptr ? pPed : nullptr;
            }
            if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(static_cast<int>(holder.id)))
            {
                return pNetworkPlayer->GetReadyPed();
            }
            return nullptr;
        default:
            return nullptr;
    }
}

// The copy comes off its parent: the parent's reference to our m_pAttachedTo goes first (the engine's detach leaves it).
// pResolvedParent: the parent resolved now, when the engine already detached the copy (its stale reference goes).
void DetachCopy(SCopy& copy, CObject* pObject, CEntity* pResolvedParent)
{
    if (CPhysical* pParent = pObject->m_pAttachedTo)
    {
        pParent->CleanUpOldReference(reinterpret_cast<CEntity**>(&pObject->m_pAttachedTo));
        pObject->DettachAutoAttachedEntity();
    }
    else if (pResolvedParent != nullptr && pResolvedParent == copy.pParent)
    {
        pResolvedParent->CleanUpOldReference(reinterpret_cast<CEntity**>(&pObject->m_pAttachedTo));
    }
    pObject->m_nPhysicalFlags.bAttachedToEntity = 0;
    copy.pParent = nullptr;
    copy.attachedTo = SParent{};
    if (!HasBits(pObject, ENTITY_FLAGS, ENTITY_STATIC))
    {
        pObject->AddToMovingList();  // (attached it was positioned by CWorld::ProcessAttachedEntities)
    }
}

void AttachCopy(SCopy& copy, CObject* pObject, CEntity* pParent, const SObjectRecord& record, bool bAgain)
{
    const SParent& parent = record.state.parent;
    if (pObject->m_pAttachedTo != nullptr)
    {
        DetachCopy(copy, pObject, nullptr);
    }
    else if (pParent == copy.pParent)
    {
        // the engine dropped it (a forklift's fork came down, design 3.3): its reference is still on the parent
        pParent->CleanUpOldReference(reinterpret_cast<CEntity**>(&pObject->m_pAttachedTo));
    }
    CVector offset(parent.offsetX, parent.offsetY, parent.offsetZ);
    if (parent.bQuaternion)
    {
        RtQuat rotation;
        rotation.imag.x = parent.rotation.x;
        rotation.imag.y = parent.rotation.y;
        rotation.imag.z = parent.rotation.z;
        rotation.real = parent.rotation.w;
        pObject->AttachEntityToEntity(pParent, &offset, &rotation);
    }
    else
    {
        // An object with bInfiniteMass (a hanging one, CObjectData::SetObjectData by model, so the copy has it too)
        // ignores the angles: the Euler attach stores its position there and PositionAttachedEntity keeps the object's
        // own matrix (gta-reversed Physical.cpp AttachEntityToEntity and PositionAttachedEntity). The host sends no
        // angles for one; its world rotation is the FULL's, given to the copy first.
        if (HasBits(pObject, PHYSICAL_FLAGS, PHYSICAL_INFINITE_MASS) && pObject->m_matrix != nullptr)
        {
            const CVector at = pObject->GetPosition();
            SPosition position;
            position.x = at.x;
            position.y = at.y;
            position.z = at.z;
            if (IsFinitePose(position, record.state.rotation))
            {
                WriteMatrix(pObject, position, record.state.rotation);
            }
        }
        pObject->AttachEntityToEntity(pParent, offset, CVector(parent.eulerX, parent.eulerY, parent.eulerZ));
    }
    copy.pParent = pParent;
    copy.attachedTo = parent;
    if (bAgain)
    {
        ++ms_counters.reattached;
    }
    else
    {
        ++ms_counters.attached;
    }
    if (ShouldLog(bAgain ? ms_counters.reattached : ms_counters.attached))
    {
        CMirrorObjects::Log("event=%s id=%05X parent=%u/%d quaternion=%d", bAgain ? "reattached" : "attached", copy.id,
            parent.kind, parent.id, parent.bQuaternion);
    }
}

// the type-0x133 task in the ped's slot 4 when it holds the copy (ours, or the engine's clone of ours), else nullptr
CTask* FindHoldOfCopy(CPed* pPed, const CObject* pObject)
{
    if (pPed == nullptr || pObject == nullptr || pPed->m_pIntelligence == nullptr)
    {
        return nullptr;
    }
    CTask* pSlot = pPed->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[HOLD_SLOT];
    if (pSlot == nullptr || pSlot->GetTaskType() != HOLD_TASK_TYPE || ReadField<CEntity*>(pSlot, HOLD_ENTITY) != pObject)
    {
        return nullptr;
    }
    return pSlot;
}

// Our hold task is in the ped's slot and still holds the copy. A FlushImmediately on the holder replaced it with a clone
// (see the comment at the top): the clone holds the copy the same way and is adopted as ours.
bool IsOurHoldRunning(SCopy& copy, CObject* pObject, CPed* pPed)
{
    if (copy.pHoldTask == nullptr || pPed == nullptr || pPed != copy.pHolder)
    {
        return false;
    }
    CTask* pSlot = FindHoldOfCopy(pPed, pObject);
    if (pSlot == nullptr)
    {
        return false;
    }
    if (pSlot != copy.pHoldTask)
    {
        copy.pHoldTask = pSlot;
        ++ms_counters.holdAdopted;
        if (ShouldLog(ms_counters.holdAdopted))
        {
            CMirrorObjects::Log("event=hold-adopted id=%05X (the task was replaced by its clone)", copy.id);
        }
    }
    return true;
}

// Ends our hold task without the engine deleting the copy (see the comment at the top): the task we gave, or any
// type-0x133 task in the holder's slot that holds the copy (a FlushImmediately clone of ours).
void EndHold(SCopy& copy, CObject* pObject, const char* why)
{
    CPed* pPed = GetLiveHolder(copy);
    bool bEnded = false;
    if (pPed != nullptr && pPed->m_pIntelligence != nullptr)
    {
        CTaskManager& taskManager = pPed->m_pIntelligence->m_TaskMgr;
        CTask* pSlot = taskManager.m_aSecondaryTasks[HOLD_SLOT];
        if (pSlot != nullptr && pSlot->GetTaskType() == HOLD_TASK_TYPE)
        {
            CEntity* pHeld = ReadField<CEntity*>(pSlot, HOLD_ENTITY);
            const bool bHoldsCopy = pObject != nullptr && pHeld == pObject;
            const bool bOurs = (copy.pHoldTask != nullptr && pSlot == copy.pHoldTask) || bHoldsCopy;
            if (bOurs && bHoldsCopy)
            {
                plugin::CallMethod<0x6916E0, CTask*>(pSlot);  // ReleaseEntity
                SetBits(pObject, ENTITY_FLAGS, ENTITY_STREAMING_DONT_DELETE, false);
            }
            if (bOurs && (bHoldsCopy || pHeld == nullptr))
            {
                pSlot->MakeAbortable(pPed, ABORT_PRIORITY_URGENT, nullptr);
                taskManager.SetTaskSecondary(nullptr, HOLD_SLOT);
                bEnded = true;
            }
        }
    }
    if (copy.pHoldTask != nullptr || bEnded)
    {
        CMirrorObjects::Log("event=hold-ended id=%05X why=%s ped=%d", copy.id, why, pPed != nullptr);
    }
    copy.pHoldTask = nullptr;
    copy.pHolder = nullptr;
    copy.holderRef = -1;
    copy.heldAs = SHolder{};
    if (pObject != nullptr)
    {
        pObject->m_nPhysicalFlags.bAttachedToEntity = 0;
    }
}

// The group's anim block as StartAnim finds it (see the comment at the top): its index, or -1. The block count is
// CAnimManager::ms_numAnimBlocks at 0xB4EA30 (CreateAnimAssocGroups loops to it over 0xB5D4A0 in 0x20 steps,
// 0x4D3CE0-0x4D3D10).
int FindAnimBlockIndex(const uint8_t* pGroup, int32_t group, bool& bLoaded)
{
    bLoaded = false;
    const uint8_t* pBlock = ReadField<const uint8_t*>(pGroup, ANIM_GROUP_BLOCK);
    if (pBlock == nullptr)
    {
        const char* name = plugin::CallAndReturn<const char*, 0x4D3A30, int32_t>(group);  // GetAnimBlockName
        if (name == nullptr)
        {
            return -1;
        }
        pBlock = plugin::CallAndReturn<const uint8_t*, 0x4D3940, const char*>(name);  // GetAnimationBlock
    }
    const int32_t blockCount = *reinterpret_cast<const int32_t*>(ANIM_BLOCK_COUNT);
    const uintptr_t at = reinterpret_cast<uintptr_t>(pBlock);
    if (pBlock == nullptr || at < ANIM_BLOCKS || ((at - ANIM_BLOCKS) & ((1u << ANIM_BLOCK_SIZE_SHIFT) - 1)) != 0)
    {
        return -1;
    }
    const int index = static_cast<int>((at - ANIM_BLOCKS) >> ANIM_BLOCK_SIZE_SHIFT);
    if (index >= blockCount)
    {
        return -1;
    }
    bLoaded = ReadField<uint8_t>(pBlock, ANIM_BLOCK_LOADED) != 0;
    return index;
}

// The anim the task would get (DecideHoldAnim) is none, or an id of its group as this game has the group now (see the
// comment at the top). A group whose block isn't loaded is requested as StartAnim does (throttled) and the copy waits
// in the hand (LOADING); a bad group or id keeps it in the hand (INVALID).
CopiesPolicy::eHoldAnim CheckHoldAnim(SCopy& copy, const SHolder& holder, uint32_t now)
{
    using CopiesPolicy::eHoldAnim;
    const CopiesPolicy::SHoldAnim anim = CopiesPolicy::DecideHoldAnim(holder);
    if (anim.animId == CopiesPolicy::ANIM_ID_NONE)
    {
        return eHoldAnim::VALID;
    }
    const uint8_t* pGroups = *reinterpret_cast<const uint8_t* const*>(ANIM_ASSOC_GROUPS);
    const uint32_t groupCount = *reinterpret_cast<const uint32_t*>(ANIM_ASSOC_GROUP_COUNT);
    if (pGroups == nullptr || anim.animGroup < 0 || static_cast<uint32_t>(anim.animGroup) >= groupCount)
    {
        return eHoldAnim::INVALID;
    }
    const uint8_t* pGroup = pGroups + static_cast<size_t>(anim.animGroup) * ANIM_GROUP_SIZE;
    if (ReadField<const void*>(pGroup, ANIM_GROUP_ASSOCIATIONS) == nullptr)
    {
        bool bLoaded = false;
        const int block = anim.animGroup != 0 ? FindAnimBlockIndex(pGroup, anim.animGroup, bLoaded) : -1;
        if (block < 0 || bLoaded)
        {
            return eHoldAnim::INVALID;  // no block, or loaded without this group's associations
        }
        if (!CTimer::m_UserPause && !CTimer::m_CodePause &&
            CopiesPolicy::ShouldRequestAnimBlock(copy.bAnimRequested, copy.animRequestedAt, now))
        {
            CStreaming::RequestModel(IFP_MODEL_BASE + block, STREAMING_KEEP_IN_MEMORY);
            if (!copy.bAnimRequested)
            {
                CMirrorObjects::Log("event=hold-anim-requested id=%05X group=%d block=%d", copy.id, anim.animGroup, block);
            }
            copy.bAnimRequested = true;
            copy.animRequestedAt = now;
        }
        return eHoldAnim::LOADING;
    }
    copy.bAnimRequested = false;
    return CopiesPolicy::IsAnimInGroup(anim.animId, ReadField<int32_t>(pGroup, ANIM_GROUP_FIRST_ID),
               ReadField<uint32_t>(pGroup, ANIM_GROUP_NUM_ANIMS)) ?
        eHoldAnim::VALID :
        eHoldAnim::INVALID;
}

bool GiveHold(SCopy& copy, CObject* pObject, CPed* pPed, const SHolder& holder)
{
    if (pPed->m_pIntelligence == nullptr || pPed->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[HOLD_SLOT] != nullptr)
    {
        return false;
    }
    void* pMemory = CTask::operator new(HOLD_TASK_SIZE);
    if (pMemory == nullptr)
    {
        return false;
    }
    const CopiesPolicy::SHoldAnim anim = CopiesPolicy::DecideHoldAnim(holder);
    CVector offset(holder.offsetX, holder.offsetY, holder.offsetZ);
    plugin::CallMethod<0x6913A0, void*, CEntity*, CVector*, uint8_t, uint8_t, int, int, bool>(pMemory, pObject, &offset,
        holder.bone, holder.boneFlags, anim.animId, anim.animGroup, holder.bDisallowDrop);
    CTask* pTask = static_cast<CTask*>(pMemory);
    pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(pTask, HOLD_SLOT);
    copy.pHoldTask = pTask;
    copy.pHolder = pPed;
    copy.holderRef = CPools::GetPedRef(pPed);
    copy.heldAs = holder;
    ++ms_counters.holdGiven;
    if (ShouldLog(ms_counters.holdGiven))
    {
        CMirrorObjects::Log("event=hold-given id=%05X holder=%u/%d bone=%u flags=%02X anim=%d/%d byName=%d", copy.id,
            holder.kind, holder.id, holder.bone, holder.boneFlags, anim.animId, anim.animGroup, holder.bAnimByName);
    }
    return true;
}

// the hold task's fallback: at the holder's right hand with the host's offset, facing as the ped
void PlaceAtHand(CObject* pObject, CPed* pPed, const SHolder& holder)
{
    if (pPed->m_matrix == nullptr || pObject->m_matrix == nullptr)
    {
        return;
    }
    RwV3d position = {holder.offsetX, holder.offsetY, holder.offsetZ};
    pPed->GetTransformedBonePosition(position, BONE_RIGHTHAND, false);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
    {
        return;
    }
    CWorld::Remove(pObject);
    CMatrix& matrix = *pObject->m_matrix;
    matrix.right = pPed->m_matrix->right;
    matrix.up = pPed->m_matrix->up;
    matrix.at = pPed->m_matrix->at;
    matrix.pos = CVector(position.x, position.y, position.z);
    CWorld::Add(pObject);
}

// leaving the hand: the record's collision and static flag again
void LeaveHand(SCopy& copy, CObject* pObject, const SObjectRecord& record)
{
    if (!copy.bHandFlags)
    {
        return;
    }
    copy.bHandFlags = false;
    SetBits(pObject, ENTITY_FLAGS, ENTITY_USES_COLLISION, (record.state.flags & MirrorObjectsWire::FLAG_COLLISION) != 0);
    SetStatic(pObject, (record.state.flags & MirrorObjectsWire::FLAG_STATIC) != 0);
}

// ---- creation and deletion ----

const char* CreateCopy(SCopy& copy, const SObjectRecord& record)
{
    const int model = record.state.model;
    if (model < 0 || model >= MirrorObjectsWire::MODEL_COUNT)
    {
        return "model-range";
    }
    CBaseModelInfo* pModelInfo = CModelInfo::ms_modelInfoPtrs[model];
    if (pModelInfo == nullptr)
    {
        return "no-model-info";
    }
    if (!CopiesPolicy::IsObjectModelType(static_cast<int>(pModelInfo->GetModelType())))
    {
        return "model-type";
    }
    if (CPools::ms_pObjectPool == nullptr ||
        !CopiesPolicy::CanTakePoolSlot(static_cast<int>(CPools::ms_pObjectPool->GetNoOfFreeSpaces())))
    {
        return "pool-full";
    }
    if (CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
    {
        // as CNetworkVehicle::CreateVehicle
        const unsigned char oldFlags = CStreaming::ms_aInfoForModel[model].m_nFlags;
        CStreaming::RequestModel(model, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (!(oldFlags & GAME_REQUIRED))
        {
            CStreaming::SetModelIsDeletable(model);
            CStreaming::SetModelTxdIsDeletable(model);
        }
        if (CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
        {
            return "model-load";
        }
    }

    pModelInfo->m_nAlpha = 255;
    CObject* pObject = plugin::CallAndReturn<CObject*, 0x5A1F60, int, bool>(model, false);
    if (pObject == nullptr)
    {
        return "create";
    }
    pObject->m_nObjectType = OBJECT_MISSION2;
    const SPosition& position = record.state.position;
    pObject->SetPosn(position.x, position.y, position.z);
    pObject->SetOrientation(0.0f, 0.0f, 0.0f);
    if (pObject->m_matrix != nullptr && IsFinitePose(position, record.state.rotation))
    {
        WriteMatrix(pObject, position, record.state.rotation);
    }
    pObject->UpdateRwMatrix();
    pObject->UpdateRwFrame();
    if (pModelInfo->AsLodAtomicModelInfoPtr() != nullptr)
    {
        pObject->SetupBigBuilding();
    }
    copy.link = eLink::WORLD;
    ApplyFields(pObject, record, false);
    // before CWorld::Add, which puts a non-static physical entity on the moving list
    SetBits(pObject, ENTITY_FLAGS, ENTITY_STATIC, (record.state.flags & MirrorObjectsWire::FLAG_STATIC) != 0);
    CWorld::Add(pObject);
    SetSpeeds(pObject, record.state.moveSpeed, record.state.turnSpeed);

    copy.pObject = pObject;
    copy.objectRef = CPools::GetObjectRef(pObject);
    copy.model = model;
    copy.bBroken = (record.state.flags & MirrorObjectsWire::FLAG_BROKEN) != 0;
    copy.bFullPending = false;
    copy.bSampleApplied = false;
    copy.rest.Reset();
    copy.pParent = nullptr;
    copy.attachedTo = SParent{};
    copy.pHoldTask = nullptr;
    copy.pHolder = nullptr;
    copy.holderRef = -1;
    copy.bHandFlags = false;
    copy.bLocalHold = false;
    return nullptr;
}

// a copy where there is none (the model and pool permitting), at once and then throttled
void EnsureCopy(SCopy& copy, const SObjectRecord& record, uint32_t now)
{
    if (GetLiveCopy(copy) != nullptr || copy.idKind != MirrorObjectsWire::ID_MISSION)
    {
        return;
    }
    // this game's own world first (a new game, a load, a death): CSessionLifecycle's rule; and nothing is created or
    // streamed in while this game is paused (the menu: CTimer::m_UserPause; CTimer::m_CodePause, as CRadioSync checks)
    if (!CSessionLifecycle::IsLocalGameReady() || CTimer::m_UserPause || CTimer::m_CodePause ||
        !copy.createThrottle.ShouldAttempt(now))
    {
        return;
    }
    const bool bRetry = copy.createAttempts > 0;
    copy.createAttempts++;
    const char* failure = CreateCopy(copy, record);
    if (failure != nullptr)
    {
        ++ms_counters.createFailed;
        if (ShouldLog(ms_counters.createFailed))
        {
            CMirrorObjects::Log("event=copy-create-failed id=%05X model=%d reason=%s attempt=%u pool-free=%d", copy.id,
                record.state.model, failure, copy.createAttempts,
                CPools::ms_pObjectPool != nullptr ? static_cast<int>(CPools::ms_pObjectPool->GetNoOfFreeSpaces()) : -1);
        }
        return;
    }
    copy.createAttempts = 0;
    ++ms_counters.created;
    if (bRetry)
    {
        ++ms_counters.retries;
    }
    if (ShouldLog(ms_counters.created))
    {
        CMirrorObjects::Log("event=%s id=%05X model=%d ref=%08X flags=%04X area=%u at=%.2f,%.2f,%.2f", bRetry ?
            "copy-retry-created" : "copy-created", copy.id, copy.model, static_cast<uint32_t>(copy.objectRef),
            record.state.flags, record.state.area, record.state.position.x, record.state.position.y,
            record.state.position.z);
    }
}

// the copy goes as delete_object 0108 deletes an object; nothing keeps a pointer to it
void DeleteCopy(SCopy& copy, const char* why)
{
    CObject* pObject = GetLiveCopy(copy);
    if (pObject == nullptr)
    {
        EndHold(copy, nullptr, why);
        return;
    }
    EndHold(copy, pObject, why);
    // copies attached to this one fall back to their own transform
    for (SCopy& child : ms_copies)
    {
        if (!child.bUsed || &child == &copy || child.pObject == nullptr)
        {
            continue;
        }
        CObject* pChild = GetLiveCopy(child);
        if (pChild == nullptr)
        {
            continue;
        }
        if (pChild->m_pAttachedTo == pObject)
        {
            DetachCopy(child, pChild, nullptr);
            child.link = eLink::WORLD;
            child.bFullPending = true;
        }
        else if (child.pParent == pObject)
        {
            child.pParent = nullptr;  // its reference on this one goes with it
        }
    }
    DetachCopy(copy, pObject, nullptr);
    CWorld::Remove(pObject);
    CWorld::RemoveReferencesToDeletedObject(pObject);
    delete pObject;
    copy.pObject = nullptr;
    copy.objectRef = -1;
    ++ms_counters.removed;
    if (ShouldLog(ms_counters.removed))
    {
        CMirrorObjects::Log("event=copy-removed id=%05X why=%s", copy.id, why);
    }
}

// ---- following the host ----

void ProcessWorld(SCopy& copy, CObject* pObject, const SObjectRecord& record, uint32_t now)
{
    SPosition position;
    SRotation rotation;
    ReadPose(pObject, position, rotation);

    if (CopiesPolicy::IsMotionActive(record))
    {
        copy.bFullPending = false;
        copy.rest.Reset();
        const MirrorObjectsPolicy::SMotionSample& newest = record.samples[0];
        if (CopiesPolicy::IsKinematic(record))
        {
            const CopiesPolicy::SPose pose = CopiesPolicy::KinematicPose(record, g_serverTime);
            if (pose.source == CopiesPolicy::ePoseSource::NONE)
            {
                return;
            }
            const float step = MirrorObjectsPolicy::Distance(position, pose.position);
            if (step < 0.001f && MirrorObjectsPolicy::AngleBetweenDegrees(rotation, pose.rotation) < 0.1f)
            {
                ZeroSpeeds(pObject);
                return;  // held there already: no new placement
            }
            if (CopiesPolicy::IsSnap(step))
            {
                ++ms_counters.snapped;
                if (ShouldLog(ms_counters.snapped))
                {
                    CMirrorObjects::Log("event=snapped id=%05X kind=kinematic distance=%.2f", copy.id, step);
                }
            }
            SetPose(pObject, pose.position, pose.rotation);
            ZeroSpeeds(pObject);
            return;
        }
        if (copy.bSampleApplied && copy.lastSampleTime == newest.serverTime)
        {
            return;  // the copy's physics until the next sample
        }
        copy.bSampleApplied = true;
        copy.lastSampleTime = newest.serverTime;
        const float distance = MirrorObjectsPolicy::Distance(position, newest.position);
        const CopiesPolicy::eCorrection correction = CopiesPolicy::DecideCorrection(distance);
        if (correction != CopiesPolicy::eCorrection::SPEEDS_ONLY)
        {
            SetPose(pObject, newest.position, newest.rotation);
        }
        if (correction == CopiesPolicy::eCorrection::SNAP)
        {
            ++ms_counters.snapped;
            if (ShouldLog(ms_counters.snapped))
            {
                CMirrorObjects::Log("event=snapped id=%05X kind=dynamic distance=%.2f", copy.id, distance);
            }
        }
        SetStatic(pObject, false);
        SetSpeeds(pObject, newest.moveSpeed, newest.turnSpeed);
        return;
    }

    if (copy.bFullPending)
    {
        copy.bFullPending = false;
        copy.rest.Reset();
        SetPose(pObject, record.state.position, record.state.rotation);
        SetStatic(pObject, (record.state.flags & MirrorObjectsWire::FLAG_STATIC) != 0);
        SetSpeeds(pObject, record.state.moveSpeed, record.state.turnSpeed);
        return;
    }
    if (copy.bBroken)
    {
        return;  // static and hidden where it broke
    }
    const float distance = MirrorObjectsPolicy::Distance(position, record.state.position);
    const float angle = MirrorObjectsPolicy::AngleBetweenDegrees(rotation, record.state.rotation);
    if (copy.rest.Update(CopiesPolicy::IsOffRest(distance, angle), now))
    {
        SetPose(pObject, record.state.position, record.state.rotation);
        SetStatic(pObject, (record.state.flags & MirrorObjectsWire::FLAG_STATIC) != 0);
        SetSpeeds(pObject, record.state.moveSpeed, record.state.turnSpeed);
        ++ms_counters.putBack;
        if (ShouldLog(ms_counters.putBack))
        {
            CMirrorObjects::Log("event=put-back id=%05X distance=%.2f angle=%.1f", copy.id, distance, angle);
        }
    }
}

void EnterLink(SCopy& copy, eLink link, const char* reason)
{
    if (copy.link == link)
    {
        return;
    }
    if (link == eLink::HAND)
    {
        ++ms_counters.holdFallback;
        if (ShouldLog(ms_counters.holdFallback))
        {
            CMirrorObjects::Log("event=hold-fallback id=%05X reason=%s", copy.id, reason);
        }
    }
    else if (link == eLink::WORLD && reason != nullptr)
    {
        ++ms_counters.attachFallback;
        if (ShouldLog(ms_counters.attachFallback))
        {
            CMirrorObjects::Log("event=attach-fallback id=%05X reason=%s", copy.id, reason);
        }
    }
    copy.link = link;
}

void ProcessCopy(SCopy& copy, CObject* pObject, const SObjectRecord& record, uint32_t now)
{
    // a hold task destroyed with its ped flags the entity it held for deletion (see the comment at the top)
    pObject->m_bRemoveFromWorld = 0;

    // the hold this game's player took with the engine's own pick-up: the copy is left to it
    bool bLocalHold = false;
    if (CPlayerPed* pPlayer = FindPlayerPed(0))
    {
        if (pPlayer->m_pIntelligence != nullptr)
        {
            if (const CTaskSimpleHoldEntity* pTask = pPlayer->m_pIntelligence->GetTaskHold(false))
            {
                bLocalHold = ReadField<CEntity*>(pTask, HOLD_ENTITY) == pObject;
            }
        }
    }
    if (bLocalHold != copy.bLocalHold)
    {
        copy.bLocalHold = bLocalHold;
        if (bLocalHold)
        {
            ++ms_counters.localHold;
            // our task on a remote ped and our attachment end first, leaving what the player's task set on the copy
            // (it sets bAttachedToEntity and bit 0x400 once, when it starts)
            const bool bAttachedFlag = pObject->m_nPhysicalFlags.bAttachedToEntity != 0;
            const bool bDontDelete = HasBits(pObject, ENTITY_FLAGS, ENTITY_STREAMING_DONT_DELETE);
            if (copy.pHoldTask != nullptr || copy.pHolder != nullptr)
            {
                EndHold(copy, pObject, "local-hold");
            }
            if (pObject->m_pAttachedTo != nullptr || copy.pParent != nullptr)
            {
                DetachCopy(copy, pObject, nullptr);
            }
            pObject->m_nPhysicalFlags.bAttachedToEntity = bAttachedFlag ? 1 : 0;
            SetBits(pObject, ENTITY_FLAGS, ENTITY_STREAMING_DONT_DELETE, bDontDelete);
            copy.bHandFlags = false;
            copy.link = eLink::WORLD;
        }
        CMirrorObjects::Log("event=local-hold id=%05X held=%d", copy.id, bLocalHold);
        copy.bFullPending = !bLocalHold;
    }
    if (bLocalHold)
    {
        return;
    }

    const SParent& parent = record.state.parent;
    const SHolder& holder = record.state.holder;
    CEntity* pParent = parent.kind != MirrorObjectsWire::PARENT_NONE ? ResolveParent(parent, pObject) : nullptr;
    bool bHolderLocal = false;
    CPed* pHolderPed = holder.kind != MirrorObjectsWire::HOLDER_NONE ? ResolveHolder(holder, bHolderLocal) : nullptr;

    // a new holder (or new hold fields) gets its own task attempts
    if (!MirrorObjectsPolicy::SameHolder(copy.throttledFor, holder))
    {
        copy.throttledFor = holder;
        copy.holdThrottle.Reset();
    }

    // our task, if one runs: on another ped than the record's, or with other fields, it ends (EndHold also takes the
    // old holder's clone of it)
    CPed* pOldHolder = GetLiveHolder(copy);
    if (copy.pHoldTask != nullptr &&
        (pOldHolder == nullptr || pOldHolder != pHolderPed || !MirrorObjectsPolicy::SameHolder(copy.heldAs, holder)))
    {
        EndHold(copy, pObject, pOldHolder == nullptr ? "holder-gone" : "holder-changed");
        ApplyFields(pObject, record, false);
    }
    bool bOurHold = IsOurHoldRunning(copy, pObject, pHolderPed);
    if (bOurHold && holder.bDisallowDrop && ReadField<uint8_t>(copy.pHoldTask, HOLD_DISALLOW_DROP) == 0 &&
        !copy.holdThrottle.IsExhausted(now))
    {
        // an adopted clone lost the host's disallow-drop (see the comment at the top): our own task again (throttled)
        EndHold(copy, pObject, "clone-allows-drop");
        bOurHold = false;
    }
    if (copy.pHoldTask != nullptr && !bOurHold)
    {
        // the ped's own logic dropped it, or the task was replaced: given again (throttled), else the hand
        ++ms_counters.holdLost;
        CMirrorObjects::Log("event=hold-lost id=%05X", copy.id);
        copy.pHoldTask = nullptr;
        copy.pHolder = nullptr;
        copy.holderRef = -1;
        pObject->m_nPhysicalFlags.bAttachedToEntity = 0;
    }

    CopiesPolicy::SLinkInput input;
    input.parentKind = parent.kind;
    input.bParentResolved = pParent != nullptr;
    input.holderKind = holder.kind;
    input.bHolderResolved = pHolderPed != nullptr;
    input.bHolderIsLocalPlayer = bHolderLocal;
    input.bHoldSlotFree = bOurHold || (pHolderPed != nullptr && pHolderPed->m_pIntelligence != nullptr &&
        pHolderPed->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[HOLD_SLOT] == nullptr);
    input.bHoldRetriesLeft = bOurHold || !copy.holdThrottle.IsExhausted(now);
    input.bHoldBoneValid = CopiesPolicy::IsHoldBoneValid(holder.bone);
    CopiesPolicy::eHoldAnim holdAnim = CopiesPolicy::eHoldAnim::VALID;
    if (!bOurHold && pHolderPed != nullptr && !bHolderLocal)
    {
        holdAnim = CheckHoldAnim(copy, holder, now);
    }
    input.bHoldAnimValid = holdAnim == CopiesPolicy::eHoldAnim::VALID;
    eLink link = CopiesPolicy::DecideLink(input);
    const bool bGiveHold = link == eLink::HOLD_TASK && !bOurHold;
    if (bGiveHold && !copy.holdThrottle.ShouldAttempt(now))
    {
        link = eLink::HAND;  // between two attempts
    }

    // leaving the old link
    if (copy.link != link)
    {
        if (copy.link == eLink::ATTACH)
        {
            DetachCopy(copy, pObject, pParent);
        }
        if (copy.link == eLink::HOLD_TASK)
        {
            EndHold(copy, pObject, "link-changed");
            ApplyFields(pObject, record, IsHeldLink(link));
        }
        if (copy.link == eLink::HAND)
        {
            LeaveHand(copy, pObject, record);
        }
        if (link == eLink::WORLD)
        {
            copy.bFullPending = true;
        }
    }
    if (link == eLink::HOLD_TASK && !bOurHold)
    {
        if (pObject->m_pAttachedTo != nullptr)
        {
            DetachCopy(copy, pObject, nullptr);
        }
        if (!GiveHold(copy, pObject, pHolderPed, holder))
        {
            link = eLink::HAND;
        }
    }

    const char* reason = nullptr;
    if (link == eLink::WORLD)
    {
        if (holder.kind != MirrorObjectsWire::HOLDER_NONE)
        {
            reason = "holder-unresolved";
        }
        else if (parent.kind != MirrorObjectsWire::PARENT_NONE)
        {
            reason = "parent-unresolved";
        }
    }
    else if (link == eLink::HAND)
    {
        reason = bHolderLocal ? "local-player" : (!input.bHoldBoneValid ? "bone" : (!input.bHoldAnimValid ?
            (holdAnim == CopiesPolicy::eHoldAnim::LOADING ? "anim-loading" : "anim") :
            (!input.bHoldSlotFree ? "slot-busy" : "task")));
    }
    EnterLink(copy, link, reason);

    // this game's forklift or dumper took the copy (CObject::ProcessControl's auto-attach) while the host's object
    // isn't attached here: it comes off again
    if (link != eLink::ATTACH && pObject->m_pAttachedTo != nullptr)
    {
        ++ms_counters.autoDetached;
        if (ShouldLog(ms_counters.autoDetached))
        {
            CMirrorObjects::Log("event=auto-detached id=%05X link=%s", copy.id, CopiesPolicy::LinkName(link));
        }
        DetachCopy(copy, pObject, nullptr);
        if (link == eLink::WORLD)
        {
            copy.bFullPending = true;
        }
    }

    switch (link)
    {
        case eLink::ATTACH:
        {
            const bool bSame = pObject->m_pAttachedTo == pParent && copy.pParent == pParent &&
                MirrorObjectsPolicy::SameParent(copy.attachedTo, parent);
            if (!bSame)
            {
                // a new parent or new offsets at once; the engine dropping it again (a forklift copy) throttled
                const bool bDropped = pObject->m_pAttachedTo == nullptr && copy.pParent == pParent &&
                    MirrorObjectsPolicy::SameParent(copy.attachedTo, parent);
                if (!bDropped)
                {
                    copy.attachThrottle.Reset();
                }
                if (copy.attachThrottle.ShouldAttempt(now))
                {
                    AttachCopy(copy, pObject, pParent, record, bDropped);
                }
            }
            break;
        }
        case eLink::HAND:
            if (!copy.bHandFlags)
            {
                copy.bHandFlags = true;
                SetBits(pObject, ENTITY_FLAGS, ENTITY_USES_COLLISION, false);
                SetStatic(pObject, true);
                ZeroSpeeds(pObject);
            }
            PlaceAtHand(pObject, pHolderPed, holder);
            break;
        case eLink::HOLD_TASK:
            break;  // the task places it
        default:
            ProcessWorld(copy, pObject, record, now);
            break;
    }
}
}  // namespace

void CMirrorObjectCopies::OnRecordFull(const MirrorObjectsPolicy::SObjectRecord& record, bool bNew)
{
    if (record.idKind != MirrorObjectsWire::ID_MISSION)
    {
        return;  // persistent records are OB-4
    }
    SCopy* pCopy = FindEntry(record.idKind, record.id);
    if (pCopy == nullptr)
    {
        pCopy = AddEntry(record.idKind, record.id);
        if (pCopy == nullptr)
        {
            return;
        }
    }
    SCopy& copy = *pCopy;
    CObject* pObject = GetLiveCopy(copy);
    if (pObject != nullptr && pObject->m_nModelIndex != record.state.model)
    {
        DeleteCopy(copy, "model-changed");
        pObject = nullptr;
    }
    if (pObject == nullptr)
    {
        if (bNew)
        {
            copy.createThrottle.Reset();
        }
        EnsureCopy(copy, record, GetTickCount());
        return;
    }

    if ((record.state.flags & MirrorObjectsWire::FLAG_BROKEN) != 0 && !copy.bBroken)
    {
        ReplayBreak(copy, pObject);
    }
    // collision stays as the hold left it, ours or this game's player's
    ApplyFields(pObject, record, IsHeldLink(copy.link) || copy.bLocalHold);
    copy.bFullPending = true;
    copy.bSampleApplied = false;
}

void CMirrorObjectCopies::OnRecordRemoved(const MirrorObjectsPolicy::SObjectRecord& record)
{
    SCopy* pCopy = FindEntry(record.idKind, record.id);
    if (pCopy == nullptr)
    {
        return;
    }
    DeleteCopy(*pCopy, "removed");
    pCopy->bUsed = false;
}

void CMirrorObjectCopies::OnOwnerChanged(const MirrorObjectsPolicy::SObjectRecord& record)
{
    if (SCopy* pCopy = FindEntry(record.idKind, record.id))
    {
        pCopy->bSampleApplied = false;
        pCopy->bFullPending = true;
    }
}

void CMirrorObjectCopies::OnMotion(const MirrorObjectsPolicy::SObjectRecord& record)
{
    (void)record;  // Process reads the samples from the record (CMirrorObjects::GetRecords) every frame
}

void CMirrorObjectCopies::OnRecordsCleared(const char* why)
{
    int count = 0;
    for (SCopy& copy : ms_copies)
    {
        if (!copy.bUsed)
        {
            continue;
        }
        DeleteCopy(copy, why);
        copy.bUsed = false;
        count++;
    }
    ms_counters.copies = 0;
    if (count > 0)
    {
        CMirrorObjects::Log("event=copies-cleared why=%s count=%d", why, count);
    }
}

void CMirrorObjectCopies::OnLocalRestart()
{
    // CWorld::ClearForRestart deleted them (DeleteAllMissionObjects 0x5A1910: types 2 and 6) and the peds holding them;
    // one still in its pool slot (a restart path that kept it) goes the usual way
    int forgotten = 0;
    for (SCopy& copy : ms_copies)
    {
        if (!copy.bUsed)
        {
            continue;
        }
        if (copy.pObject != nullptr && copy.objectRef >= 0 && CPools::GetObject(copy.objectRef) == copy.pObject &&
            copy.pObject->IsVTableValid())
        {
            DeleteCopy(copy, "local-restart");
        }
        else if (copy.pObject != nullptr)
        {
            forgotten++;
        }
        const uint8_t idKind = copy.idKind;
        const int32_t id = copy.id;
        copy = SCopy{};
        copy.bUsed = true;
        copy.idKind = idKind;
        copy.id = id;
    }
    ms_counters.copies = 0;
    CMirrorObjects::Log("event=copies-forgotten why=local-restart count=%d (rebuilt once this game is ready)", forgotten);
}

void CMirrorObjectCopies::Process()
{
    const uint32_t now = GetTickCount();
    const MirrorObjectsPolicy::CRecordTable& records = CMirrorObjects::GetRecords();

    // copies whose record went without a REMOVED (none expected: the table reports every removal)
    for (SCopy& copy : ms_copies)
    {
        if (copy.bUsed && records.Find(copy.idKind, copy.id) == nullptr)
        {
            DeleteCopy(copy, "no-record");
            copy.bUsed = false;
        }
    }
    // a copy for every mission record (after this game's restart, or one the pool or model refused)
    for (int i = 0; i < records.Count(); i++)
    {
        const SObjectRecord& record = records.At(i);
        if (record.idKind != MirrorObjectsWire::ID_MISSION)
        {
            continue;
        }
        SCopy* pCopy = FindEntry(record.idKind, record.id);
        if (pCopy == nullptr)
        {
            pCopy = AddEntry(record.idKind, record.id);
        }
        if (pCopy != nullptr)
        {
            EnsureCopy(*pCopy, record, now);
        }
    }

    unsigned int live = 0;
    for (SCopy& copy : ms_copies)
    {
        if (!copy.bUsed)
        {
            continue;
        }
        CObject* pObject = GetLiveCopy(copy);
        const SObjectRecord* pRecord = records.Find(copy.idKind, copy.id);
        if (pObject == nullptr || pRecord == nullptr)
        {
            continue;
        }
        live++;
        if (pRecord->state.owner.playerId != MirrorObjectsWire::OWNER_HOST)
        {
            continue;  // handed over (OB-3): not in OB-1
        }
        ProcessCopy(copy, pObject, *pRecord, now);
    }
    ms_counters.copies = live;
}

void CMirrorObjectCopies::ClearRemoveFlags()
{
    for (SCopy& copy : ms_copies)
    {
        if (!copy.bUsed)
        {
            continue;
        }
        if (CObject* pObject = GetLiveCopy(copy))
        {
            pObject->m_bRemoveFromWorld = 0;
        }
    }
}

CObject* CMirrorObjectCopies::FindCopy(int32_t hostHandle)
{
    SCopy* pCopy = FindEntry(MirrorObjectsWire::ID_MISSION, hostHandle);
    return pCopy != nullptr ? GetLiveCopy(*pCopy) : nullptr;
}

void CMirrorObjectCopies::WriteDiagSummary(FILE* file)
{
    const SCounters& c = ms_counters;
    fprintf(file,
        " | copies now=%u created=%u createFail=%u retries=%u removed=%u lost=%u snapped=%u putBack=%u attached=%u "
        "reattached=%u attachFallback=%u holdGiven=%u holdLost=%u holdFallback=%u holdAdopted=%u breakReplayed=%u "
        "localHold=%u autoDetached=%u\n",
        c.copies, c.created, c.createFailed, c.retries, c.removed, c.lost, c.snapped, c.putBack, c.attached,
        c.reattached, c.attachFallback, c.holdGiven, c.holdLost, c.holdFallback, c.holdAdopted, c.breakReplayed,
        c.localHold, c.autoDetached);
}
