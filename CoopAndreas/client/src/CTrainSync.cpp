#include "stdafx.h"
#include "CTrainSync.h"
#include "CTrainSyncPolicy.h"
#include "CSessionLifecycle.h"
#include "CVehicleSyncPolicy.h"
#include <CTaskSimpleCarSetPedOut.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr uint32_t RECREATE_THROTTLE_MS = 1000;
constexpr uint32_t HOST_RESET_SNAPSHOT_DELAY_MS = 2000;
constexpr float FRIEND_KEEP_TRAIN_DISTANCE_SQ = 220.0f * 220.0f;

struct SHostTrain
{
    bool bUsed = false;
    bool bSpawnSent = false;
    uint8_t trainId = 0;
    uint8_t configIndex = 0;
    uint8_t carriageCount = 0;
    CTrain* pCarriages[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    CVehiclePoolReference carriageRefs[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    int vehicleIds[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    uint16_t models[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    Packets::Train::STrainCarriageStateEntry lastCarriageStates[Packets::Train::MAX_TRAIN_CARRIAGES]{};

    uint8_t sequence = 0;
    uint8_t jumpCounter = 0;
    uint32_t lastStateSentMs = 0;
    float lastRailDistance = 0.0f;
    float lastSpeedPerStep = 0.0f;
    bool bLastMoving = false;
    bool bLastClockwise = false;
    bool bLastDerailed = false;
    bool bLastStoppedAtStation = false;
};

struct SCopyTrain
{
    bool bHasDescriptor = false;
    bool bCopyLost = false;
    Packets::Train::STrainSpawnPayload desc{};
    Packets::Train::STrainCarriageStateEntry carriageStates[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    bool bHasCarriageStates = false;

    CTrain* pCarriages[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    CVehiclePoolReference carriageRefs[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    bool bBound[Packets::Train::MAX_TRAIN_CARRIAGES]{};

    Packets::Train::STrainStatePayload latestState{};
    bool bHasState = false;
    uint32_t lastStateRecvMs = 0;
    uint8_t appliedJumpCounter = 0;

    float targetRailDistance = 0.0f;
    float displayDistance = 0.0f;
    float hostSpeed = 0.0f;
    float hostGas = 0.0f;
    float hostBrake = 0.0f;
    bool bHostStoppedAtStation = false;
    bool bDerailed = false;
    uint32_t lastSteppedFrame = 0;
    bool bSnappedThisFrame = false;
    uint32_t lastRecreateAttemptMs = 0;
};

struct SDiagCounters
{
    unsigned int hostTrains = 0;
    unsigned int copyTrains = 0;
    unsigned int boundCarriages = 0;
    unsigned int spawnsSent = 0;
    unsigned int spawnsReceived = 0;
    unsigned int copiesCreated = 0;
    unsigned int copiesRecreated = 0;
    unsigned int snaps = 0;
    unsigned int derails = 0;
    unsigned int skipRequests = 0;
    unsigned int attachesSent = 0;
    unsigned int attachesApplied = 0;
    float maxCorrection = 0.0f;
};

struct SOwnedAttachmentState
{
    bool bHadSent = false;
    TrainSyncPolicy::SAttachmentSpec lastSent{};
};

struct SRemoteAttachmentState
{
    bool bActive = false;
    TrainSyncPolicy::SAttachmentSpec spec{};
    CEntity* pLastSubject = nullptr;
    CVehicle* pLastTarget = nullptr;
};

SHostTrain ms_aHostTrains[Packets::Train::MAX_TRAINS]{};
SCopyTrain ms_aCopies[Packets::Train::MAX_TRAINS]{};
SDiagCounters ms_diag{};
SOwnedAttachmentState ms_localPlayerAttachSent{};
SOwnedAttachmentState ms_aOwnedPedAttachSent[Config::MAX_SERVER_PEDS]{};
SOwnedAttachmentState ms_aOwnedVehicleAttachSent[Config::MAX_SERVER_VEHICLES]{};
SRemoteAttachmentState ms_aRemotePlayerAttachments[Config::MAX_SERVER_PLAYERS]{};
SRemoteAttachmentState ms_aRemotePedAttachments[Config::MAX_SERVER_PEDS]{};
SRemoteAttachmentState ms_aRemoteVehicleAttachments[Config::MAX_SERVER_VEHICLES]{};
bool ms_bInitialized = false;
bool ms_bLayoutMismatch = false;
bool ms_bGenHooked = false;
bool ms_bSkipHooked = false;
bool ms_bPendingHostResetSnapshot = false;
uint32_t ms_nHostResetSnapshotDueMs = 0;
int ms_nLastKnownHostId = -1;

void DetachPedCleanly(CPed* pPed)
{
    if (pPed == nullptr || !CPools::ms_pPedPool || !CPools::ms_pPedPool->IsObjectValid(pPed) ||
        !pPed->IsVTableValid() || pPed->m_pAttachedTo == nullptr)
    {
        return;
    }
    const int savedType = pPed->m_nPedType;
    if (savedType == PED_TYPE_PLAYER1 || savedType == PED_TYPE_PLAYER2)
    {
        pPed->m_nPedType = PED_TYPE_CIVMALE;
    }
    pPed->DettachPedFromEntity();
    pPed->m_nPedType = savedType;
}

void DetachVehicleCleanly(CVehicle* pVehicle)
{
    if (pVehicle == nullptr || !CPools::ms_pVehiclePool || !CPools::ms_pVehiclePool->IsObjectValid(pVehicle) ||
        !pVehicle->IsVTableValid() || pVehicle->m_pAttachedTo == nullptr)
    {
        return;
    }
    pVehicle->DettachEntityFromEntity(0.0f, 0.0f, 0.0f, true);
}

const float* GetTrackLengths()
{
    return reinterpret_cast<const float*>(0xC37FEC);
}

const uint32_t* GetTrackNodeCounts()
{
    return reinterpret_cast<const uint32_t*>(0xC38014);
}

const uint32_t (*GetTrainConfigs())[16]
{
    return reinterpret_cast<const uint32_t(*)[16]>(0x8D44F8);
}

float GetTrackLength(uint8_t trackId)
{
    return trackId < 4 ? GetTrackLengths()[trackId] : 0.0f;
}

uint32_t ComputeLocalLayoutHash()
{
    const uint32_t* counts = GetTrackNodeCounts();
    if (counts[0] == 0)
    {
        return 0;
    }
    return TrainSyncPolicy::ComputeLayoutHash(counts, GetTrackLengths(), GetTrainConfigs());
}

bool IsTrainEntity(const CVehicle* pVehicle)
{
    return pVehicle != nullptr && pVehicle->m_nVehicleSubType == static_cast<unsigned int>(VEHICLE_TRAIN) &&
           CNetworkVehicle::IsTrainModel(pVehicle->m_nModelIndex);
}

bool HasNpcDriver(const CTrain* pHead)
{
    if (pHead == nullptr || pHead->m_pDriver == nullptr)
    {
        return false;
    }
    CPed* pDriver = pHead->m_pDriver;
    if (pDriver == FindPlayerPed(0) || pDriver->IsPlayer())
    {
        return false;
    }
    if (CNetworkPlayerManager::GetPlayer(pDriver) != nullptr || CNetworkPedManager::GetPed(pDriver) != nullptr)
    {
        return false;
    }
    return true;
}

void RemoveCosmeticDriverIfPresent(CTrain* pHead)
{
    if (!HasNpcDriver(pHead))
    {
        return;
    }
    CPed* pDriver = pHead->m_pDriver;
    if (pDriver == nullptr || !CPools::ms_pPedPool->IsObjectValid(pDriver))
    {
        pHead->m_pDriver = nullptr;
        return;
    }
    CTaskSimpleCarSetPedOut outTask(pHead, 1, false);
    outTask.m_bWarpingOutOfCar = true;
    outTask.ProcessPed(pDriver);
    pHead->m_pDriver = nullptr;
    if (CPools::ms_pPedPool->IsObjectValid(pDriver))
    {
        CWorld::Remove(pDriver);
        CWorld::RemoveReferencesToDeletedObject(pDriver);
        delete pDriver;
    }
}

void TakeNetworkedOccupantsOut(CTrain* pCarriage, int vehicleid)
{
    if (pCarriage == nullptr)
    {
        return;
    }
    CNetworkVehicle::TakePlayersOut(pCarriage, vehicleid);

    auto ejectNetworkPed = [pCarriage](CPed* pPed)
    {
        if (pPed != nullptr && CNetworkPedManager::GetPed(pPed) != nullptr && CPools::ms_pPedPool->IsObjectValid(pPed))
        {
            CTaskSimpleCarSetPedOut task(pCarriage, 1, false);
            task.m_bWarpingOutOfCar = true;
            task.ProcessPed(pPed);
        }
    };

    ejectNetworkPed(pCarriage->m_pDriver);
    for (int s = 0; s < static_cast<int>(ARRAY_SIZE(pCarriage->m_apPassengers)); s++)
    {
        ejectNetworkPed(pCarriage->m_apPassengers[s]);
    }

    // Also cleanly detach any peds or vehicles attached to this carriage before it is deleted
    if (CPlayerPed* pLocal = FindPlayerPed(0); pLocal != nullptr && pLocal->m_pAttachedTo == pCarriage)
    {
        DetachPedCleanly(pLocal);
    }
    for (CNetworkPlayer* pNetPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetPlayer != nullptr)
        {
            if (CPlayerPed* pPed = pNetPlayer->GetLivePed(); pPed != nullptr && pPed->m_pAttachedTo == pCarriage)
            {
                DetachPedCleanly(pPed);
            }
        }
    }
    for (CNetworkPed* pNetPed : CNetworkPedManager::m_pPeds)
    {
        if (pNetPed != nullptr)
        {
            if (CPed* pPed = pNetPed->GetLivePed(); pPed != nullptr && pPed->m_pAttachedTo == pCarriage)
            {
                DetachPedCleanly(pPed);
            }
        }
    }
    for (CNetworkVehicle* pNetVeh : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetVeh != nullptr)
        {
            if (CVehicle* pVeh = pNetVeh->GetLiveVehicle(); pVeh != nullptr && pVeh->m_pAttachedTo == pCarriage)
            {
                DetachVehicleCleanly(pVeh);
            }
        }
    }
}

void DeleteSingleTrainCarriage(CTrain* pCarriage, int vehicleid)
{
    if (pCarriage == nullptr || !CPools::ms_pVehiclePool->IsObjectValid(pCarriage) || !pCarriage->IsVTableValid())
    {
        return;
    }
    CNetworkVehicleManager::UnbindTrainCopy(pCarriage);
    TakeNetworkedOccupantsOut(pCarriage, vehicleid);
    RemoveCosmeticDriverIfPresent(pCarriage);
    CWorld::Remove(pCarriage);
    CWorld::RemoveReferencesToDeletedObject(pCarriage);
    delete pCarriage;
}

void DestroyCopyChainVehicles(SCopyTrain& copy, const char* reason)
{
    bool bHadAny = false;
    for (int i = 0; i < copy.desc.carriageCount && i < Packets::Train::MAX_TRAIN_CARRIAGES; i++)
    {
        CTrain* pCarriage = copy.pCarriages[i];
        const bool bValid = pCarriage != nullptr && copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) &&
                            pCarriage->IsVTableValid();
        copy.pCarriages[i] = nullptr;
        copy.carriageRefs[i].Clear();
        copy.bBound[i] = false;
        if (bValid)
        {
            bHadAny = true;
            DeleteSingleTrainCarriage(pCarriage, copy.desc.carriages[i].vehicleid);
        }
    }
    if (bHadAny && reason != nullptr)
    {
        CTrainSync::LogEvent("copy-destroyed-chain", copy.desc.trainId, "reason=%s", reason);
    }
}

void RemoveCopySlot(int trainId, const char* reason)
{
    if (trainId < 0 || trainId >= Packets::Train::MAX_TRAINS)
    {
        return;
    }
    SCopyTrain& copy = ms_aCopies[trainId];
    if (!copy.bHasDescriptor)
    {
        return;
    }
    DestroyCopyChainVehicles(copy, reason);
    copy = SCopyTrain{};
}

void RemoveAllCopies(const char* reason)
{
    for (int i = 0; i < Packets::Train::MAX_TRAINS; i++)
    {
        RemoveCopySlot(i, reason);
    }
}

bool CheckLayoutHash(uint32_t hostHash)
{
    if (ms_bLayoutMismatch)
    {
        return false;
    }
    if (hostHash == 0)
    {
        return true;
    }
    const uint32_t localHash = ComputeLocalLayoutHash();
    if (localHash == 0)
    {
        return true;
    }
    if (localHash != hostHash)
    {
        ms_bLayoutMismatch = true;
        CTrainSync::LogEvent("layout-mismatch", -1, "host=0x%08X local=0x%08X", hostHash, localHash);
        RemoveAllCopies("layout-mismatch");
        return false;
    }
    return true;
}

bool IsCarriageInAnyCopy(const CTrain* pTrain, int* outTrainId = nullptr, int* outCarriageIdx = nullptr)
{
    if (pTrain == nullptr)
    {
        return false;
    }
    for (int t = 0; t < Packets::Train::MAX_TRAINS; t++)
    {
        const SCopyTrain& copy = ms_aCopies[t];
        if (!copy.bHasDescriptor)
        {
            continue;
        }
        for (int i = 0; i < copy.desc.carriageCount; i++)
        {
            if (copy.pCarriages[i] == pTrain && copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pTrain))
            {
                if (outTrainId != nullptr)
                {
                    *outTrainId = t;
                }
                if (outCarriageIdx != nullptr)
                {
                    *outCarriageIdx = i;
                }
                return true;
            }
        }
    }
    return false;
}

void RemoveLocalNonMissionTrains()
{
    if (!CPools::ms_pVehiclePool)
    {
        return;
    }
    const bool bKeepLocalMissionTrain = CTheScripts::IsPlayerOnAMission();
    std::vector<CTrain*> toRemove;
    for (CVehicle* pVehicle : CPools::ms_pVehiclePool)
    {
        if (!IsTrainEntity(pVehicle))
        {
            continue;
        }
        auto* pTrain = reinterpret_cast<CTrain*>(pVehicle);
        if (IsCarriageInAnyCopy(pTrain))
        {
            continue;
        }
        if (bKeepLocalMissionTrain && pTrain->m_nTrainFlags.bMissionTrain)
        {
            continue;
        }
        toRemove.push_back(pTrain);
    }

    for (CTrain* pTrain : toRemove)
    {
        if (CPools::ms_pVehiclePool->IsObjectValid(pTrain) && pTrain->IsVTableValid())
        {
            DeleteSingleTrainCarriage(pTrain, -1);
        }
    }
    if (!toRemove.empty())
    {
        CTrainSync::LogEvent("follower-cleaned-local-trains", -1, "removed=%u", static_cast<unsigned>(toRemove.size()));
    }
}

void ApplyDerailToCopy(SCopyTrain& copy)
{
    if (copy.bDerailed)
    {
        return;
    }
    copy.bDerailed = true;
    for (int i = 0; i < copy.desc.carriageCount; i++)
    {
        CTrain* pCarriage = copy.pCarriages[i];
        if (pCarriage != nullptr && copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) &&
            pCarriage->IsVTableValid())
        {
            pCarriage->m_nTrainFlags.bNotOnARailRoad = 1;
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pCarriage) + 0x40) &= 0xFFFDFFFBu;
            pCarriage->SetIsStatic(false);
        }
    }
    ms_diag.derails++;
    CTrainSync::LogEvent("copy-derailed", copy.desc.trainId, "carriages=%u", copy.desc.carriageCount);
}

