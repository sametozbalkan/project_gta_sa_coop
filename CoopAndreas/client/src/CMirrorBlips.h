#pragma once

#include <cstdint>
#include <cstdio>

class CRunningScript;
namespace Packets::Mirror
{
class MirrorBlips;
}

// Private build: the mission mirror layer's blips (docs/design/mirror-layer.md 3.5, 3.10 and 3.11, package M2;
// decision M-D1 in docs/design/README.md). The blips the host's mission creates reach the other players with every
// change, with no lines in the mission scripts. The rules are in CMirrorBlipsPolicy.h.
//
// Host: a creating blip command (add_blip_for_char, add_blip_for_coord, ...) run by the host's mission family
// (CMissionMirror) makes the handle it left in ScriptParams[0] an owned blip. Once per script frame the module reads
// the owned radar traces and sends what changed (MISSION_MIRROR_BLIPS): the whole state of a new or changed blip, its
// removal, and the position of a blip the others show at the host's position. Owned blips stay mirrored after the
// mission ends for as long as the host keeps them. A joiner gets them all (bReset, to that player alone); the host's
// restart or load resets them on every other game. The host's static blip snapshot (CNetworkStaticBlip) leaves owned
// traces out.
// Receivers: an entity blip on their copy of a network entity, found by its network id; a radar-only coordinate blip
// at the host's position while it isn't found (and for objects, pickups, searchlights and trains), switched to the
// entity blip once it is; the same coordinate or contact point blip as the host's, with its interior entrance; no blip
// on their own player. Everything mirrored goes when the host changes, leaves or the session ends; their static blip
// snapshot (CNetworkStaticBlip::Create) leaves the traces this module made.
// Everything runs on the game thread except WriteDiagSummary.
class CMirrorBlips
{
public:
    // CMissionMirror::OnCommandExecuted, after every command a script ran
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed, once per script frame
    static void Process();
    // MISSION_MIRROR_BLIPS from the server
    static void HandlePacket(const Packets::Mirror::MirrorBlips& packet);

    // the static path (M-D1): the host's CNetworkStaticBlip::Send skips the traces of owned blips, a receiver's
    // CNetworkStaticBlip::Create leaves the traces this module made
    static bool IsOwnedTrace(int traceIndex);
    static bool IsMirrorTrace(int traceIndex);

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
