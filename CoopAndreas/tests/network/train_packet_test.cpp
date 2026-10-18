// Real packet serializers and SDK value types; no GTA engine or network calls.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class Packet;
struct RegistrationStub
{
    void RegisterPacket(Packet*) {}
};
inline RegistrationStub& GetPacketFactory()
{
    static RegistrationStub stub;
    return stub;
}

#include "network/packets/train.h"
#include "network/packets/system.h"
#include "semver.h"

#if !defined(NDEBUG) && !defined(SERIALIZE_DEBUG)
#error Debug regression runs must use the real serializer assertions.
#endif

using namespace Packets::Train;

static void Require(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct Wire
{
    // BitReader loads words; keep the allocation padded while passing the exact wire length.
    std::array<uint8_t, 16384> bytes{};
    int size = 0;
    int bits = 0;
};

static Wire Encode(Packet& packet)
{
    Wire wire;
    serialize::MeasureStream measure;
    Require(packet.SerializeMeasure(measure), "measure packet");
    serialize::WriteStream writer(wire.bytes.data(), static_cast<int>(wire.bytes.size()));
    Require(packet.SerializeWrite(writer), "write packet");
    wire.bits = writer.GetBitsProcessed();
    writer.Flush();
    wire.size = writer.GetBytesProcessed();
    // MeasureStream conservatively adds seven bits at every byte alignment.
    Require(measure.GetBitsProcessed() >= wire.bits, "measure does not underestimate write");
    return wire;
}

static bool Decode(Packet& packet, const Wire& wire, int size)
{
    serialize::ReadStream reader(wire.bytes.data(), size);
    return packet.SerializeRead(reader);
}

static void Save(const std::filesystem::path& path, const Wire& wire)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(wire.bytes.data()), wire.size);
    Require(out.good(), "save wire fixture");
}

static Wire Load(const std::filesystem::path& path)
{
    Wire wire;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    Require(in.good(), "open peer wire fixture");
    wire.size = static_cast<int>(in.tellg());
    Require(wire.size > 0 && wire.size < static_cast<int>(wire.bytes.size()), "fixture length");
    in.seekg(0);
    in.read(reinterpret_cast<char*>(wire.bytes.data()), wire.size);
    Require(in.good(), "read peer wire fixture");
    return wire;
}

static STrainStatePayload State()
{
    STrainStatePayload value;
    value.sequence = 255;
    value.flags = STATE_FLAG_CLOCKWISE | STATE_FLAG_DERAILED | STATE_FLAG_HAS_NPC_DRIVER |
        STATE_FLAG_STOPPED_AT_STATION;
    value.jumpCounter = 254;
    value.trackId = 3;
    value.railDistance = 12345.5f;
    value.speedQuantized = -32767;
    value.gasHalf = 255;
    value.brakeHalf = 254;
    return value;
}

static void CheckState(const STrainStatePayload& actual, const STrainStatePayload& expected)
{
    Require(actual.sequence == expected.sequence && actual.flags == expected.flags &&
        actual.jumpCounter == expected.jumpCounter && actual.trackId == expected.trackId &&
        actual.railDistance == expected.railDistance && actual.speedQuantized == expected.speedQuantized &&
        actual.gasHalf == expected.gasHalf && actual.brakeHalf == expected.brakeHalf, "state fields");
}

static uint32_t NextIncarnation(uint32_t value)
{
    ++value;
    return value == 0 ? 1 : value;
}

static STrainSpawnPayload Spawn(int index, uint32_t incarnation)
{
    STrainSpawnPayload value;
    value.trainId = static_cast<uint8_t>(MAX_TRAINS - 1 - index);
    value.incarnation = incarnation;
    value.trackId = 3;
    value.configIndex = 15;
    value.configurationOffset = 0;
    value.bClockwise = true;
    value.nodeIndex = 65535;
    value.carriageCount = MAX_TRAIN_CARRIAGES;
    value.layoutHash = 0xfedcba98u + index;
    value.initialState = State();
    for (int i = 0; i < value.carriageCount; ++i)
    {
        value.carriages[i].vehicleid = Config::MAX_SERVER_VEHICLES - 1 - i;
        value.carriages[i].modelid = static_cast<uint16_t>(537 + i);
        value.carriages[i].doorLock = static_cast<uint8_t>(i == 0 ? 8 : i);
        constexpr uint16_t health[] = {0, 327, 1000, 65535};
        value.carriages[i].health = health[i % 4];
        value.carriages[i].bWrecked = i % 2 == 0;
    }
    return value;
}

