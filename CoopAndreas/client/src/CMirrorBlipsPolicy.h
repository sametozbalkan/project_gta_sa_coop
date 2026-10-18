#pragma once

#include <cstdint>

// Private build: the rules of the mission mirror layer's blips (CMirrorBlips; docs/design/mirror-layer.md sections 3.5,
// 3.10 and 3.11, package M2; decision M-D1 in docs/design/README.md), without game or network types, so
// tests/mirror_blips_test.cpp checks them without GTA.
namespace MirrorBlipsPolicy
{
// CRadar::ms_RadarTrace has 175 entries (MAX_RADAR_TRACES, plugin-sdk CRadar.h): no more blips can be owned or mirrored
constexpr int MAX_BLIPS = 175;

// eBlipType (plugin-sdk CRadar.h), bits 2-5 of the radar trace byte at +0x26
namespace Type
{
constexpr uint8_t NONE = 0;
constexpr uint8_t CAR = 1;
constexpr uint8_t CHAR = 2;
constexpr uint8_t OBJECT = 3;
constexpr uint8_t COORD = 4;
constexpr uint8_t CONTACT_POINT = 5;
constexpr uint8_t SPOTLIGHT = 6;
constexpr uint8_t PICKUP = 7;
constexpr uint8_t AIRSTRIP = 8;
}  // namespace Type

// eBlipDisplay, bits 0-1 of the radar trace byte at +0x26: bit 0 the marker in the world, bit 1 the blip on the radar
namespace Display
{
constexpr uint8_t NEITHER = 0;
constexpr uint8_t MARKER_ONLY = 1;
constexpr uint8_t BLIP_ONLY = 2;
constexpr uint8_t BOTH = 3;
}  // namespace Display

// What an entity blip of the host points at on the wire (design 3.10). The host gives a network id only for an entity
// in the managers' lists (a confirmed ped or vehicle, a connected player), never the 0 an unconfirmed entity reads.
enum class eEntityKind : uint8_t
{
    NONE,     // a coordinate, or an entity without a network id: object, pickup, searchlight, train, not confirmed yet
    PLAYER,   // a player's ped, by player id
    PED,      // CNetworkPed id
    VEHICLE,  // CNetworkVehicle id
    OBJECT,   // CMirrorObjects id (host object handle)
};

// The commands that create a blip and leave its handle in ScriptParams[0] when their handler returns (design 3.5 and
// risk 6: every case ends with `mov [0A43C78h],reg` and StoreParameters, see CMirrorBlips.cpp). Sanny Builder 4 names.
inline bool IsCreatingCommand(uint16_t opcode)
{
    switch (opcode)
    {
        case 0x0161:  // add_blip_for_car_old
        case 0x0167:  // add_blip_for_coord_old
        case 0x0186:  // add_blip_for_car
        case 0x0187:  // add_blip_for_char
        case 0x0188:  // add_blip_for_object
        case 0x018A:  // add_blip_for_coord
        case 0x02A7:  // add_sprite_blip_for_contact_point
        case 0x02A8:  // add_sprite_blip_for_coord
        case 0x03DC:  // add_blip_for_pickup
        case 0x04CE:  // add_short_range_sprite_blip_for_coord
        case 0x0570:  // add_short_range_sprite_blip_for_contact_point
        case 0x06C4:  // add_blip_for_searchlight
        case 0x0888:  // add_blip_for_dead_char
            return true;
        default:
            return false;
    }
}

// A radar blip handle: the trace index in the low 16 bits and the trace's counter (1 or more once used, SetCoordBlip
// 0x583820) in the high 16 bits; -1 when no trace was free.
inline bool IsBlipHandle(uint32_t handle)
{
    return handle != 0xFFFFFFFFu && (handle & 0xFFFFu) < static_cast<uint32_t>(MAX_BLIPS) && (handle >> 16) != 0;
}

inline bool IsValidType(uint8_t type)
{
    return type >= Type::CAR && type <= Type::AIRSTRIP;
}

// a blip at a fixed point (SetCoordBlip): the receivers make the same coordinate blip
inline bool IsCoordinateType(uint8_t type)
{
    return type == Type::COORD || type == Type::CONTACT_POINT || type == Type::AIRSTRIP;
}

// The state of one blip as the receivers should show it.
struct SBlipState
{
    uint8_t type = Type::NONE;
    eEntityKind entityKind = eEntityKind::NONE;
    int32_t entityId = 0;  // only with an entity kind
    // the entity's position (entity types, where the radar draws it), or the trace's (coordinate types; an interior's
    // coordinates when the blip has an entrance)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t colour = 0;  // raw, an eBlipColour or an RGBA value
    uint8_t sprite = 0;   // 0..63
    uint8_t display = Display::NEITHER;
    uint8_t size = 1;
    bool bBright = true;
    bool bShortRange = false;
    bool bFriendly = false;
    bool bRemain = false;
    bool bFade = false;
    uint8_t coordAppearance = 0;  // 0..2
    // coordinate types: the blip shows at an interior's entrance (the centre of the entrance rectangle)
    bool bEntrance = false;
    float entranceX = 0.0f;
    float entranceY = 0.0f;
    float entranceZ = 0.0f;
};

inline float DistanceSq(const SBlipState& a, const SBlipState& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// everything but the position
inline bool SameLook(const SBlipState& a, const SBlipState& b)
{
    if (a.type != b.type || a.entityKind != b.entityKind ||
        (a.entityKind != eEntityKind::NONE && a.entityId != b.entityId))
    {
        return false;
    }
    if (a.colour != b.colour || a.sprite != b.sprite || a.display != b.display || a.size != b.size ||
        a.bBright != b.bBright || a.bShortRange != b.bShortRange || a.bFriendly != b.bFriendly ||
        a.bRemain != b.bRemain || a.bFade != b.bFade || a.coordAppearance != b.coordAppearance)
    {
        return false;
    }
    if (a.bEntrance != b.bEntrance)
    {
        return false;
    }
    return !a.bEntrance || (a.entranceX == b.entranceX && a.entranceY == b.entranceY && a.entranceZ == b.entranceZ);
}

// a coordinate blip doesn't move; a position that differs by more than this goes out in full
constexpr float COORD_MOVE_EPSILON = 0.01f;
// A blip the receivers show at the host's position (eEntityKind::NONE: objects, pickups, searchlights, trains, an
// entity without a network id yet) follows it at up to 4 Hz once it moved this far (design 3.8).
constexpr uint32_t FOLLOW_INTERVAL_MS = 250;
constexpr float FOLLOW_MIN_MOVE = 0.5f;
// An entity blip the receivers attach to their copy of the entity: its position only matters while a receiver can't
// find that copy, 0.5 Hz (design 3.8).
constexpr uint32_t ENTITY_POSITION_INTERVAL_MS = 2000;
constexpr float ENTITY_MIN_MOVE = 2.0f;

enum class eSend : uint8_t
{
    NONE,
    FULL,
    POSITION,
};

// The host's per-frame difference for an owned blip (design 3.5): bSent = the receivers have it, sent = what they have,
// msSincePosition = the time since its position last went out (in full or alone).
inline eSend DecideSend(bool bSent, const SBlipState& sent, const SBlipState& now, uint32_t msSincePosition)
{
    if (!bSent || !SameLook(sent, now))
    {
        return eSend::FULL;
    }
    const float movedSq = DistanceSq(sent, now);
    if (IsCoordinateType(now.type))
    {
        return movedSq > COORD_MOVE_EPSILON * COORD_MOVE_EPSILON ? eSend::FULL : eSend::NONE;
    }
    const bool bFollow = now.entityKind == eEntityKind::NONE;
    const uint32_t interval = bFollow ? FOLLOW_INTERVAL_MS : ENTITY_POSITION_INTERVAL_MS;
    const float minMove = bFollow ? FOLLOW_MIN_MOVE : ENTITY_MIN_MOVE;
    if (movedSq >= minMove * minMove && msSincePosition >= interval)
    {
        return eSend::POSITION;
    }
    return eSend::NONE;
}

// An owned blip on the host: one a creating command of the host's mission family made (design 3.5).
struct SOwnedBlip
{
    uint32_t handle = 0;
    bool bSent = false;           // the receivers have it (a full state went out)
    SBlipState sent;              // what they have
    uint32_t positionSentAt = 0;  // GetTickCount when its position last went out
};

class COwnedBlips
{
public:
    // false when it is no blip handle, owned already or the set is full
    bool Add(uint32_t handle)
    {
        if (!IsBlipHandle(handle) || Contains(handle) || m_nCount >= MAX_BLIPS)
        {
            return false;
        }
        m_blips[m_nCount] = SOwnedBlip{};
        m_blips[m_nCount].handle = handle;
        m_nCount++;
        return true;
    }

    bool Contains(uint32_t handle) const
    {
        for (int i = 0; i < m_nCount; i++)
        {
            if (m_blips[i].handle == handle)
            {
                return true;
            }
        }
        return false;
    }

    int Count() const { return m_nCount; }
    SOwnedBlip& At(int i) { return m_blips[i]; }
    const SOwnedBlip& At(int i) const { return m_blips[i]; }

    // the order doesn't matter: the last one takes its place
    void RemoveAt(int i)
    {
        if (i < 0 || i >= m_nCount)
        {
            return;
        }
        m_blips[i] = m_blips[m_nCount - 1];
        m_nCount--;
    }

    void Clear() { m_nCount = 0; }

private:
    SOwnedBlip m_blips[MAX_BLIPS]{};
    int m_nCount = 0;
};

// The receiver's blip for one of the host's (design 3.5 and 3.10).
enum class eLocalForm : uint8_t
{
    NONE,    // no blip: it points at this game's own player
    ENTITY,  // an entity blip on this game's copy of the entity
    FOLLOW,  // a radar-only coordinate blip at the host's position, until the entity resolves (or for good: objects,
             // pickups, searchlights, trains)
    COORD,   // the same coordinate or contact point blip as the host's
};

// bResolved: this game has the entity the host's blip points at (a live ped or vehicle with that network id);
// bLocalPlayer: it is this game's own player
inline eLocalForm DecideLocalForm(const SBlipState& host, bool bResolved, bool bLocalPlayer)
{
    if (IsCoordinateType(host.type))
    {
        return eLocalForm::COORD;
    }
    if (host.entityKind == eEntityKind::PLAYER && bLocalPlayer)
    {
        return eLocalForm::NONE;
    }
    if (host.entityKind != eEntityKind::NONE && bResolved)
    {
        return eLocalForm::ENTITY;
    }
    return eLocalForm::FOLLOW;
}

// the eBlipType of the local blip
inline uint8_t LocalType(const SBlipState& host, eLocalForm form)
{
    switch (form)
    {
        case eLocalForm::ENTITY:
            if (host.entityKind == eEntityKind::VEHICLE)
            {
                return Type::CAR;
            }
            if (host.entityKind == eEntityKind::OBJECT)
            {
                return Type::OBJECT;
            }
            return Type::CHAR;
        case eLocalForm::FOLLOW:
            return Type::COORD;
        case eLocalForm::COORD:
            return host.type == Type::CONTACT_POINT ? Type::CONTACT_POINT : Type::COORD;
        default:
            return Type::NONE;
    }
}

// A blip that follows the host's position shows on the radar only, so no ground marker jumps around (design 3.5).
inline uint8_t LocalDisplay(const SBlipState& host, eLocalForm form)
{
    if (form == eLocalForm::FOLLOW)
    {
        return (host.display & Display::BLIP_ONLY) != 0 ? Display::BLIP_ONLY : Display::NEITHER;
    }
    return host.display;
}

// The local blip has to be made again rather than updated: there is none (or its trace is gone), it has another form or
// type, or it is an entity blip on another entity (a pool reference).
inline bool NeedsRebuild(bool bHaveLocal, eLocalForm haveForm, uint8_t haveType, int haveEntityRef, eLocalForm wantForm,
    uint8_t wantType, int wantEntityRef)
{
    if (!bHaveLocal || haveForm != wantForm || haveType != wantType)
    {
        return true;
    }
    return wantForm == eLocalForm::ENTITY && haveEntityRef != wantEntityRef;
}

// A receiver applies MISSION_MIRROR_BLIPS only from the current host: a former host's packet still in the packet buffer
// (it waits 50 ms there, PLAYER_DISCONNECTED and PLAYER_ASSIGN_HOST don't) is dropped, and so is everything while no
// host is known or while this game is the host.
inline bool AcceptFrom(bool bIsHost, bool bHasHost, int hostId, int senderId)
{
    return !bIsHost && bHasHost && senderId == hostId;
}

// One of the host's blips on a receiver.
struct SMirroredBlip
{
    uint32_t hostHandle = 0;
    SBlipState host;          // the host's last full state; POSITION updates its position
    int localHandle = -1;     // this game's radar handle, -1 = none
    eLocalForm form = eLocalForm::NONE;
    uint8_t localType = Type::NONE;
    int localEntityRef = 0;   // ENTITY: the pool reference the local blip points at
};

class CMirroredBlips
{
public:
    SMirroredBlip* Find(uint32_t hostHandle)
    {
        for (int i = 0; i < m_nCount; i++)
        {
            if (m_blips[i].hostHandle == hostHandle)
            {
                return &m_blips[i];
            }
        }
        return nullptr;
    }

    // a full state: the existing entry with the new state, or a new one without a local blip (nullptr when full)
    SMirroredBlip* Upsert(uint32_t hostHandle, const SBlipState& state)
    {
        SMirroredBlip* pBlip = Find(hostHandle);
        if (pBlip == nullptr)
        {
            if (m_nCount >= MAX_BLIPS)
            {
                return nullptr;
            }
            pBlip = &m_blips[m_nCount++];
            *pBlip = SMirroredBlip{};
            pBlip->hostHandle = hostHandle;
        }
        pBlip->host = state;
        return pBlip;
    }

    // the host removed it: false when unknown; localHandle gets its local blip (-1 = none) for the caller to clear
    bool Remove(uint32_t hostHandle, int& localHandle)
    {
        localHandle = -1;
        for (int i = 0; i < m_nCount; i++)
        {
            if (m_blips[i].hostHandle == hostHandle)
            {
                localHandle = m_blips[i].localHandle;
                m_blips[i] = m_blips[m_nCount - 1];
                m_nCount--;
                return true;
            }
        }
        return false;
    }

    int Count() const { return m_nCount; }
    SMirroredBlip& At(int i) { return m_blips[i]; }
    const SMirroredBlip& At(int i) const { return m_blips[i]; }
    void Clear() { m_nCount = 0; }

private:
    SMirroredBlip m_blips[MAX_BLIPS]{};
    int m_nCount = 0;
};
}  // namespace MirrorBlipsPolicy
