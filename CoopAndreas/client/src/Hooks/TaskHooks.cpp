#include "stdafx.h"
#include "TaskHooks.h"
#include "CTaskSync.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include "CAimSync.h"
#include <CKeySync.h>
#include <CReplayEntityPolicy.h>

// when local player enters any vehicle
static void __fastcall CTaskComplexEnterCarAsDriver__Ctor_Hook(
    CTaskComplexEnterCarAsDriver* This, SKIP_EDX, CVehicle* vehicle)
{
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);

    if (networkVehicle == nullptr)
    {
        //CChat::AddMessage("ERROR: this vehicle is not synced");
        plugin::CallMethod<0x6402F0, CTaskComplexEnterCarAsDriver*, CVehicle*>(This, vehicle);
        return;
    }

    Packets::Vehicles::VehicleEnter packet{};
    packet.seatid = 0;
    packet.vehicleid = networkVehicle->m_nVehicleId;
    GetPacketFactory().Send(packet);

    plugin::CallMethod<0x6402F0, CTaskComplexEnterCarAsDriver*, CVehicle*>(This, vehicle);
}

static void __fastcall CTaskComplexLeaveCar__Ctor_Hook(CTaskComplexLeaveCar* This, SKIP_EDX, CVehicle* vehicle,
    int targetDoor, int delayTime, bool sensibleLeaveCar, bool forceGetOut)
{
    // private build (vehicle sync protocol): only for the local player's vehicle (CWorld::Process runs CPlayerInfo::Process
    // for CWorld::Players[1] too, a network player, with pad 1: loop at 0x568A78-0x568A9A); the exits the exit key doesn't
    // start go out from CNetworkVehicleManager::ProcessLocalExit, which this one keeps from sending a second VEHICLE_EXIT
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (CNetwork::m_bAuthenticated && pLocalPlayer != nullptr && pLocalPlayer->m_nPedFlags.bInVehicle &&
        pLocalPlayer->m_pVehicle == vehicle)
    {
        Packets::Vehicles::VehicleExit packet{};
        GetPacketFactory().Send(packet);
        CNetworkVehicleManager::OnKeyExitSent();
    }

    plugin::CallMethod<0x63B8C0, CTaskComplexLeaveCar*, CVehicle*, int, int, bool, bool>(
        This, vehicle, targetDoor, delayTime, sensibleLeaveCar, forceGetOut);
}

// private build: a CTaskComplexLeaveCar without its vehicle (vtable 0x86E828, gta-reversed TaskComplexLeaveCar.cpp; the
// slots below read from gta_sa.exe: 0x86E848 holds CreateNextSubTask 0x6419F0, 0x86E84C CreateFirstSubTask 0x641FC0, which
// CTaskManager::AddSubTasks calls through [vtable+24h] at 0x681A53). Once the ped is in a car (CPed +0x46C bit 0x100)
// CreateFirstSubTask reads m_pTargetVehicle (+0xC) unchecked (0x642064, 0x642067: both other games in Drive-By, a replayed
// Task.LeaveCar built with no vehicle), and so do branches of CreateNextSubTask (0x641B68); ControlSubTask 0x6421B0 and
// MakeAbortable 0x641100 check it. TASK_LEAVE_CAR (05CD, case 0x490554) builds the task with whatever CPools::GetVehicle
// returns, and a vehicle deleted while the task waits in a sequence or runs clears the registered reference. Then the ped
// leaves the car it sits in; a ped on foot keeps the game's "ped not in car" path at the start (0x642020, no vehicle read),
// later the task ends (ReplayEntityPolicy::DecideLeaveCar). Both slots are replaced only while they hold the originals.
static constexpr uintptr_t CTASKCOMPLEXLEAVECAR_VTBL_CREATENEXTSUBTASK = 0x86E848;
static constexpr uintptr_t CTASKCOMPLEXLEAVECAR_VTBL_CREATEFIRSTSUBTASK = 0x86E84C;
static constexpr uintptr_t CTASKCOMPLEXLEAVECAR_CREATENEXTSUBTASK = 0x6419F0;
static constexpr uintptr_t CTASKCOMPLEXLEAVECAR_CREATEFIRSTSUBTASK = 0x641FC0;

