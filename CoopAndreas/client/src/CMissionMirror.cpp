#include "stdafx.h"
#include "CMissionMirror.h"
#include "CMirrorPolicy.h"
#include "CMirrorBlips.h"
#include "CMirrorAreas.h"
#include "CMirrorHud.h"
#include "CMirrorObjects.h"
#include "COpCodeSync.h"
#include "CSessionLifecycle.h"
#include <CTimer.h>
#include <cstdarg>

// Private build: the mission mirror layer's core, texts and mission-pass rewards, see CMissionMirror.h.
//
// gta_sa.exe 1.0 US used below:
// - CRunningScript (plugin-sdk CRunningScript.h): m_szName +0x08, m_pBaseIP +0x10, m_pCurrentIP +0x14, m_bIsMission +0xDC;
//   CTheScripts::pActiveScripts 0xA8B42C, MissionBlock 0xA7A6A0, ScriptParams 0xA43C78 (plugin-sdk CTheScripts.cpp).
// - load_and_launch_mission 0417 (case 0x489921 of ProcessCommands1000To1099 0x489500, dumpbin): CollectParameters(1) at
//   0x489929 is the only write to ScriptParams in the case, apart from 0x48994F, which runs only while
//   CTheScripts::NumberOfExclusiveMissionScripts (0xA444B8) is above 0 (our main.scm declares 0 exclusive missions). The
//   functions the case calls don't write it: WipeLocalVariableMemoryForMissionScript 0x464BB0, StartNewScript 0x464C20,
//   CGameLogic::ClearSkip 0x441560 (ReVa), the CFileMgr and CTimer calls. So ScriptParams[0] still holds the mission id
//   after the command, and the mission is the head of pActiveScripts (StartNewScript inserts there) with m_bIsMission and
//   m_pBaseIP = MissionBlock (written at 0x489A05/0x489A0B).
// - TheText 0xC1B340: CText::Get 0x6A0050 searches the main keys (CText__TKey at +0x00, GetTextByLabel 0x6A0000 with
//   (key, bool* found), called at 0x6A0070), then the mission keys (+0x10) only while +0x23 (missionTableLoaded) is set
//   (read at 0x6A0086), and returns GxtErrorString 0xC1AEB8 for a missing key (0x6A00E0; gta-reversed common.h). The
//   mission table name is at +0x24, the TABL entries at +0x12C and their count at +0xA8C (read by LoadMissionTable at
//   0x69FC6E) (plugin-sdk CText.h). LoadMissionTable 0x69FBF0, which the 054C case 0x48D587 calls, clears this game's
//   messages and unloads the old table first, then loads nothing for a name that isn't in TABL. Cutscene subtitles are
//   looked up with CText::Get when they show (gta-reversed CCutsceneMgr::Update), so a receiver needs the host's table
//   loaded before the host's cutscene plays.
// - CMessages::BriefMessages 0xC1A7F0 (plugin-sdk): [0] is the small message on screen; print and print_now (cases
//   0x4680CF, 0x46815B) pass the pointer CText::Get returned, the time and the flag to CMessages::AddMessage 0x69F0B0 /
//   AddMessageJumpQ 0x69F1E0. m_dwStartTime is in CTimer::m_snTimeInMilliseconds.
// - CHud (plugin-sdk): m_nHelpMessageState 0xBAA474 (HelpMessageDisplayed 0x588B50 returns it != 0, dumpbin);
//   SetHelpMessage 0x588BE0: clear_help 03E6 (case 0x48566A) calls it with (nullptr, true, false, false), and so does
//   CMissionCleanup::Process 0x468560 unless CTheScripts::bMiniGameInProgress (0xA444A8) is set (ReVa); m_ItemToFlash
//   0xBAB1DC: flash_hud_object 03E7 (case 0x48567E) writes it, CMissionCleanup::Process sets it to -1.
static_assert(sizeof(OpcodeSyncHeader) == MirrorPolicy::HEADER_SIZE, "OPCODE_SYNC header layout (CMirrorPolicy.h)");

