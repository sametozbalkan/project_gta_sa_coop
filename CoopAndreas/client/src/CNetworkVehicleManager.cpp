#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CKeySync.h>
#include "CNetworkVehicleManager.h"
#include "COpCodeSync.h"
#include "CMissionMirror.h"
#include <CTimer.h>
#include <cmath>
#include <cstdarg>

// Private build (vehicle sync protocol, 0.1.1; rules in CVehicleSyncPolicy.h, server side in CVehicleSeatPolicy.h):
// - ASSIGN_VEHICLE says what this game now is for a vehicle; one for a vehicle this game doesn't know yet waits
//   (CPendingVehicleAssignments) until its VEHICLE_SPAWN or VEHICLE_CONFIRM comes.
// - Every vehicle the game destroys passes CVehicle::~CVehicle (VehicleHooks.cpp hooks its five callers), where its record
//   lets go of it: a vehicle this game syncs goes on every game (VEHICLE_REMOVE), a temp record keeps its slot until
//   VEHICLE_CONFIRM and removes the vehicle then (REMOVE_UNCONFIRMED), a copy the host's mission deleted goes on every
//   game (REMOVE_BY_HOST_MISSION), any other copy is recreated from its syncer's next update (a wreck isn't).
// - Copies stay MISSION_VEHICLE and "needed" (GuardCopy), so this game's population, garages and wreck clean-up don't
//   delete them; the local createdBy changes of scripts are kept in the record instead (OnLocalCreatedByChanged).
// - Stored pointers are checked against the pool slot generation they had (CVehiclePoolReference).
namespace
{
CPendingVehicleAssignments ms_pendingAssignments;
int ms_nLastTempId = -1;
CVehicle* ms_pLastRemovedVehicle = nullptr;  // the vehicle the last hooked CWorld::Remove took out of the world
uintptr_t ms_nLastRemoveSite = 0;            // and that call's site
int ms_nDestructorHooks = 0;                 // ~CVehicle call sites redirected / checked (VehicleHooks.cpp)
int ms_nDestructorSites = 0;
bool ms_bHooksLogged = false;

// CVehicle::m_nTimeTillWeNeedThisCar (+0x4D0) of a copy. 1.0 US reads it only in CCarCtrl::PossiblyRemoveVehicle
// 0x424F80 (0x4250CD, 0x42529B, 0x4253A7; dumpbin of .text and .HOODLUM), whose wreck clean-up (CWorld::Remove at
// 0x42541E) deletes a vehicle wrecked for 60 s (+0x4D8), off screen and more than 6.5 m away without asking
// CanBeDeleted, but not before this time. BlowUpCar sets it to now + 3000 (0x6A2B26, 0x6BDE58, 0x6C4423, 0x6CAB7A,
// 0x6F0220), so the sweep writes it again.
constexpr unsigned int COPY_KEEP_TIME = 0xFFFFFFFF;

// The CWorld::Remove call sites WorldHooks.cpp redirects that can take a vehicle (the others remove peds, objects,
// pickups, projectiles and the like), by caller: callers from ReVa's names of 1.0 US (gta-reversed and plugin-sdk) and
// their code read with dumpbin. A copy (MISSION_VEHICLE, GuardCopy) never passes the CanBeDeleted (0x6D1180) or
// createdBy checks; "copies too" marks the paths that destroy one anyway.
struct SRemoveSite
{
    uintptr_t site;
    const char* name;
};
const SRemoveSite removeSiteClasses[] = {
    {0x4251E6, "population"},          // CCarCtrl::PossiblyRemoveVehicle 0x424F80: CanBeDeleted cars
    {0x425221, "population"},          // same
    {0x42541E, "population-wreck"},    // same, and its wreck clean-up (copies kept by COPY_KEEP_TIME)
    {0x449729, "garage-door"},         // CGarage::RemoveCarsBlockingDoorNotInside: CanBeDeleted (0x449715)
    {0x4499F3, "garage-store"},        // CGarage::StoreAndRemoveCarsForThisHideOut 0x449900: not MISSION_VEHICLE
    {0x449B43, "garage-store"},        // its copy 0x449A50 (CGarage::Update 0x44C465)
    {0x449CE0, "garage-tidy"},         // TidyUpGarage 0x449C50: wrecked or upside down in a garage, copies too
    {0x449E2A, "garage-tidy"},         // TidyUpGarageClose 0x449D10: wrecks touching a garage, copies too
    {0x4610CC, "roadblock"},           // CRoadBlocks::ClearSpaceForRoadBlockObject: CanBeDeleted (0x461084)
    {0x467B3C, "script-delete-car"},   // DELETE_CAR (00A6, case 0x467B1A): any script, copies too
    {0x486D3E, "script-clear-space"},  // CTheScripts::ClearSpaceForMissionEntity 0x486B00: CanBeDeleted
    {0x499D90, "set-piece"},           // CSetPiece::Update 0x499A80: its own cars
    {0x49A45A, "set-piece"},           // same
    {0x53C98C, "game-shutdown"},       // CGame::Shutdown, copies too
    {0x5667B0, "script-clear-cars"},   // CWorld::ClearCarsFromArea 0x566610 (only caller 03BA): CanBeDeleted
    {0x5D5112, "save-load"},           // CTheScripts::Load, copies too
    {0x6C7B9A, "police-heli"},         // CHeli::UpdateHelis 0x6C79A0: its own police helis
    {0x6CCCB2, "ambient-planes"},      // CPlane::SwitchAmbientPlanes 0x6CCC50: RANDOM_VEHICLE only
    {0x6D22D7, "crusher"},             // DestroyVehicleAndDriverAndPassengers (CGarage::Update 0x44B99B), copies too
    {0x6F5DD7, "train"},               // CTrain::RemoveOneMissionTrain (trains never become copies)
    {0x6F6A7B, "train"},               // CTrain::RemoveMissionTrains
    {0x6F6B31, "train"},               // CTrain::RemoveAllTrains
    {0x6F7ACA, "train"},               // CTrain::DoTrainGenerationAndRemoval
    {0x45BEFF, "replay"},              // CReplay (playback only), copies too
    {0x45D3C9, "replay"},
    {0x45D3FE, "replay"},
    {0x45ED11, "replay"},
    {0x45ED69, "replay"},
    {0x45FF78, "replay"},
    {0x45FFCB, "replay"},
};

const char* RemoveSiteClass(uintptr_t site)
{
    if (site == 0)
    {
        return "unhooked";  // not through a redirected CWorld::Remove (e.g. CCarCtrl::RemoveCarsIfThePoolGetsFull 0x4323F9)
    }
    for (const SRemoveSite& entry : removeSiteClasses)
    {
        if (entry.site == site)
        {
            return entry.name;
        }
    }
    return "other";
}

unsigned int CountTemp()
{
    unsigned int count = 0;
    for (CNetworkVehicle* pTemp : CNetworkVehicleManager::m_apTempVehicles)
    {
        count += pTemp != nullptr;
    }
    return count;
}

unsigned int CountLost()
{
    unsigned int count = 0;
    for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        count += pNetworkVehicle->m_bCopyLost;
    }
    return count;
}

