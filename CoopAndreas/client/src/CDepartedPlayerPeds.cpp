#include "stdafx.h"
#include "CDepartedPlayerPeds.h"
#include "CDepartedPlayerPolicy.h"
#include "CSessionLifecycle.h"
#include "CPedSyncState.h"
#include <cstddef>
#include <cstring>
#include <new>

// Private build: the stand-ins for players who left during the host's mission, see CDepartedPlayerPeds.h.
//
// gta_sa.exe 1.0 US used below:
// - CPlayerPed::m_pPlayerData +0x480 (plugin-sdk CPed; ~CPlayerPed 0x6093B0 reads it at 0x6093D3) points into the ped's
//   CWorld::Players slot (0xB7CD98, CPlayerInfo 0x190 bytes, m_PlayerData +4, plugin-sdk), as the CPlayerPed constructor
//   that CREATE_PLAYER's CPlayerPed::SetupPlayerPed (0x60D790) runs sets it (gta-reversed PlayerPed.cpp). Through it
//   ~CPlayerPed releases the melee anim blocks (+0x48 and +0x4C, CAnimManager::RemoveAnimBlockRef 0x4D3FD0 at
//   0x6093FF/0x609437) and the player's group (+0x38, CPedGroups::RemoveGroup 0x5FB870 at 0x609462); it frees nothing
//   else. SetupPlayerPed builds a new ped on a slot without deleting the old one, the constructor gives it a new group
//   (CPedGroups::AddGroup) and CPlayerPedData::AllocateData (0x571880) only allocates the wanted and clothes objects that
//   are missing. So a stand-in gets a copy of its slot's data, which holds its own anim blocks and group from then on,
//   and the slot keeps neither (a player who comes back gets a new ped there). The wanted and clothes objects stay
//   shared; ~CPlayerPed doesn't free them and a stand-in never uses them (it isn't processed).
// - CWorld::Add 0x563220 leaves an entity with m_bIsStatic (+0x1C bit 0x4, test at 0x563267) out of the moving list, so
//   CWorld::Process never processes it (it walks only that list); CWorld::Remove 0x563280. A parked stand-in is in the
//   sector lists, so CWorld::ClearForRestart deletes it with the other peds before a new game or a load (gta-reversed
//   World.cpp), while the groups and anim blocks it releases still exist.
// - the deleting destructor is vtable slot 0 (0x60A9A0 for CPlayerPed), called as CNetworkPlayer::DestroyPed does.
static_assert(sizeof(CPlayerData) == 0xAC, "CPlayerData");
static_assert(sizeof(CPlayerInfo) == 0x190, "CPlayerInfo");
static_assert(offsetof(CPed, m_pPlayerData) == 0x480, "CPed::m_pPlayerData");

namespace
{
struct SStandInExtra
{
    CPedPoolReference poolReference;
    CPlayerData* pPlayerData = nullptr;  // the stand-in's own copy (m_pPlayerData), freed after its destructor
    CVector park{};
    int playerId = -1;
};

// one per player who left during the current mission; a player who returns and leaves again adds one
constexpr size_t MAX_STAND_INS = Config::MAX_SERVER_PLAYERS * 2;
using CStandIns = DepartedPlayerPolicy::CStandInList<CPlayerPed, SStandInExtra, MAX_STAND_INS>;

// built on first use (the list is used only at run time, but this keeps it independent of the static init order)
CStandIns& StandIns()
{
    static CStandIns standIns;
    return standIns;
}

// --coopdiag: stand-in records in CoopAndreas_ped_sync.log next to gta_sa.exe (at most 256 records and 1 MB)
void LogStandIn(const char* event, CPlayerPed* pPed, int playerId, const char* detail = nullptr)
{
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
        fprintf(file, "t=%u stand-in=%s player=%d ref=%d count=%u%s%s\n", GetTickCount(), event, playerId,
            pPed ? CPools::GetPedRef(pPed) : -1, static_cast<unsigned int>(StandIns().Size()), detail ? " " : "",
            detail ? detail : "");
        ++records;
    }
    fclose(file);
}

