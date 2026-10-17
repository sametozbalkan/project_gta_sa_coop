// Private build: policy test for the vehicle sync protocol (0.1.1) without GTA or ENet: the client's records
// (client/src/CVehicleSyncPolicy.h: stated assignments and the ones that wait for their vehicle, removal before
// VEHICLE_CONFIRM, what a destroyed vehicle does to its record, copies and their createdBy, takeovers, recreation, temp
// ids and their expiry, the local player's updates while getting out and the exits the others hear of, the pool slot
// generation, a createdBy change before VEHICLE_CONFIRM, NPC drivers' car copies), the server's seats, syncers and
// removals (server/src/CVehicleSeatPolicy.h), the order of the lifecycle packets and the ids
// (shared/network/entity_lifecycle.h) and the heading of resent vehicles (shared/network/vehicle_sync.h). Build and run:
// see CLAUDE.md (Fork changes, vehicle sync protocol).
#include "../client/src/CVehicleSyncPolicy.h"
#include "../server/src/CVehicleSeatPolicy.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using VehicleSyncPolicy::eAssignment;
using VehicleSyncPolicy::eConfirm;
using VehicleSyncPolicy::eTakeover;
using VehicleSyncPolicy::eVehicleGone;
using VehicleSeatPolicy::eSyncerLeft;

// --- client side -----------------------------------------------------------------------------------------------------

// A client's record as CNetworkVehicleManager keeps it, driven by the same rules (ASSIGN_VEHICLE, VEHICLE_SPAWN /
// VEHICLE_CONFIRM, VEHICLE_REMOVE)
struct FakeClient
{
    struct Record
    {
        uint16_t model = 0;
        bool bSyncing = false;
    };
    std::map<int, Record> records;
    CPendingVehicleAssignments pending;

    void Assign(int id, uint16_t model, bool bSyncing, uint32_t now)
    {
        auto it = records.find(id);
        const bool bKnown = it != records.end();
        switch (VehicleSyncPolicy::DecideAssignment(bKnown, bKnown && it->second.model == model,
            bKnown && it->second.bSyncing, bSyncing))
        {
            case eAssignment::DEFER: pending.Store(id, model, bSyncing, now); break;
            case eAssignment::UNCHANGED: break;
            case eAssignment::APPLY: it->second.bSyncing = bSyncing; break;
        }
    }
    void Spawn(int id, uint16_t model, uint32_t now)  // VEHICLE_SPAWN: a copy, which a waiting assignment may make ours
    {
        records[id] = {model, false};
        if (const auto state = pending.Take(id, model, now))
            records[id].bSyncing = *state;
    }
    void Confirm(int id, uint16_t model)  // VEHICLE_CONFIRM: ours; a waiting assignment is about an older vehicle
    {
        pending.Drop(id);
        records[id] = {model, true};
    }
    void Remove(int id)
    {
        pending.Drop(id);
        records.erase(id);
    }
};

void AssignmentsAreStated()
{
    FakeClient client;
    client.Spawn(5, 400, 0);
    client.Assign(5, 400, true, 10);
    CHECK(client.records[5].bSyncing);
    client.Assign(5, 400, true, 20);  // a doubled assignment: upstream flipped it back to "not syncing"
    CHECK(client.records[5].bSyncing);
    client.Assign(5, 400, false, 30);
    CHECK(!client.records[5].bSyncing);
    client.Assign(5, 400, false, 40);
    CHECK(!client.records[5].bSyncing);

    CHECK(VehicleSyncPolicy::DecideAssignment(true, true, false, true) == eAssignment::APPLY);
    CHECK(VehicleSyncPolicy::DecideAssignment(true, true, true, true) == eAssignment::UNCHANGED);
    CHECK(VehicleSyncPolicy::DecideAssignment(false, false, false, true) == eAssignment::DEFER);
    CHECK(VehicleSyncPolicy::DecideAssignment(true, false, true, false) == eAssignment::DEFER);  // another vehicle, same id

    // the createdBy the vehicle of a record gets: its own for the syncer, MISSION_VEHICLE for a copy
    CHECK(VehicleSyncPolicy::LocalCreatedBy(true, VehicleSync::CREATED_BY_RANDOM) == VehicleSync::CREATED_BY_RANDOM);
    CHECK(VehicleSyncPolicy::LocalCreatedBy(false, VehicleSync::CREATED_BY_RANDOM) == VehicleSync::CREATED_BY_MISSION);
}

void AssignmentBeforeTheVehicle()
{
    // the spawn still waits in the packet buffer: the assignment waits for it
    FakeClient client;
    client.Assign(7, 411, true, 100);
    CHECK(client.records.count(7) == 0 && client.pending.Size() == 1);
    client.Spawn(7, 411, 200);
    CHECK(client.records[7].bSyncing && client.pending.Size() == 0);

    // our own vehicle: the server sends VEHICLE_CONFIRM before any assignment of it (both on its clock), so one that
    // waits by then is about an older vehicle with the id and is dropped (upstream's record took it)
    FakeClient creator;
    creator.Assign(9, 560, false, 100);
    creator.Confirm(9, 560);
    CHECK(creator.records[9].bSyncing && creator.pending.Size() == 0);

    // the newest wins
    FakeClient twice;
    twice.Assign(3, 400, false, 0);
    twice.Assign(3, 400, true, 10);
    twice.Spawn(3, 400, 20);
    CHECK(twice.records[3].bSyncing);

    // an assignment of an older vehicle with the same id (another model) isn't applied to the new one
    FakeClient older;
    older.Assign(4, 400, true, 0);
    older.Spawn(4, 402, 10);
    CHECK(!older.records[4].bSyncing && older.pending.Size() == 0);

    // VEHICLE_REMOVE of the id drops it
    FakeClient removed;
    removed.Assign(6, 400, true, 0);
    removed.Remove(6);
    removed.Spawn(6, 400, 10);
    CHECK(!removed.records[6].bSyncing);

    // it expires
    CPendingVehicleAssignments pending;
    pending.Store(1, 400, true, 1000);
    pending.Store(2, 401, false, 5000);
    CHECK(pending.Expire(1000 + CPendingVehicleAssignments::MaxAgeMs - 1).empty());
    const auto expired = pending.Expire(1000 + CPendingVehicleAssignments::MaxAgeMs);
    CHECK(expired.size() == 1 && expired[0].vehicleId == 1 && expired[0].bSyncing);
    CHECK(pending.Size() == 1);
    CHECK(!pending.Take(2, 401, 5000 + CPendingVehicleAssignments::MaxAgeMs).has_value());  // too old when it appears
    CHECK(pending.Size() == 0);

    // across the tick wrap
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 100;
    pending.Store(8, 400, true, beforeWrap);
    CHECK(pending.Take(8, 400, 500).value_or(false));  // age 601
}