// --coopdiag: frequent events at most once a second each
bool ShouldLogFrequent(const char* event)
{
    static const char* events[8]{};
    static uint32_t lastLogged[8]{};
    const uint32_t now = GetTickCount();
    for (size_t i = 0; i < ARRAY_SIZE(events); i++)
    {
        if (events[i] == nullptr)
        {
            events[i] = event;
            lastLogged[i] = now;
            return true;
        }
        if (strcmp(events[i], event) == 0)
        {
            if (static_cast<uint32_t>(now - lastLogged[i]) < 1000)
            {
                return false;
            }
            lastLogged[i] = now;
            return true;
        }
    }
    return true;
}

void DeleteRecord(CNetworkVehicle* pNetworkVehicle)
{
    CNetworkVehicleManager::Remove(pNetworkVehicle);
    delete pNetworkVehicle;
}

// recreates the record's vehicle (a lost copy, or one this game takes over) with the syncer's last state
CVehicle* RecreateVehicle(CNetworkVehicle* pNetworkVehicle, const CVector& position, const CVector& right,
    const CVector& forward, uint8_t color1, uint8_t color2, const char* reason)
{
    const float heading = atan2f(-forward.x, forward.y);
    if (!pNetworkVehicle->CreateVehicle(pNetworkVehicle->m_nVehicleId, pNetworkVehicle->m_nModelId, position, heading,
            color1, color2) ||
        pNetworkVehicle->GetLiveVehicle() == nullptr)
    {
        CNetworkVehicleManager::LogEvent("copy-recreate-failed", pNetworkVehicle->m_nVehicleId, "model=%d reason=%s",
            pNetworkVehicle->m_nModelId, reason);
        return nullptr;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    pVehicle->m_matrix->right = right;
    pVehicle->m_matrix->up = forward;
    pNetworkVehicle->m_bCopyLost = false;
    pNetworkVehicle->m_bLostWreck = false;
    pNetworkVehicle->m_nBlipHandle = -1;
    CNetworkVehicleManager::LogEvent("copy-recreated", pNetworkVehicle->m_nVehicleId, "model=%d reason=%s pos=%.1f,%.1f,%.1f",
        pNetworkVehicle->m_nModelId, reason, position.x, position.y, position.z);
    return pVehicle;
}

// ASSIGN_VEHICLE's state (now or after it waited); false when the record went (a takeover with nothing to take over)
bool ApplyAssignment(CNetworkVehicle* pNetworkVehicle, bool bSyncing, const char* source)
{
    const int vehicleid = pNetworkVehicle->m_nVehicleId;
    pNetworkVehicle->m_bSyncing = bSyncing;
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle();

    if (!bSyncing)
    {
        if (pVehicle != nullptr)
        {
            CNetworkVehicleManager::GuardCopy(pVehicle);
        }
        CNetworkVehicleManager::LogEvent("assignment-applied", vehicleid, "syncing=0 source=%s vehicle=%d", source,
            pVehicle != nullptr);
        return true;
    }

    // this game is the syncer now (it drives the vehicle, a passenger of a driverless one, or the host taking over a car
    // of its mission whose syncer left)
    switch (VehicleSyncPolicy::DecideTakeover(
        pVehicle != nullptr, pNetworkVehicle->m_bLostWreck, pNetworkVehicle->m_bHasLastState))
    {
        case VehicleSyncPolicy::eTakeover::RECREATE:
            pVehicle = RecreateVehicle(pNetworkVehicle, pNetworkVehicle->m_vecLastPosition,
                pNetworkVehicle->m_vecLastRight, pNetworkVehicle->m_vecLastForward, pNetworkVehicle->m_nLastColor1,
                pNetworkVehicle->m_nLastColor2, "takeover");
            if (pVehicle != nullptr)
            {
                break;
            }
            [[fallthrough]];
        case VehicleSyncPolicy::eTakeover::REMOVE:
            // nothing here to simulate: the vehicle goes on every game
            CNetworkVehicleManager::SendRemove(vehicleid, VehicleSync::REMOVE_BY_SYNCER);
            CNetworkVehicleManager::LogEvent("removal-requested", vehicleid, "reason=syncer source=takeover-%s wreck=%d",
                source, pNetworkVehicle->m_bLostWreck);
            DeleteRecord(pNetworkVehicle);
            return false;
        case VehicleSyncPolicy::eTakeover::KEEP:
            break;
    }

    pVehicle->SetVehicleCreatedBy(VehicleSyncPolicy::LocalCreatedBy(true, pNetworkVehicle->m_nCreatedBy));
    // the population's own time for it, as for a vehicle this game created (CNetworkVehicle::CreateHosted)
    pVehicle->m_nTimeTillWeNeedThisCar = CTimer::m_snTimeInMilliseconds + 5000;
    // a network player who drove it may be gone already (a player who left): no player-controlled car without a driver
    const bool bStatusFixed = pVehicle->m_pDriver == nullptr && pVehicle->m_nStatus == STATUS_PLAYER;
    if (bStatusFixed)
    {
        pVehicle->m_nStatus = static_cast<unsigned char>(STATUS_ABANDONED);
    }
    pNetworkVehicle->m_bCopyLost = false;
    pNetworkVehicle->m_bLostWreck = false;
    CNetworkVehicleManager::LogEvent("assignment-applied", vehicleid, "syncing=1 source=%s createdBy=%d driver=%d status-fixed=%d",
        source, pNetworkVehicle->m_nCreatedBy, pVehicle->m_pDriver != nullptr, bStatusFixed);
    return true;
}
}  // namespace

