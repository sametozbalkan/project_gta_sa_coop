#include "stdafx.h"
#include "CHostManager.h"
#include "CHostPolicy.h"
#include "network/packets/system.h"

// Private build: story host pinning and host-only relays, see CHostManager.h and CHostPolicy.h.

void CHostManager::Init()
{
    const std::string configured = CConfigManager::GetConfigStoryHost();
    ms_sStoryHost = HostPolicy::NormalizeStoryHost(configured);

    if (ms_sStoryHost.empty())
    {
        printf("[!] : Story host : none, the first player to connect is the host\n");
    }
    else if (ms_sStoryHost != configured)
    {
        printf("[!] : Story host : %s (story-host = %s, compared as the server sees nicknames)\n",
            ms_sStoryHost.c_str(), configured.c_str());
    }
    else
    {
        printf("[!] : Story host : %s, nobody else becomes the host\n", ms_sStoryHost.c_str());
    }
}

void CHostManager::Update(CNetworkPlayer* pJoined, CNetworkPlayer* pLeft)
{
    std::vector<CNetworkPlayer*>& players = CNetworkPlayerManager::m_pPlayers;

    // the player who left is out of the list already; its object stays allocated, so don't let it look like a host
    const bool bHostLeft = pLeft != nullptr && pLeft->m_bIsHost;
    if (pLeft != nullptr)
    {
        pLeft->m_bIsHost = false;
    }
    if (bHostLeft)
    {
        printf("[Host] : The host %s (id %d) left\n", pLeft->GetName().c_str(), pLeft->m_iPlayerId);
    }

    std::vector<HostPolicy::SPlayer> candidates;
    candidates.reserve(players.size());
    int joinedIndex = -1;
    for (size_t i = 0; i < players.size(); i++)
    {
        candidates.push_back({players[i]->m_Name, players[i]->m_bIsHost});
        if (players[i] == pJoined)
        {
            joinedIndex = static_cast<int>(i);
        }
    }

    const HostPolicy::SDecision decision = HostPolicy::Decide(candidates, ms_sStoryHost, joinedIndex);
    for (size_t i = 0; i < players.size(); i++)
    {
        players[i]->m_bIsHost = static_cast<int>(i) == decision.hostIndex;
    }
    CNetworkPlayer* pHost = decision.hostIndex >= 0 ? players[decision.hostIndex] : nullptr;

    if (decision.bAnnounce)
    {
        Packets::System::PlayerAssignHost playerAssignHost{};
        playerAssignHost.playerid = pHost->m_iPlayerId;
        GetPacketFactory().SendToAll(playerAssignHost);
        printf("[Host] : %s (id %d) is the host now (%s)\n", pHost->GetName().c_str(), pHost->m_iPlayerId,
            ms_sStoryHost.empty() ? "first player" : "story-host");
    }
    else if (decision.bTellJoiner)
    {
        Packets::System::PlayerAssignHost playerAssignHost{};
        playerAssignHost.playerid = pHost->m_iPlayerId;
        GetPacketFactory().Send(playerAssignHost, pJoined);
        printf("[Host] : %s (id %d) stays the host, told %s (id %d)\n", pHost->GetName().c_str(), pHost->m_iPlayerId,
            pJoined->GetName().c_str(), pJoined->m_iPlayerId);
    }
    else if (pHost != nullptr)
    {
        printf("[Host] : %s (id %d) stays the host\n", pHost->GetName().c_str(), pHost->m_iPlayerId);
    }
    else if (decision.previousIndex >= 0)
    {
        // only possible if the rule changed under a connected host; the games can't be told that nobody is the host
        CNetworkPlayer* pPrevious = players[decision.previousIndex];
        printf("[Host] : %s (id %d) is no longer the host, nobody is\n", pPrevious->GetName().c_str(),
            pPrevious->m_iPlayerId);
    }
    else if (!ms_sStoryHost.empty())
    {
        printf("[Host] : No host until the story host %s connects (%u player(s) waiting)\n", ms_sStoryHost.c_str(),
            static_cast<unsigned>(players.size()));
    }
    else
    {
        printf("[Host] : No host, the server is empty\n");
    }
}

bool CHostManager::SendFromHostToOthers(Packet& packet, CNetworkPlayer* pSender)
{
    if (pSender == nullptr || !pSender->m_bIsHost)
    {
        return false;
    }
    GetPacketFactory().SendToAll(packet, pSender);
    return true;
}

bool CHostManager::SendFromHostToPlayer(Packet& packet, CNetworkPlayer* pSender, int targetPlayerId)
{
    if (pSender == nullptr || !pSender->m_bIsHost)
    {
        return false;
    }
    CNetworkPlayer* pTarget = CNetworkPlayerManager::GetPlayer(targetPlayerId);
    if (pTarget == nullptr || pTarget == pSender)
    {
        return false;
    }
    GetPacketFactory().Send(packet, pTarget);
    return true;
}
