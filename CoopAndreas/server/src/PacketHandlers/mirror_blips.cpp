#include "network/packets/mirror_blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "CHostManager.h"

// private build (mission mirror layer, client/src/CMirrorBlips.h): the host mission's blips, from the host only, with
// the sender filled in; a join snapshot goes to the player it names, everything else to every other player
PACKET_HANDLER(ePacketType::MISSION_MIRROR_BLIPS, Packets::Mirror::MirrorBlips* pMirrorBlips,
    CNetworkPlayer* pNetworkPlayer)
{
    pMirrorBlips->playerid = pNetworkPlayer->m_iPlayerId;
    if (pMirrorBlips->targetPlayerId >= 0)
    {
        CHostManager::SendFromHostToPlayer(*pMirrorBlips, pNetworkPlayer, pMirrorBlips->targetPlayerId);
    }
    else
    {
        CHostManager::SendFromHostToOthers(*pMirrorBlips, pNetworkPlayer);
    }
}
