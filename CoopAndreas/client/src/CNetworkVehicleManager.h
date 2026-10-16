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
    // packets: VEHICLE_SPAWN made this record, VEHICLE_CONFIRM, ASSIGN_VEHICLE, VEHICLE_REMOVE
    static void OnSpawned(CNetworkVehicle* pNetworkVehicle);
    static void OnConfirmed(uint8_t tempid, int vehicleid);
    static void OnAssignment(int vehicleid, uint16_t modelid, bool bSyncing);
    static void OnRemovePacket(int vehicleid);
    // the vehicle goes for the network (VEHICLE_REMOVE, an id reused by VEHICLE_SPAWN): players out, our copy deleted,
    // the record deleted; nothing is sent
    static void RemoveForNetwork(CNetworkVehicle* pNetworkVehicle, const char* reason);
    static void SendRemove(int vehicleid, uint8_t reason);
    // hooks: CWorld::Remove of a vehicle (site = the call site), CVehicle::~CVehicle, a local createdBy change
    static void OnWorldRemove(CVehicle* pVehicle, uintptr_t site);
    static void OnVehicleDestroyed(CVehicle* pVehicle);
    static void OnLocalCreatedByChanged(CVehicle* pVehicle, int createdBy);
    static void OnDestructorHooksInstalled(int hooked, int total);
    // a copy (a vehicle another game syncs) stays MISSION_VEHICLE and is never "no longer needed"
    static void GuardCopy(CVehicle* pVehicle);
    // a lost copy comes back where the syncer's update puts it (nullptr when it isn't recreated now)
    static CVehicle* RecreateLostCopy(CNetworkVehicle* pNetworkVehicle, const CVector& position, const CVector& right,
        const CVector& forward, uint8_t color1, uint8_t color2);
    // --coopdiag: one record in CoopAndreas_vehicle_sync.log next to gta_sa.exe (at most 512 records and 1 MB); the
    // frequent events (seats, unchanged assignments) at most once a second each
    static void LogEvent(const char* event, int vehicleid, const char* format = nullptr, ...);
    static void LogFrequentEvent(const char* event, int vehicleid, const char* format = nullptr, ...);
};

