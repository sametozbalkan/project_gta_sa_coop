#pragma once

#include "CWorldStatePolicy.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Private build: the logic of the world-state registry's doors and garages without game types (entry 4 Garages and the
// EntryExits adapter in CWorldStateEntriesDoors.cpp; docs/design/world-state.md W5, W10 and 3.2, decision W-D7 in
// docs/design/README.md), so that tests/world_state_doors_test.cpp can check it.

namespace WorldStateDoorsPolicy
{
using WorldStatePolicy::Blob;

// A name in a fixed field of maxLength bytes (a garage name, a script name; not necessarily terminated within the
// field) equals `expected`, ignoring case as the game's own name lookups do (_stricmp, _strnicmp).
inline bool NameEquals(const char* name, size_t maxLength, const char* expected)
{
    size_t i = 0;
    for (; i < maxLength && name[i] != '\0'; i++)
    {
        if (expected[i] == '\0')
        {
            return false;
        }
        char a = name[i];
        char b = expected[i];
        if (a >= 'a' && a <= 'z')
        {
            a = static_cast<char>(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z')
        {
            b = static_cast<char>(b - 'a' + 'A');
        }
        if (a != b)
        {
            return false;
        }
    }
    return expected[i] == '\0';
}

// ---- garages (entry 4) ----

// CGarages::aGarages holds 50 garages (gta-reversed Garages.h, MAX_NUM_GARAGES)
constexpr int GARAGE_COUNT_MAX = 50;
// CGarage::m_anName, not necessarily terminated within it
constexpr size_t GARAGE_NAME_SIZE = 8;
// CGarage flags (+0x4E): 0x02 inactive, cleared by CGarages::ActivateGarage and set by DeActivateGarage (1.0 US bytes,
// see CWorldStateEntriesDoors.cpp). The other bits are not mirrored: 0x80 resprays always free (Michelle's garage while
// she is the player's girlfriend, a per-player perk), the door, camera and respray-used bits.
constexpr uint8_t GARAGE_FLAG_INACTIVE = 0x02;
// the highest garage type 1.0 knows: gta-reversed eGarageType ends at HANGAR_ABANDONED_AIRPORT (45), the scripts'
// GarageType at VerdantMeadowsHangar (45)
constexpr uint8_t GARAGE_TYPE_MAX = 45;
// a record: type, inactive (0 or 1)
constexpr uint16_t GARAGE_RECORD_SIZE = 2;
// the blob's format, the start of the layout hash; the garage names in index order follow
constexpr uint32_t GARAGES_LAYOUT = WorldStatePolicy::Fnv1a32Text("Garages:type-u8,inactive-u8");

struct SGarageRecord
{
    uint8_t type = 0;
    uint8_t inactive = 0;

    bool operator==(const SGarageRecord& other) const { return type == other.type && inactive == other.inactive; }
    bool operator!=(const SGarageRecord& other) const { return !(*this == other); }
};

inline SGarageRecord MakeGarageRecord(uint8_t type, uint8_t flags)
{
    return {type, static_cast<uint8_t>((flags & GARAGE_FLAG_INACTIVE) != 0 ? 1 : 0)};
}

// One name of the layout hash: the bytes up to the terminator (at most maxLength), then a 0 as separator.
inline uint32_t AddGarageName(uint32_t hash, const char* name, size_t maxLength)
{
    size_t length = 0;
    while (length < maxLength && name[length] != '\0')
    {
        length++;
    }
    hash = WorldStatePolicy::Fnv1a32(name, length, hash);
    const uint8_t separator = 0;
    return WorldStatePolicy::Fnv1a32(&separator, 1, hash);
}

// The garages whose type and activity stay per player: MICHDR, Michelle's garage. Her girlfriend agent (GFAGNT.txt,
// which every game runs from GFINIT) turns it into a free Pay 'n' Spray (type Respray with flag 0x80) while she is the
// player's girlfriend and back to ForScriptToOpenAndClose when he leaves its area. Its record is {0, 0} on every game,
// so it never differs and is never applied. The name is compared case-insensitively, as GetGarageNumberByName does.
inline bool IsPerPlayerGarage(const char* name, size_t maxLength)
{
    return NameEquals(name, maxLength, "MICHDR");
}

// the record of garage `index` in a FULL blob of this entry (the registry checks the size against the record count)
inline SGarageRecord GetGarageRecord(const Blob& blob, size_t index)
{
    const size_t offset = WorldStatePolicy::HEADER_SIZE + index * GARAGE_RECORD_SIZE;
    return {blob[offset], blob[offset + 1]};
}

inline void PutGarageRecord(Blob& blob, const SGarageRecord& record)
{
    blob.push_back(record.type);
    blob.push_back(record.inactive);
}

// one garage as the game has it
struct SGarageState
{
    const char* name = nullptr;  // GARAGE_NAME_SIZE bytes
    uint8_t type = 0;
    uint8_t flags = 0;
};

// The entry's blob: the layout header (the garage count; the format and the names in index order, hashed), then one
// record per garage, {0, 0} for a per-player garage.
inline void BuildGaragesBlob(Blob& blob, const SGarageState* garages, size_t count)
{
    uint32_t hash = GARAGES_LAYOUT;
    for (size_t i = 0; i < count; i++)
    {
        hash = AddGarageName(hash, garages[i].name, GARAGE_NAME_SIZE);
    }
    WorldStatePolicy::BeginBlob(blob, {static_cast<uint16_t>(count), hash});
    for (size_t i = 0; i < count; i++)
    {
        const bool bPerPlayer = IsPerPlayerGarage(garages[i].name, GARAGE_NAME_SIZE);
        PutGarageRecord(blob, bPerPlayer ? SGarageRecord{} : MakeGarageRecord(garages[i].type, garages[i].flags));
    }
}

// what this game does to one garage to match the host's record
struct SGarageAction
{
    bool bChangeType = false;   // CGarages::ChangeGarageType(index, type) first
    uint8_t type = 0;
    bool bSetActive = false;    // then CGarages::ActivateGarage (bActive) or DeActivateGarage
    bool bActive = false;
    bool bUnknownType = false;  // the host's type is not one 1.0 knows: the type is left as it is
};

inline SGarageAction PlanGarage(const SGarageRecord& desired, const SGarageRecord& local)
{
    SGarageAction action;
    if (desired.type != local.type)
    {
        if (desired.type <= GARAGE_TYPE_MAX)
        {
            action.bChangeType = true;
            action.type = desired.type;
        }
        else
        {
            action.bUnknownType = true;
        }
    }
    // ChangeGarageType leaves the flags alone (it writes the type, the door state and the door position)
    if ((desired.inactive != 0) != (local.inactive != 0))
    {
        action.bSetActive = true;
        action.bActive = desired.inactive == 0;
    }
    return action;
}

// ---- entry-exits (the EntryExits adapter) ----

// CEntryExit::m_nFlags bits (names from the scripts' EntryexitsFlag enum) that this game's engine changes for its own
// player around a transition, read from the 1.0 US bytes. They stay this game's own when the host's flags are
// applied, by the adapter and by CEntryExitMarkerSync::Receive (W-D7):
//   0x0002 Oldstyle   set on the entered marker when the player warps without a door (CEntryExit::TransitionStarted,
//                     `or byte ptr [esi+30h],2` at 0x4403DA); TransitionFinished then takes the fade path
//   0x0008 Shop       read on the spawn point by CEntryExit::TransitionFinished (0x4409F0) to load the shop; never
//                     written at run time, so the same on every game (kept per game as W-D7 decided)
//   0x0010 Shopexit   set on the entered marker when its spawn point is a shop (TransitionFinished 0x440A04)
//   0x2000 InUse      set on the entered marker (TransitionStarted 0x440113), cleared on the spawn point
//                     (TransitionFinished 0x44071D, 0x440A33)
//   0x8000 Temporary  set when the building of a 2dfx marker in use streams out (CEntity::DestroyEffects 0x533CFF);
//                     such a marker is deleted once its transition is over (TransitionFinished 0x440A3C)
constexpr uint16_t ENEX_FLAG_OLDSTYLE = 0x0002;
constexpr uint16_t ENEX_FLAG_SHOP = 0x0008;
constexpr uint16_t ENEX_FLAG_SHOPEXIT = 0x0010;
constexpr uint16_t ENEX_FLAG_IN_USE = 0x2000;
constexpr uint16_t ENEX_FLAG_TEMPORARY = 0x8000;
constexpr uint16_t ENEX_LOCAL_FLAGS =
    ENEX_FLAG_OLDSTYLE | ENEX_FLAG_SHOP | ENEX_FLAG_SHOPEXIT | ENEX_FLAG_IN_USE | ENEX_FLAG_TEMPORARY;

// Scripts of this game that switch entry-exits for its own player outside a mission. The re-apply waits while one of
// them runs, as the registry's gate waits during a mission of this game:
// - GFDATE, the girlfriend date, a streamed script her agent GFAGNT starts on every game: it opens the date
//   restaurants and sets NoWarp (the date transition) and WarpGroup on restaurants and bars for the date, and puts
//   them back afterwards (GFDATE.txt);
// - CARMOD, the mod garage, a streamed script MAIN starts while a mod garage is open: it switches the garage
//   interior's marker (CARMOD1..3) off while it runs, so that the marker doesn't take the car out (CARMOD.txt).
inline bool IsOwnEnExScript(const char* name, size_t maxLength)
{
    return NameEquals(name, maxLength, "GFDATE") || NameEquals(name, maxLength, "CARMOD");
}

// the host's flags differ from this game's in a bit that follows the host
inline bool EnExFlagsDiffer(uint16_t host, uint16_t local)
{
    return ((host ^ local) & static_cast<uint16_t>(~ENEX_LOCAL_FLAGS)) != 0;
}

// the host's flags with this game's own transition bits
inline uint16_t MergeEnExFlags(uint16_t host, uint16_t local)
{
    return static_cast<uint16_t>((host & ~ENEX_LOCAL_FLAGS) | (local & ENEX_LOCAL_FLAGS));
}

// Upstream's identity of a marker (CEntryExitMarkerSync): the floor of its entrance rectangle's left and bottom and its
// area. The pool slots differ between games: 2dfx markers come and go with the buildings that carry them (gta-reversed
// CEntity::CreateRwObject and DeleteRwObject create and destroy the effects; CEntity::DestroyEffects 0x533BF0 deletes
// the marker, 0x533D0C), in each game's streaming order.
inline uint64_t MakeEnExKey(int16_t left, int16_t bottom, uint8_t area)
{
    return (static_cast<uint64_t>(static_cast<uint16_t>(left)) << 24) |
           (static_cast<uint64_t>(static_cast<uint16_t>(bottom)) << 8) | area;
}

struct SEnExRecord
{
    uint64_t key = 0;
    uint16_t flags = 0;
};

// the two switches upstream's packet carries besides the markers
struct SEnExGlobals
{
    bool bDisabled = false;  // CEntryExitManager::ms_bDisabled (disable_all_entry_exits)
    bool bBurglary = false;  // CEntryExitManager::ms_bBurglaryHousesEnabled

    bool operator==(const SEnExGlobals& other) const
    {
        return bDisabled == other.bDisabled && bBurglary == other.bBurglary;
    }
    bool operator!=(const SEnExGlobals& other) const { return !(*this == other); }
};

// A packet's markers by key. For a key the packet holds more than once the last one counts, as in
// CEntryExitMarkerSync::Receive, which applies the packet's markers in order to every local marker with that key.
class CEnExIndex
{
public:
    void Build(const SEnExRecord* records, size_t count)
    {
        m_records.assign(records, records + count);
        // stable: equal keys stay in packet order, and the last of each run is kept
        std::stable_sort(m_records.begin(), m_records.end(),
            [](const SEnExRecord& a, const SEnExRecord& b) { return a.key < b.key; });
        std::vector<SEnExRecord> unique;
        unique.reserve(m_records.size());
        for (size_t i = 0; i < m_records.size(); i++)
        {
            if (i + 1 < m_records.size() && m_records[i + 1].key == m_records[i].key)
            {
                continue;
            }
            unique.push_back(m_records[i]);
        }
        m_records.swap(unique);
    }

    bool Find(uint64_t key, uint16_t& flags) const
    {
        const auto it = std::lower_bound(m_records.begin(), m_records.end(), key,
            [](const SEnExRecord& record, uint64_t value) { return record.key < value; });
        if (it == m_records.end() || it->key != key)
        {
            return false;
        }
        flags = it->flags;
        return true;
    }

    void Clear() { m_records.clear(); }
    size_t Size() const { return m_records.size(); }

private:
    std::vector<SEnExRecord> m_records;  // sorted by key, one per key
};

// The host: how many of its markers changed (under the mask) since the packet that went out last, each switch counted
// as one. Only markers that went out last count; one that streamed in or out since changes nothing by itself (a script
// command sends at once anyway, through upstream's opcode hook).
inline size_t CountHostChanges(const CEnExIndex& sent, const SEnExGlobals& sentGlobals, const SEnExRecord* now,
    size_t count, const SEnExGlobals& nowGlobals)
{
    size_t changes = 0;
    for (size_t i = 0; i < count; i++)
    {
        uint16_t sentFlags = 0;
        if (sent.Find(now[i].key, sentFlags) && EnExFlagsDiffer(sentFlags, now[i].flags))
        {
            changes++;
        }
    }
    if (sentGlobals.bDisabled != nowGlobals.bDisabled)
    {
        changes++;
    }
    if (sentGlobals.bBurglary != nowGlobals.bBurglary)
    {
        changes++;
    }
    return changes;
}

// The other games: is local marker `local` one the host's packet has, with other flags (under the mask)? `merged` gets
// the flags it should have.
inline bool NeedsHostFlags(const CEnExIndex& host, const SEnExRecord& local, uint16_t& merged)
{
    uint16_t hostFlags = 0;
    if (!host.Find(local.key, hostFlags) || !EnExFlagsDiffer(hostFlags, local.flags))
    {
        return false;
    }
    merged = MergeEnExFlags(hostFlags, local.flags);
    return true;
}

// The other games: how many local markers and switches differ from the host's packet.
inline size_t CountDifferences(const CEnExIndex& host, const SEnExGlobals& hostGlobals, const SEnExRecord* local,
    size_t count, const SEnExGlobals& localGlobals)
{
    size_t differences = 0;
    uint16_t merged = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (NeedsHostFlags(host, local[i], merged))
        {
            differences++;
        }
    }
    if (hostGlobals.bDisabled != localGlobals.bDisabled)
    {
        differences++;
    }
    if (hostGlobals.bBurglary != localGlobals.bBurglary)
    {
        differences++;
    }
    return differences;
}

// The adapter's blob for the registry. The EnEx state itself travels in upstream's ENEX_SYNC; the registry only
// schedules, gates and logs the re-apply. Its desired blob says "no difference" (0) and is delivered locally whenever
// the host's ENEX_SYNC arrives; the local blob holds the number of differences (at most 255), so the registry applies
// while there are any and its log shows how many.
constexpr uint32_t ENEX_ADAPTER_LAYOUT = WorldStatePolicy::Fnv1a32Text("EntryExits:adapter:differences-u8");

inline void MakeEnExAdapterBlob(Blob& blob, size_t differences)
{
    WorldStatePolicy::BeginBlob(blob, {0, ENEX_ADAPTER_LAYOUT});
    blob.push_back(static_cast<uint8_t>(std::min<size_t>(differences, UINT8_MAX)));
}
}  // namespace WorldStateDoorsPolicy
