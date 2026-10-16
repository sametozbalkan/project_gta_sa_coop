#pragma once

#include <cstdint>
#include <cstring>

// Private build: the rules of the mission mirror layer (CMissionMirror; docs/design/mirror-layer.md sections 3.2 and 3.4,
// decisions M-D2 to M-D5 and M-D10 in docs/design/README.md), without game or network types, so
// tests/mission_mirror_test.cpp checks them without GTA.
namespace MirrorPolicy
{
// gta_sa.exe 1.0 US: CTheScripts::MissionBlock 0xA7A6A0 (plugin-sdk CTheScripts.cpp). load_and_launch_mission (0417, case
// 0x489921 of CRunningScript::ProcessCommands1000To1099 0x489500) reads 0x10D88 = 69,000 bytes of the mission there
// (push 10D88h / push 0A7A6A0h at 0x4899D1 and 0x489A5A, dumpbin) and starts it with StartNewScript(0xA7A6A0).
constexpr uintptr_t MISSION_BLOCK = 0xA7A6A0;
constexpr uintptr_t MISSION_BLOCK_SIZE = 69000;

// M-D2: missions 0-10 are not mirrored: Initial 1 and 2 and Intro, which every game runs at New Game, the arcade games,
// betting, pool, the lowrider game and Beefy Baron (scm/main.txt, DEFINE MISSION 0 to 10).
constexpr int LAST_EXCLUDED_MISSION = 10;
constexpr int NO_MISSION = -2;
// a mission script that runs without its 0417 having been seen (the debug mission runner starts missions with
// plugin-sdk's Command<>, which CRunningScript::ProcessOneCommand runs outside the command hook)
constexpr int UNKNOWN_MISSION = -1;

// the OPCODE_SYNC buffer as COpCodeSync::SerializeOpcode writes it: OpcodeSyncHeader (COpCodeSync.h: opcode u16, the int
// parameter count in the low and the text count in the high 4 bits of one byte, one padding byte), the int parameters,
// then each text as a length byte and its characters
constexpr int HEADER_SIZE = 4;
constexpr int MAX_PARAMS = 15;  // NUM_SYNCED_PARAMS, the most a 4-bit count holds
constexpr int MAX_TEXT = 16;    // text keys and table names have up to 7 characters; longer texts are cut in the view

// script commands, Sanny Builder 4 ids (sdk/Sanny Builder 4/data/sa_sbl_coopandreas)
namespace Op
{
constexpr uint16_t PRINT_BIG = 0x00BA;                      // key, time, style
constexpr uint16_t PRINT = 0x00BB;                          // key, time, flag
constexpr uint16_t PRINT_NOW = 0x00BC;                      // key, time, flag
constexpr uint16_t CLEAR_PRINTS = 0x00BE;
constexpr uint16_t ADD_SCORE = 0x0109;                      // player, money
constexpr uint16_t PRINT_WITH_NUMBER_BIG = 0x01E3;          // key, number, time, style (the M_PASS print)
constexpr uint16_t PRINT_WITH_NUMBER = 0x01E4;              // key, number, time, flag
constexpr uint16_t PRINT_WITH_NUMBER_NOW = 0x01E5;          // key, number, time, flag
constexpr uint16_t PRINT_BIG_Q = 0x0217;                    // key, time, style
constexpr uint16_t PRINT_WITH_2_NUMBERS_NOW = 0x02FD;       // key, 2 numbers, time, flag
constexpr uint16_t PRINT_WITH_3_NUMBERS = 0x02FF;           // key, 3 numbers, time, flag
constexpr uint16_t PRINT_WITH_4_NUMBERS = 0x0302;           // key, 4 numbers, time, flag
constexpr uint16_t PRINT_WITH_4_NUMBERS_NOW = 0x0303;       // key, 4 numbers, time, flag
constexpr uint16_t PRINT_WITH_6_NUMBERS = 0x0308;           // key, 6 numbers, time, flag
constexpr uint16_t REGISTER_MISSION_PASSED = 0x0318;        // key
constexpr uint16_t PRINT_WITH_2_NUMBERS_BIG = 0x036D;       // key, 2 numbers, time, style
constexpr uint16_t PRINT_STRING_IN_STRING_NOW = 0x0384;     // key, key, time, flag
constexpr uint16_t PLAY_MISSION_PASSED_TUNE = 0x0394;       // sound
constexpr uint16_t CLEAR_THIS_PRINT = 0x03D5;               // key
constexpr uint16_t CLEAR_THIS_BIG_PRINT = 0x03D6;           // key
constexpr uint16_t PRINT_HELP = 0x03E5;                     // key
constexpr uint16_t CLEAR_HELP = 0x03E6;
constexpr uint16_t FLASH_HUD_OBJECT = 0x03E7;               // HUD object, -1 = none
constexpr uint16_t CLEAR_SMALL_PRINTS = 0x03EB;
constexpr uint16_t LOAD_AND_LAUNCH_MISSION = 0x0417;        // mission id
constexpr uint16_t PRINT_HELP_FOREVER = 0x0512;             // key
constexpr uint16_t PRINT_HELP_FOREVER_WITH_NUMBER = 0x0513; // key, number
constexpr uint16_t LOAD_MISSION_TEXT = 0x054C;              // table name
constexpr uint16_t AWARD_PLAYER_MISSION_RESPECT = 0x0998;   // respect
constexpr uint16_t ADD_NEXT_MESSAGE_TO_PREVIOUS_BRIEFS = 0x09C1;  // state
constexpr uint16_t CLEAR_THIS_PRINT_BIG_NOW = 0x0A0E;       // style
}  // namespace Op

// print_big style 2 (TextStyle.BottomRight): the mission titles the MAIN strand threads print before they launch the
// mission. The 00BA case (0x46807B) passes style - 1 = STYLE_BOTTOM_RIGHT to CMessages::AddBigMessage (0x69F2B0).
constexpr int32_t TITLE_STYLE = 2;

// the join snapshot replays an objective text only while this much of its time is left
constexpr uint32_t MIN_SNAPSHOT_TEXT_MS = 1000;

inline bool EqualsNoCase(const char* a, const char* b, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
        {
            ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z')
        {
            cb = static_cast<char>(cb - 'A' + 'a');
        }
        if (ca != cb)
        {
            return false;
        }
        if (ca == '\0')
        {
            return true;
        }
    }
    return true;
}

// design 2.1 "text" rows, plus play_mission_passed_tune and flash_hud_object (3.4): mirrored for the host's mission family
inline bool IsTextCommand(uint16_t opcode)
{
    switch (opcode)
    {
        case Op::PRINT_BIG:
        case Op::PRINT:
        case Op::PRINT_NOW:
        case Op::CLEAR_PRINTS:
        case Op::PRINT_WITH_NUMBER_BIG:
        case Op::PRINT_WITH_NUMBER:
        case Op::PRINT_WITH_NUMBER_NOW:
        case Op::PRINT_BIG_Q:
        case Op::PRINT_WITH_2_NUMBERS_NOW:
        case Op::PRINT_WITH_3_NUMBERS:
        case Op::PRINT_WITH_4_NUMBERS:
        case Op::PRINT_WITH_4_NUMBERS_NOW:
        case Op::PRINT_WITH_6_NUMBERS:
        case Op::PRINT_WITH_2_NUMBERS_BIG:
        case Op::PRINT_STRING_IN_STRING_NOW:
        case Op::PLAY_MISSION_PASSED_TUNE:
        case Op::CLEAR_THIS_PRINT:
        case Op::CLEAR_THIS_BIG_PRINT:
        case Op::PRINT_HELP:
        case Op::CLEAR_HELP:
        case Op::FLASH_HUD_OBJECT:
        case Op::CLEAR_SMALL_PRINTS:
        case Op::PRINT_HELP_FOREVER:
        case Op::PRINT_HELP_FOREVER_WITH_NUMBER:
        case Op::LOAD_MISSION_TEXT:
        case Op::ADD_NEXT_MESSAGE_TO_PREVIOUS_BRIEFS:
        case Op::CLEAR_THIS_PRINT_BIG_NOW:
            return true;
        default:
            return false;
    }
}

// the prints that show the text of their keys: a receiver needs the host's mission table for a key that isn't in the
// main table (the clears only compare text pointers and never need a table)
inline bool IsKeyedPrint(uint16_t opcode)
{
    switch (opcode)
    {
        case Op::PRINT_BIG:
        case Op::PRINT:
        case Op::PRINT_NOW:
        case Op::PRINT_WITH_NUMBER_BIG:
        case Op::PRINT_WITH_NUMBER:
        case Op::PRINT_WITH_NUMBER_NOW:
        case Op::PRINT_BIG_Q:
        case Op::PRINT_WITH_2_NUMBERS_NOW:
        case Op::PRINT_WITH_3_NUMBERS:
        case Op::PRINT_WITH_4_NUMBERS:
        case Op::PRINT_WITH_4_NUMBERS_NOW:
        case Op::PRINT_WITH_6_NUMBERS:
        case Op::PRINT_WITH_2_NUMBERS_BIG:
        case Op::PRINT_STRING_IN_STRING_NOW:
        case Op::PRINT_HELP:
        case Op::PRINT_HELP_FOREVER:
        case Op::PRINT_HELP_FOREVER_WITH_NUMBER:
            return true;
        default:
            return false;
    }
}

// the commands that change the help box
inline bool IsHelpCommand(uint16_t opcode)
{
    return opcode == Op::PRINT_HELP || opcode == Op::PRINT_HELP_FOREVER || opcode == Op::PRINT_HELP_FOREVER_WITH_NUMBER ||
           opcode == Op::CLEAR_HELP;
}

// M-D3: money and respect leave COpCodeSync's general replay; the mirror sends the ones a passed mission gives
inline bool IsRewardCommand(uint16_t opcode)
{
    return opcode == Op::ADD_SCORE || opcode == Op::AWARD_PLAYER_MISSION_RESPECT;
}

// M-D5: the MAIN dialogue helpers missions start to play a line and print its subtitle count as the mission. Script names
// are lower case in memory (script_name 03A4, case 0x4846A1, lowers A-Z before it copies 8 bytes).
inline bool IsDialogueHelper(const char* name)
{
    static const char* const helpers[] = {"audiol", "a_cont", "a_alap"};
    for (const char* helper : helpers)
    {
        if (EqualsNoCase(name, helper, 8))
        {
            return true;
        }
    }
    return false;
}

inline bool IsMissionExcluded(int missionId)
{
    return missionId >= 0 && missionId <= LAST_EXCLUDED_MISSION;
}

// The host's mission as this game saw it start: the id that load_and_launch_mission (0417) took, until its script leaves
// CTheScripts::pActiveScripts.
class CMissionTracker
{
public:
    void OnLaunched(int missionId)
    {
        m_nMissionId = missionId < 0 ? UNKNOWN_MISSION : missionId;
    }
    void OnDetected()
    {
        m_nMissionId = UNKNOWN_MISSION;
    }
    void OnEnded()
    {
        m_nMissionId = NO_MISSION;
    }

