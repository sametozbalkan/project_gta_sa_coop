#include "stdafx.h"
#include "CTrainSyncPolicy.h"
#include <CTaskSimpleCarSetPedOut.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <intrin.h>

// private build (remote player ped recovery): the game can destroy a network player's CPlayerPed on its own. A
// vehicle's destructor flags its driver and passengers (CVehicle::~CVehicle 0x6E2B40 calls
// FlagToDestroyWhenNextProcessed, CPed's is 0x5E7B70, through the vtable at 0x6E2BC6 and 0x6E2BF0), and the next
// CWorld::Process deletes every flagged ped that isn't FindPlayerPed() (0x56860C; CPopulation::RemovePed 0x610F20 is
// called at 0x568631). ~CPlayerPed (0x6093B0) doesn't clear CWorld::Players[n].m_pPed and ~CPed (0x5E8620) deletes
// the intelligence without clearing m_pIntelligence, so the old pointer kept "working": the ped was gone from the
// world (invisible, no name tag), the radar marker still read it, and the next task set on it crashed in
// RpAnimBlendClumpGetAssociation (0x4D68BA) on its NULL clump. PlayerHooks replaces CPlayerPed's
// FlagToDestroyWhenNextProcessed, so a network player's ped only leaves the vehicle (KeepFlaggedPed, FinishKeptPed).
// Should the game delete one anyway: every CPlayerPed is deleted through the deleting destructor 0x60A9A0, whose only
// call of ~CPlayerPed (0x60A9A3) PlayerHooks already redirects: it reports the ped (OnPedDestroyed), the pool
// reference catches anything else (GetLivePed), and ProcessPedRecovery() recreates the ped on the game thread.
// Tasks, anims, weapons and clothes go only to a ped with a clump and intelligence.

CNetworkPlayer::~CNetworkPlayer()
{
    if (m_pPed == nullptr)
        return;

    this->DestroyPed();
}

CNetworkPlayer::CNetworkPlayer(int id, CVector position)
{
    m_iPlayerId = id;

    m_pPedClothesDesc.SetTextureAndModel("VEST", "VEST", 0);
    m_pPedClothesDesc.SetTextureAndModel("JEANSDENIM", "JEANS", 2);
    m_pPedClothesDesc.SetTextureAndModel("SNEAKERBINCBLK", "SNEAKER", 3);
    m_pPedClothesDesc.SetTextureAndModel("PLAYER_FACE", "HEAD", 1);

    SetLastKnownState(position); // private build: where a recreated ped starts until the first sync arrives
    CreatePed(id, position);
}

void CNetworkPlayer::CreatePed(int id, CVector position)
{
    // private build: a new ped never continues the move of a kept one out of a destroyed vehicle
    m_bKeptPending = false;
    m_bKeptFlushed = false;

    // private build: CREATE_PLAYER (handler 0x4668DC) constructs the CPlayerPed (CPlayerPed::SetupPlayerPed 0x60D790)
    // without checking that the ped and intelligence pools have room
    if (CPools::ms_pPedPool->GetNoOfFreeSpaces() == 0 || CPools::ms_pPedIntelligencePool->GetNoOfFreeSpaces() == 0)
    {
        m_pPed = nullptr;
        LogPedDiagnostic("recreate-failed", "pool-full");
        return;
    }

    unsigned int actorId = 0;
    int playerId = id + 2;

    plugin::Command<Commands::CREATE_PLAYER>(playerId, position.x, position.y, position.z, &actorId);
    plugin::Command<Commands::GET_PLAYER_CHAR>(playerId, &actorId);

    m_pPed = (CPlayerPed*)CPools::GetPed(actorId);

    // private build: remember the pool generation, so a slot the game freed or reused is never taken for this ped
    if (m_pPed == nullptr || !m_poolReference.Capture(CPools::ms_pPedPool, m_pPed))
    {
        m_pPed = nullptr;
        return;
    }

    m_pPed->SetOrientation(0.0f, 0.0f, 0.0f);

    // set player immunies, he doesn't care about the pain now
    Command<Commands::SET_CHAR_PROOFS>(actorId, 0, 1, 1, 0, 0);

    *m_pPed->m_pPlayerData->m_pPedClothesDesc = m_pPedClothesDesc;

    // private build: CClothes::RebuildPlayer (0x5A82C0) starts with the anims of the current clump
    if (m_pPed->m_pRwClump)
    {
        CClothes::RebuildPlayer(m_pPed, false);
        m_bClothesPending = false;
    }
    else
    {
        m_bClothesPending = true;
    }

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;
}