void RemovalBeforeConfirmation()
{
    // the game destroys our vehicle before VEHICLE_CONFIRM: the record keeps its temp slot, the confirmation removes it
    CHECK(VehicleSyncPolicy::DecideVehicleGone(false, true, false, false, false) == eVehicleGone::REMOVE_AT_CONFIRM);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(false, true, false, false, true) == eVehicleGone::REMOVE_AT_CONFIRM);
    CHECK(VehicleSyncPolicy::DecideConfirm(true, false) == eConfirm::REMOVE_UNCONFIRMED);
    CHECK(VehicleSyncPolicy::DecideConfirm(true, true) == eConfirm::ATTACH);
    CHECK(VehicleSyncPolicy::DecideConfirm(false, false) == eConfirm::UNKNOWN_TEMP);
    // the server takes that removal from the creator only, even when someone else syncs the vehicle by then
    CHECK(VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_UNCONFIRMED, false, true, false));
    CHECK(!VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_UNCONFIRMED, true, false, true));

    // temp ids go round, so a confirmation still on its way never meets a newer vehicle in its slot
    bool taken[5] = {true, false, false, false, false};
    auto isFree = [&](int slot) { return !taken[slot]; };
    int last = -1;
    const int first = VehicleSyncPolicy::NextTempId(last, 5, isFree);
    CHECK(first == 1);
    taken[first] = true;  // waits for its confirmation (also after its vehicle went)
    last = first;
    const int second = VehicleSyncPolicy::NextTempId(last, 5, isFree);
    CHECK(second == 2);
    taken[1] = false;  // confirmed
    taken[second] = true;
    last = second;
    CHECK(VehicleSyncPolicy::NextTempId(last, 5, isFree) == 3);  // not slot 1 again
    taken[3] = taken[4] = true;
    CHECK(VehicleSyncPolicy::NextTempId(4, 5, isFree) == 1);  // wraps
    taken[1] = true;
    CHECK(VehicleSyncPolicy::NextTempId(1, 5, isFree) == -1);  // all taken
}

void DestroyedVehicles()
{
    // ours: removed on every game
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, false, false, false) == eVehicleGone::SEND_REMOVE);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, false, true, false) == eVehicleGone::SEND_REMOVE);
    // ours while this game starts over (CWorld::ClearForRestart): the server decides about the host's cars
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, false, false, true) == eVehicleGone::SEND_REMOVE_RESTARTED);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, false, true, true) == eVehicleGone::SEND_REMOVE_RESTARTED);
    // the host's mission deleted it: removed on every game, through the host's request, whoever syncs it
    CHECK(VehicleSyncPolicy::IsHostMissionDelete(true, true));
    CHECK(!VehicleSyncPolicy::IsHostMissionDelete(false, true));  // the friend's own scripts
    CHECK(!VehicleSyncPolicy::IsHostMissionDelete(true, false));  // the host's population or another script
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, true, false, false) == eVehicleGone::HOST_MISSION_REMOVE);
    // the review's case: the friend got into the mission's car, the server made it the syncer and sent the host
    // ASSIGN_VEHICLE(false), and the host's DELETE_CAR runs before that is handled: its record still says "syncing".
    // A REMOVE_BY_SYNCER was rejected and the server sent the car back, so the friend kept a car the mission had deleted.
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, true, false, false) == eVehicleGone::HOST_MISSION_REMOVE);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, true, true, false) == eVehicleGone::HOST_MISSION_REMOVE);
    using VehicleSeatPolicy::eRemove;
    CHECK(VehicleSeatPolicy::DecideRemove(VehicleSync::REMOVE_BY_HOST_MISSION, false, false, false, true, true, false) ==
          eRemove::REMOVE);  // the host isn't the syncer any more: still accepted
    CHECK(VehicleSeatPolicy::DecideRemove(VehicleSync::REMOVE_BY_SYNCER, false, false, false, true, true, false) ==
          eRemove::REJECT_AND_RESEND);  // what the host sent before
    // not yet confirmed: the confirmation removes it everywhere anyway
    CHECK(VehicleSyncPolicy::DecideVehicleGone(false, true, true, false, false) == eVehicleGone::REMOVE_AT_CONFIRM);
    CHECK(VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_BY_HOST_MISSION, false, false, true));
    CHECK(!VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_BY_HOST_MISSION, true, true, false));
    // a copy this game lost to its own engine: recreated from the syncer's next update, a wreck isn't; a restart
    // destroys the copies as well, and their syncers bring them back the same way
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, false, false, false) == eVehicleGone::KEEP_FOR_RECREATE);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, false, true, false) == eVehicleGone::KEEP_WRECK);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, false, false, true) == eVehicleGone::KEEP_FOR_RECREATE);
    // upstream's rule stays: the syncer removes its vehicle
    CHECK(VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_BY_SYNCER, true, false, false));
    CHECK(!VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_BY_SYNCER, false, true, true));
    CHECK(!VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_REASON_MAX + 1, true, true, true));
}

