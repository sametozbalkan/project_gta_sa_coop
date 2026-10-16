#include "network/packets/vehicles.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include "CRadioSync.h"

// private build: a passenger seat the vehicle has. The packets allow -1..7; m_apPassengers has 8 entries (index -1
// is m_pDriver) and most vehicles have fewer seats (m_nMaxPassengers).
static bool IsPassengerSeatValid(const CVehicle* pVehicle, int seatid)
{
    return seatid >= 0 && seatid < static_cast<int>(ARRAY_SIZE(pVehicle->m_apPassengers)) &&
        seatid < pVehicle->m_nMaxPassengers;
}

PACKET_HANDLER(ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE SPAWN %d %d %.1f %.1f %.1f %.1f", pVehicleSpawn->vehicleid, pVehicleSpawn->modelid,
        pVehicleSpawn->pos.x, pVehicleSpawn->pos.y, pVehicleSpawn->pos.z, pVehicleSpawn->rot.m_angle);
#endif

    CNetworkVehicle* pNetworkVehicle =
        new CNetworkVehicle(pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos,
            pVehicleSpawn->rot.m_angle, pVehicleSpawn->color1, pVehicleSpawn->color2, pVehicleSpawn->createdBy);
    CNetworkVehicleManager::Add(pNetworkVehicle);
    CNetworkVehicleManager::OnSpawned(pNetworkVehicle); // private build (vehicle sync protocol): an assignment that came first
}

PACKET_HANDLER(ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE REMOVE %d", pVehicleRemove->vehicleid);
#endif
    // private build (vehicle sync protocol): whatever the reason and whether this game synced it, the vehicle goes (players
    // out first) and so does its record; upstream's record destructor sent the removal back when this game synced it.
    // The reason goes into the --coopdiag log.
    CNetworkVehicleManager::OnRemovePacket(pVehicleRemove->vehicleid, pVehicleRemove->reason);
}

