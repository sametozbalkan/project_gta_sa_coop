#pragma once

#include <cstdint>
#include <cstdio>

class CRunningScript;

// Private build: the mission mirror layer's core (docs/design/mirror-layer.md, package M1; decisions in
// docs/design/README.md). What the host's mission shows on screen reaches the other players without lines in the mission
// scripts: texts, help boxes, mission titles and "Mission passed" with its tune go through COpCodeSync's replay, and the
// money and respect a passed mission gives reach every player as their own (M-D3). The rules are in CMirrorPolicy.h.
//
// Host: COpCodeSync's command hook (0x469FF3) tells this module about every script command; it decides which of the
// host's commands the other games replay: the texts of the host's mission family, the mission titles (print_big style 2
// from any script, M-D4) and the rewards of a mission that passes in the same script frame. On PLAYER_JOINED it sends the
// joiner the mission view (the host's $onmission, M-D10; the mission text table; the objective and help still on screen).
// Receivers: the host's text table for the keys a replayed print needs, and at the end of the host's mission the help box
// and a mirrored HUD flash are cleared, as CMissionCleanup::Process does on the host.
// Everything runs on the game thread.
class CMissionMirror
{
public:
    // CCore::Init: subscribes to the session lifecycle
    static void Init();

    // --- from COpCodeSync ---
    // after every command a script ran in CRunningScript::Process (BuildAndSendOpcode, before anything else)
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // IsOpcodeSyncable: the mirror wants this command's parameters collected and serialized (inside a script command on
    // the host: a text of the family, a print_big of any script, a reward of the family)
    static bool WantsCommand(CRunningScript* pScript, uint16_t opcode);
    // IsOpcodeSyncable: money and respect leave the general replay of registered scripts
    static bool IsRewardCommand(uint16_t opcode);
    // BuildAndSendOpcode, the serialized command (bMirrorOnly: only the mirror wanted it): true = send it now; false for a
    // print_big that isn't a title and for rewards, which wait for the end of the script frame
    static bool OnCommandSerialized(CRunningScript* pScript, uint16_t opcode, bool bMirrorOnly, const uint8_t* data,
        int size);
    // the end of CTheScripts::Process (the 0x46A22E redirect): the frame's rewards, the end of the host's mission
    static void OnScriptsProcessed();
    // COpCodeSync::HandlePacket before it replays a command of the host: false = don't run it (load_mission_text only
    // names the host's table here); makes the keys of a replayed print resolve
    static bool OnReplayingCommand(const uint8_t* data, int size);

    // the host's mission family (design 3.2) for a script running a command; false on the other games. For the hooks
    // that run inside command handlers: IsFamilyScript(COpCodeSync::GetScriptRunningCommand()).
    static bool IsFamilyScript(CRunningScript* pScript);

    // receivers (ADD_MESSAGE_GXT and the replayed prints): load the host's mission table when key needs it
    static void PrepareTextKey(const char* key);

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
