#pragma once
#include "CVehicleSyncPolicy.h"

class CNetworkVehicle
{
private:
	CNetworkVehicle() {}
public:
	int m_nVehicleId = -1;
	// private build (vehicle sync protocol): nulled when the game destroys the vehicle (CNetworkVehicleManager::
	// OnVehicleDestroyed); new code reads it through GetLiveVehicle()
	CVehicle* m_pVehicle = nullptr;
	int m_nModelId = 0;
	char m_nPaintJob = -1;
	bool m_bSyncing = false;
	unsigned char m_nTempId = 255;
	unsigned char m_nCreatedBy;
	int m_nBlipHandle = -1;
	Packets::Vehicles::VehicleDriverUpdate m_playerDriverSnapshot{};
	CDamageManager m_oldDamageState{};
	int8_t m_nRadioStation = -1; // private build: the station its driver last sent (CRadioSync), -1 = unknown

	// private build (vehicle sync protocol, CVehicleSyncPolicy.h)
	CVehiclePoolReference m_poolReference;   // the pool slot generation m_pVehicle had when it was stored
	bool m_bRemovedBeforeConfirm = false;    // temp record whose vehicle the game destroyed before VEHICLE_CONFIRM
	bool m_bCopyLost = false;                // this game destroyed its copy; the syncer's next update recreates it
	bool m_bLostWreck = false;               // the lost copy was a wreck: not recreated
	bool m_bHostMissionDelete = false;       // a script of the host's mission is deleting this copy (DELETE_CAR)
	uint32_t m_nTempSince = 0;               // GetTickCount() when this game's VEHICLE_SPAWN went out (temp records)
	bool m_bHasLastState = false;            // the last update's state below is set (from VEHICLE_SPAWN until one came)
	CVector m_vecLastPosition{};
	CVector m_vecLastRight{};
	CVector m_vecLastForward{};
	uint8_t m_nLastColor1 = 0;
	uint8_t m_nLastColor2 = 0;
	CVehicleRecreateThrottle m_recreateThrottle;

	~CNetworkVehicle();
	CNetworkVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy);
	bool CreateVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2);
	bool HasDriver();
	
	// private build (vehicle sync protocol): m_pVehicle while it is still the vehicle stored there (same pool slot
	// generation, valid vtable), else nullptr; a stale pointer is cleared (and a copy counts as lost)
	CVehicle* GetLiveVehicle();
	// the state of the syncer's last update, for recreating a lost copy or taking it over
	void RememberState(const CVector& position, const CVector& right, const CVector& forward, uint8_t color1, uint8_t color2);
	// players (local and network) out of the vehicle before it is deleted for the network; how many it took out
	static unsigned int TakePlayersOut(CVehicle* pVehicle, int vehicleid);
	// a train model: no game makes a copy of a train (CreateVehicle)
	static bool IsTrainModel(int modelid);

	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);
};