namespace
{
using MirrorPolicy::Op::FLASH_HUD_OBJECT;
using MirrorPolicy::Op::LOAD_MISSION_TEXT;

// CText::Get's result for a key that is in neither table
char* const GXT_ERROR_STRING = reinterpret_cast<char*>(0xC1AEB8);

// every game: the mission its scripts run (the family rule needs it once this game is the host)
MirrorPolicy::CMissionTracker ms_mission;
CRunningScript* ms_pMissionScript = nullptr;
// host: the rewards of the current script frame (M-D3)
MirrorPolicy::CRewardFrame ms_rewards;
// what a joiner gets (design 3.11): the mission's text table (also kept while this game isn't the host), the family's
// last objective and help box sent
char ms_szTable[8]{};
MirrorPolicy::STrackedText ms_objective;
MirrorPolicy::STrackedText ms_help;

// receivers: the host's mission text table (the last load_mission_text replayed), and a HUD flash the host set here
char ms_szHostTable[8]{};
bool ms_bMirroredFlash = false;
int16_t ms_nMirroredFlashItem = -1;

// --coopdiag (plain values; the diag thread reads them)
struct SCounters
{
    unsigned int textsMirrored = 0;    // texts only the mirror sent (unregistered family scripts)
    unsigned int textsRegistered = 0;  // texts of registered scripts (COpCodeSync's general replay)
    unsigned int explosionsMirrored = 0;    // EX-1: script explosions only the mirror sent
    unsigned int explosionsRegistered = 0;  // EX-1: script explosions of registered scripts
    unsigned int titles = 0;           // print_big style 2 outside the family
    unsigned int bigPrintsKept = 0;    // print_big outside the family that wasn't a title
    unsigned int rewardsHeld = 0;
    unsigned int rewardsSent = 0;
    unsigned int rewardsDropped = 0;   // held, but no pass signal in their frame
    unsigned int rewardsKept = 0;      // the family's rewards that weren't held: not positive (bets, costs)
    unsigned int passFrames = 0;
    unsigned int snapshots = 0;
    unsigned int sentOpcodeSync = 0;   // OPCODE_SYNC packets the mirror sent: its texts, explosions, titles, rewards, snapshot tables
    unsigned int sentAddMessage = 0;   // ADD_MESSAGE_GXT (snapshot)
    unsigned int sentMissionFlag = 0;  // ON_MISSION_FLAG_SYNC (snapshot)
    unsigned int replays = 0;          // receiver: replayed texts of the host
    unsigned int explosionReplays = 0; // receiver: replayed script explosions of the host
    unsigned int explosionSkips = 0;   // receiver: script explosions of the host dropped without a local player
    unsigned int tableLoads = 0;       // receiver: host tables loaded (load_mission_text or a key that needed it)
    unsigned int tableSkips = 0;       // receiver: load_mission_text of the table already loaded
    unsigned int helpClears = 0;       // receiver: help box cleared at the end of the host's mission
    unsigned int textsPeakPerSecond = 0;
};
SCounters ms_counters;
char ms_szMissionName[8]{};
unsigned int ms_nTextsThisSecond = 0;
uint32_t ms_nTextSecondStart = 0;

// --coopdiag: CoopAndreas_mirror.log next to gta_sa.exe, at most 512 records and 1 MB (as CoopAndreas_session.log)
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
        fprintf(file, "t=%u frame=%u %s host=%d mission=%d\n", GetTickCount(), CTimer::m_FrameCounter, text,
            CSessionLifecycle::IsHost(), ms_mission.GetMissionId());
        ++records;
    }
    fclose(file);
}