CNetworkVehicle* CNetworkVehicleManager::GetVehicle(int vehicleid)
{
	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_nVehicleId == vehicleid)
		{
			m_pVehicles[i]->GetLiveVehicle(); // private build: a stale pointer is cleared before it is read
			return m_pVehicles[i];
		}
	}
	return nullptr;
}
CNetworkVehicle* CNetworkVehicleManager::GetVehicle(CEntity* vehicle)
{
	// private build (vehicle sync protocol): lost copies have no vehicle
	if (vehicle == nullptr)
	{
		return nullptr;
	}

	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		// private build: only while the record still holds this vehicle (a new one in a reused pool slot doesn't match)
		if (m_pVehicles[i]->m_pVehicle == vehicle && m_pVehicles[i]->GetLiveVehicle() == vehicle)
		{
			return m_pVehicles[i];
		}
	}

	return nullptr;
}

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
	CNetworkVehicleManager::m_pVehicles.push_back(vehicle);
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle)
{
	auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
	if (it != m_pVehicles.end())
	{
		m_pVehicles.erase(it);
	}
}

void CNetworkVehicleManager::UpdateDriver(CVehicle* pVehicle)
{
	if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
	{
		Packets::Vehicles::VehicleDriverUpdate vehicleDriverUpdate{};
		CPlayerPed* pPlayerPed = FindPlayerPed(0);

		vehicleDriverUpdate.vehicleid = pNetworkVehicle->m_nVehicleId;
		vehicleDriverUpdate.pos = pVehicle->m_matrix->pos;
		vehicleDriverUpdate.roll = pVehicle->m_matrix->right;
		vehicleDriverUpdate.rot = pVehicle->m_matrix->up;
		vehicleDriverUpdate.velocity = pVehicle->m_vecMoveSpeed;
		vehicleDriverUpdate.turnSpeed = pVehicle->m_vecTurnSpeed;

		CWeapon& weapon = pPlayerPed->GetWeapon();
		vehicleDriverUpdate.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		vehicleDriverUpdate.playerWeapon.iWeaponState = weapon.m_nState;
		vehicleDriverUpdate.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		vehicleDriverUpdate.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		vehicleDriverUpdate.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		CKeySync::CollectState(vehicleDriverUpdate.playerKeys);

		vehicleDriverUpdate.color1 = pVehicle->m_nPrimaryColor;
		vehicleDriverUpdate.color2 = pVehicle->m_nSecondaryColor;

		vehicleDriverUpdate.health = pVehicle->m_fHealth;

		vehicleDriverUpdate.paintjob = pVehicle->GetRemapIndex();

		if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
		{
			CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
			vehicleDriverUpdate.miscComponentAngle = pAutomobile->m_wMiscComponentAngle;
		}

		if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
		{
			CBike* pBike = (CBike*)pVehicle;
			vehicleDriverUpdate.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
		}

		if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
		{
			CPlane* pPlane = (CPlane*)pVehicle;
			vehicleDriverUpdate.planeGearState = pPlane->m_fLandingGearStatus;
		}

		vehicleDriverUpdate.locked = pVehicle->m_eDoorLock;

		GetPacketFactory().Send(vehicleDriverUpdate);
	}
}

