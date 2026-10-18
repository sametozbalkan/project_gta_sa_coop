#pragma once

#include <cmath>
#include <cstdint>

#include "CMirrorObjectsPolicy.h"

// Private build: the rules of the receivers' object copies (CMirrorObjectCopies; docs/design/mission-objects.md 5.5-5.7,
// the receiver half of package OB-1), without game code, so tests/mirror_objects_test.cpp checks them without GTA:
// the kinematic interpolation window, the dynamic correction and snap thresholds, the resting put-back timer, the retry
// throttle and how a copy follows its parent or holder.
namespace MirrorObjectCopiesPolicy
{
using MirrorObjectsPolicy::SMotionSample;
using MirrorObjectsPolicy::SObjectRecord;
using MirrorObjectsWire::SPosition;
using MirrorObjectsWire::SRotation;

// ---- motion (design 5.6) ----

// kinematic objects are shown this far behind the host's clock (the 50 ms packet buffer plus one sample interval)
constexpr uint32_t RENDER_DELAY_MS = 100;
// past the newest sample they go on with its velocity this long, then hold
constexpr uint32_t MAX_EXTRAPOLATION_MS = 250;
// two samples further apart than this give no velocity (a pause in the motion): the newest sample's own speed counts
constexpr uint32_t MAX_SAMPLE_GAP_MS = 1000;
// a dynamic copy this close to a sample only takes its speeds; beyond SNAP_DISTANCE either kind jumps (counted)
constexpr float CORRECT_DISTANCE = 0.1f;
constexpr float SNAP_DISTANCE = 2.0f;
// at rest, a copy this far from the host's FULL for REST_PUT_BACK_MS is put back
constexpr float REST_DISTANCE = 0.3f;
constexpr float REST_ANGLE_DEGREES = 10.0f;
constexpr uint32_t REST_PUT_BACK_MS = 1000;
// a sample whose move and turn speed are both below this (engine units per 1/50 s: 2.5 cm/s) was placed by a script
constexpr float KINEMATIC_SPEED = 0.0005f;
// object pool slots a copy never takes, so this game's own world and scripts keep room (risk 7)
constexpr int POOL_RESERVE = 10;
// the engine's speeds are per 1/50 s: per millisecond that is 50 / 1000
constexpr float SPEED_PER_MS = 0.05f;

inline bool IsFrozen(uint16_t flags)
{
    return (flags & (MirrorObjectsWire::FLAG_DISABLE_COLLISION_FORCE | MirrorObjectsWire::FLAG_DONT_APPLY_SPEED)) != 0;
}

inline bool IsStill(const MirrorObjectsWire::SSpeed& speed)
{
    return std::fabs(speed.x) < KINEMATIC_SPEED && std::fabs(speed.y) < KINEMATIC_SPEED &&
        std::fabs(speed.z) < KINEMATIC_SPEED;
}

// Moved without physics (a frozen object, or one a script places: no speed) - interpolated; else the samples drive the
// copy's own physics.
inline bool IsKinematic(uint16_t flags, const SMotionSample& newest)
{
    return IsFrozen(flags) || (IsStill(newest.moveSpeed) && IsStill(newest.turnSpeed));
}

// The same for a record: an object the host's copy holds or has attached (its samples come at a reduced rate, for a
// receiver that can't resolve the parent or holder) is always interpolated, never left to the copy's own physics.
inline bool IsKinematic(const SObjectRecord& record)
{
    return MirrorObjectsPolicy::FollowsParent(record.state) ||
        (record.sampleCount > 0 && IsKinematic(record.state.flags, record.samples[0]));
}

// samples newer than the last FULL drive the copy; a FULL (the resting transform, or any change) puts it at the FULL's
// transform until the next sample. Both are stamped with the host's server clock, so a late sample from before a FULL
// doesn't restart the motion.
inline bool IsMotionActive(const SObjectRecord& record)
{
    return record.sampleCount > 0 && static_cast<int32_t>(record.samples[0].serverTime - record.fullServerTime) > 0;
}

// spherical interpolation along the shorter arc; t may run past 1 (extrapolation)
inline SRotation Slerp(const SRotation& a, const SRotation& b, float t)
{
    float bx = b.x, by = b.y, bz = b.z, bw = b.w;
    float dot = a.x * bx + a.y * by + a.z * bz + a.w * bw;
    if (dot < 0.0f)
    {
        dot = -dot;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }
    float wa = 1.0f - t;
    float wb = t;
    if (dot < 0.9995f)
    {
        const float theta = std::acos(dot > 1.0f ? 1.0f : dot);
        const float sinTheta = std::sin(theta);
        wa = std::sin((1.0f - t) * theta) / sinTheta;
        wb = std::sin(t * theta) / sinTheta;
    }
    SRotation q;
    q.x = wa * a.x + wb * bx;
    q.y = wa * a.y + wb * by;
    q.z = wa * a.z + wb * bz;
    q.w = wa * a.w + wb * bw;
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(length > 0.000001f) || !std::isfinite(length))
    {
        return b;
    }
    q.x /= length;
    q.y /= length;
    q.z /= length;
    q.w /= length;
    return q;
}

