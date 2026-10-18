// Private build: policy test for the mission object mirror (client/src/CMirrorObjects.h, package OB-1) without GTA or
// ENet: which creates are owned, what counts as a change, the motion sampling rates, thresholds and resting FULL, the
// owner/epoch and sender filters, the receivers' record table and its resets, rotations, the bounds checks of the
// wire format (shared/network/mirror_objects.h), and the receivers' copies (client/src/CMirrorObjectCopiesPolicy.h:
// the interpolation window, the correction and snap thresholds, the put-back timer, the retry throttle, how a copy
// follows its parent or holder). Built and run by .tools\helpers\run_policy_tests.cmd (every
// CoopAndreas\tests\*_test.cpp with /I CoopAndreas\shared /I CoopAndreas\third_party).
#include "../client/src/CMirrorObjectsPolicy.h"
#include "../client/src/CMirrorObjectCopiesPolicy.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace MirrorObjectsPolicy;
namespace Wire = MirrorObjectsWire;

namespace
{
bool NearF(float a, float b, float epsilon) { return std::fabs(a - b) <= epsilon; }

SObjectFull MakeState()
{
    SObjectFull state;
    state.model = 1271;  // a crate
    state.area = 0;
    state.flags = Wire::FLAG_VISIBLE | Wire::FLAG_COLLISION | Wire::FLAG_LIFTABLE;
    state.position = Wire::SPosition{2490.5f, -1680.25f, 13.5f};
    state.rotation = SRotation{};
    return state;
}

SRotation AxisAngle(float ax, float ay, float az, float radians)
{
    const float length = std::sqrt(ax * ax + ay * ay + az * az);
    const float s = std::sin(radians * 0.5f) / length;
    SRotation q;
    q.x = ax * s;
    q.y = ay * s;
    q.z = az * s;
    q.w = std::cos(radians * 0.5f);
    return q;
}

// the records and the owned set are large: kept off the stack
CRecordTable g_table;
COwnedObjects g_owned;
}  // namespace

// ---- ownership (design 5.3) ----

void Ownership()
{
    CHECK(IsCreatingCommand(0x0107));
    CHECK(IsCreatingCommand(0x029B));
    CHECK(!IsCreatingCommand(0x0108));  // delete_object
    CHECK(!IsCreatingCommand(0x01C4));  // mark_object_as_no_longer_needed
    CHECK(!IsCreatingCommand(0x8107));  // the caller masks the not-flag first

    CHECK(IsObjectHandle(0));                       // slot 0 with its id wrapped to 0
    CHECK(IsObjectHandle((349 << 8) | 0x7F));       // the last slot of 1.0 US, the largest id
    CHECK(!IsObjectHandle(350 << 8));               // no slot 350
    CHECK(!IsObjectHandle((3 << 8) | 0x80));        // a free slot's byte (bit 0x80)
    CHECK(!IsObjectHandle(-1));
    CHECK(!IsObjectHandle(Wire::MISSION_ID_MAX + 1));

    g_owned.Clear();
    CHECK(g_owned.Add(0x0105));
    CHECK(!g_owned.Add(0x0105));  // owned already
    CHECK(!g_owned.Add(-1));
    CHECK(g_owned.Add(0x0205));
    CHECK(g_owned.Contains(0x0105) && g_owned.Contains(0x0205) && g_owned.Count() == 2);
    // the same slot with another id byte is another object
    CHECK(!g_owned.Contains(0x0106));
    g_owned.RemoveAt(g_owned.IndexOf(0x0105));
    CHECK(!g_owned.Contains(0x0105) && g_owned.Contains(0x0205) && g_owned.Count() == 1);
    g_owned.Clear();
    for (int slot = 0; slot < MAX_OWNED; slot++)
    {
        CHECK(g_owned.Add(slot << 8));
    }
    CHECK(!g_owned.Add((10 << 8) | 1));  // full
    g_owned.Clear();
}

// ---- change detection (design 5.4) ----

void ChangeDetection()
{
    const SObjectFull base = MakeState();
    SObjectFull now = base;

    // transform, speeds and the static flag are no field change
    now.position.x += 5.0f;
    now.rotation = AxisAngle(0.0f, 0.0f, 1.0f, 1.0f);
    now.moveSpeed.x = 0.5f;
    now.turnSpeed.z = 0.1f;
    now.flags |= Wire::FLAG_STATIC;
    CHECK(SameFields(base, now));

    now = base;
    now.model = 1272;
    CHECK(!SameFields(base, now));
    now = base;
    now.area = 3;
    CHECK(!SameFields(base, now));
    now = base;
    now.flags = static_cast<uint16_t>(now.flags & ~Wire::FLAG_VISIBLE);
    CHECK(!SameFields(base, now));
    now = base;
    now.flags |= Wire::FLAG_DISABLE_COLLISION_FORCE | Wire::FLAG_DONT_APPLY_SPEED;  // freeze_object
    CHECK(!SameFields(base, now));
    now = base;
    now.flags |= Wire::FLAG_BROKEN;
    CHECK(!SameFields(base, now));
    now = base;
    now.bScaled = true;
    now.scale = 2.0f;
    CHECK(!SameFields(base, now));
    now = base;
    now.owner.epoch = 1;
    CHECK(!SameFields(base, now));

    // attachments: a new parent, another offset or rotation; closer than the wire's resolution is the same
    SObjectFull attached = base;
    attached.parent.kind = Wire::PARENT_VEHICLE;
    attached.parent.id = 12;
    attached.parent.offsetX = 0.2f;
    attached.parent.offsetY = -1.5f;
    attached.parent.offsetZ = 0.4f;
    attached.parent.eulerZ = 1.5f;
    CHECK(!SameFields(base, attached));
    now = attached;
    now.parent.offsetY += 0.0005f;
    CHECK(SameFields(attached, now));
    now.parent.offsetY += 0.01f;
    CHECK(!SameFields(attached, now));
    now = attached;
    now.parent.id = 13;
    CHECK(!SameFields(attached, now));
    now = attached;
    now.parent.eulerZ = 1.6f;
    CHECK(!SameFields(attached, now));
    now = attached;
    now.parent.bQuaternion = true;
    now.parent.rotation = AxisAngle(0.0f, 0.0f, 1.0f, 0.5f);
    SObjectFull quaternion = now;
    now.parent.rotation.x = -now.parent.rotation.x;  // q and -q are the same rotation
    now.parent.rotation.y = -now.parent.rotation.y;
    now.parent.rotation.z = -now.parent.rotation.z;
    now.parent.rotation.w = -now.parent.rotation.w;
    CHECK(SameFields(quaternion, now));
    now.parent.rotation = AxisAngle(0.0f, 0.0f, 1.0f, 0.52f);
    CHECK(!SameFields(quaternion, now));

    // holders
    SObjectFull held = base;
    held.holder.kind = Wire::HOLDER_PLAYER;
    held.holder.id = 1;
    held.holder.bone = 24;
    held.holder.animId = 0x1A3;
    CHECK(!SameFields(base, held));
    now = held;
    now.holder.id = 0;
    CHECK(!SameFields(held, now));
    now = held;
    now.holder.kind = Wire::HOLDER_PED;
    CHECK(!SameFields(held, now));
    now = held;
    now.holder.bAnimByName = true;
    CHECK(!SameFields(held, now));
    now = held;
    now.holder.offsetZ = 0.1f;
    CHECK(!SameFields(held, now));

    CHECK(!FollowsParent(base));
    CHECK(FollowsParent(attached));
    CHECK(FollowsParent(held));

    CHECK(IsOffsetSendable(32.0f, -32.0f, 0.0f));
    CHECK(!IsOffsetSendable(33.0f, 0.0f, 0.0f));
    CHECK(!IsOffsetSendable(0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f));
}

