#pragma once

#include <cstdint>
#include <cstring>

#include "serialize.h"

// Private build: the on-screen timers and counters of the host's mission as MISSION_MIRROR_HUD carries them
// (network/packets/mirror_hud.h; client/src/CMirrorHud.h; docs/design/mirror-layer.md 3.7 and 3.8). Plain data without
// game types, so the server and the standalone policy test (tests/mirror_hud_test.cpp) compile it too.
//
// The fields copy CUserDisplay::OnscnTimer (gta_sa.exe 1.0 US 0xBA1788, a COnscreenTimer of 0x154 bytes; plugin-sdk
// COnscreenTimer.h, COnscreenTimerEntry.h, COnscreenCounterEntry.h): the clock (0x40 bytes at +0x00) and four counters
// (0x44 bytes each at +0x40). A value is the content of the script variable an entry shows, in the variable's own unit
// (the clock counts milliseconds).
namespace MirrorHudWire
{
constexpr int NUM_COUNTERS = 4;

// The key buffers of COnscreenTimerEntry (+0x04) and COnscreenCounterEntry (+0x08) hold 10 bytes. The display commands
// read the key with ReadTextLabelFromScript(buffer, 8) and copy it with strncpy(entry key, buffer, 10) (1.0 US AddClock
// 0x44CD50 and AddCounter 0x44CDA0, relocated to 0x156E3E0 and 0x1561C50, dumpbin); GXT keys have at most 7 characters.
constexpr int KEY_SIZE = 10;

// COnscreenCounterEntry +0x12: 0 a number, 1 a bar, 2 "value / second value" (the second variable at +0x04). The display
// code reads the second variable for type 2 only (COnscreenCounterEntry::ProcessForDisplayCounter 0x44CA90, dumpbin).
constexpr uint16_t COUNTER_TYPE_TWO_VALUES = 2;

// a field written with exactly `bits` bits (values are copied raw, as the entries hold them)
template <typename Stream, typename T>
bool SerializeBitsOf(Stream& stream, T& field, int bits)
{
    uint64_t value = static_cast<uint32_t>(field);
    serialize_bits(stream, value, bits);
    if (Stream::IsReading)
    {
        field = static_cast<T>(static_cast<uint32_t>(value));
    }
    return true;
}

// a key with its terminator inside the buffer
inline bool IsKeyTerminated(const char (&key)[KEY_SIZE])
{
    return strnlen(key, KEY_SIZE) < KEY_SIZE;
}

// the clock (COnscreenTimerEntry)
struct SClock
{
    bool bOn = false;
    uint16_t generation = 0;   // the host's number for this clock, new for each clock its mission adds; never 0 when on
    char key[KEY_SIZE]{};      // +0x04, the label shown next to it (empty = none)
    bool bCountDown = true;    // +0x39 m_nTimerDirection: 0 counts up, anything else down
    uint32_t beepSeconds = 12; // +0x3C m_nClockBeepCountdownSecs (COnscreenTimer::Init 0x44CBC0 sets 12)
    int32_t value = 0;         // the clock's variable, ms

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, bOn);
        if (!bOn)
        {
            if (Stream::IsReading)
            {
                *this = SClock{};
            }
            return true;
        }
        if (Stream::IsWriting && (generation == 0 || !IsKeyTerminated(key)))
        {
            return false;  // the bit packer would write a truncated or unterminated field
        }
        if (!SerializeBitsOf(stream, generation, 16))
        {
            return false;
        }
        serialize_string(stream, key, KEY_SIZE);
        serialize_bool(stream, bCountDown);
        if (!SerializeBitsOf(stream, beepSeconds, 32) || !SerializeBitsOf(stream, value, 32))
        {
            return false;
        }
        return !(Stream::IsReading && generation == 0);
    }
};

// one counter (COnscreenCounterEntry)
struct SCounter
{
    bool bOn = false;
    uint16_t generation = 0;   // as SClock::generation
    char key[KEY_SIZE]{};      // +0x08
    uint16_t type = 0;         // +0x12, see COUNTER_TYPE_TWO_VALUES
    uint8_t colour = 0;        // +0x40 m_nColourId, a HUD colour index
    bool bFlash = false;       // +0x3F m_bFlashWhenFirstDisplayed
    int32_t value = 0;         // the counter's variable
    int32_t value2 = 0;        // the second variable, type 2 only (0 otherwise)

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, bOn);
        if (!bOn)
        {
            if (Stream::IsReading)
            {
                *this = SCounter{};
            }
            return true;
        }
        if (Stream::IsWriting && (generation == 0 || !IsKeyTerminated(key)))
        {
            return false;
        }
        if (!SerializeBitsOf(stream, generation, 16))
        {
            return false;
        }
        serialize_string(stream, key, KEY_SIZE);
        if (!SerializeBitsOf(stream, type, 16) || !SerializeBitsOf(stream, colour, 8))
        {
            return false;
        }
        serialize_bool(stream, bFlash);
        if (!SerializeBitsOf(stream, value, 32))
        {
            return false;
        }
        if (type == COUNTER_TYPE_TWO_VALUES)
        {
            if (!SerializeBitsOf(stream, value2, 32))
            {
                return false;
            }
        }
        else if (Stream::IsReading)
        {
            value2 = 0;
        }
        return !(Stream::IsReading && generation == 0);
    }
};

// everything the host's mission shows there; an entry the mission didn't add is off
struct SState
{
    SClock clock;
    SCounter counters[NUM_COUNTERS];  // by the host's slot
    bool bPaused = false;             // COnscreenTimer::m_bPaused (+0x151, freeze_onscreen_timer 0396), with a clock only

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, clock);
        for (SCounter& counter : counters)
        {
            serialize_object(stream, counter);
        }
        serialize_bool(stream, bPaused);
        return true;
    }
};
}  // namespace MirrorHudWire
