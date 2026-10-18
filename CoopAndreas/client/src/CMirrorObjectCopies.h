#pragma once

#include <cstdint>
#include <cstdio>

class CObject;
namespace MirrorObjectsPolicy
{
struct SObjectRecord;
}

// Private build: the other games' copies of the host's mission objects (docs/design/mission-objects.md 5.5-5.7 and
// 5.10, the receiver half of package OB-1; rules in CMirrorObjectCopiesPolicy.h). CMirrorObjects keeps the host's
// records (CMirrorObjects::GetRecords: the last FULL with owner and epoch, the owner's last two motion samples stamped
// with its server clock) and calls these hooks on the game thread; this class makes and drives the copies.
//
// A copy is an ordinary object of this game, type 6 (OBJECT_MISSION2: the population never deletes it, the save never
// holds it, a restart deletes it), with the host object's model, flags, area and scale. While the host moves the object
// its motion samples drive the copy (interpolated when a script places it, the copy's own physics corrected by each
// sample when it is dynamic); at rest it sits at the host's last FULL and is put back when it stays off. An attached
// copy rides on this game's copy of the parent, a held one in the hands of this game's copy of the holder (a hold task,
// or the right hand's position as a fallback). The copies are never handed to this game's scripts.
class CMirrorObjectCopies
{
public:
    // a FULL of the host arrived (bNew: the record is new; else its state changed, its samples may have been reset by a
    // new owner or epoch)
    static void OnRecordFull(const MirrorObjectsPolicy::SObjectRecord& record, bool bNew);
    // the host removed the object (the record as it was; it is gone from the table)
    static void OnRecordRemoved(const MirrorObjectsPolicy::SObjectRecord& record);
    // an OWNER entry gave the object another owner or epoch (OB-3); its samples were dropped
    static void OnOwnerChanged(const MirrorObjectsPolicy::SObjectRecord& record);
    // a motion sample of the owner was stored (record.samples[0] is the newest)
    static void OnMotion(const MirrorObjectsPolicy::SObjectRecord& record);
    // every record was dropped: why = "reset" (the host's bReset), "host-changed", "host-left", "disconnected",
    // "this-game-is-host"
    static void OnRecordsCleared(const char* why);
    // this game started over (a new game or a load): CWorld::ClearForRestart deleted every copy; the records stay
    static void OnLocalRestart();
    // once per frame after CGame::Process (Main.cpp), on the games that aren't the host
    static void Process();
    // CMirrorObjects::Process at the end of the script frame, before CWorld::Process: no copy is left flagged for
    // deletion by a holder ped deleted since the last frame (CMirrorObjectCopies.cpp)
    static void ClearRemoveFlags();

    // this game's copy of the host's mission object (MirrorObjectsWire::ID_MISSION), or nullptr
    static CObject* FindCopy(int32_t hostHandle);

    // --coopdiag: the copies' part of the objects: line (ends it), from the diag thread (plain cached values)
    static void WriteDiagSummary(FILE* file);
};
