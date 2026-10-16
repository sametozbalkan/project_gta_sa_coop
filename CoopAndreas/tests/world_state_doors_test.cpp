// Private build: policy test for the world-state registry's doors and garages (client/src/CWorldStateDoorsPolicy.h,
// used by CWorldStateEntriesDoors.cpp) without GTA: the garage records and blob, Michelle's garage, what applying a
// record does; the entry-exit bits this game keeps (W-D7), upstream's marker key, the host's packet by key (the last
// marker of a key counts, as in CEntryExitMarkerSync::Receive), the host's resend check (streamed markers don't count)
// and the re-apply on the other games. Both entries are played through the registry's policy (CWorldStatePolicy.h):
// the friend's mod shops after #10 and the shops after the friend restarts its game. Build and run: see CLAUDE.md
// (Fork changes, world-state registry).
#include "../client/src/CWorldStateDoorsPolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace WorldStatePolicy;
using namespace WorldStateDoorsPolicy;

namespace
{
// script GarageType values (Sanny Builder's enums.txt)
constexpr uint8_t TYPE_RESPRAY = 5;
constexpr uint8_t TYPE_SCRIPT_OPEN_CLOSE = 19;
constexpr uint8_t TYPE_HIDEOUT_ONE = 16;
constexpr uint8_t TYPE_LOCO_LOW_CO = 36;
constexpr uint8_t TYPE_WHEEL_ARCH_ANGELS = 37;
constexpr uint8_t TYPE_TRANSFENDER = 38;

// garage flags besides the inactive bit: door opens up 0x08, door goes in 0x10, camera follows 0x20, door closed 0x40,
// resprays always free 0x80
constexpr uint8_t FLAG_DOOR_BITS = 0x08 | 0x10 | 0x40;
constexpr uint8_t FLAG_FREE_RESPRAYS = 0x80;

// entry-exit flags (the scripts' EntryexitsFlag enum)
constexpr uint16_t ENEX_ENABLED = 0x4000;
constexpr uint16_t ENEX_NO_WARP = 0x0200;
constexpr uint16_t ENEX_WARP_GROUP = 0x0100;
constexpr uint16_t ENEX_BURGLARY = 0x1000;
constexpr uint16_t ENEX_CAR_WARP = 0x0020;

// a game's garages: names in 8-byte fields like CGarage::m_anName
struct SGarages
{
    char names[8][GARAGE_NAME_SIZE]{};
    uint8_t types[8]{};
    uint8_t flags[8]{};
    size_t count = 0;

    void Add(const char* name, uint8_t type, uint8_t flag)
    {
        std::memset(names[count], 0, GARAGE_NAME_SIZE);
        std::memcpy(names[count], name, std::strlen(name) < GARAGE_NAME_SIZE ? std::strlen(name) : GARAGE_NAME_SIZE);
        types[count] = type;
        flags[count] = flag;
        count++;
    }

    Blob Capture() const
    {
        SGarageState states[8];
        for (size_t i = 0; i < count; i++)
        {
            states[i].name = names[i];
            states[i].type = types[i];
            states[i].flags = flags[i];
        }
        Blob blob;
        BuildGaragesBlob(blob, states, count);
        return blob;
    }

    // what CWorldStateGarages::Apply does, on this model (ChangeGarageType writes the type, (De)ActivateGarage the bit)
    void Apply(const Blob& desired, const Blob& local)
    {
        const size_t n = GetU16(desired.data());
        for (size_t i = 0; i < n; i++)
        {
            const SGarageAction action = PlanGarage(GetGarageRecord(desired, i), GetGarageRecord(local, i));
            if (action.bChangeType)
            {
                types[i] = action.type;
            }
            if (action.bSetActive)
            {
                flags[i] = action.bActive ? static_cast<uint8_t>(flags[i] & ~GARAGE_FLAG_INACTIVE)
                                          : static_cast<uint8_t>(flags[i] | GARAGE_FLAG_INACTIVE);
            }
        }
    }

