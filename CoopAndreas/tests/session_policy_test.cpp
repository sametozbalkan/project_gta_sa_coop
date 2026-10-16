// Private build: policy test for the session foundation without GTA or ENet: the server's host choice with and without
// "story-host" (server/src/CHostPolicy.h, applied by CHostManager) and the client lifecycle's state (client/src/
// CSessionPolicy.h, used by CSessionLifecycle). Build and run: see CLAUDE.md (Fork changes, session lifecycle).
#include "../server/src/CHostPolicy.h"
#include "../client/src/CSessionPolicy.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

// The server's side of a host decision, as CHostManager::Update applies it: the flags, and who hears about it.
struct FakeServer
{
    std::string storyHost;
    std::vector<HostPolicy::SPlayer> players;  // in connection order, like CNetworkPlayerManager::m_pPlayers
    std::vector<std::string> announced;        // PLAYER_ASSIGN_HOST to everyone: the host's name
    std::vector<std::string> told;             // PLAYER_ASSIGN_HOST to the joiner alone: "joiner>host"

    explicit FakeServer(const std::string& configured) : storyHost(HostPolicy::NormalizeStoryHost(configured)) {}

    void Apply(int joinedIndex)
    {
        const HostPolicy::SDecision decision = HostPolicy::Decide(players, storyHost, joinedIndex);
        for (size_t i = 0; i < players.size(); i++)
        {
            players[i].bIsHost = static_cast<int>(i) == decision.hostIndex;
        }
        if (decision.bAnnounce)
        {
            announced.push_back(players[decision.hostIndex].name);
        }
        else if (decision.bTellJoiner)
        {
            told.push_back(players[joinedIndex].name + ">" + players[decision.hostIndex].name);
        }
    }

    void Connect(const std::string& name)
    {
        players.push_back({name, false});
        Apply(static_cast<int>(players.size()) - 1);
    }

