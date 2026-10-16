// Private build: policy test for another player leaving during the host's mission and for story mission starts, without
// GTA or ENet: the host's stand-in rules (client/src/CDepartedPlayerPolicy.h, used by CDepartedPlayerPeds), who may start
// a story mission (client/src/CStoryStartPolicy.h, used by Hooks/PlayerHooks.cpp) and who takes over the peds a leaving
// player synced (server/src/CPedHandoverPolicy.h, used by CNetworkPedManager). Build and run: see CLAUDE.md (Fork
// changes, another player leaving).
#include "../client/src/CDepartedPlayerPolicy.h"
#include "../client/src/CStoryStartPolicy.h"
#include "../server/src/CPedHandoverPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace DepartedPlayerPolicy;

static void KeepAndRelease()
{
    // only the host, only during a mission of its own, only a ped with a clump and intelligence
    CHECK(ShouldKeepStandIn(true, true, true));
    CHECK(!ShouldKeepStandIn(false, true, true));   // a game that isn't the host runs no story mission
    CHECK(!ShouldKeepStandIn(true, false, true));   // free roam: nothing holds the handle
    CHECK(!ShouldKeepStandIn(true, true, false));   // a clumpless ped is deleted as before
    CHECK(!ShouldKeepStandIn(false, false, false));

    CHECK(!ShouldReleaseStandIns(true));
    CHECK(ShouldReleaseStandIns(false));
}

static SStandInState Parked()
{
    SStandInState state;
    state.bInVehicle = false;
    state.bInMovingList = false;
    state.bVisible = false;
    state.bUsesCollision = false;
    state.bStatic = true;
    state.bRemoveFromWorld = false;
    state.distanceFromPark = 0.0f;
    return state;
}

static void Fixes()
{
    // as it was parked: nothing to do
    CHECK(GetStandInFixes(Parked()) == STANDIN_FIX_NONE);

    SStandInState state = Parked();
    state.distanceFromPark = PARK_TOLERANCE;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_NONE);

    // SET_CHAR_COORDINATES on the stored handle moved it (CPed::Teleport keeps it static)
    state.distanceFromPark = PARK_TOLERANCE + 0.5f;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_PARK);

    // WARP_CHAR_INTO_CAR seated it: the seat goes back first, then it is parked again
    state = Parked();
    state.bInVehicle = true;
    CHECK(GetStandInFixes(state) == (STANDIN_FIX_LEAVE_VEHICLE | STANDIN_FIX_PARK));

    state = Parked();
    state.bInMovingList = true;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_PARK);

    state = Parked();
    state.bVisible = true;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_PARK);

    state = Parked();
    state.bUsesCollision = true;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_PARK);

    state = Parked();
    state.bStatic = false;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_PARK);

    // a vehicle's destructor flagged it (the next CWorld::Process walks only the moving list, but the flag goes)
    state = Parked();
    state.bRemoveFromWorld = true;
    CHECK(GetStandInFixes(state) == STANDIN_FIX_PARK);
}

static_assert(PARK_DEPTH >= 150.0f, "a stand-in waits out of every ped's scan range");

struct FakePed
{
    int id = 0;
};

static void List()
{
    CStandInList<FakePed, int, 3> list;
    FakePed a{1}, b{2}, c{3}, d{4};
    CHECK(list.IsEmpty() && list.Size() == 0 && !list.IsFull());

    CHECK(list.Add(nullptr, 0) == nullptr);
    CHECK(list.Add(&a, 10) != nullptr);
    CHECK(list.Add(&a, 11) == nullptr);  // once per ped
    CHECK(list.Add(&b, 20) != nullptr);
    CHECK(list.Add(&c, 30) != nullptr);
    CHECK(list.IsFull() && list.Size() == 3);
    CHECK(list.Add(&d, 40) == nullptr);  // full: the ped is deleted as before

    CHECK(list.Find(&b) != nullptr && list.Find(&b)->extra == 20);
    CHECK(list.Find(&d) == nullptr);

    // the destructor hook takes a stand-in out and gets what was kept with it; the last one moves into its place
    int removed = 0;
    CHECK(list.Remove(&a, &removed) && removed == 10);
    CHECK(list.Size() == 2 && list.At(0).pPed == &c && list.At(1).pPed == &b);
    CHECK(!list.Remove(&a, &removed));
    CHECK(list.Find(&a) == nullptr);

    // released from the last one while deleting removes the current entry (CDepartedPlayerPeds::Process): every one is
    // seen exactly once
    CHECK(list.Add(&a, 10) != nullptr);  // c, b, a
    std::vector<int> seen;
    for (size_t i = list.Size(); i-- > 0;)
    {
        FakePed* pPed = list.At(i).pPed;
        seen.push_back(pPed->id);
        CHECK(list.Remove(pPed));  // what the destructor hook does during the deletion
    }
    CHECK(list.IsEmpty());
    CHECK(seen.size() == 3 && seen[0] == 1 && seen[1] == 2 && seen[2] == 3);

    // one gone in the middle while the others are only reparked
    CHECK(list.Add(&a, 1) && list.Add(&b, 2) && list.Add(&c, 3));
    seen.clear();
    for (size_t i = list.Size(); i-- > 0;)
    {
        FakePed* pPed = list.At(i).pPed;
        seen.push_back(pPed->id);
        if (pPed == &b)
        {
            CHECK(list.Remove(pPed));
        }
    }
    CHECK(seen.size() == 3 && list.Size() == 2 && list.Find(&a) && list.Find(&c) && !list.Find(&b));
}