// ---- rotations ----

void Rotations()
{
    const SRotation cases[] = {
        SRotation{},
        AxisAngle(0.0f, 0.0f, 1.0f, 1.2f),
        AxisAngle(1.0f, 0.0f, 0.0f, 3.14159265f),   // 180 degrees about x
        AxisAngle(0.0f, 1.0f, 0.0f, 3.14159265f),
        AxisAngle(0.0f, 0.0f, 1.0f, 3.14159265f),
        AxisAngle(1.0f, 2.0f, -3.0f, -2.5f),
        AxisAngle(-0.3f, 0.9f, 0.1f, 0.01f),
    };
    for (const SRotation& q : cases)
    {
        const SRotation back = QuaternionFromAxes(AxesFromQuaternion(q));
        CHECK(AngleBetweenDegrees(q, back) < 0.05f);
        const float length = std::sqrt(back.x * back.x + back.y * back.y + back.z * back.z + back.w * back.w);
        CHECK(NearF(length, 1.0f, 0.0001f));
    }
    // the axes of a rotation of 90 degrees about z: right -> +y, forward -> -x
    const SAxes axes = AxesFromQuaternion(AxisAngle(0.0f, 0.0f, 1.0f, 1.5707963f));
    CHECK(NearF(axes.right[0], 0.0f, 0.0001f) && NearF(axes.right[1], 1.0f, 0.0001f));
    CHECK(NearF(axes.forward[0], -1.0f, 0.0001f) && NearF(axes.forward[1], 0.0f, 0.0001f));
    CHECK(NearF(axes.up[2], 1.0f, 0.0001f));
    CHECK(NearF(AngleBetweenDegrees(SRotation{}, AxisAngle(0.0f, 0.0f, 1.0f, 0.5f)), 28.6479f, 0.01f));
    // a degenerate matrix gives the identity, not NaN
    SAxes zero;
    zero.right[0] = zero.forward[1] = zero.up[2] = 0.0f;
    const SRotation fallback = QuaternionFromAxes(zero);
    CHECK(std::isfinite(fallback.x) && std::isfinite(fallback.w));
}

// ---- motion (design 5.6) ----

