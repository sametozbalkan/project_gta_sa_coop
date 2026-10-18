#pragma once

#include "config.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)

// Private build: train mirror packets (client/src/CTrainSync.h; docs/design/trains.md §5, §7, packages T1b, T4, T5).
namespace Packets::Train
{
constexpr int MAX_TRAINS = 32;
constexpr int MAX_TRAIN_CARRIAGES = 8;
constexpr int MAX_SNAPSHOT_TRAINS = 8;

constexpr uint8_t STATE_FLAG_CLOCKWISE = 1 << 0;
constexpr uint8_t STATE_FLAG_DERAILED = 1 << 1;
constexpr uint8_t STATE_FLAG_HAS_NPC_DRIVER = 1 << 2;
constexpr uint8_t STATE_FLAG_STOPPED_AT_STATION = 1 << 3;

enum class eTrainEventKind : uint8_t
{
    SPAWN = 0,
    REMOVE = 1,
    CARRIAGES = 2,
    SNAPSHOT = 3,
    REQUEST = 4,
    KIND_MAX = 4
};

enum class eTrainRequestKind : uint8_t
{
    SKIP_STATION = 0,
    REQUEST_MAX = 0
};

struct STrainStatePayload
{
    uint8_t sequence = 0;
    uint8_t flags = 0;
    uint8_t jumpCounter = 0;
    uint8_t trackId = 0;
    float railDistance = 0.0f;
    int16_t speedQuantized = 0;  // m_fTrainSpeed * 10000.0f clamped to [-32767, 32767]
    uint8_t gasHalf = 0;         // m_fTrainGas * 0.5f clamped to [0, 255]
    uint8_t brakeHalf = 0;       // m_fTrainBrake * 0.5f clamped to [0, 255]

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, sequence);
        serialize_uint8(stream, flags);
        serialize_uint8(stream, jumpCounter);
        serialize_int(stream, trackId, 0, 3);
        serialize_float(stream, railDistance);
        serialize_int(stream, speedQuantized, -32767, 32767);
        serialize_uint8(stream, gasHalf);
        serialize_uint8(stream, brakeHalf);
        return true;
    }
};

struct STrainCarriageEntry
{
    int vehicleid = 0;
    uint16_t modelid = 537;
    uint8_t doorLock = 0;
    uint16_t health = 1000;
    bool bWrecked = false;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, vehicleid, 0, Config::MAX_SERVER_VEHICLES - 1);
        serialize_uint16(stream, modelid);
        serialize_int(stream, doorLock, 0, 8);
        serialize_uint16(stream, health);
        serialize_bool(stream, bWrecked);
        return true;
    }
};

struct STrainSpawnPayload
{
    uint8_t trainId = 0;
    uint32_t incarnation = 0;
    uint8_t trackId = 0;
    uint8_t configIndex = 0;
    uint8_t configurationOffset = 0;
    bool bClockwise = false;
    uint16_t nodeIndex = 0;
    uint8_t carriageCount = 1;
    STrainCarriageEntry carriages[MAX_TRAIN_CARRIAGES]{};
    uint32_t layoutHash = 0;
    STrainStatePayload initialState{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && (carriageCount < 1 || carriageCount > MAX_TRAIN_CARRIAGES))
        {
            return false;
        }
        serialize_int(stream, trainId, 0, MAX_TRAINS - 1);
        serialize_uint32(stream, incarnation);
        serialize_int(stream, trackId, 0, 3);
        serialize_int(stream, configIndex, 0, 15);
        serialize_int(stream, configurationOffset, 0, 15);
        serialize_bool(stream, bClockwise);
        serialize_uint16(stream, nodeIndex);
        serialize_int(stream, carriageCount, 1, MAX_TRAIN_CARRIAGES);
        for (int i = 0; i < carriageCount; i++)
        {
            serialize_object(stream, carriages[i]);
        }
        serialize_uint32(stream, layoutHash);
        serialize_object(stream, initialState);
        return true;
    }
};

