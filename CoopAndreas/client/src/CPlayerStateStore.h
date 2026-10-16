#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "network/player_state.h"

// Private build: player state for co-op saves, the logic without game calls. CPlayerStateSave.cpp drives it; the
// standalone test tests/player_state_test.cpp checks it.

namespace PlayerStateSidecar
{
// "<save file>.coop" next to the host's GTASAsf<n>.b, little-endian:
//    0  char[4]  "CAPS"
//    4  uint16   format version (2; version 1 files, from before respect was kept, are still read)
//    6  uint16   number of entries
//    8  uint32   size of the save file it was written with
//   12  uint64   FNV-1a 64 of that save file
//   20  uint32   body size in bytes
//   24  body     per entry: nickname (serialize_string), the SPlayerState in the layout of the version (GetStateLayout:
//                v1 SPlayerState::LAYOUT_V1, without respect; v2 LAYOUT_V2); bit-packed like the packets
//  end  uint32   FNV-1a 32 of everything before it
constexpr char MAGIC[4] = {'C', 'A', 'P', 'S'};
constexpr uint16_t VERSION = 2;
constexpr size_t HEADER_SIZE = 24;
constexpr size_t CHECKSUM_SIZE = 4;
constexpr size_t MAX_ENTRIES = 64;
constexpr size_t MAX_FILE_SIZE = 64 * 1024;

// the SPlayerState layout of a format version, 0 for a version this build can't read
constexpr int GetStateLayout(uint32_t version)
{
    return version == 1 ? SPlayerState::LAYOUT_V1 : version == 2 ? SPlayerState::LAYOUT_V2 : 0;
}
static_assert(GetStateLayout(VERSION) == SPlayerState::LAYOUT_CURRENT, "files are written in the packets' layout");

struct SEntry
{
    std::string name;
    SPlayerState state;
};

struct SFile
{
    uint16_t version = VERSION;  // the version Read found; Write always writes VERSION
    uint32_t saveSize = 0;
    uint64_t saveHash = 0;
    std::vector<SEntry> entries;
};

enum class eReadResult
{
    Ok,
    TooShortOrTooLong,
    BadMagic,
    UnknownVersion,
    BadChecksum,
    Corrupt
};

inline const char* ToString(eReadResult result)
{
    switch (result)
    {
        case eReadResult::Ok:
            return "ok";
        case eReadResult::TooShortOrTooLong:
            return "bad size";
        case eReadResult::BadMagic:
            return "not a player state file";
        case eReadResult::UnknownVersion:
            return "unknown version";
        case eReadResult::BadChecksum:
            return "bad checksum";
        case eReadResult::Corrupt:
            return "corrupt";
    }
    return "?";
}

namespace Detail
{
inline void Put(std::vector<uint8_t>& out, uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; i++)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

inline uint64_t Get(const uint8_t* data, int bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < bytes; i++)
    {
        value |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

template <typename Stream>
bool SerializeEntry(Stream& stream, char (&name)[Config::MAX_NICKNAME_LENGTH + 1], SPlayerState& state, int layout)
{
    serialize_string(stream, name, static_cast<int>(sizeof(name)));
    return state.SerializeLayout(stream, layout);
}
}  // namespace Detail

// false when an entry has an invalid nickname or state, a nickname twice, or there are too many entries
inline bool Write(const SFile& file, std::vector<uint8_t>& out)
{
    out.clear();
    if (file.entries.size() > MAX_ENTRIES)
    {
        return false;
    }

    // the bit writer has no bounds checks in release builds: measure (conservatively) first
    serialize::MeasureStream measure;
    for (size_t i = 0; i < file.entries.size(); i++)
    {
        const SEntry& entry = file.entries[i];
        if (!IsValidPlayerName(entry.name.c_str()))
        {
            return false;
        }
        for (size_t j = 0; j < i; j++)
        {
            if (file.entries[j].name == entry.name)
            {
                return false;
            }
        }
        char name[Config::MAX_NICKNAME_LENGTH + 1]{};
        memcpy(name, entry.name.c_str(), entry.name.size());
        SPlayerState state = entry.state;
        if (!Detail::SerializeEntry(measure, name, state, SPlayerState::LAYOUT_CURRENT))
        {
            return false;
        }
    }

    std::vector<uint8_t> body(((static_cast<size_t>(measure.GetBitsProcessed()) + 7) / 8 + 8 + 3) & ~size_t(3), 0);
    serialize::WriteStream stream(body.data(), static_cast<int>(body.size()));
    for (const SEntry& entry : file.entries)
    {
        char name[Config::MAX_NICKNAME_LENGTH + 1]{};
        memcpy(name, entry.name.c_str(), entry.name.size());
        SPlayerState state = entry.state;
        if (!Detail::SerializeEntry(stream, name, state, SPlayerState::LAYOUT_CURRENT))
        {
            return false;
        }
    }
    stream.Flush();
    const size_t bodySize = static_cast<size_t>(stream.GetBytesProcessed());

    out.insert(out.end(), MAGIC, MAGIC + sizeof(MAGIC));
    Detail::Put(out, VERSION, 2);
    Detail::Put(out, file.entries.size(), 2);
    Detail::Put(out, file.saveSize, 4);
    Detail::Put(out, file.saveHash, 8);
    Detail::Put(out, bodySize, 4);
    out.insert(out.end(), body.begin(), body.begin() + bodySize);
    Detail::Put(out, Fnv1a32(out.data(), out.size()), 4);
    return true;
}

inline eReadResult Read(const std::vector<uint8_t>& data, SFile& out)
{
    out = SFile{};
    if (data.size() < HEADER_SIZE + CHECKSUM_SIZE || data.size() > MAX_FILE_SIZE)
    {
        return eReadResult::TooShortOrTooLong;
    }
    if (memcmp(data.data(), MAGIC, sizeof(MAGIC)) != 0)
    {
        return eReadResult::BadMagic;
    }
    const uint16_t version = static_cast<uint16_t>(Detail::Get(&data[4], 2));
    const int layout = GetStateLayout(version);
    if (layout == 0)
    {
        return eReadResult::UnknownVersion;
    }
    const size_t checked = data.size() - CHECKSUM_SIZE;
    if (Detail::Get(&data[checked], 4) != Fnv1a32(data.data(), checked))
    {
        return eReadResult::BadChecksum;
    }

    const size_t count = static_cast<size_t>(Detail::Get(&data[6], 2));
    const size_t bodySize = static_cast<size_t>(Detail::Get(&data[20], 4));
    if (count > MAX_ENTRIES || bodySize != checked - HEADER_SIZE)
    {
        return eReadResult::Corrupt;
    }

    // the bit reader reads whole 32-bit words (and wants a buffer even for an empty body)
    std::vector<uint8_t> body(((bodySize + 3) & ~size_t(3)) + 4, 0);
    if (bodySize != 0)
    {
        memcpy(body.data(), &data[HEADER_SIZE], bodySize);
    }
    serialize::ReadStream stream(body.data(), static_cast<int>(bodySize));

    SFile file;
    file.version = version;
    file.saveSize = static_cast<uint32_t>(Detail::Get(&data[8], 4));
    file.saveHash = Detail::Get(&data[12], 8);
    for (size_t i = 0; i < count; i++)
    {
        char name[Config::MAX_NICKNAME_LENGTH + 1]{};
        SEntry entry;
        if (!Detail::SerializeEntry(stream, name, entry.state, layout) || !IsValidPlayerName(name))
        {
            return eReadResult::Corrupt;
        }
        entry.name = name;
        for (const SEntry& other : file.entries)
        {
            if (other.name == entry.name)
            {
                return eReadResult::Corrupt;
            }
        }
        file.entries.push_back(entry);
    }
    // the writer's body ends where its last entry does
    if (static_cast<size_t>(stream.GetBytesProcessed()) != bodySize)
    {
        return eReadResult::Corrupt;
    }
    out = file;
    return eReadResult::Ok;
}
}  // namespace PlayerStateSidecar

// Host side. One record per nickname for the whole game session: the latest state the host knows of that player,
// from their snapshots or from the sidecar of a save the host loaded, whichever came last.
//
// A restore goes out when a player's snapshot arrives from a connection the record hasn't seen (a new session, a
// reconnect after a crash, a restart of their game) and the record has a state; the snapshot itself is dropped. A load
// rolls back every player in its sidecar: those connected get a restore right away (TakeLoadRestores), the others when
// they come back. While a restore is unacknowledged, the player's snapshots are dropped, so a state from before the
// restore (a new game's empty one, or the one before a load) never overwrites the stored state.
class CPlayerStateStore
{
public:
    enum class eSnapshotResult
    {
        Accepted,
        Dropped,
        SendRestore
    };