void SendOpcodeBuffer(const uint8_t* data, int size)
{
    if (size < MirrorPolicy::HEADER_SIZE || size > Packets::Scripts::OpCodeSync::MAX_BUFFER_SIZE)
    {
        return;
    }
    static Packets::Scripts::OpCodeSync packet{};
    packet.size = size;
    memcpy(packet.buffer, data, static_cast<size_t>(size));
    GetPacketFactory().Send(packet);
    ++ms_counters.sentOpcodeSync;
}

// risk 7 (design 6): how many texts one second carries at most, before anybody throttles them
void CountText()
{
    const uint32_t now = GetTickCount();
    if (now - ms_nTextSecondStart >= 1000)
    {
        ms_nTextSecondStart = now;
        ms_nTextsThisSecond = 0;
    }
    if (++ms_nTextsThisSecond > ms_counters.textsPeakPerSecond)
    {
        ms_counters.textsPeakPerSecond = ms_nTextsThisSecond;
    }
}

// the family rule without the host condition: the code of the mission this game runs
bool IsMissionCode(CRunningScript* pScript)
{
    if (pScript == nullptr)
    {
        return false;
    }
    MirrorPolicy::SScriptInfo info;
    info.bIsMission = pScript->m_bIsMission;
    info.ip = reinterpret_cast<uintptr_t>(pScript->m_pCurrentIP);
    memcpy(info.name, pScript->m_szName, sizeof(info.name));
    return MirrorPolicy::IsFamily(info, ms_mission);
}

void ForgetMission()
{
    ms_mission.OnEnded();
    ms_pMissionScript = nullptr;
    ms_szTable[0] = '\0';
    ms_objective.Clear();
    ms_help.Clear();
    ms_szMissionName[0] = '\0';
}

// load_and_launch_mission ran: the mission it started and the id it took (see the 0417 note at the top)
void OnMissionLaunched()
{
    CRunningScript* pScript = CTheScripts::pActiveScripts;
    if (pScript == nullptr || !pScript->m_bIsMission ||
        pScript->m_pBaseIP != reinterpret_cast<unsigned char*>(CTheScripts::MissionBlock))
    {
        return;
    }
    ForgetMission();
    ms_pMissionScript = pScript;
    ms_mission.OnLaunched(ScriptParams[0]);
    LogLine("event=mission-start id=%d mirrored=%d", ms_mission.GetMissionId(), ms_mission.IsMirrored());
}

// once per script frame: the mission ends when its script leaves the active list; a mission script that runs without
// its 0417 seen (the debug mission runner) is taken with an unknown id
void UpdateMission()
{
    CRunningScript* pMission = nullptr;
    for (CRunningScript* pScript = CTheScripts::pActiveScripts; pScript != nullptr; pScript = pScript->m_pNext)
    {
        if (pScript->m_bIsMission)
        {
            pMission = pScript;
            break;
        }
    }

    if (ms_mission.IsRunning() && pMission != ms_pMissionScript)
    {
        LogLine("event=mission-end id=%d name=%.8s", ms_mission.GetMissionId(), ms_szMissionName);
        ForgetMission();
    }
    if (!ms_mission.IsRunning() && pMission != nullptr)
    {
        ms_pMissionScript = pMission;
        ms_mission.OnDetected();
        LogLine("event=mission-detected");
    }
    if (ms_pMissionScript != nullptr)
    {
        memcpy(ms_szMissionName, ms_pMissionScript->m_szName, sizeof(ms_szMissionName));
    }
}

// what a joiner gets from the texts the family sends
void TrackSentText(const MirrorPolicy::SCommandView& view)
{
    using namespace MirrorPolicy::Op;
    switch (view.opcode)
    {
        case CLEAR_PRINTS:
        case CLEAR_SMALL_PRINTS:
            ms_objective.Clear();
            break;
        case CLEAR_THIS_PRINT:
            if (ms_objective.bValid && view.textCount >= 1 && MirrorPolicy::EqualsNoCase(ms_objective.key, view.texts[0], 8))
            {
                ms_objective.Clear();
            }
            break;
        case CLEAR_HELP:
            ms_help.Clear();
            break;
        case PRINT_HELP:
        case PRINT_HELP_FOREVER:
        case PRINT_HELP_FOREVER_WITH_NUMBER:
            ms_help.Set(view);
            break;
        default:
            if (MirrorPolicy::IsObjectivePrint(view.opcode))
            {
                ms_objective.Set(view);
            }
            break;
    }
}