void ApplyCarriageStatesToCopy(SCopyTrain& copy)
{
    if (!copy.bHasCarriageStates)
    {
        return;
    }
    for (int i = 0; i < copy.desc.carriageCount; i++)
    {
        CTrain* pCarriage = copy.pCarriages[i];
        if (pCarriage == nullptr || !copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) ||
            !pCarriage->IsVTableValid())
        {
            continue;
        }
        pCarriage->m_eDoorLock = static_cast<eDoorLock>(copy.carriageStates[i].doorLock);
        pCarriage->m_fHealth = static_cast<float>(copy.carriageStates[i].health);
        if (copy.carriageStates[i].bWrecked && pCarriage->m_nStatus != STATUS_WRECKED)
        {
            pCarriage->m_nStatus = STATUS_WRECKED;
        }
    }
}

void ApplyStatePayloadToCopy(SCopyTrain& copy, const Packets::Train::STrainStatePayload& state, bool bInitialSnap)
{
    copy.latestState = state;
    copy.bHasState = true;
    copy.lastStateRecvMs = GetTickCount();
    copy.targetRailDistance = state.railDistance;
    copy.hostSpeed = TrainSyncPolicy::DequantizeSpeed(state.speedQuantized);
    copy.hostGas = TrainSyncPolicy::DequantizePedal(state.gasHalf);
    copy.hostBrake = TrainSyncPolicy::DequantizePedal(state.brakeHalf);
    copy.bHostStoppedAtStation = (state.flags & Packets::Train::STATE_FLAG_STOPPED_AT_STATION) != 0;

    if (bInitialSnap)
    {
        copy.displayDistance = state.railDistance;
        copy.appliedJumpCounter = state.jumpCounter;
        copy.bSnappedThisFrame = true;
    }

    const bool bClockwise = (state.flags & Packets::Train::STATE_FLAG_CLOCKWISE) != 0;
    for (int i = 0; i < copy.desc.carriageCount; i++)
    {
        CTrain* pCarriage = copy.pCarriages[i];
        if (pCarriage == nullptr || !copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) ||
            !pCarriage->IsVTableValid())
        {
            continue;
        }
        pCarriage->m_nTrackId = static_cast<char>(state.trackId);
        pCarriage->m_nTrainFlags.bClockwiseDirection = bClockwise ? 1 : 0;
        pCarriage->m_nTrainFlags.bStoppedAtStation = copy.bHostStoppedAtStation ? 1 : 0;
        pCarriage->m_nTrainFlags.bPassengersCanEnterAndLeave = copy.bHostStoppedAtStation ? 1 : 0;
    }

    if ((state.flags & Packets::Train::STATE_FLAG_HAS_NPC_DRIVER) == 0 && copy.pCarriages[0] != nullptr &&
        copy.carriageRefs[0].Matches(CPools::ms_pVehiclePool, copy.pCarriages[0]))
    {
        RemoveCosmeticDriverIfPresent(copy.pCarriages[0]);
    }

    if ((state.flags & Packets::Train::STATE_FLAG_DERAILED) != 0)
    {
        ApplyDerailToCopy(copy);
    }
}

void TryBindCopyCarriages(SCopyTrain& copy)
{
    for (int i = 0; i < copy.desc.carriageCount; i++)
    {
        if (copy.bBound[i])
        {
            continue;
        }
        CTrain* pCarriage = copy.pCarriages[i];
        if (pCarriage == nullptr || !copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) ||
            !pCarriage->IsVTableValid())
        {
            continue;
        }
        if (CNetworkVehicleManager::BindTrainCopy(
                copy.desc.carriages[i].vehicleid, copy.desc.carriages[i].modelid, pCarriage))
        {
            copy.bBound[i] = true;
            CTrainSync::LogEvent("copy-carriage-bound", copy.desc.trainId, "idx=%d vehicle=%d model=%u", i,
                copy.desc.carriages[i].vehicleid, copy.desc.carriages[i].modelid);
        }
    }
}

void StepCopyTrainForFrame(SCopyTrain& copy)
{
    if (copy.bDerailed || !copy.bHasState)
    {
        return;
    }
    if (copy.lastSteppedFrame == CTimer::m_FrameCounter)
    {
        return;
    }
    copy.lastSteppedFrame = CTimer::m_FrameCounter;

    const uint8_t trackId = copy.latestState.trackId;
    const float trackLen = GetTrackLength(trackId);
    const float dtSteps = CTimer::ms_fTimeStep;
    const uint32_t now = GetTickCount();
    const uint32_t msSinceState = now >= copy.lastStateRecvMs ? (now - copy.lastStateRecvMs) : 0;
    const bool bJumpChanged = copy.latestState.jumpCounter != copy.appliedJumpCounter;

    if (msSinceState <= TrainSyncPolicy::HOLD_MS && !bJumpChanged)
    {
        copy.targetRailDistance =
            TrainSyncPolicy::WrapRailDistance(copy.targetRailDistance + copy.hostSpeed * dtSteps, trackLen);
    }

    const TrainSyncPolicy::SStepResult step = TrainSyncPolicy::StepDisplayRailDistance(copy.displayDistance,
        copy.targetRailDistance, copy.hostSpeed, dtSteps, trackLen, msSinceState, bJumpChanged);

    copy.displayDistance = step.displayDistance;
    copy.appliedJumpCounter = copy.latestState.jumpCounter;
    copy.bSnappedThisFrame = step.bSnapped;

    const float absCorr = std::fabs(step.appliedCorrection);
    if (absCorr > ms_diag.maxCorrection)
    {
        ms_diag.maxCorrection = absCorr;
    }
    if (step.bSnapped)
    {
        ms_diag.snaps++;
        CTrainSync::LogEvent("copy-snapped", copy.desc.trainId, "dist=%.1f err=%.1f jump=%d", copy.displayDistance,
            step.appliedCorrection, bJumpChanged ? 1 : 0);
    }

    // Propagate the frame's rail distance down the chain so any pool iteration order uses the same frame position
    for (int i = 0; i < copy.desc.carriageCount; i++)
    {
        CTrain* pCarriage = copy.pCarriages[i];
        if (pCarriage == nullptr || !copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) ||
            !pCarriage->IsVTableValid())
        {
            continue;
        }
        if (i == 0)
        {
            pCarriage->m_fCurrentRailDistance = copy.displayDistance;
        }
        else if (copy.pCarriages[i - 1] != nullptr)
        {
            pCarriage->m_fCurrentRailDistance = TrainSyncPolicy::WrapRailDistance(
                copy.pCarriages[i - 1]->m_fCurrentRailDistance + pCarriage->m_fLength, trackLen);
        }
    }
}

