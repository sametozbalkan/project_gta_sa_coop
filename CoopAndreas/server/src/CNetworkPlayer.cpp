#include "stdafx.h"

CNetworkPlayer::CNetworkPlayer(ENetPeer* peer, int playerid)
{
    m_pPeer = peer;
    m_iPlayerId = playerid;
}

std::string CNetworkPlayer::GetName()
{
    if (m_Name[0])
        return std::string(m_Name);
    else
        return "player " + std::to_string(m_iPlayerId);
}

void CNetworkPlayer::RemoveFromVehicle()
{
    // private build (vehicle sync protocol): out of the seat it holds, and of any stale second one (CVehicleSeatPolicy.h)
    VehicleSeatPolicy::LeaveSeat<CNetworkVehicle>(
        this, [](int vehicleid) { return CNetworkVehicleManager::GetVehicle(vehicleid); });
}
