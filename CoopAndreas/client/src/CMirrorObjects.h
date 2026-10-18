#pragma once

#include <cstdint>
#include <cstdio>

class CObject;
class CRunningScript;
namespace Packets::Mirror
{
class MirrorObjects;
class MirrorObjectMotion;
}
namespace MirrorObjectsPolicy
{
class CRecordTable;
struct SObjectRecord;
}

// Private build: the mission object mirror (docs/design/mission-objects.md, package OB-1; decisions O-D5 and O-D10).
// The objects the host's mission creates reach the other players with their state and motion, with no lines in the
// mission scripts. The rules are in CMirrorObjectsPolicy.h, the wire format in shared/network/mirror_objects.h
// (packets MISSION_OBJECTS and MISSION_OBJECT_MOTION).
//
// Host: create_object 0107 or create_object_no_offset 029B run by the host's mission family (CMissionMirror) makes the
// handle it left in ScriptParams[0] an owned object, and its FULL goes out at once. At the end of each script frame
// (before the blips, so a blip finds its object) every owned object is read again and a FULL goes out when a field
// other than its transform changed (model, area, flags, scale, parent, holder, owner); a handle that no longer resolves
// (the slot was freed, or reused with another id byte) goes out as REMOVED. After CGame::Process the transforms of
// moving objects go out as motion samples (10 Hz, 20 Hz above 5 m/s), and a reliable FULL with the resting transform
// once one has been still for 300 ms (measured over 100 ms windows) or came to rest; an object held by or attached to an
// entity the others know sends samples at 5 Hz (20 Hz above 5 m/s) while it moves and no resting FULL, for a receiver
// that can't resolve that entity. Owned objects stay mirrored after the mission for as long as the host's pool keeps them (O-D5 (a)). A
// joiner gets the whole set with bReset (that player alone); the host's restart or load sends its empty set with bReset
// to everyone. Nothing goes out while no other player is in or before the first server-time answer; tracking goes on.
// `mission-objects = false` in coopandreas.ini (CConfigLoader) turns the mirror off on that game: as the host it owns
// and sends nothing, as a receiver it ignores both packets and keeps no copies.
// Receivers: the current host's records (CMirrorObjects::GetRecords), kept from its MISSION_OBJECTS and the owner's
// MISSION_OBJECT_MOTION; dropped when the host changes or leaves, on a disconnect and on the host's bReset.
// CMirrorObjectCopies makes this game's copies from them.
// Everything runs on the game thread except WriteDiagSummary.
class CMirrorObjects
{
public:
    // CMissionMirror::OnCommandExecuted, after every command a script ran
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed, once per script frame (before CMirrorBlips::Process and CWorld::Process)
    static void Process();
    // the kill switch (coopandreas.ini `mission-objects`, read at start-up)
    static bool IsEnabled();
    // Main.cpp, once per frame after CGame::Process: the host's motion samples, the receivers' copies
    static void ProcessMotion();
    // MISSION_OBJECTS and MISSION_OBJECT_MOTION from the server (PacketHandlers/mirror_objects.cpp)
    static void HandlePacket(const Packets::Mirror::MirrorObjects& packet);
    static void HandleMotion(const Packets::Mirror::MirrorObjectMotion& packet);

    // the host: an object of its mission the mirror sends (its object pool handle)
    static bool IsOwnedObject(int32_t hostHandle);
    // the other games: this game's copy of the host's object, or nullptr (CMirrorObjectCopies)
    static CObject* ResolveObjectCopy(int32_t hostHandle);

    // the other games: the current host's records (empty on the host)
    static const MirrorObjectsPolicy::CRecordTable& GetRecords();
    static const MirrorObjectsPolicy::SObjectRecord* FindRecord(uint8_t idKind, int32_t id);

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
    // --coopdiag: a record in CoopAndreas_objects.log (CMirrorObjectCopies' events), game thread
    static void Log(const char* format, ...);
};
