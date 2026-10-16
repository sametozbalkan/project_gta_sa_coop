// Private build: policy test for the vehicle sync protocol (0.1.1) without GTA or ENet: the client's records
// (client/src/CVehicleSyncPolicy.h: stated assignments and the ones that wait for their vehicle, removal before
// VEHICLE_CONFIRM, what a destroyed vehicle does to its record, copies and their createdBy, takeovers, recreation, temp
// ids, the pool slot generation) and the server's seats, syncers and removals (server/src/CVehicleSeatPolicy.h). Build
// and run: see CLAUDE.md (Fork changes, vehicle sync protocol).
#include "../client/src/CVehicleSyncPolicy.h"
#include "../server/src/CVehicleSeatPolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
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
    void Appear(int id, uint16_t model, bool bSyncing, uint32_t now)  // VEHICLE_SPAWN (false) or VEHICLE_CONFIRM (true)
    {
        records[id] = {model, bSyncing};
        if (const auto state = pending.Take(id, model, now))
            records[id].bSyncing = *state;
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
    client.Appear(5, 400, false, 0);
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
    client.Appear(7, 411, false, 200);
    CHECK(client.records[7].bSyncing && client.pending.Size() == 0);

    // our own vehicle, confirmed after the server already gave it to someone else
    FakeClient creator;
    creator.Assign(9, 560, false, 100);
    creator.Appear(9, 560, true, 150);
    CHECK(!creator.records[9].bSyncing);

    // the newest wins
    FakeClient twice;
    twice.Assign(3, 400, true, 0);
    twice.Assign(3, 400, false, 10);
    twice.Appear(3, 400, true, 20);
    CHECK(!twice.records[3].bSyncing);

    // an assignment of an older vehicle with the same id (another model) isn't applied to the new one
    FakeClient older;
    older.Assign(4, 400, true, 0);
    older.Appear(4, 402, false, 10);
    CHECK(!older.records[4].bSyncing && older.pending.Size() == 0);

    // VEHICLE_REMOVE of the id drops it
    FakeClient removed;
    removed.Assign(6, 400, true, 0);
    removed.Remove(6);
    removed.Appear(6, 400, false, 10);
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
    CHECK(VehicleSyncPolicy::DecideVehicleGone(false, true, false, false) == eVehicleGone::REMOVE_AT_CONFIRM);
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
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, false, false) == eVehicleGone::SEND_REMOVE);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, true, true, true) == eVehicleGone::SEND_REMOVE);
    // the host's mission deleted a copy: removed on every game, through the host's request
    CHECK(VehicleSyncPolicy::IsHostMissionDelete(true, false, true));
    CHECK(!VehicleSyncPolicy::IsHostMissionDelete(false, false, true));  // the friend's own scripts
    CHECK(!VehicleSyncPolicy::IsHostMissionDelete(true, true, true));    // the host syncs it: a normal removal
    CHECK(!VehicleSyncPolicy::IsHostMissionDelete(true, false, false));  // the host's population or another script
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, true, false) == eVehicleGone::HOST_MISSION_REMOVE);
    CHECK(VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_BY_HOST_MISSION, false, false, true));
    CHECK(!VehicleSeatPolicy::AcceptRemove(VehicleSync::REMOVE_BY_HOST_MISSION, true, true, false));
    // a copy this game lost to its own engine: recreated from the syncer's next update, a wreck isn't
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, false, false) == eVehicleGone::KEEP_FOR_RECREATE);
    CHECK(VehicleSyncPolicy::DecideVehicleGone(true, false, false, true) == eVehicleGone::KEEP_WRECK);
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
    std::puts("vehicle_sync: all checks passed");
}
