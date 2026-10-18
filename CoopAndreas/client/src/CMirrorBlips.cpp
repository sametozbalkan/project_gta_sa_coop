#include "stdafx.h"
#include "CMirrorBlips.h"
#include "CMirrorBlipsPolicy.h"
#include "CMirrorObjects.h"
#include "CMissionMirror.h"
#include "CNetworkStaticBlip.h"
#include "CServerTime.h"
#include "CSessionLifecycle.h"
#include "CTrainSync.h"
#include "network/packets/mirror_blips.h"
#include <CEntryExit.h>
#include <CEntryExitManager.h>
#include <CPickups.h>
#include <CPools.h>
#include <CTimer.h>
#include <cstdarg>

// Private build: the mission mirror layer's blips, see CMirrorBlips.h.
//
// gta_sa.exe 1.0 US used below:
// - The creating commands (design risk 6; cases found through CRunningScript::CommandHandlerTable 0x8A6168, read
//   with dumpbin): each case ends with `mov [0A43C78h],reg` (ScriptParams[0]) and CRunningScript::StoreParameters
//   0x464370 with 1, so the handle is in ScriptParams[0] when CRunningScript::Process's command call returns: 0161
//   case 0x47C610 (store at 0x47C64C), 0167 0x47C6AA (0x47C750), 0186 0x47CEBF, 0187 0x47CEFD, 0188 0x47CF25 and
//   018A 0x47CF4E (shared store at 0x47CFF6), 02A7 0x47F6BD and 02A8 0x47F75D (0x47F80A), 03DC 0x4854B9
//   (0x485504), 04CE 0x48BC0A (0x48BCB4), 0570 0x48DB3D (0x48DBED), 06C4 0x497BC7 (0x497C14; jump table 0x498F10
//   of ProcessCommands1700To1799 0x496E00), 0888 0x471D2B (0x471D68). A handle is CRadar's (trace index | counter
//   << 16), -1 when no trace was free (CRadar::SetCoordBlip 0x583820, SetEntityBlip 0x5839A0).
//   CRadar::GetActualBlipArrayIndex 0x582870 checks the counter and the in-use bit.
// - tRadarTrace (plugin-sdk CRadar.h, checked against the 1.0 setters with dumpbin; the static_asserts below):
//   colour u32 +0x00 (ChangeBlipColour 0x583AB0), entity handle +0x04 (SetEntityBlip), position +0x08
//   (SetCoordBlip), counter u16 +0x14, size u16 +0x1C (ChangeBlipScale 0x583CC0), CEntryExit* +0x20
//   (SetBlipEntryExit 0x583F00), sprite +0x24 (SetBlipSprite 0x583D70). The byte at +0x25: bright 0x01, in use
//   0x02, short range 0x04 (SetShortRangeCoordBlip 0x583920), friendly 0x08 (SetBlipFriendly 0x583EB0), remain
//   0x10 (SetBlipAlwaysDisplayInZoom 0x583DB0), fade 0x20 (SetBlipFade 0x583E00), coordinate appearance 0xC0
//   (SetCoordBlipAppearance 0x583E50, which changes BLIP_COORD traces only: it compares the type bits with 0x10;
//   gta-reversed's BLIP_CAR test and m_nAppearance field are wrong). The byte at +0x26: display bits 0-1
//   (ChangeBlipDisplay 0x583D20), type bits 2-5. The setters write a trace only while it is in use; the fields are
//   written here directly, as CNetworkStaticBlip::Create does, after GetActualBlipArrayIndex checked that.
// - Where the radar draws an entity blip (CRadar::DrawEntityBlip 0x587000, jump table 0x587BEC by type): car
//   0x58703C at the vehicle; char 0x58706F at the ped, or at its vehicle while it is in one (CPed::m_nPedFlags
//   +0x46C bit 0x100, m_pVehicle +0x58C); object 0x58709C; searchlight 0x5870B0 at
//   CTheScripts::ScriptSearchLightArray (0xA94D68, 0x7C each, index CTheScripts::GetActualScriptThingIndex(handle,
//   2) 0x4839A0) + 0x14 (plugin-sdk tScriptSearchlight::target); pickup 0x5870F6 at CPickups::aPickUps (0x9788C0,
//   0x20 each, index CPickups::GetActualPickupIndex 0x4552A0) through CPickup::GetPosn 0x4549A0. Coordinate types
//   are drawn by DrawCoordBlip at the trace position, moved by its entrance.
// - Entrances: set_blip_entry_exit 08DC (case 0x475838) calls CEntryExitManager::FindNearestEntryExit 0x43F4B0
//   with (&{x, y}, radius, 0), CEntryExitManager::GetEntryExit 0x43EF00 on its result and SetBlipEntryExit at
//   0x475882. FindNearestEntryExit measures from the middle of CEntryExit::m_recEntrance (floats at +0x08..+0x14,
//   0x43F56A) and takes an entrance closer than twice the range; ignoreArea 0 skips entrances that lead to the
//   outside world.
// - Entity destructors clear their blips (gta-reversed ~CPed, ~CVehicle, ~CObject call CRadar::ClearBlipForEntity
//   0x587C60), so an entity blip whose entity is gone stops resolving: removed on the host, rebuilt on the others.
// - A train: CTrain's constructor sets CVehicle::m_nVehicleSubType to VEHICLE_TRAIN (gta-reversed Train.cpp); the
//   other games don't create trains (CNetworkVehicle::CreateVehicle), so a train blip always follows the host's
//   position.

