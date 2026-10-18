#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CAimSync.h>
#include <CEntryExitTransitionSync.h>
#include <CProjectileInfo.h>
#include "CTrainSyncPolicy.h"
#include "CBulletSyncPolicy.h"

PACKET_HANDLER(ePacketType::PLAYER_ONFOOT_UPDATE, Packets::Players::OnFootUpdate* pOnFootUpdate)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pOnFootUpdate->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    // private build: the snapshot is kept first; a missing or broken ped is recreated at its position
    // (CNetworkPlayer::GetOrRecoverPed), and the ped is used only while it has a clump and intelligence
    pNetworkPlayer->m_oldControllerState = pOnFootUpdate->keySnapshot.oldControllerState;
    pNetworkPlayer->m_newControllerState = pOnFootUpdate->keySnapshot.newControllerState;

    pNetworkPlayer->m_onFootSnapshotInterpolated = *pOnFootUpdate;

    CVector vecPos = pOnFootUpdate->vecPos;
    CPlayerPed* pPlayerPed = pNetworkPlayer->GetOrRecoverPed(&vecPos);
    if (pPlayerPed == nullptr)
    {
        return;
    }

    if (pPlayerPed->m_pVehicle != nullptr && pPlayerPed->m_nPedFlags.bInVehicle)
    {
        pNetworkPlayer->RemoveFromVehicle(pPlayerPed->m_pVehicle);
        if ((pPlayerPed = pNetworkPlayer->GetReadyPed()) == nullptr)
        {
            return;
        }
    }

    if (TrainSyncPolicy::ShouldApplyOnFootPosition(pPlayerPed->m_pAttachedTo != nullptr))
    {
        pPlayerPed->SetPosn(pOnFootUpdate->vecPos);
        pPlayerPed->m_vecMoveSpeed = pOnFootUpdate->vecMoveSpeed;
    }

    pNetworkPlayer->ApplyWeaponSnapshot(pOnFootUpdate->weaponSnapshot);
    if ((pPlayerPed = pNetworkPlayer->GetReadyPed()) == nullptr)
    {
        return;
    }

    pPlayerPed->m_fCurrentRotation = pOnFootUpdate->currentRotation.m_angle;
    pPlayerPed->m_fAimingRotation = pOnFootUpdate->aimingRotation.m_angle;

    CUtil::SetPlayerJetpack(pNetworkPlayer, pOnFootUpdate->bHasJetpack);
    if ((pPlayerPed = pNetworkPlayer->GetReadyPed()) == nullptr)
    {
        return;
    }

    if (CUtil::IsDucked(pPlayerPed) != pOnFootUpdate->bDucking)
    {
        if (pOnFootUpdate->bDucking)
        {
            pPlayerPed->m_pIntelligence->SetTaskDuckSecondary(0);
            // abort fighting and jumping tasks maybe?
        }
        else
        {
            pPlayerPed->m_pIntelligence->ClearTaskDuckSecondary();
        }
    }

    pPlayerPed->m_nFightingStyle = pOnFootUpdate->iFightingStyle;
    pPlayerPed->m_nAllowedAttackMoves |= 15u;
}

PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC, Packets::Players::KeyPressed* pKeyPressed)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pKeyPressed->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_oldControllerState = pKeyPressed->keySnapshot.oldControllerState;
    pNetworkPlayer->m_newControllerState = pKeyPressed->keySnapshot.newControllerState;

    if (CPlayerPed* pPlayerPed = pNetworkPlayer->GetLivePed()) // private build: no destroyed ped
    {
        pPlayerPed->m_fCurrentRotation = pKeyPressed->currentRotation.m_angle;
        pPlayerPed->m_fAimingRotation = pKeyPressed->aimingRotation.m_angle;
    }

    pNetworkPlayer->m_onFootSnapshotInterpolated.currentRotation = pKeyPressed->currentRotation;
    pNetworkPlayer->m_onFootSnapshotInterpolated.aimingRotation = pKeyPressed->aimingRotation;
}

PACKET_HANDLER(ePacketType::PLAYER_CAMERA_SYNC, Packets::Players::PlayerCameraSync* pPlayerCameraSync)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerCameraSync->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_startedInterpolatingCameraAt = GetTickCount();
    pNetworkPlayer->m_cameraSnapshotOld = pNetworkPlayer->m_cameraSnapshot;
    if (pPlayerCameraSync->bFullUpdate)
    {
        pNetworkPlayer->m_cameraSnapshot = *pPlayerCameraSync;
    }
    else
    {
        pNetworkPlayer->m_cameraSnapshot.cameraMode = pPlayerCameraSync->cameraMode;
        pNetworkPlayer->m_cameraSnapshot.orientation = pPlayerCameraSync->orientation;
        pNetworkPlayer->m_cameraSnapshot.front.x = pPlayerCameraSync->front.x;
        pNetworkPlayer->m_cameraSnapshot.front.y = pPlayerCameraSync->front.y;
    }
}

PACKET_HANDLER(ePacketType::SET_PLAYER_TASK, Packets::Players::SetPlayerTask* pSetPlayerTask)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pSetPlayerTask->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->HandleTask(*pSetPlayerTask);
}

PACKET_HANDLER(ePacketType::ENEX_TRANSITION, Packets::Players::EnExTransition* pEnExTransition)
{
    CEntryExitTransitionSync::Receive(*pEnExTransition);
}

PACKET_HANDLER(ePacketType::PLAYER_PLACE_WAYPOINT, Packets::Players::PlayerPlaceWaypoint* pPlayerPlaceWaypoint)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerPlaceWaypoint->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_waypointState = *pPlayerPlaceWaypoint;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("WAYPOINT PLACE %d %.0f %.0f\n", packet->place, packet->position.x, packet->position.y);
#endif
}

