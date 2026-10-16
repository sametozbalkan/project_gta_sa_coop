#pragma once

#include "config.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/player_state.h"
#include "network/serializable_types.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)

// Private build: player state for co-op saves (client/src/CPlayerStateSave.h). The server sends a snapshot only from a
// player who isn't the host to the host, and a restore only from the host to the player it names.
namespace Packets::PlayerState
{
// a player who isn't the host -> server -> host: the player's current state, sent when it changes
class PlayerStateSnapshot : public Packet
{
    DEFINE_PACKET_TYPE(PlayerStateSnapshot, ePacketType::PLAYER_STATE_SNAPSHOT, ePacketChannel::EVENT);

public:
    SenderPlayerId playerid{};
    uint32_t connectionNonce = 0;  // new for every connection and every restart of the sender's game, never 0
    uint32_t ackRestoreId = 0;     // the last restore the sender applied, 0 = none
    SPlayerState state{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_uint32(stream, connectionNonce);
        serialize_uint32(stream, ackRestoreId);
        serialize_object(stream, state);

        if (Stream::IsReading && connectionNonce == 0)
        {
            return false;
        }
        return true;
    }
};

// host -> server -> one player: the state stored for that player's nickname, to apply in their game
class PlayerStateRestore : public Packet
{
    DEFINE_PACKET_TYPE(PlayerStateRestore, ePacketType::PLAYER_STATE_RESTORE, ePacketChannel::EVENT);

public:
    int targetPlayerId = 0;
    char targetName[Config::MAX_NICKNAME_LENGTH + 1]{};  // the target's nickname as the server knows it
    uint32_t restoreId = 0;                              // acknowledged in the target's next snapshot, never 0
    SPlayerState state{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, targetPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_string(stream, targetName, static_cast<int>(sizeof(targetName)));
        serialize_uint32(stream, restoreId);
        serialize_object(stream, state);

        if (Stream::IsReading && (restoreId == 0 || !IsValidPlayerName(targetName)))
        {
            return false;
        }
        return true;
    }
};
}  // namespace Packets::PlayerState

// NOLINTEND(readability-simplify-boolean-expr)
