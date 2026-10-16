#include "stdafx.h"
#include "CSessionLifecycle.h"
#include "CSessionPolicy.h"
#include "COpCodeSync.h"
#include "CNetworkCheckpoint.h"
#include "CNetworkEntityBlip.h"
#include "CServerTime.h"
#include <CTimer.h>
#include <cstdarg>
#include <vector>

// Private build: the co-op session lifecycle, see CSessionLifecycle.h.
//
// gta_sa.exe 1.0 US used below (plugin-sdk names):
// - CRunningScript::m_bIsMission +0xDC (LOAD_AND_LAUNCH_MISSION_INTERNAL 0417 sets it at 0x489A05),
//   CTheScripts::pActiveScripts 0xA8B42C (CTheScripts::Process walks it at 0x46A1D1), m_pNext +0.
// - CDraw::FadeValue (0xC3EFAB) is the fade drawn over the screen: CCamera::ProcessFade (0x50B5D0) writes it at
//   0x50B6C3, the render path reads it at 0x53E6A2.
// - The commands the host's mission end replays on the other games, read from the 1.0 US handlers (dumpbin of the
//   CommandHandlerTable 0x8A6168 dispatch, tools/pe_peek.py for its tables; function names from gta-reversed):
//   CLEAR_CUTSCENE 02EA (case 0x48078A in ProcessCommands700To799 0x47FA30) only calls CCutsceneMgr::DeleteCutsceneData
//   0x4D5ED0; RESTORE_CAMERA_JUMPCUT 02EB (case 0x48079E) calls CCamera::RestoreWithJumpCut 0x50BAB0 on TheCamera
//   0xB6F028; SET_CAMERA_BEHIND_PLAYER 0373 (case 0x48331B in ProcessCommands800To899 0x481300) calls 0x50BD40 on
//   TheCamera; SET_PLAYER_CONTROL 01B4 (case 0x47D3C8 in ProcessCommands400To499 0x47D210) calls
//   CPlayerInfo::MakePlayerSafe 0x56E870 (state 0 -> safe) with radius 10.0 on CWorld::Players[param 0].
static_assert(offsetof(CRunningScript, m_bIsMission) == 0xDC, "CRunningScript::m_bIsMission");

namespace
{
// the host's last packets on the other channels wait INTERP_BUFFER_DELAY_MS (50 ms) in the packet buffer after its
// PLAYER_DISCONNECTED was handled; this leaves room for them and a late resend
constexpr uint32_t HOST_LOSS_DELAY_MS = 250;
// frames to wait for a host cutscene that is still loading before it is cleared
constexpr int CUTSCENE_CLEAR_FRAMES = 30;
constexpr unsigned int CUTSCENE_NOT_LOADED = 0;
constexpr unsigned int CUTSCENE_LOADED = 2;

std::vector<CSessionLifecycle::Subscriber> ms_subscribers;
CSessionHostState ms_hostState;
CHostMissionState ms_hostMission;
CHostLossTimer ms_hostLoss;
CGameRestartDetector ms_restart;
CFreePlayTimer ms_freePlay;
bool ms_bSessionOpen = false;      // connected (or authenticated) since the last DISCONNECTED
uint32_t ms_nFreePlayMs = 0;       // GetFreePlayDuration, once per frame
int ms_nCutsceneClearFrames = 0;   // a host cutscene still to clear after the host left
unsigned int ms_nEvents = 0;       // --coopdiag summary
char ms_szLoadedSave[MAX_PATH]{};  // from OnGameLoaded until SAVE_LOADED
bool ms_bTakeoverPending = false;  // this game became the host with players in; their join snapshots wait for the clock
void (*ms_pfnTakeoverSnapshot)() = nullptr;  // OnHostAssigned's takeoverSnapshot

const char* ToString(eSessionEvent type)
{
    switch (type)
    {
        case eSessionEvent::AUTHENTICATED: return "authenticated";
        case eSessionEvent::DISCONNECTED: return "disconnected";
        case eSessionEvent::HOST_CHANGED: return "host-changed";
        case eSessionEvent::PLAYER_JOINED: return "player-joined";
        case eSessionEvent::PLAYER_LEFT: return "player-left";
        case eSessionEvent::GAME_RESTARTED: return "game-restarted";
        case eSessionEvent::SAVE_LOADED: return "save-loaded";
        case eSessionEvent::HOST_MISSION_STARTED: return "host-mission-started";
        case eSessionEvent::HOST_MISSION_ENDED: return "host-mission-ended";
    }
    return "?";
}

// --coopdiag: CoopAndreas_session.log next to gta_sa.exe, at most 512 records and 1 MB (as CoopAndreas_ped_sync.log)
void LogLine(const char* format, ...)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 512)
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
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_session.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        fprintf(file, "t=%u frame=%u %s myId=%d hostId=%d hostMission=%d\n", GetTickCount(), CTimer::m_FrameCounter,
            text, ms_hostState.GetMyId(), ms_hostState.GetHostId(), ms_hostMission.IsOnMission());
        ++records;
    }
    fclose(file);
}

