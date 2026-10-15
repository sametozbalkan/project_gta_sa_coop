#pragma once

// Private build: city unlock sync. STAT_CITY_UNLOCKED (181, "CitiesPassed" in the scripts) is raised only by the
// host's story missions, but every game reads its own copy: without this the other players still get 4 stars in a
// city the host has opened. The host sends its value when it changes and to every player who connects
// (UPDATE_CITY_UNLOCKED); the other players keep theirs equal to the host's.
class CCityUnlockSync
{
public:
    static void Process();
    static void SyncCurrentState();
    static void HandlePacket(Packets::World::UpdateCityUnlocked* pUpdateCityUnlocked);
};
