#include "stdafx.h"
#include "PlayerHooks.h"
#include "CKeySync.h"
#include "CAimSync.h"
#include <game_sa/CPedDamageResponseInfo.h>
#include <CPacketBuffer.h>
#include <CServerTime.h>
#include <CDepartedPlayerPeds.h>
#include <CSessionLifecycle.h>
#include <COpCodeSync.h>
#include <CStoryStartPolicy.h>
#include "CBulletSyncPolicy.h"

static void __fastcall CPlayerPed__ProcessControl_Hook(CPlayerPed* This)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);

    if (This == localPlayer)
    {
        patch::SetRaw(0x6884C4, (void*)"\xD9\x96\x5C\x05\x00\x00", 6, false);
        plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
        patch::Nop(0x6884C4, 6, false);
        return;
    }

    // private build: a departed player's stand-in is static, so CWorld::Process doesn't reach it; should a script
    // command have put it into the moving list, it still doesn't act (the original would read the local pad for it)
    if (CDepartedPlayerPeds::IsStandIn(This))
    {
        return;
    }

    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(This);

    if (player == nullptr)
    {
        plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
        return;
    }

    // private build: a ped kept from a destroyed vehicle drops its in-car tasks before they run again
    player->FinishKeptPed(true);

    int playerNum = player->GetInternalId();

    if (playerNum == -1)
    {
        plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
        return;
    }

    CWorld::PlayerInFocus = playerNum;

    CKeySync::ApplyNetworkPlayerContext(player);
    CAimSync::ApplyNetworkPlayerContext(player);
    // CStatsSync::ApplyNetworkPlayerContext(player);

    // if (CPad::GetPad(0)->NewState.RightShoulder1) // is aiming
    //{
    //     player->m_pPed->m_fCurrentRotation = player->m_playerOnFoot.currentRotation;
    // }
    // player->m_pPed->m_fAimingRotation = player->m_playerOnFoot.aimingRotation;

    player->m_pPed->m_fHealth = player->m_onFootSnapshotInterpolated.healthSnapshot.iHealth;
    player->m_pPed->m_fArmour = player->m_onFootSnapshotInterpolated.healthSnapshot.iArmour;

    /*CTask* activeTask = player->m_pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();

    if (activeTask && activeTask->GetTaskType() != eTaskType::TASK_COMPLEX_JUMP)
    {
        player->m_pPed->m_vecMoveSpeed = player->m_onFootSnapshotInterpolated.vecMoveSpeed;
    }*/

    plugin::CallMethod<0x60EA90, CPlayerPed*>(This);

    // player->m_pPed->m_fAimingRotation = player->m_onFootSnapshotInterpolated.aimingRotation.m_angle;

    CWorld::PlayerInFocus = 0;

    CKeySync::ApplyLocalContext();
    CAimSync::ApplyLocalContext();
    // CStatsSync::ApplyLocalContext();
}

static void CollectShotSnapshot(Packets::Players::PlayerBulletShot& packet, CEntity* owner, CVector* startPoint,
    CVector* endPoint, CColPoint* colPoint, CEntity* victim, int incrementalHit, eWeaponType weaponType)
{
    (void)owner;
    packet.iWeaponType = weaponType;
    packet.startPos = *startPoint;
    if (victim && colPoint)
    {
        packet.endPos = colPoint->m_vecPoint;
    }
    else
    {
        packet.endPos = *endPoint;
    }

    packet.hitEntity.SetEntity(victim);
    const auto shot = BulletSyncPolicy::ClassifyShot(
        victim != nullptr,
        victim ? victim->m_nType : 0,
        static_cast<int>(packet.hitEntity.entityType),
        packet.hitEntity.entityId,
        colPoint ? colPoint->m_nSurfaceTypeB : 0);

    packet.bHitSomething = shot.bHitSomething;
    packet.bHitNetworkEntity = shot.bHitNetworkEntity;
    if (shot.bHasPedPiece)
    {
        packet.pedPiece = static_cast<ePedPieceTypes>(shot.pedPiece);
    }

    packet.incrementalHit = incrementalHit;
}

