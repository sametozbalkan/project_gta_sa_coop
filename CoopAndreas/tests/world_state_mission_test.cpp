// Private build: policy test for the world-state registry's entry 8, MissionSettings (MissionSettingsPolicy in
// client/src/CWorldStateEntries.h), without GTA or ENet: the blob, the engine calls that make a game match the host's
// settings, the values a mission end leaves, the reset when the host is lost during its mission, and the expected effect
// played through the registry's policy (CWorldStatePolicy.h): after a failed host mission the other game's traffic and
// pedestrians come back. Build and run: see CLAUDE.md (Fork changes, world-state registry).
#include "../client/src/CWorldStateEntries.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace MissionSettingsPolicy;
using WorldStatePolicy::eCheck;
using WorldStatePolicy::eGate;
using WorldStatePolicy::eReceive;
using WorldStatePolicy::eSend;
using WorldStatePolicy::HEADER_SIZE;
using WorldStatePolicy::SGateInput;

namespace
{
// a game's settings, its Pay 'n' Spray doors and the calls ApplyDifferences made on it, in order
struct CFakeEngine
{
    SSettings state;
    bool bRespraysOpen = true;
    std::vector<std::string> calls;

    void SetCarDensity(float value) { state.carDensity = value; calls.push_back("car"); }
    void SetPedDensity(float value) { state.pedDensity = value; calls.push_back("ped"); }
    void SetRandomPedsType(int32_t type) { state.randomPedsType = type; calls.push_back("pedType"); }
    void SetWantedMultiplier(float value) { state.wantedMultiplier = value; calls.push_back("wanted"); }
    void ForceWeather(int16_t type) { state.forcedWeather = type; calls.push_back("force-weather"); }
    void ReleaseWeather() { state.forcedWeather = WEATHER_NONE; calls.push_back("release-weather"); }
    void SetNoRandomGangMembers(bool value) { state.noRandomGangMembers = value; calls.push_back("gangOff"); }
    void SetOnlyRandomGangMembers(bool value) { state.onlyRandomGangMembers = value; calls.push_back("gangOnly"); }
    void SetNoRandomCops(bool value) { state.noRandomCops = value; calls.push_back("copsOff"); }
    void SetNoResprays(bool value) { state.noResprays = value; calls.push_back("noResprays"); }
    void OpenOrCloseAllResprays(bool bOpen)
    {
        bRespraysOpen = bOpen;
        calls.push_back(bOpen ? "resprays-open" : "resprays-closed");
    }
    void SetFreeResprays(bool value) { state.freeResprays = value; calls.push_back("freeResprays"); }
    void SetMilitaryZonesDisabled(bool value) { state.militaryZonesDisabled = value; calls.push_back("militaryOff"); }
    void DisableRandomTrains(bool value) { state.randomTrainsDisabled = value; calls.push_back("trainsOff"); }
    void SwitchAmbientPlanes(bool bOn) { state.ambientPlanesOn = bOn; calls.push_back("planes"); }
    void DisableCopBikes(bool value) { state.copBikesDisabled = value; calls.push_back("copBikesOff"); }
};

// what a mission sets at its start: little traffic, no random gang members or cops, its own wanted multiplier and
// weather, no resprays (and the doors closed), free resprays, no military zone stars, no random trains, ambient planes
// or cop bikes
SSettings MissionValues()
{
    SSettings settings;
    settings.carDensity = 0.0f;
    settings.pedDensity = 0.2f;
    settings.wantedMultiplier = 0.5f;
    settings.forcedWeather = 4;
    settings.noRandomGangMembers = true;
    settings.noRandomCops = true;
    settings.noResprays = true;
    settings.freeResprays = true;
    settings.militaryZonesDisabled = true;
    settings.randomTrainsDisabled = true;
    settings.ambientPlanesOn = false;
    settings.copBikesDisabled = true;
    return settings;
}

Blob ToBlob(const SSettings& settings)
{
    Blob blob;
    Write(settings, blob);
    return blob;
}

bool Same(const SSettings& a, const SSettings& b) { return Diff(a, b) == 0; }

bool Calls(const CFakeEngine& engine, const std::vector<std::string>& expected) { return engine.calls == expected; }

SGateInput OpenGate()
{
    SGateInput input;
    input.bAuthenticated = true;
    input.bIsHost = false;
    input.bHasHost = true;
    input.bRanSinceRestart = true;
    input.bLocalGameReady = true;
    input.bLocalMissionRunning = false;
    return input;
}
}  // namespace

