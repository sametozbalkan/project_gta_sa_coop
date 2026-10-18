// Private build: the real MISSION_OBJECTS and MISSION_OBJECT_MOTION serializers (shared/network/packets/mirror_objects.h)
// and SDK value types; no GTA engine or network calls. Built and run by scripts/test-train-packets.ps1 (client and server
// builds swap fixtures).
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

#include "network/packets/mirror_objects.h"

#if !defined(NDEBUG) && !defined(SERIALIZE_DEBUG)
#error Debug regression runs must use the real serializer assertions.
#endif

using namespace Packets::Mirror;
namespace Wire = MirrorObjectsWire;

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

struct Wire_
{
    // BitReader loads words; keep the allocation padded while passing the exact wire length.
    std::array<uint8_t, 16384> bytes{};
    int size = 0;
    int bits = 0;
};

static Wire_ Encode(Packet& packet)
{
    Wire_ wire;
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

static bool Decode(Packet& packet, const Wire_& wire)
{
    serialize::ReadStream reader(wire.bytes.data(), wire.size);
    return packet.SerializeRead(reader);
}

static void Save(const std::filesystem::path& path, const Wire_& wire)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(wire.bytes.data()), wire.size);
    Require(out.good(), "save wire fixture");
}

static Wire_ Load(const std::filesystem::path& path)
{
    Wire_ wire;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    Require(in.good(), "open peer wire fixture");
    wire.size = static_cast<int>(in.tellg());
    Require(wire.size > 0 && wire.size < static_cast<int>(wire.bytes.size()), "fixture length");
    in.seekg(0);
    in.read(reinterpret_cast<char*>(wire.bytes.data()), wire.size);
    Require(in.good(), "read peer wire fixture");
    return wire;
}

static Wire::SRotation Rotation(float ax, float ay, float az, float radians)
{
    const float length = std::sqrt(ax * ax + ay * ay + az * az);
    const float s = std::sin(radians * 0.5f) / length;
    Wire::SRotation q;
    q.x = ax * s;
    q.y = ay * s;
    q.z = az * s;
    q.w = std::cos(radians * 0.5f);
    return q;
}

static Wire::SObjectEntry FullEntry(int32_t handle)
{
    Wire::SObjectEntry entry;
    entry.idKind = Wire::ID_MISSION;
    entry.id = handle;
    entry.op = Wire::OP_FULL;
    entry.full.model = 1271;
    entry.full.flags = Wire::FLAG_VISIBLE | Wire::FLAG_COLLISION | Wire::FLAG_LIFTABLE;
    entry.full.position = Wire::SPosition{2490.5f, -1680.25f, 13.5f};
    entry.full.rotation = Rotation(0.0f, 0.0f, 1.0f, 0.75f);
    return entry;
}