void Motion()
{
    // movement: the speeds at once, the transform over 100 ms windows
    {
        SMovementWatch watch;
        Wire::SPosition at{0.0f, 0.0f, 0.0f};
        CHECK(!watch.Update(at, SRotation{}, 0.0f, 0.0f, 1000));  // the first frame starts the window
        CHECK(!watch.Update(at, SRotation{}, 0.04f, 0.005f, 1005));  // a resting crate's jitter
        CHECK(watch.Update(at, SRotation{}, 0.06f, 0.0f, 1010) && watch.lastMovingAt == 1010);
        CHECK(watch.Update(at, SRotation{}, 0.0f, 0.02f, 1020));
        // a slow creeper at a high frame rate: 0.2 mm per 2 ms frame never counts per frame, but does per window
        uint32_t now = 2000;
        watch = SMovementWatch{};
        bool bMoved = false;
        for (int frame = 0; frame <= 60; frame++, now += 2)
        {
            at.x = 0.0002f * static_cast<float>(frame);
            bMoved = watch.Update(at, SRotation{}, 0.0f, 0.0f, now) || bMoved;
        }
        CHECK(bMoved && watch.lastMovingAt >= 2100);
        // held still: the window that saw the last of the movement counts, the next one doesn't
        for (int frame = 0; frame <= 60; frame++, now += 2)
        {
            watch.Update(at, SRotation{}, 0.0f, 0.0f, now);
        }
        const uint32_t moved = watch.lastMovingAt;
        CHECK(moved >= 2200 && moved <= 2202);
        for (int frame = 0; frame <= 60; frame++, now += 2)
        {
            CHECK(!watch.Update(at, SRotation{}, 0.0f, 0.0f, now));
        }
        CHECK(watch.lastMovingAt == moved);
        // a turn over a window
        now += 100;
        CHECK(watch.Update(at, AxisAngle(0.0f, 0.0f, 1.0f, 0.003f), 0.0f, 0.0f, now));  // 0.17 degrees
    }

    SMotionInput in;
    in.speed = 1.0f;
    in.msSinceSample = 99;
    CHECK(DecideMotion(in) == eMotion::NONE);  // 10 Hz
    in.msSinceSample = 100;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.speed = 5.5f;  // a crate thrown from the train
    in.msSinceSample = 49;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.msSinceSample = 50;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.speed = 5.0f;  // 5 m/s exactly: still 10 Hz
    CHECK(DecideMotion(in) == eMotion::NONE);

    // no speed, but a turn, or the position or rotation changed enough since the last sample
    in = SMotionInput{};
    in.msSinceSample = 200;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.turnRate = 0.02f;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.turnRate = 0.0f;
    in.movedSinceSample = 0.009f;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.movedSinceSample = 0.01f;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.movedSinceSample = 0.0f;
    in.turnedSinceSample = 0.49f;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.turnedSinceSample = 0.5f;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);

    // held or attached to an entity the others know: samples at 5 Hz (20 Hz fast) once it changed, never a resting FULL
    in = SMotionInput{};
    in.bFollowsParent = true;
    in.speed = 1.0f;
    in.msSinceSample = 500;
    CHECK(DecideMotion(in) == eMotion::NONE);  // no change since the last sample: speed alone doesn't count
    in.movedSinceSample = 0.05f;
    in.msSinceSample = 199;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.msSinceSample = 200;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.movedSinceSample = 0.0f;
    in.turnedSinceSample = 0.5f;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);  // a turn alone (a held cigar)
    in.turnedSinceSample = 0.0f;
    in.speed = 10.0f;  // crates on the caboose at speed
    in.movedSinceSample = 0.5f;
    in.msSinceSample = 50;
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.msSinceSample = 49;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.speed = 0.0f;  // an attached object's own speed may be zero: the distance over the time counts
    in.movedSinceSample = 0.6f;
    in.msSinceSample = 100;  // 6 m/s
    CHECK(DecideMotion(in) == eMotion::SAMPLE);
    in.movedSinceSample = 0.4f;  // 4 m/s: 5 Hz
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.movedSinceSample = 0.0f;
    in.bMotionActive = true;
    in.msStill = 1000;
    in.bBecameStatic = true;
    CHECK(DecideMotion(in) == eMotion::NONE);

    // the resting FULL: 300 ms still after samples went out, or at once when it became static
    in = SMotionInput{};
    in.bMotionActive = true;
    in.msSinceSample = 400;
    in.msStill = 299;
    CHECK(DecideMotion(in) == eMotion::NONE);
    in.msStill = 300;
    CHECK(DecideMotion(in) == eMotion::REST_FULL);
    in.msStill = 20;
    in.bBecameStatic = true;
    CHECK(DecideMotion(in) == eMotion::REST_FULL);
    // nothing went out since the last FULL: nothing to correct
    in.bMotionActive = false;
    in.msStill = 5000;
    CHECK(DecideMotion(in) == eMotion::NONE);

    // what a sample and a FULL do to the host's state
    SOwnedObject object;
    object.handle = 0x0301;
    SObjectFull state = MakeState();
    OnFullSent(object, state, 1000);
    CHECK(object.bSent && !object.bMotionActive && object.sampledAt == 1000);
    CHECK(Distance(object.samplePosition, state.position) == 0.0f && !object.bSampleStatic);
    Wire::SPosition moved = state.position;
    moved.z -= 2.0f;
    OnSampleSent(object, moved, state.rotation, false, 1100);
    CHECK(object.bMotionActive && object.sampledAt == 1100 && NearF(Distance(object.samplePosition, state.position), 2.0f, 0.0001f));
    state.flags |= Wire::FLAG_STATIC;
    OnFullSent(object, state, 1500);
    CHECK(!object.bMotionActive && object.bSampleStatic && SameFields(object.sent, state));
}

// ---- receivers: senders, owners and epochs ----

void Filters()
{
    // MISSION_OBJECTS from the current host only, never on the host, never without one
    CHECK(AcceptFrom(false, true, 2, 2));
    CHECK(!AcceptFrom(false, true, 2, 1));  // a former host's packet left in the buffer
    CHECK(!AcceptFrom(true, true, 2, 2));
    CHECK(!AcceptFrom(false, false, -1, 0));

    SObjectRecord record;
    record.state.owner.playerId = Wire::OWNER_HOST;
    record.state.owner.epoch = 4;
    CHECK(OwnerPlayerId(record.state.owner, 3) == 3);
    CHECK(AcceptMotion(false, true, 3, &record, 3, 4));
    CHECK(!AcceptMotion(false, true, 3, &record, 3, 5));  // another epoch
    CHECK(!AcceptMotion(false, true, 3, &record, 1, 4));  // not the owner
    CHECK(!AcceptMotion(true, true, 3, &record, 3, 4));   // this game is the host
    CHECK(!AcceptMotion(false, false, -1, &record, 3, 4));
    CHECK(!AcceptMotion(false, true, 3, nullptr, 3, 4));  // no FULL yet
    // OB-3: handed to player 1; the host's own motion no longer counts
    record.state.owner.playerId = 1;
    CHECK(AcceptMotion(false, true, 3, &record, 1, 4));
    CHECK(!AcceptMotion(false, true, 3, &record, 3, 4));
}

// ---- receivers: the record table and its resets ----

Wire::SObjectEntry Full(int32_t id, uint8_t epoch = 0)
{
    Wire::SObjectEntry entry;
    entry.idKind = Wire::ID_MISSION;
    entry.id = id;
    entry.op = Wire::OP_FULL;
    entry.full = MakeState();
    entry.full.owner.epoch = epoch;
    return entry;
}

Wire::SObjectEntry Op(int32_t id, uint8_t op)
{
    Wire::SObjectEntry entry;
    entry.idKind = Wire::ID_MISSION;
    entry.id = id;
    entry.op = op;
    return entry;
}

SMotionSample Sample(uint32_t stamp, float z)
{
    SMotionSample sample;
    sample.serverTime = stamp;
    sample.position = Wire::SPosition{1.0f, 2.0f, z};
    return sample;
}