// a table the GXT has (LoadMissionTable unloads the current one before it looks for the name)
bool IsKnownTable(const char* table)
{
    for (int i = 0; i < TheText.tabl.size && i < static_cast<int>(ARRAY_SIZE(TheText.tabl.data)); i++)
    {
        if (strncmp(TheText.tabl.data[i].name, table, sizeof(TheText.tabl.data[i].name)) == 0)
        {
            return true;
        }
    }
    return false;
}

bool IsTableLoaded(const char* table)
{
    return TheText.missionTableLoaded != 0 && strncmp(TheText.missionTableName, table, sizeof(TheText.missionTableName)) == 0;
}

// the host's mission text table for the receivers (design 3.11): a receiver loads it unless it has it loaded already,
// so the players already in see nothing happen
void SendTableName(const char* table)
{
    uint8_t data[MirrorPolicy::HEADER_SIZE + 1 + sizeof(ms_szTable)]{};
    OpcodeSyncHeader header{};
    header.opcode = LOAD_MISSION_TEXT;
    header.intParamCount = 0;
    header.stringParamCount = 1;
    memcpy(data, &header, sizeof(header));
    const uint8_t length = static_cast<uint8_t>(strnlen(table, sizeof(ms_szTable) - 1));
    data[MirrorPolicy::HEADER_SIZE] = length;
    memcpy(data + MirrorPolicy::HEADER_SIZE + 1, table, length);
    SendOpcodeBuffer(data, MirrorPolicy::HEADER_SIZE + 1 + length);
}

void SendMessageToPlayer(int playerId, Packets::Scripts::AddMessageGXT::eGXTMsgType type, const char* key, uint32_t time,
    uint8_t flag)
{
    Packets::Scripts::AddMessageGXT packet{};
    packet.forWhoPlayerId = playerId;
    packet.type = type;
    packet.time = time;
    packet.flag = flag;
    strncpy_s(packet.gxt, key, sizeof(packet.gxt) - 1);
    GetPacketFactory().Send(packet);
    ++ms_counters.sentAddMessage;
}

// PLAYER_JOINED on the host (design 3.11): the mission view of the joiner. The per-player ADD_MESSAGE_GXT reaches the
// joiner alone (the server relays it to forWhoPlayerId); the two broadcasts change nothing for the players already in.
void SendJoinSnapshot(int playerId, bool bTakeover)
{
    if (!CSessionLifecycle::IsHost() || playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }
    ++ms_counters.snapshots;

    // M-D10: the host's $onmission (ON_MISSION_FLAG_SYNC goes out only on a change, so a joiner never had it). Only a
    // mission in progress is resent: 0 is the joiner's state already, and on a game in its own new-game mission it would
    // run the 1 -> 0 clean-up (PacketHandlers/scripts.cpp). The players already in have 1 already.
    bool bFlag = false;
    if (CTheScripts::OnAMissionFlag && CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])
    {
        Packets::Scripts::OnMissionFlagSync packet{};
        packet.bOnMission = true;
        GetPacketFactory().Send(packet);
        ++ms_counters.sentMissionFlag;
        bFlag = true;
    }

    // the mission's texts, while a mirrored mission runs
    bool bTable = false;
    uint32_t objectiveLeft = 0;
    bool bHelp = false;
    if (ms_mission.IsMirrored())
    {
        if (ms_szTable[0] != '\0')
        {
            SendTableName(ms_szTable);
            bTable = true;
        }

        // the current objective, with the time it has left, if it is still the message on screen
        if (MirrorPolicy::CanSnapshotObjective(ms_objective))
        {
            const char* text = TheText.Get(ms_objective.key);
            const tMessage& message = CMessages::BriefMessages[0];
            if (text != GXT_ERROR_STRING && message.m_pText == text)
            {
                const uint32_t left =
                    MirrorPolicy::GetTimeLeft(message.m_dwStartTime, message.m_dwTime, CTimer::m_snTimeInMilliseconds);
                if (left >= MirrorPolicy::MIN_SNAPSHOT_TEXT_MS)
                {
                    SendMessageToPlayer(playerId, Packets::Scripts::AddMessageGXT::sync_COMMAND_PRINT_NOW,
                        ms_objective.key, left, static_cast<uint8_t>(ms_objective.flag));
                    objectiveLeft = left;
                }
            }
        }

        // the help box, if it is still on screen (a permanent one arrives as an ordinary help box)
        if (MirrorPolicy::CanSnapshotHelp(ms_help) && CHud::m_nHelpMessageState != 0)
        {
            SendMessageToPlayer(
                playerId, Packets::Scripts::AddMessageGXT::sync_COMMAND_PRINT_HELP, ms_help.key, 0, 0);
            bHelp = true;
        }
    }

    LogLine("event=snapshot player=%d takeover=%d flag=%d table=%.7s objective=%.7s/%u help=%.7s", playerId, bTakeover,
        bFlag, bTable ? ms_szTable : "-", objectiveLeft ? ms_objective.key : "-", objectiveLeft,
        bHelp ? ms_help.key : "-");
}

