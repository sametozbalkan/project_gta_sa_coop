#pragma once

#include <string>

class CNetworkPlayer;
class Packet;

// Private build: the host decision (story host pinning, rules in CHostPolicy.h) and the relays for packets only the
// host may send.
class CHostManager
{
public:
    // after CConfigManager::Init: reads "story-host" from server-config.ini and prints the rule in use
    static void Init();

    // After a player connected (pJoined, already in the player list) or disconnected (pLeft, already out of it):
    // applies HostPolicy::Decide, sends PLAYER_ASSIGN_HOST to everyone when the host changes and to the new player
    // alone when it doesn't (so every game knows who the host is), and prints the decision. There is no packet for
    // "nobody is the host": a host that leaves unreplaced shows up only as its PLAYER_DISCONNECTED, and a player who
    // connects while nobody is the host gets no PLAYER_ASSIGN_HOST.
    static void Update(CNetworkPlayer* pJoined, CNetworkPlayer* pLeft);

    // Relays for host-only packets, for the handlers of new packet types. Nothing is sent and false comes back when
    // pSender isn't the host, so a packet still in flight from a player who stopped being host is dropped.
    // From the host to every other player:
    static bool SendFromHostToOthers(Packet& packet, CNetworkPlayer* pSender);
    // From the host to one other player (false also when targetPlayerId isn't connected or is the host itself):
    static bool SendFromHostToPlayer(Packet& packet, CNetworkPlayer* pSender, int targetPlayerId);

    // the normalized story host nickname, empty = upstream's rule (the first player)
    static const std::string& GetStoryHost() { return ms_sStoryHost; }

private:
    static inline std::string ms_sStoryHost;
};