// the ped's player data lies in a CWorld::Players slot (where the CPlayerPed constructor points it)
bool IsSlotPlayerData(const CPlayerData* pPlayerData)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(pPlayerData);
    const uintptr_t first = reinterpret_cast<uintptr_t>(&CWorld::Players[0]);
    const uintptr_t end = reinterpret_cast<uintptr_t>(&CWorld::Players[Config::MAX_SERVER_PLAYERS + 2]);
    return address >= first && address < end &&
           (address - first) % sizeof(CPlayerInfo) == offsetof(CPlayerInfo, m_PlayerData);
}

// out of the world, hidden, without collision and static, at its place below where its player left, into the world again
// (sector lists only: CWorld::Add leaves a static entity out of the moving list)
void Park(CPlayerPed* pPed, const CVector& park)
{
    CWorld::Remove(pPed);
    pPed->m_bIsVisible = false;
    pPed->m_bUsesCollision = false;
    pPed->m_bIsStatic = true;
    pPed->m_bRemoveFromWorld = false;
    pPed->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    pPed->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    pPed->SetPosn(park);
    CWorld::Add(pPed);
}

// the deleting destructor (the CPlayerPed destructor hook forgets the stand-in and frees its player data)
void Destroy(CPlayerPed* pPed)
{
    CNetworkPlayer::GiveUpVehicleSeat(pPed);
    CWorld::Remove(pPed);

    uintptr_t pedPtr = reinterpret_cast<uintptr_t>(pPed);
    __asm
    {
        mov ecx, pedPtr
        mov eax, [ecx]  // vtable
        push 1
        call [eax]      // deleting destructor
    }
}

// puts back what a frame's script commands changed on a stand-in (a mission that skipped the validity check)
void Repark(CPlayerPed* pPed, const SStandInExtra& extra)
{
    DepartedPlayerPolicy::SStandInState state;
    state.bInVehicle = pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle != nullptr;
    state.bInMovingList = pPed->m_pMovingList != nullptr;
    state.bVisible = pPed->m_bIsVisible;
    state.bUsesCollision = pPed->m_bUsesCollision;
    state.bStatic = pPed->m_bIsStatic;
    state.bRemoveFromWorld = pPed->m_bRemoveFromWorld;
    state.distanceFromPark = DistanceBetweenPoints(pPed->GetPosition(), extra.park);

    const uint8_t fixes = DepartedPlayerPolicy::GetStandInFixes(state);
    if (fixes == DepartedPlayerPolicy::STANDIN_FIX_NONE)
    {
        return;
    }
    if (fixes & DepartedPlayerPolicy::STANDIN_FIX_LEAVE_VEHICLE)
    {
        CNetworkPlayer::GiveUpVehicleSeat(pPed);
        if (pPed->m_pRwClump && pPed->m_pIntelligence)
        {
            pPed->m_pIntelligence->FlushImmediately(true); // the drive task a warp into the vehicle gave it
        }
    }
    if (fixes & DepartedPlayerPolicy::STANDIN_FIX_PARK)
    {
        Park(pPed, extra.park);
    }
    LogStandIn("reparked", pPed, extra.playerId, state.bInVehicle ? "was-seated" : nullptr);
}
}  // namespace

