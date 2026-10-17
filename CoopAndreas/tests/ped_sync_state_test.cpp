#include "../client/src/CPedSyncState.h"
#include "../shared/network/entity_lifecycle.h"
#include "../server/src/CPedHandoverPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

struct Ped { unsigned char storage[24]; };
struct FakePool
{
    Ped* m_pObjects;
    unsigned char* m_byteMap;
    int m_nSize;
    int metadataReads = 0;
    unsigned int GetObjectSize() { return sizeof(Ped); }
    bool IsFreeSlotAtIndex(int index) { ++metadataReads; return (m_byteMap[index] & 0x80) != 0; }
    int GetRef(Ped* ped) { ++metadataReads; const int index = static_cast<int>(ped - m_pObjects); return (index << 8) | m_byteMap[index]; }
    Ped* GetAtRef(int ref) { ++metadataReads; return m_byteMap[ref >> 8] == (ref & 0xFF) ? &m_pObjects[ref >> 8] : nullptr; }
};

void PoolLifetime()
{
    Ped objects[2]{};
    unsigned char flags[2]{1, 2};
    FakePool pool{objects, flags, 2};
    CPedPoolReference captured;
    CHECK(captured.Capture(&pool, &objects[1]));
    CHECK(captured.GetRef() == 258);
    CHECK(captured.Matches(&pool, &objects[1])); // No world-membership dependency.
    CHECK(!captured.Matches(&pool, &objects[0]));
    flags[1] = 0x82; // Deleted, even if memory and vtable bytes remain intact.
    CHECK(!captured.Matches(&pool, &objects[1]));
    flags[1] = 3; // New ped occupies the same pointer.
    CHECK(!captured.Matches(&pool, &objects[1]));
    flags[1] = 2;
    Ped replacement[2]{};
    FakePool replacementPool{replacement, flags, 2};
    CHECK(!captured.Matches(&replacementPool, &objects[1]));
    unsigned char replacementFlags[2]{1, 2};
    replacementPool = {objects, replacementFlags, 2};
    CHECK(!captured.Matches(&replacementPool, &objects[1]));
    pool.m_nSize = 1;
    CHECK(!captured.Matches(&pool, &objects[1]));
    CHECK(!captured.Matches(static_cast<FakePool*>(nullptr), &objects[1]));

    pool.m_nSize = 2;
    CPedPoolReference invalid;
    pool.metadataReads = 0;
    CHECK(!invalid.Capture(&pool, reinterpret_cast<Ped*>(reinterpret_cast<uintptr_t>(objects) + 1)));
    CHECK(!invalid.Capture(&pool, &objects[2]));
    CHECK(!invalid.Capture(&pool, reinterpret_cast<Ped*>(reinterpret_cast<uintptr_t>(objects) - 1)));
    CHECK(pool.metadataReads == 0); // No flags read before range/alignment checks.
    flags[1] |= 0x80;
    CHECK(!invalid.Capture(&pool, &objects[1]));
}

struct Visual { int weapon; bool aim; bool duck; };

void LatestAndReadiness()
{
    CLatestPedSnapshot<Visual> state;
    bool valid = true;
    bool clump = false;
    bool intelligence = true;
    int engineCalls = 0;
    Visual applied{};
    auto process = [&](uint32_t now) {
        return state.Process(now, [&] { return valid; }, [&] { return clump && intelligence; },
            [&](const Visual& value) { ++engineCalls; applied = value; return true; });
    };
    state.Submit({25, true, true}, 100);
    CHECK(process(100) == ePedSnapshotResult::Deferred);
    CHECK(engineCalls == 0);
    state.Submit({22, false, false}, 110);
    CHECK(process(111) == ePedSnapshotResult::Deferred);
    clump = true;
    intelligence = false;
    CHECK(process(112) == ePedSnapshotResult::Deferred);
    CHECK(engineCalls == 0);
    intelligence = true;
    CHECK(process(113) == ePedSnapshotResult::Applied);
    CHECK(engineCalls == 1 && applied.weapon == 22 && !applied.aim && !applied.duck);
    CHECK(process(114) == ePedSnapshotResult::Idle);
    CHECK(engineCalls == 1); // Snapshot consumed once, no replay queue.

    state.Submit({25, true, true}, 200);
    valid = false;
    CHECK(process(201) == ePedSnapshotResult::Invalid);
    valid = true;
    CHECK(process(202) == ePedSnapshotResult::Idle);
    CHECK(engineCalls == 1);
}

