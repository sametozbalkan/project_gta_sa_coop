// Private build: policy test for the mission mirror layer without GTA (client/src/CMirrorPolicy.h, used by CMissionMirror):
// the mission exclusion, the family rule, the title rule, the reward frame (pass frame, positive only, bets and costs),
// the OPCODE_SYNC parser, the join snapshot helpers and the receiver's table guard. Build and run: see CLAUDE.md (Fork
// changes, mission mirror layer).
#include "../client/src/CMirrorPolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace MirrorPolicy;

namespace
{
// a command as COpCodeSync::SerializeOpcode writes it (MSVC puts the int count in the low 4 bits)
std::vector<uint8_t> Serialize(uint16_t opcode, std::initializer_list<int32_t> ints, std::initializer_list<const char*> texts)
{
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(opcode & 0xFF));
    data.push_back(static_cast<uint8_t>(opcode >> 8));
    data.push_back(static_cast<uint8_t>((ints.size() & 0x0F) | ((texts.size() & 0x0F) << 4)));
    data.push_back(0xCD);  // the padding byte, uninitialized in the game
    for (int32_t value : ints)
    {
        for (int b = 0; b < 4; b++)
        {
            data.push_back(static_cast<uint8_t>(static_cast<uint32_t>(value) >> (8 * b)));
        }
    }
    for (const char* text : texts)
    {
        const size_t length = std::strlen(text);
        data.push_back(static_cast<uint8_t>(length));
        data.insert(data.end(), text, text + length);
    }
    return data;
}

SCommandView View(const std::vector<uint8_t>& data)
{
    SCommandView view;
    CHECK(ParseCommand(data.data(), static_cast<int>(data.size()), view));
    return view;
}

SScriptInfo Script(bool bIsMission, uintptr_t ip, const char* name)
{
    SScriptInfo script;
    script.bIsMission = bIsMission;
    script.ip = ip;
    const size_t length = std::strlen(name);
    std::memcpy(script.name, name, length < sizeof(script.name) ? length : sizeof(script.name));  // like m_szName
    return script;
}

constexpr uintptr_t SCRIPT_SPACE = 0xA49960;  // MAIN's code (CTheScripts::ScriptSpace)
constexpr uintptr_t EXTERNAL_IP = 0x1F00000;  // a streamed script's own buffer

// what CMissionMirror does for a reward command of the family
bool HoldReward(CRewardFrame& frame, uint16_t opcode, std::initializer_list<int32_t> ints)
{
    const std::vector<uint8_t> data = Serialize(opcode, ints, {});
    return frame.Hold(View(data), data.data(), static_cast<int>(data.size()));
}

// the end of a script frame: the opcodes and values that went out
struct SFrameResult
{
    std::vector<uint16_t> opcodes;
    std::vector<int32_t> values;
    int dropped = 0;
};

SFrameResult EndFrame(CRewardFrame& frame)
{
    SFrameResult result;
    frame.EndFrame(
        [&result](const uint8_t* data, int size)
        {
            SCommandView view;
            CHECK(ParseCommand(data, size, view));
            result.opcodes.push_back(view.opcode);
            result.values.push_back(GetRewardValue(view));
        },
        result.dropped);
    return result;
}

void MissionExclusion()
{
    // M-D2: missions 0-10 (new-game missions, arcade, betting, pool, lowrider game, Beefy Baron)
    for (int id = 0; id <= 10; id++)
    {
        CHECK(IsMissionExcluded(id));
    }
    CHECK(!IsMissionExcluded(11));   // INTRO1, Big Smoke
    CHECK(!IsMissionExcluded(19));   // SWEET6, Cesar Vialpando
    CHECK(!IsMissionExcluded(134));
    CHECK(!IsMissionExcluded(UNKNOWN_MISSION));
    CHECK(!IsMissionExcluded(NO_MISSION));

    CMissionTracker mission;
    CHECK(!mission.IsRunning() && !mission.IsMirrored() && mission.GetMissionId() == NO_MISSION);
    mission.OnLaunched(2);  // INTRO on every game at New Game
    CHECK(mission.IsRunning() && !mission.IsMirrored());
    mission.OnLaunched(9);  // LOWR, the lowrider bet game
    CHECK(mission.IsRunning() && !mission.IsMirrored());
    mission.OnLaunched(17);  // SWEET4
    CHECK(mission.IsRunning() && mission.IsMirrored() && mission.GetMissionId() == 17);
    mission.OnEnded();
    CHECK(!mission.IsRunning() && !mission.IsMirrored());
    mission.OnDetected();  // the debug mission runner: id unknown, mirrored
    CHECK(mission.IsRunning() && mission.IsMirrored() && mission.GetMissionId() == UNKNOWN_MISSION);
    mission.OnLaunched(-7);  // a bad id is unknown, never "no mission"
    CHECK(mission.IsRunning() && mission.GetMissionId() == UNKNOWN_MISSION);
}