    struct SRestore
    {
        std::string name;
        uint32_t restoreId = 0;
        SPlayerState state;
    };

    explicit CPlayerStateStore(uint32_t restoreIdSeed = 1) : m_nNextRestoreId(restoreIdSeed) {}

    eSnapshotResult OnSnapshot(const std::string& name, uint32_t connectionNonce, uint32_t ackRestoreId,
        const SPlayerState& state, SRestore& outRestore)
    {
        SRecord& record = m_records[name];
        if (connectionNonce != record.connectionNonce)
        {
            record.connectionNonce = connectionNonce;
            if (record.hasState)
            {
                record.pendingRestoreId = NextRestoreId();
                record.pushAfterLoad = false;
                outRestore = SRestore{name, record.pendingRestoreId, record.state};
                return eSnapshotResult::SendRestore;
            }
            record.pendingRestoreId = 0;
        }
        if (record.pendingRestoreId != 0)
        {
            if (ackRestoreId != record.pendingRestoreId)
            {
                return eSnapshotResult::Dropped;
            }
            record.pendingRestoreId = 0;
        }
        record.state = state;
        record.hasState = true;
        return eSnapshotResult::Accepted;
    }

    // the host's game loaded a save whose sidecar holds these entries; players not in it keep their record. An entry
    // from a v1 file has no respect (hasRespect false): its restore leaves the player's respect as it is, and their
    // next snapshot brings it into the record.
    void OnSaveLoaded(const std::vector<PlayerStateSidecar::SEntry>& entries)
    {
        for (const PlayerStateSidecar::SEntry& entry : entries)
        {
            SRecord& record = m_records[entry.name];
            record.state = entry.state;
            record.hasState = true;
            // from now on this player's snapshots wait for the restore below (or for a new connection)
            record.pendingRestoreId = NextRestoreId();
            record.pushAfterLoad = true;
        }
    }

