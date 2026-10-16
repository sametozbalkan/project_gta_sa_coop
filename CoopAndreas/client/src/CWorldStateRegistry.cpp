#include "stdafx.h"
#include "CWorldStateRegistry.h"
#include "CWorldStateEntries.h"
#include "CWorldStatePolicy.h"
#include "CSessionLifecycle.h"
#include "CServerTime.h"
#include <CTimer.h>
#include <cstdarg>
#include <memory>
#include <vector>

// Private build: the world-state registry, see CWorldStateRegistry.h. Design: docs/design/world-state.md 3.1 (module and
// gate), 3.5 (packet), 3.6 (lifecycle); decisions in docs/design/README.md: W-D1 (the city unlock is entry 0), W-D2 (one
// generic packet at UPDATE_CITY_UNLOCKED's position), W-D8 (no story guard: the server pins the story host). The logic
// without game calls is in CWorldStatePolicy.h (tests/world_state_test.cpp). Everything here runs on the game thread
// except WriteDiagSummary. No hooks: Main.cpp's frame event, the packet handler and the lifecycle's events drive it.

static_assert(WorldStatePolicy::MAX_DATA_SIZE == sizeof(Packets::World::WorldState::data), "WORLD_STATE data size");

namespace
{
using WorldStatePolicy::Blob;
using WorldStatePolicy::eCheck;
using WorldStatePolicy::eGate;
using WorldStatePolicy::eReceive;
using WorldStatePolicy::eSend;
using WorldStatePolicy::SGateInput;
using WorldStatePolicy::SLayout;

// a re-apply is logged right after a packet, otherwise at most this often per entry
constexpr uint32_t APPLY_LOG_INTERVAL_MS = 10000;

struct SEntrySlot
{
    std::unique_ptr<CWorldStateEntry> pEntry;
    bool bEnabled = false;  // Init() succeeded and its wire id is unique
    uint32_t nLastRun = 0;  // the host's last poll or the last re-apply check (GetTickCount)
    bool bRan = false;
    // diagnostics
    uint32_t nLastApplyLog = 0;
    unsigned int nAppliesSinceLog = 0;
    bool bApplyLogged = false;
    bool bRefusedLogged = false;  // a FULL bigger than one packet
    bool bInvalidLogged = false;  // a captured blob that breaks its own layout rules
};

std::vector<SEntrySlot> ms_slots;  // in registration order; ms_table uses the same indexes
CWorldStateTable ms_table;
bool ms_bStarted = false;
Blob ms_captured;  // scratch blobs, reused
Blob ms_records;
Blob ms_after;

bool ms_bWaitingForClock = false;

// --coopdiag summary: plain values, read by the diag thread
struct SDiag
{
    bool bStarted = false;
    bool bClockSynced = false;
    const char* szRole = "none";
    unsigned int nEntries = 0;
    unsigned int nOffAtStart = 0;
    unsigned int nLayoutMismatches = 0;
    unsigned int nSentFull = 0;
    unsigned int nSentRecords = 0;
    unsigned int nReceivedFull = 0;
    unsigned int nReceivedRecords = 0;
    unsigned int nDropped = 0;
    unsigned int nApplies = 0;
    unsigned int nNotConverged = 0;
    eGate lastGate = eGate::NO_SESSION;
    const char* szLastApply = "-";
    uint32_t nLastApplyTick = 0;
} ms_diag;

const char* RoleName()
{
    if (CSessionLifecycle::IsHost())
    {
        return "host";
    }
    return CSessionLifecycle::HasHost() ? "other" : "none";
}

// --coopdiag: CoopAndreas_world_state.log next to gta_sa.exe, at most 512 records and 1 MB (as CoopAndreas_session.log)
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
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_world_state.log");
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
        fprintf(file, "t=%u frame=%u role=%s hostId=%d %s\n", GetTickCount(), CTimer::m_FrameCounter, RoleName(),
            CSessionLifecycle::GetHostId(), text);
        ++records;
    }
    fclose(file);
}