// true when the game's own code may run: the task has its vehicle now, or the ped's state needs none
static bool PrepareLeaveCarVehicle(CTaskComplexLeaveCar* pTask, CPed* pPed, bool bFirstSubTask)
{
    const bool bPedInVehicle = pPed->m_nPedFlags.bInVehicle;
    switch (ReplayEntityPolicy::DecideLeaveCar(pTask->m_pTargetVehicle != nullptr, bPedInVehicle, pPed->m_pVehicle != nullptr,
        bFirstSubTask))
    {
        case ReplayEntityPolicy::eLeaveCar::ORIGINAL:
            return true;

        case ReplayEntityPolicy::eLeaveCar::TAKE_PED_VEHICLE:
            // the door and getting-in counts the task set (+0x20, +0x21) belonged to the vehicle that went;
            // ~CTaskComplexLeaveCar (0x63B970) takes them off m_pTargetVehicle
            pTask->m_pTargetVehicle = pPed->m_pVehicle;
            pTask->m_pTargetVehicle->RegisterReference(reinterpret_cast<CEntity**>(&pTask->m_pTargetVehicle));
            pTask->m_nDoorFlagsSet = 0;
            pTask->m_nNumGettingInSet = 0;
            CNetworkPedManager::LogEvent("leave-car-no-vehicle", -1, "subtask=%s action=own-vehicle model=%d",
                bFirstSubTask ? "first" : "next", pPed->m_pVehicle->m_nModelIndex);
            return true;

        case ReplayEntityPolicy::eLeaveCar::NO_SUBTASK:
            break;
    }
    CNetworkPedManager::LogEvent("leave-car-no-vehicle", -1, "subtask=%s action=end in-vehicle=%d",
        bFirstSubTask ? "first" : "next", bPedInVehicle);
    return false;
}

static CTask* __fastcall CTaskComplexLeaveCar__CreateFirstSubTask_Hook(CTaskComplexLeaveCar* This, SKIP_EDX, CPed* pPed)
{
    if (!PrepareLeaveCarVehicle(This, pPed, true))
    {
        return nullptr;  // the task ends: AddSubTasks (0x681A58) asks its parent for the next subtask (0x681920)
    }
    return plugin::CallMethodAndReturn<CTask*, CTASKCOMPLEXLEAVECAR_CREATEFIRSTSUBTASK, CTaskComplexLeaveCar*, CPed*>(This,
        pPed);
}

static CTask* __fastcall CTaskComplexLeaveCar__CreateNextSubTask_Hook(CTaskComplexLeaveCar* This, SKIP_EDX, CPed* pPed)
{
    if (!PrepareLeaveCarVehicle(This, pPed, false))
    {
        return nullptr;  // as the original's TASK_FINISHED (CreateSubTask 0x641530 returns nullptr for it, 0x6419DA)
    }
    return plugin::CallMethodAndReturn<CTask*, CTASKCOMPLEXLEAVECAR_CREATENEXTSUBTASK, CTaskComplexLeaveCar*, CPed*>(This,
        pPed);
}

CTaskManager* pTaskMgr = nullptr;
CTask* pTask = nullptr;
int nTasksId = 0;
CNetworkPed* pNetworkPed = nullptr;
DWORD CTaskManager__SetTask_Hook_Ret = 0x681AF5;
size_t nTaskPacketSize = 0;
void* pSerializedTask = nullptr;
int nTaskSlot = 0;
static void __declspec(naked) CTaskManager__SetTask_Hook()
{
    __asm
    {

        // original code
        push ebx
        mov ebx, [esp + 8]
        // -------------

        mov pTaskMgr, ecx  // get *this

        pushad

                // get pTask
        mov eax, [esp + 10h]
        mov pTask, eax

            // get task slot
        mov eax, [esp + 14h]
        mov nTaskSlot, eax
    }

    if (pTaskMgr && pTask)
    {
        pNetworkPed = CNetworkPedManager::GetPed(pTaskMgr->m_pPed);

        if (pNetworkPed && pNetworkPed->m_bSyncing)
        {
#ifdef TASK_LOG
            CChat::AddMessage("SET PRIMARY %s", CDebugPedTasks::TaskNames[pTask->GetTaskType()]);
#endif
            nTaskPacketSize = 0;
            pSerializedTask = CTaskSync::SerializeTask(pTask, pNetworkPed, true, nTaskSlot, &nTaskPacketSize);

            if (nTaskPacketSize > 0)
            {
                /*CNetwork::SendPacket(
                    CPacketsID::PED_ADD_TASK, pSerializedTask, nTaskPacketSize, ENET_PACKET_FLAG_RELIABLE);*/

            }
        }
    }

    __asm
    {
        popad

        jmp CTaskManager__SetTask_Hook_Ret
    }
}