bool CreateCopyChain(SCopyTrain& copy, const char* reason)
{
    if (!CSessionLifecycle::IsLocalGameReady() || ms_bLayoutMismatch)
    {
        return false;
    }
    if (!CPools::ms_pVehiclePool || CPools::ms_pVehiclePool->GetNoOfFreeSpaces() < copy.desc.carriageCount)
    {
        CTrainSync::LogEvent("copy-create-failed", copy.desc.trainId, "reason=pool-full free=%d needed=%u",
            CPools::ms_pVehiclePool ? CPools::ms_pVehiclePool->GetNoOfFreeSpaces() : 0, copy.desc.carriageCount);
        return false;
    }

    DestroyCopyChainVehicles(copy, nullptr);

    // Request and load all models required by the train configuration
    uint16_t uniqueModels[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    unsigned char oldFlags[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    int uniqueCount = 0;
    for (int i = 0; i < copy.desc.carriageCount; i++)
    {
        const uint16_t modelId = copy.desc.carriages[i].modelid;
        bool bSeen = false;
        for (int u = 0; u < uniqueCount; u++)
        {
            if (uniqueModels[u] == modelId)
            {
                bSeen = true;
                break;
            }
        }
        if (!bSeen)
        {
            uniqueModels[uniqueCount] = modelId;
            oldFlags[uniqueCount] = CStreaming::ms_aInfoForModel[modelId].m_nFlags;
            CStreaming::RequestModel(modelId, GAME_REQUIRED);
            uniqueCount++;
        }
    }
    CStreaming::LoadAllRequestedModels(false);

    bool bAllLoaded = true;
    for (int u = 0; u < uniqueCount; u++)
    {
        if (CStreaming::ms_aInfoForModel[uniqueModels[u]].m_nLoadState != LOADSTATE_LOADED)
        {
            bAllLoaded = false;
            break;
        }
    }
    if (!bAllLoaded)
    {
        for (int u = 0; u < uniqueCount; u++)
        {
            if (!(oldFlags[u] & GAME_REQUIRED))
            {
                CStreaming::SetModelIsDeletable(uniqueModels[u]);
                CStreaming::SetModelTxdIsDeletable(uniqueModels[u]);
            }
        }
        CTrainSync::LogEvent("copy-create-failed", copy.desc.trainId, "reason=models-not-loaded");
        return false;
    }

    const uint32_t maxNodes = copy.desc.trackId < 4 ? GetTrackNodeCounts()[copy.desc.trackId] : 0;
    const int safeNode = (maxNodes > 0 && copy.desc.nodeIndex < maxNodes) ? static_cast<int>(copy.desc.nodeIndex) : 0;

    CTrain* pFirst = nullptr;
    CTrain* pLast = nullptr;
    CTrain::CreateMissionTrain(CVector(0.0f, 0.0f, 0.0f), copy.desc.bClockwise, copy.desc.configIndex, &pFirst, &pLast,
        safeNode, copy.desc.trackId, false);

    for (int u = 0; u < uniqueCount; u++)
    {
        if (!(oldFlags[u] & GAME_REQUIRED))
        {
            CStreaming::SetModelIsDeletable(uniqueModels[u]);
            CStreaming::SetModelTxdIsDeletable(uniqueModels[u]);
        }
    }

    if (pFirst == nullptr)
    {
        CTrainSync::LogEvent("copy-create-failed", copy.desc.trainId, "reason=engine-null config=%u",
            copy.desc.configIndex);
        return false;
    }

    // Collect and validate the created chain against the descriptor
    CTrain* createdChain[Packets::Train::MAX_TRAIN_CARRIAGES]{};
    int createdCount = 0;
    for (CTrain* pCurr = pFirst; pCurr != nullptr && createdCount < Packets::Train::MAX_TRAIN_CARRIAGES;
         pCurr = pCurr->m_pNextCarriage)
    {
        createdChain[createdCount++] = pCurr;
    }

    bool bValidChain = (createdCount == copy.desc.carriageCount);
    for (int i = 0; bValidChain && i < createdCount; i++)
    {
        if (static_cast<uint16_t>(createdChain[i]->m_nModelIndex) != copy.desc.carriages[i].modelid)
        {
            bValidChain = false;
        }
    }

    if (!bValidChain)
    {
        for (int i = 0; i < createdCount; i++)
        {
            DeleteSingleTrainCarriage(createdChain[i], -1);
        }
        CTrainSync::LogEvent("copy-mismatch", copy.desc.trainId, "created=%d expected=%u config=%u", createdCount,
            copy.desc.carriageCount, copy.desc.configIndex);
        copy = SCopyTrain{};
        return false;
    }

    for (int i = 0; i < createdCount; i++)
    {
        CTrain* pCarriage = createdChain[i];
        copy.pCarriages[i] = pCarriage;
        copy.carriageRefs[i].Capture(CPools::ms_pVehiclePool, pCarriage);
        copy.bBound[i] = false;

        pCarriage->m_nTrainFlags.bMissionTrain = 1;
        pCarriage->m_nTrainFlags.bStopsAtStations = 0;
        pCarriage->m_nTrainFlags.bForceSlowDown = 0;
        pCarriage->m_eDoorLock = static_cast<eDoorLock>(copy.desc.carriages[i].doorLock);
        CNetworkVehicleManager::GuardCopy(pCarriage);
    }

    copy.bCopyLost = false;
    copy.bDerailed = false;
    copy.lastSteppedFrame = 0;

    TryBindCopyCarriages(copy);
    ApplyCarriageStatesToCopy(copy);
    ApplyStatePayloadToCopy(copy, copy.bHasState ? copy.latestState : copy.desc.initialState, true);

    // Place the carriages at the initial rail distance right away if not derailed
    if (!copy.bDerailed)
    {
        for (int i = 0; i < createdCount; i++)
        {
            CTrainSync::ProcessControlCopy(copy.pCarriages[i]);
        }
    }

    if ( std::strcmp(reason, "recreate") == 0 )
    {
        ms_diag.copiesRecreated++;
    }
    else
    {
        ms_diag.copiesCreated++;
    }
    CTrainSync::LogEvent("copy-created", copy.desc.trainId, "reason=%s config=%u carriages=%u dist=%.1f", reason,
        copy.desc.configIndex, copy.desc.carriageCount, copy.displayDistance);
    return true;
}

void UpsertCopyDescriptor(const Packets::Train::STrainSpawnPayload& spawn, const char* source)
{
    if (spawn.trainId >= Packets::Train::MAX_TRAINS)
    {
        return;
    }
    if (!CheckLayoutHash(spawn.layoutHash))
    {
        return;
    }

    SCopyTrain& copy = ms_aCopies[spawn.trainId];
    if (copy.bHasDescriptor)
    {
        DestroyCopyChainVehicles(copy, "replace-descriptor");
    }

    copy = SCopyTrain{};
    copy.bHasDescriptor = true;
    copy.bCopyLost = true;
    copy.desc = spawn;
    copy.latestState = spawn.initialState;
    copy.bHasState = true;
    copy.lastStateRecvMs = GetTickCount();
    copy.appliedJumpCounter = spawn.initialState.jumpCounter;
    copy.targetRailDistance = spawn.initialState.railDistance;
    copy.displayDistance = spawn.initialState.railDistance;
    for (int i = 0; i < spawn.carriageCount && i < Packets::Train::MAX_TRAIN_CARRIAGES; i++)
    {
        copy.carriageStates[i].doorLock = spawn.carriages[i].doorLock;
        copy.carriageStates[i].health = 1000;
        copy.carriageStates[i].bWrecked = false;
    }
    copy.bHasCarriageStates = true;

    ms_diag.spawnsReceived++;
    CTrainSync::LogEvent("spawn-received", spawn.trainId, "source=%s config=%u carriages=%u track=%u node=%u", source,
        spawn.configIndex, spawn.carriageCount, spawn.trackId, spawn.nodeIndex);

    if (CSessionLifecycle::IsLocalGameReady())
    {
        CreateCopyChain(copy, source);
    }
}

void BuildHostStatePayload(const SHostTrain& slot, Packets::Train::STrainStatePayload& outState)
{
    const CTrain* pHead = slot.pCarriages[0];
    outState.sequence = slot.sequence;
    outState.flags = 0;
    if (pHead->m_nTrainFlags.bClockwiseDirection)
    {
        outState.flags |= Packets::Train::STATE_FLAG_CLOCKWISE;
    }
    if (pHead->m_nTrainFlags.bNotOnARailRoad)
    {
        outState.flags |= Packets::Train::STATE_FLAG_DERAILED;
    }
    if (HasNpcDriver(pHead))
    {
        outState.flags |= Packets::Train::STATE_FLAG_HAS_NPC_DRIVER;
    }
    if (pHead->m_nTrainFlags.bStoppedAtStation)
    {
        outState.flags |= Packets::Train::STATE_FLAG_STOPPED_AT_STATION;
    }
    outState.jumpCounter = slot.jumpCounter;
    outState.trackId = static_cast<uint8_t>(std::clamp<int>(pHead->m_nTrackId, 0, 3));
    outState.railDistance = pHead->m_fCurrentRailDistance;
    outState.speedQuantized = TrainSyncPolicy::QuantizeSpeed(pHead->m_fTrainSpeed);
    outState.gasHalf = TrainSyncPolicy::QuantizePedal(pHead->m_fTrainGas);
    outState.brakeHalf = TrainSyncPolicy::QuantizePedal(pHead->m_fTrainBrake);
}

void BuildHostSpawnPayload(const SHostTrain& slot, Packets::Train::STrainSpawnPayload& outSpawn)
{
    const CTrain* pHead = slot.pCarriages[0];
    outSpawn.trainId = slot.trainId;
    outSpawn.trackId = static_cast<uint8_t>(std::clamp<int>(pHead->m_nTrackId, 0, 3));
    outSpawn.configIndex = slot.configIndex;
    outSpawn.bClockwise = pHead->m_nTrainFlags.bClockwiseDirection != 0;
    outSpawn.nodeIndex = static_cast<uint16_t>(std::max<short>(0, pHead->m_nNodeIndex));
    outSpawn.carriageCount = slot.carriageCount;
    for (int i = 0; i < slot.carriageCount; i++)
    {
        const CTrain* pCarriage = slot.pCarriages[i];
        outSpawn.carriages[i].vehicleid = slot.vehicleIds[i];
        outSpawn.carriages[i].modelid = slot.models[i];
        outSpawn.carriages[i].doorLock = static_cast<uint8_t>(std::clamp<int>(pCarriage->m_eDoorLock, 0, 8));
    }
    outSpawn.layoutHash = ComputeLocalLayoutHash();
    BuildHostStatePayload(slot, outSpawn.initialState);
}

bool IsHostTrainIntact(const SHostTrain& slot)
{
    if (!slot.bUsed || slot.carriageCount < 1)
    {
        return false;
    }
    for (int i = 0; i < slot.carriageCount; i++)
    {
        CTrain* pCarriage = slot.pCarriages[i];
        if (pCarriage == nullptr || !slot.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCarriage) ||
            !pCarriage->IsVTableValid())
        {
            return false;
        }
        if (i == 0 && pCarriage->m_pPrevCarriage != nullptr)
        {
            return false;
        }
        if (i > 0 && pCarriage->m_pPrevCarriage != slot.pCarriages[i - 1])
        {
            return false;
        }
    }
    return true;
}

bool IsCarriageInAnyHostTrain(const CTrain* pTrain)
{
    for (const SHostTrain& slot : ms_aHostTrains)
    {
        if (!slot.bUsed)
        {
            continue;
        }
        for (int i = 0; i < slot.carriageCount; i++)
        {
            if (slot.pCarriages[i] == pTrain && slot.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pTrain))
            {
                return true;
            }
        }
    }
    return false;
}

void SendHostSnapshots(int targetPlayerId, bool bResetFirst)
{
    std::vector<int> confirmedSlots;
    for (int i = 0; i < Packets::Train::MAX_TRAINS; i++)
    {
        if (ms_aHostTrains[i].bUsed && ms_aHostTrains[i].bSpawnSent && IsHostTrainIntact(ms_aHostTrains[i]))
        {
            confirmedSlots.push_back(i);
        }
    }

    if (confirmedSlots.empty())
    {
        if (bResetFirst)
        {
            Packets::Train::TrainEvent packet{};
            packet.targetPlayerId = targetPlayerId;
            packet.kind = Packets::Train::eTrainEventKind::SNAPSHOT;
            packet.bReset = true;
            packet.snapshotCount = 0;
            GetPacketFactory().Send(packet);
            CTrainSync::LogEvent("snapshot-sent", -1, "target=%d count=0 reset=1", targetPlayerId);
        }
        return;
    }

    size_t offset = 0;
    bool bFirstPacket = true;
    while (offset < confirmedSlots.size())
    {
        Packets::Train::TrainEvent packet{};
        packet.targetPlayerId = targetPlayerId;
        packet.kind = Packets::Train::eTrainEventKind::SNAPSHOT;
        packet.bReset = bFirstPacket && bResetFirst;
        bFirstPacket = false;

        uint8_t count = 0;
        while (offset < confirmedSlots.size() && count < Packets::Train::MAX_SNAPSHOT_TRAINS)
        {
            BuildHostSpawnPayload(ms_aHostTrains[confirmedSlots[offset]], packet.snapshotTrains[count]);
            count++;
            offset++;
        }
        packet.snapshotCount = count;
        GetPacketFactory().Send(packet);
        CTrainSync::LogEvent("snapshot-sent", -1, "target=%d count=%u reset=%d", targetPlayerId, count,
            packet.bReset ? 1 : 0);
    }
}

