#include "stdafx.h"
#include "CWorldStateEntries.h"
#include "CWorldStateDoorsPolicy.h"
#include "CWorldStateRegistry.h"
#include "CSessionLifecycle.h"
#include "CEntryExitMarkerSync.h"
#include "CEntryExitManager.h"
#include "CEntryExit.h"
#include <CTimer.h>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <vector>

// Private build: the world-state registry's doors and garages, see CWorldStateEntries.h (docs/design/world-state.md W5,
// W10, 3.2; decision W-D7). The logic without game calls is in CWorldStateDoorsPolicy.h
// (tests/world_state_doors_test.cpp).

using namespace WorldStatePolicy;
using namespace WorldStateDoorsPolicy;

namespace
{
// --coopdiag: what the doors and garages did, in CoopAndreas_world_state.log next to gta_sa.exe (the registry's own
// lines are there too), at most 256 records of this file and while the log is under 1 MB
void LogLine(const char* format, ...)
{
    static const bool enabled = strstr(GetCommandLineA(), "--coopdiag") != nullptr;
    static unsigned int records = 0;
    if (!enabled || records >= 256)
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
        fprintf(file, "t=%u frame=%u hostId=%d %s\n", GetTickCount(), CTimer::m_FrameCounter,
            CSessionLifecycle::GetHostId(), text);
        ++records;
    }
    fclose(file);
}

// ---- garages ----
//
// CGarages in gta_sa.exe 1.0 US (plugin-sdk has no garage functions; gta-reversed Garages.h/Garage.h names them, the
// bytes below are read with dumpbin). The functions are `jmp` stubs into the .HOODLUM section:
// - NumGarages 0x96C024 (int32): the loop bound of GetGarageNumberByName, `mov eax,[0096C024h]` at 0x156DCF0.
// - aGarages 0x96C048, 50 garages of 0xD8 bytes: every body below indexes it as `imul eax,eax,0D8h; add eax,96C048h`;
//   type at +0x4C, door state +0x4D, flags +0x4E (0x02 inactive), name at +0x44 (8 bytes; GetGarageNumberByName
//   compares `add eax,96C08Ch`).
// - GetGarageNumberByName(const char*) 0x447680 -> 0x156DCF0: _stricmp over the names of the first NumGarages
//   garages, -1 when none matches. The entry reads the names itself, for the layout hash and Michelle's garage.
// - ChangeGarageType(int16 garage, uint8 type, uint32 unused) 0x4476D0 -> 0x1561690, cdecl: writes the type, then
//   opens a closed door for types 2..5 (door state 1, door position +0x38 = 1.0), leaves Burglary (43) alone and
//   closes the door (state 0, position 0) for any other type; the flags stay. The 02FA handler calls it at 0x480A97
//   with (garage, ScriptParams[0] as a byte, 0), then `add esp,0Ch`.
// - ActivateGarage(int16) 0x447CD0 -> 0x15677F0, cdecl: `and byte ptr [eax+4Eh],0FDh`, and a closed door of type 11
//   starts opening; the 0299 handler calls it at 0x47F4DD.
// - DeActivateGarage(int16) 0x447CB0 -> 0x1563E70, cdecl: `or byte ptr [eax+4Eh],2`; the 02B9 handler calls it at
//   0x47F999.
// Nothing else in the exe calls these three directly. A new game or a load resets every type to the garage's original
// one (CGarages::Init_AfterRestart 0x448B60 copies +0x4F to +0x4C and leaves the flags); a load then copies type, door
// and flags from the save (gta-reversed CGarages::Load 0x5D3270).
constexpr uintptr_t ADDRESS_NUM_GARAGES = 0x96C024;
constexpr uintptr_t ADDRESS_GARAGES = 0x96C048;
constexpr size_t GARAGE_SIZE = 0xD8;
constexpr size_t GARAGE_NAME_OFFSET = 0x44;  // GARAGE_NAME_SIZE (8) bytes
constexpr size_t GARAGE_TYPE_OFFSET = 0x4C;
constexpr size_t GARAGE_FLAGS_OFFSET = 0x4E;

