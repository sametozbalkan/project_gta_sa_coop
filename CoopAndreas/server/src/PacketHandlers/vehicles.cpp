#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/vehicles.h"
#include "network/entity_lifecycle.h"

// private build (vehicle sync protocol): a packet stamped before the vehicle with its id was created is about an older
// vehicle that had the id (EntityLifecycle::IsFromBeforeCreation; a game stamps with its own clock, which passes the stamp
// of the server's VEHICLE_SPAWN/VEHICLE_CONFIRM before it knows the id): it is dropped instead of acting on the new vehicle
// (upstream seated a late driver in it and made that game its syncer). A line at most once a second.
static bool IsFromOlderVehicle(const Packet& packet, const CNetworkVehicle* vehicle, CNetworkPlayer* player, const char* what)
{
    if (!EntityLifecycle::IsFromBeforeCreation(packet.serverTime, vehicle->m_nCreatedAt))
    {
        return false;
    }
    static bool bPrinted = false;
    static uint32_t lastPrinted = 0;
    if (!bPrinted || static_cast<uint32_t>(g_serverTime - lastPrinted) >= 1000)
    {
        bPrinted = true;
        lastPrinted = g_serverTime;
        printf("[Vehicle] : dropped a late %s of vehicle %d from %s (id %d), sent before this vehicle had the id\n", what,
            vehicle->m_nVehicleId, player->GetName().c_str(), player->m_iPlayerId);
    }
    return true;
}