void LogEvent(const SSessionEvent& event)
{
    const char* name = ToString(event.type);
    switch (event.type)
    {
        case eSessionEvent::HOST_CHANGED:
            LogLine("event=%s host=%d->%d local=%d->%d", name, event.oldHostId, event.newHostId, event.bWasLocal,
                event.bIsLocal);
            break;
        case eSessionEvent::PLAYER_JOINED:
            LogLine("event=%s player=%d takeover=%d", name, event.playerId, event.bTakeover);
            break;
        case eSessionEvent::PLAYER_LEFT:
            LogLine("event=%s player=%d wasHost=%d", name, event.playerId, event.bWasHost);
            break;
        case eSessionEvent::GAME_RESTARTED:
            LogLine("event=%s loaded=%d", name, event.bLoaded);
            break;
        case eSessionEvent::SAVE_LOADED:
            LogLine("event=%s file=%s", name, event.saveFileName ? event.saveFileName : "");
            break;
        case eSessionEvent::HOST_MISSION_STARTED:
        case eSessionEvent::HOST_MISSION_ENDED:
            LogLine("event=%s local=%d hostLost=%d", name, event.bLocal, event.bHostLost);
            break;
        default:
            LogLine("event=%s", name);
            break;
    }
}

void Emit(const SSessionEvent& event)
{
    LogEvent(event);
    ++ms_nEvents;
    // by index: a subscriber may subscribe another one
    for (size_t i = 0; i < ms_subscribers.size(); i++)
    {
        ms_subscribers[i](event);
    }
}

void EmitHostChanged(const CSessionHostState::STransition& transition)
{
    SSessionEvent event;
    event.type = eSessionEvent::HOST_CHANGED;
    event.oldHostId = transition.oldHostId;
    event.newHostId = transition.newHostId;
    event.bWasLocal = transition.bWasLocal;
    event.bIsLocal = transition.bIsLocal;
    Emit(event);
}

void EmitHostMission(bool bOnMission, bool bLocal, bool bHostLost)
{
    SSessionEvent event;
    event.type = bOnMission ? eSessionEvent::HOST_MISSION_STARTED : eSessionEvent::HOST_MISSION_ENDED;
    event.bLocal = bLocal;
    event.bHostLost = bHostLost;
    Emit(event);
}

// A host cutscene that this game loaded or plays goes away as the host's CLEAR_CUTSCENE would take it; one that is
// still loading is cleared once it has loaded (CCutsceneMgr::Update finishes a load in the next frame).
void ProcessCutsceneClear()
{
    if (ms_nCutsceneClearFrames <= 0)
    {
        return;
    }
    ms_nCutsceneClearFrames--;
    if (CCutsceneMgr::ms_cutsceneLoadStatus == CUTSCENE_LOADED)
    {
        Command<Commands::CLEAR_CUTSCENE>();
        ms_nCutsceneClearFrames = 0;
        LogLine("event=cleanup what=cutscene-cleared");
    }
    else if (CCutsceneMgr::ms_cutsceneLoadStatus == CUTSCENE_NOT_LOADED)
    {
        ms_nCutsceneClearFrames = 0;
    }
}

