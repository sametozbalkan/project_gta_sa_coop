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
//   (CPendingVehicleAssignments) until its VEHICLE_SPAWN comes (a VEHICLE_CONFIRM of the id drops it as stale); one that
//   expires while it says "syncer" removes the vehicle (VehicleSyncPolicy::DecideExpiredAssignment).
// - The server stamps the spawns and removals it relays with its own clock (EntityLifecycle::UsesServerTime), so every
//   game handles them in the server's order, with VEHICLE_CONFIRM, ASSIGN_VEHICLE and the join snapshot.
// - Every vehicle the game destroys passes CVehicle::~CVehicle (VehicleHooks.cpp hooks its five callers), where its record
//   lets go of it: a vehicle this game syncs goes on every game (VEHICLE_REMOVE), a temp record keeps its slot until
//   VEHICLE_CONFIRM and removes the vehicle then (REMOVE_UNCONFIRMED), a copy the host's mission deleted goes on every
//   game (REMOVE_BY_HOST_MISSION), any other copy is recreated from its syncer's next update (a wreck isn't).
// - Copies stay MISSION_VEHICLE and "needed" (GuardCopy), so this game's population, garages and wreck clean-up don't
//   delete them; the local createdBy changes of scripts are kept in the record instead (OnLocalCreatedByChanged).
// - Stored pointers are checked against the pool slot generation they had (CVehiclePoolReference).
// - A temp record gives its slot back after TEMP_RECORD_MAX_AGE_MS without VEHICLE_CONFIRM; a confirmation for an empty
//   slot removes the server's vehicle on every game (nobody would sync it).
// - While this game starts over (CWorld::ClearForRestart, VehicleHooks.cpp) the vehicles it syncs go with
//   REMOVE_SYNCER_RESTARTED, and the server gives the cars of the host's scripts back to the host.
// - A script of the host's mission family that deletes a vehicle removes it on every game (REMOVE_BY_HOST_MISSION),
//   whatever this game's record says about syncing it.
// - The local player's car exits that don't come from the exit key (a script's task, being dragged out, a warp) send
//   VEHICLE_EXIT too (ProcessLocalExit, VehicleSyncPolicy::CLocalExitTracker).
namespace
{
CPendingVehicleAssignments ms_pendingAssignments;
int ms_nLastTempId = -1;
CVehicle* ms_pLastRemovedVehicle = nullptr;  // the vehicle the last hooked CWorld::Remove took out of the world
uintptr_t ms_nLastRemoveSite = 0;            // and that call's site
int ms_nDestructorHooks = 0;                 // ~CVehicle call sites redirected / checked (VehicleHooks.cpp)
int ms_nDestructorSites = 0;
bool ms_bHooksLogged = false;
bool ms_bRestartHooked = false;              // the CWorld::ClearForRestart call is redirected (VehicleHooks.cpp)
bool ms_bRestartClear = false;               // CWorld::ClearForRestart runs: this game starts over
const CVehicle* ms_pDestroyingVehicle = nullptr;  // whose ~CVehicle runs (OnVehicleDestroyed .. OnVehicleDestructorDone)
int ms_nDestroyingVehicleId = -1;                 // and the id of the record that held it
VehicleSyncPolicy::CLocalExitTracker ms_localExit;  // the local player's car exits (ProcessLocalExit, OnKeyExitSent)

// --coopdiag: written by the game thread, read by CDiagLog's thread (plain counters, as the other modules' lines)
struct SDiagCounters
{
    unsigned records = 0;  // refreshed by every sweep (RemoveHostedUnused)
    unsigned syncing = 0;
    unsigned temp = 0;
    unsigned pending = 0;
    unsigned lost = 0;
    unsigned tempExpired = 0;
    unsigned tempFull = 0;
    unsigned confirmUnknown = 0;
    unsigned createFailed = 0;
    unsigned recreated = 0;
    unsigned assignmentsExpired = 0;
    unsigned removalsSent = 0;
    unsigned removalsApplied = 0;
    unsigned restartRemovals = 0;
    unsigned handlerVehicleGone = 0;
    unsigned confirms = 0;           // private build (review 2): VEHICLE_CONFIRMs attached, the slow ones included
    unsigned createdByAtConfirm = 0;  // createdBy changes found at VEHICLE_CONFIRM
    unsigned exitsKey = 0;           // VEHICLE_EXITs of the exit key (TaskHooks.cpp) and of the other exits
    unsigned exitsTask = 0;
    unsigned exitsDragged = 0;
    unsigned exitsWarped = 0;
};
SDiagCounters ms_diag;

// --coopdiag: the routine events, one summary line a minute instead of one line each: the population's removals of this
// game's random and parked cars (VEHICLE_REMOVE sent), the other games' removals of theirs (VEHICLE_REMOVE handled) and
// the confirmations of this game's new vehicles (VehicleSyncPolicy::IsConfirmWorthALine)
constexpr uint32_t ROUTINE_SUMMARY_MS = 60000;
struct SRoutineCounts
{
    unsigned population = 0;  // CCarCtrl::PossiblyRemoveVehicle
    unsigned wreck = 0;       // its wreck clean-up
    unsigned unhooked = 0;    // not through a hooked CWorld::Remove (CCarCtrl::RemoveCarsIfThePoolGetsFull)
    unsigned applied = 0;     // another game's REMOVE_BY_SYNCER of a random or parked car
    unsigned confirms = 0;    // VEHICLE_CONFIRM attached quickly, nothing replaced
    uint32_t since = 0;
};
SRoutineCounts ms_routine;
unsigned ms_nRestartRemovals = 0;  // during the current CWorld::ClearForRestart
unsigned ms_nRestartCopiesLost = 0;

// private build (vehicle sync protocol review 2): the population's own cars, random traffic and car generators' parked cars
// (CREATED_BY_PARKED, which the population removes as it does random ones)
bool IsPopulationCar(uint8_t createdBy)
{
    return createdBy == VehicleSync::CREATED_BY_RANDOM || createdBy == VehicleSync::CREATED_BY_PARKED;
}

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

// --coopdiag: a removal of this game's random or parked car by its own population, counted in the routine summary instead
// of a line of its own (the population removes only vehicles CanBeDeleted 0x6D1180 allows, and its wreck clean-up any
// wreck)
bool CountRoutineRemoval(uintptr_t site, uint8_t createdBy)
{
    if (!IsPopulationCar(createdBy))
    {
        return false;
    }
    const char* siteClass = RemoveSiteClass(site);
    if (strcmp(siteClass, "population") == 0)
    {
        ms_routine.population++;
    }
    else if (strcmp(siteClass, "population-wreck") == 0)
    {
        ms_routine.wreck++;
    }
    else if (strcmp(siteClass, "unhooked") == 0)
    {
        ms_routine.unhooked++;
    }
    else
    {
        return false;
    }
    return true;
}

void FlushRoutineSummary(uint32_t now)
{
    if (static_cast<uint32_t>(now - ms_routine.since) < ROUTINE_SUMMARY_MS)
    {
        return;
    }
    const SRoutineCounts counts = ms_routine;
    ms_routine = SRoutineCounts{};
    ms_routine.since = now;
    if (counts.population + counts.wreck + counts.unhooked + counts.applied + counts.confirms == 0)
    {
        return;
    }
    CNetworkVehicleManager::LogEvent("routine", -1,
        "window=%us population=%u wreck=%u pool-full=%u applied=%u confirms=%u",
        static_cast<unsigned>(now - counts.since) / 1000, counts.population, counts.wreck, counts.unhooked,
        counts.applied, counts.confirms);
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

// --coopdiag: the counts of the vehicle-sync: line, on the game thread
void RefreshDiagCounts()
{
    unsigned syncing = 0;
    unsigned lost = 0;
    for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        syncing += pNetworkVehicle->m_bSyncing;
        lost += pNetworkVehicle->m_bCopyLost;
    }
    ms_diag.records = static_cast<unsigned>(CNetworkVehicleManager::m_pVehicles.size());
    ms_diag.syncing = syncing;
    ms_diag.lost = lost;
    ms_diag.temp = CountTemp();
    ms_diag.pending = static_cast<unsigned>(ms_pendingAssignments.Size());
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
    const float heading = VehicleSync::HeadingFromForward(forward.x, forward.y);
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
    ms_diag.recreated++;
    CNetworkVehicleManager::LogEvent("copy-recreated", pNetworkVehicle->m_nVehicleId,
        "model=%d reason=%s syncing=%d pos=%.1f,%.1f,%.1f", pNetworkVehicle->m_nModelId, reason,
        pNetworkVehicle->m_bSyncing, position.x, position.y, position.z);
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

		// private build (vehicle sync protocol): also while the local player driving it gets out, who sends no driver
		// updates then (VehicleSyncPolicy::ChooseLocalUpdate)
		if (pNetworkVehicle->m_bSyncing &&
			!VehicleSyncPolicy::IsDrivenForIdleUpdates(pVehicle->m_pDriver != nullptr,
				pVehicle->m_pDriver != nullptr && IsLocalPlayerLeavingVehicle(pVehicle->m_pDriver)))
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
		LogEvent("destructor-hooks", -1, "hooked=%d/%d restart-hook=%d", ms_nDestructorHooks, ms_nDestructorSites,
			ms_bRestartHooked);
	}