static_assert(offsetof(tRadarTrace, m_nColour) == 0x00, "tRadarTrace::m_nColour");
static_assert(offsetof(tRadarTrace, m_nEntityHandle) == 0x04, "tRadarTrace::m_nEntityHandle");
static_assert(offsetof(tRadarTrace, m_vecPos) == 0x08, "tRadarTrace::m_vecPos");
static_assert(offsetof(tRadarTrace, m_nCounter) == 0x14, "tRadarTrace::m_nCounter");
static_assert(offsetof(tRadarTrace, m_nBlipSize) == 0x1C, "tRadarTrace::m_nBlipSize");
static_assert(offsetof(tRadarTrace, m_pEntryExit) == 0x20, "tRadarTrace::m_pEntryExit");
static_assert(offsetof(tRadarTrace, m_nRadarSprite) == 0x24, "tRadarTrace::m_nRadarSprite");
static_assert(sizeof(tRadarTrace) == 0x28, "tRadarTrace");
static_assert(MirrorBlipsPolicy::MAX_BLIPS == MAX_RADAR_TRACES, "one owned or mirrored blip per radar trace at most");
static_assert(MirrorBlipsPolicy::Type::CAR == BLIP_CAR && MirrorBlipsPolicy::Type::CHAR == BLIP_CHAR &&
                  MirrorBlipsPolicy::Type::OBJECT == BLIP_OBJECT && MirrorBlipsPolicy::Type::COORD == BLIP_COORD &&
                  MirrorBlipsPolicy::Type::CONTACT_POINT == BLIP_CONTACTPOINT &&
                  MirrorBlipsPolicy::Type::SPOTLIGHT == BLIP_SPOTLIGHT &&
                  MirrorBlipsPolicy::Type::PICKUP == BLIP_PICKUP && MirrorBlipsPolicy::Type::AIRSTRIP == BLIP_AIRSTRIP,
    "eBlipType");
static_assert(MirrorBlipsPolicy::Type::CAR == MirrorBlipsWire::TYPE_FIRST &&
                  MirrorBlipsPolicy::Type::AIRSTRIP == MirrorBlipsWire::TYPE_LAST,
    "the wire's blip types");
static_assert(static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::NONE) == MirrorBlipsWire::ENTITY_NONE &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::PLAYER) == MirrorBlipsWire::ENTITY_PLAYER &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::PED) == MirrorBlipsWire::ENTITY_PED &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::VEHICLE) == MirrorBlipsWire::ENTITY_VEHICLE &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::OBJECT) == MirrorBlipsWire::ENTITY_OBJECT,
    "the wire's entity kinds");
static_assert(Config::MAX_SERVER_PLAYERS <= 32, "one bit per player in the pending join snapshots");

namespace
{
using MirrorBlipsPolicy::eEntityKind;
using MirrorBlipsPolicy::eLocalForm;
using MirrorBlipsPolicy::eSend;
using MirrorBlipsPolicy::SBlipState;
using MirrorBlipsPolicy::SMirroredBlip;
using MirrorBlipsPolicy::SOwnedBlip;

// how often a receiver checks every mirrored blip (an entity that resolved, a local blip the game cleared)
constexpr uint32_t RECONCILE_INTERVAL_MS = 250;
// FindNearestEntryExit range for a mirrored entrance: the host sends the exact middle of the entrance rectangle
constexpr float ENTRANCE_SEARCH_RANGE = 1.0f;
// set_blip_entry_exit's ignoreArea (the 08DC case pushes 0)
constexpr int ENTRANCE_IGNORE_AREA = 0;

// host: the blips the mission family created
MirrorBlipsPolicy::COwnedBlips ms_owned;
// receivers: the host's blips and this game's copies of them
MirrorBlipsPolicy::CMirroredBlips ms_mirrored;

bool ms_bStarted = false;
uint32_t ms_nLastFrame = 0;          // CTimer::m_FrameCounter at the last call (a smaller one = the game restarted)
bool ms_bRestartSeen = false;        // CheckRestart saw a restart in frame ms_nRestartFrame (GAME_RESTARTED follows)
uint32_t ms_nRestartFrame = 0;
uint32_t ms_nLastReconcile = 0;      // GetTickCount
bool ms_bReconcileNow = false;
bool ms_bBroadcastPending = false;   // host: its whole set with bReset to everyone (after its game restarted)
uint32_t ms_nSnapshotsPending = 0;   // host: join snapshots still to send, one bit per player id

// --coopdiag (plain values; the diag thread reads them)
struct SCounters
{
    unsigned int owned = 0;           // host: owned now
    unsigned int ownedTotal = 0;
    unsigned int ownFailures = 0;     // a family's creating command left no handle (no free trace), or the set is full
    unsigned int sentFull = 0;
    unsigned int sentPosition = 0;
    unsigned int sentRemoved = 0;
    unsigned int sentPackets = 0;
    unsigned int snapshots = 0;       // join snapshots
    unsigned int broadcasts = 0;      // the whole set to everyone with bReset (restart, load)
    unsigned int captureFailures = 0; // an owned entity blip whose entity wasn't found in a frame
    unsigned int staticSkipped = 0;   // host: owned traces left out of the static blip snapshot
    unsigned int mirrored = 0;        // receiver: mirrored now
    unsigned int formEntity = 0;
    unsigned int formFollow = 0;
    unsigned int formCoord = 0;
    unsigned int formNone = 0;
    unsigned int recvPackets = 0;
    unsigned int recvDropped = 0;     // not from the current host
    unsigned int recvResets = 0;
    unsigned int recvFull = 0;
    unsigned int recvPosition = 0;
    unsigned int recvRemoved = 0;
    unsigned int recvUnknown = 0;     // a position or removal for a blip this game doesn't have
    unsigned int rebuilds = 0;
    unsigned int switches = 0;        // follow -> entity blip
    unsigned int createFailures = 0;  // no free radar trace for a local blip
    unsigned int staticKept = 0;      // receiver: mirror traces the static blip snapshot left alone
};
SCounters ms_counters;

// --coopdiag: CoopAndreas_mirror.log next to gta_sa.exe (CMissionMirror writes there too), at most 512 records of this
// module and 1 MB
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
        fprintf(file, "t=%u frame=%u blips %s host=%d hostId=%d owned=%d mirrored=%d\n", GetTickCount(),
            CTimer::m_FrameCounter, text, CSessionLifecycle::IsHost(), CSessionLifecycle::GetHostId(), ms_owned.Count(),
            ms_mirrored.Count());
        ++records;
    }
    fclose(file);
}

