#include "stdafx.h"
#include "CCommandRemoveCheckpointForNetworkPlayer.h"

void CCommandRemoveCheckpointForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(1);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (networkPlayer == nullptr) // private build: a stale or disconnected player's handle does nothing
		return;

	Packets::Blips::RemoveCheckpoint packet{};
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	GetPacketFactory().Send(packet);
}