    bool IsRunning() const { return m_nMissionId != NO_MISSION; }
    int GetMissionId() const { return m_nMissionId; }
    // a mission runs whose display is mirrored (M-D2)
    bool IsMirrored() const { return IsRunning() && !IsMissionExcluded(m_nMissionId); }

private:
    int m_nMissionId = NO_MISSION;
};

// what the family rule reads from a CRunningScript
struct SScriptInfo
{
    bool bIsMission = false;  // m_bIsMission (+0xDC), set by the 0417 case at 0x489A05
    uintptr_t ip = 0;         // m_pCurrentIP (+0x14)
    char name[8]{};           // m_szName (+0x08)
};

// The host's mission family (design 3.2): while the host runs a mission that isn't excluded, the mission script, a script
// whose instruction pointer is in the mission block (one the mission started), and the dialogue helpers (M-D5). Strand
// threads, phone calls, shops, external scripts and the engine's own messages are not in it. The caller checks that this
// game is the host and that the command isn't a network replay.
inline bool IsFamily(const SScriptInfo& script, const CMissionTracker& mission)
{
    if (!mission.IsMirrored())
    {
        return false;
    }
    if (script.bIsMission)
    {
        return true;
    }
    if (script.ip >= MISSION_BLOCK && script.ip < MISSION_BLOCK + MISSION_BLOCK_SIZE)
    {
        return true;
    }
    return IsDialogueHelper(script.name);
}

// a serialized command, as far as the mirror reads it (the PED/PLAYER/VEHICLE parameters of COpCodeSync's complex
// opcodes hold network ids there, not the script's values)
struct SCommandView
{
    uint16_t opcode = 0;
    int intCount = 0;
    int32_t ints[MAX_PARAMS]{};
    int textCount = 0;
    char texts[MAX_PARAMS][MAX_TEXT]{};
};

// false when the buffer is cut short
inline bool ParseCommand(const uint8_t* data, int size, SCommandView& view)
{
    view = SCommandView{};
    if (data == nullptr || size < HEADER_SIZE)
    {
        return false;
    }
    view.opcode = static_cast<uint16_t>((data[0] | (data[1] << 8)) & 0x7FFF);  // without the NOT flag of conditions
    view.intCount = data[2] & 0x0F;
    view.textCount = data[2] >> 4;

    int offset = HEADER_SIZE;
    if (size - offset < view.intCount * 4)
    {
        return false;
    }
    for (int i = 0; i < view.intCount; i++)
    {
        uint32_t value = 0;
        for (int b = 0; b < 4; b++)
        {
            value |= static_cast<uint32_t>(data[offset + b]) << (8 * b);
        }
        view.ints[i] = static_cast<int32_t>(value);
        offset += 4;
    }
    for (int i = 0; i < view.textCount; i++)
    {
        if (offset >= size)
        {
            return false;
        }
        const int length = data[offset++];
        if (size - offset < length)
        {
            return false;
        }
        const int copied = length < MAX_TEXT - 1 ? length : MAX_TEXT - 1;
        for (int c = 0; c < copied; c++)
        {
            view.texts[i][c] = static_cast<char>(data[offset + c]);
        }
        view.texts[i][copied] = '\0';
        offset += length;
    }
    return true;
}

// M-D4: a mission title, print_big with style 2; from any of the host's scripts
inline bool IsTitle(const SCommandView& view)
{
    return view.opcode == Op::PRINT_BIG && view.intCount >= 2 && view.ints[1] == TITLE_STYLE;
}

// the "Mission passed" big print (M_PASS, M_PASSS, M_PASSR, M_PASSD keys)
inline bool IsPassPrint(const SCommandView& view)
{
    switch (view.opcode)
    {
        case Op::PRINT_BIG:
        case Op::PRINT_WITH_NUMBER_BIG:
        case Op::PRINT_BIG_Q:
        case Op::PRINT_WITH_2_NUMBERS_BIG:
            return view.textCount >= 1 && EqualsNoCase(view.texts[0], "M_PASS", 6);
        default:
            return false;
    }
}

// the pass signals that need no parameters: register_mission_passed and play_mission_passed_tune
inline bool IsPassCommand(uint16_t opcode)
{
    return opcode == Op::REGISTER_MISSION_PASSED || opcode == Op::PLAY_MISSION_PASSED_TUNE;
}

// the money or respect a reward command adds (0 when it isn't one)
inline int32_t GetRewardValue(const SCommandView& view)
{
    if (view.opcode == Op::ADD_SCORE && view.intCount >= 2)
    {
        return view.ints[1];
    }
    if (view.opcode == Op::AWARD_PLAYER_MISSION_RESPECT && view.intCount >= 1)
    {
        return view.ints[0];
    }
    return 0;
}

// M-D3: the positive money and respect the family gives are held until the end of the script frame (CTheScripts::Process).
// When a pass signal of the family (register_mission_passed, the M_PASS print, play_mission_passed_tune) came in the same
// frame, in any order, they go to every player; otherwise they are dropped: bets, in-mission costs and payouts stay with
// the host. Negative values are never held.
class CRewardFrame
{
public:
    static constexpr int MAX_HELD = 8;
    static constexpr int MAX_SIZE = 64;  // a serialized reward has 12 bytes

