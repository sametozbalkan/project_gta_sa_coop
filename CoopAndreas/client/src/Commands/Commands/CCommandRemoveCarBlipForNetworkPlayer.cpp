#include "stdafx.h"
#include "CCommandRemoveCarBlipForNetworkPlayer.h"

void CCommandRemoveCarBlipForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(2);

	// private build: a stale or disconnected player's handle does nothing (the asserts are gone in a release build)
	auto ped = CPools::GetPed(ScriptParams[0]);
	auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped);
	if (networkPlayer == nullptr)
		return;

	auto vehicle = CPools::GetVehicle(ScriptParams[1]);
	assert(vehicle && "opcode 0xF05: invalid CVehicle*");

	auto networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
	assert(networkVehicle && "opcode 0xF05: invalid CNetworkVehicle*");

	Packets::Blips::RemoveEntityBlip packet{};
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	packet.entity.SetEntity(vehicle);

	GetPacketFactory().Send(packet);
}