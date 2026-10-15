#include "stdafx.h"
#include "CNetworkPed.h"
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <Hooks/PedHooks.h>
#include <CPools.h>

CNetworkPed::CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[])
{
    if (modelId >= 290 && modelId <= 299)
        CStreaming::RequestSpecialModel(modelId, specialModelName, 0);
    else
        CStreaming::RequestModel(modelId, 0);

    CStreaming::LoadAllRequestedModels(false);

    if (pedType == PED_TYPE_COP)
    {
        switch (modelId) 
        {
        case MODEL_LAPDM1:
            modelId = COP_TYPE_LAPDM1;
            break;
        case MODEL_CSHER:
            modelId = COP_TYPE_CSHER;
            break;
        case MODEL_SWAT:
            modelId = COP_TYPE_SWAT1;
            break;
        case MODEL_FBI:
            modelId = COP_TYPE_FBI;
            break;
        case MODEL_ARMY:
            modelId = COP_TYPE_ARMY;
            break;
        }
    }

    if (pedType == PED_TYPE_COP)
    {
        m_pPed = new CCopPed((eCopType)modelId);
    }
    else if (pedType == PED_TYPE_MEDIC || pedType == PED_TYPE_FIREMAN)
    {
        m_pPed = new CEmergencyPed(pedType, modelId);
    }
    else
    {
        m_pPed = new CCivilianPed(pedType, modelId);
    }

    m_poolReference.Capture(CPools::ms_pPedPool, m_pPed);
    m_pPed->m_nCreatedBy = 2;
    m_pPed->m_pIntelligence->SetPedDecisionMakerType(-1);
    m_pPed->m_pIntelligence->SetSeeingRange(30.0);
    m_pPed->m_pIntelligence->SetHearingRange(30.0);
    m_pPed->m_pIntelligence->m_fDmRadius = 0.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 0;
    
    m_pPed->SetPosn(pos);
    m_pPed->SetOrientation(0.f, 0.f, 0.f);
    CWorld::Add(m_pPed);

    m_nPedId = pedid;
    m_nPedType = pedType;
    m_bSyncing = false;
    m_nCreatedBy = createdBy;

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;
}

CNetworkPed::~CNetworkPed()
{
    CancelPendingOnFoot("deleted");
    if (m_bSyncing)
    {
        Packets::Peds::PedRemove packet{};
        packet.pedid = m_nPedId;
        GetPacketFactory().Send(packet);
    }
    else
    {

        if (GetLivePed())
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(m_pPed));
                //CChat::AddMessage("REMOVE THE FUCKING BLIP");
            }

            if (m_pPed->m_nPedFlags.bInVehicle && IsModelReady())
            {
                plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed), 0.f, 0.f, 0.f);
            }

            if (!GetLivePed()) return;
            CWorld::Remove(m_pPed);
            //CWorld::RemoveReferencesToDeletedObject(m_pPed);
            m_pPed->Remove();
            delete m_pPed;
        }
    }
}

CNetworkPed* CNetworkPed::CreateHosted(CPed* ped)
{
    CNetworkPed* networkPed = new CNetworkPed();
    networkPed->m_pPed = ped;
    if (!networkPed->m_poolReference.Capture(CPools::ms_pPedPool, ped))
    {
        networkPed->m_pPed = nullptr;
        delete networkPed;
        return nullptr;
    }
    ped->field_54C += 5000; // m_nTimeTillWeNeedThisPed
    networkPed->m_nPedId = 0;
    networkPed->m_nCreatedBy = ped->m_nCreatedBy;
    networkPed->m_bSyncing = true;
    networkPed->m_nTempId = CNetworkPedManager::AddToTempList(networkPed);

    Packets::Peds::PedSpawn packet{};
    packet.tempid = networkPed->m_nTempId;
    packet.pedid = networkPed->m_nPedId;
    packet.modelId = static_cast<eModelID>(ped->m_nModelIndex);
    packet.pos = ped->GetPosition();
    packet.pedType = static_cast<ePedType>(ped->m_nPedType);
    packet.createdBy = static_cast<eCharCreatedBy>(ped->m_nCreatedBy);

    if (packet.modelId >= MODEL_SPECIAL01 && packet.modelId <= MODEL_SPECIAL10)
    {
        strcpy_s(packet.specialModelName, PedHooks::ms_aszLoadedSpecialModels[packet.modelId - MODEL_SPECIAL01]);
        packet.specialModelName[7] = '\0';
    }
    GetPacketFactory().Send(packet);

    return networkPed;
}