static void __fastcall CWeapon__DoBulletImpact_Hook(CWeapon* weapon, SKIP_EDX, CEntity* owner, CEntity* victim,
    CVector* startPoint, CVector* endPoint, CColPoint* colPoint, int incrementalHit)
{
    if (owner == FindPlayerPed(0))
    {
        if (!BulletSyncPolicy::ShouldSendMinigunShot(victim != nullptr, victim ? victim->m_nType : 0) && weapon->m_eWeaponType == WEAPON_MINIGUN)
        {
            weapon->DoBulletImpact(owner, victim, startPoint, endPoint, colPoint, incrementalHit);
            return;
        }
        Packets::Players::PlayerBulletShot packet{};
        CollectShotSnapshot(packet, owner, startPoint, endPoint, colPoint, victim, incrementalHit, ((CPed*)owner)->GetWeapon().m_eWeaponType);
        GetPacketFactory().Send(packet);

        weapon->DoBulletImpact(owner, victim, startPoint, endPoint, colPoint, incrementalHit);
    }
    else if (owner->m_nType == eEntityType::ENTITY_TYPE_PED)
    {
        CPed* ped = (CPed*)owner;

        if (!ped->IsPlayer())
        {
            weapon->DoBulletImpact(owner, victim, startPoint, endPoint, colPoint, incrementalHit);
        }
    }
}

static void __fastcall CPedIK__PointGunInDirection_Hook(
    CPedIK* This, SKIP_EDX, float dirX, float dirY, char flag, float float1)
{
    // if (This->m_pPed == FindPlayerPed(0))
    //{
    //     // save last aim data for syncing
    //     CLocalPlayer::m_vecLastAimX = dirX;
    //     CLocalPlayer::m_vecLastAimY = dirY;

    //    This->PointGunInDirection(dirX, dirY, flag, float1);
    //    return;
    //}

    // CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(This->m_pPed);

    // if (player == nullptr)
    //{
    //     This->PointGunInDirection(dirX, dirY, flag, float1);
    //     return;
    // }

    // if (player->m_playerOnFoot == nullptr)
    //     return;

    // player->m_pPed->m_fAimingRotation = player->m_playerOnFoot.aimingRotation;
    //
    // eWeaponType weapon = player->m_pPed->m_aWeapons[player->m_pPed->m_nActiveWeaponSlot].m_eWeaponType;

    ///*if (weapon != WEAPON_SNIPERRIFLE)
    //    dirY = player->m_aimSyncData.aimY;*/

    This->PointGunInDirection(dirX, dirY, flag, float1);
}

static void __fastcall CPlayerPed__dctor_Hook(CPlayerPed* This, SKIP_EDX)
{
    // private build: the game can delete a network player's ped on its own (CNetworkPlayer.cpp); this call (0x60A9A3,
    // the only call of ~CPlayerPed 0x6093B0) sees every CPlayerPed, so its network player forgets it here
    CNetworkPlayerManager::OnPlayerPedDestroyed(This);
    // private build: a departed player's stand-in has its own player data, which ~CPlayerPed reads (CDepartedPlayerPeds)
    void* pStandInPlayerData = CDepartedPlayerPeds::OnPedDestroying(This);

    This->m_pPlayerData->m_nMeleeWeaponAnimReferencedExtra = 0;

    plugin::CallMethod<0x6093B0, CPlayerPed*>(This);

    CDepartedPlayerPeds::FreePlayerData(pStandInPlayerData);
}

// private build (remote player ped recovery): CPlayerPed's FlagToDestroyWhenNextProcessed, vtable slot 21 (0x86D1BC in
// the CPlayerPed vtable 0x86D168, read from gta_sa.exe: holds CPed's 0x5E7B70, which nothing calls directly). The
// original marks the ped for deletion by the next CWorld::Process (0x568631); a vehicle's destructor calls it for its
// driver and passengers (0x6E2BC6, 0x6E2BF0). A network player's ped is kept instead (CNetworkPlayer::KeepFlaggedPed),
// so the host's mission deleting a car the friend sits in keeps the friend's ped and its handle. Every other ped,
// the local player included, gets the original.
static constexpr uintptr_t CPLAYERPED_VTBL_FLAGTODESTROY = 0x86D1BC;
static constexpr uintptr_t CPED_FLAGTODESTROY = 0x5E7B70;

static void __fastcall CPlayerPed__FlagToDestroyWhenNextProcessed_Hook(CPlayerPed* This, SKIP_EDX)
{
    if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(This))
    {
        pNetworkPlayer->KeepFlaggedPed(This);
        return;
    }

    plugin::CallMethod<CPED_FLAGTODESTROY, CPed*>(This);
}

