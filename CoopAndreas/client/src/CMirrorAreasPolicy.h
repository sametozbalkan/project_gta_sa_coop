#pragma once

#include "network/mirror_areas.h"
#include <cmath>
#include <cstdint>

// Private build: the rules of the mission mirror layer's areas (CMirrorAreas; docs/design/mirror-layer.md 3.6, package
// M3), without game or network types, so tests/mirror_areas_test.cpp checks them without GTA.
namespace MirrorAreasPolicy
{
using MirrorAreasWire::MAX_AREAS;
using MirrorAreasWire::SArea;

// An area the family drew this recently is active. The scripts draw a locate sphere in every frame in which they test
// the locate (a mission's loop waits 0 between tests); the grace time bridges the frames in which they don't.
constexpr uint32_t GRACE_MS = 250;
// A set goes out again when an area came or went, a centre moved more than MOVE_THRESHOLD (an entity-relative locate
// moves with its target) or a size changed more than RESIZE_THRESHOLD, both measured against the set sent last.
constexpr float MOVE_THRESHOLD = 0.5f;
constexpr float RESIZE_THRESHOLD = 0.1f;
// at most 10 sets a second; the end of the mission and a join snapshot go out at once
constexpr uint32_t MIN_SEND_INTERVAL_MS = 100;

// The receivers' marker ids: the host's id with the top two bits set, away from the ids the receiver's game uses
// itself: the script + instruction pointer sums of its own scripts (a script in CTheScripts::ScriptsArray 0xA8B430,
// 0xE0 each, plus an IP in ScriptSpace 0xA49960 or the mission block 0xA7A6A0 stays below 0x40000000; plugin-sdk
// CTheScripts.cpp), the radar's cone handles (trace index | counter << 16, CRadar::Draw3dMarkers 0x585BF0,
// gta-reversed), entry-exit addresses, and the bits of a positive float (CNetworkCheckpoint::Process).
constexpr uint32_t LOCAL_ID_BITS = 0xC0000000u;

inline uint32_t ToLocalId(uint32_t hostId)
{
    return (hostId & ~LOCAL_ID_BITS) | LOCAL_ID_BITS;
}

// CTheScripts::HighlightImportantArea (0x485E00): the box of the two corners a script passes, in any order
inline SArea MakeArea(uint32_t id, float x1, float y1, float x2, float y2, float z, uint8_t areaCode)
{
    const float minX = x1 < x2 ? x1 : x2;
    const float maxX = x1 < x2 ? x2 : x1;
    const float minY = y1 < y2 ? y1 : y2;
    const float maxY = y1 < y2 ? y2 : y1;
    SArea area;
    area.id = id;
    area.kind = MirrorAreasWire::KIND_AREA;
    area.areaCode = areaCode;
    area.x = (minX + maxX) * 0.5f;
    area.y = (minY + maxY) * 0.5f;
    area.z = z;
    area.sizeX = maxX - area.x;
    area.sizeY = maxY - area.y;
    return area;
}

// CTheScripts::HighlightImportantAngledArea (0x485EF0) renders the axis-aligned box of the four edge midpoints of the
// corners a script passes (gta-reversed TheScripts.cpp); that box is the area, drawn like HighlightImportantArea's
inline SArea MakeAngledArea(uint32_t id, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
    float z, uint8_t areaCode)
{
    const float midX[4] = {(x1 + x2) * 0.5f, (x2 + x3) * 0.5f, (x3 + x4) * 0.5f, (x4 + x1) * 0.5f};
    const float midY[4] = {(y1 + y2) * 0.5f, (y2 + y3) * 0.5f, (y3 + y4) * 0.5f, (y4 + y1) * 0.5f};
    float minX = midX[0];
    float maxX = midX[0];
    float minY = midY[0];
    float maxY = midY[0];
    for (int i = 1; i < 4; i++)
    {
        minX = midX[i] < minX ? midX[i] : minX;
        maxX = midX[i] > maxX ? midX[i] : maxX;
        minY = midY[i] < minY ? midY[i] : minY;
        maxY = midY[i] > maxY ? midY[i] : maxY;
    }
    return MakeArea(id, minX, minY, maxX, maxY, z, areaCode);
}

// draw_sphere (03A1): the cylinder marker at the position its handler resolved, with the radius it passes
inline SArea MakeMarker(uint32_t id, float x, float y, float z, float radius, uint8_t areaCode)
{
    SArea area;
    area.id = id;
    area.kind = MirrorAreasWire::KIND_MARKER;
    area.areaCode = areaCode;
    area.x = x;
    area.y = y;
    area.z = z;
    area.sizeX = radius;
    area.sizeY = 0.0f;
    return area;
}

// the same area differs enough from the one sent last to be sent again
inline bool HasChanged(const SArea& sent, const SArea& now)
{
    if (sent.id != now.id || sent.kind != now.kind || sent.areaCode != now.areaCode)
    {
        return true;
    }
    const bool bGroundNow = MirrorAreasWire::IsGroundZ(now.z);
    if (MirrorAreasWire::IsGroundZ(sent.z) != bGroundNow)
    {
        return true;
    }
    const float dx = now.x - sent.x;
    const float dy = now.y - sent.y;
    const float dz = bGroundNow ? 0.0f : now.z - sent.z;
    if (dx * dx + dy * dy + dz * dz > MOVE_THRESHOLD * MOVE_THRESHOLD)
    {
        return true;
    }
    return std::fabs(now.sizeX - sent.sizeX) > RESIZE_THRESHOLD || std::fabs(now.sizeY - sent.sizeY) > RESIZE_THRESHOLD;
}

// a receiver draws only the areas of the area (interior) its game shows: a cylinder marker takes its height from the
// collision below it, and a game outside has no interior collision loaded, so an interior's sphere would land on the
// street under that interior (CMirrorAreas.cpp)
inline bool ShouldDraw(const SArea& area, int localAreaCode)
{
    return area.kind <= MirrorAreasWire::KIND_LAST_SUPPORTED && static_cast<int>(area.areaCode) == localAreaCode;
}

// The host's side: the areas the mission family drew lately, and the set the other games were sent last. Times are in
// ms on one clock (the game's, CTimer::m_snTimeInMilliseconds); a clock that went back (a restart) expires everything.
class CHostAreas
{
public:
    // the family drew area at now: false when the table is full and the area is new (it isn't mirrored then)
    bool Record(const SArea& area, uint32_t now)
    {
        for (int i = 0; i < m_nRecorded; i++)
        {
            if (m_recorded[i].area.id == area.id)
            {
                m_recorded[i].area = area;
                m_recorded[i].lastSeen = now;
                return true;
            }
        }
        if (m_nRecorded >= MAX_AREAS)
        {
            return false;
        }
        m_recorded[m_nRecorded].area = area;
        m_recorded[m_nRecorded].lastSeen = now;
        m_nRecorded++;
        return true;
    }