void CNetworkPlayer::DestroyPed()
{
    // private build: never touch a ped the game already destroyed, and warp a live one out of its vehicle only with
    // a clump and intelligence (the warp aborts the drive task, which plays anims)
    CPlayerPed* pPed = GetLivePed();
    if (pPed == nullptr)
    {
        m_pPed = nullptr;
        return;
    }

    if (pPed->m_pVehicle && IsModelReady())
    {
        plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(pPed), 0.f, 0.f, 0.f);

        pPed = GetLivePed();
        if (pPed == nullptr)
        {
            return;
        }
    }
    else
    {
        // private build: a seated ped without a clump gives its seat back too, before it goes: the vehicle may stay in
        // this game (a player who leaves: the host takes its vehicle over)
        GiveUpVehicleSeat(pPed);
    }

    uintptr_t pedPtr = (uintptr_t)pPed;
    m_bDestroyingPed = true;
    if (pPed->IsVTableValid())
    {
        CWorld::Remove(pPed);

        // destroy the ped
        __asm
        {
			mov ecx, pedPtr
			mov ebx, [ecx]  // vtable addr
			push 1  // unused arg
			call[ebx]  // call destructor
        }
    }
    m_bDestroyingPed = false;

    // private build: the deleting destructor (0x60A9A0) leaves CWorld::Players[n].m_pPed set; OnPedDestroyed clears
    // it too, but only for a player still in CNetworkPlayerManager (not one being deleted after a disconnect)
    for (int i = 2; i < Config::MAX_SERVER_PLAYERS + 2; i++)
    {
        if ((uintptr_t)CWorld::Players[i].m_pPed == pedPtr)
        {
            CWorld::Players[i].m_pPed = nullptr;
        }
    }
    m_pPed = nullptr;
    m_poolReference = CPedPoolReference();
}

void CNetworkPlayer::Respawn()
{
    if (m_pPed)
    {
        this->DestroyPed();
    }

    this->CreatePed(m_iPlayerId, m_onFootSnapshotInterpolated.vecPos);
}

int CNetworkPlayer::GetInternalId()  // most used for CWorld::PlayerInFocus
{
    // private build: no live ped, no player slot (a NULL m_pPed matched the first empty CWorld::Players entry)
    CPlayerPed* pPed = GetLivePed();
    if (pPed == nullptr)
    {
        return -1;
    }

    byte playerNumber = 0;

    for (; playerNumber < Config::MAX_SERVER_PLAYERS + 2; playerNumber++)
    {
        if (pPed == CWorld::Players[playerNumber].m_pPed)
        {
            return playerNumber;
        }
    }

    return -1;
}

std::string CNetworkPlayer::GetName()
{
    if (m_Name[0] == '\0')
    {
        char buffer[32 + 1];
        sprintf(buffer, "player %d", m_iPlayerId);
        return buffer;
    }

    return m_Name;
}

char CNetworkPlayer::GetWeaponSkill(eWeaponType weaponType)
{
    if (weaponType < WEAPON_PISTOL || weaponType > WEAPON_TEC9)
        return 1;

    eStats weaponStatId = plugin::CallAndReturn<eStats, 0x743CD0>(weaponType);  // CWeaponInfo::GetSkillStatIndex
    int statSyncId = CStatsSync::GetSyncIdByInternal(weaponStatId);
    float weaponStat = m_stats[weaponStatId];

    if (CWeaponInfo::GetWeaponInfo(weaponType, 2)->m_nReqStatLevel <= weaponStat)
        return 2;

    return CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nReqStatLevel <= weaponStat;
}