void CNetworkVehicleManager::UpdateIdle()
{
	CNetworkVehicleManager::RemoveHostedUnused();

	for (auto pNetworkVehicle : m_pVehicles)
	{
		CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle(); // private build
		if (pVehicle == nullptr)
			continue;

		if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->HasDriver())
		{
			Packets::Vehicles::VehicleIdleUpdate packet{};
			packet.vehicleid = pNetworkVehicle->m_nVehicleId;

			packet.pos = pVehicle->m_matrix->pos;
			packet.roll = pVehicle->m_matrix->right;
			packet.rot = pVehicle->m_matrix->up;
			packet.velocity = pVehicle->m_vecMoveSpeed;
			packet.turnSpeed = pVehicle->m_vecTurnSpeed;
			packet.color1 = pVehicle->m_nPrimaryColor;
			packet.color2 = pVehicle->m_nSecondaryColor;
			packet.health = pVehicle->m_fHealth;
			packet.paintjob = pVehicle->GetRemapIndex();

			if (CUtil::GetVehicleType(pVehicle) == eVehicleType::VEHICLE_PLANE)
			{
				CPlane* plane = (CPlane*)pVehicle;
				packet.planeGearState = plane->m_fLandingGearStatus;
			}

			packet.locked = pVehicle->m_eDoorLock;
			GetPacketFactory().Send(packet);
		}
	}
}

void CNetworkVehicleManager::UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed)
{
	 CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
	 if (pNetworkVehicle)
	 {
		 Packets::Vehicles::VehiclePassengerUpdate packet{};

		 CWeapon& weapon = pPlayerPed->GetWeapon();
		 packet.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		 packet.playerWeapon.iWeaponState = weapon.m_nState;
		 packet.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		 packet.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		 packet.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		 CKeySync::CollectState(packet.playerKeys);

		 packet.vehicleid = pNetworkVehicle->m_nVehicleId;
		 packet.driveby = CDriveBy::IsPedInDriveby(pPlayerPed);

		 // private build (vehicle sync protocol): -1 when the local ped isn't among the passengers (was 0, which the
		 // server recorded as seat 1)
		 packet.seatid = VehicleSync::SEAT_UNKNOWN;
		 for (int i = 0; i < pVehicle->m_nMaxPassengers && i < static_cast<int>(ARRAY_SIZE(pVehicle->m_apPassengers)); i++)
		 {
			 if (pVehicle->m_apPassengers[i] == pPlayerPed)
			 {
				 packet.seatid = i;
				 break;
			 }
		 }
		 if (packet.seatid == VehicleSync::SEAT_UNKNOWN)
		 {
			 LogFrequentEvent("seat-unknown", pNetworkVehicle->m_nVehicleId, "packet=passenger-update maxPassengers=%d",
				 pVehicle->m_nMaxPassengers);
		 }
		 GetPacketFactory().Send(packet);
	 }
}

