#pragma once

#include <algorithm>
#include <vector>

// Private build: what happens to the peds a leaving player synced (CNetworkPedManager::RemoveAllHostedAndNotify). The
// plain rule, so tests/player_leave_test.cpp checks it without ENet.
namespace PedHandoverPolicy
{
// The player who takes over a ped its leaving syncer leaves behind: the first remaining player (connection order) who
// claimed it (PED_CLAIM_ON_RELEASE, Coop.ClaimPedOnRelease), as the PED_REMOVE handler hands over a ped its syncer
// releases; nullptr when nobody did, and the ped is removed on every game. A claimer's script holds the ped's handle:
// the shop scripts share a shopkeeper the other game created (BARB, JFUD, PSHOP, TATTO), e.g. the barber of mission #3.
template <class TPlayer, class TPed>
TPlayer* FindClaimer(const std::vector<TPlayer*>& players, const TPlayer* pLeaving, const TPed* pPed)
{
    for (TPlayer* pPlayer : players)
    {
        if (pPlayer == nullptr || pPlayer == pLeaving)
        {
            continue;
        }
        if (std::find(pPlayer->m_vPedClaims.begin(), pPlayer->m_vPedClaims.end(), pPed) != pPlayer->m_vPedClaims.end())
        {
            return pPlayer;
        }
    }
    return nullptr;
}

// Private build (vehicle sync protocol): a game that started over (PLAYER_GAME_RESTARTED) gets every ped another game
// syncs again, as a joiner does (PED_SPAWN). The peds it synced itself are not among them: its PED_REMOVE for each, sent
// before the notice, handed them to a claimer (FindClaimer, which sends it the ped back as a copy) or removed them from
// every game, and the server releases the same way one it still counts as that game's (CNetworkPedManager::
// RemoveAllHostedAndNotify, as when a syncer leaves; vehicles go the same way: REMOVE_SYNCER_RESTARTED). TPed: m_pSyncer.
template <class TPlayer, class TPed>
bool IsSentAfterRestart(const TPed* pPed, const TPlayer* pRestarted)
{
    return pPed != nullptr && pPed->m_pSyncer != nullptr && pPed->m_pSyncer != pRestarted;
}
}  // namespace PedHandoverPolicy