    void Disconnect(const std::string& name)
    {
        for (size_t i = 0; i < players.size(); i++)
        {
            if (players[i].name == name)
            {
                players.erase(players.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        Apply(-1);
    }

    std::string Host() const
    {
        for (const HostPolicy::SPlayer& player : players)
        {
            if (player.bIsHost)
            {
                return player.name;
            }
        }
        return "";
    }

    int HostCount() const
    {
        int count = 0;
        for (const HostPolicy::SPlayer& player : players)
        {
            count += player.bIsHost ? 1 : 0;
        }
        return count;
    }
};

void NormalizeStoryHost()
{
    CHECK(HostPolicy::NormalizeStoryHost("").empty());
    CHECK(HostPolicy::NormalizeStoryHost("Samet") == "Samet");
    CHECK(HostPolicy::NormalizeStoryHost("[CJ]_Carl99") == "[CJ]_Carl99");
    // Packets::System::PlayerConnected's rule: anything else becomes '_', at most 24 characters
    CHECK(HostPolicy::NormalizeStoryHost("Big Smoke") == "Big_Smoke");
    CHECK(HostPolicy::NormalizeStoryHost("a-b.c") == "a_b_c");
    CHECK(HostPolicy::NormalizeStoryHost("\xC5\x9F" "ahin") == "__ahin");  // UTF-8 bytes count one by one
    CHECK(HostPolicy::NormalizeStoryHost("abcdefghijklmnopqrstuvwxyz") == "abcdefghijklmnopqrstuvwx");
    CHECK(HostPolicy::NormalizeStoryHost("abcdefghijklmnopqrstuvwxyz").size() == Config::MAX_NICKNAME_LENGTH);
}

void ChooseHost()
{
    using HostPolicy::ChooseHost;
    const std::vector<HostPolicy::SPlayer> none;
    const std::vector<HostPolicy::SPlayer> two = {{"Friend", false}, {"Samet", false}};

    // upstream's rule: the first player in the list
    CHECK(ChooseHost(none, "") == -1);
    CHECK(ChooseHost(two, "") == 0);

    // pinned: only that nickname, exact and case-sensitive
    CHECK(ChooseHost(none, "Samet") == -1);
    CHECK(ChooseHost(two, "Samet") == 1);
    CHECK(ChooseHost(two, "Friend") == 0);
    CHECK(ChooseHost(two, "samet") == -1);
    CHECK(ChooseHost(two, "Sam") == -1);
    CHECK(ChooseHost({{"Friend", false}}, "Samet") == -1);
    // the same nickname twice (a debug server allows it): the one connected first
    CHECK(ChooseHost({{"Samet", false}, {"Samet", false}}, "Samet") == 0);
}

void UpstreamRule()
{
    FakeServer server("");
    server.Connect("A");
    CHECK(server.Host() == "A");
    CHECK(server.announced == std::vector<std::string>({"A"}));

    // a player who connects later learns who the host is, nobody else hears anything
    server.Connect("B");
    CHECK(server.Host() == "A");
    CHECK(server.announced.size() == 1);
    CHECK(server.told == std::vector<std::string>({"B>A"}));

    // a player other than the host leaves: nothing changes
    server.Connect("C");
    server.Disconnect("B");
    CHECK(server.Host() == "A");
    CHECK(server.announced.size() == 1);

    // the host leaves: the first one left takes over and everyone hears it
    server.Disconnect("A");
    CHECK(server.Host() == "C");
    CHECK(server.announced == std::vector<std::string>({"A", "C"}));

    // the old host comes back: it stays a player (upstream's behaviour, kept without story-host)
    server.Connect("A");
    CHECK(server.Host() == "C");
    CHECK(server.told.back() == "A>C");

    server.Disconnect("C");
    CHECK(server.Host() == "A");
    server.Disconnect("A");
    CHECK(server.Host().empty());
    CHECK(server.announced == std::vector<std::string>({"A", "C", "A"}));
    CHECK(server.HostCount() == 0);
}

void StoryHostConnectsSecond()
{
    FakeServer server("Samet");

    // the friend connects first: no host, nobody is told anything
    server.Connect("Friend");
    CHECK(server.Host().empty());
    CHECK(server.announced.empty() && server.told.empty());

    // the story host connects: it becomes the host and everyone hears it
    server.Connect("Samet");
    CHECK(server.Host() == "Samet");
    CHECK(server.announced == std::vector<std::string>({"Samet"}));
    CHECK(server.told.empty());

    // a third player learns who the host is
    server.Connect("Third");
    CHECK(server.told == std::vector<std::string>({"Third>Samet"}));
    CHECK(server.HostCount() == 1);
}

void StoryHostLeavesAndReturns()
{
    FakeServer server("Samet");
    server.Connect("Samet");
    CHECK(server.Host() == "Samet");
    server.Connect("Friend");
    CHECK(server.told == std::vector<std::string>({"Friend>Samet"}));

    // the story host leaves: nobody takes over and no PLAYER_ASSIGN_HOST goes out (the friend sees the host's
    // PLAYER_DISCONNECTED only)
    server.Disconnect("Samet");
    CHECK(server.Host().empty());
    CHECK(server.HostCount() == 0);
    CHECK(server.announced == std::vector<std::string>({"Samet"}));

    // another player connecting meanwhile doesn't become the host either and isn't told about one
    server.Connect("Third");
    CHECK(server.Host().empty());
    CHECK(server.told.size() == 1);

    // the story host returns, last in the list now: the host again
    server.Connect("Samet");
    CHECK(server.Host() == "Samet");
    CHECK(server.announced == std::vector<std::string>({"Samet", "Samet"}));

    // the friend leaving changes nothing
    server.Disconnect("Friend");
    CHECK(server.Host() == "Samet");
    CHECK(server.announced.size() == 2);
}

void StoryHostSpelledAsInTheIni()
{
    // story-host = Big Smoke names the player the server knows as Big_Smoke
    FakeServer server("Big Smoke");
    server.Connect("Ryder");
    CHECK(server.Host().empty());
    server.Connect("Big_Smoke");
    CHECK(server.Host() == "Big_Smoke");
}

void RuleChangedUnderAHost()
{
    // not reachable while the server runs (the config is read once), but the decision stays sane: the flag goes, and
    // there is nothing to announce
    const std::vector<HostPolicy::SPlayer> players = {{"Samet", true}, {"Friend", false}};
    const HostPolicy::SDecision decision = HostPolicy::Decide(players, "Other", -1);
    CHECK(decision.hostIndex == -1);
    CHECK(decision.previousIndex == 0);
    CHECK(!decision.bAnnounce && !decision.bTellJoiner);

    // two flags (never set by the server): the first counts as the previous host, the chosen one is announced
    const std::vector<HostPolicy::SPlayer> twoFlags = {{"A", true}, {"B", true}};
    const HostPolicy::SDecision fixed = HostPolicy::Decide(twoFlags, "B", -1);
    CHECK(fixed.previousIndex == 0 && fixed.hostIndex == 1 && fixed.bAnnounce);
}

void ClientHostState()
{
    CSessionHostState state;
    CSessionHostState::STransition transition;

    // no session yet: PLAYER_ASSIGN_HOST and PLAYER_DISCONNECTED change nothing
    CHECK(!state.IsInSession());
    CHECK(!state.OnAssign(0, transition));
    CHECK(!state.HasHost());

    // the friend's game (id 0) is in, nobody is the host
    state.Reset(0);
    CHECK(state.IsInSession() && !state.HasHost() && !state.IsLocalHost() && state.GetHostId() == -1);

    // the story host (id 1) connects and gets the host
    CHECK(state.OnAssign(1, transition));
    CHECK(transition.oldHostId == -1 && transition.newHostId == 1 && !transition.bWasLocal && !transition.bIsLocal);
    CHECK(state.HasHost() && state.GetHostId() == 1 && !state.IsLocalHost());
    CHECK(!state.OnAssign(1, transition));  // a repeat is no change
    CHECK(!state.OnAssign(-1, transition));

    // another player leaving isn't the host's loss
    CHECK(!state.OnPlayerLeft(2, transition));
    CHECK(state.GetHostId() == 1);

    // the host leaves: nobody is the host
    CHECK(state.OnPlayerLeft(1, transition));
    CHECK(transition.oldHostId == 1 && transition.newHostId == -1 && !transition.bWasLocal && !transition.bIsLocal);
    CHECK(!state.HasHost());
    CHECK(!state.OnPlayerLeft(1, transition));

    // upstream's rule hands the host to this game
    CHECK(state.OnAssign(0, transition));
    CHECK(transition.oldHostId == -1 && transition.newHostId == 0 && !transition.bWasLocal && transition.bIsLocal);
    CHECK(state.IsLocalHost());

    // and to another game while this one is connected (the server doesn't do that today)
    CHECK(state.OnAssign(3, transition));
    CHECK(transition.oldHostId == 0 && transition.newHostId == 3 && transition.bWasLocal && !transition.bIsLocal);

    // the session ends
    state.Reset(-1);
    CHECK(!state.IsInSession() && !state.HasHost() && !state.IsLocalHost());
    CHECK(!state.OnAssign(3, transition));
}

void HostMission()
{
    CHostMissionState mission;
    CHECK(!mission.IsOnMission());
    CHECK(!mission.OnFlag(false));
    CHECK(mission.OnFlag(true));  // started
    CHECK(!mission.OnFlag(true));
    CHECK(mission.IsOnMission());
    CHECK(mission.OnFlag(false));  // ended normally
    CHECK(!mission.EndAfterHostLoss());

    // the host leaves during its mission: it ends once, when the loss is settled
    CHECK(mission.OnFlag(true));
    CHECK(mission.EndAfterHostLoss());
    CHECK(!mission.IsOnMission());
    CHECK(!mission.EndAfterHostLoss());
}

void HostLossTimer()
{
    CHostLossTimer timer;
    CHECK(!timer.IsPending());
    CHECK(!timer.IsDue(1000, 250));

    timer.Start(1000);
    CHECK(timer.IsPending());
    CHECK(!timer.IsDue(1100, 250));
    CHECK(!timer.IsDue(1249, 250));
    CHECK(timer.IsDue(1250, 250));
    CHECK(!timer.IsPending());
    CHECK(!timer.IsDue(5000, 250));  // once only

    // GetTickCount wraps after 49.7 days
    timer.Start(0xFFFFFF00u);
    CHECK(!timer.IsDue(0xFFFFFFF0u, 250));
    CHECK(timer.IsDue(0x00000010u, 250));

    timer.Start(2000);
    timer.Cancel();
    CHECK(!timer.IsPending());
    CHECK(!timer.IsDue(9000, 250));
}

void GameRestart()
{
    CGameRestartDetector detector;
    bool bLoaded = true;
    CHECK(!detector.Update(10, bLoaded));
    CHECK(!bLoaded);
    CHECK(!detector.Update(20, bLoaded));
    CHECK(!detector.Update(20, bLoaded));  // a frame with the counter standing still (paused)

    // a new game: the frame counter starts over
    CHECK(detector.Update(3, bLoaded));
    CHECK(!bLoaded);
    CHECK(!detector.Update(4, bLoaded));

    // a load: the counter went back and the GenericLoad hook told us
    detector.OnSaveLoaded();
    CHECK(detector.Update(1, bLoaded));
    CHECK(bLoaded);
    CHECK(!detector.Update(2, bLoaded));
    CHECK(!bLoaded);

    // a load right after start-up, before the counter got far: the hook alone says it
    detector.OnSaveLoaded();
    CHECK(detector.Update(5, bLoaded));
    CHECK(bLoaded);
    CHECK(!detector.Update(6, bLoaded));
}

void FreePlay()
{
    CFreePlayTimer timer;
    CHECK(timer.Update(false, 100) == 0);
    CHECK(timer.Update(true, 200) == 0);
    CHECK(timer.Update(true, 700) == 500);
    CHECK(timer.Update(true, 1200) == 1000);
    CHECK(timer.Update(false, 1300) == 0);  // a menu, fade or cutscene breaks it
    CHECK(timer.Update(true, 1400) == 0);
    CHECK(timer.Update(true, 2400) == 1000);
    timer.Reset();  // the game restarted
    CHECK(timer.Update(true, 2500) == 0);
    CHECK(timer.Update(true, 2600) == 100);

    // GetTickCount wraps after 49.7 days
    timer.Reset();
    CHECK(timer.Update(true, 0xFFFFFF00u) == 0);
    CHECK(timer.Update(true, 0x00000010u) == 0x110u);
}

// The friend's game through a pinned session: no host, the story host comes, plays a mission and drops out during it.
void FriendThroughAPinnedSession()
{
    CSessionHostState host;
    CHostMissionState mission;
    CHostLossTimer loss;
    CSessionHostState::STransition transition;

    host.Reset(0);
    CHECK(!host.HasHost());

    CHECK(host.OnAssign(1, transition) && !transition.bIsLocal);
    CHECK(mission.OnFlag(true));

    // the story host's PLAYER_DISCONNECTED: nobody is the host at once, its mission waits for its last packets
    CHECK(host.OnPlayerLeft(1, transition));
    loss.Start(10000);
    CHECK(!host.HasHost() && loss.IsPending() && mission.IsOnMission());

    // a packet still in the buffer (sent before it left) changes the flag first
    CHECK(!mission.OnFlag(true));
    CHECK(!loss.IsDue(10100, 250));

    // settled: the mission ends here (CSessionLifecycle cleans up its copy and sends HOST_MISSION_ENDED, bHostLost)
    CHECK(loss.IsDue(10250, 250));
    CHECK(mission.EndAfterHostLoss());
    CHECK(!mission.IsOnMission());

    // the story host returns
    CHECK(host.OnAssign(1, transition) && transition.oldHostId == -1);
    CHECK(!mission.IsOnMission());
}

int main()
{
    NormalizeStoryHost();
    ChooseHost();
    UpstreamRule();
    StoryHostConnectsSecond();
    StoryHostLeavesAndReturns();
    StoryHostSpelledAsInTheIni();
    RuleChangedUnderAHost();
    ClientHostState();
    HostMission();
    HostLossTimer();
    GameRestart();
    FreePlay();
    FriendThroughAPinnedSession();
    std::printf("session_policy_test: all checks passed\n");
    return 0;
}
