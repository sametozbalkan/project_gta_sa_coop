#include "ConfigManager.h"

void CConfigManager::Init()
{
	if (!HasConfigExists())
	{
		CreateConfig();
	}

	ms_pReader = new INIReader(ms_sConfigPath);
}

bool CConfigManager::HasConfigExists()
{
	std::ifstream file(ms_sConfigPath);
	return file.good();
}

void CConfigManager::CreateConfig()
{
	std::ofstream file(ms_sConfigPath);

	if (!file)
	{
		printf("[ERROR]: Couldn't create a server config file!\n");
		return;
	}

	for (const auto& [key, value] : ms_umDefaultConfig)
	{
		file << key << " = " << value << "\n";
	}
	file << "bind = " << ms_sDefaultBindAddress << "\n"; // private build: listen address
	file << "story-host =\n"; // private build: story host pinning, empty = the first player is the host
}

uint16_t CConfigManager::GetConfigPort()
{
	return (uint16_t)ms_pReader->GetUnsigned("", "port", ms_umDefaultConfig.at("port"));
}

uint16_t CConfigManager::GetConfigMaxPlayers()
{
	return (uint16_t)ms_pReader->GetUnsigned("", "maxplayers", ms_umDefaultConfig.at("maxplayers"));
}

// private build: a missing or empty "bind" keeps the default (all interfaces)
std::string CConfigManager::GetConfigBindAddress()
{
	return ms_pReader->GetString("", "bind", ms_sDefaultBindAddress);
}

// private build: a missing or empty "story-host" keeps upstream's host rule
std::string CConfigManager::GetConfigStoryHost()
{
	return ms_pReader->GetString("", "story-host", "");
}