    bool IsInactive(size_t i) const { return (flags[i] & GARAGE_FLAG_INACTIVE) != 0; }
};

// the five mod shops as INITIAL sets them, then the hideout and Michelle's garage
SGarages NewGameGarages(bool bIntroDone)
{
    SGarages garages;
    const uint8_t modShop = bIntroDone ? GARAGE_FLAG_INACTIVE : 0;  // INTRO deactivates them
    garages.Add("BODLAWN", TYPE_TRANSFENDER, modShop | FLAG_DOOR_BITS);
    garages.Add("MODLAST", TYPE_LOCO_LOW_CO, modShop);
    garages.Add("MDSSFSE", TYPE_TRANSFENDER, modShop);
    garages.Add("MDS1SFS", TYPE_WHEEL_ARCH_ANGELS, modShop);
    garages.Add("VECMOD", TYPE_TRANSFENDER, modShop);
    garages.Add("CESAFE1", TYPE_HIDEOUT_ONE, GARAGE_FLAG_INACTIVE);  // INITIAL deactivates the hideouts
    garages.Add("MICHDR", TYPE_SCRIPT_OPEN_CLOSE, 0);
    garages.Add("HBGDSFS", TYPE_SCRIPT_OPEN_CLOSE, 0);
    return garages;
}

// a marker as the policy sees it
SEnExRecord Marker(int16_t left, int16_t bottom, uint8_t area, uint16_t flags)
{
    SEnExRecord record;
    record.key = MakeEnExKey(left, bottom, area);
    record.flags = flags;
    return record;
}

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

void GarageRecords()
{
    // the type and only the inactive bit: free resprays (Michelle's perk), door and camera bits are not mirrored
    CHECK(MakeGarageRecord(TYPE_LOCO_LOW_CO, GARAGE_FLAG_INACTIVE) == (SGarageRecord{TYPE_LOCO_LOW_CO, 1}));
    CHECK(MakeGarageRecord(TYPE_LOCO_LOW_CO, FLAG_FREE_RESPRAYS | FLAG_DOOR_BITS) ==
          (SGarageRecord{TYPE_LOCO_LOW_CO, 0}));
    CHECK(MakeGarageRecord(TYPE_RESPRAY, 0xFF) == (SGarageRecord{TYPE_RESPRAY, 1}));
    CHECK(MakeGarageRecord(0, 0) == SGarageRecord{});

    // round trip through a blob
    SGarages garages;
    garages.Add("MODLAST", TYPE_LOCO_LOW_CO, GARAGE_FLAG_INACTIVE | FLAG_FREE_RESPRAYS);
    garages.Add("BODLAWN", TYPE_TRANSFENDER, FLAG_DOOR_BITS);
    const Blob blob = garages.Capture();
    CHECK(IsValidFull(blob, GARAGE_RECORD_SIZE));
    CHECK(blob.size() == HEADER_SIZE + 2 * GARAGE_RECORD_SIZE);
    CHECK(GetU16(blob.data()) == 2);
    CHECK(GetGarageRecord(blob, 0) == (SGarageRecord{TYPE_LOCO_LOW_CO, 1}));
    CHECK(GetGarageRecord(blob, 1) == (SGarageRecord{TYPE_TRANSFENDER, 0}));
}

void GarageLayout()
{
    const char a[GARAGE_NAME_SIZE] = {'M', 'O', 'D', 'L', 'A', 'S', 'T', '\0'};
    const char b[GARAGE_NAME_SIZE] = {'B', 'O', 'D', 'L', 'A', 'W', 'N', '\0'};
    const char full[GARAGE_NAME_SIZE] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};  // all 8 bytes used, no terminator
    char shortName[GARAGE_NAME_SIZE] = {'V', 'E', 'C', 'M', 'O', 'D', '\0', 'X'};    // bytes after the terminator
    char shortClean[GARAGE_NAME_SIZE] = {'V', 'E', 'C', 'M', 'O', 'D', '\0', '\0'};

    // the names in index order: another order is another layout
    const uint32_t ab = AddGarageName(AddGarageName(GARAGES_LAYOUT, a, GARAGE_NAME_SIZE), b, GARAGE_NAME_SIZE);
    const uint32_t ba = AddGarageName(AddGarageName(GARAGES_LAYOUT, b, GARAGE_NAME_SIZE), a, GARAGE_NAME_SIZE);
    CHECK(ab != ba);
    // only the bytes up to the terminator count; a name using all 8 bytes stops at 8
    CHECK(AddGarageName(GARAGES_LAYOUT, shortName, GARAGE_NAME_SIZE) ==
          AddGarageName(GARAGES_LAYOUT, shortClean, GARAGE_NAME_SIZE));
    CHECK(AddGarageName(GARAGES_LAYOUT, full, GARAGE_NAME_SIZE) != AddGarageName(GARAGES_LAYOUT, a, GARAGE_NAME_SIZE));
    // the separator keeps "AB" + "C" apart from "A" + "BC"
    CHECK(AddGarageName(AddGarageName(GARAGES_LAYOUT, "AB", 8), "C", 8) !=
          AddGarageName(AddGarageName(GARAGES_LAYOUT, "A", 8), "BC", 8));

