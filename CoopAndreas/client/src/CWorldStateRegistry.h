#pragma once

#include <cstdio>

namespace Packets::World
{
class WorldState;
}

// Private build: the world-state registry (docs/design/world-state.md; decisions in docs/design/README.md).
//
// The state that sets the rules of the host's world is defined in one place, as entries (CWorldStateEntry.h,
// CWorldStateEntries.h): 0 the city unlock (CCityUnlockSync until 0.3.103), 1 the maximum wanted level. The host polls
// each entry and sends it (WORLD_STATE) when it changes, and in full when a player joins, when this game becomes the host
// and once its game has run for 2 s after a new game or a load. The other games keep their engine equal to the host's,
// also against their own scripts (MAIN, INITIAL, INTRO at a new game), whenever the apply gate is open: in a session
// with a host, their game running, and for most entries no mission of their own running. When the host leaves, the
// world stays as it is and nothing is enforced until the next host's state arrives.
//
// It learns about joins, host changes, disconnects and restarts from CSessionLifecycle (subscribed when it starts) and
// runs on the game thread: Process from Main.cpp, HandlePacket from PacketHandlers/world.cpp.
class CWorldStateRegistry
{
public:
    // every frame, from Main.cpp (gameProcessEvent), after CSessionLifecycle::Process
    static void Process();

    // WORLD_STATE from the server (the host's entries)
    static void HandlePacket(const Packets::World::WorldState& packet);

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
