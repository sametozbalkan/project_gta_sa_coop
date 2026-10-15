#pragma once

#include <string>
#include <unordered_map>

#include "config.h"

class CConfigManager
{
public:
    inline static INIReader* ms_pReader = nullptr;
    inline static std::string ms_sConfigPath = "server-config.ini";

    inline static const std::unordered_map<std::string, uint16_t> ms_umDefaultConfig = {
        {"port", Config::DEFAULT_PORT}, {"maxplayers", Config::MAX_SERVER_PLAYERS}};

    // private build: listen address ("bind"), all interfaces by default; 127.0.0.1 keeps the server
    // reachable only from this PC (the CoopTunnel host and the host's own game)
    inline static const std::string ms_sDefaultBindAddress = "0.0.0.0";

    static void Init();
    static bool HasConfigExists();
    static void CreateConfig();
    static uint16_t GetConfigPort();
    static uint16_t GetConfigMaxPlayers();
    static std::string GetConfigBindAddress();
};