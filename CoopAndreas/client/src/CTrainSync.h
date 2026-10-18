#pragma once

#include <cstdint>
#include <cstdio>
#include "network/packets/train.h"

class CTrain;
class CVehicle;

// Private build: host-authoritative train mirror with per-carriage network vehicle identity
// (docs/design/trains.md §5, §6, §7, packages T1b, T4, T5).
class CTrainSync
{
public:
    static void Init();
    static void Process();

    // Network packet handlers
    static void HandleTrainState(const Packets::Train::TrainState& packet);
    static void HandleTrainEvent(const Packets::Train::TrainEvent& packet);
    static void HandleEntityAttach(const Packets::Train::EntityAttach& packet);

    // Join snapshot & restart hooks
    static void SendSnapshotToPlayer(int playerId);
    static void OnGameRestartCleared();
    static void ClearForRestart();

    // Vehicle sync lifecycle callbacks
    static void OnVehicleDestroyed(CVehicle* pVehicle);
    static void OnCarriageRemovePacket(int vehicleid, CVehicle* pVehicle);

    // Engine hooks (VehicleHooks.cpp)
    static bool ProcessControlCopy(CTrain* pTrain);
    static void OnDoTrainGenerationAndRemoval();
    static bool OnSkipToNextAllowedStation(CTrain* pTrain);
    static void OnHooksInstalled(bool bGenHooked, bool bSkipHooked);

    // Mirror blips & diagnostics
    static bool IsEnabled();
    static void WriteDiagSummary(FILE* file);
    static void LogEvent(const char* event, int trainId, const char* format, ...);
};