void Records()
{
    g_table.Clear();
    SObjectRecord affected;
    CHECK(g_table.Apply(Full(0x0101), affected) == eApplied::CREATED);
    CHECK(affected.id == 0x0101 && affected.state.model == 1271 && g_table.Count() == 1);
    Wire::SObjectEntry changed = Full(0x0101);
    changed.full.flags = Wire::FLAG_VISIBLE;
    CHECK(g_table.Apply(changed, affected) == eApplied::UPDATED);
    CHECK(g_table.Find(Wire::ID_MISSION, 0x0101)->state.flags == Wire::FLAG_VISIBLE);
    // a persistent record (OB-4) with the same number is another record
    Wire::SObjectEntry persistent = Full(0x0101);
    persistent.idKind = Wire::ID_PERSISTENT;
    CHECK(g_table.Apply(persistent, affected) == eApplied::CREATED);
    CHECK(g_table.Count() == 2);

    // samples: the newest two by stamp, late ones dropped, across the clock's wrap
    SObjectRecord* pRecord = g_table.Find(Wire::ID_MISSION, 0x0101);
    CHECK(pRecord != nullptr);
    CHECK(AddSample(*pRecord, Sample(0xFFFFFF00u, 1.0f)));
    CHECK(AddSample(*pRecord, Sample(0x00000010u, 2.0f)));  // after the wrap
    CHECK(pRecord->sampleCount == 2 && pRecord->samples[0].position.z == 2.0f && pRecord->samples[1].position.z == 1.0f);
    CHECK(!AddSample(*pRecord, Sample(0xFFFFFF80u, 9.0f)));  // older than the newest
    CHECK(!AddSample(*pRecord, Sample(0x00000010u, 9.0f)));  // the same stamp again
    CHECK(AddSample(*pRecord, Sample(0x00000060u, 3.0f)));
    CHECK(pRecord->sampleCount == 2 && pRecord->samples[0].position.z == 3.0f && pRecord->samples[1].position.z == 2.0f);

    // a FULL with the same owner keeps the samples; a new owner or epoch drops them
    CHECK(g_table.Apply(Full(0x0101), affected) == eApplied::UPDATED && affected.sampleCount == 2);
    Wire::SObjectEntry owner = Op(0x0101, Wire::OP_OWNER);
    owner.owner.playerId = Wire::OWNER_HOST;
    owner.owner.epoch = 0;
    CHECK(g_table.Apply(owner, affected) == eApplied::OWNER_SAME && affected.sampleCount == 2);
    owner.owner.playerId = 1;
    owner.owner.epoch = 1;
    CHECK(g_table.Apply(owner, affected) == eApplied::OWNER_CHANGED && affected.sampleCount == 0);
    CHECK(affected.state.owner.playerId == 1 && affected.state.owner.epoch == 1);
    CHECK(AddSample(*g_table.Find(Wire::ID_MISSION, 0x0101), Sample(0x100, 4.0f)));
    CHECK(g_table.Apply(Full(0x0101, 2), affected) == eApplied::UPDATED && affected.sampleCount == 0);
    CHECK(affected.state.owner.playerId == Wire::OWNER_HOST && affected.state.owner.epoch == 2);

    // removals; unknown ids
    CHECK(g_table.Apply(Op(0x0202, Wire::OP_REMOVED), affected) == eApplied::UNKNOWN);
    CHECK(g_table.Apply(Op(0x0202, Wire::OP_OWNER), affected) == eApplied::UNKNOWN);
    CHECK(g_table.Apply(Op(0x0101, Wire::OP_REMOVED), affected) == eApplied::REMOVED);
    CHECK(affected.id == 0x0101 && affected.idKind == Wire::ID_MISSION);
    CHECK(g_table.Find(Wire::ID_MISSION, 0x0101) == nullptr && g_table.Count() == 1);
    CHECK(g_table.Find(Wire::ID_PERSISTENT, 0x0101) != nullptr);

    // the host's bReset, a host change or leave: everything goes
    g_table.Clear();
    CHECK(g_table.Count() == 0 && g_table.Find(Wire::ID_PERSISTENT, 0x0101) == nullptr);

    // the table holds a whole object pool and more; beyond it, FULLs are refused
    for (int i = 0; i < MAX_RECORDS; i++)
    {
        CHECK(g_table.Apply(Full(i), affected) == eApplied::CREATED);
    }
    CHECK(g_table.Apply(Full(MAX_RECORDS), affected) == eApplied::TABLE_FULL);
    CHECK(g_table.Apply(Full(7), affected) == eApplied::UPDATED);
    g_table.Clear();
}

// ---- the wire format ----

template <typename T>
int Write(T& value, uint8_t* buffer, int size, bool& bOk)
{
    serialize::WriteStream stream(buffer, size);
    bOk = value.Serialize(stream);
    stream.Flush();
    return stream.GetBytesProcessed();
}

template <typename T>
bool Read(T& value, const uint8_t* buffer, int size)
{
    serialize::ReadStream stream(buffer, size);
    return value.Serialize(stream);
}

Wire::SObjectEntry Everything()
{
    Wire::SObjectEntry entry = Full((349 << 8) | 0x7F, 200);
    SObjectFull& full = entry.full;
    full.model = Wire::MODEL_COUNT - 1;
    full.area = 255;
    full.flags = Wire::FLAGS_KNOWN;
    full.bScaled = true;
    full.scale = 0.5f;
    full.position = Wire::SPosition{-2900.123f, 2999.0f, 999.5f};
    full.rotation = AxisAngle(1.0f, -2.0f, 0.5f, 2.0f);
    full.moveSpeed = Wire::SSpeed{0.3f, -0.2f, 1.4f};
    full.turnSpeed = Wire::SSpeed{3.0f, 0.0f, -2.0f};  // beyond the compressed range: raw
    full.owner.playerId = 7;
    full.parent.kind = Wire::PARENT_OBJECT;
    full.parent.id = Wire::MISSION_ID_MAX;
    full.parent.offsetX = -32.767f;
    full.parent.offsetY = 1.234f;
    full.parent.offsetZ = 32.767f;
    full.parent.bQuaternion = true;
    full.parent.rotation = AxisAngle(0.0f, 1.0f, 0.0f, -1.0f);
    full.holder.kind = Wire::HOLDER_PED;
    full.holder.id = 254;
    full.holder.offsetX = 0.1f;
    full.holder.offsetY = -0.2f;
    full.holder.offsetZ = 0.3f;
    full.holder.bone = 35;
    full.holder.boneFlags = 0x10;
    full.holder.animId = Wire::ANIM_MAX;
    full.holder.animGroup = -1;
    full.holder.bAnimByName = true;
    full.holder.bDisallowDrop = true;
    return entry;
}

