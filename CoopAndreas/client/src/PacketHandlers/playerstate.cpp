#include "network/packets/playerstate.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CPlayerStateSave.h>

// private build: player state for co-op saves (CPlayerStateSave.h)

// the host: another player's state
PACKET_HANDLER(ePacketType::PLAYER_STATE_SNAPSHOT, Packets::PlayerState::PlayerStateSnapshot* pPlayerStateSnapshot)
{
    CPlayerStateSave::HandleSnapshot(*pPlayerStateSnapshot);
}

// the other players: the state the host keeps for this player
PACKET_HANDLER(ePacketType::PLAYER_STATE_RESTORE, Packets::PlayerState::PlayerStateRestore* pPlayerStateRestore)
{
    CPlayerStateSave::HandleRestore(*pPlayerStateRestore);
}