// a blob's payload (after the layout header) for the log: hex when short, else its size
const char* Describe(const Blob& blob, char (&text)[40])
{
    const size_t payload = blob.size() > WorldStatePolicy::HEADER_SIZE ? blob.size() - WorldStatePolicy::HEADER_SIZE : 0;
    if (payload == 0)
    {
        return "-";
    }
    if (payload > 12)
    {
        snprintf(text, sizeof(text), "%u bytes", static_cast<unsigned int>(payload));
        return text;
    }
    for (size_t i = 0; i < payload; i++)
    {
        snprintf(text + i * 2, sizeof(text) - i * 2, "%02X", blob[WorldStatePolicy::HEADER_SIZE + i]);
    }
    return text;
}

template <typename TEntry>
void Add()
{
    SEntrySlot slot;
    slot.pEntry = std::make_unique<TEntry>();
    ms_slots.push_back(std::move(slot));
}

// The entries by wire id (docs/design/world-state.md 3.2). A new entry is its class (CWorldStateEntries.h and the
// CWorldStateEntries*.cpp file of its subsystem) plus one line here; a wire id is never reused.
void CreateEntries()
{
    Add<CWorldStateCityUnlock>();      // 0
    Add<CWorldStateMaxWantedLevel>();  // 1
    Add<CWorldStateGarages>();         // 4
    Add<CWorldStateEntryExits>();      // no wire id (CWorldStateEntryExits::LOCAL_ID)
    Add<CWorldStateMissionSettings>(); // 8
}

