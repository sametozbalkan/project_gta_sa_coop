#pragma once

#include "CWorldStateEntry.h"
#include <cstdio>

// Private build: the world-state registry's entries (docs/design/world-state.md 3.2), registered by wire id in
// CWorldStateRegistry.cpp (CreateEntries). Each is declared here and defined in the CWorldStateEntries*.cpp file of its
// subsystem.

// 0: STAT_CITY_UNLOCKED (W1), CCityUnlockSync until 0.3.103. CWorldStateEntriesStory.cpp
class CWorldStateCityUnlock final : public CWorldStateEntry
{
public:
    uint8_t Id() const override { return 0; }
    const char* Name() const override { return "CityUnlock"; }
    uint32_t IntervalMs() const override { return 0; }
    // the other game's own MAIN and INTRO set 0 at a new game: put back at once, as CCityUnlockSync did
    bool SuspendDuringOwnMission() const override { return false; }
    bool Init() override { return true; }
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;
};

// 1: the maximum wanted level and the chaos cap that goes with it (W2). CWorldStateEntriesStory.cpp
class CWorldStateMaxWantedLevel final : public CWorldStateEntry
{
public:
    uint8_t Id() const override { return 1; }
    const char* Name() const override { return "MaxWantedLevel"; }
    uint32_t IntervalMs() const override { return 0; }
    // the other game's own MAIN sets 6, then 4 at a new game: put back at once
    bool SuspendDuringOwnMission() const override { return false; }
    bool Init() override { return true; }
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;
};

