#include "stdafx.h"
#include "CCommandClearAllEntityBlipsForNetworkPlayer.h"

void CCommandClearAllEntityBlipsForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(1);
	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (networkPlayer == nullptr) // private build: a stale or disconnected player's handle does nothing
		return;
	Packets::Blips::ClearEntityBlips packet{};
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	GetPacketFactory().Send(packet);
}
