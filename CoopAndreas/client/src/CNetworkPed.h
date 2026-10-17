#pragma once
#include "CPedSyncState.h"
class CNetworkPed
{
private:
	CNetworkPed() {}
	CPedPoolReference m_poolReference;
	struct OnFootVisualSnapshot
	{
		Packets::Players::SWeaponSnapshot weapon;
		CVector aim;
		bool ducked;
		bool aiming;
		unsigned char fightingStyle;
		CVector position;
	};
	CLatestPedSnapshot<OnFootVisualSnapshot> m_pendingOnFoot;
	bool m_bVisualDeferred = false;
	bool m_abLoggedSyncDiagnostic[4]{};
	uint32_t m_anLastSyncDiagnostic[4]{};
	void LogSyncDiagnostic(const char* phase, const char* reason);
	bool ApplyOnFootVisual(const OnFootVisualSnapshot& snapshot);
	bool ApplyWeapon(unsigned char weapon, unsigned short ammo, bool select);
	// private build (vehicle sync protocol review 2): what a copy is made from, again for a lost one (RecreateLostCopy)
	int m_nModelId = 0;
	char m_szSpecialModelName[8]{};
	CPedCopyRecreateThrottle m_recreateThrottle;
	bool CreateCopy(const CVector& pos);
public:
	int m_nPedId = -1;
	CPed* m_pPed = nullptr;
	bool m_bSyncing = false;
	unsigned char m_nTempId = 255;
	ePedType m_nPedType;
	unsigned char m_nCreatedBy;
	CVector m_vecVelocity{0.0f, 0.0f, 0.0f};
	float m_fAimingRotation = 0.0f;
	float m_fCurrentRotation = 0.0f;
	float m_fLookDirection = 0.0f;
	eMoveState m_nMoveState = eMoveState::PEDMOVE_NONE;
	float m_fMoveBlendRatio = 0.0f;
	CAutoPilot m_autoPilot;
	float m_fGasPedal = 0.0f;
	float m_fBreakPedal = 0.0f;
	float m_fSteerAngle = 0.0f;
	float m_fHealth = 100.0f;
	int m_nBlipHandle = -1;
	bool m_bClaimOnRelease = false;
	// private build (vehicle sync protocol review 2): this game has no ped for this copy (it couldn't be made, or the game
	// destroyed it); the syncer's next update brings it back (RecreateLostCopy, PedSyncPolicy::ShouldRecreateCopy)
	bool m_bCopyLost = false;

	static CNetworkPed* CreateHosted(CPed* ped);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void RemoveFromVehicle(CVehicle* vehicle);
	void ClaimOnRelease();
	void CancelClaim();

	CPed* GetLivePed();
	bool IsModelReady();
	void ReceiveOnFoot(const Packets::Peds::PedOnFoot& snapshot);
	void ProcessPendingOnFoot();
	void CancelPendingOnFoot(const char* reason);
	bool ApplyWeaponSnapshot(const Packets::Players::SWeaponSnapshot& weaponSnapshot);
	// private build (vehicle sync protocol review 2): a lost copy made again at pos, at once and then throttled
	// (CPedCopyRecreateThrottle); nullptr when it isn't now. Loads the model (CStreaming::LoadAllRequestedModels).
	CPed* RecreateLostCopy(const CVector& pos);
	// CWorld::ClearForRestart deleted the ped: the record lets go of it without touching the game (CNetworkPedManager)
	void ForgetPedForRestart();

	CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[]);
	~CNetworkPed();
};