static void CheckSpawn(const STrainSpawnPayload& actual, const STrainSpawnPayload& expected)
{
    Require(actual.trainId == expected.trainId && actual.incarnation == expected.incarnation &&
        actual.trackId == expected.trackId && actual.configurationOffset == expected.configurationOffset &&
        actual.configIndex == expected.configIndex && actual.bClockwise == expected.bClockwise &&
        actual.nodeIndex == expected.nodeIndex && actual.carriageCount == expected.carriageCount &&
        actual.layoutHash == expected.layoutHash, "spawn fields");
    CheckState(actual.initialState, expected.initialState);
    for (int i = 0; i < expected.carriageCount; ++i)
    {
        Require(actual.carriages[i].vehicleid == expected.carriages[i].vehicleid &&
            actual.carriages[i].modelid == expected.carriages[i].modelid &&
            actual.carriages[i].doorLock == expected.carriages[i].doorLock &&
            actual.carriages[i].health == expected.carriages[i].health &&
            actual.carriages[i].bWrecked == expected.carriages[i].bWrecked, "spawn carriage fields");
    }
}

static TrainEvent Event(eTrainEventKind kind, uint32_t incarnation = 1)
{
    TrainEvent value;
    value.playerid.value = Config::MAX_SERVER_PLAYERS - 1;
    value.targetPlayerId = Config::MAX_SERVER_PLAYERS - 1;
    value.kind = kind;
    value.spawn = Spawn(0, incarnation);
    value.trainId = MAX_TRAINS - 1;
    value.incarnation = incarnation;
    value.carriageCount = MAX_TRAIN_CARRIAGES;
    for (int i = 0; i < value.carriageCount; ++i)
    {
        value.carriageStates[i].doorLock = 8;
        value.carriageStates[i].health = static_cast<uint16_t>(i == 0 ? 0 : 65535);
        value.carriageStates[i].bWrecked = i % 2 == 0;
    }
    value.bReset = true;
    value.snapshotCount = MAX_SNAPSHOT_TRAINS;
    uint32_t descriptorIncarnation = incarnation;
    for (int i = 0; i < value.snapshotCount; ++i)
    {
        descriptorIncarnation = NextIncarnation(descriptorIncarnation);
        value.snapshotTrains[i] = Spawn(i, descriptorIncarnation);
    }
    return value;
}

static void CheckSender(int value)
{
    Require(value == (Config::IsClient ? Config::MAX_SERVER_PLAYERS - 1 : 2),
        "S2C sender carried; C2S sender retained from authenticated server context");
}

static void CheckEvent(const TrainEvent& actual, const TrainEvent& expected)
{
    CheckSender(actual.playerid.value);
    Require(actual.targetPlayerId == expected.targetPlayerId && actual.kind == expected.kind, "event envelope");
    if (expected.kind != eTrainEventKind::SPAWN)
        Require(actual.incarnation == expected.incarnation, "event incarnation or snapshot restart fence");
    switch (expected.kind)
    {
        case eTrainEventKind::SPAWN: CheckSpawn(actual.spawn, expected.spawn); break;
        case eTrainEventKind::REMOVE: Require(actual.trainId == expected.trainId, "remove train"); break;
        case eTrainEventKind::CARRIAGES:
            Require(actual.trainId == expected.trainId && actual.carriageCount == expected.carriageCount,
                "carriages envelope");
            for (int i = 0; i < expected.carriageCount; ++i)
                Require(actual.carriageStates[i].doorLock == expected.carriageStates[i].doorLock &&
                    actual.carriageStates[i].health == expected.carriageStates[i].health &&
                    actual.carriageStates[i].bWrecked == expected.carriageStates[i].bWrecked, "carriage state fields");
            break;
        case eTrainEventKind::SNAPSHOT:
            Require(actual.bReset == expected.bReset && actual.snapshotCount == expected.snapshotCount,
                "snapshot envelope");
            for (int i = 0; i < expected.snapshotCount; ++i)
                CheckSpawn(actual.snapshotTrains[i], expected.snapshotTrains[i]);
            break;
        case eTrainEventKind::REQUEST:
            Require(actual.trainId == expected.trainId && actual.requestKind == eTrainRequestKind::SKIP_STATION,
                "request fields");
            break;
    }
}

