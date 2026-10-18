#include "../client/src/CEntityAttachmentState.h"
#include "../client/src/CPacketOrderingPolicy.h"
#include "../shared/network/entity_lifecycle.h"
#include "../server/src/CVehicleSeatPolicy.h"
#include "../server/src/CPedHandoverPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

namespace
{
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (false)
using State = EntityAttachment::State<4, 8, 16>;
using Kind = EntityAttachment::Kind;

TrainSyncPolicy::SAttachmentSpec Attached(int vehicleId)
{
    TrainSyncPolicy::SAttachmentSpec spec{};
    spec.bAttached = true;
    spec.targetVehicleId = vehicleId;
    spec.offsetZ = 2.0f;
    return spec;
}

void RestartDetachesOnce()
{
    State state;
    EntityAttachment::RemoteState peer;
    peer.Receive(Attached(5));
    state.localPlayer.bHadSent = true;
    state.localPlayer.lastSent = Attached(5);
    unsigned sent = 0;
    auto detach = [&] { ++sent; peer.Receive({}); };
    // The pre-clear callback sends while the old network identity is still valid.
    state.DetachLocalPlayerForRestart(true, detach);
    CHECK(sent == 1);
    CHECK(!peer.bActive);
    CHECK(TrainSyncPolicy::ShouldApplyOnFootPosition(peer.bActive));
    // Post-clear and New Game / save-loaded notifications repeat the reset safely.
    state.Reset(true, true, detach);
    state.Reset(true, true, detach);
    CHECK(sent == 1);
    state.localPlayer.bHadSent = true;
    state.localPlayer.lastSent = Attached(5);
    state.Reset(true, false, detach);
    CHECK(sent == 1); // a disconnected session has nobody to notify
    CHECK(!state.localPlayer.bHadSent);
    // A join snapshot may have attached a peer before the first owner poll. It
    // must not suppress the initial broadcast, but still requires restart detach.
    state.localPlayer.bMayHaveAttachedPeer = true;
    CHECK(!state.localPlayer.bHadSent);
    state.DetachLocalPlayerForRestart(true, detach);
    CHECK(sent == 2);
    CHECK(!state.localPlayer.bMayHaveAttachedPeer);
}

void MissingCopiesWaitForSnapshot()
{
    State state;
    int oldSubject = 0, oldTarget = 0, newSubject = 0, newTarget = 0;
    state.peds[3].Receive(Attached(5));
    state.peds[3].pLastSubject = &oldSubject;
    state.peds[3].pLastTarget = &oldTarget;
    state.Reset(true, true, [] {});
    CHECK(state.peds[3].bActive);
    CHECK(state.peds[3].pLastSubject == nullptr);
    // Several ready frames can run before PED_SPAWN arrives. They only forget
    // copy pointers, as TryApplyRemotePedAttachment does for a missing record.
    for (int frame = 0; frame < 5; ++frame) state.peds[3].ForgetCopy();
    CHECK(state.peds[3].bActive);
    CHECK(state.peds[3].spec.targetVehicleId == 5);
    state.peds[3].pLastSubject = &newSubject;
    state.peds[3].pLastTarget = &newTarget;
    CHECK(state.peds[3].bActive); // same network identities, recreated engine copies

    // An attachment may also arrive before its initial subject/target SPAWN.
    state.vehicles[7].Receive(Attached(6));
    state.vehicles[7].ForgetCopy();
    CHECK(state.vehicles[7].bActive);
    CHECK(state.vehicles[7].spec.targetVehicleId == 6);
    state.Reset(false, false, [] {});
    CHECK(!state.peds[3].bActive && !state.vehicles[7].bActive);
}

void DefinitiveRemovalsInvalidateBothEnds()
{
    State state;
    std::vector<Kind> detached;
    auto detach = [&](Kind kind, int) { detached.push_back(kind); };
    state.players[1].Receive(Attached(5));
    state.peds[3].Receive(Attached(5));
    state.vehicles[7].Receive(Attached(5));
    state.peds[4].Receive(Attached(6));
    state.ownedPeds[2] = {true, Attached(5)};
    state.ownedVehicles[8] = {true, Attached(5)};
    state.localPlayer = {true, Attached(5)};
    state.RemoveIdentity(Kind::VEHICLE, 5, detach);
    CHECK(detached.size() == 3);
    CHECK(!state.players[1].bActive && !state.peds[3].bActive && !state.vehicles[7].bActive);
    CHECK(state.ownedPeds[2].bHadSent && state.ownedVehicles[8].bHadSent);
    CHECK(state.localPlayer.bHadSent);
    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(state.localPlayer.bHadSent,
        state.localPlayer.lastSent, {}, true)); // target removal must not erase detach obligation
    CHECK(state.peds[4].bActive); // unrelated pending attachment is preserved
    // Reusing target ID 5 never resurrects any old attachment.
    state.players[1].ForgetCopy();
    CHECK(!state.players[1].bActive);