DWORD CTaskManager__SetTaskSecondary_Hook_Ret = 0x681B65;
static void __declspec(naked) CTaskManager__SetTaskSecondary_Hook()
{
    __asm
    {

        // original code
        push ebx
        mov ebx, [esp + 8]
        // -------------

        mov pTaskMgr, ecx  // get *this

        pushad

                // get pTask
        mov eax, [esp + 10h]
        mov pTask, eax

            // get task slot
        mov eax, [esp + 14h]
        mov nTaskSlot, eax
    }

    if (pTaskMgr && pTask)
    {
        pNetworkPed = CNetworkPedManager::GetPed(pTaskMgr->m_pPed);

        if (pNetworkPed)
        {
#ifdef TASK_LOG
            CChat::AddMessage("SET SECONDARY %s", CDebugPedTasks::TaskNames[pTask->GetTaskType()]);
#endif
            nTaskPacketSize = 0;
            pSerializedTask = CTaskSync::SerializeTask(pTask, pNetworkPed, false, nTaskSlot, &nTaskPacketSize);

            if (nTaskPacketSize > 0)
            {
                /*CNetwork::SendPacket(
                    CPacketsID::PED_ADD_TASK, pSerializedTask, nTaskPacketSize, ENET_PACKET_FLAG_RELIABLE*/
            }
        }
    }

    __asm
    {
        popad

        jmp CTaskManager__SetTaskSecondary_Hook_Ret
    }
}

DWORD CTaskComplex__SetSubTask_Hook_Ret = 0x61A44E;
static void __declspec(naked) CTaskComplex__SetSubTask_Hook()
{
    __asm
    {
        // original code
        test edi, edi
        mov[esi + 8], edi
            // --------------

        pushad
        pushfd

        mov pTask, edi
    }

    if (pTask)
    {
        pNetworkPed = CUtil::GetNetworkPedByTask(pTask);

#ifdef TASK_LOG
        if (pNetworkPed)
        {
            CChat::AddMessage("SET SUB %s", CDebugPedTasks::TaskNames[pTask->GetTaskType()]);
        }
#endif
    }

    __asm
    {
        popfd
        popad

        jmp CTaskComplex__SetSubTask_Hook_Ret
    }
}

CTaskSimple* pTaskSimple = nullptr;
DWORD CTaskSimple__dtor_Hook_Ret = 0x61A3A6;
DWORD CTask_vtable = 0x86D48C;
void** vtable;
DWORD dwGetIdAddr = 0;
eTaskType nTaskType{};
static void __declspec(naked) CTaskSimple__dtor_Hook()
{
    __asm
    {
        pushad
        mov pTaskSimple, ecx
    }

    if (pTaskSimple)
    {
        vtable = *(void***)pTaskSimple;                    // get vtable

        dwGetIdAddr = patch::GetUInt((DWORD)vtable + 16);  // get `GetId`

        if (dwGetIdAddr == 0x82263A)                       // if virtual function is not implemented (purecall)
        {
            goto skip;
        }

        pNetworkPed = CUtil::GetNetworkPedByTask(pTaskSimple);

        if (pNetworkPed && pNetworkPed->m_bSyncing)
        {
            nTaskType = plugin::CallMethodAndReturnDyn<eTaskType>(dwGetIdAddr, pTaskSimple);
#ifdef TASK_LOG
            CChat::AddMessage("DESTROY TASK %s", CDebugPedTasks::TaskNames[nTaskType]);
#endif  // TASK_LOG
        }
    }

skip:
    __asm
    {
        popad
        mov dword ptr[ecx], offset CTask_vtable;
        jmp CTaskSimple__dtor_Hook_Ret
    }
}

CTaskComplex* pTaskComplex = nullptr;
DWORD CTaskComplex__dtor_Hook_Ret = 0x61A419;
static void __declspec(naked) CTaskComplex__dtor_Hook()
{
    __asm
    {
        pushad
        mov pTaskComplex, esi
    }

    if (pTaskComplex)
    {
        vtable = *(void***)pTaskComplex;                   // get vtable

        dwGetIdAddr = patch::GetUInt((DWORD)vtable + 16);  // get `GetId`

        if (dwGetIdAddr == 0x82263A)                       // if virtual function is not implemented (purecall)
        {
            goto skip;
        }

        pNetworkPed = CUtil::GetNetworkPedByTask(pTaskComplex);

        if (pNetworkPed && pNetworkPed->m_bSyncing)
        {
            nTaskType = plugin::CallMethodAndReturnDyn<eTaskType>(dwGetIdAddr, pTaskComplex);
#ifdef TASK_LOG
            CChat::AddMessage("DESTROY TASK %s", CDebugPedTasks::TaskNames[nTaskType]);
#endif
        }
    }

skip:
    __asm
    {
        popad
        mov dword ptr[esi], offset CTask_vtable;
        jmp CTaskComplex__dtor_Hook_Ret
    }
}

