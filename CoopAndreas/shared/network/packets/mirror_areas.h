#pragma once

#include "config.h"
#include "network/mirror_areas.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// Private build: the mission mirror layer's areas (client/src/CMirrorAreas.h; docs/design/mirror-layer.md 3.6 and 3.8).
namespace Packets::Mirror
{
// host -> server -> every other player: the whole set of areas the host's mission draws now (the locate spheres and
// the draw_sphere markers of its mission family), replacing the set sent before; an empty set clears it. The host sends
// it when it changes (at most 10 times a second), at once when its mission ends, and when a player joins. The server
// relays it from the host only and fills in the sender, so the other games can drop what a former host sent before it
// left.
class MirrorAreas : public Packet
{
    DEFINE_PACKET_TYPE(MirrorAreas, ePacketType::MISSION_MIRROR_AREAS, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};  // the host, filled in by the server
    int count = 0;              // areas used, 0..MAX_AREAS
    MirrorAreasWire::SArea areas[MirrorAreasWire::MAX_AREAS]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        // the bit writer only asserts its bounds: a count out of range is refused before anything is written
        if (Stream::IsWriting && (count < 0 || count > MirrorAreasWire::MAX_AREAS))
        {
            return false;
        }
        serialize_object(stream, playerid);
        serialize_int(stream, count, 0, MirrorAreasWire::MAX_AREAS);
        for (int i = 0; i < count; i++)
        {
            MirrorAreasWire::SArea& area = areas[i];
            serialize_uint32(stream, area.id);
            serialize_int(stream, area.kind, 0, MirrorAreasWire::KIND_FIELD_MAX);
            serialize_int(stream, area.areaCode, 0, UINT8_MAX);
            serialize_float(stream, area.x);
            serialize_float(stream, area.y);

            // the ground under the centre travels as a flag: the receiver's game looks for its own ground
            bool bGroundZ = false;
            if (Stream::IsWriting)
            {
                bGroundZ = MirrorAreasWire::IsGroundZ(area.z);
            }
            serialize_bool(stream, bGroundZ);
            if (bGroundZ)
            {
                if (Stream::IsReading)
                {
                    area.z = MirrorAreasWire::GROUND_Z;
                }
            }
            else
            {
                serialize_float(stream, area.z);
            }

            serialize_float(stream, area.sizeX);
            if (area.kind == MirrorAreasWire::KIND_AREA)
            {
                serialize_float(stream, area.sizeY);
            }
            else if (Stream::IsReading)
            {
                area.sizeY = 0.0f;
            }

            // the receivers pass these values to the game
            if (Stream::IsReading && !MirrorAreasWire::IsValidArea(area))
            {
                return false;
            }
        }
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[24];
        snprintf(text, sizeof(text), "%d areas", count);
        return text;
    }
};
}  // namespace Packets::Mirror
