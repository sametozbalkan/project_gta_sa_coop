#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "config.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)

// Private build: the mission object mirror's wire format (docs/design/mission-objects.md section 7, package OB-1):
// MISSION_OBJECTS (network/packets/mirror_objects.h, SCRIPT channel, from the host) and MISSION_OBJECT_MOTION (SYNC
// channel, from an object's owner). Plain data without game types, so the server and the standalone policy test
// (tests/mirror_objects_test.cpp) compile it too. Every field is bounds-checked when read: a value out of range, a float
// that isn't finite or an unknown kind makes the whole packet fail to read; raw positions and speeds are clamped and
// rotations normalized.
//
// The field set is the one OB-3 (hand-over, owners and epochs) and OB-4 (persistent records by global index) need, so
// those packages change no wire: in OB-1 the owner is always the host (OWNER_HOST) and ID_PERSISTENT is never sent.
namespace MirrorObjectsWire
{
// the identity of an entry: a mission object by the host's object pool handle, or a persistent record by the index of
// the script global that holds it (OB-4)
constexpr uint8_t ID_MISSION = 0;
constexpr uint8_t ID_PERSISTENT = 1;
// An object pool handle is (slot << 8) | the slot's 7-bit id (CPool::GetRef, gta_sa.exe 1.0 US CPools::GetObjectRef
// 0x550020). 1.0 US has 350 object slots (CPools::Initialise, push 15Eh at 0x551096), so the largest handle is 0x15D7F;
// 17 bits leave room for 512 slots (as MirrorBlipsWire::OBJECT_ID_MAX). 0 is a valid handle.
constexpr int32_t MISSION_ID_MAX = 0x1FFFF;
// script globals are numbered in 16 bits (the variable operand of a script command)
constexpr int32_t PERSISTENT_ID_MAX = 0xFFFF;

constexpr uint8_t OP_FULL = 0;     // the object's whole state: new or changed
constexpr uint8_t OP_REMOVED = 1;  // the host's object is gone
constexpr uint8_t OP_OWNER = 2;    // only its owner and epoch changed (OB-3)

// SOwner::playerId: the host that sent the packet (whoever it is), else a player id
constexpr int OWNER_HOST = -1;

// SObjectFull::flags (the receivers rebuild the same object)
constexpr uint16_t FLAG_VISIBLE = 0x0001;                  // CEntity +0x1C bit 0x80 (m_bIsVisible)
constexpr uint16_t FLAG_COLLISION = 0x0002;                // CEntity +0x1C bit 0x01 (m_bUsesCollision)
constexpr uint16_t FLAG_STATIC = 0x0004;                   // CEntity +0x1C bit 0x04 (m_bIsStatic), information only
constexpr uint16_t FLAG_DISABLE_COLLISION_FORCE = 0x0008;  // CPhysical +0x40 bit 0x0004 (freeze_object 0550 sets both)
constexpr uint16_t FLAG_DONT_APPLY_SPEED = 0x0010;         // CPhysical +0x40 bit 0x2000
constexpr uint16_t FLAG_LIFTABLE = 0x0020;                 // CObject +0x140 bit 0x2000 (bIsLiftable, "stealable")
constexpr uint16_t FLAG_BROKEN = 0x0040;                   // CObject +0x140 bit 0x0400 (bIsBroken)
constexpr uint16_t FLAG_RENDER_DAMAGED = 0x0080;           // CEntity +0x1C bit 0x200 (m_bRenderDamaged)
constexpr uint16_t FLAGS_KNOWN = 0x00FF;

// the model table has 20000 entries in 1.0 (plugin-sdk CModelInfo.h, ms_modelInfoPtrs[20000])
constexpr int MODEL_COUNT = 20000;
// a scale (set_object_scale 08D2 writes CObject::m_fScale +0x15C) the receivers accept
constexpr float SCALE_MAX = 1000.0f;

// SParent::kind
constexpr uint8_t PARENT_NONE = 0;     // not attached, or attached to something without a network identity: world motion
constexpr uint8_t PARENT_VEHICLE = 1;  // CNetworkVehicle id (a train carriage too, once the train sync gives it one)
constexpr uint8_t PARENT_PED = 2;      // CNetworkPed id
constexpr uint8_t PARENT_PLAYER = 3;   // a player's ped, by player id
constexpr uint8_t PARENT_OBJECT = 4;   // another mirrored object, by the host's handle
constexpr uint8_t PARENT_LAST = PARENT_OBJECT;

// SHolder::kind
constexpr uint8_t HOLDER_NONE = 0;
constexpr uint8_t HOLDER_PED = 1;     // CNetworkPed id
constexpr uint8_t HOLDER_PLAYER = 2;  // a player's ped, by player id
constexpr uint8_t HOLDER_LAST = HOLDER_PLAYER;

// offsets (attach offset, hold offset) go as 16-bit fixed point at 1 mm
constexpr float OFFSET_LIMIT = 32.767f;
// anim ids and anim groups of a hold task (AnimationId, AssocGroupId; -1 = none)
constexpr int ANIM_MAX = 0x7FFF;

// entries per packet; a bigger set goes out in several packets, bReset only on the first
constexpr int MAX_ENTRIES = 32;
constexpr int MAX_MOTION_ENTRIES = 24;

// What a reader accepts of a raw float (the writer sends the compressed forms inside the map and its speeds): a position
// is clamped to +-POSITION_LIMIT on every axis (a sanity bound well past the map's +-3000 m), a speed
// component to +-SPEED_LIMIT engine units (per 1/50 s: 500 m/s), so a bad value can't throw a copy into the distance.
constexpr float POSITION_LIMIT = 20000.0f;
constexpr float SPEED_LIMIT = 10.0f;

inline bool IsFinite(float value)
{
    return std::isfinite(value);
}

inline float Clamp(float value, float limit)
{
    return value < -limit ? -limit : (value > limit ? limit : value);
}

inline int32_t IdMax(uint8_t idKind)
{
    return idKind == ID_PERSISTENT ? PERSISTENT_ID_MAX : MISSION_ID_MAX;
}

// a small unsigned field in [min, max], through an int so the bit packer's conversions stay explicit
template <typename Stream, typename T>
bool SerializeSmall(Stream& stream, T& field, int32_t min, int32_t max)
{
    int32_t value = static_cast<int32_t>(field);
    serialize_int(stream, value, min, max);
    if (Stream::IsReading)
    {
        field = static_cast<T>(value);
    }
    return true;
}

// a field written with exactly `bits` bits
template <typename Stream, typename T>
bool SerializeBitsOf(Stream& stream, T& field, int bits)
{
    uint32_t value = static_cast<uint32_t>(field);
    serialize_bits(stream, value, bits);
    if (Stream::IsReading)
    {
        field = static_cast<T>(value);
    }
    return true;
}

// a position, as WorldPositionCompressed (network/serializable_types.h) packs it: 1 mm inside the map's bounds, raw floats
// outside
struct SPosition
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && (!IsFinite(x) || !IsFinite(y) || !IsFinite(z)))
        {
            return false;
        }
        bool bCompressed = x >= -3000.0f && x <= 3000.0f && y >= -3000.0f && y <= 3000.0f && z >= -120.0f && z <= 1000.0f;
        serialize_bool(stream, bCompressed);
        if (bCompressed)
        {
            serialize_compressed_float(stream, x, -3000.0f, 3000.0f, 0.001f);
            serialize_compressed_float(stream, y, -3000.0f, 3000.0f, 0.001f);
            serialize_compressed_float(stream, z, -120.0f, 1000.0f, 0.001f);
        }
        else
        {
            serialize_float(stream, x);
            serialize_float(stream, y);
            serialize_float(stream, z);
        }
        if (Stream::IsReading)
        {
            if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
            {
                return false;
            }
            x = Clamp(x, POSITION_LIMIT);
            y = Clamp(y, POSITION_LIMIT);
            z = Clamp(z, POSITION_LIMIT);
        }
        return true;
    }
};

