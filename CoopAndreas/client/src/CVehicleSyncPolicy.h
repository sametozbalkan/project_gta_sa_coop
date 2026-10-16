#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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

// ASSIGN_VEHICLE for a vehicle this game doesn't know yet (its VEHICLE_SPAWN still waits in the packet buffer, or the
// server confirmed our vehicle after the assignment was handled): kept per vehicle id, the newest wins, and applied when
// a vehicle with this id and model appears. A VEHICLE_REMOVE of the id drops it; it expires after MaxAgeMs. Unsigned
// elapsed time also handles the tick wrap.
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

// A script of the host's mission family deletes (DELETE_CAR) a vehicle another game syncs: it goes on every game, through
// a VEHICLE_REMOVE with REMOVE_BY_HOST_MISSION, which the server accepts from the host only
inline bool IsHostMissionDelete(bool bIsHost, bool bSyncing, bool bFamilyScriptRunning)
{
    return bIsHost && !bSyncing && bFamilyScriptRunning;
}

// The game destroys the vehicle of a record
enum class eVehicleGone
{
    SEND_REMOVE,          // this game syncs it: VEHICLE_REMOVE (REMOVE_BY_SYNCER), the record goes
    REMOVE_AT_CONFIRM,    // not confirmed yet: the record keeps its temp slot, VEHICLE_CONFIRM sends REMOVE_UNCONFIRMED
    HOST_MISSION_REMOVE,  // the host's mission deleted a copy: VEHICLE_REMOVE (REMOVE_BY_HOST_MISSION), the record goes
    KEEP_FOR_RECREATE,    // a copy lost to a local path: recreated from its syncer's next update
    KEEP_WRECK            // a wrecked copy: kept without a vehicle until its syncer removes it
};

inline eVehicleGone DecideVehicleGone(bool bConfirmed, bool bSyncing, bool bHostMissionDelete, bool bWrecked)
{
    if (!bConfirmed)
        return eVehicleGone::REMOVE_AT_CONFIRM;
    if (bSyncing)
        return eVehicleGone::SEND_REMOVE;
    if (bHostMissionDelete)
        return eVehicleGone::HOST_MISSION_REMOVE;
    return bWrecked ? eVehicleGone::KEEP_WRECK : eVehicleGone::KEEP_FOR_RECREATE;
}

// VEHICLE_CONFIRM for a temp record
enum class eConfirm
{
    UNKNOWN_TEMP,       // no record waits in that temp slot
    ATTACH,             // the record gets its id
    REMOVE_UNCONFIRMED  // its vehicle is already gone: VEHICLE_REMOVE (REMOVE_UNCONFIRMED), the record goes
};

inline eConfirm DecideConfirm(bool bTempKnown, bool bVehicleAlive)
{
    if (!bTempKnown)
        return eConfirm::UNKNOWN_TEMP;
    return bVehicleAlive ? eConfirm::ATTACH : eConfirm::REMOVE_UNCONFIRMED;
}

// ASSIGN_VEHICLE makes this game the syncer of a vehicle: a copy it lost is recreated where its last update put it, or
// removed on every game when there is nothing to recreate (a wreck, or no update ever came)
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
// still on its way for a slot isn't matched with a newer vehicle. -1 = all count slots are taken.
template <class IsFree> int NextTempId(int last, int count, IsFree isFree)
{
    for (int step = 1; step <= count; step++)
    {
        const int id = (last + step) % count;
        if (isFree(id))
            return id;
    }
    return -1;
}
}  // namespace VehicleSyncPolicy