    for (Kind kind : {Kind::PLAYER, Kind::PED, Kind::VEHICLE})
    {
        auto& subject = kind == Kind::PLAYER ? state.players[2] :
            kind == Kind::PED ? state.peds[2] : state.vehicles[2];
        subject.Receive(Attached(6));
        state.RemoveIdentity(kind, 2, detach);
        CHECK(!subject.bActive);
        // A new record with the same subject ID starts without the old spec.
        subject.ForgetCopy();
        CHECK(!subject.bActive);
        subject.Receive(Attached(7)); // its new owner's explicit state can attach
        CHECK(subject.bActive && subject.spec.targetVehicleId == 7);
    }
    // Removal before a delayed SPAWN must invalidate even an absent subject.
    state.peds[6].Receive(Attached(9));
    state.RemoveIdentity(Kind::PED, 6, detach);
    CHECK(!state.peds[6].bActive);
    state.RemoveIdentity(Kind::VEHICLE, -1, detach);
    state.RemoveIdentity(Kind::PLAYER, 100, detach);

    CHECK(EntityAttachment::PreserveVehicleIdentityOnSpawn(537, 537));
    CHECK(!EntityAttachment::PreserveVehicleIdentityOnSpawn(537, 538));
    // Same-model resend keeps specs while replacing local copies; a changed model
    // is a known identity replacement even if the old removal was not observed.
    state.peds[6].Receive(Attached(9));
    for (int model : {537, 538})
    {
        if (!EntityAttachment::PreserveVehicleIdentityOnSpawn(537, model))
            state.RemoveIdentity(Kind::VEHICLE, 9, detach);
        else
            state.peds[6].ForgetCopy();
        CHECK(state.peds[6].bActive == (model == 537));
    }
}

void OrderedDisconnectFence()
{
    CHECK(EntityLifecycle::UsesServerTime(ePacketType::ENTITY_ATTACH));
    CHECK(EntityLifecycle::UsesServerTime(ePacketType::PED_REMOVE));
    CHECK(EntityLifecycle::UsesServerTime(ePacketType::VEHICLE_REMOVE));
    struct Event { uint32_t serverTime; TrainSyncPolicy::SAttachmentSpec spec; };
    for (bool equalStamps : {false, true})
    {
        State state;
        std::deque<Event*> buffer;
        Event laterUnrelated{500, {}};
        Event oldAttach{100, Attached(5)};
        Event disconnectDetach{equalStamps ? 100u : 101u, {}};
        Event newAttach{equalStamps ? 100u : 102u, Attached(6)};
        // Exercise the production buffer insertion, including its upper_bound
        // path and equal-stamp order, rather than copying sorting rules here.
        PacketOrderingPolicy::InsertByServerTime(buffer, &laterUnrelated);
        PacketOrderingPolicy::InsertByServerTime(buffer, &oldAttach);
        // SYSTEM leave and rejoin drain before delayed reliable EVENT delivery.
        state.players[1].Receive(oldAttach.spec);
        state.RemoveIdentity(Kind::PLAYER, 1, [](Kind, int) {});
        CHECK(!state.players[1].bActive);
        // The new identity now exists. The server sent this fence before recycling
        // the ID, so even the old attach that arrives after rejoin is cleared.
        PacketOrderingPolicy::InsertByServerTime(buffer, &disconnectDetach);
        state.players[1].Receive(buffer.front()->spec);
        buffer.pop_front();
        CHECK(state.players[1].bActive);
        CHECK(buffer.front() == &disconnectDetach);
        state.players[1].Receive(buffer.front()->spec);
        buffer.pop_front();
        CHECK(!state.players[1].bActive);
        CHECK(TrainSyncPolicy::ShouldApplyOnFootPosition(state.players[1].bActive));
        PacketOrderingPolicy::InsertByServerTime(buffer, &newAttach);
        CHECK(buffer.front() == &newAttach);
        state.players[1].Receive(buffer.front()->spec);
        CHECK(state.players[1].bActive && state.players[1].spec.targetVehicleId == 6);
    }
}