// The end of the host's mission on the other games: what CMissionCleanup::Process does to the HUD on the host, the help
// box (unless a minigame runs) and the HUD flash, as far as a mission of this game doesn't own them. Texts stay: the host's
// clean-up doesn't clear them either.
void CleanUpAfterHostMission(bool bHostLost)
{
    if (CSessionLifecycle::IsLocalMissionRunning())
    {
        LogLine("event=cleanup what=skipped-local-mission hostLost=%d", bHostLost);
        ms_bMirroredFlash = false;
        return;
    }
    if (!CTheScripts::bMiniGameInProgress)
    {
        CHud::SetHelpMessage(nullptr, true, false, false);
        ++ms_counters.helpClears;
    }
    if (ms_bMirroredFlash && CHud::m_ItemToFlash == ms_nMirroredFlashItem)
    {
        CHud::m_ItemToFlash = -1;
    }
    ms_bMirroredFlash = false;
    LogLine("event=cleanup what=help hostLost=%d minigame=%d", bHostLost, CTheScripts::bMiniGameInProgress);
}

void OnSessionEvent(const SSessionEvent& event)
{
    switch (event.type)
    {
        case eSessionEvent::PLAYER_JOINED:
            SendJoinSnapshot(event.playerId, event.bTakeover);
            break;
        case eSessionEvent::HOST_MISSION_ENDED:
            if (!event.bLocal)
            {
                CleanUpAfterHostMission(event.bHostLost);
            }
            break;
        case eSessionEvent::HOST_CHANGED:
        case eSessionEvent::DISCONNECTED:
            // a new host starts empty (its own table comes with its next mission or its join snapshot)
            ms_szHostTable[0] = '\0';
            ms_bMirroredFlash = false;
            LogLine("event=receiver-reset");
            break;
        case eSessionEvent::GAME_RESTARTED:
            // the HUD was reset. The mission itself is left to UpdateMission: this event comes after the frame's scripts,
            // in which MAIN may already have launched a new-game mission (0417 seen, its id recorded)
            ms_objective.Clear();
            ms_help.Clear();
            ms_bMirroredFlash = false;
            break;
        default:
            break;
    }
}
}  // namespace