struct STrainCarriageStateEntry
{
    uint8_t doorLock = 0;
    uint16_t health = 1000;
    bool bWrecked = false;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, doorLock, 0, 8);
        serialize_uint16(stream, health);
        serialize_bool(stream, bWrecked);
        return true;
    }
};

class TrainState : public Packet
{
    DEFINE_PACKET_TYPE(TrainState, ePacketType::TRAIN_STATE, ePacketChannel::SYNC);

public:
    SenderPlayerId playerid{};
    uint8_t trainId = 0;
    uint32_t incarnation = 0;
    STrainStatePayload state{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_int(stream, trainId, 0, MAX_TRAINS - 1);
        serialize_uint32(stream, incarnation);
        serialize_object(stream, state);
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[64];
        snprintf(text, sizeof(text), "train %u seq %u track %u dist %.1f", trainId, state.sequence, state.trackId,
            state.railDistance);
        return text;
    }
};

class TrainEvent : public Packet
{
    DEFINE_PACKET_TYPE(TrainEvent, ePacketType::TRAIN_EVENT, ePacketChannel::EVENT);

public:
    SenderPlayerId playerid{};
    int targetPlayerId = -1;
    eTrainEventKind kind = eTrainEventKind::SPAWN;

    // SPAWN
    STrainSpawnPayload spawn{};

    // REMOVE / CARRIAGES / REQUEST; SNAPSHOT uses incarnation as the restart fence.
    uint8_t trainId = 0;
    uint32_t incarnation = 0;

    // CARRIAGES
    uint8_t carriageCount = 1;
    STrainCarriageStateEntry carriageStates[MAX_TRAIN_CARRIAGES]{};

    // SNAPSHOT
    bool bReset = false;
    uint8_t snapshotCount = 0;
    STrainSpawnPayload snapshotTrains[MAX_SNAPSHOT_TRAINS]{};

    // REQUEST
    eTrainRequestKind requestKind = eTrainRequestKind::SKIP_STATION;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_int(stream, targetPlayerId, -1, Config::MAX_SERVER_PLAYERS - 1);
        uint8_t kindRaw = static_cast<uint8_t>(kind);
        serialize_int(stream, kindRaw, 0, static_cast<int>(eTrainEventKind::KIND_MAX));
        if (Stream::IsReading)
        {
            kind = static_cast<eTrainEventKind>(kindRaw);
        }

        switch (kind)
        {
            case eTrainEventKind::SPAWN:
                if (!spawn.Serialize(stream))
                {
                    return false;
                }
                break;

            case eTrainEventKind::REMOVE:
                serialize_int(stream, trainId, 0, MAX_TRAINS - 1);
                serialize_uint32(stream, incarnation);
                break;

            case eTrainEventKind::CARRIAGES:
                if (Stream::IsWriting && (carriageCount < 1 || carriageCount > MAX_TRAIN_CARRIAGES))
                {
                    return false;
                }
                serialize_int(stream, trainId, 0, MAX_TRAINS - 1);
                serialize_uint32(stream, incarnation);
                serialize_int(stream, carriageCount, 1, MAX_TRAIN_CARRIAGES);
                for (int i = 0; i < carriageCount; i++)
                {
                    serialize_object(stream, carriageStates[i]);
                }
                break;

            case eTrainEventKind::SNAPSHOT:
                if (Stream::IsWriting && snapshotCount > MAX_SNAPSHOT_TRAINS)
                {
                    return false;
                }
                serialize_uint32(stream, incarnation);
                serialize_bool(stream, bReset);
                serialize_int(stream, snapshotCount, 0, MAX_SNAPSHOT_TRAINS);
                for (int i = 0; i < snapshotCount; i++)
                {
                    if (!snapshotTrains[i].Serialize(stream))
                    {
                        return false;
                    }
                }
                break;

            case eTrainEventKind::REQUEST:
            {
                serialize_int(stream, trainId, 0, MAX_TRAINS - 1);
                serialize_uint32(stream, incarnation);
                uint8_t reqRaw = static_cast<uint8_t>(requestKind);
                // A single-value integer range is invalid for serialize_int. Keep an explicit
                // discriminator on the wire and reject unknown requests in every stream mode.
                if (Stream::IsWriting && requestKind != eTrainRequestKind::SKIP_STATION)
                {
                    return false;
                }
                serialize_uint8(stream, reqRaw);
                if (reqRaw != static_cast<uint8_t>(eTrainRequestKind::SKIP_STATION))
                {
                    return false;
                }
                if (Stream::IsReading)
                {
                    requestKind = static_cast<eTrainRequestKind>(reqRaw);
                }
                break;
            }
        }
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[64];
        snprintf(text, sizeof(text), "kind %u train %u target %d", static_cast<unsigned>(kind), trainId,
            targetPlayerId);
        return text;
    }
};

