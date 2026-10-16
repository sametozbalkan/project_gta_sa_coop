#pragma once

#include <cstdint>
#include <cstdio>

class CPlayerPed;

// Private build: the co-op session lifecycle (docs/design/README.md, section 2), one source for the mission mirror layer,
// the world-state registry and whatever else reacts to the session. It turns the existing network and game signals into
// events and answers state questions. Everything runs on the game thread: the packet handlers
// (PacketHandlers/system.cpp, scripts.cpp), the GenericLoad hook (Hooks/SaveLoadHooks.cpp) and Main.cpp's frame event
// call it, and it calls the subscribers from there.
enum class eSessionEvent : uint8_t
{
    // the server accepted this game (PLAYER_HANDSHAKE); no host is known yet, PLAYER_ASSIGN_HOST may follow at once
    AUTHENTICATED,
    // the connection ended (also one that was never authenticated); afterwards there is no session, no host and no
    // HOST_CHANGED for it
    DISCONNECTED,
    // oldHostId -> newHostId (-1 = nobody); bWasLocal / bIsLocal: this game was / is the host
    HOST_CHANGED,
    // host side: playerId connected, or was already connected when this game became the host (bTakeover; the server
    // pins the story host, who can connect after the others). For the join snapshots.
    PLAYER_JOINED,
    // playerId left (bWasHost: it was the host, HOST_CHANGED to nobody follows); it is still in CNetworkPlayerManager
    // while the subscribers run and deleted right after
    PLAYER_LEFT,
    // the local game started over: a new game or a load (bLoaded), connected or not
    GAME_RESTARTED,
    // the local game loaded saveFileName (right after GAME_RESTARTED); the host's load when IsHost()
    SAVE_LOADED,
    // the host's $onmission went to 1 (bLocal: this game is the host)
    HOST_MISSION_STARTED,
    // the host's $onmission went to 0, or the host left or the session ended during its mission (bHostLost; sent once
    // the host's last packets are handled, after this game's own clean-up of the host's mission)
    HOST_MISSION_ENDED,
};

struct SSessionEvent
{
    eSessionEvent type = eSessionEvent::AUTHENTICATED;
    int playerId = -1;    // PLAYER_JOINED, PLAYER_LEFT
    int oldHostId = -1;   // HOST_CHANGED
    int newHostId = -1;   // HOST_CHANGED
    bool bWasLocal = false;  // HOST_CHANGED
    bool bIsLocal = false;   // HOST_CHANGED
    bool bTakeover = false;  // PLAYER_JOINED
    bool bWasHost = false;   // PLAYER_LEFT
    bool bLoaded = false;    // GAME_RESTARTED
    bool bLocal = false;     // HOST_MISSION_STARTED / HOST_MISSION_ENDED
    bool bHostLost = false;  // HOST_MISSION_ENDED
    const char* saveFileName = nullptr;  // SAVE_LOADED, valid during the call
};

class CSessionLifecycle
{
public:
    using Subscriber = void (*)(const SSessionEvent& event);

    // A module's handler for every event, called in the order of subscription. Subscribe once at start-up (for example
    // from CCore::Init); a subscriber may also send packets and change game state.
    static void Subscribe(Subscriber subscriber);

    // --- state, on the game thread ---
    static bool IsAuthenticated();  // connected and accepted by the server
    static bool IsHost();           // this game is the session's host
    static bool HasHost();          // some game (this one or another) is the host
    static int GetHostId();         // the host's player id, -1 = nobody
    static int GetMyId();           // this game's player id, -1 = not in a session
    static bool IsHostOnMission();  // the host's $onmission as last sent (host) or received (others)
    // The local player is fully there: ped, clump, player data and clothes, playing, alive, not dying or arrested
    // (the first half of CPlayerStateSave's rule).
    static bool IsLocalGameReady();
    static bool IsLocalGameReady(CPlayerPed* pPed);
    // ... and in plain free play: no menu, fade, cutscene or disabled controls and no mission of this game
    // (CPlayerStateSave's rule for snapshots and restores).
    static bool IsInFreePlay();
    static bool IsInFreePlay(CPlayerPed* pPed);
    // ms in free play without a break, 0 when not in it; updated once per frame by Process
    static uint32_t GetFreePlayDuration();
    // a mission of this game runs (a script with m_bIsMission in CTheScripts::pActiveScripts); on a game that isn't the
    // host that is its own mission (the new-game missions INITIAL, INITIL2, INTRO or a side mission), never the host's
    static bool IsLocalMissionRunning();

    // --- inputs from the existing code ---
    static void Process();                                            // Main.cpp, every frame (gameProcessEvent)
    static void OnAuthenticated();                                    // PLAYER_HANDSHAKE, after m_bAuthenticated
    static void OnPlayerConnected(int playerId, bool bAlreadyConnected);  // PLAYER_CONNECTED, after the player is added
    static void OnPlayerDisconnected(int playerId);                   // PLAYER_DISCONNECTED, before the player is deleted
    // PLAYER_ASSIGN_HOST, after the handler set CLocalPlayer::m_bIsHost: true when this game has just become the host
    static bool OnHostAssigned(int playerId);
    static void OnHostMissionFlag(bool bOnMission);       // ON_MISSION_FLAG_SYNC from the host (games that aren't it)
    static void OnLocalHostMissionFlag(bool bOnMission);  // Main.cpp sent ON_MISSION_FLAG_SYNC as the host
    static void OnGameLoaded(const char* saveFileName);   // the GenericLoad hook, after a successful load

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
