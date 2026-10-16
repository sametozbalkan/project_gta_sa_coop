#pragma once
#include <deque>

// Private build: keeps the connection to the server alive while GTA SA is in the background (alt-tab, minimized).
//
// While the window is inactive, 1.0 US WinMain runs no frames and sleeps 100 ms per loop instead, so nothing serviced
// ENet and the server dropped the client after ~30 s. The hook turns that sleep into a wait on the ENet socket on the
// main thread: acks and pings keep flowing, reliable packets are kept in arrival order and handed to the normal receive
// path in the first frame after the game is back, unreliable sync snapshots are dropped. The game itself stays paused
// and no packet handler runs while it is. On the way into the background the player's update goes out once more with
// every key released, so the other games don't replay a held key for the whole time.
class CNetworkKeepAlive
{
public:
    static void InjectHooks();

    // Hands the kept packets to CPacketFactory. Called at the start of CNetwork::ProcessReceive, before newer packets
    // are taken from ENet, so the arrival order is kept.
    static void ProcessDeferred();

    // --coopdiag: packets waiting for the next frame, and sync snapshots dropped during the current (or last) time in
    // the background
    static uint32_t GetKeptCount() { return ms_nKeptPackets; }
    static uint32_t GetDroppedCount() { return ms_nDroppedPackets; }

private:
    static void __stdcall BackgroundSleep(DWORD dwMilliseconds);
    static void SendKeysReleased();
    static bool IsServiceable();
    static bool ShouldKeep(const ENetEvent& event);
    static void OnBackgroundEvent(ENetEvent& event);
    static void FreeKeptPackets();

    inline static VOID(WINAPI** ms_ppfnSleep)(DWORD) = nullptr;  // the game's IAT slot of Sleep
    inline static void(__stdcall* ms_pfnBackgroundSleep)(DWORD) = &BackgroundSleep;  // read by the patched call

    inline static std::deque<ENetPacket*> ms_keptPackets{};
    inline static size_t ms_nKeptBytes = 0;
    inline static uint32_t ms_nKeptPackets = 0;
    inline static uint32_t ms_nDroppedPackets = 0;
    inline static bool ms_bInBackground = false;
    inline static DWORD ms_nBackgroundSince = 0;
};
