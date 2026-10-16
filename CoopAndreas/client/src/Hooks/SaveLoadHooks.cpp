#include "stdafx.h"
#include "SaveLoadHooks.h"
#include "CGenericGameStorage.h"
#include "../CPlayerStateSave.h"


char* __fastcall CSimpleVariablesSaveStructure__Construct_CText__Get_Hook(CText* This, SKIP_EDX, char* key)
{
    const char* text = TheText.Get(key);
    std::string modifiedText = "(Coop) " + std::string(text);
    return _strdup(modifiedText.c_str());
}

// in the original implementation, R* write a ped if their type 
// is PED_TYPE_PLAYER1, network players are also PED_TYPE_PLAYER1, 
// so their data gets saved, which causes critical bugs when loading 
// the save again
bool CPools__SavePedPool_Reimpl()
{
    int playerCount = 1;
    CGenericGameStorage::_SaveDataToWorkBuffer(&playerCount, 4);

    CPlayerPed* ped = FindPlayerPed(0);
    int pedRef = CPools::GetPedRef(ped);
    int modelIndex = ped->m_nModelIndex;
    int pedType = ped->m_nPedType;

    CGenericGameStorage::_SaveDataToWorkBuffer(&pedRef,  4);
    CGenericGameStorage::_SaveDataToWorkBuffer(&modelIndex, 4);
    CGenericGameStorage::_SaveDataToWorkBuffer(&pedType, 4);
    plugin::CallMethod<0x5D57E0>(ped); // CPlayerPed::Save

    return true;
}

// private build: player state for co-op saves (CPlayerStateSave.h). C_PcSave::SaveSlot (0x619060, thiscall on
// s_PcSaveHelper 0xC17034) builds the file name with CGenericGameStorage::MakeValidSaveName (0x5D0E90) into
// ms_ValidSaveName (0xC16DB8, the name OpenFileForWriting 0x5D0DD0 opens), runs GenericSave (0x5D13E0) and returns 0
// when it succeeded, 2 when not (0x619089). Its only caller is CMenuManager::ProcessFileActions, call at 0x578DFA.
static uint32_t __fastcall C_PcSave__SaveSlot_Hook(void* This, SKIP_EDX, int slot)
{
    uint32_t result = plugin::CallMethodAndReturn<uint32_t, 0x619060, void*, int>(This, slot);
    if (result == 0)
    {
        CPlayerStateSave::OnGameSaved(CGenericGameStorage::ms_ValidSaveName);
    }
    return result;
}

// private build: player state for co-op saves. CGenericGameStorage::GenericLoad(bool*) (0x5D17B0, cdecl) reads the file
// named by ms_LoadFileName (0xC15FC8, OpenFileForReading 0x5D0D20) and returns true when it loaded. Its only caller is
// CGame::InitialiseWhenRestarting (0x53C680), call at 0x53C70B, for a load from the start menu and from the pause menu.
static bool __cdecl CGenericGameStorage__GenericLoad_Hook(bool* pbVariablesLoaded)
{
    bool bLoaded = plugin::CallAndReturn<bool, 0x5D17B0, bool*>(pbVariablesLoaded);
    if (bLoaded)
    {
        CPlayerStateSave::OnGameLoaded(CGenericGameStorage::ms_LoadFileName);
    }
    return bLoaded;
}

void SaveLoadHooks::InjectHooks()
{
    patch::RedirectCall(0x5D1BB2, CSimpleVariablesSaveStructure__Construct_CText__Get_Hook); // add [Coop] prefix to save display name

    patch::RedirectJump(0x5D4B40, CPools__SavePedPool_Reimpl);

    patch::RedirectCall(0x578DFA, C_PcSave__SaveSlot_Hook); // private build: player state next to the save
    patch::RedirectCall(0x53C70B, CGenericGameStorage__GenericLoad_Hook); // private build: player state from the save
}
