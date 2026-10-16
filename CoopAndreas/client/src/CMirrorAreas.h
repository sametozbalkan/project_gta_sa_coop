#pragma once

#include <cstdint>
#include <cstdio>

class CVector;
namespace Packets::Mirror
{
class MirrorAreas;
}

// Private build: the mission mirror layer's areas (docs/design/mirror-layer.md 3.6, package M3; decisions in
// docs/design/README.md): the locate spheres (the yellow mission markers) and the draw_sphere markers of the host's
// mission family appear on the other players' screens. The rules are in CMirrorAreasPolicy.h, the wire format in
// shared/network/mirror_areas.h.
//
// Host: wrappers at the 14 CTheScripts::HighlightImportantArea calls, the 3 HighlightImportantAngledArea calls and the
// draw_sphere marker call (Hooks/MirrorAreaHooks.cpp) record what a script of the family
// (CMissionMirror::IsFamilyScript) draws. Once per script frame an area seen within the last 250 ms is active, and the
// active set goes out (MISSION_MIRROR_AREAS) when it changes, at most 10 times a second; an empty set at once when the
// mission ends or the game starts over; the current set when a player joins.
// Others: the current host's set replaces the last one and is drawn every frame in the plugin's drawing event, where
// CNetworkCheckpoint::Process draws (Main.cpp), with the game's own functions and the host's ids with the top bits set.
// A host change or a disconnect drops it, and so does the host's $onmission going to 0.
// Everything runs on the game thread.
class CMirrorAreas
{
public:
    // Hooks/MirrorAreaHooks.cpp, before the hooks go in: the session lifecycle and the drawing event
    static void Init();
    // Hooks/MirrorAreaHooks.cpp: how many of the call sites were hooked (a site that doesn't hold the expected call is
    // skipped and logged)
    static void OnHooksInstalled(int hooked, int total);
    static void OnHookSkipped(uintptr_t site, uint8_t firstByte, uintptr_t target);

    // --- the call-site wrappers, after the original ran (inside a script command) ---
    static void OnHighlightArea(uint32_t id, float x1, float y1, float x2, float y2, float z);
    static void OnHighlightAngledArea(
        uint32_t id, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float z);
    static void OnDrawSphere(uint32_t id, const CVector& position, float radius);

    // once per script frame, from CMissionMirror::OnScriptsProcessed after it updated the mission: bMirroredMission = a
    // mission whose display is mirrored runs (its end sends an empty set at once)
    static void Process(bool bMirroredMission);

    // others: MISSION_MIRROR_AREAS from the host
    static void HandlePacket(const Packets::Mirror::MirrorAreas& packet);

    // --coopdiag: one summary line for CoopAndreas_diag.log, from the diag thread (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