PACKET_HANDLER(ePacketType::VEHICLE_CONFIRM, Packets::Vehicles::VehicleConfirm* pVehicleConfirm)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE CONFIRM %d %d", pVehicleConfirm->vehicleid, pVehicleConfirm->tempid);
#endif

    // private build (vehicle sync protocol): a vehicle the game destroyed before this is removed on every game, an
    // assignment that came first applies now
    CNetworkVehicleManager::OnConfirmed(pVehicleConfirm->tempid, pVehicleConfirm->vehicleid);
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid);

    if (pNetworkVehicle == nullptr)
        return;

    // private build (vehicle sync protocol): the syncer's state, and a copy this game lost comes back where it is
    pNetworkVehicle->RememberState(pVehicleIdleUpdate->pos, pVehicleIdleUpdate->roll, pVehicleIdleUpdate->rot,
        pVehicleIdleUpdate->color1, pVehicleIdleUpdate->color2);
    if (!pNetworkVehicle->GetLiveVehicle() &&
        !CNetworkVehicleManager::RecreateLostCopy(pNetworkVehicle, pVehicleIdleUpdate->pos, pVehicleIdleUpdate->roll,
            pVehicleIdleUpdate->rot, pVehicleIdleUpdate->color1, pVehicleIdleUpdate->color2))
        return;

    if (pNetworkVehicle->m_pVehicle->m_matrix == nullptr)
        return;

    pNetworkVehicle->m_pVehicle->m_matrix->pos = pVehicleIdleUpdate->pos;

    // if (CUtil::IsPositionUpdateNeeded(pNetworkVehicle->m_pVehicle->m_matrix->right, pVehicleIdleUpdate->roll, 1))
    pNetworkVehicle->m_pVehicle->m_matrix->right = pVehicleIdleUpdate->roll;

    // if (CUtil::IsPositionUpdateNeeded(pNetworkVehicle->m_pVehicle->m_matrix->up, pVehicleIdleUpdate->rot, 1))
    pNetworkVehicle->m_pVehicle->m_matrix->up = pVehicleIdleUpdate->rot;

    pNetworkVehicle->m_pVehicle->m_vecMoveSpeed = pVehicleIdleUpdate->velocity;
    pNetworkVehicle->m_pVehicle->m_vecTurnSpeed = pVehicleIdleUpdate->turnSpeed;

    pNetworkVehicle->m_pVehicle->m_nPrimaryColor = pVehicleIdleUpdate->color1;
    pNetworkVehicle->m_pVehicle->m_nSecondaryColor = pVehicleIdleUpdate->color2;
    pNetworkVehicle->m_pVehicle->m_fHealth = pVehicleIdleUpdate->health;

    if (pNetworkVehicle->m_pVehicle->GetRemapIndex() != pVehicleIdleUpdate->paintjob)
        pNetworkVehicle->m_pVehicle->SetRemap(pVehicleIdleUpdate->paintjob);

    if (CUtil::GetVehicleType(pNetworkVehicle->m_pVehicle) == eVehicleType::VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pNetworkVehicle->m_pVehicle;

        pPlane->m_fLandingGearStatus = pVehicleIdleUpdate->planeGearState;
    }

    pNetworkVehicle->m_pVehicle->m_eDoorLock = pVehicleIdleUpdate->locked;
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleDriverUpdate->playerid);

    if (pNetworkVehicle == nullptr || pNetworkPlayer == nullptr)
        return;

    // private build (vehicle sync protocol): the driver's keys and health before anything can return early. The other
    // games replay a player's last keys in every frame, and the update with every key released that CNetworkKeepAlive
    // sends when that player's game goes to the background must not be dropped for a copy that isn't recreated now.
    pNetworkPlayer->m_oldControllerState = pVehicleDriverUpdate->playerKeys.oldControllerState;
    pNetworkPlayer->m_newControllerState = pVehicleDriverUpdate->playerKeys.newControllerState;
    pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehicleDriverUpdate->playerHealth;

    // private build (vehicle sync protocol): the driver's state, and a copy this game lost comes back where the driver
    // has it (upstream read a NULL or freed vehicle here)
    const int vehicleid = pVehicleDriverUpdate->vehicleid;
    pNetworkVehicle->RememberState(pVehicleDriverUpdate->pos, pVehicleDriverUpdate->roll, pVehicleDriverUpdate->rot,
        pVehicleDriverUpdate->color1, pVehicleDriverUpdate->color2);
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle();
    if (pVehicle == nullptr)
    {
        pVehicle = CNetworkVehicleManager::RecreateLostCopy(pNetworkVehicle, pVehicleDriverUpdate->pos,
            pVehicleDriverUpdate->roll, pVehicleDriverUpdate->rot, pVehicleDriverUpdate->color1,
            pVehicleDriverUpdate->color2);
        if (pVehicle == nullptr)
            return;
    }

    // private build: a missing or broken ped is recreated at the vehicle and seated only while it has a clump and
    // intelligence; the vehicle is updated either way. The recovery can destroy vehicles, this one too: CREATE_PLAYER
    // (case 0x4668DC) clears space at the new ped with CTheScripts::ClearSpaceForMissionEntity (0x486B00, call at
    // 0x4669B9), which deletes a car there that CanBeDeleted, and the ~CVehicle hook then deletes the record of a car this
    // game syncs. So the record and its vehicle are looked up again after each call that can get there.
    CVector vecPos = pVehicleDriverUpdate->pos;
    CPlayerPed* pRecoveredPed = pNetworkPlayer->GetOrRecoverPed(&vecPos);
    if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle))
        return;
    if (pRecoveredPed != nullptr && (pRecoveredPed->m_pVehicle != pVehicle || !pRecoveredPed->m_nPedFlags.bInVehicle))
    {
        pNetworkPlayer->WarpIntoVehicleDriver(pVehicle);
        if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle))
            return;
    }

    pVehicle->m_matrix->pos = pVehicleDriverUpdate->pos;
    pVehicle->m_matrix->right = pVehicleDriverUpdate->roll;
    pVehicle->m_matrix->up = pVehicleDriverUpdate->rot;
    pVehicle->m_vecMoveSpeed = pVehicleDriverUpdate->velocity;
    pVehicle->m_vecTurnSpeed = pVehicleDriverUpdate->turnSpeed;

    // private build: the weapon's model may load here (CStreaming::LoadAllRequestedModels)
    pNetworkPlayer->ApplyWeaponSnapshot(pVehicleDriverUpdate->playerWeapon);
    if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle))
        return;
    if (CPlayerPed* pPlayerPed = pNetworkPlayer->GetLivePed()) // private build
    {
        pPlayerPed->m_fHealth = pVehicleDriverUpdate->playerHealth.iHealth;
        pPlayerPed->m_fArmour = pVehicleDriverUpdate->playerHealth.iArmour;
    }

    pVehicle->m_nPrimaryColor = pVehicleDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pVehicleDriverUpdate->color2;

    pVehicle->m_fHealth = pVehicleDriverUpdate->health;

    if (pVehicle->GetRemapIndex() != pVehicleDriverUpdate->paintjob)
        pVehicle->SetRemap(pVehicleDriverUpdate->paintjob);

    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
        pAutomobile->m_wMiscComponentAngle = pVehicleDriverUpdate->miscComponentAngle;
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pVehicleDriverUpdate->bikeLean;
    }

    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pVehicle;

        pPlane->m_fLandingGearStatus = pVehicleDriverUpdate->planeGearState;
    }

    pVehicle->m_eDoorLock = pVehicleDriverUpdate->locked;
    pNetworkVehicle->m_playerDriverSnapshot = *pVehicleDriverUpdate;
}