void ProcessHost()
{
    if (!CPools::ms_pVehiclePool)
    {
        return;
    }
    const uint32_t now = GetTickCount();

    if (ms_bPendingHostResetSnapshot && now >= ms_nHostResetSnapshotDueMs)
    {
        ms_bPendingHostResetSnapshot = false;
        SendHostSnapshots(-1, true);
    }

    // 1. Validate existing host trains and send REMOVE for any chain that lost a carriage
    for (int i = 0; i < Packets::Train::MAX_TRAINS; i++)
    {
        SHostTrain& slot = ms_aHostTrains[i];
        if (!slot.bUsed)
        {
            continue;
        }
        if (!IsHostTrainIntact(slot))
        {
            if (slot.bSpawnSent)
            {
                Packets::Train::TrainEvent removeEvent{};
                removeEvent.kind = Packets::Train::eTrainEventKind::REMOVE;
                removeEvent.trainId = slot.trainId;
                GetPacketFactory().Send(removeEvent);
                CTrainSync::LogEvent("host-remove-sent", slot.trainId, "reason=chain-broken");
            }
            slot = SHostTrain{};
        }
    }

    // 2. Scan the vehicle pool for new train chain heads
    for (CVehicle* pVehicle : CPools::ms_pVehiclePool)
    {
        if (!IsTrainEntity(pVehicle))
        {
            continue;
        }
        auto* pHead = reinterpret_cast<CTrain*>(pVehicle);
        if (pHead->m_pPrevCarriage != nullptr || IsCarriageInAnyHostTrain(pHead))
        {
            continue;
        }

        CTrain* chain[Packets::Train::MAX_TRAIN_CARRIAGES]{};
        uint16_t models[Packets::Train::MAX_TRAIN_CARRIAGES]{};
        int count = 0;
        bool bOverflow = false;
        for (CTrain* pCurr = pHead; pCurr != nullptr; pCurr = pCurr->m_pNextCarriage)
        {
            if (!CPools::ms_pVehiclePool->IsObjectValid(pCurr) || !IsTrainEntity(pCurr))
            {
                bOverflow = true;
                break;
            }
            if (count >= Packets::Train::MAX_TRAIN_CARRIAGES)
            {
                bOverflow = true;
                break;
            }
            chain[count] = pCurr;
            models[count] = static_cast<uint16_t>(pCurr->m_nModelIndex);
            count++;
        }
        if (bOverflow || count < 1)
        {
            continue;
        }

        const int cfg = TrainSyncPolicy::MatchConfiguration(models, count, GetTrainConfigs());
        if (cfg < 0)
        {
            continue;
        }

        int freeSlot = -1;
        for (int s = 0; s < Packets::Train::MAX_TRAINS; s++)
        {
            if (!ms_aHostTrains[s].bUsed)
            {
                freeSlot = s;
                break;
            }
        }
        if (freeSlot < 0)
        {
            break;
        }

        SHostTrain& slot = ms_aHostTrains[freeSlot];
        slot = SHostTrain{};
        slot.bUsed = true;
        slot.trainId = static_cast<uint8_t>(freeSlot);
        slot.configIndex = static_cast<uint8_t>(cfg);
        slot.carriageCount = static_cast<uint8_t>(count);

        for (int i = 0; i < count; i++)
        {
            slot.pCarriages[i] = chain[i];
            slot.carriageRefs[i].Capture(CPools::ms_pVehiclePool, chain[i]);
            slot.models[i] = models[i];
            slot.vehicleIds[i] = -1;
            if (CNetworkVehicleManager::GetVehicle(chain[i]) == nullptr &&
                CNetworkVehicleManager::GetTempVehicle(chain[i]) == nullptr)
            {
                CNetworkVehicle::CreateHosted(chain[i]);
            }
        }
        CTrainSync::LogEvent("host-train-found", slot.trainId, "config=%u carriages=%u track=%d", slot.configIndex,
            slot.carriageCount, static_cast<int>(pHead->m_nTrackId));
    }

    // 3. Confirm carriage vehicle IDs, send SPAWN, CARRIAGES updates, and 5 Hz / immediate TRAIN_STATE
    for (int s = 0; s < Packets::Train::MAX_TRAINS; s++)
    {
        SHostTrain& slot = ms_aHostTrains[s];
        if (!slot.bUsed)
        {
            continue;
        }

        if (!slot.bSpawnSent)
        {
            bool bAllConfirmed = true;
            for (int i = 0; i < slot.carriageCount; i++)
            {
                CNetworkVehicle* pNetVeh = CNetworkVehicleManager::GetVehicle(slot.pCarriages[i]);
                if (pNetVeh != nullptr)
                {
                    slot.vehicleIds[i] = pNetVeh->m_nVehicleId;
                }
                else
                {
                    bAllConfirmed = false;
                    if (CNetworkVehicleManager::GetTempVehicle(slot.pCarriages[i]) == nullptr)
                    {
                        CNetworkVehicle::CreateHosted(slot.pCarriages[i]);
                    }
                }
            }
            if (!bAllConfirmed)
            {
                continue;
            }

            const CTrain* pHead = slot.pCarriages[0];
            slot.bSpawnSent = true;
            slot.lastStateSentMs = now;
            slot.lastRailDistance = pHead->m_fCurrentRailDistance;
            slot.lastSpeedPerStep = pHead->m_fTrainSpeed;
            slot.bLastMoving = std::fabs(pHead->m_fTrainSpeed) > 1e-4f;
            slot.bLastClockwise = pHead->m_nTrainFlags.bClockwiseDirection != 0;
            slot.bLastDerailed = pHead->m_nTrainFlags.bNotOnARailRoad != 0;
            slot.bLastStoppedAtStation = pHead->m_nTrainFlags.bStoppedAtStation != 0;

            for (int i = 0; i < slot.carriageCount; i++)
            {
                const CTrain* pCar = slot.pCarriages[i];
                slot.lastCarriageStates[i].doorLock = static_cast<uint8_t>(std::clamp<int>(pCar->m_eDoorLock, 0, 8));
                slot.lastCarriageStates[i].health =
                    static_cast<uint16_t>(std::clamp(pCar->m_fHealth, 0.0f, 65535.0f));
                slot.lastCarriageStates[i].bWrecked = (pCar->m_nStatus == STATUS_WRECKED);
            }

            Packets::Train::TrainEvent spawnEvent{};
            spawnEvent.kind = Packets::Train::eTrainEventKind::SPAWN;
            BuildHostSpawnPayload(slot, spawnEvent.spawn);
            GetPacketFactory().Send(spawnEvent);
            ms_diag.spawnsSent++;
            CTrainSync::LogEvent("host-spawn-sent", slot.trainId, "config=%u carriages=%u firstVeh=%d dist=%.1f",
                slot.configIndex, slot.carriageCount, slot.vehicleIds[0], slot.lastRailDistance);
            continue;
        }

        // Check for carriage property changes (lock, health, wrecked)
        bool bCarriagesChanged = false;
        for (int i = 0; i < slot.carriageCount; i++)
        {
            const CTrain* pCar = slot.pCarriages[i];
            const uint8_t lock = static_cast<uint8_t>(std::clamp<int>(pCar->m_eDoorLock, 0, 8));
            const uint16_t health = static_cast<uint16_t>(std::clamp(pCar->m_fHealth, 0.0f, 65535.0f));
            const bool bWrecked = (pCar->m_nStatus == STATUS_WRECKED);

            if (lock != slot.lastCarriageStates[i].doorLock || bWrecked != slot.lastCarriageStates[i].bWrecked ||
                std::abs(static_cast<int>(health) - static_cast<int>(slot.lastCarriageStates[i].health)) >= 5)
            {
                slot.lastCarriageStates[i].doorLock = lock;
                slot.lastCarriageStates[i].health = health;
                slot.lastCarriageStates[i].bWrecked = bWrecked;
                bCarriagesChanged = true;
            }
        }
        if (bCarriagesChanged)
        {
            Packets::Train::TrainEvent carEvent{};
            carEvent.kind = Packets::Train::eTrainEventKind::CARRIAGES;
            carEvent.trainId = slot.trainId;
            carEvent.carriageCount = slot.carriageCount;
            for (int i = 0; i < slot.carriageCount; i++)
            {
                carEvent.carriageStates[i] = slot.lastCarriageStates[i];
            }
            GetPacketFactory().Send(carEvent);
        }

        // Check immediate state triggers or 5 Hz interval
        const CTrain* pHead = slot.pCarriages[0];
        const float trackLen = GetTrackLength(static_cast<uint8_t>(std::clamp<int>(pHead->m_nTrackId, 0, 3)));
        const float expectedDist = TrainSyncPolicy::WrapRailDistance(
            slot.lastRailDistance + slot.lastSpeedPerStep * CTimer::ms_fTimeStep, trackLen);
        const bool bNowMoving = std::fabs(pHead->m_fTrainSpeed) > 1e-4f;
        const bool bNowClockwise = pHead->m_nTrainFlags.bClockwiseDirection != 0;
        const bool bNowDerailed = pHead->m_nTrainFlags.bNotOnARailRoad != 0;
        const bool bNowStopped = pHead->m_nTrainFlags.bStoppedAtStation != 0;
        bool bJumpDetected = false;

        const bool bImmediate = TrainSyncPolicy::ShouldSendImmediateState(slot.bLastMoving, bNowMoving,
            slot.bLastClockwise, bNowClockwise, slot.bLastDerailed, bNowDerailed, slot.bLastStoppedAtStation,
            bNowStopped, expectedDist, pHead->m_fCurrentRailDistance, trackLen, bJumpDetected);

        slot.lastRailDistance = pHead->m_fCurrentRailDistance;
        slot.lastSpeedPerStep = pHead->m_fTrainSpeed;
        slot.bLastMoving = bNowMoving;
        slot.bLastClockwise = bNowClockwise;
        slot.bLastDerailed = bNowDerailed;
        slot.bLastStoppedAtStation = bNowStopped;
        if (bJumpDetected && !bNowDerailed)
        {
            slot.jumpCounter++;
        }

        if (bImmediate || (now - slot.lastStateSentMs) >= TrainSyncPolicy::STATE_INTERVAL_MS)
        {
            slot.sequence++;
            slot.lastStateSentMs = now;
            Packets::Train::TrainState statePacket{};
            statePacket.trainId = slot.trainId;
            BuildHostStatePayload(slot, statePacket.state);
            GetPacketFactory().Send(statePacket);
        }
    }
}

