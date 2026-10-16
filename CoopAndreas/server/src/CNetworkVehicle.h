#pragma once

#include <iostream>
#include <vector>
#include <algorithm>

#include "CVector.h"
#include "CNetworkPlayerManager.h"
#include <CDamageManager.h>
#include "CVehicleSeatPolicy.h"

class CNetworkVehicle
{
public:
    CNetworkVehicle(int vehicleid, unsigned short model, CVector pos, float rot);

    int m_nVehicleId;
    CNetworkPlayer* m_pSyncer = nullptr;
    unsigned short m_nModelId;
    CVector m_vecPosition;
    CVector m_vecRotation;
    CVector m_vecVelocity;
    CNetworkPlayer* m_pPlayers[8] = {nullptr};
    unsigned char m_nPrimaryColor;
    unsigned char m_nSecondaryColor;
    CDamageManager m_damageManager;
    std::vector<int> m_pComponents;
    uint8_t m_nCreatedBy = VehicleSync::CREATED_BY_RANDOM;
    bool m_bUsedByPed = false;

    // private build (vehicle sync protocol, rules in CVehicleSeatPolicy.h)
    CNetworkPlayer* m_pCreator = nullptr;  // the player whose game spawned it (REMOVE_UNCONFIRMED comes from that game)
    bool m_bHostMission = false;           // MISSION_VEHICLE of the host's scripts: back to the host when its syncer leaves
    uint32_t m_nCreatedAt = 0;             // g_serverTime of its VEHICLE_SPAWN: packets stamped before are about an older
                                           // vehicle with this id (EntityLifecycle::IsFromBeforeCreation)
    VehicleSeatPolicy::CSyncerCorrection m_syncerCorrection;

    // the syncer is newSyncer: ASSIGN_VEHICLE with bSyncing false to the old syncer and true to the new one
    void ReassignSyncer(CNetworkPlayer* newSyncer);
    // the player takes the seat (nullptr empties it); it leaves the seat it held, in any vehicle (CVehicleSeatPolicy.h)
    void SetOccupant(int8_t seatid, CNetworkPlayer* player);
    // private build: a game that sent an update for a vehicle it doesn't sync hears that it isn't the syncer (rate-limited)
    void CorrectStaleSyncer(CNetworkPlayer* player);

    ~CNetworkVehicle() {}
};