#include "stdafx.h"
#include "CMirrorHud.h"
#include "CMirrorHudPolicy.h"
#include "CMissionMirror.h"
#include "CServerTime.h"
#include "CSessionLifecycle.h"
#include "network/packets/mirror_hud.h"
#include <CTimer.h>
#include <CUserDisplay.h>
#include <cstdarg>

// Private build: the mission mirror layer's timers and counters, see CMirrorHud.h.
//
// gta_sa.exe 1.0 US used below; the layout is plugin-sdk's (COnscreenTimer.h, COnscreenTimerEntry.h,
// COnscreenCounterEntry.h, CUserDisplay.cpp; the static_asserts pin it), the behaviour was read from the exe with dumpbin:
// - CUserDisplay::OnscnTimer 0xBA1788: the clock at +0x00 (var offset +0x00, key +0x04, m_bEnabled +0x38,
//   m_nTimerDirection +0x39, m_nClockBeepCountdownSecs +0x3C), four counters of 0x44 bytes at +0x40 (var offset +0x00,
//   second var offset +0x04, key +0x08, m_nType +0x12, m_bEnabled +0x3E, m_bFlashWhenFirstDisplayed +0x3F, m_nColourId
//   +0x40), m_bDisplay +0x150, m_bPaused +0x151. The script handlers load ecx = 0xBA1788 for every call (0x47C29D,
//   0x484CA2, 0x484CF7, 0x48C578, ...).
// - Every reader of a var offset adds it to ScriptSpace (0xA49960, plugin-sdk CTheScripts::ScriptSpace) in 32-bit
//   arithmetic: COnscreenTimerEntry::Process 0x44CB10 (mov ecx,[edi+0A49960h]; counts by the frame's time step and writes
//   back at 0x44CB58 / 0x44CBAD; once a clock counting down is below 0 it writes 0 there and clears var offset, key and
//   m_bEnabled, 0x44CB6B-0x44CB80; below the beep time it beeps unless the widescreen is on), ProcessForDisplayClock
//   0x44CA40, ProcessForDisplayCounter 0x44CA90 (the second variable only for type 2) and COnscreenTimer::ProcessForDisplay
//   (0x44CC20, a jump to its relocated copy 0x1569FD0: [eax+0A49960h] at 0x1569FF2 for the clock, 0x156A082 and 0x156A09F
//   for the counters). CHud::DrawMissionTimers 0x58B180 draws only the entries' texts, keys, colour and flash flags. So a
//   var offset of (an address in this DLL - 0xA49960) makes the engine read and write that address, mod 2^32.
// - AddClock 0x44CD50 (relocated 0x156E3E0) returns when the clock's var offset isn't 0, else stores the var offset, the
//   direction byte and up to 10 key bytes; it leaves the beep time alone. AddCounter 0x44CDA0 (relocated 0x1561C50)
//   returns when the slot's var offset isn't 0, else stores var offset, key, type, +0x04 = 0, flash = 1, colour = 3.
//   ClearClock 0x44CE60 (relocated 0x1565730) and ClearCounter 0x44CE80 (relocated 0x156AD90) clear only the entries
//   whose var offset equals the argument. COnscreenTimer::Init (0x44CBC0, relocated 0x15701D0) clears every entry, the
//   paused flag, and sets the beep time to 12; CUserDisplay::Init 0x571EE0 calls it, from CGame::ReInitGameObjectVariables
//   0x53BCF0 on every new game and load. COnscreenTimer::Process 0x44CD30 runs the clock unless CReplay::Mode is playback
//   or m_bPaused is set; m_bPaused is written by freeze_onscreen_timer 0396 (case 0x484370) and CMissionCleanup::Process
//   (0x4687D9).
// - The display commands (cases through CRunningScript::CommandHandlerTable 0x8A6168): 014E (case 0x47C284) calls
//   AddClock(var, nullptr, param != 0) at 0x47C2AC and 0x47C2C3, 03C3 (case 0x484C57) AddClock(var, key, param != 0) at
//   0x484CA7, 03C4 (case 0x484CB3) AddCounter(var, type, key, 0) at 0x484CFC, 04F7 (case 0x48C52F) AddCounter(var, type,
//   key, slot - 1) at 0x48C57D. Their var offsets are 16 bits (movzx), never one of this DLL's. Nothing else in the exe
//   calls AddClock or AddCounter (scan of every E8/E9 for the two targets); 07B5-07B8 (two-value counters) and 093C
//   (counter colour) are empty cases in 1.0 (0x46CBCB, 0x477BEE), so the type-2 and colour fields are copied as they are.
static_assert(offsetof(COnscreenTimerEntry, m_szDescriptionTextKey) == 0x04, "COnscreenTimerEntry key");
static_assert(offsetof(COnscreenTimerEntry, m_nTimerDirection) == 0x39, "COnscreenTimerEntry direction");
static_assert(offsetof(COnscreenTimerEntry, m_nClockBeepCountdownSecs) == 0x3C, "COnscreenTimerEntry beep time");
static_assert(offsetof(COnscreenCounterEntry, m_nMaxVarValue) == 0x04, "COnscreenCounterEntry second variable");
static_assert(offsetof(COnscreenCounterEntry, m_szDescriptionTextKey) == 0x08, "COnscreenCounterEntry key");
static_assert(offsetof(COnscreenCounterEntry, m_nType) == 0x12, "COnscreenCounterEntry type");
static_assert(offsetof(COnscreenCounterEntry, m_bFlashWhenFirstDisplayed) == 0x3F, "COnscreenCounterEntry flash");
static_assert(offsetof(COnscreenCounterEntry, m_nColourId) == 0x40, "COnscreenCounterEntry colour");
static_assert(offsetof(COnscreenTimer, m_aCounters) == 0x40, "COnscreenTimer counters");
static_assert(offsetof(COnscreenTimer, m_bPaused) == 0x151, "COnscreenTimer paused flag");
static_assert(sizeof(COnscreenTimerEntry::m_szDescriptionTextKey) == MirrorHudWire::KEY_SIZE, "clock key size");
static_assert(sizeof(COnscreenCounterEntry::m_szDescriptionTextKey) == MirrorHudWire::KEY_SIZE, "counter key size");