	const uint32_t now = GetTickCount();

	// assignments whose vehicle never came (VehicleSyncPolicy::DecideExpiredAssignment)
	for (const auto& entry : ms_pendingAssignments.Expire(now))
	{
		ms_diag.assignmentsExpired++;
		CNetworkVehicle* pRecord = GetVehicle(entry.vehicleId);
		const int recordModel = pRecord != nullptr ? pRecord->m_nModelId : -1;
		if (pRecord != nullptr && pRecord->m_nModelId == entry.modelId)
		{
			// the vehicle is here after all (nothing should keep an entry then): the assignment applies to it
			LogEvent("assignment-expired", entry.vehicleId, "syncing=%d model=%d applied=1", entry.bSyncing,
				entry.modelId);
			if (pRecord->m_bSyncing != entry.bSyncing)
			{
				ApplyAssignment(pRecord, entry.bSyncing, "expired");
			}
			continue;
		}

		switch (VehicleSyncPolicy::DecideExpiredAssignment(entry.bSyncing, pRecord != nullptr))
		{
			case VehicleSyncPolicy::eExpiredAssignment::NOTHING:
				LogEvent("assignment-expired", entry.vehicleId, "syncing=0 model=%d record-model=%d", entry.modelId,
					recordModel);
				break;
			case VehicleSyncPolicy::eExpiredAssignment::REMOVE_EVERYWHERE_AND_LOCAL_RECORD:
				RemoveForNetwork(pRecord, "assignment-expired-other-model");
				[[fallthrough]];
			case VehicleSyncPolicy::eExpiredAssignment::REMOVE_EVERYWHERE:
				// the server counts on this game for a vehicle it doesn't have
				SendRemove(entry.vehicleId, VehicleSync::REMOVE_BY_SYNCER);
				LogEvent("assignment-expired", entry.vehicleId,
					"syncing=1 model=%d record-model=%d removal-requested=syncer", entry.modelId, recordModel);
				break;
		}
	}

