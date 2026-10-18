#pragma once

#include "CTrainSyncPolicy.h"
#include <cstddef>

// Network identities outlive their local engine copies. Only a definitive network removal
// invalidates a spec; a missing copy (including the restart/resnapshot interval) waits.
namespace EntityAttachment
{
enum class Kind { PLAYER, PED, VEHICLE };

// A repeated spawn can be a rejected-removal resend of the same network identity.
// Authoritative REMOVE already invalidates true same-model ID reuse in EVENT order.
inline bool PreserveVehicleIdentityOnSpawn(int oldModel, int newModel)
{
    return oldModel == newModel;
}

struct OwnedState
{
    bool bHadSent = false;
    TrainSyncPolicy::SAttachmentSpec lastSent{};
    // Join snapshots can expose an attachment before the first owner polling pass.
    bool bMayHaveAttachedPeer = false;
};

struct RemoteState
{
    bool bActive = false;
    TrainSyncPolicy::SAttachmentSpec spec{};
    const void* pLastSubject = nullptr;
    const void* pLastTarget = nullptr;

    void ForgetCopy()
    {
        pLastSubject = nullptr;
        pLastTarget = nullptr;
    }

    void Receive(const TrainSyncPolicy::SAttachmentSpec& incoming)
    {
        *this = {};
        bActive = incoming.bAttached;
        if (bActive) spec = incoming;
    }
};

// A reconnecting player takes the departed one's id (the server hands out the lowest free id). The SYSTEM leave and
// join notifications skip the event buffer, while the departed player's attachments and the server's detach fence
// (sent before PLAYER_DISCONNECTED) wait in it. From the leave until that fence, attachments for the id are the old
// player's: the newest is kept aside and dropped by the fence. The fence can also be handled before the leave (ENet
// doesn't order across channels); then no fence follows, and after FENCE_WAIT_MS the attachment kept aside is the new
// player's and is used (owners send an attachment only when it changes).
constexpr uint32_t FENCE_WAIT_MS = 5000;

struct DepartureFence
{
    bool bWaiting = false;
    uint32_t since = 0;
    bool bHeld = false;
    TrainSyncPolicy::SAttachmentSpec held{};

    void OnPlayerLeft(uint32_t now)
    {
        *this = {};
        bWaiting = true;
        since = now;
    }

    // Whether an ENTITY_ATTACH for this player may be used now; an attach during the wait is kept aside, the first
    // detach is the fence.
    bool Accept(const TrainSyncPolicy::SAttachmentSpec& spec)
    {
        if (!bWaiting) return true;
        if (spec.bAttached)
        {
            bHeld = true;
            held = spec;
            return false;
        }
        *this = {};
        return true;
    }
};

template <size_t Players, size_t Peds, size_t Vehicles>
class State
{
public:
    OwnedState localPlayer{};
    OwnedState ownedPeds[Peds]{};
    OwnedState ownedVehicles[Vehicles]{};
    RemoteState players[Players]{};
    RemoteState peds[Peds]{};
    RemoteState vehicles[Vehicles]{};
    DepartureFence playerFences[Players]{};

    template <typename Detach>
    void OnPlayerLeft(int id, uint32_t now, Detach detach)
    {
        RemoveIdentity(Kind::PLAYER, id, detach);
        if (Valid(id, Players)) playerFences[id].OnPlayerLeft(now);
    }

    bool AcceptPlayerAttach(int id, const TrainSyncPolicy::SAttachmentSpec& spec)
    {
        return !Valid(id, Players) || playerFences[id].Accept(spec);
    }

    // Ends the waits no fence ended in time; the attachment kept aside becomes the player's spec.
    void ExpireFences(uint32_t now)
    {
        for (size_t id = 0; id < Players; ++id)
        {
            DepartureFence& fence = playerFences[id];
            if (!fence.bWaiting || now - fence.since < FENCE_WAIT_MS) continue;
            if (fence.bHeld) players[id].Receive(fence.held);
            fence = {};
        }
    }

    template <typename SendDetach>
    void DetachLocalPlayerForRestart(bool authenticated, SendDetach sendDetach)
    {
        if (authenticated && (localPlayer.bMayHaveAttachedPeer ||
            (localPlayer.bHadSent && localPlayer.lastSent.bAttached)))
        {
            sendDetach();
        }
        localPlayer = {};
    }

    template <typename SendDetach>
    void Reset(bool keepRemoteSpecs, bool authenticated, SendDetach sendDetach)
    {
        DetachLocalPlayerForRestart(authenticated, sendDetach);
        for (auto& state : ownedPeds) state = {};
        for (auto& state : ownedVehicles) state = {};
        ResetRemote(players, keepRemoteSpecs);
        ResetRemote(peds, keepRemoteSpecs);
        ResetRemote(vehicles, keepRemoteSpecs);
        // Fences belong to the remote specs: kept with them (a follower's restart), dropped with them (disconnect).
        if (!keepRemoteSpecs)
            for (auto& fence : playerFences) fence = {};
    }

    // detach receives a current network identity, never a cached engine pointer.
    template <typename Detach>
    void RemoveIdentity(Kind kind, int id, Detach detach)
    {
        if (kind == Kind::PLAYER && Valid(id, Players))
        {
            Clear(players[id], kind, id, detach);
        }
        else if (kind == Kind::PED && Valid(id, Peds))
        {
            Clear(peds[id], kind, id, detach);
            ownedPeds[id] = {};
        }
        else if (kind == Kind::VEHICLE && Valid(id, Vehicles))
        {
            Clear(vehicles[id], kind, id, detach);
            ownedVehicles[id] = {};
            RemoveTarget(players, Kind::PLAYER, id, detach);
            RemoveTarget(peds, Kind::PED, id, detach);
            RemoveTarget(vehicles, Kind::VEHICLE, id, detach);
            // Owners still owe detach updates after the engine loses its target.
            // Retain lastSent until polling observes and publishes that transition.
        }
    }

private:
    static bool Valid(int id, size_t count) { return id >= 0 && static_cast<size_t>(id) < count; }

    template <size_t Count>
    static void ResetRemote(RemoteState (&states)[Count], bool keep)
    {
        for (auto& state : states)
        {
            if (keep) state.ForgetCopy();
            else state = {};
        }
    }

    template <typename Detach>
    static void Clear(RemoteState& state, Kind kind, int id, Detach detach)
    {
        if (state.bActive) detach(kind, id);
        state = {};
    }

    template <size_t Count, typename Detach>
    static void RemoveTarget(RemoteState (&states)[Count], Kind kind, int vehicleId, Detach detach)
    {
        for (size_t i = 0; i < Count; ++i)
        {
            auto& state = states[i];
            if (state.bActive && state.spec.targetVehicleId == vehicleId)
                Clear(state, kind, static_cast<int>(i), detach);
        }
    }

};
} // namespace EntityAttachment