// 8: the host's mission-scoped engine settings (W12; decision W-D5 in docs/design/README.md): traffic and pedestrian
// density, random gang members and cops, resprays, military zones, the wanted multiplier, random trains, ambient planes,
// cop bikes and forced weather. Missions set them and CMissionCleanup::Process puts them back at every mission end, but
// only in the game whose script ran the mission: the other games follow the host's live values, so the host's mission
// end (passed or failed) reaches them too. CWorldStateEntriesMission.cpp (engine side, 1.0 US addresses)
//
// The logic without game types, for tests/world_state_mission_test.cpp: the blob, the engine calls that make a game match
// the host's settings, the values a mission end leaves and when the host's loss resets them.
namespace MissionSettingsPolicy
{
using WorldStatePolicy::Blob;

// CWeather::ForcedWeatherType when no weather is forced (CWeather::ReleaseWeather)
constexpr int16_t WEATHER_NONE = -1;

// The settings in the engine's own units. The defaults are what a mission end leaves (and a new game starts with).
struct SSettings
{
    float carDensity = 1.0f;               // CCarCtrl::CarDensityMultiplier
    float pedDensity = 1.0f;               // CPopulation::PedDensityMultiplier
    int32_t randomPedsType = -1;           // CPopulation::m_AllRandomPedsThisType (-1: any type)
    float wantedMultiplier = 1.0f;         // CWanted::m_fMultiplier of the local player
    int16_t forcedWeather = WEATHER_NONE;  // CWeather::ForcedWeatherType
    bool noRandomGangMembers = false;      // CPopulation::m_bDontCreateRandomGangMembers
    bool onlyRandomGangMembers = false;    // CPopulation::m_bOnlyCreateRandomGangMembers
    bool noRandomCops = false;             // CPopulation::m_bDontCreateRandomCops
    bool noResprays = false;               // CGarages::NoResprays
    bool freeResprays = false;             // CGarages::RespraysAreFree (a mission end leaves it alone)
    bool militaryZonesDisabled = false;    // CCullZones::bMilitaryZonesDisabled
    bool randomTrainsDisabled = false;     // CTrain::bDisableRandomTrains
    bool ambientPlanesOn = true;           // CPlane::GenPlane_Active
    bool copBikesDisabled = false;         // CStreaming::m_bDisableCopBikes
};

// one bit per setting, in blob order
enum eField : uint32_t
{
    CAR_DENSITY = 1u << 0,
    PED_DENSITY = 1u << 1,
    RANDOM_PEDS_TYPE = 1u << 2,
    WANTED_MULTIPLIER = 1u << 3,
    FORCED_WEATHER = 1u << 4,
    NO_RANDOM_GANG_MEMBERS = 1u << 5,
    ONLY_RANDOM_GANG_MEMBERS = 1u << 6,
    NO_RANDOM_COPS = 1u << 7,
    NO_RESPRAYS = 1u << 8,
    FREE_RESPRAYS = 1u << 9,
    MILITARY_ZONES_DISABLED = 1u << 10,
    RANDOM_TRAINS_DISABLED = 1u << 11,
    AMBIENT_PLANES_ON = 1u << 12,
    COP_BIKES_DISABLED = 1u << 13,
};
constexpr int FIELD_COUNT = 14;

// The blob: the layout header (no records), then the settings in SSettings order, little-endian: the car density, the
// ped density, the ped type (int32), the wanted multiplier (floats as their bits), the weather (int16), then one byte
// (0 or 1) per flag. Equal blobs mean equal settings; floats compare by their bits.
constexpr uint32_t LAYOUT = WorldStatePolicy::Fnv1a32Text(
    "MissionSettings:car-f32,ped-f32,ped-type-i32,wanted-f32,weather-i16,gang-off,gang-only,cops-off,no-resprays,"
    "free-resprays,military-off,trains-off,planes-on,cop-bikes-off");
constexpr size_t PAYLOAD_SIZE = 4 * 4 + 2 + 9;
constexpr size_t BLOB_SIZE = WorldStatePolicy::HEADER_SIZE + PAYLOAD_SIZE;

inline uint32_t FloatBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float BitsFloat(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline void Write(const SSettings& settings, Blob& blob)
{
    using namespace WorldStatePolicy;
    BeginBlob(blob, {0, LAYOUT});
    PutU32(blob, FloatBits(settings.carDensity));
    PutU32(blob, FloatBits(settings.pedDensity));
    PutU32(blob, static_cast<uint32_t>(settings.randomPedsType));
    PutU32(blob, FloatBits(settings.wantedMultiplier));
    PutU16(blob, static_cast<uint16_t>(settings.forcedWeather));
    const bool flags[] = {settings.noRandomGangMembers, settings.onlyRandomGangMembers, settings.noRandomCops,
        settings.noResprays, settings.freeResprays, settings.militaryZonesDisabled, settings.randomTrainsDisabled,
        settings.ambientPlanesOn, settings.copBikesDisabled};
    for (bool flag : flags)
    {
        blob.push_back(static_cast<uint8_t>(flag ? 1 : 0));
    }
}

// false when the blob isn't this layout
inline bool Read(const Blob& blob, SSettings& settings)
{
    using namespace WorldStatePolicy;
    SLayout layout;
    if (blob.size() != BLOB_SIZE || !ReadLayout(blob, layout) || layout != SLayout{0, LAYOUT})
    {
        return false;
    }
    const uint8_t* data = blob.data() + HEADER_SIZE;
    settings.carDensity = BitsFloat(GetU32(data));
    settings.pedDensity = BitsFloat(GetU32(data + 4));
    settings.randomPedsType = static_cast<int32_t>(GetU32(data + 8));
    settings.wantedMultiplier = BitsFloat(GetU32(data + 12));
    settings.forcedWeather = static_cast<int16_t>(GetU16(data + 16));
    const uint8_t* flags = data + 18;
    settings.noRandomGangMembers = flags[0] != 0;
    settings.onlyRandomGangMembers = flags[1] != 0;
    settings.noRandomCops = flags[2] != 0;
    settings.noResprays = flags[3] != 0;
    settings.freeResprays = flags[4] != 0;
    settings.militaryZonesDisabled = flags[5] != 0;
    settings.randomTrainsDisabled = flags[6] != 0;
    settings.ambientPlanesOn = flags[7] != 0;
    settings.copBikesDisabled = flags[8] != 0;
    return true;
}

inline uint32_t FieldIf(bool bDiffers, uint32_t field) { return bDiffers ? field : 0u; }

// the fields (eField bits) in which `a` and `b` differ
inline uint32_t Diff(const SSettings& a, const SSettings& b)
{
    return FieldIf(FloatBits(a.carDensity) != FloatBits(b.carDensity), CAR_DENSITY) |
           FieldIf(FloatBits(a.pedDensity) != FloatBits(b.pedDensity), PED_DENSITY) |
           FieldIf(a.randomPedsType != b.randomPedsType, RANDOM_PEDS_TYPE) |
           FieldIf(FloatBits(a.wantedMultiplier) != FloatBits(b.wantedMultiplier), WANTED_MULTIPLIER) |
           FieldIf(a.forcedWeather != b.forcedWeather, FORCED_WEATHER) |
           FieldIf(a.noRandomGangMembers != b.noRandomGangMembers, NO_RANDOM_GANG_MEMBERS) |
           FieldIf(a.onlyRandomGangMembers != b.onlyRandomGangMembers, ONLY_RANDOM_GANG_MEMBERS) |
           FieldIf(a.noRandomCops != b.noRandomCops, NO_RANDOM_COPS) |
           FieldIf(a.noResprays != b.noResprays, NO_RESPRAYS) |
           FieldIf(a.freeResprays != b.freeResprays, FREE_RESPRAYS) |
           FieldIf(a.militaryZonesDisabled != b.militaryZonesDisabled, MILITARY_ZONES_DISABLED) |
           FieldIf(a.randomTrainsDisabled != b.randomTrainsDisabled, RANDOM_TRAINS_DISABLED) |
           FieldIf(a.ambientPlanesOn != b.ambientPlanesOn, AMBIENT_PLANES_ON) |
           FieldIf(a.copBikesDisabled != b.copBikesDisabled, COP_BIKES_DISABLED);
}

// Makes a game that has `local` match `desired`: only the fields that differ, each the way its script command sets it
// (CWorldStateEntriesMission.cpp lists the commands). TEngine makes the calls: the game in the entry, a recorder in the
// test. Returns the fields it changed.
template <typename TEngine>
uint32_t ApplyDifferences(const SSettings& local, const SSettings& desired, TEngine& engine)
{
    const uint32_t fields = Diff(local, desired);
    if (fields & CAR_DENSITY)
    {
        engine.SetCarDensity(desired.carDensity);
    }
    if (fields & PED_DENSITY)
    {
        engine.SetPedDensity(desired.pedDensity);
    }
    if (fields & RANDOM_PEDS_TYPE)
    {
        engine.SetRandomPedsType(desired.randomPedsType);
    }
    if (fields & WANTED_MULTIPLIER)
    {
        engine.SetWantedMultiplier(desired.wantedMultiplier);
    }
    if (fields & FORCED_WEATHER)
    {
        if (desired.forcedWeather == WEATHER_NONE)
        {
            engine.ReleaseWeather();
        }
        else
        {
            engine.ForceWeather(desired.forcedWeather);
        }
    }
    if (fields & NO_RANDOM_GANG_MEMBERS)
    {
        engine.SetNoRandomGangMembers(desired.noRandomGangMembers);
    }
    if (fields & ONLY_RANDOM_GANG_MEMBERS)
    {
        engine.SetOnlyRandomGangMembers(desired.onlyRandomGangMembers);
    }
    if (fields & NO_RANDOM_COPS)
    {
        engine.SetNoRandomCops(desired.noRandomCops);
    }
    if (fields & NO_RESPRAYS)
    {
        // SET_NO_RESPRAYS also closes every Pay 'n' Spray door, or opens them again
        engine.SetNoResprays(desired.noResprays);
        engine.OpenOrCloseAllResprays(!desired.noResprays);
    }
    if (fields & FREE_RESPRAYS)
    {
        engine.SetFreeResprays(desired.freeResprays);
    }
    if (fields & MILITARY_ZONES_DISABLED)
    {
        engine.SetMilitaryZonesDisabled(desired.militaryZonesDisabled);
    }
    if (fields & RANDOM_TRAINS_DISABLED)
    {
        engine.DisableRandomTrains(desired.randomTrainsDisabled);
    }
    if (fields & AMBIENT_PLANES_ON)
    {
        engine.SwitchAmbientPlanes(desired.ambientPlanesOn);
    }
    if (fields & COP_BIKES_DISABLED)
    {
        engine.DisableCopBikes(desired.copBikesDisabled);
    }
    return fields;
}

// What CMissionCleanup::Process leaves at a mission end: every setting back to its default except free resprays, which
// it doesn't touch.
inline void ApplyMissionCleanup(SSettings& settings)
{
    const bool freeResprays = settings.freeResprays;
    settings = SSettings{};
    settings.freeResprays = freeResprays;
}

// HOST_MISSION_ENDED on a game that wasn't the host: when the host left (or this game's session ended) during its
// mission, no host is left to send its mission end, so this game ends the mission's settings itself, as the engine would
// have. Not while a mission of this game runs: its own mission end resets them in this game.
inline bool ResetsAfterHostLoss(bool bHostLost, bool bLocal, bool bLocalMissionRunning)
{
    return bHostLost && !bLocal && !bLocalMissionRunning;
}

inline const char* FieldName(uint32_t field)
{
    switch (field)
    {
        case CAR_DENSITY: return "car";
        case PED_DENSITY: return "ped";
        case RANDOM_PEDS_TYPE: return "pedType";
        case WANTED_MULTIPLIER: return "wanted";
        case FORCED_WEATHER: return "weather";
        case NO_RANDOM_GANG_MEMBERS: return "gangOff";
        case ONLY_RANDOM_GANG_MEMBERS: return "gangOnly";
        case NO_RANDOM_COPS: return "copsOff";
        case NO_RESPRAYS: return "noResprays";
        case FREE_RESPRAYS: return "freeResprays";
        case MILITARY_ZONES_DISABLED: return "militaryOff";
        case RANDOM_TRAINS_DISABLED: return "trainsOff";
        case AMBIENT_PLANES_ON: return "planesOn";
        case COP_BIKES_DISABLED: return "copBikesOff";
    }
    return "?";
}

// diagnostics: "car,weather", "-" for none
inline void FormatFields(uint32_t fields, char* text, size_t size)
{
    if (size == 0)
    {
        return;
    }
    text[0] = '\0';
    size_t length = 0;
    for (int bit = 0; bit < FIELD_COUNT; bit++)
    {
        const uint32_t field = 1u << bit;
        if ((fields & field) == 0)
        {
            continue;
        }
        const int written = std::snprintf(text + length, size - length, "%s%s", length > 0 ? "," : "", FieldName(field));
        if (written < 0 || static_cast<size_t>(written) >= size - length)
        {
            break;
        }
        length += static_cast<size_t>(written);
    }
    if (length == 0)
    {
        std::snprintf(text, size, "-");
    }
}

// diagnostics: every setting by its FieldName
inline void Format(const SSettings& settings, char* text, size_t size)
{
    std::snprintf(text, size,
        "car=%.2f ped=%.2f pedType=%d wanted=%.2f weather=%d gangOff=%d gangOnly=%d copsOff=%d noResprays=%d "
        "freeResprays=%d militaryOff=%d trainsOff=%d planesOn=%d copBikesOff=%d",
        static_cast<double>(settings.carDensity), static_cast<double>(settings.pedDensity),
        static_cast<int>(settings.randomPedsType), static_cast<double>(settings.wantedMultiplier),
        static_cast<int>(settings.forcedWeather), static_cast<int>(settings.noRandomGangMembers),
        static_cast<int>(settings.onlyRandomGangMembers), static_cast<int>(settings.noRandomCops),
        static_cast<int>(settings.noResprays), static_cast<int>(settings.freeResprays),
        static_cast<int>(settings.militaryZonesDisabled), static_cast<int>(settings.randomTrainsDisabled),
        static_cast<int>(settings.ambientPlanesOn), static_cast<int>(settings.copBikesDisabled));
}
}  // namespace MissionSettingsPolicy

class CWorldStateMissionSettings final : public CWorldStateEntry
{
public:
    uint8_t Id() const override { return 8; }
    const char* Name() const override { return "MissionSettings"; }
    // every frame, as the synced commands of registered missions that set the same values (01EB, 03DE, 03C7, 0A24): the
    // host sends a change in the frame its script made it, so a replayed command and this entry's packet are handled
    // together and the re-apply never puts the old value back in between
    uint32_t IntervalMs() const override { return 0; }
    // the other game's own missions (INITIAL, INITIL2 and INTRO at a new game; taxi, vigilante and the other side missions)
    // set these too: wait until they have ended (their own mission end resets them there), then the host's values return
    bool SuspendDuringOwnMission() const override { return true; }
    bool Init() override;
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;
};
class CEntryExit;
struct SEntryExitFlags;
namespace Packets::Scripts
{
class EnExSync;
}

// 4: the garages' type and whether they are active (W5); not their doors, not Michelle's garage.
// CWorldStateEntriesDoors.cpp
class CWorldStateGarages final : public CWorldStateEntry
{
public:
    uint8_t Id() const override { return 4; }
    const char* Name() const override { return "Garages"; }
    uint32_t IntervalMs() const override { return 1000; }
    uint16_t RecordSize() const override;
    bool Init() override { return true; }
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;
};

// The EntryExits adapter (W10, W-D7): upstream's ENEX_SYNC carries the host's entry-exit flags and the server keeps the
// last one for joiners. The adapter adds what it lacks: the host sends it again when its flags changed without a script
// command (a save load), and the other games put the host's flags back after their own scripts changed them (their
// INTRO closes the shops again at a new game), keeping their own transition bits and leaving their girlfriend dates and
// mod garage visits alone. It has no wire id: its Id() only names it inside the registry, nothing goes out as
// WORLD_STATE for it. CWorldStateEntriesDoors.cpp
class CWorldStateEntryExits final : public CWorldStateEntry
{
public:
    static constexpr uint8_t LOCAL_ID = 0xFF;  // never on the wire, never a wire id of another entry

    uint8_t Id() const override { return LOCAL_ID; }
    const char* Name() const override { return "EntryExits"; }
    uint32_t IntervalMs() const override { return 1000; }
    bool Init() override { return true; }
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;

    // --- CEntryExitMarkerSync ---
    // Send: the packet that went out (the host's poll compares with it)
    static void OnSent(const Packets::Scripts::EnExSync& packet);
    // Receive: one of the host's markers for a local one with the same key; this game keeps its own transition bits,
    // and a marker this game's player is passing through is left to the re-apply once the transition is over
    static void ApplyHostFlags(CEntryExit* pEntryExit, const SEntryExitFlags& hostFlags);
    // Receive, at the end: CEntryExitMarkerSync::ms_lastData now holds the host's state, the registry re-applies it
    static void OnReceived();
};