// every op and kind at its edges: handle 0, the largest 1.0 US handle (slot 349, id 0x7F) and the wire's maximum, an
// Euler and a quaternion attachment, a held object, an owner change, a removal and a persistent record
static MirrorObjects Objects()
{
    MirrorObjects packet;
    packet.playerid.value = Config::MAX_SERVER_PLAYERS - 1;
    packet.targetPlayerId = 1;
    packet.bReset = true;
    packet.count = 7;
    packet.entries[0] = FullEntry(0);
    packet.entries[1] = FullEntry((349 << 8) | 0x7F);
    packet.entries[1].full.parent.kind = Wire::PARENT_VEHICLE;
    packet.entries[1].full.parent.id = Config::MAX_SERVER_VEHICLES - 1;
    packet.entries[1].full.parent.offsetX = 0.25f;
    packet.entries[1].full.parent.offsetY = -1.5f;
    packet.entries[1].full.parent.offsetZ = 0.5f;
    packet.entries[1].full.parent.eulerZ = 1.5f;
    packet.entries[2] = FullEntry(Wire::MISSION_ID_MAX);
    packet.entries[2].full.parent.kind = Wire::PARENT_OBJECT;
    packet.entries[2].full.parent.id = 0;
    packet.entries[2].full.parent.bQuaternion = true;
    packet.entries[2].full.parent.rotation = Rotation(1.0f, 0.0f, 0.0f, -0.4f);
    packet.entries[2].full.holder.kind = Wire::HOLDER_PLAYER;
    packet.entries[2].full.holder.id = Config::MAX_SERVER_PLAYERS - 1;
    packet.entries[2].full.holder.bone = 24;
    packet.entries[2].full.holder.boneFlags = 0x10;
    packet.entries[2].full.holder.animId = 0x1A3;
    packet.entries[2].full.holder.animGroup = 0;
    packet.entries[3].idKind = Wire::ID_MISSION;
    packet.entries[3].id = 0x0101;
    packet.entries[3].op = Wire::OP_OWNER;
    packet.entries[3].owner.playerId = 2;
    packet.entries[3].owner.epoch = 255;
    packet.entries[4].idKind = Wire::ID_MISSION;
    packet.entries[4].id = 0x0102;
    packet.entries[4].op = Wire::OP_REMOVED;
    packet.entries[5] = FullEntry(Wire::PERSISTENT_ID_MAX);
    packet.entries[5].idKind = Wire::ID_PERSISTENT;
    packet.entries[6] = FullEntry(0x0203);
    packet.entries[6].full.bScaled = true;
    packet.entries[6].full.scale = 1.5f;
    packet.entries[6].full.area = 3;
    packet.entries[6].full.moveSpeed = Wire::SSpeed{0.1f, -0.2f, 0.0f};
    return packet;
}

// the largest packet: every entry FULL with every optional field, positions and speeds outside the compressed bounds
static MirrorObjects Largest()
{
    MirrorObjects packet;
    packet.playerid.value = 0;
    packet.targetPlayerId = -1;
    packet.count = Wire::MAX_ENTRIES;
    for (int i = 0; i < packet.count; i++)
    {
        Wire::SObjectEntry& entry = packet.entries[i];
        entry = FullEntry(Wire::MISSION_ID_MAX - i);
        Wire::SObjectFull& full = entry.full;
        full.model = Wire::MODEL_COUNT - 1;
        full.area = 255;
        full.flags = Wire::FLAGS_KNOWN;
        full.bScaled = true;
        full.scale = Wire::SCALE_MAX;
        full.position = Wire::SPosition{-5000.0f, 5000.0f, 2000.0f + static_cast<float>(i)};
        full.moveSpeed = Wire::SSpeed{5.0f, -5.0f, 5.0f};
        full.turnSpeed = Wire::SSpeed{-5.0f, 5.0f, -5.0f};
        full.owner.playerId = Config::MAX_SERVER_PLAYERS - 1;
        full.owner.epoch = 255;
        full.parent.kind = Wire::PARENT_OBJECT;
        full.parent.id = Wire::MISSION_ID_MAX;
        full.parent.offsetX = Wire::OFFSET_LIMIT;
        full.parent.offsetY = -Wire::OFFSET_LIMIT;
        full.parent.offsetZ = 1.0f;
        full.parent.eulerX = 1.0f;
        full.parent.eulerY = -1.0f;
        full.parent.eulerZ = 3.0f;
        full.holder.kind = Wire::HOLDER_PED;
        full.holder.id = Config::MAX_SERVER_PEDS - 1;
        full.holder.offsetX = -Wire::OFFSET_LIMIT;
        full.holder.offsetY = Wire::OFFSET_LIMIT;
        full.holder.offsetZ = 0.5f;
        full.holder.bone = 255;
        full.holder.boneFlags = 255;
        full.holder.animId = Wire::ANIM_MAX;
        full.holder.animGroup = Wire::ANIM_MAX;
        full.holder.bAnimByName = true;
        full.holder.bDisallowDrop = true;
    }
    return packet;
}

