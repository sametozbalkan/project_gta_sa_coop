#pragma once

#include "CWorldStatePolicy.h"
#include <cstdint>

// Private build: one entry of the world-state registry (CWorldStateRegistry.h; docs/design/world-state.md 3.1).
//
// An entry turns one piece of engine state into a canonical blob: the layout header of network/world_state.h, then a
// fixed layout with engine-driven and per-player bits masked out. It can also make the local engine match a desired
// blob. Equal blobs mean equal state, so one comparison serves both the host's change detection and the re-apply on the
// other games. Entries run on the game thread only.
//
// A new entry is its class (declared in CWorldStateEntries.h, defined in the CWorldStateEntries*.cpp file of its
// subsystem) plus one registration line in CWorldStateRegistry.cpp. Its wire id is never reused.
class CWorldStateEntry
{
public:
    using Blob = WorldStatePolicy::Blob;

    virtual ~CWorldStateEntry() = default;

    virtual uint8_t Id() const = 0;           // wire id, never reused
    virtual const char* Name() const = 0;     // diagnostics
    virtual uint32_t IntervalMs() const = 0;  // the host's poll and the other games' re-apply check; 0 = every frame
    // > 0: the blob holds that many bytes per record after the header, and the host can send only the records that
    // changed (RECORDS); 0: the blob is one value, sent whole
    virtual uint16_t RecordSize() const { return 0; }
    // true: the other games don't re-apply while a mission of their own runs (their new-game INITIAL, INITIL2, INTRO, or
    // a side mission), and put the host's state back once it has ended
    virtual bool SuspendDuringOwnMission() const { return true; }

    // once, when the registry starts (the game has loaded its data by then): resolve slots and names; false turns the
    // entry off on this game
    virtual bool Init() = 0;
    // the local state as a blob that starts with the layout header; false while it can't be read in full
    virtual bool Capture(Blob& blob) = 0;
    // make the local state match `desired` (the host's); `local` is what Capture made just before. Both have the same
    // layout header and size (the registry checks it), so the entry can index both alike.
    virtual void Apply(const Blob& desired, const Blob& local) = 0;
};