void CNetworkPlayer::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    // private build: car tasks only for a ped with a clump and intelligence, checked again after each engine call
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

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    // private build: see WarpIntoVehicleDriver
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

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::EnterVehiclePassenger(CVehicle* vehicle, int seatid)
{
    // private build: see WarpIntoVehicleDriver
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

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = new CTaskComplexEnterCarAsPassenger(vehicle, doorId, false);
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(task, 3, false);
}

void CNetworkPlayer::RemoveFromVehicle(CVehicle* vehicle)
{
    // private build: see WarpIntoVehicleDriver
    if (!IsModelReady() || !vehicle || !vehicle->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
    if (!IsModelReady()) return;

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::HandleTask(Packets::Players::SetPlayerTask& packet)
{
    // private build: tasks only for a ped with a clump and intelligence
    if (!IsModelReady())
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("HandleTask %d toggle %d", packet.taskType, packet.toggle);
#endif

    if (TrainSyncPolicy::ShouldApplyOnFootPosition(m_pPed->m_pAttachedTo != nullptr))
    {
        m_pPed->SetPosn(packet.vecPos);
    }
    m_pPed->m_fCurrentRotation = packet.currentRotation.m_angle;
    m_pPed->m_fAimingRotation = packet.aimingRotation.m_angle;

    CTask* activeTask = m_pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();
    eTaskType activeTaskType = activeTask ? activeTask->GetTaskType() : TASK_NONE;
    switch ((eTaskType)packet.taskType)
    {
        case eTaskType::TASK_COMPLEX_JUMP:
        {
            m_pPed->ClearWeaponTarget();
            if (activeTask && activeTaskType == packet.taskType)  // if the jump task is active
            {
                return;
            }

            if (m_pPed->m_pIntelligence->GetTaskDuck(false))
            {
                CTaskSimpleDuckToggle(0).ProcessPed(m_pPed);
            }

            m_pPed->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexJump(0), 3, false);

            break;
        }
        case eTaskType::TASK_SIMPLE_DUCK:
        {
            m_bRequestedDuckTask = true;
            //if (packet.toggle)
            //{
            //    m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);

            //    CTaskSimpleFight* pFightingTask = m_pPed->m_pIntelligence->GetTaskFighting();
            //    if (pFightingTask)
            //    {
            //        // abort fighting task (0x6879C2)
            //        plugin::CallMethodAndReturn<bool, 0x61C5E0, CTaskSimpleFight*, CEntity*, int8_t>(pFightingTask, nullptr, 18);
            //    }
            //}
            //else
            //{
            //    m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
            //}
            break;
        }
    }
}

void CNetworkPlayer::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    m_onFootSnapshotInterpolated.weaponSnapshot = weaponSnapshot;

    // private build: weapons (models, anims) only for a ped with a clump and intelligence
    if (!IsModelReady())
    {
        return;
    }

    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);

    // private build: GiveWeaponByPacket can load the weapon model synchronously
    if (CPlayerPed* pPed = GetReadyPed())
    {
        pPed->m_aWeapons[pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
    }
}

// private build (remote player ped recovery) ------------------------------------------------------------------------

CPlayerPed* CNetworkPlayer::GetLivePed()
{
    if (m_pPed == nullptr)
    {
        return nullptr;
    }

    if (!m_poolReference.Matches(CPools::ms_pPedPool, m_pPed) || !m_pPed->IsVTableValid())
    {
        // OnPedDestroyed normally forgets the ped first; this catches what the destructor hook didn't see
        LogPedDiagnostic("invalid", "pool-generation-or-vtable");
        for (int i = 2; i < Config::MAX_SERVER_PLAYERS + 2; i++)
        {
            if (CWorld::Players[i].m_pPed == m_pPed)
            {
                CWorld::Players[i].m_pPed = nullptr;
            }
        }
        m_pPed = nullptr;
        m_poolReference = CPedPoolReference();
        return nullptr;
    }

    return m_pPed;
}

bool CNetworkPlayer::IsModelReady()
{
    CPlayerPed* pPed = GetLivePed();
    return pPed && pPed->m_pRwClump && pPed->m_pIntelligence;
}