	// temp records whose VEHICLE_CONFIRM never came give their slot back; a vehicle of theirs stays in this game only
	for (int slot = 0; slot < static_cast<int>(ARRAY_SIZE(m_apTempVehicles)); slot++)
	{
		CNetworkVehicle* pTemp = m_apTempVehicles[slot];
		if (pTemp == nullptr || !VehicleSyncPolicy::IsTempRecordExpired(pTemp->m_nTempSince, now))
		{
			continue;
		}
		m_apTempVehicles[slot] = nullptr;
		ms_diag.tempExpired++;
		LogEvent("temp-expired", -1, "tempid=%d model=%d createdBy=%d vehicle=%d removed-before-confirm=%d", slot,
			pTemp->m_nModelId, pTemp->m_nCreatedBy, pTemp->GetLiveVehicle() != nullptr, pTemp->m_bRemovedBeforeConfirm);
		delete pTemp;
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
				LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId, "reason=syncer syncing=1 source=sweep");
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

	// private build (--coopdiag)
	RefreshDiagCounts();
	FlushRoutineSummary(now);
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

    // an assignment waiting for this id is about an older vehicle that had it: the server sends VEHICLE_CONFIRM before
    // any ASSIGN_VEHICLE of the new one, and both carry its clock (upstream applied the waiting entry to the new vehicle)
    if (ms_pendingAssignments.Drop(vehicleid))
    {
        LogEvent("assignment-dropped", vehicleid, "reason=confirm tempid=%d", tempid);
    }

