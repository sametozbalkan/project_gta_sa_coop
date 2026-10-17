#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED SPAWN %d %d %.1f %.1f %.1f %d %d", packet->pedid, packet->modelId, packet->pos.x,
        packet->pos.y, packet->pos.z, packet->pedType, packet->createdBy);
#endif

    // private build (vehicle sync protocol review 2): one record per id. The snapshot after this game's restart can bring a
    // ped whose relayed spawn came in between, a claim handover sends its ped back here: the record with a ped stays, one
    // without a ped goes (PedSyncPolicy::DecideSpawn)
    if (!CNetworkPedManager::PrepareSpawn(pPedSpawn->pedid))
    {
        return;
    }

    CNetworkPed* pNetworkPed = new CNetworkPed(pPedSpawn->pedid, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos,
        pPedSpawn->createdBy, pPedSpawn->specialModelName);

    CNetworkPedManager::Add(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_CONFIRM, Packets::Peds::PedConfirm* pPedConfirm)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED CONFIRM %d %d", packet->pedid, packet->tempid);
#endif

    if (pPedConfirm->tempid < ARRAY_SIZE(CNetworkPedManager::m_apTempPeds))
    {
        CNetworkPed* pTempPed = CNetworkPedManager::m_apTempPeds[pPedConfirm->tempid];
        if (pTempPed)
        {
            pTempPed->m_nPedId = pPedConfirm->pedid;
            CNetworkPedManager::Add(pTempPed);
            CNetworkPedManager::m_apTempPeds[pPedConfirm->tempid] = nullptr;
        }
    }
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED REMOVE %d", pPedRemove->pedid);
#endif

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);
    if (pNetworkPed)
    {
        CNetworkPedManager::Remove(pNetworkPed);
        delete pNetworkPed;
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_PED, Packets::Peds::AssignPedSyncer* pAssignPedSyncer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pAssignPedSyncer->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    pNetworkPed->CancelPendingOnFoot("ownership");
    if (pNetworkPed->m_bSyncing)
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("NOT SYNCING PED %d ANYMORE", pAssignPedSyncer->pedid);
#endif
        pNetworkPed->m_bSyncing = false;

        if (auto pPed = pNetworkPed->GetLivePed())
        {
            pPed->SetCharCreatedBy(MISSION_CHAR);
        }
    }
    else
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("SYNCING VEHICLE %d", pAssignPedSyncer->pedid);
#endif
        pNetworkPed->m_bSyncing = true;
        pNetworkPed->m_bClaimOnRelease = false;

        if (auto pPed = pNetworkPed->GetLivePed())
        {
            pPed->SetCharCreatedBy(pNetworkPed->m_nCreatedBy);
        }
    }
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);
    if (pNetworkPed) pNetworkPed->ReceiveOnFoot(*pPedOnFoot);
}

// private build (vehicle sync protocol review 2): the car's part of a PED_DRIVER_UPDATE, also onto a car copy whose driver
// copy is gone (VehicleSyncPolicy::ePedDriverVehicle::APPLY_VEHICLE)
static void ApplyDriverUpdateToVehicle(
    CNetworkVehicle* pNetworkVehicle, CVehicle* pVehicle, const Packets::Peds::PedDriverUpdate* pPedDriverUpdate)
{
    pVehicle->m_matrix->pos = pPedDriverUpdate->pos;
    pVehicle->m_matrix->right = pPedDriverUpdate->roll;
    pVehicle->m_matrix->up = pPedDriverUpdate->rot;
    pVehicle->m_vecMoveSpeed = pPedDriverUpdate->velocity;
    pVehicle->m_vecTurnSpeed = pPedDriverUpdate->turnSpeed;

    pVehicle->m_nPrimaryColor = pPedDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pPedDriverUpdate->color2;

    pVehicle->m_fHealth = pPedDriverUpdate->health;

    if (pNetworkVehicle->m_nPaintJob != pPedDriverUpdate->paintjob)
    {
        pVehicle->SetRemap(pPedDriverUpdate->paintjob);
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pPedDriverUpdate->bikeLean;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pVehicle;
        pPlane->m_fLandingGearStatus = pPedDriverUpdate->planeGearState;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_BMX)
    {
        CBmx* pBmx = (CBmx*)pVehicle;
        pBmx->m_fControlPedaling = pPedDriverUpdate->controlPedaling;
    }
    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        ((CAutomobile*)pVehicle)->m_wMiscComponentAngle = pPedDriverUpdate->miscComponentAngle;
    }
    pVehicle->m_eDoorLock = pPedDriverUpdate->locked;

    pVehicle->m_fGasPedal = pPedDriverUpdate->gasPedal;
    pVehicle->m_fBreakPedal = pPedDriverUpdate->breakPedal;
    pVehicle->m_fSteerAngle = pPedDriverUpdate->steerAngle;
}