// a move or turn speed in the engine's unit (per 1/50 s), as MoveSpeedCompressed packs it: one bit for zero, 1 mm steps
// within +-1.5 (75 m/s), raw floats beyond
struct SSpeed
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && (!IsFinite(x) || !IsFinite(y) || !IsFinite(z)))
        {
            return false;
        }
        bool bZero = std::fabs(x) < 0.001f && std::fabs(y) < 0.001f && std::fabs(z) < 0.001f;
        serialize_bool(stream, bZero);
        if (bZero)
        {
            if (Stream::IsReading)
            {
                x = y = z = 0.0f;
            }
            return true;
        }
        bool bCompressed = x >= -1.5f && x <= 1.5f && y >= -1.5f && y <= 1.5f && z >= -1.5f && z <= 1.5f;
        serialize_bool(stream, bCompressed);
        if (bCompressed)
        {
            serialize_compressed_float(stream, x, -1.5f, 1.5f, 0.001f);
            serialize_compressed_float(stream, y, -1.5f, 1.5f, 0.001f);
            serialize_compressed_float(stream, z, -1.5f, 1.5f, 0.001f);
        }
        else
        {
            serialize_float(stream, x);
            serialize_float(stream, y);
            serialize_float(stream, z);
        }
        if (Stream::IsReading)
        {
            if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
            {
                return false;
            }
            x = Clamp(x, SPEED_LIMIT);
            y = Clamp(y, SPEED_LIMIT);
            z = Clamp(z, SPEED_LIMIT);
        }
        return true;
    }
};