void CMissionMirror::Init()
{
    CSessionLifecycle::Subscribe(OnSessionEvent);

    // the OPCODE_SYNC header as CMirrorPolicy.h reads it (MSVC allocates bit fields from the lowest bit)
    OpcodeSyncHeader header{};
    header.opcode = 0x1234;
    header.intParamCount = 3;
    header.stringParamCount = 5;
    uint8_t bytes[sizeof(header)]{};
    memcpy(bytes, &header, sizeof(header));
    if (bytes[0] != 0x34 || bytes[1] != 0x12 || bytes[2] != 0x53)
    {
        LogLine("event=error what=header-layout bytes=%02X%02X%02X", bytes[0], bytes[1], bytes[2]);
    }
}

bool CMissionMirror::IsFamilyScript(CRunningScript* pScript)
{
    return CSessionLifecycle::IsHost() && IsMissionCode(pScript);
}

bool CMissionMirror::IsRewardCommand(uint16_t opcode) { return MirrorPolicy::IsRewardCommand(opcode & 0x7FFF); }

bool CMissionMirror::WantsCommand(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    // the host's own scripts, inside a command CRunningScript::Process runs: not a network replay, not a command this DLL
    // runs with plugin-sdk's Command<> (CRunningScript::ProcessOneCommand, outside the hook)
    if (pScript == nullptr || pScript != COpCodeSync::GetScriptRunningCommand() || COpCodeSync::bProcessingNetworkOpcode ||
        !CSessionLifecycle::IsHost())
    {
        return false;
    }
    if (MirrorPolicy::IsTextCommand(opcode))
    {
        // M-D4: a print_big of any script may be a mission title; OnCommandSerialized looks at its style
        return opcode == MirrorPolicy::Op::PRINT_BIG || IsMissionCode(pScript);
    }
    if (MirrorPolicy::IsRewardCommand(opcode))
    {
        return IsMissionCode(pScript);
    }
    if (MirrorPolicy::IsExplosionCommand(opcode))
    {
        return IsMissionCode(pScript);
    }
    return false;
}

void CMissionMirror::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    if (pScript == nullptr)
    {
        return;
    }
    CMirrorObjects::OnCommandExecuted(pScript, opcode); // OB-1: the objects the host's mission family creates
    CMirrorBlips::OnCommandExecuted(pScript, opcode); // M2: the blips the host's mission family creates
    CMirrorHud::OnCommandExecuted(pScript, opcode); // M4: which timers and counters the host's mission added

    // every game: the mission its scripts launch, and the text table the mission loaded (its handler has just loaded it;
    // the name is empty for one the GXT doesn't have). Kept while this game isn't the host for the join snapshot it may
    // have to send later.
    if (opcode == MirrorPolicy::Op::LOAD_AND_LAUNCH_MISSION)
    {
        OnMissionLaunched();
        return;
    }
    if (opcode == LOAD_MISSION_TEXT && IsMissionCode(pScript))
    {
        memcpy(ms_szTable, TheText.missionTableName, sizeof(ms_szTable) - 1);
        ms_szTable[sizeof(ms_szTable) - 1] = '\0';
    }

    if (CSessionLifecycle::IsHost())
    {
        // M-D3: register_mission_passed or play_mission_passed_tune of the family (the M_PASS print: OnCommandSerialized)
        if (MirrorPolicy::IsPassCommand(opcode) && IsMissionCode(pScript))
        {
            ms_rewards.OnPassSignal();
        }
        // another script's help box replaces the family's; the family's own one is tracked again in OnCommandSerialized
        if (MirrorPolicy::IsHelpCommand(opcode))
        {
            ms_help.Clear();
        }
    }
    else if (opcode == FLASH_HUD_OBJECT)
    {
        // this game's own scripts own the HUD flash from now on
        ms_bMirroredFlash = false;
    }
}