int GetGarageCount()
{
    return *reinterpret_cast<const int32_t*>(ADDRESS_NUM_GARAGES);
}

const uint8_t* GetGarage(int index)
{
    return reinterpret_cast<const uint8_t*>(ADDRESS_GARAGES + static_cast<size_t>(index) * GARAGE_SIZE);
}

const char* GetGarageName(int index)
{
    return reinterpret_cast<const char*>(GetGarage(index) + GARAGE_NAME_OFFSET);
}

// a name for the log (m_anName need not be terminated within its 8 bytes)
void CopyGarageName(int index, char (&name)[GARAGE_NAME_SIZE + 1])
{
    memcpy(name, GetGarageName(index), GARAGE_NAME_SIZE);
    name[GARAGE_NAME_SIZE] = '\0';
}

void ChangeGarageType(int garage, uint8_t type)
{
    plugin::Call<0x4476D0, int, int, int>(garage, type, 0);
}

void ActivateGarage(int garage)
{
    plugin::Call<0x447CD0, int>(garage);
}

void DeActivateGarage(int garage)
{
    plugin::Call<0x447CB0, int>(garage);
}

// a garage's log lines: an unknown type once, applies at most every LOG_INTERVAL_MS (a script of this game that keeps
// changing a garage back would otherwise fill the log)
constexpr uint32_t LOG_INTERVAL_MS = 10000;
bool ms_abUnknownTypeLogged[GARAGE_COUNT_MAX]{};
bool ms_abGarageLogged[GARAGE_COUNT_MAX]{};
uint32_t ms_anGarageLastLog[GARAGE_COUNT_MAX]{};
unsigned int ms_anGarageAppliesSinceLog[GARAGE_COUNT_MAX]{};

// ---- entry-exits ----
//
// CEntryExitManager and CEntryExit in gta_sa.exe 1.0 US (plugin-sdk CEntryExitManager.cpp and CEntryExit.cpp, checked
// in the bytes with dumpbin):
// - mp_poolEntryExits 0x96A7D8: CPool<CEntryExit> of 400 (CEntity::DestroyEffects reads it at 0x533CD8).
// - mp_Active 0x96A7D4: the marker of the transition in progress. CEntryExitManager::Update (0x440D10) sets it when
//   CEntryExit::TransitionStarted returns true (0x44109B), calls CEntryExit::TransitionFinished on it every frame
//   (0x440F79, 0x440F89) and clears it when that returns true (0x440F96).
// - CEntryExit::ms_spawnPoint 0x96A7C0: where the transition ends; TransitionFinished reads its flags (0x4409D8,
//   0x440A12, 0x440A37).
// TransitionFinished reads the flags of both every frame, so the two markers of a transition of this game's player are
// left alone until it is over.
static_assert(sizeof(SEntryExitFlags) == sizeof(uint16_t), "CEntryExit::m_nFlags is 16 bits");

uint16_t ToBits(const SEntryExitFlags& flags)
{
    uint16_t bits = 0;
    memcpy(&bits, &flags, sizeof(bits));
    return bits;
}

void SetBits(SEntryExitFlags& flags, uint16_t bits)
{
    memcpy(&flags, &bits, sizeof(bits));
}

// upstream's identity of a marker (CEntryExitMarkerSync::Send and Receive use the same expressions)
uint64_t KeyOf(const CEntryExit* pEntryExit)
{
    return MakeEnExKey(static_cast<int16_t>(std::floor(pEntryExit->m_recEntrance.left)),
        static_cast<int16_t>(std::floor(pEntryExit->m_recEntrance.bottom)), pEntryExit->m_nArea);
}

bool IsTransitionRunning()
{
    return CEntryExitManager::mp_Active != nullptr;
}

bool IsInTransition(const CEntryExit* pEntryExit)
{
    return IsTransitionRunning() &&
           (pEntryExit == CEntryExitManager::mp_Active || pEntryExit == CEntryExit::ms_spawnPoint);
}