void Blobs()
{
    // the defaults are a mission end's values
    const SSettings defaults;
    const Blob blob = ToBlob(defaults);
    CHECK(BLOB_SIZE == HEADER_SIZE + 27);
    CHECK(blob.size() == BLOB_SIZE);
    // one value, no records: what the registry checks before it sends or takes a FULL, and it fits one packet
    CHECK(WorldStatePolicy::IsValidFull(blob, 0));
    CHECK(WorldStatePolicy::FitsInOnePacket(blob.size()));
    WorldStatePolicy::SLayout layout;
    CHECK(WorldStatePolicy::ReadLayout(blob, layout));
    CHECK(layout.recordCount == 0 && layout.hash == LAYOUT);

    // little-endian, in SSettings order: the car density 1.0f (0x3F800000), the weather -1, then one byte per flag
    const uint8_t carDensity[] = {0x00, 0x00, 0x80, 0x3F};
    CHECK(std::memcmp(blob.data() + HEADER_SIZE, carDensity, sizeof(carDensity)) == 0);
    CHECK(blob[HEADER_SIZE + 8] == 0xFF && blob[HEADER_SIZE + 11] == 0xFF);  // ped type -1
    CHECK(blob[HEADER_SIZE + 16] == 0xFF && blob[HEADER_SIZE + 17] == 0xFF);  // no forced weather
    for (size_t flag = 0; flag < 9; flag++)
    {
        CHECK(blob[HEADER_SIZE + 18 + flag] == (flag == 7 ? 1 : 0));  // only the ambient planes are on
    }

    // round trips
    SSettings read;
    CHECK(Read(blob, read) && Same(read, defaults));
    const SSettings mission = MissionValues();
    CHECK(Read(ToBlob(mission), read) && Same(read, mission));
    SSettings odd;
    odd.carDensity = -0.0f;
    odd.pedDensity = 3.5f;
    odd.randomPedsType = 7;
    odd.wantedMultiplier = 0.25f;
    odd.forcedWeather = 22;
    odd.onlyRandomGangMembers = true;
    CHECK(Read(ToBlob(odd), read) && Same(read, odd));
    CHECK(read.forcedWeather == 22 && read.randomPedsType == 7 && read.onlyRandomGangMembers);

    // not this layout: another size, another hash, records
    Blob shorter = blob;
    shorter.pop_back();
    CHECK(!Read(shorter, read));
    Blob longer = blob;
    longer.push_back(0);
    CHECK(!Read(longer, read));
    Blob otherHash = blob;
    otherHash[2] ^= 0x01;
    CHECK(!Read(otherHash, read));
    Blob withRecords = blob;
    withRecords[0] = 1;
    CHECK(!Read(withRecords, read));
    CHECK(!Read(Blob{}, read));

    // equal settings, equal blobs; a float compares by its bits (0.0 and -0.0 differ, as their blobs do)
    CHECK(ToBlob(MissionValues()) == ToBlob(MissionValues()));
    SSettings negativeZero = defaults;
    negativeZero.carDensity = -0.0f;
    SSettings zero = defaults;
    zero.carDensity = 0.0f;
    CHECK(Diff(negativeZero, zero) == CAR_DENSITY);
    CHECK(ToBlob(negativeZero) != ToBlob(zero));
}

void Fields()
{
    const SSettings base;
    CHECK(Diff(base, base) == 0);

    // each setting has its own bit
    uint32_t seen = 0;
    auto changed = [&](SSettings settings, uint32_t field) {
        CHECK(Diff(base, settings) == field);
        CHECK(Diff(settings, base) == field);
        CHECK((seen & field) == 0);
        CHECK(ToBlob(settings) != ToBlob(base));
        seen |= field;
    };
    SSettings s;
    s = base; s.carDensity = 0.5f; changed(s, CAR_DENSITY);
    s = base; s.pedDensity = 0.5f; changed(s, PED_DENSITY);
    s = base; s.randomPedsType = 4; changed(s, RANDOM_PEDS_TYPE);
    s = base; s.wantedMultiplier = 2.0f; changed(s, WANTED_MULTIPLIER);
    s = base; s.forcedWeather = 8; changed(s, FORCED_WEATHER);
    s = base; s.noRandomGangMembers = true; changed(s, NO_RANDOM_GANG_MEMBERS);
    s = base; s.onlyRandomGangMembers = true; changed(s, ONLY_RANDOM_GANG_MEMBERS);
    s = base; s.noRandomCops = true; changed(s, NO_RANDOM_COPS);
    s = base; s.noResprays = true; changed(s, NO_RESPRAYS);
    s = base; s.freeResprays = true; changed(s, FREE_RESPRAYS);
    s = base; s.militaryZonesDisabled = true; changed(s, MILITARY_ZONES_DISABLED);
    s = base; s.randomTrainsDisabled = true; changed(s, RANDOM_TRAINS_DISABLED);
    s = base; s.ambientPlanesOn = false; changed(s, AMBIENT_PLANES_ON);
    s = base; s.copBikesDisabled = true; changed(s, COP_BIKES_DISABLED);
    CHECK(seen == (1u << FIELD_COUNT) - 1);
}