bool __fastcall CTaskSimpleUseGun__SetPedPosition_Hook(CTaskSimpleUseGun* This, SKIP_EDX, CPed* pPed)
{
    if (!pPed->IsPlayer() || pPed == FindPlayerPed(0))
    {
        return plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);
    }

    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed);
    if (pNetworkPlayer == nullptr)
    {
        return plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);
    }

    int playerNum = pNetworkPlayer->GetInternalId();
    if (playerNum == -1)
    {
        return plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);
    }

    CWorld::PlayerInFocus = playerNum;
    CKeySync::ApplyNetworkPlayerContext(pNetworkPlayer);
    CAimSync::ApplyNetworkPlayerContext(pNetworkPlayer);

    bool result = plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);

    CWorld::PlayerInFocus = 0;
    CKeySync::ApplyLocalContext();
    CAimSync::ApplyLocalContext();

    return result;
}

void TaskHooks::InjectHooks()
{
    patch::RedirectCall(0x570A1B, CTaskComplexEnterCarAsDriver__Ctor_Hook);
    patch::RedirectCall(0x570A94, CTaskComplexEnterCarAsDriver__Ctor_Hook);

    patch::RedirectCall(0x57049C, CTaskComplexLeaveCar__Ctor_Hook);
    patch::RedirectCall(0x5703F7, CTaskComplexLeaveCar__Ctor_Hook);

    // private build: CTaskComplexLeaveCar without its vehicle, checked before each slot is replaced
    if (*reinterpret_cast<const uintptr_t*>(CTASKCOMPLEXLEAVECAR_VTBL_CREATEFIRSTSUBTASK) ==
        CTASKCOMPLEXLEAVECAR_CREATEFIRSTSUBTASK)
    {
        patch::SetPointer(CTASKCOMPLEXLEAVECAR_VTBL_CREATEFIRSTSUBTASK, CTaskComplexLeaveCar__CreateFirstSubTask_Hook);
    }
    else
    {
        logger::warn("[TaskHooks] unexpected CTaskComplexLeaveCar::CreateFirstSubTask at 0x%X, a leave-car task without its "
            "vehicle isn't caught", CTASKCOMPLEXLEAVECAR_VTBL_CREATEFIRSTSUBTASK);
    }
    if (*reinterpret_cast<const uintptr_t*>(CTASKCOMPLEXLEAVECAR_VTBL_CREATENEXTSUBTASK) ==
        CTASKCOMPLEXLEAVECAR_CREATENEXTSUBTASK)
    {
        patch::SetPointer(CTASKCOMPLEXLEAVECAR_VTBL_CREATENEXTSUBTASK, CTaskComplexLeaveCar__CreateNextSubTask_Hook);
    }
    else
    {
        logger::warn("[TaskHooks] unexpected CTaskComplexLeaveCar::CreateNextSubTask at 0x%X, a leave-car task without its "
            "vehicle isn't caught", CTASKCOMPLEXLEAVECAR_VTBL_CREATENEXTSUBTASK);
    }

    // patch::RedirectCall(0x62D59B, CTaskSimpleGangDriveBy__SetupStaticAnimForPlayer_Hook);

    // patch::RedirectCall(0x62D813, CTaskSimpleGangDriveBy__ProcessAiming_Hook);

    // ped tasks hooks (help me im going crazy) usused, maybe it will be useful in future?
    /* patch::RedirectJump(0x681AF0, CTaskManager__SetTask_Hook);
     patch::RedirectJump(0x681B60, CTaskManager__SetTaskSecondary_Hook);
     patch::RedirectJump(0x61A449, CTaskComplex__SetSubTask_Hook);
     patch::RedirectJump(0x61A3A0, CTaskSimple__dtor_Hook);
     patch::RedirectJump(0x61A413, CTaskComplex__dtor_Hook);*/

    patch::SetPointer(0x86D744, CTaskSimpleUseGun__SetPedPosition_Hook);
}