// The host left (or this game's session ended) during the host's mission: what that mission had set on this game would
// stay, because only the host's script ends it. Put back what the end of the host's mission replays here: its cutscene,
// the camera and player control (synced commands 02EA, 02EB, 0373, 01B4), and the clean-up that ON_MISSION_FLAG_SYNC
// 1 -> 0 runs (PacketHandlers/scripts.cpp; repeated here because by now this game may be the host, where that handler
// returns at once). Nothing while a mission of this game runs: that mission owns the controls, camera and $onmission.
bool CleanUpHostMission()
{
    if (CSessionLifecycle::IsLocalMissionRunning())
    {
        LogLine("event=cleanup what=skipped-local-mission");
        return false;
    }

    // don't start what the host asked for any more (Main.cpp starts these once they have loaded)
    COpCodeSync::ms_bLoadingCutscene = false;
    for (bool& bLoading : COpCodeSync::ms_abLoadingMissionAudio)
    {
        bLoading = false;
    }
    if (CCutsceneMgr::ms_cutsceneLoadStatus != CUTSCENE_NOT_LOADED)
    {
        ms_nCutsceneClearFrames = CUTSCENE_CLEAR_FRAMES;
        ProcessCutsceneClear();
    }

    Command<Commands::RESTORE_CAMERA_JUMPCUT>();
    Command<Commands::SET_CAMERA_BEHIND_PLAYER>();
    Command<Commands::SET_PLAYER_CONTROL>(0, 1);

    // the ON_MISSION_FLAG_SYNC 1 -> 0 clean-up
    if (CTheScripts::OnAMissionFlag)
    {
        CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] = 0;
    }
    CNetworkCheckpoint::Remove();
    CNetworkEntityBlip::ClearEntityBlips();
    TheCamera.SetWideScreenOff();
    CPad::GetPad(0)->SetDrunkInputDelay(0);
    CPad::GetPad(0)->DisablePlayerControls = 0;
    CPad::GetPad(0)->bApplyBrakes = 0;
    CPad::GetPad(0)->bDisablePlayerEnterCar = 0;
    CPad::GetPad(0)->bDisablePlayerDuck = 0;
    CPad::GetPad(0)->bDisablePlayerFireWeapon = 0;
    CPad::GetPad(0)->bDisablePlayerFireWeaponWithL1 = 0;
    CPad::GetPad(0)->bDisablePlayerCycleWeapon = 0;
    CPad::GetPad(0)->bDisablePlayerJump = 0;
    CPad::GetPad(0)->bDisablePlayerDisplayVitalStats = 0;
    CDraw::FadeValue = 0;

    LogLine("event=cleanup what=host-mission");
    return true;
}

// the host's last packets are through (or none will come): end its mission here if it still counts as running
void SettleHostLoss()
{
    if (!ms_hostMission.EndAfterHostLoss())
    {
        return;
    }
    CleanUpHostMission();
    EmitHostMission(false, false, true);
}

// The takeover's join snapshots (OnHostAssigned) wait for this game's first server-time answer: packets sent before it
// carry the PC's uptime as their stamp (CServerTime, g_serverTime) and wait in the other games' packet buffers
// (CPacketBuffer), and a story host who connects second becomes the host in its handshake frame, before CServerTime has
// even asked (it asks only once authenticated).
void ReleaseTakeoverIfReady()
{
    if (!ms_bTakeoverPending || CServerTime::m_serverTimeOffset == 0)
    {
        return;
    }
    ms_bTakeoverPending = false;
    if (!ms_hostState.IsLocalHost())
    {
        return;  // the host changed again meanwhile
    }
    LogLine("event=takeover-released players=%d", static_cast<int>(CNetworkPlayerManager::m_pPlayers.size()));
    if (ms_pfnTakeoverSnapshot != nullptr)
    {
        ms_pfnTakeoverSnapshot();
    }
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        SSessionEvent event;
        event.type = eSessionEvent::PLAYER_JOINED;
        event.playerId = pNetworkPlayer->m_iPlayerId;
        event.bTakeover = true;
        Emit(event);
    }
}

void OnSessionClosed()
{
    ms_bSessionOpen = false;
    ms_bTakeoverPending = false;

    // no packet is handled after a disconnect (Main.cpp processes the packet buffer only while connected), so a remote
    // host's mission ends here at once
    const bool bWasHost = ms_hostState.IsLocalHost();
    ms_hostLoss.Cancel();
    if (!bWasHost)
    {
        SettleHostLoss();
    }
    else
    {
        // this game's own mission goes on without the session
        ms_hostMission.EndAfterHostLoss();
    }

    // (a host cutscene still loading is cleared by Process afterwards: that is this game's state, not the session's)
    ms_hostState.Reset(-1);

    SSessionEvent event;
    event.type = eSessionEvent::DISCONNECTED;
    Emit(event);
}
}  // namespace