void ApplyCalls()
{
    // a mission starts on the host: every setting it changed, each the way its command does it, in blob order
    CFakeEngine game;
    const SSettings mission = MissionValues();
    const uint32_t fields = ApplyDifferences(game.state, mission, game);
    CHECK(fields == Diff(SSettings{}, mission));
    CHECK(Calls(game, {"car", "ped", "wanted", "force-weather", "gangOff", "copsOff", "noResprays", "resprays-closed",
                          "freeResprays", "militaryOff", "trainsOff", "planes", "copBikesOff"}));
    CHECK(Same(game.state, mission));
    CHECK(!game.bRespraysOpen);  // SET_NO_RESPRAYS closes every Pay 'n' Spray
    CHECK(game.state.forcedWeather == 4);

    // nothing differs: no call
    game.calls.clear();
    CHECK(ApplyDifferences(game.state, mission, game) == 0);
    CHECK(game.calls.empty());

    // only what differs is touched
    SSettings densityOnly = mission;
    densityOnly.pedDensity = 1.0f;
    CHECK(ApplyDifferences(game.state, densityOnly, game) == PED_DENSITY);
    CHECK(Calls(game, {"ped"}));

    // the weather: none means release, anything else is forced as it is
    game.calls.clear();
    SSettings released = densityOnly;
    released.forcedWeather = WEATHER_NONE;
    CHECK(ApplyDifferences(game.state, released, game) == FORCED_WEATHER);
    CHECK(Calls(game, {"release-weather"}));
    CHECK(game.state.forcedWeather == WEATHER_NONE);
    game.calls.clear();
    SSettings forced = released;
    forced.forcedWeather = 19;
    CHECK(ApplyDifferences(game.state, forced, game) == FORCED_WEATHER);
    CHECK(Calls(game, {"force-weather"}));
    CHECK(game.state.forcedWeather == 19);

    // resprays allowed again: the doors open with it
    game.calls.clear();
    SSettings resprays = forced;
    resprays.noResprays = false;
    CHECK(ApplyDifferences(game.state, resprays, game) == NO_RESPRAYS);
    CHECK(Calls(game, {"noResprays", "resprays-open"}));
    CHECK(game.bRespraysOpen && !game.state.noResprays);

    // free resprays alone don't touch the doors
    game.calls.clear();
    SSettings notFree = resprays;
    notFree.freeResprays = false;
    CHECK(ApplyDifferences(game.state, notFree, game) == FREE_RESPRAYS);
    CHECK(Calls(game, {"freeResprays"}));
}

void MissionEnd()
{
    // CMissionCleanup::Process: everything back except free resprays, which it leaves alone
    SSettings settings = MissionValues();
    ApplyMissionCleanup(settings);
    SSettings expected;
    expected.freeResprays = true;
    CHECK(Same(settings, expected));

    SSettings notFree = MissionValues();
    notFree.freeResprays = false;
    ApplyMissionCleanup(notFree);
    CHECK(Same(notFree, SSettings{}));

    // the defaults are a new game's: traffic, peds, planes on, nothing forced
    const SSettings defaults;
    CHECK(defaults.carDensity == 1.0f && defaults.pedDensity == 1.0f && defaults.wantedMultiplier == 1.0f);
    CHECK(defaults.randomPedsType == -1 && defaults.forcedWeather == WEATHER_NONE && defaults.ambientPlanesOn);
    CHECK(!defaults.noResprays && !defaults.randomTrainsDisabled && !defaults.copBikesDisabled);
}

