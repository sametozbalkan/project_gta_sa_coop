#pragma once

#include <cmath>
#include <cstdint>

#include "network/mirror_objects.h"

// Private build: the rules of the mission object mirror (CMirrorObjects; docs/design/mission-objects.md sections 5.3,
// 5.4, 5.6 and 5.10, package OB-1), without game or network code, so tests/mirror_objects_test.cpp checks them without
// GTA. The state they work on is the wire format of MISSION_OBJECTS and MISSION_OBJECT_MOTION
// (shared/network/mirror_objects.h).
namespace MirrorObjectsPolicy
{
using MirrorObjectsWire::SObjectEntry;
using MirrorObjectsWire::SObjectFull;
using MirrorObjectsWire::SOwner;
using MirrorObjectsWire::SParent;
using MirrorObjectsWire::SHolder;
using MirrorObjectsWire::SMotionEntry;
using MirrorObjectsWire::SRotation;

// CPools::ms_pObjectPool has 350 slots in 1.0 US (CPools::Initialise, push 15Eh at 0x551096): no more objects can be
// owned at once
constexpr int POOL_SIZE = 350;
constexpr int MAX_OWNED = POOL_SIZE;
// a receiver's records of the host: its mission objects (one pool) plus room for the persistent records of OB-4
constexpr int MAX_RECORDS = 512;

// The commands that create an object and leave its handle in ScriptParams[0] when their handler returns (design 3.2:
// both cases store CPool::GetRef's result with `mov [0A43C78h],eax` before StoreParameters, CMirrorObjects.cpp).
inline bool IsCreatingCommand(uint16_t opcode)
{
    return opcode == 0x0107     // create_object
        || opcode == 0x029B;    // create_object_no_offset
}

// a handle the object pool can give out: (slot << 8) | a 7-bit id, slot below POOL_SIZE
inline bool IsObjectHandle(int32_t handle)
{
    return handle >= 0 && handle <= MirrorObjectsWire::MISSION_ID_MAX && (handle >> 8) < POOL_SIZE &&
        (handle & 0xFF) < 0x80;
}

// ---- rotations (unit quaternions; the axes are a matrix's right, forward and up vectors) ----

struct SAxes
{
    float right[3] = {1.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 1.0f, 0.0f};
    float up[3] = {0.0f, 0.0f, 1.0f};
};

// the rotation whose matrix has these columns (local x, y, z in world space)
inline SRotation QuaternionFromAxes(const SAxes& axes)
{
    // R[row][column]: column 0 = right, 1 = forward, 2 = up
    const float m00 = axes.right[0], m10 = axes.right[1], m20 = axes.right[2];
    const float m01 = axes.forward[0], m11 = axes.forward[1], m21 = axes.forward[2];
    const float m02 = axes.up[0], m12 = axes.up[1], m22 = axes.up[2];
    SRotation q;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(length > 0.000001f) || !std::isfinite(length))
    {
        return SRotation{};
    }
    q.x /= length;
    q.y /= length;
    q.z /= length;
    q.w /= length;
    return q;
}

// the inverse of QuaternionFromAxes, for the receivers' copies
inline SAxes AxesFromQuaternion(const SRotation& q)
{
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    SAxes axes;
    axes.right[0] = 1.0f - 2.0f * (yy + zz);
    axes.right[1] = 2.0f * (xy + wz);
    axes.right[2] = 2.0f * (xz - wy);
    axes.forward[0] = 2.0f * (xy - wz);
    axes.forward[1] = 1.0f - 2.0f * (xx + zz);
    axes.forward[2] = 2.0f * (yz + wx);
    axes.up[0] = 2.0f * (xz + wy);
    axes.up[1] = 2.0f * (yz - wx);
    axes.up[2] = 1.0f - 2.0f * (xx + yy);
    return axes;
}

// the angle between two rotations, degrees (q and -q are the same rotation)
inline float AngleBetweenDegrees(const SRotation& a, const SRotation& b)
{
    float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    if (dot > 1.0f)
    {
        dot = 1.0f;
    }
    return 2.0f * std::acos(dot) * 57.2957795f;
}