    // a reward command of the family: false when it isn't held (not positive, too big, buffer full)
    bool Hold(const SCommandView& view, const uint8_t* data, int size)
    {
        if (GetRewardValue(view) <= 0 || data == nullptr || size <= 0 || size > MAX_SIZE || m_nHeld >= MAX_HELD)
        {
            return false;
        }
        std::memcpy(m_held[m_nHeld].data, data, static_cast<size_t>(size));
        m_held[m_nHeld].size = size;
        m_nHeld++;
        return true;
    }

    void OnPassSignal() { m_bPassed = true; }
    bool HasPassed() const { return m_bPassed; }
    int GetHeldCount() const { return m_nHeld; }

    // the end of the script frame: send(data, size) for every held reward when the mission passed in this frame; returns
    // how many were sent, dropped gets how many were dropped
    template <typename Send>
    int EndFrame(Send&& send, int& dropped)
    {
        int sent = 0;
        dropped = 0;
        for (int i = 0; i < m_nHeld; i++)
        {
            if (m_bPassed)
            {
                send(m_held[i].data, m_held[i].size);
                sent++;
            }
            else
            {
                dropped++;
            }
        }
        Reset();
        return sent;
    }

    void Reset()
    {
        m_nHeld = 0;
        m_bPassed = false;
    }

private:
    struct SHeld
    {
        uint8_t data[MAX_SIZE];
        int size;
    };
    SHeld m_held[MAX_HELD]{};
    int m_nHeld = 0;
    bool m_bPassed = false;
};

// A text the host sent, kept for the join snapshot (design 3.11): the last objective (a small print) and the last help.
struct STrackedText
{
    bool bValid = false;
    uint16_t opcode = 0;
    char key[8]{};
    int32_t flag = 0;  // print and print_now: their flag parameter