void FamilyRule()
{
    const SScriptInfo missionScript = Script(true, MISSION_BLOCK + 1234, "sweet4");
    const SScriptInfo subScript = Script(false, MISSION_BLOCK + 2000, "sw4_sub");
    const SScriptInfo firstByte = Script(false, MISSION_BLOCK, "x");
    const SScriptInfo lastByte = Script(false, MISSION_BLOCK + MISSION_BLOCK_SIZE - 1, "x");
    const SScriptInfo pastBlock = Script(false, MISSION_BLOCK + MISSION_BLOCK_SIZE, "x");
    const SScriptInfo beforeBlock = Script(false, MISSION_BLOCK - 1, "x");
    const SScriptInfo strandThread = Script(false, SCRIPT_SPACE + 5000, "sweet");
    const SScriptInfo helpThread = Script(false, SCRIPT_SPACE + 9000, "help");
    const SScriptInfo shop = Script(false, EXTERNAL_IP, "barber");
    const SScriptInfo audiol = Script(false, SCRIPT_SPACE + 100, "audiol");
    const SScriptInfo audiolUpper = Script(false, SCRIPT_SPACE + 100, "AUDIOL");
    const SScriptInfo aCont = Script(false, SCRIPT_SPACE + 200, "a_cont");
    const SScriptInfo aAlap = Script(false, SCRIPT_SPACE + 300, "a_alap");
    const SScriptInfo aTerm = Script(false, SCRIPT_SPACE + 400, "a_term");  // not one of the three
    const SScriptInfo audio = Script(false, SCRIPT_SPACE + 500, "audio");

    // no mission: nothing is the family, not even a dialogue helper
    CMissionTracker mission;
    CHECK(!IsFamily(missionScript, mission));
    CHECK(!IsFamily(subScript, mission));
    CHECK(!IsFamily(audiol, mission));

    mission.OnLaunched(17);
    CHECK(IsFamily(missionScript, mission));
    CHECK(IsFamily(subScript, mission));
    CHECK(IsFamily(firstByte, mission));
    CHECK(IsFamily(lastByte, mission));
    CHECK(!IsFamily(pastBlock, mission));
    CHECK(!IsFamily(beforeBlock, mission));
    CHECK(!IsFamily(strandThread, mission));  // the title it prints goes by the title rule
    CHECK(!IsFamily(helpThread, mission));
    CHECK(!IsFamily(shop, mission));
    CHECK(IsFamily(audiol, mission));
    CHECK(IsFamily(audiolUpper, mission));
    CHECK(IsFamily(aCont, mission));
    CHECK(IsFamily(aAlap, mission));
    CHECK(!IsFamily(aTerm, mission));
    CHECK(!IsFamily(audio, mission));

    // excluded missions: the mission script, its sub-scripts and the helpers are not mirrored
    for (int id : {0, 1, 2, 7, 8, 9, 10})
    {
        mission.OnLaunched(id);
        CHECK(!IsFamily(missionScript, mission));
        CHECK(!IsFamily(subScript, mission));
        CHECK(!IsFamily(audiol, mission));
    }

    // the debug runner's mission (id unknown) is mirrored
    mission.OnDetected();
    CHECK(IsFamily(missionScript, mission));
    mission.OnEnded();
    CHECK(!IsFamily(missionScript, mission));
}

