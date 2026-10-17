#include "stdafx.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include <CTimer.h>
#include <cstdarg>

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;
CNetworkPed* CNetworkPedManager::m_apTempPeds[255];

// private build (vehicle sync protocol review 2): --coopdiag counters, written by the game thread, read by CDiagLog's
// thread (plain counters, as the vehicle sync's)
namespace
{
struct SPedDiagCounters
{
    unsigned records = 0;  // refreshed by every sweep (RemoveHostedUnused)
    unsigned syncing = 0;
    unsigned lost = 0;
    unsigned recreated = 0;
    unsigned restarts = 0;
    unsigned restartRemoved = 0;
    unsigned restartDropped = 0;
    unsigned spawnsKept = 0;
    unsigned spawnsReplaced = 0;
};
SPedDiagCounters ms_pedDiag;
}  // namespace

CNetworkPed* CNetworkPedManager::GetPed(int pedid)
{
    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_nPedId == pedid)
        {
            return m_pPeds[i];
        }
    }
    return nullptr;
}

CNetworkPed* CNetworkPedManager::GetPed(CEntity* entity)
{
    if (entity == nullptr)
        return nullptr;

    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->GetLivePed() == entity)
        {
            return m_pPeds[i];
        }
    }

    return nullptr;
}

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    CNetworkPedManager::m_pPeds.push_back(ped);
}

void CNetworkPedManager::Remove(CNetworkPed* ped)
{
    auto it = std::find(m_pPeds.begin(), m_pPeds.end(), ped);
    if (it != m_pPeds.end())
    {
        m_pPeds.erase(it);
    }
}

void CNetworkPedManager::Update()
{
    CNetworkPedManager::RemoveHostedUnused();

    for (CNetworkPed* pNetworkPed : m_pPeds)
    {
        if (!pNetworkPed->m_bSyncing)
            continue;

        CPed* pPed = pNetworkPed->GetLivePed();
        if (!pPed || !pNetworkPed->IsModelReady())
            continue;

        CVehicle* pVehicle = pPed->m_pVehicle;
        if (pVehicle && !pVehicle->IsVTableValid()) continue;
        CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;

        if (pNetworkVehicle && pPed->m_nPedFlags.bInVehicle)
        {
            bool isDriver = (pVehicle->m_pDriver == pPed);

            if (isDriver)
            {
                Packets::Peds::PedDriverUpdate packet{};

                packet.vehicleSubType = static_cast<eVehicleType>(pVehicle->m_nVehicleSubType);

                packet.pedid = pNetworkPed->m_nPedId;
                packet.vehicleid = pNetworkVehicle->m_nVehicleId;

                if (!pVehicle->IsVTableValid() || !pVehicle->m_matrix) continue;
                packet.pos = pVehicle->m_matrix->pos;
                packet.roll = pVehicle->m_matrix->right;
                packet.rot = pVehicle->m_matrix->up;
                packet.velocity = pVehicle->m_vecMoveSpeed;
                packet.turnSpeed = pVehicle->m_vecTurnSpeed;

                packet.pedHealth.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
                packet.pedHealth.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

                CWeapon& weapon = pPed->GetWeapon();
                packet.pedWeapon.iWeaponType = weapon.m_eWeaponType;
                packet.pedWeapon.iWeaponState = weapon.m_nState;
                packet.pedWeapon.nAmmo = weapon.m_nAmmoInClip;

                packet.color1 = pVehicle->m_nPrimaryColor;
                packet.color2 = pVehicle->m_nSecondaryColor;

                packet.health = pVehicle->m_fHealth;

                packet.paintjob = pVehicle->GetRemapIndex();

                if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
                {
                    CBike* pBike = (CBike*)pVehicle;
                    packet.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
                }
                if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
                {
                    CPlane* pPlane = (CPlane*)pVehicle;
                    packet.planeGearState = pPlane->m_fLandingGearStatus;
                }
                if (pVehicle->m_nVehicleSubType == eVehicleType::VEHICLE_BMX)
                {
                    CBmx* pBmx = (CBmx*)pVehicle;
                    packet.controlPedaling = pBmx->m_fControlPedaling;
                }
                if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
                {
                    CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
                    packet.bikeLean = pAutomobile->m_wMiscComponentAngle;
                }
                packet.locked = pVehicle->m_eDoorLock;
                packet.gasPedal = pVehicle->m_fGasPedal;
                packet.breakPedal = pVehicle->m_fBreakPedal;
                packet.steerAngle = pVehicle->m_fSteerAngle;

                GetPacketFactory().Send(packet);
            }
            else
            {
                Packets::Peds::PedPassengerSync packet{};
                packet.pedid = pNetworkPed->m_nPedId;
                packet.vehicleid = pNetworkVehicle->m_nVehicleId;

                packet.healthSnapshot.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
                packet.healthSnapshot.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

                CWeapon& weapon = pPed->GetWeapon();
                packet.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
                packet.weaponSnapshot.iWeaponState = weapon.m_nState;
                packet.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

                for (int i = 0; i < pNetworkVehicle->m_pVehicle->m_nMaxPassengers; i++)
                {
                    if (pNetworkVehicle->m_pVehicle->m_apPassengers[i] == pNetworkPed->m_pPed)
                    {
                        packet.seatid = i;
                        break;
                    }
                }
                GetPacketFactory().Send(packet);
            }
        }
        else
        {
            Packets::Peds::PedOnFoot packet{};

            packet.pedid = pNetworkPed->m_nPedId;
            packet.pos = pPed->GetPosition();
            packet.velocity = pPed->m_vecMoveSpeed;

            packet.healthSnapshot.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
            packet.healthSnapshot.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

            CWeapon& weapon = pPed->GetWeapon();
            packet.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
            packet.weaponSnapshot.iWeaponState = weapon.m_nState;
            packet.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

            packet.aimingRotation = pPed->m_fAimingRotation;
            packet.currentRotation = pPed->m_fCurrentRotation;
            packet.lookDirection = pPed->m_fLookDirection;
            packet.moveState = (eMoveState)pPed->m_nMoveState;

            packet.bDucked = CUtil::IsDucked(pPed);

            CTaskSimpleUseGun* useGun = pPed->m_pIntelligence->GetTaskUseGun();

            if (useGun)
            {
                packet.bAiming = true;
                packet.weaponAim = useGun->m_pTarget && (useGun->m_vecTarget.x == 0.f || useGun->m_vecTarget.y == 0.f)
                                       ? useGun->m_pTarget->GetPosition()
                                       : useGun->m_vecTarget;
            }

            packet.fightingStyle = pPed->m_nFightingStyle;

            GetPacketFactory().Send(packet);
        }
    }
}