namespace
{
using MirrorHudPolicy::CClockExpiry;
using MirrorHudPolicy::CClockOverride;
using MirrorHudPolicy::CLOCK_ENTRY;
using MirrorHudPolicy::COwnership;
using MirrorHudPolicy::CounterEntry;
using MirrorHudPolicy::CSendLimiter;
using MirrorHudPolicy::eSendReason;
using MirrorHudPolicy::eSlotUse;
using MirrorHudPolicy::KEY_SIZE;
using MirrorHudPolicy::NUM_COUNTERS;
using MirrorHudPolicy::NUM_ENTRIES;
using MirrorHudWire::SClock;
using MirrorHudWire::SCounter;
using MirrorHudWire::SState;

// the functions the five display-command call sites must call (see the top)
constexpr uintptr_t ADD_CLOCK = 0x44CD50;
constexpr uintptr_t ADD_COUNTER = 0x44CDA0;
// mission_has_finished: its handler (case 0x46945E) is the only caller of CMissionCleanup::Process (call at 0x469471,
// for a script with m_bUseMissionCleanup +0xC6 only), which clears COnscreenTimer::m_bPaused at 0x4687D9 (ebx = 0 from
// 0x468567)
constexpr uint16_t MISSION_HAS_FINISHED = 0x00D8;
static_assert(offsetof(CRunningScript, m_bUseMissionCleanup) == 0xC6, "CRunningScript::m_bUseMissionCleanup");
// CTheScripts::ScriptSpace holds MAIN (200,000 bytes) and the mission block right after it (69,000 bytes at 0xA7A6A0;
// plugin-sdk CTheScripts.cpp)
constexpr uint32_t SCRIPT_SPACE_SIZE = 200000 + 69000;

bool ms_bStarted = false;
uint32_t ms_nLastFrame = 0;  // CTimer::m_FrameCounter at the last look; it starts again from 0 on every restart

// ---- host ----
COwnership ms_ownership;
CSendLimiter ms_limiter;

// ---- receivers ----
// the host's values; the mirrored entries' var offsets point here
struct SValues
{
    int32_t clock;
    int32_t counters[NUM_COUNTERS];
    int32_t counters2[NUM_COUNTERS];
};
SValues ms_values{};

SState ms_desired{};          // the host's last state
bool ms_bHasDesired = false;  // from the current host

struct SPlacedClock
{
    bool bPlaced = false;  // the mirror's clock is in the clock slot (as far as the mirror put it there)
    uint16_t generation = 0;
    bool bCountDown = true;
    bool bPending = false;  // wanted, but a local script holds the clock
};
SPlacedClock ms_clock;
CClockOverride ms_clockOverride;
CClockExpiry ms_expiry;

struct SPlacedCounter
{
    uint16_t generation = 0;  // of the counter last placed for this host slot
    bool bShown = false;      // that generation was placed once: placing it again (moved, restart) doesn't flash it
    bool bPending = false;    // wanted, no free slot
};
SPlacedCounter ms_counters[NUM_COUNTERS];

// --coopdiag (plain values; the diag thread reads them)
struct SDiag
{
    bool bStarted = false;
    int wrappers = 0;
    const char* role = "none";
    unsigned int ownedMask = 0;    // host: the entries owned now (bit 0 the clock, bit 1 + i counter slot i)
    unsigned int ownedTotal = 0;   // host: entries that became owned
    unsigned int sentFull = 0;
    unsigned int sentStructure = 0;
    unsigned int sentValues = 0;
    unsigned int snapshots = 0;
    unsigned int received = 0;
    unsigned int dropped = 0;
    unsigned int placements = 0;   // receiver: entries added to the timer
    unsigned int vacated = 0;      // receiver: entries moved away for a local script
    unsigned int ranOut = 0;       // receiver: the mirror's clock ran out here
    unsigned int revived = 0;      // receiver: ... and came back with a host value of CLOCK_REVIVE_MIN_MS or more
    unsigned int restarts = 0;
    bool bClockShown = false;
    bool bClockPending = false;
    int countersShown = 0;
    int countersPending = 0;
};
SDiag ms_diag;

// --coopdiag: CoopAndreas_mirror.log next to gta_sa.exe (the file CMissionMirror writes), at most 512 records of this
// module and 1 MB
void LogLine(const char* format, ...)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 512)
    {
        return;
    }

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    {
        return;
    }
    char* fileName = strrchr(path, '\\');
    if (!fileName)
    {
        return;
    }
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_mirror.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        fprintf(file, "t=%u frame=%u hud %s host=%d\n", GetTickCount(), CTimer::m_FrameCounter, text,
            CSessionLifecycle::IsHost());
        ++records;
    }
    fclose(file);
}

