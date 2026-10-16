// Private build: policy test for the co-op save player state (client/src/CPlayerStateSave.h) without GTA: the state
// format and its checks, the sidecar file, the host's store (merge, carry-over, restores) and the other players' send
// rules. Build and run: see CLAUDE.md (Fork changes, player state for co-op saves).
#include "../client/src/CPlayerStateStore.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using eSnapshotResult = CPlayerStateStore::eSnapshotResult;

SPlayerState MakeState(int seed)
{
    SPlayerState state;
    state.weapons[0] = {1, 1, 0};        // brass knuckles, slot 0
    state.weapons[2] = {24, 70, 7};      // desert eagle, slot 2
    state.weapons[5] = {31, 99999, 50};  // M4, slot 5
    state.weapons[11] = {46, 1, 1};      // parachute, slot 11
    state.activeSlot = 5;
    state.health = 176.0f - static_cast<float>(seed);
    state.armour = 150.0f;
    state.money = -1234 - seed;
    state.fightingStyle = 6;
    state.allowedAttackMoves = 0x0F;
    state.maxHealth = 176;
    state.maxArmour = 150;
    state.neverGetsTired = true;
    state.fastReload = (seed & 1) != 0;
    state.fireProof = true;
    state.getOutOfJailFree = false;
    state.freeHealthCare = true;
    for (int i = 0; i < SPlayerState::NUM_FLOAT_STATS; i++)
    {
        state.floatStats[i] = static_cast<float>(i * 37 + seed) + 0.25f;
    }
    for (int i = 0; i < SPlayerState::NUM_INT_STATS; i++)
    {
        state.intStats[i] = i * 200 + seed;
    }
    for (int i = 0; i < SPlayerState::NUM_CLOTHES_MODELS; i++)
    {
        state.clothesModelKeys[i] = 0x9E3779B9u * static_cast<uint32_t>(i + 1 + seed);
    }
    for (int i = 0; i < SPlayerState::NUM_CLOTHES_TEXTURES; i++)
    {
        state.clothesTextureKeys[i] = 0x85EBCA6Bu * static_cast<uint32_t>(i + 3 + seed);
    }
    return state;
}

std::vector<uint8_t> WriteState(SPlayerState state, bool* ok = nullptr)
{
    std::vector<uint8_t> buffer(1024, 0);
    serialize::WriteStream stream(buffer.data(), static_cast<int>(buffer.size()));
    const bool written = state.Serialize(stream);
    stream.Flush();
    if (ok != nullptr)
    {
        *ok = written;
    }
    buffer.resize(written ? static_cast<size_t>(stream.GetBytesProcessed()) : 0);
    return buffer;
}

bool ReadState(std::vector<uint8_t> bytes, size_t usedBytes, SPlayerState& state)
{
    bytes.resize(((bytes.size() + 3) & ~size_t(3)) + 4, 0);
    serialize::ReadStream stream(bytes.data(), static_cast<int>(usedBytes));
    return state.Serialize(stream);
}