const char* FormName(eLocalForm form)
{
    switch (form)
    {
        case eLocalForm::ENTITY: return "entity";
        case eLocalForm::FOLLOW: return "follow";
        case eLocalForm::COORD: return "coord";
        default: return "none";
    }
}

bool IsServerClockSynced()
{
    // packets sent before the first server-time answer carry the PC's uptime as their stamp and wait in the other
    // games' packet buffers (see CWorldStateRegistry); CServerTime::RecalculateOffset sets the offset from 0 on that
    // answer
    return CServerTime::m_serverTimeOffset != 0;
}

// ---- the host ----

bool IsValidNetworkId(eEntityKind kind, int id)
{
    switch (kind)
    {
        case eEntityKind::PLAYER: return id >= 0 && id < Config::MAX_SERVER_PLAYERS;
        case eEntityKind::PED: return id >= 0 && id < Config::MAX_SERVER_PEDS;
        case eEntityKind::VEHICLE: return id >= 0 && id < Config::MAX_SERVER_VEHICLES;
        case eEntityKind::OBJECT: return id >= 0 && id <= MirrorBlipsWire::OBJECT_ID_MAX;
        default: return false;
    }
}

void SetEntity(SBlipState& state, eEntityKind kind, int id)
{
    if (IsValidNetworkId(kind, id))
    {
        state.entityKind = kind;
        state.entityId = id;
    }
}

// a ped's network identity by membership in the lists: a player's id, or a confirmed CNetworkPed's (a ped still
// waiting for PED_CONFIRM is only in CNetworkPedManager::m_apTempPeds and reads id 0, design 3.10)
void SetPedEntity(SBlipState& state, CPed* pPed)
{
    if (pPed->IsPlayer())
    {
        if (pPed == FindPlayerPed(0))
        {
            SetEntity(state, eEntityKind::PLAYER, CNetworkPlayerManager::m_nMyId);
        }
        else if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed))
        {
            SetEntity(state, eEntityKind::PLAYER, pNetworkPlayer->m_iPlayerId);
        }
        return;
    }
    if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed))
    {
        SetEntity(state, eEntityKind::PED, pNetworkPed->m_nPedId);
    }
}

void SetPosition(SBlipState& state, const CVector& position)
{
    state.x = position.x;
    state.y = position.y;
    state.z = position.z;
}

// The owned blip in trace `index` as the other games should show it; false when its entity isn't there this frame
// (nothing is sent for it then).
bool CaptureBlip(int index, SBlipState& state)
{
    const tRadarTrace& trace = CRadar::ms_RadarTrace[index];
    state = SBlipState{};
    state.type = static_cast<uint8_t>(trace.m_nBlipType);
    if (!MirrorBlipsPolicy::IsValidType(state.type))
    {
        return false;
    }
    state.colour = trace.m_nColour;
    state.sprite = trace.m_nRadarSprite < 64 ? trace.m_nRadarSprite : 0;
    state.display = static_cast<uint8_t>(trace.m_nBlipDisplay);
    state.size = static_cast<uint8_t>(trace.m_nBlipSize < 255 ? trace.m_nBlipSize : 255);
    state.bBright = trace.m_bBright != 0;
    state.bShortRange = trace.m_bShortRange != 0;
    state.bFriendly = trace.m_bFriendly != 0;
    state.bRemain = trace.m_bBlipRemain != 0;
    state.bFade = trace.m_bBlipFade != 0;
    state.coordAppearance = static_cast<uint8_t>(trace.m_nCoordBlipAppearance);

    const int handle = static_cast<int>(trace.m_nEntityHandle);
    switch (state.type)
    {
        case MirrorBlipsPolicy::Type::CAR:
        {
            CVehicle* pVehicle = CPools::GetVehicle(handle);
            if (pVehicle == nullptr)
            {
                return false;
            }
            SetPosition(state, pVehicle->GetPosition());
            if (pVehicle->m_nVehicleSubType != static_cast<unsigned int>(VEHICLE_TRAIN) || CTrainSync::IsEnabled())
            {
                if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
                {
                    SetEntity(state, eEntityKind::VEHICLE, pNetworkVehicle->m_nVehicleId);
                }
            }
            return true;
        }
        case MirrorBlipsPolicy::Type::CHAR:
        {
            CPed* pPed = CPools::GetPed(handle);
            if (pPed == nullptr)
            {
                return false;
            }
            if (pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle != nullptr)
            {
                SetPosition(state, pPed->m_pVehicle->GetPosition());
            }
            else
            {
                SetPosition(state, pPed->GetPosition());
            }
            SetPedEntity(state, pPed);
            return true;
        }
        case MirrorBlipsPolicy::Type::OBJECT:
        {
            CObject* pObject = CPools::GetObject(handle);
            if (pObject == nullptr)
            {
                return false;
            }
            SetPosition(state, pObject->GetPosition());
            if (CMirrorObjects::IsOwnedObject(handle))
            {
                SetEntity(state, eEntityKind::OBJECT, handle);
            }
            return true;
        }
        case MirrorBlipsPolicy::Type::SPOTLIGHT:
        {
            const int light = CTheScripts::GetActualScriptThingIndex(handle, 2);
            if (light < 0)
            {
                return false;
            }
            const RwV3d& target = CTheScripts::ScriptSearchLightArray[light].target;
            SetPosition(state, CVector(target.x, target.y, target.z));
            return true;
        }
        case MirrorBlipsPolicy::Type::PICKUP:
        {
            const int pickup = CPickups::GetActualPickupIndex(handle);
            if (pickup < 0)
            {
                return false;
            }
            SetPosition(state, CPickups::aPickUps[pickup].GetPosn());
            return true;
        }
        default:  // coordinate, contact point (airstrip)
        {
            SetPosition(state, trace.m_vecPos);
            if (trace.m_pEntryExit != nullptr)
            {
                const CRect& rect = trace.m_pEntryExit->m_recEntrance;
                state.bEntrance = true;
                state.entranceX = (rect.left + rect.right) * 0.5f;
                state.entranceY = (rect.bottom + rect.top) * 0.5f;
                state.entranceZ = trace.m_pEntryExit->m_fEntranceZ;
            }
            return true;
        }
    }
}