COnscreenTimer& Timer() { return CUserDisplay::OnscnTimer; }

// the var offset that makes the engine use *pValue (ScriptSpace + varId, mod 2^32)
uint32_t VarIdOf(const int32_t* pValue)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pValue) - reinterpret_cast<uintptr_t>(CTheScripts::ScriptSpace));
}
uint32_t ClockVar() { return VarIdOf(&ms_values.clock); }
uint32_t CounterVar(int hostSlot) { return VarIdOf(&ms_values.counters[hostSlot]); }
uint32_t CounterVar2(int hostSlot) { return VarIdOf(&ms_values.counters2[hostSlot]); }

// the host slot whose mirrored counter has this var offset, -1 = not one of the mirror's
int HostCounterOf(uint32_t varId)
{
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        if (varId == CounterVar(h))
        {
            return h;
        }
    }
    return -1;
}

void CaptureVarIds(uint32_t (&varIds)[NUM_ENTRIES])
{
    COnscreenTimer& timer = Timer();
    varIds[CLOCK_ENTRY] = timer.m_Clock.m_nVarId;
    for (int i = 0; i < NUM_COUNTERS; i++)
    {
        varIds[CounterEntry(i)] = timer.m_aCounters[i].m_nVarId;
    }
}

// a host script variable (the host's own entries always have 16-bit offsets into ScriptSpace)
int32_t ReadScriptVar(uint32_t varId)
{
    if (varId == 0 || varId > SCRIPT_SPACE_SIZE - sizeof(int32_t))
    {
        return 0;
    }
    int32_t value = 0;
    memcpy(&value, CTheScripts::ScriptSpace + varId, sizeof(value));
    return value;
}

const char* EntryName(int entry)
{
    static const char* const names[NUM_ENTRIES] = {"clock", "counter0", "counter1", "counter2", "counter3"};
    return entry >= 0 && entry < NUM_ENTRIES ? names[entry] : "?";
}

// ---- host ----

// what the host's mission shows now (the owned entries)
void BuildState(SState& state)
{
    state = SState{};
    COnscreenTimer& timer = Timer();
    if (ms_ownership.IsOwned(CLOCK_ENTRY))
    {
        const COnscreenTimerEntry& entry = timer.m_Clock;
        SClock& clock = state.clock;
        clock.bOn = true;
        clock.generation = ms_ownership.GetGeneration(CLOCK_ENTRY);
        MirrorHudPolicy::CopyKey(clock.key, entry.m_szDescriptionTextKey);
        clock.bCountDown = entry.m_nTimerDirection != 0;
        clock.beepSeconds = entry.m_nClockBeepCountdownSecs;
        clock.value = ReadScriptVar(entry.m_nVarId);
        state.bPaused = timer.m_bPaused;
    }
    for (int i = 0; i < NUM_COUNTERS; i++)
    {
        if (!ms_ownership.IsOwned(CounterEntry(i)))
        {
            continue;
        }
        const COnscreenCounterEntry& entry = timer.m_aCounters[i];
        SCounter& counter = state.counters[i];
        counter.bOn = true;
        counter.generation = ms_ownership.GetGeneration(CounterEntry(i));
        MirrorHudPolicy::CopyKey(counter.key, entry.m_szDescriptionTextKey);
        counter.type = entry.m_nType;
        counter.colour = entry.m_nColourId;
        counter.bFlash = entry.m_bFlashWhenFirstDisplayed;
        counter.value = ReadScriptVar(entry.m_nVarId);
        counter.value2 =
            entry.m_nType == MirrorHudWire::COUNTER_TYPE_TWO_VALUES ? ReadScriptVar(entry.m_nMaxVarValue) : 0;
    }
}

