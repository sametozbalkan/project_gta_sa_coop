#include "stdafx.h"
#include <network/packets/vehicles.h>

std::vector<CNetworkVehicle*> CNetworkVehicleManager::m_pVehicles;

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
    m_pVehicles.push_back(vehicle);
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle)
{
    auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
    if (it != m_pVehicles.end())
    {
        m_pVehicles.erase(it);
    }
}

CNetworkVehicle* CNetworkVehicleManager::GetVehicle(int vehicleid)
{
    for (int i = 0; i != m_pVehicles.size(); i++)
    {
        if (m_pVehicles[i]->m_nVehicleId == vehicleid)
        {
            return m_pVehicles[i];
        }
    }
    return nullptr;
}

int CNetworkVehicleManager::GetFreeID()
{
    for (int i = 0; i < Config::MAX_SERVER_VEHICLES; i++)
    {
        if (CNetworkVehicleManager::GetVehicle(i) == nullptr)
            return i;
    }

    return -1;
}

void CNetworkVehicleManager::RemoveAllHostedAndNotify(CNetworkPlayer* player)
{
    // private build (vehicle sync protocol): the player who leaves holds no seat anywhere (CNetwork::HandlePlayerDisconnected
    // emptied its seats; its ids go too)
    VehicleSeatPolicy::LeaveSeat<CNetworkVehicle>(player, [](int vehicleid) { return GetVehicle(vehicleid); });

    // still in the player list here, and still flagged if it is the host
    CNetworkPlayer* pHost = CNetworkPlayerManager::GetHost();
    const bool bHostStays = pHost != nullptr && pHost != player;

    for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
    {
        CNetworkVehicle* pVehicle = *it;
        if (pVehicle->m_pCreator == player)
        {
            pVehicle->m_pCreator = nullptr;
        }
        // the host leaves: its scripts own no car any more (a later host's game doesn't know them)
        if (player->m_bIsHost)
        {
            pVehicle->m_bHostMission = false;
        }

        if (pVehicle->m_pSyncer != player)
        {
            ++it;
            continue;
        }

        // private build: the host's mission keeps its car; the host's game takes it over (it gets ASSIGN_VEHICLE, the
        // player who left gets nothing)
        if (VehicleSeatPolicy::OnSyncerLeft(pVehicle->m_bHostMission, bHostStays) ==
            VehicleSeatPolicy::eSyncerLeft::GIVE_TO_HOST)
        {
            pVehicle->m_pSyncer = nullptr;
            pVehicle->ReassignSyncer(pHost);
            printf("[Vehicle] : vehicle %d of the host's mission goes back to %s (id %d), %s (id %d) left\n",
                pVehicle->m_nVehicleId, pHost->GetName().c_str(), pHost->m_iPlayerId, player->GetName().c_str(),
                player->m_iPlayerId);
            ++it;
            continue;
        }

        Packets::Vehicles::VehicleRemove packet{};
        packet.vehicleid = pVehicle->m_nVehicleId;
        packet.reason = VehicleSync::REMOVE_BY_SYNCER;
        GetPacketFactory().SendToAll(packet, player);

        // private build: the players still seated in it don't keep its id (a new vehicle can get the same id)
        VehicleSeatPolicy::ClearSeats(*pVehicle);

        delete pVehicle;
        it = CNetworkVehicleManager::m_pVehicles.erase(it);
    }
}

void CNetworkVehicleManager::Delete(CNetworkVehicle* vehicle)
{
    VehicleSeatPolicy::ClearSeats(*vehicle);
    Remove(vehicle);
    delete vehicle;
}