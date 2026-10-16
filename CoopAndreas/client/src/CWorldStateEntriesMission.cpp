#include "stdafx.h"
#include "CWorldStateEntries.h"
#include "CSessionLifecycle.h"
#include <CCullZones.h>
#include <CTimer.h>
#include <CWanted.h>
#include <cstdarg>

// Private build: entry 8 of the world-state registry, the host's mission-scoped engine settings (W12 in
// docs/design/world-state.md, decision W-D5 in docs/design/README.md). The logic without game types is
// MissionSettingsPolicy in CWorldStateEntries.h (tests/world_state_mission_test.cpp).
//
// The host sends its live settings and the other games take them whenever no mission of their own runs, so the start
// and the end of the host's missions (CMissionCleanup::Process, passed or failed) reach them as changes of the host's
// values. When the host leaves during its mission no host remains to send that end, so the other games reset the
// settings as the mission end would have (HOST_MISSION_ENDED with bHostLost).
//
// gta_sa.exe 1.0 US, read with dumpbin (script command cases resolved through CRunningScript::CommandHandlerTable
// 0x8A6168 and the dispatch table of each ProcessCommandsN00ToN99) and tools/pe_peek.py; names from plugin-sdk and
// gta-reversed:
// - CMissionCleanup::Process 0x468560 (00D8 calls it at 0x469471) at every mission end: CTrain::DisableRandomTrains(false)
//   at 0x4685A8, CPlane::SwitchAmbientPlanes(true) 0x4685B4, ped and car density 1.0 at 0x4685C3 / 0x4685CD, 0x8D2534 = -1
//   at 0x4685F5, 0xC0FCB2..0xC0FCB4 = 0 at 0x468608..0x468614, CGarages::NoResprays = 0 at 0x46861A and
//   AllRespraysCloseOrOpen(true) at 0x468620, CCullZones::bMilitaryZonesDisabled = 0 at 0x468626,
//   FindPlayerWanted(-1)->m_fMultiplier = 1.0 at 0x468631, CStreaming::DisableCopBikes(false) at 0x468643,
//   CWeather::ReleaseWeather at 0x468732. CGarages::RespraysAreFree isn't reset there.
// - CCarCtrl::CarDensityMultiplier 0x8A5B20 (gta-reversed CarCtrl.h; not in plugin-sdk): SET_CAR_DENSITY_MULTIPLIER 01EB
//   (case 0x47DDBB) writes it. CPopulation::PedDensityMultiplier 0x8D2530 (plugin-sdk): SET_PED_DENSITY_MULTIPLIER 03DE
//   (case 0x485516).
// - CPopulation::m_bDontCreateRandomGangMembers 0xC0FCB2, m_bOnlyCreateRandomGangMembers 0xC0FCB3 and
//   m_bDontCreateRandomCops 0xC0FCB4 (plugin-sdk, gta-reversed): SET_CREATE_RANDOM_GANG_MEMBERS 08EA (case 0x475C91) and
//   SET_CREATE_RANDOM_COPS 099E (case 0x478FB2) store the opposite of their argument in 0xC0FCB2 and 0xC0FCB4;
//   CCheat::GangLandCheat 0x4393D0 stores the cheat's new state in 0xC0FCB3, and so does the command Sanny Builder names
//   SET_ONLY_CREATE_GANG_MEMBERS 0983 (case 0x4789BF), inverted (`sete` at 0x4789CF). CPopCycle::FindNewPedType 0x60FBD0
//   reads all three (0xC0FCB3 at 0x60FC34: only gang members while set, gta-reversed PopCycle.cpp).
// - CPopulation::m_AllRandomPedsThisType 0x8D2534 (plugin-sdk, gta-reversed; read at 0x614AA4): 1.0 US only ever writes
//   -1 (CPopulation::Initialise at 0x610E6E, CDarkel at 0x43DB88 / 0x43DBE5 / 0x43DC40 / 0x43DCAC, the mission end), and
//   the case of FORCE_RANDOM_PED_TYPE 03DF (0x485695) only returns. Mirrored as the design lists it; it never differs.
// - CGarages::NoResprays 0x96C008 and RespraysAreFree 0x96C009 (gta-reversed Garages.h; saved by CGarages::Save):
//   SET_NO_RESPRAYS 0A14 (case 0x47B983) stores its state and calls CGarages::AllRespraysCloseOrOpen(!state) 0x448B30
//   (cdecl; a jump to 0x1563A10, which reads the low byte of its argument and sets the door, +0x4D, of every garage of
//   type 5, +0x4C, to open or closed); SET_FREE_RESPRAYS 0335 (case 0x481808) stores its state. CGarage::Update reads
//   them at 0x44AC50, 0x44AEC0 and 0x44AC97.
// - CCullZones::bMilitaryZonesDisabled 0xC87ACD (plugin-sdk): SET_DISABLE_MILITARY_ZONES 0A24 (case 0x47BDB4).
// - CWanted::m_fMultiplier +0x14 (plugin-sdk CWanted.h): SET_WANTED_MULTIPLIER 03C7 (case 0x485003) writes it in
//   FindPlayerWanted(-1) 0x56E230, which returns CWorld::Players[n].m_PlayerData.m_pWanted (0xB7CD9C + n * 0x190); the
//   local player is n = 0 here. CPlayerPedData::DeAllocateData frees it and leaves null (gta-reversed PlayerPedData.cpp).
// - CTrain::DisableRandomTrains 0x6F5DB0 stores CTrain::bDisableRandomTrains 0xC38010 (plugin-sdk, gta-reversed);
//   SWITCH_RANDOM_TRAINS 06D7 (case 0x497F51) passes !state.
// - CPlane::SwitchAmbientPlanes 0x6CCC50 stores CPlane::GenPlane_Active 0x8D33BC (plugin-sdk, gta-reversed); switched off
//   while on, it first deletes the random helis and planes (vehicle type 3 or 4 at +0x594, created by 1 at +0x4A4)
//   through the CWorld::Remove call at 0x6CCCB2, which WorldHooks.cpp redirects: a vehicle this game syncs goes to the
//   others with VEHICLE_REMOVE, and the other games' vehicles are MISSION_VEHICLE here (CNetworkVehicle) and stay.
//   SWITCH_AMBIENT_PLANES 0923 (case 0x476C9B) passes state.
// - CStreaming::DisableCopBikes 0x407D10 (a jump to 0x15703D0) stores CStreaming::m_bDisableCopBikes 0x9654BF (plugin-sdk,
//   gta-reversed); SWITCH_COPS_ON_BIKES 072C (case 0x46DBE9) passes !state.
// - CWeather::ForceWeather 0x72A4E0 and ReleaseWeather 0x72A510 write CWeather::ForcedWeatherType 0xC81318 (int16, -1 =
//   none; plugin-sdk, gta-reversed): FORCE_WEATHER 01B5 (case 0x47D42B) and RELEASE_WEATHER 01B7 (case 0x47D48D) call
//   them. FORCE_WEATHER_NOW 01B6 also sets the old and new weather, which upstream's CWeatherSync (GAME_WEATHER_TIME)
//   already brings to the other games; the forced type was its gap (commented out there). CWeather::Update reads it at
//   0x72B8C0.
static_assert(offsetof(CWanted, m_fMultiplier) == 0x14, "CWanted::m_fMultiplier");