// A script of this game that switches entry-exits for its own player runs (IsOwnEnExScript: the girlfriend date, the
// mod garage). CTheScripts::pActiveScripts 0xA8B42C, CRunningScript::m_szName +0x08 and m_pNext +0 (plugin-sdk; the
// list CSessionLifecycle::IsLocalMissionRunning walks).
bool IsOwnEnExScriptRunning(char (&name)[9])
{
    for (CRunningScript* pScript = CTheScripts::pActiveScripts; pScript != nullptr; pScript = pScript->m_pNext)
    {
        if (IsOwnEnExScript(pScript->m_szName, sizeof(pScript->m_szName)))
        {
            memcpy(name, pScript->m_szName, sizeof(pScript->m_szName));
            name[sizeof(pScript->m_szName)] = '\0';
            return true;
        }
    }
    return false;
}

// the other games: the re-apply waits during a transition of this game's player and while one of its own entry-exit
// scripts runs (logged when the wait for a script starts)
bool MustWait()
{
    if (IsTransitionRunning())
    {
        return true;
    }
    static bool waitingForScript = false;
    char name[9];
    if (!IsOwnEnExScriptRunning(name))
    {
        waitingForScript = false;
        return false;
    }
    if (!waitingForScript)
    {
        waitingForScript = true;
        LogLine("event=enex-wait script=%s (this game's own entry-exit script runs, the re-apply follows afterwards)",
            name);
    }
    return true;
}

SEnExGlobals GetLocalGlobals()
{
    SEnExGlobals globals;
    globals.bDisabled = CEntryExitManager::ms_bDisabled;
    globals.bBurglary = CEntryExitManager::ms_bBurglaryHousesEnabled;
    return globals;
}

// this game's markers, in pool order
std::vector<CEntryExit*> ms_localMarkers;
std::vector<SEnExRecord> ms_localRecords;

void CollectLocal()
{
    ms_localMarkers.clear();
    ms_localRecords.clear();
    if (CEntryExitManager::mp_poolEntryExits == nullptr)
    {
        return;
    }
    for (auto pEntryExit : CEntryExitManager::mp_poolEntryExits)
    {
        SEnExRecord record;
        record.key = KeyOf(pEntryExit);
        record.flags = ToBits(pEntryExit->m_nFlags);
        ms_localMarkers.push_back(pEntryExit);
        ms_localRecords.push_back(record);
    }
}

// a packet's markers as records (the host's last ENEX_SYNC on the other games, what went out on the host)
void IndexPacket(const Packets::Scripts::EnExSync& packet, CEnExIndex& index)
{
    static std::vector<SEnExRecord> records;
    records.clear();
    const int count = std::min(packet.count, static_cast<int>(Packets::Scripts::EnExSync::MAX_ENEX_COUNT));
    for (int i = 0; i < count; i++)
    {
        const Packets::Scripts::_EnExPayload& marker = packet.enexes[i];
        SEnExRecord record;
        record.key = MakeEnExKey(marker.rectLeft, marker.rectBottom, marker.areaId);
        record.flags = ToBits(marker.flags);
        records.push_back(record);
    }
    index.Build(records.data(), records.size());
}

SEnExGlobals GetPacketGlobals(const Packets::Scripts::EnExSync& packet)
{
    SEnExGlobals globals;
    globals.bDisabled = packet.bDisabled;
    globals.bBurglary = packet.bBurglary;
    return globals;
}

// the other games: the host's last ENEX_SYNC
CEnExIndex ms_hostIndex;

// the host: what went out last (CEntryExitMarkerSync::Send), and its own poll's schedule
CEnExIndex ms_sentIndex;
SEnExGlobals ms_sentGlobals;
bool ms_bHasSent = false;
uint32_t ms_nLastHostPoll = 0;
bool ms_bHostPolled = false;