inline float Distance(const MirrorObjectsWire::SPosition& a, const MirrorObjectsWire::SPosition& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- the host: what changed ----

// offsets and Euler angles closer than this are the same (the wire's resolution: 1 mm, 0.0001 rad)
constexpr float OFFSET_EPSILON = 0.001f;
constexpr float ANGLE_EPSILON = 0.001f;
// quaternion attachments closer than this angle are the same (the wire's 10-bit components: about 0.16 degrees)
constexpr float ROTATION_EPSILON_DEGREES = 0.25f;

inline bool Near(float a, float b, float epsilon)
{
    return std::fabs(a - b) <= epsilon;
}

inline bool SameParent(const SParent& a, const SParent& b)
{
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == MirrorObjectsWire::PARENT_NONE)
    {
        return true;
    }
    if (a.id != b.id || a.bQuaternion != b.bQuaternion || !Near(a.offsetX, b.offsetX, OFFSET_EPSILON) ||
        !Near(a.offsetY, b.offsetY, OFFSET_EPSILON) || !Near(a.offsetZ, b.offsetZ, OFFSET_EPSILON))
    {
        return false;
    }
    if (a.bQuaternion)
    {
        return AngleBetweenDegrees(a.rotation, b.rotation) <= ROTATION_EPSILON_DEGREES;
    }
    return Near(a.eulerX, b.eulerX, ANGLE_EPSILON) && Near(a.eulerY, b.eulerY, ANGLE_EPSILON) &&
        Near(a.eulerZ, b.eulerZ, ANGLE_EPSILON);
}

inline bool SameHolder(const SHolder& a, const SHolder& b)
{
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == MirrorObjectsWire::HOLDER_NONE)
    {
        return true;
    }
    return a.id == b.id && Near(a.offsetX, b.offsetX, OFFSET_EPSILON) && Near(a.offsetY, b.offsetY, OFFSET_EPSILON) &&
        Near(a.offsetZ, b.offsetZ, OFFSET_EPSILON) && a.bone == b.bone && a.boneFlags == b.boneFlags &&
        a.animId == b.animId && a.animGroup == b.animGroup && a.bAnimByName == b.bAnimByName &&
        a.bDisallowDrop == b.bDisallowDrop;
}

// Everything but the transform, the speeds and the static flag (design 5.4: "a FULL for any field change other than
// motion"). The engine sets and clears the static flag of a dynamic object on its own whenever it comes to rest or is
// pushed; it goes out with every FULL, and the motion rules send the resting FULL (DecideMotion).
inline bool SameFields(const SObjectFull& a, const SObjectFull& b)
{
    constexpr uint16_t MASK = static_cast<uint16_t>(MirrorObjectsWire::FLAGS_KNOWN & ~MirrorObjectsWire::FLAG_STATIC);
    return a.model == b.model && a.area == b.area && (a.flags & MASK) == (b.flags & MASK) && a.bScaled == b.bScaled &&
        (!a.bScaled || a.scale == b.scale) && a.owner.playerId == b.owner.playerId && a.owner.epoch == b.owner.epoch &&
        SameParent(a.parent, b.parent) && SameHolder(a.holder, b.holder);
}

// attached to a parent the others know, or held by a ped they know: the copy follows that locally (the samples at a
// reduced rate only for a receiver that can't resolve it)
inline bool FollowsParent(const SObjectFull& state)
{
    return state.parent.kind != MirrorObjectsWire::PARENT_NONE || state.holder.kind != MirrorObjectsWire::HOLDER_NONE;
}

// an attach or hold offset the wire can carry (16-bit fixed point at 1 mm); a parent beyond it is sent as none
inline bool IsOffsetSendable(float x, float y, float z)
{
    const float limit = MirrorObjectsWire::OFFSET_LIMIT;
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) <= limit &&
        std::fabs(y) <= limit && std::fabs(z) <= limit;
}

// ---- the host: motion (design 5.6) ----

constexpr float MOVING_SPEED = 0.05f;        // m/s
constexpr float FAST_SPEED = 5.0f;           // m/s: 20 Hz above
constexpr float TURNING_RATE = 0.01f;        // rad/s
constexpr float SAMPLE_MOVE = 0.01f;         // m since the last sample
constexpr float SAMPLE_TURN_DEGREES = 0.5f;  // since the last sample
constexpr uint32_t SAMPLE_INTERVAL_MS = 100; // 10 Hz
constexpr uint32_t FAST_SAMPLE_INTERVAL_MS = 50;
constexpr uint32_t REST_MS = 300;            // still this long after moving: a reliable FULL with the resting transform
// An object held or attached to an entity the others know: samples at a reduced rate (20 Hz above 5 m/s), for a
// receiver that can't resolve the parent or holder (no carriage copy, its spawn not there yet, a copy being recreated);
// the others follow the parent locally and take samples only in their WORLD link. Never a resting FULL.
constexpr uint32_t FOLLOWER_SAMPLE_INTERVAL_MS = 200;
// Stillness is measured over windows of this length (not per frame, so a slow creeper at a high frame rate doesn't
// count as still): moving more than this within one window counts as moving (a resting dynamic object jitters below it)
constexpr uint32_t STILL_WINDOW_MS = 100;
constexpr float WINDOW_MOVE = 0.001f;
constexpr float WINDOW_TURN_DEGREES = 0.1f;
// the engine's speeds are per 1/50 s (set_object_velocity 0381 multiplies by 0.02, design 3.3)
constexpr float SPEED_UNITS_PER_SECOND = 50.0f;

