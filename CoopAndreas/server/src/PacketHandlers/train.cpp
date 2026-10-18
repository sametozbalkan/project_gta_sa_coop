#include "network/packets/train.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "CHostManager.h"

// private build (train sync protocol, client/src/CTrainSync.h; docs/design/trains.md §5, §7):
// - TRAIN_STATE: relayed from the host to all other players with sender filled in
// - TRAIN_EVENT:
//   - REQUEST: relayed from a non-host player to the current host only
//   - SNAPSHOT with targetPlayerId >= 0: relayed from the host to that player only
//   - SPAWN / REMOVE / CARRIAGES / broadcast SNAPSHOT: relayed from the host to all other players
PACKET_HANDLER(ePacketType::TRAIN_STATE, Packets::Train::TrainState* pTrainState, CNetworkPlayer* pNetworkPlayer)
{
    pTrainState->playerid = pNetworkPlayer->m_iPlayerId;
    CHostManager::SendFromHostToOthers(*pTrainState, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::TRAIN_EVENT, Packets::Train::TrainEvent* pTrainEvent, CNetworkPlayer* pNetworkPlayer)
{
    pTrainEvent->playerid = pNetworkPlayer->m_iPlayerId;
    if (pTrainEvent->kind == Packets::Train::eTrainEventKind::REQUEST)
    {
        if (pNetworkPlayer->m_bIsHost)
        {
            return;
        }
        for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pPlayer != nullptr && pPlayer->m_bIsHost && pPlayer != pNetworkPlayer)
            {
                GetPacketFactory().Send(*pTrainEvent, pPlayer);
                break;
            }
        }
        return;
    }

    if (pTrainEvent->kind == Packets::Train::eTrainEventKind::SNAPSHOT && pTrainEvent->targetPlayerId >= 0)
    {
        CHostManager::SendFromHostToPlayer(*pTrainEvent, pNetworkPlayer, pTrainEvent->targetPlayerId);
    }
    else
    {
        CHostManager::SendFromHostToOthers(*pTrainEvent, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::ENTITY_ATTACH, Packets::Train::EntityAttach* pEntityAttach, CNetworkPlayer* pNetworkPlayer)
{
    bool bAllowed = pNetworkPlayer->m_bIsHost;
    if (!bAllowed)
    {
        switch (pEntityAttach->entityKind)
        {
            case Packets::Train::eAttachEntityKind::PLAYER:
                bAllowed = (pEntityAttach->entityId == pNetworkPlayer->m_iPlayerId);
                break;

            case Packets::Train::eAttachEntityKind::PED:
            {
                CNetworkPed* pPed = CNetworkPedManager::GetPed(pEntityAttach->entityId);
                bAllowed = (pPed != nullptr && pPed->m_pSyncer == pNetworkPlayer);
                break;
            }

            case Packets::Train::eAttachEntityKind::VEHICLE:
            {
                CNetworkVehicle* pVeh = CNetworkVehicleManager::GetVehicle(pEntityAttach->entityId);
                bAllowed = (pVeh != nullptr && pVeh->m_pSyncer == pNetworkPlayer);
                break;
            }

            default:
                break;
        }
    }
    if (!bAllowed)
    {
        return;
    }

    pEntityAttach->playerid = pNetworkPlayer->m_iPlayerId;
    if (pEntityAttach->targetPlayerId >= 0)
    {
        CNetworkPlayer* pTarget = CNetworkPlayerManager::GetPlayer(pEntityAttach->targetPlayerId);
        if (pTarget != nullptr && pTarget != pNetworkPlayer)
        {
            GetPacketFactory().Send(*pEntityAttach, pTarget);
        }
    }
    else
    {
        GetPacketFactory().SendToAll(*pEntityAttach, pNetworkPlayer);
    }
}