uint8_t CNetworkVehicleManager::AddToTempList(CNetworkVehicle* networkVehicle)
{
	// private build (vehicle sync protocol): the next free slot after the last one handed out, not the first free one, so
	// a VEHICLE_CONFIRM still on its way for a slot is never matched with a newer vehicle; a record whose vehicle went
	// before its confirmation keeps its slot until VEHICLE_CONFIRM comes
	const int id = VehicleSyncPolicy::NextTempId(ms_nLastTempId, static_cast<int>(ARRAY_SIZE(m_apTempVehicles)),
		[](int slot) { return m_apTempVehicles[slot] == nullptr; });
	if (id < 0)
	{
		return 255;
	}

	m_apTempVehicles[id] = networkVehicle;
	ms_nLastTempId = id;
	return static_cast<uint8_t>(id);
}

void CNetworkVehicleManager::RemoveHostedUnused()
{
	// private build (vehicle sync protocol)
	if (!ms_bHooksLogged)
	{
		ms_bHooksLogged = true;
		LogEvent("destructor-hooks", -1, "hooked=%d/%d", ms_nDestructorHooks, ms_nDestructorSites);
	}

	// assignments whose vehicle never came
	for (const auto& entry : ms_pendingAssignments.Expire(GetTickCount()))
	{
		LogEvent("assignment-expired", entry.vehicleId, "syncing=%d model=%d", entry.bSyncing, entry.modelId);
	}

	for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
	{
		CNetworkVehicle* pNetworkVehicle = *it;
		// private build: the pool slot generation decides, not IsVehiclePointerValid (0x6E38F0), which a new vehicle in
		// the same slot passes and a vehicle out of the world fails
		CVehicle* vehicle = pNetworkVehicle->GetLiveVehicle();
		if (pNetworkVehicle->m_bSyncing)
		{
			// a vehicle of ours the ~CVehicle hook didn't see go
			if (vehicle == nullptr)
			{
				SendRemove(pNetworkVehicle->m_nVehicleId, VehicleSync::REMOVE_BY_SYNCER);
				LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId, "reason=syncer source=sweep");
				delete pNetworkVehicle;
				it = m_pVehicles.erase(it);
				continue;
			}
		}
		else if (vehicle != nullptr)
		{
			// private build: copies stay mission vehicles that are still needed, whatever changed them since
			GuardCopy(vehicle);
		}
		++it;
	}
}