// A rotation as a unit quaternion, "smallest three" in 32 bits: the index of the largest component (2 bits, sent
// positive) and the other three in 10 bits each over +-1/sqrt(2). The writer normalizes; the reader rebuilds the fourth.
struct SRotation
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        constexpr float LIMIT = 0.7072f;
        constexpr int32_t STEPS = 511;  // -511..511: 10 bits, 0 exact
        float q[4] = {x, y, z, w};
        int largest = 0;
        if (Stream::IsWriting)
        {
            if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z) || !IsFinite(w))
            {
                return false;
            }
            const float length = std::sqrt(x * x + y * y + z * z + w * w);
            if (length < 0.000001f)
            {
                q[0] = q[1] = q[2] = 0.0f;
                q[3] = 1.0f;
            }
            else
            {
                for (float& c : q)
                {
                    c /= length;
                }
            }
            for (int i = 1; i < 4; i++)
            {
                if (std::fabs(q[i]) > std::fabs(q[largest]))
                {
                    largest = i;
                }
            }
            if (q[largest] < 0.0f)
            {
                for (float& c : q)
                {
                    c = -c;
                }
            }
        }
        serialize_int(stream, largest, 0, 3);
        float sumSq = 0.0f;
        for (int i = 0; i < 4; i++)
        {
            if (i == largest)
            {
                continue;
            }
            int32_t step = 0;
            if (Stream::IsWriting)
            {
                const float scaled = q[i] / LIMIT * static_cast<float>(STEPS);
                step = static_cast<int32_t>(std::lround(scaled < -STEPS ? -STEPS : (scaled > STEPS ? STEPS : scaled)));
            }
            serialize_int(stream, step, -STEPS, STEPS);
            q[i] = static_cast<float>(step) / static_cast<float>(STEPS) * LIMIT;
            sumSq += q[i] * q[i];
        }
        if (Stream::IsReading)
        {
            // three components near the limit can add up past 1: the fourth is 0 then and the whole is normalized
            q[largest] = std::sqrt(sumSq < 1.0f ? 1.0f - sumSq : 0.0f);
            const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (!(length > 0.000001f) || !IsFinite(length))
            {
                return false;
            }
            x = q[0] / length;
            y = q[1] / length;
            z = q[2] / length;
            w = q[3] / length;
        }
        return true;
    }
};

// a 16-bit fixed-point offset at 1 mm (the writer clamps to +-OFFSET_LIMIT; the host sends no parent beyond it)
template <typename Stream>
bool SerializeOffset(Stream& stream, float& value)
{
    int32_t mm = 0;
    if (Stream::IsWriting)
    {
        if (!IsFinite(value))
        {
            return false;
        }
        const float clamped = value < -OFFSET_LIMIT ? -OFFSET_LIMIT : (value > OFFSET_LIMIT ? OFFSET_LIMIT : value);
        mm = static_cast<int32_t>(std::lround(clamped * 1000.0f));
    }
    serialize_int(stream, mm, -32767, 32767);
    if (Stream::IsReading)
    {
        value = static_cast<float>(mm) / 1000.0f;
    }
    return true;
}

inline bool IsValidNetworkId(uint8_t kind, bool bHolder, int32_t id)
{
    if (bHolder)
    {
        switch (kind)
        {
            case HOLDER_PED: return id >= 0 && id < Config::MAX_SERVER_PEDS;
            case HOLDER_PLAYER: return id >= 0 && id < Config::MAX_SERVER_PLAYERS;
            default: return false;
        }
    }
    switch (kind)
    {
        case PARENT_VEHICLE: return id >= 0 && id < Config::MAX_SERVER_VEHICLES;
        case PARENT_PED: return id >= 0 && id < Config::MAX_SERVER_PEDS;
        case PARENT_PLAYER: return id >= 0 && id < Config::MAX_SERVER_PLAYERS;
        case PARENT_OBJECT: return id >= 0 && id <= MISSION_ID_MAX;
        default: return false;
    }
}