    switch (VehicleSyncPolicy::DecideConfirm(pTemp != nullptr, bAlive))
    {
        case VehicleSyncPolicy::eConfirm::UNKNOWN_TEMP:
            // no record waits in that slot (it expired: VehicleSyncPolicy::IsTempRecordExpired): nobody would sync the
            // server's vehicle, so it goes on every game, as one destroyed before its confirmation
            ms_diag.confirmUnknown++;
            SendRemove(vehicleid, VehicleSync::REMOVE_UNCONFIRMED);
            LogEvent("confirm-unknown", vehicleid, "tempid=%d removal-requested=unconfirmed", tempid);
            return;

        case VehicleSyncPolicy::eConfirm::REMOVE_UNCONFIRMED:
            // the game destroyed it while VEHICLE_SPAWN was on its way: it goes on the server and the other games too
            m_apTempVehicles[tempid] = nullptr;
            SendRemove(vehicleid, VehicleSync::REMOVE_UNCONFIRMED);
            LogEvent("removal-requested", vehicleid, "reason=unconfirmed syncing=1 tempid=%d model=%d", tempid,
                pTemp->m_nModelId);
            delete pTemp;
            return;

        case VehicleSyncPolicy::eConfirm::ATTACH:
            break;
    }

    m_apTempVehicles[tempid] = nullptr;
    // the server gave this id to our vehicle, so a vehicle this game still has with it is gone
    const bool bReplaced = GetVehicle(vehicleid) != nullptr;
    if (CNetworkVehicle* pStale = GetVehicle(vehicleid))
    {
        RemoveForNetwork(pStale, "confirm-reuses-id");
    }
    pTemp->m_nVehicleId = vehicleid;
    Add(pTemp);

    // private build (vehicle sync protocol review 2): a createdBy change while VEHICLE_SPAWN was on its way
    // (VehicleSyncPolicy::DecideCreatedByAtConfirm); the record's value is the one VEHICLE_SPAWN sent
    const uint8_t spawnCreatedBy = pTemp->m_nCreatedBy;
    CVehicle* pVehicle = pTemp->GetLiveVehicle();
    const uint8_t liveCreatedBy = pVehicle != nullptr ? static_cast<uint8_t>(pVehicle->m_nCreatedBy) : spawnCreatedBy;
    const auto createdBy = VehicleSyncPolicy::DecideCreatedByAtConfirm(CLocalPlayer::m_bIsHost, spawnCreatedBy, liveCreatedBy);
    if (createdBy.bChanged)
    {
        pTemp->m_nCreatedBy = liveCreatedBy;
        ms_diag.createdByAtConfirm++;
    }
    if (createdBy.bSend)
    {
        Packets::Vehicles::SetVehicleCreatedBy packet{};
        packet.vehicleid = vehicleid;
        packet.createdBy = static_cast<eVehicleCreatedBy>(liveCreatedBy);
        GetPacketFactory().Send(packet);
    }

    // --coopdiag: the usual quick confirmation goes into the routine summary line
    ms_diag.confirms++;
    const uint32_t waited = GetTickCount() - pTemp->m_nTempSince;
    if (VehicleSyncPolicy::IsConfirmWorthALine(waited, bReplaced, createdBy.bChanged))
    {
        LogEvent("confirm-attach", vehicleid, "tempid=%d model=%d createdBy=%d waited=%ums replaced=%d spawn-createdBy=%d "
            "createdBy-sent=%d", tempid, pTemp->m_nModelId, pTemp->m_nCreatedBy, static_cast<unsigned>(waited), bReplaced,
            spawnCreatedBy, createdBy.bSend);
    }
    else
    {
        ms_routine.confirms++;
    }
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

void CNetworkVehicleManager::OnRemovePacket(int vehicleid, uint8_t reason)
{
    if (ms_pendingAssignments.Drop(vehicleid))
    {
        LogEvent("assignment-dropped", vehicleid, "reason=remove-packet");
    }
    if (CNetworkVehicle* pNetworkVehicle = GetVehicle(vehicleid))
    {
        RemoveForNetwork(pNetworkVehicle, "remove-packet", reason);
    }
    else
    {
        // nothing here with that id: a removal handled before the spawn of its vehicle would leave a ghost
        LogFrequentEvent("remove-unknown", vehicleid, "reason=%s", VehicleSync::RemoveReasonName(reason));
    }
}

void CNetworkVehicleManager::RemoveForNetwork(CNetworkVehicle* pNetworkVehicle, const char* source, int packetReason)
{
    // let go of the vehicle first: the ~CVehicle hook then finds no record for it
    CVehicle* pVehicle = pNetworkVehicle->GetLiveVehicle();
    pNetworkVehicle->m_pVehicle = nullptr;
    pNetworkVehicle->m_poolReference.Reset();

    unsigned int playersOut = 0;
    if (pVehicle != nullptr)
    {
        if (pNetworkVehicle->m_nBlipHandle != -1)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(pVehicle));
        }
        playersOut = CNetworkVehicle::TakePlayersOut(pVehicle, pNetworkVehicle->m_nVehicleId);
        CWorld::Remove(pVehicle);
        CWorld::RemoveReferencesToDeletedObject(pVehicle);
        delete pVehicle;
    }

    ms_diag.removalsApplied++;
    // --coopdiag: another game's population removing a random or parked car of its own is routine (summary line)
    const bool bRoutine = packetReason == VehicleSync::REMOVE_BY_SYNCER && !pNetworkVehicle->m_bSyncing &&
        IsPopulationCar(pNetworkVehicle->m_nCreatedBy) && playersOut == 0;
    if (bRoutine)
    {
        ms_routine.applied++;
    }
    else
    {
        LogEvent("removal-applied", pNetworkVehicle->m_nVehicleId,
            "source=%s reason=%s syncing=%d vehicle=%d lost=%d model=%d createdBy=%d players-out=%u", source,
            packetReason >= 0 ? VehicleSync::RemoveReasonName(static_cast<unsigned>(packetReason)) : "-",
            pNetworkVehicle->m_bSyncing, pVehicle != nullptr, pNetworkVehicle->m_bCopyLost, pNetworkVehicle->m_nModelId,
            pNetworkVehicle->m_nCreatedBy, playersOut);
    }
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
    ms_diag.removalsSent++;
}

