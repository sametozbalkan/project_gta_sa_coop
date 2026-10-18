// Private build: the real MISSION_MIRROR_BLIPS serializer (shared/network/packets/mirror_blips.h) and SDK value types; no
// GTA engine or network calls. Built and run by scripts/test-train-packets.ps1 (client and server builds swap fixtures).
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

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

#include "network/packets/mirror_blips.h"

#if !defined(NDEBUG) && !defined(SERIALIZE_DEBUG)
#error Debug regression runs must use the real serializer assertions.
#endif

using namespace Packets::Mirror;

static void Require(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// CPacketFactory::Send writes into a 10 KiB buffer: 16-bit type and 32-bit server time before the packet
constexpr int SEND_BUFFER_BYTES = 10 * 1024;
constexpr int SEND_HEADER_BITS = 16 + 32;
constexpr uint8_t BLIP_OBJECT = 3;  // eBlipType

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
    Require(measure.GetBitsProcessed() >= wire.bits, "measure does not underestimate write");
    return wire;
}

static bool Decode(Packet& packet, const Wire& wire)
{
    serialize::ReadStream reader(wire.bytes.data(), wire.size);
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

static SMirrorBlipEntry ObjectEntry(uint32_t hostHandle, int objectHandle)
{
    SMirrorBlipEntry entry;
    entry.hostHandle = hostHandle;
    entry.op = MirrorBlipsWire::OP_FULL;
    entry.position = CVector(2815.25f, -1165.5f, 25.75f);
    entry.type = BLIP_OBJECT;
    entry.entityKind = MirrorBlipsWire::ENTITY_OBJECT;
    entry.entityId = objectHandle;
    entry.colour = 0xFF0000FFu;
    entry.display = 2;
    entry.size = 2;
    return entry;
}

// object handles at the edges: 0 (valid), the largest 1.0 US handle (slot 349, id 0x7F) and the wire's maximum; then a
// position update and a removal
static MirrorBlips Objects()
{
    MirrorBlips packet;
    packet.playerid.value = Config::MAX_SERVER_PLAYERS - 1;
    packet.targetPlayerId = 1;
    packet.bReset = true;
    packet.count = 5;
    packet.entries[0] = ObjectEntry(0x00010002u, 0);
    packet.entries[1] = ObjectEntry(0x00020003u, (349 << 8) | 0x7F);
    packet.entries[2] = ObjectEntry(0xFFFF0004u, MirrorBlipsWire::OBJECT_ID_MAX);
    packet.entries[3].hostHandle = 0x00010002u;
    packet.entries[3].op = MirrorBlipsWire::OP_POSITION;
    packet.entries[3].position = CVector(2816.0f, -1165.5f, 25.75f);
    packet.entries[4].hostHandle = 0x00020003u;
    packet.entries[4].op = MirrorBlipsWire::OP_REMOVED;
    return packet;
}

// the largest packet: every entry FULL with every optional field, positions outside the compressed bounds
static MirrorBlips Largest()
{
    MirrorBlips packet;
    packet.playerid.value = 0;
    packet.targetPlayerId = -1;
    packet.count = MirrorBlipsWire::MAX_ENTRIES;
    for (int i = 0; i < packet.count; i++)
    {
        SMirrorBlipEntry& entry = packet.entries[i];
        entry = ObjectEntry(0xFFFFFFFFu - static_cast<uint32_t>(i), MirrorBlipsWire::OBJECT_ID_MAX - i);
        entry.position = CVector(-5000.0f, 5000.0f, 2000.0f + static_cast<float>(i));
        entry.colour = 0xFFFFFFFFu;
        entry.sprite = 63;
        entry.display = 3;
        entry.size = 255;
        entry.bBright = entry.bShortRange = entry.bFriendly = entry.bRemain = entry.bFade = true;
        entry.coordAppearance = 3;
        entry.bEntrance = true;
        entry.entrance = CVector(5000.0f, -5000.0f, -500.0f);
    }
    return packet;
}

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static bool SameEntry(const SMirrorBlipEntry& a, const SMirrorBlipEntry& b)
{
    if (a.hostHandle != b.hostHandle || a.op != b.op)
    {
        return false;
    }
    if (a.op == MirrorBlipsWire::OP_REMOVED)
    {
        return true;
    }
    if (!Near(a.position.x, b.position.x) || !Near(a.position.y, b.position.y) || !Near(a.position.z, b.position.z))
    {
        return false;
    }
    if (a.op == MirrorBlipsWire::OP_POSITION)
    {
        return true;
    }
    return a.type == b.type && a.entityKind == b.entityKind && a.entityId == b.entityId && a.colour == b.colour &&
        a.sprite == b.sprite && a.display == b.display && a.size == b.size && a.bBright == b.bBright &&
        a.bShortRange == b.bShortRange && a.bFriendly == b.bFriendly && a.bRemain == b.bRemain && a.bFade == b.bFade &&
        a.coordAppearance == b.coordAppearance && a.bEntrance == b.bEntrance &&
        (!a.bEntrance || (Near(a.entrance.x, b.entrance.x) && Near(a.entrance.y, b.entrance.y) &&
                             Near(a.entrance.z, b.entrance.z)));
}

static void Check(const MirrorBlips& expected, const Wire& wire, const char* name)
{
    MirrorBlips actual;
    Require(Decode(actual, wire), name);
    // the sender id goes S2C only (SenderPlayerId): a server reading a client's fixture keeps 0
    const int sender = Config::IsServer ? 0 : expected.playerid.value;
    Require(actual.playerid.value == sender && actual.targetPlayerId == expected.targetPlayerId &&
            actual.bReset == expected.bReset && actual.count == expected.count, name);
    for (int i = 0; i < expected.count; i++)
    {
        Require(SameEntry(actual.entries[i], expected.entries[i]), name);
    }
}

static void RejectWrites()
{
    MirrorBlips packet = Objects();
    packet.count = MirrorBlipsWire::MAX_ENTRIES + 1;
    std::array<uint8_t, 16384> bytes{};
    serialize::WriteStream writer(bytes.data(), static_cast<int>(bytes.size()));
    Require(!static_cast<Packet&>(packet).SerializeWrite(writer), "a count above MAX_ENTRIES is refused");
}

int main(int argc, char** argv)
{
    Require(argc == 3, "usage: mirror_blips_packet_test --write|--read fixture-directory");
    const bool writing = std::strcmp(argv[1], "--write") == 0;
    Require(writing || std::strcmp(argv[1], "--read") == 0, "known test phase");
    const std::filesystem::path dir(argv[2]);
    if (writing) std::filesystem::create_directories(dir);
    RejectWrites();

    MirrorBlips objects = Objects();
    MirrorBlips largest = Largest();
    const Wire largestWire = Encode(largest);
    Require((SEND_HEADER_BITS + largestWire.bits + 7) / 8 <= SEND_BUFFER_BYTES, "48 entries fit CPacketFactory's buffer");
    if (writing)
    {
        Save(dir / "mirror-blips-objects.bin", Encode(objects));
        Save(dir / "mirror-blips-largest.bin", largestWire);
        std::printf("mirror_blips_packet_test: largest packet %d bytes (%d bits)\n", largestWire.size, largestWire.bits);
    }
    else
    {
        Check(objects, Load(dir / "mirror-blips-objects.bin"), "decode object entries");
        Check(largest, Load(dir / "mirror-blips-largest.bin"), "decode the largest packet");
    }
    return 0;
}
