#pragma once

#include <cstdio>
#include <string>

#include "config.h"
#include "network/mirror_hud_state.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)

// Private build: the mission mirror layer's timers and counters (client/src/CMirrorHud.h; docs/design/mirror-layer.md
// 3.7 and 3.8, package M4). The host sends the on-screen timers and counters its mission added, in full, when they change
// and when a player joins; the server relays the packet from the host only and fills in the sender, so the other games
// can drop what a former host sent before it left. SCRIPT channel: reliable and in order with OPCODE_SYNC.
namespace Packets::Mirror
{
class MirrorHud : public Packet
{
    DEFINE_PACKET_TYPE(MirrorHud, ePacketType::MISSION_MIRROR_HUD, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};      // the host, filled in by the server
    MirrorHudWire::SState state{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_object(stream, state);
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[96];
        snprintf(text, sizeof(text), "clock %d/%u %d, counters %d%d%d%d, paused %d", state.clock.bOn,
            state.clock.generation, state.clock.value, state.counters[0].bOn, state.counters[1].bOn,
            state.counters[2].bOn, state.counters[3].bOn, state.bPaused);
        return text;
    }
};
}  // namespace Packets::Mirror

// NOLINTEND(readability-simplify-boolean-expr)