namespace
{
using namespace MissionSettingsPolicy;

// --coopdiag: at most this many records of this entry in the registry's log
constexpr unsigned int MAX_LOG_RECORDS = 256;

// the globals plugin-sdk doesn't name (sources above)
float& CarDensityMultiplier() { return *reinterpret_cast<float*>(0x8A5B20); }
uint8_t& NoResprays() { return *reinterpret_cast<uint8_t*>(0x96C008); }
uint8_t& RespraysAreFree() { return *reinterpret_cast<uint8_t*>(0x96C009); }

// a flag the engine keeps in a byte, read as the byte: set unless 0, as the engine tests it
bool IsSet(const bool& flag) { return *reinterpret_cast<const uint8_t*>(&flag) != 0; }

// The local settings. The wanted multiplier lives in the local player's CWanted, which is freed and allocated again with
// the player data (a new game, a load): false while it is missing, with the mission end's 1.0 in its place.
bool ReadLocal(SSettings& settings)
{
    CWanted* pWanted = FindPlayerWanted(0);
    settings.carDensity = CarDensityMultiplier();
    settings.pedDensity = CPopulation::PedDensityMultiplier;
    settings.randomPedsType = CPopulation::m_AllRandomPedsThisType;
    settings.wantedMultiplier = pWanted != nullptr ? pWanted->m_fMultiplier : 1.0f;
    settings.forcedWeather = CWeather::ForcedWeatherType;
    settings.noRandomGangMembers = IsSet(CPopulation::m_bDontCreateRandomGangMembers);
    settings.onlyRandomGangMembers = IsSet(CPopulation::m_bOnlyCreateRandomGangMembers);
    settings.noRandomCops = IsSet(CPopulation::m_bDontCreateRandomCops);
    settings.noResprays = NoResprays() != 0;
    settings.freeResprays = RespraysAreFree() != 0;
    settings.militaryZonesDisabled = IsSet(CCullZones::bMilitaryZonesDisabled);
    settings.randomTrainsDisabled = IsSet(CTrain::bDisableRandomTrains);
    settings.ambientPlanesOn = IsSet(CPlane::GenPlane_Active);
    settings.copBikesDisabled = IsSet(CStreaming::m_bDisableCopBikes);
    return pWanted != nullptr;
}

// MissionSettingsPolicy::ApplyDifferences' calls on this game, each the way its script command makes it (sources above)
struct SGameEngine
{
    void SetCarDensity(float value) { CarDensityMultiplier() = value; }
    void SetPedDensity(float value) { CPopulation::PedDensityMultiplier = value; }
    void SetRandomPedsType(int32_t type) { CPopulation::m_AllRandomPedsThisType = type; }
    void SetWantedMultiplier(float value)
    {
        if (CWanted* pWanted = FindPlayerWanted(0))
        {
            pWanted->m_fMultiplier = value;
        }
    }
    void ForceWeather(int16_t type) { CWeather::ForceWeather(type); }
    void ReleaseWeather() { CWeather::ReleaseWeather(); }
    void SetNoRandomGangMembers(bool state) { CPopulation::m_bDontCreateRandomGangMembers = state; }
    void SetOnlyRandomGangMembers(bool state) { CPopulation::m_bOnlyCreateRandomGangMembers = state; }
    void SetNoRandomCops(bool state) { CPopulation::m_bDontCreateRandomCops = state; }
    void SetNoResprays(bool state) { NoResprays() = static_cast<uint8_t>(state ? 1 : 0); }
    // CGarages::AllRespraysCloseOrOpen
    void OpenOrCloseAllResprays(bool bOpen) { plugin::Call<0x448B30, bool>(bOpen); }
    void SetFreeResprays(bool state) { RespraysAreFree() = static_cast<uint8_t>(state ? 1 : 0); }
    void SetMilitaryZonesDisabled(bool state) { CCullZones::bMilitaryZonesDisabled = state; }
    void DisableRandomTrains(bool state) { CTrain::DisableRandomTrains(state); }
    void SwitchAmbientPlanes(bool bOn) { CPlane::SwitchAmbientPlanes(bOn); }
    void DisableCopBikes(bool state) { CStreaming::DisableCopBikes(state); }
};

const char* RoleName()
{
    if (CSessionLifecycle::IsHost())
    {
        return "host";
    }
    return CSessionLifecycle::HasHost() ? "other" : "none";
}

// --coopdiag: this entry's details in CoopAndreas_world_state.log next to gta_sa.exe (the registry's log; these records
// count separately), at most MAX_LOG_RECORDS and while the file is under 1 MB
void LogLine(const char* format, ...)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= MAX_LOG_RECORDS)
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
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_world_state.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char text[768];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        fprintf(file, "t=%u frame=%u role=%s hostId=%d entry=MissionSettings %s\n", GetTickCount(), CTimer::m_FrameCounter,
            RoleName(), CSessionLifecycle::GetHostId(), text);
        ++records;
    }
    fclose(file);
}