// Packets sent before this game's first server-time answer carry the PC's uptime as their stamp and wait in the other
// games' packet buffers (CWorldStateRegistry, the same rule), so nothing goes out before it.
bool CanSend()
{
    return CSessionLifecycle::IsAuthenticated() && CSessionLifecycle::IsHost() && CServerTime::m_serverTimeOffset != 0;
}

void Send(const SState& state, eSendReason reason)
{
    static Packets::Mirror::MirrorHud packet{};
    packet.state = state;
    GetPacketFactory().Send(packet);
    ms_limiter.OnSent(state, GetTickCount());

    switch (reason)
    {
        case eSendReason::FULL: ++ms_diag.sentFull; break;
        case eSendReason::STRUCTURE: ++ms_diag.sentStructure; break;
        default: ++ms_diag.sentValues; break;
    }
    if (reason != eSendReason::VALUES)
    {
        LogLine("event=send reason=%s clock=%d/%u/%.9s/%d counters=%d/%u %d/%u %d/%u %d/%u paused=%d",
            MirrorHudPolicy::ToString(reason), state.clock.bOn, state.clock.generation, state.clock.key, state.clock.value,
            state.counters[0].bOn, state.counters[0].generation, state.counters[1].bOn, state.counters[1].generation,
            state.counters[2].bOn, state.counters[2].generation, state.counters[3].bOn, state.counters[3].generation,
            state.bPaused);
    }
}

// once per script frame, and for a join snapshot (bSnapshot: also when no other player was in before)
void ProcessHost(bool bSnapshot)
{
    uint32_t varIds[NUM_ENTRIES];
    CaptureVarIds(varIds);
    ms_ownership.Refresh(varIds);  // a clock that ran out was cleared by the engine, outside any command
    ms_diag.ownedMask = ms_ownership.GetOwnedMask();

    // alone, nothing goes out: the join snapshot brings the state to whoever connects
    if (!CanSend() || (!bSnapshot && CNetworkPlayerManager::m_pPlayers.empty()))
    {
        return;
    }
    SState state;
    BuildState(state);
    const eSendReason reason = ms_limiter.Check(state, GetTickCount());
    if (reason != eSendReason::NONE)
    {
        Send(state, reason);
    }
}

// nothing owned, the whole state again (this game became the host, a restart or a load of the host's game, a disconnect)
void ResetHost()
{
    uint32_t varIds[NUM_ENTRIES];
    CaptureVarIds(varIds);
    ms_ownership.Reset(varIds);
    ms_limiter.ForceFull();
    ms_diag.ownedMask = 0;
}

// ---- receivers ----

// a timer entry of this game's own scripts is on screen
bool AnyLocalEntry()
{
    COnscreenTimer& timer = Timer();
    if (timer.m_Clock.m_nVarId != 0 && timer.m_Clock.m_nVarId != ClockVar())
    {
        return true;
    }
    for (const COnscreenCounterEntry& entry : timer.m_aCounters)
    {
        if (entry.m_nVarId != 0 && HostCounterOf(entry.m_nVarId) < 0)
        {
            return true;
        }
    }
    return false;
}

// The host's mission text table for a label that isn't in the main table (CMissionMirror's receiver guard; the HUD looks
// the label up with CText::Get every frame it draws). Not while a local script shows a timer or counter of its own: that
// script's labels need its own table.
void PrepareKey(const char* key)
{
    if (key[0] != '\0' && !AnyLocalEntry())
    {
        CMissionMirror::PrepareTextKey(key);
    }
}

// The local game started over: COnscreenTimer::Init cleared every entry, the mirror's too, and reset the beep time and
// the paused flag, so nothing of the mirror's is in place any more and nothing is to be put back. The host's state stays
// wanted and is placed again.
void CheckLocalRestart()
{
    const uint32_t frame = CTimer::m_FrameCounter;
    if (frame < ms_nLastFrame)
    {
        if (ms_clock.bPlaced)
        {
            ms_clock.bPlaced = false;
            ms_clockOverride.End(Timer().m_bPaused);
        }
        ms_expiry.Clear();
        ++ms_diag.restarts;
        LogLine("event=local-restart frame=%u->%u", ms_nLastFrame, frame);
    }
    ms_nLastFrame = frame;
}

// the local game's beep time and paused flag back, once the mirror's clock left the clock slot
void RestoreLocalClockSettings()
{
    if (!ms_clockOverride.IsActive())
    {
        return;
    }
    COnscreenTimer& timer = Timer();
    timer.m_Clock.m_nClockBeepCountdownSecs = ms_clockOverride.GetLocalBeep();
    timer.m_bPaused = ms_clockOverride.End(timer.m_bPaused);
}

void ReleaseClock(const char* reason)
{
    COnscreenTimer& timer = Timer();
    if (timer.m_Clock.m_nVarId == ClockVar())
    {
        timer.ClearClock(ClockVar());
    }
    RestoreLocalClockSettings();
    ms_clock.bPlaced = false;
    LogLine("event=release entry=clock gen=%u reason=%s", ms_clock.generation, reason);
}

