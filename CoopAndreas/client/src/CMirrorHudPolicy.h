#pragma once

#include <cstdint>
#include <cstring>

#include "network/mirror_hud_state.h"

// Private build: the rules of the mission mirror layer's timers and counters (CMirrorHud; docs/design/mirror-layer.md
// 3.7, package M4), without game or network code, so tests/mirror_hud_test.cpp checks them without GTA. The state they
// work on is the wire format of MISSION_MIRROR_HUD (shared/network/mirror_hud_state.h).
namespace MirrorHudPolicy
{
using MirrorHudWire::SClock;
using MirrorHudWire::SCounter;
using MirrorHudWire::SState;

constexpr int NUM_COUNTERS = MirrorHudWire::NUM_COUNTERS;
constexpr int KEY_SIZE = MirrorHudWire::KEY_SIZE;

// The five entries of CUserDisplay::OnscnTimer as the host compares them: [0] the clock, [1 + i] counter slot i.
constexpr int NUM_ENTRIES = 1 + NUM_COUNTERS;
constexpr int CLOCK_ENTRY = 0;
constexpr int CounterEntry(int slot) { return 1 + slot; }

// The design's rates (3.7): changed counter values go out at most 4 times a second, the clock's value (it changes every
// frame while it runs) once a second. Every packet carries the whole state, so each one corrects all values.
constexpr uint32_t COUNTER_VALUE_INTERVAL_MS = 250;
constexpr uint32_t CLOCK_VALUE_INTERVAL_MS = 1000;

// A clock that ran out on a receiver doesn't come back with a late update of the same generation (design 3.7). It does
// when the host still reports at least this much time: then the receiver's copy ran ahead of the host's, because the
// host's game stood still (menu, alt-tab) while the receiver's counted on, or the host's mission put a new clock on the
// same variable in the frame its old one ran out (the clock is cleared outside script commands, so no comparison sees
// the gap and the generation stays). A late update carries about one round trip and the packet buffer, far below this.
constexpr int32_t CLOCK_REVIVE_MIN_MS = 2000;

// copies a key with its terminator inside KEY_SIZE (an entry's key buffer may hold stack bytes after an 8-character key)
inline void CopyKey(char (&out)[KEY_SIZE], const char* in)
{
    std::memset(out, 0, sizeof(out));
    for (int i = 0; in != nullptr && i < KEY_SIZE - 1 && in[i] != '\0'; i++)
    {
        out[i] = in[i];
    }
}

inline bool SameKey(const char* a, const char* b) { return std::strncmp(a, b, KEY_SIZE) == 0; }

// Host (design 3.7): which entries the host's mission added. After every script command the five var offsets are
// compared with the copy of the last comparison. An entry whose offset changed to a non-zero value in a command of the
// mission family is owned and gets a new generation; any other change leaves it unowned: another script's command, the
// engine clearing a clock that ran out (COnscreenTimerEntry::Process, outside commands), a restart. An owned entry stays
// owned for as long as its offset stays, after the mission too (the host's clean-up clears it, design 3.11).
class COwnership
{
public:
    // the comparison after a command; returns a mask of the entries that became owned (bit = entry)
    unsigned int OnCommand(const uint32_t (&varIds)[NUM_ENTRIES], bool bFamily)
    {
        unsigned int becameOwned = 0;
        for (int i = 0; i < NUM_ENTRIES; i++)
        {
            if (varIds[i] == m_varIds[i])
            {
                continue;
            }
            m_varIds[i] = varIds[i];
            m_bOwned[i] = bFamily && varIds[i] != 0;
            m_generations[i] = m_bOwned[i] ? NextGeneration() : 0;
            if (m_bOwned[i])
            {
                becameOwned |= 1u << i;
            }
        }
        return becameOwned;
    }

    // a comparison outside script commands: whatever changed isn't the mission's
    void Refresh(const uint32_t (&varIds)[NUM_ENTRIES]) { OnCommand(varIds, false); }

    // nothing owned, the next comparison starts from varIds (this game became the host, a restart); generations go on, so
    // a receiver never takes a later entry for an earlier one
    void Reset(const uint32_t (&varIds)[NUM_ENTRIES])
    {
        std::memcpy(m_varIds, varIds, sizeof(m_varIds));
        std::memset(m_bOwned, 0, sizeof(m_bOwned));
        std::memset(m_generations, 0, sizeof(m_generations));
    }

    bool HasChanged(const uint32_t (&varIds)[NUM_ENTRIES]) const
    {
        return std::memcmp(varIds, m_varIds, sizeof(m_varIds)) != 0;
    }
    bool IsOwned(int entry) const { return m_bOwned[entry]; }
    uint16_t GetGeneration(int entry) const { return m_generations[entry]; }
    uint32_t GetVarId(int entry) const { return m_varIds[entry]; }
    unsigned int GetOwnedMask() const
    {
        unsigned int mask = 0;
        for (int i = 0; i < NUM_ENTRIES; i++)
        {
            mask |= m_bOwned[i] ? 1u << i : 0u;
        }
        return mask;
    }

private:
    uint16_t NextGeneration()
    {
        if (++m_nLastGeneration == 0)
        {
            m_nLastGeneration = 1;
        }
        return m_nLastGeneration;
    }

