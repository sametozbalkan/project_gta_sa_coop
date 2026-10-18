#pragma once

class CConfigLoader
{
private:
	static void BuildPath();
	static void BuildDefaultConfig();
	static void SetDirMyDocuments_Hook();
	static void EnsureCreated();
	static void Load();

	static inline std::string ms_sDataPath = "";

	static inline std::unordered_map<std::string, std::string> ms_umDefaultConfig =
	{
		{"nickname", ""},
		{"ip", ""},
		{"port", std::to_string(Config::DEFAULT_PORT)},
		{"report-crashlogs", "false"}, // private build: crash reports are opt-in
		{"mission-objects", "true"} // private build: the mission object mirror's kill switch (CMirrorObjects)
	};
	
public:
	static inline const std::string CONFIG_SECTION = "config";
	static inline const std::string CONFIG_FILE_NAME = "coopandreas.ini";
	static inline bool ms_bMissionObjects = true; // private build

	static void Init();
	static void Save();
};