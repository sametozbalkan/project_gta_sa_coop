#pragma once
#include "CPedSyncState.h"

class CNetworkPlayer
{
public:
	// private build: read through GetLivePed()/GetReadyPed(); the game can destroy this ped on its own (see
	// CNetworkPlayer.cpp), the pointer then names a freed or reused ped pool slot
	CPlayerPed* m_pPed = nullptr;
	int m_iPlayerId;

	Packets::Players::OnFootUpdate m_onFootSnapshotInterpolated{};
	
	uint32_t m_startedInterpolatingCameraAt = 0;
	Packets::Players::PlayerCameraSync m_cameraSnapshotOld{};
	Packets::Players::PlayerCameraSync m_cameraSnapshot{};
	
	signed short m_oShockButtonL;
	signed short m_lShockButtonL;

	Packets::Players::PlayerPlaceWaypoint m_waypointState{};

	char m_Name[Config::MAX_NICKNAME_LENGTH + 1] = { 0 };
	uint16_t m_nRTT = 0;

	CControllerState m_oldControllerState{};
	CControllerState m_newControllerState{};

	CNetworkPlayerStats m_stats{};
	CPedClothesDesc m_pPedClothesDesc{};
	bool m_bHasBeenConnectedBeforeMe = false;

	bool m_bExtrapolating = false;

	bool m_bRequestedDuckTask = false;

	bool m_bIsHost = false;

	~CNetworkPlayer();
	CNetworkPlayer(int id, CVector position);

	void CreatePed(int id, CVector position);
	void DestroyPed();
	void Respawn();
	int GetInternalId();
	std::string GetName();
	char GetWeaponSkill(eWeaponType weaponType);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void RemoveFromVehicle(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void EnterVehiclePassenger(CVehicle* vehicle, int seatid);
	void HandleTask(Packets::Players::SetPlayerTask& packet);
	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);

	// private build (remote player ped recovery)
	CPlayerPed* GetLivePed();
	bool IsModelReady();
	CPlayerPed* GetReadyPed() { return IsModelReady() ? m_pPed : nullptr; }
	CPlayerPed* GetOrRecoverPed(const CVector* position = nullptr);
	void SetLastKnownState(const CVector& position, int areaCode = -1);
	void SetClothes(const CPedClothesDesc& clothesDesc);
	void ProcessPedRecovery();
	void OnPedDestroyed(CPlayerPed* ped);
	void KeepFlaggedPed(CPlayerPed* ped);
	void FinishKeptPed(bool bInsideWorldProcess);
	void LogPedDiagnostic(const char* phase, const char* reason, const char* detail = nullptr);

private:
	CPedPoolReference m_poolReference;
	CPedRecoveryThrottle m_recoveryThrottle;
	CVector m_vecLastPosition{};
	float m_fLastHeading = 0.0f;
	uint8_t m_nLastAreaCode = 0;
	bool m_bHasLastPosition = false;
	bool m_bClothesPending = false;
	bool m_bDestroyingPed = false;
	CVector m_vecKeptPosition{};
	bool m_bKeptPending = false;
	bool m_bKeptFlushed = false;
	uint8_t m_nPedProblem = 0;
	uint8_t m_nHiddenReason = 0;
	bool m_abLoggedPedDiagnostic[11]{};
	uint32_t m_anLastPedDiagnostic[11]{};

	void RecreatePed(const char* reason);
	void ApplyLastKnownState(CPlayerPed* ped);
	void CheckHidden(CPlayerPed* ped);
};

