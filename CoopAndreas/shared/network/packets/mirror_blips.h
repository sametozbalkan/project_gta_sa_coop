#pragma once

#include "config.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)

// Private build: the mission mirror layer's blips (client/src/CMirrorBlips.h; docs/design/mirror-layer.md 3.8,
// package M2). The host sends the radar blips its mission created, as long as it keeps them: what changed once per
// frame, and its whole set with bReset to a player who joins (that player alone) and after its own game restarted (to
// everyone). The server relays it from the host only and fills in the sender, so the other games can drop what a
// former host sent before it left.
namespace MirrorBlipsWire
{
constexpr uint8_t OP_FULL = 0;      // the blip's whole state: new or changed
constexpr uint8_t OP_REMOVED = 1;   // the host's blip is gone
constexpr uint8_t OP_POSITION = 2;  // only its position, for a blip the receivers may show at the host's position

constexpr uint8_t ENTITY_NONE = 0;     // a coordinate, or an entity without a network id
constexpr uint8_t ENTITY_PLAYER = 1;   // a player's ped, by player id
constexpr uint8_t ENTITY_PED = 2;      // CNetworkPed id
constexpr uint8_t ENTITY_VEHICLE = 3;  // CNetworkVehicle id

constexpr uint8_t TYPE_FIRST = 1;  // eBlipType BLIP_CAR
constexpr uint8_t TYPE_LAST = 8;   // eBlipType BLIP_AIRSTRIP

// entries per packet (at most about 40 bytes each); a bigger set goes out in several packets, bReset only on the first
constexpr int MAX_ENTRIES = 48;
}  // namespace MirrorBlipsWire

namespace Packets::Mirror
{
struct SMirrorBlipEntry
{
    uint32_t hostHandle = 0;  // the host's radar handle, the blip's identity
    uint8_t op = MirrorBlipsWire::OP_FULL;
    WorldPositionCompressed position{};  // FULL and POSITION

    // FULL only
    uint8_t type = MirrorBlipsWire::TYPE_FIRST;  // eBlipType
    uint8_t entityKind = MirrorBlipsWire::ENTITY_NONE;
    int entityId = 0;  // with an entity kind
    uint32_t colour = 0;
    uint8_t sprite = 0;   // eRadarSprite
    uint8_t display = 0;  // eBlipDisplay
    uint8_t size = 1;
    bool bBright = true;
    bool bShortRange = false;
    bool bFriendly = false;
    bool bRemain = false;
    bool bFade = false;
    uint8_t coordAppearance = 0;
    bool bEntrance = false;                // coordinate types: shown at an interior's entrance
    WorldPositionCompressed entrance{};    // the centre of that entrance

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint32(stream, hostHandle);
        serialize_int(stream, op, MirrorBlipsWire::OP_FULL, MirrorBlipsWire::OP_POSITION);
        if (op == MirrorBlipsWire::OP_REMOVED)
        {
            return true;
        }
        serialize_object(stream, position);
        if (op == MirrorBlipsWire::OP_POSITION)
        {
            return true;
        }

        serialize_int(stream, type, MirrorBlipsWire::TYPE_FIRST, MirrorBlipsWire::TYPE_LAST);
        serialize_int(stream, entityKind, MirrorBlipsWire::ENTITY_NONE, MirrorBlipsWire::ENTITY_VEHICLE);
        switch (entityKind)
        {
            case MirrorBlipsWire::ENTITY_PLAYER:
                serialize_int(stream, entityId, 0, Config::MAX_SERVER_PLAYERS - 1);
                break;
            case MirrorBlipsWire::ENTITY_PED:
                serialize_int(stream, entityId, 0, Config::MAX_SERVER_PEDS - 1);
                break;
            case MirrorBlipsWire::ENTITY_VEHICLE:
                serialize_int(stream, entityId, 0, Config::MAX_SERVER_VEHICLES - 1);
                break;
            default:
                break;
        }
        serialize_uint32(stream, colour);
        serialize_int(stream, sprite, 0, 63);
        serialize_int(stream, display, 0, 3);
        serialize_int(stream, size, 0, 255);
        serialize_bool(stream, bBright);
        serialize_bool(stream, bShortRange);
        serialize_bool(stream, bFriendly);
        serialize_bool(stream, bRemain);
        serialize_bool(stream, bFade);
        serialize_int(stream, coordAppearance, 0, 3);
        serialize_bool(stream, bEntrance);
        if (bEntrance)
        {
            serialize_object(stream, entrance);
        }
        return true;
    }
};

class MirrorBlips : public Packet
{
    DEFINE_PACKET_TYPE(MirrorBlips, ePacketType::MISSION_MIRROR_BLIPS, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};  // the host, filled in by the server
    int targetPlayerId = -1;    // -1 = every other player, else that player alone (a join snapshot)
    bool bReset = false;        // drop every mirrored blip before applying the entries
    int count = 0;
    SMirrorBlipEntry entries[MirrorBlipsWire::MAX_ENTRIES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        // the bit writer only asserts its buffer bound: a count out of range is refused before anything is written
        if (Stream::IsWriting && (count < 0 || count > MirrorBlipsWire::MAX_ENTRIES))
        {
            return false;
        }
        serialize_object(stream, playerid);
        serialize_int(stream, targetPlayerId, -1, Config::MAX_SERVER_PLAYERS - 1);
        serialize_bool(stream, bReset);
        serialize_int(stream, count, 0, MirrorBlipsWire::MAX_ENTRIES);
        for (int i = 0; i < count; i++)
        {
            serialize_object(stream, entries[i]);
        }
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[48];
        snprintf(text, sizeof(text), "reset %d, target %d, %d entries", bReset ? 1 : 0, targetPlayerId, count);
        return text;
    }
};
}  // namespace Packets::Mirror

// NOLINTEND(readability-simplify-boolean-expr)