bool CMissionMirror::OnCommandSerialized(
    CRunningScript* pScript, uint16_t opcode, bool bMirrorOnly, const uint8_t* data, int size)
{
    opcode &= 0x7FFF;
    if (MirrorPolicy::IsExplosionCommand(opcode))
    {
        if (bMirrorOnly)
        {
            ++ms_counters.explosionsMirrored;
            ++ms_counters.sentOpcodeSync;
        }
        else
        {
            ++ms_counters.explosionsRegistered;
        }
        return true;
    }

    const bool bReward = MirrorPolicy::IsRewardCommand(opcode);
    if (!bReward && !MirrorPolicy::IsTextCommand(opcode))
    {
        return true;
    }

    MirrorPolicy::SCommandView view;
    if (!MirrorPolicy::ParseCommand(data, size, view))
    {
        return !bMirrorOnly && !bReward;
    }
    const bool bFamily = IsFamilyScript(pScript);

    // M-D3: the "Mission passed" print of the family
    if (bFamily && MirrorPolicy::IsPassPrint(view))
    {
        ms_rewards.OnPassSignal();
    }

    if (bReward)
    {
        // only the mirror sends money and respect (IsOpcodeSyncable keeps them out of the general replay): the positive
        // ones of the family, at the end of this script frame if the mission passed in it
        if (bFamily && ms_rewards.Hold(view, data, size))
        {
            ++ms_counters.rewardsHeld;
        }
        else
        {
            ++ms_counters.rewardsKept;
        }
        return false;
    }

    if (bMirrorOnly && !bFamily)
    {
        // M-D4: a print_big outside the family goes out only as a mission title
        if (!MirrorPolicy::IsTitle(view))
        {
            ++ms_counters.bigPrintsKept;
            return false;
        }
        ++ms_counters.titles;
        ++ms_counters.sentOpcodeSync;
        LogLine("event=title key=%.7s", view.texts[0]);
        CountText();
        return true;
    }

    if (bMirrorOnly)
    {
        ++ms_counters.textsMirrored;
        ++ms_counters.sentOpcodeSync;
    }
    else
    {
        ++ms_counters.textsRegistered;
    }
    CountText();
    if (bFamily)
    {
        TrackSentText(view);
    }
    return true;
}

void CMissionMirror::OnScriptsProcessed()
{
    // M-D3: the rewards of this script frame
    if (ms_rewards.GetHeldCount() > 0 || ms_rewards.HasPassed())
    {
        const bool bPassed = ms_rewards.HasPassed();
        const int held = ms_rewards.GetHeldCount();
        int dropped = 0;
        const int sent = ms_rewards.EndFrame([](const uint8_t* data, int size) { SendOpcodeBuffer(data, size); }, dropped);
        ms_counters.passFrames += bPassed ? 1 : 0;
        ms_counters.rewardsSent += static_cast<unsigned int>(sent);
        ms_counters.rewardsDropped += static_cast<unsigned int>(dropped);
        LogLine("event=rewards passed=%d held=%d sent=%d dropped=%d", bPassed, held, sent, dropped);
    }

    UpdateMission();
    CMirrorObjects::Process(); // OB-1: the host's objects, before the blips so a blip finds its object
    CMirrorBlips::Process(); // M2: the host sends its mission's blips, the others keep their copies in line
    CMirrorAreas::Process(ms_mission.IsMirrored()); // the host's locate spheres and markers (package M3)
    CMirrorHud::Process(); // M4: the host's timers and counters out, the mirrored ones on the other games
}