void CDepartedPlayerPeds::OnPlayerLeaving(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
    {
        return;
    }
    CPlayerPed* pPed = pNetworkPlayer->GetReadyPed();
    if (!DepartedPlayerPolicy::ShouldKeepStandIn(CSessionLifecycle::IsHost(), CSessionLifecycle::IsLocalMissionRunning(),
            pPed != nullptr))
    {
        return;
    }

    CStandIns& standIns = StandIns();
    if (standIns.IsFull() || !IsSlotPlayerData(pPed->m_pPlayerData))
    {
        LogStandIn("not-kept", pPed, pNetworkPlayer->m_iPlayerId, standIns.IsFull() ? "list-full" : "player-data");
        return;  // the CNetworkPlayer destructor deletes the ped, as before
    }

    pPed = pNetworkPlayer->ReleasePedForStandIn();
    if (pPed == nullptr)
    {
        return;
    }

    // its own player data from now on (see the top of this file); the slot keeps neither its anim blocks nor its group
    CPlayerData* pSlotData = pPed->m_pPlayerData;
    CPlayerData* pOwnData = static_cast<CPlayerData*>(::operator new(sizeof(CPlayerData)));
    memcpy(pOwnData, pSlotData, sizeof(CPlayerData));
    pSlotData->m_nMeleeWeaponAnimReferenced = 0;
    pSlotData->m_nMeleeWeaponAnimReferencedExtra = 0;
    pPed->m_pPlayerData = pOwnData;

    // the player is gone for the host: no blip on the stand-in (CRadar::ClearBlipForEntity 0x587C60)
    CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(pPed));

    SStandInExtra extra;
    extra.poolReference.Capture(CPools::ms_pPedPool, pPed);
    extra.pPlayerData = pOwnData;
    extra.park = pPed->GetPosition();
    extra.park.z -= DepartedPlayerPolicy::PARK_DEPTH;
    extra.playerId = pNetworkPlayer->m_iPlayerId;
    Park(pPed, extra.park);
    standIns.Add(pPed, extra);
    LogStandIn("kept", pPed, extra.playerId);
}

void CDepartedPlayerPeds::Process()
{
    CStandIns& standIns = StandIns();
    if (standIns.IsEmpty())
    {
        return;
    }

    const bool bRelease = DepartedPlayerPolicy::ShouldReleaseStandIns(CSessionLifecycle::IsLocalMissionRunning());
    // from the last one: deleting a stand-in takes it out of the list (OnPedDestroying), the last entry moves into its
    // place, and that one has been handled already
    for (size_t i = standIns.Size(); i-- > 0;)
    {
        CStandIns::SEntry& entry = standIns.At(i);
        CPlayerPed* pPed = entry.pPed;
        if (!entry.extra.poolReference.Matches(CPools::ms_pPedPool, pPed) || !pPed->IsVTableValid())
        {
            // gone without the destructor hook (every CPlayerPed is deleted through it, so this shouldn't happen)
            SStandInExtra removed;
            standIns.Remove(pPed, &removed);
            LogStandIn("lost", nullptr, removed.playerId);
            FreePlayerData(removed.pPlayerData);
            continue;
        }

        if (bRelease)
        {
            LogStandIn("released", pPed, entry.extra.playerId);
            Destroy(pPed);
            if (standIns.Find(pPed) != nullptr)
            {
                // the destructor hook didn't see it (it always should): forget it, and leave its player data alone, the
                // ped may still be using it
                standIns.Remove(pPed);
            }
            continue;
        }

        Repark(pPed, entry.extra);
    }
}

void* CDepartedPlayerPeds::OnPedDestroying(CPlayerPed* pPed)
{
    CStandIns& standIns = StandIns();
    if (standIns.IsEmpty())
    {
        return nullptr;
    }
    SStandInExtra removed;
    if (!standIns.Remove(pPed, &removed))
    {
        return nullptr;
    }
    LogStandIn("deleted", pPed, removed.playerId);
    return removed.pPlayerData;
}

void CDepartedPlayerPeds::FreePlayerData(void* pPlayerData)
{
    if (pPlayerData != nullptr)
    {
        ::operator delete(pPlayerData);
    }
}

bool CDepartedPlayerPeds::IsStandIn(CPlayerPed* pPed)
{
    CStandIns& standIns = StandIns();
    return !standIns.IsEmpty() && standIns.Find(pPed) != nullptr;
}