void ApplyClock(bool bNewValues)
{
    COnscreenTimer& timer = Timer();
    COnscreenTimerEntry& entry = timer.m_Clock;
    const uint32_t var = ClockVar();

    if (ms_clock.bPlaced && entry.m_nVarId != var)
    {
        // gone without the mirror clearing it (CheckLocalRestart handles a restart)
        ms_clock.bPlaced = false;
        RestoreLocalClockSettings();
        if (ms_expiry.OnGone(ms_clock.generation, ms_clock.bCountDown))
        {
            ++ms_diag.ranOut;
            LogLine("event=ran-out gen=%u", ms_clock.generation);
        }
    }

    const SClock& want = ms_desired.clock;
    const bool bWanted = ms_bHasDesired && ms_expiry.MayPlace(want);
    if (ms_clock.bPlaced && (!bWanted || want.generation != ms_clock.generation))
    {
        ReleaseClock(bWanted ? "replaced" : "removed");
    }

    if (ms_clock.bPlaced)
    {
        // the engine counts it between the host's corrections
        if (bNewValues)
        {
            ms_values.clock = want.value;
            entry.m_nClockBeepCountdownSecs = want.beepSeconds;
        }
        timer.m_bPaused = ms_clockOverride.PausedToWrite(timer.m_bPaused, ms_desired.bPaused);
        return;
    }
    if (!bWanted)
    {
        ms_clock.bPending = false;
        return;
    }
    if (entry.m_nVarId != 0)
    {
        // a local script holds the clock: it comes first
        if (!ms_clock.bPending)
        {
            ms_clock.bPending = true;
            LogLine("event=pending entry=clock gen=%u reason=local-clock", want.generation);
        }
        return;
    }

    const bool bRevive = ms_expiry.IsRevive(want);
    ms_clockOverride.Begin(timer.m_bPaused, entry.m_nClockBeepCountdownSecs);
    ms_values.clock = want.value;
    char key[KEY_SIZE];
    MirrorHudPolicy::CopyKey(key, want.key);
    timer.AddClock(var, key, want.bCountDown);
    if (entry.m_nVarId != var)
    {
        ms_clockOverride.End(timer.m_bPaused);
        LogLine("event=error what=add-clock-refused gen=%u", want.generation);
        return;
    }
    entry.m_nClockBeepCountdownSecs = want.beepSeconds;
    timer.m_bPaused = ms_clockOverride.PausedToWrite(timer.m_bPaused, ms_desired.bPaused);
    ms_clock.bPlaced = true;
    ms_clock.generation = want.generation;
    ms_clock.bCountDown = want.bCountDown;
    ms_clock.bPending = false;
    ++ms_diag.placements;
    if (bRevive)
    {
        ++ms_diag.revived;
    }
    PrepareKey(key);
    LogLine("event=place entry=clock gen=%u key=%.9s value=%d down=%d beep=%u paused=%d revive=%d", want.generation, key,
        want.value, want.bCountDown, want.beepSeconds, ms_desired.bPaused, bRevive);
}

void PlaceCounter(int hostSlot, int slot)
{
    COnscreenTimer& timer = Timer();
    const SCounter& want = ms_desired.counters[hostSlot];
    SPlacedCounter& placed = ms_counters[hostSlot];
    const bool bAgain = placed.bShown && placed.generation == want.generation;  // moved, or back after a restart

    ms_values.counters[hostSlot] = want.value;
    ms_values.counters2[hostSlot] = want.value2;
    char key[KEY_SIZE];
    MirrorHudPolicy::CopyKey(key, want.key);
    timer.AddCounter(static_cast<int>(CounterVar(hostSlot)), static_cast<short>(want.type), key,
        static_cast<unsigned short>(slot));
    COnscreenCounterEntry& entry = timer.m_aCounters[slot];
    if (entry.m_nVarId != CounterVar(hostSlot))
    {
        LogLine("event=error what=add-counter-refused host=%d slot=%d gen=%u", hostSlot, slot, want.generation);
        return;
    }
    entry.m_nMaxVarValue = want.type == MirrorHudWire::COUNTER_TYPE_TWO_VALUES ? CounterVar2(hostSlot) : 0;
    entry.m_nColourId = want.colour;
    entry.m_bFlashWhenFirstDisplayed = bAgain ? false : want.bFlash;
    placed.generation = want.generation;
    placed.bShown = true;
    placed.bPending = false;
    ++ms_diag.placements;
    PrepareKey(key);
    LogLine("event=place entry=counter%d slot=%d gen=%u key=%.9s type=%u value=%d again=%d", hostSlot, slot,
        want.generation, key, want.type, want.value, bAgain);
}