static void SetBits(Wire& wire, int offset, int count, unsigned value)
{
    for (int i = 0; i < count; ++i)
    {
        const int bit = offset + i;
        wire.bytes[bit / 8] = static_cast<uint8_t>((wire.bytes[bit / 8] & ~(1u << (bit % 8))) |
            (((value >> i) & 1u) << (bit % 8)));
    }
}

static void RejectWrites()
{
    TrainEvent event = Event(eTrainEventKind::REQUEST);
    event.requestKind = static_cast<eTrainRequestKind>(1);
    serialize::MeasureStream measure;
    Require(!static_cast<Packet&>(event).SerializeMeasure(measure), "unknown request rejected by measure");
    Wire wire;
    serialize::WriteStream writer(wire.bytes.data(), static_cast<int>(wire.bytes.size()));
    Require(!static_cast<Packet&>(event).SerializeWrite(writer), "unknown request rejected by writer with assertions enabled");
    for (auto kind : {eTrainEventKind::SPAWN, eTrainEventKind::CARRIAGES, eTrainEventKind::SNAPSHOT})
    {
        for (int count : {0, MAX_TRAIN_CARRIAGES + 1, 255})
        {
            if (kind == eTrainEventKind::SNAPSHOT && count == 0) continue;
            event = Event(kind);
            event.spawn.carriageCount = event.carriageCount = event.snapshotCount = static_cast<uint8_t>(count);
            serialize::MeasureStream invalidMeasure;
            Require(!static_cast<Packet&>(event).SerializeMeasure(invalidMeasure), "invalid count rejected by measure");
            serialize::WriteStream invalidWriter(wire.bytes.data(), static_cast<int>(wire.bytes.size()));
            Require(!static_cast<Packet&>(event).SerializeWrite(invalidWriter), "invalid count rejected by writer");
        }
    }
}

static void VersionCheck()
{
    const uint32_t current = semver_parse(COOPANDREAS_VERSION, nullptr);
    const uint32_t released = semver_parse("0.1.2-alpha", nullptr);
    Require(current == semver_parse("0.1.3-alpha", nullptr) && current != released,
        "changed protocol has distinct packed version");
    // Exercise the actual compatibility-stable handshake and mismatch-response serializers.
    Packets::System::PlayerConnected connect{};
    connect.payload.playerid = 0;
    std::strcpy(connect.payload.name, "PacketTest");
    connect.payload.version = released;
    const Wire hello = Encode(connect);
    Packets::System::PlayerConnected decoded{};
    Require(Decode(decoded, hello, hello.size), "read old-version hello");
    Require(decoded.payload.version != current, "server equality gate rejects released hello");
    connect.payload.version = current;
    const Wire matching = Encode(connect);
    Require(Decode(decoded, matching, matching.size) && decoded.payload.version == current,
        "server equality gate accepts current hello");
    Packets::System::PlayerDisconnected reject{};
    reject.payload.playerid = -1;
    reject.payload.reason = Packets::System::PlayerDisconnected::DISCONNECTION_REASON_VERSION_MISMATCH;
    reject.payload.version = current;
    const Wire response = Encode(reject);
    Packets::System::PlayerDisconnected received{};
    Require(Decode(received, response, response.size) && received.payload.playerid == -1 &&
        received.payload.reason == Packets::System::PlayerDisconnected::DISCONNECTION_REASON_VERSION_MISMATCH &&
        received.payload.version == current, "version mismatch response retains reason and server version");
}