    // both games load the same garages: the same layout; a game with other garages or another order has another one
    const Blob host = NewGameGarages(true).Capture();
    const Blob friendGame = NewGameGarages(true).Capture();
    SLayout hostLayout;
    SLayout friendLayout;
    CHECK(ReadLayout(host, hostLayout) && ReadLayout(friendGame, friendLayout));
    CHECK(hostLayout == friendLayout);
    CHECK(hostLayout.recordCount == 8);
    SGarages other = NewGameGarages(true);
    std::memcpy(other.names[0], "BODLAWX", 7);
    SLayout otherLayout;
    CHECK(ReadLayout(other.Capture(), otherLayout));
    CHECK(otherLayout != hostLayout);
}

void Names()
{
    CHECK(NameEquals("MODLAST", GARAGE_NAME_SIZE, "MODLAST"));
    CHECK(NameEquals("modlast", GARAGE_NAME_SIZE, "MODLAST"));
    CHECK(NameEquals("MODLAST", GARAGE_NAME_SIZE, "modlast"));
    CHECK(!NameEquals("MODLAS", GARAGE_NAME_SIZE, "MODLAST"));
    CHECK(!NameEquals("MODLASTX", GARAGE_NAME_SIZE, "MODLAST"));
    CHECK(!NameEquals("", GARAGE_NAME_SIZE, "MODLAST"));
    CHECK(NameEquals("", GARAGE_NAME_SIZE, ""));
    // a field without a terminator ends at its size
    const char field[4] = {'G', 'Y', 'M', '1'};
    CHECK(NameEquals(field, sizeof(field), "GYM1"));
    CHECK(!NameEquals(field, sizeof(field), "GYM12"));
    CHECK(!NameEquals(field, sizeof(field), "GYM"));

    // the scripts during which the entry-exit re-apply waits: the girlfriend date and the mod garage (script_name
    // 'GFDATE' and 'CARMOD'; CRunningScript::m_szName is 8 bytes)
    char scriptName[8] = {'G', 'F', 'D', 'A', 'T', 'E', '\0', '\0'};
    CHECK(IsOwnEnExScript(scriptName, sizeof(scriptName)));
    CHECK(IsOwnEnExScript("gfdate", 8));
    CHECK(IsOwnEnExScript("CARMOD", 8));
    CHECK(IsOwnEnExScript("carmod", 8));
    CHECK(!IsOwnEnExScript("GFAGNT", 8));  // the girlfriend agent itself switches restaurants only by console command
    CHECK(!IsOwnEnExScript("GYM", 8));
    CHECK(!IsOwnEnExScript("coopand", 8));
    CHECK(!IsOwnEnExScript("CARMOD1", 8));
}

void MichelleGarage()
{
    CHECK(IsPerPlayerGarage("MICHDR", GARAGE_NAME_SIZE));
    CHECK(IsPerPlayerGarage("michdr", GARAGE_NAME_SIZE));  // GetGarageNumberByName compares with _stricmp
    const char terminated[GARAGE_NAME_SIZE] = {'M', 'I', 'C', 'H', 'D', 'R', '\0', 'Z'};
    CHECK(IsPerPlayerGarage(terminated, GARAGE_NAME_SIZE));
    CHECK(IsPerPlayerGarage("MICHDR", 6));  // all of the field used
    CHECK(!IsPerPlayerGarage("MICHDRX", GARAGE_NAME_SIZE));
    CHECK(!IsPerPlayerGarage("MICHD", GARAGE_NAME_SIZE));
    CHECK(!IsPerPlayerGarage("MODLAST", GARAGE_NAME_SIZE));
    CHECK(!IsPerPlayerGarage("", GARAGE_NAME_SIZE));
    const char full[GARAGE_NAME_SIZE] = {'M', 'I', 'C', 'H', 'D', 'R', 'A', 'B'};
    CHECK(!IsPerPlayerGarage(full, GARAGE_NAME_SIZE));

    // its record is {0, 0} on every game: the host's girlfriend perk doesn't reach the friend, and the friend's own one
    // (GFAGNT: Respray with free resprays) is never put back to the host's type
    SGarages host = NewGameGarages(true);
    SGarages friendGame = NewGameGarages(true);
    host.types[6] = TYPE_RESPRAY;
    host.flags[6] = FLAG_FREE_RESPRAYS;
    CHECK(GetGarageRecord(host.Capture(), 6) == SGarageRecord{});
    CHECK(host.Capture() == friendGame.Capture());
    friendGame.types[6] = TYPE_RESPRAY;
    friendGame.flags[6] = FLAG_FREE_RESPRAYS | GARAGE_FLAG_INACTIVE;
    host.types[6] = TYPE_SCRIPT_OPEN_CLOSE;
    host.flags[6] = 0;
    CHECK(host.Capture() == friendGame.Capture());
}

