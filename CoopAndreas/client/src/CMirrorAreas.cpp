#include "stdafx.h"
#include "CMirrorAreas.h"
#include "CMirrorAreasPolicy.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "CServerTime.h"
#include "CSessionLifecycle.h"
#include "network/packets/mirror_areas.h"
#include <C3dMarkers.h>
#include <CTimer.h>
#include <cstdarg>

// Private build: the mission mirror layer's areas, see CMirrorAreas.h.
//
// gta_sa.exe 1.0 US used below:
// - CTheScripts::HighlightImportantArea 0x485E00 (cdecl: id, x1, y1, x2, y2, z) and C3dMarkers::PlaceMarkerSet
//   0x725BA0 (cdecl: id, type, CVector&, size, r, g, b, a, pulse period, pulse fraction, rotate rate), plugin-sdk
//   CTheScripts.cpp and C3dMarkers.cpp. The receivers call them as the host's game did: draw_sphere's case in
//   ProcessCommands900To999 (0x483BD0, CommandHandlerTable[9]) pushes type 1 (MARKER3D_CYLINDER), 255/0/0/228, 2048,
//   0.1f and 0 for its call at 0x4845F8 (dumpbin 0x4845AF-0x4845F7).
// - Markers last one frame: C3dMarkers::Update 0x7227B0 activates the markers placed since the last render, and
//   Render3dMarkers renders and releases them (gta-reversed 3dMarkers.cpp). So the receivers place theirs again in
//   every frame, as CNetworkCheckpoint::Process does from the same plugin-sdk drawing event (after the call at 0x53E293
//   in Render2dStuff 0x53E230); they are activated and rendered in the next frame.
// - CGame::currArea 0xB72914 (plugin-sdk CGame.cpp): the area code (interior) the game shows. A cylinder marker takes
//   its height from the collision under it (C3dMarker::UpdateZCoordinate 0x724D40, from C3dMarkers::PlaceMarker
//   0x725120), and CColStore requires the interior collision files only for a game inside an interior
//   (SetIfCollisionIsRequired, gta-reversed ColStore.cpp): an interior's sphere drawn by a game outside would land on
//   the street under that interior. So every area carries the host's area code, and a receiver draws only those of the
//   area it shows.
// - CTimer::m_snTimeInMilliseconds: the game clock; it stands still while the game is paused, as the scripts do.
namespace
{
using MirrorAreasPolicy::SArea;

// host: what the mission family drew and what went out
MirrorAreasPolicy::CHostAreas ms_host;
// others: the current host's set
MirrorAreasPolicy::CReceivedAreas ms_received;
// the mission state of the last script frame (its end clears the host's set)
bool ms_bWasMirrored = false;

// --coopdiag (plain values; the diag thread reads them). Constant-initialized: the hooks report into it during the
// DLL's static initialization.
struct SCounters
{
    int sitesHooked = 0;
    int sitesTotal = 0;
    unsigned int recordedAreas = 0;    // HighlightImportantArea calls of the family
    unsigned int recordedAngled = 0;   // HighlightImportantAngledArea calls of the family
    unsigned int recordedMarkers = 0;  // draw_sphere calls of the family
    unsigned int recordsDropped = 0;   // values out of bounds, or the table full
    unsigned int setsSent = 0;
    unsigned int emptySent = 0;        // of which empty: the mission ended, the game started over, all areas expired
    unsigned int snapshots = 0;        // join snapshots
    unsigned int framesDeferred = 0;   // script frames with a set due but no session or server clock yet
    int activeCount = 0;
    unsigned int packetsReceived = 0;
    unsigned int packetsDropped = 0;   // not from the current host
    int receivedCount = 0;
    unsigned int draws = 0;            // areas drawn, summed over frames
    unsigned int drawsSkipped = 0;     // areas of another area code, summed over frames
};
SCounters ms_counters;

// --coopdiag: CoopAndreas_mirror.log next to gta_sa.exe (CMissionMirror's log), at most 512 records of this module and
// 1 MB
void LogLine(const char* format, ...)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 512)
    {
        return;
    }

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    {
        return;
    }
    char* fileName = strrchr(path, '\\');
    if (!fileName)
    {
        return;
    }
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_mirror.log");
    FILE* file = fopen(path, "a+");
    if (!file)
    {
        return;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) < 1024 * 1024)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        fprintf(file, "t=%u frame=%u areas %s host=%d\n", GetTickCount(), CTimer::m_FrameCounter, text,
            CSessionLifecycle::IsHost());
        ++records;
    }
    fclose(file);
}

uint8_t GetAreaCode()
{
    const int area = CGame::currArea;
    return static_cast<uint8_t>(area < 0 ? 0 : (area > UINT8_MAX ? UINT8_MAX : area));
}

// the running script is the host's mission family (inside a command CRunningScript::Process runs, COpCodeSync's hook)
bool IsFamilyDrawing()
{
    return CMissionMirror::IsFamilyScript(COpCodeSync::GetScriptRunningCommand());
}