PACKET_HANDLER(ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (!pNetworkPed) return;
    pNetworkPed->CancelPendingOnFoot("driver");
    if (pNetworkPed->m_bSyncing) return;

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    // private build (vehicle sync protocol): the vehicle's state, and a copy this game lost comes back where the ped's
    // syncer has it (a car an NPC drives gets no idle or player driver updates, only these)
    const int vehicleid = pPedDriverUpdate->vehicleid;
    pNetworkVehicle->RememberState(pPedDriverUpdate->pos, pPedDriverUpdate->roll, pPedDriverUpdate->rot,
        pPedDriverUpdate->color1, pPedDriverUpdate->color2);

    // private build (vehicle sync protocol review 2): a lost driver copy comes back first, and the car copy only with its
    // driver (VehicleSyncPolicy::DecidePedDriverVehicle): CWorld::ClearForRestart deletes a car copy with its NPC driver
    // copy, and the car alone came back from this update as a frozen, driverless MISSION_VEHICLE nobody removed
    CPed* pPed = pNetworkPed->GetLivePed();
    if (pPed == nullptr)
    {
        pPed = pNetworkPed->RecreateLostCopy(pPedDriverUpdate->pos);
        // the ped's model load (CStreaming::LoadAllRequestedModels): the record again, as after the weapon load below
        if (CNetworkVehicleManager::GetVehicle(vehicleid) != pNetworkVehicle) return;
    }
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle();
    switch (VehicleSyncPolicy::DecidePedDriverVehicle(pVehicle != nullptr, pPed != nullptr))
    {
        case VehicleSyncPolicy::ePedDriverVehicle::SKIP:
            return;

        case VehicleSyncPolicy::ePedDriverVehicle::APPLY_VEHICLE:
            if (pVehicle->IsVTableValid() && pVehicle->m_matrix)
            {
                ApplyDriverUpdateToVehicle(pNetworkVehicle, pVehicle, pPedDriverUpdate);
            }
            return;

        case VehicleSyncPolicy::ePedDriverVehicle::RECREATE:
            pVehicle = CNetworkVehicleManager::RecreateLostCopy(pNetworkVehicle, pPedDriverUpdate->pos,
                pPedDriverUpdate->roll, pPedDriverUpdate->rot, pPedDriverUpdate->color1, pPedDriverUpdate->color2);
            break;

        case VehicleSyncPolicy::ePedDriverVehicle::SEAT_AND_APPLY:
            break;
    }
    if (pVehicle == nullptr || !pVehicle->IsVTableValid())
    {
        return;
    }

    // private build: the car's model load in RecreateLostCopy can take the ped's clump or slot (see ApplyWeapon)
    pPed = pNetworkPed->GetLivePed();
    if (pPed == nullptr)
    {
        return;
    }

    if (pPed->m_pVehicle != pVehicle || !pPed->m_nPedFlags.bInVehicle)
    {
        pNetworkPed->WarpIntoVehicleDriver(pVehicle);
    }
    // private build (vehicle sync protocol): the record and its vehicle again after the warp (see VEHICLE_DRIVER_UPDATE)
    if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle)) return;
    if (!pNetworkPed->GetLivePed() || !pVehicle->m_matrix) return;
    ApplyDriverUpdateToVehicle(pNetworkVehicle, pVehicle, pPedDriverUpdate);
    pNetworkPed->m_vecVelocity = pPedDriverUpdate->velocity;
    pNetworkPed->m_fGasPedal = pPedDriverUpdate->gasPedal;
    pNetworkPed->m_fBreakPedal = pPedDriverUpdate->breakPedal;
    pNetworkPed->m_fSteerAngle = pPedDriverUpdate->steerAngle;

    pNetworkPed->ApplyWeaponSnapshot(pPedDriverUpdate->pedWeapon);
    // private build (vehicle sync protocol): the weapon's model may load in there (CStreaming::LoadAllRequestedModels)
    if (!CNetworkVehicleManager::StillHolds(vehicleid, pNetworkVehicle, pVehicle)) return;
    pPed = pNetworkPed->GetLivePed();
    if (!pPed) return;

    pNetworkPed->m_fHealth = pPed->m_fHealth = pPedDriverUpdate->pedHealth.iHealth;
    pPed->m_fArmour = pPedDriverUpdate->pedHealth.iArmour;
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);
    if (!pNetworkPed) return;
    pNetworkPed->CancelPendingOnFoot("passenger");
    if (pNetworkPed->m_bSyncing) return;
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);

    if (pNetworkVehicle == nullptr || pNetworkPed == nullptr)
        return;

    if (pNetworkVehicle->m_pVehicle == nullptr)
        return;

    // private build (vehicle sync protocol review 2): a lost passenger copy comes back at its vehicle; its model load may
    // destroy vehicles, so the record and its vehicle again (StillHolds)
    if (!pNetworkPed->GetLivePed())
    {
        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
        if (!pVehicle->IsVTableValid() || !pVehicle->m_matrix || !pNetworkPed->RecreateLostCopy(pVehicle->GetPosition()) ||
            !CNetworkVehicleManager::StillHolds(pPedPassengerSync->vehicleid, pNetworkVehicle, pVehicle))
        {
            return;
        }
    }

    if (!pNetworkPed->GetLivePed())
        return;

    if (!pNetworkVehicle->m_pVehicle->IsVTableValid())
        return;

    if (!pNetworkPed->m_pPed->m_nPedFlags.bInVehicle || pNetworkVehicle->m_pVehicle->m_pDriver == pNetworkPed->m_pPed)
    {
        pNetworkPed->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pPedPassengerSync->seatid);
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedPassengerSync->weaponSnapshot);
    if (!pNetworkPed->GetLivePed()) return;

    pNetworkPed->m_fHealth = pNetworkPed->m_pPed->m_fHealth = pPedPassengerSync->healthSnapshot.iHealth;
    pNetworkPed->m_pPed->m_fArmour = pPedPassengerSync->healthSnapshot.iArmour;
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);

    if (pNetworkPed && pNetworkPed->IsModelReady())
    {
        if (pNetworkPed->m_pPed->GetWeapon().m_eWeaponType != pPedShotSync->weaponType)
        {
            pNetworkPed->m_pPed->SetCurrentWeapon(pPedShotSync->weaponType);
        }
        if (!pNetworkPed->IsModelReady()) return;
        pNetworkPed->m_pPed->GetWeapon().Fire(
            pNetworkPed->m_pPed, &pPedShotSync->origin, &pPedShotSync->effect, nullptr, &pPedShotSync->target, nullptr);
    }
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay)
{
    // CChat::AddMessage("PedSay %d %d %d %d %d", pPedSay->phraseId, pPedSay->startTimeDelay, pPedSay->overrideSilence,
    // pPedSay->isForceAudible, pPedSay->isFrontEnd);

    CPed* pPed = (CPed*)pPedSay->entity.GetEntity();
    if (pPed)
    {
        // CAEPedSpeechAudioEntity::AddSayEvent
        plugin::CallMethodAndReturn<int16_t, 0x4E6550, CAEPedSpeechAudioEntity*, int, int16_t, uint32_t, float, bool,
            bool, bool>(&pPed->m_pedSpeech, AE_SPEECH_PED, pPedSay->phraseId, pPedSay->startTimeDelay, 1.0f,
            pPedSay->overrideSilence, pPedSay->isForceAudible, pPedSay->isFrontEnd);
    }
}


PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims)
{
    if (auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid))
    {
        if (!pNetworkPed->m_bSyncing)
        {
            pNetworkPed->m_bClaimOnRelease = false;
        }
    }
}
