#pragma once

#include "network/world_state.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Private build: the world-state registry's logic without game or network types. CWorldStateRegistry.cpp drives it;
// tests/world_state_test.cpp checks it without GTA: the blobs and their layout header (network/world_state.h), the
// host's choice between FULL and RECORDS, the other games' desired blobs (FULL, RECORDS patches, the sender check,
// layout mismatches), the apply gate (docs/design/world-state.md 3.1) and the lifecycle marks (3.6). There is no story
// guard: the server pins the story host instead (docs/design/README.md, W-D8).

namespace WorldStatePolicy
{
using Blob = std::vector<uint8_t>;

constexpr size_t HEADER_SIZE = static_cast<size_t>(WorldStateWire::LAYOUT_HEADER_SIZE);
constexpr size_t MAX_DATA_SIZE = static_cast<size_t>(WorldStateWire::MAX_DATA_SIZE);

// the host's game has to be ready this long after a new game or a load before every entry goes out in full again
constexpr uint32_t RESTART_FULL_DELAY_MS = 2000;

// FNV-1a, 32 bits, of bytes and of a zero-terminated text (layout hashes)
inline uint32_t Fnv1a32(const void* data, size_t size, uint32_t hash = 2166136261u)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

constexpr uint32_t Fnv1a32Text(const char* text, uint32_t hash = 2166136261u)
{
    for (; *text != '\0'; text++)
    {
        hash ^= static_cast<uint8_t>(*text);
        hash *= 16777619u;
    }
    return hash;
}

// little-endian numbers in blobs
inline void PutU16(Blob& blob, uint16_t value)
{
    blob.push_back(static_cast<uint8_t>(value & 0xFF));
    blob.push_back(static_cast<uint8_t>(value >> 8));
}

inline void PutU32(Blob& blob, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        blob.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

inline uint16_t GetU16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

inline uint32_t GetU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

// the layout header every blob and packet starts with
struct SLayout
{
    uint16_t recordCount = 0;
    uint32_t hash = 0;

    bool operator==(const SLayout& other) const { return recordCount == other.recordCount && hash == other.hash; }
    bool operator!=(const SLayout& other) const { return !(*this == other); }
};

// starts an entry's blob with its layout header; the entry appends its payload
inline void BeginBlob(Blob& blob, const SLayout& layout)
{
    blob.clear();
    PutU16(blob, layout.recordCount);
    PutU32(blob, layout.hash);
}

inline bool ReadLayout(const uint8_t* data, size_t size, SLayout& layout)
{
    if (data == nullptr || size < HEADER_SIZE)
    {
        return false;
    }
    layout.recordCount = GetU16(data);
    layout.hash = GetU32(data + 2);
    return true;
}

inline bool ReadLayout(const Blob& blob, SLayout& layout) { return ReadLayout(blob.data(), blob.size(), layout); }

// A FULL blob as its entry defines it: the header, then exactly the record count's records for an entry with records.
// An entry without records says 0 records; its value's format is part of the layout hash.
inline bool IsValidFull(const uint8_t* data, size_t size, uint16_t recordSize)
{
    SLayout layout;
    if (!ReadLayout(data, size, layout))
    {
        return false;
    }
    if (recordSize == 0)
    {
        return layout.recordCount == 0;
    }
    return size == HEADER_SIZE + static_cast<size_t>(layout.recordCount) * recordSize;
}

inline bool IsValidFull(const Blob& blob, uint16_t recordSize) { return IsValidFull(blob.data(), blob.size(), recordSize); }

// One WORLD_STATE packet carries it. A FULL that doesn't fit would have to be split into parts (design 3.5); no entry
// needs that yet, so the registry refuses to send it and says so in its log.
inline bool FitsInOnePacket(size_t size) { return size >= HEADER_SIZE && size <= MAX_DATA_SIZE; }

enum class eSend : uint8_t
{
    NONE,     // unchanged since the last send
    FULL,     // the whole blob
    RECORDS,  // only the records that changed
};

// The host's choice for an entry whose captured state is `now` (a valid FULL blob): NONE when it equals what went out
// last; RECORDS, built into `records`, when the entry has records, the layout is the same, fewer than half of the
// records changed and that is smaller than the whole blob; FULL otherwise, also when nothing went out yet or a FULL is
// due (join, host change, restart).
inline eSend ChooseSend(const Blob* pLastSent, const Blob& now, uint16_t recordSize, bool bFullDue, Blob& records)
{
    records.clear();
    if (bFullDue || pLastSent == nullptr)
    {
        return eSend::FULL;
    }
    if (*pLastSent == now)
    {
        return eSend::NONE;
    }
    if (recordSize == 0 || pLastSent->size() != now.size() || !IsValidFull(now, recordSize) ||
        std::memcmp(pLastSent->data(), now.data(), HEADER_SIZE) != 0)
    {
        return eSend::FULL;
    }

    const size_t count = GetU16(now.data());
    size_t changed = 0;
    for (size_t i = 0; i < count; i++)
    {
        const size_t offset = HEADER_SIZE + i * recordSize;
        if (std::memcmp(pLastSent->data() + offset, now.data() + offset, recordSize) != 0)
        {
            changed++;
        }
    }
    if (changed * 2 >= count)
    {
        return eSend::FULL;
    }

    records.assign(now.begin(), now.begin() + static_cast<std::ptrdiff_t>(HEADER_SIZE));
    for (size_t i = 0; i < count; i++)
    {
        const size_t offset = HEADER_SIZE + i * recordSize;
        if (std::memcmp(pLastSent->data() + offset, now.data() + offset, recordSize) != 0)
        {
            PutU16(records, static_cast<uint16_t>(i));
            records.insert(records.end(), now.data() + offset, now.data() + offset + recordSize);
        }
    }
    if (records.size() >= now.size())
    {
        records.clear();
        return eSend::FULL;
    }
    return eSend::RECORDS;
}

enum class eReceive : uint8_t
{
    ACCEPTED,
    MALFORMED,        // doesn't fit the entry: size, record count, a record index out of range
    NO_BASE,          // RECORDS without a FULL from the same host to patch
    STALE_LAYOUT,     // RECORDS for another layout than the FULL they would patch
    LAYOUT_REJECTED,  // this game found that layout different from its own: the entry stays off
};

enum class eCheck : uint8_t
{
    NOTHING,          // no desired blob, or the local state equals it
    APPLY,            // the local state differs: make it match
    LAYOUT_MISMATCH,  // the host's layout isn't this game's (found now): the entry turns off on this game
    DISABLED,         // off since an earlier mismatch
};

// The apply gate on the games that aren't the host (design 3.1).
struct SGateInput
{
    bool bAuthenticated = false;        // connected and accepted by the server
    bool bIsHost = false;               // this game is the host: it captures and sends instead
    bool bHasHost = false;              // some game is the host
    bool bRanSinceRestart = false;      // a frame passed since this game's last new game or load
    bool bLocalGameReady = false;       // the local player is there, playing and alive
    bool bLocalMissionRunning = false;  // a mission of this game runs (at a new game INITIAL, INITIL2, INTRO)
};

enum class eGate : uint8_t
{
    NO_SESSION,
    IS_HOST,
    NO_HOST,
    NOT_READY,
    OWN_MISSION,
    OPEN,
};

// Entries that SuspendDuringOwnMission wait while a mission of this game runs (the friend's own new-game missions and
// side missions); the others (city unlock, maximum wanted level) are put back at once, as CCityUnlockSync did.
inline eGate DecideGate(const SGateInput& input, bool bSuspendDuringOwnMission)
{
    if (!input.bAuthenticated)
    {
        return eGate::NO_SESSION;
    }
    if (input.bIsHost)
    {
        return eGate::IS_HOST;
    }
    if (!input.bHasHost)
    {
        return eGate::NO_HOST;
    }
    if (!input.bRanSinceRestart || !input.bLocalGameReady)
    {
        return eGate::NOT_READY;
    }
    if (bSuspendDuringOwnMission && input.bLocalMissionRunning)
    {
        return eGate::OWN_MISSION;
    }
    return eGate::OPEN;
}

inline const char* ToString(eSend send)
{
    switch (send)
    {
        case eSend::NONE: return "none";
        case eSend::FULL: return "full";
        case eSend::RECORDS: return "records";
    }
    return "?";
}

inline const char* ToString(eReceive receive)
{
    switch (receive)
    {
        case eReceive::ACCEPTED: return "accepted";
        case eReceive::MALFORMED: return "malformed";
        case eReceive::NO_BASE: return "no-base";
        case eReceive::STALE_LAYOUT: return "stale-layout";
        case eReceive::LAYOUT_REJECTED: return "layout-rejected";
    }
    return "?";
}

inline const char* ToString(eGate gate)
{
    switch (gate)
    {
        case eGate::NO_SESSION: return "no-session";
        case eGate::IS_HOST: return "is-host";
        case eGate::NO_HOST: return "no-host";
        case eGate::NOT_READY: return "not-ready";
        case eGate::OWN_MISSION: return "own-mission";
        case eGate::OPEN: return "open";
    }
    return "?";
}
}  // namespace WorldStatePolicy

// One entry's state in the registry. The host remembers what went out last and whether a FULL is due; the other games
// keep the host's desired blob with the player it came from, and turn the entry off when the host's layout isn't
// theirs (instead of writing into the wrong slots).
class CWorldStateEntryState
{
public:
    using Blob = WorldStatePolicy::Blob;