void ProcessFollower()
{
    if (!CSessionLifecycle::IsLocalGameReady() || ms_bLayoutMismatch)
    {
        return;
    }

    RemoveLocalNonMissionTrains();

    const uint32_t now = GetTickCount();
    for (int t = 0; t < Packets::Train::MAX_TRAINS; t++)
    {
        SCopyTrain& copy = ms_aCopies[t];
        if (!copy.bHasDescriptor)
        {
            continue;
        }

        // Verify all carriages in the copy chain are still alive
        for (int i = 0; i < copy.desc.carriageCount; i++)
        {
            CTrain* pCar = copy.pCarriages[i];
            if (pCar == nullptr || !copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pCar) ||
                !pCar->IsVTableValid())
            {
                copy.pCarriages[i] = nullptr;
                copy.carriageRefs[i].Clear();
                copy.bBound[i] = false;
                copy.bCopyLost = true;
            }
        }

        if (copy.bCopyLost)
        {
            DestroyCopyChainVehicles(copy, "lost-cleanup");
            if (now - copy.lastRecreateAttemptMs >= RECREATE_THROTTLE_MS)
            {
                copy.lastRecreateAttemptMs = now;
                CreateCopyChain(copy, "recreate");
            }
            continue;
        }

        TryBindCopyCarriages(copy);
        for (int i = 0; i < copy.desc.carriageCount; i++)
        {
            if (copy.pCarriages[i] != nullptr)
            {
                CNetworkVehicleManager::GuardCopy(copy.pCarriages[i]);
            }
        }
    }
}

void RefreshDiagCounts()
{
    unsigned int hostCount = 0;
    for (const SHostTrain& slot : ms_aHostTrains)
    {
        if (slot.bUsed)
        {
            hostCount++;
        }
    }
    unsigned int copyCount = 0;
    unsigned int boundCount = 0;
    for (const SCopyTrain& copy : ms_aCopies)
    {
        if (copy.bHasDescriptor && !copy.bCopyLost)
        {
            copyCount++;
            for (int i = 0; i < copy.desc.carriageCount; i++)
            {
                if (copy.bBound[i])
                {
                    boundCount++;
                }
            }
        }
    }
    ms_diag.hostTrains = hostCount;
    ms_diag.copyTrains = copyCount;
    ms_diag.boundCarriages = boundCount;
}

bool AttachPedCleanly(CPed* pPed, CVehicle* pTarget, const TrainSyncPolicy::SAttachmentSpec& spec)
{
    if (pPed == nullptr || pTarget == nullptr || !CPools::ms_pPedPool || !CPools::ms_pVehiclePool ||
        !CPools::ms_pPedPool->IsObjectValid(pPed) || !CPools::ms_pVehiclePool->IsObjectValid(pTarget) ||
        !pPed->IsVTableValid() || !pTarget->IsVTableValid() || pPed->m_pRwClump == nullptr ||
        pPed->m_pIntelligence == nullptr)
    {
        return false;
    }

    if (pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle != nullptr)
    {
        if (CNetworkPlayer* pNetPlayer = CNetworkPlayerManager::GetPlayer(pPed))
        {
            pNetPlayer->RemoveFromVehicle(pPed->m_pVehicle);
            if (!pNetPlayer->IsModelReady())
            {
                return false;
            }
        }
        else if (CNetworkPed* pNetPed = CNetworkPedManager::GetPed(pPed))
        {
            pNetPed->RemoveFromVehicle(pPed->m_pVehicle);
            if (!pNetPed->IsModelReady())
            {
                return false;
            }
        }
        if (!CPools::ms_pVehiclePool->IsObjectValid(pTarget) || !pTarget->IsVTableValid())
        {
            return false;
        }
    }

    if (pPed->m_pAttachedTo != nullptr && pPed->m_pAttachedTo != pTarget)
    {
        DetachPedCleanly(pPed);
    }

    eWeaponType weaponToGive = WEAPON_UNARMED;
    if (spec.weaponType != static_cast<uint8_t>(WEAPON_UNARMED))
    {
        const int weaponModel = CUtil::GetWeaponModelById(spec.weaponType);
        if (weaponModel >= 0 && CStreaming::ms_aInfoForModel[weaponModel].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(weaponModel, eStreamingFlags::GAME_REQUIRED | eStreamingFlags::MISSION_REQUIRED |
                                                      eStreamingFlags::KEEP_IN_MEMORY |
                                                      eStreamingFlags::PRIORITY_REQUEST);
            CStreaming::LoadAllRequestedModels(true);
            if (!CPools::ms_pPedPool->IsObjectValid(pPed) || !pPed->IsVTableValid() || pPed->m_pRwClump == nullptr ||
                pPed->m_pIntelligence == nullptr || !CPools::ms_pVehiclePool->IsObjectValid(pTarget) ||
                !pTarget->IsVTableValid())
            {
                return false;
            }
        }
        if (weaponModel < 0 || CStreaming::ms_aInfoForModel[weaponModel].m_nLoadState == LOADSTATE_LOADED)
        {
            weaponToGive = static_cast<eWeaponType>(spec.weaponType);
        }
    }

    const int savedType = pPed->m_nPedType;
    if (savedType == PED_TYPE_PLAYER1 || savedType == PED_TYPE_PLAYER2)
    {
        pPed->m_nPedType = PED_TYPE_CIVMALE;
    }
    pPed->AttachPedToEntity(pTarget, CVector(spec.offsetX, spec.offsetY, spec.offsetZ), spec.turretPosition,
        spec.angleLimit, weaponToGive);
    pPed->m_nPedType = savedType;
    return pPed->m_pAttachedTo == pTarget;
}

bool AttachVehicleCleanly(CVehicle* pVehicle, CVehicle* pTarget, const TrainSyncPolicy::SAttachmentSpec& spec)
{
    if (pVehicle == nullptr || pTarget == nullptr || pVehicle == pTarget || !CPools::ms_pVehiclePool ||
        !CPools::ms_pVehiclePool->IsObjectValid(pVehicle) || !CPools::ms_pVehiclePool->IsObjectValid(pTarget) ||
        !pVehicle->IsVTableValid() || !pTarget->IsVTableValid())
    {
        return false;
    }
    if (pVehicle->m_pAttachedTo != nullptr && pVehicle->m_pAttachedTo != pTarget)
    {
        DetachVehicleCleanly(pVehicle);
    }
    pVehicle->AttachEntityToEntity(
        pTarget, CVector(spec.offsetX, spec.offsetY, spec.offsetZ), CVector(spec.rotX, spec.rotY, spec.rotZ));
    return pVehicle->m_pAttachedTo == pTarget;
}

// Reads a ped's live vehicle attachment state. Returns false if attached to a vehicle whose network ID is still pending
// confirmation (so the caller waits rather than sending a premature detach or unconfirmed ID).
bool ReadPedAttachment(const CPed* pPed, TrainSyncPolicy::SAttachmentSpec& outSpec)
{
    outSpec = TrainSyncPolicy::SAttachmentSpec{};
    if (pPed == nullptr || pPed->m_pAttachedTo == nullptr)
    {
        return true;
    }
    CEntity* pTarget = pPed->m_pAttachedTo;
    if (pTarget->m_nType != ENTITY_TYPE_VEHICLE || !CPools::ms_pVehiclePool ||
        !CPools::ms_pVehiclePool->IsObjectValid(reinterpret_cast<CVehicle*>(pTarget)))
    {
        return true;
    }
    auto* pTargetVeh = reinterpret_cast<CVehicle*>(pTarget);
    CNetworkVehicle* pNetVeh = CNetworkVehicleManager::GetVehicle(pTargetVeh);
    if (pNetVeh == nullptr || pNetVeh->m_nVehicleId < 0)
    {
        if (CNetworkVehicleManager::GetTempVehicle(pTargetVeh) != nullptr || IsTrainEntity(pTargetVeh))
        {
            return false;
        }
        return true;
    }

    const uintptr_t pedAddr = reinterpret_cast<uintptr_t>(pPed);
    const uint16_t rawPos = *reinterpret_cast<const uint16_t*>(pedAddr + 0x778);
    const float rawAngle = *reinterpret_cast<const float*>(pedAddr + 0x77C);

    outSpec.bAttached = true;
    outSpec.targetVehicleId = pNetVeh->m_nVehicleId;
    outSpec.offsetX = pPed->m_vecTurretOffset.x;
    outSpec.offsetY = pPed->m_vecTurretOffset.y;
    outSpec.offsetZ = pPed->m_vecTurretOffset.z;
    outSpec.turretPosition = static_cast<uint8_t>(std::min<uint16_t>(rawPos, 3));
    outSpec.angleLimit = rawAngle;
    outSpec.weaponType = static_cast<uint8_t>(pPed->m_aWeapons[pPed->m_nActiveWeaponSlot].m_eWeaponType);
    return true;
}

bool ReadVehicleAttachment(const CVehicle* pVehicle, TrainSyncPolicy::SAttachmentSpec& outSpec)
{
    outSpec = TrainSyncPolicy::SAttachmentSpec{};
    if (pVehicle == nullptr || pVehicle->m_pAttachedTo == nullptr)
    {
        return true;
    }
    CEntity* pTarget = pVehicle->m_pAttachedTo;
    if (pTarget->m_nType != ENTITY_TYPE_VEHICLE || !CPools::ms_pVehiclePool ||
        !CPools::ms_pVehiclePool->IsObjectValid(reinterpret_cast<CVehicle*>(pTarget)))
    {
        return true;
    }
    auto* pTargetVeh = reinterpret_cast<CVehicle*>(pTarget);
    CNetworkVehicle* pNetVeh = CNetworkVehicleManager::GetVehicle(pTargetVeh);
    if (pNetVeh == nullptr || pNetVeh->m_nVehicleId < 0)
    {
        if (CNetworkVehicleManager::GetTempVehicle(pTargetVeh) != nullptr || IsTrainEntity(pTargetVeh))
        {
            return false;
        }
        return true;
    }

    outSpec.bAttached = true;
    outSpec.targetVehicleId = pNetVeh->m_nVehicleId;
    outSpec.offsetX = pVehicle->m_vecAttachOffset.x;
    outSpec.offsetY = pVehicle->m_vecAttachOffset.y;
    outSpec.offsetZ = pVehicle->m_vecAttachOffset.z;
    outSpec.rotX = pVehicle->m_vecAttachedEntityPosn.x;
    outSpec.rotY = pVehicle->m_vecAttachedEntityPosn.y;
    outSpec.rotZ = pVehicle->m_vecAttachedEntityPosn.z;
    return true;
}

void SendEntityAttachPacket(Packets::Train::eAttachEntityKind kind, int entityId,
    const TrainSyncPolicy::SAttachmentSpec& spec, int targetPlayerId)
{
    Packets::Train::EntityAttach packet{};
    packet.targetPlayerId = targetPlayerId;
    packet.entityKind = kind;
    packet.entityId = entityId;
    packet.bAttached = spec.bAttached;
    if (spec.bAttached)
    {
        packet.targetVehicleId = spec.targetVehicleId;
        packet.vecOffset = CVector(spec.offsetX, spec.offsetY, spec.offsetZ);
        packet.nTurretPosition = spec.turretPosition;
        packet.fAngleLimit = spec.angleLimit;
        packet.nWeaponType = spec.weaponType;
        packet.vecRotation = CVector(spec.rotX, spec.rotY, spec.rotZ);
    }
    GetPacketFactory().Send(packet);
    ms_diag.attachesSent++;
    CTrainSync::LogEvent("attach-sent", -1, "kind=%u id=%d attached=%d veh=%d targetPlayer=%d",
        static_cast<unsigned>(kind), entityId, spec.bAttached ? 1 : 0, spec.targetVehicleId, targetPlayerId);
}