void FillFull(Packets::Mirror::SMirrorBlipEntry& entry, uint32_t handle, const SBlipState& state)
{
    entry.hostHandle = handle;
    entry.op = MirrorBlipsWire::OP_FULL;
    entry.position = CVector(state.x, state.y, state.z);
    entry.type = state.type;
    entry.entityKind = static_cast<uint8_t>(state.entityKind);
    entry.entityId = state.entityKind != eEntityKind::NONE ? state.entityId : 0;
    entry.colour = state.colour;
    entry.sprite = state.sprite;
    entry.display = state.display;
    entry.size = state.size;
    entry.bBright = state.bBright;
    entry.bShortRange = state.bShortRange;
    entry.bFriendly = state.bFriendly;
    entry.bRemain = state.bRemain;
    entry.bFade = state.bFade;
    entry.coordAppearance = state.coordAppearance;
    entry.bEntrance = state.bEntrance;
    if (state.bEntrance)
    {
        entry.entrance = CVector(state.entranceX, state.entranceY, state.entranceZ);
    }
}

// MISSION_MIRROR_BLIPS packets of one target, filled entry by entry; a full packet goes out and the next one continues
// (bReset only on the first)
class CBatch
{
public:
    CBatch(int targetPlayerId, bool bReset) : m_nTarget(targetPlayerId), m_bReset(bReset) { Packet().count = 0; }

    Packets::Mirror::SMirrorBlipEntry& Next()
    {
        Packets::Mirror::MirrorBlips& packet = Packet();
        if (packet.count >= MirrorBlipsWire::MAX_ENTRIES)
        {
            Send();
        }
        Packets::Mirror::SMirrorBlipEntry& entry = packet.entries[packet.count++];
        entry = Packets::Mirror::SMirrorBlipEntry{};
        return entry;
    }

    // bEvenEmpty: a reset goes out even without entries
    void Flush(bool bEvenEmpty)
    {
        if (Packet().count > 0 || (bEvenEmpty && !m_bSentAny))
        {
            Send();
        }
    }

private:
    static Packets::Mirror::MirrorBlips& Packet()
    {
        static Packets::Mirror::MirrorBlips packet{};  // 48 entries, kept off the stack
        return packet;
    }

    void Send()
    {
        Packets::Mirror::MirrorBlips& packet = Packet();
        packet.targetPlayerId = m_nTarget;
        packet.bReset = m_bReset && !m_bSentAny;
        GetPacketFactory().Send(packet);
        ++ms_counters.sentPackets;
        m_bSentAny = true;
        packet.count = 0;
    }

    int m_nTarget;
    bool m_bReset;
    bool m_bSentAny = false;
};

// The whole owned set with bReset (design 3.11): to a player who joined, or to everyone (-1) after this game restarted.
// What everyone got becomes the base of the next differences.
void SendSnapshot(int targetPlayerId)
{
    const uint32_t now = GetTickCount();
    CBatch batch(targetPlayerId, true);
    int count = 0;
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        SOwnedBlip& blip = ms_owned.At(i);
        const int index = CRadar::GetActualBlipArrayIndex(static_cast<int>(blip.handle));
        SBlipState state;
        if (index < 0 || !CaptureBlip(index, state))
        {
            continue;  // the next difference removes it, or sends it once its entity is there
        }
        FillFull(batch.Next(), blip.handle, state);
        ++count;
        if (targetPlayerId < 0)
        {
            blip.bSent = true;
            blip.sent = state;
            blip.positionSentAt = now;
        }
    }
    batch.Flush(true);
    ms_counters.sentFull += static_cast<unsigned int>(count);
    if (targetPlayerId < 0)
    {
        ++ms_counters.broadcasts;
    }
    else
    {
        ++ms_counters.snapshots;
    }
    LogLine("event=snapshot target=%d blips=%d", targetPlayerId, count);
}