void CopiesAndCreatedBy()
{
    // MARK_CAR_AS_NO_LONGER_NEEDED on a copy the friend's game doesn't sync: it stays MISSION_VEHICLE there, the record
    // keeps the host's value only
    auto change = VehicleSyncPolicy::DecideCreatedByChange(false, false, VehicleSync::CREATED_BY_RANDOM);
    CHECK(!change.bRecordTakesIt && change.bRestoreMission);
    // the host's script releases a car another game syncs: the record takes it (SET_VEHICLE_CREATED_BY goes out), the
    // host's copy stays MISSION_VEHICLE
    change = VehicleSyncPolicy::DecideCreatedByChange(false, true, VehicleSync::CREATED_BY_RANDOM);
    CHECK(change.bRecordTakesIt && change.bRestoreMission);
    // the syncer's own decision
    change = VehicleSyncPolicy::DecideCreatedByChange(true, false, VehicleSync::CREATED_BY_RANDOM);
    CHECK(change.bRecordTakesIt && !change.bRestoreMission);
    change = VehicleSyncPolicy::DecideCreatedByChange(false, false, VehicleSync::CREATED_BY_MISSION);
    CHECK(!change.bRestoreMission);
}

void Takeover()
{
    CHECK(VehicleSyncPolicy::DecideTakeover(true, false, false) == eTakeover::KEEP);
    CHECK(VehicleSyncPolicy::DecideTakeover(false, false, true) == eTakeover::RECREATE);
    CHECK(VehicleSyncPolicy::DecideTakeover(false, true, true) == eTakeover::REMOVE);   // a wreck
    CHECK(VehicleSyncPolicy::DecideTakeover(false, false, false) == eTakeover::REMOVE);  // nothing to recreate from
}

void RecreateThrottle()
{
    CVehicleRecreateThrottle throttle;
    CHECK(throttle.ShouldAttempt(1000));    // at once
    CHECK(!throttle.ShouldAttempt(1999));   // age 999
    CHECK(throttle.ShouldAttempt(2000));
    CHECK(throttle.ShouldAttempt(3000));    // three in the window
    CHECK(!throttle.ShouldAttempt(4000));
    CHECK(!throttle.ShouldAttempt(30999));  // the first is 29999 old
    CHECK(throttle.ShouldAttempt(31000));   // it left the window

    CVehicleRecreateThrottle wrap;
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 10;
    CHECK(wrap.ShouldAttempt(beforeWrap));
    CHECK(!wrap.ShouldAttempt(988));  // age 999 across the wrap
    CHECK(wrap.ShouldAttempt(989));
}

struct Vehicle { unsigned char storage[32]; };
struct FakePool
{
    Vehicle* m_pObjects;
    unsigned char* m_byteMap;
    int m_nSize;
    unsigned int GetObjectSize() { return sizeof(Vehicle); }
    bool IsFreeSlotAtIndex(int index) { return (m_byteMap[index] & 0x80) != 0; }
    int GetRef(Vehicle* vehicle) { const int index = static_cast<int>(vehicle - m_pObjects); return (index << 8) | m_byteMap[index]; }
    Vehicle* GetAtRef(int ref) { return m_byteMap[ref >> 8] == (ref & 0xFF) ? &m_pObjects[ref >> 8] : nullptr; }
};

void PoolSlotGeneration()
{
    Vehicle objects[3]{};
    unsigned char flags[3]{1, 5, 0x80};
    FakePool pool{objects, flags, 3};
    CVehiclePoolReference reference;
    CHECK(reference.Capture(&pool, &objects[1]));
    CHECK(reference.Matches(&pool, &objects[1]));
    CHECK(!reference.Matches(&pool, &objects[0]));
    flags[1] = 0x85;  // destroyed: what IsVehiclePointerValid also sees
    CHECK(!reference.Matches(&pool, &objects[1]));
    flags[1] = 6;  // a new vehicle in the same slot passes IsVehiclePointerValid, not this
    CHECK(!reference.Matches(&pool, &objects[1]));
    CHECK(!reference.Capture(&pool, &objects[2]));  // a free slot
    CHECK(!reference.Capture(&pool, reinterpret_cast<Vehicle*>(reinterpret_cast<uintptr_t>(objects) + 1)));
    reference.Reset();
    CHECK(!reference.Matches(&pool, &objects[1]));
}

// --- server side -----------------------------------------------------------------------------------------------------

struct Player
{
    int id;
    int m_nVehicleId = -1;
    int8_t m_nSeatId = -1;
};

struct ServerVehicle
{
    int m_nVehicleId;
    Player* m_pPlayers[VehicleSeatPolicy::NUM_SEATS]{};
    Player* syncer = nullptr;
    bool bMission = false;
    bool bUsedByPed = false;
};

struct FakeServer
{
    std::vector<ServerVehicle> vehicles;

    ServerVehicle* Find(int id)
    {
        for (ServerVehicle& vehicle : vehicles)
            if (vehicle.m_nVehicleId == id)
                return &vehicle;
        return nullptr;
    }
    auto Finder() { return [this](int id) { return Find(id); }; }

    void Enter(Player& player, int vehicleid, bool bPassenger, int seatid)
    {
        const int seat = VehicleSeatPolicy::SeatFromEnter(bPassenger, seatid);
        if (seat >= 0)
            VehicleSeatPolicy::SetOccupant(*Find(vehicleid), seat, &player, Finder());
    }
    void DriverUpdate(Player& player, int vehicleid)
    {
        VehicleSeatPolicy::SetOccupant(*Find(vehicleid), VehicleSeatPolicy::DRIVER_SEAT, &player, Finder());
        Find(vehicleid)->syncer = &player;
    }
    void PassengerUpdate(Player& player, int vehicleid, int seatid)
    {
        ServerVehicle& vehicle = *Find(vehicleid);
        const int seat = VehicleSeatPolicy::SeatFromPassenger(seatid);
        if (seat < 0)
            return;
        VehicleSeatPolicy::SetOccupant(vehicle, seat, &player, Finder());
        if (Player* syncer = VehicleSeatPolicy::ChoosePassengerSyncer<Player>(vehicle, vehicle.bMission, vehicle.bUsedByPed))
            vehicle.syncer = syncer;
    }
    void Exit(Player& player) { VehicleSeatPolicy::LeaveSeat<ServerVehicle>(&player, Finder()); }

