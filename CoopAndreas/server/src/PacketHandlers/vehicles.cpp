#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/vehicles.h"

PACKET_HANDLER(
    ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn, CNetworkPlayer* pNetworkPlayer)
{
    pVehicleSpawn->vehicleid = CNetworkVehicleManager::GetFreeID();
    // private build (vehicle sync protocol): no record with id -1 (nothing could be sent for it anyway)
    if (pVehicleSpawn->vehicleid < 0)
    {
        logger::warn("%s spawned a vehicle, but all %d vehicle ids are taken", pNetworkPlayer->GetName().c_str(),
            Config::MAX_SERVER_VEHICLES);
        return;
    }
    GetPacketFactory().SendToAll(*pVehicleSpawn, pNetworkPlayer);

    // send it back to the syncer of the vehicle so that he knows the id
    Packets::Vehicles::VehicleConfirm vehicleConfirmPacket{};
    vehicleConfirmPacket.tempid = pVehicleSpawn->tempid;
    vehicleConfirmPacket.vehicleid = pVehicleSpawn->vehicleid;
    GetPacketFactory().Send(vehicleConfirmPacket, pNetworkPlayer);

    CNetworkVehicle* vehicle = new CNetworkVehicle(
        pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos, pVehicleSpawn->rot.m_angle);

    vehicle->m_pSyncer = pNetworkPlayer;
    vehicle->m_nPrimaryColor = pVehicleSpawn->color1;
    vehicle->m_nSecondaryColor = pVehicleSpawn->color2;
    vehicle->m_nCreatedBy = pVehicleSpawn->createdBy;
    // private build (vehicle sync protocol)
    vehicle->m_pCreator = pNetworkPlayer;
    vehicle->m_bHostMission = VehicleSeatPolicy::IsHostMission(vehicle->m_nCreatedBy, pNetworkPlayer->m_bIsHost);

    CNetworkVehicleManager::Add(vehicle);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid))
    {
        // private build (vehicle sync protocol): the syncer, the creator for a vehicle its game lost before
        // VEHICLE_CONFIRM, or the host whose mission deleted a vehicle another game syncs (VehicleSync::eRemoveReason)
        if (VehicleSeatPolicy::AcceptRemove(pVehicleRemove->reason, vehicle->m_pSyncer == pNetworkPlayer,
                vehicle->m_pCreator == pNetworkPlayer, pNetworkPlayer->m_bIsHost))
        {
            GetPacketFactory().SendToAll(*pVehicleRemove, pNetworkPlayer);

            if (pVehicleRemove->reason != VehicleSync::REMOVE_BY_SYNCER)
            {
                printf("[Vehicle] : %s (id %d) removed vehicle %d (%s)\n", pNetworkPlayer->GetName().c_str(),
                    pNetworkPlayer->m_iPlayerId, vehicle->m_nVehicleId,
                    pVehicleRemove->reason == VehicleSync::REMOVE_UNCONFIRMED ? "gone before its confirmation"
                                                                              : "the host's mission deleted it");
            }
            CNetworkVehicleManager::Delete(vehicle);
        }
        else
        {
            logger::warn("%s tried to delete someone else's vehicle", pNetworkPlayer->GetName().c_str());
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            vehicle->m_bUsedByPed = false;
            vehicle->m_vecPosition = pVehicleIdleUpdate->pos;
            vehicle->m_vecRotation = pVehicleIdleUpdate->rot;
            GetPacketFactory().SendToAll(*pVehicleIdleUpdate, pNetworkPlayer);
        }
        else
        {
            logger::warn("%s tried to update someone else's vehicle (idle)", pNetworkPlayer->GetName().c_str());
            // private build (vehicle sync protocol): that game still thinks it syncs the vehicle; it hears it doesn't
            vehicle->CorrectStaleSyncer(pNetworkPlayer);
        }
    }
    else
    {
        logger::warn("%s tried to update a vehicle (idle) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid))
    {
        pNetworkVehicle->SetOccupant(0, pNetworkPlayer);
        pNetworkVehicle->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->m_vecRotation = pVehicleDriverUpdate->rot;
        pNetworkVehicle->m_bUsedByPed = false;
        pNetworkVehicle->ReassignSyncer(pNetworkPlayer);

        pVehicleDriverUpdate->playerid = pNetworkPlayer->m_iPlayerId;
        GetPacketFactory().SendToAll(*pVehicleDriverUpdate, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to update a vehicle (driver) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid))
    {
        // private build (vehicle sync protocol): an unknown passenger seat (-1) isn't recorded (upstream made it seat 0,
        // the driver's); the sender's next VEHICLE_PASSENGER_UPDATE names its seat
        const int seat = VehicleSeatPolicy::SeatFromEnter(pVehicleEnter->bPassenger, pVehicleEnter->seatid);
        if (seat >= 0)
        {
            pNetworkVehicle->SetOccupant(static_cast<int8_t>(seat), pNetworkPlayer);
        }

        pVehicleEnter->playerid = pNetworkPlayer->m_iPlayerId;
        GetPacketFactory().SendToAll(*pVehicleEnter, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to enter a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit, CNetworkPlayer* pNetworkPlayer)
{
    pVehicleExit->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pVehicleExit, pNetworkPlayer);

    // private build (vehicle sync protocol): the seat it held, whichever it was (CVehicleSeatPolicy.h)
    pNetworkPlayer->RemoveFromVehicle();
}

PACKET_HANDLER(
    ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        pNetworkVehicle->m_damageManager = pVehicleDamage->damageManager;

        GetPacketFactory().SendToAll(*pVehicleDamage, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to damage a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid))
    {
        pNetworkVehicle->m_pComponents.push_back(pVehicleComponentAdd->componentid);

        GetPacketFactory().SendToAll(*pVehicleComponentAdd, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp add) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_REMOVE,
    Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid))
    {
        auto it = std::find(pNetworkVehicle->m_pComponents.begin(), pNetworkVehicle->m_pComponents.end(),
            pVehicleComponentRemove->componentid);
        if (it != pNetworkVehicle->m_pComponents.end())
        {
            pNetworkVehicle->m_pComponents.erase(it);
        }

        GetPacketFactory().SendToAll(*pVehicleComponentRemove, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp remove) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_PASSENGER_UPDATE,
    Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid))
    {
        pVehiclePassengerUpdate->playerid = pNetworkPlayer->m_iPlayerId;
        GetPacketFactory().SendToAll(*pVehiclePassengerUpdate, pNetworkPlayer);

        // private build (vehicle sync protocol): an unknown seat (-1, the sender didn't find its ped among the
        // passengers) isn't recorded and changes no syncer; upstream read it as seat 0 (here 1)
        const int seat = VehicleSeatPolicy::SeatFromPassenger(pVehiclePassengerUpdate->seatid);
        if (seat < 0)
        {
            return;
        }
        pNetworkVehicle->SetOccupant(static_cast<int8_t>(seat), pNetworkPlayer);

        // no driver: a passenger becomes the syncer (not of a mission vehicle, nor of one a ped drives)
        if (auto pNewSyncer = VehicleSeatPolicy::ChoosePassengerSyncer<CNetworkPlayer>(*pNetworkVehicle,
                pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE, pNetworkVehicle->m_bUsedByPed))
        {
            pNetworkVehicle->ReassignSyncer(pNewSyncer);
        }
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (passenger) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy,
    CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIsHost)
    {
        if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
        {
            pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;
            // private build (vehicle sync protocol): the host's scripts own it while they keep it a mission vehicle
            pNetworkVehicle->m_bHostMission = VehicleSeatPolicy::IsHostMission(pNetworkVehicle->m_nCreatedBy, true);
            GetPacketFactory().SendToAll(*pSetVehicleCreatedBy, pNetworkPlayer);
        }
    }
}

// private build (radio sync): the driver's radio station, relayed to everyone else
PACKET_HANDLER(ePacketType::VEHICLE_RADIO_STATION, Packets::Vehicles::VehicleRadioStation* pVehicleRadioStation,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CNetworkVehicleManager::GetVehicle(pVehicleRadioStation->vehicleid))
    {
        GetPacketFactory().SendToAll(*pVehicleRadioStation, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync the radio of a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}
