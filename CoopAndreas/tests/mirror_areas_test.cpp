// Private build: policy test for the mission mirror layer's areas without GTA (client/src/CMirrorAreasPolicy.h and
// shared/network/mirror_areas.h, used by CMirrorAreas): the boxes of the highlight functions, the value checks, the
// receivers' ids and area filter, the grace time, the change thresholds, the rate limit, the mission end and the
// receiver set. Build and run: see CLAUDE.md (Fork changes, mission mirror layer, areas).
#include "../client/src/CMirrorAreasPolicy.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace MirrorAreasPolicy;
using MirrorAreasWire::GROUND_Z;
using MirrorAreasWire::IsValidArea;
using MirrorAreasWire::KIND_AREA;
using MirrorAreasWire::KIND_MARKER;

namespace
{
bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.0001f;
}

// a script's id: its address in CTheScripts::ScriptsArray (0xA8B430, 0xE0 each) plus an instruction pointer in the
// mission block (0xA7A6A0)
constexpr uint32_t MISSION_ID = 0xA8B430 + 0xE0 * 3 + 0xA7A6A0 + 0x1234;
constexpr uint32_t OTHER_ID = MISSION_ID + 0x40;

SArea Area(uint32_t id, float x, float y, float z = GROUND_Z, float half = 2.0f)
{
    return MakeArea(id, x - half, y - half, x + half, y + half, z, 0);
}

// what CMirrorAreas does once per script frame on the host: the active set, and whether it went out
bool Frame(CHostAreas& host, uint32_t now, int* sentCount = nullptr)
{
    host.Expire(now);
    if (!host.IsSendDue(now))
    {
        return false;
    }
    SArea out[MAX_AREAS];
    const int count = host.GetActive(out, MAX_AREAS);
    host.OnSent(now);
    if (sentCount != nullptr)
    {
        *sentCount = count;
    }
    return true;
}

void TestWireAndIds()
{
    CHECK(MirrorAreasWire::MAX_AREAS == 32);
    CHECK(GROUND_Z == -100.0f);
    CHECK(MirrorAreasWire::IsGroundZ(-100.0f));
    CHECK(MirrorAreasWire::IsGroundZ(-5000.0f));
    CHECK(!MirrorAreasWire::IsGroundZ(-99.9f));
    CHECK(MirrorAreasWire::KIND_LAST_SUPPORTED < MirrorAreasWire::KIND_FIELD_MAX);

    // the receivers' ids: the top two bits set, the rest of the host's id kept
    CHECK(ToLocalId(MISSION_ID) == (MISSION_ID | 0xC0000000u));
    CHECK((ToLocalId(0u) & 0xC0000000u) == 0xC0000000u);
    CHECK(ToLocalId(0xFFFFFFFFu) == 0xFFFFFFFFu);
    CHECK(ToLocalId(MISSION_ID) != ToLocalId(OTHER_ID));
    // the game's own script ids stay below 0x40000000, CNetworkCheckpoint's (a positive float's bits) below 0x80000000
    CHECK(MISSION_ID < 0x40000000u);
    float radius = 2.0f;
    uint32_t checkpointId = 0;
    static_assert(sizeof(radius) == sizeof(checkpointId), "float size");
    std::memcpy(&checkpointId, &radius, sizeof(radius));
    CHECK(ToLocalId(MISSION_ID) != checkpointId && (checkpointId & 0x80000000u) == 0);
}

