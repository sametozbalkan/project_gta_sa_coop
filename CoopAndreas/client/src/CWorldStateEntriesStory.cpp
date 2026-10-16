#include "stdafx.h"
#include "CWorldStateEntries.h"
#include <CWanted.h>
#include <algorithm>

// Private build: the world-state registry's single-value story entries, see CWorldStateEntries.h. Their payload
// formats are part of the layout hashes (network/world_state.h).

using namespace WorldStatePolicy;

namespace
{
constexpr uint32_t CITY_UNLOCK_LAYOUT = Fnv1a32Text("CityUnlock:stat181:u8");
constexpr uint32_t MAX_WANTED_LEVEL_LAYOUT = Fnv1a32Text("MaxWantedLevel:level-u8,chaos-cap-u32");

// CWanted::SetMaximumWantedLevel takes 0..6 and ignores anything else
constexpr uint8_t MAX_WANTED_LEVEL = 6;
}  // namespace

// ---- 0: city unlock ----
//
// STAT_CITY_UNLOCKED is int stat 181 (plugin-sdk and gta-reversed eStats.h). The scripts write it with SET_INT_STAT
// (0629, its handler at 0x492188 calls CStats::SetStatValue 0x55A070): MAIN and INTRO set 0 on a new game, LA1FIN2
// (The Green Sabre) 1, SYN7 (Yay Ka-Boom-Boom) 2, CASIN10 (Saint Mark's Bistro) 3 and FINALEC (End of the Line) 4.
// gta_sa.exe 1.0 US reads it only through CStats::GetStatValue (0x558E40, `push 0B5h` before each call; nothing reads its
// slot 0xB790F4 directly):
// - CGameLogic::SetPlayerWantedLevelForForbiddenTerritories (0x441770): 4 stars in San Fierro while it is 0 and in
//   Las Venturas while it is 0 or 1
// - CRestart::FindClosestHospitalRestartPoint (0x460850) and FindClosestPoliceRestartPoint (0x460A50)
// - CPlayerPed::MakeThisPedJoinOurGroup (0x60C840): no gang recruits while it is 1 or 2
// - CGameLogic::CalcDistanceToForbiddenTrainCrossing (0x4418E0), CTrain::IsNextStationAllowed (0x6F7260) and
//   CTrain::SkipToNextAllowedStation (0x6F72F0)
// - the radio's DJ lines (CAERadioTrackManager::CheckForMissionStatsChanges 0x4E8410) and
//   CGangWars::StartOffensiveGangWar (0x446050)
// Blob: the value clamped to 0..255, as UPDATE_CITY_UNLOCKED carried it until 0.3.103.

bool CWorldStateCityUnlock::Capture(Blob& blob)
{
    const int value = static_cast<int>(CStats::GetStatValue(STAT_CITY_UNLOCKED));
    BeginBlob(blob, {0, CITY_UNLOCK_LAYOUT});
    blob.push_back(static_cast<uint8_t>(std::clamp(value, 0, static_cast<int>(UINT8_MAX))));
    return true;
}

void CWorldStateCityUnlock::Apply(const Blob& desired, const Blob&)
{
    CStats::SetStatValue(STAT_CITY_UNLOCKED, static_cast<float>(desired[HEADER_SIZE]));
}

// ---- 1: maximum wanted level ----
//
// CWanted::MaximumWantedLevel (0x8CDEE4) and the chaos cap that goes with it, MaximumChaosLevel (0x8CDEE8;
// plugin-sdk CWanted.cpp names it nMaximumWantedLevel, gta-reversed Wanted.h MaximumChaosLevel). CWanted::
// SetMaximumWantedLevel(int) (0x561E70, plugin-sdk) is cdecl: the 01F0 handler pushes ScriptParams[0] and calls it at
// 0x47DEAA, then `add esp,4`. Its 1.0 US bytes (dumpbin): a level above 6 returns at once; 0..6 jump through the table
// at 0x561F10 and write both globals: 0/0, 1/115, 2/365, 3/875, 4/1800, 5/3500, 6/6900. CWanted::
// InitialiseStaticVariables (0x561C70) sets 6/9200, the .data values at start-up (tools/pe_peek.py: 0x8CDEE4 = 6,
// 0x8CDEE8 = 0x23F0), and a save load writes both from the save's simple variables (gta-reversed
// CSimpleVariablesSaveStructure::Extract), so the cap travels with the level.
// Who sets it: MAIN 6, then 4 at a new game (every game); MOB_CAT 5 (host only, the call after the Badlands strand);
// DESERT4 6; missions set 0 and restore the value they saved. The engine clamps a player's chaos to the cap in
// CWanted::UpdateWantedLevel (0x561C90), so a lower cap takes the stars above it away.
// Blob: the level (clamped to 0..255) and the chaos cap.

bool CWorldStateMaxWantedLevel::Capture(Blob& blob)
{
    BeginBlob(blob, {0, MAX_WANTED_LEVEL_LAYOUT});
    blob.push_back(static_cast<uint8_t>(std::min<unsigned int>(CWanted::MaximumWantedLevel, UINT8_MAX)));
    PutU32(blob, CWanted::nMaximumWantedLevel);
    return true;
}

void CWorldStateMaxWantedLevel::Apply(const Blob& desired, const Blob& local)
{
    const uint8_t level = desired[HEADER_SIZE];
    const uint32_t chaosCap = GetU32(desired.data() + HEADER_SIZE + 1);
    if (level > MAX_WANTED_LEVEL)
    {
        // not a level the engine takes; the registry logs that this entry doesn't converge
        return;
    }
    if (local[HEADER_SIZE] != level)
    {
        CWanted::SetMaximumWantedLevel(level);
    }
    // a cap that isn't the level's own (the start-up 9200, a loaded save's) is written the way a save load writes it
    if (CWanted::nMaximumWantedLevel != chaosCap)
    {
        CWanted::nMaximumWantedLevel = chaosCap;
    }
}