void SendPendingSnapshots()
{
    if (ms_bBroadcastPending)
    {
        ms_bBroadcastPending = false;
        SendSnapshot(-1);
    }
    for (int playerId = 0; playerId < Config::MAX_SERVER_PLAYERS && ms_nSnapshotsPending != 0; playerId++)
    {
        const uint32_t bit = 1u << playerId;
        if ((ms_nSnapshotsPending & bit) == 0)
        {
            continue;
        }
        ms_nSnapshotsPending &= ~bit;
        if (CNetworkPlayerManager::GetPlayer(playerId) != nullptr)
        {
            SendSnapshot(playerId);
        }
    }
}

// once per script frame: what changed since the last frame
void ProcessHost(uint32_t now)
{
    if (!CSessionLifecycle::IsAuthenticated() || !IsServerClockSynced())
    {
        return;  // nothing before the first server-time answer; what is due stays due
    }
    SendPendingSnapshots();

    CBatch batch(-1, false);
    for (int i = 0; i < ms_owned.Count();)
    {
        SOwnedBlip& blip = ms_owned.At(i);
        const int index = CRadar::GetActualBlipArrayIndex(static_cast<int>(blip.handle));
        if (index < 0)
        {
            // removed (remove_blip, the mission's clean-up, its entity destroyed)
            if (blip.bSent)
            {
                Packets::Mirror::SMirrorBlipEntry& entry = batch.Next();
                entry.hostHandle = blip.handle;
                entry.op = MirrorBlipsWire::OP_REMOVED;
                ++ms_counters.sentRemoved;
            }
            LogLine("event=removed handle=%08X sent=%d", blip.handle, blip.bSent);
            ms_owned.RemoveAt(i);
            continue;
        }

        SBlipState state;
        if (!CaptureBlip(index, state))
        {
            ++ms_counters.captureFailures;
            i++;
            continue;
        }
        switch (MirrorBlipsPolicy::DecideSend(blip.bSent, blip.sent, state, now - blip.positionSentAt))
        {
            case eSend::FULL:
                FillFull(batch.Next(), blip.handle, state);
                blip.bSent = true;
                blip.sent = state;
                blip.positionSentAt = now;
                ++ms_counters.sentFull;
                break;
            case eSend::POSITION:
            {
                Packets::Mirror::SMirrorBlipEntry& entry = batch.Next();
                entry.hostHandle = blip.handle;
                entry.op = MirrorBlipsWire::OP_POSITION;
                entry.position = CVector(state.x, state.y, state.z);
                blip.sent.x = state.x;
                blip.sent.y = state.y;
                blip.sent.z = state.z;
                blip.positionSentAt = now;
                ++ms_counters.sentPosition;
                break;
            }
            default:
                break;
        }
        i++;
    }
    batch.Flush(false);
}

void ForgetOwned(const char* why)
{
    if (ms_owned.Count() > 0)
    {
        LogLine("event=forget-owned why=%s", why);
    }
    ms_owned.Clear();
}

// ---- the other games ----

struct SResolved
{
    bool bResolved = false;     // this game has the entity
    bool bLocalPlayer = false;  // it is this game's own player
    int entityRef = 0;          // its pool reference (CPools::GetPedRef / GetVehicleRef / GetObjectRef)
};

// the host's entity in this game, by membership in the lists
SResolved ResolveEntity(const SBlipState& host)
{
    SResolved resolved;
    switch (host.entityKind)
    {
        case eEntityKind::PLAYER:
            if (host.entityId == CNetworkPlayerManager::m_nMyId)
            {
                resolved.bLocalPlayer = true;
            }
            else if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(host.entityId))
            {
                if (CPlayerPed* pPed = pNetworkPlayer->GetLivePed())
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetPedRef(pPed);
                }
            }
            break;
        case eEntityKind::PED:
            if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(host.entityId))
            {
                if (CPed* pPed = pNetworkPed->GetLivePed())
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetPedRef(pPed);
                }
            }
            break;
        case eEntityKind::VEHICLE:
            if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(host.entityId))
            {
                // NULL for a train (CNetworkVehicle::CreateVehicle doesn't create one)
                CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
                if (pVehicle != nullptr && CPools::ms_pVehiclePool->IsObjectValid(pVehicle) &&
                    pVehicle->IsVTableValid())
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetVehicleRef(pVehicle);
                }
            }
            break;
        case eEntityKind::OBJECT:
            if (CObject* pObject = CMirrorObjects::ResolveObjectCopy(host.entityId))
            {
                resolved.bResolved = true;
                resolved.entityRef = CPools::GetObjectRef(pObject);
            }
            break;
        default:
            break;
    }
    return resolved;
}

CEntryExit* FindEntrance(const SBlipState& host)
{
    const int index = CEntryExitManager::FindNearestEntryExit(
        CVector2D(host.entranceX, host.entranceY), ENTRANCE_SEARCH_RANGE, ENTRANCE_IGNORE_AREA);
    return index >= 0 ? CEntryExitManager::GetEntryExit(index) : nullptr;
}

bool HasLocalTrace(const SMirroredBlip& blip)
{
    return blip.localHandle != -1 && CRadar::GetActualBlipArrayIndex(blip.localHandle) != -1;
}

