#pragma once

#include <cstdint>

// Private build: a player whose game crashed reconnects at once with the same nickname. A crashed, frozen or killed game
// sends no ENet disconnect, so its session stays until the ENet timeout (30-60 s). CNetwork::HandlePlayerConnected asks
// this rule when a new connection brings the nickname of a connected player; pure logic without ENet, so
// tests/reconnect_policy_test.cpp checks it.
namespace ReconnectPolicy
{
// How long the old session's ENet peer must have received nothing (ENetPeer::lastReceiveTime, set by every
// acknowledgement) before a connection with its nickname replaces it. A live game, also an alt-tabbed one (it keeps
// servicing ENet through CNetworkKeepAlive), acknowledges the server's pings every 500 ms (ENET_PEER_PING_INTERVAL),
// so 5 s is well above that and above Wi-Fi spikes: nobody kicks a live player by using their name. A game blocked in
// a long load could be replaced, but only by a connection using its own nickname.
constexpr uint32_t STALE_SESSION_MS = 5000;

// silentMs: time since the old session's peer last received anything (wrap-safe ENet time difference)
inline bool IsSessionStale(uint32_t silentMs)
{
    return silentMs >= STALE_SESSION_MS;
}
}  // namespace ReconnectPolicy
