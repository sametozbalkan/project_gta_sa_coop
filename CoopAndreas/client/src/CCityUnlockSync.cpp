#include "stdafx.h"
#include "CCityUnlockSync.h"
#include <algorithm>

// Private build: city unlock sync, see CCityUnlockSync.h.
//
// STAT_CITY_UNLOCKED is int stat 181 (plugin-sdk and gta-reversed eStats.h). The scripts write it with SET_INT_STAT
// (0629, its handler at 0x492188 calls CStats::SetStatValue 0x55A070): MAIN and INTRO set 0 on a new game, LA1FIN2
// (The Green Sabre) 1, SYN7 (Yay Ka-Boom-Boom) 2, CASIN10 (Saint Mark's Bistro) 3 and FINALEC (End of the Line) 4.
// gta_sa.exe 1.0 US reads it only through CStats::GetStatValue (0x558E40, `push 0B5h` before each call; nothing
// reads its slot 0xB790F4 directly):
// - CGameLogic::SetPlayerWantedLevelForForbiddenTerritories (0x441770): 4 stars in San Fierro while it is 0 and in
//   Las Venturas while it is 0 or 1
// - CRestart::FindClosestHospitalRestartPoint (0x460850) and FindClosestPoliceRestartPoint (0x460A50)
// - CPlayerPed::MakeThisPedJoinOurGroup (0x60C840): no gang recruits while it is 1 or 2
// - CGameLogic::CalcDistanceToForbiddenTrainCrossing (0x4418E0), CTrain::IsNextStationAllowed (0x6F7260) and
//   CTrain::SkipToNextAllowedStation (0x6F72F0)
// - the radio's DJ lines (CAERadioTrackManager::CheckForMissionStatsChanges 0x4E8410) and
//   CGangWars::StartOffensiveGangWar (0x446050)

// host: the value sent last, -1 = none yet
static int ms_nSentValue = -1;

// the other players: the host's value, -1 = not received yet
static int ms_nHostValue = -1;

static int GetLocalValue()
{
    return static_cast<int>(CStats::GetStatValue(STAT_CITY_UNLOCKED));
}

static void ApplyHostValue()
{
    if (ms_nHostValue >= 0 && GetLocalValue() != ms_nHostValue)
    {
        CStats::SetStatValue(STAT_CITY_UNLOCKED, static_cast<float>(ms_nHostValue));
    }
}

void CCityUnlockSync::Process()
{
    if (!CNetwork::m_bConnected)
    {
        ms_nSentValue = -1;
        ms_nHostValue = -1;
        return;
    }

    if (!CNetwork::m_bAuthenticated)
    {
        return;
    }

    if (CLocalPlayer::m_bIsHost)
    {
        ms_nHostValue = -1;

        if (GetLocalValue() != ms_nSentValue)
        {
            SyncCurrentState();
        }
        return;
    }

    ms_nSentValue = -1;

    // this game's own scripts reset it on a new game (MAIN, INTRO), which may run after the host's value arrived
    ApplyHostValue();
}

void CCityUnlockSync::SyncCurrentState()
{
    int value = GetLocalValue();

    Packets::World::UpdateCityUnlocked packet{};
    packet.cityUnlocked = static_cast<uint8_t>(std::clamp(value, 0, static_cast<int>(UINT8_MAX)));
    GetPacketFactory().Send(packet);

    ms_nSentValue = value;
}

void CCityUnlockSync::HandlePacket(Packets::World::UpdateCityUnlocked* pUpdateCityUnlocked)
{
    if (CLocalPlayer::m_bIsHost)
    {
        return;
    }

    ms_nHostValue = pUpdateCityUnlocked->cityUnlocked;
    ApplyHostValue();
}
