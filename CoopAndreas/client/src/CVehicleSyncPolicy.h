#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "network/entity_lifecycle.h"
#include "network/vehicle_sync.h"

// Private build (vehicle sync protocol, 0.1.1): the client's rules for its network vehicle records. Pure logic without GTA,
// so tests/vehicle_sync_test.cpp checks it; CNetworkVehicle and CNetworkVehicleManager apply it.
//
// A record (CNetworkVehicle) is this game's view of one network vehicle: a temp record of a vehicle this game created
// until VEHICLE_CONFIRM gives it an id, then a confirmed record; m_bSyncing says whether this game is its syncer (the game
// that simulates it and sends its updates). The other games keep a copy, marked MISSION_VEHICLE so that their own
// population code leaves it alone.

// A stored vehicle pointer must still name the same occupied generation of the same vehicle pool slot (the rule of
// CPedPoolReference in CPedSyncState.h). IsVehiclePointerValid (0x6E38F0) only checks that the slot is occupied and that
// the vehicle is in the world (bytes at 0x6E38F7-0x6E3947), so a new vehicle in a reused slot passes it.
class CVehiclePoolReference
{
    int m_ref = -1;
    uintptr_t m_objects = 0;
    uintptr_t m_flags = 0;

public:
    int GetRef() const { return m_ref; }
    void Reset() { m_ref = -1; }
    void Clear() { Reset(); }

    template <class Pool, class Vehicle> bool Capture(Pool* pool, Vehicle* vehicle)
    {
        m_ref = -1;
        if (!pool || !vehicle || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize <= 0)
            return false;
        const auto base = reinterpret_cast<uintptr_t>(pool->m_pObjects);
        const auto address = reinterpret_cast<uintptr_t>(vehicle);
        const auto stride = pool->GetObjectSize();
        if (!stride || address < base || (address - base) % stride != 0 ||
            (address - base) / stride >= static_cast<uintptr_t>(pool->m_nSize))
            return false;
        if (pool->IsFreeSlotAtIndex(static_cast<int>((address - base) / stride)))
            return false;
        m_objects = base;
        m_flags = reinterpret_cast<uintptr_t>(pool->m_byteMap);
        m_ref = pool->GetRef(vehicle);
        return true;
    }

    template <class Pool, class Vehicle> bool Matches(Pool* pool, Vehicle* vehicle) const
    {
        return m_ref >= 0 && pool && vehicle && pool->m_pObjects && pool->m_byteMap &&
               reinterpret_cast<uintptr_t>(pool->m_pObjects) == m_objects &&
               reinterpret_cast<uintptr_t>(pool->m_byteMap) == m_flags && (m_ref >> 8) < pool->m_nSize &&
               !pool->IsFreeSlotAtIndex(m_ref >> 8) && pool->GetAtRef(m_ref) == vehicle;
    }
};

// ASSIGN_VEHICLE for a vehicle this game doesn't know yet: kept per vehicle id, the newest wins, and applied when a
// VEHICLE_SPAWN brings a vehicle with this id and model. Since the server stamps the spawns with its own clock
// (EntityLifecycle::UsesServerTime) a vehicle's spawn comes before its assignments, so this is a safety net. A
// VEHICLE_REMOVE of the id drops it, and so does a VEHICLE_CONFIRM: the server sends the confirmation before any
// assignment of the new vehicle, so an entry waiting by then is about an older vehicle with that id. It expires after
// MaxAgeMs (DecideExpiredAssignment). Unsigned elapsed time also handles the tick wrap.
class CPendingVehicleAssignments
{
public:
    static constexpr uint32_t MaxAgeMs = 10000;

    struct SEntry
    {
        int vehicleId;
        uint16_t modelId;
        bool bSyncing;
        uint32_t receivedAt;
    };

