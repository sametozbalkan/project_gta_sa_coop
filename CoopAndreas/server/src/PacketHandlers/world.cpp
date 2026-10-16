#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/world.h"
#include "CHostManager.h"

PACKET_HANDLER(
    ePacketType::GAME_WEATHER_TIME, Packets::World::GameWeatherTime* pGameWeatherTime, CNetworkPlayer* pNetworkPlayer)
{
	if (!pNetworkPlayer->m_bIsHost)
	{
		return;
	}

	GetPacketFactory().SendToAll(*pGameWeatherTime, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ADD_EXPLOSION, Packets::World::AddExplosion* pAddExplosion, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pAddExplosion, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::TAG_UPDATE, Packets::World::TagUpdate* pTagUpdate, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pTagUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::UPDATE_ALL_TAGS, Packets::World::UpdateAllTags* pUpdateAllTags, CNetworkPlayer* pNetworkPlayer)
{
	if (pNetworkPlayer->m_bIsHost)
	{
		GetPacketFactory().SendToAll(*pUpdateAllTags, pNetworkPlayer);
	}
}

PACKET_HANDLER(ePacketType::UPDATE_MOON_SIZE, Packets::World::UpdateMoonSize* pUpdateMoonSize, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pUpdateMoonSize, pNetworkPlayer);
}

// private build (world-state registry, client/src/CWorldStateRegistry.h): the host's entries, relayed to everyone else
// with the sender filled in; dropped when the sender isn't the host
PACKET_HANDLER(ePacketType::WORLD_STATE, Packets::World::WorldState* pWorldState, CNetworkPlayer* pNetworkPlayer)
{
	pWorldState->playerid = pNetworkPlayer->m_iPlayerId;
	CHostManager::SendFromHostToOthers(*pWorldState, pNetworkPlayer);
}