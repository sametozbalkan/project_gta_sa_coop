#include "stdafx.h"
#include "CCommandShowTextBoxForNetworkPlayer.h"

void CCommandShowTextBoxForNetworkPlayer::Process(CRunningScript* script)
{
	char gxt[8];
	script->ReadTextLabelFromScript(gxt, 8);
	gxt[7] = '\0';

	script->CollectParameters(1);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (networkPlayer == nullptr) // private build: a stale or disconnected player's handle does nothing
		return;

	Packets::Scripts::AddMessageGXT packet{};
	strncpy(packet.gxt, gxt, 8);
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	packet.type = Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_HELP;
	GetPacketFactory().Send(packet);
}