static MirrorObjectMotion Motion()
{
    MirrorObjectMotion packet;
    packet.playerid.value = 3;
    packet.count = 2;
    packet.entries[0].id = 0x0203;
    packet.entries[0].epoch = 7;
    packet.entries[0].position = Wire::SPosition{100.0f, -200.0f, 15.0f};
    packet.entries[0].rotation = Rotation(0.0f, 1.0f, 0.0f, 2.0f);
    packet.entries[0].moveSpeed = Wire::SSpeed{0.2f, 0.0f, -0.1f};
    packet.entries[0].turnSpeed = Wire::SSpeed{0.0f, 0.0f, 0.05f};
    packet.entries[1].idKind = Wire::ID_PERSISTENT;
    packet.entries[1].id = 2706;
    return packet;
}

static MirrorObjectMotion LargestMotion()
{
    MirrorObjectMotion packet;
    packet.playerid.value = Config::MAX_SERVER_PLAYERS - 1;
    packet.count = Wire::MAX_MOTION_ENTRIES;
    for (int i = 0; i < packet.count; i++)
    {
        Wire::SMotionEntry& entry = packet.entries[i];
        entry.id = Wire::MISSION_ID_MAX - i;
        entry.epoch = 255;
        entry.position = Wire::SPosition{5000.0f, -5000.0f, -500.0f};
        entry.rotation = Rotation(1.0f, 1.0f, 1.0f, 2.0f);
        entry.moveSpeed = Wire::SSpeed{9.0f, -9.0f, 9.0f};
        entry.turnSpeed = Wire::SSpeed{-9.0f, 9.0f, -9.0f};
    }
    return packet;
}

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static bool SamePosition(const Wire::SPosition& a, const Wire::SPosition& b)
{
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

static bool SameSpeed(const Wire::SSpeed& a, const Wire::SSpeed& b)
{
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

// q and -q are the same rotation; the wire keeps about 0.1 degree
static bool SameRotation(const Wire::SRotation& a, const Wire::SRotation& b)
{
    const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return dot > 0.99999f;
}

static bool SameFull(const Wire::SObjectFull& a, const Wire::SObjectFull& b)
{
    if (a.model != b.model || a.area != b.area || a.flags != b.flags || a.bScaled != b.bScaled ||
        (a.bScaled && a.scale != b.scale) || !SamePosition(a.position, b.position) ||
        !SameRotation(a.rotation, b.rotation) || !SameSpeed(a.moveSpeed, b.moveSpeed) ||
        !SameSpeed(a.turnSpeed, b.turnSpeed) || a.owner.playerId != b.owner.playerId || a.owner.epoch != b.owner.epoch)
    {
        return false;
    }
    const Wire::SParent& p = a.parent;
    const Wire::SParent& q = b.parent;
    if (p.kind != q.kind || (p.kind != Wire::PARENT_NONE &&
        (p.id != q.id || !Near(p.offsetX, q.offsetX) || !Near(p.offsetY, q.offsetY) || !Near(p.offsetZ, q.offsetZ) ||
         p.bQuaternion != q.bQuaternion ||
         (p.bQuaternion ? !SameRotation(p.rotation, q.rotation)
                        : (!Near(p.eulerX, q.eulerX) || !Near(p.eulerY, q.eulerY) || !Near(p.eulerZ, q.eulerZ))))))
    {
        return false;
    }
    const Wire::SHolder& h = a.holder;
    const Wire::SHolder& k = b.holder;
    return h.kind == k.kind && (h.kind == Wire::HOLDER_NONE ||
        (h.id == k.id && Near(h.offsetX, k.offsetX) && Near(h.offsetY, k.offsetY) && Near(h.offsetZ, k.offsetZ) &&
         h.bone == k.bone && h.boneFlags == k.boneFlags && h.animId == k.animId && h.animGroup == k.animGroup &&
         h.bAnimByName == k.bAnimByName && h.bDisallowDrop == k.bDisallowDrop));
}

static bool SameEntry(const Wire::SObjectEntry& a, const Wire::SObjectEntry& b)
{
    if (a.idKind != b.idKind || a.id != b.id || a.op != b.op)
    {
        return false;
    }
    switch (a.op)
    {
        case Wire::OP_FULL: return SameFull(a.full, b.full);
        case Wire::OP_OWNER: return a.owner.playerId == b.owner.playerId && a.owner.epoch == b.owner.epoch;
        default: return true;
    }
}

static void Check(const MirrorObjects& expected, const Wire_& wire, const char* name)
{
    MirrorObjects actual;
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

static void CheckMotion(const MirrorObjectMotion& expected, const Wire_& wire, const char* name)
{
    MirrorObjectMotion actual;
    Require(Decode(actual, wire), name);
    const int sender = Config::IsServer ? 0 : expected.playerid.value;
    Require(actual.playerid.value == sender && actual.count == expected.count, name);
    for (int i = 0; i < expected.count; i++)
    {
        const Wire::SMotionEntry& a = actual.entries[i];
        const Wire::SMotionEntry& b = expected.entries[i];
        Require(a.idKind == b.idKind && a.id == b.id && a.epoch == b.epoch && SamePosition(a.position, b.position) &&
                SameRotation(a.rotation, b.rotation) && SameSpeed(a.moveSpeed, b.moveSpeed) &&
                SameSpeed(a.turnSpeed, b.turnSpeed), name);
    }
}

static void RejectWrites()
{
    std::array<uint8_t, 16384> bytes{};
    MirrorObjects packet = Objects();
    packet.count = Wire::MAX_ENTRIES + 1;
    serialize::WriteStream writer(bytes.data(), static_cast<int>(bytes.size()));
    Require(!static_cast<Packet&>(packet).SerializeWrite(writer), "a count above MAX_ENTRIES is refused");

    MirrorObjectMotion motion = Motion();
    motion.count = Wire::MAX_MOTION_ENTRIES + 1;
    serialize::WriteStream motionWriter(bytes.data(), static_cast<int>(bytes.size()));
    Require(!static_cast<Packet&>(motion).SerializeWrite(motionWriter), "a count above MAX_MOTION_ENTRIES is refused");
}

int main(int argc, char** argv)
{
    Require(argc == 3, "usage: mirror_objects_packet_test --write|--read fixture-directory");
    const bool writing = std::strcmp(argv[1], "--write") == 0;
    Require(writing || std::strcmp(argv[1], "--read") == 0, "known test phase");
    const std::filesystem::path dir(argv[2]);
    if (writing) std::filesystem::create_directories(dir);
    RejectWrites();

    MirrorObjects objects = Objects();
    MirrorObjects largest = Largest();
    MirrorObjectMotion motion = Motion();
    MirrorObjectMotion largestMotion = LargestMotion();
    const Wire_ largestWire = Encode(largest);
    const Wire_ largestMotionWire = Encode(largestMotion);
    Require((SEND_HEADER_BITS + largestWire.bits + 7) / 8 <= SEND_BUFFER_BYTES,
        "32 FULL entries fit CPacketFactory's buffer");
    Require((SEND_HEADER_BITS + largestMotionWire.bits + 7) / 8 <= SEND_BUFFER_BYTES,
        "24 motion entries fit CPacketFactory's buffer");
    if (writing)
    {
        Save(dir / "mirror-objects.bin", Encode(objects));
        Save(dir / "mirror-objects-largest.bin", largestWire);
        Save(dir / "mirror-object-motion.bin", Encode(motion));
        Save(dir / "mirror-object-motion-largest.bin", largestMotionWire);
        std::printf("mirror_objects_packet_test: largest MISSION_OBJECTS %d bytes (%d bits), largest "
                    "MISSION_OBJECT_MOTION %d bytes (%d bits)\n",
            largestWire.size, largestWire.bits, largestMotionWire.size, largestMotionWire.bits);
    }
    else
    {
        Check(objects, Load(dir / "mirror-objects.bin"), "decode MISSION_OBJECTS entries");
        Check(largest, Load(dir / "mirror-objects-largest.bin"), "decode the largest MISSION_OBJECTS");
        CheckMotion(motion, Load(dir / "mirror-object-motion.bin"), "decode MISSION_OBJECT_MOTION");
        CheckMotion(largestMotion, Load(dir / "mirror-object-motion-largest.bin"), "decode the largest motion");
    }
    return 0;
}