    // drops the areas not drawn within GRACE_MS of now; what stays is the active set
    void Expire(uint32_t now)
    {
        int kept = 0;
        for (int i = 0; i < m_nRecorded; i++)
        {
            if (now - m_recorded[i].lastSeen <= GRACE_MS)
            {
                m_recorded[kept++] = m_recorded[i];
            }
        }
        m_nRecorded = kept;
    }

    // the active set differs from the set sent last: an area came or went, moved or was resized
    bool HasChanges() const
    {
        if (m_nRecorded != m_nSent)
        {
            return true;
        }
        for (int i = 0; i < m_nRecorded; i++)
        {
            const SArea* pSent = FindSent(m_recorded[i].area.id);
            if (pSent == nullptr || HasChanged(*pSent, m_recorded[i].area))
            {
                return true;
            }
        }
        return false;
    }

    // after Expire: the active set should go out now (it changed, and the last set went out at least
    // MIN_SEND_INTERVAL_MS before now)
    bool IsSendDue(uint32_t now) const
    {
        return HasChanges() && (!m_bSentOnce || now - m_nLastSendTime >= MIN_SEND_INTERVAL_MS);
    }

    // the active set, for the packet; returns its size
    int GetActive(SArea* out, int max) const
    {
        int count = 0;
        for (int i = 0; i < m_nRecorded && count < max; i++)
        {
            out[count++] = m_recorded[i].area;
        }
        return count;
    }

    // the active set went out at now
    void OnSent(uint32_t now)
    {
        m_nSent = 0;
        for (int i = 0; i < m_nRecorded; i++)
        {
            m_sent[m_nSent++] = m_recorded[i].area;
        }
        m_nLastSendTime = now;
        m_bSentOnce = true;
    }

    // the mission ended or the game started over: nothing is active any more. True when the other games still show
    // areas from this game: send the (empty) active set at once.
    bool ClearRecorded()
    {
        m_nRecorded = 0;
        return m_nSent > 0;
    }

    // the other games dropped what they had (the host's $onmission went to 0 there) or a player joined: what is still
    // active goes out again
    void ForgetSent() { m_nSent = 0; }

    void Reset() { *this = CHostAreas{}; }

    int GetRecordedCount() const { return m_nRecorded; }
    int GetSentCount() const { return m_nSent; }

private:
    struct SRecorded
    {
        SArea area{};
        uint32_t lastSeen = 0;
    };

    const SArea* FindSent(uint32_t id) const
    {
        for (int i = 0; i < m_nSent; i++)
        {
            if (m_sent[i].id == id)
            {
                return &m_sent[i];
            }
        }
        return nullptr;
    }

    SRecorded m_recorded[MAX_AREAS]{};
    int m_nRecorded = 0;
    SArea m_sent[MAX_AREAS]{};
    int m_nSent = 0;
    uint32_t m_nLastSendTime = 0;
    bool m_bSentOnce = false;
};

// The other games' side: the host's current set, replaced by every set it sends.
class CReceivedAreas
{
public:
    void Set(const SArea* areas, int count)
    {
        m_nCount = 0;
        for (int i = 0; areas != nullptr && i < count && i < MAX_AREAS; i++)
        {
            m_areas[m_nCount++] = areas[i];
        }
    }
    void Clear() { m_nCount = 0; }

    int GetCount() const { return m_nCount; }
    const SArea& Get(int index) const { return m_areas[index]; }

private:
    SArea m_areas[MAX_AREAS]{};
    int m_nCount = 0;
};
}  // namespace MirrorAreasPolicy