template <typename Stream>
bool SerializeEntityId(Stream& stream, uint8_t kind, bool bHolder, int32_t& id)
{
    if (Stream::IsWriting && !IsValidNetworkId(kind, bHolder, id))
    {
        return false;
    }
    int32_t idMax = 0;
    if (bHolder)
    {
        idMax = kind == HOLDER_PED ? Config::MAX_SERVER_PEDS - 1 : Config::MAX_SERVER_PLAYERS - 1;
    }
    else
    {
        switch (kind)
        {
            case PARENT_VEHICLE: idMax = Config::MAX_SERVER_VEHICLES - 1; break;
            case PARENT_PED: idMax = Config::MAX_SERVER_PEDS - 1; break;
            case PARENT_PLAYER: idMax = Config::MAX_SERVER_PLAYERS - 1; break;
            default: idMax = MISSION_ID_MAX; break;
        }
    }
    serialize_int(stream, id, 0, idMax);
    return true;
}

// Attachment to a parent (CPhysical::m_pAttachedTo +0xFC). The attach commands store the offset at +0x100 and either
// Euler angles in radians at +0x10C (attach_object_to_car 0681 and the other Euler forms, AttachEntityToEntity 0x54D570,
// which zeroes the quaternion: xor ebx,ebx at 0x54D603, then +0x118..+0x124) or a quaternion at +0x118 (the quaternion
// form 0x54D690; relative to the fork frame on a forklift). The receiver repeats the same form.
struct SParent
{
    uint8_t kind = PARENT_NONE;
    int32_t id = 0;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float offsetZ = 0.0f;
    bool bQuaternion = false;  // the quaternion form: rotation; else the Euler form: euler*
    SRotation rotation{};
    float eulerX = 0.0f;  // radians, -pi..pi
    float eulerY = 0.0f;
    float eulerZ = 0.0f;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!SerializeSmall(stream, kind, PARENT_NONE, PARENT_LAST))
        {
            return false;
        }
        if (kind == PARENT_NONE)
        {
            if (Stream::IsReading)
            {
                *this = SParent{};
            }
            return true;
        }
        if (!SerializeEntityId(stream, kind, false, id) || !SerializeOffset(stream, offsetX) ||
            !SerializeOffset(stream, offsetY) || !SerializeOffset(stream, offsetZ))
        {
            return false;
        }
        serialize_bool(stream, bQuaternion);
        if (bQuaternion)
        {
            serialize_object(stream, rotation);
            return true;
        }
        constexpr float PI_F = 3.14159265f;
        for (float* angle : {&eulerX, &eulerY, &eulerZ})
        {
            float value = *angle;
            if (Stream::IsWriting)
            {
                if (!IsFinite(value))
                {
                    return false;
                }
                value = std::remainder(value, 2.0f * PI_F);
            }
            serialize_compressed_float(stream, value, -PI_F, PI_F, 0.0001f);
            if (Stream::IsReading)
            {
                *angle = value;
            }
        }
        return true;
    }
};

// The ped holding the object: the task CPedIntelligence::GetTaskHold (0x600FF0) returns, a CTaskSimpleHoldEntity (or its
// pick-up / put-down subclasses; constructor 0x6913A0, bytes): held entity +0x08, offset +0x0C, bone +0x18, bone flags
// +0x19, anim id +0x20, anim group +0x24, m_pAnimBlendHierarchy +0x30 (set by the anim-name constructor 0x691470: the
// anim id and group mean nothing then), m_bDisallowDroppingOnAnimEnd +0x36.
struct SHolder
{
    uint8_t kind = HOLDER_NONE;
    int32_t id = 0;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float offsetZ = 0.0f;
    uint8_t bone = 0;
    uint8_t boneFlags = 0;
    int32_t animId = -1;
    int32_t animGroup = -1;
    bool bAnimByName = false;
    bool bDisallowDrop = false;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!SerializeSmall(stream, kind, HOLDER_NONE, HOLDER_LAST))
        {
            return false;
        }
        if (kind == HOLDER_NONE)
        {
            if (Stream::IsReading)
            {
                *this = SHolder{};
            }
            return true;
        }
        if (!SerializeEntityId(stream, kind, true, id) || !SerializeOffset(stream, offsetX) ||
            !SerializeOffset(stream, offsetY) || !SerializeOffset(stream, offsetZ))
        {
            return false;
        }
        if (!SerializeBitsOf(stream, bone, 8) || !SerializeBitsOf(stream, boneFlags, 8))
        {
            return false;
        }
        serialize_int(stream, animId, -1, ANIM_MAX);
        serialize_int(stream, animGroup, -1, ANIM_MAX);
        serialize_bool(stream, bAnimByName);
        serialize_bool(stream, bDisallowDrop);
        return true;
    }
};