void CSessionLifecycle::Subscribe(Subscriber subscriber)
{
    if (subscriber != nullptr)
    {
        ms_subscribers.push_back(subscriber);
    }
}

bool CSessionLifecycle::IsAuthenticated() { return CNetwork::m_bConnected && CNetwork::m_bAuthenticated; }

bool CSessionLifecycle::IsHost() { return ms_hostState.IsLocalHost(); }

bool CSessionLifecycle::HasHost() { return ms_hostState.HasHost(); }

int CSessionLifecycle::GetHostId() { return ms_hostState.GetHostId(); }

int CSessionLifecycle::GetMyId() { return ms_hostState.GetMyId(); }

bool CSessionLifecycle::IsHostOnMission() { return ms_hostMission.IsOnMission(); }

bool CSessionLifecycle::IsLocalGameReady() { return IsLocalGameReady(FindPlayerPed(0)); }

bool CSessionLifecycle::IsLocalGameReady(CPlayerPed* pPed)
{
    if (pPed == nullptr || pPed->m_pRwClump == nullptr || pPed->m_pPlayerData == nullptr ||
        pPed->m_pPlayerData->m_pPedClothesDesc == nullptr)
    {
        return false;
    }
    if (CWorld::Players[0].m_nPlayerState != PLAYERSTATE_PLAYING || !(pPed->m_fHealth > 0.0f))
    {
        return false;
    }
    switch (pPed->m_ePedState)
    {
        case PEDSTATE_DIE:
        case PEDSTATE_DEAD:
        case PEDSTATE_DIE_BY_STEALTH:
        case PEDSTATE_ARRESTED:
            return false;
        default:
            return true;
    }
}

bool CSessionLifecycle::IsInFreePlay() { return IsInFreePlay(FindPlayerPed(0)); }

// Plain free play: no menu, no fade (the new-game start keeps the screen black until INTRO has given the default
// clothes, $350 and removed the weapons), no cutscene, controls on, and no mission of this game. FadeValue, not the
// camera's own alpha: the end-of-mission clean-up on the games that aren't the host clears only FadeValue.
bool CSessionLifecycle::IsInFreePlay(CPlayerPed* pPed)
{
    if (!IsLocalGameReady(pPed))
    {
        return false;
    }
    if (FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::ms_running || CDraw::FadeValue != 0 ||
        CPad::GetPad(0)->DisablePlayerControls != 0)
    {
        return false;
    }
    return !IsLocalMissionRunning();
}

uint32_t CSessionLifecycle::GetFreePlayDuration() { return ms_nFreePlayMs; }

bool CSessionLifecycle::IsLocalMissionRunning()
{
    for (CRunningScript* pScript = CTheScripts::pActiveScripts; pScript != nullptr; pScript = pScript->m_pNext)
    {
        if (pScript->m_bIsMission)
        {
            return true;
        }
    }
    return false;
}

void CSessionLifecycle::Process()
{
    // the session ended: CNetwork::Disconnect from a packet handler, the keep-alive or ENet's disconnect event
    if (CNetwork::m_bConnected)
    {
        ms_bSessionOpen = true;
    }
    else if (ms_bSessionOpen)
    {
        OnSessionClosed();
    }
    ReleaseTakeoverIfReady();

    if (ms_hostLoss.IsDue(GetTickCount(), HOST_LOSS_DELAY_MS))
    {
        SettleHostLoss();
    }
    ProcessCutsceneClear();

    bool bLoaded = false;
    if (ms_restart.Update(CTimer::m_FrameCounter, bLoaded))
    {
        ms_freePlay.Reset();

        SSessionEvent event;
        event.type = eSessionEvent::GAME_RESTARTED;
        event.bLoaded = bLoaded;
        Emit(event);

        if (bLoaded)
        {
            SSessionEvent loaded;
            loaded.type = eSessionEvent::SAVE_LOADED;
            loaded.saveFileName = ms_szLoadedSave;
            Emit(loaded);
        }
    }

    ms_nFreePlayMs = ms_freePlay.Update(IsInFreePlay(), GetTickCount());
}

