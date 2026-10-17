// Private build: policy test for the client's server clock (client/src/CServerTimePolicy.h) without GTA or ENet: one
// request in flight, answers paired with their own request, a stamped clock that never runs backwards after its first
// answer, and the stamps of a game whose clock hadn't had its first answer. Build and run: see CLAUDE.md (Fork changes,
// vehicle sync protocol); no include path needed.
#include "../client/src/CServerTimePolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace ServerTimePolicy;

// The client as CServerTime runs it: a request every REQUEST_INTERVAL_MS while none is in flight, the answer's offset
// smoothed into the target, the stamped clock following it
struct FakeClock
{
    uint32_t requestTick = 0;
    uint32_t lastRequest = 0;
    bool bSynced = false;
    int32_t target = 0;
    int32_t applied = 0;
    uint32_t lastRecalculated = 0;

    bool Tick(uint32_t now)  // CServerTime::Process: true when a request goes out
    {
        Recalculate(now);
        if (now - lastRequest > REQUEST_INTERVAL_MS && MaySendRequest(requestTick, now))
        {
            requestTick = now;
            lastRequest = now;
            return true;
        }
        return false;
    }
    bool Answer(uint32_t now, uint32_t echoedTick, uint32_t serverRespond)
    {
        if (!IsAnswerToOutstanding(requestTick, echoedTick))
            return false;
        const uint32_t rtt = now - requestTick;
        const uint32_t serverTime = serverRespond + rtt / 2;
        const int32_t sample = static_cast<int32_t>(static_cast<int64_t>(serverTime) - static_cast<int64_t>(now));
        const bool bFirst = !bSynced;
        target = NextTargetOffset(bFirst, target, sample);
        if (bFirst)
        {
            bSynced = true;
            applied = target;
            lastRecalculated = now;
        }
        Recalculate(now);
        requestTick = 0;
        return true;
    }
    void Recalculate(uint32_t now)
    {
        if (bSynced)
        {
            applied = NextAppliedOffset(applied, target, now - lastRecalculated);
            lastRecalculated = now;
        }
    }
    uint32_t Stamp(uint32_t now) const { return now + static_cast<uint32_t>(applied); }
};

void OneRequestInFlight()
{
    CHECK(MaySendRequest(0, 1000));
    CHECK(!MaySendRequest(1000, 1000 + REQUEST_TIMEOUT_MS - 1));
    CHECK(MaySendRequest(1000, 1000 + REQUEST_TIMEOUT_MS));  // given up: a new one may go
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 100;
    CHECK(!MaySendRequest(beforeWrap, 500));  // 601 ms across the wrap

    FakeClock clock;
    CHECK(clock.Tick(1000));
    CHECK(!clock.Tick(1600));  // upstream sent another one here and overwrote the request's tick
    CHECK(!clock.Tick(2999));
    CHECK(clock.Tick(3000));   // the first one is given up after REQUEST_TIMEOUT_MS
}

void AnswersPairWithTheirRequest()
{
    // The review's case: a request at 1000, its answer 600 ms later. Upstream had sent the next request at 1501 and paired
    // the answer with it: a round trip of 99 ms instead of 600, the offset 250 ms too low, the clock back by a tenth of that.
    FakeClock clock;
    clock.Tick(1000);
    CHECK(clock.Answer(1600, 1000, 5000));  // server time 5000 when it answered, 300 ms to come back
    CHECK(clock.bSynced && clock.target == 5000 + 300 - 1600);
    // an answer that matches no request in flight (late after a give-up, or after the keep-alive cancelled it) is ignored
    clock.Tick(2200);
    CHECK(!clock.Answer(2300, 1000, 5600));
    CHECK(!clock.Answer(2300, 0, 5600));
    CHECK(clock.Answer(2300, 2200, 5650));
    // the keep-alive's cancel (requestTick = 0): its answer is dropped
    FakeClock cancelled;
    cancelled.Tick(1000);
    cancelled.requestTick = 0;
    CHECK(!cancelled.Answer(1100, 1000, 2000));
    CHECK(!cancelled.bSynced);
    CHECK(cancelled.Tick(1600));  // and a new request goes out
}

void NeverBackwards()
{
    // the first answer replaces the PC's uptime outright (a step back of hours is expected there)
    CHECK(NextTargetOffset(true, 0, -123456) == -123456);
    // later ones move the estimate by a tenth
    CHECK(NextTargetOffset(false, 1000, 1100) == 1010);
    CHECK(NextTargetOffset(false, 1000, 900) == 990);

    // a correction forwards applies at once, one backwards is spread out and never makes the clock go back
    CHECK(NextAppliedOffset(100, 150, 16) == 150);
    CHECK(NextAppliedOffset(100, 40, 30) == 97);   // 30 ms: at most 3 ms back
    CHECK(NextAppliedOffset(100, 99, 30) == 99);   // no further than the target
    CHECK(NextAppliedOffset(100, 40, 5) == 99);    // a short frame: at least 1 ms
    CHECK(NextAppliedOffset(100, 40, 0) == 100);   // no time passed: no change

    // a whole session: answers with jittering round trips, the stamps keep going up, and the clock reaches the estimate
    FakeClock clock;
    uint32_t now = 1000;
    clock.Tick(now);
    now += 80;
    clock.Answer(now, 1000, 50000);
    uint32_t lastStamp = clock.Stamp(now);
    const uint32_t trueOffset = 50000 + 40 - 1080;  // the server's clock minus this PC's
    for (int frame = 0; frame < 20000; frame++)
    {
        now += 33;  // the frame limiter's ~30 fps
        if (clock.Tick(now))
        {
            // the answer comes 20..200 ms later, the server's clock at the half-way point plus up to 60 ms of asymmetry
            const uint32_t sent = clock.requestTick;
            const uint32_t rtt = 20 + static_cast<uint32_t>((frame * 37) % 180);
            const uint32_t asymmetry = static_cast<uint32_t>((frame * 13) % 60);
            const uint32_t respond = sent + trueOffset + rtt / 2 - 30 + asymmetry;
            now += rtt;
            CHECK(clock.Answer(now, sent, respond));
        }
        const uint32_t stamp = clock.Stamp(now);
        CHECK(static_cast<int32_t>(stamp - lastStamp) >= 0);
        lastStamp = stamp;
    }
    // within a round trip of the truth
    const int32_t error = clock.applied - static_cast<int32_t>(trueOffset);
    CHECK(error > -100 && error < 100);
}

void UnsyncedStamps()
{
    // a joiner stamps with its PC's uptime until its first answer: hours ahead of a server that started an hour ago
    const uint32_t serverNow = 3600u * 1000u;
    const uint32_t uptime = 5u * 3600u * 1000u;
    CHECK(IsStampFromUnsyncedClock(uptime, serverNow));
    // what synced clocks send: a round trip around now, also a little ahead
    CHECK(!IsStampFromUnsyncedClock(serverNow + 300, serverNow));
    CHECK(!IsStampFromUnsyncedClock(serverNow + MAX_STAMP_AHEAD_MS, serverNow));
    CHECK(IsStampFromUnsyncedClock(serverNow + MAX_STAMP_AHEAD_MS + 1, serverNow));
    // behind: handled at once anyway, as before
    CHECK(!IsStampFromUnsyncedClock(serverNow - 3600000u, serverNow));
}

int main()
{
    OneRequestInFlight();
    AnswersPairWithTheirRequest();
    NeverBackwards();
    UnsyncedStamps();
    std::puts("server_time: all checks passed");
}