int FindSlot(uint8_t entryId)
{
    for (size_t i = 0; i < ms_slots.size(); i++)
    {
        if (ms_slots[i].pEntry->Id() == entryId)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool IsDue(const SEntrySlot& slot, uint32_t now)
{
    const uint32_t interval = slot.pEntry->IntervalMs();
    return interval == 0 || !slot.bRan || now - slot.nLastRun >= interval;
}

void MarkRun(SEntrySlot& slot, uint32_t now)
{
    slot.nLastRun = now;
    slot.bRan = true;
}

SGateInput MakeGateInput()
{
    SGateInput input;
    input.bAuthenticated = CSessionLifecycle::IsAuthenticated();
    input.bIsHost = CSessionLifecycle::IsHost();
    input.bHasHost = CSessionLifecycle::HasHost();
    input.bRanSinceRestart = ms_table.RanSinceRestart(CTimer::m_FrameCounter);
    input.bLocalGameReady = CSessionLifecycle::IsLocalGameReady();
    input.bLocalMissionRunning = CSessionLifecycle::IsLocalMissionRunning();
    return input;
}

void SendPacket(uint8_t entryId, uint8_t kind, const Blob& data)
{
    static Packets::World::WorldState packet;  // 8 KB of data, kept off the stack
    packet.entry = entryId;
    packet.kind = kind;
    packet.size = static_cast<int>(data.size());
    memcpy(packet.data, data.data(), data.size());
    GetPacketFactory().Send(packet);
}

// ---- the host ----

void PollHostEntry(size_t index)
{
    SEntrySlot& slot = ms_slots[index];
    CWorldStateEntry& entry = *slot.pEntry;
    CWorldStateEntryState& state = ms_table.Entry(index);

    if (!entry.Capture(ms_captured))
    {
        return;
    }
    const uint16_t recordSize = entry.RecordSize();
    if (!WorldStatePolicy::IsValidFull(ms_captured, recordSize))
    {
        if (!slot.bInvalidLogged)
        {
            slot.bInvalidLogged = true;
            LogLine("event=capture-invalid entry=%s size=%u (not sent)", entry.Name(),
                static_cast<unsigned int>(ms_captured.size()));
        }
        return;
    }

    const eSend send = state.Poll(ms_captured, recordSize, ms_records);
    if (send == eSend::NONE)
    {
        return;
    }
    const bool bFull = send == eSend::FULL;
    const Blob& data = bFull ? ms_captured : ms_records;
    if (!WorldStatePolicy::FitsInOnePacket(data.size()))
    {
        // a FULL split into parts waits until an entry needs it (design 3.5); RECORDS are never bigger than the FULL
        state.OnSendRefused();
        if (!slot.bRefusedLogged)
        {
            slot.bRefusedLogged = true;
            LogLine("event=send-refused entry=%s size=%u max=%d (too big for one packet, not sent)", entry.Name(),
                static_cast<unsigned int>(data.size()), WorldStateWire::MAX_DATA_SIZE);
            logger::warn("CWorldStateRegistry: %s is %u bytes, more than one packet carries; not sent", entry.Name(),
                static_cast<unsigned int>(data.size()));
        }
        return;
    }

    SendPacket(entry.Id(), bFull ? WorldStateWire::KIND_FULL : WorldStateWire::KIND_RECORDS, data);
    state.OnSent(ms_captured);
    if (bFull)
    {
        ms_diag.nSentFull++;
    }
    else
    {
        ms_diag.nSentRecords++;
    }

    char value[40];
    LogLine("event=send entry=%s kind=%s size=%u value=%s", entry.Name(), WorldStatePolicy::ToString(send),
        static_cast<unsigned int>(data.size()), Describe(ms_captured, value));
}

// A packet carries its sender's server time (CPacketFactory::Send, relayed unchanged by the server), and the other games'
// packet buffers hold it until their own server time has passed it (CPacketBuffer::Process). Until a game's first
// SERVER_TIME_REQUEST answer (asked for once it is authenticated, answered about a round trip later) its server time is
// its own GetTickCount, the PC's uptime, while the server counts from its own start: whatever it sends in that time
// waits in the other buffers until the server's clock reaches that uptime (with server.exe on the host's PC: as long as
// that PC had been up when the server started). A story host that connects while the others wait becomes the host in
// its first frame, so the host's sends wait for that answer and what is due stays due. CServerTime::RecalculateOffset
// sets m_serverTimeOffset from 0 on the first answer (an offset of exactly 0 would only mean sending at once, as before).
bool IsServerClockSynced()
{
    return CServerTime::m_serverTimeOffset != 0;
}

void ProcessHost(uint32_t now)
{
    if (ms_table.UpdateHostRestart(CSessionLifecycle::IsLocalGameReady(), now))
    {
        LogLine("event=restart-full (the game ran %u ms after its restart: every entry in full)",
            WorldStatePolicy::RESTART_FULL_DELAY_MS);
    }

    ms_diag.bClockSynced = IsServerClockSynced();
    if (!ms_diag.bClockSynced)
    {
        if (!ms_bWaitingForClock)
        {
            ms_bWaitingForClock = true;
            LogLine("event=waiting-for-server-clock (nothing is sent before the first server time answer)");
        }
        return;
    }
    if (ms_bWaitingForClock)
    {
        ms_bWaitingForClock = false;
        LogLine("event=server-clock-synced offset=%d", CServerTime::m_serverTimeOffset);
    }

    for (size_t i = 0; i < ms_slots.size(); i++)
    {
        SEntrySlot& slot = ms_slots[i];
        if (!slot.bEnabled || (!ms_table.Entry(i).IsFullDue() && !IsDue(slot, now)))
        {
            continue;
        }
        MarkRun(slot, now);
        PollHostEntry(i);
    }
}

// ---- the other games ----

// If the gate is open and the local state differs from the host's, make it match (design 3.1: every IntervalMs and
// right after a packet).
void CheckEntry(size_t index, const SGateInput& gateInput, bool bAfterPacket)
{
    SEntrySlot& slot = ms_slots[index];
    CWorldStateEntry& entry = *slot.pEntry;
    CWorldStateEntryState& state = ms_table.Entry(index);

    const eGate gate = WorldStatePolicy::DecideGate(gateInput, entry.SuspendDuringOwnMission());
    ms_diag.lastGate = gate;
    if (gate != eGate::OPEN || !entry.Capture(ms_captured))
    {
        return;
    }

    switch (state.Check(ms_captured))
    {
        case eCheck::NOTHING:
        case eCheck::DISABLED:
            return;
        case eCheck::LAYOUT_MISMATCH:
        {
            ms_diag.nLayoutMismatches++;
            SLayout local;
            WorldStatePolicy::ReadLayout(ms_captured, local);
            const SLayout& host = state.GetRejectedLayout();
            LogLine("event=layout-mismatch entry=%s host=%u/%08X local=%u/%08X size=%u (entry off on this game)",
                entry.Name(), host.recordCount, host.hash, local.recordCount, local.hash,
                static_cast<unsigned int>(ms_captured.size()));
            logger::warn("CWorldStateRegistry: %s differs between the host's game and this one, turned off", entry.Name());
            return;
        }
        case eCheck::APPLY:
            break;
    }

    entry.Apply(state.GetDesired(), ms_captured);
    const bool bConverged = entry.Capture(ms_after) && ms_after == state.GetDesired();

    const uint32_t now = GetTickCount();
    ms_diag.nApplies++;
    ms_diag.szLastApply = entry.Name();
    ms_diag.nLastApplyTick = now;
    if (!bConverged)
    {
        ms_diag.nNotConverged++;
    }

    slot.nAppliesSinceLog++;
    if (bAfterPacket || !slot.bApplyLogged || now - slot.nLastApplyLog >= APPLY_LOG_INTERVAL_MS)
    {
        char before[40];
        char desired[40];
        LogLine("event=apply entry=%s why=%s local=%s desired=%s converged=%d applies=%u", entry.Name(),
            bAfterPacket ? "packet" : "reapply", Describe(ms_captured, before), Describe(state.GetDesired(), desired),
            bConverged, slot.nAppliesSinceLog);
        slot.bApplyLogged = true;
        slot.nLastApplyLog = now;
        slot.nAppliesSinceLog = 0;
    }
}

void ProcessOthers(uint32_t now)
{
    const SGateInput gateInput = MakeGateInput();
    for (size_t i = 0; i < ms_slots.size(); i++)
    {
        SEntrySlot& slot = ms_slots[i];
        if (!slot.bEnabled || !ms_table.Entry(i).HasDesired() || !IsDue(slot, now))
        {
            continue;
        }
        MarkRun(slot, now);
        CheckEntry(i, gateInput, false);
    }
}

// ---- lifecycle ----

void OnSessionEvent(const SSessionEvent& event)
{
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            if (CSessionLifecycle::IsHost())
            {
                ms_table.OnPlayerJoined();
                LogLine("event=player-joined player=%d takeover=%d (every entry in full)", event.playerId,
                    event.bTakeover);
            }
            break;
        case eSessionEvent::HOST_CHANGED:
            ms_table.OnHostChanged(event.bIsLocal, event.newHostId);
            LogLine("event=host-changed host=%d->%d local=%d->%d (%s)", event.oldHostId, event.newHostId,
                event.bWasLocal, event.bIsLocal,
                event.bIsLocal ? "this game sends every entry in full"
                               : "the new host's state is awaited, the world stays as it is");
            break;
        case eSessionEvent::DISCONNECTED:
            ms_table.OnDisconnected();
            LogLine("event=disconnected (the world stays as it is)");
            break;
        case eSessionEvent::GAME_RESTARTED:
            ms_table.OnGameRestarted(CTimer::m_FrameCounter, CSessionLifecycle::IsHost());
            LogLine("event=game-restarted loaded=%d%s", event.bLoaded,
                CSessionLifecycle::IsHost() ? " (every entry in full once the game has run for a while)" : "");
            break;
        case eSessionEvent::SAVE_LOADED:
            LogLine("event=save-loaded file=%s", event.saveFileName != nullptr ? event.saveFileName : "");
            break;
        default:
            break;
    }
}

// Sets the registry up on first use: Main.cpp's frame call, or a WORLD_STATE handled before it in the same frame (the
// packet handlers run before the frame event). Not from CCore::Init, which runs during the DLL's static initialization
// (CoopAndreasPlugin is a global object), possibly before CSessionLifecycle's subscriber list is constructed. The first
// frame of a session can handle PLAYER_HANDSHAKE and PLAYER_ASSIGN_HOST before this: a game that is the host already
// does what becoming the host and a restart would have done.
void EnsureStarted()
{
    if (ms_bStarted)
    {
        return;
    }
    ms_bStarted = true;

    CreateEntries();
    ms_table.Resize(ms_slots.size());
    for (size_t i = 0; i < ms_slots.size(); i++)
    {
        SEntrySlot& slot = ms_slots[i];
        CWorldStateEntry& entry = *slot.pEntry;
        const bool bDuplicate = FindSlot(entry.Id()) != static_cast<int>(i);
        slot.bEnabled = !bDuplicate && entry.Init();
        if (!slot.bEnabled)
        {
            ms_diag.nOffAtStart++;
        }
        LogLine("event=entry id=%u name=%s intervalMs=%u recordSize=%u suspendDuringOwnMission=%d enabled=%d%s",
            entry.Id(), entry.Name(), entry.IntervalMs(), entry.RecordSize(), entry.SuspendDuringOwnMission(),
            slot.bEnabled, bDuplicate ? " (wire id used twice)" : "");
    }
    ms_diag.nEntries = static_cast<unsigned int>(ms_slots.size());
    ms_diag.bStarted = true;

    CSessionLifecycle::Subscribe(OnSessionEvent);

    if (CSessionLifecycle::IsHost())
    {
        ms_table.OnHostChanged(true, CSessionLifecycle::GetMyId());
        ms_table.OnGameRestarted(CTimer::m_FrameCounter, true);
        LogLine("event=started-as-host (every entry in full now and once the game has run for a while)");
    }
}

void Drop(const Packets::World::WorldState& packet, const char* reason)
{
    ms_diag.nDropped++;
    LogLine("event=drop entry=%u kind=%u size=%d sender=%d reason=%s", packet.entry, packet.kind, packet.size,
        packet.playerid.value, reason);
}
}  // namespace