// the host's fields in this game's trace (CNetworkStaticBlip::Create writes them the same way)
void ApplyFields(SMirroredBlip& blip)
{
    const int index = CRadar::GetActualBlipArrayIndex(blip.localHandle);
    if (index < 0)
    {
        return;
    }
    tRadarTrace& trace = CRadar::ms_RadarTrace[index];
    const SBlipState& host = blip.host;
    trace.m_nColour = host.colour;
    trace.m_nRadarSprite = host.sprite;
    trace.m_nBlipDisplay = MirrorBlipsPolicy::LocalDisplay(host, blip.form);
    trace.m_nBlipSize = host.size;
    trace.m_bBright = host.bBright;
    trace.m_bShortRange = host.bShortRange;
    trace.m_bFriendly = host.bFriendly;
    trace.m_bBlipRemain = host.bRemain;
    trace.m_bBlipFade = host.bFade;
    trace.m_nCoordBlipAppearance = host.coordAppearance;

    if (blip.form == eLocalForm::FOLLOW)
    {
        trace.m_vecPos = CVector(host.x, host.y, host.z);
        CRadar::SetBlipEntryExit(blip.localHandle, nullptr);
    }
    else if (blip.form == eLocalForm::COORD)
    {
        // an interior's blip shows at its entrance through the same entry-exit as on the host; without it, at the
        // entrance itself (as the static blip snapshot places such blips)
        CEntryExit* pEntryExit = host.bEntrance ? FindEntrance(host) : nullptr;
        if (host.bEntrance && pEntryExit == nullptr)
        {
            trace.m_vecPos = CVector(host.entranceX, host.entranceY, host.entranceZ);
        }
        else
        {
            trace.m_vecPos = CVector(host.x, host.y, host.z);
        }
        CRadar::SetBlipEntryExit(blip.localHandle, pEntryExit);
    }
}

int CreateLocal(const SMirroredBlip& blip, eLocalForm form, uint8_t type, int entityRef)
{
    const eBlipDisplay display = static_cast<eBlipDisplay>(MirrorBlipsPolicy::LocalDisplay(blip.host, form));
    if (form == eLocalForm::ENTITY)
    {
        return CRadar::SetEntityBlip(static_cast<eBlipType>(type), entityRef, 0, display);
    }
    return CRadar::SetCoordBlip(
        static_cast<eBlipType>(type), CVector(blip.host.x, blip.host.y, blip.host.z), 0, display, nullptr);
}

// Brings this game's blip in line with the host's: made again when the form, type or entity changed or its trace is
// gone, the fields written after a rebuild or when the host's state changed (bApply).
void Reconcile(SMirroredBlip& blip, bool bApply)
{
    const SResolved resolved = ResolveEntity(blip.host);
    const eLocalForm form = MirrorBlipsPolicy::DecideLocalForm(blip.host, resolved.bResolved, resolved.bLocalPlayer);
    const bool bHave = HasLocalTrace(blip);

    if (form == eLocalForm::NONE)
    {
        if (bHave)
        {
            CRadar::ClearBlip(blip.localHandle);
        }
        blip.localHandle = -1;
        blip.form = eLocalForm::NONE;
        blip.localType = MirrorBlipsPolicy::Type::NONE;
        return;
    }

    const uint8_t type = MirrorBlipsPolicy::LocalType(blip.host, form);
    if (MirrorBlipsPolicy::NeedsRebuild(
            bHave, blip.form, blip.localType, blip.localEntityRef, form, type, resolved.entityRef))
    {
        const bool bSwitch = blip.form == eLocalForm::FOLLOW && form == eLocalForm::ENTITY;
        if (bHave)
        {
            CRadar::ClearBlip(blip.localHandle);
        }
        blip.localHandle = CreateLocal(blip, form, type, resolved.entityRef);
        if (blip.localHandle == -1)
        {
            blip.form = eLocalForm::NONE;
            blip.localType = MirrorBlipsPolicy::Type::NONE;
            if (ms_counters.createFailures++ == 0)
            {
                LogLine("event=create-failed handle=%08X form=%s (no free radar trace; retried)", blip.hostHandle,
                    FormName(form));
            }
            return;
        }
        blip.form = form;
        blip.localType = type;
        blip.localEntityRef = form == eLocalForm::ENTITY ? resolved.entityRef : 0;
        ++ms_counters.rebuilds;
        if (bSwitch)
        {
            ++ms_counters.switches;
            LogLine("event=switch handle=%08X form=entity", blip.hostHandle);
        }
        bApply = true;
    }
    if (bApply)
    {
        ApplyFields(blip);
    }
}

void CountForms()
{
    unsigned int entity = 0;
    unsigned int follow = 0;
    unsigned int coord = 0;
    unsigned int none = 0;
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        switch (ms_mirrored.At(i).form)
        {
            case eLocalForm::ENTITY: entity++; break;
            case eLocalForm::FOLLOW: follow++; break;
            case eLocalForm::COORD: coord++; break;
            default: none++; break;
        }
    }
    ms_counters.formEntity = entity;
    ms_counters.formFollow = follow;
    ms_counters.formCoord = coord;
    ms_counters.formNone = none;
}

void DropMirrored(const char* why)
{
    if (ms_mirrored.Count() == 0)
    {
        return;
    }
    LogLine("event=drop-mirrored why=%s", why);
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        const SMirroredBlip& blip = ms_mirrored.At(i);
        if (blip.localHandle != -1)
        {
            CRadar::ClearBlip(blip.localHandle);
        }
    }
    ms_mirrored.Clear();
    CountForms();
}

// This game restarted (a new game or a load): its radar traces were cleared or replaced by the loaded ones, so the
// local handles mean nothing any more and are forgotten without being cleared (one may now name a loaded blip). The
// host's state stays: the blips are made again at once.
void ForgetLocalHandles(const char* why)
{
    if (ms_mirrored.Count() == 0)
    {
        return;
    }
    LogLine("event=forget-local why=%s", why);
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        SMirroredBlip& blip = ms_mirrored.At(i);
        blip.localHandle = -1;
        blip.form = eLocalForm::NONE;
        blip.localType = MirrorBlipsPolicy::Type::NONE;
    }
    ms_bReconcileNow = true;
}