    // ---- the host ----

    void MarkFull() { m_bFullDue = true; }
    bool IsFullDue() const { return m_bFullDue; }
    bool HasSent() const { return m_bHasSent; }

    // what to send for the captured state `now` (a valid FULL blob); RECORDS are built into `records`
    WorldStatePolicy::eSend Poll(const Blob& now, uint16_t recordSize, Blob& records) const
    {
        return WorldStatePolicy::ChooseSend(m_bHasSent ? &m_sent : nullptr, now, recordSize, m_bFullDue, records);
    }

    // `now` went out, whole or as the RECORDS that turn the last one into it
    void OnSent(const Blob& now)
    {
        m_sent = now;
        m_bHasSent = true;
        m_bFullDue = false;
    }

    // a FULL couldn't go out (bigger than one packet): no new try every frame; the next change or mark tries again
    void OnSendRefused() { m_bFullDue = false; }

    void ForgetSent()
    {
        m_sent.clear();
        m_bHasSent = false;
        m_bFullDue = false;
    }

    // ---- the other games ----

    // a FULL from senderId: replaces the desired blob
    WorldStatePolicy::eReceive OnFull(int senderId, const uint8_t* data, size_t size, uint16_t recordSize)
    {
        using namespace WorldStatePolicy;
        if (!IsValidFull(data, size, recordSize))
        {
            return eReceive::MALFORMED;
        }
        SLayout layout;
        ReadLayout(data, size, layout);
        if (m_bLayoutRejected)
        {
            if (layout == m_rejectedLayout)
            {
                return eReceive::LAYOUT_REJECTED;
            }
            m_bLayoutRejected = false;  // another layout gets another check
        }
        m_desired.assign(data, data + size);
        m_bHasDesired = true;
        m_nDesiredSender = senderId;
        return eReceive::ACCEPTED;
    }