inline float Length(float x, float y, float z)
{
    return std::sqrt(x * x + y * y + z * z);
}

// The motion watch of one object, updated every frame: lastMovingAt is the last moment it moved (still = none for
// REST_MS). The speeds count at once; the transform counts once per STILL_WINDOW_MS, against the window's start.
struct SMovementWatch
{
    MirrorObjectsWire::SPosition windowPosition{};
    SRotation windowRotation{};
    uint32_t windowStart = 0;
    bool bKnown = false;
    uint32_t lastMovingAt = 0;

    // true when the object counts as moving now
    bool Update(const MirrorObjectsWire::SPosition& position, const SRotation& rotation, float speed, float turnRate,
        uint32_t now)
    {
        bool bMoving = speed > MOVING_SPEED || turnRate > TURNING_RATE;
        if (!bKnown)
        {
            bKnown = true;
            windowPosition = position;
            windowRotation = rotation;
            windowStart = now;
        }
        else if (static_cast<uint32_t>(now - windowStart) >= STILL_WINDOW_MS)
        {
            if (Distance(windowPosition, position) > WINDOW_MOVE ||
                AngleBetweenDegrees(windowRotation, rotation) > WINDOW_TURN_DEGREES)
            {
                bMoving = true;
            }
            windowPosition = position;
            windowRotation = rotation;
            windowStart = now;
        }
        if (bMoving)
        {
            lastMovingAt = now;
        }
        return bMoving;
    }
};

struct SMotionInput
{
    bool bFollowsParent = false;   // held or attached to an entity the others know: reduced rate, no resting FULL
    bool bBecameStatic = false;    // dynamic at the last sample, static now
    bool bMotionActive = false;    // samples went out since the last FULL
    float speed = 0.0f;            // m/s
    float turnRate = 0.0f;         // rad/s
    float movedSinceSample = 0.0f; // m since the last sample or FULL
    float turnedSinceSample = 0.0f;// degrees since the last sample or FULL
    uint32_t msSinceSample = 0;
    uint32_t msStill = 0;          // since the object last moved
};

enum class eMotion : uint8_t
{
    NONE,
    SAMPLE,     // a motion sample (unreliable)
    REST_FULL,  // a FULL with the resting transform (reliable); the samples stop
};

inline eMotion DecideMotion(const SMotionInput& in)
{
    if (in.bFollowsParent)
    {
        // only a real change since the last sample; the speed of an attached object is not always its own (a parent's
        // motion moves it), so the distance over the time since the sample counts too
        if (in.movedSinceSample < SAMPLE_MOVE && in.turnedSinceSample < SAMPLE_TURN_DEGREES)
        {
            return eMotion::NONE;
        }
        const float derived = in.msSinceSample > 0 ?
            in.movedSinceSample * 1000.0f / static_cast<float>(in.msSinceSample) : 0.0f;
        const bool bFast = in.speed > FAST_SPEED || derived > FAST_SPEED;
        const uint32_t interval = bFast ? FAST_SAMPLE_INTERVAL_MS : FOLLOWER_SAMPLE_INTERVAL_MS;
        return in.msSinceSample >= interval ? eMotion::SAMPLE : eMotion::NONE;
    }
    const bool bDue = in.speed > MOVING_SPEED || in.turnRate > TURNING_RATE || in.movedSinceSample >= SAMPLE_MOVE ||
        in.turnedSinceSample >= SAMPLE_TURN_DEGREES;
    if (bDue)
    {
        const uint32_t interval = in.speed > FAST_SPEED ? FAST_SAMPLE_INTERVAL_MS : SAMPLE_INTERVAL_MS;
        return in.msSinceSample >= interval ? eMotion::SAMPLE : eMotion::NONE;
    }
    if (in.bMotionActive && (in.bBecameStatic || in.msStill >= REST_MS))
    {
        return eMotion::REST_FULL;
    }
    return eMotion::NONE;
}