void ProcessReceiver(uint32_t now)
{
    if (ms_mirrored.Count() == 0)
    {
        return;
    }
    if (!ms_bReconcileNow && now - ms_nLastReconcile < RECONCILE_INTERVAL_MS)
    {
        return;
    }
    ms_bReconcileNow = false;
    ms_nLastReconcile = now;
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        Reconcile(ms_mirrored.At(i), false);
    }
    CountForms();
}

SBlipState ReadState(const Packets::Mirror::SMirrorBlipEntry& entry)
{
    SBlipState state;
    state.type = entry.type;
    state.entityKind = static_cast<eEntityKind>(entry.entityKind);
    state.entityId = entry.entityId;
    state.x = entry.position.x;
    state.y = entry.position.y;
    state.z = entry.position.z;
    state.colour = entry.colour;
    state.sprite = entry.sprite;
    state.display = entry.display;
    state.size = entry.size;
    state.bBright = entry.bBright;
    state.bShortRange = entry.bShortRange;
    state.bFriendly = entry.bFriendly;
    state.bRemain = entry.bRemain;
    state.bFade = entry.bFade;
    state.coordAppearance = entry.coordAppearance;
    state.bEntrance = entry.bEntrance;
    if (entry.bEntrance)
    {
        state.entranceX = entry.entrance.x;
        state.entranceY = entry.entrance.y;
        state.entranceZ = entry.entrance.z;
    }
    return state;
}

void ApplyEntry(const Packets::Mirror::SMirrorBlipEntry& entry)
{
    switch (entry.op)
    {
        case MirrorBlipsWire::OP_REMOVED:
        {
            int localHandle = -1;
            if (!ms_mirrored.Remove(entry.hostHandle, localHandle))
            {
                ++ms_counters.recvUnknown;
                return;
            }
            if (localHandle != -1)
            {
                CRadar::ClearBlip(localHandle);
            }
            ++ms_counters.recvRemoved;
            return;
        }
        case MirrorBlipsWire::OP_POSITION:
        {
            SMirroredBlip* pBlip = ms_mirrored.Find(entry.hostHandle);
            if (pBlip == nullptr)
            {
                ++ms_counters.recvUnknown;
                return;
            }
            pBlip->host.x = entry.position.x;
            pBlip->host.y = entry.position.y;
            pBlip->host.z = entry.position.z;
            if (pBlip->form == eLocalForm::FOLLOW && HasLocalTrace(*pBlip))
            {
                CRadar::ms_RadarTrace[CRadar::GetActualBlipArrayIndex(pBlip->localHandle)].m_vecPos =
                    CVector(entry.position.x, entry.position.y, entry.position.z);
            }
            ++ms_counters.recvPosition;
            return;
        }
        default:
        {
            SMirroredBlip* pBlip = ms_mirrored.Upsert(entry.hostHandle, ReadState(entry));
            if (pBlip == nullptr)
            {
                ++ms_counters.createFailures;
                return;
            }
            Reconcile(*pBlip, true);
            ++ms_counters.recvFull;
            return;
        }
    }
}

// ---- both ----

// This game restarted: on the host its radar was reset, so nothing it owned is there any more and the others drop
// what they mirror from it (its whole set, empty now, with bReset); on the others the local handles are forgotten.
void OnLocalRestart(const char* why)
{
    ForgetLocalHandles(why);
    ForgetOwned(why);
    if (CSessionLifecycle::IsHost())
    {
        ms_bBroadcastPending = true;
    }
}

// the frame counter starts over with a new game or a load (CTimer::Initialise 0x5617E0 zeroes CTimer::m_FrameCounter
// 0xB7CB4C; CSessionPolicy.h). Checked at every entry point, before the GAME_RESTARTED event of the same frame, so no
// local handle of the old radar is used after the restart.
void CheckRestart()
{
    const uint32_t frame = CTimer::m_FrameCounter;
    if (frame < ms_nLastFrame)
    {
        OnLocalRestart("frame-counter");
        ms_bRestartSeen = true;
        ms_nRestartFrame = frame;
    }
    ms_nLastFrame = frame;
}

void OnSessionEvent(const SSessionEvent& event)
{
    CheckRestart();
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            if (CSessionLifecycle::IsHost() && event.playerId >= 0 && event.playerId < Config::MAX_SERVER_PLAYERS)
            {
                // the takeover's events come after the first server-time answer; a join before it waits for it
                ms_nSnapshotsPending |= 1u << event.playerId;
                if (CSessionLifecycle::IsAuthenticated() && IsServerClockSynced())
                {
                    SendPendingSnapshots();
                }
            }
            break;
        case eSessionEvent::HOST_CHANGED:
            // a new host starts empty (design 3.11); this game, if it is the new host, drops the old host's blips
            DropMirrored("host-changed");
            if (!event.bIsLocal)
            {
                ForgetOwned("host-changed");
            }
            ms_nSnapshotsPending = 0;
            ms_bBroadcastPending = false;
            break;
        case eSessionEvent::DISCONNECTED:
            DropMirrored("disconnected");
            ForgetOwned("disconnected");
            ms_nSnapshotsPending = 0;
            ms_bBroadcastPending = false;
            break;
        case eSessionEvent::GAME_RESTARTED:
            // the restart CheckRestart saw in this frame already, or a load right after start-up that didn't move the
            // frame counter back
            if (!ms_bRestartSeen || ms_nRestartFrame != CTimer::m_FrameCounter)
            {
                OnLocalRestart(event.bLoaded ? "game-loaded" : "new-game");
            }
            ms_bRestartSeen = false;
            break;
        case eSessionEvent::SAVE_LOADED:
            if (CSessionLifecycle::IsHost())
            {
                // the loaded coordinate blips are nobody's: they go the static way (M-D1), which no hook triggers after
                // a load
                CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = true;
            }
            break;
        default:
            break;
    }
}