void WireFormat()
{
    alignas(4) uint8_t buffer[512]{};
    bool bOk = false;

    Wire::SObjectEntry sent = Everything();
    const int bytes = Write(sent, buffer, sizeof(buffer), bOk);
    CHECK(bOk && bytes > 0 && bytes <= 100);
    Wire::SObjectEntry got;
    CHECK(Read(got, buffer, bytes));
    const SObjectFull& a = sent.full;
    const SObjectFull& b = got.full;
    CHECK(got.idKind == sent.idKind && got.id == sent.id && got.op == Wire::OP_FULL);
    CHECK(b.model == a.model && b.area == a.area && b.flags == a.flags && b.bScaled && b.scale == 0.5f);
    CHECK(NearF(b.position.x, a.position.x, 0.001f) && NearF(b.position.y, a.position.y, 0.001f) &&
        NearF(b.position.z, a.position.z, 0.001f));
    CHECK(AngleBetweenDegrees(a.rotation, b.rotation) < 0.3f);
    CHECK(NearF(b.moveSpeed.z, 1.4f, 0.001f) && b.turnSpeed.x == 3.0f && b.turnSpeed.z == -2.0f);
    CHECK(b.owner.playerId == 7 && b.owner.epoch == 200);
    CHECK(b.parent.kind == Wire::PARENT_OBJECT && b.parent.id == Wire::MISSION_ID_MAX && b.parent.bQuaternion);
    CHECK(NearF(b.parent.offsetX, -32.767f, 0.0005f) && NearF(b.parent.offsetY, 1.234f, 0.0005f));
    CHECK(AngleBetweenDegrees(a.parent.rotation, b.parent.rotation) < 0.3f);
    CHECK(SameHolder(a.holder, b.holder));
    CHECK(SameFields(a, b));

    // an Euler attachment and the identity rotation
    Wire::SObjectEntry euler = Full(5);
    euler.full.parent.kind = Wire::PARENT_VEHICLE;
    euler.full.parent.id = 254;
    euler.full.parent.eulerX = 0.0f;
    euler.full.parent.eulerY = -3.0f;
    euler.full.parent.eulerZ = 7.0f;  // sent as 7 - 2pi
    CHECK(Write(euler, buffer, sizeof(buffer), bOk) > 0 && bOk);
    CHECK(Read(got, buffer, sizeof(buffer)));
    CHECK(!got.full.parent.bQuaternion && NearF(got.full.parent.eulerY, -3.0f, 0.0002f));
    CHECK(NearF(got.full.parent.eulerZ, 7.0f - 6.2831853f, 0.0002f));
    CHECK(got.full.holder.kind == Wire::HOLDER_NONE && !got.full.bScaled && got.full.scale == 1.0f);
    CHECK(AngleBetweenDegrees(got.full.rotation, SRotation{}) < 0.01f);

    // REMOVED and OWNER are a few bytes
    Wire::SObjectEntry removed = Op(0x15D7F, Wire::OP_REMOVED);
    CHECK(Write(removed, buffer, sizeof(buffer), bOk) <= 4 && bOk);
    CHECK(Read(got, buffer, 4) && got.op == Wire::OP_REMOVED && got.id == 0x15D7F);
    Wire::SObjectEntry owner = Op(9, Wire::OP_OWNER);
    owner.owner.playerId = 3;
    owner.owner.epoch = 255;
    CHECK(Write(owner, buffer, sizeof(buffer), bOk) <= 8 && bOk);
    CHECK(Read(got, buffer, 8) && got.op == Wire::OP_OWNER && got.owner.playerId == 3 && got.owner.epoch == 255);

    // a motion sample
    Wire::SMotionEntry motion;
    motion.id = 0x0203;
    motion.epoch = 9;
    motion.position = Wire::SPosition{10.0f, 20.0f, 30.0f};
    motion.rotation = AxisAngle(0.0f, 0.0f, 1.0f, -0.7f);
    motion.moveSpeed = Wire::SSpeed{0.1f, 0.0f, -0.3f};
    const int motionBytes = Write(motion, buffer, sizeof(buffer), bOk);
    CHECK(bOk && motionBytes <= 24);
    Wire::SMotionEntry motionBack;
    CHECK(Read(motionBack, buffer, motionBytes));
    CHECK(motionBack.id == 0x0203 && motionBack.epoch == 9 && NearF(motionBack.position.z, 30.0f, 0.001f));
    CHECK(AngleBetweenDegrees(motionBack.rotation, motion.rotation) < 0.3f);
    CHECK(motionBack.turnSpeed.x == 0.0f && NearF(motionBack.moveSpeed.z, -0.3f, 0.001f));

    // the writer refuses what the reader would refuse
    Wire::SObjectEntry bad = Everything();
    bad.full.position.x = std::numeric_limits<float>::quiet_NaN();
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);
    bad = Everything();
    bad.full.flags = 0x0100;
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);
    bad = Everything();
    bad.full.scale = 0.0f;
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);
    bad = Everything();
    bad.id = Wire::MISSION_ID_MAX + 1;
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);
    bad = Everything();
    bad.full.parent.kind = Wire::PARENT_PLAYER;
    bad.full.parent.id = Config::MAX_SERVER_PLAYERS;
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);

    // the reader refuses fields out of range: raw bits after a valid id (kind 0, id 5)
    {
        serialize::WriteStream raw(buffer, sizeof(buffer));
        uint32_t value = 0;
        raw.SerializeBits(value, 1);  // ID_MISSION
        value = 5;
        raw.SerializeBits(value, 17);
        value = 3;                    // op 3: unknown
        raw.SerializeBits(value, 2);
        raw.Flush();
        CHECK(!Read(got, buffer, raw.GetBytesProcessed()));
    }
    {
        // a FULL whose model is past the model table
        serialize::WriteStream raw(buffer, sizeof(buffer));
        uint32_t value = 0;
        raw.SerializeBits(value, 1);
        value = 5;
        raw.SerializeBits(value, 17);
        value = 0;                    // OP_FULL
        raw.SerializeBits(value, 2);
        value = 32767;                // model: 15 bits, beyond 19999
        raw.SerializeBits(value, 15);
        raw.Flush();
        CHECK(!Read(got, buffer, sizeof(buffer)));
    }
    {
        // a FULL whose flags carry an unknown bit, or whose scale is NaN
        Wire::SObjectEntry entry = Full(5);
        entry.full.bScaled = true;
        entry.full.scale = 2.0f;
        const int size = Write(entry, buffer, sizeof(buffer), bOk);
        CHECK(bOk);
        // the flags are bits 18+2+15+8 = 43.. of the stream: set bit 8 of them
        const int flagsBit = 1 + 17 + 2 + 15 + 8;
        alignas(4) uint8_t copy[512]{};
        std::memcpy(copy, buffer, sizeof(copy));
        // the bit packer writes little-endian 32-bit words, lowest bit first
        const int bit = flagsBit + 8;
        copy[bit / 8] = static_cast<uint8_t>(copy[bit / 8] | (1u << (bit % 8)));
        CHECK(!Read(got, copy, size));
        std::memcpy(copy, buffer, sizeof(copy));
        const int scaleBit = flagsBit + 16 + 1;
        const uint32_t nan = 0x7FC00000u;
        for (int i = 0; i < 32; i++)
        {
            const int at = scaleBit + i;
            const uint8_t mask = static_cast<uint8_t>(1u << (at % 8));
            copy[at / 8] = static_cast<uint8_t>(((nan >> i) & 1u) ? (copy[at / 8] | mask) : (copy[at / 8] & ~mask));
        }
        CHECK(!Read(got, copy, size));
        CHECK(Read(got, buffer, size) && got.full.scale == 2.0f);  // the untouched one reads
    }
    // a buffer cut short is refused
    const int whole = Write(sent, buffer, sizeof(buffer), bOk);
    CHECK(bOk && !Read(got, buffer, whole / 2));

    // raw positions and speeds are clamped when read
    {
        Wire::SMotionEntry far;
        far.position = Wire::SPosition{50000.0f, -1.0e30f, 5000.0f};
        far.moveSpeed = Wire::SSpeed{40.0f, -0.5f, -1.0e20f};
        const int size = Write(far, buffer, sizeof(buffer), bOk);
        CHECK(bOk);
        Wire::SMotionEntry back;
        CHECK(Read(back, buffer, size));
        CHECK(back.position.x == Wire::POSITION_LIMIT && back.position.y == -Wire::POSITION_LIMIT &&
            back.position.z == 5000.0f);
        CHECK(back.moveSpeed.x == Wire::SPEED_LIMIT && back.moveSpeed.y == -0.5f &&
            back.moveSpeed.z == -Wire::SPEED_LIMIT);
    }
    // a rotation whose three sent components add up past 1 is normalized when read
    {
        serialize::WriteStream raw(buffer, sizeof(buffer));
        uint32_t value = 0;
        raw.SerializeBits(value, 2);  // the largest is x
        for (int i = 0; i < 3; i++)
        {
            value = 1022;             // +511 steps: 0.7072 each
            raw.SerializeBits(value, 10);
        }
        raw.Flush();
        SRotation rotation;
        CHECK(Read(rotation, buffer, raw.GetBytesProcessed()));
        const float length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z +
            rotation.w * rotation.w);
        CHECK(NearF(length, 1.0f, 0.0001f) && rotation.x == 0.0f && NearF(rotation.y, 0.57735f, 0.0001f));
    }
}

