#include "stdafx.h"
#include "MirrorAreaHooks.h"
#include "../CMirrorAreas.h"

// Private build: the mission mirror layer's areas (CMirrorAreas.h; docs/design/mirror-layer.md 3.3 and 3.6). Each
// wrapper runs the call it replaces with the same arguments, then tells CMirrorAreas, which keeps what a script of the
// host's mission family drew.
//
// gta_sa.exe 1.0 US, all cdecl (plugin-sdk CTheScripts.cpp and C3dMarkers.cpp "Converted from cdecl"; the callers
// clean the stack: add esp,18h after the area calls, 28h after the angled ones, 2Ch after the marker call, dumpbin):
// - CTheScripts::HighlightImportantArea 0x485E00 (id, x1, y1, x2, y2, z): the yellow locate sphere of a box. Its 14
//   call sites: 4 area checks in CRunningScript::ProcessCommands100To199 (0x466DE0), 1 in ProcessCommands700To799
//   (0x47FA30), and the helpers LocateCharCommand 0x486D80, LocateCharCharCommand 0x4870F0, LocateCharCarCommand
//   0x487420, LocateCharObjectCommand 0x487720 (the three entity-relative ones), LocateCarCommand 0x487A20,
//   LocateObjectCommand 0x487D10, CharInAreaCheckCommand 0x488B50, CarInAreaCheckCommand 0x488EC0 and
//   ObjectInAreaCheckCommand 0x489150 (plugin-sdk meta.CTheScripts.h refs; names gta-reversed RunningScript.cpp).
// - CTheScripts::HighlightImportantAngledArea 0x485EF0 (id, four corners, z), called by CharInAngledAreaCheckCommand
//   0x487F60, ObjectInAngledAreaCheckCommand 0x4883F0 and FlameInAngledAreaCheckCommand 0x488780.
// - C3dMarkers::PlaceMarkerSet 0x725BA0 (id, type, CVector&, size, r, g, b, a, pulse period, pulse fraction, rotate
//   rate), called by draw_sphere (03A1) at 0x4845F8 in ProcessCommands900To999 (0x483BD0); its other callers (the
//   radar, RenderIndicatorShadow, the script spheres' relocated DrawScriptSpheres) are left alone.
// Every script passes id = its own address + its instruction pointer (mov edx,[esi+14h]; add edx,esi before the
// calls). A scan of the executable sections for E8/E9 to the three targets finds exactly these 18 call sites, each an
// E8 to its target in C:\Games\GTASA_Coop\gta_sa.exe (also tools/audit_hooks.py); none of them was hooked before. A
// site that doesn't hold that call at start-up (another plugin hooked it) is skipped and logged.
namespace
{
constexpr uintptr_t HIGHLIGHT_IMPORTANT_AREA = 0x485E00;
constexpr uintptr_t HIGHLIGHT_IMPORTANT_ANGLED_AREA = 0x485EF0;
constexpr uintptr_t PLACE_MARKER_SET = 0x725BA0;

// the call sites (tools/audit_hooks.py reads these tables)
const uintptr_t areaCallAddresses[] = {
    0x467989, 0x467A96, 0x467FA0, 0x46805E, // ProcessCommands100To199
    0x4801D7,                               // ProcessCommands700To799
    0x48704F, 0x4873A4, 0x4876B2, 0x4879B2, // LocateChar, LocateCharChar, LocateCharCar, LocateCharObject
    0x487CA9, 0x487F13,                     // LocateCar, LocateObject
    0x488E6A, 0x489111, 0x489378,           // CharInArea, CarInArea, ObjectInArea
};
const uintptr_t angledAreaCallAddresses[] = {0x488374, 0x488735, 0x488AFA};
const uintptr_t drawSphereCallAddresses[] = {0x4845F8};

void __cdecl CTheScripts__HighlightImportantArea_Hook(uint32_t id, float x1, float y1, float x2, float y2, float z)
{
    plugin::Call<HIGHLIGHT_IMPORTANT_AREA, uint32_t, float, float, float, float, float>(id, x1, y1, x2, y2, z);
    CMirrorAreas::OnHighlightArea(id, x1, y1, x2, y2, z);
}

void __cdecl CTheScripts__HighlightImportantAngledArea_Hook(
    uint32_t id, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float z)
{
    plugin::Call<HIGHLIGHT_IMPORTANT_ANGLED_AREA, uint32_t, float, float, float, float, float, float, float, float,
        float>(id, x1, y1, x2, y2, x3, y3, x4, y4, z);
    CMirrorAreas::OnHighlightAngledArea(id, x1, y1, x2, y2, x3, y3, x4, y4, z);
}

// the small integers are passed on as the 32-bit stack slots the caller pushed
void __cdecl C3dMarkers__PlaceMarkerSet_Hook(uint32_t id, uint32_t type, CVector* pPosition, float size, uint32_t red,
    uint32_t green, uint32_t blue, uint32_t alpha, uint32_t pulsePeriod, float pulseFraction, uint32_t rotateRate)
{
    const CVector position = pPosition != nullptr ? *pPosition : CVector(0.0f, 0.0f, 0.0f);
    plugin::Call<PLACE_MARKER_SET, uint32_t, uint32_t, CVector*, float, uint32_t, uint32_t, uint32_t, uint32_t,
        uint32_t, float, uint32_t>(id, type, pPosition, size, red, green, blue, alpha, pulsePeriod, pulseFraction,
        rotateRate);
    if (pPosition != nullptr)
    {
        CMirrorAreas::OnDrawSphere(id, position, size);
    }
}

// the site holds call rel32 (E8) to target
bool IsCallTo(uintptr_t site, uintptr_t target, uint8_t& firstByte, uintptr_t& actualTarget)
{
    firstByte = patch::GetUChar(static_cast<int>(site), false);
    actualTarget = site + 5 + static_cast<uintptr_t>(patch::GetInt(static_cast<int>(site + 1), false));
    return firstByte == 0xE8 && actualTarget == target;
}

template <size_t N>
void HookCalls(const uintptr_t (&sites)[N], uintptr_t target, void* hook, int& hooked, int& total)
{
    for (uintptr_t site : sites)
    {
        total++;
        uint8_t firstByte = 0;
        uintptr_t actualTarget = 0;
        if (!IsCallTo(site, target, firstByte, actualTarget))
        {
            CMirrorAreas::OnHookSkipped(site, firstByte, actualTarget);
            continue;
        }
        patch::RedirectCall(static_cast<int>(site), hook);
        hooked++;
    }
}
}  // namespace

void MirrorAreaHooks::InjectHooks()
{
    CMirrorAreas::Init();

    int hooked = 0;
    int total = 0;
    HookCalls(areaCallAddresses, HIGHLIGHT_IMPORTANT_AREA, CTheScripts__HighlightImportantArea_Hook, hooked, total);
    HookCalls(angledAreaCallAddresses, HIGHLIGHT_IMPORTANT_ANGLED_AREA, CTheScripts__HighlightImportantAngledArea_Hook,
        hooked, total);
    HookCalls(drawSphereCallAddresses, PLACE_MARKER_SET, C3dMarkers__PlaceMarkerSet_Hook, hooked, total);
    CMirrorAreas::OnHooksInstalled(hooked, total);
}
