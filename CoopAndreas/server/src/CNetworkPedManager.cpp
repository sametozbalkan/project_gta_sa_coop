#include "stdafx.h"
#include <network/packets/peds.h>
#include <network/entity_lifecycle.h>
#include "CPedHandoverPolicy.h"

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;

// private build (vehicle sync protocol review): the id handed out last (round robin, EntityLifecycle::NextFreeId)
static int ms_nLastPedId = -1;

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    m_pPeds.push_back(ped);
}

void CNetworkPedManager::Remove(CNetworkPed* ped)
{
    auto it = std::find(m_pPeds.begin(), m_pPeds.end(), ped);
    // std::find()
    if (it != m_pPeds.end())
    {
        m_pPeds.erase(it);
    }
}

CNetworkPed* CNetworkPedManager::GetPed(int pedid)
{
    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_nPedId == pedid)
        {
            return m_pPeds[i];
        }
    }
    return nullptr;
}

int CNetworkPedManager::GetFreeId()
{
    // private build (vehicle sync protocol review): the next free id after the last one handed out, as for vehicles, so
    // a late packet about a removed ped (PED_REMOVE among them) doesn't meet a new ped with its id
    const int id = EntityLifecycle::NextFreeId(ms_nLastPedId, Config::MAX_SERVER_PEDS,
        [](int candidate) { return CNetworkPedManager::GetPed(candidate) == nullptr; });
    if (id >= 0)
    {
        ms_nLastPedId = id;
    }
    return id;
}

void CNetworkPedManager::RemoveAllHostedAndNotify(CNetworkPlayer* player, const char* reason)
{
    Packets::Peds::PedRemove packet{};

    for (auto it = CNetworkPedManager::m_pPeds.begin(); it != CNetworkPedManager::m_pPeds.end();)
    {
        if ((*it)->m_pSyncer == player)
        {
            // private build: a ped another player claimed goes to that player instead (PedHandoverPolicy), as when its
            // syncer releases it (PED_REMOVE): their script keeps its handle. The new syncer's game turns its copy into
            // the synced one (ASSIGN_PED); the others keep theirs and get its updates from the new syncer.
            if (CNetworkPlayer* pClaimer = PedHandoverPolicy::FindClaimer(CNetworkPlayerManager::m_pPlayers, player, *it))
            {
                (*it)->m_pSyncer = pClaimer;
                auto claim = std::find(pClaimer->m_vPedClaims.begin(), pClaimer->m_vPedClaims.end(), *it);
                if (claim != pClaimer->m_vPedClaims.end())
                {
                    pClaimer->m_vPedClaims.erase(claim);
                }

                Packets::Peds::AssignPedSyncer assignPedPacket{};
                assignPedPacket.pedid = (*it)->m_nPedId;
                GetPacketFactory().Send(assignPedPacket, pClaimer);
                printf("[Game] : ped %i goes to %i, who claimed it (its syncer %i %s)\n", (*it)->m_nPedId,
                    pClaimer->m_iPlayerId, player->m_iPlayerId, reason);
                ++it;
                continue;
            }

            packet.pedid = (*it)->m_nPedId;
            GetPacketFactory().SendToAll(packet, player);

            // private build: no claim keeps pointing at the deleted ped (claims are matched by pointer, and a new ped
            // can get the same address)
            for (auto* pPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                auto claim = std::find(pPlayer->m_vPedClaims.begin(), pPlayer->m_vPedClaims.end(), *it);
                if (claim != pPlayer->m_vPedClaims.end())
                {
                    pPlayer->m_vPedClaims.erase(claim);
                }
            }

            delete *it;
            it = CNetworkPedManager::m_pPeds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void CNetworkPedManager::SendPedTo(CNetworkPed* ped, CNetworkPlayer* player)
{
    // private build (vehicle sync protocol): shared by the join snapshot (CNetwork::HandlePlayerConnected) and a restart
    Packets::Peds::PedSpawn packet{};
    packet.pedid = ped->m_nPedId;
    packet.modelId = ped->m_nModelId;
    packet.pos = ped->m_vecPos;
    packet.pedType = ped->m_nPedType;
    packet.createdBy = ped->m_nCreatedBy;
    snprintf(packet.specialModelName, sizeof(packet.specialModelName), "%s", ped->m_szSpecialModelName);
    GetPacketFactory().Send(packet, player);
}

unsigned int CNetworkPedManager::SendPedsAfterRestart(CNetworkPlayer* player)
{
    // private build (vehicle sync protocol): CWorld::ClearForRestart deleted that game's copies of every ped, and nothing
    // recreated them (its NPC drivers' cars came back as frozen copies without a driver); the peds it synced itself went
    // with its PED_REMOVE before (to a claimer, or from every game)
    unsigned int sent = 0;
    for (CNetworkPed* ped : m_pPeds)
    {
        if (PedHandoverPolicy::IsSentAfterRestart(ped, player))
        {
            SendPedTo(ped, player);
            sent++;
        }
    }
    return sent;
}