#pragma once

#include <cstdint>

// private build: upstream 0.3.0 + patch 100 and up, bumped with every protocol change so that mixed builds get
// "Version mismatch" instead of misreading packets (100 = radio sync, VEHICLE_RADIO_STATION)
#define COOPANDREAS_VERSION "0.3.100-alpha"
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