void CNetworkPed::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    if (!IsModelReady() || !vehicle || !vehicle->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    if (!IsModelReady()) return;
    m_pPed->m_pIntelligence->FlushImmediately(false);
    if (!IsModelReady()) return;

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    if (!IsModelReady() || !vehicle || !vehicle->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    if (!IsModelReady()) return;
    m_pPed->m_pIntelligence->FlushImmediately(false);
    if (!IsModelReady()) return;

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::RemoveFromVehicle(CVehicle* vehicle)
{
    if (!IsModelReady() || !vehicle || !vehicle->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
    if (!IsModelReady()) return;

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2; // 2 - normal
    }

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.ProcessPed(m_pPed);

    if (!IsModelReady()) return;
    m_pPed->m_pIntelligence->FlushImmediately(true); // create a default primary task (fix bug)
}

void CNetworkPed::ClaimOnRelease()
{
    if (m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedClaimOnRelease packet{};
    packet.pedid = m_nPedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = true;
}

void CNetworkPed::CancelClaim()
{
    if (!m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedCancelClaim packet{};
    packet.pedid = m_nPedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = false;
}

CPed* CNetworkPed::GetLivePed()
{
    if (!m_pPed) return nullptr;
    if (!m_poolReference.Matches(CPools::ms_pPedPool, m_pPed) || !m_pPed->IsVTableValid())
    {
        LogSyncDiagnostic("invalid", "pool-generation-or-vtable");
        m_pendingOnFoot.Cancel();
        m_bVisualDeferred = false;
        m_pPed = nullptr; // Never recapture a generation for this wrapper.
        return nullptr;
    }
    return m_pPed;
}

bool CNetworkPed::IsModelReady()
{
    CPed* ped = GetLivePed();
    return ped && ped->m_pRwClump && ped->m_pIntelligence;
}

void CNetworkPed::LogSyncDiagnostic(const char* phase, const char* reason)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 512) return;
    const uint32_t now = GetTickCount();
    const unsigned int phaseIndex = strcmp(phase, "deferred") == 0 ? 0 : strcmp(phase, "resumed") == 0 ? 1 :
        strcmp(phase, "invalid") == 0 ? 2 : 3;
    if (m_abLoggedSyncDiagnostic[phaseIndex] && static_cast<uint32_t>(now - m_anLastSyncDiagnostic[phaseIndex]) < 1000) return;
    m_abLoggedSyncDiagnostic[phaseIndex] = true;
    m_anLastSyncDiagnostic[phaseIndex] = now;

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) return;
    char* fileName = strrchr(path, '\\');
    if (!fileName) return;
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_ped_sync.log");
    FILE* file = fopen(path, "a+");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        const bool poolValid = m_poolReference.Matches(CPools::ms_pPedPool, m_pPed);
        CPed* ped = poolValid && m_pPed->IsVTableValid() ? m_pPed : nullptr;
        fprintf(file, "t=%u phase=%s reason=%s pedID=%d poolRef=%d poolValid=%d model=%d area=%d localArea=%d "
            "clump=%p intelligence=%p streamingPin=%d\n", now, phase, reason, m_nPedId,
            m_poolReference.GetRef(), poolValid, ped ? static_cast<int>(ped->m_nModelIndex) : -1,
            ped ? static_cast<int>(ped->m_nAreaCode) : -1, CGame::currArea,
            ped ? ped->m_pRwClump : nullptr, ped ? ped->m_pIntelligence : nullptr,
            ped ? ped->m_bStreamingDontDelete : 0);
        ++records;
    }
    fclose(file);
}

void CNetworkPed::CancelPendingOnFoot(const char* reason)
{
    if (m_pendingOnFoot.Cancel()) LogSyncDiagnostic("cancelled", reason);
    m_bVisualDeferred = false;
}

void CNetworkPed::ReceiveOnFoot(const Packets::Peds::PedOnFoot& snapshot)
{
    if (m_bSyncing) return;
    CPed* ped = GetLivePed();
    if (!ped) return;

    // These fields do not need a RenderWare object or intelligence tasks.
    ped->SetPosn(snapshot.pos);
    m_fCurrentRotation = ped->m_fCurrentRotation = snapshot.currentRotation.m_angle;
    m_fAimingRotation = ped->m_fAimingRotation = snapshot.aimingRotation.m_angle;
    m_fLookDirection = ped->m_fLookDirection = snapshot.lookDirection.m_angle;
    m_fHealth = ped->m_fHealth = snapshot.healthSnapshot.iHealth;
    ped->m_fArmour = snapshot.healthSnapshot.iArmour;
    m_vecVelocity = snapshot.velocity;
    m_nMoveState = snapshot.moveState;

    m_pendingOnFoot.Submit({snapshot.weaponSnapshot, snapshot.weaponAim, snapshot.bDucked,
        snapshot.bAiming, snapshot.fightingStyle, snapshot.pos}, GetTickCount());
    ProcessPendingOnFoot();
}