    void Store(int vehicleId, uint16_t modelId, bool bSyncing, uint32_t now)
    {
        for (SEntry& entry : m_entries)
        {
            if (entry.vehicleId == vehicleId)
            {
                entry = {vehicleId, modelId, bSyncing, now};
                return;
            }
        }
        m_entries.push_back({vehicleId, modelId, bSyncing, now});
    }

    // the state for the vehicle that appeared with this id, once; nothing for another model (an assignment of an older
    // vehicle with this id) or an expired entry, both dropped
    std::optional<bool> Take(int vehicleId, uint16_t modelId, uint32_t now)
    {
        for (size_t i = 0; i < m_entries.size(); i++)
        {
            if (m_entries[i].vehicleId != vehicleId)
                continue;
            const SEntry entry = m_entries[i];
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
            if (entry.modelId != modelId || static_cast<uint32_t>(now - entry.receivedAt) >= MaxAgeMs)
                return std::nullopt;
            return entry.bSyncing;
        }
        return std::nullopt;
    }

    bool Drop(int vehicleId)
    {
        for (size_t i = 0; i < m_entries.size(); i++)
        {
            if (m_entries[i].vehicleId == vehicleId)
            {
                m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    // removes the entries older than MaxAgeMs and returns them
    std::vector<SEntry> Expire(uint32_t now)
    {
        std::vector<SEntry> expired;
        for (size_t i = 0; i < m_entries.size();)
        {
            if (static_cast<uint32_t>(now - m_entries[i].receivedAt) >= MaxAgeMs)
            {
                expired.push_back(m_entries[i]);
                m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            i++;
        }
        return expired;
    }

    size_t Size() const { return m_entries.size(); }

private:
    std::vector<SEntry> m_entries;
};

// Recreating a copy this game lost: at once, then at most once per RetryMs and MaxPerWindow times per WindowMs, so a local
// engine path that keeps destroying it (a garage tidying up an upside-down car) doesn't loop. Unsigned elapsed time also
// handles the tick wrap.
class CVehicleRecreateThrottle
{
public:
    static constexpr uint32_t RetryMs = 1000;
    static constexpr uint32_t WindowMs = 30000;
    static constexpr unsigned MaxPerWindow = 3;

    bool ShouldAttempt(uint32_t now)
    {
        unsigned recent = 0;
        for (unsigned i = 0; i < m_count; i++)
        {
            const uint32_t age = static_cast<uint32_t>(now - m_attempts[i]);
            if (age < RetryMs)
                return false;
            if (age < WindowMs)
                recent++;
        }
        if (recent >= MaxPerWindow)
            return false;
        m_attempts[m_next] = now;
        m_next = (m_next + 1) % MaxPerWindow;
        if (m_count < MaxPerWindow)
            m_count++;
        return true;
    }

private:
    uint32_t m_attempts[MaxPerWindow]{};
    unsigned m_count = 0;
    unsigned m_next = 0;
};

namespace VehicleSyncPolicy
{
// ASSIGN_VEHICLE states what this game now is for the vehicle (upstream sent the same packet to the old and the new
// syncer and each flipped m_bSyncing, so one lost or doubled assignment inverted every later one)
enum class eAssignment
{
    APPLY,
    UNCHANGED,
    DEFER  // no record with this id and model yet: CPendingVehicleAssignments
};

inline eAssignment DecideAssignment(bool bRecordKnown, bool bSameModel, bool bSyncingNow, bool bSyncing)
{
    if (!bRecordKnown || !bSameModel)
        return eAssignment::DEFER;
    return bSyncingNow == bSyncing ? eAssignment::UNCHANGED : eAssignment::APPLY;
}

// the createdBy this game's vehicle gets from its record: the record's own while this game syncs it, MISSION_VEHICLE for
// a copy
inline uint8_t LocalCreatedBy(bool bSyncing, uint8_t recordCreatedBy)
{
    return bSyncing ? recordCreatedBy : VehicleSync::CREATED_BY_MISSION;
}

// A local createdBy change (a script's SetVehicleCreatedBy, MARK_CAR_AS_NO_LONGER_NEEDED, the mission cleanup) on a
// vehicle with a record: the record takes the new value when this game syncs the vehicle or is the host (whose scripts
// decide for every game, SET_VEHICLE_CREATED_BY); a copy this game doesn't sync goes back to MISSION_VEHICLE, so its own
// population, garages and wreck clean-up keep leaving it alone.
struct SCreatedByChange
{
    bool bRecordTakesIt;
    bool bRestoreMission;
};

inline SCreatedByChange DecideCreatedByChange(bool bSyncing, bool bIsHost, uint8_t createdBy)
{
    return {bSyncing || bIsHost, !bSyncing && createdBy != VehicleSync::CREATED_BY_MISSION};
}

// A script of the host's mission family deletes (DELETE_CAR) a vehicle: it goes on every game, through a VEHICLE_REMOVE
// with REMOVE_BY_HOST_MISSION, which the server accepts from the host only, whoever syncs the vehicle. Whatever this
// game's record says: in the 50-60 ms before the host handles the ASSIGN_VEHICLE that made another game the syncer, a
// REMOVE_BY_SYNCER was rejected and the server sent the car back, so the other game kept a car the mission had deleted.
inline bool IsHostMissionDelete(bool bIsHost, bool bFamilyScriptRunning)
{
    return bIsHost && bFamilyScriptRunning;
}

// The game destroys the vehicle of a record
enum class eVehicleGone
{
    SEND_REMOVE,            // this game syncs it: VEHICLE_REMOVE (REMOVE_BY_SYNCER), the record goes
    SEND_REMOVE_RESTARTED,  // the same while this game starts over (CWorld::ClearForRestart): REMOVE_SYNCER_RESTARTED,
                            // with which the server gives a car of the host's scripts back to the host
    REMOVE_AT_CONFIRM,      // not confirmed yet: the record keeps its temp slot, VEHICLE_CONFIRM sends REMOVE_UNCONFIRMED
    HOST_MISSION_REMOVE,    // the host's mission deleted it (IsHostMissionDelete), synced here or not: VEHICLE_REMOVE
                            // (REMOVE_BY_HOST_MISSION), the record goes
    KEEP_FOR_RECREATE,      // a copy lost to a local path: recreated from its syncer's next update
    KEEP_WRECK              // a wrecked copy: kept without a vehicle until its syncer removes it
};

inline eVehicleGone DecideVehicleGone(
    bool bConfirmed, bool bSyncing, bool bHostMissionDelete, bool bWrecked, bool bRestarting)
{
    if (!bConfirmed)
        return eVehicleGone::REMOVE_AT_CONFIRM;
    // before the syncing state: the host's record may still say "syncing" for a car the server gave to another game
    if (bHostMissionDelete)
        return eVehicleGone::HOST_MISSION_REMOVE;
    if (bSyncing)
        return bRestarting ? eVehicleGone::SEND_REMOVE_RESTARTED : eVehicleGone::SEND_REMOVE;
    return bWrecked ? eVehicleGone::KEEP_WRECK : eVehicleGone::KEEP_FOR_RECREATE;
}

// VEHICLE_CONFIRM for a temp record
enum class eConfirm
{
    UNKNOWN_TEMP,       // no record waits in that temp slot (it expired, see IsTempRecordExpired): the server's vehicle
                        // has nobody to sync it, so it goes on every game too (VEHICLE_REMOVE, REMOVE_UNCONFIRMED)
    ATTACH,             // the record gets its id
    REMOVE_UNCONFIRMED  // its vehicle is already gone: VEHICLE_REMOVE (REMOVE_UNCONFIRMED), the record goes
};

inline eConfirm DecideConfirm(bool bTempKnown, bool bVehicleAlive)
{
    if (!bTempKnown)
        return eConfirm::UNKNOWN_TEMP;
    return bVehicleAlive ? eConfirm::ATTACH : eConfirm::REMOVE_UNCONFIRMED;
}

// VEHICLE_CONFIRM attaches a temp record. A script that changed the vehicle's createdBy while its VEHICLE_SPAWN was on its
// way (CREATE_CAR and MARK_CAR_AS_NO_LONGER_NEEDED within a few frames; CTaskSimpleCreateCarAndGetIn releases its car at
// 0x64CED4/0x64D186) found no confirmed record in the createdBy hooks, so the record still holds the value VEHICLE_SPAWN
// sent: the server and the other games kept a released car a mission vehicle that never despawns. The record takes the
// vehicle's value, and the host, whose scripts decide for every game, sends it (SET_VEHICLE_CREATED_BY).
struct SCreatedByAtConfirm
{
    bool bChanged;  // the record takes the vehicle's value
    bool bSend;     // SET_VEHICLE_CREATED_BY
};

inline SCreatedByAtConfirm DecideCreatedByAtConfirm(bool bIsHost, uint8_t spawnCreatedBy, uint8_t liveCreatedBy)
{
    const bool bChanged = spawnCreatedBy != liveCreatedBy;
    return {bChanged, bChanged && bIsHost};
}

// --coopdiag: a confirmation is routine (counted in the summary line a minute) unless it waited CONFIRM_SLOW_MS or longer,
// replaced a record with its id or found a createdBy change; a line for each filled the 4096-record cap of
// CoopAndreas_vehicle_sync.log after 1.5-3 hours of driving
constexpr uint32_t CONFIRM_SLOW_MS = 500;

inline bool IsConfirmWorthALine(uint32_t waitedMs, bool bReplaced, bool bCreatedByChanged)
{
    return waitedMs >= CONFIRM_SLOW_MS || bReplaced || bCreatedByChanged;
}

// PED_DRIVER_UPDATE of a ped another game syncs (an NPC driving): the car copy follows the update, and a lost car copy is
// recreated only for a driver copy that is there (the ped copy is recreated first, when it was lost). A car copy recreated
// without its driver stood frozen as a MISSION_VEHICLE (nothing else moves a car an NPC drives: its syncer sends no idle
// updates for it) until its syncer removed it; CWorld::ClearForRestart deletes the car copies with their driver copies.
enum class ePedDriverVehicle
{
    SKIP,            // no car copy, and none is made without a driver copy
    RECREATE,        // the car copy is lost: recreated, then as SEAT_AND_APPLY
    SEAT_AND_APPLY,  // the driver copy into the car (when it isn't), the update onto both
    APPLY_VEHICLE    // the driver copy is gone: the update onto the car only, so it doesn't stand still
};

inline ePedDriverVehicle DecidePedDriverVehicle(bool bVehicleAlive, bool bPedAlive)
{
    if (!bPedAlive)
        return bVehicleAlive ? ePedDriverVehicle::APPLY_VEHICLE : ePedDriverVehicle::SKIP;
    return bVehicleAlive ? ePedDriverVehicle::SEAT_AND_APPLY : ePedDriverVehicle::RECREATE;
}

// ASSIGN_VEHICLE makes this game the syncer of a vehicle: a copy it lost is recreated where its last update (or, before
// the first one, its VEHICLE_SPAWN) put it, or removed on every game when there is nothing to recreate (a wreck, or a
// record of this game's own vehicle that never got an update)
enum class eTakeover
{
    KEEP,
    RECREATE,
    REMOVE
};

inline eTakeover DecideTakeover(bool bHasVehicle, bool bWreck, bool bHasLastState)
{
    if (bHasVehicle)
        return eTakeover::KEEP;
    return !bWreck && bHasLastState ? eTakeover::RECREATE : eTakeover::REMOVE;
}

// The temp id of a new vehicle of this game: the first free slot after the last one handed out, so a VEHICLE_CONFIRM
// still on its way for a slot isn't matched with a newer vehicle. -1 = all count slots are taken: the vehicle stays local
// (no VEHICLE_SPAWN without a slot, its VEHICLE_CONFIRM couldn't name the record).
template <class IsFree> int NextTempId(int last, int count, IsFree isFree)
{
    return EntityLifecycle::NextFreeId(last, count, isFree);
}

// A temp record whose VEHICLE_CONFIRM hasn't come for this long gives its slot back (the server answers in the tick it
// handles VEHICLE_SPAWN, but it drops the spawn without an answer when all its ids are taken); its vehicle, if it still
// exists, stays in this game only. A confirmation that comes later finds no record (eConfirm::UNKNOWN_TEMP). Unsigned
// elapsed time also handles the tick wrap.
constexpr uint32_t TEMP_RECORD_MAX_AGE_MS = 30000;

inline bool IsTempRecordExpired(uint32_t createdAt, uint32_t now)
{
    return static_cast<uint32_t>(now - createdAt) >= TEMP_RECORD_MAX_AGE_MS;
}

// An ASSIGN_VEHICLE that waited in vain for its vehicle (CPendingVehicleAssignments::Expire). "Not the syncer" changes
// nothing. "The syncer" means the server counts on this game for a vehicle it doesn't have: nobody would simulate or
// remove it, a frozen ghost on every game. As in a takeover with nothing to take over (eTakeover::REMOVE) it goes on every
// game: VEHICLE_REMOVE with REMOVE_BY_SYNCER, which the server takes only while this game is still the syncer (if it
// already gave the vehicle to another game, the removal is rejected and the vehicle comes back here as a copy). A record
// this game holds with that id for another model doesn't match the server's vehicle and goes too.
enum class eExpiredAssignment
{
    NOTHING,
    REMOVE_EVERYWHERE,
    REMOVE_EVERYWHERE_AND_LOCAL_RECORD
};

inline eExpiredAssignment DecideExpiredAssignment(bool bSyncing, bool bRecordWithOtherModel)
{
    if (!bSyncing)
        return eExpiredAssignment::NOTHING;
    return bRecordWithOtherModel ? eExpiredAssignment::REMOVE_EVERYWHERE_AND_LOCAL_RECORD
                                 : eExpiredAssignment::REMOVE_EVERYWHERE;
}

// The local player's update in a frame (Main.cpp). While its car exit runs (CTaskComplexLeaveCar in its primary task
// slot, set by CPlayerInfo::Process 0x56F8D0 at 0x5703F7/0x57049C, where TaskHooks sends VEHICLE_EXIT, or by a script, an
// event or a sequence; or a dragged-out task while a jacker pulls it out, see CLocalExitTracker), the ped is still the
// vehicle's driver or passenger: CTaskSimpleCarSetPedOut (0x647D10) unlinks it only at the end of the exit. Its driver or
// passenger updates then re-seated it on the server right after VEHICLE_EXIT had freed the seat (a jacked driver's put it
// back into the seat its jacker had taken, and the syncer went back and forth), and nothing freed it again, so "no driver:
// a passenger becomes the syncer" never ran. Now it sends none until it is out (on foot) or the exit is over without
// getting out (driving again); the vehicle's own state keeps going out as idle updates while this game syncs it
// (IsDrivenForIdleUpdates).
enum class eLocalUpdate
{
    ON_FOOT,
    DRIVER,
    PASSENGER,
    NONE  // getting out of a vehicle
};

inline eLocalUpdate ChooseLocalUpdate(bool bInVehicle, bool bDriver, bool bLeavingVehicle)
{
    if (!bInVehicle)
        return eLocalUpdate::ON_FOOT;
    if (bLeavingVehicle)
        return eLocalUpdate::NONE;
    return bDriver ? eLocalUpdate::DRIVER : eLocalUpdate::PASSENGER;
}

// Whether a vehicle this game syncs counts as driven, so that its state goes out in the driver's updates instead of idle
// updates: not while the local player driving it is getting out (it sends no driver updates then)
inline bool IsDrivenForIdleUpdates(bool bHasDriver, bool bDriverIsLocalPlayerLeaving)
{
    return bHasDriver && !bDriverIsLocalPlayerLeaving;
}

// The local player's car exits the other games hear of (VEHICLE_EXIT). An exit by the exit key goes out from
// CPlayerInfo::Process (TaskHooks.cpp, its two calls of the CTaskComplexLeaveCar constructor 0x63B8C0 at 0x5703F7 and
// 0x57049C). Every other exit is seen once a frame after CGame::Process (Main.cpp): a script's Task.LeaveCar or
// Task.LeaveAnyCar, an event response, a sequence, being dragged out by a jacker (the jacker's enter task gives the
// victim CTaskComplexCarSlowBeDraggedOutAndStandUp through AddTaskPrimaryMaybeInGroup, gta-reversed TaskComplexEnterCar.cpp,
// not a leave task), or a warp. Before, those sent nothing, and since the game sends no driver or passenger updates while
// its player gets out (ChooseLocalUpdate), the others kept the player seated and replayed its last keys, and the server
// kept its seat for OnFootSeatTimeoutMs.
enum class eExitNotice
{
    NONE,
    LEAVE,        // VEHICLE_EXIT: the others play the exit (CTaskComplexLeaveCar), as for the exit key
    DRAGGED_OUT,  // VEHICLE_EXIT with bDraggedOut: on every game the jacker's own enter task drags the player out, the
                  // others only release its keys
    WARPED        // VEHICLE_EXIT with bForce: out of the vehicle without an exit task (a script's warp, a respawn)
};

// Update: bInVehicle = the local player is in a vehicle, bLeaving = a leave-car task in its primary task slots (or the
// subtask chain of one), bDraggedOut = a dragged-out task there. Each exit is announced once.
class CLocalExitTracker
{
public:
    // the exit key's VEHICLE_EXIT went out (TaskHooks.cpp), earlier in the same frame
    void OnKeyExitSent() { m_bAnnounced = true; }

    eExitNotice Update(bool bInVehicle, bool bLeaving, bool bDraggedOut)
    {
        eExitNotice notice = eExitNotice::NONE;
        if (bInVehicle && (bLeaving || bDraggedOut))
        {
            if (!m_bAnnounced)
            {
                notice = bDraggedOut ? eExitNotice::DRAGGED_OUT : eExitNotice::LEAVE;
            }
            m_bAnnounced = true;
        }
        else if (bInVehicle)
        {
            m_bAnnounced = false;  // no exit runs (any more): the next one is announced again
        }
        else
        {
            if (m_bWasInVehicle && !m_bAnnounced)
            {
                notice = eExitNotice::WARPED;
            }
            m_bAnnounced = false;
        }
        m_bWasInVehicle = bInVehicle;
        return notice;
    }

private:
    bool m_bWasInVehicle = false;
    bool m_bAnnounced = false;
};

// Train carriage identity rules (T1a, docs/design/trains.md 6.1): standalone CNetworkVehicle constructors never create
// a train carriage (CTrain::CreateMissionTrain builds the chain in CTrainSync), and on-rail carriages neither send nor
// apply generic vehicle idle/driver kinematics; derailed carriages fall back to generic vehicle physics (T5).
constexpr bool ShouldSpawnStandaloneCarriage(int modelId) noexcept
{
    return !VehicleSync::IsTrainModelId(modelId);
}

constexpr bool ShouldSendCarriageIdleUpdate(int modelId, bool isDerailed) noexcept
{
    return !VehicleSync::IsTrainModelId(modelId) || isDerailed;
}

constexpr bool ShouldApplyCarriageKinematics(int modelId, bool isDerailed) noexcept
{
    return !VehicleSync::IsTrainModelId(modelId) || isDerailed;
}
}  // namespace VehicleSyncPolicy
