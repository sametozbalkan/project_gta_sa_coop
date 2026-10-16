#pragma once


class CNetworkVehicleManager
{
public:
    static std::vector<CNetworkVehicle*> m_pVehicles;
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle);
    static CNetworkVehicle* GetVehicle(int vehicleid);
    static int GetFreeID();
    // private build (vehicle sync protocol): a player leaves; the cars of the host's scripts it synced go back to the host,
    // its other vehicles are removed on every game (VehicleSeatPolicy::OnSyncerLeft)
    static void RemoveAllHostedAndNotify(CNetworkPlayer* player);
    // private build: the vehicle leaves the server, its occupants lose their seats
    static void Delete(CNetworkVehicle* vehicle);
};