    uint32_t m_varIds[NUM_ENTRIES]{};
    bool m_bOwned[NUM_ENTRIES]{};
    uint16_t m_generations[NUM_ENTRIES]{};
    uint16_t m_nLastGeneration = 0;
};

// What makes a receiver add, re-add or change an entry (design 3.7): an entry added or removed (or a new generation in
// its place), its key, type, colour, flash, direction or beep time, and the paused flag. Values aside.
inline bool SameStructure(const SState& a, const SState& b)
{
    if (a.clock.bOn != b.clock.bOn || a.bPaused != b.bPaused)
    {
        return false;
    }
    if (a.clock.bOn &&
        (a.clock.generation != b.clock.generation || !SameKey(a.clock.key, b.clock.key) ||
            a.clock.bCountDown != b.clock.bCountDown || a.clock.beepSeconds != b.clock.beepSeconds))
    {
        return false;
    }
    for (int i = 0; i < NUM_COUNTERS; i++)
    {
        const SCounter& ca = a.counters[i];
        const SCounter& cb = b.counters[i];
        if (ca.bOn != cb.bOn)
        {
            return false;
        }
        if (ca.bOn && (ca.generation != cb.generation || !SameKey(ca.key, cb.key) || ca.type != cb.type ||
                          ca.colour != cb.colour || ca.bFlash != cb.bFlash))
        {
            return false;
        }
    }
    return true;
}

// the values of the counters both states show (for states of the same structure)
inline bool SameCounterValues(const SState& a, const SState& b)
{
    for (int i = 0; i < NUM_COUNTERS; i++)
    {
        if (a.counters[i].bOn && (a.counters[i].value != b.counters[i].value || a.counters[i].value2 != b.counters[i].value2))
        {
            return false;
        }
    }
    return true;
}

inline bool SameClockValue(const SState& a, const SState& b) { return !a.clock.bOn || a.clock.value == b.clock.value; }

enum class eSendReason : uint8_t
{
    NONE,
    FULL,       // nothing sent yet, a player joined, this game became the host, a restart
    STRUCTURE,  // SameStructure differs: at once
    VALUES,     // only values changed, at the design's rates
};

inline const char* ToString(eSendReason reason)
{
    switch (reason)
    {
        case eSendReason::NONE: return "none";
        case eSendReason::FULL: return "full";
        case eSendReason::STRUCTURE: return "structure";
        case eSendReason::VALUES: return "values";
    }
    return "?";
}

// Host: when the state goes out (design 3.7). Every packet carries the whole state; the intervals count from the last
// packet of any reason.
class CSendLimiter
{
public:
    eSendReason Check(const SState& current, uint32_t now) const
    {
        if (m_bFullDue || !m_bSent)
        {
            return eSendReason::FULL;
        }
        if (!SameStructure(current, m_lastSent))
        {
            return eSendReason::STRUCTURE;
        }
        const uint32_t since = now - m_nLastSend;
        if (!SameCounterValues(current, m_lastSent) && since >= COUNTER_VALUE_INTERVAL_MS)
        {
            return eSendReason::VALUES;
        }
        if (!SameClockValue(current, m_lastSent) && since >= CLOCK_VALUE_INTERVAL_MS)
        {
            return eSendReason::VALUES;
        }
        return eSendReason::NONE;
    }

    void OnSent(const SState& sent, uint32_t now)
    {
        m_lastSent = sent;
        m_nLastSend = now;
        m_bSent = true;
        m_bFullDue = false;
    }