void GaragePlan()
{
    const SGarageRecord active{TYPE_LOCO_LOW_CO, 0};
    const SGarageRecord inactive{TYPE_LOCO_LOW_CO, 1};

    SGarageAction action = PlanGarage(active, active);
    CHECK(!action.bChangeType && !action.bSetActive && !action.bUnknownType);

    // only the activity differs: ActivateGarage / DeActivateGarage, no type change (that would close the door)
    action = PlanGarage(active, inactive);
    CHECK(!action.bChangeType && action.bSetActive && action.bActive);
    action = PlanGarage(inactive, active);
    CHECK(!action.bChangeType && action.bSetActive && !action.bActive);

    // only the type differs
    action = PlanGarage(SGarageRecord{TYPE_SCRIPT_OPEN_CLOSE, 0}, SGarageRecord{TYPE_RESPRAY, 0});
    CHECK(action.bChangeType && action.type == TYPE_SCRIPT_OPEN_CLOSE && !action.bSetActive);

    // both: the type first, then the activity
    action = PlanGarage(SGarageRecord{TYPE_TRANSFENDER, 0}, SGarageRecord{TYPE_HIDEOUT_ONE, 1});
    CHECK(action.bChangeType && action.type == TYPE_TRANSFENDER && action.bSetActive && action.bActive);

    // types 0 (no garage) to 45 are 1.0's; another one is left as it is, the activity still follows
    action = PlanGarage(SGarageRecord{0, 0}, SGarageRecord{TYPE_RESPRAY, 0});
    CHECK(action.bChangeType && action.type == 0);
    action = PlanGarage(SGarageRecord{GARAGE_TYPE_MAX, 0}, SGarageRecord{TYPE_RESPRAY, 0});
    CHECK(action.bChangeType && action.type == GARAGE_TYPE_MAX);
    action = PlanGarage(SGarageRecord{GARAGE_TYPE_MAX + 1, 1}, SGarageRecord{TYPE_RESPRAY, 0});
    CHECK(!action.bChangeType && action.bUnknownType && action.bSetActive && !action.bActive);
}

