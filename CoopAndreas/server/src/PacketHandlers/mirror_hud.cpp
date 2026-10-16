#include "network/packets/mirror_hud.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "CHostManager.h"

// private build (mission mirror layer, timers and counters, client/src/CMirrorHud.h): the host's state, relayed to
// everyone else with the sender filled in; dropped when the sender isn't the host. No state is kept: the host sends the
// whole state when a player joins.
PACKET_HANDLER(ePacketType::MISSION_MIRROR_HUD, Packets::Mirror::MirrorHud* pMirrorHud, CNetworkPlayer* pNetworkPlayer)
{
    pMirrorHud->playerid = pNetworkPlayer->m_iPlayerId;
    CHostManager::SendFromHostToOthers(*pMirrorHud, pNetworkPlayer);
}