void HostLossRule()
{
    // only a lost host (not a mission that ended), only on a game that wasn't the host, only without a mission of its own
    CHECK(ResetsAfterHostLoss(true, false, false));
    CHECK(!ResetsAfterHostLoss(false, false, false));  // the host's mission ended normally: its values come by packet
    CHECK(!ResetsAfterHostLoss(true, true, false));
    CHECK(!ResetsAfterHostLoss(true, false, true));    // this game's own mission resets them at its own end
    CHECK(!ResetsAfterHostLoss(false, true, true));
}

// The expected effect, played through the registry's policy as CWorldStateRegistry.cpp drives it: the host polls its
// settings every frame and sends a change as a FULL; the other game applies the host's blob while its gate is open
// (SuspendDuringOwnMission) and converges.
void FailedHostMission()
{
    const int HOST = 0;
    CWorldStateTable hostTable;
    CWorldStateTable friendTable;
    hostTable.Resize(1);
    friendTable.Resize(1);
    CFakeEngine host;
    CFakeEngine friendGame;
    Blob records;

    auto hostPoll = [&]() -> bool {
        const Blob now = ToBlob(host.state);
        CHECK(WorldStatePolicy::IsValidFull(now, 0));
        const eSend send = hostTable.Entry(0).Poll(now, 0, records);
        if (send == eSend::NONE)
        {
            return false;
        }
        CHECK(send == eSend::FULL);  // one value: never RECORDS
        hostTable.Entry(0).OnSent(now);
        CHECK(friendTable.Entry(0).OnFull(HOST, now.data(), now.size(), 0) == eReceive::ACCEPTED);
        return true;
    };
    auto friendCheck = [&](const SGateInput& gate) -> uint32_t {
        if (WorldStatePolicy::DecideGate(gate, true) != eGate::OPEN)
        {
            return 0;
        }
        const Blob local = ToBlob(friendGame.state);
        if (friendTable.Entry(0).Check(local) != eCheck::APPLY)
        {
            return 0;
        }
        SSettings desired;
        SSettings have;
        CHECK(Read(friendTable.Entry(0).GetDesired(), desired));
        CHECK(Read(local, have));
        friendGame.calls.clear();
        const uint32_t fields = ApplyDifferences(have, desired, friendGame);
        CHECK(ToBlob(friendGame.state) == friendTable.Entry(0).GetDesired());  // converged
        return fields;
    };

    // the friend joins in free play: the host's FULL equals the friend's settings, nothing to do
    hostTable.OnHostChanged(true, HOST);
    friendTable.OnHostChanged(false, HOST);
    hostTable.OnPlayerJoined();
    CHECK(hostPoll());
    CHECK(friendCheck(OpenGate()) == 0);
    CHECK(!hostPoll());

    // the host's mission starts: the friend gets its settings (no traffic, its weather, no Pay 'n' Spray...)
    host.state = MissionValues();
    CHECK(hostPoll());
    CHECK(friendCheck(OpenGate()) == Diff(SSettings{}, MissionValues()));
    CHECK(Same(friendGame.state, MissionValues()));
    CHECK(!friendGame.bRespraysOpen);

    // a replayed command of a registered mission (01EB) and the host's FULL of the same change in one frame: the friend
    // already has the value, nothing is put back
    host.state.carDensity = 0.3f;
    friendGame.state.carDensity = 0.3f;  // COpCodeSync's replay
    CHECK(hostPoll());
    CHECK(friendCheck(OpenGate()) == 0);

    // the mission fails: CMissionCleanup::Process runs only on the host; the friend's traffic and peds come back with
    // the host's values, the doors open, the planes return, the weather is released
    ApplyMissionCleanup(host.state);
    CHECK(hostPoll());
    const uint32_t restored = friendCheck(OpenGate());
    CHECK((restored & (CAR_DENSITY | PED_DENSITY | FORCED_WEATHER | AMBIENT_PLANES_ON | NO_RESPRAYS)) ==
          (CAR_DENSITY | PED_DENSITY | FORCED_WEATHER | AMBIENT_PLANES_ON | NO_RESPRAYS));
    CHECK((restored & FREE_RESPRAYS) == 0);  // the host keeps them too
    CHECK(friendGame.state.carDensity == 1.0f && friendGame.state.pedDensity == 1.0f);
    CHECK(friendGame.state.forcedWeather == WEATHER_NONE && friendGame.state.ambientPlanesOn);
    CHECK(friendGame.bRespraysOpen);
    CHECK(Same(friendGame.state, host.state));

    // the friend's own side mission (taxi) sets its own values: the entry waits while it runs, also when the host's
    // values change meanwhile; its own mission end resets the friend's settings, then the host's return
    SGateInput ownMission = OpenGate();
    ownMission.bLocalMissionRunning = true;
    friendGame.state.carDensity = 0.5f;
    friendGame.state.wantedMultiplier = 2.0f;
    host.state.pedDensity = 0.6f;
    CHECK(hostPoll());
    CHECK(friendCheck(ownMission) == 0);
    CHECK(friendGame.state.carDensity == 0.5f && friendGame.state.pedDensity == 1.0f);
    ApplyMissionCleanup(friendGame.state);  // the friend's CMissionCleanup::Process
    CHECK(friendCheck(OpenGate()) == PED_DENSITY);
    CHECK(Same(friendGame.state, host.state));

    // the friend's new game: its INITIAL/INTRO are missions; the host's values return once they have ended
    friendTable.OnGameRestarted(1, false);
    friendGame.state = SSettings{};
    friendGame.state.forcedWeather = 1;  // INTRO's own forced weather
    SGateInput intro = ownMission;
    intro.bRanSinceRestart = friendTable.RanSinceRestart(2);
    CHECK(friendCheck(intro) == 0);
    ApplyMissionCleanup(friendGame.state);
    SGateInput afterIntro = OpenGate();
    afterIntro.bRanSinceRestart = friendTable.RanSinceRestart(3);
    CHECK(friendCheck(afterIntro) == (PED_DENSITY | FREE_RESPRAYS));
    CHECK(Same(friendGame.state, host.state));
}

