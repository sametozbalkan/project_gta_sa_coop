// Private build: policy test for a player whose game crashed reconnecting at once with the same nickname
// (server/src/CReconnectPolicy.h, used by CNetwork::HandlePlayerConnected) without ENet: a silent session is replaced,
// a live one (also alt-tabbed or on a spiky Wi-Fi) keeps its nickname. No include path needed.
#include "../server/src/CReconnectPolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace ReconnectPolicy;

// ENET_TIME_DIFFERENCE (third_party/enet/time.h), the server's measure of the old session's silence
static uint32_t EnetTimeDifference(uint32_t a, uint32_t b)
{
    const uint32_t overflow = 86400000;
    return a - b >= overflow ? b - a : a - b;
}

void LiveSessionKeepsItsName()
{
    CHECK(!IsSessionStale(0));
    CHECK(!IsSessionStale(500));   // one ping interval (ENET_PEER_PING_INTERVAL)
    CHECK(!IsSessionStale(1500));  // a few lost pings
    CHECK(!IsSessionStale(STALE_SESSION_MS - 1));
}

void SilentSessionIsReplaced()
{
    CHECK(IsSessionStale(STALE_SESSION_MS));
    CHECK(IsSessionStale(30000));  // still before the ENet timeout (timeoutMinimum 30 s), which nobody waits for now
    CHECK(IsSessionStale(std::numeric_limits<uint32_t>::max()));
}

void ThresholdAbovePings()
{
    CHECK(STALE_SESSION_MS >= 10u * 500u);  // ten missed pings
    CHECK(STALE_SESSION_MS < 30000u);       // well below the ENet timeout it replaces
}

void SilenceAcrossTheClockWrap()
{
    // the last acknowledgement shortly before the 32-bit millisecond clock wraps, the new connection shortly after
    const uint32_t last = std::numeric_limits<uint32_t>::max() - 999;
    CHECK(EnetTimeDifference(last + 6000, last) == 6000);
    CHECK(IsSessionStale(EnetTimeDifference(last + 6000, last)));
    CHECK(!IsSessionStale(EnetTimeDifference(last + 1200, last)));
    // an acknowledgement stamped after the time read for the decision counts as no silence, not as a huge one
    CHECK(!IsSessionStale(EnetTimeDifference(1000, 1003)));
}

int main()
{
    LiveSessionKeepsItsName();
    SilentSessionIsReplaced();
    ThresholdAbovePings();
    SilenceAcrossTheClockWrap();
    std::puts("reconnect_policy: all checks passed");
}