void TestBoxes()
{
    // HighlightImportantArea: the corners in any order give one box
    const SArea a = MakeArea(MISSION_ID, 10.0f, 20.0f, 14.0f, 26.0f, 15.0f, 3);
    const SArea b = MakeArea(MISSION_ID, 14.0f, 26.0f, 10.0f, 20.0f, 15.0f, 3);
    CHECK(a.kind == KIND_AREA && a.areaCode == 3 && a.id == MISSION_ID);
    CHECK(Near(a.x, 12.0f) && Near(a.y, 23.0f) && Near(a.z, 15.0f) && Near(a.sizeX, 2.0f) && Near(a.sizeY, 3.0f));
    CHECK(Near(b.x, a.x) && Near(b.y, a.y) && Near(b.sizeX, a.sizeX) && Near(b.sizeY, a.sizeY));
    const SArea c = MakeArea(MISSION_ID, 14.0f, 20.0f, 10.0f, 26.0f, GROUND_Z, 0);
    CHECK(Near(c.x, 12.0f) && Near(c.sizeX, 2.0f) && c.z == GROUND_Z);
    // drawing it again with the receiver's corners gives the same box
    const SArea redrawn = MakeArea(MISSION_ID, a.x - a.sizeX, a.y - a.sizeY, a.x + a.sizeX, a.y + a.sizeY, a.z, 3);
    CHECK(Near(redrawn.x, a.x) && Near(redrawn.y, a.y) && Near(redrawn.sizeX, a.sizeX) && Near(redrawn.sizeY, a.sizeY));

    // HighlightImportantAngledArea: the axis-aligned box of the edge midpoints
    const SArea square = MakeAngledArea(MISSION_ID, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 5.0f, 0);
    CHECK(square.kind == KIND_AREA);
    CHECK(Near(square.x, 0.0f) && Near(square.y, 0.0f) && Near(square.sizeX, 0.5f) && Near(square.sizeY, 0.5f));
    const SArea rect = MakeAngledArea(MISSION_ID, 0.0f, 0.0f, 10.0f, 0.0f, 10.0f, 4.0f, 0.0f, 4.0f, GROUND_Z, 0);
    CHECK(Near(rect.x, 5.0f) && Near(rect.y, 2.0f) && Near(rect.sizeX, 5.0f) && Near(rect.sizeY, 2.0f));
    CHECK(rect.z == GROUND_Z);
    // corners in the other direction
    const SArea rect2 = MakeAngledArea(MISSION_ID, 0.0f, 4.0f, 10.0f, 4.0f, 10.0f, 0.0f, 0.0f, 0.0f, GROUND_Z, 0);
    CHECK(Near(rect2.x, rect.x) && Near(rect2.y, rect.y));
    CHECK(Near(rect2.sizeX, rect.sizeX) && Near(rect2.sizeY, rect.sizeY));

    // draw_sphere
    const SArea marker = MakeMarker(MISSION_ID, 100.0f, -200.0f, 13.5f, 3.0f, 0);
    CHECK(marker.kind == KIND_MARKER && Near(marker.sizeX, 3.0f) && marker.sizeY == 0.0f && Near(marker.z, 13.5f));
}

void TestValidation()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(IsValidArea(Area(MISSION_ID, 2495.0f, -1687.0f)));
    CHECK(IsValidArea(Area(MISSION_ID, 2495.0f, -1687.0f, 1014.0f)));
    CHECK(IsValidArea(MakeMarker(MISSION_ID, 0.0f, 0.0f, -5000.0f, 1.0f, 0)));  // a height far below counts as ground
    CHECK(!IsValidArea(Area(MISSION_ID, nan, 0.0f)));
    CHECK(!IsValidArea(Area(MISSION_ID, 0.0f, inf)));
    CHECK(!IsValidArea(Area(MISSION_ID, 0.0f, 0.0f, nan)));
    CHECK(!IsValidArea(Area(MISSION_ID, 0.0f, 0.0f, -inf)));
    CHECK(!IsValidArea(Area(MISSION_ID, 25000.0f, 0.0f)));
    CHECK(!IsValidArea(Area(MISSION_ID, 0.0f, 0.0f, 25000.0f)));
    CHECK(!IsValidArea(MakeMarker(MISSION_ID, 0.0f, 0.0f, 10.0f, -1.0f, 0)));
    CHECK(!IsValidArea(MakeMarker(MISSION_ID, 0.0f, 0.0f, 10.0f, 6000.0f, 0)));
    CHECK(!IsValidArea(MakeMarker(MISSION_ID, 0.0f, 0.0f, 10.0f, nan, 0)));
    SArea later = Area(MISSION_ID, 0.0f, 0.0f);
    later.kind = MirrorAreasWire::KIND_RACE_CHECKPOINT;  // M5's kinds aren't drawn by this build
    CHECK(!IsValidArea(later));
    later.kind = MirrorAreasWire::KIND_SCRIPT_SPHERE;
    CHECK(!IsValidArea(later));
}