// An object the host's mission family created (design 5.3), with what the others have of it.
struct SOwnedObject
{
    int32_t handle = 0;
    uint8_t epoch = 0;          // OB-3 counts it up with every hand-over
    bool bSent = false;         // the others have it (a FULL went out, or nobody was there to get one)
    SObjectFull sent;           // the state they have (the fields of the last FULL)
    // motion
    MirrorObjectsWire::SPosition samplePosition{};  // the transform of the last sample or FULL
    SRotation sampleRotation{};
    uint32_t sampledAt = 0;     // GetTickCount of the last sample or FULL
    bool bSampleStatic = false; // the static flag then
    bool bMotionActive = false; // samples went out since the last FULL
    SMovementWatch movement;    // GetTickCount when it last moved
    // --coopdiag: its parent or holder had no network identity at the last capture (counted when that starts)
    bool bParentUnknown = false;
    bool bHolderUnknown = false;
};

class COwnedObjects
{
public:
    // false when it is no object handle, owned already or the set is full
    bool Add(int32_t handle)
    {
        if (!IsObjectHandle(handle) || Contains(handle) || m_nCount >= MAX_OWNED)
        {
            return false;
        }
        m_objects[m_nCount] = SOwnedObject{};
        m_objects[m_nCount].handle = handle;
        m_nCount++;
        return true;
    }

    int IndexOf(int32_t handle) const
    {
        for (int i = 0; i < m_nCount; i++)
        {
            if (m_objects[i].handle == handle)
            {
                return i;
            }
        }
        return -1;
    }

    bool Contains(int32_t handle) const { return IndexOf(handle) >= 0; }
    int Count() const { return m_nCount; }
    SOwnedObject& At(int i) { return m_objects[i]; }
    const SOwnedObject& At(int i) const { return m_objects[i]; }

    // the order doesn't matter: the last one takes its place
    void RemoveAt(int i)
    {
        if (i < 0 || i >= m_nCount)
        {
            return;
        }
        m_objects[i] = m_objects[m_nCount - 1];
        m_nCount--;
    }

    void Clear() { m_nCount = 0; }

private:
    SOwnedObject m_objects[MAX_OWNED]{};
    int m_nCount = 0;
};

// What a FULL that went out (or a REST_FULL) means for the motion state: the others have this transform now.
inline void OnFullSent(SOwnedObject& object, const SObjectFull& state, uint32_t now)
{
    object.bSent = true;
    object.sent = state;
    object.samplePosition = state.position;
    object.sampleRotation = state.rotation;
    object.sampledAt = now;
    object.bSampleStatic = (state.flags & MirrorObjectsWire::FLAG_STATIC) != 0;
    object.bMotionActive = false;
}

inline void OnSampleSent(SOwnedObject& object, const MirrorObjectsWire::SPosition& position, const SRotation& rotation,
    bool bStatic, uint32_t now)
{
    object.samplePosition = position;
    object.sampleRotation = rotation;
    object.sampledAt = now;
    object.bSampleStatic = bStatic;
    object.bMotionActive = true;
}

// ---- receivers ----

// MISSION_OBJECTS only from the current host: a former host's packet still in the packet buffer (it waits 50 ms there,
// PLAYER_DISCONNECTED and PLAYER_ASSIGN_HOST don't) is dropped, and so is everything while no host is known or while
// this game is the host.
inline bool AcceptFrom(bool bIsHost, bool bHasHost, int hostId, int senderId)
{
    return !bIsHost && bHasHost && senderId == hostId;
}

// the player whose motion counts for an object (OWNER_HOST: the host that sent the records)
inline int OwnerPlayerId(const SOwner& owner, int hostId)
{
    return owner.playerId == MirrorObjectsWire::OWNER_HOST ? hostId : owner.playerId;
}

// one motion sample as a receiver keeps it, stamped with the sender's server clock
struct SMotionSample
{
    uint32_t serverTime = 0;
    MirrorObjectsWire::SPosition position{};
    SRotation rotation{};
    MirrorObjectsWire::SSpeed moveSpeed{};
    MirrorObjectsWire::SSpeed turnSpeed{};
};

// One of the host's objects on a receiver: the last FULL (its owner and epoch included; OWNER entries update them) and
// the last two motion samples of the owner with that epoch, newest first.
struct SObjectRecord
{
    uint8_t idKind = MirrorObjectsWire::ID_MISSION;
    int32_t id = 0;
    SObjectFull state;
    uint32_t fullServerTime = 0;  // the sender's server clock on the packet of the last FULL (CMirrorObjects::HandlePacket)
    int sampleCount = 0;
    SMotionSample samples[2]{};
};