void ExpiryAndWrap()
{
    CLatestPedSnapshot<int> state;
    int calls = 0;
    auto process = [&](uint32_t now, bool ready) {
        return state.Process(now, [] { return true; }, [=] { return ready; },
            [&](int) { ++calls; return true; });
    };
    state.Submit(1, 100);
    CHECK(process(2099, true) == ePedSnapshotResult::Applied);
    state.Submit(2, 100);
    CHECK(process(2100, true) == ePedSnapshotResult::Expired);
    CHECK(process(2101, true) == ePedSnapshotResult::Idle);
    CHECK(calls == 1);
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 100;
    state.Submit(3, beforeWrap);
    CHECK(process(1898, false) == ePedSnapshotResult::Deferred); // Age 1999.
    CHECK(process(1899, true) == ePedSnapshotResult::Expired); // Age 2000.
    state.Submit(4, 1900);
    CHECK(process(1901, true) == ePedSnapshotResult::Applied); // Fresh state resumes.
}

void CancellationAndStreamingLoss()
{
    CLatestPedSnapshot<int> state;
    int calls = 0;
    // Test the shared cancellation operation; packet-handler wiring requires
    // source review and the actual ownership/vehicle transitions require GTA.
    state.Submit(1, 100);
    CHECK(state.Cancel());
    CHECK(!state.Cancel());
    CHECK(state.Process(101, [] { return true; }, [] { return true; },
        [&](int) { ++calls; return true; }) == ePedSnapshotResult::Idle);
    CHECK(calls == 0);

    bool valid = true;
    bool ready = true;
    state.Submit(5, 100);
    CHECK(state.Process(101, [&] { return valid; }, [&] { return ready; }, [&](int value) {
        CHECK(value == 5); ++calls; ready = false; return false;
    }) == ePedSnapshotResult::Deferred);
    CHECK(state.Process(102, [&] { return valid; }, [&] { return ready; }, [&](int) {
        ++calls; return true;
    }) == ePedSnapshotResult::Deferred);
    CHECK(calls == 1);
    state.Submit(6, 103); // Replaces partially applied visual state.
    ready = true;
    CHECK(state.Process(104, [&] { return valid; }, [&] { return ready; }, [&](int value) {
        CHECK(value == 6); ++calls; return true;
    }) == ePedSnapshotResult::Applied);

    state.Submit(7, 105);
    CHECK(state.Process(106, [&] { return valid; }, [] { return true; }, [&](int value) {
        valid = false;
        state.Cancel(); // Mirrors GetLivePed invalidation during streaming.
        CHECK(value == 7); // Apply owns a copy despite cancellation.
        return false;
    }) == ePedSnapshotResult::Invalid);
    valid = true;
    CHECK(state.Process(107, [&] { return valid; }, [] { return true; }, [&](int) {
        ++calls; return true;
    }) == ePedSnapshotResult::Idle);
    CHECK(calls == 2);
}