    // one seat in one vehicle per player, and the ids agree with the tables
    void CheckConsistent(const std::vector<Player*>& players)
    {
        for (Player* player : players)
        {
            int seats = 0;
            for (ServerVehicle& vehicle : vehicles)
                for (int seat = 0; seat < VehicleSeatPolicy::NUM_SEATS; seat++)
                    if (vehicle.m_pPlayers[seat] == player)
                    {
                        seats++;
                        CHECK(player->m_nVehicleId == vehicle.m_nVehicleId && player->m_nSeatId == seat);
                    }
            CHECK(seats <= 1);
            if (seats == 0)
                CHECK(player->m_nVehicleId == -1 && player->m_nSeatId == -1);
        }
    }
};

void Seats()
{
    // an unknown passenger seat isn't the driver's seat any more
    CHECK(VehicleSeatPolicy::SeatFromEnter(true, VehicleSync::SEAT_UNKNOWN) == -1);
    CHECK(VehicleSeatPolicy::SeatFromEnter(false, 0) == VehicleSeatPolicy::DRIVER_SEAT);
    CHECK(VehicleSeatPolicy::SeatFromEnter(true, 0) == 1);
    CHECK(VehicleSeatPolicy::SeatFromPassenger(VehicleSync::SEAT_UNKNOWN) == -1);
    CHECK(VehicleSeatPolicy::SeatFromPassenger(6) == 7);
    CHECK(VehicleSeatPolicy::SeatFromPassenger(7) == -1);  // an 8th passenger has no server seat

    FakeServer server;
    server.vehicles.push_back({1});
    server.vehicles.push_back({2});
    Player host{0}, friendPlayer{1};
    const std::vector<Player*> players{&host, &friendPlayer};

    server.Enter(friendPlayer, 1, true, VehicleSync::SEAT_UNKNOWN);
    CHECK(server.Find(1)->m_pPlayers[0] == nullptr && friendPlayer.m_nVehicleId == -1);
    server.PassengerUpdate(friendPlayer, 1, VehicleSync::SEAT_UNKNOWN);  // unknown: no seat, no syncer
    CHECK(server.Find(1)->syncer == nullptr);
    server.CheckConsistent(players);

    // driving together
    server.Enter(host, 1, false, 0);
    server.DriverUpdate(host, 1);
    server.Enter(friendPlayer, 1, true, 0);
    server.PassengerUpdate(friendPlayer, 1, 0);
    CHECK(server.Find(1)->syncer == &host);  // a driver: the passenger doesn't take over
    server.CheckConsistent(players);

    // a seat change: passenger seat 0 to seat 2
    server.PassengerUpdate(friendPlayer, 1, 2);
    CHECK(server.Find(1)->m_pPlayers[1] == nullptr && server.Find(1)->m_pPlayers[3] == &friendPlayer);
    server.CheckConsistent(players);

    // driver and passenger swap: the host gets out and in as a passenger, the friend drives
    server.Exit(host);
    CHECK(server.Find(1)->m_pPlayers[0] == nullptr);
    server.PassengerUpdate(friendPlayer, 1, 2);  // no driver for a moment: the passenger syncs it
    CHECK(server.Find(1)->syncer == &friendPlayer);
    server.Enter(host, 1, true, 0);
    server.Enter(friendPlayer, 1, false, 0);  // straight from a passenger seat to the driver's
    server.DriverUpdate(friendPlayer, 1);
    server.PassengerUpdate(host, 1, 0);
    CHECK(server.Find(1)->m_pPlayers[0] == &friendPlayer && server.Find(1)->m_pPlayers[1] == &host);
    CHECK(server.Find(1)->m_pPlayers[3] == nullptr);  // the friend's old passenger seat is free
    CHECK(server.Find(1)->syncer == &friendPlayer);
    server.CheckConsistent(players);

    // into another vehicle without an exit (warped by a script): the old seat goes
    server.DriverUpdate(host, 2);
    CHECK(server.Find(1)->m_pPlayers[1] == nullptr && server.Find(2)->m_pPlayers[0] == &host);
    server.CheckConsistent(players);

    // the old driver's seat no longer blocks "no driver: a passenger becomes the syncer"
    server.Exit(friendPlayer);
    server.Enter(host, 1, true, 1);
    server.PassengerUpdate(host, 1, 1);
    CHECK(server.Find(1)->syncer == &host);
    server.CheckConsistent(players);

    // mission vehicles and the ones peds drive keep their syncer
    ServerVehicle mission{3};
    mission.m_pPlayers[2] = &friendPlayer;
    CHECK(VehicleSeatPolicy::ChoosePassengerSyncer<Player>(mission, true, false) == nullptr);
    CHECK(VehicleSeatPolicy::ChoosePassengerSyncer<Player>(mission, false, true) == nullptr);
    CHECK(VehicleSeatPolicy::ChoosePassengerSyncer<Player>(mission, false, false) == &friendPlayer);

    // a jacked seat: whoever held it loses it
    server.DriverUpdate(host, 2);
    CHECK(server.Find(1)->m_pPlayers[2] == nullptr && server.Find(2)->m_pPlayers[0] == &host);
    server.Enter(friendPlayer, 2, false, 0);
    CHECK(host.m_nVehicleId == -1 && host.m_nSeatId == -1 && server.Find(2)->m_pPlayers[0] == &friendPlayer);
    server.CheckConsistent(players);

    // a leave: out of every seat; a removed vehicle frees its occupants
    VehicleSeatPolicy::LeaveSeat<ServerVehicle>(&friendPlayer, server.Finder());
    server.CheckConsistent(players);
    server.Enter(host, 2, true, 3);
    VehicleSeatPolicy::ClearSeats(*server.Find(2));
    CHECK(host.m_nVehicleId == -1 && host.m_nSeatId == -1);
    server.CheckConsistent(players);

    // a stale second seat an older build left is cleaned up when the player moves
    ServerVehicle stale{4};
    stale.m_pPlayers[0] = &host;
    stale.m_pPlayers[5] = &host;
    host.m_nVehicleId = 4;
    host.m_nSeatId = 0;
    server.vehicles.push_back(stale);
    server.DriverUpdate(host, 1);
    CHECK(server.Find(4)->m_pPlayers[0] == nullptr && server.Find(4)->m_pPlayers[5] == nullptr);
    server.CheckConsistent(players);
}

