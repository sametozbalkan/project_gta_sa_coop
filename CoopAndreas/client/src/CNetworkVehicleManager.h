#pragma once
class CNetworkVehicleManager
{
public:
    static inline std::vector<CNetworkVehicle*> m_pVehicles;
    static inline CNetworkVehicle* m_apTempVehicles[255];

    // private build (vehicle sync protocol): both return a record whose m_pVehicle is the live vehicle or nullptr; by
    // pointer only a record that still holds that vehicle (CVehiclePoolReference)
    static CNetworkVehicle* GetVehicle(int vehicleid);
    static CNetworkVehicle* GetVehicle(CEntity* vehicle);
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle);
    static void UpdateDriver(CVehicle* pVehicle);
    static void UpdateIdle();
    static void UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed);
    static uint8_t AddToTempList(CNetworkVehicle* networkVehicle);
    static void RemoveHostedUnused();
    static void UpdateDamageSync();

    // private build (vehicle sync protocol, rules in CVehicleSyncPolicy.h) --------------------------------------------
    // the temp record (ours, not confirmed yet) that holds this vehicle
    static CNetworkVehicle* GetTempVehicle(CEntity* vehicle);
    // packets: VEHICLE_SPAWN made this record, VEHICLE_CONFIRM, ASSIGN_VEHICLE, VEHICLE_REMOVE (with its reason)
    static void OnSpawned(CNetworkVehicle* pNetworkVehicle);
    static void OnConfirmed(uint8_t tempid, int vehicleid);
    static void OnAssignment(int vehicleid, uint16_t modelid, bool bSyncing);
    static void OnRemovePacket(int vehicleid, uint8_t reason);
    // the vehicle goes for the network (VEHICLE_REMOVE, an id reused by VEHICLE_SPAWN): players out, our copy deleted,
    // the record deleted; nothing is sent. packetReason: the VEHICLE_REMOVE's reason, -1 for the other sources.
    static void RemoveForNetwork(CNetworkVehicle* pNetworkVehicle, const char* source, int packetReason = -1);
    static void SendRemove(int vehicleid, uint8_t reason);
    // a packet handler held this record and its vehicle across a call that can destroy vehicles (the ped recovery's
    // CREATE_PLAYER and its clear-space, a warp, a synchronous model load): whether the record with this id is still that
    // record and still holds that live vehicle (the ~CVehicle hook may have deleted the record or let go of the vehicle)
    static bool StillHolds(int vehicleid, const CNetworkVehicle* pNetworkVehicle, const CVehicle* pVehicle);
    // the local player is getting out of its vehicle (a leave-car or dragged-out task in its primary task slots): no driver
    // or passenger updates meanwhile (VehicleSyncPolicy::ChooseLocalUpdate)
    static bool IsLocalPlayerLeavingVehicle(CPed* pPed);
    // once a frame after CGame::Process (Main.cpp): VEHICLE_EXIT for an exit the exit key didn't start (a script's task,
    // being dragged out, a warp; VehicleSyncPolicy::CLocalExitTracker); the exit key's goes out from TaskHooks.cpp, which
    // tells OnKeyExitSent
    static void ProcessLocalExit(CPlayerPed* pPlayerPed);
    static void OnKeyExitSent();
    // hooks: CWorld::Remove of a vehicle (site = the call site), CVehicle::~CVehicle (before and after the original), a
    // local createdBy change, CWorld::ClearForRestart (a new game or a load destroys every vehicle in between)
    static void OnWorldRemove(CVehicle* pVehicle, uintptr_t site);
    static void OnVehicleDestroyed(CVehicle* pVehicle);
    static void OnVehicleDestructorDone(CVehicle* pVehicle);
    static void OnLocalCreatedByChanged(CVehicle* pVehicle, int createdBy);
    static void OnDestructorHooksInstalled(int hooked, int total);
    static void SetRestartClear(bool bRestarting);
    static void OnRestartHookInstalled(bool bHooked);
    // --coopdiag counters and records: no temp slot for a new vehicle of this game, a copy VEHICLE_SPAWN couldn't make
    static void OnTempSlotsFull();
    static void OnCopyCreateFailed(int vehicleid, int modelid, int createdBy);
    // the network id of a vehicle for the ped sync log, also while its destructor runs (its record has let go of it by then)
    static int GetIdForLog(const CVehicle* pVehicle);
    // a copy (a vehicle another game syncs) stays MISSION_VEHICLE and is never "no longer needed"
    static void GuardCopy(CVehicle* pVehicle);
    static void ForgetCopy(CVehicle* pVehicle);
    // train carriage copies built by CTrain::CreateMissionTrain in CTrainSync (T1a)
    static bool BindTrainCopy(int vehicleid, uint16_t expectedModel, CTrain* pCarriage);
    static void UnbindTrainCopy(CTrain* pCarriage);
    // a lost copy comes back where the syncer's update puts it (nullptr when it isn't recreated now)
    static CVehicle* RecreateLostCopy(CNetworkVehicle* pNetworkVehicle, const CVector& position, const CVector& right,
        const CVector& forward, uint8_t color1, uint8_t color2);
    // --coopdiag: one record in CoopAndreas_vehicle_sync.log next to gta_sa.exe (at most 4096 records and 2 MB); the
    // frequent events (seats, unchanged assignments) at most once a second each; the routine ones (the population's
    // removals of random cars, here and on the other games) counted and written as one summary line a minute
    static void LogEvent(const char* event, int vehicleid, const char* format = nullptr, ...);
    static void LogFrequentEvent(const char* event, int vehicleid, const char* format = nullptr, ...);
    // --coopdiag: the vehicle-sync: line of CoopAndreas_diag.log (CDiagLog's thread; reads counters the game thread keeps)
    static void WriteDiagSummary(FILE* file);
};

