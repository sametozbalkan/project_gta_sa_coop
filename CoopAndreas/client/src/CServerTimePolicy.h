#pragma once

#include <cstdint>

// Private build (vehicle sync protocol, 0.1.1): the client's estimate of the server's clock, the stamp of everything it
// sends (CPacketFactory) and the time its packet buffer releases what it received (CPacketBuffer). Pure logic without GTA
// or ENet, so tests/server_time_test.cpp checks it; CServerTime and CPacketBuffer apply it.
//
// Upstream sent a SERVER_TIME_REQUEST every 500 ms and kept only the newest request's tick, so an answer slower than
// 500 ms was paired with the newer request: its round trip came out too short and the clock stepped back by 45-75 ms in
// one frame. And every answer moved the offset by a tenth of its difference, backwards too, so the stamps of a game's
// packets could go back in time.
namespace ServerTimePolicy
{
constexpr uint32_t REQUEST_INTERVAL_MS = 500;   // as upstream's REQUEST_FREQUENCY
constexpr uint32_t REQUEST_TIMEOUT_MS = 2000;   // an unanswered request holds back the next one this long
constexpr uint32_t SLEW_DIVISOR = 10;           // a correction backwards slows the clock to 9/10 of real time at most

// A new request: not while the last one has been unanswered for less than REQUEST_TIMEOUT_MS (requestTick 0 = none in
// flight). Unsigned elapsed time also handles the tick wrap.
inline bool MaySendRequest(uint32_t requestTick, uint32_t now)
{
    return requestTick == 0 || static_cast<uint32_t>(now - requestTick) >= REQUEST_TIMEOUT_MS;
}

// The answer echoes its request's tick (Packets::System::ServerTimeRequest::clientTick): it counts only for the request
// in flight. A late answer to a request given up after REQUEST_TIMEOUT_MS, or to one CNetworkKeepAlive cancelled (it sets
// the tick to 0 after the background, whose hand-over would count as network delay), isn't paired with another.
inline bool IsAnswerToOutstanding(uint32_t requestTick, uint32_t echoedTick)
{
    return requestTick != 0 && echoedTick == requestTick;
}

// The estimate of the offset (server time - GetTickCount()): the first answer sets it, every later one moves it by a
// tenth of the difference (upstream's smoothing)
inline int32_t NextTargetOffset(bool bFirstAnswer, int32_t target, int32_t sample)
{
    if (bFirstAnswer)
    {
        return sample;
    }
    const int64_t difference = static_cast<int64_t>(sample) - static_cast<int64_t>(target);
    return static_cast<int32_t>(static_cast<int64_t>(target) + static_cast<int64_t>(0.1 * static_cast<double>(difference)));
}

// The offset the stamped clock (g_serverTime = GetTickCount() + offset) uses after `elapsed` ms: a correction forwards
// at once, one backwards spread out, by at most elapsed / SLEW_DIVISOR (at least 1 ms, never more than elapsed) per call,
// so the clock never runs backwards after its first answer; it runs slower until it has caught up.
inline int32_t NextAppliedOffset(int32_t applied, int32_t target, uint32_t elapsed)
{
    if (target >= applied)
    {
        return target;
    }
    uint32_t back = elapsed / SLEW_DIVISOR;
    if (back == 0 && elapsed > 0)
    {
        back = 1;
    }
    const uint32_t gap = static_cast<uint32_t>(static_cast<int64_t>(applied) - static_cast<int64_t>(target));
    if (back > gap)
    {
        back = gap;
    }
    return static_cast<int32_t>(static_cast<int64_t>(applied) - static_cast<int64_t>(back));
}

// A packet stamped this far ahead of this game's clock, once that clock has had its first answer, was stamped by a game
// whose clock hadn't had its first answer yet: with its PC's uptime, which runs hours or days ahead of the server's clock
// (that starts at 0 with server.exe). The packet buffer held such a packet until this game's clock got there, in effect
// for good; it is handled as if stamped now instead. Synced clocks differ by about a round trip.
constexpr uint32_t MAX_STAMP_AHEAD_MS = 10000;

inline bool IsStampFromUnsyncedClock(uint32_t stamp, uint32_t now)
{
    return static_cast<int32_t>(stamp - now) > static_cast<int32_t>(MAX_STAMP_AHEAD_MS);
}
}  // namespace ServerTimePolicy