// A motion sample counts only from the object's owner with its current epoch, and only on a game that isn't the host.
inline bool AcceptMotion(bool bIsHost, bool bHasHost, int hostId, const SObjectRecord* pRecord, int senderId,
    uint8_t epoch)
{
    if (bIsHost || !bHasHost || pRecord == nullptr)
    {
        return false;
    }
    return OwnerPlayerId(pRecord->state.owner, hostId) == senderId && pRecord->state.owner.epoch == epoch;
}

// keeps the newest two samples by stamp; false for one older than both (a late unreliable packet) or a repeat
inline bool AddSample(SObjectRecord& record, const SMotionSample& sample)
{
    if (record.sampleCount > 0)
    {
        const int32_t sinceNewest = static_cast<int32_t>(sample.serverTime - record.samples[0].serverTime);
        if (sinceNewest <= 0)
        {
            return false;
        }
    }
    record.samples[1] = record.samples[0];
    record.samples[0] = sample;
    if (record.sampleCount < 2)
    {
        record.sampleCount++;
    }
    return true;
}

enum class eApplied : uint8_t
{
    CREATED,        // a FULL for an object the receiver didn't have
    UPDATED,        // a FULL for one it had
    REMOVED,
    OWNER_CHANGED,  // an OWNER entry changed the owner or epoch: the old samples are gone (a FULL does the same)
    OWNER_SAME,
    UNKNOWN,        // REMOVED or OWNER for an object the receiver doesn't have
    TABLE_FULL,
};

class CRecordTable
{
public:
    SObjectRecord* Find(uint8_t idKind, int32_t id)
    {
        for (int i = 0; i < m_nCount; i++)
        {
            if (m_records[i].id == id && m_records[i].idKind == idKind)
            {
                return &m_records[i];
            }
        }
        return nullptr;
    }

    const SObjectRecord* Find(uint8_t idKind, int32_t id) const
    {
        return const_cast<CRecordTable*>(this)->Find(idKind, id);
    }

    // one entry of MISSION_OBJECTS; affected gets the record as it is now (as it was, for REMOVED)
    eApplied Apply(const SObjectEntry& entry, SObjectRecord& affected)
    {
        SObjectRecord* pRecord = Find(entry.idKind, entry.id);
        switch (entry.op)
        {
            case MirrorObjectsWire::OP_REMOVED:
                if (pRecord == nullptr)
                {
                    return eApplied::UNKNOWN;
                }
                affected = *pRecord;
                *pRecord = m_records[m_nCount - 1];
                m_nCount--;
                return eApplied::REMOVED;
            case MirrorObjectsWire::OP_OWNER:
            {
                if (pRecord == nullptr)
                {
                    return eApplied::UNKNOWN;
                }
                const bool bChanged = SetOwner(*pRecord, entry.owner);
                affected = *pRecord;
                return bChanged ? eApplied::OWNER_CHANGED : eApplied::OWNER_SAME;
            }
            default:
            {
                bool bCreated = false;
                if (pRecord == nullptr)
                {
                    if (m_nCount >= MAX_RECORDS)
                    {
                        return eApplied::TABLE_FULL;
                    }
                    pRecord = &m_records[m_nCount++];
                    *pRecord = SObjectRecord{};
                    pRecord->idKind = entry.idKind;
                    pRecord->id = entry.id;
                    pRecord->state.owner = entry.full.owner;
                    bCreated = true;
                }
                SetOwner(*pRecord, entry.full.owner);
                pRecord->state = entry.full;
                affected = *pRecord;
                return bCreated ? eApplied::CREATED : eApplied::UPDATED;
            }
        }
    }

    int Count() const { return m_nCount; }
    SObjectRecord& At(int i) { return m_records[i]; }
    const SObjectRecord& At(int i) const { return m_records[i]; }
    void Clear() { m_nCount = 0; }

private:
    // a new owner or epoch: the samples of the old one are gone
    static bool SetOwner(SObjectRecord& record, const SOwner& owner)
    {
        if (record.state.owner.playerId == owner.playerId && record.state.owner.epoch == owner.epoch)
        {
            return false;
        }
        record.state.owner = owner;
        record.sampleCount = 0;
        return true;
    }

    SObjectRecord m_records[MAX_RECORDS]{};
    int m_nCount = 0;
};
}  // namespace MirrorObjectsPolicy