// ---- the receivers' copies (design 5.5-5.7) ----

namespace
{
namespace Copies = MirrorObjectCopiesPolicy;

MirrorObjectsPolicy::SMotionSample CopySample(uint32_t serverTime, float x, const SRotation& rotation = SRotation{})
{
    MirrorObjectsPolicy::SMotionSample sample;
    sample.serverTime = serverTime;
    sample.position = Wire::SPosition{x, 0.0f, 10.0f};
    sample.rotation = rotation;
    return sample;
}

SObjectRecord TwoSamples(uint32_t t1, float x1, uint32_t t0, float x0)
{
    SObjectRecord record;
    record.state = MakeState();
    record.sampleCount = 2;
    record.samples[1] = CopySample(t1, x1);
    record.samples[0] = CopySample(t0, x0);
    return record;
}
}  // namespace

void CopiesMotion()
{
    // two samples 100 ms apart, x 0 -> 1, shown 100 ms behind the clock
    const SObjectRecord two = TwoSamples(1000, 0.0f, 1100, 1.0f);
    Copies::SPose pose = Copies::KinematicPose(two, 1000);  // before the older one
    CHECK(pose.source == Copies::ePoseSource::SAMPLE && NearF(pose.position.x, 0.0f, 1e-5f));
    pose = Copies::KinematicPose(two, 1100);  // render time = the older sample
    CHECK(pose.source == Copies::ePoseSource::SAMPLE && NearF(pose.position.x, 0.0f, 1e-5f));
    pose = Copies::KinematicPose(two, 1150);
    CHECK(pose.source == Copies::ePoseSource::INTERPOLATED && NearF(pose.position.x, 0.5f, 1e-4f));
    pose = Copies::KinematicPose(two, 1200);  // at the newest
    CHECK(pose.source == Copies::ePoseSource::INTERPOLATED && NearF(pose.position.x, 1.0f, 1e-4f));
    pose = Copies::KinematicPose(two, 1300);  // 100 ms past it: on at 10 m/s
    CHECK(pose.source == Copies::ePoseSource::EXTRAPOLATED && NearF(pose.position.x, 2.0f, 1e-4f));
    pose = Copies::KinematicPose(two, 1450);  // the last extrapolated moment
    CHECK(pose.source == Copies::ePoseSource::EXTRAPOLATED && NearF(pose.position.x, 3.5f, 1e-4f));
    pose = Copies::KinematicPose(two, 1451);
    CHECK(pose.source == Copies::ePoseSource::HELD && NearF(pose.position.x, 3.5f, 1e-4f));
    pose = Copies::KinematicPose(two, 60000);  // held for good
    CHECK(pose.source == Copies::ePoseSource::HELD && NearF(pose.position.x, 3.5f, 1e-4f));
    CHECK(NearF(pose.position.z, 10.0f, 1e-5f));

    // across the 32-bit wrap of the server clock
    const SObjectRecord wrapped = TwoSamples(0xFFFFFFC0u, 0.0f, 0x00000024u, 1.0f);  // 100 ms apart
    pose = Copies::KinematicPose(wrapped, 0x00000024u + 50u);
    CHECK(pose.source == Copies::ePoseSource::INTERPOLATED && NearF(pose.position.x, 0.5f, 1e-4f));

    // rotation: halfway between 0 and 90 degrees about z is 45
    SObjectRecord turning = TwoSamples(1000, 0.0f, 1100, 0.0f);
    turning.samples[1].rotation = SRotation{};
    turning.samples[0].rotation = AxisAngle(0.0f, 0.0f, 1.0f, 1.5707963f);
    pose = Copies::KinematicPose(turning, 1150);
    CHECK(NearF(AngleBetweenDegrees(pose.rotation, AxisAngle(0.0f, 0.0f, 1.0f, 0.7853982f)), 0.0f, 0.05f));
    pose = Copies::KinematicPose(turning, 1300);  // extrapolated on to 180
    CHECK(NearF(AngleBetweenDegrees(pose.rotation, AxisAngle(0.0f, 0.0f, 1.0f, 3.1415927f)), 0.0f, 0.05f));
    // slerp takes the shorter arc (q and -q)
    SRotation negated = AxisAngle(0.0f, 0.0f, 1.0f, 0.2f);
    negated.x = -negated.x;
    negated.y = -negated.y;
    negated.z = -negated.z;
    negated.w = -negated.w;
    CHECK(NearF(AngleBetweenDegrees(Copies::Slerp(SRotation{}, negated, 0.5f), AxisAngle(0.0f, 0.0f, 1.0f, 0.1f)),
        0.0f, 0.05f));

    // one sample: at it, then on with its own speed (0.02 per 1/50 s = 1 m/s) for at most 250 ms
    SObjectRecord one;
    one.sampleCount = 1;
    one.samples[0] = CopySample(1000, 0.0f);
    one.samples[0].moveSpeed = Wire::SSpeed{0.02f, 0.0f, 0.0f};
    pose = Copies::KinematicPose(one, 1100);
    CHECK(pose.source == Copies::ePoseSource::SAMPLE && NearF(pose.position.x, 0.0f, 1e-5f));
    pose = Copies::KinematicPose(one, 1200);
    CHECK(pose.source == Copies::ePoseSource::EXTRAPOLATED && NearF(pose.position.x, 0.1f, 1e-4f));
    pose = Copies::KinematicPose(one, 5000);
    CHECK(pose.source == Copies::ePoseSource::HELD && NearF(pose.position.x, 0.25f, 1e-4f));
    // no samples: nothing
    SObjectRecord none;
    CHECK(Copies::KinematicPose(none, 1000).source == Copies::ePoseSource::NONE);
    // two samples too far apart give no velocity: the newest's own speed (none) holds it
    const SObjectRecord apart = TwoSamples(1000, 0.0f, 3000, 5.0f);
    pose = Copies::KinematicPose(apart, 3200);
    CHECK(NearF(pose.position.x, 5.0f, 1e-4f));

    // which samples drive: only those stamped after the last FULL
    SObjectRecord record = TwoSamples(1000, 0.0f, 1100, 1.0f);
    record.fullServerTime = 1050;
    CHECK(Copies::IsMotionActive(record));
    record.fullServerTime = 1100;  // the resting FULL came with the last sample's time
    CHECK(!Copies::IsMotionActive(record));
    record.fullServerTime = 1200;  // a late sample from before the FULL doesn't restart the motion
    CHECK(!Copies::IsMotionActive(record));
    record.fullServerTime = 0xFFFFFF00u;  // across the wrap
    CHECK(Copies::IsMotionActive(record));
    record.sampleCount = 0;
    CHECK(!Copies::IsMotionActive(record));

    // kinematic (placed by a script, or frozen) or dynamic (moved by physics)
    MirrorObjectsPolicy::SMotionSample still = CopySample(0, 0.0f);
    CHECK(Copies::IsKinematic(0, still));
    MirrorObjectsPolicy::SMotionSample falling = CopySample(0, 0.0f);
    falling.moveSpeed = Wire::SSpeed{0.0f, 0.0f, -0.1f};
    CHECK(!Copies::IsKinematic(0, falling));
    CHECK(Copies::IsKinematic(Wire::FLAG_DONT_APPLY_SPEED, falling));
    CHECK(Copies::IsKinematic(Wire::FLAG_DISABLE_COLLISION_FORCE, falling));
    MirrorObjectsPolicy::SMotionSample spinning = CopySample(0, 0.0f);
    spinning.turnSpeed = Wire::SSpeed{0.0f, 0.0f, 0.01f};
    CHECK(!Copies::IsKinematic(Wire::FLAG_STATIC, spinning));
    // a record: the newest sample decides, but a held or attached object's samples are always interpolated
    SObjectRecord moving;
    moving.sampleCount = 1;
    moving.samples[0] = falling;
    CHECK(!Copies::IsKinematic(moving));
    moving.state.parent.kind = Wire::PARENT_VEHICLE;  // crates on a caboose this game has no copy of
    CHECK(Copies::IsKinematic(moving));
    moving.state.parent.kind = Wire::PARENT_NONE;
    moving.state.holder.kind = Wire::HOLDER_PED;
    CHECK(Copies::IsKinematic(moving));

    // dynamic corrections: speeds only below 0.1 m, the sample's transform up to 2 m, a counted snap beyond
    CHECK(Copies::DecideCorrection(0.0f) == Copies::eCorrection::SPEEDS_ONLY);
    CHECK(Copies::DecideCorrection(0.099f) == Copies::eCorrection::SPEEDS_ONLY);
    CHECK(Copies::DecideCorrection(0.1f) == Copies::eCorrection::SET);
    CHECK(Copies::DecideCorrection(2.0f) == Copies::eCorrection::SET);
    CHECK(Copies::DecideCorrection(2.01f) == Copies::eCorrection::SNAP);
    CHECK(Copies::DecideCorrection(std::numeric_limits<float>::quiet_NaN()) == Copies::eCorrection::SNAP);
    CHECK(!Copies::IsSnap(2.0f) && Copies::IsSnap(2.5f));

    // at rest: 0.3 m or 10 degrees off for 1 s puts it back
    CHECK(!Copies::IsOffRest(0.3f, 10.0f));
    CHECK(Copies::IsOffRest(0.31f, 0.0f));
    CHECK(Copies::IsOffRest(0.0f, 10.5f));
    CHECK(Copies::IsOffRest(std::numeric_limits<float>::infinity(), 0.0f));
    Copies::CRestWatch watch;
    CHECK(!watch.Update(true, 5000));
    CHECK(!watch.Update(true, 5999));
    CHECK(watch.Update(true, 6000));   // put back once
    CHECK(!watch.Update(true, 6001));  // and the time starts over
    CHECK(!watch.Update(true, 6999));
    CHECK(watch.Update(true, 7001));
    CHECK(!watch.Update(true, 8000));
    CHECK(!watch.Update(false, 8500)); // back in range: over
    CHECK(!watch.Update(true, 8600));
    CHECK(!watch.Update(true, 9599));
    CHECK(watch.Update(true, 9600));
    watch.Reset();
    CHECK(!watch.Update(true, 0xFFFFFE00u));  // across the wrap
    CHECK(watch.Update(true, 0x000001E8u));
}