// The wire format written without the checks, to see the reader refuse what a writer must never produce.
template <typename Stream>
bool WriteUnchecked(Stream& stream, SPlayerState& s)
{
    for (SPlayerState::SWeapon& weapon : s.weapons)
    {
        int32_t type = weapon.type;
        serialize_int(stream, type, 0, SPlayerState::MAX_WEAPON_TYPE);
        if (type != 0)
        {
            int32_t total = static_cast<int32_t>(weapon.totalAmmo);
            int32_t clip = static_cast<int32_t>(weapon.ammoInClip);
            serialize_int(stream, total, 0, SPlayerState::MAX_AMMO);
            serialize_int(stream, clip, 0, SPlayerState::MAX_AMMO);
        }
    }
    int32_t slot = s.activeSlot;
    serialize_int(stream, slot, 0, SPlayerState::NUM_WEAPON_SLOTS - 1);
    serialize_float(stream, s.health);
    serialize_float(stream, s.armour);
    uint32_t money = static_cast<uint32_t>(s.money);
    serialize_bits(stream, money, 32);
    int32_t style = s.fightingStyle;
    serialize_int(stream, style, SPlayerState::FIGHTING_STYLE_FIRST, SPlayerState::FIGHTING_STYLE_LAST);
    uint32_t moves = s.allowedAttackMoves, maxHealth = s.maxHealth, maxArmour = s.maxArmour;
    serialize_bits(stream, moves, 8);
    serialize_bits(stream, maxHealth, 8);
    serialize_bits(stream, maxArmour, 8);
    serialize_bool(stream, s.neverGetsTired);
    serialize_bool(stream, s.fastReload);
    serialize_bool(stream, s.fireProof);
    serialize_bool(stream, s.getOutOfJailFree);
    serialize_bool(stream, s.freeHealthCare);
    for (float& value : s.floatStats)
    {
        serialize_float(stream, value);
    }
    for (int32_t& value : s.intStats)
    {
        serialize_int(stream, value, 0, 1000);
    }
    for (uint32_t& key : s.clothesModelKeys)
    {
        serialize_bits(stream, key, 32);
    }
    for (uint32_t& key : s.clothesTextureKeys)
    {
        serialize_bits(stream, key, 32);
    }
    return true;
}

bool ReadsBack(SPlayerState unchecked)
{
    std::vector<uint8_t> buffer(1024, 0);
    serialize::WriteStream stream(buffer.data(), static_cast<int>(buffer.size()));
    CHECK(WriteUnchecked(stream, unchecked));
    stream.Flush();
    SPlayerState read;
    return ReadState(buffer, static_cast<size_t>(stream.GetBytesProcessed()), read);
}