bool CNetworkVehicleManager::StillHolds(int vehicleid, const CNetworkVehicle* pNetworkVehicle, const CVehicle* pVehicle)
{
    // only pointers are compared: the old record may be freed already
    CNetworkVehicle* pCurrent = GetVehicle(vehicleid);
    if (pVehicle != nullptr && pCurrent != nullptr && pCurrent == pNetworkVehicle && pCurrent->GetLiveVehicle() == pVehicle)
    {
        return true;
    }
    ms_diag.handlerVehicleGone++;
    LogFrequentEvent("handler-vehicle-gone", vehicleid, "record=%d", pCurrent != nullptr && pCurrent == pNetworkVehicle);
    return false;
}

namespace
{
// private build (vehicle sync protocol review 2): the tasks of a car exit in the local player's primary task slots or in
// the subtask chain of the task there (CTaskManager::FindTaskByType 0x6817D0, plugin-sdk CTaskManager.cpp; gta-reversed
// TaskManager.cpp, FindFirstTaskOfType). The exit key puts its exit into the primary slot (CPlayerInfo::Process,
// SetTask(task, 3) at 0x570418 and 0x5704BD); a script command's task goes there too (CEventHandler::
// ComputeScriptCommandResponse 0x4BA7C0, gta-reversed EventHandler.cpp), and so does whatever AddTaskPrimaryMaybeInGroup
// gives a player (a CEventScriptCommand for TASK_PRIMARY_PRIMARY, gta-reversed PedIntelligence.cpp); an event's response
// would sit in one of the two event response slots.
const int exitTaskSlots[] = {TASK_PRIMARY_EVENT_RESPONSE_TEMP, TASK_PRIMARY_EVENT_RESPONSE_NONTEMP, TASK_PRIMARY_PRIMARY};

// Every exit of a ped runs a CTaskComplexLeaveCar, itself or as the subtask of the task that wants the ped out
// (TASK_LEAVE_ANY_CAR's CTaskComplexLeaveAnyCar, LEAVE_CAR_AND_FLEE/WANDER/DIE, a sequence, the AI tasks): the 56 calls
// of its constructor 0x63B8C0 (54 in .text, 2 in .HOODLUM; an E8 scan of C:\Games\GTASA_Coop\gta_sa.exe, ReVa
// cross-references name the callers: CPlayerInfo::Process, ProcessCommands1400To1499 and 1500To1599, the event handler,
// the group event handler, the tasks' CreateSubTask/CreateFirstSubTask, and in .HOODLUM the copy of
// CCarAI::TellOccupantsToLeaveCar 0x41C760, which sets it into every occupant's primary slot, gta-reversed CarAI.cpp).
const int leaveTaskTypes[] = {TASK_COMPLEX_LEAVE_CAR};

// A jacker drags the player out: its CTaskComplexEnterCar gives the victim CTaskComplexCarSlowBeDraggedOutAndStandUp (a
// bike's rider CTaskSimpleBikeJacked) through AddTaskPrimaryMaybeInGroup (gta-reversed TaskComplexEnterCar.cpp), not a
// leave task; the victim stays the vehicle's driver until the drag sets it out.
const int draggedOutTaskTypes[] = {TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP, TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT,
    TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT, TASK_SIMPLE_CAR_SLOW_BE_DRAGGED_OUT, TASK_SIMPLE_CAR_QUICK_BE_DRAGGED_OUT,
    TASK_SIMPLE_BIKE_JACKED};

template <size_t N> bool HasExitTask(CPed* pPed, const int (&taskTypes)[N])
{
    CTaskManager& taskManager = pPed->m_pIntelligence->m_TaskMgr;
    for (int slot : exitTaskSlots)
    {
        for (int taskType : taskTypes)
        {
            if (taskManager.FindTaskByType(slot, taskType) != nullptr)
            {
                return true;
            }
        }
    }
    return false;
}

const char* ExitNoticeName(VehicleSyncPolicy::eExitNotice notice)
{
    switch (notice)
    {
        case VehicleSyncPolicy::eExitNotice::LEAVE: return "leave";
        case VehicleSyncPolicy::eExitNotice::DRAGGED_OUT: return "dragged-out";
        case VehicleSyncPolicy::eExitNotice::WARPED: return "warped";
        default: return "none";
    }
}
}  // namespace