    // the whole state again at the next check (it is sent in full anyway; this sends it even when nothing changed)
    void ForceFull() { m_bFullDue = true; }
    bool IsFullDue() const { return m_bFullDue || !m_bSent; }
    const SState& GetLastSent() const { return m_lastSent; }

private:
    SState m_lastSent{};
    uint32_t m_nLastSend = 0;
    bool m_bSent = false;
    bool m_bFullDue = true;
};

// Receiver: the generation rule for the clock (design 3.7). The receiver's engine counts its copy of the host's clock
// and clears it once it is below 0 (COnscreenTimerEntry::Process 0x44CB10); the host's corrections of that generation
// then don't bring it back unless they report at least CLOCK_REVIVE_MIN_MS. A new generation is a new clock.
class CClockExpiry
{
public:
    // The mirror's clock left its slot without the mirror clearing it, and the game didn't start over (a restart clears
    // every entry and is handled before this). One that counted down ran out; one that counts up never does (nothing but
    // the mirror and COnscreenTimer::Init clears it then). Returns whether it ran out.
    bool OnGone(uint16_t generation, bool bCountDown)
    {
        if (!bCountDown)
        {
            return false;
        }
        m_nRanOut = generation;
        return true;
    }
    void Clear() { m_nRanOut = 0; }
    bool MayPlace(const SClock& clock) const
    {
        return clock.bOn && (clock.generation != m_nRanOut || clock.value >= CLOCK_REVIVE_MIN_MS);
    }
    // a placement that only the revive allows
    bool IsRevive(const SClock& clock) const { return clock.bOn && clock.generation == m_nRanOut; }
    uint16_t GetRanOut() const { return m_nRanOut; }

private:
    uint16_t m_nRanOut = 0;  // 0 = none: generations are never 0
};

// Receiver: COnscreenTimer::m_bPaused is one flag for the whole timer and freezes only the clock. While the mirror's
// clock is in the clock slot the flag follows the host; what this game's own code sets meanwhile (freeze_onscreen_timer
// 0396 of a local script, CMissionCleanup::Process at the end of a local mission) is noted, and the local value and the
// local beep time are put back when the mirror's clock leaves the slot (local scripts come first).
class CClockOverride
{
public:
    void Begin(bool bPausedNow, uint32_t beepNow)
    {
        m_bActive = true;
        m_bLocalPaused = bPausedNow;
        m_bAppliedPaused = bPausedNow;
        m_nLocalBeep = beepNow;
    }
    // the flag to write while the mirror's clock is shown (the host's), after noting a local change since the last write
    bool PausedToWrite(bool bPausedNow, bool bHostPaused)
    {
        NoteLocal(bPausedNow);
        m_bAppliedPaused = bHostPaused;
        return bHostPaused;
    }
    // the mirror's clock left the slot: the flag to write back (the local game's)
    bool End(bool bPausedNow)
    {
        NoteLocal(bPausedNow);
        m_bActive = false;
        return m_bLocalPaused;
    }
    // A local mission ended: CMissionCleanup::Process cleared the flag (0x4687D9, from mission_has_finished 00D8 only), a
    // write that can't be told from the mirror's own when the host's flag is clear too.
    void OnLocalMissionCleanup()
    {
        if (m_bActive)
        {
            m_bLocalPaused = false;
        }
    }
    // the local game changed the flag since the last write
    bool HasLocalChange(bool bPausedNow) const { return m_bActive && bPausedNow != m_bAppliedPaused; }
    bool IsActive() const { return m_bActive; }
    uint32_t GetLocalBeep() const { return m_nLocalBeep; }
    bool GetLocalPaused() const { return m_bLocalPaused; }

private:
    void NoteLocal(bool bPausedNow)
    {
        if (bPausedNow != m_bAppliedPaused)
        {
            m_bLocalPaused = bPausedNow;
        }
    }

    bool m_bActive = false;
    bool m_bLocalPaused = false;
    bool m_bAppliedPaused = false;
    uint32_t m_nLocalBeep = 12;
};

// what holds a counter slot on a receiver
enum class eSlotUse : uint8_t
{
    FREE,    // var offset 0
    LOCAL,   // one of this game's own scripts
    MIRROR,  // a counter of the host's mission (placedAt says which)
};

// Receiver (design 3.7): where the host's counters go. Local scripts come first: a slot a local script holds is never
// used. A counter already shown stays where it is; one to place goes to its host slot when that is free, else to the
// lowest free slot, else it waits. use: the slots now; placedAt[h]: the slot showing host counter h (-1 = none); wanted[h]:
// host counter h should show. target[h]: the slot for host counter h, -1 = not shown (a shown one that isn't wanted any
// more is cleared and its slot is free for the others).
inline void PlanCounterSlots(const eSlotUse (&use)[NUM_COUNTERS], const int (&placedAt)[NUM_COUNTERS],
    const bool (&wanted)[NUM_COUNTERS], int (&target)[NUM_COUNTERS])
{
    bool taken[NUM_COUNTERS];
    for (int s = 0; s < NUM_COUNTERS; s++)
    {
        taken[s] = use[s] != eSlotUse::FREE;
    }
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        target[h] = -1;
        const int at = placedAt[h];
        if (at < 0 || at >= NUM_COUNTERS)
        {
            continue;
        }
        if (wanted[h])
        {
            target[h] = at;
        }
        else
        {
            taken[at] = false;
        }
    }
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        if (wanted[h] && target[h] < 0 && !taken[h])
        {
            target[h] = h;
            taken[h] = true;
        }
    }
    for (int h = 0; h < NUM_COUNTERS; h++)
    {
        if (!wanted[h] || target[h] >= 0)
        {
            continue;
        }
        for (int s = 0; s < NUM_COUNTERS; s++)
        {
            if (!taken[s])
            {
                target[h] = s;
                taken[s] = true;
                break;
            }
        }
    }
}
}  // namespace MirrorHudPolicy
