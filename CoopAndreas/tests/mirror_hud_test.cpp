// Private build: policy test for the mission mirror layer's timers and counters (client/src/CMirrorHud.h) without GTA or
// ENet: ownership by comparison of the var offsets, the send rates, the clock's generation rule, the paused flag and beep
// time a receiver puts back, where a receiver places the host's counters, and the MISSION_MIRROR_HUD wire format. Build
// and run: see CLAUDE.md (Fork changes, mission mirror layer, timers and counters).
#include "../client/src/CMirrorHudPolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace MirrorHudPolicy;

namespace
{
// the five var offsets: [0] the clock, [1 + i] counter slot i
struct SVars
{
    uint32_t v[NUM_ENTRIES]{};
};

SClock MakeClock(uint16_t generation, int32_t value, const char* key = "TX_TIME")
{
    SClock clock;
    clock.bOn = true;
    clock.generation = generation;
    CopyKey(clock.key, key);
    clock.bCountDown = true;
    clock.beepSeconds = 12;
    clock.value = value;
    return clock;
}

SCounter MakeCounter(uint16_t generation, int32_t value, const char* key = "SWE4_08", uint16_t type = 1)
{
    SCounter counter;
    counter.bOn = true;
    counter.generation = generation;
    CopyKey(counter.key, key);
    counter.type = type;
    counter.colour = 3;
    counter.bFlash = true;
    counter.value = value;
    return counter;
}

bool SameState(const SState& a, const SState& b)
{
    return SameStructure(a, b) && SameCounterValues(a, b) && SameClockValue(a, b) && a.clock.value == b.clock.value;
}
}  // namespace

// ---- ownership by comparison (host) ----

void OwnershipByComparison()
{
    COwnership ownership;
    SVars vars;
    ownership.Reset(vars.v);
    CHECK(ownership.GetOwnedMask() == 0);

    // the mission's display_onscreen_timer_with_string: the clock is the mission's
    vars.v[CLOCK_ENTRY] = 100;
    CHECK(ownership.HasChanged(vars.v));
    CHECK(ownership.OnCommand(vars.v, true) == 1u << CLOCK_ENTRY);
    CHECK(ownership.IsOwned(CLOCK_ENTRY));
    const uint16_t clockGeneration = ownership.GetGeneration(CLOCK_ENTRY);
    CHECK(clockGeneration != 0);
    CHECK(!ownership.HasChanged(vars.v));

    // another script's counter (a strand thread, the host's gym): not the mission's
    vars.v[CounterEntry(0)] = 200;
    CHECK(ownership.OnCommand(vars.v, false) == 0);
    CHECK(!ownership.IsOwned(CounterEntry(0)));
    CHECK(ownership.GetGeneration(CounterEntry(0)) == 0);

    // the mission's next commands change nothing: the clock keeps its generation, the other counter stays unowned
    CHECK(ownership.OnCommand(vars.v, true) == 0);
    CHECK(ownership.GetGeneration(CLOCK_ENTRY) == clockGeneration);
    CHECK(!ownership.IsOwned(CounterEntry(0)));

    // the mission's counter in slot 1
    vars.v[CounterEntry(1)] = 300;
    CHECK(ownership.OnCommand(vars.v, true) == 1u << CounterEntry(1));
    const uint16_t counterGeneration = ownership.GetGeneration(CounterEntry(1));
    CHECK(counterGeneration != 0 && counterGeneration != clockGeneration);
    CHECK(ownership.GetOwnedMask() == ((1u << CLOCK_ENTRY) | (1u << CounterEntry(1))));

    // the mission clears its clock and displays one again on the same variable: a new generation
    vars.v[CLOCK_ENTRY] = 0;
    CHECK(ownership.OnCommand(vars.v, true) == 0);
    CHECK(!ownership.IsOwned(CLOCK_ENTRY));
    CHECK(ownership.GetGeneration(CLOCK_ENTRY) == 0);
    vars.v[CLOCK_ENTRY] = 100;
    CHECK(ownership.OnCommand(vars.v, true) == 1u << CLOCK_ENTRY);
    const uint16_t secondClock = ownership.GetGeneration(CLOCK_ENTRY);
    CHECK(secondClock != clockGeneration && secondClock != 0);

    // the engine clears a clock that ran out between frames: seen outside a command, unowned
    vars.v[CLOCK_ENTRY] = 0;
    ownership.Refresh(vars.v);
    CHECK(!ownership.IsOwned(CLOCK_ENTRY));

    // a non-family script takes the free clock, then (rule check) the mission puts its own variable there
    vars.v[CLOCK_ENTRY] = 500;
    CHECK(ownership.OnCommand(vars.v, false) == 0);
    CHECK(!ownership.IsOwned(CLOCK_ENTRY));
    vars.v[CLOCK_ENTRY] = 600;
    CHECK(ownership.OnCommand(vars.v, true) == 1u << CLOCK_ENTRY);

    // a change outside commands never makes an entry the mission's
    vars.v[CounterEntry(2)] = 700;
    ownership.Refresh(vars.v);
    CHECK(!ownership.IsOwned(CounterEntry(2)));

    // the counter of slot 1 stays the mission's while its offset stays (after the mission too)
    CHECK(ownership.IsOwned(CounterEntry(1)) && ownership.GetGeneration(CounterEntry(1)) == counterGeneration);

    // a reset (this game became the host, a restart): nothing owned, later generations keep growing
    const uint16_t beforeReset = ownership.GetGeneration(CLOCK_ENTRY);
    ownership.Reset(vars.v);
    CHECK(ownership.GetOwnedMask() == 0);
    CHECK(!ownership.HasChanged(vars.v));
    vars.v[CounterEntry(3)] = 800;
    CHECK(ownership.OnCommand(vars.v, true) == 1u << CounterEntry(3));
    CHECK(ownership.GetGeneration(CounterEntry(3)) > beforeReset);
}