bool CNetworkVehicleManager::IsLocalPlayerLeavingVehicle(CPed* pPed)
{
    // private build (vehicle sync protocol review 2): every exit, the dragged-out one too (was the exit key's only)
    return pPed != nullptr && pPed == FindPlayerPed(0) && pPed->m_pIntelligence != nullptr &&
           (HasExitTask(pPed, leaveTaskTypes) || HasExitTask(pPed, draggedOutTaskTypes));
}

void CNetworkVehicleManager::ProcessLocalExit(CPlayerPed* pPlayerPed)
{
    // private build (vehicle sync protocol review 2): the exits that don't come from the exit key (CLocalExitTracker)
    const bool bInVehicle = pPlayerPed != nullptr && pPlayerPed->m_nPedFlags.bInVehicle && pPlayerPed->m_pVehicle != nullptr;
    const bool bTasks = bInVehicle && pPlayerPed->m_pIntelligence != nullptr;
    const bool bDraggedOut = bTasks && HasExitTask(pPlayerPed, draggedOutTaskTypes);
    const bool bLeaving = bTasks && HasExitTask(pPlayerPed, leaveTaskTypes);
    const VehicleSyncPolicy::eExitNotice notice = ms_localExit.Update(bInVehicle, bLeaving, bDraggedOut);
    if (notice == VehicleSyncPolicy::eExitNotice::NONE)
    {
        return;
    }

    Packets::Vehicles::VehicleExit packet{};
    packet.bDraggedOut = notice == VehicleSyncPolicy::eExitNotice::DRAGGED_OUT;
    packet.bForce = notice == VehicleSyncPolicy::eExitNotice::WARPED;
    GetPacketFactory().Send(packet);

    switch (notice)
    {
        case VehicleSyncPolicy::eExitNotice::LEAVE: ms_diag.exitsTask++; break;
        case VehicleSyncPolicy::eExitNotice::DRAGGED_OUT: ms_diag.exitsDragged++; break;
        default: ms_diag.exitsWarped++; break;
    }
    const CNetworkVehicle* pRecord = bInVehicle ? GetVehicle(pPlayerPed->m_pVehicle) : nullptr;
    LogEvent("exit-notice", pRecord != nullptr ? pRecord->m_nVehicleId : -1, "kind=%s", ExitNoticeName(notice));
}

void CNetworkVehicleManager::OnKeyExitSent()
{
    ms_localExit.OnKeyExitSent();
    ms_diag.exitsKey++;
}