enum class eAttachEntityKind : uint8_t
{
    PLAYER = 0,
    PED = 1,
    OBJECT = 2,
    VEHICLE = 3,
    KIND_MAX = 3
};

class EntityAttach : public Packet
{
    DEFINE_PACKET_TYPE(EntityAttach, ePacketType::ENTITY_ATTACH, ePacketChannel::EVENT);

public:
    SenderPlayerId playerid{};
    int targetPlayerId = -1;
    eAttachEntityKind entityKind = eAttachEntityKind::PED;
    int entityId = 0;
    bool bAttached = false;
    int targetVehicleId = 0;
    CVector vecOffset{};

    // Ped attachment fields (PLAYER / PED)
    uint8_t nTurretPosition = 0;
    float fAngleLimit = 0.0f;
    uint8_t nWeaponType = 0;

    // Physical attachment fields (OBJECT / VEHICLE)
    CVector vecRotation{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_int(stream, targetPlayerId, -1, Config::MAX_SERVER_PLAYERS - 1);
        uint8_t kindRaw = static_cast<uint8_t>(entityKind);
        serialize_int(stream, kindRaw, 0, static_cast<int>(eAttachEntityKind::KIND_MAX));
        if (Stream::IsReading)
        {
            entityKind = static_cast<eAttachEntityKind>(kindRaw);
        }

        int maxEntityId = Config::MAX_SERVER_VEHICLES - 1;
        switch (entityKind)
        {
            case eAttachEntityKind::PLAYER:
                maxEntityId = Config::MAX_SERVER_PLAYERS - 1;
                break;
            case eAttachEntityKind::PED:
                maxEntityId = Config::MAX_SERVER_PEDS - 1;
                break;
            case eAttachEntityKind::OBJECT:
            case eAttachEntityKind::VEHICLE:
            default:
                maxEntityId = Config::MAX_SERVER_VEHICLES - 1;
                break;
        }
        if (Stream::IsWriting && (entityId < 0 || entityId > maxEntityId))
        {
            return false;
        }
        serialize_int(stream, entityId, 0, maxEntityId);
        serialize_bool(stream, bAttached);

        if (bAttached)
        {
            serialize_int(stream, targetVehicleId, 0, Config::MAX_SERVER_VEHICLES - 1);
            serialize_vector(stream, vecOffset);
            if (entityKind == eAttachEntityKind::PLAYER || entityKind == eAttachEntityKind::PED)
            {
                serialize_int(stream, nTurretPosition, 0, 3);
                serialize_float(stream, fAngleLimit);
                serialize_uint8(stream, nWeaponType);
            }
            else
            {
                serialize_vector(stream, vecRotation);
            }
        }
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[80];
        snprintf(text, sizeof(text), "kind %u id %d attached %d veh %d", static_cast<unsigned>(entityKind), entityId,
            bAttached ? 1 : 0, targetVehicleId);
        return text;
    }
};
}  // namespace Packets::Train

// NOLINTEND(readability-simplify-boolean-expr)