void GenerationsNeverZero()
{
    COwnership ownership;
    SVars vars;
    ownership.Reset(vars.v);
    bool sawWrap = false;
    uint16_t last = 0;
    for (int i = 0; i < 140000; i++)  // 70,000 clocks: the 16-bit generation wraps once
    {
        vars.v[CLOCK_ENTRY] = (i % 2 == 0) ? 100u : 0u;
        ownership.OnCommand(vars.v, true);
        if (vars.v[CLOCK_ENTRY] != 0)
        {
            const uint16_t generation = ownership.GetGeneration(CLOCK_ENTRY);
            CHECK(generation != 0);
            CHECK(generation != last);
            sawWrap = sawWrap || generation < last;
            last = generation;
        }
    }
    CHECK(sawWrap);
}

// ---- send rates (host) ----

void SendRates()
{
    CSendLimiter limiter;
    SState state;
    CHECK(limiter.Check(state, 0) == eSendReason::FULL);  // nothing sent yet
    limiter.OnSent(state, 1000);
    CHECK(limiter.Check(state, 1000) == eSendReason::NONE);
    CHECK(limiter.Check(state, 99999) == eSendReason::NONE);

    // a counter added: at once
    state.counters[0] = MakeCounter(1, 100);
    CHECK(limiter.Check(state, 1001) == eSendReason::STRUCTURE);
    limiter.OnSent(state, 1001);

    // its value changes: at most 4 times a second
    state.counters[0].value = 90;
    CHECK(limiter.Check(state, 1100) == eSendReason::NONE);
    CHECK(limiter.Check(state, 1250) == eSendReason::NONE);
    CHECK(limiter.Check(state, 1251) == eSendReason::VALUES);
    limiter.OnSent(state, 1251);
    state.counters[0].value = 80;
    CHECK(limiter.Check(state, 1400) == eSendReason::NONE);
    CHECK(limiter.Check(state, 1501) == eSendReason::VALUES);
    limiter.OnSent(state, 1501);

    // a second value for type 2 counts as a value
    state.counters[0].type = MirrorHudWire::COUNTER_TYPE_TWO_VALUES;
    CHECK(limiter.Check(state, 1502) == eSendReason::STRUCTURE);  // the type is structure
    limiter.OnSent(state, 1502);
    state.counters[0].value2 = 5;
    CHECK(limiter.Check(state, 1751) == eSendReason::NONE);
    CHECK(limiter.Check(state, 1752) == eSendReason::VALUES);
    limiter.OnSent(state, 1752);

    // a clock: added at once, its value (every frame while it runs) once a second
    state.clock = MakeClock(2, 60000);
    CHECK(limiter.Check(state, 1800) == eSendReason::STRUCTURE);
    limiter.OnSent(state, 1800);
    state.clock.value = 59967;
    CHECK(limiter.Check(state, 1833) == eSendReason::NONE);
    CHECK(limiter.Check(state, 2799) == eSendReason::NONE);
    CHECK(limiter.Check(state, 2800) == eSendReason::VALUES);
    limiter.OnSent(state, 2800);

    // a counter update carries the clock: the clock waits a second from that packet
    state.counters[0].value = 70;
    state.clock.value = 58900;
    CHECK(limiter.Check(state, 3050) == eSendReason::VALUES);
    limiter.OnSent(state, 3050);
    state.clock.value = 58600;
    CHECK(limiter.Check(state, 3800) == eSendReason::NONE);
    CHECK(limiter.Check(state, 4050) == eSendReason::VALUES);
    limiter.OnSent(state, 4050);

    // structure: the paused flag, beep time, direction, key, colour, flash, a new generation, removal
    SState changed = state;
    changed.bPaused = true;
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    changed.clock.beepSeconds = 5;
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    changed.clock.bCountDown = false;
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    CopyKey(changed.clock.key, "OTHER");
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    changed.counters[0].colour = 4;
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    changed.counters[0].bFlash = false;
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    changed.counters[0].generation = 9;
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);
    changed = state;
    changed.counters[0] = SCounter{};
    CHECK(limiter.Check(changed, 4051) == eSendReason::STRUCTURE);

    // what an entry that is off holds doesn't count
    SState offValues = state;
    offValues.counters[3].value = 1234;
    CHECK(limiter.Check(offValues, 4051) == eSendReason::NONE);

    // a join snapshot sends the unchanged state
    limiter.ForceFull();
    CHECK(limiter.IsFullDue());
    CHECK(limiter.Check(state, 4052) == eSendReason::FULL);
    limiter.OnSent(state, 4052);
    CHECK(!limiter.IsFullDue());
    CHECK(limiter.Check(state, 4053) == eSendReason::NONE);

    // GetTickCount wraps
    CSendLimiter wrap;
    SState s;
    s.counters[1] = MakeCounter(3, 1);
    wrap.OnSent(s, 0xFFFFFF00u);
    s.counters[1].value = 2;
    CHECK(wrap.Check(s, 0xFFFFFFF0u) == eSendReason::NONE);
    CHECK(wrap.Check(s, 0x00000100u) == eSendReason::VALUES);  // 512 ms later
}