// #10 (Cesar Vialpando) passed on the host: SWEET6 activated Loco Low Co (MODLAST) and the Los Santos Transfender
// (BODLAWN). The friend's own INTRO deactivated all mod shops. Played through the registry's policy as entry 4.
void ModShopsAfterTen()
{
    const int HOST = 0;
    CWorldStateTable host;
    CWorldStateTable friendTable;
    host.Resize(1);
    friendTable.Resize(1);

    SGarages hostGarages = NewGameGarages(true);
    hostGarages.flags[0] &= static_cast<uint8_t>(~GARAGE_FLAG_INACTIVE);  // BODLAWN
    hostGarages.flags[1] &= static_cast<uint8_t>(~GARAGE_FLAG_INACTIVE);  // MODLAST
    SGarages friendGarages = NewGameGarages(true);
    Blob records;

    auto hostPoll = [&]() -> eSend {
        const Blob now = hostGarages.Capture();
        const eSend send = host.Entry(0).Poll(now, GARAGE_RECORD_SIZE, records);
        if (send == eSend::FULL)
        {
            CHECK(friendTable.Entry(0).OnFull(HOST, now.data(), now.size(), GARAGE_RECORD_SIZE) == eReceive::ACCEPTED);
        }
        else if (send == eSend::RECORDS)
        {
            CHECK(friendTable.Entry(0).OnRecords(HOST, records.data(), records.size(), GARAGE_RECORD_SIZE) ==
                  eReceive::ACCEPTED);
        }
        if (send != eSend::NONE)
        {
            host.Entry(0).OnSent(now);
        }
        return send;
    };
    auto friendReapply = [&](const SGateInput& gate) -> bool {
        if (DecideGate(gate, true) != eGate::OPEN)
        {
            return false;
        }
        const Blob local = friendGarages.Capture();
        if (friendTable.Entry(0).Check(local) != eCheck::APPLY)
        {
            return false;
        }
        friendGarages.Apply(friendTable.Entry(0).GetDesired(), local);
        CHECK(friendTable.Entry(0).Check(friendGarages.Capture()) == eCheck::NOTHING);  // converged
        return true;
    };

    // the friend joins: the host's garages in full
    friendTable.OnHostChanged(false, HOST);
    host.OnPlayerJoined();
    CHECK(hostPoll() == eSend::FULL);
    CHECK(hostPoll() == eSend::NONE);

    // still in the friend's own INTRO: waits
    SGateInput intro = OpenGate();
    intro.bLocalMissionRunning = true;
    CHECK(!friendReapply(intro));
    CHECK(friendGarages.IsInactive(0) && friendGarages.IsInactive(1));

    // after it: Loco Low Co and the Transfender open as on the host, the other mod shops stay closed, doors untouched
    CHECK(friendReapply(OpenGate()));
    CHECK(!friendGarages.IsInactive(0) && !friendGarages.IsInactive(1));
    CHECK(friendGarages.IsInactive(2) && friendGarages.IsInactive(3) && friendGarages.IsInactive(4));
    CHECK((friendGarages.flags[0] & FLAG_DOOR_BITS) == FLAG_DOOR_BITS);
    CHECK(!friendReapply(OpenGate()));

    // the host's driving school turns the mod shops off: few records changed, RECORDS patch the friend's copy
    hostGarages.flags[0] |= GARAGE_FLAG_INACTIVE;
    hostGarages.flags[1] |= GARAGE_FLAG_INACTIVE;
    CHECK(hostPoll() == eSend::RECORDS);
    CHECK(friendReapply(OpenGate()));
    CHECK(friendGarages.IsInactive(0) && friendGarages.IsInactive(1));
    // and on again
    hostGarages.flags[0] &= static_cast<uint8_t>(~GARAGE_FLAG_INACTIVE);
    hostGarages.flags[1] &= static_cast<uint8_t>(~GARAGE_FLAG_INACTIVE);
    CHECK(hostPoll() == eSend::RECORDS);
    CHECK(friendReapply(OpenGate()));
    CHECK(!friendGarages.IsInactive(0) && !friendGarages.IsInactive(1));

    // the friend restarts its game: INITIAL and INTRO close the mod shops again; the host's blob is kept and put back
    // once the friend's game runs and its INTRO is over
    friendTable.OnGameRestarted(1, false);
    friendGarages = NewGameGarages(true);
    SGateInput restarted = OpenGate();
    restarted.bRanSinceRestart = friendTable.RanSinceRestart(1);
    CHECK(!friendReapply(restarted));
    CHECK(!friendReapply(intro));
    CHECK(friendReapply(OpenGate()));
    CHECK(!friendGarages.IsInactive(0) && !friendGarages.IsInactive(1));

    // the friend's own burglary mission changes a type: not while it runs, the host's type afterwards
    friendGarages.types[7] = 43;  // Burglary
    SGateInput burglary = OpenGate();
    burglary.bLocalMissionRunning = true;
    CHECK(!friendReapply(burglary));
    CHECK(friendReapply(OpenGate()));
    CHECK(friendGarages.types[7] == TYPE_SCRIPT_OPEN_CLOSE);

    // a game with other garages turns the entry off instead of writing the wrong ones
    SGarages wrong = NewGameGarages(true);
    std::memcpy(wrong.names[3], "XXXXXXX", 7);
    CHECK(friendTable.Entry(0).Check(wrong.Capture()) == eCheck::LAYOUT_MISMATCH);
    CHECK(friendTable.Entry(0).IsDisabled());
}