void LeaveRule()
{
    // a car of the host's scripts goes back to the host
    CHECK(VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_MISSION, true));
    CHECK(VehicleSeatPolicy::OnSyncerLeft(true, true) == eSyncerLeft::GIVE_TO_HOST);
    // the friend's own mission car, or a random car: removed everywhere as before
    CHECK(!VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_MISSION, false));
    CHECK(!VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_RANDOM, true));
    CHECK(VehicleSeatPolicy::OnSyncerLeft(false, true) == eSyncerLeft::REMOVE);
    // the host itself leaves (or no host): nobody to give it to
    CHECK(VehicleSeatPolicy::OnSyncerLeft(true, false) == eSyncerLeft::REMOVE);

    // the server's flag over a car's life: the host spawns it for its mission, the friend drives it, the host releases it
    bool bHostMission = VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_MISSION, true);
    CHECK(VehicleSeatPolicy::OnSyncerLeft(bHostMission, true) == eSyncerLeft::GIVE_TO_HOST);
    bHostMission = VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_RANDOM, true);  // MARK_CAR_AS_NO_LONGER_NEEDED
    CHECK(VehicleSeatPolicy::OnSyncerLeft(bHostMission, true) == eSyncerLeft::REMOVE);
    // a car the friend's game spawned that the host's mission takes (STORE_CAR_CHAR_IS_IN, SET_VEHICLE_CREATED_BY)
    CHECK(VehicleSeatPolicy::OnSyncerLeft(VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_RANDOM, false),
              true) == eSyncerLeft::REMOVE);
    CHECK(VehicleSeatPolicy::OnSyncerLeft(VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_MISSION, true),
              true) == eSyncerLeft::GIVE_TO_HOST);
}

void SyncerCorrection()
{
    VehicleSeatPolicy::CSyncerCorrection correction;
    CHECK(correction.ShouldSend(100));
    CHECK(!correction.ShouldSend(1099));
    CHECK(correction.ShouldSend(1100));
    VehicleSeatPolicy::CSyncerCorrection wrap;
    CHECK(wrap.ShouldSend(std::numeric_limits<uint32_t>::max() - 10));
    CHECK(!wrap.ShouldSend(988));
    CHECK(wrap.ShouldSend(989));
}

// --- review fixes (0.1.1) ----------------------------------------------------------------------------------------------

// A game's packet buffer as client/src/CPacketBuffer.cpp keeps it: sorted by stamp, equal stamps in arrival order
struct FakeBuffer
{
    struct Item
    {
        uint32_t stamp;
        const char* what;
    };
    std::vector<Item> items;

    void Receive(uint32_t stamp, const char* what)
    {
        const auto it = std::upper_bound(items.begin(), items.end(), stamp,
            [](uint32_t time, const Item& item) { return time < item.stamp; });
        items.insert(it, {stamp, what});
    }
};

// The server's stamp on what it sends (server/src/CPacketFactory.cpp): its own clock for what it makes and for the
// lifecycle packets it relays, the sender's for the other relays
uint32_t ServerStamp(ePacketType type, uint32_t senderStamp, uint32_t serverNow)
{
    return senderStamp == 0 || EntityLifecycle::UsesServerTime(type) ? serverNow : senderStamp;
}

void LifecycleOrder()
{
    using EntityLifecycle::UsesServerTime;
    // what the server stamps itself when it relays: spawns, removals and the state the join snapshot carries
    CHECK(UsesServerTime(ePacketType::VEHICLE_SPAWN) && UsesServerTime(ePacketType::VEHICLE_REMOVE));
    CHECK(UsesServerTime(ePacketType::VEHICLE_DAMAGE) && UsesServerTime(ePacketType::VEHICLE_COMPONENT_ADD));
    CHECK(UsesServerTime(ePacketType::VEHICLE_COMPONENT_REMOVE) && UsesServerTime(ePacketType::SET_VEHICLE_CREATED_BY));
    CHECK(UsesServerTime(ePacketType::PED_SPAWN) && UsesServerTime(ePacketType::PED_REMOVE));
    // the player's own stream keeps its clock
    CHECK(!UsesServerTime(ePacketType::VEHICLE_IDLE_UPDATE) && !UsesServerTime(ePacketType::VEHICLE_DRIVER_UPDATE));
    CHECK(!UsesServerTime(ePacketType::VEHICLE_PASSENGER_UPDATE) && !UsesServerTime(ePacketType::VEHICLE_ENTER));
    CHECK(!UsesServerTime(ePacketType::VEHICLE_EXIT) && !UsesServerTime(ePacketType::VEHICLE_RADIO_STATION));
    CHECK(!UsesServerTime(ePacketType::PLAYER_ONFOOT_UPDATE) && !UsesServerTime(ePacketType::PED_ONFOOT));

    // The review's ghost car: the friend's car 7 goes (its clock 1010, ahead of the server's), and in the same server
    // tick (1000) the host's new car gets the freed id 7. Upstream's stamps: the host handles VEHICLE_CONFIRM(7) first
    // and the late VEHICLE_REMOVE(7) deletes its new car.
    const uint32_t friendClock = 1010;
    const uint32_t serverNow = 1000;
    FakeBuffer upstream;
    upstream.Receive(friendClock, "remove");  // relayed with the sender's clock
    upstream.Receive(serverNow, "confirm");
    CHECK(std::string(upstream.items[0].what) == "confirm");
    // now: both on the server's clock, in the order the server sent them
    FakeBuffer host;
    host.Receive(ServerStamp(ePacketType::VEHICLE_REMOVE, friendClock, serverNow), "remove");
    host.Receive(ServerStamp(ePacketType::VEHICLE_CONFIRM, 0, serverNow), "confirm");
    CHECK(std::string(host.items[0].what) == "remove" && std::string(host.items[1].what) == "confirm");

    // the joiner: the snapshot's VEHICLE_SPAWN(7) at 2000, then a removal the friend sent before the join (its clock
    // 1990) relayed at 2001: upstream's stamps put the removal first (a ghost); now it comes after the spawn
    FakeBuffer joinerUpstream;
    joinerUpstream.Receive(2000, "spawn");
    joinerUpstream.Receive(1990, "remove");
    CHECK(std::string(joinerUpstream.items[0].what) == "remove");
    FakeBuffer joiner;
    joiner.Receive(ServerStamp(ePacketType::VEHICLE_SPAWN, 0, 2000), "spawn");
    joiner.Receive(ServerStamp(ePacketType::VEHICLE_REMOVE, 1990, 2001), "remove");
    CHECK(std::string(joiner.items[0].what) == "spawn" && std::string(joiner.items[1].what) == "remove");

    // a game's own updates of a vehicle come after its spawn anyway: it handles the server-stamped VEHICLE_CONFIRM no
    // earlier than its clock reaches the stamp plus the buffer delay, and stamps its updates with that clock
    const uint32_t confirmStamp = 5000;
    const uint32_t firstUpdate = confirmStamp + 50;
    CHECK(!EntityLifecycle::IsFromBeforeCreation(firstUpdate, confirmStamp));
    // a late update or removal sent for the vehicle that had the id before: the server drops it
    CHECK(EntityLifecycle::IsFromBeforeCreation(confirmStamp - 1, confirmStamp));
    CHECK(!EntityLifecycle::IsFromBeforeCreation(confirmStamp, confirmStamp));
    // across the wrap of the 32-bit clocks
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 5;
    CHECK(EntityLifecycle::IsFromBeforeCreation(beforeWrap, 10));
    CHECK(!EntityLifecycle::IsFromBeforeCreation(10, beforeWrap));
    // only within the window: a game without its first server-time answer stamps with its PC's uptime (here 30 days
    // against a server up for an hour) and isn't taken for late
    const uint32_t createdAt = 100000;
    CHECK(EntityLifecycle::IsFromBeforeCreation(createdAt - EntityLifecycle::LATE_PACKET_WINDOW_MS + 1, createdAt));
    CHECK(!EntityLifecycle::IsFromBeforeCreation(createdAt - EntityLifecycle::LATE_PACKET_WINDOW_MS, createdAt));
    CHECK(!EntityLifecycle::IsFromBeforeCreation(30u * 24u * 3600u * 1000u, 3600u * 1000u));
}