    // RECORDS from senderId: patch the desired blob from the same sender and layout, all records or none
    WorldStatePolicy::eReceive OnRecords(int senderId, const uint8_t* data, size_t size, uint16_t recordSize)
    {
        using namespace WorldStatePolicy;
        SLayout layout;
        if (recordSize == 0 || !ReadLayout(data, size, layout))
        {
            return eReceive::MALFORMED;
        }
        if (m_bLayoutRejected && layout == m_rejectedLayout)
        {
            return eReceive::LAYOUT_REJECTED;
        }
        if (!m_bHasDesired || m_nDesiredSender != senderId)
        {
            return eReceive::NO_BASE;
        }
        SLayout base;
        ReadLayout(m_desired, base);
        if (base != layout)
        {
            return eReceive::STALE_LAYOUT;
        }

        const size_t pairSize = 2 + static_cast<size_t>(recordSize);
        const size_t body = size - HEADER_SIZE;
        if (body == 0 || body % pairSize != 0)
        {
            return eReceive::MALFORMED;
        }
        for (size_t offset = HEADER_SIZE; offset < size; offset += pairSize)
        {
            if (GetU16(data + offset) >= layout.recordCount)
            {
                return eReceive::MALFORMED;
            }
        }
        for (size_t offset = HEADER_SIZE; offset < size; offset += pairSize)
        {
            const size_t index = GetU16(data + offset);
            std::memcpy(m_desired.data() + HEADER_SIZE + index * recordSize, data + offset + 2, recordSize);
        }
        return eReceive::ACCEPTED;
    }