CPlayerPed* CNetworkPlayer::GetOrRecoverPed(const CVector* position)
{
    if (position)
    {
        SetLastKnownState(*position);
    }

    if (CPlayerPed* pPed = GetReadyPed())
    {
        return pPed;
    }

    ProcessPedRecovery();
    return GetReadyPed();
}

void CNetworkPlayer::SetLastKnownState(const CVector& position, int areaCode)
{
    m_vecLastPosition = position;
    m_bHasLastPosition = true;
    if (areaCode >= 0)
    {
        m_nLastAreaCode = static_cast<uint8_t>(areaCode);
    }
}

void CNetworkPlayer::SetClothes(const CPedClothesDesc& clothesDesc)
{
    if (&clothesDesc != &m_pPedClothesDesc)
    {
        m_pPedClothesDesc = clothesDesc;
    }

    // CClothes::RebuildPlayer (0x5A82C0) starts with the anims of the current clump, so a ped without one gets the
    // clothes once it is ready again (ProcessPedRecovery) instead of never
    CPlayerPed* pPed = GetReadyPed();
    if (pPed == nullptr || pPed->m_pPlayerData == nullptr || pPed->m_pPlayerData->m_pPedClothesDesc == nullptr)
    {
        if (!m_bClothesPending)
        {
            LogPedDiagnostic("deferred", "clothes");
        }
        m_bClothesPending = true;
        return;
    }

    *pPed->m_pPlayerData->m_pPedClothesDesc = m_pPedClothesDesc;
    CClothes::RebuildPlayer(pPed, false);
    m_bClothesPending = false;
}

void CNetworkPlayer::ProcessPedRecovery()
{
    // a ped kept from a destroyed vehicle goes back into the world on foot (never called inside CWorld::Process)
    FinishKeptPed(false);

    CPlayerPed* pPed = GetLivePed();
    if (pPed && pPed->m_pRwClump && pPed->m_pIntelligence)
    {
        m_nPedProblem = 0;
        m_recoveryThrottle.Reset();
        m_vecLastPosition = pPed->GetPosition();
        m_fLastHeading = pPed->GetHeading();
        m_nLastAreaCode = pPed->m_nAreaCode;
        m_bHasLastPosition = true;

        if (pPed->m_bRemoveFromWorld)
        {
            // flagged by FlagToDestroyWhenNextProcessed (0x5E7B70) although PlayerHooks keeps network player peds
            // (hook not installed?): the next CWorld::Process deletes it
            LogPedDiagnostic("flagged", "remove-from-world");
        }
        CheckHidden(pPed);

        if (m_bClothesPending)
        {
            SetClothes(m_pPedClothesDesc);
        }
        return;
    }

    // missing (destroyed, or never created) or broken (no clump or intelligence)
    const uint8_t nProblem = pPed ? 2 : 1;
    if (m_nPedProblem != nProblem)
    {
        LogPedDiagnostic(pPed ? "clumpless" : "missing", pPed ? (pPed->m_pRwClump ? "no-intelligence" : "no-clump") : "no-ped");
        m_nPedProblem = nProblem;
    }

    if (!m_recoveryThrottle.ShouldAttempt(GetTickCount()))
    {
        return;
    }

    RecreatePed(pPed ? "clumpless" : "missing");
}

void CNetworkPlayer::RecreatePed(const char* reason)
{
    if (m_pPed)
    {
        DestroyPed();
    }

    // Respawn-like: model 0 is loaded by CREATE_PLAYER (0x4668E7), clothes come from m_pPedClothesDesc and
    // m_bStreamingDontDelete is set by CreatePed; the next sync packet puts it back into its vehicle
    CVector position = m_bHasLastPosition ? m_vecLastPosition : CVector(m_onFootSnapshotInterpolated.vecPos);
    CreatePed(m_iPlayerId, position);

    CPlayerPed* pPed = GetLivePed();
    if (pPed == nullptr)
    {
        LogPedDiagnostic("recreate-failed", reason);
        return;
    }

    ApplyLastKnownState(pPed);
    m_nHiddenReason = 0;
    LogPedDiagnostic("recreated", reason);
}