void TestAreaFilter()
{
    SArea outside = Area(MISSION_ID, 2495.0f, -1687.0f, 13.0f);
    SArea inside = Area(OTHER_ID, 2496.0f, -1700.0f, 1014.0f);
    inside.areaCode = 3;
    CHECK(ShouldDraw(outside, 0) && !ShouldDraw(outside, 3));
    CHECK(ShouldDraw(inside, 3) && !ShouldDraw(inside, 0) && !ShouldDraw(inside, 1));
    SArea later = outside;
    later.kind = MirrorAreasWire::KIND_RACE_CHECKPOINT;
    CHECK(!ShouldDraw(later, 0));
}

void TestThresholds()
{
    const SArea sent = Area(MISSION_ID, 100.0f, 100.0f, 20.0f);
    CHECK(!HasChanged(sent, sent));
    CHECK(!HasChanged(sent, Area(MISSION_ID, 100.49f, 100.0f, 20.0f)));
    CHECK(HasChanged(sent, Area(MISSION_ID, 100.51f, 100.0f, 20.0f)));
    CHECK(!HasChanged(sent, Area(MISSION_ID, 100.3f, 100.3f, 20.0f)));   // 0.42 m
    CHECK(HasChanged(sent, Area(MISSION_ID, 100.4f, 100.4f, 20.0f)));    // 0.57 m
    CHECK(!HasChanged(sent, Area(MISSION_ID, 100.0f, 100.0f, 20.45f)));
    CHECK(HasChanged(sent, Area(MISSION_ID, 100.0f, 100.0f, 20.55f)));   // a 3D locate's height counts
    CHECK(HasChanged(sent, Area(MISSION_ID, 100.0f, 100.0f, GROUND_Z))); // a height <-> the ground
    const SArea ground = Area(MISSION_ID, 100.0f, 100.0f, GROUND_Z);
    SArea groundLower = ground;
    groundLower.z = -500.0f;  // both are the ground
    CHECK(!HasChanged(ground, groundLower));
    // resized
    CHECK(!HasChanged(sent, Area(MISSION_ID, 100.0f, 100.0f, 20.0f, 2.09f)));
    CHECK(HasChanged(sent, Area(MISSION_ID, 100.0f, 100.0f, 20.0f, 2.11f)));
    SArea wider = sent;
    wider.sizeY += 0.2f;
    CHECK(HasChanged(sent, wider));
    // another kind, area code or id
    SArea marker = sent;
    marker.kind = KIND_MARKER;
    CHECK(HasChanged(sent, marker));
    SArea interior = sent;
    interior.areaCode = 5;
    CHECK(HasChanged(sent, interior));
    CHECK(HasChanged(sent, Area(OTHER_ID, 100.0f, 100.0f, 20.0f)));
}

void TestGraceTime()
{
    CHostAreas host;
    CHECK(host.Record(Area(MISSION_ID, 0.0f, 0.0f), 1000));
    host.Expire(1250);
    CHECK(host.GetRecordedCount() == 1);  // seen 250 ms ago: still active
    host.Expire(1251);
    CHECK(host.GetRecordedCount() == 0);  // 251 ms: gone

    // drawn again within the grace time: stays
    CHECK(host.Record(Area(MISSION_ID, 0.0f, 0.0f), 2000));
    CHECK(host.Record(Area(MISSION_ID, 0.0f, 0.0f), 2200));
    host.Expire(2400);
    CHECK(host.GetRecordedCount() == 1);
    // a record updates the area in place
    CHECK(host.Record(Area(MISSION_ID, 5.0f, 0.0f), 2410));
    SArea out[MAX_AREAS];
    CHECK(host.GetActive(out, MAX_AREAS) == 1 && Near(out[0].x, 5.0f));

    // the clock went back (the game started over): everything expires
    host.Expire(100);
    CHECK(host.GetRecordedCount() == 0);
}