// the host's settings, each time they change (the registry's own log shows only the blob's size)
void LogHostSettings(const SSettings& settings)
{
    static SSettings logged;
    static bool bLogged = false;
    if (bLogged && Diff(settings, logged) == 0)
    {
        return;
    }
    logged = settings;
    bLogged = true;
    char text[256];
    Format(settings, text, sizeof(text));
    LogLine("event=host-settings %s", text);
}

// the fields a re-apply changed on this game, when they or the host's settings differ from the last ones logged (a
// script of this game that keeps writing the same field is logged once)
void LogApplied(uint32_t fields, const SSettings& desired, const SSettings& local)
{
    static SSettings loggedDesired;
    static uint32_t loggedFields = 0;
    static bool bLogged = false;
    if (fields == 0 || (bLogged && fields == loggedFields && Diff(desired, loggedDesired) == 0))
    {
        return;
    }
    loggedDesired = desired;
    loggedFields = fields;
    bLogged = true;
    char changed[160];
    char wanted[256];
    char before[256];
    FormatFields(fields, changed, sizeof(changed));
    Format(desired, wanted, sizeof(wanted));
    Format(local, before, sizeof(before));
    LogLine("event=apply-detail changed=%s desired={%s} local={%s}", changed, wanted, before);
}

// HOST_MISSION_ENDED with bHostLost (CSessionLifecycle sends it on the games that weren't the host, once the host's last
// packets are handled): the settings go back to what the mission end leaves, unless a mission of this game runs.
void OnSessionEvent(const SSessionEvent& event)
{
    if (event.type != eSessionEvent::HOST_MISSION_ENDED || !event.bHostLost)
    {
        return;
    }
    const bool bLocalMission = CSessionLifecycle::IsLocalMissionRunning();
    if (!ResetsAfterHostLoss(event.bHostLost, event.bLocal, bLocalMission))
    {
        LogLine("event=host-lost-reset skipped=%s", bLocalMission ? "own-mission" : "local-host");
        return;
    }

    SSettings local;
    ReadLocal(local);  // without player data the wanted multiplier reads as the reset value and stays as it is
    SSettings reset = local;
    ApplyMissionCleanup(reset);
    SGameEngine engine;
    const uint32_t fields = ApplyDifferences(local, reset, engine);

    char changed[160];
    char before[256];
    FormatFields(fields, changed, sizeof(changed));
    Format(local, before, sizeof(before));
    LogLine("event=host-lost-reset changed=%s before={%s}", changed, before);
}
}  // namespace

// Called once when the registry starts, on the game thread (not from CCore::Init), so subscribing here is safe.
bool CWorldStateMissionSettings::Init()
{
    CSessionLifecycle::Subscribe(OnSessionEvent);
    return true;
}

bool CWorldStateMissionSettings::Capture(Blob& blob)
{
    SSettings settings;
    if (!ReadLocal(settings))
    {
        return false;  // the player data is being rebuilt: nothing to send or compare yet
    }
    Write(settings, blob);
    if (CSessionLifecycle::IsHost())
    {
        LogHostSettings(settings);
    }
    return true;
}

void CWorldStateMissionSettings::Apply(const Blob& desired, const Blob& local)
{
    SSettings want;
    SSettings have;
    if (!Read(desired, want) || !Read(local, have))
    {
        return;  // not this layout; the registry checks the layout header and the size before it calls Apply
    }
    SGameEngine engine;
    LogApplied(ApplyDifferences(have, want, engine), want, have);
}