// ---- the clock's generation rule (receiver) ----

void ClockGenerationRule()
{
    CClockExpiry expiry;
    CHECK(!expiry.MayPlace(SClock{}));  // off
    CHECK(expiry.MayPlace(MakeClock(7, 500)));
    CHECK(!expiry.IsRevive(MakeClock(7, 500)));

    // it ran out here: late updates of that generation don't bring it back
    CHECK(expiry.OnGone(7, true));
    CHECK(expiry.GetRanOut() == 7);
    CHECK(!expiry.MayPlace(MakeClock(7, 0)));
    CHECK(!expiry.MayPlace(MakeClock(7, 500)));
    CHECK(!expiry.MayPlace(MakeClock(7, CLOCK_REVIVE_MIN_MS - 1)));
    // unless the host still has time (its game stood still while this copy ran on)
    CHECK(expiry.MayPlace(MakeClock(7, CLOCK_REVIVE_MIN_MS)));
    CHECK(expiry.IsRevive(MakeClock(7, CLOCK_REVIVE_MIN_MS)));
    // a new clock is placed whatever it shows
    CHECK(expiry.MayPlace(MakeClock(8, 100)));
    CHECK(!expiry.IsRevive(MakeClock(8, 100)));

    // a clock counting up never runs out
    CClockExpiry up;
    CHECK(!up.OnGone(9, false));
    CHECK(up.MayPlace(MakeClock(9, 0)));

    // cleared by a restart or a host change
    expiry.Clear();
    CHECK(expiry.MayPlace(MakeClock(7, 500)));
}

// ---- the paused flag and beep time a receiver puts back ----