// Subscribes on first use (Process from the first script frame, or a packet or command before it), not from
// CCore::Init, which runs during the DLL's static initialization (as CWorldStateRegistry does).
void EnsureStarted()
{
    if (ms_bStarted)
    {
        return;
    }
    ms_bStarted = true;
    ms_nLastFrame = CTimer::m_FrameCounter;
    CSessionLifecycle::Subscribe(OnSessionEvent);
}
}  // namespace

void CMirrorBlips::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    if (!MirrorBlipsPolicy::IsCreatingCommand(opcode) || !CMissionMirror::IsFamilyScript(pScript))
    {
        return;
    }
    EnsureStarted();
    CheckRestart();

    const uint32_t handle = static_cast<uint32_t>(ScriptParams[0]);
    if (CRadar::GetActualBlipArrayIndex(static_cast<int>(handle)) < 0 || !ms_owned.Add(handle))
    {
        if (!ms_owned.Contains(handle))
        {
            ++ms_counters.ownFailures;
            LogLine("event=own-failed handle=%08X opcode=%04X script=%.8s", handle, opcode, pScript->m_szName);
        }
        return;
    }
    ++ms_counters.ownedTotal;
    LogLine("event=own handle=%08X opcode=%04X script=%.8s", handle, opcode, pScript->m_szName);
}

void CMirrorBlips::Process()
{
    EnsureStarted();
    CheckRestart();

    const uint32_t now = GetTickCount();
    if (CSessionLifecycle::IsHost())
    {
        DropMirrored("this-game-is-host");  // (HOST_CHANGED has done it)
        ProcessHost(now);
    }
    else
    {
        ForgetOwned("not-host");  // (HOST_CHANGED has done it)
        ProcessReceiver(now);
    }
    ms_counters.owned = static_cast<unsigned int>(ms_owned.Count());
    ms_counters.mirrored = static_cast<unsigned int>(ms_mirrored.Count());
}

void CMirrorBlips::HandlePacket(const Packets::Mirror::MirrorBlips& packet)
{
    EnsureStarted();
    CheckRestart();
    ++ms_counters.recvPackets;

    const int sender = packet.playerid.value;
    if (!MirrorBlipsPolicy::AcceptFrom(
            CSessionLifecycle::IsHost(), CSessionLifecycle::HasHost(), CSessionLifecycle::GetHostId(), sender))
    {
        if (ms_counters.recvDropped++ < 16)
        {
            LogLine("event=drop-packet sender=%d reset=%d count=%d (not from the current host)", sender, packet.bReset,
                packet.count);
        }
        return;
    }

    if (packet.bReset)
    {
        DropMirrored("reset");
        ++ms_counters.recvResets;
        LogLine("event=reset sender=%d target=%d count=%d", sender, packet.targetPlayerId, packet.count);
    }
    for (int i = 0; i < packet.count && i < MirrorBlipsWire::MAX_ENTRIES; i++)
    {
        ApplyEntry(packet.entries[i]);
    }
    CountForms();
    ms_counters.mirrored = static_cast<unsigned int>(ms_mirrored.Count());
}

bool CMirrorBlips::IsOwnedTrace(int traceIndex)
{
    CheckRestart();
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        if (CRadar::GetActualBlipArrayIndex(static_cast<int>(ms_owned.At(i).handle)) == traceIndex)
        {
            ++ms_counters.staticSkipped;
            return true;
        }
    }
    return false;
}

bool CMirrorBlips::IsMirrorTrace(int traceIndex)
{
    CheckRestart();
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        const SMirroredBlip& blip = ms_mirrored.At(i);
        if (blip.localHandle != -1 && CRadar::GetActualBlipArrayIndex(blip.localHandle) == traceIndex)
        {
            ++ms_counters.staticKept;
            return true;
        }
    }
    return false;
}

void CMirrorBlips::WriteDiagSummary(FILE* file)
{
    const SCounters& c = ms_counters;
    fprintf(file,
        "  mirror-blips: owned=%u total=%u ownFail=%u sent full=%u pos=%u removed=%u packets=%u snapshots=%u "
        "broadcasts=%u captureFail=%u staticSkipped=%u | recv mirrored=%u entity=%u follow=%u coord=%u none=%u "
        "packets=%u dropped=%u resets=%u full=%u pos=%u removed=%u unknown=%u rebuilds=%u switches=%u createFail=%u "
        "staticKept=%u\n",
        c.owned, c.ownedTotal, c.ownFailures, c.sentFull, c.sentPosition, c.sentRemoved, c.sentPackets, c.snapshots,
        c.broadcasts, c.captureFailures, c.staticSkipped, c.mirrored, c.formEntity, c.formFollow, c.formCoord,
        c.formNone, c.recvPackets, c.recvDropped, c.recvResets, c.recvFull, c.recvPosition, c.recvRemoved,
        c.recvUnknown, c.rebuilds, c.switches, c.createFailures, c.staticKept);
}
