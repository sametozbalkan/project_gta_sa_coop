#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CTaskSimpleCarSetPedOut.h>

// private build (remote player ped recovery): a vehicle deleted for the network takes no player with it.
// CVehicle::~CVehicle (0x6E2B40) flags its driver and passengers with FlagToDestroyWhenNextProcessed (virtual calls at
// 0x6E2BC6 and 0x6E2BF0; CPed's is 0x5E7B70), and the next CWorld::Process deletes every flagged ped except
// FindPlayerPed() (0x56860C; the others go to CPopulation::RemovePed 0x610F20 at 0x568631) and only removes the local
// player from the world. A network player still seated in our copy of a vehicle its syncer removed (VEHICLE_REMOVE,
// or a VEHICLE_SPAWN that reuses the id) was destroyed behind CNetworkPlayer's back: invisible from then on, and the
// next task given to it crashed (0x4D68BA). Players are warped out before the vehicle is deleted; a network player's
// ped still seated in any vehicle being destroyed is kept by PlayerHooks (CNetworkPlayer::KeepFlaggedPed).
static void TakePlayersOutOfVehicle(CVehicle* pVehicle, int vehicleid)
{
    if (pVehicle == nullptr || !pVehicle->IsVTableValid())
    {
        return;
    }

    CPed* apOccupants[1 + ARRAY_SIZE(pVehicle->m_apPassengers)]{};
    unsigned int nOccupants = 0;
    if (pVehicle->m_pDriver)
    {
        apOccupants[nOccupants++] = pVehicle->m_pDriver;
    }
    for (CPed* pPassenger : pVehicle->m_apPassengers)
    {
        if (pPassenger)
        {
            apOccupants[nOccupants++] = pPassenger;
        }
    }

    for (unsigned int i = 0; i < nOccupants; i++)
    {
        CPed* pPed = apOccupants[i];
        if (!pPed->m_nPedFlags.bInVehicle || pPed->m_pVehicle != pVehicle)
        {
            continue;
        }

        if (pPed == FindPlayerPed(0))
        {
            // the first step of WARP_CHAR_FROM_CAR_TO_COORD (handler 0x482EA3, FlushImmediately at 0x482F04): aborting
            // the drive task at once warps the ped out (CTaskSimpleCarDrive::MakeAbortable 0x63C670 runs
            // CTaskSimpleCarSetPedOut). Not the command itself: it clears space at the destination, which could delete
            // this very vehicle before its owner does.
            if (pPed->m_pRwClump && pPed->m_pIntelligence)
            {
                pPed->m_pIntelligence->FlushImmediately(true);
                if (pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle == pVehicle)
                {
                    auto task = CTaskSimpleCarSetPedOut(pVehicle, 1, false);
                    task.m_bWarpingOutOfCar = true;
                    task.ProcessPed(pPed);
                }
            }
        }
        else if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed))
        {
            char szDetail[32];
            sprintf_s(szDetail, "vehicle=%d", vehicleid);
            pNetworkPlayer->RemoveFromVehicle(pVehicle);
            pNetworkPlayer->LogPedDiagnostic("taken-out", "vehicle-deleted-for-network", szDetail);
        }
    }
}

CNetworkVehicle::CNetworkVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(vehicleid))
    {
        if (vehicle->m_pVehicle && vehicle->m_pVehicle->IsVTableValid())
        {
            TakePlayersOutOfVehicle(vehicle->m_pVehicle, vehicleid); // private build
            CWorld::Remove(vehicle->m_pVehicle);
            delete vehicle->m_pVehicle;
        }
        CNetworkVehicleManager::Remove(vehicle);
    }

    m_nVehicleId = vehicleid;
    m_bSyncing = false;
    m_nTempId = 255;
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;

    if (!CreateVehicle(vehicleid, modelid, pos, rotation, color1, color2))
        return;

}