void CReferences__RemoveReferencesToPlayer_Hook()
{
    plugin::Call<0x571AD0>();  // CReferences::RemoveReferencesToPlayer();

    if (CNetwork::m_bAuthenticated)
    {
        Packets::Players::RespawnPlayer respawnPlayer{};
        GetPacketFactory().Send(respawnPlayer);
    }
}

bool __fastcall CWeapon__TakePhotograph_Hook(CWeapon* This, SKIP_EDX, CEntity* entity, CVector* point)
{
    if (entity == FindPlayerPed(0))
    {
        return This->TakePhotograph(entity, point);
    }

    return false;
}

void __fastcall CTaskSimpleJetPack__DropJetPack_Hook(CTaskSimpleJetPack* This, SKIP_EDX, CPed* ped)
{
    if (ped != FindPlayerPed(0) && ped->IsPlayer())
    {
        // dont create a pickup if the player is network
        This->m_bIsFinished = true;
    }

    plugin::CallMethod<0x67B660, CTaskSimpleJetPack*>(This, ped);  // CTaskSimpleJetPack::DropJetPack
}

void __fastcall CPedDamageResponseCalculator__ComputeWillKillPed_Hook(
    uintptr_t This, SKIP_EDX, CPed* ped, CPedDamageResponseInfo* dmgResponse, bool speak)
{
    plugin::CallMethod<0x4B3210, uintptr_t>(This, ped, dmgResponse, speak);

    if (ped == FindPlayerPed(0))
    {
        return;
    }

    if (ped->m_nPedType > 3)  // peds
    {
        if (auto networkPed = CNetworkPedManager::GetPed(ped))
        {
            if (!networkPed->m_bSyncing)
            {
                if (networkPed->m_fHealth >= 1.0f)
                {
                    ped->m_fHealth = networkPed->m_fHealth;
                    dmgResponse->m_bHealthZero = false;
                    dmgResponse->m_bForceDeath = false;
                    dmgResponse->m_fDamageHealth = 0.0f;
                    dmgResponse->m_fDamageArmor = 0.0f;
                }
            }
        }

        return;
    }

    // players
    if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped))
    {
        if (networkPlayer->m_onFootSnapshotInterpolated.healthSnapshot.iHealth >= 1.0f)
        {
            ped->m_fHealth = networkPlayer->m_onFootSnapshotInterpolated.healthSnapshot.iHealth;
            dmgResponse->m_bHealthZero = false;
            dmgResponse->m_bForceDeath = false;
            dmgResponse->m_fDamageHealth = 0.0f;
            dmgResponse->m_fDamageArmor = 0.0f;
        }
    }
}

bool __fastcall CPlayerPed__CanPlayerStartMission_Hook(CPlayerPed* This, SKIP_EDX)
{
    return This->CanPlayerStartMission() /* && CLocalPlayer::m_bIsHost*/;
}

// private build: story missions start only from the host's game. The call at 0x4895B0 is CAN_PLAYER_START_MISSION (03EE,
// case 0x489595 of CRunningScript::ProcessCommands1000To1099 0x489500), which MAIN's story strand threads ask right before
// they launch a mission; in a session, on a game that isn't the host, it answers false for them (StoryStartPolicy), so
// their markers do nothing there. Side activities, shops and every game that plays alone keep the game's answer. The
// script asking is the one CRunningScript::Process runs a command for (COpCodeSync's hook at 0x469FF3). The other call
// (0x4577E6, CPickup::Update) is the save disk and the two-player pickups (models 0x8CD5E4, 0x8CD5F4 and 0x8CD5F8 compared
// at 0x4577C3..0x4577E2: MI_PICKUP_SAVEGAME, MI_PICKUP_2P_KILLFRENZY, MI_PICKUP_2P_COOP in gta-reversed ModelIndices.cpp)
// and keeps upstream's hook.
static bool __fastcall CPlayerPed__CanPlayerStartMission_Script_Hook(CPlayerPed* This, SKIP_EDX)
{
    CRunningScript* pScript = COpCodeSync::GetScriptRunningCommand();
    return StoryStartPolicy::MayStartMission(This->CanPlayerStartMission(), CSessionLifecycle::IsAuthenticated(),
        CSessionLifecycle::IsHost(), pScript != nullptr ? pScript->m_szName : nullptr, sizeof(pScript->m_szName));
}

void __fastcall CPlayerPed__ProcessWeaponSwitch_Hook(CPlayerPed* This, SKIP_EDX, CPad* pad)
{
    if (CWorld::PlayerInFocus == 0)
    {
        This->ProcessWeaponSwitch(pad);
    }
}

