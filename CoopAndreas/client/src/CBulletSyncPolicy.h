#pragma once

#include <cstdint>

namespace BulletSyncPolicy
{
// Matches GTA eEntityType values without requiring CEntity.h in standalone policy tests.
constexpr int kEntityTypeNothing = 0;
constexpr int kEntityTypeBuilding = 1;
constexpr int kEntityTypeVehicle = 2;
constexpr int kEntityTypePed = 3;
constexpr int kEntityTypeObject = 4;
constexpr int kEntityTypeDummy = 5;

// Matches eNetworkEntityType values in shared/network/enums.h:
// 0 = NETWORK_ENTITY_TYPE_PLAYER
// 1 = NETWORK_ENTITY_TYPE_NOTHING
// 2 = NETWORK_ENTITY_TYPE_BUILDING
// 3 = NETWORK_ENTITY_TYPE_VEHICLE
// 4 = NETWORK_ENTITY_TYPE_PED
// 5 = NETWORK_ENTITY_TYPE_OBJECT
// 6 = NETWORK_ENTITY_TYPE_DUMMY
// 7 = NETWORK_ENTITY_TYPE_NOTINPOOLS
constexpr int kNetEntityPlayer = 0;
constexpr int kNetEntityNothing = 1;
constexpr int kNetEntityVehicle = 3;
constexpr int kNetEntityPed = 4;

// Matches ePedPieceTypes serialization range in Packets::Players::PlayerBulletShot:
// PED_COL_SPHERE_LEG (3) .. PED_SPHERE_HEAD (9).
constexpr int kPedPieceMin = 3;
constexpr int kPedPieceMax = 9;

struct SerializedEntityTarget
{
    int entityType = kNetEntityNothing;
    int entityId = 0;
};

struct ShotClassification
{
    bool bHitSomething = false;
    bool bHitNetworkEntity = false;
    bool bHasPedPiece = false;
    int pedPiece = kPedPieceMin;
};

constexpr int ClampPedPiece(int rawPiece) noexcept
{
    if (rawPiece < kPedPieceMin)
        return kPedPieceMin;
    if (rawPiece > kPedPieceMax)
        return kPedPieceMax;
    return rawPiece;
}

// Resolves a local CEntity into a wire (entityType, entityId) pair for CNetworkEntitySerializer::SetEntity.
// Never emits PLAYER/PED/VEHICLE with a fallback id of 0 when the entity has no valid network id (lookupId < 0);
// un-networked peds/vehicles, objects, buildings, dummies, and null entities resolve to (kNetEntityNothing, 0).
constexpr SerializedEntityTarget ResolveSerializedEntity(
    bool hasEntity,
    int gtaEntityType,
    bool isPlayerPed,
    int lookupNetworkId) noexcept
{
    SerializedEntityTarget out{};
    if (!hasEntity || lookupNetworkId < 0)
        return out;

    if (gtaEntityType == kEntityTypePed)
    {
        out.entityType = isPlayerPed ? kNetEntityPlayer : kNetEntityPed;
        out.entityId = lookupNetworkId;
    }
    else if (gtaEntityType == kEntityTypeVehicle)
    {
        out.entityType = kNetEntityVehicle;
        out.entityId = lookupNetworkId;
    }
    return out;
}

constexpr bool IsResolvedNetworkEntity(int networkEntityType, int networkEntityId) noexcept
{
    return (networkEntityType == kNetEntityPlayer ||
            networkEntityType == kNetEntityVehicle ||
            networkEntityType == kNetEntityPed) &&
           networkEntityId >= 0;
}

// Classifies a local bullet impact from the victim entity (NOT the shooter owner!) and its serialized network target.
constexpr ShotClassification ClassifyShot(
    bool hasVictim,
    int victimGtaEntityType,
    int serializedNetworkEntityType,
    int serializedNetworkEntityId,
    int rawSurfaceTypeB) noexcept
{
    ShotClassification out{};
    if (!hasVictim)
        return out;

    out.bHitSomething = true;
    out.bHitNetworkEntity = IsResolvedNetworkEntity(serializedNetworkEntityType, serializedNetworkEntityId);
    if (out.bHitNetworkEntity && victimGtaEntityType == kEntityTypePed)
    {
        out.bHasPedPiece = true;
        out.pedPiece = ClampPedPiece(rawSurfaceTypeB);
    }
    return out;
}

// Minigun rate-reduction filter: allow sending shots that hit peds, vehicles, or objects (barrels, targets, etc.),
// while suppressing misses and world building/ground impacts.
constexpr bool ShouldSendMinigunShot(bool hasVictim, int victimGtaEntityType) noexcept
{
    if (!hasVictim)
        return false;
    return victimGtaEntityType == kEntityTypeVehicle ||
           victimGtaEntityType == kEntityTypePed ||
           victimGtaEntityType == kEntityTypeObject;
}

// Receiver rule: should the receiver overwrite the local ProcessLineOfSight victim with hitEntity.GetEntity()?
// Only when bHitNetworkEntity is true. When bHitSomething is true and bHitNetworkEntity is false (object, glass,
// building, or un-networked entity hit), the receiver keeps the local raycast victim and colPoint.
constexpr bool ShouldOverrideRaycastVictimWithNetworkEntity(bool bHitSomething, bool bHitNetworkEntity) noexcept
{
    return bHitSomething && bHitNetworkEntity;
}
} // namespace BulletSyncPolicy
