#include "stdafx.h"
#include "CCommandUpdateCheckpointForNetworkPlayer.h"

void CCommandUpdateCheckpointForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(7);
	
	//CChat::AddMessage("CCommandUpdateCheckpointForNetworkPlayer::Process");
	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[6]));
	if (networkPlayer == nullptr) // private build: a stale or disconnected player's handle does nothing
		return;

	Packets::Blips::UpdateCheckpoint packet{};
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	packet.position = *(CVector*)&ScriptParams[0];
	packet.radius = *(CVector*)&ScriptParams[3];
	GetPacketFactory().Send(packet);
}