void CopiesLifetime()
{
    // retries: at once, then once a second, three per 30 s
    Copies::CRetryThrottle throttle;
    CHECK(!throttle.IsExhausted(0));
    CHECK(throttle.ShouldAttempt(100));
    CHECK(!throttle.ShouldAttempt(500));
    CHECK(!throttle.ShouldAttempt(1099));
    CHECK(throttle.ShouldAttempt(1100));
    CHECK(throttle.ShouldAttempt(2100));
    CHECK(throttle.IsExhausted(3100));
    CHECK(!throttle.ShouldAttempt(3100));
    CHECK(!throttle.ShouldAttempt(30099));
    CHECK(throttle.ShouldAttempt(30100));  // the first left the window
    throttle.Reset();
    CHECK(throttle.ShouldAttempt(30200));
    Copies::CRetryThrottle wrap;
    CHECK(wrap.ShouldAttempt(0xFFFFFF00u));
    CHECK(!wrap.ShouldAttempt(0x00000010u));
    CHECK(wrap.ShouldAttempt(0x00000300u));

    // the pool keeps room for this game's own world
    CHECK(!Copies::CanTakePoolSlot(0));
    CHECK(!Copies::CanTakePoolSlot(Copies::POOL_RESERVE));
    CHECK(Copies::CanTakePoolSlot(Copies::POOL_RESERVE + 1));

    // parents: attached once this game has the parent, its own transform until then
    Copies::SLinkInput in;
    CHECK(Copies::DecideLink(in) == Copies::eLink::WORLD);
    in.parentKind = Wire::PARENT_VEHICLE;
    CHECK(Copies::DecideLink(in) == Copies::eLink::WORLD);
    in.bParentResolved = true;
    CHECK(Copies::DecideLink(in) == Copies::eLink::ATTACH);
    // holders: a task when the ped is there with a free slot and retries left, else the hand
    in.holderKind = Wire::HOLDER_PED;
    CHECK(Copies::DecideLink(in) == Copies::eLink::WORLD);  // held beats attached; the holder isn't here
    in.bHolderResolved = true;
    in.bHoldSlotFree = true;
    in.bHoldRetriesLeft = true;
    in.bHoldBoneValid = true;
    CHECK(Copies::DecideLink(in) == Copies::eLink::HAND);  // an anim id outside its group here
    in.bHoldAnimValid = true;
    CHECK(Copies::DecideLink(in) == Copies::eLink::HOLD_TASK);
    in.bHoldSlotFree = false;
    CHECK(Copies::DecideLink(in) == Copies::eLink::HAND);
    in.bHoldSlotFree = true;
    in.bHoldRetriesLeft = false;
    CHECK(Copies::DecideLink(in) == Copies::eLink::HAND);
    in.bHoldRetriesLeft = true;
    in.bHoldBoneValid = false;
    CHECK(Copies::DecideLink(in) == Copies::eLink::HAND);
    in.bHoldBoneValid = true;
    in.holderKind = Wire::HOLDER_PLAYER;
    in.bHolderIsLocalPlayer = true;  // never a task on this game's own player
    CHECK(Copies::DecideLink(in) == Copies::eLink::HAND);
    CHECK(Copies::IsHoldBoneValid(0) && Copies::IsHoldBoneValid(18) && !Copies::IsHoldBoneValid(19));

    // hold animations: the host's id and group, none for a task made by anim names or out of range
    Wire::SHolder holder;
    holder.kind = Wire::HOLDER_PED;
    holder.animId = 5;
    holder.animGroup = 81;
    Copies::SHoldAnim anim = Copies::DecideHoldAnim(holder);
    CHECK(anim.animId == 5 && anim.animGroup == 81);
    holder.bAnimByName = true;
    anim = Copies::DecideHoldAnim(holder);
    CHECK(anim.animId == Copies::ANIM_ID_NONE && anim.animGroup == 0);
    holder.bAnimByName = false;
    holder.animGroup = Copies::ANIM_GROUP_COUNT;
    CHECK(Copies::DecideHoldAnim(holder).animId == Copies::ANIM_ID_NONE);
    holder.animGroup = 0;
    holder.animId = -1;
    CHECK(Copies::DecideHoldAnim(holder).animId == Copies::ANIM_ID_NONE);
    // an anim id must be one of its group's on this game: [first, first + count)
    CHECK(Copies::IsAnimInGroup(Copies::ANIM_ID_NONE, 0, 0));
    CHECK(Copies::IsAnimInGroup(400, 400, 3) && Copies::IsAnimInGroup(402, 400, 3));
    CHECK(!Copies::IsAnimInGroup(403, 400, 3) && !Copies::IsAnimInGroup(399, 400, 3));
    CHECK(!Copies::IsAnimInGroup(400, 400, 0));  // the group's associations aren't made
    CHECK(!Copies::IsAnimInGroup(Wire::ANIM_MAX, 0x7FFFFFF0, 0xFFFFFFFFu));
    // an unloaded group's block: requested at once, then at most once a second
    CHECK(Copies::ShouldRequestAnimBlock(false, 0, 5000));
    CHECK(!Copies::ShouldRequestAnimBlock(true, 5000, 5999));
    CHECK(Copies::ShouldRequestAnimBlock(true, 5000, 6000));
    CHECK(Copies::ShouldRequestAnimBlock(true, 0xFFFFFF00u, 0x00000400u));  // across the wrap

    // a FULL's model must be an object model: atomic, time, weapon, clump, LOD atomic
    CHECK(Copies::IsObjectModelType(1) && Copies::IsObjectModelType(3) && Copies::IsObjectModelType(4));
    CHECK(Copies::IsObjectModelType(5) && Copies::IsObjectModelType(8));
    CHECK(!Copies::IsObjectModelType(0) && !Copies::IsObjectModelType(2) && !Copies::IsObjectModelType(6));
    CHECK(!Copies::IsObjectModelType(7) && !Copies::IsObjectModelType(9));
}

int main()
{
    Ownership();
    ChangeDetection();
    Rotations();
    Motion();
    Filters();
    Records();
    WireFormat();
    CopiesMotion();
    CopiesLifetime();
    std::printf("mirror_objects_test: all checks passed\n");
    return 0;
}
