#pragma once

class CPlayerPed;
class CNetworkPlayer;

// Private build: another player leaving during the host's mission (docs/mission-porting.md, "A player who leaves during
// the mission"; rules in CDepartedPlayerPolicy.h). A mission script keeps the other players' ped handles
// ($NETWORK_PLAYER[i]) and may use them after a wait without checking Coop.IsNetworkPlayerActorValid again; when the
// player has left by then, the vanilla command reads a NULL ped and the host crashes. So on the host, while a mission of
// its own runs, a departed player's ped stays as a stand-in instead of being deleted: out of its vehicle and of its
// CWorld::Players slot, hidden, without collision, static (out of CWorld::Process) and parked below where the player
// left, with its own copy of its player data. The co-op commands treat the player as gone (it isn't in
// CNetworkPlayerManager any more), the vanilla commands work on a real ped, and a player who comes back gets a new ped in
// the free slot. The stand-ins are deleted once no mission of this game runs.
class CDepartedPlayerPeds
{
public:
    // PLAYER_DISCONNECTED (PacketHandlers/system.cpp), after the player left CNetworkPlayerManager and before it is
    // deleted: keeps its ped as a stand-in when DepartedPlayerPolicy::ShouldKeepStandIn says so
    static void OnPlayerLeaving(CNetworkPlayer* pNetworkPlayer);
    // every frame while connected (CNetworkPlayerManager::ProcessPeds, outside CWorld::Process): puts back what script
    // commands changed on a stand-in, deletes them all once no mission of this game runs
    static void Process();
    // the CPlayerPed destructor hook (Hooks/PlayerHooks.cpp), before ~CPlayerPed: forgets a stand-in and returns its own
    // player data, which ~CPlayerPed still reads (the hook frees it afterwards with FreePlayerData), else nullptr
    static void* OnPedDestroying(CPlayerPed* pPed);
    static void FreePlayerData(void* pPlayerData);
    static bool IsStandIn(CPlayerPed* pPed);
};