void CNetworkVehicleManager::UpdateDamageSync()
{
	for (auto pNetworkVehicle : m_pVehicles)
	{
		if (pNetworkVehicle->m_bSyncing)
		{
			CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle(); // private build
			if (pVehicle && pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
			{
				CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
				if (pNetworkVehicle->m_oldDamageState != pAutomobile->m_damageManager)
				{
					pNetworkVehicle->m_oldDamageState = pAutomobile->m_damageManager;
					Packets::Vehicles::VehicleDamage packet{};
					packet.vehicleid = pNetworkVehicle->m_nVehicleId;
					packet.damageManager = pAutomobile->m_damageManager;
					GetPacketFactory().Send(packet);
				}
			}
		}
	}
}

// private build (vehicle sync protocol) ---------------------------------------------------------------------------------

CNetworkVehicle* CNetworkVehicleManager::GetTempVehicle(CEntity* vehicle)
{
    if (vehicle == nullptr)
    {
        return nullptr;
    }
    for (CNetworkVehicle* pTemp : m_apTempVehicles)
    {
        if (pTemp != nullptr && pTemp->m_pVehicle == vehicle && pTemp->GetLiveVehicle() == vehicle)
        {
            return pTemp;
        }
    }
    return nullptr;
}

void CNetworkVehicleManager::OnSpawned(CNetworkVehicle* pNetworkVehicle)
{
    const std::optional<bool> pending = ms_pendingAssignments.Take(
        pNetworkVehicle->m_nVehicleId, static_cast<uint16_t>(pNetworkVehicle->m_nModelId), GetTickCount());
    if (pending.has_value() && *pending != pNetworkVehicle->m_bSyncing)
    {
        ApplyAssignment(pNetworkVehicle, *pending, "deferred");
    }
}

void CNetworkVehicleManager::OnConfirmed(uint8_t tempid, int vehicleid)
{
    CNetworkVehicle* pTemp = tempid < ARRAY_SIZE(m_apTempVehicles) ? m_apTempVehicles[tempid] : nullptr;
    const bool bAlive = pTemp != nullptr && !pTemp->m_bRemovedBeforeConfirm && pTemp->GetLiveVehicle() != nullptr;

    switch (VehicleSyncPolicy::DecideConfirm(pTemp != nullptr, bAlive))
    {
        case VehicleSyncPolicy::eConfirm::UNKNOWN_TEMP:
            LogEvent("confirm-unknown", vehicleid, "tempid=%d", tempid);
            return;

        case VehicleSyncPolicy::eConfirm::REMOVE_UNCONFIRMED:
            // the game destroyed it while VEHICLE_SPAWN was on its way: it goes on the server and the other games too
            m_apTempVehicles[tempid] = nullptr;
            ms_pendingAssignments.Drop(vehicleid);
            SendRemove(vehicleid, VehicleSync::REMOVE_UNCONFIRMED);
            LogEvent("removal-requested", vehicleid, "reason=unconfirmed tempid=%d", tempid);
            delete pTemp;
            return;

        case VehicleSyncPolicy::eConfirm::ATTACH:
            break;
    }

    m_apTempVehicles[tempid] = nullptr;
    // the server gave this id to our vehicle, so a vehicle this game still has with it is gone
    if (CNetworkVehicle* pStale = GetVehicle(vehicleid))
    {
        RemoveForNetwork(pStale, "confirm-reuses-id");
    }
    pTemp->m_nVehicleId = vehicleid;
    Add(pTemp);
    OnSpawned(pTemp);
}

void CNetworkVehicleManager::OnAssignment(int vehicleid, uint16_t modelid, bool bSyncing)
{
    CNetworkVehicle* pNetworkVehicle = GetVehicle(vehicleid);
    const bool bKnown = pNetworkVehicle != nullptr;
    const bool bSameModel = bKnown && pNetworkVehicle->m_nModelId == modelid;

    switch (VehicleSyncPolicy::DecideAssignment(bKnown, bSameModel, bKnown && pNetworkVehicle->m_bSyncing, bSyncing))
    {
        case VehicleSyncPolicy::eAssignment::DEFER:
            ms_pendingAssignments.Store(vehicleid, modelid, bSyncing, GetTickCount());
            LogEvent("assignment-deferred", vehicleid, "syncing=%d model=%d known=%d", bSyncing, modelid, bKnown);
            return;

        case VehicleSyncPolicy::eAssignment::UNCHANGED:
            if (!bSyncing)
            {
                if (CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle())
                {
                    GuardCopy(pVehicle);
                }
            }
            LogFrequentEvent("assignment-unchanged", vehicleid, "syncing=%d", bSyncing);
            return;

        case VehicleSyncPolicy::eAssignment::APPLY:
            ApplyAssignment(pNetworkVehicle, bSyncing, "packet");
            return;
    }
}

void CNetworkVehicleManager::OnRemovePacket(int vehicleid)
{
    if (ms_pendingAssignments.Drop(vehicleid))
    {
        LogEvent("assignment-dropped", vehicleid, "reason=remove-packet");
    }
    if (CNetworkVehicle* pNetworkVehicle = GetVehicle(vehicleid))
    {
        RemoveForNetwork(pNetworkVehicle, "remove-packet");
    }
}

void CNetworkVehicleManager::RemoveForNetwork(CNetworkVehicle* pNetworkVehicle, const char* reason)
{
    // let go of the vehicle first: the ~CVehicle hook then finds no record for it
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle();
    pNetworkVehicle->m_pVehicle = nullptr;
    pNetworkVehicle->m_poolReference.Reset();

    if (pVehicle != nullptr)
    {
        if (pNetworkVehicle->m_nBlipHandle != -1)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(pVehicle));
        }
        CNetworkVehicle::TakePlayersOut(pVehicle, pNetworkVehicle->m_nVehicleId);
        CWorld::Remove(pVehicle);
        CWorld::RemoveReferencesToDeletedObject(pVehicle);
        delete pVehicle;
    }

    LogEvent("removal-applied", pNetworkVehicle->m_nVehicleId, "reason=%s syncing=%d vehicle=%d lost=%d", reason,
        pNetworkVehicle->m_bSyncing, pVehicle != nullptr, pNetworkVehicle->m_bCopyLost);
    DeleteRecord(pNetworkVehicle);
}

void CNetworkVehicleManager::SendRemove(int vehicleid, uint8_t reason)
{
    if (vehicleid < 0 || vehicleid >= Config::MAX_SERVER_VEHICLES)
    {
        return;
    }
    Packets::Vehicles::VehicleRemove packet{};
    packet.vehicleid = vehicleid;
    packet.reason = reason;
    GetPacketFactory().Send(packet);
}