// The host's poll: send upstream's packet again when the flags changed without a script command since it last went out
// (a load writes every flag from the save: gta-reversed CEntryExitManager::Load 0x5D55C0). Script commands send at
// once through upstream's opcode hook (COpCodeSync, after the script frame), which also updates what went out last.
void PollHost(uint32_t intervalMs)
{
    // the registry polls an entry every frame while its FULL is due, and this one never sends a FULL
    const uint32_t now = GetTickCount();
    if (ms_bHostPolled && now - ms_nLastHostPoll < intervalMs)
    {
        return;
    }
    ms_bHostPolled = true;
    ms_nLastHostPoll = now;

    // before anything went out, there is nothing to compare with: the join snapshot (or a script command) sends first
    if (!ms_bHasSent)
    {
        return;
    }
    CollectLocal();
    const SEnExGlobals globals = GetLocalGlobals();
    const size_t changes =
        CountHostChanges(ms_sentIndex, ms_sentGlobals, ms_localRecords.data(), ms_localRecords.size(), globals);
    if (changes == 0)
    {
        return;
    }
    LogLine("event=enex-host-resend changes=%u disabled=%d->%d burglary=%d->%d markers=%u (flags changed without a "
            "script command)",
        static_cast<unsigned int>(changes), ms_sentGlobals.bDisabled, globals.bDisabled, ms_sentGlobals.bBurglary,
        globals.bBurglary, static_cast<unsigned int>(ms_localRecords.size()));
    CEntryExitMarkerSync::Send();
}
}  // namespace

// ---- 4: garages ----
//
// Per garage the type (+0x4C) and the inactive bit (0x02 of the flags at +0x4E), in index order, behind a layout
// header with the garage count and a hash of the names (both games load the same garages from the same IPL files;
// another order turns the entry off instead of writing the wrong garages). Michelle's garage (MICHDR) is always
// {0, 0}: its type and its free resprays come from the player's own girlfriend (GFAGNT). Who changes them (W5): INITIAL
// sets the mod shops' types and deactivates the hideouts, INTRO deactivates the mod shops (both on every game at a new
// game), SWEET6 activates Loco Low Co and the Los Santos Transfender (#10), TRUTH2, STEAL1 and MOB_SF the other mod
// shops, BUYPRO1 the hideouts of bought properties, DSKOOL turns the mod shops off and on again, some missions change a
// type. The doors are not mirrored: each game opens them for its own player.

uint16_t CWorldStateGarages::RecordSize() const
{
    return GARAGE_RECORD_SIZE;
}

bool CWorldStateGarages::Capture(Blob& blob)
{
    const int count = GetGarageCount();
    if (count <= 0 || count > GARAGE_COUNT_MAX)
    {
        // not loaded yet
        return false;
    }

    SGarageState garages[GARAGE_COUNT_MAX];
    for (int i = 0; i < count; i++)
    {
        const uint8_t* garage = GetGarage(i);
        garages[i].name = GetGarageName(i);
        garages[i].type = garage[GARAGE_TYPE_OFFSET];
        garages[i].flags = garage[GARAGE_FLAGS_OFFSET];
    }
    BuildGaragesBlob(blob, garages, static_cast<size_t>(count));
    return true;
}

