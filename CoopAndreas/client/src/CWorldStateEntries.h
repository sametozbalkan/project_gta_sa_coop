#pragma once

#include "CWorldStateEntry.h"

// Private build: the world-state registry's entries (docs/design/world-state.md 3.2), registered by wire id in
// CWorldStateRegistry.cpp (CreateEntries). Each is declared here and defined in the CWorldStateEntries*.cpp file of its
// subsystem.

// 0: STAT_CITY_UNLOCKED (W1), CCityUnlockSync until 0.3.103. CWorldStateEntriesStory.cpp
class CWorldStateCityUnlock final : public CWorldStateEntry
{
public:
    uint8_t Id() const override { return 0; }
    const char* Name() const override { return "CityUnlock"; }
    uint32_t IntervalMs() const override { return 0; }
    // the other game's own MAIN and INTRO set 0 at a new game: put back at once, as CCityUnlockSync did
    bool SuspendDuringOwnMission() const override { return false; }
    bool Init() override { return true; }
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;
};

// 1: the maximum wanted level and the chaos cap that goes with it (W2). CWorldStateEntriesStory.cpp
class CWorldStateMaxWantedLevel final : public CWorldStateEntry
{
public:
    uint8_t Id() const override { return 1; }
    const char* Name() const override { return "MaxWantedLevel"; }
    uint32_t IntervalMs() const override { return 0; }
    // the other game's own MAIN sets 6, then 4 at a new game: put back at once
    bool SuspendDuringOwnMission() const override { return false; }
    bool Init() override { return true; }
    bool Capture(Blob& blob) override;
    void Apply(const Blob& desired, const Blob& local) override;
};