void ApplyCounters(bool bNewValues)
{
    COnscreenTimer& timer = Timer();

    // what holds each slot now
    eSlotUse use[NUM_COUNTERS];
    int placedAt[NUM_COUNTERS] = {-1, -1, -1, -1};
    for (int s = 0; s < NUM_COUNTERS; s++)
    {
        const uint32_t varId = timer.m_aCounters[s].m_nVarId;
        const int h = HostCounterOf(varId);
        use[s] = varId == 0 ? eSlotUse::FREE : (h >= 0 ? eSlotUse::MIRROR : eSlotUse::LOCAL);
        if (h >= 0)
        {
            placedAt[h] = s;
        }
    }

    bool wanted[NUM_COUNTERS];
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        const SCounter& want = ms_desired.counters[h];
        wanted[h] = ms_bHasDesired && want.bOn;
        SPlacedCounter& placed = ms_counters[h];
        if (placedAt[h] >= 0 && (!wanted[h] || want.generation != placed.generation))
        {
            // the host removed it or put another counter in its slot
            timer.ClearCounter(CounterVar(h));
            use[placedAt[h]] = eSlotUse::FREE;
            placedAt[h] = -1;
            LogLine("event=release entry=counter%d gen=%u reason=%s", h, placed.generation,
                wanted[h] ? "replaced" : "removed");
        }
        if (!wanted[h])
        {
            placed.bPending = false;
        }
        else if (placedAt[h] >= 0)
        {
            // shown: the engine reads the value from the buffer
            COnscreenCounterEntry& entry = timer.m_aCounters[placedAt[h]];
            entry.m_nType = want.type;
            entry.m_nColourId = want.colour;
            entry.m_nMaxVarValue = want.type == MirrorHudWire::COUNTER_TYPE_TWO_VALUES ? CounterVar2(h) : 0;
            if (bNewValues)
            {
                ms_values.counters[h] = want.value;
                ms_values.counters2[h] = want.value2;
            }
        }
    }

    int target[NUM_COUNTERS];
    MirrorHudPolicy::PlanCounterSlots(use, placedAt, wanted, target);
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        if (!wanted[h] || placedAt[h] >= 0)
        {
            continue;
        }
        if (target[h] < 0)
        {
            // no free slot: local scripts and the mirror's other counters hold them all
            if (!ms_counters[h].bPending)
            {
                ms_counters[h].bPending = true;
                LogLine("event=pending entry=counter%d gen=%u reason=no-free-slot", h, ms_desired.counters[h].generation);
            }
            continue;
        }
        PlaceCounter(h, target[h]);
    }
}

void UpdateReceiverDiag()
{
    ms_diag.bClockShown = ms_clock.bPlaced;
    ms_diag.bClockPending = ms_clock.bPending;
    int shown = 0;
    int pending = 0;
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        for (const COnscreenCounterEntry& entry : Timer().m_aCounters)
        {
            if (entry.m_nVarId == CounterVar(h))
            {
                shown++;
                break;
            }
        }
        pending += ms_counters[h].bPending ? 1 : 0;
    }
    ms_diag.countersShown = shown;
    ms_diag.countersPending = pending;
}

void Apply(bool bNewValues)
{
    ApplyClock(bNewValues);
    ApplyCounters(bNewValues);
    UpdateReceiverDiag();
}

// a host change or the end of the session: the mirror's entries go, a new host starts empty
void DropMirrored(const char* reason)
{
    const bool bHadState = ms_bHasDesired || ms_clock.bPlaced;
    if (ms_clock.bPlaced)
    {
        ReleaseClock(reason);
    }
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        Timer().ClearCounter(CounterVar(h));
        ms_counters[h] = SPlacedCounter{};
    }
    ms_desired = SState{};
    ms_bHasDesired = false;
    ms_clock = SPlacedClock{};
    ms_expiry.Clear();
    UpdateReceiverDiag();
    if (bHadState)
    {
        LogLine("event=drop-all reason=%s", reason);
    }
}

// --- local scripts come first: the wrappers at the display commands' Add* call sites ---

// a local script's AddClock (014E, 03C3) is about to run: the clock slot is its if the mirror's clock holds it; the
// mirror's clock waits until the local one is gone
void VacateClockForLocalScript()
{
    if (!ms_clock.bPlaced || Timer().m_Clock.m_nVarId != ClockVar())
    {
        return;
    }
    ReleaseClock("local-script");
    ms_clock.bPending = true;
    ++ms_diag.vacated;
    UpdateReceiverDiag();
}

// a local script's AddCounter (03C4, 04F7) for this slot is about to run: a mirrored counter there moves to another
// free slot at the end of this script frame (Process), or waits for one
void VacateCounterSlotForLocalScript(uint32_t slot)
{
    if (slot >= static_cast<uint32_t>(NUM_COUNTERS))
    {
        return;
    }
    const int h = HostCounterOf(Timer().m_aCounters[slot].m_nVarId);
    if (h < 0)
    {
        return;
    }
    Timer().ClearCounter(CounterVar(h));
    ++ms_diag.vacated;
    LogLine("event=vacate entry=counter%d slot=%u for=local-script", h, slot);
    UpdateReceiverDiag();
}

