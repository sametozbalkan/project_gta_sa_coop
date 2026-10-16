#include "stdafx.h"
#include "CCommandGetNetworkPlayerPedHandleTo.h"

void CCommandGetNetworkPlayerPedHandleTo::Process(CRunningScript* script)
{
	script->CollectParameters(1);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(ScriptParams[0]);
	//CChat::AddMessage("network player id: %d", ScriptParams[0]);
	// private build: -1 for an unknown player or one whose ped is being recreated
	CPlayerPed* ped = networkPlayer ? networkPlayer->GetLivePed() : nullptr;
	ScriptParams[0] = ped ? CPools::GetPedRef(ped) : -1;
	//CChat::AddMessage("ped ref: %d", ScriptParams[0]);
	script->StoreParameters(1);
}