void CNetworkPlayer::ApplyLastKnownState(CPlayerPed* pPed)
{
    pPed->SetHeading(m_fLastHeading);
    pPed->m_fCurrentRotation = m_fLastHeading;
    pPed->m_fAimingRotation = m_fLastHeading;
    pPed->m_nAreaCode = m_nLastAreaCode;
    pPed->UpdateRwMatrix();
}

void CNetworkPlayer::CheckHidden(CPlayerPed* pPed)
{
    // 0 shown, 1 invisible flag, 2 out of the world (on foot, not static and without a moving list link: CWorld::Remove
    // takes it off the list, CWorld::Add puts it back), 3 another area (interior)
    uint8_t nReason = 0;
    if (!pPed->m_bIsVisible)
        nReason = 1;
    else if (!pPed->m_nPedFlags.bInVehicle && !pPed->m_bIsStatic && pPed->m_pMovingList == nullptr)
        nReason = 2;
    else if (pPed->m_nAreaCode != CGame::currArea)
        nReason = 3;

    if (nReason == m_nHiddenReason)
    {
        return;
    }

    static const char* const s_aszReasons[] = {"shown", "invisible-flag", "not-in-world", "other-area"};
    m_nHiddenReason = nReason;
    LogPedDiagnostic("hidden", s_aszReasons[nReason]);
}

// Return addresses into gta_sa.exe's code (.text 0x401000-0x856FFF) on the current stack, like the crash log's
// unreliable backtrace: the game code that destroyed a ped is among the first ones.
static void DescribeGameCallers(char* buffer, size_t size)
{
    buffer[0] = '\0';
    const uintptr_t* pSlot = reinterpret_cast<const uintptr_t*>(_AddressOfReturnAddress());
    const uintptr_t stackBase = __readfsdword(0x04);
    size_t used = 0;
    int found = 0;
    for (int i = 0; i < 512 && found < 8; i++, pSlot++)
    {
        if (reinterpret_cast<uintptr_t>(pSlot + 1) > stackBase)
        {
            break;
        }

        const uintptr_t value = *pSlot;
        if (value < 0x401006 || value >= 0x857000)
        {
            continue;
        }

        // a call instruction ends at a return address: E8 rel32, FF /2 with modrm only, disp8 or disp32
        const uint8_t* pCode = reinterpret_cast<const uint8_t*>(value);
        if (pCode[-5] != 0xE8 && !(pCode[-2] == 0xFF && (pCode[-1] & 0x38) == 0x10) &&
            !(pCode[-3] == 0xFF && (pCode[-2] & 0xF8) == 0x50) && !(pCode[-6] == 0xFF && ((pCode[-5] & 0xF8) == 0x90 || pCode[-5] == 0x15)))
        {
            continue;
        }

        const int written = snprintf(buffer + used, size - used, " 0x%X", value);
        if (written <= 0 || used + written >= size)
        {
            break;
        }
        used += written;
        found++;
    }
}

void CNetworkPlayer::OnPedDestroyed(CPlayerPed* pPed)
{
    if (pPed == nullptr || pPed != m_pPed)
    {
        return;
    }

    // still intact here: the destructor hook runs before ~CPlayerPed
    m_vecLastPosition = pPed->GetPosition();
    m_fLastHeading = pPed->GetHeading();
    m_nLastAreaCode = pPed->m_nAreaCode;
    m_bHasLastPosition = true;

    if (!m_bDestroyingPed)
    {
        char szCallers[136] = "callers:";
        DescribeGameCallers(szCallers + 8, sizeof(szCallers) - 8);
        LogPedDiagnostic("destroyed", pPed->m_bRemoveFromWorld ? "flagged-by-game" : "deleted-by-game", szCallers);
    }

    for (int i = 2; i < Config::MAX_SERVER_PLAYERS + 2; i++)
    {
        if (CWorld::Players[i].m_pPed == pPed)
        {
            CWorld::Players[i].m_pPed = nullptr;
        }
    }
    m_pPed = nullptr;
    m_poolReference = CPedPoolReference();
}

