#pragma once
#include <cstdint>
#include <optional>

// A stored pointer must still name the same occupied pool generation. World
// membership is deliberately irrelevant: scripts temporarily remove live peds.
class CPedPoolReference
{
    int m_ref = -1;
    uintptr_t m_objects = 0;
    uintptr_t m_flags = 0;
public:
    int GetRef() const { return m_ref; }

    template<class Pool, class Ped> bool Capture(Pool* pool, Ped* ped)
    {
        m_ref = -1;
        if (!pool || !ped || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize <= 0)
            return false;
        const auto base = reinterpret_cast<uintptr_t>(pool->m_pObjects);
        const auto address = reinterpret_cast<uintptr_t>(ped);
        const auto stride = pool->GetObjectSize();
        if (!stride || address < base || (address - base) % stride != 0 ||
            (address - base) / stride >= static_cast<uintptr_t>(pool->m_nSize))
            return false;
        if (pool->IsFreeSlotAtIndex(static_cast<int>((address - base) / stride)))
            return false;
        m_objects = base;
        m_flags = reinterpret_cast<uintptr_t>(pool->m_byteMap);
        m_ref = pool->GetRef(ped);
        return true;
    }

    template<class Pool, class Ped> bool Matches(Pool* pool, Ped* ped) const
    {
        return m_ref >= 0 && pool && ped && pool->m_pObjects && pool->m_byteMap &&
            reinterpret_cast<uintptr_t>(pool->m_pObjects) == m_objects &&
            reinterpret_cast<uintptr_t>(pool->m_byteMap) == m_flags &&
            (m_ref >> 8) < pool->m_nSize && !pool->IsFreeSlotAtIndex(m_ref >> 8) &&
            pool->GetAtRef(m_ref) == ped;
    }
};

// private build (remote player ped recovery): when a missing or broken remote player ped may be recreated: at
// once, then at most once per RetryMs while recreating keeps failing; Reset() once the ped is healthy again.
// Unsigned elapsed time also handles tick wrap.
class CPedRecoveryThrottle
{
    uint32_t m_lastAttempt = 0;
    bool m_bAttempted = false;
public:
    static constexpr uint32_t RetryMs = 1000;
    void Reset() { m_bAttempted = false; }
    bool ShouldAttempt(uint32_t now)
    {
        if (m_bAttempted && static_cast<uint32_t>(now - m_lastAttempt) < RetryMs) return false;
        m_bAttempted = true;
        m_lastAttempt = now;
        return true;
    }
};

// private build (vehicle sync protocol review 2): recreating a copy (a ped another game syncs) that this game lost: it
// couldn't be made (the ped pool was full, the model didn't load) or this game's engine destroyed it (a car copy destroyed
// with its NPC driver copy inside). At once, then at most once per RetryMs and MaxPerWindow times per WindowMs, as
// CVehicleRecreateThrottle (CVehicleSyncPolicy.h) for vehicle copies, so a local path that keeps destroying it doesn't
// loop. Unsigned elapsed time also handles the tick wrap.
class CPedCopyRecreateThrottle
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

namespace PedSyncPolicy
{
// private build (vehicle sync protocol review 2): PED_SPAWN for an id this game already has a record with. A client finds
// a ped by the first record with its id (CNetworkPedManager::GetPed), so a second record would never be found.
enum class eSpawn
{
    CREATE,         // no record: a new copy
    KEEP_EXISTING,  // a record with its ped: the same ped again (the snapshot a game gets after its restart, following the
                    // spawn's relay, or a claim handover's copy); nothing new
    REPLACE         // a record without a ped (a lost copy, or a syncing record of a ped this game no longer has, which the
                    // server doesn't count as this game's any more since it sends it): it goes without a PED_REMOVE, then
                    // a new copy
};

inline eSpawn DecideSpawn(bool bRecordKnown, bool bRecordHasPed)
{
    if (!bRecordKnown)
        return eSpawn::CREATE;
    return bRecordHasPed ? eSpawn::KEEP_EXISTING : eSpawn::REPLACE;
}

// This game started over: CWorld::ClearForRestart (0x564360) deleted every ped, the copies of the other games' peds too,
// and nothing recreated them (a copy without a ped was never made again; its NPC driver's car came back from the next
// update as a driverless, frozen copy). Every record goes at once, before the server's snapshot (PLAYER_GAME_RESTARTED)
// brings the other games' peds back as new copies: a record of a ped this game synced sends PED_REMOVE (the server hands
// the ped to a claimer, which sends it back here as a copy, or removes it from every game, as when a syncer leaves), a
// copy's record goes without a word.
enum class eRestartRecord
{
    SEND_REMOVE,
    DROP
};

inline eRestartRecord DecideRestartRecord(bool bSyncing)
{
    return bSyncing ? eRestartRecord::SEND_REMOVE : eRestartRecord::DROP;
}

// A copy whose ped is gone comes back from its syncer's next update (on foot, driver or passenger) where that puts it; a
// ped this game syncs never does (its record goes with the next sweep, PED_REMOVE)
inline bool ShouldRecreateCopy(bool bSyncing, bool bCopyLost, bool bHasPed)
{
    return !bSyncing && bCopyLost && !bHasPed;
}
}  // namespace PedSyncPolicy

enum class ePedSnapshotResult { Idle, Invalid, Expired, Deferred, Applied };

// Exactly one visual snapshot; unsigned elapsed time also handles tick wrap.
template<class Snapshot> class CLatestPedSnapshot
{
    std::optional<Snapshot> m_latest;
    uint32_t m_receivedAt = 0;
public:
    static constexpr uint32_t MaxAgeMs = 2000;
    void Submit(const Snapshot& snapshot, uint32_t now) { m_latest = snapshot; m_receivedAt = now; }
    bool Cancel() { const bool hadSnapshot = m_latest.has_value(); m_latest.reset(); return hadSnapshot; }

    template<class Valid, class Ready, class Apply>
    ePedSnapshotResult Process(uint32_t now, Valid valid, Ready ready, Apply apply)
    {
        if (!m_latest) return ePedSnapshotResult::Idle;
        if (!valid()) { Cancel(); return ePedSnapshotResult::Invalid; }
        if (static_cast<uint32_t>(now - m_receivedAt) >= MaxAgeMs)
        { Cancel(); return ePedSnapshotResult::Expired; }
        if (!ready()) return ePedSnapshotResult::Deferred;
        // Validation inside an engine callback may clear pending state.
        const Snapshot snapshot = *m_latest;
        if (!apply(snapshot))
        {
            if (!valid()) { Cancel(); return ePedSnapshotResult::Invalid; }
            return ePedSnapshotResult::Deferred;
        }
        Cancel();
        return ePedSnapshotResult::Applied;
    }
};
