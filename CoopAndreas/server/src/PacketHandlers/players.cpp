#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"

PACKET_HANDLER(
    ePacketType::PLAYER_ONFOOT_UPDATE, Packets::Players::OnFootUpdate* pOnFootUpdate, CNetworkPlayer* pNetworkPlayer)
{
    pOnFootUpdate->playerid.value = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pOnFootUpdate, pNetworkPlayer);

    // private build (vehicle sync protocol): a seat its holder's game has reported on foot for a while goes (an entry
    // that never completed, an exit no VEHICLE_EXIT reported); not while the player is still getting in
    // (VehicleSeatPolicy::ShouldFreeSeatOnFoot)
    if (VehicleSeatPolicy::ShouldFreeSeatOnFoot(
            pNetworkPlayer->m_nVehicleId >= 0, g_serverTime, pNetworkPlayer->m_nSeatClaimedAt))
    {
        const int vehicleid = pNetworkPlayer->m_nVehicleId;
        const int seat = pNetworkPlayer->m_nSeatId;
        pNetworkPlayer->RemoveFromVehicle();
        printf("[Vehicle] : %s (id %d) has been on foot for %u s, its seat %d in vehicle %d is free again\n",
            pNetworkPlayer->GetName().c_str(), pNetworkPlayer->m_iPlayerId,
            static_cast<unsigned>(g_serverTime - pNetworkPlayer->m_nSeatClaimedAt) / 1000, seat, vehicleid);
    }
}

PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC, Packets::Players::KeyPressed* pKeyPressed, CNetworkPlayer* pNetworkPlayer)
{
    pKeyPressed->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pKeyPressed, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_CAMERA_SYNC, Packets::Players::PlayerCameraSync* pPlayerCameraSync, CNetworkPlayer* pNetworkPlayer)
{
    pPlayerCameraSync->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerCameraSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::SET_PLAYER_TASK, Packets::Players::SetPlayerTask* pSetPlayerTask, CNetworkPlayer* pNetworkPlayer)
{
    pSetPlayerTask->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pSetPlayerTask, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ENEX_TRANSITION, Packets::Players::EnExTransition* pEnExTransition,
    CNetworkPlayer* pNetworkPlayer)
{
    pEnExTransition->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pEnExTransition, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_PLACE_WAYPOINT, Packets::Players::PlayerPlaceWaypoint* pPlayerPlaceWaypoint, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_waypointState = *pPlayerPlaceWaypoint;

    pPlayerPlaceWaypoint->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerPlaceWaypoint, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::RESPAWN_PLAYER, Packets::Players::RespawnPlayer* pRespawnPlayer, CNetworkPlayer* pNetworkPlayer)
{
    pRespawnPlayer->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pRespawnPlayer, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_BULLET_SHOT, Packets::Players::PlayerBulletShot* pPlayerBulletShot, CNetworkPlayer* pNetworkPlayer)
{
    pPlayerBulletShot->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerBulletShot, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ADD_PROJECTILE, Packets::Players::AddProjectile* pAddProjectile, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pAddProjectile, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_STATS, Packets::Players::PlayerStats* pPlayerStats, CNetworkPlayer* pNetworkPlayer)
{
    for (size_t i = 0; i < ARRAY_SIZE(pNetworkPlayer->m_afStats); i++)
    {
        pNetworkPlayer->m_afStats[i] = pPlayerStats->stats[i];
    }

    pNetworkPlayer->m_ucSyncFlags.bStatsModified = true;
    pPlayerStats->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerStats, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::REBUILD_PLAYER, Packets::Players::RebuildPlayer* pRebuildPlayer, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;
    pNetworkPlayer->m_ucSyncFlags.bClothesModified = true;
    pRebuildPlayer->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pRebuildPlayer, pNetworkPlayer);
}

// private build (vehicle sync protocol): that player's game started over (a new game or a load), after the removals of
// what it synced (same channel). Its scripts are gone, so are the ped claims they made (a ped handed to it later would
// have nobody to hold it); its player sits in no vehicle; a ped the server still counts as its own (handed to it while it
// restarted, or spawned just before: that game has no record of it any more) goes to a claimer or from every game, as
// when a player leaves; and it gets the peds the other games sync again, as a joiner (PedHandoverPolicy::
// IsSentAfterRestart). Their cars come back on that game from their drivers' next updates, the lost copies' records are
// still there (client CVehicleSyncPolicy.h, ePedDriverVehicle).
PACKET_HANDLER(ePacketType::PLAYER_GAME_RESTARTED, Packets::Players::PlayerGameRestarted* pPlayerGameRestarted,
    CNetworkPlayer* pNetworkPlayer)
{
    (void)pPlayerGameRestarted;
    const size_t claims = pNetworkPlayer->m_vPedClaims.size();
    pNetworkPlayer->m_vPedClaims.clear();
    pNetworkPlayer->RemoveFromVehicle();
    CNetworkPedManager::RemoveAllHostedAndNotify(pNetworkPlayer, "started over");
    const unsigned int sent = CNetworkPedManager::SendPedsAfterRestart(pNetworkPlayer);
    printf("[Game] : %s (id %d) started over: %u peds sent again, %u ped claims dropped\n",
        pNetworkPlayer->GetName().c_str(), pNetworkPlayer->m_iPlayerId, sent, static_cast<unsigned>(claims));
}