PACKET_HANDLER(ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleEnter->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    // private build: tasks only for a ped with a clump and intelligence (GetReadyPed)
    CPlayerPed* pPlayerPed = pNetworkPlayer->GetReadyPed();
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle(); // private build (vehicle sync protocol): may be lost

    if (pPlayerPed == nullptr || pVehicle == nullptr)
        return;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("player %d entered vehicleid %d %s", pVehicleEnter->playerid, pVehicleEnter->vehicleid,
        pVehicleEnter->seatid != 0 ? "as passenger" : "");
#endif

    // drop the jetpack if present
    if (auto pTask = pPlayerPed->m_pIntelligence->GetTaskJetPack())
    {
        pTask->MakeAbortable(pPlayerPed, ABORT_PRIORITY_URGENT, nullptr);
        pTask->m_nThrustStop = 0;
        pTask->m_nThrustFwd = 0;
        pTask->m_fThrustStrafe = 0.0f;
    }

    if (pVehicleEnter->bPassenger)
    {
        // private build: an unknown seat (-1 from CCarEnterExit::ComputePassengerIndexFromCarDoor) would be door -1;
        // the next VEHICLE_PASSENGER_UPDATE seats the player
        if (IsPassengerSeatValid(pVehicle, pVehicleEnter->seatid))
        {
            pNetworkPlayer->EnterVehiclePassenger(pVehicle, pVehicleEnter->seatid);
        }
        else
        {
            CNetworkVehicleManager::LogFrequentEvent("seat-unknown", pVehicleEnter->vehicleid,
                "packet=enter player=%d seat=%d maxPassengers=%d", pVehicleEnter->playerid.value, pVehicleEnter->seatid,
                pVehicle->m_nMaxPassengers);
        }
    }
    else
    {
        if (pVehicleEnter->bForce)
        {
            pNetworkPlayer->WarpIntoVehicleDriver(pVehicle);
        }
        else
        {
            pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never
            auto pTask = new CTaskComplexEnterCarAsDriver(pVehicle);
            pPlayerPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleExit->playerid);

    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    // private build (vehicle sync protocol): that player's game sends no driver or passenger update while it gets out
    // (VehicleSyncPolicy::ChooseLocalUpdate), so the keys of its last one would drive the car on here; a car whose driver
    // is getting out takes no more control input (CAutomobile::ProcessControl skips ProcessControlInputs for a driver in
    // PEDSTATE_EXIT_CAR, gta-reversed Automobile.cpp), so it keeps the pedals of the last frame before. Released keys
    // until its next update (or key event) brings the real ones.
    pNetworkPlayer->m_oldControllerState = CControllerState{};
    pNetworkPlayer->m_newControllerState = CControllerState{};

    // private build: see VEHICLE_ENTER
    CPlayerPed* pPlayerPed = pNetworkPlayer->GetReadyPed();
    if (pPlayerPed == nullptr || pPlayerPed->m_pVehicle == nullptr || !pPlayerPed->m_nPedFlags.bInVehicle)
    {
        return;
    }

    CVehicle* pVehicle = pPlayerPed->m_pVehicle;

    if (!pVehicle->IsVTableValid() || !pPlayerPed->IsVTableValid())
        return;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("player %d exited from vehicle", pVehicleExit->playerid);
#endif

    if (pVehicleExit->bForce)
    {
        pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal
        CVector doorPos{};
        int door = CCarEnterExit::ComputeTargetDoorToExit(pVehicle, pPlayerPed);
        auto task = CTaskSimpleCarSetPedOut(pVehicle, door, false);
        task.ProcessPed(pPlayerPed);
    }
    else
    {
        pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal
        auto* task = new CTaskComplexLeaveCar(pVehicle, 0, 0, true, false);
        pPlayerPed->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, false);
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VehicleDamage__Handle");
#endif

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        if (auto pVehicle = pNetworkVehicle->GetLiveVehicle()) // private build (vehicle sync protocol): may be lost
        {
            if (pVehicle->IsVTableValid())
            {
                if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
                {
                    CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
                    pAutomobile->m_damageManager = pVehicleDamage->damageManager;
                    pAutomobile->SetupDamageAfterLoad();
                    pNetworkVehicle->m_oldDamageState = pAutomobile->m_damageManager;
                }
            }
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle(); // private build (vehicle sync protocol): may be lost
    if (!pVehicle || !pVehicle->IsVTableValid())
    {
        return;
    }

    CStreaming::RequestModel(pVehicleComponentAdd->componentid, eStreamingFlags::GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);
    CStreaming::RequestVehicleUpgrade(pVehicleComponentAdd->componentid, eStreamingFlags::GAME_REQUIRED);

    uint8_t count = 10;
    while (!CStreaming::HasVehicleUpgradeLoaded(pVehicleComponentAdd->componentid) && count)
    {
        Sleep(5);
        count--;
    }

    if (!count)
        return;

    // private build (vehicle sync protocol): the model loads above ran engine code; the vehicle must still be this record's
    if (!CNetworkVehicleManager::StillHolds(pVehicleComponentAdd->vehicleid, pNetworkVehicle, pVehicle))
        return;

    pVehicle->AddVehicleUpgrade(pVehicleComponentAdd->componentid);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_COMPONENT_REMOVE, Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle(); // private build (vehicle sync protocol): may be lost
    if (!pVehicle || !pVehicle->IsVTableValid())
    {
        return;
    }

    pVehicle->RemoveVehicleUpgrade(pVehicleComponentRemove->componentid);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_PASSENGER_UPDATE, Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehiclePassengerUpdate->playerid);

    if (pNetworkVehicle == nullptr || pNetworkPlayer == nullptr)
    {
        return;
    }

    // private build: the snapshot is kept first, also for a copy that is lost now (the keys as in VEHICLE_DRIVER_UPDATE);
    // a missing or broken ped is recreated at the vehicle and seated, armed and given drive-by tasks only while it has a
    // clump and intelligence
    pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehiclePassengerUpdate->playerHealth;
    pNetworkPlayer->m_newControllerState = pVehiclePassengerUpdate->playerKeys.newControllerState;
    pNetworkPlayer->m_oldControllerState = pVehiclePassengerUpdate->playerKeys.oldControllerState;

    const int vehicleid = pVehiclePassengerUpdate->vehicleid;
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle(); // private build (vehicle sync protocol): may be lost

    if (pVehicle == nullptr)
    {
        return;
    }

    // private build: m_apPassengers[seatid] below, and the seat the player is warped into
    if (!IsPassengerSeatValid(pVehicle, pVehiclePassengerUpdate->seatid))
    {
        return;
    }

    // private build (vehicle sync protocol): the recovery can destroy vehicles, this one too (see VEHICLE_DRIVER_UPDATE):
    // the record and its vehicle are looked up again after each call that can get there
    CVector vecPos = pVehicle->GetPosition();
    CPlayerPed* pPlayerPed = pNetworkPlayer->GetOrRecoverPed(&vecPos);
    if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle))
    {
        return;
    }
    if (pPlayerPed == nullptr)
    {
        return;
    }

    if (!pPlayerPed->m_nPedFlags.bInVehicle || pPlayerPed->m_pVehicle != pVehicle ||
        pVehicle->m_apPassengers[pVehiclePassengerUpdate->seatid] != pPlayerPed)
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("forcing enter passenger %d", player->m_iPlayerId);
#endif

        // private build (vehicle sync protocol, --coopdiag): the seat the passenger reports wins
        CNetworkVehicleManager::LogFrequentEvent("seat-correction", pVehiclePassengerUpdate->vehicleid,
            "player=%d seat=%d inVehicle=%d sameVehicle=%d", pVehiclePassengerUpdate->playerid.value,
            pVehiclePassengerUpdate->seatid, pPlayerPed->m_nPedFlags.bInVehicle, pPlayerPed->m_pVehicle == pVehicle);
        pNetworkPlayer->WarpIntoVehiclePassenger(pVehicle, pVehiclePassengerUpdate->seatid);
        if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle))
        {
            return;
        }
    }

    // private build: the weapon's model may load here (CStreaming::LoadAllRequestedModels)
    pNetworkPlayer->ApplyWeaponSnapshot(pVehiclePassengerUpdate->playerWeapon);
    if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle))
    {
        return;
    }
    if ((pPlayerPed = pNetworkPlayer->GetReadyPed()) == nullptr)
    {
        return;
    }
    pPlayerPed->m_fHealth = pVehiclePassengerUpdate->playerHealth.iHealth;
    pPlayerPed->m_fArmour = pVehiclePassengerUpdate->playerHealth.iArmour;

    if (pVehiclePassengerUpdate->driveby && !CDriveBy::IsPedInDriveby(pPlayerPed))
    {
        CDriveBy::StartDriveby(pPlayerPed);
    }
    else if (!pVehiclePassengerUpdate->driveby && CDriveBy::IsPedInDriveby(pPlayerPed))
    {
        CDriveBy::StopDriveby(pPlayerPed);
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_VEHICLE, Packets::Vehicles::AssignVehicleSyncer* pAssignVehicleSyncer)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("%s VEHICLE %d", pAssignVehicleSyncer->bSyncing ? "SYNCING" : "NOT SYNCING",
        pAssignVehicleSyncer->vehicleid);