// Same contract as the engine functions: thiscall, the callee pops the arguments (ret 0Ch / ret 10h in the relocated
// copies). The arguments are read as whole stack slots: the engine reads only the direction byte, the type word and the
// slot word.
void __fastcall COnscreenTimer__AddClock_Hook(COnscreenTimer* pTimer, SKIP_EDX, uint32_t varId, char* gxt, uint32_t direction)
{
    VacateClockForLocalScript();
    pTimer->AddClock(varId, gxt, (direction & 0xFF) != 0);
}

void __fastcall COnscreenTimer__AddCounter_Hook(
    COnscreenTimer* pTimer, SKIP_EDX, int varId, uint32_t type, char* gxt, uint32_t slot)
{
    VacateCounterSlotForLocalScript(slot & 0xFFFF);
    pTimer->AddCounter(varId, static_cast<short>(type & 0xFFFF), gxt, static_cast<unsigned short>(slot & 0xFFFF));
}

// a call site is redirected only while it holds `call target` (E8 rel32)
bool RedirectCallChecked(uintptr_t site, uintptr_t target, void* hook)
{
    const uint8_t* code = reinterpret_cast<const uint8_t*>(site);
    int32_t relative = 0;
    memcpy(&relative, code + 1, sizeof(relative));
    if (code[0] != 0xE8 || site + 5 + static_cast<uintptr_t>(relative) != target)
    {
        logger::warn("[MirrorHud] unexpected code at 0x%X, a local script's timer may not show while the host's does",
            site);
        LogLine("event=error what=unexpected-code site=%08X", site);
        return false;
    }
    patch::RedirectCall(static_cast<int>(site), hook);
    return true;
}

void InstallWrappers()
{
    void* const addClock = reinterpret_cast<void*>(COnscreenTimer__AddClock_Hook);
    void* const addCounter = reinterpret_cast<void*>(COnscreenTimer__AddCounter_Hook);
    int installed = 0;
    installed += RedirectCallChecked(0x47C2AC, ADD_CLOCK, addClock) ? 1 : 0;      // display_onscreen_timer 014E, down
    installed += RedirectCallChecked(0x47C2C3, ADD_CLOCK, addClock) ? 1 : 0;      // display_onscreen_timer 014E, up
    installed += RedirectCallChecked(0x484CA7, ADD_CLOCK, addClock) ? 1 : 0;      // display_onscreen_timer_with_string 03C3
    installed += RedirectCallChecked(0x484CFC, ADD_COUNTER, addCounter) ? 1 : 0;  // display_onscreen_counter_with_string 03C4
    installed += RedirectCallChecked(0x48C57D, ADD_COUNTER, addCounter) ? 1 : 0;  // display_nth_onscreen_counter_with_string 04F7
    ms_diag.wrappers = installed;
}

void OnSessionEvent(const SSessionEvent& event)
{
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            // the join snapshot (design 3.11): the whole state, which changes nothing for the players already in
            if (CSessionLifecycle::IsHost())
            {
                ms_limiter.ForceFull();
                ++ms_diag.snapshots;
                ProcessHost(true);
                LogLine("event=snapshot player=%d takeover=%d sent=%d", event.playerId, event.bTakeover,
                    !ms_limiter.IsFullDue());
            }
            break;
        case eSessionEvent::HOST_CHANGED:
            DropMirrored("host-changed");
            ResetHost();
            LogLine("event=host-changed host=%d->%d local=%d", event.oldHostId, event.newHostId, event.bIsLocal);
            break;
        case eSessionEvent::DISCONNECTED:
            DropMirrored("disconnected");
            ResetHost();
            break;
        case eSessionEvent::GAME_RESTARTED:
            // the receivers' entries were cleared by the restart (CheckLocalRestart); the host's owned ones too
            ms_expiry.Clear();
            if (CSessionLifecycle::IsHost())
            {
                ResetHost();
                LogLine("event=host-restart loaded=%d", event.bLoaded);
            }
            break;
        case eSessionEvent::SAVE_LOADED:
            if (CSessionLifecycle::IsHost())
            {
                ResetHost();
            }
            break;
        default:
            break;
    }
}

// Sets the module up on first use: the first script command, script frame or packet. Not from CCore::Init (CCore.cpp
// isn't this module's; CWorldStateRegistry starts the same way). Until then there is nothing to mirror, and the session
// state it needs is read from CSessionLifecycle.
void EnsureStarted()
{
    if (ms_bStarted)
    {
        return;
    }
    ms_bStarted = true;
    InstallWrappers();
    CSessionLifecycle::Subscribe(OnSessionEvent);
    ResetHost();
    ms_nLastFrame = CTimer::m_FrameCounter;
    ms_diag.bStarted = true;
    LogLine("event=started wrappers=%d values=%p clockVar=%08X", ms_diag.wrappers, static_cast<void*>(&ms_values),
        ClockVar());
}

