#include "network/packets/playerstate.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"

// private build: player state for co-op saves (client/src/CPlayerStateSave.h)

// a snapshot goes from a player who isn't the host to the host only; the host's own state is in its save
PACKET_HANDLER(ePacketType::PLAYER_STATE_SNAPSHOT, Packets::PlayerState::PlayerStateSnapshot* pPlayerStateSnapshot,
    CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIsHost)
    {
        return;
    }
    CNetworkPlayer* pHost = CNetworkPlayerManager::GetHost();
    if (pHost == nullptr || pHost == pNetworkPlayer)
    {
        return;
    }
    pPlayerStateSnapshot->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().Send(*pPlayerStateSnapshot, pHost);
}

// a restore comes from the host only and goes to the player it names only
PACKET_HANDLER(ePacketType::PLAYER_STATE_RESTORE, Packets::PlayerState::PlayerStateRestore* pPlayerStateRestore,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!pNetworkPlayer->m_bIsHost)
    {
        return;
    }
    CNetworkPlayer* pTarget = CNetworkPlayerManager::GetPlayer(pPlayerStateRestore->targetPlayerId);
    if (pTarget == nullptr || pTarget == pNetworkPlayer ||
        strncmp(pTarget->m_Name, pPlayerStateRestore->targetName, sizeof(pPlayerStateRestore->targetName)) != 0)
    {
        return;
    }
    GetPacketFactory().Send(*pPlayerStateRestore, pTarget);
}