void SendOwnedAttachmentsToPlayer(int targetPlayerId)
{
    if (targetPlayerId < 0 || !CSessionLifecycle::IsAuthenticated() || !CSessionLifecycle::IsLocalGameReady())
    {
        return;
    }

    const int myId = CSessionLifecycle::GetMyId();
    if (myId >= 0 && myId < Config::MAX_SERVER_PLAYERS)
    {
        if (CPlayerPed* pLocal = FindPlayerPed(0))
        {
            TrainSyncPolicy::SAttachmentSpec spec{};
            if (ReadPedAttachment(pLocal, spec) && spec.bAttached)
            {
                SendEntityAttachPacket(Packets::Train::eAttachEntityKind::PLAYER, myId, spec, targetPlayerId);
            }
        }
    }

    for (CNetworkPed* pNetPed : CNetworkPedManager::m_pPeds)
    {
        if (pNetPed == nullptr || !pNetPed->m_bSyncing || pNetPed->m_nPedId < 0 ||
            pNetPed->m_nPedId >= Config::MAX_SERVER_PEDS)
        {
            continue;
        }
        if (CPed* pPed = pNetPed->GetLivePed())
        {
            TrainSyncPolicy::SAttachmentSpec spec{};
            if (ReadPedAttachment(pPed, spec) && spec.bAttached)
            {
                SendEntityAttachPacket(
                    Packets::Train::eAttachEntityKind::PED, pNetPed->m_nPedId, spec, targetPlayerId);
            }
        }
    }

    for (CNetworkVehicle* pNetVeh : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetVeh == nullptr || !pNetVeh->m_bSyncing || pNetVeh->m_nVehicleId < 0 ||
            pNetVeh->m_nVehicleId >= Config::MAX_SERVER_VEHICLES || CNetworkVehicle::IsTrainModel(pNetVeh->m_nModelId))
        {
            continue;
        }
        if (CVehicle* pVeh = pNetVeh->GetLiveVehicle())
        {
            TrainSyncPolicy::SAttachmentSpec spec{};
            if (ReadVehicleAttachment(pVeh, spec) && spec.bAttached)
            {
                SendEntityAttachPacket(
                    Packets::Train::eAttachEntityKind::VEHICLE, pNetVeh->m_nVehicleId, spec, targetPlayerId);
            }
        }
    }
}

void PollOwnedAttachments()
{
    // 1. Local player
    const int myId = CSessionLifecycle::GetMyId();
    if (myId >= 0 && myId < Config::MAX_SERVER_PLAYERS)
    {
        if (CPlayerPed* pLocal = FindPlayerPed(0))
        {
            TrainSyncPolicy::SAttachmentSpec current{};
            if (ReadPedAttachment(pLocal, current))
            {
                if (TrainSyncPolicy::ShouldSendAttachmentUpdate(
                        ms_localPlayerAttachSent.bHadSent, ms_localPlayerAttachSent.lastSent, current, true))
                {
                    ms_localPlayerAttachSent.bHadSent = true;
                    ms_localPlayerAttachSent.lastSent = current;
                    SendEntityAttachPacket(Packets::Train::eAttachEntityKind::PLAYER, myId, current, -1);
                }
            }
        }
    }

    // 2. Synced peds
    bool abSeenSyncedPed[Config::MAX_SERVER_PEDS]{};
    for (CNetworkPed* pNetPed : CNetworkPedManager::m_pPeds)
    {
        if (pNetPed == nullptr || !pNetPed->m_bSyncing || pNetPed->m_nPedId < 0 ||
            pNetPed->m_nPedId >= Config::MAX_SERVER_PEDS)
        {
            continue;
        }
        const int pedId = pNetPed->m_nPedId;
        abSeenSyncedPed[pedId] = true;
        CPed* pPed = pNetPed->GetLivePed();
        if (pPed == nullptr)
        {
            continue;
        }

        TrainSyncPolicy::SAttachmentSpec current{};
        if (!ReadPedAttachment(pPed, current))
        {
            continue;
        }
        SOwnedAttachmentState& state = ms_aOwnedPedAttachSent[pedId];
        if (TrainSyncPolicy::ShouldSendAttachmentUpdate(state.bHadSent, state.lastSent, current, true))
        {
            state.bHadSent = true;
            state.lastSent = current;
            SendEntityAttachPacket(Packets::Train::eAttachEntityKind::PED, pedId, current, -1);
        }
    }
    for (int i = 0; i < Config::MAX_SERVER_PEDS; i++)
    {
        if (!abSeenSyncedPed[i])
        {
            ms_aOwnedPedAttachSent[i] = SOwnedAttachmentState{};
        }
    }

    // 3. Synced non-train vehicles
    bool abSeenSyncedVehicle[Config::MAX_SERVER_VEHICLES]{};
    for (CNetworkVehicle* pNetVeh : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetVeh == nullptr || !pNetVeh->m_bSyncing || pNetVeh->m_nVehicleId < 0 ||
            pNetVeh->m_nVehicleId >= Config::MAX_SERVER_VEHICLES || CNetworkVehicle::IsTrainModel(pNetVeh->m_nModelId))
        {
            continue;
        }
        const int vehId = pNetVeh->m_nVehicleId;
        abSeenSyncedVehicle[vehId] = true;
        CVehicle* pVeh = pNetVeh->GetLiveVehicle();
        if (pVeh == nullptr)
        {
            continue;
        }

        TrainSyncPolicy::SAttachmentSpec current{};
        if (!ReadVehicleAttachment(pVeh, current))
        {
            continue;
        }
        SOwnedAttachmentState& state = ms_aOwnedVehicleAttachSent[vehId];
        if (TrainSyncPolicy::ShouldSendAttachmentUpdate(state.bHadSent, state.lastSent, current, false))
        {
            state.bHadSent = true;
            state.lastSent = current;
            SendEntityAttachPacket(Packets::Train::eAttachEntityKind::VEHICLE, vehId, current, -1);
        }
    }
    for (int i = 0; i < Config::MAX_SERVER_VEHICLES; i++)
    {
        if (!abSeenSyncedVehicle[i])
        {
            ms_aOwnedVehicleAttachSent[i] = SOwnedAttachmentState{};
        }
    }
}

void TryApplyRemotePlayerAttachment(int playerId)
{
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }
    SRemoteAttachmentState& slot = ms_aRemotePlayerAttachments[playerId];
    if (!slot.bActive)
    {
        return;
    }
    CNetworkPlayer* pNetPlayer = CNetworkPlayerManager::GetPlayer(playerId);
    if (pNetPlayer == nullptr)
    {
        slot = SRemoteAttachmentState{};
        return;
    }
    CPlayerPed* pPed = pNetPlayer->GetReadyPed();
    CNetworkVehicle* pTargetNetVeh = CNetworkVehicleManager::GetVehicle(slot.spec.targetVehicleId);
    CVehicle* pTargetVeh = pTargetNetVeh != nullptr ? pTargetNetVeh->GetLiveVehicle() : nullptr;
    if (pPed == nullptr || pTargetVeh == nullptr)
    {
        return;
    }
    if (pPed->m_pAttachedTo != pTargetVeh || slot.pLastSubject != pPed || slot.pLastTarget != pTargetVeh)
    {
        if (AttachPedCleanly(pPed, pTargetVeh, slot.spec))
        {
            slot.pLastSubject = pPed;
            slot.pLastTarget = pTargetVeh;
            ms_diag.attachesApplied++;
            CTrainSync::LogEvent("attach-applied", -1, "kind=player id=%d veh=%d", playerId, slot.spec.targetVehicleId);
        }
    }
}

void TryApplyRemotePedAttachment(int pedId)
{
    if (pedId < 0 || pedId >= Config::MAX_SERVER_PEDS)
    {
        return;
    }
    SRemoteAttachmentState& slot = ms_aRemotePedAttachments[pedId];
    if (!slot.bActive)
    {
        return;
    }
    CNetworkPed* pNetPed = CNetworkPedManager::GetPed(pedId);
    if (pNetPed == nullptr)
    {
        slot = SRemoteAttachmentState{};
        return;
    }
    if (pNetPed->m_bSyncing)
    {
        ms_aOwnedPedAttachSent[pedId].bHadSent = true;
        ms_aOwnedPedAttachSent[pedId].lastSent = slot.spec;
        slot = SRemoteAttachmentState{};
        return;
    }
    if (!pNetPed->IsModelReady())
    {
        return;
    }
    CPed* pPed = pNetPed->GetLivePed();
    CNetworkVehicle* pTargetNetVeh = CNetworkVehicleManager::GetVehicle(slot.spec.targetVehicleId);
    CVehicle* pTargetVeh = pTargetNetVeh != nullptr ? pTargetNetVeh->GetLiveVehicle() : nullptr;
    if (pPed == nullptr || pTargetVeh == nullptr)
    {
        return;
    }
    if (pPed->m_pAttachedTo != pTargetVeh || slot.pLastSubject != pPed || slot.pLastTarget != pTargetVeh)
    {
        if (AttachPedCleanly(pPed, pTargetVeh, slot.spec))
        {
            slot.pLastSubject = pPed;
            slot.pLastTarget = pTargetVeh;
            ms_diag.attachesApplied++;
            CTrainSync::LogEvent("attach-applied", -1, "kind=ped id=%d veh=%d", pedId, slot.spec.targetVehicleId);
        }
    }
}

void TryApplyRemoteVehicleAttachment(int vehicleId)
{
    if (vehicleId < 0 || vehicleId >= Config::MAX_SERVER_VEHICLES)
    {
        return;
    }
    SRemoteAttachmentState& slot = ms_aRemoteVehicleAttachments[vehicleId];
    if (!slot.bActive)
    {
        return;
    }
    CNetworkVehicle* pNetVeh = CNetworkVehicleManager::GetVehicle(vehicleId);
    if (pNetVeh == nullptr)
    {
        slot = SRemoteAttachmentState{};
        return;
    }
    if (pNetVeh->m_bSyncing)
    {
        ms_aOwnedVehicleAttachSent[vehicleId].bHadSent = true;
        ms_aOwnedVehicleAttachSent[vehicleId].lastSent = slot.spec;
        slot = SRemoteAttachmentState{};
        return;
    }
    CVehicle* pVeh = pNetVeh->GetLiveVehicle();
    CNetworkVehicle* pTargetNetVeh = CNetworkVehicleManager::GetVehicle(slot.spec.targetVehicleId);
    CVehicle* pTargetVeh = pTargetNetVeh != nullptr ? pTargetNetVeh->GetLiveVehicle() : nullptr;
    if (pVeh == nullptr || pTargetVeh == nullptr)
    {
        return;
    }
    if (pVeh->m_pAttachedTo != pTargetVeh || slot.pLastSubject != pVeh || slot.pLastTarget != pTargetVeh)
    {
        if (AttachVehicleCleanly(pVeh, pTargetVeh, slot.spec))
        {
            slot.pLastSubject = pVeh;
            slot.pLastTarget = pTargetVeh;
            ms_diag.attachesApplied++;
            CTrainSync::LogEvent(
                "attach-applied", -1, "kind=vehicle id=%d veh=%d", vehicleId, slot.spec.targetVehicleId);
        }
    }
}

void ProcessRemoteAttachments()
{
    for (int i = 0; i < Config::MAX_SERVER_PLAYERS; i++)
    {
        if (ms_aRemotePlayerAttachments[i].bActive)
        {
            TryApplyRemotePlayerAttachment(i);
        }
    }
    for (int i = 0; i < Config::MAX_SERVER_PEDS; i++)
    {
        if (ms_aRemotePedAttachments[i].bActive)
        {
            TryApplyRemotePedAttachment(i);
        }
    }
    for (int i = 0; i < Config::MAX_SERVER_VEHICLES; i++)
    {
        if (ms_aRemoteVehicleAttachments[i].bActive)
        {
            TryApplyRemoteVehicleAttachment(i);
        }
    }
}