// Called by the CPlayerPed::FlagToDestroyWhenNextProcessed hook (PlayerHooks.cpp) for this player's ped instead of
// the original, after which the next CWorld::Process would delete it. The callers that reach a player are a
// vehicle's destructor (CVehicle::~CVehicle 0x6E2B40) and CVehicle::DestroyVehicleAndDriverAndPassengers (0x6D2250,
// right before it deletes the vehicle); CAutomobile::ProcessHarvester (0x6A9680) skips players. The vehicle is half
// destroyed by then (its RW object is gone), so only the original's unlinking runs here: no task, no anim, nothing
// that reads the vehicle later. FinishKeptPed does the rest.
void CNetworkPlayer::KeepFlaggedPed(CPlayerPed* pPed)
{
    if (pPed == nullptr || pPed != m_pPed)
    {
        return;
    }

    char szDetail[160];
    CVehicle* pVehicle = pPed->m_nPedFlags.bInVehicle ? pPed->m_pVehicle : nullptr;
    if (pVehicle == nullptr)
    {
        // the original only sets m_bRemoveFromWorld for a ped that isn't seated (getting in or out); its tasks lose the
        // vehicle through their entity references when the vehicle is deleted, as the local player's do
        strcpy_s(szDetail, "callers:");
        DescribeGameCallers(szDetail + 8, sizeof(szDetail) - 8);
        LogPedDiagnostic("kept", "not-seated", szDetail);
        return;
    }

    // private build (vehicle sync protocol): the ~CVehicle hook lets the record go before this runs, so the id comes from
    // the vehicle being destroyed (CNetworkVehicleManager::GetIdForLog)
    const int nWritten = sprintf_s(szDetail, "vehicle=%d callers:", CNetworkVehicleManager::GetIdForLog(pVehicle));
    const size_t nUsed = nWritten > 0 ? static_cast<size_t>(nWritten) : 0;
    DescribeGameCallers(szDetail + nUsed, sizeof(szDetail) - nUsed);
    m_vecKeptPosition = pVehicle->GetPosition();

    // CPed::FlagToDestroyWhenNextProcessed (0x5E7B70) unlinks the ped: the vehicle's driver reference or
    // CVehicle::RemovePassenger (0x6D1610), bInVehicle, and m_pVehicle with its entity reference. It also sets
    // m_bRemoveFromWorld and, for a mission ped (CREATE_PLAYER makes this one PED_MISSION at 0x46692B), an inlined
    // CPed::SetPedState(PEDSTATE_DEAD) (0x5E4500) that clears the ped's blip when bClearRadarBlipOnDeath is set. The
    // flag is cleared again, the blip stays, and the state becomes PEDSTATE_IDLE as in CTaskSimpleCarSetPedOut.
    const bool bClearRadarBlipOnDeath = pPed->m_nPedFlags.bClearRadarBlipOnDeath;
    pPed->m_nPedFlags.bClearRadarBlipOnDeath = false;
    plugin::CallMethod<0x5E7B70, CPed*>(pPed);
    pPed->m_nPedFlags.bClearRadarBlipOnDeath = bClearRadarBlipOnDeath;
    pPed->m_bRemoveFromWorld = false;
    pPed->m_ePedState = PEDSTATE_IDLE;

    m_bKeptPending = true;
    m_bKeptFlushed = false;
    LogPedDiagnostic("kept", "vehicle-destroyed", szDetail);
}

