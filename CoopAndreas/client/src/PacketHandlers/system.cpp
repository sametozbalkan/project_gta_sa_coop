#include "CEntryExitMarkerSync.h"
#include "CNetworkStaticBlip.h"
#include "network/packet_types.h"
#include "network/packets/system.h"
#include "stdafx.h"
#include <CWeatherSync.h>
#include <CMoonSync.h>
#include <game_sa/CTagManager.h>
#include <CSessionLifecycle.h>

// private build: the host's join snapshot, also sent when this game becomes the host while players are in already (the
// server pins the story host, who can connect after the others; CSessionLifecycle)
static void SendHostJoinSnapshot()
{
    CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = true;
    CEntryExitMarkerSync::ms_bUpdateAfterProcessingThisFrame = true;
    CTagSync::SyncCurrentState();
    CMoonSync::SyncCurrentState();
    // (the world state, the city unlock among it, goes out from CWorldStateRegistry on PLAYER_JOINED)
}

PACKET_HANDLER(ePacketType::PLAYER_CONNECTED, Packets::System::PlayerConnected* pPlayerConnected)
{
    CNetworkPlayer* pNetworkPlayer =
        new CNetworkPlayer(pPlayerConnected->payload.playerid, CVector(2246.506f, -1259.552f, 23.9531f));
    CNetworkPlayerManager::Add(pNetworkPlayer);

    strcpy_s(pNetworkPlayer->m_Name, pPlayerConnected->payload.name);

    pNetworkPlayer->m_bHasBeenConnectedBeforeMe = pPlayerConnected->payload.isAlreadyConnected;

    if (!pNetworkPlayer->m_bHasBeenConnectedBeforeMe)
    {
        CChat::AddMessage("[Player] " + pNetworkPlayer->GetName() + " connected");
    }
    // TODO REFACTOR
    if (CLocalPlayer::m_bIsHost)
    {
        SendHostJoinSnapshot();
    }

    // private build: session lifecycle (PLAYER_JOINED on the host)
    CSessionLifecycle::OnPlayerConnected(pNetworkPlayer->m_iPlayerId, pNetworkPlayer->m_bHasBeenConnectedBeforeMe);
}

PACKET_HANDLER(ePacketType::PLAYER_DISCONNECTED, Packets::System::PlayerDisconnected* pPlayerDisconnected)
{
    if (pPlayerDisconnected->payload.playerid == -1)
    {
        if (pPlayerDisconnected->payload.reason == Packets::System::PlayerDisconnected::DISCONNECTION_REASON_VERSION_MISMATCH)
        {
            char buffer[23];
            semver_t expected;
            semver_unpack(pPlayerDisconnected->payload.version, &expected);
            semver_to_string(&expected, buffer, sizeof buffer);
            buffer[22] = '\0';
            CChat::AddMessage("{cecedb}[Network] Version mismatch, server: %s client: %s", buffer, COOPANDREAS_VERSION);
        }
        else if (pPlayerDisconnected->payload.reason == Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NAME_TAKEN)
        {
            CChat::AddMessage("{cecedb}[Network] The nickname '%s' is already taken.", CLocalPlayer::m_Name);
        }
        CNetwork::Disconnect();
        return;
    }

    // private build: session lifecycle (PLAYER_LEFT, and the host's loss), while the player is still there
    CSessionLifecycle::OnPlayerDisconnected(pPlayerDisconnected->payload.playerid);

    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(pPlayerDisconnected->payload.playerid);
    if (player != nullptr)
    {
        CChat::AddMessage("[Player] " + std::string(player->GetName()) + " disconnected");
        CNetworkPlayerManager::Remove(player);
        delete player;
    }
}

PACKET_HANDLER(ePacketType::PLAYER_HANDSHAKE, Packets::System::PlayerHandshake* pPlayerHandshake)
{
    CNetworkPlayerManager::m_nMyId = pPlayerHandshake->yourid;
    CNetwork::m_bAuthenticated = true;
    CPatch::RevertTemporaryPatches();
    logger::info("Authenticated, playerid %d", pPlayerHandshake->yourid);
    CSessionLifecycle::OnAuthenticated(); // private build: session lifecycle
}

PACKET_HANDLER(ePacketType::RTT_BROADCAST, Packets::System::RTTBroadcast* pRTTBroadcast)
{
    for (int nRTTIndex = 0; nRTTIndex < pRTTBroadcast->playerCount; nRTTIndex++)
    {
        Packets::System::RTTBroadcast::SPlayerRTT& playerRTT = pRTTBroadcast->rtts[nRTTIndex];
        if (playerRTT.playerid == CNetworkPlayerManager::m_nMyId)
        {
            continue;
        }

        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerRTT.playerid);
        if (pNetworkPlayer != nullptr)
        {
            pNetworkPlayer->m_nRTT = playerRTT.rtt;
        }
    }
}

PACKET_HANDLER(ePacketType::PLAYER_ASSIGN_HOST, Packets::System::PlayerAssignHost* pPlayerAssignHost)
{
    if (pPlayerAssignHost->playerid == CNetworkPlayerManager::m_nMyId)
    {
        CLocalPlayer::m_bIsHost = true;

        CPatch::RevertTemporaryPatchesForHost(); // TODO is this needed?

        CNetworkPedManager::AssignHost();
        CWeatherSync::SyncCurrentState();
        CTagSync::SyncCurrentState();
        CChat::AddMessage("[Player] You are the host now");

        // private build: session lifecycle; the players who are in already haven't had this game's join snapshot. It
        // goes out with the takeover's PLAYER_JOINED events once this game has its first server-time answer (a story
        // host who connects second becomes the host in its handshake frame; earlier packets would carry the PC's uptime
        // as their stamp and wait in the others' packet buffers).
        CSessionLifecycle::OnHostAssigned(pPlayerAssignHost->playerid, &SendHostJoinSnapshot);
        return;
    }
    
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_iPlayerId == pPlayerAssignHost->playerid)
        {
            CChat::AddMessage("[Player] " + pNetworkPlayer->GetName() + " is the host now");
            pNetworkPlayer->m_bIsHost = true;
        }
        else
        {
            pNetworkPlayer->m_bIsHost = false;
        }
    }

    CLocalPlayer::m_bIsHost = false;

    CSessionLifecycle::OnHostAssigned(pPlayerAssignHost->playerid); // private build: session lifecycle
}

PACKET_HANDLER(ePacketType::PLAYER_CHAT_MESSAGE, Packets::System::ChatMessage* pChatMessage)
{
    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(pChatMessage->playerid);

    if (player)
    {
        CChat::SendPlayerMessage(player->GetName().c_str(), player->m_iPlayerId, pChatMessage->message);
    }
}