void CNetworkVehicleManager::OnWorldRemove(CVehicle* pVehicle, uintptr_t site)
{
    ms_pLastRemovedVehicle = pVehicle;
    ms_nLastRemoveSite = site;

    CNetworkVehicle* pNetworkVehicle = GetVehicle(pVehicle);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    // a script of the host's mission takes a vehicle out of the world (DELETE_CAR deletes it right after): the players in
    // it get out first, as for a VEHICLE_REMOVE; ~CVehicle sends the removal (REMOVE_BY_HOST_MISSION). Also for a vehicle
    // this game's record still says it syncs: the server may have given it to another game a moment ago
    CRunningScript* pScript = COpCodeSync::GetScriptRunningCommand();
    const bool bFamily = pScript != nullptr && CMissionMirror::IsFamilyScript(pScript);
    if (VehicleSyncPolicy::IsHostMissionDelete(CLocalPlayer::m_bIsHost, bFamily))
    {
        CNetworkVehicle::TakePlayersOut(pVehicle, pNetworkVehicle->m_nVehicleId);
        pNetworkVehicle->m_bHostMissionDelete = true;
        LogEvent("host-mission-delete", pNetworkVehicle->m_nVehicleId, "site=0x%X class=%s script=%.8s syncing=%d",
            static_cast<unsigned>(site), RemoveSiteClass(site), pScript->m_szName, pNetworkVehicle->m_bSyncing);
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

    // for the logs of what the destructor then does to the occupants (CNetworkPlayer::KeepFlaggedPed), until
    // OnVehicleDestructorDone: the records below let go of the vehicle first
    ms_pDestroyingVehicle = pVehicle;
    ms_nDestroyingVehicleId = -1;

    // ours, not confirmed yet: the slot stays taken, VEHICLE_CONFIRM removes it everywhere (REMOVE_UNCONFIRMED)
    for (CNetworkVehicle* pTemp : m_apTempVehicles)
    {
        if (pTemp != nullptr && pTemp->m_pVehicle == pVehicle)
        {
            pTemp->m_pVehicle = nullptr;
            pTemp->m_poolReference.Reset();
            pTemp->m_bRemovedBeforeConfirm = true;
            LogEvent("removed-before-confirm", -1, "tempid=%d syncing=1 site=0x%X class=%s restarting=%d",
                pTemp->m_nTempId, static_cast<unsigned>(site), RemoveSiteClass(site), ms_bRestartClear);
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
        ms_nDestroyingVehicleId = pNetworkVehicle->m_nVehicleId;

        const bool bWrecked = pVehicle->m_nStatus == STATUS_WRECKED;
        CRunningScript* pScript = COpCodeSync::GetScriptRunningCommand();
        const bool bHostMissionDelete = pNetworkVehicle->m_bHostMissionDelete ||
            VehicleSyncPolicy::IsHostMissionDelete(
                CLocalPlayer::m_bIsHost, pScript != nullptr && CMissionMirror::IsFamilyScript(pScript));

        switch (VehicleSyncPolicy::DecideVehicleGone(
            true, pNetworkVehicle->m_bSyncing, bHostMissionDelete, bWrecked, ms_bRestartClear))
        {
            case VehicleSyncPolicy::eVehicleGone::SEND_REMOVE:
                SendRemove(pNetworkVehicle->m_nVehicleId, VehicleSync::REMOVE_BY_SYNCER);
                if (!CountRoutineRemoval(site, pVehicle->m_nCreatedBy))
                {
                    LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId,
                        "reason=syncer syncing=1 site=0x%X class=%s model=%d createdBy=%d", static_cast<unsigned>(site),
                        RemoveSiteClass(site), pNetworkVehicle->m_nModelId, pVehicle->m_nCreatedBy);
                }
                DeleteRecord(pNetworkVehicle);
                continue;

            case VehicleSyncPolicy::eVehicleGone::SEND_REMOVE_RESTARTED:
                // this game starts over: the server gives a car of the host's scripts back to the host
                SendRemove(pNetworkVehicle->m_nVehicleId, VehicleSync::REMOVE_SYNCER_RESTARTED);
                ms_diag.restartRemovals++;
                ms_nRestartRemovals++;
                LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId,
                    "reason=syncer-restarted syncing=1 model=%d createdBy=%d", pNetworkVehicle->m_nModelId,
                    pVehicle->m_nCreatedBy);
                DeleteRecord(pNetworkVehicle);
                continue;

            case VehicleSyncPolicy::eVehicleGone::HOST_MISSION_REMOVE:
                SendRemove(pNetworkVehicle->m_nVehicleId, VehicleSync::REMOVE_BY_HOST_MISSION);
                LogEvent("removal-requested", pNetworkVehicle->m_nVehicleId,
                    "reason=host-mission syncing=%d site=0x%X class=%s model=%d", pNetworkVehicle->m_bSyncing,
                    static_cast<unsigned>(site), RemoveSiteClass(site), pNetworkVehicle->m_nModelId);
                DeleteRecord(pNetworkVehicle);
                continue;

            case VehicleSyncPolicy::eVehicleGone::KEEP_FOR_RECREATE:
            case VehicleSyncPolicy::eVehicleGone::KEEP_WRECK:
            case VehicleSyncPolicy::eVehicleGone::REMOVE_AT_CONFIRM:
                // a copy this game destroyed itself: its syncer still has it
                pNetworkVehicle->m_bCopyLost = true;
                pNetworkVehicle->m_bLostWreck = bWrecked;
                pNetworkVehicle->m_bHostMissionDelete = false;
                if (ms_bRestartClear)
                {
                    ms_nRestartCopiesLost++;  // one summary line when the restart's clear-up is over
                }
                else
                {
                    LogEvent("copy-destroyed", pNetworkVehicle->m_nVehicleId,
                        "syncing=0 wreck=%d site=0x%X class=%s model=%d", bWrecked, static_cast<unsigned>(site),
                        RemoveSiteClass(site), pNetworkVehicle->m_nModelId);
                }
                break;
        }
        i++;
    }
}

