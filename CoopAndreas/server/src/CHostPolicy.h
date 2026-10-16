#pragma once

#include <string>
#include <vector>

#include "network/player_state.h"  // SanitizePlayerName: the nickname rule of Packets::System::PlayerConnected

// Private build: who is the host (story host pinning). Pure logic without ENet, so tests/session_policy_test.cpp checks
// it; CHostManager applies it on every connect and disconnect.
//
// - "story-host" empty or missing in server-config.ini: upstream's rule, the first player in the server's list (the one
//   connected longest) is the host (CNetworkPlayerManager::AssignHostToFirstPlayer).
// - "story-host" set: only the player with that nickname is ever the host. While they are away nobody is: players who
//   connect first wait without a host, and a story host who leaves isn't replaced until they are back.
namespace HostPolicy
{
struct SPlayer
{
    std::string name;  // CNetworkPlayer::m_Name, as the server read it from PLAYER_CONNECTED
    bool bIsHost = false;
};

struct SDecision
{
    int hostIndex = -1;        // the host after this decision (index into the players), -1 = nobody
    int previousIndex = -1;    // the host before it (the first player flagged as host), -1 = nobody
    bool bAnnounce = false;    // a new host: PLAYER_ASSIGN_HOST to everyone
    bool bTellJoiner = false;  // the host stays and a player connected: PLAYER_ASSIGN_HOST to that player only
};

// The configured nickname in the form the server compares: Packets::System::PlayerConnected turns every character
// other than a-z, A-Z, 0-9, '_', '[' and ']' into '_' and keeps at most MAX_NICKNAME_LENGTH of them, so "story-host =
// Big Smoke" names the player the server knows as "Big_Smoke". Empty stays empty (upstream's rule).
inline std::string NormalizeStoryHost(const std::string& configured)
{
    char name[Config::MAX_NICKNAME_LENGTH + 1];
    SanitizePlayerName(configured.c_str(), name);
    return name;
}

// index of the player who should be the host, -1 = nobody; storyHost is normalized
inline int ChooseHost(const std::vector<SPlayer>& players, const std::string& storyHost)
{
    if (storyHost.empty())
    {
        return players.empty() ? -1 : 0;
    }
    // exact and case-sensitive, like the server's own "nickname already taken" check
    for (size_t i = 0; i < players.size(); i++)
    {
        if (players[i].name == storyHost)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// joinedIndex: the player who just connected, -1 after a disconnect
inline SDecision Decide(const std::vector<SPlayer>& players, const std::string& storyHost, int joinedIndex)
{
    SDecision decision;
    for (size_t i = 0; i < players.size(); i++)
    {
        if (players[i].bIsHost)
        {
            decision.previousIndex = static_cast<int>(i);
            break;
        }
    }
    decision.hostIndex = ChooseHost(players, storyHost);
    decision.bAnnounce = decision.hostIndex >= 0 && decision.hostIndex != decision.previousIndex;
    decision.bTellJoiner = !decision.bAnnounce && decision.hostIndex >= 0 && joinedIndex >= 0 &&
                           joinedIndex != decision.hostIndex;
    return decision;
}
}  // namespace HostPolicy
