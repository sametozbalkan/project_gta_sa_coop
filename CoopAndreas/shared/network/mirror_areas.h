#pragma once

#include <cmath>
#include <cstdint>

// Private build: the areas of the mission mirror layer (client/src/CMirrorAreas.h; docs/design/mirror-layer.md 3.6 and
// 3.8, package M3) as plain data without game types, shared by the MISSION_MIRROR_AREAS packet
// (network/packets/mirror_areas.h), the client's rules (client/src/CMirrorAreasPolicy.h) and their standalone test
// (tests/mirror_areas_test.cpp).
namespace MirrorAreasWire
{
// the most areas one set carries. The host's game draws far fewer at a time: C3dMarkers has 32 markers (plugin-sdk
// C3dMarkers.h), and a locate sphere takes three of them (CShadows::RenderIndicatorShadow 0x70CCB0 places a set of
// three, gta-reversed Shadows.cpp; its calls of PlaceMarkerSet at 0x70CD1F, 0x70CD6C and 0x70CDBC)
constexpr int MAX_AREAS = 32;

// what an area is, and so how the other games draw it
constexpr uint8_t KIND_AREA = 0;    // a locate sphere: CTheScripts::HighlightImportantArea (box centre, half sizes, z)
constexpr uint8_t KIND_MARKER = 1;  // draw_sphere (03A1): a C3dMarkers cylinder (centre, radius in sizeX)
// reserved for M5, race checkpoints and script spheres (docs/design/mirror-layer.md 3.6, "Later package"): the kind
// field has room for them already, their own fields come with them
constexpr uint8_t KIND_RACE_CHECKPOINT = 2;
constexpr uint8_t KIND_SCRIPT_SPHERE = 3;
constexpr uint8_t KIND_FIELD_MAX = 15;                // the kind's range on the wire (4 bits)
constexpr uint8_t KIND_LAST_SUPPORTED = KIND_MARKER;  // the kinds this build sends and draws

// CTheScripts::HighlightImportantArea and HighlightImportantAngledArea take the ground under the centre for a height
// at or below -100.0 (gta_sa.exe 1.0 US: fcomp with the float at 0x859014 = -100.0f at 0x485E7D and 0x4860A5,
// dumpbin; tools/pe_peek.py for the constant). The wire carries such a height as a flag, and the receiver passes
// GROUND_Z on so that its own game finds its own ground.
constexpr float GROUND_Z = -100.0f;

// what a receiver accepts before it passes the values to the game (the map spans +-3000, interiors sit above it)
constexpr float MAX_COORDINATE = 20000.0f;
constexpr float MAX_HALF_SIZE = 5000.0f;

struct SArea
{
    uint32_t id = 0;       // the host's marker id: the script's address + its instruction pointer, as the game has it
    uint8_t kind = KIND_AREA;
    uint8_t areaCode = 0;  // the host's CGame::currArea when it was drawn: 0 the outside world, else an interior
    float x = 0.0f;        // the centre
    float y = 0.0f;
    float z = 0.0f;        // KIND_AREA: at or below GROUND_Z means the ground under the centre
    float sizeX = 0.0f;    // KIND_AREA: the half sizes; KIND_MARKER: the radius (sizeY 0)
    float sizeY = 0.0f;
};

inline bool IsGroundZ(float z)
{
    return z <= GROUND_Z;
}

// finite, within the bounds above, and a kind this build draws
inline bool IsValidArea(const SArea& area)
{
    if (area.kind > KIND_LAST_SUPPORTED)
    {
        return false;
    }
    if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.z) || !std::isfinite(area.sizeX) ||
        !std::isfinite(area.sizeY))
    {
        return false;
    }
    if (std::fabs(area.x) > MAX_COORDINATE || std::fabs(area.y) > MAX_COORDINATE || area.z > MAX_COORDINATE)
    {
        return false;
    }
    return area.sizeX >= 0.0f && area.sizeX <= MAX_HALF_SIZE && area.sizeY >= 0.0f && area.sizeY <= MAX_HALF_SIZE;
}
}  // namespace MirrorAreasWire