// The rest of the move out of a vehicle that was destroyed under this player's ped (KeepFlaggedPed), once the
// vehicle's destructor is over and the ped's tasks have lost their references to it (~CEntity ResolveReferences).
// First the ped's side of CTaskSimpleCarSetPedOut::ProcessPed (0x647D10), at the start of the ped's next
// ProcessControl or here, whichever comes first. Then, only outside CWorld::Process (which walks its moving entity
// list while it calls ProcessControl), CPed::Teleport (0x5E4110: FlushImmediately for a player, CWorld::Remove,
// SetPosn, CWorld::Add, no speed) to where the vehicle was, which also puts the ped back into the world if it was out.
void CNetworkPlayer::FinishKeptPed(bool bInsideWorldProcess)
{
    if (!m_bKeptPending)
    {
        return;
    }

    CPlayerPed* pPed = GetReadyPed();
    if (pPed == nullptr || pPed->m_nPedFlags.bInVehicle)
    {
        // gone or broken (ProcessPedRecovery recreates it on foot), or a sync packet has seated it again
        m_bKeptPending = false;
        return;
    }

    if (!m_bKeptFlushed)
    {
        m_bKeptFlushed = true;
        pPed->m_bUsesCollision = true; // cleared while seated (CTaskSimpleCarSetPedInAsDriver::ProcessPed 0x64B950)
        CCarEnterExit::RemoveCarSitAnim(pPed);
        pPed->RestartNonPartialAnims();
        // the drive task now ends without touching its vehicle (CTaskSimpleCarDrive::MakeAbortable 0x63C670 sets
        // the ped out only while it is in one), and a player gets CTaskSimplePlayerOnFoot as its default task again
        pPed->m_pIntelligence->FlushImmediately(true);
        pPed->ReplaceWeaponWhenExitingVehicle();
        pPed->m_ePedState = PEDSTATE_IDLE;

        pPed = GetReadyPed();
        if (pPed == nullptr)
        {
            m_bKeptPending = false;
            return;
        }
    }

    if (bInsideWorldProcess)
    {
        return;
    }

    const float fHeading = pPed->GetHeading();
    pPed->Teleport(m_vecKeptPosition, false);
    pPed->SetHeading(fHeading);
    pPed->m_fCurrentRotation = fHeading;
    pPed->m_fAimingRotation = fHeading;
    pPed->UpdateRwMatrix();
    m_bKeptPending = false;
}

// private build (another player leaving during the host's mission) -------------------------------------------------

// The ped gives its seat back without the vehicle going away: a player who leaves takes nothing with them, the host
// takes the vehicle over, so it must not keep a driver or passenger that is about to be deleted or parked as a stand-in.
// The game's own unlink, CPed::FlagToDestroyWhenNextProcessed (0x5E7B70, called directly, not through the CPlayerPed
// vtable slot PlayerHooks replaces): for the vehicle's driver it drops the driver reference and leaves the vehicle
// STATUS_ABANDONED for a player (0x5E7BB3..0x5E7BEE), for a passenger it calls CVehicle::RemovePassenger (0x6D1610), then
// it clears bInVehicle and m_pVehicle. No task and no animation: it also works without a clump. Its deletion flag and,
// for a mission ped (CREATE_PLAYER makes every player ped one), its PEDSTATE_DEAD with the blip it clears are undone,
// as in KeepFlaggedPed.
void CNetworkPlayer::GiveUpVehicleSeat(CPlayerPed* pPed)
{
    if (pPed == nullptr || !pPed->m_nPedFlags.bInVehicle || pPed->m_pVehicle == nullptr)
    {
        return;
    }

    const bool bClearRadarBlipOnDeath = pPed->m_nPedFlags.bClearRadarBlipOnDeath;
    const bool bRemoveFromWorld = pPed->m_bRemoveFromWorld;
    pPed->m_nPedFlags.bClearRadarBlipOnDeath = false;
    plugin::CallMethod<0x5E7B70, CPed*>(pPed);
    pPed->m_nPedFlags.bClearRadarBlipOnDeath = bClearRadarBlipOnDeath;
    pPed->m_bRemoveFromWorld = bRemoveFromWorld;
    pPed->m_ePedState = PEDSTATE_IDLE;
}