void CNetworkVehicleManager::OnWorldRemove(CVehicle* pVehicle, uintptr_t site)
{
    ms_pLastRemovedVehicle = pVehicle;
    ms_nLastRemoveSite = site;

    CNetworkVehicle* pNetworkVehicle = GetVehicle(pVehicle);
    if (pNetworkVehicle == nullptr || pNetworkVehicle->m_bSyncing)
    {
        return;
    }

    // a script of the host's mission takes a vehicle another game syncs out of the world (DELETE_CAR deletes it right
    // after): the players in it get out first, as for a VEHICLE_REMOVE; ~CVehicle sends the removal
    CRunningScript* pScript = COpCodeSync::GetScriptRunningCommand();
    const bool bFamily = pScript != nullptr && CMissionMirror::IsFamilyScript(pScript);
    if (VehicleSyncPolicy::IsHostMissionDelete(CLocalPlayer::m_bIsHost, pNetworkVehicle->m_bSyncing, bFamily))
    {
        CNetworkVehicle::TakePlayersOut(pVehicle, pNetworkVehicle->m_nVehicleId);
        pNetworkVehicle->m_bHostMissionDelete = true;
        LogEvent("host-mission-delete", pNetworkVehicle->m_nVehicleId, "site=0x%X class=%s script=%.8s",
            static_cast<unsigned>(site), RemoveSiteClass(site), pScript->m_szName);
    }
}

void CNetworkVehicleManager::OnVehicleDestroyed(CVehicle* pVehicle)
{
    const uintptr_t site = pVehicle == ms_pLastRemovedVehicle ? ms_nLastRemoveSite : 0;
    if (pVehicle == ms_pLastRemovedVehicle)
    {
        ms_pLastRemovedVehicle = nullptr;
        ms_nLastRemoveSite = 0;
    }

    // ours, not confirmed yet: the slot stays taken, VEHICLE_CONFIRM removes it everywhere (REMOVE_UNCONFIRMED)
    for (CNetworkVehicle* pTemp : m_apTempVehicles)
    {
        if (pTemp != nullptr && pTemp->m_pVehicle == pVehicle)
        {
            pTemp->m_pVehicle = nullptr;
            pTemp->m_poolReference.Reset();
            pTemp->m_bRemovedBeforeConfirm = true;
            LogEvent("removed-before-confirm", -1, "tempid=%d site=0x%X class=%s", pTemp->m_nTempId,
                static_cast<unsigned>(site), RemoveSiteClass(site));
        }
    }

    for (size_t i = 0; i < m_pVehicles.size();)
    {
        CNetworkVehicle* pNetworkVehicle = m_pVehicles[i];
        if (pNetworkVehicle->m_pVehicle != pVehicle)
        {
            i++;
            continue;
        }

        // the pool slot is still taken while the destructors run: a record whose generation differs held a stale
        // pointer to an earlier vehicle in this slot
        const bool bSameVehicle = pNetworkVehicle->m_poolReference.Matches(CPools::ms_pVehiclePool, pVehicle);
        pNetworkVehicle->m_pVehicle = nullptr;
        pNetworkVehicle->m_poolReference.Reset();
        pNetworkVehicle->m_nBlipHandle = -1;  // the destructor clears the vehicle's blips
        if (!bSameVehicle)
        {
            pNetworkVehicle->m_bCopyLost = !pNetworkVehicle->m_bSyncing;
            LogEvent("pointer-stale", pNetworkVehicle->m_nVehicleId, "syncing=%d source=destructor",
                pNetworkVehicle->m_bSyncing);
            i++;
            continue;
        }

        const bool bWrecked = pVehicle->m_nStatus == STATUS_WRECKED;
        CRunningScript* pScript = COpCodeSync::GetScriptRunningCommand();
        const bool bHostMissionDelete = pNetworkVehicle->m_bHostMissionDelete ||
            VehicleSyncPolicy::IsHostMissionDelete(CLocalPlayer::m_bIsHost, pNetworkVehicle->m_bSyncing,
                pScript != nullptr && CMissionMirror::IsFamilyScript(pScript));

        switch (VehicleSyncPolicy::DecideVehicleGone(true, pNetworkVehicle->m_bSyncing, bHostMissionDelete, bWrecked))
        {
            case VehicleSyncPolicy::eVehicleGone::SEND_REMOVE:
                SendRemove(pNetworkVehicle->m_nVehicleId, VehicleSync::REMOVE_BY_SYNCER);
                LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId, "reason=syncer site=0x%X class=%s",
                    static_cast<unsigned>(site), RemoveSiteClass(site));
                DeleteRecord(pNetworkVehicle);
                continue;

            case VehicleSyncPolicy::eVehicleGone::HOST_MISSION_REMOVE:
                SendRemove(pNetworkVehicle->m_nVehicleId, VehicleSync::REMOVE_BY_HOST_MISSION);
                LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId, "reason=host-mission site=0x%X class=%s",
                    static_cast<unsigned>(site), RemoveSiteClass(site));
                DeleteRecord(pNetworkVehicle);
                continue;

            case VehicleSyncPolicy::eVehicleGone::KEEP_FOR_RECREATE:
            case VehicleSyncPolicy::eVehicleGone::KEEP_WRECK:
            case VehicleSyncPolicy::eVehicleGone::REMOVE_AT_CONFIRM:
                // a copy this game destroyed itself: its syncer still has it
                pNetworkVehicle->m_bCopyLost = true;
                pNetworkVehicle->m_bLostWreck = bWrecked;
                pNetworkVehicle->m_bHostMissionDelete = false;
                LogEvent("copy-destroyed", pNetworkVehicle->m_nVehicleId, "wreck=%d site=0x%X class=%s", bWrecked,
                    static_cast<unsigned>(site), RemoveSiteClass(site));
                break;
        }
        i++;
    }
}

