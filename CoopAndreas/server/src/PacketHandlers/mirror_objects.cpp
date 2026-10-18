#include "network/packets/mirror_objects.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "CHostManager.h"

// private build (mission object mirror, client/src/CMirrorObjects.h; docs/design/mission-objects.md 7):
// - MISSION_OBJECTS: from the host only, with the sender filled in; a join snapshot goes to the player it names,
//   everything else to every other player
// - MISSION_OBJECT_MOTION: from anyone to everyone else, with the sender filled in; the receivers keep only the samples
//   of an object's owner with its current epoch (the server keeps no object state)
PACKET_HANDLER(ePacketType::MISSION_OBJECTS, Packets::Mirror::MirrorObjects* pMirrorObjects,
    CNetworkPlayer* pNetworkPlayer)
{
    pMirrorObjects->playerid = pNetworkPlayer->m_iPlayerId;
    if (pMirrorObjects->targetPlayerId >= 0)
    {
        CHostManager::SendFromHostToPlayer(*pMirrorObjects, pNetworkPlayer, pMirrorObjects->targetPlayerId);
    }
    else
    {
        CHostManager::SendFromHostToOthers(*pMirrorObjects, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::MISSION_OBJECT_MOTION, Packets::Mirror::MirrorObjectMotion* pMotion,
    CNetworkPlayer* pNetworkPlayer)
{
    pMotion->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pMotion, pNetworkPlayer);
}