void TestSendsAndRateLimit()
{
    CHostAreas host;
    // nothing drawn, nothing sent
    CHECK(!Frame(host, 0));
    CHECK(!Frame(host, 5000));

    // a new area goes out at once
    int count = -1;
    host.Record(Area(MISSION_ID, 0.0f, 0.0f), 10000);
    CHECK(Frame(host, 10000, &count) && count == 1);
    // unchanged: nothing more, however long it stays
    for (uint32_t t = 10016; t < 12000; t += 16)
    {
        host.Record(Area(MISSION_ID, 0.0f, 0.0f), t);
        CHECK(!Frame(host, t));
    }

    // a second area 50 ms after the last send waits for the rate limit
    CHostAreas limited;
    limited.Record(Area(MISSION_ID, 0.0f, 0.0f), 20000);
    CHECK(Frame(limited, 20000, &count) && count == 1);
    limited.Record(Area(MISSION_ID, 0.0f, 0.0f), 20050);
    limited.Record(Area(OTHER_ID, 50.0f, 0.0f), 20050);
    CHECK(limited.HasChanges());
    CHECK(!Frame(limited, 20050));
    limited.Record(Area(MISSION_ID, 0.0f, 0.0f), 20099);
    limited.Record(Area(OTHER_ID, 50.0f, 0.0f), 20099);
    CHECK(!Frame(limited, 20099));
    limited.Record(Area(MISSION_ID, 0.0f, 0.0f), 20100);
    limited.Record(Area(OTHER_ID, 50.0f, 0.0f), 20100);
    CHECK(Frame(limited, 20100, &count) && count == 2);

    // an area that is no longer drawn (OTHER_ID, last at 20100) goes in the first frame after the grace time, and the
    // smaller set goes out then
    uint32_t removedAt = 0;
    for (uint32_t t = 20116; t <= 20600 && removedAt == 0; t += 16)
    {
        limited.Record(Area(MISSION_ID, 0.0f, 0.0f), t);
        if (Frame(limited, t, &count))
        {
            removedAt = t;
        }
    }
    CHECK(removedAt > 20100 + GRACE_MS && removedAt <= 20100 + GRACE_MS + 16 && count == 1);
    limited.Record(Area(MISSION_ID, 0.0f, 0.0f), removedAt + 16);
    CHECK(!Frame(limited, removedAt + 16));

    // an entity-relative locate moving at 20 m/s, a frame every 16 ms: sent when 0.5 m away from the last set, at most
    // every 100 ms
    CHostAreas moving;
    int sends = 0;
    uint32_t lastSend = 0;
    for (uint32_t frame = 0; frame <= 62; frame++)  // about a second
    {
        const uint32_t now = 30000 + frame * 16;
        moving.Record(Area(MISSION_ID, frame * 0.32f, 0.0f, 10.0f), now);
        if (Frame(moving, now))
        {
            CHECK(sends == 0 || now - lastSend >= MIN_SEND_INTERVAL_MS);
            lastSend = now;
            sends++;
        }
    }
    CHECK(sends >= 9 && sends <= 11);

    // a slow one: small steps add up against the set sent last
    CHostAreas slow;
    slow.Record(Area(MISSION_ID, 0.0f, 0.0f, 10.0f), 40000);
    CHECK(Frame(slow, 40000));
    int slowSends = 0;
    for (uint32_t frame = 1; frame <= 60; frame++)  // 0.02 m a frame, 1.2 m in all
    {
        const uint32_t now = 40000 + frame * 16;
        slow.Record(Area(MISSION_ID, frame * 0.02f, 0.0f, 10.0f), now);
        slowSends += Frame(slow, now) ? 1 : 0;
    }
    CHECK(slowSends == 2);
}