// Hands this player's ped over to CDepartedPlayerPeds as a stand-in (only a ped with a clump and intelligence): out of
// its vehicle with the ped side of a car exit (as FinishKeptPed), without the tasks it had (one getting into a car
// still holds the door), out of its CWorld::Players slot, and forgotten here, so the destructor of this object leaves it
// alone. The slot is free for the next player with this id.
CPlayerPed* CNetworkPlayer::ReleasePedForStandIn()
{
    CPlayerPed* pPed = GetReadyPed();
    if (pPed == nullptr)
    {
        return nullptr;
    }

    m_bKeptPending = false;
    const bool bWasSeated = pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle;
    if (bWasSeated)
    {
        GiveUpVehicleSeat(pPed);
        pPed->m_bUsesCollision = true; // cleared while seated (CTaskSimpleCarSetPedInAsDriver::ProcessPed 0x64B950)
        CCarEnterExit::RemoveCarSitAnim(pPed);
        pPed->RestartNonPartialAnims();
    }
    // a drive task now ends without touching its vehicle (CTaskSimpleCarDrive::MakeAbortable 0x63C670 sets the ped out
    // only while it is in one); a player gets CTaskSimplePlayerOnFoot as its default task again
    pPed->m_pIntelligence->FlushImmediately(true);
    pPed = GetReadyPed();
    if (pPed == nullptr)
    {
        return nullptr;
    }
    if (bWasSeated)
    {
        pPed->ReplaceWeaponWhenExitingVehicle();
        pPed->m_ePedState = PEDSTATE_IDLE;
    }

    for (int i = 2; i < Config::MAX_SERVER_PLAYERS + 2; i++)
    {
        if (CWorld::Players[i].m_pPed == pPed)
        {
            CWorld::Players[i].m_pPed = nullptr;
        }
    }
    LogPedDiagnostic("stand-in", "left-during-host-mission");
    m_pPed = nullptr;
    m_poolReference = CPedPoolReference();
    return pPed;
}

void CNetworkPlayer::LogPedDiagnostic(const char* phase, const char* reason, const char* detail)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 512) return;

    // one record per phase and second; "hidden" is written only when the reason changes (CheckHidden)
    static const char* const s_aszPhases[] = {"destroyed", "invalid", "missing", "clumpless", "recreated",
        "recreate-failed", "flagged", "deferred", "taken-out", "kept", "stand-in", "hidden"};
    static_assert(ARRAY_SIZE(s_aszPhases) == ARRAY_SIZE(m_abLoggedPedDiagnostic), "one slot per phase");
    unsigned int phaseIndex = ARRAY_SIZE(s_aszPhases) - 1;
    for (unsigned int i = 0; i < ARRAY_SIZE(s_aszPhases); i++)
    {
        if (strcmp(phase, s_aszPhases[i]) == 0)
        {
            phaseIndex = i;
            break;
        }
    }
    const uint32_t now = GetTickCount();
    if (strcmp(phase, "hidden") != 0 && m_abLoggedPedDiagnostic[phaseIndex] &&
        static_cast<uint32_t>(now - m_anLastPedDiagnostic[phaseIndex]) < 1000)
    {
        return;
    }
    m_abLoggedPedDiagnostic[phaseIndex] = true;
    m_anLastPedDiagnostic[phaseIndex] = now;

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
        // never through GetLivePed: no side effects, and the destructor hook logs a ped that is still in its slot
        const bool poolValid = m_pPed && m_poolReference.Matches(CPools::ms_pPedPool, m_pPed);
        CPlayerPed* pPed = poolValid && m_pPed->IsVTableValid() ? m_pPed : nullptr;
        const CVector position = pPed ? pPed->GetPosition() : m_vecLastPosition;
        fprintf(file, "t=%u player=%d phase=%s reason=%s poolRef=%d poolValid=%d clump=%p intelligence=%p "
            "inWorld=%d visible=%d area=%d localArea=%d inVehicle=%d vehicle=%p pedState=%d pos=%.1f,%.1f,%.1f%s%s\n",
            now, m_iPlayerId, phase, reason, m_poolReference.GetRef(), poolValid, pPed ? pPed->m_pRwClump : nullptr,
            pPed ? pPed->m_pIntelligence : nullptr, pPed ? static_cast<int>(pPed->m_pMovingList != nullptr) : -1,
            pPed ? static_cast<int>(pPed->m_bIsVisible) : -1, pPed ? static_cast<int>(pPed->m_nAreaCode) : static_cast<int>(m_nLastAreaCode),
            CGame::currArea, pPed ? static_cast<int>(pPed->m_nPedFlags.bInVehicle) : -1, pPed ? pPed->m_pVehicle : nullptr,
            pPed ? static_cast<int>(pPed->m_ePedState) : -1, position.x, position.y, position.z,
            detail ? " " : "", detail ? detail : "");
        ++records;
    }
    fclose(file);
}