void EnExMask()
{
    // the bits this game keeps: Oldstyle, Shop, Shopexit, InUse, Temporary
    CHECK(ENEX_LOCAL_FLAGS == 0xA01A);

    // the transition bits never make a difference
    CHECK(!EnExFlagsDiffer(ENEX_ENABLED | ENEX_FLAG_IN_USE, ENEX_ENABLED));
    CHECK(!EnExFlagsDiffer(ENEX_FLAG_OLDSTYLE, 0));
    CHECK(!EnExFlagsDiffer(ENEX_FLAG_SHOP, 0));
    CHECK(!EnExFlagsDiffer(ENEX_FLAG_SHOPEXIT, 0));
    CHECK(!EnExFlagsDiffer(ENEX_FLAG_TEMPORARY, 0));
    CHECK(!EnExFlagsDiffer(ENEX_LOCAL_FLAGS, 0));
    // what scripts switch follows the host
    CHECK(EnExFlagsDiffer(ENEX_ENABLED, 0));
    CHECK(EnExFlagsDiffer(0, ENEX_ENABLED));
    CHECK(EnExFlagsDiffer(ENEX_NO_WARP, 0));
    CHECK(EnExFlagsDiffer(ENEX_WARP_GROUP, 0));
    CHECK(EnExFlagsDiffer(ENEX_BURGLARY, 0));
    CHECK(EnExFlagsDiffer(ENEX_CAR_WARP, 0));

    // the host's flags with this game's transition bits
    CHECK(MergeEnExFlags(ENEX_ENABLED | ENEX_NO_WARP, ENEX_FLAG_IN_USE | ENEX_FLAG_OLDSTYLE | ENEX_FLAG_SHOPEXIT) ==
          (ENEX_ENABLED | ENEX_NO_WARP | ENEX_FLAG_IN_USE | ENEX_FLAG_OLDSTYLE | ENEX_FLAG_SHOPEXIT));
    // the host's own transition bits stay on the host
    CHECK(MergeEnExFlags(ENEX_ENABLED | ENEX_FLAG_IN_USE | ENEX_FLAG_TEMPORARY | ENEX_FLAG_SHOPEXIT, 0) ==
          ENEX_ENABLED);
    // a shop door closed by the host's INTRO
    CHECK(MergeEnExFlags(0, ENEX_ENABLED | ENEX_FLAG_SHOP) == ENEX_FLAG_SHOP);
    // after a merge nothing differs
    for (uint32_t host = 0; host <= 0xFFFF; host += 0x0123)
    {
        for (uint32_t local = 0; local <= 0xFFFF; local += 0x0321)
        {
            const uint16_t merged = MergeEnExFlags(static_cast<uint16_t>(host), static_cast<uint16_t>(local));
            CHECK(!EnExFlagsDiffer(static_cast<uint16_t>(host), merged));
            CHECK(static_cast<uint32_t>(merged & ENEX_LOCAL_FLAGS) == (local & ENEX_LOCAL_FLAGS));
        }
    }
}

void EnExKeyAndIndex()
{
    // upstream's identity: floor(left), floor(bottom), area; negative coordinates don't spill into the other fields
    CHECK(MakeEnExKey(-1, -1, 0) != MakeEnExKey(-1, -1, 1));
    CHECK(MakeEnExKey(100, -200, 3) != MakeEnExKey(-200, 100, 3));
    CHECK(MakeEnExKey(-1, 0, 0) != MakeEnExKey(0, -1, 0));
    CHECK(MakeEnExKey(0, 0, 255) != MakeEnExKey(0, 1, 0));
    CHECK(MakeEnExKey(2244, -1660, 0) == MakeEnExKey(2244, -1660, 0));

    // the last marker of a key counts, as Receive applies the packet in order
    const SEnExRecord packet[] = {
        Marker(10, 20, 0, ENEX_ENABLED),
        Marker(-30, 40, 5, 0),
        Marker(10, 20, 0, 0),
        Marker(10, 20, 1, ENEX_NO_WARP),
    };
    CEnExIndex index;
    index.Build(packet, 4);
    CHECK(index.Size() == 3);
    uint16_t flags = 0xFFFF;
    CHECK(index.Find(MakeEnExKey(10, 20, 0), flags) && flags == 0);
    CHECK(index.Find(MakeEnExKey(10, 20, 1), flags) && flags == ENEX_NO_WARP);
    CHECK(index.Find(MakeEnExKey(-30, 40, 5), flags) && flags == 0);
    CHECK(!index.Find(MakeEnExKey(-30, 40, 0), flags));

    CEnExIndex empty;
    empty.Build(packet, 0);
    CHECK(empty.Size() == 0 && !empty.Find(MakeEnExKey(10, 20, 0), flags));
    index.Clear();
    CHECK(index.Size() == 0);
}

void HostResend()
{
    // what went out last: two shops (one open), a 2dfx burglary house, markers not disabled
    const SEnExRecord sentRecords[] = {
        Marker(100, 100, 0, ENEX_ENABLED),
        Marker(200, 200, 0, 0),
        Marker(300, 300, 0, ENEX_ENABLED),
    };
    CEnExIndex sent;
    sent.Build(sentRecords, 3);
    const SEnExGlobals sentGlobals{};

    // unchanged, or only the host's own transition bits: nothing to send
    std::vector<SEnExRecord> now(sentRecords, sentRecords + 3);
    CHECK(CountHostChanges(sent, sentGlobals, now.data(), now.size(), sentGlobals) == 0);
    now[0].flags |= ENEX_FLAG_IN_USE | ENEX_FLAG_SHOPEXIT;
    now[2].flags |= ENEX_FLAG_OLDSTYLE | ENEX_FLAG_TEMPORARY;
    CHECK(CountHostChanges(sent, sentGlobals, now.data(), now.size(), sentGlobals) == 0);

    // markers that streamed in or out since don't count by themselves
    now.push_back(Marker(400, 400, 0, 0));
    CHECK(CountHostChanges(sent, sentGlobals, now.data(), now.size(), sentGlobals) == 0);
    now.erase(now.begin() + 2);
    CHECK(CountHostChanges(sent, sentGlobals, now.data(), now.size(), sentGlobals) == 0);

    // a load opened the second shop and closed the first: sent again
    now[0].flags &= static_cast<uint16_t>(~ENEX_ENABLED);
    now[1].flags |= ENEX_ENABLED;
    CHECK(CountHostChanges(sent, sentGlobals, now.data(), now.size(), sentGlobals) == 2);

    // the switches count too
    std::vector<SEnExRecord> same(sentRecords, sentRecords + 3);
    SEnExGlobals disabled;
    disabled.bDisabled = true;
    CHECK(CountHostChanges(sent, sentGlobals, same.data(), same.size(), disabled) == 1);
    SEnExGlobals both;
    both.bDisabled = true;
    both.bBurglary = true;
    CHECK(CountHostChanges(sent, sentGlobals, same.data(), same.size(), both) == 2);
}