void CSessionLifecycle::OnAuthenticated()
{
    ms_bSessionOpen = true;
    ms_hostState.Reset(CNetworkPlayerManager::m_nMyId);

    SSessionEvent event;
    event.type = eSessionEvent::AUTHENTICATED;
    Emit(event);
}

void CSessionLifecycle::OnPlayerConnected(int playerId, bool bAlreadyConnected)
{
    // the players who were in before this game get their snapshot when (if) it becomes the host: OnHostAssigned
    if (!ms_hostState.IsLocalHost() || bAlreadyConnected)
    {
        return;
    }
    SSessionEvent event;
    event.type = eSessionEvent::PLAYER_JOINED;
    event.playerId = playerId;
    Emit(event);
}

void CSessionLifecycle::OnPlayerDisconnected(int playerId)
{
    if (!ms_hostState.IsInSession())
    {
        return;
    }

    CSessionHostState::STransition transition;
    const bool bWasHost = ms_hostState.OnPlayerLeft(playerId, transition);
    if (bWasHost)
    {
        // its mission (if it ran, or starts in the packets still on the way) ends once those packets are through
        ms_hostLoss.Start(GetTickCount());
    }

    SSessionEvent event;
    event.type = eSessionEvent::PLAYER_LEFT;
    event.playerId = playerId;
    event.bWasHost = bWasHost;
    Emit(event);

    if (bWasHost)
    {
        EmitHostChanged(transition);
    }
}

bool CSessionLifecycle::OnHostAssigned(int playerId, void (*takeoverSnapshot)())
{
    CSessionHostState::STransition transition;
    if (!ms_hostState.OnAssign(playerId, transition))
    {
        return false;
    }

    if (transition.bWasLocal && ms_hostMission.EndAfterHostLoss())
    {
        // this game stopped being the host while connected (the server never does that today): its own mission's flag
        // doesn't stand for the session's host any more
        LogLine("event=cleanup what=local-host-flag-dropped");
    }

    EmitHostChanged(transition);

    if (transition.bIsLocal)
    {
        // the players who are in already get the join snapshots of this game, as soon as it has its first server-time
        // answer (now, or from Process)
        ms_pfnTakeoverSnapshot = takeoverSnapshot;
        ms_bTakeoverPending = !CNetworkPlayerManager::m_pPlayers.empty();
        if (ms_bTakeoverPending && CServerTime::m_serverTimeOffset == 0)
        {
            LogLine("event=takeover-waiting-for-server-clock");
        }
        ReleaseTakeoverIfReady();
    }
    else
    {
        ms_bTakeoverPending = false;
    }
    return transition.bIsLocal;
}

void CSessionLifecycle::OnHostMissionFlag(bool bOnMission)
{
    if (!ms_hostState.IsInSession() || ms_hostState.IsLocalHost())
    {
        return;
    }
    // a late packet after a lost host's mission was settled: nobody's mission any more
    if (!ms_hostState.HasHost() && !ms_hostLoss.IsPending())
    {
        return;
    }
    if (ms_hostMission.OnFlag(bOnMission))
    {
        EmitHostMission(bOnMission, false, false);
    }
}

void CSessionLifecycle::OnLocalHostMissionFlag(bool bOnMission)
{
    if (!ms_hostState.IsLocalHost())
    {
        return;
    }
    if (ms_hostMission.OnFlag(bOnMission))
    {
        EmitHostMission(bOnMission, true, false);
    }
}

void CSessionLifecycle::OnGameLoaded(const char* saveFileName)
{
    ms_restart.OnSaveLoaded();
    strncpy_s(ms_szLoadedSave, saveFileName != nullptr ? saveFileName : "", _TRUNCATE);
}

void CSessionLifecycle::WriteDiagSummary(FILE* file)
{
    fprintf(file,
        "  session: open=%d myId=%d hostId=%d isHost=%d hostMission=%d hostLossPending=%d freePlayMs=%u events=%u\n",
        ms_bSessionOpen, ms_hostState.GetMyId(), ms_hostState.GetHostId(), ms_hostState.IsLocalHost(),
        ms_hostMission.IsOnMission(), ms_hostLoss.IsPending(), ms_nFreePlayMs, ms_nEvents);
}