void CNetworkPedManager::Process()
{
    for (auto networkPed : m_pPeds)
    {
        if (networkPed->m_bSyncing)
            continue;

        networkPed->ProcessPendingOnFoot();
        CPed* ped = networkPed->GetLivePed();

        if (ped == nullptr)
            continue;

        if (!ped->m_nPedFlags.bInVehicle)
        {
            ped->m_fAimingRotation = networkPed->m_fAimingRotation;
            ped->m_fCurrentRotation = networkPed->m_fCurrentRotation;
            ped->m_fLookDirection = networkPed->m_fLookDirection;
        }
    }
}

void CNetworkPedManager::AssignHost()
{
    /*for (auto networkPed : m_pPeds)
    {
        CPed* ped = networkPed->m_pPed;

        if (ped)
        {
            ped->SetCharCreatedBy(networkPed->m_nCreatedBy);

            CTaskComplexWander* task = plugin::CallAndReturn<CTaskComplexWander*, 0x673D00>(ped); //
    GetWanderTaskByPedType ped->m_pIntelligence->m_TaskMgr.SetTask(task, 0, false);
        }
    }*/
}

unsigned char CNetworkPedManager::AddToTempList(CNetworkPed* networkPed)
{
    for (unsigned char i = 0; i < 255; i++)
    {
        if (m_apTempPeds[i] == nullptr)
        {
            m_apTempPeds[i] = networkPed;
            return i;
        }
    }

    return 255;
}

void CNetworkPedManager::RemoveHostedUnused()
{
    for (auto it = CNetworkPedManager::m_pPeds.begin(); it != CNetworkPedManager::m_pPeds.end();)
    {
        if ((*it)->m_bSyncing)
        {
            if (!(*it)->GetLivePed())
            {
                delete *it;
                it = m_pPeds.erase(it);
                continue;
            }
        }
        ++it;
    }

    // private build (--coopdiag): the counts of the ped-sync: line
    unsigned syncing = 0;
    unsigned lost = 0;
    for (CNetworkPed* pNetworkPed : m_pPeds)
    {
        syncing += pNetworkPed->m_bSyncing;
        lost += pNetworkPed->m_bCopyLost;
    }
    ms_pedDiag.records = static_cast<unsigned>(m_pPeds.size());
    ms_pedDiag.syncing = syncing;
    ms_pedDiag.lost = lost;
}

// private build (vehicle sync protocol review 2) ------------------------------------------------------------------------

