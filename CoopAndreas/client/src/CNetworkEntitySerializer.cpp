#include "stdafx.h"
#include "network/CNetworkEntitySerializer.h"
#include "CBulletSyncPolicy.h"

CEntity* CNetworkEntitySerializer::GetEntity()
{
	if (entityType == NETWORK_ENTITY_TYPE_PED)
	{
		if (auto networkPed = CNetworkPedManager::GetPed(entityId))
		{
			if (auto ped = networkPed->GetLivePed())
			{
				return ped;
			}
		}
	}
	else if (entityType == NETWORK_ENTITY_TYPE_VEHICLE)
	{
		if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(entityId))
		{
			if (auto vehicle = networkVehicle->GetLiveVehicle())
			{
				return vehicle;
			}
		}
	}
	else if (entityType == NETWORK_ENTITY_TYPE_PLAYER)
	{
		if (CNetworkPlayerManager::m_nMyId == entityId)
		{
			return FindPlayerPed(0);
		}
		if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(entityId))
		{
			if (auto ped = networkPlayer->GetLivePed()) // private build: not a destroyed ped's old pointer
			{
				return ped;
			}
		}
	}

	return nullptr;
}

void CNetworkEntitySerializer::SetEntity(CEntity* entity)
{
	entityType = NETWORK_ENTITY_TYPE_NOTHING;
	entityId = 0;

	int lookupId = -1;
	bool isPlayerPed = false;

	if (entity != nullptr)
	{
		if (entity->m_nType == ENTITY_TYPE_PED)
		{
			isPlayerPed = ((CPed*)entity)->IsPlayer();
			if (isPlayerPed)
			{
				if (entity == FindPlayerPed(0))
				{
					lookupId = CNetworkPlayerManager::m_nMyId;
				}
				else if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(entity))
				{
					lookupId = networkPlayer->m_iPlayerId;
				}
			}
			else if (auto networkPed = CNetworkPedManager::GetPed(entity))
			{
				lookupId = networkPed->m_nPedId;
			}
		}
		else if (entity->m_nType == ENTITY_TYPE_VEHICLE)
		{
			if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(entity))
			{
				lookupId = networkVehicle->m_nVehicleId;
			}
		}
	}

	const auto resolved = BulletSyncPolicy::ResolveSerializedEntity(
		entity != nullptr, entity ? entity->m_nType : 0, isPlayerPed, lookupId);
	entityType = static_cast<eNetworkEntityType>(resolved.entityType);
	entityId = resolved.entityId;
}