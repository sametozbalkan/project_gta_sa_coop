#include "../client/src/CBulletSyncPolicy.h"

#include <cassert>
#include <cstdio>

namespace
{
using namespace BulletSyncPolicy;

void TestResolveSerializedEntity()
{
    // Null entity -> (kNetEntityNothing, 0)
    {
        const auto res = ResolveSerializedEntity(false, kEntityTypePed, true, 0);
        assert(res.entityType == kNetEntityNothing);
        assert(res.entityId == 0);
    }

    // Networked player (lookupNetworkId = 0 or 2) -> (kNetEntityPlayer, id)
    {
        const auto res0 = ResolveSerializedEntity(true, kEntityTypePed, true, 0);
        assert(res0.entityType == kNetEntityPlayer);
        assert(res0.entityId == 0);

        const auto res2 = ResolveSerializedEntity(true, kEntityTypePed, true, 2);
        assert(res2.entityType == kNetEntityPlayer);
        assert(res2.entityId == 2);
    }

    // Un-networked player ped (lookupNetworkId = -1) -> (kNetEntityNothing, 0)
    {
        const auto res = ResolveSerializedEntity(true, kEntityTypePed, true, -1);
        assert(res.entityType == kNetEntityNothing);
        assert(res.entityId == 0);
    }

    // Networked ped (lookupNetworkId = 5) -> (kNetEntityPed, 5)
    {
        const auto res = ResolveSerializedEntity(true, kEntityTypePed, false, 5);
        assert(res.entityType == kNetEntityPed);
        assert(res.entityId == 5);
    }

    // Un-networked ped (lookupNetworkId = -1) -> (kNetEntityNothing, 0)
    // Regression test for the bug that sent NETWORK_ENTITY_TYPE_PED with entityId = 0, hitting NPC 0.
    {
        const auto res = ResolveSerializedEntity(true, kEntityTypePed, false, -1);
        assert(res.entityType == kNetEntityNothing);
        assert(res.entityId == 0);
    }

    // Networked vehicle (lookupNetworkId = 12) -> (kNetEntityVehicle, 12)
    {
        const auto res = ResolveSerializedEntity(true, kEntityTypeVehicle, false, 12);
        assert(res.entityType == kNetEntityVehicle);
        assert(res.entityId == 12);
    }

    // Un-networked vehicle (lookupNetworkId = -1) -> (kNetEntityNothing, 0)
    {
        const auto res = ResolveSerializedEntity(true, kEntityTypeVehicle, false, -1);
        assert(res.entityType == kNetEntityNothing);
        assert(res.entityId == 0);
    }

    // CObject, CBuilding, CDummy, Nothing -> (kNetEntityNothing, 0)
    {
        const auto objRes = ResolveSerializedEntity(true, kEntityTypeObject, false, 10);
        assert(objRes.entityType == kNetEntityNothing);
        assert(objRes.entityId == 0);

        const auto bldRes = ResolveSerializedEntity(true, kEntityTypeBuilding, false, 10);
        assert(bldRes.entityType == kNetEntityNothing);
        assert(bldRes.entityId == 0);

        const auto dummyRes = ResolveSerializedEntity(true, kEntityTypeDummy, false, 10);
        assert(dummyRes.entityType == kNetEntityNothing);
        assert(dummyRes.entityId == 0);

        const auto nothingRes = ResolveSerializedEntity(true, kEntityTypeNothing, false, 10);
        assert(nothingRes.entityType == kNetEntityNothing);
        assert(nothingRes.entityId == 0);
    }
}

void TestClassifyShot()
{
    // Missed shot (hasVictim = false) -> bHitSomething = false, bHitNetworkEntity = false, bHasPedPiece = false
    {
        const auto shot = ClassifyShot(false, kEntityTypeNothing, kNetEntityNothing, 0, 0);
        assert(!shot.bHitSomething);
        assert(!shot.bHitNetworkEntity);
        assert(!shot.bHasPedPiece);
        assert(shot.pedPiece == kPedPieceMin);
    }

    // Shot hitting a CObject (kEntityTypeObject) -> bHitSomething = true, bHitNetworkEntity = false, bHasPedPiece = false
    // Regression test for OB-0 where owner->m_nType caused bHitNetworkEntity = true.
    {
        const auto target = ResolveSerializedEntity(true, kEntityTypeObject, false, -1);
        const auto shot = ClassifyShot(true, kEntityTypeObject, target.entityType, target.entityId, 0);
        assert(shot.bHitSomething);
        assert(!shot.bHitNetworkEntity);
        assert(!shot.bHasPedPiece);
    }

    // Shot hitting a CBuilding / glass (kEntityTypeBuilding) -> bHitSomething = true, bHitNetworkEntity = false
    {
        const auto target = ResolveSerializedEntity(true, kEntityTypeBuilding, false, -1);
        const auto shot = ClassifyShot(true, kEntityTypeBuilding, target.entityType, target.entityId, 0);
        assert(shot.bHitSomething);
        assert(!shot.bHitNetworkEntity);
        assert(!shot.bHasPedPiece);
    }

    // Shot hitting an un-networked CPed (kEntityTypePed, serializedNetworkEntityType = kNetEntityNothing)
    // -> bHitSomething = true, bHitNetworkEntity = false, bHasPedPiece = false
    {
        const auto target = ResolveSerializedEntity(true, kEntityTypePed, false, -1);
        const auto shot = ClassifyShot(true, kEntityTypePed, target.entityType, target.entityId, 9);
        assert(shot.bHitSomething);
        assert(!shot.bHitNetworkEntity);
        assert(!shot.bHasPedPiece);
    }

    // Shot hitting a networked CPed (kEntityTypePed, kNetEntityPed, id 3, piece 9)
    // -> bHitSomething = true, bHitNetworkEntity = true, bHasPedPiece = true, pedPiece = 9
    {
        const auto target = ResolveSerializedEntity(true, kEntityTypePed, false, 3);
        const auto shot = ClassifyShot(true, kEntityTypePed, target.entityType, target.entityId, 9);
        assert(shot.bHitSomething);
        assert(shot.bHitNetworkEntity);
        assert(shot.bHasPedPiece);
        assert(shot.pedPiece == 9);
    }

    // Shot hitting a networked Player CPed (kEntityTypePed, kNetEntityPlayer, id 1, piece 5)
    {
        const auto target = ResolveSerializedEntity(true, kEntityTypePed, true, 1);
        const auto shot = ClassifyShot(true, kEntityTypePed, target.entityType, target.entityId, 5);
        assert(shot.bHitSomething);
        assert(shot.bHitNetworkEntity);
        assert(shot.bHasPedPiece);
        assert(shot.pedPiece == 5);
    }

    // Shot hitting a networked CPed with out-of-range rawSurfaceTypeB (0 or 99) -> pedPiece clamped to [3, 9]
    {
        const auto shotLow = ClassifyShot(true, kEntityTypePed, kNetEntityPed, 3, 0);
        assert(shotLow.bHitSomething);
        assert(shotLow.bHitNetworkEntity);
        assert(shotLow.bHasPedPiece);
        assert(shotLow.pedPiece == kPedPieceMin);

        const auto shotHigh = ClassifyShot(true, kEntityTypePed, kNetEntityPed, 3, 99);
        assert(shotHigh.bHitSomething);
        assert(shotHigh.bHitNetworkEntity);
        assert(shotHigh.bHasPedPiece);
        assert(shotHigh.pedPiece == kPedPieceMax);
    }

    // Shot hitting a networked CVehicle (kEntityTypeVehicle, kNetEntityVehicle, id 4)
    // -> bHitSomething = true, bHitNetworkEntity = true, bHasPedPiece = false
    {
        const auto target = ResolveSerializedEntity(true, kEntityTypeVehicle, false, 4);
        const auto shot = ClassifyShot(true, kEntityTypeVehicle, target.entityType, target.entityId, 0);
        assert(shot.bHitSomething);
        assert(shot.bHitNetworkEntity);
        assert(!shot.bHasPedPiece);
    }
}

void TestShouldSendMinigunShot()
{
    // Returns false for miss (hasVictim = false), building (kEntityTypeBuilding), dummy (kEntityTypeDummy)
    assert(!ShouldSendMinigunShot(false, kEntityTypeNothing));
    assert(!ShouldSendMinigunShot(false, kEntityTypePed));
    assert(!ShouldSendMinigunShot(true, kEntityTypeNothing));
    assert(!ShouldSendMinigunShot(true, kEntityTypeBuilding));
    assert(!ShouldSendMinigunShot(true, kEntityTypeDummy));

    // Returns true for ped (kEntityTypePed), vehicle (kEntityTypeVehicle), and object (kEntityTypeObject)
    assert(ShouldSendMinigunShot(true, kEntityTypePed));
    assert(ShouldSendMinigunShot(true, kEntityTypeVehicle));
    assert(ShouldSendMinigunShot(true, kEntityTypeObject));
}

void TestShouldOverrideRaycastVictimWithNetworkEntity()
{
    // Returns true only when bHitSomething && bHitNetworkEntity
    assert(ShouldOverrideRaycastVictimWithNetworkEntity(true, true));

    // Returns false for object/building hits (bHitSomething && !bHitNetworkEntity),
    // preserving the receiver's CWorld::ProcessLineOfSight hit.
    assert(!ShouldOverrideRaycastVictimWithNetworkEntity(true, false));

    // Returns false when nothing was hit
    assert(!ShouldOverrideRaycastVictimWithNetworkEntity(false, false));
    assert(!ShouldOverrideRaycastVictimWithNetworkEntity(false, true));
}
} // namespace

int main()
{
    TestResolveSerializedEntity();
    TestClassifyShot();
    TestShouldSendMinigunShot();
    TestShouldOverrideRaycastVictimWithNetworkEntity();

    std::printf("bullet_sync_test passed.\n");
    return 0;
}
