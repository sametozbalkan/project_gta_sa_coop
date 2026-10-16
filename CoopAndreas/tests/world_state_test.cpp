// Private build: policy test for the world-state registry (client/src/CWorldStateRegistry.h) without GTA or ENet: blobs
// and their layout header, the host's FULL/RECORDS choice and the RECORDS patching on the other games, the sender
// check, layout mismatches, the apply gate and the lifecycle marks (join, host change, disconnect, restart), plus the
// city unlock's behaviour from CCityUnlockSync played through the policy. Build and run: see CLAUDE.md (Fork changes,
// world-state registry).
#include "../client/src/CWorldStatePolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace WorldStatePolicy;

namespace
{
constexpr uint16_t RECORD_SIZE = 4;
const uint32_t RECORDS_LAYOUT = Fnv1a32Text("Test:records:a,b,c,d,e,f,g,h,i,j");
const uint32_t VALUE_LAYOUT = Fnv1a32Text("Test:value:u8");

// an entry with `count` records of RECORD_SIZE bytes, record i filled with value[i]
Blob MakeRecords(const std::vector<uint8_t>& values, uint32_t hash = RECORDS_LAYOUT)
{
    Blob blob;
    BeginBlob(blob, {static_cast<uint16_t>(values.size()), hash});
    for (uint8_t value : values)
    {
        for (uint16_t i = 0; i < RECORD_SIZE; i++)
        {
            blob.push_back(value);
        }
    }
    return blob;
}

// an entry with one value (like the city unlock)
Blob MakeValue(uint8_t value, uint32_t hash = VALUE_LAYOUT)
{
    Blob blob;
    BeginBlob(blob, {0, hash});
    blob.push_back(value);
    return blob;
}

SGateInput OpenGate()
{
    SGateInput input;
    input.bAuthenticated = true;
    input.bIsHost = false;
    input.bHasHost = true;
    input.bRanSinceRestart = true;
    input.bLocalGameReady = true;
    input.bLocalMissionRunning = false;
    return input;
}
}  // namespace

void Hashes()
{
    // FNV-1a 32 reference values
    CHECK(Fnv1a32Text("") == 0x811C9DC5u);
    CHECK(Fnv1a32Text("a") == 0xE40C292Cu);
    CHECK(Fnv1a32Text("foobar") == 0xBF9CF968u);
    CHECK(Fnv1a32("foobar", 6) == 0xBF9CF968u);
    // chained: a layout hash over names in index order
    CHECK(Fnv1a32Text("bar", Fnv1a32Text("foo")) == Fnv1a32Text("foobar"));
    // the entries' layout hashes are compile-time constants
    static_assert(Fnv1a32Text("a") == 0xE40C292Cu, "constexpr FNV-1a");
}

void Layout()
{
    Blob blob;
    BeginBlob(blob, {0x0102, 0xA1B2C3D4u});
    CHECK(blob.size() == HEADER_SIZE);
    // little-endian on the wire
    const uint8_t expected[] = {0x02, 0x01, 0xD4, 0xC3, 0xB2, 0xA1};
    CHECK(std::memcmp(blob.data(), expected, sizeof(expected)) == 0);

    SLayout layout;
    CHECK(ReadLayout(blob, layout));
    CHECK(layout.recordCount == 0x0102 && layout.hash == 0xA1B2C3D4u);
    CHECK(!ReadLayout(blob.data(), HEADER_SIZE - 1, layout));
    CHECK(!ReadLayout(nullptr, 100, layout));

    Blob numbers;
    PutU16(numbers, 0xBEEF);
    PutU32(numbers, 0x01234567u);
    CHECK(GetU16(numbers.data()) == 0xBEEF);
    CHECK(GetU32(numbers.data() + 2) == 0x01234567u);

    // FULL blobs as the entry defines them
    CHECK(IsValidFull(MakeRecords({1, 2, 3}), RECORD_SIZE));
    CHECK(IsValidFull(MakeRecords({}), RECORD_SIZE));
    Blob shortBlob = MakeRecords({1, 2, 3});
    shortBlob.pop_back();
    CHECK(!IsValidFull(shortBlob, RECORD_SIZE));
    Blob longBlob = MakeRecords({1, 2, 3});
    longBlob.push_back(0);
    CHECK(!IsValidFull(longBlob, RECORD_SIZE));
    CHECK(IsValidFull(MakeValue(4), 0));
    CHECK(!IsValidFull(MakeRecords({1}), 0));  // an entry without records says 0 records
    CHECK(!IsValidFull(blob.data(), 3, 0));

    // one packet: the header at least, MAX_DATA_SIZE at most (bigger FULLs would need parts)
    CHECK(!FitsInOnePacket(HEADER_SIZE - 1));
    CHECK(FitsInOnePacket(HEADER_SIZE));
    CHECK(FitsInOnePacket(8000));
    CHECK(!FitsInOnePacket(8001));
    CHECK(MAX_DATA_SIZE == 8000);
}

