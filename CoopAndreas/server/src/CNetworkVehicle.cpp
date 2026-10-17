#include "stdafx.h"
#include <network/packets/vehicles.h>

CNetworkVehicle::CNetworkVehicle(int vehicleid, unsigned short model, CVector pos, float rot)
{
    m_nVehicleId = vehicleid;
    m_nModelId = model;
    m_vecPosition = pos;
    // private build (vehicle sync protocol): the forward vector of the spawn's heading (as the updates carry it,
    // SendVehicleTo turns it back with VehicleSync::HeadingFromForward); upstream kept (0, 0, 0)
    m_vecRotation = CVector(-sinf(rot), cosf(rot), 0.0f);
}

void CNetworkVehicle::ReassignSyncer(CNetworkPlayer* newSyncer)
{
    if (m_pSyncer != newSyncer)
    {
        // private build (vehicle sync protocol): each receiver hears what it is now instead of flipping its state
        Packets::Vehicles::AssignVehicleSyncer packet{};
        packet.vehicleid = m_nVehicleId;
        packet.modelid = m_nModelId;

        // send to the old vehicle syncer
        if (m_pSyncer)
        {
            packet.bSyncing = false;
            GetPacketFactory().Send(packet, m_pSyncer);
        }

        // send to the new
        if (newSyncer)
        {
            packet.bSyncing = true;
            GetPacketFactory().Send(packet, newSyncer);
        }

        m_pSyncer = newSyncer;
    }
}

void CNetworkVehicle::SetOccupant(int8_t seatid, CNetworkPlayer* player)
{
    // private build (vehicle sync protocol): one seat in one vehicle per player (CVehicleSeatPolicy.h); upstream left the
    // player's previous seat recorded
    VehicleSeatPolicy::SetOccupant(*this, seatid, player,
        [](int vehicleid) { return CNetworkVehicleManager::GetVehicle(vehicleid); });
    // private build: the seat is fresh again (VehicleSeatPolicy::ShouldFreeSeatOnFoot)
    if (player != nullptr)
    {
        player->m_nSeatClaimedAt = g_serverTime;
    }
}

void CNetworkVehicle::CorrectStaleSyncer(CNetworkPlayer* player)
{
    if (player == nullptr || player == m_pSyncer || !m_syncerCorrection.ShouldSend(g_serverTime))
    {
        return;
    }

    Packets::Vehicles::AssignVehicleSyncer packet{};
    packet.vehicleid = m_nVehicleId;
    packet.modelid = m_nModelId;
    packet.bSyncing = false;
    GetPacketFactory().Send(packet, player);
    printf("[Vehicle] : %s (id %d) isn't the syncer of vehicle %d, told it so\n", player->GetName().c_str(),
        player->m_iPlayerId, m_nVehicleId);
}