void TitleRule()
{
    // M-D4: print_big style 2 (TextStyle.BottomRight)
    CHECK(IsTitle(View(Serialize(Op::PRINT_BIG, {1000, 2}, {"SWEET_4"}))));
    CHECK(!IsTitle(View(Serialize(Op::PRINT_BIG, {5000, 1}, {"M_FAIL"}))));   // Middle
    CHECK(!IsTitle(View(Serialize(Op::PRINT_BIG, {3000, 5}, {"RACES"}))));    // MiddleSmallerHigher
    CHECK(!IsTitle(View(Serialize(Op::PRINT_WITH_NUMBER_BIG, {200, 5000, 2}, {"M_PASS"}))));
    CHECK(!IsTitle(View(Serialize(Op::PRINT_BIG_Q, {1000, 2}, {"SWEET_4"}))));
    CHECK(!IsTitle(View(Serialize(Op::PRINT_NOW, {1000, 2}, {"SWE4_A"}))));
    CHECK(!IsTitle(View(Serialize(Op::PRINT_BIG, {1000}, {"SWEET_4"}))));     // style missing
}

void PassSignals()
{
    CHECK(IsPassPrint(View(Serialize(Op::PRINT_WITH_NUMBER_BIG, {200, 5000, 1}, {"M_PASSS"}))));
    CHECK(IsPassPrint(View(Serialize(Op::PRINT_WITH_NUMBER_BIG, {1000, 5000, 1}, {"M_PASS"}))));
    CHECK(IsPassPrint(View(Serialize(Op::PRINT_WITH_NUMBER_BIG, {3, 5000, 1}, {"M_PASSR"}))));
    CHECK(IsPassPrint(View(Serialize(Op::PRINT_BIG, {5000, 1}, {"M_PASSD"}))));  // VCRASH1, VCR2
    CHECK(IsPassPrint(View(Serialize(Op::PRINT_BIG, {5000, 1}, {"m_pass"}))));
    CHECK(!IsPassPrint(View(Serialize(Op::PRINT_BIG, {5000, 1}, {"M_FAIL"}))));
    CHECK(!IsPassPrint(View(Serialize(Op::PRINT_NOW, {5000, 1}, {"M_PASS"}))));
    CHECK(!IsPassPrint(View(Serialize(Op::PRINT_BIG, {5000, 1}, {}))));
    CHECK(IsPassCommand(Op::REGISTER_MISSION_PASSED));
    CHECK(IsPassCommand(Op::PLAY_MISSION_PASSED_TUNE));
    CHECK(!IsPassCommand(Op::PRINT_WITH_NUMBER_BIG));
    CHECK(!IsPassCommand(0x030C));  // player_made_progress
}