void HostChoosesWhatToSend()
{
    Blob records;
    const Blob ten = MakeRecords({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

    // nothing sent yet, or a FULL is due: FULL
    CHECK(ChooseSend(nullptr, ten, RECORD_SIZE, false, records) == eSend::FULL);
    CHECK(ChooseSend(&ten, ten, RECORD_SIZE, true, records) == eSend::FULL);
    // unchanged
    CHECK(ChooseSend(&ten, ten, RECORD_SIZE, false, records) == eSend::NONE);
    CHECK(records.empty());

    // two of ten changed: RECORDS {header, index, record} x 2
    const Blob twoChanged = MakeRecords({0, 1, 42, 3, 4, 5, 6, 7, 8, 43});
    CHECK(ChooseSend(&ten, twoChanged, RECORD_SIZE, false, records) == eSend::RECORDS);
    CHECK(records.size() == HEADER_SIZE + 2 * (2 + RECORD_SIZE));
    CHECK(std::memcmp(records.data(), twoChanged.data(), HEADER_SIZE) == 0);
    CHECK(GetU16(records.data() + HEADER_SIZE) == 2);
    CHECK(records[HEADER_SIZE + 2] == 42);
    CHECK(GetU16(records.data() + HEADER_SIZE + 2 + RECORD_SIZE) == 9);
    CHECK(records[HEADER_SIZE + 2 + RECORD_SIZE + 2] == 43);

    // half or more changed: FULL
    const Blob fiveChanged = MakeRecords({9, 9, 9, 9, 9, 5, 6, 7, 8, 9});
    CHECK(ChooseSend(&ten, fiveChanged, RECORD_SIZE, false, records) == eSend::FULL);
    CHECK(records.empty());
    const Blob fourChanged = MakeRecords({9, 9, 9, 3, 4, 5, 6, 7, 8, 8});
    CHECK(ChooseSend(&ten, fourChanged, RECORD_SIZE, false, records) == eSend::RECORDS);

    // another layout (count or hash): FULL
    CHECK(ChooseSend(&ten, MakeRecords({0, 1, 2, 3, 4, 5, 6, 7, 8}), RECORD_SIZE, false, records) == eSend::FULL);
    CHECK(ChooseSend(&ten, MakeRecords({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, 1234), RECORD_SIZE, false, records) ==
          eSend::FULL);

    // an entry without records: any change is a FULL
    const Blob zero = MakeValue(0);
    CHECK(ChooseSend(&zero, MakeValue(1), 0, false, records) == eSend::FULL);
    CHECK(ChooseSend(&zero, MakeValue(0), 0, false, records) == eSend::NONE);

    // RECORDS that wouldn't be smaller than the blob: FULL (1-byte records cost 3 bytes each as RECORDS)
    Blob small;
    BeginBlob(small, {6, RECORDS_LAYOUT});
    small.insert(small.end(), {1, 2, 3, 4, 5, 6});
    Blob smallChanged = small;
    smallChanged[HEADER_SIZE + 0] = 10;
    smallChanged[HEADER_SIZE + 3] = 13;
    CHECK(ChooseSend(&small, smallChanged, 1, false, records) == eSend::FULL);
    Blob smallOne = small;
    smallOne[HEADER_SIZE + 5] = 16;
    CHECK(ChooseSend(&small, smallOne, 1, false, records) == eSend::RECORDS);
    CHECK(records.size() == HEADER_SIZE + 3);
}

void HostEntryState()
{
    CWorldStateEntryState host;
    Blob records;
    const Blob a = MakeRecords({1, 2, 3, 4, 5, 6});
    const Blob b = MakeRecords({1, 2, 3, 4, 5, 7});

    CHECK(!host.HasSent() && !host.IsFullDue());
    CHECK(host.Poll(a, RECORD_SIZE, records) == eSend::FULL);
    host.OnSent(a);
    CHECK(host.HasSent());
    CHECK(host.Poll(a, RECORD_SIZE, records) == eSend::NONE);
    CHECK(host.Poll(b, RECORD_SIZE, records) == eSend::RECORDS);
    host.OnSent(b);

    // a join forces a FULL of an unchanged blob, once
    host.MarkFull();
    CHECK(host.IsFullDue());
    CHECK(host.Poll(b, RECORD_SIZE, records) == eSend::FULL);
    host.OnSent(b);
    CHECK(!host.IsFullDue());
    CHECK(host.Poll(b, RECORD_SIZE, records) == eSend::NONE);

    // a FULL refused for its size isn't retried every frame
    host.MarkFull();
    host.OnSendRefused();
    CHECK(!host.IsFullDue());
    CHECK(host.HasSent());  // the last blob that did go out still counts

    host.ForgetSent();
    CHECK(!host.HasSent());
    CHECK(host.Poll(b, RECORD_SIZE, records) == eSend::FULL);
}

void OthersReceiveAndPatch()
{
    const int HOST = 3;
    CWorldStateEntryState other;
    const Blob base = MakeRecords({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    const Blob target = MakeRecords({0, 1, 2, 30, 4, 5, 6, 7, 8, 90});
    Blob records;

    // RECORDS without a FULL first: dropped
    CHECK(ChooseSend(&base, target, RECORD_SIZE, false, records) == eSend::RECORDS);
    CHECK(other.OnRecords(HOST, records.data(), records.size(), RECORD_SIZE) == eReceive::NO_BASE);
    CHECK(!other.HasDesired());

    // a malformed FULL: dropped
    CHECK(other.OnFull(HOST, base.data(), base.size() - 1, RECORD_SIZE) == eReceive::MALFORMED);
    CHECK(other.OnFull(HOST, base.data(), 3, RECORD_SIZE) == eReceive::MALFORMED);
    CHECK(!other.HasDesired());

    CHECK(other.OnFull(HOST, base.data(), base.size(), RECORD_SIZE) == eReceive::ACCEPTED);
    CHECK(other.HasDesired() && other.GetDesired() == base && other.GetDesiredSender() == HOST);

    // RECORDS from another player than the FULL's: no base
    CHECK(other.OnRecords(HOST + 1, records.data(), records.size(), RECORD_SIZE) == eReceive::NO_BASE);
    CHECK(other.GetDesired() == base);

    // the host's RECORDS turn the desired blob into the host's blob
    CHECK(other.OnRecords(HOST, records.data(), records.size(), RECORD_SIZE) == eReceive::ACCEPTED);
    CHECK(other.GetDesired() == target);

    // RECORDS for another layout than the FULL: stale
    Blob otherLayout = records;
    otherLayout[0] = 11;  // record count
    CHECK(other.OnRecords(HOST, otherLayout.data(), otherLayout.size(), RECORD_SIZE) == eReceive::STALE_LAYOUT);
    CHECK(other.GetDesired() == target);

    // an index out of range or a cut pair: nothing is patched
    Blob badIndex = records;
    badIndex[HEADER_SIZE + 2 + RECORD_SIZE] = 10;  // the second pair's index: 10 of 10 records
    badIndex[HEADER_SIZE + 2] = 77;                // the first pair is fine, but nothing may change
    CHECK(other.OnRecords(HOST, badIndex.data(), badIndex.size(), RECORD_SIZE) == eReceive::MALFORMED);
    CHECK(other.GetDesired() == target);
    CHECK(other.OnRecords(HOST, records.data(), records.size() - 1, RECORD_SIZE) == eReceive::MALFORMED);
    CHECK(other.OnRecords(HOST, records.data(), HEADER_SIZE, RECORD_SIZE) == eReceive::MALFORMED);
    CHECK(other.GetDesired() == target);
    // an entry without records takes no RECORDS
    CHECK(other.OnRecords(HOST, records.data(), records.size(), 0) == eReceive::MALFORMED);

    // a new FULL replaces the desired blob
    CHECK(other.OnFull(HOST, base.data(), base.size(), RECORD_SIZE) == eReceive::ACCEPTED);
    CHECK(other.GetDesired() == base);
}

void OthersCheckAndLayoutMismatch()
{
    const int HOST = 0;
    CWorldStateEntryState other;
    const Blob hostBlob = MakeRecords({5, 6, 7});

    // nothing desired: nothing to do
    CHECK(other.Check(MakeRecords({1, 2, 3})) == eCheck::NOTHING);

    CHECK(other.OnFull(HOST, hostBlob.data(), hostBlob.size(), RECORD_SIZE) == eReceive::ACCEPTED);
    CHECK(other.Check(hostBlob) == eCheck::NOTHING);
    CHECK(other.Check(MakeRecords({5, 6, 8})) == eCheck::APPLY);

    // this game's data has other records (another hash, or another count): the entry turns off instead of writing
    // into the wrong slots, once
    CHECK(other.Check(MakeRecords({5, 6, 7}, 999)) == eCheck::LAYOUT_MISMATCH);
    CHECK(other.IsDisabled() && !other.HasDesired());
    CHECK(other.GetRejectedLayout().hash == RECORDS_LAYOUT);
    CHECK(other.Check(MakeRecords({5, 6, 7}, 999)) == eCheck::DISABLED);
    CHECK(other.Check(hostBlob) == eCheck::DISABLED);
    // the host's next FULL with the same layout stays rejected, and its RECORDS too
    CHECK(other.OnFull(HOST, hostBlob.data(), hostBlob.size(), RECORD_SIZE) == eReceive::LAYOUT_REJECTED);
    Blob records;
    const Blob hostChanged = MakeRecords({5, 9, 7});
    CHECK(ChooseSend(&hostBlob, hostChanged, RECORD_SIZE, false, records) == eSend::RECORDS);
    CHECK(other.OnRecords(HOST, records.data(), records.size(), RECORD_SIZE) == eReceive::LAYOUT_REJECTED);
    CHECK(other.IsDisabled());

    // a FULL with another layout gets a new check
    const Blob grown = MakeRecords({5, 6, 7, 8});
    CHECK(other.OnFull(HOST, grown.data(), grown.size(), RECORD_SIZE) == eReceive::ACCEPTED);
    CHECK(!other.IsDisabled());
    CHECK(other.Check(MakeRecords({5, 6, 7, 1})) == eCheck::APPLY);
    CHECK(other.Check(MakeRecords({5, 6, 7})) == eCheck::LAYOUT_MISMATCH);  // count differs

    // a value entry with another format or size is a mismatch too
    CWorldStateEntryState value;
    const Blob one = MakeValue(1);
    CHECK(value.OnFull(HOST, one.data(), one.size(), 0) == eReceive::ACCEPTED);
    Blob wider = MakeValue(1);
    wider.push_back(0);
    CHECK(value.Check(wider) == eCheck::LAYOUT_MISMATCH);

    // forgetting (host change, disconnect) clears the rejection
    other.ForgetDesired();
    CHECK(!other.IsDisabled() && !other.HasDesired());
    CHECK(other.OnFull(HOST, hostBlob.data(), hostBlob.size(), RECORD_SIZE) == eReceive::ACCEPTED);
}

void Gate()
{
    SGateInput input = OpenGate();
    CHECK(DecideGate(input, true) == eGate::OPEN);
    CHECK(DecideGate(input, false) == eGate::OPEN);

    // a mission of this game (its new-game INTRO, a side mission): the entries that suspend wait, the others don't
    input.bLocalMissionRunning = true;
    CHECK(DecideGate(input, true) == eGate::OWN_MISSION);
    CHECK(DecideGate(input, false) == eGate::OPEN);

    input = OpenGate();
    input.bLocalGameReady = false;  // no player yet, dead or arrested
    CHECK(DecideGate(input, false) == eGate::NOT_READY);
    input = OpenGate();
    input.bRanSinceRestart = false;  // the frame of a new game or load
    CHECK(DecideGate(input, false) == eGate::NOT_READY);

    input = OpenGate();
    input.bHasHost = false;  // the host left and nobody took over (story host pinned)
    CHECK(DecideGate(input, false) == eGate::NO_HOST);

    input = OpenGate();
    input.bIsHost = true;  // the host captures and sends, it never applies
    CHECK(DecideGate(input, false) == eGate::IS_HOST);

    input = OpenGate();
    input.bAuthenticated = false;
    input.bHasHost = false;
    CHECK(DecideGate(input, false) == eGate::NO_SESSION);
}

void LifecycleJoinAndHostChange()
{
    CWorldStateTable table;
    table.Resize(3);
    const Blob v = MakeValue(2);

    // the host: everything went out once
    for (size_t i = 0; i < table.Size(); i++)
    {
        table.Entry(i).OnSent(v);
    }

    // a player joins: every entry goes out in full again
    table.OnPlayerJoined();
    for (size_t i = 0; i < table.Size(); i++)
    {
        CHECK(table.Entry(i).IsFullDue());
    }

    // this game stops being the host (another player is it now): nothing sent is remembered, no FULL is due
    table.OnHostChanged(false, 5);
    for (size_t i = 0; i < table.Size(); i++)
    {
        CHECK(!table.Entry(i).IsFullDue() && !table.Entry(i).HasSent());
    }

    // the new host's FULLs arrive; then the host changes to someone else: only what came from that one stays
    CHECK(table.Entry(0).OnFull(5, v.data(), v.size(), 0) == eReceive::ACCEPTED);
    CHECK(table.Entry(1).OnFull(6, v.data(), v.size(), 0) == eReceive::ACCEPTED);  // overtook its PLAYER_ASSIGN_HOST
    table.OnHostChanged(false, 6);
    CHECK(!table.Entry(0).HasDesired());
    CHECK(table.Entry(1).HasDesired() && table.Entry(1).GetDesiredSender() == 6);
    CHECK(!table.Entry(2).HasDesired());

    // the host leaves and nobody takes over: nothing is kept
    table.OnHostChanged(false, -1);
    CHECK(!table.Entry(1).HasDesired());

    // a former host's packet still in the buffer while nobody is the host is kept with its sender, and goes when
    // another player becomes the host
    CHECK(table.Entry(2).OnFull(6, v.data(), v.size(), 0) == eReceive::ACCEPTED);
    table.OnHostChanged(false, 7);
    CHECK(!table.Entry(2).HasDesired());

    // this game becomes the host: the old host's blobs go, every entry goes out in full
    CHECK(table.Entry(0).OnFull(7, v.data(), v.size(), 0) == eReceive::ACCEPTED);
    table.OnHostChanged(true, 1);
    for (size_t i = 0; i < table.Size(); i++)
    {
        CHECK(!table.Entry(i).HasDesired());
        CHECK(table.Entry(i).IsFullDue() && !table.Entry(i).HasSent());
    }

    // the session ends: all goes
    table.Entry(0).OnSent(v);
    table.OnDisconnected();
    for (size_t i = 0; i < table.Size(); i++)
    {
        CHECK(!table.Entry(i).IsFullDue() && !table.Entry(i).HasSent() && !table.Entry(i).HasDesired());
    }
}

void LifecycleRestart()
{
    CWorldStateTable table;
    table.Resize(2);

    // before any restart the game counts as running
    CHECK(table.RanSinceRestart(0));
    CHECK(table.RanSinceRestart(500));

    // the host loads a save at frame 2: FULLs once its game has been ready for 2 s without a break
    table.OnGameRestarted(2, true);
    CHECK(!table.RanSinceRestart(2));
    CHECK(table.RanSinceRestart(3));
    CHECK(table.Restart().IsFullPending());
    CHECK(!table.UpdateHostRestart(false, 1000));  // still loading or dead
    CHECK(!table.UpdateHostRestart(true, 1100));
    CHECK(!table.UpdateHostRestart(true, 2000));
    CHECK(!table.UpdateHostRestart(false, 2500));  // the wait starts over
    CHECK(!table.UpdateHostRestart(true, 3000));
    CHECK(!table.UpdateHostRestart(true, 4999));
    CHECK(!table.Entry(0).IsFullDue());
    CHECK(table.UpdateHostRestart(true, 5000));
    CHECK(table.Entry(0).IsFullDue() && table.Entry(1).IsFullDue());
    CHECK(!table.Restart().IsFullPending());
    CHECK(!table.UpdateHostRestart(true, 9000));  // once

    // GetTickCount wraps
    table.OnGameRestarted(1, true);
    CHECK(!table.UpdateHostRestart(true, 0xFFFFFF00u));
    CHECK(table.UpdateHostRestart(true, 0x00000800u));

    // the friend restarts: no FULL from its side, it keeps the host's blobs (its gate waits for a frame)
    CWorldStateTable other;
    other.Resize(1);
    const Blob v = MakeValue(1);
    CHECK(other.Entry(0).OnFull(0, v.data(), v.size(), 0) == eReceive::ACCEPTED);
    other.OnGameRestarted(1, false);
    CHECK(!other.Restart().IsFullPending());
    CHECK(!other.UpdateHostRestart(true, 100000));
    CHECK(other.Entry(0).HasDesired());
    CHECK(!other.RanSinceRestart(1) && other.RanSinceRestart(2));

    // a host that restarted and lost the host role before the wait was over: no FULL
    CWorldStateTable former;
    former.Resize(1);
    former.OnGameRestarted(1, true);
    former.OnHostChanged(false, 4);
    CHECK(!former.Restart().IsFullPending());
    CHECK(!former.UpdateHostRestart(true, 100000));
    // and one that disconnected
    former.OnGameRestarted(1, true);
    former.OnDisconnected();
    CHECK(!former.UpdateHostRestart(true, 100000));
}

// The city unlock (entry 0) as CCityUnlockSync behaved, played through the policy: the host sends its stat when it
// changes and in full when a player joins; the other game keeps the host's value, also against its own MAIN/INTRO, and
// forgets it when the session ends.
void CityUnlockAsBefore()
{
    const int HOST = 0;
    CWorldStateTable host;
    CWorldStateTable friendGame;
    host.Resize(1);
    friendGame.Resize(1);
    uint8_t hostStat = 1;    // the host's save: The Green Sabre passed
    uint8_t friendStat = 0;  // the friend's new game
    Blob records;

    auto hostPoll = [&]() -> bool {
        const Blob now = MakeValue(hostStat);
        const eSend send = host.Entry(0).Poll(now, 0, records);
        if (send == eSend::NONE)
        {
            return false;
        }
        CHECK(send == eSend::FULL);
        host.Entry(0).OnSent(now);
        CHECK(friendGame.Entry(0).OnFull(HOST, now.data(), now.size(), 0) == eReceive::ACCEPTED);
        return true;
    };
    auto friendReapply = [&](const SGateInput& gate) -> bool {
        if (DecideGate(gate, false) != eGate::OPEN)
        {
            return false;
        }
        if (friendGame.Entry(0).Check(MakeValue(friendStat)) != eCheck::APPLY)
        {
            return false;
        }
        friendStat = friendGame.Entry(0).GetDesired()[HEADER_SIZE];
        return true;
    };

    // the host becomes the host: everything out in full (relayed to nobody yet)
    host.OnHostChanged(true, HOST);
    CHECK(hostPoll());
    CHECK(!hostPoll());

    // the friend connects: PLAYER_JOINED on the host, a FULL of the unchanged value
    friendGame.OnHostChanged(false, HOST);
    host.OnPlayerJoined();
    CHECK(hostPoll());
    CHECK(friendReapply(OpenGate()));
    CHECK(friendStat == 1);
    CHECK(!friendReapply(OpenGate()));

    // the friend's own INTRO writes 0 again: put back at once, even while that mission runs
    friendStat = 0;
    SGateInput intro = OpenGate();
    intro.bLocalMissionRunning = true;
    CHECK(friendReapply(intro));
    CHECK(friendStat == 1);

    // the host passes Yay Ka-Boom-Boom: sent on change
    hostStat = 2;
    CHECK(hostPoll());
    CHECK(friendReapply(OpenGate()));
    CHECK(friendStat == 2);

    // the friend restarts its game (new game: the stat is 0): kept and put back once its game runs
    friendGame.OnGameRestarted(1, false);
    friendStat = 0;
    SGateInput restarted = OpenGate();
    restarted.bRanSinceRestart = friendGame.RanSinceRestart(1);
    CHECK(!friendReapply(restarted));
    restarted.bRanSinceRestart = friendGame.RanSinceRestart(2);
    CHECK(friendReapply(restarted));
    CHECK(friendStat == 2);

    // the session ends: the friend keeps its stat (no rollback) and forgets the host's value
    friendGame.OnDisconnected();
    host.OnDisconnected();
    CHECK(friendStat == 2);
    CHECK(!friendGame.Entry(0).HasDesired());
    friendStat = 0;
    CHECK(!friendReapply(OpenGate()));
    CHECK(friendStat == 0);
}

int main()
{
    Hashes();
    Layout();
    HostChoosesWhatToSend();
    HostEntryState();
    OthersReceiveAndPatch();
    OthersCheckAndLayoutMismatch();
    Gate();
    LifecycleJoinAndHostChange();
    LifecycleRestart();
    CityUnlockAsBefore();
    std::printf("world_state_test: all checks passed\n");
    return 0;
}