// whose motion the receivers accept for an object: the host's (OWNER_HOST) or a player's it was handed to (OB-3); the
// epoch changes with every hand-over, so a motion sample of an older owner is dropped
struct SOwner
{
    int32_t playerId = OWNER_HOST;
    uint8_t epoch = 0;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, playerId, OWNER_HOST, Config::MAX_SERVER_PLAYERS - 1);
        return SerializeBitsOf(stream, epoch, 8);
    }
};

// the whole state of one object (OP_FULL)
struct SObjectFull
{
    int32_t model = 0;           // CEntity::m_nModelIndex +0x22
    uint8_t area = 0;            // CEntity::m_nAreaCode +0x2F
    uint16_t flags = 0;          // FLAG_*
    bool bScaled = false;        // m_fScale +0x15C is not 1
    float scale = 1.0f;
    SPosition position{};        // the matrix
    SRotation rotation{};
    SSpeed moveSpeed{};          // CPhysical +0x44, per 1/50 s
    SSpeed turnSpeed{};          // CPhysical +0x50
    SOwner owner{};
    SParent parent{};
    SHolder holder{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, model, 0, MODEL_COUNT - 1);
        if (!SerializeBitsOf(stream, area, 8))
        {
            return false;
        }
        if (Stream::IsWriting && (flags & ~FLAGS_KNOWN) != 0)
        {
            return false;
        }
        if (!SerializeBitsOf(stream, flags, 16))
        {
            return false;
        }
        if (Stream::IsReading && (flags & ~FLAGS_KNOWN) != 0)
        {
            return false;
        }
        serialize_bool(stream, bScaled);
        if (bScaled)
        {
            if (Stream::IsWriting && !(IsFinite(scale) && scale > 0.0f && scale <= SCALE_MAX))
            {
                return false;
            }
            serialize_float(stream, scale);
            if (Stream::IsReading && !(IsFinite(scale) && scale > 0.0f && scale <= SCALE_MAX))
            {
                return false;
            }
        }
        else if (Stream::IsReading)
        {
            scale = 1.0f;
        }
        serialize_object(stream, position);
        serialize_object(stream, rotation);
        serialize_object(stream, moveSpeed);
        serialize_object(stream, turnSpeed);
        serialize_object(stream, owner);
        serialize_object(stream, parent);
        serialize_object(stream, holder);
        return true;
    }
};

inline bool IsValidId(uint8_t idKind, int32_t id)
{
    return (idKind == ID_MISSION || idKind == ID_PERSISTENT) && id >= 0 && id <= IdMax(idKind);
}

template <typename Stream>
bool SerializeObjectId(Stream& stream, uint8_t& idKind, int32_t& id)
{
    if (Stream::IsWriting && !IsValidId(idKind, id))
    {
        return false;
    }
    if (!SerializeSmall(stream, idKind, ID_MISSION, ID_PERSISTENT))
    {
        return false;
    }
    serialize_int(stream, id, 0, IdMax(idKind));
    return true;
}

// one entry of MISSION_OBJECTS
struct SObjectEntry
{
    uint8_t idKind = ID_MISSION;
    int32_t id = 0;
    uint8_t op = OP_FULL;
    SObjectFull full{};  // OP_FULL
    SOwner owner{};      // OP_OWNER

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!SerializeObjectId(stream, idKind, id))
        {
            return false;
        }
        if (!SerializeSmall(stream, op, OP_FULL, OP_OWNER))
        {
            return false;
        }
        if (op == OP_FULL)
        {
            serialize_object(stream, full);
        }
        else if (op == OP_OWNER)
        {
            serialize_object(stream, owner);
        }
        return true;
    }
};

// one entry of MISSION_OBJECT_MOTION
struct SMotionEntry
{
    uint8_t idKind = ID_MISSION;
    int32_t id = 0;
    uint8_t epoch = 0;
    SPosition position{};
    SRotation rotation{};
    SSpeed moveSpeed{};
    SSpeed turnSpeed{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!SerializeObjectId(stream, idKind, id))
        {
            return false;
        }
        if (!SerializeBitsOf(stream, epoch, 8))
        {
            return false;
        }
        serialize_object(stream, position);
        serialize_object(stream, rotation);
        serialize_object(stream, moveSpeed);
        serialize_object(stream, turnSpeed);
        return true;
    }
};
}  // namespace MirrorObjectsWire

// NOLINTEND(readability-simplify-boolean-expr)