enum class ePoseSource : uint8_t
{
    NONE,          // no sample
    SAMPLE,        // at a sample (the older one while the render time hasn't passed it)
    INTERPOLATED,  // between the two newest
    EXTRAPOLATED,  // past the newest, within MAX_EXTRAPOLATION_MS
    HELD,          // past that: held where the extrapolation ended
};

struct SPose
{
    SPosition position{};
    SRotation rotation{};
    ePoseSource source = ePoseSource::NONE;
};

// Where a kinematic copy is shown at this server time (the receiver's g_serverTime): RENDER_DELAY_MS behind, between the
// two newest samples, then extrapolated for at most MAX_EXTRAPOLATION_MS.
inline SPose KinematicPose(const SObjectRecord& record, uint32_t serverNow)
{
    SPose pose;
    if (record.sampleCount <= 0)
    {
        return pose;
    }
    const SMotionSample& s0 = record.samples[0];
    const int32_t render = static_cast<int32_t>(serverNow - RENDER_DELAY_MS - s0.serverTime);  // relative to s0
    if (record.sampleCount >= 2)
    {
        const SMotionSample& s1 = record.samples[1];
        const int32_t gap = static_cast<int32_t>(s0.serverTime - s1.serverTime);
        const int32_t sinceS1 = render + gap;  // render time relative to s1
        if (gap > 0 && sinceS1 <= 0)
        {
            pose.position = s1.position;
            pose.rotation = s1.rotation;
            pose.source = ePoseSource::SAMPLE;
            return pose;
        }
        if (gap > 0 && render <= 0)
        {
            const float alpha = static_cast<float>(sinceS1) / static_cast<float>(gap);
            pose.position.x = s1.position.x + (s0.position.x - s1.position.x) * alpha;
            pose.position.y = s1.position.y + (s0.position.y - s1.position.y) * alpha;
            pose.position.z = s1.position.z + (s0.position.z - s1.position.z) * alpha;
            pose.rotation = Slerp(s1.rotation, s0.rotation, alpha);
            pose.source = ePoseSource::INTERPOLATED;
            return pose;
        }
        if (gap > 0 && render > 0 && static_cast<uint32_t>(gap) <= MAX_SAMPLE_GAP_MS)
        {
            const bool bHeld = static_cast<uint32_t>(render) > MAX_EXTRAPOLATION_MS;
            const int32_t extra = bHeld ? static_cast<int32_t>(MAX_EXTRAPOLATION_MS) : render;
            const float alpha = static_cast<float>(extra) / static_cast<float>(gap);
            pose.position.x = s0.position.x + (s0.position.x - s1.position.x) * alpha;
            pose.position.y = s0.position.y + (s0.position.y - s1.position.y) * alpha;
            pose.position.z = s0.position.z + (s0.position.z - s1.position.z) * alpha;
            pose.rotation = Slerp(s1.rotation, s0.rotation, 1.0f + alpha);
            pose.source = bHeld ? ePoseSource::HELD : ePoseSource::EXTRAPOLATED;
            return pose;
        }
    }
    // one sample (or two too far apart): at it, then on with its own speed
    pose.position = s0.position;
    pose.rotation = s0.rotation;
    if (render <= 0)
    {
        pose.source = ePoseSource::SAMPLE;
        return pose;
    }
    const bool bHeld = static_cast<uint32_t>(render) > MAX_EXTRAPOLATION_MS;
    const float ms = static_cast<float>(bHeld ? MAX_EXTRAPOLATION_MS : static_cast<uint32_t>(render));
    pose.position.x += s0.moveSpeed.x * SPEED_PER_MS * ms;
    pose.position.y += s0.moveSpeed.y * SPEED_PER_MS * ms;
    pose.position.z += s0.moveSpeed.z * SPEED_PER_MS * ms;
    pose.source = bHeld ? ePoseSource::HELD : ePoseSource::EXTRAPOLATED;
    return pose;
}

enum class eCorrection : uint8_t
{
    SPEEDS_ONLY,  // close: the speeds, the copy's physics goes on (ground contact stays smooth)
    SET,          // the sample's position, rotation and speeds
    SNAP,         // the same, counted: far off
};

