#include "stdafx.h"
#include "CCommandCollectNetworkPlayersForTheMission.h"

void CCommandCollectNetworkPlayersForTheMission::Process(CRunningScript* script)
{
	uint8_t i = 0;

	memset(ScriptParams, 0, 3 * sizeof(int));

	for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		if (auto player = networkPlayer->GetLivePed()) // private build: not a destroyed ped's old handle
		{
			ScriptParams[i] = CPools::GetPedRef(player);
		}

		if (++i >= 3)
			break;
	}

	//CChat::AddMessage("CCommandCollectNetworkPlayersForTheMission::Process stored params %d %d %d", ScriptParams[0], ScriptParams[1], ScriptParams[2]);
	script->StoreParameters(3);
}