void __fastcall CTaskSimplePlayerOnFoot__PlayIdleAnimations_Hook(
    CTaskSimplePlayerOnFoot* This, SKIP_EDX, CPlayerPed* playerPed)
{
    if (CWorld::PlayerInFocus == 0)
    {
        plugin::CallMethod<0x6872C0>(This, playerPed);
    }
}

bool __fastcall CPad__JumpJustDown_Hook(CPad* This)
{
    if (CWorld::PlayerInFocus == 0)
    {
        return This->JumpJustDown();
    }

    return false;
}

void __fastcall CTaskComplexJump_CTaskManager__SetTask_Hook(
    CTaskManager* This, SKIP_EDX, CTask* task, int tasksId, bool a4)
{
    This->SetTask(task, tasksId, a4);

    if (CWorld::PlayerInFocus == 0)
    {
        CLocalPlayer::BuildTaskPacket(TASK_COMPLEX_JUMP);
    }
}

void __fastcall CRunningScript__DoDeatharrestCheck_Hook(CRunningScript* This, SKIP_EDX)
{
    if (!This->m_bWastedBustedCheck)
    {
        return;
    }

    if (!CTheScripts::IsPlayerOnAMission())
    {
        return;
    }

    bool wastedOrBusted = false;

    auto* playerInfo = &CWorld::Players[CWorld::PlayerInFocus];

    if (playerInfo->m_nPlayerState == ePlayerState::PLAYERSTATE_HASDIED ||
        playerInfo->m_nPlayerState == ePlayerState::PLAYERSTATE_HASBEENARRESTED)
    {
        wastedOrBusted = true;
    }

    if (!wastedOrBusted)
    {
        for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (auto ped = networkPlayer->GetLivePed()) // private build: not a destroyed ped's old memory
            {
                if (ped->m_ePedState == PEDSTATE_ARRESTED || ped->m_ePedState == PEDSTATE_DEAD ||
                    (ped->m_ePedState == PEDSTATE_DIE && ped->m_nPedFlags.bIsDyingStuck))
                {
                    wastedOrBusted = true;
                }
            }
        }
    }

    // private build: a mission that hasn't entered its first gosub has no return address to go back to (m_nSP 0). The story
    // trigger threads set $onmission to 1 before they launch a mission (INT.txt), a non-host's $onmission follows the
    // host's, and a dead or arrested network player counts, so the check can come in the mission's first frame; the pop
    // below then read m_apStack[0xFFFF] (script + 0x40014) as the IP and the first command crashed (ScriptsArray slot 69
    // read 0xACF0A4, which holds 0x00D2FFFF: CIP 0x00D30001, EIP 0x3F800000 from beyond CommandHandlerTable). Vanilla's
    // check (0x485A50, moved to 0x1564F00) pops the same way. The next frame's check fails the mission inside its first
    // gosub instead.
    if (wastedOrBusted && This->m_nSP == 0)
    {
        return;
    }

    if (wastedOrBusted)
    {
        CMessages::ClearSmallMessagesOnly();
        memset(&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag], 0, sizeof(uint32_t));

        if (This->m_nSP > 1u)
        {
            uint16_t nsp = This->m_nSP;
            do
            {
                --nsp;
            } while (nsp > 1u);
            This->m_nSP = nsp;
        }
        This->m_pCurrentIP = This->m_apStack[--This->m_nSP];

        This->m_bWastedOrBusted = true;
        This->m_nWakeTime = 0;
    }
}

void __fastcall CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer(
    CPedIntelligence* This, SKIP_EDX, uint16_t nLengthOfDuck)
{
    CPlayerPed* pPlayerPed = (CPlayerPed*)This->m_pPed;
    if (pPlayerPed == FindPlayerPed(0))
    {
        CLocalPlayer::BuildTaskPacket(eTaskType::TASK_SIMPLE_DUCK, true);
    }
    This->SetTaskDuckSecondary(nLengthOfDuck);
}

void __fastcall CPedIntelligence__ClearTaskDuckSecondary_HookTaskPlayer(CPedIntelligence* This, SKIP_EDX)
{
    CPlayerPed* pPlayerPed = (CPlayerPed*)This->m_pPed;
    if (pPlayerPed == FindPlayerPed(0))
    {
        CLocalPlayer::BuildTaskPacket(eTaskType::TASK_SIMPLE_DUCK, false);
    }
    This->ClearTaskDuckSecondary();
}