// private build (remote player ped recovery)
void RecreatedPlayerPedReference()
{
    // A remote player ped is destroyed and recreated: the old reference never matches again, also when the new
    // ped lands in the same slot, and a fresh capture of the new ped does.
    Ped objects[2]{};
    unsigned char flags[2]{1, 7};
    FakePool pool{objects, flags, 2};
    CPedPoolReference player;
    CHECK(player.Capture(&pool, &objects[1]));
    flags[1] = 0x87; // Destroyed: the slot is free, memory and vtable bytes may remain.
    CHECK(!player.Matches(&pool, &objects[1]));
    flags[1] = 8; // Recreated in the same slot, next generation.
    CHECK(!player.Matches(&pool, &objects[1]));
    CPedPoolReference recreated;
    CHECK(recreated.Capture(&pool, &objects[1]));
    CHECK(recreated.Matches(&pool, &objects[1]));
    CHECK(recreated.GetRef() == ((1 << 8) | 8));
}

void RecoveryThrottle()
{
    CPedRecoveryThrottle throttle;
    CHECK(throttle.ShouldAttempt(100)); // The first loss is handled at once.
    CHECK(!throttle.ShouldAttempt(100));
    CHECK(!throttle.ShouldAttempt(1099)); // Age 999.
    CHECK(throttle.ShouldAttempt(1100)); // Age 1000: retry.
    CHECK(!throttle.ShouldAttempt(1200));
    throttle.Reset(); // Healthy again.
    CHECK(throttle.ShouldAttempt(1201)); // A new loss is handled at once.

    CPedRecoveryThrottle wrap;
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 10;
    CHECK(wrap.ShouldAttempt(beforeWrap));
    CHECK(!wrap.ShouldAttempt(988)); // Age 999 across the tick wrap.
    CHECK(wrap.ShouldAttempt(989)); // Age 1000.
}

// private build (vehicle sync protocol review): the server's ped ids and the order of PED_SPAWN/PED_REMOVE
// (shared/network/entity_lifecycle.h, applied in server/src/CNetworkPedManager.cpp, PacketHandlers/peds.cpp, CPacketFactory)
void PedIdsAndRemovals()
{
    // A client finds a ped by the first record with its id (CNetworkPedManager::GetPed), and its PED_REMOVE handler deletes
    // that record; a syncing one even sends PED_REMOVE back and leaves its ped unsynced. With the sender's clock on the
    // relayed spawns and removals, a game whose clock ran ahead could have the server's PED_CONFIRM of a reused id handled
    // before the late PED_SPAWN and PED_REMOVE of the ped that had it, and the removal then hit the new ped (a mission
    // ped of the host's). The server stamps both itself now, and hands out ids round robin.
    CHECK(EntityLifecycle::UsesServerTime(ePacketType::PED_SPAWN));
    CHECK(EntityLifecycle::UsesServerTime(ePacketType::PED_REMOVE));
    CHECK(!EntityLifecycle::UsesServerTime(ePacketType::PED_CONFIRM));  // made by the server: its clock anyway
    CHECK(!EntityLifecycle::UsesServerTime(ePacketType::PED_ONFOOT));
    CHECK(!EntityLifecycle::UsesServerTime(ePacketType::PED_DRIVER_UPDATE));

    bool taken[4] = {true, false, false, false};
    auto isFree = [&](int id) { return !taken[id]; };
    CHECK(EntityLifecycle::NextFreeId(0, 4, isFree) == 1);
    taken[1] = true;
    taken[0] = false;                                       // ped 0 goes
    CHECK(EntityLifecycle::NextFreeId(1, 4, isFree) == 2);  // not 0 again at once

    // a late removal or update stamped before the server created the ped with that id is dropped by the server
    CHECK(EntityLifecycle::IsFromBeforeCreation(9999, 10000));
    CHECK(!EntityLifecycle::IsFromBeforeCreation(10050, 10000));
}