void ClockOverride()
{
    // the host's clock is frozen; the local game had neither frozen it nor changed the beep time
    CClockOverride override;
    CHECK(!override.IsActive());
    override.Begin(false, 12);
    CHECK(override.IsActive());
    bool flag = override.PausedToWrite(false, true);
    CHECK(flag);
    CHECK(!override.HasLocalChange(flag));
    // a local script unfreezes the timer (it wants its own clock running later): noted, the host's flag is written back
    CHECK(override.HasLocalChange(false));
    flag = override.PausedToWrite(false, true);
    CHECK(flag);
    CHECK(!override.GetLocalPaused());
    // the host unfreezes
    flag = override.PausedToWrite(flag, false);
    CHECK(!flag);
    // a local script freezes it (freeze_onscreen_timer before its own display_onscreen_timer)
    CHECK(override.HasLocalChange(true));
    flag = override.PausedToWrite(true, false);
    CHECK(!flag);
    // the mirror's clock leaves the slot: the local game's flag and beep time come back
    CHECK(override.End(flag));
    CHECK(override.GetLocalBeep() == 12);
    CHECK(!override.IsActive());
    CHECK(!override.HasLocalChange(false));

    // the local game had frozen the timer and set its beep time before the mirror's clock came
    CClockOverride frozen;
    frozen.Begin(true, 5);
    flag = frozen.PausedToWrite(true, false);
    CHECK(!flag);
    // CMissionCleanup::Process of a local mission clears the flag meanwhile: the same value the mirror wrote, so the
    // command (00D8) tells it
    CHECK(!frozen.HasLocalChange(false));
    frozen.OnLocalMissionCleanup();
    CHECK(!frozen.End(false));
    CHECK(frozen.GetLocalBeep() == 5);
    frozen.OnLocalMissionCleanup();  // inactive: nothing
    CHECK(!frozen.IsActive());

    // ... and with the host's clock frozen the clean-up's write is seen as a change
    CClockOverride seen;
    seen.Begin(true, 9);
    flag = seen.PausedToWrite(true, true);
    CHECK(flag && !seen.HasLocalChange(flag));
    CHECK(seen.HasLocalChange(false));
    seen.OnLocalMissionCleanup();
    flag = seen.PausedToWrite(false, true);
    CHECK(flag);
    CHECK(!seen.End(flag));

    CClockOverride kept;
    kept.Begin(true, 7);
    kept.PausedToWrite(true, false);
    CHECK(kept.End(false));  // nothing local changed since: the local game's frozen timer comes back
}

// ---- where a receiver places the host's counters ----

void CounterSlots()
{
    using U = eSlotUse;
    int target[NUM_COUNTERS];

    {
        // all free: the host's slot
        const eSlotUse use[NUM_COUNTERS] = {U::FREE, U::FREE, U::FREE, U::FREE};
        const int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, false, false, true};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == 0 && target[1] == -1 && target[2] == -1 && target[3] == 3);
    }
    {
        // a local script holds the host's slot: the lowest free one
        const eSlotUse use[NUM_COUNTERS] = {U::LOCAL, U::FREE, U::FREE, U::FREE};
        const int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, false, false, false};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == 1);
    }
    {
        // host slots 0 and 1, the gym bench holds 1 (04F7 slot 2 = index 1): 0 stays, 1 goes to 2
        const eSlotUse use[NUM_COUNTERS] = {U::FREE, U::LOCAL, U::FREE, U::FREE};
        const int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, true, false, false};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == 0 && target[1] == 2);
    }
    {
        // host slots 0 and 1, a local script holds 0: 1 keeps its slot first, 0 takes the next free one
        const eSlotUse use[NUM_COUNTERS] = {U::LOCAL, U::FREE, U::FREE, U::FREE};
        const int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, true, false, false};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[1] == 1 && target[0] == 2);
    }
    {
        // every slot is a local script's: nothing shown, nothing written
        const eSlotUse use[NUM_COUNTERS] = {U::LOCAL, U::LOCAL, U::LOCAL, U::LOCAL};
        const int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, true, true, true};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == -1 && target[1] == -1 && target[2] == -1 && target[3] == -1);
    }
    {
        // a counter already shown stays where it is, even when its host slot came free
        const eSlotUse use[NUM_COUNTERS] = {U::FREE, U::FREE, U::MIRROR, U::FREE};
        const int placedAt[NUM_COUNTERS] = {2, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, false, false, false};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == 2);
    }
    {
        // a shown counter the host removed frees its slot for another
        const eSlotUse use[NUM_COUNTERS] = {U::LOCAL, U::LOCAL, U::MIRROR, U::LOCAL};
        const int placedAt[NUM_COUNTERS] = {2, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {false, false, false, true};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == -1 && target[3] == 2);
    }
    {
        // four counters, two slots held locally: two shown, two wait
        const eSlotUse use[NUM_COUNTERS] = {U::FREE, U::LOCAL, U::FREE, U::LOCAL};
        const int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
        const bool wanted[NUM_COUNTERS] = {true, true, true, true};
        PlanCounterSlots(use, placedAt, wanted, target);
        CHECK(target[0] == 0 && target[2] == 2 && target[1] == -1 && target[3] == -1);
    }
}

