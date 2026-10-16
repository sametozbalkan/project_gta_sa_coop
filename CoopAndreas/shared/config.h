#pragma once

#include <cstdint>

// private build: upstream 0.3.0 + patch 100 and up, bumped with every protocol change so that mixed builds get
// "Version mismatch" instead of misreading packets (100 = radio sync, VEHICLE_RADIO_STATION; 101 = wanted level opcodes
// from the host's missions and city unlock sync, UPDATE_CITY_UNLOCKED; 102 = NPC render/lifetime sync fix,
// unchanged wire format, new binary version so both players install the fix;
// 103 = remote player ped recovery (pool-checked network player peds, recreated when the game destroyed them,
// players taken out of a vehicle before it is deleted for the network) and the other players' state kept with the host's
// save, PLAYER_STATE_SNAPSHOT and PLAYER_STATE_RESTORE). 0.3.103 is the last version of this line (tag v0.3.103-alpha).
// From here our own numbering (CLAUDE.md, Fork changes, Version): 0.1.0 = group 1 with the mirror layer and the
// world-state registry (docs/design/README.md); WORLD_STATE replaced UPDATE_CITY_UNLOCKED at its position, so a 0.3.103
// game must never meet this one.
#define COOPANDREAS_VERSION "0.1.0-alpha"
struct Config
{
    static constexpr uint16_t DEFAULT_PORT = 6767;
    static constexpr uint16_t MAX_SERVER_PLAYERS = 8;
    static constexpr uint16_t MAX_SERVER_VEHICLES = 255;
    static constexpr uint16_t MAX_SERVER_PEDS = 255;
    static constexpr uint16_t MAX_NICKNAME_LENGTH = 24;
    static constexpr uint16_t MAX_CHAT_MESSAGE_LENGTH = 128;
    static constexpr uint32_t INTERP_BUFFER_DELAY_MS = 50;

#ifdef COOP_CLIENT
    static constexpr bool IsClient = true;
    static constexpr bool IsServer = false;
#endif
#ifdef COOP_SERVER
    static constexpr bool IsClient = false;
    static constexpr bool IsServer = true;
#endif
};