void OthersReapply()
{
    // the host's packet: shop A open, shop B open, burglary house C with NoWarp, markers enabled
    const SEnExRecord packet[] = {
        Marker(100, 100, 0, ENEX_ENABLED),
        Marker(200, 200, 0, ENEX_ENABLED | ENEX_FLAG_SHOP),
        Marker(300, 300, 0, ENEX_NO_WARP | ENEX_FLAG_IN_USE),
    };
    CEnExIndex host;
    host.Build(packet, 3);
    SEnExGlobals hostGlobals;
    hostGlobals.bBurglary = true;

    // the friend: A as the host, B closed by its own INTRO (InUse set: the friend is passing through it), D only here
    std::vector<SEnExRecord> local = {
        Marker(100, 100, 0, ENEX_ENABLED),
        Marker(200, 200, 0, ENEX_FLAG_SHOP | ENEX_FLAG_IN_USE),
        Marker(500, 500, 0, 0),
        Marker(300, 300, 0, ENEX_FLAG_SHOPEXIT),
    };
    SEnExGlobals localGlobals;
    CHECK(CountDifferences(host, hostGlobals, local.data(), local.size(), localGlobals) == 3);

    uint16_t merged = 0;
    CHECK(!NeedsHostFlags(host, local[0], merged));
    CHECK(NeedsHostFlags(host, local[1], merged) && merged == (ENEX_ENABLED | ENEX_FLAG_SHOP | ENEX_FLAG_IN_USE));
    local[1].flags = merged;
    CHECK(!NeedsHostFlags(host, local[2], merged));  // the host's packet doesn't have it: left alone
    CHECK(NeedsHostFlags(host, local[3], merged) && merged == (ENEX_NO_WARP | ENEX_FLAG_SHOPEXIT));
    local[3].flags = merged;
    localGlobals = hostGlobals;
    CHECK(CountDifferences(host, hostGlobals, local.data(), local.size(), localGlobals) == 0);

    // a marker the friend streams in later is compared once it is there
    local.push_back(Marker(-30, 40, 0, ENEX_ENABLED));
    CHECK(CountDifferences(host, hostGlobals, local.data(), local.size(), localGlobals) == 0);
    const SEnExRecord packet2[] = {packet[0], packet[1], packet[2], Marker(-30, 40, 0, 0)};
    host.Build(packet2, 4);
    CHECK(CountDifferences(host, hostGlobals, local.data(), local.size(), localGlobals) == 1);

    // the adapter's blobs: the number of differences (at most 255) against the desired "none"
    Blob blob;
    MakeEnExAdapterBlob(blob, 3);
    CHECK(blob.size() == HEADER_SIZE + 1 && blob[HEADER_SIZE] == 3);
    SLayout layout;
    CHECK(ReadLayout(blob, layout) && layout.recordCount == 0 && layout.hash == ENEX_ADAPTER_LAYOUT);
    CHECK(IsValidFull(blob, 0));
    MakeEnExAdapterBlob(blob, 300);
    CHECK(blob[HEADER_SIZE] == 255);
    Blob none;
    MakeEnExAdapterBlob(none, 0);
    Blob alsoNone;
    MakeEnExAdapterBlob(alsoNone, 0);
    CHECK(none == alsoNone && none != blob);
}