// ---- keys ----

void Keys()
{
    char key[KEY_SIZE];
    CopyKey(key, "SWE4_08");
    CHECK(std::strcmp(key, "SWE4_08") == 0);
    // an entry key buffer after an 8-character key holds whatever followed on the stack
    const char entry[KEY_SIZE] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
    CopyKey(key, entry);
    CHECK(std::strlen(key) == KEY_SIZE - 1);
    CHECK(MirrorHudWire::IsKeyTerminated(key));
    CopyKey(key, nullptr);
    CHECK(key[0] == '\0');
    CHECK(SameKey("TX_TIME", "TX_TIME") && !SameKey("TX_TIME", "TX_TIM"));
}

// ---- the wire format ----

int Write(SState& state, uint8_t* buffer, int size, bool& bOk)
{
    serialize::WriteStream stream(buffer, size);
    bOk = state.Serialize(stream);
    stream.Flush();
    return stream.GetBytesProcessed();
}

bool Read(SState& state, const uint8_t* buffer, int size)
{
    serialize::ReadStream stream(buffer, size);
    return state.Serialize(stream);
}

void WireFormat()
{
    alignas(4) uint8_t buffer[512]{};

    SState sent;
    sent.clock = MakeClock(11, 125000, "PIMP_T");
    sent.clock.bCountDown = false;
    sent.clock.beepSeconds = 5;
    sent.counters[0] = MakeCounter(12, -3, "TX_TOTL", 0);
    sent.counters[2] = MakeCounter(13, 40, "TWO", MirrorHudWire::COUNTER_TYPE_TWO_VALUES);
    sent.counters[2].value2 = 99;
    sent.counters[2].colour = 255;
    sent.counters[2].bFlash = false;
    sent.counters[3].value = 77;  // off: not sent
    sent.bPaused = true;

    bool bOk = false;
    const int bytes = Write(sent, buffer, sizeof(buffer), bOk);
    CHECK(bOk && bytes > 0 && bytes < 128);

    serialize::MeasureStream measure;
    SState measured = sent;
    CHECK(measured.Serialize(measure));
    CHECK(measure.GetBytesProcessed() <= bytes + 4);

    SState received;
    received.counters[1].value = 5;  // whatever was there before
    CHECK(Read(received, buffer, bytes));
    CHECK(received.clock.bOn && received.clock.generation == 11 && std::strcmp(received.clock.key, "PIMP_T") == 0);
    CHECK(!received.clock.bCountDown && received.clock.beepSeconds == 5 && received.clock.value == 125000);
    CHECK(received.counters[0].bOn && received.counters[0].value == -3 && received.counters[0].type == 0);
    CHECK(received.counters[0].value2 == 0);
    CHECK(!received.counters[1].bOn && received.counters[1].value == 0);
    CHECK(received.counters[2].value2 == 99 && received.counters[2].colour == 255 && !received.counters[2].bFlash);
    CHECK(!received.counters[3].bOn && received.counters[3].value == 0);
    CHECK(received.bPaused);
    SState expected = sent;
    expected.counters[3] = SCounter{};
    CHECK(SameState(received, expected));

    // nothing on: a few bits
    SState empty;
    const int emptyBytes = Write(empty, buffer, sizeof(buffer), bOk);
    CHECK(bOk && emptyBytes <= 4);
    SState emptyBack = sent;
    CHECK(Read(emptyBack, buffer, emptyBytes));
    CHECK(!emptyBack.clock.bOn && !emptyBack.counters[0].bOn && !emptyBack.counters[2].bOn && !emptyBack.bPaused);

    // the writer refuses what the reader would refuse or the bit packer would cut
    SState bad = sent;
    bad.counters[0].generation = 0;
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);
    bad = sent;
    std::memset(bad.clock.key, 'K', sizeof(bad.clock.key));  // no terminator
    Write(bad, buffer, sizeof(buffer), bOk);
    CHECK(!bOk);

    // a buffer cut short is refused
    const int full = Write(sent, buffer, sizeof(buffer), bOk);
    CHECK(bOk);
    SState cut;
    CHECK(!Read(cut, buffer, full / 2));
}

int main()
{
    OwnershipByComparison();
    GenerationsNeverZero();
    SendRates();
    ClockGenerationRule();
    ClockOverride();
    CounterSlots();
    Keys();
    WireFormat();
    std::printf("mirror_hud_test: all checks passed\n");
    return 0;
}