    void Set(const SCommandView& view)
    {
        bValid = view.textCount >= 1;
        opcode = view.opcode;
        std::memset(key, 0, sizeof(key));
        if (bValid)
        {
            std::memcpy(key, view.texts[0], sizeof(key) - 1);
        }
        flag = (opcode == Op::PRINT || opcode == Op::PRINT_NOW) && view.intCount >= 2 ? view.ints[1] : 0;
    }
    void Clear() { *this = STrackedText{}; }
};

// the small prints, the text a joiner may still need to see (the current objective or subtitle)
inline bool IsObjectivePrint(uint16_t opcode)
{
    switch (opcode)
    {
        case Op::PRINT:
        case Op::PRINT_NOW:
        case Op::PRINT_WITH_NUMBER:
        case Op::PRINT_WITH_NUMBER_NOW:
        case Op::PRINT_WITH_2_NUMBERS_NOW:
        case Op::PRINT_WITH_3_NUMBERS:
        case Op::PRINT_WITH_4_NUMBERS:
        case Op::PRINT_WITH_4_NUMBERS_NOW:
        case Op::PRINT_WITH_6_NUMBERS:
        case Op::PRINT_STRING_IN_STRING_NOW:
            return true;
        default:
            return false;
    }
}

// What ADD_MESSAGE_GXT (key, time, flag, one player) can carry for a joiner: an objective without numbers, and a help
// box without a number (a permanent one arrives as an ordinary help box).
inline bool CanSnapshotObjective(const STrackedText& text)
{
    return text.bValid && (text.opcode == Op::PRINT || text.opcode == Op::PRINT_NOW);
}

inline bool CanSnapshotHelp(const STrackedText& text)
{
    return text.bValid && (text.opcode == Op::PRINT_HELP || text.opcode == Op::PRINT_HELP_FOREVER);
}

// time left of a message that started at start (CTimer::m_snTimeInMilliseconds) and lasts duration ms
inline uint32_t GetTimeLeft(uint32_t start, uint32_t duration, uint32_t now)
{
    const uint32_t elapsed = now - start;
    return elapsed >= duration ? 0 : duration - elapsed;
}

// The receiver's table guard (design 3.4): a replayed print whose key isn't in the main table needs the host's mission
// table. True when that table must be loaded first: it is known and isn't the one loaded (a receiver's own script, a gym
// for example, may have loaded another). Loading clears this game's messages, as LoadMissionText always does.
inline bool NeedsHostTable(bool bKeyInMainTable, const char* hostTable, bool bMissionTableLoaded, const char* loadedTable)
{
    if (bKeyInMainTable || hostTable == nullptr || hostTable[0] == '\0')
    {
        return false;
    }
    return !bMissionTableLoaded || loadedTable == nullptr || std::strncmp(hostTable, loadedTable, 8) != 0;
}
}  // namespace MirrorPolicy