#endif

    // private build (vehicle sync protocol): the packet says what this game now is for the vehicle (upstream flipped
    // m_bSyncing, so a lost assignment inverted every later one); one for a vehicle this game doesn't know yet waits
    // for it. A copy is MISSION_VEHICLE, the syncer's vehicle has its own createdBy.
    CNetworkVehicleManager::OnAssignment(
        pAssignVehicleSyncer->vehicleid, pAssignVehicleSyncer->modelid, pAssignVehicleSyncer->bSyncing);
}


PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy)
{
    if (CLocalPlayer::m_bIsHost)
    {
        return;
    }

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
    {
        pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;

        if (pNetworkVehicle->m_bSyncing)
        {
            if (auto pVehicle = pNetworkVehicle->GetLiveVehicle()) // private build (vehicle sync protocol)
            {
                pVehicle->SetVehicleCreatedBy(pSetVehicleCreatedBy->createdBy);
            }
        }
    }
}

// private build (radio sync)
PACKET_HANDLER(ePacketType::VEHICLE_RADIO_STATION, Packets::Vehicles::VehicleRadioStation* pVehicleRadioStation)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE RADIO %d %d", pVehicleRadioStation->vehicleid, pVehicleRadioStation->station);
#endif

    CRadioSync::OnStationReceived(pVehicleRadioStation->vehicleid, pVehicleRadioStation->station);
}