void StateRoundTripAndChecks()
{
    const SPlayerState state = MakeState(3);
    CHECK(state.IsValid());
    CHECK(SPlayerState{}.IsValid());

    bool ok = false;
    const std::vector<uint8_t> bytes = WriteState(state, &ok);
    CHECK(ok);
    CHECK(bytes.size() < 400);  // a few hundred bytes per player
    SPlayerState read;
    CHECK(ReadState(bytes, bytes.size(), read));
    CHECK(read == state);
    CHECK(!(read != state));

    // a truncated packet or file entry is refused
    for (size_t cut : {size_t(0), size_t(1), bytes.size() / 2, bytes.size() - 1})
    {
        SPlayerState partial;
        CHECK(!ReadState(bytes, cut, partial));
    }

    // the unchecked writer produces the same bits for a valid state
    CHECK(ReadsBack(state));

    // every check
    auto invalid = [&](void (*change)(SPlayerState&)) {
        SPlayerState changed = state;
        change(changed);
        bool written = true;
        WriteState(changed, &written);
        return !changed.IsValid() && !written;
    };
    CHECK(invalid([](SPlayerState& s) { s.weapons[3] = {19, 1, 1}; }));  // a projectile type
    CHECK(invalid([](SPlayerState& s) { s.weapons[3] = {21, 1, 1}; }));
    CHECK(invalid([](SPlayerState& s) { s.weapons[3] = {47, 1, 1}; }));
    CHECK(invalid([](SPlayerState& s) { s.weapons[5].totalAmmo = 100000; }));
    CHECK(invalid([](SPlayerState& s) { s.weapons[5].ammoInClip = 99999; s.weapons[5].totalAmmo = 30; }));
    CHECK(invalid([](SPlayerState& s) { s.weapons[1] = {0, 5, 0}; }));  // ammo in an empty slot
    CHECK(invalid([](SPlayerState& s) { s.activeSlot = 13; }));
    CHECK(invalid([](SPlayerState& s) { s.health = 0.0f; }));
    CHECK(invalid([](SPlayerState& s) { s.health = -5.0f; }));
    CHECK(invalid([](SPlayerState& s) { s.health = 1000.5f; }));
    CHECK(invalid([](SPlayerState& s) { s.health = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(invalid([](SPlayerState& s) { s.health = std::numeric_limits<float>::infinity(); }));
    CHECK(invalid([](SPlayerState& s) { s.armour = -1.0f; }));
    CHECK(invalid([](SPlayerState& s) { s.armour = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(invalid([](SPlayerState& s) { s.money = 1000000000; }));
    CHECK(invalid([](SPlayerState& s) { s.money = -1000000000; }));
    CHECK(invalid([](SPlayerState& s) { s.fightingStyle = 3; }));
    CHECK(invalid([](SPlayerState& s) { s.fightingStyle = 17; }));
    CHECK(invalid([](SPlayerState& s) { s.maxHealth = 0; }));
    CHECK(invalid([](SPlayerState& s) { s.floatStats[4] = -0.5f; }));
    CHECK(invalid([](SPlayerState& s) { s.floatStats[0] = 1000.01f; }));
    CHECK(invalid([](SPlayerState& s) { s.floatStats[17] = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(invalid([](SPlayerState& s) { s.intStats[0] = -1; }));
    CHECK(invalid([](SPlayerState& s) { s.intStats[4] = 1001; }));

    // edges that are fine
    SPlayerState edges = state;
    edges.money = SPlayerState::MAX_MONEY;
    edges.health = 1000.0f;
    edges.armour = 0.0f;
    edges.weapons[5] = {31, 0, 0};  // a gun without ammo stays in its slot
    edges.floatStats[0] = 1000.0f;
    edges.intStats[0] = 1000;
    CHECK(edges.IsValid());
    const std::vector<uint8_t> edgeBytes = WriteState(edges, &ok);
    CHECK(ok && ReadState(edgeBytes, edgeBytes.size(), read) && read == edges);
    edges.money = -SPlayerState::MAX_MONEY;
    CHECK(edges.IsValid());

    // the reader refuses what passes the bit ranges but not the checks
    auto refused = [&](void (*change)(SPlayerState&)) {
        SPlayerState changed = state;
        change(changed);
        return !ReadsBack(changed);
    };
    CHECK(refused([](SPlayerState& s) { s.weapons[3] = {20, 1, 1}; }));
    CHECK(refused([](SPlayerState& s) { s.weapons[5].ammoInClip = 40; s.weapons[5].totalAmmo = 30; }));
    CHECK(refused([](SPlayerState& s) { s.health = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(refused([](SPlayerState& s) { s.health = 0.0f; }));
    CHECK(refused([](SPlayerState& s) { s.money = 2000000000; }));
    CHECK(refused([](SPlayerState& s) { s.maxHealth = 0; }));
    CHECK(refused([](SPlayerState& s) { s.floatStats[2] = -std::numeric_limits<float>::infinity(); }));

    // stat ids: float ones below 0x52, int ones from 120 on, never the city unlock stat (181)
    for (uint16_t id : SPlayerState::FLOAT_STAT_IDS)
    {
        CHECK(id < 0x52);
    }
    for (uint16_t id : SPlayerState::INT_STAT_IDS)
    {
        CHECK(id >= 120 && id != 181);
    }
}

void Names()
{
    char out[Config::MAX_NICKNAME_LENGTH + 1];
    SanitizePlayerName("Samet[TR]_1", out);
    CHECK(std::string(out) == "Samet[TR]_1");
    SanitizePlayerName("a b%c\xC5\x9F", out);  // as the server and the host see it
    CHECK(std::string(out) == "a_b_c__");
    SanitizePlayerName("0123456789012345678901234567", out);
    CHECK(std::string(out) == "012345678901234567890123");
    SanitizePlayerName(nullptr, out);
    CHECK(out[0] == '\0');

    CHECK(IsValidPlayerName("Friend"));
    CHECK(IsValidPlayerName("012345678901234567890123"));
    CHECK(!IsValidPlayerName("0123456789012345678901234"));
    CHECK(!IsValidPlayerName(""));
    CHECK(!IsValidPlayerName(nullptr));
    CHECK(!IsValidPlayerName("a b"));
    CHECK(!IsValidPlayerName("%s"));
}

void Fingerprints()
{
    CHECK(Fnv1a64("", 0) == 0xCBF29CE484222325ull);
    CHECK(Fnv1a64("a", 1) == 0xAF63DC4C8601EC8Cull);
    CHECK(Fnv1a32("", 0) == 0x811C9DC5u);
    CHECK(Fnv1a32("a", 1) == 0xE40C292Cu);
    // chunked hashing (the save file is read in pieces) gives the same value
    CHECK(Fnv1a64("bc", 2, Fnv1a64("a", 1)) == Fnv1a64("abc", 3));
}

void FixChecksum(std::vector<uint8_t>& data)
{
    const size_t checked = data.size() - PlayerStateSidecar::CHECKSUM_SIZE;
    const uint32_t sum = Fnv1a32(data.data(), checked);
    for (int i = 0; i < 4; i++)
    {
        data[checked + i] = static_cast<uint8_t>(sum >> (8 * i));
    }
}

void Sidecar()
{
    using namespace PlayerStateSidecar;
    SFile file;
    file.saveSize = 202752;
    file.saveHash = 0x0123456789ABCDEFull;
    file.entries.push_back({"FriendOne", MakeState(1)});
    file.entries.push_back({"FriendTwo", MakeState(2)});

    std::vector<uint8_t> data;
    CHECK(Write(file, data));
    CHECK(data.size() < 1024);
    CHECK(memcmp(data.data(), "CAPS", 4) == 0);

    SFile read;
    CHECK(Read(data, read) == eReadResult::Ok);
    CHECK(read.saveSize == file.saveSize && read.saveHash == file.saveHash);
    CHECK(read.entries.size() == 2);
    CHECK(read.entries[0].name == "FriendOne" && read.entries[0].state == MakeState(1));
    CHECK(read.entries[1].name == "FriendTwo" && read.entries[1].state == MakeState(2));

    // no entries is a valid file too (the game removes the file instead)
    std::vector<uint8_t> empty;
    CHECK(Write(SFile{}, empty));
    CHECK(Read(empty, read) == eReadResult::Ok && read.entries.empty());

    // what Write refuses
    SFile bad = file;
    bad.entries[1].name = "FriendOne";
    CHECK(!Write(bad, data) && data.empty());
    bad = file;
    bad.entries[1].name = "";
    CHECK(!Write(bad, data));
    bad.entries[1].name = "bad name";
    CHECK(!Write(bad, data));
    bad.entries[1].name = "0123456789012345678901234";
    CHECK(!Write(bad, data));
    bad = file;
    bad.entries[0].state.health = 0.0f;
    CHECK(!Write(bad, data));
    bad = SFile{};
    for (size_t i = 0; i <= MAX_ENTRIES; i++)
    {
        bad.entries.push_back({"P" + std::to_string(i), MakeState(static_cast<int>(i))});
    }
    CHECK(!Write(bad, data));
    bad.entries.pop_back();
    CHECK(Write(bad, data) && data.size() <= MAX_FILE_SIZE);
    CHECK(Read(data, read) == eReadResult::Ok && read.entries.size() == MAX_ENTRIES);

    // what Read refuses
    CHECK(Write(file, data));
    const std::vector<uint8_t> good = data;
    CHECK(Read(std::vector<uint8_t>(good.begin(), good.begin() + 20), read) == eReadResult::TooShortOrTooLong);
    CHECK(Read(std::vector<uint8_t>(MAX_FILE_SIZE + 1, 0), read) == eReadResult::TooShortOrTooLong);
    CHECK(read.entries.empty());

    data = good;
    data[0] = 'X';
    CHECK(Read(data, read) == eReadResult::BadMagic);

    data = good;
    data[4] = 2;  // a newer format
    CHECK(Read(data, read) == eReadResult::UnknownVersion);

    data = good;
    data[HEADER_SIZE + 10] ^= 0x40;
    CHECK(Read(data, read) == eReadResult::BadChecksum);

    data = good;
    data.pop_back();
    CHECK(Read(data, read) == eReadResult::BadChecksum);

    data = good;
    data[6] = 3;  // more entries than the body holds
    FixChecksum(data);
    CHECK(Read(data, read) == eReadResult::Corrupt);

    data = good;
    data[6] = 1;  // fewer: the rest of the body would be left over
    FixChecksum(data);
    CHECK(Read(data, read) == eReadResult::Corrupt);
    data[6] = 0;
    FixChecksum(data);
    CHECK(Read(data, read) == eReadResult::Corrupt);

    data = good;
    data[20] ^= 1;  // body size
    FixChecksum(data);
    CHECK(Read(data, read) == eReadResult::Corrupt);

    // the same nickname twice
    data = good;
    const std::string two = "FriendTwo";
    auto at = std::search(data.begin() + HEADER_SIZE, data.end(), two.begin(), two.end());
    CHECK(at != data.end());
    memcpy(&*at, "FriendOne", 9);
    FixChecksum(data);
    CHECK(Read(data, read) == eReadResult::Corrupt);

    // a nickname the server would never let through
    data = good;
    at = std::search(data.begin() + HEADER_SIZE, data.end(), two.begin(), two.end());
    *at = ' ';
    FixChecksum(data);
    CHECK(Read(data, read) == eReadResult::Corrupt);

    // every single bit flipped, with the checksum fixed so the body checks get it: the reader never accepts an
    // invalid state, and a flip either leaves a valid file (a clothes key, a stat bit) or is refused
    size_t refusedFlips = 0;
    for (size_t i = 0; i + CHECKSUM_SIZE < good.size(); i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            std::vector<uint8_t> flipped = good;
            flipped[i] ^= static_cast<uint8_t>(1 << bit);
            CHECK(Read(flipped, read) == (i < 4 ? eReadResult::BadMagic : i < 6 ? eReadResult::UnknownVersion
                                                                                  : eReadResult::BadChecksum));
            FixChecksum(flipped);
            const eReadResult result = Read(flipped, read);
            if (result == eReadResult::Ok)
            {
                for (const SEntry& entry : read.entries)
                {
                    CHECK(entry.state.IsValid() && IsValidPlayerName(entry.name.c_str()));
                }
            }
            else
            {
                refusedFlips++;
            }
        }
    }
    CHECK(refusedFlips > 0);

    // garbage with a valid header and checksum: refused or valid, never anything else
    uint32_t random = 12345;
    for (int round = 0; round < 3000; round++)
    {
        std::vector<uint8_t> garbage(good.begin(), good.begin() + HEADER_SIZE);
        const size_t bodySize = static_cast<size_t>(round % 700);
        for (size_t i = 0; i < bodySize; i++)
        {
            random = random * 1664525u + 1013904223u;
            garbage.push_back(static_cast<uint8_t>(random >> 24));
        }
        garbage[6] = static_cast<uint8_t>(round % 4);  // entry count
        garbage[7] = 0;
        for (int i = 0; i < 4; i++)
        {
            garbage[20 + i] = static_cast<uint8_t>(bodySize >> (8 * i));
        }
        garbage.resize(garbage.size() + CHECKSUM_SIZE, 0);
        FixChecksum(garbage);
        const eReadResult result = Read(garbage, read);
        CHECK(result == eReadResult::Ok || result == eReadResult::Corrupt);
        if (result == eReadResult::Ok)
        {
            for (const SEntry& entry : read.entries)
            {
                CHECK(entry.state.IsValid() && IsValidPlayerName(entry.name.c_str()));
            }
        }
    }

    CHECK(std::string(ToString(eReadResult::UnknownVersion)) == "unknown version");
}

void StoreSnapshotsAndReconnects()
{
    CPlayerStateStore store(100);
    CPlayerStateStore::SRestore restore;
    const SPlayerState first = MakeState(1);
    const SPlayerState second = MakeState(2);
    const SPlayerState fresh{};  // a new game: no weapons, no money

    // a player the host doesn't know yet: their state is taken as it is
    CHECK(store.OnSnapshot("Friend", 11, 0, first, restore) == eSnapshotResult::Accepted);
    CHECK(store.Find("Friend") != nullptr && *store.Find("Friend") == first);
    CHECK(store.OnSnapshot("Friend", 11, 0, second, restore) == eSnapshotResult::Accepted);
    CHECK(*store.Find("Friend") == second);

    // the friend's game crashed and they connect again with a new game: the stored state goes back, the empty one is
    // dropped until the friend has applied it
    CHECK(store.OnSnapshot("Friend", 22, 0, fresh, restore) == eSnapshotResult::SendRestore);
    CHECK(restore.name == "Friend" && restore.state == second && restore.restoreId == 100);
    CHECK(*store.Find("Friend") == second);
    CHECK(store.OnSnapshot("Friend", 22, 0, fresh, restore) == eSnapshotResult::Dropped);
    CHECK(store.OnSnapshot("Friend", 22, 99, fresh, restore) == eSnapshotResult::Dropped);
    CHECK(*store.Find("Friend") == second);
    CHECK(store.OnSnapshot("Friend", 22, 100, second, restore) == eSnapshotResult::Accepted);
    CHECK(store.OnSnapshot("Friend", 22, 100, first, restore) == eSnapshotResult::Accepted);
    CHECK(*store.Find("Friend") == first);

    // other players are separate
    CHECK(store.OnSnapshot("Other", 5, 0, fresh, restore) == eSnapshotResult::Accepted);
    CHECK(*store.Find("Friend") == first && *store.Find("Other") == fresh);
    CHECK(store.Find("Nobody") == nullptr);

    // restore ids are never 0, also across the wrap
    CPlayerStateStore wrap(0xFFFFFFFFu);
    CHECK(wrap.OnSnapshot("A", 1, 0, first, restore) == eSnapshotResult::Accepted);
    CHECK(wrap.OnSnapshot("A", 2, 0, first, restore) == eSnapshotResult::SendRestore && restore.restoreId == 0xFFFFFFFFu);
    CHECK(wrap.OnSnapshot("A", 3, 0, first, restore) == eSnapshotResult::SendRestore && restore.restoreId == 1);
    CPlayerStateStore zero(0);
    CHECK(zero.OnSnapshot("A", 1, 0, first, restore) == eSnapshotResult::Accepted);
    CHECK(zero.OnSnapshot("A", 2, 0, first, restore) == eSnapshotResult::SendRestore && restore.restoreId == 1);
}

void StoreLoadsAndSaves()
{
    using PlayerStateSidecar::SEntry;
    const SPlayerState old1 = MakeState(10), old2 = MakeState(20), live1 = MakeState(11), live3 = MakeState(31);
    CPlayerStateStore::SRestore restore;

    // the host loads its save at the start of the session, before anyone is connected
    CPlayerStateStore store(1);
    store.OnSaveLoaded({SEntry{"Friend", old1}, SEntry{"Away", old2}});
    auto restores = store.TakeLoadRestores([](const std::string&) { return false; });
    CHECK(restores.empty());
    CHECK(store.TakeLoadRestores([](const std::string&) { return true; }).empty());  // taken once

    // the friend connects with a new game and gets the saved state back
    CHECK(store.OnSnapshot("Friend", 7, 0, SPlayerState{}, restore) == eSnapshotResult::SendRestore);
    CHECK(restore.state == old1);
    CHECK(store.OnSnapshot("Friend", 7, restore.restoreId, old1, restore) == eSnapshotResult::Accepted);
    // plays on
    CHECK(store.OnSnapshot("Friend", 7, restore.restoreId, live1, restore) == eSnapshotResult::Accepted);

    // a third player joins this session for the first time
    CHECK(store.OnSnapshot("New", 9, 0, live3, restore) == eSnapshotResult::Accepted);

    // the host saves: this session's latest, plus the loaded entry of the player not seen since
    std::vector<SEntry> entries = store.GetEntries();
    CHECK(entries.size() == 3);
    for (const SEntry& entry : entries)
    {
        CHECK((entry.name == "Friend" && entry.state == live1) || (entry.name == "Away" && entry.state == old2) ||
              (entry.name == "New" && entry.state == live3));
    }

    // the friend crashes and reconnects: this session's latest beats the loaded save
    CHECK(store.OnSnapshot("Friend", 8, 0, SPlayerState{}, restore) == eSnapshotResult::SendRestore);
    CHECK(restore.state == live1);
    const uint32_t reconnectRestore = restore.restoreId;
    CHECK(store.OnSnapshot("Friend", 8, reconnectRestore, live1, restore) == eSnapshotResult::Accepted);

    // the host loads the older save while the friend is connected: the friend rolls back. Snapshots from before the
    // load (still on their way) don't overwrite the loaded state.
    store.OnSaveLoaded({SEntry{"Friend", old1}});
    CHECK(store.OnSnapshot("Friend", 8, reconnectRestore, live1, restore) == eSnapshotResult::Dropped);
    CHECK(*store.Find("Friend") == old1);
    restores = store.TakeLoadRestores([](const std::string& name) { return name == "Friend"; });
    CHECK(restores.size() == 1 && restores[0].name == "Friend" && restores[0].state == old1);
    CHECK(store.OnSnapshot("Friend", 8, reconnectRestore, live1, restore) == eSnapshotResult::Dropped);
    CHECK(store.OnSnapshot("Friend", 8, restores[0].restoreId, old1, restore) == eSnapshotResult::Accepted);
    // players the older save doesn't know keep their state
    CHECK(*store.Find("New") == live3 && *store.Find("Away") == old2);

    // a load restore for someone who isn't connected waits for their next connection
    store.OnSaveLoaded({SEntry{"New", old2}});
    CHECK(store.TakeLoadRestores([](const std::string&) { return false; }).empty());
    CHECK(store.OnSnapshot("New", 9, 0, live3, restore) == eSnapshotResult::Dropped);  // a late packet of the old connection
    CHECK(store.OnSnapshot("New", 10, 0, live3, restore) == eSnapshotResult::SendRestore && restore.state == old2);

    // carried over through a sidecar file into the next session
    PlayerStateSidecar::SFile file;
    file.saveSize = 1;
    file.saveHash = 2;
    file.entries = store.GetEntries();
    std::vector<uint8_t> data;
    CHECK(PlayerStateSidecar::Write(file, data));
    PlayerStateSidecar::SFile read;
    CHECK(PlayerStateSidecar::Read(data, read) == PlayerStateSidecar::eReadResult::Ok);
    CPlayerStateStore next(500);
    next.OnSaveLoaded(read.entries);
    CHECK(*next.Find("Friend") == old1 && *next.Find("Away") == old2 && *next.Find("New") == old2);
    CHECK(next.OnSnapshot("Away", 1, 0, SPlayerState{}, restore) == eSnapshotResult::SendRestore && restore.state == old2);
}

void OtherPlayerSide()
{
    CPlayerStateClient client;
    client.Reset(0);
    CHECK(client.GetConnectionNonce() == 1);  // never 0
    client.Reset(42);
    CHECK(client.GetConnectionNonce() == 42 && client.GetAckRestoreId() == 0);

    const SPlayerState a = MakeState(1), b = MakeState(2);
    // the first snapshot of a connection goes out at once
    CHECK(client.IsCaptureDue(1000));
    CHECK(client.OnCaptured(1000, a));
    // then at most every 2 s, and only when it changed
    CHECK(!client.IsCaptureDue(1500));
    CHECK(client.IsCaptureDue(3000));
    CHECK(!client.OnCaptured(3000, a));
    CHECK(!client.IsCaptureDue(4999));
    CHECK(client.IsCaptureDue(5000));
    CHECK(client.OnCaptured(5000, b));
    // tick count wrap
    CHECK(!client.OnCaptured(0xFFFFFF00u, b));
    CHECK(!client.IsCaptureDue(0x00000100u));
    CHECK(client.IsCaptureDue(0xFFFFFF00u + 2000u));

    // a new host gets the current state again
    client.ForceSend();
    CHECK(client.IsCaptureDue(0xFFFFFF01u) && client.OnCaptured(0xFFFFFF01u, b));

    // a restore: nothing is sent until it is applied, the newest one wins, then the state goes out with the ack
    client.OnRestoreReceived(5, a);
    client.OnRestoreReceived(7, b);
    CHECK(client.HasPendingRestore() && client.GetPendingRestore() == b);
    CHECK(!client.IsCaptureDue(0x10000000u));
    CHECK(!client.OnCaptured(0x10000000u, a));
    client.OnRestoreDone();
    CHECK(!client.HasPendingRestore() && client.GetAckRestoreId() == 7);
    CHECK(client.IsCaptureDue(0x10000001u) && client.OnCaptured(0x10000001u, b));  // unchanged, sent for the ack
    client.OnRestoreDone();  // nothing pending: no change
    CHECK(client.GetAckRestoreId() == 7 && !client.IsCaptureDue(0x10000002u));

    // a restart of the friend's game keeps a restore that wasn't applied yet
    client.OnRestoreReceived(9, a);
    client.Reset(43);
    CHECK(client.HasPendingRestore() && client.GetConnectionNonce() == 43);
}

// The host and one friend through a whole evening, with the packets in between.
void Session()
{
    CPlayerStateStore host(1000);
    CPlayerStateClient friendGame;
    CPlayerStateStore::SRestore restore;
    SPlayerState friendState = MakeState(5);  // what the friend has in their game

    auto sendSnapshot = [&](uint32_t now) {
        if (!friendGame.IsCaptureDue(now) || !friendGame.OnCaptured(now, friendState))
        {
            return eSnapshotResult::Dropped;
        }
        const eSnapshotResult result = host.OnSnapshot(
            "Friend", friendGame.GetConnectionNonce(), friendGame.GetAckRestoreId(), friendState, restore);
        if (result == eSnapshotResult::SendRestore)
        {
            friendGame.OnRestoreReceived(restore.restoreId, restore.state);
        }
        return result;
    };
    auto applyRestore = [&] {
        CHECK(friendGame.HasPendingRestore());
        friendState = friendGame.GetPendingRestore();
        friendGame.OnRestoreDone();
    };

    friendGame.Reset(1);
    CHECK(sendSnapshot(0) == eSnapshotResult::Accepted);
    friendState.money += 5000;
    CHECK(sendSnapshot(2000) == eSnapshotResult::Accepted);
    const SPlayerState saved = friendState;

    // the friend's game crashes; New Game; connects again
    friendState = SPlayerState{};
    friendGame = CPlayerStateClient{};
    friendGame.Reset(2);
    CHECK(sendSnapshot(10000) == eSnapshotResult::SendRestore);
    CHECK(sendSnapshot(12000) == eSnapshotResult::Dropped);  // waits for the restore
    applyRestore();
    CHECK(friendState == saved);
    CHECK(sendSnapshot(12001) == eSnapshotResult::Accepted);
    CHECK(*host.Find("Friend") == saved);
}

int main()
{
    StateRoundTripAndChecks();
    Names();
    Fingerprints();
    Sidecar();
    StoreSnapshotsAndReconnects();
    StoreLoadsAndSaves();
    OtherPlayerSide();
    Session();
    std::puts("player_state_test: all checks passed");
    return 0;
}