// a dynamic copy's distance to a new sample
inline eCorrection DecideCorrection(float distance)
{
    if (!std::isfinite(distance) || distance > SNAP_DISTANCE)
    {
        return eCorrection::SNAP;
    }
    return distance < CORRECT_DISTANCE ? eCorrection::SPEEDS_ONLY : eCorrection::SET;
}

// a kinematic copy jumps when its shown pose moves this far in one step (only counted: it is placed anyway)
inline bool IsSnap(float distance)
{
    return !std::isfinite(distance) || distance > SNAP_DISTANCE;
}

inline bool IsOffRest(float distance, float angleDegrees)
{
    return !std::isfinite(distance) || distance > REST_DISTANCE || !std::isfinite(angleDegrees) ||
        angleDegrees > REST_ANGLE_DEGREES;
}

// A copy at rest that stays off the host's FULL for REST_PUT_BACK_MS is put back (design 5.5); coming back in range or
// a reset starts the time over. Unsigned elapsed time handles the tick wrap.
class CRestWatch
{
public:
    // true once when it is time to put the copy back
    bool Update(bool bOff, uint32_t now)
    {
        if (!bOff)
        {
            m_bOff = false;
            return false;
        }
        if (!m_bOff)
        {
            m_bOff = true;
            m_since = now;
            return false;
        }
        if (static_cast<uint32_t>(now - m_since) >= REST_PUT_BACK_MS)
        {
            m_bOff = false;
            return true;
        }
        return false;
    }

    void Reset() { m_bOff = false; }

private:
    bool m_bOff = false;
    uint32_t m_since = 0;
};

// ---- copies (design 5.5) ----

// A copy the pool or the model couldn't give, a hold task the ped dropped, an attachment the engine undid: tried at once,
// then at most once per RetryMs and MaxPerWindow times per WindowMs (CVehicleRecreateThrottle's rule for the vehicles'
// lost copies, CVehicleSyncPolicy.h). Unsigned elapsed time handles the tick wrap.
class CRetryThrottle
{
public:
    static constexpr uint32_t RetryMs = 1000;
    static constexpr uint32_t WindowMs = 30000;
    static constexpr unsigned MaxPerWindow = 3;

    bool ShouldAttempt(uint32_t now)
    {
        unsigned recent = 0;
        for (unsigned i = 0; i < m_count; i++)
        {
            const uint32_t age = static_cast<uint32_t>(now - m_attempts[i]);
            if (age < RetryMs)
            {
                return false;
            }
            if (age < WindowMs)
            {
                recent++;
            }
        }
        if (recent >= MaxPerWindow)
        {
            return false;
        }
        m_attempts[m_next] = now;
        m_next = (m_next + 1) % MaxPerWindow;
        if (m_count < MaxPerWindow)
        {
            m_count++;
        }
        return true;
    }

    // the window is used up: the next attempt waits for the oldest to leave it
    bool IsExhausted(uint32_t now) const
    {
        unsigned recent = 0;
        for (unsigned i = 0; i < m_count; i++)
        {
            if (static_cast<uint32_t>(now - m_attempts[i]) < WindowMs)
            {
                recent++;
            }
        }
        return recent >= MaxPerWindow;
    }

    void Reset()
    {
        m_count = 0;
        m_next = 0;
    }

private:
    uint32_t m_attempts[MaxPerWindow]{};
    unsigned m_count = 0;
    unsigned m_next = 0;
};

// a copy takes a pool slot only while more than POOL_RESERVE are free
inline bool CanTakePoolSlot(int freeSlots)
{
    return freeSlots > POOL_RESERVE;
}

// A FULL's model must be one an object is made of (CBaseModelInfo::GetModelType, plugin-sdk ModelInfoType): atomic 1,
// time 3, weapon 4, clump 5 or LOD atomic 8. A vehicle (6), a ped (7) or anything else gets no copy.
inline bool IsObjectModelType(int modelType)
{
    return modelType == 1 || modelType == 3 || modelType == 4 || modelType == 5 || modelType == 8;
}

// ---- parents and holders (design 5.7) ----

enum class eLink : uint8_t
{
    WORLD,      // its own transform (the FULL and the motion samples)
    ATTACH,     // AttachEntityToEntity to this game's copy of the parent
    HOLD_TASK,  // a CTaskSimpleHoldEntity on this game's copy of the holder
    HAND,       // placed at the holder's right hand every frame (the hold task's fallback)
};

struct SLinkInput
{
    uint8_t parentKind = MirrorObjectsWire::PARENT_NONE;
    bool bParentResolved = false;      // this game has the parent (live, pool generation checked)
    uint8_t holderKind = MirrorObjectsWire::HOLDER_NONE;
    bool bHolderResolved = false;      // this game has the holder: a live ped with its clump and intelligence
    bool bHolderIsLocalPlayer = false; // the holder is this game's own player: never a task on it
    bool bHoldSlotFree = false;        // its secondary slot 4 is empty, or holds our task for this copy
    bool bHoldRetriesLeft = false;     // the hold throttle allows a task (or ours is running)
    bool bHoldBoneValid = false;       // the host's bone is one the task can use (IsHoldBoneValid)
    bool bHoldAnimValid = false;       // the task's anim is none, or an id of its group on this game (IsAnimInGroup;
                                       // false while the group's anim block streams in)
};