static void StoryThreads()
{
    using namespace StoryStartPolicy;

    // CRunningScript::m_szName: 8 characters, not always terminated, any case
    const char sweet[8] = {'s', 'w', 'e', 'e', 't', 0, 0, 0};
    CHECK(NameEquals(sweet, 8, "SWEET"));
    CHECK(!NameEquals(sweet, 8, "SWEET1"));
    CHECK(!NameEquals("SWEET1", 8, "SWEET"));
    CHECK(!NameEquals("INTRO1", 8, "INT"));
    CHECK(!NameEquals("INT", 8, "INTRO1"));
    const char full[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    CHECK(NameEquals(full, 8, "abcdefgh"));
    CHECK(!NameEquals(full, 8, "ABCDEFG"));

    // all 28 story strand trigger threads of MAIN
    size_t count = 0;
    for (const char* thread : STORY_TRIGGER_THREADS)
    {
        CHECK(IsStoryTriggerThread(thread, 8));
        count++;
    }
    CHECK(count == 28);
    CHECK(IsStoryTriggerThread("int", 8));
    CHECK(IsStoryTriggerThread("mansion", 8));

    // side activities, vehicle sub-missions, shops, the missions themselves and the other MAIN threads aren't
    const char* const notStory[] = {"GYM", "SHOOT", "TRUCKS", "QUARRYS", "BSCHOO", "BIKES", "BLOODR", "HOTR", "KICKS",
        "TRI", "R3", "ODDVEH", "BUY1", "BUY_PRO", "BARB", "JFUD", "TATTO", "CLOTH", "AMUNAT", "ROULETE", "MOB_GF", "GFAGNT",
        "ADPLANE", "INTRO", "INTRO1", "SWEET1", "SWEET3", "MAIN", "FLOW", "INTROST", "coopand", ""};
    for (const char* name : notStory)
    {
        CHECK(!IsStoryTriggerThread(name, 8));
    }
    CHECK(!IsStoryTriggerThread(nullptr, 8));
}

static void StoryStarts()
{
    using StoryStartPolicy::MayStartMission;

    // the game says no: no
    CHECK(!MayStartMission(false, false, false, "SMOKE", 8));
    CHECK(!MayStartMission(false, true, true, "SMOKE", 8));

    // alone (not in a session): the game's answer, story included
    CHECK(MayStartMission(true, false, false, "SMOKE", 8));

    // the host starts the story
    CHECK(MayStartMission(true, true, true, "SMOKE", 8));
    CHECK(MayStartMission(true, true, true, "SWEET", 8));

    // another game: no story start (also while no host is known), side activities as before
    CHECK(!MayStartMission(true, true, false, "SMOKE", 8));
    CHECK(!MayStartMission(true, true, false, "CESAR", 8));
    CHECK(MayStartMission(true, true, false, "GYM", 8));
    CHECK(MayStartMission(true, true, false, "QUARRYS", 8));
    CHECK(MayStartMission(true, true, false, "BARB", 8));

    // not asked by a script (no script runs a command)
    CHECK(MayStartMission(true, true, false, nullptr, 8));
}

struct FakeNetworkPed
{
    int id = 0;
};

struct FakeNetworkPlayer
{
    int id = 0;
    std::vector<FakeNetworkPed*> m_vPedClaims;
};

static void Handover()
{
    using PedHandoverPolicy::FindClaimer;

    FakeNetworkPed barber{7}, dealer{8};
    FakeNetworkPlayer host{0}, friendPlayer{1}, third{2};
    std::vector<FakeNetworkPlayer*> players = {&host, &friendPlayer, &third};

    // nobody claimed it: removed on every game
    CHECK(FindClaimer(players, &friendPlayer, &barber) == nullptr);

    // the host's barber shop script claimed the shopkeeper the friend's game created: the host takes it over
    host.m_vPedClaims.push_back(&barber);
    CHECK(FindClaimer(players, &friendPlayer, &barber) == &host);
    CHECK(FindClaimer(players, &friendPlayer, &dealer) == nullptr);

    // the leaving player's own claims count for nothing
    friendPlayer.m_vPedClaims.push_back(&dealer);
    CHECK(FindClaimer(players, &friendPlayer, &dealer) == nullptr);

    // several claimers: the first in connection order, like PED_REMOVE
    third.m_vPedClaims.push_back(&dealer);
    host.m_vPedClaims.push_back(&dealer);
    CHECK(FindClaimer(players, &friendPlayer, &dealer) == &host);
    CHECK(FindClaimer(players, &host, &dealer) == &friendPlayer);  // the host leaves: the friend claimed it first in order
    CHECK(FindClaimer(std::vector<FakeNetworkPlayer*>{&host, &third}, &host, &dealer) == &third);

    std::vector<FakeNetworkPlayer*> withGap = {nullptr, &third};
    CHECK(FindClaimer(withGap, &friendPlayer, &dealer) == &third);
}

int main()
{
    KeepAndRelease();
    Fixes();
    List();
    StoryThreads();
    StoryStarts();
    Handover();
    std::printf("player_leave_test: all checks passed\n");
    return 0;
}