void ClearAllAttachmentState(bool bKeepRemoteSpecs)
{
    ms_localPlayerAttachSent = SOwnedAttachmentState{};
    for (SOwnedAttachmentState& s : ms_aOwnedPedAttachSent)
    {
        s = SOwnedAttachmentState{};
    }
    for (SOwnedAttachmentState& s : ms_aOwnedVehicleAttachSent)
    {
        s = SOwnedAttachmentState{};
    }
    for (SRemoteAttachmentState& r : ms_aRemotePlayerAttachments)
    {
        if (bKeepRemoteSpecs)
        {
            r.pLastSubject = nullptr;
            r.pLastTarget = nullptr;
        }
        else
        {
            r = SRemoteAttachmentState{};
        }
    }
    for (SRemoteAttachmentState& r : ms_aRemotePedAttachments)
    {
        if (bKeepRemoteSpecs)
        {
            r.pLastSubject = nullptr;
            r.pLastTarget = nullptr;
        }
        else
        {
            r = SRemoteAttachmentState{};
        }
    }
    for (SRemoteAttachmentState& r : ms_aRemoteVehicleAttachments)
    {
        if (bKeepRemoteSpecs)
        {
            r.pLastSubject = nullptr;
            r.pLastTarget = nullptr;
        }
        else
        {
            r = SRemoteAttachmentState{};
        }
    }
}

void OnSessionLifecycleEvent(const SSessionEvent& event)
{
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            if (CSessionLifecycle::IsHost())
            {
                CTrainSync::SendSnapshotToPlayer(event.playerId);
            }
            SendOwnedAttachmentsToPlayer(event.playerId);
            break;

        case eSessionEvent::HOST_CHANGED:
            if (CSessionLifecycle::IsHost())
            {
                ms_nLastKnownHostId = CSessionLifecycle::GetHostId();
                RemoveAllCopies("became-host");
            }
            else if (CSessionLifecycle::HasHost())
            {
                const int newHostId = CSessionLifecycle::GetHostId();
                if (ms_nLastKnownHostId != -1 && ms_nLastKnownHostId != newHostId)
                {
                    RemoveAllCopies("host-switched");
                }
                ms_nLastKnownHostId = newHostId;
                for (SHostTrain& slot : ms_aHostTrains)
                {
                    slot = SHostTrain{};
                }
                if (CSessionLifecycle::IsLocalGameReady())
                {
                    RemoveLocalNonMissionTrains();
                }
            }
            else
            {
                ms_nLastKnownHostId = -1;
                RemoveAllCopies("host-lost");
            }
            break;

        case eSessionEvent::DISCONNECTED:
            ms_nLastKnownHostId = -1;
            ms_bPendingHostResetSnapshot = false;
            for (SHostTrain& slot : ms_aHostTrains)
            {
                slot = SHostTrain{};
            }
            RemoveAllCopies("disconnected");
            ClearAllAttachmentState(false);
            break;

        case eSessionEvent::GAME_RESTARTED:
        case eSessionEvent::SAVE_LOADED:
            CTrainSync::ClearForRestart();
            break;

        default:
            break;
    }
}
}  // namespace

void CTrainSync::Init()
{
    if (ms_bInitialized)
    {
        return;
    }
    ms_bInitialized = true;
    CSessionLifecycle::Subscribe(OnSessionLifecycleEvent);
}

void CTrainSync::Process()
{
    if (!ms_bInitialized)
    {
        Init();
    }

    if (CSessionLifecycle::IsHost() && CSessionLifecycle::IsLocalGameReady())
    {
        ProcessHost();
    }
    else if (CSessionLifecycle::HasHost() && !CSessionLifecycle::IsHost())
    {
        ProcessFollower();
    }

    if (CSessionLifecycle::IsAuthenticated() && CSessionLifecycle::IsLocalGameReady())
    {
        PollOwnedAttachments();
        ProcessRemoteAttachments();
    }

    RefreshDiagCounts();
}

void CTrainSync::HandleTrainState(const Packets::Train::TrainState& packet)
{
    if (!CSessionLifecycle::HasHost() || CSessionLifecycle::IsHost() ||
        packet.playerid.value != CSessionLifecycle::GetHostId())
    {
        return;
    }
    if (packet.trainId >= Packets::Train::MAX_TRAINS || ms_bLayoutMismatch)
    {
        return;
    }

    SCopyTrain& copy = ms_aCopies[packet.trainId];
    if (!copy.bHasDescriptor)
    {
        return;
    }
    if (!TrainSyncPolicy::IsSequenceNewer(packet.state.sequence, copy.latestState.sequence, copy.bHasState))
    {
        return;
    }

    ApplyStatePayloadToCopy(copy, packet.state, false);
}

void CTrainSync::HandleTrainEvent(const Packets::Train::TrainEvent& packet)
{
    if (packet.kind == Packets::Train::eTrainEventKind::REQUEST)
    {
        if (!CSessionLifecycle::IsHost() || !CSessionLifecycle::IsLocalGameReady())
        {
            return;
        }
        if (packet.trainId >= Packets::Train::MAX_TRAINS)
        {
            return;
        }
        SHostTrain& slot = ms_aHostTrains[packet.trainId];
        if (!slot.bUsed || !slot.bSpawnSent || !IsHostTrainIntact(slot))
        {
            return;
        }
        if (packet.requestKind == Packets::Train::eTrainRequestKind::SKIP_STATION)
        {
            CTrain* pHead = slot.pCarriages[0];
            if (!pHead->m_nTrainFlags.bNotOnARailRoad)
            {
                CTrain::SkipToNextAllowedStation(pHead);
                ms_diag.skipRequests++;
                LogEvent("host-skip-applied", slot.trainId, "requester=%d newDist=%.1f", packet.playerid.value,
                    pHead->m_fCurrentRailDistance);
                // ProcessHost will detect the rail distance jump and immediately broadcast TRAIN_STATE
            }
        }
        return;
    }

    if (!CSessionLifecycle::HasHost() || CSessionLifecycle::IsHost() ||
        packet.playerid.value != CSessionLifecycle::GetHostId())
    {
        return;
    }

    switch (packet.kind)
    {
        case Packets::Train::eTrainEventKind::SPAWN:
            UpsertCopyDescriptor(packet.spawn, "spawn");
            break;

        case Packets::Train::eTrainEventKind::REMOVE:
            RemoveCopySlot(packet.trainId, "event-remove");
            break;

        case Packets::Train::eTrainEventKind::CARRIAGES:
            if (packet.trainId < Packets::Train::MAX_TRAINS)
            {
                SCopyTrain& copy = ms_aCopies[packet.trainId];
                if (copy.bHasDescriptor && packet.carriageCount == copy.desc.carriageCount)
                {
                    for (int i = 0; i < packet.carriageCount; i++)
                    {
                        copy.carriageStates[i] = packet.carriageStates[i];
                        copy.desc.carriages[i].doorLock = packet.carriageStates[i].doorLock;
                    }
                    copy.bHasCarriageStates = true;
                    ApplyCarriageStatesToCopy(copy);
                }
            }
            break;

        case Packets::Train::eTrainEventKind::SNAPSHOT:
            if (packet.bReset)
            {
                RemoveAllCopies("snapshot-reset");
            }
            for (int i = 0; i < packet.snapshotCount && i < Packets::Train::MAX_SNAPSHOT_TRAINS; i++)
            {
                UpsertCopyDescriptor(packet.snapshotTrains[i], "snapshot");
            }
            break;

        default:
            break;
    }
}

void CTrainSync::HandleEntityAttach(const Packets::Train::EntityAttach& packet)
{
    if (!CSessionLifecycle::IsAuthenticated())
    {
        return;
    }

    TrainSyncPolicy::SAttachmentSpec spec{};
    spec.bAttached = packet.bAttached;
    spec.targetVehicleId = packet.targetVehicleId;
    spec.offsetX = packet.vecOffset.x;
    spec.offsetY = packet.vecOffset.y;
    spec.offsetZ = packet.vecOffset.z;
    spec.turretPosition = packet.nTurretPosition;
    spec.angleLimit = packet.fAngleLimit;
    spec.weaponType = packet.nWeaponType;
    spec.rotX = packet.vecRotation.x;
    spec.rotY = packet.vecRotation.y;
    spec.rotZ = packet.vecRotation.z;

    switch (packet.entityKind)
    {
        case Packets::Train::eAttachEntityKind::PLAYER:
        {
            if (packet.entityId < 0 || packet.entityId >= Config::MAX_SERVER_PLAYERS ||
                packet.entityId == CSessionLifecycle::GetMyId())
            {
                return;
            }
            SRemoteAttachmentState& slot = ms_aRemotePlayerAttachments[packet.entityId];
            if (packet.bAttached)
            {
                slot.bActive = true;
                slot.spec = spec;
                slot.pLastSubject = nullptr;
                slot.pLastTarget = nullptr;
                if (CSessionLifecycle::IsLocalGameReady())
                {
                    TryApplyRemotePlayerAttachment(packet.entityId);
                }
            }
            else
            {
                if (CNetworkPlayer* pNetPlayer = CNetworkPlayerManager::GetPlayer(packet.entityId))
                {
                    DetachPedCleanly(pNetPlayer->GetLivePed());
                }
                slot = SRemoteAttachmentState{};
                LogEvent("detach-applied", -1, "kind=player id=%d", packet.entityId);
            }
            break;
        }

        case Packets::Train::eAttachEntityKind::PED:
        {
            if (packet.entityId < 0 || packet.entityId >= Config::MAX_SERVER_PEDS)
            {
                return;
            }
            CNetworkPed* pNetPed = CNetworkPedManager::GetPed(packet.entityId);
            if (pNetPed != nullptr && pNetPed->m_bSyncing)
            {
                return;
            }
            SRemoteAttachmentState& slot = ms_aRemotePedAttachments[packet.entityId];
            if (packet.bAttached)
            {
                slot.bActive = true;
                slot.spec = spec;
                slot.pLastSubject = nullptr;
                slot.pLastTarget = nullptr;
                if (CSessionLifecycle::IsLocalGameReady())
                {
                    TryApplyRemotePedAttachment(packet.entityId);
                }
            }
            else
            {
                if (pNetPed != nullptr)
                {
                    DetachPedCleanly(pNetPed->GetLivePed());
                }
                slot = SRemoteAttachmentState{};
                LogEvent("detach-applied", -1, "kind=ped id=%d", packet.entityId);
            }
            break;
        }

        case Packets::Train::eAttachEntityKind::VEHICLE:
        {
            if (packet.entityId < 0 || packet.entityId >= Config::MAX_SERVER_VEHICLES)
            {
                return;
            }
            CNetworkVehicle* pNetVeh = CNetworkVehicleManager::GetVehicle(packet.entityId);
            if (pNetVeh != nullptr && pNetVeh->m_bSyncing)
            {
                return;
            }
            SRemoteAttachmentState& slot = ms_aRemoteVehicleAttachments[packet.entityId];
            if (packet.bAttached)
            {
                slot.bActive = true;
                slot.spec = spec;
                slot.pLastSubject = nullptr;
                slot.pLastTarget = nullptr;
                if (CSessionLifecycle::IsLocalGameReady())
                {
                    TryApplyRemoteVehicleAttachment(packet.entityId);
                }
            }
            else
            {
                if (pNetVeh != nullptr)
                {
                    DetachVehicleCleanly(pNetVeh->GetLiveVehicle());
                }
                slot = SRemoteAttachmentState{};
                LogEvent("detach-applied", -1, "kind=vehicle id=%d", packet.entityId);
            }
            break;
        }

        default:
            break;
    }
}

void CTrainSync::SendSnapshotToPlayer(int playerId)
{
    if (!CLocalPlayer::m_bIsHost || playerId < 0)
    {
        return;
    }
    SendHostSnapshots(playerId, true);
}

void CTrainSync::OnGameRestartCleared()
{
    ClearForRestart();
}

void CTrainSync::ClearForRestart()
{
    ClearAllAttachmentState(!CLocalPlayer::m_bIsHost);
    if (CLocalPlayer::m_bIsHost)
    {
        for (SHostTrain& slot : ms_aHostTrains)
        {
            slot = SHostTrain{};
        }
        ms_bPendingHostResetSnapshot = true;
        ms_nHostResetSnapshotDueMs = GetTickCount() + HOST_RESET_SNAPSHOT_DELAY_MS;
        LogEvent("host-restart-clear", -1, "reset-snapshot-scheduled=1");
    }
    else
    {
        for (SCopyTrain& copy : ms_aCopies)
        {
            if (!copy.bHasDescriptor)
            {
                continue;
            }
            for (int i = 0; i < copy.desc.carriageCount; i++)
            {
                copy.pCarriages[i] = nullptr;
                copy.carriageRefs[i].Clear();
                copy.bBound[i] = false;
            }
            copy.bCopyLost = true;
        }
        LogEvent("follower-restart-clear", -1, "descriptors-kept=1");
    }
}