    // after a load: the restores for the players connected now (isConnected(name)), once this game is the host
    template <class IsConnected>
    std::vector<SRestore> TakeLoadRestores(IsConnected isConnected)
    {
        std::vector<SRestore> restores;
        for (auto& [name, record] : m_records)
        {
            if (!record.pushAfterLoad)
            {
                continue;
            }
            record.pushAfterLoad = false;
            if (isConnected(name))
            {
                restores.push_back(SRestore{name, record.pendingRestoreId, record.state});
            }
        }
        return restores;
    }

    // what a new save's sidecar holds
    std::vector<PlayerStateSidecar::SEntry> GetEntries() const
    {
        std::vector<PlayerStateSidecar::SEntry> entries;
        for (const auto& [name, record] : m_records)
        {
            if (record.hasState && entries.size() < PlayerStateSidecar::MAX_ENTRIES)
            {
                entries.push_back(PlayerStateSidecar::SEntry{name, record.state});
            }
        }
        return entries;
    }

    const SPlayerState* Find(const std::string& name) const
    {
        auto it = m_records.find(name);
        return it != m_records.end() && it->second.hasState ? &it->second.state : nullptr;
    }

private:
    struct SRecord
    {
        SPlayerState state;
        bool hasState = false;
        uint32_t connectionNonce = 0;   // of the last snapshot handled
        uint32_t pendingRestoreId = 0;  // sent (or about to be sent), not acknowledged yet
        bool pushAfterLoad = false;
    };

    uint32_t NextRestoreId()
    {
        if (m_nNextRestoreId == 0)
        {
            m_nNextRestoreId = 1;
        }
        return m_nNextRestoreId++;
    }

    std::map<std::string, SRecord> m_records;
    uint32_t m_nNextRestoreId;
};

// The other players' side: when to send a snapshot and which restore to apply.
class CPlayerStateClient
{
public:
    static constexpr uint32_t CAPTURE_INTERVAL_MS = 2000;

    // a new connection or a restart of this game: the host answers the next snapshot with a restore if it has one
    void Reset(uint32_t connectionNonce)
    {
        m_nConnectionNonce = connectionNonce != 0 ? connectionNonce : 1;
        m_bHasSent = false;
        m_bForceSend = true;
    }

    uint32_t GetConnectionNonce() const { return m_nConnectionNonce; }
    uint32_t GetAckRestoreId() const { return m_nLastAppliedRestoreId; }

    // a newer restore replaces one not applied yet
    void OnRestoreReceived(uint32_t restoreId, const SPlayerState& state)
    {
        m_bHasPendingRestore = true;
        m_nPendingRestoreId = restoreId;
        m_pendingRestore = state;
    }

    bool HasPendingRestore() const { return m_bHasPendingRestore; }
    const SPlayerState& GetPendingRestore() const { return m_pendingRestore; }

    // applied, or given up as unusable: either way the host learns it in the next snapshot, which goes out at once
    void OnRestoreDone()
    {
        if (!m_bHasPendingRestore)
        {
            return;
        }
        m_bHasPendingRestore = false;
        m_nLastAppliedRestoreId = m_nPendingRestoreId;
        m_bForceSend = true;
    }

    // the host may have changed: send the current state again
    void ForceSend() { m_bForceSend = true; }

    bool IsCaptureDue(uint32_t now) const
    {
        return !m_bHasPendingRestore && (m_bForceSend || !m_bHasSent || now - m_nLastCaptureAt >= CAPTURE_INTERVAL_MS);
    }

    // call with a state captured when IsCaptureDue; true when it has to be sent
    bool OnCaptured(uint32_t now, const SPlayerState& state)
    {
        m_nLastCaptureAt = now;
        if (m_bHasPendingRestore)
        {
            return false;
        }
        if (!m_bForceSend && m_bHasSent && state == m_lastSent)
        {
            return false;
        }
        m_lastSent = state;
        m_bHasSent = true;
        m_bForceSend = false;
        return true;
    }

private:
    uint32_t m_nConnectionNonce = 1;
    uint32_t m_nLastAppliedRestoreId = 0;
    bool m_bHasPendingRestore = false;
    uint32_t m_nPendingRestoreId = 0;
    SPlayerState m_pendingRestore{};
    bool m_bHasSent = false;
    bool m_bForceSend = true;
    uint32_t m_nLastCaptureAt = 0;
    SPlayerState m_lastSent{};
};