void RoundRobinIds()
{
    // the server's vehicle and ped ids: the next free one after the last handed out, so a freed id isn't handed out again
    // at once (upstream: lowest free, the id of the vehicle removed in the same tick)
    bool taken[6] = {};
    auto isFree = [&](int id) { return !taken[id]; };
    int last = -1;
    for (int expected = 0; expected < 6; expected++)
    {
        const int id = EntityLifecycle::NextFreeId(last, 6, isFree);
        CHECK(id == expected);
        taken[id] = true;
        last = id;
    }
    CHECK(EntityLifecycle::NextFreeId(last, 6, isFree) == -1);  // all taken
    taken[2] = false;                                            // 2 goes
    taken[4] = false;                                            // then 4
    CHECK(EntityLifecycle::NextFreeId(last, 6, isFree) == 2);    // after 5 the search wraps: 2 is the next free
    taken[2] = true;
    last = 2;
    taken[0] = false;
    CHECK(EntityLifecycle::NextFreeId(last, 6, isFree) == 4);  // 4 before 0, which was freed later in the cycle
    // the client's temp ids keep the same rule
    CHECK(VehicleSyncPolicy::NextTempId(last, 6, isFree) == 4);
}

void TempRecords()
{
    using VehicleSyncPolicy::IsTempRecordExpired;
    using VehicleSyncPolicy::TEMP_RECORD_MAX_AGE_MS;
    CHECK(!IsTempRecordExpired(1000, 1000 + TEMP_RECORD_MAX_AGE_MS - 1));
    CHECK(IsTempRecordExpired(1000, 1000 + TEMP_RECORD_MAX_AGE_MS));
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 100;
    CHECK(!IsTempRecordExpired(beforeWrap, 500));                     // age 601
    CHECK(IsTempRecordExpired(beforeWrap, TEMP_RECORD_MAX_AGE_MS));   // age MAX + 101
    // a confirmation for a slot without a record (it expired): the vehicle goes everywhere, asked by its creator
    CHECK(VehicleSyncPolicy::DecideConfirm(false, false) == eConfirm::UNKNOWN_TEMP);
    CHECK(VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_UNCONFIRMED, false, true, false));
}

void ExpiredAssignments()
{
    using VehicleSyncPolicy::eExpiredAssignment;
    CHECK(VehicleSyncPolicy::DecideExpiredAssignment(false, false) == eExpiredAssignment::NOTHING);
    CHECK(VehicleSyncPolicy::DecideExpiredAssignment(false, true) == eExpiredAssignment::NOTHING);
    // "you sync it" for a vehicle this game doesn't have: removed everywhere, as a takeover with nothing to take over
    CHECK(VehicleSyncPolicy::DecideExpiredAssignment(true, false) == eExpiredAssignment::REMOVE_EVERYWHERE);
    CHECK(VehicleSyncPolicy::DecideExpiredAssignment(true, true) ==
          eExpiredAssignment::REMOVE_EVERYWHERE_AND_LOCAL_RECORD);
    // the server takes it while that game is still the syncer; if the vehicle has moved on, the game gets it back
    using VehicleSeatPolicy::eRemove;
    CHECK(VehicleSeatPolicy::DecideRemove(VehicleSync::REMOVE_BY_SYNCER, false, true, false, false, false, true) ==
          eRemove::REMOVE);
    CHECK(VehicleSeatPolicy::DecideRemove(VehicleSync::REMOVE_BY_SYNCER, false, false, false, false, false, true) ==
          eRemove::REJECT_AND_RESEND);
}