    // compare the desired blob with the local state (a blob Capture just made)
    WorldStatePolicy::eCheck Check(const Blob& local)
    {
        using namespace WorldStatePolicy;
        if (m_bLayoutRejected)
        {
            return eCheck::DISABLED;
        }
        if (!m_bHasDesired)
        {
            return eCheck::NOTHING;
        }
        SLayout wanted;
        SLayout have;
        ReadLayout(m_desired, wanted);
        if (!ReadLayout(local, have) || have != wanted || local.size() != m_desired.size())
        {
            m_bLayoutRejected = true;
            m_rejectedLayout = wanted;
            m_desired.clear();
            m_bHasDesired = false;
            return eCheck::LAYOUT_MISMATCH;
        }
        return local == m_desired ? eCheck::NOTHING : eCheck::APPLY;
    }

    // The host changed to hostId (-1 = nobody), and it isn't this game: a desired blob from anyone else goes. One that came
    // from the new host already stays: PLAYER_ASSIGN_HOST travels on the SYSTEM channel, and when it is resent after a
    // loss the host's first FULLs (EVENT channel) can arrive before it.
    void KeepOnlyFrom(int hostId)
    {
        if (hostId < 0 || !m_bHasDesired || m_nDesiredSender != hostId)
        {
            ForgetDesired();
        }
    }

    // also forgets a layout rejection: the next host gets a new check
    void ForgetDesired()
    {
        m_desired.clear();
        m_bHasDesired = false;
        m_nDesiredSender = -1;
        m_bLayoutRejected = false;
        m_rejectedLayout = {};
    }

    bool HasDesired() const { return m_bHasDesired; }
    const Blob& GetDesired() const { return m_desired; }
    int GetDesiredSender() const { return m_nDesiredSender; }
    bool IsDisabled() const { return m_bLayoutRejected; }
    const WorldStatePolicy::SLayout& GetRejectedLayout() const { return m_rejectedLayout; }

private:
    Blob m_sent;
    bool m_bHasSent = false;
    bool m_bFullDue = false;

    Blob m_desired;
    bool m_bHasDesired = false;
    int m_nDesiredSender = -1;
    bool m_bLayoutRejected = false;
    WorldStatePolicy::SLayout m_rejectedLayout{};
};

// This game's restarts (a new game or a load; CSessionLifecycle's GAME_RESTARTED): the apply gate waits for the next
// frame, and on the host every entry goes out in full once its game has been ready for RESTART_FULL_DELAY_MS without a
// break, so a loaded or reset world reaches the others complete.
class CWorldStateRestart
{
public:
    void OnRestarted(uint32_t frameCounter, bool bIsHost)
    {
        m_bRestarted = true;
        m_nRestartFrame = frameCounter;
        m_bFullPending = bIsHost;
        m_bReadyActive = false;
    }