void RewardFrame()
{
    CRewardFrame frame;

    // SWEET1: register_mission_passed, M_PASSS print, respect 3, money 200, tune: both reach the others
    frame.OnPassSignal();
    CHECK(HoldReward(frame, Op::AWARD_PLAYER_MISSION_RESPECT, {3}));
    CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 200}));  // the PLAYER parameter holds a network id here
    frame.OnPassSignal();
    SFrameResult result = EndFrame(frame);
    CHECK(result.opcodes.size() == 2 && result.dropped == 0);
    CHECK(result.opcodes[0] == Op::AWARD_PLAYER_MISSION_RESPECT && result.values[0] == 3);
    CHECK(result.opcodes[1] == Op::ADD_SCORE && result.values[1] == 200);
    CHECK(!frame.HasPassed() && frame.GetHeldCount() == 0);

    // SWEET4: money and respect before the M_PASSS print in the same frame
    CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 500}));
    CHECK(HoldReward(frame, Op::AWARD_PLAYER_MISSION_RESPECT, {6}));
    frame.OnPassSignal();
    result = EndFrame(frame);
    CHECK(result.values.size() == 2 && result.values[0] == 500 && result.values[1] == 6 && result.dropped == 0);

    // SWEET6 (#10): the stake is negative and never held; a bet change in a frame without a pass is dropped; the pass
    // frame's payout (twice the bet, the M_PASS number) goes out
    CHECK(!HoldReward(frame, Op::ADD_SCORE, {0x10000001, -500}));
    CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 100}));  // lowering the bet gives money back
    result = EndFrame(frame);
    CHECK(result.opcodes.empty() && result.dropped == 1);
    frame.OnPassSignal();
    CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 1000}));
    result = EndFrame(frame);
    CHECK(result.values.size() == 1 && result.values[0] == 1000);

    // HEIST3: the in-mission cost is negative
    CHECK(!HoldReward(frame, Op::ADD_SCORE, {0x10000001, -200}));
    result = EndFrame(frame);
    CHECK(result.opcodes.empty() && result.dropped == 0);

    // VCRASH1, VCR2: add_score 0 in the pass frame gives nothing
    frame.OnPassSignal();
    CHECK(!HoldReward(frame, Op::ADD_SCORE, {0x10000001, 0}));
    result = EndFrame(frame);
    CHECK(result.opcodes.empty());

    // FINALEC: the 250,000 comes frames before the pass and is dropped; the pass frame's respect goes out
    CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 250000}));
    result = EndFrame(frame);
    CHECK(result.opcodes.empty() && result.dropped == 1);
    frame.OnPassSignal();
    CHECK(HoldReward(frame, Op::AWARD_PLAYER_MISSION_RESPECT, {150}));
    result = EndFrame(frame);
    CHECK(result.values.size() == 1 && result.values[0] == 150);

    // a pass without rewards (CATALIN, LA1FIN2) sends nothing
    frame.OnPassSignal();
    result = EndFrame(frame);
    CHECK(result.opcodes.empty() && result.dropped == 0 && !frame.HasPassed());

    // negative respect is never held; a command that isn't a reward isn't either
    CHECK(!HoldReward(frame, Op::AWARD_PLAYER_MISSION_RESPECT, {-5}));
    CHECK(!HoldReward(frame, Op::PRINT_NOW, {1000, 1}));
    CHECK(!HoldReward(frame, Op::ADD_SCORE, {500}));  // malformed: the money parameter is missing

    // at most MAX_HELD per frame
    for (int i = 0; i < CRewardFrame::MAX_HELD; i++)
    {
        CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 10 + i}));
    }
    CHECK(!HoldReward(frame, Op::ADD_SCORE, {0x10000001, 99}));
    frame.OnPassSignal();
    result = EndFrame(frame);
    CHECK(static_cast<int>(result.values.size()) == CRewardFrame::MAX_HELD && result.values.back() == 10 + CRewardFrame::MAX_HELD - 1);

    // the bytes that go out are the bytes that were held
    const std::vector<uint8_t> data = Serialize(Op::ADD_SCORE, {0x10000001, 12345}, {});
    CHECK(frame.Hold(View(data), data.data(), static_cast<int>(data.size())));
    frame.OnPassSignal();
    bool bSame = false;
    int dropped = -1;
    frame.EndFrame([&](const uint8_t* sent, int size)
        { bSame = size == static_cast<int>(data.size()) && std::memcmp(sent, data.data(), data.size()) == 0; }, dropped);
    CHECK(bSame && dropped == 0);

    // Reset (a restart) forgets a frame
    CHECK(HoldReward(frame, Op::ADD_SCORE, {0x10000001, 1}));
    frame.OnPassSignal();
    frame.Reset();
    result = EndFrame(frame);
    CHECK(result.opcodes.empty() && result.dropped == 0);
}