PACKET_HANDLER(
    ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn, CNetworkPlayer* pNetworkPlayer)
{
    // private build (vehicle sync protocol): round robin (CNetworkVehicleManager::GetFreeID)
    pVehicleSpawn->vehicleid = CNetworkVehicleManager::GetFreeID();
    // private build (vehicle sync protocol): no record with id -1 (nothing could be sent for it anyway); the creator's
    // temp record gives its slot back after a while (client CVehicleSyncPolicy.h, TEMP_RECORD_MAX_AGE_MS)
    if (pVehicleSpawn->vehicleid < 0)
    {
        logger::warn("%s spawned a vehicle, but all %d vehicle ids are taken", pNetworkPlayer->GetName().c_str(),
            Config::MAX_SERVER_VEHICLES);
        return;
    }
    // private build: relayed with the server's clock, as the VEHICLE_CONFIRM below (EntityLifecycle::UsesServerTime)
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
    vehicle->m_nCreatedAt = g_serverTime;

    CNetworkVehicleManager::Add(vehicle);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove, CNetworkPlayer* pNetworkPlayer)
{
    auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid);
    if (vehicle == nullptr)
    {
        return;
    }

    // private build (vehicle sync protocol): the syncer, the creator for a vehicle its game lost before VEHICLE_CONFIRM,
    // or the host whose mission deleted a vehicle another game syncs (VehicleSync::eRemoveReason); a car of the host's
    // scripts whose syncer's game restarted goes back to the host; a rejected removal sends the vehicle back to the game
    // that let go of it (VehicleSeatPolicy::DecideRemove). The sender's stamp is read before the relay restamps it.
    CNetworkPlayer* pHost = CNetworkPlayerManager::GetHost();
    const uint8_t reason = pVehicleRemove->reason;
    const auto decision = VehicleSeatPolicy::DecideRemove(reason,
        EntityLifecycle::IsFromBeforeCreation(pVehicleRemove->serverTime, vehicle->m_nCreatedAt),
        vehicle->m_pSyncer == pNetworkPlayer, vehicle->m_pCreator == pNetworkPlayer, pNetworkPlayer->m_bIsHost,
        vehicle->m_bHostMission, pHost != nullptr && pHost != pNetworkPlayer);

    switch (decision)
    {
        case VehicleSeatPolicy::eRemove::DROP_STALE:
            IsFromOlderVehicle(*pVehicleRemove, vehicle, pNetworkPlayer, "removal");
            return;

        case VehicleSeatPolicy::eRemove::REJECT_AND_RESEND:
            logger::warn("%s tried to delete someone else's vehicle", pNetworkPlayer->GetName().c_str());
            printf("[Vehicle] : %s (id %d) can't remove vehicle %d (%s), its game gets it back\n",
                pNetworkPlayer->GetName().c_str(), pNetworkPlayer->m_iPlayerId, vehicle->m_nVehicleId,
                VehicleSync::RemoveReasonName(reason));
            CNetworkVehicleManager::SendVehicleTo(vehicle, pNetworkPlayer);
            break;

        case VehicleSeatPolicy::eRemove::GIVE_TO_HOST:
            // the restarting game gets no ASSIGN_VEHICLE (it let go of the vehicle), the host does; then the vehicle as a
            // copy for the restarting game
            vehicle->m_pSyncer = nullptr;
            vehicle->ReassignSyncer(pHost);
            CNetworkVehicleManager::SendVehicleTo(vehicle, pNetworkPlayer);
            printf("[Vehicle] : vehicle %d of the host's mission goes back to %s (id %d), %s (id %d) started over\n",
                vehicle->m_nVehicleId, pHost->GetName().c_str(), pHost->m_iPlayerId, pNetworkPlayer->GetName().c_str(),
                pNetworkPlayer->m_iPlayerId);
            break;

        case VehicleSeatPolicy::eRemove::REMOVE:
            // private build: relayed with the server's clock (EntityLifecycle::UsesServerTime)
            GetPacketFactory().SendToAll(*pVehicleRemove, pNetworkPlayer);
            if (reason != VehicleSync::REMOVE_BY_SYNCER)
            {
                printf("[Vehicle] : %s (id %d) removed vehicle %d (%s)\n", pNetworkPlayer->GetName().c_str(),
                    pNetworkPlayer->m_iPlayerId, vehicle->m_nVehicleId, VehicleSync::RemoveReasonName(reason));
            }
            CNetworkVehicleManager::Delete(vehicle);
            break;
    }

    // a game that started over has its player in no vehicle any more
    if (reason == VehicleSync::REMOVE_SYNCER_RESTARTED)
    {
        pNetworkPlayer->RemoveFromVehicle();
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid))
    {
        // private build (vehicle sync protocol)
        if (IsFromOlderVehicle(*pVehicleIdleUpdate, vehicle, pNetworkPlayer, "idle update"))
        {
            return;
        }

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
        // private build (vehicle sync protocol): a late update for an older vehicle with this id neither seats its driver
        // here nor makes that game the syncer
        if (IsFromOlderVehicle(*pVehicleDriverUpdate, pNetworkVehicle, pNetworkPlayer, "driver update"))
        {
            return;
        }

        pNetworkVehicle->SetOccupant(0, pNetworkPlayer);
        pNetworkVehicle->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->m_vecRotation = pVehicleDriverUpdate->rot;
        pNetworkVehicle->m_bUsedByPed = false;
        if (VehicleSeatPolicy::ShouldDriverBecomeSyncer(pNetworkVehicle->m_nModelId))
        {
            pNetworkVehicle->ReassignSyncer(pNetworkPlayer);
        }

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
        if (IsFromOlderVehicle(*pVehicleEnter, pNetworkVehicle, pNetworkPlayer, "enter")) // private build
        {
            return;
        }

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
        if (IsFromOlderVehicle(*pVehicleDamage, pNetworkVehicle, pNetworkPlayer, "damage")) // private build
        {
            return;
        }

        pNetworkVehicle->m_damageManager = pVehicleDamage->damageManager;

        // private build: relayed with the server's clock, as in the join snapshot (EntityLifecycle::UsesServerTime)
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
        if (IsFromOlderVehicle(*pVehicleComponentAdd, pNetworkVehicle, pNetworkPlayer, "component")) // private build
        {
            return;
        }

        pNetworkVehicle->m_pComponents.push_back(pVehicleComponentAdd->componentid);

        // private build: relayed with the server's clock, as in the join snapshot (EntityLifecycle::UsesServerTime)
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
        if (IsFromOlderVehicle(*pVehicleComponentRemove, pNetworkVehicle, pNetworkPlayer, "component")) // private build
        {
            return;
        }

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
        // private build (vehicle sync protocol): no seat and no syncer from a late update for an older vehicle
        if (IsFromOlderVehicle(*pVehiclePassengerUpdate, pNetworkVehicle, pNetworkPlayer, "passenger update"))
        {
            return;
        }

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

        // no driver: a passenger becomes the syncer (not of a mission vehicle, a train carriage, nor of one a ped drives)
        if (auto pNewSyncer = VehicleSeatPolicy::ChoosePassengerSyncer<CNetworkPlayer>(*pNetworkVehicle,
                pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE, pNetworkVehicle->m_bUsedByPed,
                pNetworkVehicle->m_nModelId))
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
            if (IsFromOlderVehicle(*pSetVehicleCreatedBy, pNetworkVehicle, pNetworkPlayer, "created-by")) // private build
            {
                return;
            }

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
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleRadioStation->vehicleid))
    {
        if (IsFromOlderVehicle(*pVehicleRadioStation, pNetworkVehicle, pNetworkPlayer, "radio station")) // private build
        {
            return;
        }
        GetPacketFactory().SendToAll(*pVehicleRadioStation, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync the radio of a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}