void CNetworkPedManager::OnGameRestartCleared()
{
    // CWorld::ClearForRestart deleted every ped: the records go before anything else is handled, so the snapshot's new
    // copies never meet a stale record with their id (GetPed finds the first one)
    std::vector<CNetworkPed*> records;
    records.swap(m_pPeds);
    unsigned removed = 0;
    unsigned dropped = 0;
    for (CNetworkPed* pNetworkPed : records)
    {
        pNetworkPed->ForgetPedForRestart();
        if (PedSyncPolicy::DecideRestartRecord(pNetworkPed->m_bSyncing) == PedSyncPolicy::eRestartRecord::SEND_REMOVE)
        {
            removed++;
        }
        else
        {
            dropped++;
        }
        delete pNetworkPed;  // a ped this game synced: ~CNetworkPed sends PED_REMOVE
    }
    // the temp records (spawned, PED_CONFIRM still on its way) let go of their peds too; the confirmation then puts them
    // in the list and the sweep sends PED_REMOVE
    for (CNetworkPed* pTemp : m_apTempPeds)
    {
        if (pTemp != nullptr)
        {
            pTemp->ForgetPedForRestart();
        }
    }

    // after the removals, on their channel: the server sends this game the peds the other games sync, as to a joiner
    const bool bAsked = CNetwork::m_bAuthenticated;
    if (bAsked)
    {
        Packets::Players::PlayerGameRestarted packet{};
        GetPacketFactory().Send(packet);
    }

    ms_pedDiag.restarts++;
    ms_pedDiag.restartRemoved += removed;
    ms_pedDiag.restartDropped += dropped;
    LogEvent("restart-clear", -1, "removed=%u dropped=%u snapshot-asked=%d", removed, dropped, bAsked);
}

bool CNetworkPedManager::PrepareSpawn(int pedid)
{
    CNetworkPed* pExisting = GetPed(pedid);
    switch (PedSyncPolicy::DecideSpawn(pExisting != nullptr, pExisting != nullptr && pExisting->GetLivePed() != nullptr))
    {
        case PedSyncPolicy::eSpawn::KEEP_EXISTING:
            ms_pedDiag.spawnsKept++;
            LogEvent("spawn-kept", pedid, "syncing=%d", pExisting->m_bSyncing);
            return false;

        case PedSyncPolicy::eSpawn::REPLACE:
            // no PED_REMOVE: the server sends the spawn because another game syncs this ped
            ms_pedDiag.spawnsReplaced++;
            LogEvent("spawn-replaces", pedid, "syncing=%d lost=%d", pExisting->m_bSyncing, pExisting->m_bCopyLost);
            pExisting->m_bSyncing = false;
            Remove(pExisting);
            delete pExisting;
            return true;

        case PedSyncPolicy::eSpawn::CREATE:
        default:
            return true;
    }
}

void CNetworkPedManager::OnCopyRecreated()
{
    ms_pedDiag.recreated++;
}

void CNetworkPedManager::LogEvent(const char* event, int pedid, const char* format, ...)
{
    // --coopdiag: CoopAndreas_ped_sync.log, next to the records of CNetworkPed and CNetworkPlayer (1 MB, 256 records of
    // these; restarts and duplicate spawns are rare)
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 256)
    {
        return;
    }
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    {
        return;
    }
    char* fileName = strrchr(path, '\\');
    if (!fileName)
    {
        return;
    }
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_ped_sync.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char detail[256] = "";
        if (format != nullptr)
        {
            va_list args;
            va_start(args, format);
            std::vsnprintf(detail, sizeof(detail), format, args);
            va_end(args);
        }
        fprintf(file, "t=%u frame=%u event=%s pedID=%d %s records=%u host=%d\n", GetTickCount(), CTimer::m_FrameCounter,
            event, pedid, detail, static_cast<unsigned>(m_pPeds.size()), CLocalPlayer::m_bIsHost);
        ++records;
    }
    fclose(file);
}

void CNetworkPedManager::WriteDiagSummary(FILE* file)
{
    // --coopdiag: CDiagLog's thread; only counters the game thread keeps
    const SPedDiagCounters& c = ms_pedDiag;
    fprintf(file,
        "  ped-sync: records=%u syncing=%u lost=%u recreated=%u restarts=%u restartRemoved=%u restartDropped=%u "
        "spawnsKept=%u spawnsReplaced=%u\n",
        c.records, c.syncing, c.lost, c.recreated, c.restarts, c.restartRemoved, c.restartDropped, c.spawnsKept,
        c.spawnsReplaced);
}