void CNetworkVehicleManager::OnLocalCreatedByChanged(CVehicle* pVehicle, int createdBy)
{
    CNetworkVehicle* pNetworkVehicle = GetVehicle(pVehicle);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    const auto change = VehicleSyncPolicy::DecideCreatedByChange(
        pNetworkVehicle->m_bSyncing, CLocalPlayer::m_bIsHost, static_cast<uint8_t>(createdBy));
    if (change.bRecordTakesIt)
    {
        pNetworkVehicle->m_nCreatedBy = static_cast<unsigned char>(createdBy);
    }
    if (change.bRestoreMission)
    {
        // not the hooked call sites: the plugin-sdk wrapper calls 0x6D5D70 itself
        pVehicle->SetVehicleCreatedBy(MISSION_VEHICLE);
        LogEvent("copy-guard", pNetworkVehicle->m_nVehicleId, "createdBy=%d kept=%d", createdBy, change.bRecordTakesIt);
    }
}

void CNetworkVehicleManager::OnDestructorHooksInstalled(int hooked, int total)
{
    // CCore::Init runs during static initialization: the log comes with the first sweep (RemoveHostedUnused)
    ms_nDestructorHooks = hooked;
    ms_nDestructorSites = total;
}

void CNetworkVehicleManager::GuardCopy(CVehicle* pVehicle)
{
    if (pVehicle->m_nCreatedBy != MISSION_VEHICLE)
    {
        pVehicle->SetVehicleCreatedBy(MISSION_VEHICLE);
    }
    pVehicle->m_nTimeTillWeNeedThisCar = COPY_KEEP_TIME;
}

CVehicle* CNetworkVehicleManager::RecreateLostCopy(CNetworkVehicle* pNetworkVehicle, const CVector& position,
    const CVector& right, const CVector& forward, uint8_t color1, uint8_t color2)
{
    if (!pNetworkVehicle->m_bCopyLost || pNetworkVehicle->m_bLostWreck || pNetworkVehicle->m_bSyncing ||
        pNetworkVehicle->GetLiveVehicle() != nullptr)
    {
        return nullptr;
    }
    if (!pNetworkVehicle->m_recreateThrottle.ShouldAttempt(GetTickCount()))
    {
        return nullptr;
    }
    return RecreateVehicle(pNetworkVehicle, position, right, forward, color1, color2, "update");
}

static void WriteVehicleSyncRecord(const char* event, int vehicleid, const char* format, va_list args)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 512)
    {
        return;
    }

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    {
        return;
    }
    char* fileName = strrchr(path, '\\');
    if (!fileName)
    {
        return;
    }
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_vehicle_sync.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char detail[384] = "";
        if (format != nullptr)
        {
            std::vsnprintf(detail, sizeof(detail), format, args);
        }
        fprintf(file, "t=%u frame=%u event=%s vehicle=%d %s host=%d records=%u temp=%u pending=%u lost=%u\n",
            GetTickCount(), CTimer::m_FrameCounter, event, vehicleid, detail, CLocalPlayer::m_bIsHost,
            static_cast<unsigned>(CNetworkVehicleManager::m_pVehicles.size()), CountTemp(),
            static_cast<unsigned>(ms_pendingAssignments.Size()), CountLost());
        ++records;
    }
    fclose(file);
}

void CNetworkVehicleManager::LogEvent(const char* event, int vehicleid, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    WriteVehicleSyncRecord(event, vehicleid, format, args);
    va_end(args);
}

void CNetworkVehicleManager::LogFrequentEvent(const char* event, int vehicleid, const char* format, ...)
{
    if (!ShouldLogFrequent(event))
    {
        return;
    }
    va_list args;
    va_start(args, format);
    WriteVehicleSyncRecord(event, vehicleid, format, args);
    va_end(args);
}