bool CMissionMirror::OnReplayingCommand(const uint8_t* data, int size)
{
    MirrorPolicy::SCommandView view;
    if (!MirrorPolicy::ParseCommand(data, size, view))
    {
        return true;
    }
    if (MirrorPolicy::IsExplosionCommand(view.opcode))
    {
        // EX-1: CExplosion::AddExplosion 0x736A50 needs no player, but a game without one (loading) has no world to show
        // the blast in
        if (FindPlayerPed(0) == nullptr)
        {
            ++ms_counters.explosionSkips;
            return false;
        }
        ++ms_counters.explosionReplays;
        return true;
    }
    if (!MirrorPolicy::IsTextCommand(view.opcode))
    {
        return true;
    }
    ++ms_counters.replays;

    if (view.opcode == LOAD_MISSION_TEXT)
    {
        if (view.textCount < 1)
        {
            return true;
        }
        if (strncmp(ms_szHostTable, view.texts[0], sizeof(ms_szHostTable) - 1) != 0)
        {
            strncpy_s(ms_szHostTable, view.texts[0], sizeof(ms_szHostTable) - 1);
            LogLine("event=host-table table=%.7s", ms_szHostTable);
        }
        // loaded now, as the host did (the host's cutscenes show their subtitles from it), unless this game has it loaded
        // already: then a repeat (the join snapshot) changes nothing for the players already in
        if (IsTableLoaded(ms_szHostTable))
        {
            ++ms_counters.tableSkips;
            return false;
        }
        if (!IsKnownTable(ms_szHostTable))
        {
            LogLine("event=table-unknown table=%.7s", ms_szHostTable);
            return false;
        }
        ++ms_counters.tableLoads;
        return true;
    }

    if (view.opcode == FLASH_HUD_OBJECT && view.intCount >= 1)
    {
        ms_bMirroredFlash = view.ints[0] != -1;
        ms_nMirroredFlashItem = static_cast<int16_t>(view.ints[0]);
    }

    if (MirrorPolicy::IsKeyedPrint(view.opcode))
    {
        for (int i = 0; i < view.textCount; i++)
        {
            PrepareTextKey(view.texts[i]);
        }
    }
    return true;
}

void CMissionMirror::PrepareTextKey(const char* key)
{
    if (key == nullptr || key[0] == '\0' || ms_szHostTable[0] == '\0')
    {
        return;
    }

    bool bInMainTable = false;
    TheText.tkeyMain.GetTextByLabel(key, &bInMainTable);
    if (!MirrorPolicy::NeedsHostTable(
            bInMainTable, ms_szHostTable, TheText.missionTableLoaded != 0, TheText.missionTableName))
    {
        return;
    }
    if (!IsKnownTable(ms_szHostTable))
    {
        LogLine("event=table-unknown table=%.7s key=%.7s", ms_szHostTable, key);
        return;
    }

    // another table took its place (this game's own script, a gym for example)
    TheText.LoadMissionTable(ms_szHostTable);
    ++ms_counters.tableLoads;
    LogLine("event=table-load table=%.7s key=%.7s", ms_szHostTable, key);
}

void CMissionMirror::WriteDiagSummary(FILE* file)
{
    const SCounters& c = ms_counters;
    fprintf(file,
        "  mirror: mission=%d mirrored=%d name=%.8s texts=%u+%u explosions=%u+%u titles=%u bigKept=%u peak=%u/s rewards "
        "held=%u sent=%u dropped=%u kept=%u passFrames=%u snapshots=%u sent opcode=%u addMessage=%u missionFlag=%u | recv "
        "replays=%u explosions=%u/%u hostTable=%.7s tableLoads=%u tableSkips=%u helpClears=%u\n",
        ms_mission.GetMissionId(), ms_mission.IsMirrored(), ms_szMissionName, c.textsMirrored, c.textsRegistered,
        c.explosionsMirrored, c.explosionsRegistered, c.titles, c.bigPrintsKept, c.textsPeakPerSecond, c.rewardsHeld,
        c.rewardsSent, c.rewardsDropped, c.rewardsKept, c.passFrames, c.snapshots, c.sentOpcodeSync, c.sentAddMessage,
        c.sentMissionFlag, c.replays, c.explosionReplays, c.explosionSkips, ms_szHostTable[0] ? ms_szHostTable : "-", c.tableLoads,
        c.tableSkips, c.helpClears);
    CMirrorBlips::WriteDiagSummary(file); // M2
    CMirrorAreas::WriteDiagSummary(file);
    CMirrorHud::WriteDiagSummary(file); // M4
    CMirrorObjects::WriteDiagSummary(file); // OB-1
}
