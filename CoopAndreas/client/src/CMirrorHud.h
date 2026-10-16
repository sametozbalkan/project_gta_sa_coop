#pragma once

#include <cstdint>
#include <cstdio>

class CRunningScript;
namespace Packets::Mirror
{
class MirrorHud;
}

// Private build: the mission mirror layer's on-screen timers and counters (docs/design/mirror-layer.md 3.7, package M4;
// decisions in docs/design/README.md). The clock and the counters the host's mission shows appear on the other players'
// screens with their labels and live values, with no lines in the mission scripts. The rules are in CMirrorHudPolicy.h,
// the wire format in shared/network/mirror_hud_state.h (packet MISSION_MIRROR_HUD).
//
// Host: after every script command (CMissionMirror::OnCommandExecuted) the five var offsets of CUserDisplay::OnscnTimer
// are compared; an entry that became non-zero in a command of the mission family (CMissionMirror::IsFamilyScript) is
// owned and gets a new generation. Once per script frame (CMissionMirror::OnScriptsProcessed) the owned entries are sent
// when they are added, removed or change key, type, colour, flash, direction, beep time or the paused flag, their values
// at most 4 times a second (counters) or once a second (the clock), and in full when a player joins.
// Receivers: a buffer in this DLL holds the values; each mirrored entry's var offset points into it (the engine reads
// ScriptSpace + varId in 32-bit arithmetic), so the game's own globals are never touched. Entries are added with AddClock /
// AddCounter in the host's slot when free, else in another free one, and cleared with ClearClock / ClearCounter. Local
// scripts come first: a slot a local script holds is never written, and a local script that asks for a slot the mirror
// holds gets it (wrappers at the five Add* call sites of the display commands move the mirror's entry away first).
// Everything runs on the game thread.
class CMirrorHud
{
public:
    // CMissionMirror::OnCommandExecuted: after every command a script ran in CRunningScript::Process (every game)
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed: once per script frame, at the end of CTheScripts::Process
    static void Process();
    // MISSION_MIRROR_HUD (PacketHandlers/mirror_hud.cpp)
    static void HandlePacket(const Packets::Mirror::MirrorHud& packet);

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