void Removals()
{
    using VehicleSeatPolicy::DecideRemove;
    using VehicleSeatPolicy::eRemove;
    // stamped before the vehicle existed: about an older vehicle with the id, nothing happens, whoever sent it
    CHECK(DecideRemove(VehicleSync::REMOVE_BY_SYNCER, true, true, true, true, true, true) == eRemove::DROP_STALE);
    // the review's crash scenario on the server side: the host's game destroyed a car it still synced while the friend
    // had already started driving it (the server made the friend the syncer): rejected, and the host gets it back
    CHECK(DecideRemove(VehicleSync::REMOVE_BY_SYNCER, false, false, true, true, false, false) ==
          eRemove::REJECT_AND_RESEND);
    // the creator before its confirmation, the host for its mission
    CHECK(DecideRemove(VehicleSync::REMOVE_UNCONFIRMED, false, false, true, false, false, true) == eRemove::REMOVE);
    CHECK(DecideRemove(VehicleSync::REMOVE_BY_HOST_MISSION, false, false, false, true, true, false) == eRemove::REMOVE);
    CHECK(DecideRemove(VehicleSync::REMOVE_BY_HOST_MISSION, false, true, true, false, false, true) ==
          eRemove::REJECT_AND_RESEND);
    // the syncer's game starts over: a car of the host's scripts goes back to the host, anything else everywhere
    CHECK(VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_SYNCER_RESTARTED, true, false, false));
    CHECK(!VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_SYNCER_RESTARTED, false, true, true));
    CHECK(DecideRemove(VehicleSync::REMOVE_SYNCER_RESTARTED, false, true, false, false, true, true) ==
          eRemove::GIVE_TO_HOST);
    CHECK(DecideRemove(VehicleSync::REMOVE_SYNCER_RESTARTED, false, true, false, false, false, true) == eRemove::REMOVE);
    // the host itself starts over: its mission is gone with its game
    CHECK(DecideRemove(VehicleSync::REMOVE_SYNCER_RESTARTED, false, true, true, true, true, false) == eRemove::REMOVE);
    // a restarting game that no longer syncs the car gets it back like any rejected removal
    CHECK(DecideRemove(VehicleSync::REMOVE_SYNCER_RESTARTED, false, false, false, false, true, true) ==
          eRemove::REJECT_AND_RESEND);
    CHECK(std::string(VehicleSync::RemoveReasonName(VehicleSync::REMOVE_SYNCER_RESTARTED)) == "syncer-restarted");
}

void LeavingAVehicle()
{
    using VehicleSyncPolicy::ChooseLocalUpdate;
    using VehicleSyncPolicy::eLocalUpdate;
    CHECK(ChooseLocalUpdate(false, false, false) == eLocalUpdate::ON_FOOT);
    CHECK(ChooseLocalUpdate(true, true, false) == eLocalUpdate::DRIVER);
    CHECK(ChooseLocalUpdate(true, false, false) == eLocalUpdate::PASSENGER);
    CHECK(ChooseLocalUpdate(true, true, true) == eLocalUpdate::NONE);   // getting out: no driver update
    CHECK(ChooseLocalUpdate(true, false, true) == eLocalUpdate::NONE);  // nor a passenger update
    // the vehicle's own state goes out as idle updates meanwhile
    CHECK(VehicleSyncPolicy::IsDrivenForIdleUpdates(true, false));
    CHECK(!VehicleSyncPolicy::IsDrivenForIdleUpdates(true, true));
    CHECK(!VehicleSyncPolicy::IsDrivenForIdleUpdates(false, false));

    // The review's case on the server: the host drives, the friend sits beside it. The host gets out (VEHICLE_EXIT); its
    // game used to send driver updates through the exit, each of which put it back into the driver's seat, so the friend
    // never took the car over. Now its game sends none until it is out.
    FakeServer server;
    server.vehicles.push_back({1});
    Player host{0}, friendPlayer{1};
    server.Enter(host, 1, false, 0);
    server.DriverUpdate(host, 1);
    server.Enter(friendPlayer, 1, true, 0);
    server.PassengerUpdate(friendPlayer, 1, 0);
    CHECK(server.Find(1)->syncer == &host);
    server.Exit(host);  // VEHICLE_EXIT at the start of the exit
    if (ChooseLocalUpdate(true, true, true) == eLocalUpdate::DRIVER)  // during the exit animation
        server.DriverUpdate(host, 1);
    server.PassengerUpdate(friendPlayer, 1, 0);
    CHECK(server.Find(1)->m_pPlayers[0] == nullptr && server.Find(1)->syncer == &friendPlayer);
    // an exit that ends without getting out (the player keeps driving): the driver updates come back and so does the seat
    if (ChooseLocalUpdate(true, true, false) == eLocalUpdate::DRIVER)
        server.DriverUpdate(host, 1);
    CHECK(server.Find(1)->m_pPlayers[0] == &host && server.Find(1)->syncer == &host);
    server.CheckConsistent({&host, &friendPlayer});
}

// --- second review fixes (0.1.1) ---------------------------------------------------------------------------------------

void CreatedByAtConfirm()
{
    using VehicleSyncPolicy::DecideCreatedByAtConfirm;
    // CREATE_CAR and MARK_CAR_AS_NO_LONGER_NEEDED within a few frames on the host: the hooks found no confirmed record, the
    // record still says MISSION; at VEHICLE_CONFIRM the record takes RANDOM and the host sends it
    auto change = DecideCreatedByAtConfirm(true, VehicleSync::CREATED_BY_MISSION, VehicleSync::CREATED_BY_RANDOM);
    CHECK(change.bChanged && change.bSend);
    // the same on another game: its record takes it, the server takes created-by changes from the host only
    change = DecideCreatedByAtConfirm(false, VehicleSync::CREATED_BY_MISSION, VehicleSync::CREATED_BY_RANDOM);
    CHECK(change.bChanged && !change.bSend);
    // nothing changed
    change = DecideCreatedByAtConfirm(true, VehicleSync::CREATED_BY_RANDOM, VehicleSync::CREATED_BY_RANDOM);
    CHECK(!change.bChanged && !change.bSend);
    // the server's flag follows the host's SET_VEHICLE_CREATED_BY: the released car no longer goes back to the host
    CHECK(!VehicleSeatPolicy::IsHostMission(VehicleSync::CREATED_BY_RANDOM, true));

    // --coopdiag: a quick confirmation is routine, a slow one, one that replaced a record or found a change gets a line
    using VehicleSyncPolicy::IsConfirmWorthALine;
    CHECK(!IsConfirmWorthALine(VehicleSyncPolicy::CONFIRM_SLOW_MS - 1, false, false));
    CHECK(IsConfirmWorthALine(VehicleSyncPolicy::CONFIRM_SLOW_MS, false, false));
    CHECK(IsConfirmWorthALine(10, true, false));
    CHECK(IsConfirmWorthALine(10, false, true));
}

