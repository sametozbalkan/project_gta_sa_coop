#pragma once

#include <cstddef>

// Private build: story missions start only from the host's game (Hooks/PlayerHooks.cpp, CAN_PLAYER_START_MISSION 03EE;
// docs/mission-porting.md, "Where the story progression lives"). The plain rule, so tests/player_leave_test.cpp checks it
// without GTA.
namespace StoryStartPolicy
{
// The trigger threads of the story strands in MAIN. Each asks CAN_PLAYER_START_MISSION right before it launches a story
// mission (CESAR, BCESAR and TRACE also the race tournament CPRACE their missions unlock). The side activities (GYM,
// SHOOT, TRUCKS, QUARRYS, BSCHOO, BIKES, BLOODR, HOTR, KICKS, TRI), the vehicle sub-missions, the shop and casino
// scripts and the girlfriends ask it too, or don't ask it, and aren't in this list.
constexpr const char* STORY_TRIGGER_THREADS[] = {"INT", "SWEET", "CRASH", "RYDER", "SMOKE", "STRAP", "CESAR", "LA1FIN",
    "BCRASH", "CAT", "TRU", "BCESAR", "GARAGE", "SCRASH", "WUZI", "SYND", "STEAL", "TRACE", "ZERO", "DESERT", "PSCH",
    "CASINO", "VCRASH", "DOC", "HEIST", "MANSION", "GROVE", "RIOT"};

// a script name (CRunningScript::m_szName: up to 8 characters, not always terminated) equals expected, ignoring case
inline bool NameEquals(const char* name, size_t maxLength, const char* expected)
{
    size_t i = 0;
    for (; i < maxLength && name[i] != '\0'; i++)
    {
        if (expected[i] == '\0')
        {
            return false;
        }
        char a = name[i];
        char b = expected[i];
        if (a >= 'a' && a <= 'z')
        {
            a = static_cast<char>(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z')
        {
            b = static_cast<char>(b - 'a' + 'A');
        }
        if (a != b)
        {
            return false;
        }
    }
    return i >= maxLength || expected[i] == '\0';
}

inline bool IsStoryTriggerThread(const char* name, size_t maxLength)
{
    if (name == nullptr)
    {
        return false;
    }
    for (const char* thread : STORY_TRIGGER_THREADS)
    {
        if (NameEquals(name, maxLength, thread))
        {
            return true;
        }
    }
    return false;
}

// CAN_PLAYER_START_MISSION for the script that asks (nullptr: not asked by a script): the game's answer, except false for
// a story trigger thread on a game in a session that isn't the host, also while no host is known (the story waits for
// its host). Alone (not connected) every game keeps the game's answer.
inline bool MayStartMission(bool bGameAnswer, bool bInSession, bool bIsHost, const char* scriptName, size_t maxLength)
{
    if (!bGameAnswer)
    {
        return false;
    }
    if (!bInSession || bIsHost)
    {
        return true;
    }
    return !IsStoryTriggerThread(scriptName, maxLength);
}
}  // namespace StoryStartPolicy
