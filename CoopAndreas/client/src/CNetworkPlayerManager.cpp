#include "stdafx.h"
#include "CDepartedPlayerPeds.h"

std::vector<CNetworkPlayer*> CNetworkPlayerManager::m_pPlayers;
CPad CNetworkPlayerManager::m_pPads[Config::MAX_SERVER_PLAYERS + 2];
int CNetworkPlayerManager::m_nMyId;

void CNetworkPlayerManager::Add(CNetworkPlayer* player)
{
    m_pPlayers.push_back(player);
}

void CNetworkPlayerManager::Remove(CNetworkPlayer* player)
{
    auto it = std::find(m_pPlayers.begin(), m_pPlayers.end(), player);
    if (it != m_pPlayers.end())
    {
        m_pPlayers.erase(it);
    }
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(SenderPlayerId playerid)
{
    return GetPlayer(playerid.value);
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(int playerid)
{
    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_iPlayerId == playerid)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(CEntity* entity)
{
    // private build: no match for NULL (the ped of a player whose ped is being recreated) or for the old pointer of a
    // ped the game destroyed (GetLivePed)
    if (entity == nullptr)
    {
        return nullptr;
    }

    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_pPed == entity && m_pPlayers[i]->GetLivePed() == entity)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

// private build: recreates a destroyed or clumpless network player ped, once per frame on the game thread
void CNetworkPlayerManager::ProcessPeds()
{
    for (auto pNetworkPlayer : m_pPlayers)
    {
        pNetworkPlayer->ProcessPedRecovery();
    }

    CDepartedPlayerPeds::Process(); // private build: the stand-ins of players who left during the host's mission
}

// private build: called by the CPlayerPed destructor hook (PlayerHooks.cpp) for every CPlayerPed, local ones too
void CNetworkPlayerManager::OnPlayerPedDestroyed(CPlayerPed* ped)
{
    for (auto pNetworkPlayer : m_pPlayers)
    {
        pNetworkPlayer->OnPedDestroyed(ped);
    }
}