void CTrainSync::OnVehicleDestroyed(CVehicle* pVehicle)
{
    if (pVehicle == nullptr)
    {
        return;
    }

    // Check host trains
    for (SHostTrain& slot : ms_aHostTrains)
    {
        if (!slot.bUsed)
        {
            continue;
        }
        for (int i = 0; i < slot.carriageCount; i++)
        {
            if (slot.pCarriages[i] == pVehicle && slot.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pVehicle))
            {
                if (slot.bSpawnSent)
                {
                    Packets::Train::TrainEvent removeEvent{};
                    removeEvent.kind = Packets::Train::eTrainEventKind::REMOVE;
                    removeEvent.trainId = slot.trainId;
                    GetPacketFactory().Send(removeEvent);
                    LogEvent("host-remove-sent", slot.trainId, "reason=destructor carriageIdx=%d", i);
                }
                slot = SHostTrain{};
                break;
            }
        }
    }

    // Check follower copies (mark lost without deleting sibling pool entries inside ~CVehicle)
    for (SCopyTrain& copy : ms_aCopies)
    {
        if (!copy.bHasDescriptor)
        {
            continue;
        }
        for (int i = 0; i < copy.desc.carriageCount; i++)
        {
            if (copy.pCarriages[i] == pVehicle && copy.carriageRefs[i].Matches(CPools::ms_pVehiclePool, pVehicle))
            {
                copy.pCarriages[i] = nullptr;
                copy.carriageRefs[i].Clear();
                copy.bBound[i] = false;
                copy.bCopyLost = true;
                LogEvent("copy-carriage-destroyed", copy.desc.trainId, "idx=%d vehicle=%d", i,
                    copy.desc.carriages[i].vehicleid);
            }
        }
    }
}

void CTrainSync::OnCarriageRemovePacket(int vehicleid, CVehicle* pVehicle)
{
    for (int t = 0; t < Packets::Train::MAX_TRAINS; t++)
    {
        SCopyTrain& copy = ms_aCopies[t];
        if (!copy.bHasDescriptor)
        {
            continue;
        }
        for (int i = 0; i < copy.desc.carriageCount; i++)
        {
            if (copy.desc.carriages[i].vehicleid == vehicleid ||
                (pVehicle != nullptr && copy.pCarriages[i] == pVehicle))
            {
                RemoveCopySlot(t, "carriage-remove-packet");
                return;
            }
        }
    }
}

bool CTrainSync::ProcessControlCopy(CTrain* pTrain)
{
    if (pTrain == nullptr || CLocalPlayer::m_bIsHost)
    {
        return false;
    }

    int trainId = -1;
    int carriageIdx = -1;
    if (!IsCarriageInAnyCopy(pTrain, &trainId, &carriageIdx))
    {
        return false;
    }

    SCopyTrain& copy = ms_aCopies[trainId];
    if (copy.bDerailed || pTrain->m_nTrainFlags.bNotOnARailRoad)
    {
        return false;
    }

    StepCopyTrainForFrame(copy);

    const unsigned char savedStatus = pTrain->m_nStatus;
    const uint8_t savedCruiseSpeed = pTrain->m_autoPilot.m_nCruiseSpeed;

    pTrain->m_nTrainFlags.bMissionTrain = 1;
    pTrain->m_nTrainFlags.bStopsAtStations = 0;
    pTrain->m_nTrainFlags.bForceSlowDown = 0;

    if (carriageIdx == 0 || pTrain->m_pPrevCarriage == nullptr)
    {
        pTrain->m_fCurrentRailDistance = copy.displayDistance;
        pTrain->m_fTrainSpeed = 0.0f;
        pTrain->m_autoPilot.m_nCruiseSpeed = 0;
        pTrain->m_fTrainGas = 0.0f;
        pTrain->m_fTrainBrake = 0.0f;
        if (pTrain->m_nStatus == STATUS_PLAYER)
        {
            pTrain->m_nStatus = static_cast<unsigned char>(STATUS_ABANDONED);
        }
    }

    const int savedFocus = CWorld::PlayerInFocus;
    CWorld::PlayerInFocus = 0;
    plugin::CallMethod<0x502280, CAEVehicleAudioEntity*>(&pTrain->m_vehicleAudio);
    plugin::CallMethodDyn<CVehicle*>(0x6F86A0, pTrain);
    CWorld::PlayerInFocus = savedFocus;

    pTrain->m_nStatus = savedStatus;
    pTrain->m_autoPilot.m_nCruiseSpeed = savedCruiseSpeed;
    pTrain->m_fTrainSpeed = copy.hostSpeed;
    pTrain->m_fTrainGas = copy.hostGas;
    pTrain->m_fTrainBrake = copy.hostBrake;
    pTrain->m_nTrainFlags.bStoppedAtStation = copy.bHostStoppedAtStation ? 1 : 0;
    pTrain->m_nTrainFlags.bPassengersCanEnterAndLeave = copy.bHostStoppedAtStation ? 1 : 0;

    if (copy.bSnappedThisFrame && pTrain->m_matrix != nullptr)
    {
        const float signedSpeed = pTrain->m_nTrainFlags.bClockwiseDirection ? copy.hostSpeed : -copy.hostSpeed;
        pTrain->m_vecMoveSpeed = pTrain->m_matrix->up * signedSpeed;
        pTrain->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    }
    return true;
}

void CTrainSync::OnDoTrainGenerationAndRemoval()
{
    // Followers never run random train generation or distance removal while a host is present
    if (CSessionLifecycle::HasHost() && !CSessionLifecycle::IsHost())
    {
        return;
    }

    // On the host, protect random trains within 220 m of any connected network player during the call (T-D3)
    CTrain* protectedCarriages[64]{};
    int protectedCount = 0;

    if (CLocalPlayer::m_bIsHost && CPools::ms_pVehiclePool && !CNetworkPlayerManager::m_pPlayers.empty())
    {
        for (CVehicle* pVehicle : CPools::ms_pVehiclePool)
        {
            if (!IsTrainEntity(pVehicle))
            {
                continue;
            }
            auto* pHead = reinterpret_cast<CTrain*>(pVehicle);
            if (pHead->m_pPrevCarriage != nullptr || pHead->m_nTrainFlags.bMissionTrain)
            {
                continue;
            }

            bool bNearNetworkPlayer = false;
            for (CTrain* pCar = pHead; pCar != nullptr && !bNearNetworkPlayer; pCar = pCar->m_pNextCarriage)
            {
                const CVector carPos = pCar->GetPosition();
                for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
                {
                    if (pPlayer == nullptr)
                    {
                        continue;
                    }
                    CPlayerPed* pPed = pPlayer->GetLivePed();
                    if (pPed != nullptr && pPed->m_pVehicle == pCar)
                    {
                        bNearNetworkPlayer = true;
                        break;
                    }
                    const CVector playerPos =
                        pPed != nullptr ? pPed->GetPosition() : CVector(pPlayer->m_onFootSnapshotInterpolated.vecPos);
                    const float dx = carPos.x - playerPos.x;
                    const float dy = carPos.y - playerPos.y;
                    if (dx * dx + dy * dy <= FRIEND_KEEP_TRAIN_DISTANCE_SQ)
                    {
                        bNearNetworkPlayer = true;
                        break;
                    }
                }
            }

            if (bNearNetworkPlayer)
            {
                for (CTrain* pCar = pHead; pCar != nullptr && protectedCount < 64; pCar = pCar->m_pNextCarriage)
                {
                    if (!pCar->m_nTrainFlags.bMissionTrain)
                    {
                        pCar->m_nTrainFlags.bMissionTrain = 1;
                        protectedCarriages[protectedCount++] = pCar;
                    }
                }
            }
        }
    }

    plugin::Call<0x6F7900>();

    for (int i = 0; i < protectedCount; i++)
    {
        CTrain* pCar = protectedCarriages[i];
        if (CPools::ms_pVehiclePool && CPools::ms_pVehiclePool->IsObjectValid(pCar) && pCar->IsVTableValid())
        {
            pCar->m_nTrainFlags.bMissionTrain = 0;
        }
    }
}

bool CTrainSync::OnSkipToNextAllowedStation(CTrain* pTrain)
{
    if (pTrain == nullptr || !CSessionLifecycle::HasHost() || CSessionLifecycle::IsHost())
    {
        return false;
    }

    int trainId = -1;
    if (!IsCarriageInAnyCopy(pTrain, &trainId, nullptr))
    {
        CTrain* pEngine = CTrain::FindEngine(pTrain);
        if (!IsCarriageInAnyCopy(pEngine, &trainId, nullptr))
        {
            return false;
        }
    }

    Packets::Train::TrainEvent req{};
    req.kind = Packets::Train::eTrainEventKind::REQUEST;
    req.trainId = static_cast<uint8_t>(trainId);
    req.requestKind = Packets::Train::eTrainRequestKind::SKIP_STATION;
    GetPacketFactory().Send(req);
    ms_diag.skipRequests++;
    LogEvent("skip-requested", trainId, "carriage=%p", pTrain);
    return true;
}

void CTrainSync::OnHooksInstalled(bool bGenHooked, bool bSkipHooked)
{
    ms_bGenHooked = bGenHooked;
    ms_bSkipHooked = bSkipHooked;
}

bool CTrainSync::IsEnabled()
{
    return !ms_bLayoutMismatch;
}

void CTrainSync::LogEvent(const char* event, int trainId, const char* format, ...)
{
    static const bool enabled = std::strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 4096)
    {
        return;
    }

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    {
        return;
    }
    char* fileName = std::strrchr(path, '\\');
    if (!fileName)
    {
        return;
    }
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_train_sync.log");
    FILE* file = std::fopen(path, "a+");
    if (!file)
    {
        return;
    }
    std::fseek(file, 0, SEEK_END);
    if (std::ftell(file) < 2 * 1024 * 1024)
    {
        char detail[384] = "";
        if (format != nullptr)
        {
            va_list args;
            va_start(args, format);
            std::vsnprintf(detail, sizeof(detail), format, args);
            va_end(args);
        }
        std::fprintf(file, "t=%u frame=%u event=%s train=%d %s host=%d hostTrains=%u copies=%u bound=%u\n",
            GetTickCount(), CTimer::m_FrameCounter, event, trainId, detail, CLocalPlayer::m_bIsHost ? 1 : 0,
            ms_diag.hostTrains, ms_diag.copyTrains, ms_diag.boundCarriages);
        ++records;
    }
    std::fclose(file);
}

void CTrainSync::WriteDiagSummary(FILE* file)
{
    const SDiagCounters& c = ms_diag;
    std::fprintf(file,
        "  train-sync: enabled=%d hooks=%d/%d hostTrains=%u copies=%u bound=%u spawnsSent=%u spawnsRecv=%u "
        "created=%u recreated=%u snaps=%u derails=%u skips=%u attaches=%u/%u maxCorr=%.2f\n",
        IsEnabled() ? 1 : 0, ms_bGenHooked ? 1 : 0, ms_bSkipHooked ? 1 : 0, c.hostTrains, c.copyTrains,
        c.boundCarriages, c.spawnsSent, c.spawnsReceived, c.copiesCreated, c.copiesRecreated, c.snaps, c.derails,
        c.skipRequests, c.attachesSent, c.attachesApplied, c.maxCorrection);
}