// private build (vehicle sync protocol, second review): ped copies a game loses and gets back
void LostCopies()
{
    using PedSyncPolicy::ShouldRecreateCopy;
    // a copy that couldn't be made (a full ped pool: upstream wrote through the NULL `new` gave) or that the game
    // destroyed comes back from its syncer's next update; this game's own peds never do (their record goes)
    CHECK(ShouldRecreateCopy(false, true, false));
    CHECK(!ShouldRecreateCopy(true, true, false));
    CHECK(!ShouldRecreateCopy(false, false, false));
    CHECK(!ShouldRecreateCopy(false, true, true));

    CPedCopyRecreateThrottle throttle;
    CHECK(throttle.ShouldAttempt(1000));    // at once
    CHECK(!throttle.ShouldAttempt(1999));   // age 999
    CHECK(throttle.ShouldAttempt(2000));
    CHECK(throttle.ShouldAttempt(3000));    // three in the window
    CHECK(!throttle.ShouldAttempt(4000));
    CHECK(!throttle.ShouldAttempt(30999));  // the first is 29999 old
    CHECK(throttle.ShouldAttempt(31000));
    CPedCopyRecreateThrottle wrap;
    const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 10;
    CHECK(wrap.ShouldAttempt(beforeWrap));
    CHECK(!wrap.ShouldAttempt(988));  // age 999 across the wrap
    CHECK(wrap.ShouldAttempt(989));
}

struct HandoverPlayer
{
    int id;
    std::vector<const void*> m_vPedClaims;
};
struct HandoverPed
{
    HandoverPlayer* m_pSyncer;
};

void Restarts()
{
    using PedSyncPolicy::DecideRestartRecord;
    using PedSyncPolicy::DecideSpawn;
    using PedSyncPolicy::eRestartRecord;
    using PedSyncPolicy::eSpawn;
    // the restarting game: every record goes at once, its own peds with PED_REMOVE, the copies without a word
    CHECK(DecideRestartRecord(true) == eRestartRecord::SEND_REMOVE);
    CHECK(DecideRestartRecord(false) == eRestartRecord::DROP);

    // the server's snapshot after PLAYER_GAME_RESTARTED: every ped another game syncs, not the restarting game's own
    HandoverPlayer host{0, {}};
    HandoverPlayer friendPlayer{1, {}};
    HandoverPed sweet{&host};       // the host's mission ped: the friend's restart brings it back there
    HandoverPed traffic{&friendPlayer};
    HandoverPed orphan{nullptr};
    CHECK(PedHandoverPolicy::IsSentAfterRestart(&sweet, &friendPlayer));
    CHECK(!PedHandoverPolicy::IsSentAfterRestart(&traffic, &friendPlayer));
    CHECK(PedHandoverPolicy::IsSentAfterRestart(&traffic, &host));  // the host loads a save: the friend's traffic returns
    CHECK(!PedHandoverPolicy::IsSentAfterRestart(&orphan, &host));
    // its own peds' PED_REMOVE hands a claimed one to the claimer (not to itself), as when a syncer leaves
    HandoverPed barber{&friendPlayer};
    host.m_vPedClaims.push_back(&barber);
    const std::vector<HandoverPlayer*> players{&host, &friendPlayer};
    CHECK(PedHandoverPolicy::FindClaimer(players, &friendPlayer, &barber) == &host);
    friendPlayer.m_vPedClaims.push_back(&traffic);
    CHECK(PedHandoverPolicy::FindClaimer(players, &friendPlayer, &traffic) == nullptr);

    // one record per id on the restarted game: the snapshot after the spawn's relay, a claim handover's copy
    CHECK(DecideSpawn(false, false) == eSpawn::CREATE);
    CHECK(DecideSpawn(true, true) == eSpawn::KEEP_EXISTING);
    CHECK(DecideSpawn(true, false) == eSpawn::REPLACE);
}

int main()
{
    PoolLifetime();
    LatestAndReadiness();
    ExpiryAndWrap();
    CancellationAndStreamingLoss();
    RecreatedPlayerPedReference();
    RecoveryThrottle();
    PedIdsAndRemovals();
    LostCopies();
    Restarts();
    std::puts("ped_sync_state: all checks passed");
}