void Record(const SArea& area, unsigned int& counter)
{
    if (!MirrorAreasWire::IsValidArea(area) || !ms_host.Record(area, CTimer::m_snTimeInMilliseconds))
    {
        ++ms_counters.recordsDropped;
        return;
    }
    ++counter;
}

// the active set to every other game (the server relays it from the host only); packets sent before the first
// server-time answer would carry the PC's uptime as their stamp and wait in the other games' packet buffers, so a set
// due then waits for the clock
void SendActive(uint32_t now, const char* reason)
{
    if (!CSessionLifecycle::IsAuthenticated() || CServerTime::m_serverTimeOffset == 0)
    {
        ++ms_counters.framesDeferred;
        return;
    }

    static Packets::Mirror::MirrorAreas packet{};
    const int previous = ms_host.GetSentCount();
    packet.count = ms_host.GetActive(packet.areas, MirrorAreasWire::MAX_AREAS);
    GetPacketFactory().Send(packet);
    ms_host.OnSent(now);

    ++ms_counters.setsSent;
    if (packet.count == 0)
    {
        ++ms_counters.emptySent;
    }
    // an entity-relative locate's moves go out up to 10 times a second: only sends that change the count are logged
    if (packet.count != previous || strcmp(reason, "change") != 0)
    {
        LogLine("event=send reason=%s areas=%d->%d first=%08X kind=%d area=%d", reason, previous, packet.count,
            packet.count > 0 ? packet.areas[0].id : 0u, packet.count > 0 ? packet.areas[0].kind : -1,
            packet.count > 0 ? packet.areas[0].areaCode : -1);
    }
}

// the mission ended or the game started over: nothing of the family is active; the others get an empty set at once
// if they still show areas of this game
void EndHostAreas(uint32_t now, const char* reason)
{
    ms_counters.activeCount = 0;
    if (ms_host.ClearRecorded())
    {
        SendActive(now, reason);
    }
}

// PLAYER_JOINED on the host (design 3.11): the current set. It goes to every other game; the players already in get
// the set they have. Marked as unsent first, so that a snapshot that can't go out yet goes with the next script frame.
void SendSnapshot(int playerId, bool bTakeover)
{
    if (!CSessionLifecycle::IsHost())
    {
        return;
    }
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    ms_host.Expire(now);
    if (ms_host.GetRecordedCount() == 0)
    {
        return;
    }
    ++ms_counters.snapshots;
    LogLine("event=snapshot player=%d takeover=%d areas=%d", playerId, bTakeover, ms_host.GetRecordedCount());
    ms_host.ForgetSent();
    SendActive(now, "snapshot");
}

void ClearReceived(const char* reason)
{
    if (ms_received.GetCount() > 0)
    {
        LogLine("event=receiver-clear reason=%s areas=%d", reason, ms_received.GetCount());
    }
    ms_received.Clear();
    ms_counters.receivedCount = 0;
}

void OnSessionEvent(const SSessionEvent& event)
{
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            SendSnapshot(event.playerId, event.bTakeover);
            break;
        case eSessionEvent::HOST_CHANGED:
        case eSessionEvent::DISCONNECTED:
            // a new host starts empty; what a former host sent goes away (its late packets are dropped by sender)
            ms_host.Reset();
            ms_counters.activeCount = 0;
            ClearReceived(event.type == eSessionEvent::HOST_CHANGED ? "host-changed" : "disconnected");
            break;
        case eSessionEvent::HOST_MISSION_ENDED:
            if (event.bLocal)
            {
                // the others drop their set on this ON_MISSION_FLAG_SYNC 1 -> 0 (the else branch on their side): what
                // the family still draws goes out again
                ms_host.ForgetSent();
            }
            else
            {
                ClearReceived(event.bHostLost ? "host-lost" : "host-mission-ended");
            }
            break;
        case eSessionEvent::GAME_RESTARTED:
        case eSessionEvent::SAVE_LOADED:
            // a new game or a load of the host (SAVE_LOADED right after GAME_RESTARTED finds the set cleared). The
            // other games keep the host's set over their own restart: the host sends only changes.
            if (CSessionLifecycle::IsHost())
            {
                EndHostAreas(CTimer::m_snTimeInMilliseconds,
                    (event.type == eSessionEvent::SAVE_LOADED || event.bLoaded) ? "save-loaded" : "restarted");
            }
            break;
        default:
            break;
    }
}

// others, every frame (the plugin-sdk drawing event): the host's areas, with the game's functions and the host's
// arguments, under local ids
void Draw()
{
    const int count = ms_received.GetCount();
    if (count == 0 || CSessionLifecycle::IsHost())
    {
        return;
    }
    const int localAreaCode = CGame::currArea;
    for (int i = 0; i < count; i++)
    {
        const SArea& area = ms_received.Get(i);
        if (!MirrorAreasPolicy::ShouldDraw(area, localAreaCode))
        {
            ++ms_counters.drawsSkipped;
            continue;
        }
        const uint32_t id = MirrorAreasPolicy::ToLocalId(area.id);
        if (area.kind == MirrorAreasWire::KIND_MARKER)
        {
            CVector position(area.x, area.y, area.z);
            C3dMarkers::PlaceMarkerSet(id, MARKER3D_CYLINDER, position, area.sizeX, 255, 0, 0, 228, 2048, 0.1f, 0);
        }
        else
        {
            CTheScripts::HighlightImportantArea(static_cast<int>(id), area.x - area.sizeX, area.y - area.sizeY,
                area.x + area.sizeX, area.y + area.sizeY, area.z);
        }
        ++ms_counters.draws;
    }
}
}  // namespace