// The shops after the friend restarts its game, played through the registry's policy as the adapter: its desired blob
// arrives with each ENEX_SYNC from the host, the check runs while the gate is open.
void ShopsAfterFriendRestart()
{
    const int HOST = 0;
    CWorldStateTable friendTable;
    friendTable.Resize(1);
    friendTable.OnHostChanged(false, HOST);

    // the host's world: Binco (A) and the barber (B) open, as SWEET1B and INTRO2 left them
    const SEnExRecord hostPacket[] = {
        Marker(2244, -1665, 0, ENEX_ENABLED | ENEX_FLAG_SHOP),
        Marker(2070, -1795, 0, ENEX_ENABLED | ENEX_FLAG_SHOP),
    };
    CEnExIndex hostIndex;
    hostIndex.Build(hostPacket, 2);
    const SEnExGlobals hostGlobals{};
    std::vector<SEnExRecord> friendMarkers(hostPacket, hostPacket + 2);
    SEnExGlobals friendGlobals{};

    Blob desired;
    MakeEnExAdapterBlob(desired, 0);
    auto onEnExSync = [&]() {
        // Receive applied the packet (masked), then OnReceived delivers the adapter's desired blob from the host
        for (SEnExRecord& marker : friendMarkers)
        {
            uint16_t merged = 0;
            if (NeedsHostFlags(hostIndex, marker, merged))
            {
                marker.flags = merged;
            }
        }
        CHECK(friendTable.Entry(0).OnFull(HOST, desired.data(), desired.size(), 0) == eReceive::ACCEPTED);
    };
    auto friendCheck = [&](const SGateInput& gate) -> bool {
        if (DecideGate(gate, true) != eGate::OPEN)
        {
            return false;
        }
        Blob local;
        MakeEnExAdapterBlob(local,
            CountDifferences(hostIndex, hostGlobals, friendMarkers.data(), friendMarkers.size(), friendGlobals));
        if (friendTable.Entry(0).Check(local) != eCheck::APPLY)
        {
            return false;
        }
        for (SEnExRecord& marker : friendMarkers)
        {
            uint16_t merged = 0;
            if (NeedsHostFlags(hostIndex, marker, merged))
            {
                marker.flags = merged;
            }
        }
        friendGlobals = hostGlobals;
        MakeEnExAdapterBlob(local,
            CountDifferences(hostIndex, hostGlobals, friendMarkers.data(), friendMarkers.size(), friendGlobals));
        CHECK(friendTable.Entry(0).Check(local) == eCheck::NOTHING);  // converged
        return true;
    };

    // no ENEX_SYNC yet: nothing to check
    CHECK(!friendTable.Entry(0).HasDesired());
    onEnExSync();
    CHECK(!friendCheck(OpenGate()));

    // the friend restarts: INTRO closes the shops; the desired blob stays, the gate waits for the next frame and INTRO
    friendTable.OnGameRestarted(1, false);
    for (SEnExRecord& marker : friendMarkers)
    {
        marker.flags &= static_cast<uint16_t>(~ENEX_ENABLED);
    }
    SGateInput restarted = OpenGate();
    restarted.bRanSinceRestart = friendTable.RanSinceRestart(1);
    CHECK(!friendCheck(restarted));
    SGateInput intro = OpenGate();
    intro.bLocalMissionRunning = true;
    CHECK(!friendCheck(intro));
    CHECK((friendMarkers[0].flags & ENEX_ENABLED) == 0);
    CHECK(friendCheck(OpenGate()));
    CHECK((friendMarkers[0].flags & ENEX_ENABLED) != 0 && (friendMarkers[1].flags & ENEX_ENABLED) != 0);
    CHECK(!friendCheck(OpenGate()));

    // the friend walks into Binco: its InUse bit is its own and is never fought over
    friendMarkers[0].flags |= ENEX_FLAG_IN_USE;
    CHECK(!friendCheck(OpenGate()));

    // the host changes to another player: the old host's desired blob goes, nothing is enforced until the new host's
    // ENEX_SYNC
    friendTable.OnHostChanged(false, 3);
    CHECK(!friendTable.Entry(0).HasDesired());
    friendMarkers[1].flags &= static_cast<uint16_t>(~ENEX_ENABLED);
    CHECK(!friendCheck(OpenGate()));

    // the session ends: the world stays as it is
    friendTable.OnDisconnected();
    CHECK(!friendTable.Entry(0).HasDesired());
    CHECK((friendMarkers[1].flags & ENEX_ENABLED) == 0);
}

int main()
{
    GarageRecords();
    GarageLayout();
    Names();
    MichelleGarage();
    GaragePlan();
    ModShopsAfterTen();
    EnExMask();
    EnExKeyAndIndex();
    HostResend();
    OthersReapply();
    ShopsAfterFriendRestart();
    std::printf("world_state_doors_test: all checks passed\n");
    return 0;
}