bool __fastcall CPad__DuckJustDown_Hook(CPad* pPad, SKIP_EDX)
{
    if (CWorld::PlayerInFocus != 0)
    {
        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(FindPlayerPed());
        if (pNetworkPlayer && pNetworkPlayer->m_bRequestedDuckTask)
        {
            pNetworkPlayer->m_bRequestedDuckTask = false;
            return true;
        }
        return false;
    }
    return pPad->DuckJustDown();
}

void CamShakeNoPos_Hook(CCamera* pCamera, float a2)
{
    if (CWorld::PlayerInFocus == 0)
    {
        plugin::Call<0x50A970>(pCamera, a2);
    }
}

void PlayerHooks::InjectHooks()
{
    patch::SetPointer(0x86D190, CPlayerPed__ProcessControl_Hook);

    patch::RedirectCall(0x73CD92, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x741199, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x7411DF, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x7412DF, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x741E30, CWeapon__DoBulletImpact_Hook);

    // patch::RedirectCall(0x5FDF7A, CPedIK__PointGunInDirection_Hook);
    // patch::RedirectCall(0x61F351, CPedIK__PointGunInDirection_Hook);
    // patch::RedirectCall(0x62876D, CPedIK__PointGunInDirection_Hook);

    // fix CPlayerPed dctor crash
    patch::RedirectCall(0x60A9A3, CPlayerPed__dctor_Hook);

    // private build (remote player ped recovery): checked before it is replaced
    if (*reinterpret_cast<const uintptr_t*>(CPLAYERPED_VTBL_FLAGTODESTROY) == CPED_FLAGTODESTROY)
    {
        patch::SetPointer(CPLAYERPED_VTBL_FLAGTODESTROY, CPlayerPed__FlagToDestroyWhenNextProcessed_Hook);
    }
    else
    {
        logger::warn("[PlayerHooks] unexpected CPlayerPed::FlagToDestroyWhenNextProcessed at 0x%X, network player peds "
            "are not kept from destroyed vehicles", CPLAYERPED_VTBL_FLAGTODESTROY);
    }

    // called when the player respawns after being busted or wasted
    patch::RedirectCall(0x443082, CReferences__RemoveReferencesToPlayer_Hook);

    patch::RedirectCall(0x74278B, CWeapon__TakePhotograph_Hook);

    patch::RedirectCall(0x5707AE, CTaskSimpleJetPack__DropJetPack_Hook);
    patch::RedirectCall(0x67E840, CTaskSimpleJetPack__DropJetPack_Hook);

    patch::RedirectCall(0x4B5B27, CPedDamageResponseCalculator__ComputeWillKillPed_Hook);

    patch::RedirectCall(0x4577E6, CPlayerPed__CanPlayerStartMission_Hook);
    patch::RedirectCall(0x4895B0, CPlayerPed__CanPlayerStartMission_Script_Hook); // private build: story starts on the host

    patch::RedirectCall(
        0x60F2E0, CPlayerPed__ProcessWeaponSwitch_Hook);  // disable switching weapon for network players

    patch::RedirectCall(0x6887E2, CTaskSimplePlayerOnFoot__PlayIdleAnimations_Hook);

    patch::RedirectCall(0x688700, CPad__JumpJustDown_Hook);
    patch::RedirectCall(0x6887D8, CTaskComplexJump_CTaskManager__SetTask_Hook);

    patch::ReplaceFunction(0x485A50, CRunningScript__DoDeatharrestCheck_Hook);

    //////////////////// player ducking sync ////////////////////
    patch::RedirectCall(0x6879B1, CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer);
    patch::RedirectCall(0x687F1A, CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer);
    patch::RedirectCall(0x6886B2, CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer);

    patch::RedirectCall(0x688290, CPedIntelligence__ClearTaskDuckSecondary_HookTaskPlayer);

    // dont let the keysync control ducking for network players
    patch::RedirectCall(0x68798B, CPad__DuckJustDown_Hook);
    patch::RedirectCall(0x687EFC, CPad__DuckJustDown_Hook);
    patch::RedirectCall(0x687FFA, CPad__DuckJustDown_Hook);
    patch::RedirectCall(0x688689, CPad__DuckJustDown_Hook);
    //////////////////////////////////////////////////////////////////

    // disable cam shake when a network player shoots the sniper rifle
    patch::RedirectCall(0x73ACE2, CamShakeNoPos_Hook);
}