int main(int argc, char** argv)
{
    Require(argc == 3, "usage: train_packet_test --write|--read fixture-directory");
    const bool writing = std::strcmp(argv[1], "--write") == 0;
    Require(writing || std::strcmp(argv[1], "--read") == 0, "known test phase");
    const std::filesystem::path dir(argv[2]);
    if (writing) std::filesystem::create_directories(dir);
    RejectWrites();
    VersionCheck();
    // Preserve all uint32 bits, including the sign boundary and the last generation before wrap.
    // Snapshot descriptors advance past each fence, skipping zero when UINT32_MAX wraps to one.
    for (uint32_t incarnation : {1u, 0x7fffffffu, 0x80000000u, 0xffffffffu})
    {
    for (int variant = 0; variant <= 9; ++variant)
    {
        const int kind = variant == 5 || variant == 9 ? 3 :
            (variant == 6 || variant == 8 ? 0 : (variant == 7 ? 2 : variant));
        TrainEvent expected = Event(static_cast<eTrainEventKind>(kind), incarnation);
        if (variant == 5)
        {
            expected.snapshotCount = 0;
            expected.bReset = false;
            expected.targetPlayerId = -1;
        }
        if (variant == 6)
        {
            expected.spawn.carriageCount = 1;
            expected.spawn.configurationOffset = 15;
        }
        if (variant == 7) expected.carriageCount = 1;
        if (variant == 8) expected.spawn.configurationOffset = 8;
        if (variant == 9)
        {
            expected.snapshotCount = 1;
            expected.snapshotTrains[0].carriageCount = 1;
            expected.snapshotTrains[0].configurationOffset = 15;
        }
        const auto path = dir / ("event-" + std::to_string(variant) + "-" +
            std::to_string(incarnation) + ".bin");
        if (writing) Save(path, Encode(expected));
        else
        {
            const Wire wire = Load(path);
            TrainEvent actual;
            actual.playerid.value = 2;
            Require(Decode(actual, wire, wire.size), "read peer event");
            CheckEvent(actual, expected);
            for (int size = 0; size < wire.size; ++size)
            {
                TrainEvent truncated;
                Require(!Decode(truncated, wire, size), "every byte-truncated event rejected");
            }
            const int senderBits = Config::IsClient ? 3 : 0;
            Wire invalid = wire;
            SetBits(invalid, senderBits + 4, 3, 7);
            TrainEvent malformed;
            Require(!Decode(malformed, invalid, invalid.size), "invalid event kind rejected");
            if (expected.kind == eTrainEventKind::REQUEST)
            {
                invalid = wire;
                // REQUEST discriminator is the final byte, independent of lifecycle envelope fields.
                serialize::ReadStream reader(wire.bytes.data(), wire.size);
                Require(static_cast<Packet&>(actual).SerializeRead(reader), "request bit length");
                for (unsigned request : {1u, 255u})
                {
                    SetBits(invalid, reader.GetBitsProcessed() - 8, 8, request);
                    Require(!Decode(malformed, invalid, invalid.size), "invalid wire request rejected");
                }
            }
            if (variant == 5)
            {
                // In an empty snapshot the count is the final four-bit field.
                serialize::ReadStream reader(wire.bytes.data(), wire.size);
                Require(static_cast<Packet&>(actual).SerializeRead(reader), "snapshot bit length");
                for (unsigned count : {9u, 15u})
                {
                    invalid = wire;
                    SetBits(invalid, reader.GetBitsProcessed() - 4, 4, count);
                    Require(!Decode(malformed, invalid, invalid.size), "invalid wire snapshot count rejected");
                }
            }
        }
    }
    TrainState expected;
    expected.playerid.value = Config::MAX_SERVER_PLAYERS - 1;
    expected.trainId = MAX_TRAINS - 1;
    expected.incarnation = incarnation;
    expected.state = State();
    const auto statePath = dir / ("state-" + std::to_string(incarnation) + ".bin");
    if (writing) Save(statePath, Encode(expected));
    else
    {
        const Wire wire = Load(statePath);
        TrainState actual;
        actual.playerid.value = 2;
        Require(Decode(actual, wire, wire.size), "read peer state");
        CheckSender(actual.playerid.value);
        Require(actual.trainId == expected.trainId && actual.incarnation == expected.incarnation,
            "state train id and incarnation");
        CheckState(actual.state, expected.state);
        for (int size = 0; size < wire.size; ++size)
        {
            TrainState truncated;
            Require(!Decode(truncated, wire, size), "every byte-truncated state rejected");
        }
        // The first input field occupies exactly 3 bits on S2C and no bits on C2S.
        serialize::ReadStream senderReader(wire.bytes.data(), wire.size);
        SenderPlayerId sender;
        sender.value = 2;
        Require(sender.Serialize(senderReader), "sender serialization");
        Require(senderReader.GetBitsProcessed() == (Config::IsClient ? 3 : 0), "sender wire direction");
    }
    }
    std::printf("ok: %s %s (%s assertions)\n", Config::IsClient ? "client" : "server", argv[1],
#ifdef NDEBUG
        "release disabled"
#else
        "debug enabled"
#endif
    );
}
