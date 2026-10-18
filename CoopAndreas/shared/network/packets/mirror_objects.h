#pragma once

#include <cstdio>
#include <string>

#include "config.h"
#include "network/mirror_objects.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)

// Private build: the mission object mirror (client/src/CMirrorObjects.h; docs/design/mission-objects.md sections 5 and
// 7, package OB-1). The wire format of the entries is in network/mirror_objects.h.
namespace Packets::Mirror
{
// The host's mission objects: creation at once, every change of state at the end of the script frame, a resting
// transform, removals; the whole set with bReset to a player who joins (that player alone) and after the host's own game
// restarted (to everyone). SCRIPT channel: reliable and in order with OPCODE_SYNC and the other mirror packets. The
// server relays it from the host only and fills in the sender, so the other games can drop what a former host sent.
class MirrorObjects : public Packet
{
    DEFINE_PACKET_TYPE(MirrorObjects, ePacketType::MISSION_OBJECTS, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};  // the host, filled in by the server
    int targetPlayerId = -1;    // -1 = every other player, else that player alone (a join snapshot)
    bool bReset = false;        // drop every record of the host before applying the entries
    int count = 0;
    MirrorObjectsWire::SObjectEntry entries[MirrorObjectsWire::MAX_ENTRIES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        // the bit writer only asserts its buffer bound: a count out of range is refused before anything is written
        if (Stream::IsWriting && (count < 0 || count > MirrorObjectsWire::MAX_ENTRIES))
        {
            return false;
        }
        serialize_object(stream, playerid);
        serialize_int(stream, targetPlayerId, -1, Config::MAX_SERVER_PLAYERS - 1);
        serialize_bool(stream, bReset);
        serialize_int(stream, count, 0, MirrorObjectsWire::MAX_ENTRIES);
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

// Motion samples of moving objects from their owner (the host in OB-1), about 10 times a second each (20 above 5 m/s),
// none at rest, 5 a second for held or attached ones. SYNC channel: unreliable, stamped with the sender's server clock like the other sync
// packets, so the receivers order and interpolate the samples. The server relays it from anyone to everyone else with
// the sender filled in; the receivers keep only what the object's owner sent with the object's current epoch.
class MirrorObjectMotion : public Packet
{
    DEFINE_PACKET_TYPE(MirrorObjectMotion, ePacketType::MISSION_OBJECT_MOTION, ePacketChannel::SYNC);

public:
    SenderPlayerId playerid{};  // the sender, filled in by the server
    int count = 0;
    MirrorObjectsWire::SMotionEntry entries[MirrorObjectsWire::MAX_MOTION_ENTRIES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && (count < 0 || count > MirrorObjectsWire::MAX_MOTION_ENTRIES))
        {
            return false;
        }
        serialize_object(stream, playerid);
        serialize_int(stream, count, 0, MirrorObjectsWire::MAX_MOTION_ENTRIES);
        for (int i = 0; i < count; i++)
        {
            serialize_object(stream, entries[i]);
        }
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[32];
        snprintf(text, sizeof(text), "%d samples", count);
        return text;
    }
};
}  // namespace Packets::Mirror

// NOLINTEND(readability-simplify-boolean-expr)
