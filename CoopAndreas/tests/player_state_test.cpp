// Private build: policy test for the co-op save player state (client/src/CPlayerStateSave.h) without GTA: the state
// format and its checks (with the respect block), the sidecar file (v2, and v1 files from before respect was kept), the
// host's store (merge, carry-over, restores) and the other players' send rules. Build and run: see CLAUDE.md (Fork
// changes, player state for co-op saves).
#include "../client/src/CPlayerStateStore.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
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
    state.hasRespect = true;
    state.respect = 1234.5f + static_cast<float>(seed);  // stat 68 isn't capped at 1000
    state.clothesRespect = 260.0f + static_cast<float>(seed);
    state.missionRespect = 1339 + seed;
    return state;
}

// what a CAPS v1 file holds of a state
SPlayerState WithoutRespect(SPlayerState state)
{
    state.hasRespect = false;
    state.respect = 0.0f;
    state.clothesRespect = 0.0f;
    state.missionRespect = 0;
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
    serialize_bool(stream, s.hasRespect);
    if (s.hasRespect)
    {
        serialize_float(stream, s.respect);
        serialize_float(stream, s.clothesRespect);
        serialize_int(stream, s.missionRespect, 0, SPlayerState::MAX_MISSION_RESPECT);
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

    // each respect field is part of the state (a change of it alone is sent and stored)
    SPlayerState differing = state;
    differing.respect += 0.75f;
    CHECK(differing != state);
    differing = state;
    differing.clothesRespect += 10.0f;
    CHECK(differing != state);
    differing = state;
    differing.missionRespect += 3;
    CHECK(differing != state);
    CHECK(WithoutRespect(state) != state);
    CHECK(WithoutRespect(SPlayerState{}) != SPlayerState{});  // "no respect yet" isn't "respect not known"

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
    CHECK(invalid([](SPlayerState& s) { s.respect = -0.5f; }));
    CHECK(invalid([](SPlayerState& s) { s.respect = SPlayerState::MAX_RESPECT + 1.0f; }));
    CHECK(invalid([](SPlayerState& s) { s.respect = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(invalid([](SPlayerState& s) { s.respect = std::numeric_limits<float>::infinity(); }));
    CHECK(invalid([](SPlayerState& s) { s.clothesRespect = -1.0f; }));
    CHECK(invalid([](SPlayerState& s) { s.clothesRespect = 1000.5f; }));  // a capped stat
    CHECK(invalid([](SPlayerState& s) { s.missionRespect = -1; }));
    CHECK(invalid([](SPlayerState& s) { s.missionRespect = SPlayerState::MAX_MISSION_RESPECT + 1; }));
    CHECK(invalid([](SPlayerState& s) { s.hasRespect = false; }));  // "not known" has no values
    CHECK(invalid([](SPlayerState& s) { s = WithoutRespect(s); s.missionRespect = 1; }));
    CHECK(invalid([](SPlayerState& s) { s = WithoutRespect(s); s.respect = std::numeric_limits<float>::quiet_NaN(); }));

    // edges that are fine
    SPlayerState edges = state;
    edges.money = SPlayerState::MAX_MONEY;
    edges.health = 1000.0f;
    edges.armour = 0.0f;
    edges.weapons[5] = {31, 0, 0};  // a gun without ammo stays in its slot
    edges.floatStats[0] = 1000.0f;
    edges.intStats[0] = 1000;
    edges.respect = SPlayerState::MAX_RESPECT;
    edges.clothesRespect = 1000.0f;
    edges.missionRespect = SPlayerState::MAX_MISSION_RESPECT;
    CHECK(edges.IsValid());
    const std::vector<uint8_t> edgeBytes = WriteState(edges, &ok);
    CHECK(ok && ReadState(edgeBytes, edgeBytes.size(), read) && read == edges);
    edges.money = -SPlayerState::MAX_MONEY;
    edges.respect = 0.0f;
    edges.clothesRespect = 0.0f;
    edges.missionRespect = 0;
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
    CHECK(refused([](SPlayerState& s) { s.respect = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(refused([](SPlayerState& s) { s.respect = SPlayerState::MAX_RESPECT + 0.5f; }));
    CHECK(refused([](SPlayerState& s) { s.respect = -std::numeric_limits<float>::infinity(); }));
    CHECK(refused([](SPlayerState& s) { s.clothesRespect = 1000.5f; }));
    CHECK(refused([](SPlayerState& s) { s.clothesRespect = -0.25f; }));

    // stat ids: float ones below 0x52, int ones from 120 on, never the city unlock stat (181)
    for (uint16_t id : SPlayerState::FLOAT_STAT_IDS)
    {
        CHECK(id < 0x52);
    }
    for (uint16_t id : SPlayerState::INT_STAT_IDS)
    {
        CHECK(id >= 120 && id != 181);
    }
    // respect: the components the player earns (respect 68, clothes respect 66, mission respect 224), each once; never
    // the total (64, the game computes it), the girlfriends' (65), the unused fitness respect (67) or the mission total
    // (228, MAIN's constant)
    CHECK(SPlayerState::STAT_ID_RESPECT == 68 && SPlayerState::STAT_ID_CLOTHES_RESPECT == 66);
    CHECK(SPlayerState::STAT_ID_MISSION_RESPECT == 224);
    for (uint16_t id : SPlayerState::FLOAT_STAT_IDS)
    {
        CHECK(id < 64 || id > 68);
    }
    for (uint16_t id : SPlayerState::INT_STAT_IDS)
    {
        CHECK(id != 224 && id != 228);
    }
}

// The layouts: the packets and v2 files carry the respect block, v1 files don't.
void Layouts()
{
    const SPlayerState state = MakeState(4);
    const SPlayerState unknown = WithoutRespect(state);
    CHECK(unknown.IsValid());

    // an older layout or one this build doesn't know is never written
    std::vector<uint8_t> buffer(1024, 0);
    serialize::WriteStream writer(buffer.data(), static_cast<int>(buffer.size()));
    SPlayerState copy = state;
    CHECK(!copy.SerializeLayout(writer, SPlayerState::LAYOUT_V1));
    CHECK(!copy.SerializeLayout(writer, 0));
    CHECK(!copy.SerializeLayout(writer, SPlayerState::LAYOUT_CURRENT + 1));
    serialize::MeasureStream measure;
    CHECK(!copy.SerializeLayout(measure, SPlayerState::LAYOUT_V1));
    CHECK(copy == state);

    // a state without respect goes over the wire as it is (a restore of a v1 entry)
    bool ok = false;
    const std::vector<uint8_t> withRespect = WriteState(state, &ok);
    CHECK(ok);
    const std::vector<uint8_t> without = WriteState(unknown, &ok);
    CHECK(ok && without.size() < withRespect.size());
    SPlayerState read;
    CHECK(ReadState(without, without.size(), read) && read == unknown && !read.hasRespect);
    CHECK(ReadState(withRespect, withRespect.size(), read) && read == state && read.hasRespect);

    // the v1 layout is the current one up to the respect block: read as v1, any state comes out without respect
    for (const std::vector<uint8_t>* bytes : {&withRespect, &without})
    {
        std::vector<uint8_t> padded = *bytes;
        padded.resize(((padded.size() + 3) & ~size_t(3)) + 4, 0);
        serialize::ReadStream reader(padded.data(), static_cast<int>(bytes->size()));
        SPlayerState v1;
        v1.respect = 5.0f;  // whatever was there before
        CHECK(v1.SerializeLayout(reader, SPlayerState::LAYOUT_V1) && v1 == unknown);
    }
    std::vector<uint8_t> current = withRespect;
    current.resize(((current.size() + 3) & ~size_t(3)) + 4, 0);
    serialize::ReadStream unknownLayout(current.data(), static_cast<int>(withRespect.size()));
    CHECK(!read.SerializeLayout(unknownLayout, SPlayerState::LAYOUT_CURRENT + 1));
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

// A CAPS v1 file as the builds before respect wrote it (0.3.103-alpha up to 4b67cc7; PlayerStateSidecar::Write of
// 4b67cc7): save size 202752, save hash 0x0123456789ABCDEF, "FriendOne" = MakeState(1) and "FriendTwo" = MakeState(2)
// without respect.
const uint8_t V1_FILE[] = {
    0x43, 0x41, 0x50, 0x53, 0x01, 0x00, 0x02, 0x00, 0x00, 0x18, 0x03, 0x00, 0xEF, 0xCD, 0xAB, 0x89,
    0x67, 0x45, 0x23, 0x01, 0xE8, 0x01, 0x00, 0x00, 0x09, 0x46, 0x72, 0x69, 0x65, 0x6E, 0x64, 0x4F,
    0x6E, 0x65, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x04, 0xE0, 0x00, 0x00, 0x00, 0x7C, 0x9F,
    0x86, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6E, 0x00, 0x80, 0x00, 0x00, 0x40, 0x01, 0x00, 0xBC,
    0x0C, 0x01, 0x00, 0x58, 0x0C, 0xB5, 0xEC, 0xFF, 0xFF, 0xCB, 0x03, 0xAC, 0xE5, 0x05, 0x00, 0x00,
    0xFD, 0x01, 0x00, 0xC8, 0x10, 0x02, 0x00, 0xB4, 0x14, 0x02, 0x00, 0x04, 0x17, 0x02, 0x00, 0xAA,
    0x18, 0x02, 0x00, 0xD2, 0x19, 0x02, 0x00, 0xFA, 0x1A, 0x02, 0x00, 0x11, 0x1C, 0x02, 0x00, 0xA5,
    0x1C, 0x02, 0x00, 0x39, 0x1D, 0x02, 0x00, 0xCD, 0x1D, 0x02, 0x00, 0x61, 0x1E, 0x02, 0x00, 0xF5,
    0x1E, 0x02, 0x00, 0x89, 0x1F, 0x02, 0x80, 0x0E, 0x20, 0x02, 0x80, 0x58, 0x20, 0x02, 0x80, 0xA2,
    0x20, 0x02, 0x80, 0xEC, 0x20, 0x0A, 0x20, 0x99, 0xC8, 0xB2, 0x0C, 0x59, 0x6E, 0xDE, 0x8D, 0x67,
    0xA5, 0xCD, 0x54, 0x9B, 0xDC, 0xBC, 0x1B, 0xAF, 0x13, 0xAC, 0xE2, 0xC2, 0x4A, 0x9B, 0xA9, 0xF6,
    0x81, 0x8A, 0x70, 0x0A, 0xB9, 0x79, 0x37, 0x3E, 0xF0, 0x68, 0xFE, 0x51, 0x27, 0x58, 0xC5, 0x65,
    0x5E, 0x47, 0x8C, 0x99, 0x35, 0xE5, 0xF5, 0xE2, 0x82, 0x5E, 0xB3, 0x53, 0xD0, 0xD7, 0x70, 0xA4,
    0x1D, 0x51, 0x2E, 0x15, 0x6B, 0xCA, 0xEB, 0x65, 0xB8, 0x43, 0xA9, 0xD6, 0x05, 0xBD, 0x66, 0x27,
    0x53, 0x36, 0x24, 0x98, 0xA0, 0xAF, 0xE1, 0xE8, 0xED, 0x28, 0x9F, 0x59, 0x3B, 0xA2, 0x5C, 0xAA,
    0x88, 0x1B, 0x1A, 0x1B, 0xD6, 0x94, 0xD7, 0x6B, 0x23, 0x0E, 0x95, 0xDC, 0x70, 0x87, 0x52, 0x2D,
    0xBE, 0x00, 0x10, 0x9E, 0x0B, 0x7A, 0xCD, 0xEE, 0x58, 0xF3, 0x8A, 0x3F, 0x01, 0x46, 0x72, 0x69,
    0x65, 0x6E, 0x64, 0x54, 0x77, 0x6F, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x04, 0xE0, 0x00,
    0x00, 0x00, 0x7C, 0x9F, 0x86, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6E, 0x00, 0x80, 0x00, 0x00,
    0x40, 0x01, 0x00, 0xB8, 0x0C, 0x01, 0x00, 0x58, 0x0C, 0xB1, 0xEC, 0xFF, 0xFF, 0xCB, 0x03, 0xAC,
    0x65, 0x05, 0x00, 0x80, 0x00, 0x02, 0x00, 0xE8, 0x10, 0x02, 0x00, 0xC4, 0x14, 0x02, 0x00, 0x14,
    0x17, 0x02, 0x00, 0xB2, 0x18, 0x02, 0x00, 0xDA, 0x19, 0x02, 0x00, 0x02, 0x1B, 0x02, 0x00, 0x15,
    0x1C, 0x02, 0x00, 0xA9, 0x1C, 0x02, 0x00, 0x3D, 0x1D, 0x02, 0x00, 0xD1, 0x1D, 0x02, 0x00, 0x65,
    0x1E, 0x02, 0x00, 0xF9, 0x1E, 0x02, 0x00, 0x8D, 0x1F, 0x02, 0x80, 0x10, 0x20, 0x02, 0x80, 0x5A,
    0x20, 0x02, 0x80, 0xA4, 0x20, 0x02, 0x80, 0xEE, 0x20, 0x12, 0x40, 0x19, 0xC9, 0xB4, 0x14, 0x79,
    0xA5, 0xCD, 0x54, 0x9B, 0xDC, 0xBC, 0x1B, 0xAF, 0x13, 0xAC, 0xE2, 0xC2, 0x4A, 0x9B, 0xA9, 0xF6,
    0x81, 0x8A, 0x70, 0x0A, 0xB9, 0x79, 0x37, 0x3E, 0xF0, 0x68, 0xFE, 0x51, 0x27, 0x58, 0xC5, 0x65,
    0x5E, 0x47, 0x8C, 0x99, 0x95, 0x36, 0x53, 0xED, 0x82, 0x5E, 0xB3, 0x53, 0xD0, 0xD7, 0x70, 0xA4,
    0x1D, 0x51, 0x2E, 0x15, 0x6B, 0xCA, 0xEB, 0x65, 0xB8, 0x43, 0xA9, 0xD6, 0x05, 0xBD, 0x66, 0x27,
    0x53, 0x36, 0x24, 0x98, 0xA0, 0xAF, 0xE1, 0xE8, 0xED, 0x28, 0x9F, 0x59, 0x3B, 0xA2, 0x5C, 0xAA,
    0x88, 0x1B, 0x1A, 0x1B, 0xD6, 0x94, 0xD7, 0x6B, 0x23, 0x0E, 0x95, 0xDC, 0x70, 0x87, 0x52, 0x2D,
    0xBE, 0x00, 0x10, 0x9E, 0x0B, 0x7A, 0xCD, 0xEE, 0x58, 0xF3, 0x8A, 0x5F, 0xA6, 0x6C, 0x48, 0x10,
    0x2F, 0x62, 0x69, 0x60,
};

std::vector<uint8_t> V1File()
{
    return std::vector<uint8_t>(std::begin(V1_FILE), std::end(V1_FILE));
}

// every single bit of a good file flipped, with the checksum fixed so the body checks get it: the reader never accepts
// an invalid state, and a flip either leaves a valid file (a clothes key, a stat bit) or is refused
void CheckEveryBitFlip(const std::vector<uint8_t>& good)
{
    using namespace PlayerStateSidecar;
    SFile read;
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
}

// garbage bodies behind a good file's header, with the checksum fixed: refused or valid, never anything else
void CheckGarbageBodies(const std::vector<uint8_t>& good, uint32_t random)
{
    using namespace PlayerStateSidecar;
    SFile read;
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
    CHECK(data[4] == 2 && data[5] == 0);  // format version 2

    SFile read;
    CHECK(Read(data, read) == eReadResult::Ok);
    CHECK(read.version == 2);
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
    data[4] = 3;  // a newer format
    CHECK(Read(data, read) == eReadResult::UnknownVersion);
    data[4] = 0;
    CHECK(Read(data, read) == eReadResult::UnknownVersion);
    data[4] = 2;
    data[5] = 1;  // 258
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

    CheckEveryBitFlip(good);
    CheckGarbageBodies(good, 12345);

    CHECK(std::string(ToString(eReadResult::UnknownVersion)) == "unknown version");
}

// CAPS v1 files, from the builds before respect was kept, are still read: their states come without respect
void OlderFiles()
{
    using namespace PlayerStateSidecar;
    CHECK(GetStateLayout(1) == SPlayerState::LAYOUT_V1 && GetStateLayout(2) == SPlayerState::LAYOUT_V2);
    CHECK(GetStateLayout(0) == 0 && GetStateLayout(3) == 0 && GetStateLayout(VERSION) == SPlayerState::LAYOUT_CURRENT);

    const std::vector<uint8_t> v1 = V1File();
    SFile read;
    CHECK(Read(v1, read) == eReadResult::Ok);
    CHECK(read.version == 1 && read.saveSize == 202752 && read.saveHash == 0x0123456789ABCDEFull);
    CHECK(read.entries.size() == 2);
    CHECK(read.entries[0].name == "FriendOne" && read.entries[0].state == WithoutRespect(MakeState(1)));
    CHECK(read.entries[1].name == "FriendTwo" && read.entries[1].state == WithoutRespect(MakeState(2)));

    // written again it becomes a v2 file, its states still without respect
    std::vector<uint8_t> data;
    CHECK(Write(read, data) && data[4] == 2 && data[5] == 0 && data.size() >= v1.size());  // a bit more per entry
    SFile again;
    CHECK(Read(data, again) == eReadResult::Ok && again.version == 2 && again.entries.size() == 2);
    CHECK(again.entries[0].state == read.entries[0].state && again.entries[1].state == read.entries[1].state);
    CHECK(!again.entries[0].state.hasRespect && !again.entries[1].state.hasRespect);

    // a v2 file holds both kinds
    SFile mixed;
    mixed.entries.push_back({"Known", MakeState(7)});
    mixed.entries.push_back({"Unknown", WithoutRespect(MakeState(8))});
    CHECK(Write(mixed, data) && Read(data, again) == eReadResult::Ok && again.entries.size() == 2);
    CHECK(again.entries[0].state == MakeState(7) && again.entries[1].state == WithoutRespect(MakeState(8)));

    // the body checks hold for v1 files too
    std::vector<uint8_t> bad = v1;
    bad[6] = 3;  // more entries than the body holds
    FixChecksum(bad);
    CHECK(Read(bad, read) == eReadResult::Corrupt);
    bad[6] = 1;  // fewer
    FixChecksum(bad);
    CHECK(Read(bad, read) == eReadResult::Corrupt);
    bad = v1;
    bad.pop_back();
    CHECK(Read(bad, read) == eReadResult::BadChecksum);
    CheckEveryBitFlip(v1);
    CheckGarbageBodies(v1, 54321);
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

// The host loads a save whose sidecar a build before respect wrote, then saves with this build.
void StoreWithOlderFile()
{
    using namespace PlayerStateSidecar;
    CPlayerStateStore::SRestore restore;
    SFile v1;
    CHECK(Read(V1File(), v1) == eReadResult::Ok);

    CPlayerStateStore store(1);
    store.OnSaveLoaded(v1.entries);
    // FriendOne connects with a new game: the restore has no respect, so their game keeps its own
    CHECK(store.OnSnapshot("FriendOne", 3, 0, SPlayerState{}, restore) == eSnapshotResult::SendRestore);
    CHECK(restore.state == WithoutRespect(MakeState(1)) && !restore.state.hasRespect);
    // applied (CPlayerStateSave::ApplyState leaves the respect stats alone), their next snapshot carries the respect
    // their game has; the record keeps it from now on
    SPlayerState applied = MakeState(1);
    applied.respect = 42.75f;
    applied.clothesRespect = 30.0f;
    applied.missionRespect = 3;
    CHECK(store.OnSnapshot("FriendOne", 3, restore.restoreId, applied, restore) == eSnapshotResult::Accepted);
    CHECK(*store.Find("FriendOne") == applied && store.Find("FriendOne")->hasRespect);

    // the next save is a v2 file: FriendOne with respect, FriendTwo (not seen since the load) still without
    SFile saved;
    saved.entries = store.GetEntries();
    std::vector<uint8_t> data;
    CHECK(Write(saved, data));
    SFile back;
    CHECK(Read(data, back) == eReadResult::Ok && back.version == 2 && back.entries.size() == 2);
    for (const SEntry& entry : back.entries)
    {
        CHECK((entry.name == "FriendOne" && entry.state == applied) ||
              (entry.name == "FriendTwo" && entry.state == WithoutRespect(MakeState(2))));
    }

    // and it gives FriendOne's respect back in the next session
    CPlayerStateStore later(9);
    later.OnSaveLoaded(back.entries);
    CHECK(later.OnSnapshot("FriendOne", 1, 0, SPlayerState{}, restore) == eSnapshotResult::SendRestore);
    CHECK(restore.state == applied && restore.state.hasRespect && restore.state.missionRespect == 3);
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
    SPlayerState respected = a;
    respected.missionRespect += 3;  // a mirrored mission reward: respect only
    CHECK(client.OnCaptured(5000, respected));
    CHECK(client.IsCaptureDue(7000));
    CHECK(client.OnCaptured(7000, b));
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
        const SPlayerState& restored = friendGame.GetPendingRestore();
        // as CPlayerStateSave::ApplyState: without respect in it, the game keeps its own
        SPlayerState next = restored;
        if (!restored.hasRespect)
        {
            next.hasRespect = true;
            next.respect = friendState.respect;
            next.clothesRespect = friendState.clothesRespect;
            next.missionRespect = friendState.missionRespect;
        }
        friendState = next;
        friendGame.OnRestoreDone();
    };

    friendGame.Reset(1);
    CHECK(sendSnapshot(0) == eSnapshotResult::Accepted);
    // the host passes a mission: the mirrored reward (money, award_player_mission_respect) lands in the friend's game
    friendState.money += 5000;
    friendState.missionRespect += 3;
    friendState.respect += 0.75f;  // and an enemy gang member the friend shot on the way
    CHECK(sendSnapshot(2000) == eSnapshotResult::Accepted);
    const SPlayerState saved = friendState;

    // the friend's game crashes; New Game; connects again
    friendState = SPlayerState{};
    friendGame = CPlayerStateClient{};
    friendGame.Reset(2);
    CHECK(sendSnapshot(10000) == eSnapshotResult::SendRestore);
    CHECK(sendSnapshot(12000) == eSnapshotResult::Dropped);  // waits for the restore
    applyRestore();
    CHECK(friendState == saved && friendState.missionRespect == MakeState(5).missionRespect + 3);
    CHECK(sendSnapshot(12001) == eSnapshotResult::Accepted);
    CHECK(*host.Find("Friend") == saved);

    // the next evening the host loads a save from before respect was kept (CAPS v1, no respect for "Friend"): the
    // friend's new game keeps its own respect, everything else comes from the save
    PlayerStateSidecar::SFile older;
    older.entries.push_back({"Friend", WithoutRespect(MakeState(6))});
    host.OnSaveLoaded(older.entries);
    friendState = SPlayerState{};
    friendState.missionRespect = 7;  // what the friend's new game has by then (a mirrored mission reward)
    friendGame = CPlayerStateClient{};
    friendGame.Reset(3);
    CHECK(sendSnapshot(20000) == eSnapshotResult::SendRestore);
    applyRestore();
    CHECK(friendState.money == MakeState(6).money && friendState.missionRespect == 7 && friendState.hasRespect);
    CHECK(sendSnapshot(20001) == eSnapshotResult::Accepted);
    CHECK(host.Find("Friend")->hasRespect && host.Find("Friend")->missionRespect == 7);
}

int main()
{
    StateRoundTripAndChecks();
    Layouts();
    Names();
    Fingerprints();
    Sidecar();
    OlderFiles();
    StoreSnapshotsAndReconnects();
    StoreLoadsAndSaves();
    StoreWithOlderFile();
    OtherPlayerSide();
    Session();
    std::puts("player_state_test: all checks passed");
    return 0;
}