void CMirrorAreas::Init()
{
    CSessionLifecycle::Subscribe(OnSessionEvent);
    Events::drawingEvent += Draw;
}

void CMirrorAreas::OnHooksInstalled(int hooked, int total)
{
    ms_counters.sitesHooked = hooked;
    ms_counters.sitesTotal = total;
    LogLine("event=hooks hooked=%d total=%d", hooked, total);
}

void CMirrorAreas::OnHookSkipped(uintptr_t site, uint8_t firstByte, uintptr_t target)
{
    logger::warn("Mission mirror areas: skipped the call at 0x%X (first byte 0x%02X, target 0x%X)",
        static_cast<unsigned int>(site), firstByte, static_cast<unsigned int>(target));
    LogLine("event=hook-skipped site=0x%X firstByte=0x%02X target=0x%X", static_cast<unsigned int>(site), firstByte,
        static_cast<unsigned int>(target));
}

void CMirrorAreas::OnHighlightArea(uint32_t id, float x1, float y1, float x2, float y2, float z)
{
    if (IsFamilyDrawing())
    {
        Record(MirrorAreasPolicy::MakeArea(id, x1, y1, x2, y2, z, GetAreaCode()), ms_counters.recordedAreas);
    }
}

void CMirrorAreas::OnHighlightAngledArea(
    uint32_t id, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float z)
{
    if (IsFamilyDrawing())
    {
        Record(MirrorAreasPolicy::MakeAngledArea(id, x1, y1, x2, y2, x3, y3, x4, y4, z, GetAreaCode()),
            ms_counters.recordedAngled);
    }
}

void CMirrorAreas::OnDrawSphere(uint32_t id, const CVector& position, float radius)
{
    if (IsFamilyDrawing())
    {
        Record(MirrorAreasPolicy::MakeMarker(id, position.x, position.y, position.z, radius, GetAreaCode()),
            ms_counters.recordedMarkers);
    }
}

void CMirrorAreas::Process(bool bMirroredMission)
{
    const bool bMissionEnded = ms_bWasMirrored && !bMirroredMission;
    ms_bWasMirrored = bMirroredMission;

    if (!CSessionLifecycle::IsHost())
    {
        if (ms_host.GetRecordedCount() != 0 || ms_host.GetSentCount() != 0)
        {
            ms_host.Reset();
        }
        ms_counters.activeCount = 0;
        return;
    }

    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    if (bMissionEnded)
    {
        EndHostAreas(now, "mission-end");
        return;
    }
    ms_host.Expire(now);
    ms_counters.activeCount = ms_host.GetRecordedCount();
    if (ms_host.IsSendDue(now))
    {
        SendActive(now, "change");
    }
}

void CMirrorAreas::HandlePacket(const Packets::Mirror::MirrorAreas& packet)
{
    // The host sends, it never draws. Only the current host's sets count: a former host's last packets wait 50 ms in
    // the packet buffer and are handled after its PLAYER_DISCONNECTED (CSessionLifecycle).
    const int sender = packet.playerid.value;
    const char* reason = nullptr;
    if (CSessionLifecycle::IsHost())
    {
        reason = "is-host";
    }
    else if (!CSessionLifecycle::HasHost() || sender != CSessionLifecycle::GetHostId())
    {
        reason = "not-from-host";
    }
    if (reason != nullptr)
    {
        ++ms_counters.packetsDropped;
        LogLine("event=drop sender=%d areas=%d reason=%s", sender, packet.count, reason);
        return;
    }

    const int previous = ms_received.GetCount();
    ms_received.Set(packet.areas, packet.count);
    ++ms_counters.packetsReceived;
    ms_counters.receivedCount = ms_received.GetCount();
    if (ms_received.GetCount() != previous)
    {
        LogLine("event=receive areas=%d->%d", previous, ms_received.GetCount());
    }
}

void CMirrorAreas::WriteDiagSummary(FILE* file)
{
    const SCounters& c = ms_counters;
    fprintf(file,
        "  mirror-areas: hooks=%d/%d recorded=%u+%u+%u dropped=%u active=%d sent=%u empty=%u snapshots=%u "
        "deferred=%u | recv packets=%u dropped=%u areas=%d draws=%u otherArea=%u\n",
        c.sitesHooked, c.sitesTotal, c.recordedAreas, c.recordedAngled, c.recordedMarkers, c.recordsDropped,
        c.activeCount, c.setsSent, c.emptySent, c.snapshots, c.framesDeferred, c.packetsReceived, c.packetsDropped,
        c.receivedCount, c.draws, c.drawsSkipped);
}
