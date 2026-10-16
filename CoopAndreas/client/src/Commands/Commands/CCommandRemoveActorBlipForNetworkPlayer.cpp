#include "stdafx.h"
#include "CCommandRemoveActorBlipForNetworkPlayer.h"

void CCommandRemoveActorBlipForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(2);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (networkPlayer == nullptr) // private build: a stale or disconnected player's handle does nothing
		return;

	Packets::Blips::RemoveEntityBlip packet{};
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	packet.entity.SetEntity(CPools::GetPed(ScriptParams[1]));
	GetPacketFactory().Send(packet);
}