    // CTimer::m_FrameCounter has moved on since the restart (it starts over at every restart and counts every frame)
    bool RanSinceRestart(uint32_t frameCounter) const { return !m_bRestarted || frameCounter != m_nRestartFrame; }

    // once per frame while this game is the host, with IsLocalGameReady and GetTickCount: true once, when the FULLs are due
    bool UpdateHost(bool bReady, uint32_t now)
    {
        if (!m_bFullPending)
        {
            return false;
        }
        if (!bReady)
        {
            m_bReadyActive = false;
            return false;
        }
        if (!m_bReadyActive)
        {
            m_bReadyActive = true;
            m_nReadySince = now;
        }
        if (now - m_nReadySince < WorldStatePolicy::RESTART_FULL_DELAY_MS)
        {
            return false;
        }
        m_bFullPending = false;
        m_bReadyActive = false;
        return true;
    }

    void CancelFull()
    {
        m_bFullPending = false;
        m_bReadyActive = false;
    }

    bool IsFullPending() const { return m_bFullPending; }

private:
    uint32_t m_nRestartFrame = 0;
    uint32_t m_nReadySince = 0;
    bool m_bRestarted = false;
    bool m_bFullPending = false;
    bool m_bReadyActive = false;
};

// Every entry's state and the lifecycle marks (design 3.6, without the story guard).
class CWorldStateTable
{
public:
    void Resize(size_t count) { m_entries.assign(count, CWorldStateEntryState{}); }
    size_t Size() const { return m_entries.size(); }
    CWorldStateEntryState& Entry(size_t index) { return m_entries[index]; }
    const CWorldStateEntryState& Entry(size_t index) const { return m_entries[index]; }
    const CWorldStateRestart& Restart() const { return m_restart; }

    // the host: a player connected, or was in when this game became the host (PLAYER_JOINED): every entry goes out in
    // full; the players who were in already get the same values again, which changes nothing
    void OnPlayerJoined() { MarkAllFull(); }

    // HOST_CHANGED. This game became the host (bIsLocal): it is the authority now, the old host's blobs go and every
    // entry goes out in full. Another player or nobody (newHostId -1): nothing more is sent from here, only what the new
    // host has sent already stays, and its FULLs follow (it marks them when it becomes the host).
    void OnHostChanged(bool bIsLocal, int newHostId)
    {
        for (CWorldStateEntryState& entry : m_entries)
        {
            entry.ForgetSent();
            if (bIsLocal)
            {
                entry.ForgetDesired();
                entry.MarkFull();
            }
            else
            {
                entry.KeepOnlyFrom(newHostId);
            }
        }
        if (!bIsLocal)
        {
            m_restart.CancelFull();
        }
    }

    // the session ended: what was sent and received goes; the engine keeps its state (no rollback)
    void OnDisconnected()
    {
        for (CWorldStateEntryState& entry : m_entries)
        {
            entry.ForgetSent();
            entry.ForgetDesired();
        }
        m_restart.CancelFull();
    }

    // GAME_RESTARTED: the other games keep the host's blobs (their gate waits, then the re-apply puts the host's world
    // back); the host sends everything in full once its game runs (UpdateHostRestart)
    void OnGameRestarted(uint32_t frameCounter, bool bIsHost) { m_restart.OnRestarted(frameCounter, bIsHost); }

    bool RanSinceRestart(uint32_t frameCounter) const { return m_restart.RanSinceRestart(frameCounter); }

    // once per frame while this game is the host: true, with every entry marked for FULL, when the restart's wait is over
    bool UpdateHostRestart(bool bReady, uint32_t now)
    {
        if (!m_restart.UpdateHost(bReady, now))
        {
            return false;
        }
        MarkAllFull();
        return true;
    }

private:
    void MarkAllFull()
    {
        for (CWorldStateEntryState& entry : m_entries)
        {
            entry.MarkFull();
        }
    }

    std::vector<CWorldStateEntryState> m_entries;
    CWorldStateRestart m_restart;
};