// The host leaves during its mission: the friend keeps the mission's settings until this reset puts back what the
// mission end leaves (unless a mission of the friend's own runs, whose end resets them).
void HostLostDuringMission()
{
    CFakeEngine friendGame;
    friendGame.state = MissionValues();
    friendGame.bRespraysOpen = false;

    CHECK(!ResetsAfterHostLoss(true, false, true));  // own mission: left to it

    CHECK(ResetsAfterHostLoss(true, false, false));
    SSettings reset = friendGame.state;
    ApplyMissionCleanup(reset);
    const uint32_t fields = ApplyDifferences(friendGame.state, reset, friendGame);
    CHECK(fields == (Diff(MissionValues(), SSettings{}) & ~FREE_RESPRAYS));
    SSettings expected;
    expected.freeResprays = true;  // as on the host, a mission end leaves them
    CHECK(Same(friendGame.state, expected));
    CHECK(friendGame.bRespraysOpen);
}

void Diagnostics()
{
    char text[256];
    FormatFields(CAR_DENSITY | FORCED_WEATHER, text, sizeof(text));
    CHECK(std::strcmp(text, "car,weather") == 0);
    FormatFields(0, text, sizeof(text));
    CHECK(std::strcmp(text, "-") == 0);
    FormatFields((1u << FIELD_COUNT) - 1, text, sizeof(text));
    CHECK(std::strstr(text, "car,ped,pedType,wanted,weather,gangOff,gangOnly,copsOff,noResprays,freeResprays,") == text);
    CHECK(std::strstr(text, "militaryOff,trainsOff,planesOn,copBikesOff") != nullptr);
    CHECK(std::strlen(text) < 160);  // the entry's buffer
    char small[8];
    FormatFields((1u << FIELD_COUNT) - 1, small, sizeof(small));  // cut, still terminated
    CHECK(std::strlen(small) < sizeof(small));

    Format(SSettings{}, text, sizeof(text));
    CHECK(std::strstr(text, "car=1.00 ped=1.00 pedType=-1 wanted=1.00 weather=-1 gangOff=0") == text);
    CHECK(std::strstr(text, "planesOn=1 copBikesOff=0") != nullptr);
    Format(MissionValues(), text, sizeof(text));
    CHECK(std::strstr(text, "car=0.00 ped=0.20 pedType=-1 wanted=0.50 weather=4 gangOff=1") == text);
    CHECK(std::strlen(text) < 200);
}

int main()
{
    Blobs();
    Fields();
    ApplyCalls();
    MissionEnd();
    HostLossRule();
    FailedHostMission();
    HostLostDuringMission();
    Diagnostics();
    std::printf("world_state_mission_test: all checks passed\n");
    return 0;
}
