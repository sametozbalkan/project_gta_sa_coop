#pragma once

#include "network/packets/playerstate.h"

// Private build: player state for co-op saves.
//
// A co-op save is the host's single-player save, so the other players' weapons, armour, money, clothes, skills and
// perks used to live only in their own game and were lost with every new session. Now each of them sends the host its
// state (PLAYER_STATE_SNAPSHOT, at most every 2 s and only when it changed); the host keeps the latest per nickname,
// writes them next to its save file when it saves ("GTASAsf<n>.b.coop", fingerprinted with the save's size and hash,
// written atomically, removed when there is nothing to store) and reads them back when it loads that save. A player
// whose stored state the host has gets it back (PLAYER_STATE_RESTORE) when they connect, reconnect after a crash or
// restart their game, and when the host loads a save that stores them (an older save rolls them back).
// The .b save, MAIN, the globals and the ped pool stay exactly as before.
//
// Identity is the coopandreas.ini nickname as the server sees it: a player who changes it isn't found any more.
class CPlayerStateSave
{
public:
    // every frame, from Main.cpp (gameProcessEvent)
    static void Process();

    // from Hooks/SaveLoadHooks.cpp, after the game wrote or read a save file successfully
    static void OnGameSaved(const char* saveFileName);
    static void OnGameLoaded(const char* saveFileName);

    // from PacketHandlers/playerstate.cpp
    static void HandleSnapshot(const Packets::PlayerState::PlayerStateSnapshot& packet);
    static void HandleRestore(const Packets::PlayerState::PlayerStateRestore& packet);
};
