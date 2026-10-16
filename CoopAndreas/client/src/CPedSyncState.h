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
