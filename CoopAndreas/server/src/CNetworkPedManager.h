#pragma once

#include <vector>

class CNetworkPlayer;
class CNetworkPed;

class CNetworkPedManager
{
public:
    static std::vector<CNetworkPed*> m_pPeds;
    static void Add(CNetworkPed* ped);
    static void Remove(CNetworkPed* ped);
    static CNetworkPed* GetPed(int pedid);
    static int GetFreeId();
    // private build: reason for the console line ("left", "started over")
    static void RemoveAllHostedAndNotify(CNetworkPlayer* player, const char* reason = "left");
    // private build (vehicle sync protocol): the ped as the join snapshot has it (PED_SPAWN), to one player: a joiner, or
    // a game that started over (PLAYER_GAME_RESTARTED: every ped another game syncs, PedHandoverPolicy::IsSentAfterRestart)
    static void SendPedTo(CNetworkPed* ped, CNetworkPlayer* player);
    static unsigned int SendPedsAfterRestart(CNetworkPlayer* player);
};