void CNetworkVehicleManager::OnVehicleDestructorDone(CVehicle* pVehicle)
{
    if (ms_pDestroyingVehicle == pVehicle)
    {
        ms_pDestroyingVehicle = nullptr;
        ms_nDestroyingVehicleId = -1;
    }
}

int CNetworkVehicleManager::GetIdForLog(const CVehicle* pVehicle)
{
    if (pVehicle == nullptr)
    {
        return -1;
    }
    if (pVehicle == ms_pDestroyingVehicle)
    {
        return ms_nDestroyingVehicleId;
    }
    const CNetworkVehicle* pNetworkVehicle = GetVehicle(const_cast<CEntity*>(static_cast<const CEntity*>(pVehicle)));
    return pNetworkVehicle != nullptr ? pNetworkVehicle->m_nVehicleId : -1;
}

void CNetworkVehicleManager::SetRestartClear(bool bRestarting)
{
    ms_bRestartClear = bRestarting;
    if (bRestarting)
    {
        ms_nRestartRemovals = 0;
        ms_nRestartCopiesLost = 0;
        LogEvent("restart-clear", -1, "phase=begin records=%u temp=%u", static_cast<unsigned>(m_pVehicles.size()),
            CountTemp());
    }
    else
    {
        LogEvent("restart-clear", -1, "phase=end removals=%u copies-lost=%u records=%u", ms_nRestartRemovals,
            ms_nRestartCopiesLost, static_cast<unsigned>(m_pVehicles.size()));
    }
}

void CNetworkVehicleManager::OnRestartHookInstalled(bool bHooked)
{
    ms_bRestartHooked = bHooked;
}

void CNetworkVehicleManager::OnTempSlotsFull()
{
    ms_diag.tempFull++;
}

void CNetworkVehicleManager::OnCopyCreateFailed(int vehicleid, int modelid, int createdBy)
{
    ms_diag.createFailed++;
    LogEvent("copy-create-failed", vehicleid, "model=%d createdBy=%d pool-free=%d", modelid, createdBy,
        CPools::ms_pVehiclePool->GetNoOfFreeSpaces());
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
    // private build: the routine removals go into one summary line a minute (FlushRoutineSummary), so the cap lasts hours
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 4096)
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
    if (ftell(file) < 2 * 1024 * 1024)
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

void CNetworkVehicleManager::WriteDiagSummary(FILE* file)
{
    // private build (--coopdiag): CDiagLog's thread; only counters the game thread keeps (RefreshDiagCounts every sweep)
    const SDiagCounters& c = ms_diag;
    fprintf(file,
        "  vehicle-sync: records=%u syncing=%u temp=%u pending=%u lost=%u destructorHooks=%d/%d restartHook=%d "
        "tempExpired=%u tempFull=%u confirmUnknown=%u createFailed=%u recreated=%u assignmentsExpired=%u "
        "removalsSent=%u removalsApplied=%u restartRemovals=%u handlerVehicleGone=%u confirms=%u createdByAtConfirm=%u "
        "exits=%u/%u/%u/%u\n",
        c.records, c.syncing, c.temp, c.pending, c.lost, ms_nDestructorHooks, ms_nDestructorSites, ms_bRestartHooked,
        c.tempExpired, c.tempFull, c.confirmUnknown, c.createFailed, c.recreated, c.assignmentsExpired, c.removalsSent,
        c.removalsApplied, c.restartRemovals, c.handlerVehicleGone, c.confirms, c.createdByAtConfirm, c.exitsKey,
        c.exitsTask, c.exitsDragged, c.exitsWarped);
}