// Held beats attached (the host's ped holds it; an attach of a held object is the hold task's own); an unresolved parent
// or holder leaves the copy at its own transform until it resolves (design 5.7's fallback: the FULL, then the host's
// reduced-rate samples of a held or attached object, interpolated).
inline eLink DecideLink(const SLinkInput& in)
{
    if (in.holderKind != MirrorObjectsWire::HOLDER_NONE)
    {
        if (!in.bHolderResolved)
        {
            return eLink::WORLD;
        }
        if (in.bHolderIsLocalPlayer || !in.bHoldSlotFree || !in.bHoldRetriesLeft || !in.bHoldBoneValid ||
            !in.bHoldAnimValid)
        {
            return eLink::HAND;
        }
        return eLink::HOLD_TASK;
    }
    if (in.parentKind != MirrorObjectsWire::PARENT_NONE)
    {
        return in.bParentResolved ? eLink::ATTACH : eLink::WORLD;
    }
    return eLink::WORLD;
}

inline const char* LinkName(eLink link)
{
    switch (link)
    {
        case eLink::ATTACH: return "attach";
        case eLink::HOLD_TASK: return "hold-task";
        case eLink::HAND: return "hand";
        default: return "world";
    }
}

// hold task animation (CTaskSimpleHoldEntity, constructor 0x6913A0): the host's anim id and group, or none. A task made
// with anim names (bAnimByName) carries no id: its names don't travel, so the copy is held without an animation, as the
// name constructor 0x691470 leaves a task without its block (anim id 0xBF, mov [esi+20h],0BFh at 0x6914C6; group 0).
constexpr int32_t ANIM_ID_NONE = 0xBF;
// AssocGroupId values the engine knows (gta-reversed AnimManager.cpp: ms_numAnimAssocDefinitions = 118)
constexpr int32_t ANIM_GROUP_COUNT = 118;

// the hold task places the entity at ped->m_apBones[bone] (gta-reversed TaskSimpleHoldEntity.cpp SetPedPosition), an
// array of 19 (plugin-sdk CPed.h, AnimBlendFrameData* m_apBones[19]): another bone gets no task, only the hand fallback
constexpr int PED_NODE_COUNT = 19;

inline bool IsHoldBoneValid(uint8_t bone)
{
    return bone < PED_NODE_COUNT;
}

struct SHoldAnim
{
    int32_t animId = ANIM_ID_NONE;
    int32_t animGroup = 0;
};

inline SHoldAnim DecideHoldAnim(const MirrorObjectsWire::SHolder& holder)
{
    SHoldAnim anim;
    if (holder.bAnimByName || holder.animId < 0 || holder.animGroup < 0 || holder.animGroup >= ANIM_GROUP_COUNT)
    {
        return anim;
    }
    anim.animId = holder.animId;
    anim.animGroup = holder.animGroup;
    return anim;
}

// The task blends anim id from group (CAnimManager::GetAnimAssociation 0x4D3A60 -> CAnimBlendAssocGroup::GetAnimation
// 0x4CE090: associations[id - the group's first id], unchecked), so an id must lie in [firstId, firstId + count) of
// the group as this game has it (count 0: the group's associations aren't made). ANIM_ID_NONE plays nothing.
inline bool IsAnimInGroup(int32_t animId, int32_t firstId, uint32_t count)
{
    if (animId == ANIM_ID_NONE)
    {
        return true;
    }
    return count > 0 && animId >= firstId && static_cast<int64_t>(animId) < static_cast<int64_t>(firstId) + count;
}

// A group whose anim block isn't loaded has no associations to check: its block is requested (as the task's own
// StartAnim does) at most once per ANIM_REQUEST_INTERVAL_MS while the copy waits in the hand.
constexpr uint32_t ANIM_REQUEST_INTERVAL_MS = 1000;

inline bool ShouldRequestAnimBlock(bool bRequested, uint32_t requestedAt, uint32_t now)
{
    return !bRequested || static_cast<uint32_t>(now - requestedAt) >= ANIM_REQUEST_INTERVAL_MS;
}

// What a hold task's anim allows: the task, the hand while its block streams in, or the hand for good (a bad id)
enum class eHoldAnim : uint8_t
{
    VALID,
    LOADING,
    INVALID,
};
}  // namespace MirrorObjectCopiesPolicy