void TestMissionEndAndReset()
{
    CHostAreas host;
    // nothing went out: the end of the mission sends nothing
    host.Record(Area(MISSION_ID, 0.0f, 0.0f), 1000);
    CHECK(!host.ClearRecorded());
    CHECK(host.GetRecordedCount() == 0);

    // something went out: an empty set goes out at once, even within the rate limit
    host.Record(Area(MISSION_ID, 0.0f, 0.0f), 2000);
    CHECK(Frame(host, 2000));
    CHECK(host.ClearRecorded());
    SArea out[MAX_AREAS];
    CHECK(host.GetActive(out, MAX_AREAS) == 0);
    host.OnSent(2010);
    CHECK(host.GetSentCount() == 0);
    CHECK(!host.HasChanges());
    CHECK(!host.ClearRecorded());  // nothing to clear on the other games any more

    // the next mission's first area waits for the rate limit after that empty set
    host.Record(Area(OTHER_ID, 1.0f, 1.0f), 2050);
    CHECK(!Frame(host, 2050));
    host.Record(Area(OTHER_ID, 1.0f, 1.0f), 2110);
    CHECK(Frame(host, 2110));

    // the others dropped their set ($onmission went to 0 there): what is still drawn goes out again
    host.ForgetSent();
    CHECK(host.HasChanges());
    host.Record(Area(OTHER_ID, 1.0f, 1.0f), 2300);
    CHECK(Frame(host, 2300));
    CHECK(!host.HasChanges());
    // ... and with nothing drawn there is nothing to send
    CHostAreas idle;
    idle.ForgetSent();
    CHECK(!idle.HasChanges());

    // a join snapshot: everything again
    host.ForgetSent();
    CHECK(host.IsSendDue(2310) == false);  // within the rate limit for the next frame's change ...
    CHECK(host.HasChanges());              // ... which the snapshot sends at once

    // a host change: nothing kept, the first area goes out at once
    host.Reset();
    CHECK(host.GetRecordedCount() == 0 && host.GetSentCount() == 0 && !host.HasChanges());
    host.Record(Area(MISSION_ID, 0.0f, 0.0f), 2320);
    CHECK(Frame(host, 2320));
}

void TestTableFull()
{
    CHostAreas host;
    for (uint32_t i = 0; i < static_cast<uint32_t>(MAX_AREAS); i++)
    {
        CHECK(host.Record(Area(MISSION_ID + i * 8, static_cast<float>(i), 0.0f), 1000));
    }
    CHECK(!host.Record(Area(MISSION_ID + 0x1000, 0.0f, 0.0f), 1000));         // a new one doesn't fit
    CHECK(host.Record(Area(MISSION_ID, 50.0f, 0.0f), 1010));                 // a known one is updated
    int count = 0;
    CHECK(Frame(host, 1010, &count) && count == MAX_AREAS);
}

void TestReceiver()
{
    CReceivedAreas received;
    CHECK(received.GetCount() == 0);
    SArea set[MAX_AREAS + 4];
    for (int i = 0; i < MAX_AREAS + 4; i++)
    {
        set[i] = Area(MISSION_ID + static_cast<uint32_t>(i), static_cast<float>(i), 0.0f);
    }
    received.Set(set, 2);
    CHECK(received.GetCount() == 2 && received.Get(1).id == MISSION_ID + 1);
    received.Set(set + 5, 1);  // a new set replaces the old one
    CHECK(received.GetCount() == 1 && received.Get(0).id == MISSION_ID + 5);
    received.Set(set, MAX_AREAS + 4);
    CHECK(received.GetCount() == MAX_AREAS);
    received.Set(set, -3);
    CHECK(received.GetCount() == 0);
    received.Set(set, 3);
    received.Set(nullptr, 3);
    CHECK(received.GetCount() == 0);
    received.Set(set, 3);
    received.Clear();
    CHECK(received.GetCount() == 0);
}
}  // namespace

int main()
{
    TestWireAndIds();
    TestBoxes();
    TestValidation();
    TestAreaFilter();
    TestThresholds();
    TestGraceTime();
    TestSendsAndRateLimit();
    TestMissionEndAndReset();
    TestTableFull();
    TestReceiver();
    std::printf("mirror_areas_test: all checks passed\n");
    return 0;
}
