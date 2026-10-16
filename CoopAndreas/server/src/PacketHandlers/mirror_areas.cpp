#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/mirror_areas.h"
#include "CHostManager.h"

// private build (mission mirror layer, client/src/CMirrorAreas.h): the areas of the host's mission, relayed to everyone
// else with the sender filled in; dropped when the sender isn't the host. The server keeps no state: the host sends its
// current set when a player joins.
PACKET_HANDLER(
    ePacketType::MISSION_MIRROR_AREAS, Packets::Mirror::MirrorAreas* pMirrorAreas, CNetworkPlayer* pNetworkPlayer)
{
    pMirrorAreas->playerid = pNetworkPlayer->m_iPlayerId;
    CHostManager::SendFromHostToOthers(*pMirrorAreas, pNetworkPlayer);
}