void PedDrivers()
{
    using VehicleSyncPolicy::DecidePedDriverVehicle;
    using VehicleSyncPolicy::ePedDriverVehicle;
    // the review's frozen ghost: after a restart the NPC's car copy and its driver copy are gone; no car without a driver
    CHECK(DecidePedDriverVehicle(false, false) == ePedDriverVehicle::SKIP);
    // the driver copy is back (the restart's snapshot, or recreated from this update): the car comes back with it
    CHECK(DecidePedDriverVehicle(false, true) == ePedDriverVehicle::RECREATE);
    CHECK(DecidePedDriverVehicle(true, true) == ePedDriverVehicle::SEAT_AND_APPLY);
    // the car copy is there but its driver copy died: the car still moves with the updates instead of standing still
    CHECK(DecidePedDriverVehicle(true, false) == ePedDriverVehicle::APPLY_VEHICLE);
}

void LocalExits()
{
    using VehicleSyncPolicy::CLocalExitTracker;
    using VehicleSyncPolicy::eExitNotice;

    // the exit key: VEHICLE_EXIT went out from CPlayerInfo::Process, nothing more
    CLocalExitTracker key;
    CHECK(key.Update(true, false, false) == eExitNotice::NONE);  // driving
    key.OnKeyExitSent();
    CHECK(key.Update(true, true, false) == eExitNotice::NONE);  // the exit runs
    CHECK(key.Update(true, true, false) == eExitNotice::NONE);
    CHECK(key.Update(false, false, false) == eExitNotice::NONE);  // out: no "warped" either

    // a script's Task.LeaveCar (#3, #4): announced once, when the exit starts
    CLocalExitTracker script;
    CHECK(script.Update(true, false, false) == eExitNotice::NONE);
    CHECK(script.Update(true, true, false) == eExitNotice::LEAVE);
    CHECK(script.Update(true, true, false) == eExitNotice::NONE);
    CHECK(script.Update(false, false, false) == eExitNotice::NONE);
    // getting in again and out with the next script task: announced again
    CHECK(script.Update(true, false, false) == eExitNotice::NONE);
    CHECK(script.Update(true, true, false) == eExitNotice::LEAVE);

    // jacked: the dragged-out task, not a leave task; the victim's game sends no driver updates meanwhile
    CLocalExitTracker jacked;
    CHECK(jacked.Update(true, false, false) == eExitNotice::NONE);
    CHECK(jacked.Update(true, false, true) == eExitNotice::DRAGGED_OUT);
    CHECK(jacked.Update(true, true, true) == eExitNotice::NONE);  // the drag's own leave subtask: no second one
    CHECK(jacked.Update(false, false, false) == eExitNotice::NONE);
    CHECK(VehicleSyncPolicy::ChooseLocalUpdate(true, true, true) == VehicleSyncPolicy::eLocalUpdate::NONE);

    // a warp out (a script, a respawn): out without an exit task
    CLocalExitTracker warped;
    CHECK(warped.Update(true, false, false) == eExitNotice::NONE);
    CHECK(warped.Update(false, false, false) == eExitNotice::WARPED);
    CHECK(warped.Update(false, false, false) == eExitNotice::NONE);

    // an exit that ends without getting out (the player keeps driving), then a new one
    CLocalExitTracker aborted;
    CHECK(aborted.Update(true, true, false) == eExitNotice::LEAVE);
    CHECK(aborted.Update(true, false, false) == eExitNotice::NONE);
    CHECK(aborted.Update(true, true, false) == eExitNotice::LEAVE);
}

void Headings()
{
    // SetOrientation(0, 0, h): forward (-sin h, cos h, 0); the server's resent vehicles face the way they did
    const float headings[] = {0.0f, 0.5f, 1.5707963f, 3.0f, -3.0f, -1.0f};
    for (float heading : headings)
    {
        const float back = VehicleSync::HeadingFromForward(-std::sin(heading), std::cos(heading));
        CHECK(std::fabs(back - heading) < 1e-4f);
    }
    CHECK(VehicleSync::HeadingFromForward(0.0f, 0.0f) == 0.0f);  // straight up or down
    // upstream: the forward vector's z read as degrees, about 0 for a car on the road
    const float east = VehicleSync::HeadingFromForward(1.0f, 0.0f);
    CHECK(std::fabs(east + 1.5707963f) < 1e-4f);
}

void SeatsOnFoot()
{
    using VehicleSeatPolicy::OnFootSeatTimeoutMs;
    using VehicleSeatPolicy::ShouldFreeSeatOnFoot;
    // an entry that never completed: the seat recorded at VEHICLE_ENTER stays while the player may still be getting in
    CHECK(!ShouldFreeSeatOnFoot(true, 1000 + OnFootSeatTimeoutMs - 1, 1000));
    CHECK(ShouldFreeSeatOnFoot(true, 1000 + OnFootSeatTimeoutMs, 1000));
    CHECK(!ShouldFreeSeatOnFoot(false, 1000 + OnFootSeatTimeoutMs, 1000));  // nothing to free
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 100;
    CHECK(!ShouldFreeSeatOnFoot(true, 500, beforeWrap));  // 601 ms across the wrap
}

int main()
{
    AssignmentsAreStated();
    AssignmentBeforeTheVehicle();
    RemovalBeforeConfirmation();
    DestroyedVehicles();
    CopiesAndCreatedBy();
    Takeover();
    RecreateThrottle();
    PoolSlotGeneration();
    Seats();
    LeaveRule();
    SyncerCorrection();
    LifecycleOrder();
    RoundRobinIds();
    TempRecords();
    ExpiredAssignments();
    Removals();
    LeavingAVehicle();
    SeatsOnFoot();
    CreatedByAtConfirm();
    PedDrivers();
    LocalExits();
    Headings();
    std::puts("vehicle_sync: all checks passed");
}
