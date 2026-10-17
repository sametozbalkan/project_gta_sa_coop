#pragma once
class CNetworkPedManager
{
public:
	static std::vector<CNetworkPed*> m_pPeds;
	static CNetworkPed* m_apTempPeds[255];

	static CNetworkPed* GetPed(int pedid);
	static CNetworkPed* GetPed(CEntity* entity);
	static void Add(CNetworkPed* ped);
	static void Remove(CNetworkPed* ped);
	static void Update();
	static void Process();
	static void AssignHost();
	static unsigned char AddToTempList(CNetworkPed* networkPed);
	static void RemoveHostedUnused();

	// private build (vehicle sync protocol review 2) ------------------------------------------------------------------
	// CWorld::ClearForRestart deleted every ped (the restart hook in VehicleHooks.cpp): every record goes
	// (PedSyncPolicy::DecideRestartRecord) and the server is asked for the peds the other games sync (PLAYER_GAME_RESTARTED)
	static void OnGameRestartCleared();
	// PED_SPAWN for this id: whether a new copy is made (PedSyncPolicy::DecideSpawn); a record without a ped goes first
	static bool PrepareSpawn(int pedid);
	// --coopdiag counters and records (CoopAndreas_ped_sync.log, the ped-sync: line of CoopAndreas_diag.log)
	static void OnCopyRecreated();
	static void LogEvent(const char* event, int pedid, const char* format = nullptr, ...);
	static void WriteDiagSummary(FILE* file);
};