void CWorldStateGarages::Apply(const Blob& desired, const Blob& local)
{
    // both blobs have this game's layout and size (the registry checked them against Capture's, made just before)
    const int count = GetU16(desired.data());
    for (int i = 0; i < count; i++)
    {
        const SGarageRecord want = GetGarageRecord(desired, static_cast<size_t>(i));
        const SGarageRecord have = GetGarageRecord(local, static_cast<size_t>(i));
        if (want == have)
        {
            continue;
        }

        const SGarageAction action = PlanGarage(want, have);
        if (action.bChangeType)
        {
            ChangeGarageType(i, action.type);
        }
        if (action.bSetActive)
        {
            if (action.bActive)
            {
                ActivateGarage(i);
            }
            else
            {
                DeActivateGarage(i);
            }
        }

        char name[GARAGE_NAME_SIZE + 1];
        CopyGarageName(i, name);
        if (action.bUnknownType && !ms_abUnknownTypeLogged[i])
        {
            ms_abUnknownTypeLogged[i] = true;
            LogLine("event=garage-unknown-type garage=%s index=%d type=%u (not a type 1.0 knows, left as it is)",
                name, i, static_cast<unsigned int>(want.type));
        }
        if (!action.bChangeType && !action.bSetActive)
        {
            continue;
        }
        ms_anGarageAppliesSinceLog[i]++;
        const uint32_t now = GetTickCount();
        if (!ms_abGarageLogged[i] || now - ms_anGarageLastLog[i] >= LOG_INTERVAL_MS)
        {
            LogLine("event=garage-apply garage=%s index=%d type=%u->%u inactive=%u->%u applies=%u", name, i,
                static_cast<unsigned int>(have.type),
                static_cast<unsigned int>(action.bChangeType ? action.type : have.type),
                static_cast<unsigned int>(have.inactive),
                action.bSetActive ? (action.bActive ? 0u : 1u) : static_cast<unsigned int>(have.inactive),
                ms_anGarageAppliesSinceLog[i]);
            ms_abGarageLogged[i] = true;
            ms_anGarageLastLog[i] = now;
            ms_anGarageAppliesSinceLog[i] = 0;
        }
    }
}

// ---- the EntryExits adapter ----
//
// Upstream sends the host's markers (CEntryExitMarkerSync::Send) after any host script runs an entry-exit command
// (COpCodeSync: 00D8, 07FB, 0864, 08E7, 098E, 09B4, 09E6) and in the join snapshot; the server keeps the last one for
// joiners; Receive applies it once. The adapter keeps that and adds:
// - the host: its poll sends again when the flags changed without a command (PollHost);
// - the other games: the registry checks every second, while its gate is open (a host, the game running, no mission of
//   this game, so not during its new-game INTRO), whether a local marker differs from the host's last packet in a bit
//   that follows the host, and puts the host's flags back with this game's transition bits (CWorldStateDoorsPolicy.h,
//   ENEX_LOCAL_FLAGS). Markers are matched by upstream's key; a marker only one game has streamed in is compared once
//   both have it. Nothing happens while this game's player is in a transition or while one of this game's own
//   entry-exit scripts runs (its girlfriend date, its mod garage: IsOwnEnExScript).
// The registry needs a desired blob from the host to check an entry: the adapter's comes with each ENEX_SYNC from the
// current host (OnReceived) instead of a WORLD_STATE packet, so the registry's host check, lifecycle (dropped when the
// host changes or the session ends, kept when this game restarts) and log apply unchanged.

bool CWorldStateEntryExits::Capture(Blob& blob)
{
    if (CSessionLifecycle::IsHost())
    {
        PollHost(IntervalMs());
        // nothing goes out as WORLD_STATE: the host's markers travel in ENEX_SYNC
        return false;
    }
    if (MustWait())
    {
        return false;
    }

    IndexPacket(CEntryExitMarkerSync::ms_lastData, ms_hostIndex);
    CollectLocal();
    const size_t differences = CountDifferences(ms_hostIndex, GetPacketGlobals(CEntryExitMarkerSync::ms_lastData),
        ms_localRecords.data(), ms_localRecords.size(), GetLocalGlobals());
    MakeEnExAdapterBlob(blob, differences);
    return true;
}

