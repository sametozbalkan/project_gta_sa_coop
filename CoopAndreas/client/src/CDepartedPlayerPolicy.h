#pragma once

#include <cstddef>
#include <cstdint>

// Private build: another player leaving during the host's mission (CDepartedPlayerPeds; docs/mission-porting.md, "A
// player who leaves during the mission"). The plain rules, without game types, so tests/player_leave_test.cpp checks
// them without GTA.
namespace DepartedPlayerPolicy
{
// The host keeps a departed player's ped as a stand-in while a mission of its own runs: the mission script may still
// hold the ped's handle ($NETWORK_PLAYER[i], from Coop.CollectNetworkPlayersForTheMission), and the vanilla commands
// that take a ped handle use the ped pool's answer without checking it (1.0 US: IS_CHAR_IN_ANY_CAR 00DF case 0x469667,
// SET_CHAR_COORDINATES 00A1 through 0x464DC0, SET_CHAR_HEADING 0173 case 0x47CB44, GIVE_WEAPON_TO_CHAR 01B2 case
// 0x47D307, the TASK_* commands through CRunningScript::GivePedScriptedTask 0x465C20, which reads [ped+0x474] at
// 0x465CC8). Only a ped with a clump and intelligence: those commands may flush its tasks, which plays animations.
inline bool ShouldKeepStandIn(bool bIsHost, bool bLocalMissionRunning, bool bModelReady)
{
    return bIsHost && bLocalMissionRunning && bModelReady;
}

// the stand-ins go once no mission of this game runs any more (the handles they stand for went with the mission script)
inline bool ShouldReleaseStandIns(bool bLocalMissionRunning)
{
    return !bLocalMissionRunning;
}

// A stand-in waits this far below where its player left: out of the range in which peds scan for others and of every
// explosion, so nothing in the mission reacts to the invisible ped
constexpr float PARK_DEPTH = 200.0f;
// a script command that moved it (SET_CHAR_COORDINATES on the stored handle) is undone beyond this distance
constexpr float PARK_TOLERANCE = 1.0f;

// What a stand-in looks like after a frame's script commands: a mission that skips Coop.IsNetworkPlayerActorValid runs
// vanilla commands on it
struct SStandInState
{
    bool bInVehicle = false;        // a command seated it (WARP_CHAR_INTO_CAR...)
    bool bInMovingList = false;     // CWorld::Process would process it (CWorld::Add leaves a static entity out)
    bool bVisible = false;
    bool bUsesCollision = false;
    bool bStatic = true;
    bool bRemoveFromWorld = false;  // flagged for deletion (CPed::FlagToDestroyWhenNextProcessed)
    float distanceFromPark = 0.0f;
};

enum eStandInFix : uint8_t
{
    STANDIN_FIX_NONE = 0,
    STANDIN_FIX_LEAVE_VEHICLE = 1,  // give the seat back first
    STANDIN_FIX_PARK = 2,           // out of the world, hidden, no collision, static, back to its place, into the world
};

inline uint8_t GetStandInFixes(const SStandInState& state)
{
    uint8_t fixes = STANDIN_FIX_NONE;
    if (state.bInVehicle)
    {
        fixes |= STANDIN_FIX_LEAVE_VEHICLE;
    }
    if (state.bInVehicle || state.bInMovingList || state.bVisible || state.bUsesCollision || !state.bStatic ||
        state.bRemoveFromWorld || state.distanceFromPark > PARK_TOLERANCE)
    {
        fixes |= STANDIN_FIX_PARK;
    }
    return fixes;
}

// The stand-ins of this game, a fixed list: one per player who left during the current mission (a player who returns
// and leaves again adds another). TExtra is what the caller keeps with each one.
template <class TPed, class TExtra, size_t N>
class CStandInList
{
public:
    struct SEntry
    {
        TPed* pPed = nullptr;
        TExtra extra{};
    };

    size_t Size() const { return m_nSize; }
    bool IsEmpty() const { return m_nSize == 0; }
    bool IsFull() const { return m_nSize >= N; }
    SEntry& At(size_t index) { return m_aEntries[index]; }

    SEntry* Find(const TPed* pPed)
    {
        for (size_t i = 0; i < m_nSize; i++)
        {
            if (m_aEntries[i].pPed == pPed)
            {
                return &m_aEntries[i];
            }
        }
        return nullptr;
    }

    // nullptr when full, for a null ped or for one that is in the list already
    SEntry* Add(TPed* pPed, const TExtra& extra)
    {
        if (pPed == nullptr || IsFull() || Find(pPed) != nullptr)
        {
            return nullptr;
        }
        SEntry& entry = m_aEntries[m_nSize++];
        entry.pPed = pPed;
        entry.extra = extra;
        return &entry;
    }

    // takes the entry out (the last one moves into its place); false when it isn't there
    bool Remove(const TPed* pPed, TExtra* pRemoved = nullptr)
    {
        for (size_t i = 0; i < m_nSize; i++)
        {
            if (m_aEntries[i].pPed == pPed)
            {
                if (pRemoved != nullptr)
                {
                    *pRemoved = m_aEntries[i].extra;
                }
                m_aEntries[i] = m_aEntries[m_nSize - 1];
                m_aEntries[m_nSize - 1] = SEntry{};
                m_nSize--;
                return true;
            }
        }
        return false;
    }

private:
    SEntry m_aEntries[N]{};
    size_t m_nSize = 0;
};
}  // namespace DepartedPlayerPolicy
