#pragma once


class CNetworkVehicleManager
{
public:
    static std::vector<CNetworkVehicle*> m_pVehicles;
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle);
    static CNetworkVehicle* GetVehicle(int vehicleid);
    // private build (vehicle sync protocol): round robin (EntityLifecycle::NextFreeId), -1 when all ids are taken
    static int GetFreeID();
    // private build: the vehicle as the join snapshot has it (VEHICLE_SPAWN, VEHICLE_DAMAGE, VEHICLE_COMPONENT_ADD), to one
    // player: a joiner, or a game whose removal was rejected or whose restart gave the vehicle back to the host
    static void SendVehicleTo(CNetworkVehicle* vehicle, CNetworkPlayer* player);
    // private build (vehicle sync protocol): a player leaves; the cars of the host's scripts it synced go back to the host,
    // its other vehicles are removed on every game (VehicleSeatPolicy::OnSyncerLeft)
    static void RemoveAllHostedAndNotify(CNetworkPlayer* player);
    // private build: the vehicle leaves the server, its occupants lose their seats
    static void Delete(CNetworkVehicle* vehicle);
};