void CWorldStateEntryExits::Apply(const Blob&, const Blob&)
{
    if (CSessionLifecycle::IsHost() || MustWait())
    {
        return;
    }

    IndexPacket(CEntryExitMarkerSync::ms_lastData, ms_hostIndex);
    CollectLocal();
    unsigned int changed = 0;
    char names[48]{};
    for (size_t i = 0; i < ms_localRecords.size(); i++)
    {
        uint16_t merged = 0;
        if (!NeedsHostFlags(ms_hostIndex, ms_localRecords[i], merged))
        {
            continue;
        }
        CEntryExit* pEntryExit = ms_localMarkers[i];
        SetBits(pEntryExit->m_nFlags, merged);
        if (changed < 4)
        {
            char name[sizeof(pEntryExit->m_szName) + 1];
            memcpy(name, pEntryExit->m_szName, sizeof(pEntryExit->m_szName));
            name[sizeof(pEntryExit->m_szName)] = '\0';
            const size_t used = strlen(names);
            snprintf(names + used, sizeof(names) - used, "%s%s:%04X->%04X", changed > 0 ? "," : "", name,
                static_cast<unsigned int>(ms_localRecords[i].flags), static_cast<unsigned int>(merged));
        }
        changed++;
    }

    const SEnExGlobals host = GetPacketGlobals(CEntryExitMarkerSync::ms_lastData);
    const SEnExGlobals local = GetLocalGlobals();
    if (host.bDisabled != local.bDisabled)
    {
        CEntryExitManager::ms_bDisabled = host.bDisabled;
    }
    if (host.bBurglary != local.bBurglary)
    {
        CEntryExitManager::ms_bBurglaryHousesEnabled = host.bBurglary;
    }

    // at most every LOG_INTERVAL_MS (a script of this game that keeps switching a marker back would fill the log)
    static bool logged = false;
    static uint32_t lastLog = 0;
    static unsigned int appliesSinceLog = 0;
    appliesSinceLog++;
    const uint32_t now = GetTickCount();
    if (!logged || now - lastLog >= LOG_INTERVAL_MS)
    {
        LogLine("event=enex-reapply markers=%u first=%s disabled=%d->%d burglary=%d->%d applies=%u", changed,
            changed > 0 ? names : "-", local.bDisabled, host.bDisabled, local.bBurglary, host.bBurglary,
            appliesSinceLog);
        logged = true;
        lastLog = now;
        appliesSinceLog = 0;
    }
}

void CWorldStateEntryExits::OnSent(const Packets::Scripts::EnExSync& packet)
{
    IndexPacket(packet, ms_sentIndex);
    ms_sentGlobals = GetPacketGlobals(packet);
    ms_bHasSent = true;
}

void CWorldStateEntryExits::ApplyHostFlags(CEntryExit* pEntryExit, const SEntryExitFlags& hostFlags)
{
    const uint16_t host = ToBits(hostFlags);
    const uint16_t local = ToBits(pEntryExit->m_nFlags);
    if (!EnExFlagsDiffer(host, local))
    {
        return;
    }
    if (IsInTransition(pEntryExit))
    {
        // TransitionFinished reads these flags every frame until the transition is over; the re-apply follows then
        static bool logged = false;
        static uint32_t lastLog = 0;
        if (!logged || GetTickCount() - lastLog >= LOG_INTERVAL_MS)
        {
            logged = true;
            lastLog = GetTickCount();
            char name[sizeof(pEntryExit->m_szName) + 1];
            memcpy(name, pEntryExit->m_szName, sizeof(pEntryExit->m_szName));
            name[sizeof(pEntryExit->m_szName)] = '\0';
            LogLine("event=enex-deferred marker=%s flags=%04X host=%04X (in this game's transition)", name,
                static_cast<unsigned int>(local), static_cast<unsigned int>(host));
        }
        return;
    }
    SetBits(pEntryExit->m_nFlags, MergeEnExFlags(host, local));
}

void CWorldStateEntryExits::OnReceived()
{
    // only from the current host (the server relays ENEX_SYNC only from the host); without one known the next ENEX_SYNC
    // starts the re-apply
    if (CSessionLifecycle::IsHost() || !CSessionLifecycle::HasHost())
    {
        return;
    }

    static Packets::World::WorldState packet;  // 8 KB of data, kept off the stack
    static Blob blob;
    MakeEnExAdapterBlob(blob, 0);
    packet.playerid.value = CSessionLifecycle::GetHostId();
    packet.entry = LOCAL_ID;
    packet.kind = WorldStateWire::KIND_FULL;
    packet.size = static_cast<int>(blob.size());
    memcpy(packet.data, blob.data(), blob.size());
    CWorldStateRegistry::HandlePacket(packet);
}