bool CNetworkVehicle::CreateVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2)
{
    unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
    CStreaming::RequestModel(modelid, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    if (!(oldFlags & GAME_REQUIRED))
    {
        CStreaming::SetModelIsDeletable(modelid);
        CStreaming::SetModelTxdIsDeletable(modelid);
    }

    switch (((CVehicleModelInfo*)CModelInfo::ms_modelInfoPtrs[modelid])->m_nVehicleType)
    {
    case VEHICLE_MTRUCK:
        m_pVehicle = new CMonsterTruck(modelid, MISSION_VEHICLE); break;

    case VEHICLE_QUAD:
        m_pVehicle = new CQuadBike(modelid, MISSION_VEHICLE); break;

    case VEHICLE_HELI:
        m_pVehicle = new CHeli(modelid, MISSION_VEHICLE); break;

    case VEHICLE_PLANE:
        m_pVehicle = new CPlane(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BIKE:
        m_pVehicle = new CBike(modelid, MISSION_VEHICLE);
        ((CBike*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_BMX:
        m_pVehicle = new CBmx(modelid, MISSION_VEHICLE);
        ((CBmx*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_TRAILER:
        m_pVehicle = new CTrailer(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BOAT:
        m_pVehicle = new CBoat(modelid, MISSION_VEHICLE); break;

    case VEHICLE_TRAIN:
        return false;

    default:
        m_pVehicle = new CAutomobile(modelid, MISSION_VEHICLE, true); break;
    }

    if (!m_pVehicle)
        return false;

    m_pVehicle->SetPosn(pos);
    m_pVehicle->SetOrientation(0.0f, 0.0f, rotation);
    m_pVehicle->m_nStatus = 4;
    m_pVehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
    m_pVehicle->m_nPrimaryColor = color1;
    m_pVehicle->m_nSecondaryColor = color2;
    CWorld::Add(m_pVehicle);

    return true;
}

CNetworkVehicle::~CNetworkVehicle()
{
    if (m_bSyncing)
    {
        Packets::Vehicles::VehicleRemove vehicleRemovePacket{};
        vehicleRemovePacket.vehicleid = m_nVehicleId;
        GetPacketFactory().Send(vehicleRemovePacket);
    }
    else
    {
        if (m_pVehicle && m_pVehicle->IsVTableValid())
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(m_pVehicle));
            }
            TakePlayersOutOfVehicle(m_pVehicle, m_nVehicleId); // private build
            CWorld::Remove(m_pVehicle);
            CWorld::RemoveReferencesToDeletedObject(m_pVehicle); // ?
            delete m_pVehicle;
        }
    }
}

bool CNetworkVehicle::HasDriver()
{
    if (m_pVehicle == nullptr)
        return false;

    return m_pVehicle->m_pDriver != nullptr;
}

CNetworkVehicle* CNetworkVehicle::CreateHosted(CVehicle* vehicle)
{
    vehicle->m_nTimeTillWeNeedThisCar += 5000;

    CNetworkVehicle* networkVehicle = new CNetworkVehicle();
    networkVehicle->m_pVehicle = vehicle;
    networkVehicle->m_nVehicleId = -1;
    networkVehicle->m_bSyncing = true;
    networkVehicle->m_nModelId = vehicle->m_nModelIndex;
    networkVehicle->m_nPaintJob = (char)vehicle->GetRemapIndex();
    networkVehicle->m_nTempId = CNetworkVehicleManager::AddToTempList(networkVehicle);
    networkVehicle->m_nCreatedBy = vehicle->m_nCreatedBy;

    Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
    vehicleSpawnPacket.vehicleid = 0;
    vehicleSpawnPacket.tempid = networkVehicle->m_nTempId;
    vehicleSpawnPacket.modelid = vehicle->m_nModelIndex;
    vehicleSpawnPacket.pos = vehicle->m_matrix->pos;
    vehicleSpawnPacket.rot = vehicle->GetHeading();
    vehicleSpawnPacket.color1 = vehicle->m_nPrimaryColor;
    vehicleSpawnPacket.color2 = vehicle->m_nSecondaryColor;
    vehicleSpawnPacket.createdBy = (eVehicleCreatedBy)vehicle->m_nCreatedBy;
    GetPacketFactory().Send(vehicleSpawnPacket);

    return networkVehicle;
}