void CWorldStateRegistry::Process()
{
    EnsureStarted();

    if (!CSessionLifecycle::IsAuthenticated())
    {
        ms_diag.szRole = "none";
        return;
    }

    const uint32_t now = GetTickCount();
    if (CSessionLifecycle::IsHost())
    {
        ms_diag.szRole = "host";
        ProcessHost(now);
    }
    else if (CSessionLifecycle::HasHost())
    {
        ms_diag.szRole = "other";
        ProcessOthers(now);
    }
    else
    {
        ms_diag.szRole = "none";
    }
}

void CWorldStateRegistry::HandlePacket(const Packets::World::WorldState& packet)
{
    EnsureStarted();

    // The host captures and sends, it never applies. With a host known only its packets count: a former host's packets
    // still in the packet buffer (they wait 50 ms there, its PLAYER_DISCONNECTED and the next PLAYER_ASSIGN_HOST don't)
    // are dropped. With none known, the packet is kept with its sender and the next PLAYER_ASSIGN_HOST decides
    // (CWorldStateEntryState::KeepOnlyFrom).
    const int sender = packet.playerid.value;
    if (CSessionLifecycle::IsHost() || (CSessionLifecycle::HasHost() && sender != CSessionLifecycle::GetHostId()))
    {
        Drop(packet, "not-from-host");
        return;
    }

    const int found = FindSlot(packet.entry);
    if (found < 0 || !ms_slots[static_cast<size_t>(found)].bEnabled)
    {
        Drop(packet, found < 0 ? "unknown-entry" : "entry-off");
        return;
    }

    const size_t index = static_cast<size_t>(found);
    SEntrySlot& slot = ms_slots[index];
    CWorldStateEntryState& state = ms_table.Entry(index);
    const uint16_t recordSize = slot.pEntry->RecordSize();
    const size_t size = static_cast<size_t>(packet.size);
    const bool bFull = packet.kind == WorldStateWire::KIND_FULL;
    const eReceive result = bFull ? state.OnFull(sender, packet.data, size, recordSize)
                                  : state.OnRecords(sender, packet.data, size, recordSize);
    if (result != eReceive::ACCEPTED)
    {
        Drop(packet, WorldStatePolicy::ToString(result));
        return;
    }

    if (bFull)
    {
        ms_diag.nReceivedFull++;
    }
    else
    {
        ms_diag.nReceivedRecords++;
    }
    char value[40];
    LogLine("event=receive entry=%s kind=%s size=%d sender=%d value=%s", slot.pEntry->Name(),
        bFull ? "full" : "records", packet.size, sender, Describe(state.GetDesired(), value));

    CheckEntry(index, MakeGateInput(), true);
}

void CWorldStateRegistry::WriteDiagSummary(FILE* file)
{
    fprintf(file,
        "  world-state: started=%d role=%s clockSynced=%d entries=%u offAtStart=%u layoutMismatches=%u sent=F%u/R%u "
        "received=F%u/R%u dropped=%u applies=%u notConverged=%u gate=%s lastApply=%s@%u\n",
        ms_diag.bStarted, ms_diag.szRole, ms_diag.bClockSynced, ms_diag.nEntries, ms_diag.nOffAtStart,
        ms_diag.nLayoutMismatches, ms_diag.nSentFull, ms_diag.nSentRecords, ms_diag.nReceivedFull,
        ms_diag.nReceivedRecords, ms_diag.nDropped, ms_diag.nApplies, ms_diag.nNotConverged,
        WorldStatePolicy::ToString(ms_diag.lastGate), ms_diag.szLastApply, ms_diag.nLastApplyTick);
}