void Parser()
{
    SCommandView view = View(Serialize(Op::PRINT_NOW, {4000, 1}, {"SWE4_A"}));
    CHECK(view.opcode == Op::PRINT_NOW && view.intCount == 2 && view.ints[0] == 4000 && view.ints[1] == 1);
    CHECK(view.textCount == 1 && std::strcmp(view.texts[0], "SWE4_A") == 0);

    // two keys (print_string_in_string_now), negative numbers, the NOT flag of conditions
    view = View(Serialize(Op::PRINT_STRING_IN_STRING_NOW, {-1, 2}, {"KEY1", "KEY2"}));
    CHECK(view.textCount == 2 && std::strcmp(view.texts[1], "KEY2") == 0 && view.ints[0] == -1);
    view = View(Serialize(0x8000 | Op::CLEAR_HELP, {}, {}));
    CHECK(view.opcode == Op::CLEAR_HELP && view.intCount == 0 && view.textCount == 0);

    // a long text is cut in the view only
    view = View(Serialize(Op::LOAD_MISSION_TEXT, {}, {"ABCDEFGHIJKLMNOPQRSTUVWXYZ"}));
    CHECK(std::strlen(view.texts[0]) == MAX_TEXT - 1 && std::strncmp(view.texts[0], "ABCDEFGHIJKLMNO", MAX_TEXT - 1) == 0);

    // the most a header holds
    std::vector<uint8_t> full = Serialize(Op::PRINT_WITH_6_NUMBERS, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o"});
    view = View(full);
    CHECK(view.intCount == 15 && view.ints[14] == 15 && view.textCount == 15 && std::strcmp(view.texts[14], "o") == 0);

    // cut short
    std::vector<uint8_t> data = Serialize(Op::PRINT_NOW, {4000, 1}, {"SWE4_A"});
    SCommandView cut;
    CHECK(!ParseCommand(data.data(), 3, cut));
    CHECK(!ParseCommand(data.data(), HEADER_SIZE + 7, cut));                    // inside the ints
    CHECK(!ParseCommand(data.data(), HEADER_SIZE + 8, cut));                    // the length byte is missing
    CHECK(!ParseCommand(data.data(), static_cast<int>(data.size()) - 1, cut));  // the text is short
    CHECK(!ParseCommand(nullptr, 12, cut));
    CHECK(ParseCommand(data.data(), static_cast<int>(data.size()), cut));
}

void Classification()
{
    const uint16_t texts[] = {Op::PRINT_BIG, Op::PRINT, Op::PRINT_NOW, Op::CLEAR_PRINTS, Op::PRINT_WITH_NUMBER_BIG,
        Op::PRINT_WITH_NUMBER, Op::PRINT_WITH_NUMBER_NOW, Op::PRINT_BIG_Q, Op::PRINT_WITH_2_NUMBERS_NOW,
        Op::PRINT_WITH_3_NUMBERS, Op::PRINT_WITH_4_NUMBERS, Op::PRINT_WITH_4_NUMBERS_NOW, Op::PRINT_WITH_6_NUMBERS,
        Op::PRINT_WITH_2_NUMBERS_BIG, Op::PRINT_STRING_IN_STRING_NOW, Op::PLAY_MISSION_PASSED_TUNE, Op::CLEAR_THIS_PRINT,
        Op::CLEAR_THIS_BIG_PRINT, Op::PRINT_HELP, Op::CLEAR_HELP, Op::FLASH_HUD_OBJECT, Op::CLEAR_SMALL_PRINTS,
        Op::PRINT_HELP_FOREVER, Op::PRINT_HELP_FOREVER_WITH_NUMBER, Op::LOAD_MISSION_TEXT,
        Op::ADD_NEXT_MESSAGE_TO_PREVIOUS_BRIEFS, Op::CLEAR_THIS_PRINT_BIG_NOW};
    CHECK(sizeof(texts) / sizeof(texts[0]) == 27);
    for (uint16_t opcode : texts)
    {
        CHECK(IsTextCommand(opcode));
        CHECK(!IsRewardCommand(opcode));
    }
    // not texts: rewards, the pass stat, progress, the mission launch, wait, the wanted and camera commands
    for (uint16_t opcode : {Op::ADD_SCORE, Op::AWARD_PLAYER_MISSION_RESPECT, Op::REGISTER_MISSION_PASSED,
             static_cast<uint16_t>(0x030C), Op::LOAD_AND_LAUNCH_MISSION, static_cast<uint16_t>(0x0001),
             static_cast<uint16_t>(0x010D), static_cast<uint16_t>(0x02EB), static_cast<uint16_t>(0x0A2C),
             static_cast<uint16_t>(0x0A19)})
    {
        CHECK(!IsTextCommand(opcode));
    }
    CHECK(IsRewardCommand(Op::ADD_SCORE) && IsRewardCommand(Op::AWARD_PLAYER_MISSION_RESPECT));
    CHECK(!IsRewardCommand(Op::REGISTER_MISSION_PASSED));

    // the receiver's guard runs only for prints that show their keys
    for (uint16_t opcode : {Op::CLEAR_PRINTS, Op::CLEAR_THIS_PRINT, Op::CLEAR_THIS_BIG_PRINT, Op::CLEAR_HELP,
             Op::CLEAR_SMALL_PRINTS, Op::CLEAR_THIS_PRINT_BIG_NOW, Op::LOAD_MISSION_TEXT, Op::PLAY_MISSION_PASSED_TUNE,
             Op::FLASH_HUD_OBJECT, Op::ADD_NEXT_MESSAGE_TO_PREVIOUS_BRIEFS})
    {
        CHECK(!IsKeyedPrint(opcode));
    }
    for (uint16_t opcode : {Op::PRINT_BIG, Op::PRINT, Op::PRINT_NOW, Op::PRINT_WITH_NUMBER_BIG, Op::PRINT_HELP,
             Op::PRINT_HELP_FOREVER, Op::PRINT_HELP_FOREVER_WITH_NUMBER, Op::PRINT_STRING_IN_STRING_NOW})
    {
        CHECK(IsKeyedPrint(opcode));
    }

    CHECK(IsHelpCommand(Op::PRINT_HELP) && IsHelpCommand(Op::PRINT_HELP_FOREVER) && IsHelpCommand(Op::CLEAR_HELP));
    CHECK(IsHelpCommand(Op::PRINT_HELP_FOREVER_WITH_NUMBER) && !IsHelpCommand(Op::PRINT_NOW));
    CHECK(IsObjectivePrint(Op::PRINT_NOW) && IsObjectivePrint(Op::PRINT) && IsObjectivePrint(Op::PRINT_WITH_NUMBER_NOW));
    CHECK(!IsObjectivePrint(Op::PRINT_BIG) && !IsObjectivePrint(Op::PRINT_HELP) && !IsObjectivePrint(Op::CLEAR_PRINTS));
}

void JoinSnapshot()
{
    // time left
    CHECK(GetTimeLeft(1000, 5000, 3000) == 3000);
    CHECK(GetTimeLeft(1000, 5000, 6000) == 0);
    CHECK(GetTimeLeft(1000, 5000, 9000) == 0);
    CHECK(GetTimeLeft(0xFFFFF000u, 0x2000, 0x00000800u) == 0x0800);  // across the wrap
    CHECK(MIN_SNAPSHOT_TEXT_MS == 1000);

    // the objective: a print_now or print without numbers fits ADD_MESSAGE_GXT
    STrackedText objective;
    CHECK(!CanSnapshotObjective(objective));
    objective.Set(View(Serialize(Op::PRINT_NOW, {7000, 1}, {"SWE4_12"})));
    CHECK(CanSnapshotObjective(objective) && std::strcmp(objective.key, "SWE4_12") == 0 && objective.flag == 1);
    objective.Set(View(Serialize(Op::PRINT, {5000, 0}, {"SWE4_13"})));
    CHECK(CanSnapshotObjective(objective) && objective.flag == 0);
    objective.Set(View(Serialize(Op::PRINT_WITH_NUMBER_NOW, {3, 5000, 1}, {"RACE_7"})));
    CHECK(!CanSnapshotObjective(objective));  // numbers don't fit
    objective.Set(View(Serialize(Op::PRINT_NOW, {7000, 1}, {"ABCDEFGHIJ"})));
    CHECK(std::strcmp(objective.key, "ABCDEFG") == 0);  // a key has 7 characters at most
    objective.Clear();
    CHECK(!CanSnapshotObjective(objective));

    // the help box: print_help and print_help_forever, not the one with a number
    STrackedText help;
    help.Set(View(Serialize(Op::PRINT_HELP, {}, {"SWE1_H1"})));
    CHECK(CanSnapshotHelp(help) && help.flag == 0);
    help.Set(View(Serialize(Op::PRINT_HELP_FOREVER, {}, {"SWE1_H2"})));
    CHECK(CanSnapshotHelp(help));
    help.Set(View(Serialize(Op::PRINT_HELP_FOREVER_WITH_NUMBER, {3}, {"GYM_1"})));
    CHECK(!CanSnapshotHelp(help));
}

void TableGuard()
{
    // a main-table key (titles, M_PASS, most help) never needs a load
    CHECK(!NeedsHostTable(true, "SWEET4", false, ""));
    // no host table known yet: nothing to load
    CHECK(!NeedsHostTable(false, "", true, "GYM"));
    CHECK(!NeedsHostTable(false, nullptr, true, "GYM"));
    // the host's table is the one loaded: the key is just missing, no load
    CHECK(!NeedsHostTable(false, "SWEET4", true, "SWEET4"));
    // the receiver's own script loaded another table, or none is loaded: load the host's
    CHECK(NeedsHostTable(false, "SWEET4", true, "GYM"));
    CHECK(NeedsHostTable(false, "SWEET4", false, "SWEET4"));
    CHECK(NeedsHostTable(false, "SWEET1", true, "SWEET1B"));
    CHECK(NeedsHostTable(false, "SWEET1B", true, "SWEET1"));
    CHECK(NeedsHostTable(false, "SWEET4", true, nullptr));
}
}  // namespace

int main()
{
    MissionExclusion();
    FamilyRule();
    TitleRule();
    PassSignals();
    RewardFrame();
    Parser();
    Classification();
    JoinSnapshot();
    TableGuard();
    std::printf("mission_mirror_test: all checks passed\n");
    return 0;
}
