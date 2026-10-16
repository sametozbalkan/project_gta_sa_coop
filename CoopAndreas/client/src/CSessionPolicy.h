#pragma once

#include <cstdint>

// Private build: the plain state behind CSessionLifecycle (CSessionLifecycle.h), without game or network types, so
// tests/session_policy_test.cpp checks it without GTA.

// Who is the host, as this game knows it: PLAYER_ASSIGN_HOST names a host (the server sends it to everyone when the host
// changes and to a player who connects while a host exists); the host's PLAYER_DISCONNECTED leaves nobody, until the
// next PLAYER_ASSIGN_HOST.
class CSessionHostState
{
public:
    struct STransition
    {
        int oldHostId = -1;      // -1 = nobody
        int newHostId = -1;      // -1 = nobody
        bool bWasLocal = false;  // this game was the host
        bool bIsLocal = false;   // this game is the host now
    };

    // a new session (PLAYER_HANDSHAKE gave this game myId) or none (disconnected, -1): no host known
    void Reset(int myId)
    {
        m_nMyId = myId;
        m_nHostId = -1;
    }

    // PLAYER_ASSIGN_HOST: true and the transition when the host changed (a repeat changes nothing)
    bool OnAssign(int playerId, STransition& transition)
    {
        if (m_nMyId < 0 || playerId < 0 || playerId == m_nHostId)
        {
            return false;
        }
        transition.oldHostId = m_nHostId;
        transition.newHostId = playerId;
        transition.bWasLocal = IsLocalHost();
        transition.bIsLocal = playerId == m_nMyId;
        m_nHostId = playerId;
        return true;
    }

    // PLAYER_DISCONNECTED: true and the transition when that player was the host (nobody is now)
    bool OnPlayerLeft(int playerId, STransition& transition)
    {
        if (m_nMyId < 0 || playerId < 0 || playerId != m_nHostId)
        {
            return false;
        }
        transition.oldHostId = m_nHostId;
        transition.newHostId = -1;
        transition.bWasLocal = IsLocalHost();
        transition.bIsLocal = false;
        m_nHostId = -1;
        return true;
    }

    bool IsInSession() const { return m_nMyId >= 0; }
    int GetMyId() const { return m_nMyId; }
    int GetHostId() const { return m_nHostId; }
    bool HasHost() const { return m_nHostId >= 0; }
    bool IsLocalHost() const { return m_nHostId >= 0 && m_nHostId == m_nMyId; }

private:
    int m_nMyId = -1;
    int m_nHostId = -1;
};

// The host's mission flag ($onmission): sent by this game while it is the host (Main.cpp, ON_MISSION_FLAG_SYNC), or
// received from the host. When the host goes away during its mission the flag is ended later, by EndAfterHostLoss.
class CHostMissionState
{
public:
    // true when the flag changed
    bool OnFlag(bool bOnMission)
    {
        if (bOnMission == m_bOnMission)
        {
            return false;
        }
        m_bOnMission = bOnMission;
        return true;
    }

    // the host left or the session ended and the host's last packets are handled: true when its mission counted as
    // running (it ends now)
    bool EndAfterHostLoss()
    {
        const bool bWasOnMission = m_bOnMission;
        m_bOnMission = false;
        return bWasOnMission;
    }

    bool IsOnMission() const { return m_bOnMission; }

private:
    bool m_bOnMission = false;
};

// The host's last packets on the other channels wait in the packet buffer (INTERP_BUFFER_DELAY_MS) after its
// PLAYER_DISCONNECTED, which is handled at once; whatever follows a host loss waits until they are through.
class CHostLossTimer
{
public:
    void Start(uint32_t now)
    {
        m_bPending = true;
        m_nSince = now;
    }

    // once per frame: true once, when the delay is over
    bool IsDue(uint32_t now, uint32_t delay)
    {
        if (!m_bPending || now - m_nSince < delay)
        {
            return false;
        }
        m_bPending = false;
        return true;
    }

    // the session ended: whatever was waiting is handled at once instead
    void Cancel() { m_bPending = false; }

    bool IsPending() const { return m_bPending; }

private:
    uint32_t m_nSince = 0;
    bool m_bPending = false;
};

// The local game started over: CTimer::Initialise (0x5617E0) zeroes CTimer::m_FrameCounter (0xB7CB4C) on a new game or
// a load, also while connected, and the GenericLoad hook reports a load (one right after start-up may not move the
// counter back). The same rule as CPlayerStateSave::Process.
class CGameRestartDetector
{
public:
    void OnSaveLoaded() { m_bLoadPending = true; }

    // once per frame: true when the game restarted since the last call; bLoaded when a load came with it
    bool Update(uint32_t frameCounter, bool& bLoaded)
    {
        bLoaded = m_bLoadPending;
        const bool bRestarted = m_bLoadPending || frameCounter < m_nLastFrameCounter;
        m_bLoadPending = false;
        m_nLastFrameCounter = frameCounter;
        return bRestarted;
    }

private:
    uint32_t m_nLastFrameCounter = 0;
    bool m_bLoadPending = false;
};

// How long the local game has been in free play without a break (CPlayerStateSave waits for 1 s of it).
class CFreePlayTimer
{
public:
    // once per frame with the current state and GetTickCount(): the time in free play so far, 0 when not in it
    uint32_t Update(bool bFreePlay, uint32_t now)
    {
        if (!bFreePlay)
        {
            m_bActive = false;
            return 0;
        }
        if (!m_bActive)
        {
            m_bActive = true;
            m_nSince = now;
        }
        return now - m_nSince;
    }

    void Reset() { m_bActive = false; }

private:
    uint32_t m_nSince = 0;
    bool m_bActive = false;
};