// host: ownership by comparison (design 3.7), after every command
void CompareAfterCommand(CRunningScript* pScript, uint16_t opcode)
{
    uint32_t varIds[NUM_ENTRIES];
    CaptureVarIds(varIds);
    if (!ms_ownership.HasChanged(varIds))
    {
        return;
    }
    const unsigned int becameOwned = ms_ownership.OnCommand(varIds, CMissionMirror::IsFamilyScript(pScript));
    ms_diag.ownedMask = ms_ownership.GetOwnedMask();
    for (int i = 0; i < NUM_ENTRIES; i++)
    {
        if (becameOwned & (1u << i))
        {
            ++ms_diag.ownedTotal;
            LogLine("event=owned entry=%s gen=%u var=%u script=%.8s opcode=%04X", EntryName(i),
                ms_ownership.GetGeneration(i), ms_ownership.GetVarId(i), pScript != nullptr ? pScript->m_szName : "",
                opcode & 0x7FFF);
        }
    }
}

// receivers: a local command froze or unfroze the timer while the mirror's clock is shown (freeze_onscreen_timer 0396,
// the clean-up of a local mission's mission_has_finished 00D8): noted for later, the host's flag stays
void NoteLocalPausedChange(CRunningScript* pScript, uint16_t opcode)
{
    if (!ms_clock.bPlaced)
    {
        return;
    }
    COnscreenTimer& timer = Timer();
    if ((opcode & 0x7FFF) == MISSION_HAS_FINISHED && pScript != nullptr && pScript->m_bUseMissionCleanup)
    {
        ms_clockOverride.OnLocalMissionCleanup();
    }
    if (ms_clockOverride.HasLocalChange(timer.m_bPaused))
    {
        timer.m_bPaused = ms_clockOverride.PausedToWrite(timer.m_bPaused, ms_desired.bPaused);
    }
}

void LogReceived(int sender)
{
    const SState& s = ms_desired;
    LogLine("event=receive sender=%d clock=%d/%u/%.9s/%d counters=%d/%u %d/%u %d/%u %d/%u paused=%d", sender, s.clock.bOn,
        s.clock.generation, s.clock.key, s.clock.value, s.counters[0].bOn, s.counters[0].generation, s.counters[1].bOn,
        s.counters[1].generation, s.counters[2].bOn, s.counters[2].generation, s.counters[3].bOn, s.counters[3].generation,
        s.bPaused);
}
}  // namespace

void CMirrorHud::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    EnsureStarted();
    if (CSessionLifecycle::IsHost())
    {
        CompareAfterCommand(pScript, opcode);
    }
    else
    {
        NoteLocalPausedChange(pScript, opcode);
    }
}

void CMirrorHud::Process()
{
    EnsureStarted();
    CheckLocalRestart();
    if (CSessionLifecycle::IsHost())
    {
        ms_diag.role = "host";
        ProcessHost(false);
        return;
    }
    ms_diag.role = CSessionLifecycle::HasHost() ? "other" : "none";
    if (ms_bHasDesired)
    {
        Apply(false);
    }
}

void CMirrorHud::HandlePacket(const Packets::Mirror::MirrorHud& packet)
{
    EnsureStarted();
    CheckLocalRestart();

    // only the current host's state counts: a former host's packets still in the packet buffer are dropped (they wait
    // 50 ms there, its PLAYER_DISCONNECTED and the next PLAYER_ASSIGN_HOST don't)
    const int sender = packet.playerid.value;
    if (CSessionLifecycle::IsHost() || !CSessionLifecycle::HasHost() || sender != CSessionLifecycle::GetHostId())
    {
        ++ms_diag.dropped;
        LogLine("event=drop sender=%d reason=%s", sender,
            CSessionLifecycle::IsHost() ? "this-game-is-host" : (CSessionLifecycle::HasHost() ? "not-from-host" : "no-host"));
        return;
    }

    const bool bStructure = !ms_bHasDesired || !MirrorHudPolicy::SameStructure(packet.state, ms_desired);
    ms_desired = packet.state;
    ms_bHasDesired = true;
    ++ms_diag.received;
    if (bStructure)
    {
        LogReceived(sender);
    }
    Apply(true);
}

void CMirrorHud::WriteDiagSummary(FILE* file)
{
    const SDiag& d = ms_diag;
    fprintf(file,
        "  mirror-hud: started=%d wrappers=%d/5 role=%s owned=%02X ownedTotal=%u sent=F%u/S%u/V%u snapshots=%u | recv=%u "
        "dropped=%u clock=%d pending=%d counters=%d pending=%d placements=%u vacated=%u ranOut=%u revived=%u "
        "restarts=%u\n",
        d.bStarted, d.wrappers, d.role, d.ownedMask, d.ownedTotal, d.sentFull, d.sentStructure, d.sentValues, d.snapshots,
        d.received, d.dropped, d.bClockShown, d.bClockPending, d.countersShown, d.countersPending, d.placements,
        d.vacated, d.ranOut, d.revived, d.restarts);
}