void DepartedPlayerWaitsForFence()
{
    // An observer back from the background: SYSTEM leave/rejoin are handled at once, the buffered EVENT stream
    // (the departed player's attach, the server's detach fence, the new player's attach) only afterwards.
    State state;
    unsigned detached = 0;
    auto detach = [&](Kind, int) { ++detached; };
    state.players[1].Receive(Attached(5));
    state.OnPlayerLeft(1, 1000, detach);
    CHECK(detached == 1 && !state.players[1].bActive);
    // The buffered attach of the departed player never reaches the new ped with the same id
    CHECK(!state.AcceptPlayerAttach(1, Attached(5)));
    CHECK(!state.players[1].bActive);
    CHECK(state.AcceptPlayerAttach(2, Attached(5))); // other players are not fenced
    CHECK(state.AcceptPlayerAttach(1, {})); // the fence passes, ends the wait and drops the attach kept aside
    state.ExpireFences(1000 + EntityAttachment::FENCE_WAIT_MS);
    CHECK(!state.players[1].bActive);
    CHECK(state.AcceptPlayerAttach(1, Attached(6))); // the new player's own attach
    CHECK(state.AcceptPlayerAttach(1, {}));

    // A fence handled before the leave (reordered across channels): no fence follows. The new player's attach is
    // kept aside (the newest wins) and used when the wait runs out, since its owner sends it only once.
    state.OnPlayerLeft(1, 2000, detach);
    CHECK(!state.AcceptPlayerAttach(1, Attached(6)));
    CHECK(!state.AcceptPlayerAttach(1, Attached(7)));
    state.ExpireFences(2000 + EntityAttachment::FENCE_WAIT_MS - 1);
    CHECK(!state.players[1].bActive);
    state.ExpireFences(2000 + EntityAttachment::FENCE_WAIT_MS);
    CHECK(state.players[1].bActive && state.players[1].spec.targetVehicleId == 7);
    CHECK(state.AcceptPlayerAttach(1, Attached(8))); // the wait is over
    // A wait that held nothing ends without touching the spec
    state.OnPlayerLeft(2, 2500, detach);
    state.ExpireFences(2500 + EntityAttachment::FENCE_WAIT_MS);
    CHECK(!state.players[2].bActive && state.AcceptPlayerAttach(2, Attached(9)));

    // A follower restart keeps the wait; a disconnect drops it
    state.OnPlayerLeft(3, 3000, detach);
    state.Reset(true, true, [] {});
    CHECK(!state.AcceptPlayerAttach(3, Attached(5)));
    state.Reset(false, false, [] {});
    CHECK(state.AcceptPlayerAttach(3, Attached(5)));
    state.ExpireFences(3000 + EntityAttachment::FENCE_WAIT_MS);
    CHECK(!state.players[3].bActive);
    CHECK(state.AcceptPlayerAttach(-1, Attached(5)) && state.AcceptPlayerAttach(100, Attached(5)));
    state.OnPlayerLeft(100, 3003, detach);
}

void AuthoritativeRemovalAcknowledgements()
{
    using VehicleSeatPolicy::eRemove;
    using VehicleSeatPolicy::DecideRemove;
    using VehicleSeatPolicy::ShouldAcknowledgeRemove;
    CHECK(!ShouldAcknowledgeRemove(eRemove::DROP_STALE));
    CHECK(!ShouldAcknowledgeRemove(eRemove::REJECT_AND_RESEND));
    CHECK(!ShouldAcknowledgeRemove(eRemove::GIVE_TO_HOST));
    CHECK(ShouldAcknowledgeRemove(eRemove::REMOVE));
    State state;
    state.peds[2].Receive(Attached(5));
    // A restarting syncer's host-owned mission vehicle survives by handback.
    auto decision = DecideRemove(VehicleSync::REMOVE_SYNCER_RESTARTED,
        false, true, false, false, true, true);
    CHECK(decision == eRemove::GIVE_TO_HOST);
    CHECK(!ShouldAcknowledgeRemove(decision));
    state.peds[2].ForgetCopy();
    CHECK(state.peds[2].bActive);
    // A normal vehicle is truly deleted. The requester has already dropped its
    // vehicle record, but the authoritative ack still clears target references.
    decision = DecideRemove(VehicleSync::REMOVE_SYNCER_RESTARTED,
        false, true, false, false, false, true);
    CHECK(decision == eRemove::REMOVE);
    if (ShouldAcknowledgeRemove(decision))
        state.RemoveIdentity(Kind::VEHICLE, 5, [](Kind, int) {});
    CHECK(!state.peds[2].bActive);

    // PED_REMOVE uses the same distinction: FindClaimer preserves identity;
    // only the no-claimer path relays definitive removal back to its requester.
    struct Ped {} ped;
    struct Player { std::vector<Ped*> m_vPedClaims; } owner, claimer;
    const std::vector<Player*> players{&owner, &claimer};
    state.peds[2].Receive(Attached(5));
    claimer.m_vPedClaims.push_back(&ped);
    CHECK(PedHandoverPolicy::FindClaimer(players, &owner, &ped) == &claimer);
    state.peds[2].ForgetCopy();
    CHECK(state.peds[2].bActive);
    claimer.m_vPedClaims.clear();
    CHECK(PedHandoverPolicy::FindClaimer(players, &owner, &ped) == nullptr);
    state.RemoveIdentity(Kind::PED, 2, [](Kind, int) {});
    CHECK(!state.peds[2].bActive);
}
} // namespace

int main()
{
    RestartDetachesOnce();
    MissingCopiesWaitForSnapshot();
    DefinitiveRemovalsInvalidateBothEnds();
    OrderedDisconnectFence();
    DepartedPlayerWaitsForFence();
    AuthoritativeRemovalAcknowledgements();
    if (failures) return EXIT_FAILURE;
    std::puts("entity_attachment_test: ok");
    return EXIT_SUCCESS;
}