void CNetworkPed::ProcessPendingOnFoot()
{
    if (m_bSyncing) { CancelPendingOnFoot("ownership"); return; }
    const auto result = m_pendingOnFoot.Process(GetTickCount(), [this] { return GetLivePed() != nullptr; },
        [this] { return IsModelReady(); }, [this](const OnFootVisualSnapshot& snapshot) { return ApplyOnFootVisual(snapshot); });
    if (result == ePedSnapshotResult::Deferred)
    {
        if (!m_bVisualDeferred) LogSyncDiagnostic("deferred", "not-ready");
        m_bVisualDeferred = true;
    }
    else if (result == ePedSnapshotResult::Applied)
    {
        if (m_bVisualDeferred) LogSyncDiagnostic("resumed", "ready");
        m_bVisualDeferred = false;
    }
    else if (result == ePedSnapshotResult::Expired)
    {
        LogSyncDiagnostic("cancelled", "expired");
        m_bVisualDeferred = false;
    }
    else if (result == ePedSnapshotResult::Invalid) m_bVisualDeferred = false;
}

bool CNetworkPed::ApplyOnFootVisual(const OnFootVisualSnapshot& snapshot)
{
    if (!IsModelReady()) return false;
    CVehicle* vehicle = m_pPed->m_pVehicle;
    if (vehicle && m_pPed->m_nPedFlags.bInVehicle)
    {
        if (!vehicle->IsVTableValid()) return false;
        RemoveFromVehicle(vehicle);
    }
    if (!IsModelReady()) return false;
    // Vehicle exit tasks can overwrite the immediately synced position. Keep
    // the latest onfoot location for immediate application and deferred retries.
    m_pPed->SetPosn(snapshot.position);
    if (!ApplyWeaponSnapshot(snapshot.weapon) || !IsModelReady()) return false;
    if (CUtil::IsDucked(m_pPed) != snapshot.ducked)
    {
        CTaskSimpleDuckToggle task(snapshot.ducked);
        task.ProcessPed(m_pPed);
    }
    if (!IsModelReady()) return false;
    CTaskSimpleUseGun* useGun = m_pPed->m_pIntelligence->GetTaskUseGun();
    if (snapshot.aiming)
    {
        if (!useGun)
        {
            auto* task = new CTaskSimpleUseGun(m_pPed->m_pTargetedObject, CVector(0.f, 0.f, 0.f), 1, 1, false);
            m_pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(task, TASK_SECONDARY_ATTACK);
        }
        if (!IsModelReady()) return false;
        useGun = m_pPed->m_pIntelligence->GetTaskUseGun();
        if (useGun) useGun->m_vecTarget = snapshot.aim;
    }
    else if (useGun)
    {
        // MakeAbortable removes stance animations and requires a live clump.
        useGun->MakeAbortable(m_pPed, ABORT_PRIORITY_URGENT, nullptr);
    }
    if (!IsModelReady()) return false;
    m_pPed->m_nFightingStyle = snapshot.fightingStyle;
    return true;
}

bool CNetworkPed::ApplyWeapon(unsigned char weapon, unsigned short ammo, bool select)
{
    if (!IsModelReady()) return false;
    const bool different = m_pPed->GetWeapon().m_eWeaponType != weapon;
    if (!different && m_pPed->GetWeapon().m_nAmmoInClip == ammo) return true;

    // Load before changing weapon state. Streaming can remove the ped's clump
    // (or delete/reuse its pool slot), so no stored weapon references cross it.
    if (weapon != WEAPON_UNARMED)
    {
        const int model = CUtil::GetWeaponModelById(weapon);
        if (model >= 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED | eStreamingFlags::MISSION_REQUIRED |
                eStreamingFlags::KEEP_IN_MEMORY | eStreamingFlags::PRIORITY_REQUEST);
            CStreaming::LoadAllRequestedModels(true);
            if (!IsModelReady() || CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED) return false;
        }
    }
    if (!IsModelReady()) return false;
    if (different && select) m_pPed->ClearWeapons();
    if (!IsModelReady()) return false;
    if (weapon == WEAPON_SATCHEL_CHARGE && !ApplyWeapon(WEAPON_DETONATOR, 1, false)) return false;
    if (!IsModelReady()) return false;
    if (weapon != WEAPON_UNARMED)
    {
        const auto type = static_cast<eWeaponType>(weapon);
        const int slot = m_pPed->GetWeaponSlot(type);
        if (m_pPed->m_aWeapons[slot].m_nTotalAmmo == 0)
            m_pPed->GiveWeapon(type, CUtil::IsMeleeWeapon(weapon) ? 1 : 99999, false);
        if (!IsModelReady()) return false;
        m_pPed->m_aWeapons[slot].m_nAmmoInClip = ammo;
    }
    if (different && select) m_pPed->SetCurrentWeapon(static_cast<eWeaponType>(weapon));
    return IsModelReady();
}

bool CNetworkPed::ApplyWeaponSnapshot(const Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (!ApplyWeapon(weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo, true)) return false;
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
    return true;
}