PACKET_HANDLER(ePacketType::RESPAWN_PLAYER, Packets::Players::RespawnPlayer* pRespawnPlayer)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pRespawnPlayer->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->Respawn();
}

PACKET_HANDLER(ePacketType::PLAYER_BULLET_SHOT, Packets::Players::PlayerBulletShot* pPlayerBulletShot)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerBulletShot->playerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    // private build: the shot needs the shooter's ped with a clump (weapon model, impact)
    CPlayerPed* pPlayerPed = pNetworkPlayer->GetReadyPed();
    if (pPlayerPed == nullptr)
    {
        return;
    }

    pPlayerPed->SetCurrentWeapon(pPlayerBulletShot->iWeaponType);

    CEntity* owner = pPlayerPed;
    CEntity* victim = nullptr;
    CVector startPoint = pPlayerBulletShot->startPos;
    CVector endPoint = pPlayerBulletShot->endPos;
    CColPoint colPoint;
    int incrementalHit = pPlayerBulletShot->incrementalHit;

    if (pPlayerBulletShot->bHitSomething)
    {
        CVector endPosCalced = (endPoint - startPoint);
        endPosCalced.Normalise();
        endPosCalced = endPoint + endPosCalced * 10.0f;
        endPoint = endPosCalced;
        CWorld::pIgnoreEntity = owner;
        CWorld::ProcessLineOfSight(startPoint, endPoint, colPoint, victim, 1, 1, 1, 1, 1, 0, 0, 1);
        CWorld::pIgnoreEntity = nullptr;

        if (BulletSyncPolicy::ShouldOverrideRaycastVictimWithNetworkEntity(
                pPlayerBulletShot->bHitSomething, pPlayerBulletShot->bHitNetworkEntity))
        {
            colPoint.m_vecPoint = pPlayerBulletShot->endPos;
            victim = pPlayerBulletShot->hitEntity.GetEntity();
            if (pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_PLAYER ||
                pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_PED)
            {
                colPoint.m_nSurfaceTypeB = pPlayerBulletShot->pedPiece;
            }
        }
        else if (victim == nullptr)
        {
            colPoint.m_vecPoint = pPlayerBulletShot->endPos;
        }
    }
    else
    {
        colPoint.m_vecPoint.Set(0.0f, 0.0f, 0.0f);
        colPoint.m_vecNormal.Set(0.0f, 0.0f, 0.0f);
        colPoint.m_nLightingB.day = 0;
        colPoint.m_nLightingB.night = 0;
        colPoint.m_nPieceTypeB = 0;
        colPoint.m_nSurfaceTypeB = 0;
    }

    if (victim && !victim->IsVTableValid())
    {
        victim = nullptr;
    }

    pPlayerPed->GetWeapon().DoBulletImpact(
        owner, victim, &startPoint, &pPlayerBulletShot->endPos, &colPoint, incrementalHit);
}

PACKET_HANDLER(ePacketType::ADD_PROJECTILE, Packets::Players::AddProjectile* pAddProjectile)
{
    if (pAddProjectile->creator.GetEntity() == nullptr)
    {
        return;
    }

    CVector origin = pAddProjectile->origin;
    CVector dir = pAddProjectile->dir;

    if (pAddProjectile->creator.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(pAddProjectile->creator.GetEntity()))
        {
            // private build: a network player's projectile needs its ped with a clump and intelligence
            if (!networkPlayer->IsModelReady())
            {
                return;
            }

            CAimSync::ApplyNetworkPlayerContext(networkPlayer);
        }
    }

    CEntity* pTarget = nullptr;
    if (pAddProjectile->bTarget)
    {
        pTarget = pAddProjectile->target.GetEntity();
    }
    if (pAddProjectile->bDir)
    {
        CProjectileInfo::AddProjectile(pAddProjectile->creator.GetEntity(), pAddProjectile->projectileType, origin, pAddProjectile->force, nullptr, pTarget);
    }
    else
    {
        CProjectileInfo::AddProjectile(pAddProjectile->creator.GetEntity(), pAddProjectile->projectileType, origin, pAddProjectile->force, &pAddProjectile->dir, pTarget);
    }

    if (pAddProjectile->creator.entityType == NETWORK_ENTITY_TYPE_PLAYER && CNetworkPlayerManager::GetPlayer(pAddProjectile->creator.GetEntity()))
    {
        CAimSync::ApplyLocalContext();
    }
}

PACKET_HANDLER(ePacketType::PLAYER_STATS, Packets::Players::PlayerStats* pPlayerStats)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerStats->playerid))
    {
        for (size_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
        {
            eStats statId = CStatsSync::m_aeSyncedStats[i];
            pNetworkPlayer->m_stats[statId] = pPlayerStats->stats[i];
        }
    }
}

PACKET_HANDLER(ePacketType::REBUILD_PLAYER, Packets::Players::RebuildPlayer* pRebuildPlayer)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pRebuildPlayer->playerid))
    {
        pNetworkPlayer->m_stats[STAT_FAT] = pRebuildPlayer->clothesDesc.m_fFatStat;
        pNetworkPlayer->m_stats[STAT_MUSCLE] = pRebuildPlayer->clothesDesc.m_fMuscleStat;

        //CStatsSync::ApplyNetworkPlayerContext(networkPlayer);

        // private build: stored, and applied once the ped has a clump and intelligence again instead of never
        pNetworkPlayer->SetClothes(pRebuildPlayer->clothesDesc);

        //CStatsSync::ApplyLocalContext();
    }
}
