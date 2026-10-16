#include "stdafx.h"
#include "CPlayerStateSave.h"
#include "CPlayerStateStore.h"
#include "CSessionLifecycle.h"
#include <CGenericGameStorage.h>
#include <CTimer.h>
#include <CWeaponInfo.h>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <io.h>

// Private build: player state for co-op saves, see CPlayerStateSave.h.
//
// gta_sa.exe 1.0 US layouts used below (plugin-sdk names, offsets checked in the exe):
// - CWorld::Players[0] (0xB7CD98, 0x190 per player) is this game's CPlayerInfo: m_nMoney +0xB8 (ADD_SCORE 0109 handler
//   0x469922), m_nDisplayMoney +0xBC, m_bDoesNotGetTired +0x14C (SET_PLAYER_NEVER_GETS_TIRED 0330, 0x48175F),
//   m_bFastReload +0x14D (SET_PLAYER_FAST_RELOAD 0331, 0x48178E), m_bFireProof +0x14E (MAKE_PLAYER_FIRE_PROOF 055D,
//   0x48D6A7), m_nMaxHealth +0x14F and m_nMaxArmour +0x150 (INCREASE_PLAYER_MAX_HEALTH/ARMOUR 055E/055F, 0x48D6DD /
//   0x48D729), m_bGetOutOfJailFree +0x151, m_bGetOutOfHospitalFree +0x152 (SET_FREE_HEALTH_CARE 0414, 0x4898F0). These
//   are the fields CPlayerInfo::Save keeps in a single-player save (gta-reversed CPlayerInfoSaveStructure), without the
//   collectables count, the busted audio state and m_bCanDoDriveBy, which the story sets.
// - CPed: m_fHealth +0x540, m_fArmour +0x548, m_aWeapons +0x5A0 (13 x 0x1C), m_nActiveWeaponSlot +0x718,
//   m_nFightingStyle +0x72D, m_nAllowedAttackMoves +0x72E (CPedSaveStructure::Construct / Extract 0x5D43D0 / 0x5D44B0);
//   m_pPlayerData +0x480, its m_pPedClothesDesc +4 (CPlayerPed::Save 0x5D57E0).
// - CWeaponInfo: m_nModelId1 +0xC, m_nModelId2 +0x10 (Extract), m_nSlot +0x14 (CPed::GiveWeapon 0x5E6080), m_nAmmoClip
//   +0x20 (CWeapon::Reload 0x73AEB0).
// - stats: float ids below 0x52 at 0xB79380 + id * 4, int ids at 0xB78E20 + id * 4 (CStats::GetStatValue 0x558E40) =
//   CStats::StatTypesFloat / CStats::StatTypesInt[id - 120]. The respect components kept (SPlayerState) are the
//   operands CStats::UpdateRespectStat (0x55BC50) reads: respect 68 at 0xB79490 (0x55BC73), clothes respect 66 at
//   0xB79488 (0x55BD39), mission respect 224 at 0xB791A0 (0x55BC7F). It writes the total (64, 0xB79480 at 0x55BDDA)
//   and runs at the start of CGameLogic::Update (call at 0x442ADA) in every frame the game isn't paused.
// The free-play rule (menu, fade, cutscene, controls, missions of this game) is CSessionLifecycle::IsInFreePlay.
static_assert(offsetof(CPlayerInfo, m_nMoney) == 0xB8, "CPlayerInfo::m_nMoney");
static_assert(offsetof(CPlayerInfo, m_nDisplayMoney) == 0xBC, "CPlayerInfo::m_nDisplayMoney");
static_assert(offsetof(CPlayerInfo, m_bDoesNotGetTired) == 0x14C, "CPlayerInfo::m_bDoesNotGetTired");
static_assert(offsetof(CPlayerInfo, m_bFastReload) == 0x14D, "CPlayerInfo::m_bFastReload");
static_assert(offsetof(CPlayerInfo, m_bFireProof) == 0x14E, "CPlayerInfo::m_bFireProof");
static_assert(offsetof(CPlayerInfo, m_nMaxHealth) == 0x14F, "CPlayerInfo::m_nMaxHealth");
static_assert(offsetof(CPlayerInfo, m_nMaxArmour) == 0x150, "CPlayerInfo::m_nMaxArmour");
static_assert(offsetof(CPlayerInfo, m_bGetOutOfJailFree) == 0x151, "CPlayerInfo::m_bGetOutOfJailFree");
static_assert(offsetof(CPlayerInfo, m_bGetOutOfHospitalFree) == 0x152, "CPlayerInfo::m_bGetOutOfHospitalFree");
static_assert(offsetof(CPed, m_pPlayerData) == 0x480, "CPed::m_pPlayerData");
static_assert(offsetof(CPed, m_fHealth) == 0x540, "CPed::m_fHealth");
static_assert(offsetof(CPed, m_fArmour) == 0x548, "CPed::m_fArmour");
static_assert(offsetof(CPed, m_aWeapons) == 0x5A0, "CPed::m_aWeapons");
static_assert(offsetof(CPed, m_nActiveWeaponSlot) == 0x718, "CPed::m_nActiveWeaponSlot");
static_assert(offsetof(CPed, m_nFightingStyle) == 0x72D, "CPed::m_nFightingStyle");
static_assert(offsetof(CPed, m_nAllowedAttackMoves) == 0x72E, "CPed::m_nAllowedAttackMoves");
static_assert(offsetof(CPlayerData, m_pPedClothesDesc) == 4, "CPlayerData::m_pPedClothesDesc");
static_assert(offsetof(CWeaponInfo, m_nModelId1) == 0xC, "CWeaponInfo::m_nModelId1");
static_assert(offsetof(CWeaponInfo, m_nModelId2) == 0x10, "CWeaponInfo::m_nModelId2");
static_assert(offsetof(CWeaponInfo, m_nSlot) == 0x14, "CWeaponInfo::m_nSlot");
static_assert(offsetof(CWeaponInfo, m_nAmmoClip) == 0x20, "CWeaponInfo::m_nAmmoClip");
static_assert(sizeof(CPedClothesDesc::m_anModelKeys) == sizeof(SPlayerState::clothesModelKeys), "clothes models");
static_assert(sizeof(CPedClothesDesc::m_anTextureKeys) == sizeof(SPlayerState::clothesTextureKeys), "clothes textures");

namespace
{
// how long this game has to stay settled (see IsSettled) before a snapshot or a restore
constexpr uint32_t SETTLED_FOR_MS = 1000;
constexpr int FIRST_INT_STAT = 120;  // CStats::StatTypesInt[0]
constexpr int KEEP_IN_MEMORY = 8;    // the flag CPedSaveStructure::Extract requests weapon models with (0x5D4559)

uint32_t MixBits(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

uint32_t NewConnectionNonce()
{
    static uint32_t ms_nCounter = 0;
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const uint32_t value = MixBits(static_cast<uint32_t>(counter.QuadPart) ^
                                   static_cast<uint32_t>(counter.QuadPart >> 32) ^ (GetCurrentProcessId() << 16) ^
                                   ++ms_nCounter);
    return value != 0 ? value : 1;
}

CPlayerStateStore ms_store{MixBits(GetTickCount() ^ GetCurrentProcessId())};
CPlayerStateClient ms_client;

bool ms_bWasAuthenticated = false;
bool ms_bLoadedSave = false;          // set by OnGameLoaded
uint32_t ms_nLastFrameCounter = 0;
bool ms_bSettled = false;
uint32_t ms_nSettledSince = 0;
int ms_nLastHostId = -2;
std::vector<std::string> ms_chatLines;  // from the save and load hooks, shown once the game runs again

void AddChatLineLater(const char* format, ...)
{
    char buffer[Config::MAX_CHAT_MESSAGE_LENGTH + 1];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    ms_chatLines.emplace_back(buffer);
}

CNetworkPlayer* FindNetworkPlayer(const std::string& name)
{
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != nullptr && name == pNetworkPlayer->m_Name)
        {
            return pNetworkPlayer;
        }
    }
    return nullptr;
}

int GetHostId()
{
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != nullptr && pNetworkPlayer->m_bIsHost)
        {
            return pNetworkPlayer->m_iPlayerId;
        }
    }
    return -1;
}

// This game is in plain free play: alive, no menu, no fade, no cutscene, controls on, and no mission of its own running
// (on this side only the new-game missions INITIAL, INITIL2, INTRO and the like can run). The rule lives in
// CSessionLifecycle::IsInFreePlay, which the other modules share.
bool IsSettledNow(CPlayerPed* pPed) { return CSessionLifecycle::IsInFreePlay(pPed); }

bool UpdateSettled(CPlayerPed* pPed)
{
    if (!IsSettledNow(pPed))
    {
        ms_bSettled = false;
        return false;
    }
    const uint32_t now = GetTickCount();
    if (!ms_bSettled)
    {
        ms_bSettled = true;
        ms_nSettledSince = now;
    }
    return now - ms_nSettledSince >= SETTLED_FOR_MS;
}

// standing on foot: CClothes::RebuildPlayer replaces the clump, which a vehicle seat, a jetpack or a fall must not see
bool IsOnFoot(CPlayerPed* pPed)
{
    if (pPed->m_nPedFlags.bInVehicle || !pPed->m_nPedFlags.bIsStanding || CUtil::IsPedHasJetpack(pPed))
    {
        return false;
    }
    switch (pPed->m_ePedState)
    {
        case PEDSTATE_DRIVING:
        case PEDSTATE_PASSENGER:
        case PEDSTATE_TAXI_PASSENGER:
        case PEDSTATE_CARJACK:
        case PEDSTATE_DRAGGED_FROM_CAR:
        case PEDSTATE_ENTER_CAR:
        case PEDSTATE_STEAL_CAR:
        case PEDSTATE_EXIT_CAR:
        case PEDSTATE_ENTER_TRAIN:
        case PEDSTATE_EXIT_TRAIN:
        case PEDSTATE_ABSEIL_FROM_HELI:
            return false;
        default:
            return true;
    }
}

float ClampFinite(float value, float min, float max, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, min, max) : fallback;
}

bool CaptureState(CPlayerPed* pPed, SPlayerState& state)
{
    state = SPlayerState{};

    for (int slot = 0; slot < SPlayerState::NUM_WEAPON_SLOTS; slot++)
    {
        const CWeapon& weapon = pPed->m_aWeapons[slot];
        const int type = static_cast<int>(weapon.m_eWeaponType);
        if (!SPlayerState::IsGivableWeaponType(type))
        {
            continue;
        }
        const CWeaponInfo* pInfo = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(type), 1);
        if (pInfo == nullptr || pInfo->m_nSlot != static_cast<unsigned int>(slot))
        {
            continue;
        }
        const uint32_t total = std::min<uint32_t>(weapon.m_nTotalAmmo, SPlayerState::MAX_AMMO);
        state.weapons[slot].type = static_cast<uint8_t>(type);
        state.weapons[slot].totalAmmo = total;
        state.weapons[slot].ammoInClip = std::min<uint32_t>(weapon.m_nAmmoInClip, total);
    }
    state.activeSlot = static_cast<uint8_t>(
        pPed->m_nActiveWeaponSlot < SPlayerState::NUM_WEAPON_SLOTS ? pPed->m_nActiveWeaponSlot : 0);

    state.health = ClampFinite(pPed->m_fHealth, 0.0f, SPlayerState::MAX_HEALTH_OR_ARMOUR, 100.0f);
    state.armour = ClampFinite(pPed->m_fArmour, 0.0f, SPlayerState::MAX_HEALTH_OR_ARMOUR, 0.0f);

    const int fightingStyle = static_cast<uint8_t>(pPed->m_nFightingStyle);
    state.fightingStyle = static_cast<uint8_t>(
        fightingStyle >= SPlayerState::FIGHTING_STYLE_FIRST && fightingStyle <= SPlayerState::FIGHTING_STYLE_LAST
            ? fightingStyle
            : SPlayerState::FIGHTING_STYLE_FIRST);
    state.allowedAttackMoves = static_cast<uint8_t>(pPed->m_nAllowedAttackMoves);

    const CPlayerInfo& info = CWorld::Players[0];
    state.money = std::clamp(info.m_nMoney, -SPlayerState::MAX_MONEY, SPlayerState::MAX_MONEY);
    state.maxHealth = info.m_nMaxHealth != 0 ? info.m_nMaxHealth : 100;
    state.maxArmour = info.m_nMaxArmour;
    state.neverGetsTired = info.m_bDoesNotGetTired;
    state.fastReload = info.m_bFastReload;
    state.fireProof = info.m_bFireProof;
    state.getOutOfJailFree = info.m_bGetOutOfJailFree;
    state.freeHealthCare = info.m_bGetOutOfHospitalFree;

    for (int i = 0; i < SPlayerState::NUM_FLOAT_STATS; i++)
    {
        state.floatStats[i] = ClampFinite(
            CStats::StatTypesFloat[SPlayerState::FLOAT_STAT_IDS[i]], 0.0f, SPlayerState::MAX_STAT_VALUE, 0.0f);
    }
    for (int i = 0; i < SPlayerState::NUM_INT_STATS; i++)
    {
        state.intStats[i] = std::clamp(CStats::StatTypesInt[SPlayerState::INT_STAT_IDS[i] - FIRST_INT_STAT], 0,
            static_cast<int>(SPlayerState::MAX_STAT_VALUE));
    }

    // the respect components this player earns; the game computes the total from them
    state.hasRespect = true;
    state.respect =
        ClampFinite(CStats::StatTypesFloat[SPlayerState::STAT_ID_RESPECT], 0.0f, SPlayerState::MAX_RESPECT, 0.0f);
    state.clothesRespect = ClampFinite(
        CStats::StatTypesFloat[SPlayerState::STAT_ID_CLOTHES_RESPECT], 0.0f, SPlayerState::MAX_STAT_VALUE, 0.0f);
    state.missionRespect = std::clamp(CStats::StatTypesInt[SPlayerState::STAT_ID_MISSION_RESPECT - FIRST_INT_STAT], 0,
        SPlayerState::MAX_MISSION_RESPECT);

    const CPedClothesDesc* pClothes = pPed->m_pPlayerData->m_pPedClothesDesc;
    memcpy(state.clothesModelKeys, pClothes->m_anModelKeys, sizeof(state.clothesModelKeys));
    memcpy(state.clothesTextureKeys, pClothes->m_anTextureKeys, sizeof(state.clothesTextureKeys));

    return state.IsValid();
}

bool AreWeaponModelsLoaded(const CWeaponInfo* pInfo)
{
    if (pInfo == nullptr)
    {
        return false;
    }
    for (int model : {pInfo->m_nModelId1, pInfo->m_nModelId2})
    {
        if (model >= 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
        {
            return false;
        }
    }
    return true;
}

// As CPedSaveStructure::Extract (0x5D44B0) gives a loaded ped its weapons: request each weapon's models, load them,
// CPed::GiveWeapon with the total, then the ammo in the clip and the current slot.
void ApplyWeapons(const SPlayerState& state)
{
    bool aGive[SPlayerState::NUM_WEAPON_SLOTS]{};
    for (int slot = 0; slot < SPlayerState::NUM_WEAPON_SLOTS; slot++)
    {
        const SPlayerState::SWeapon& weapon = state.weapons[slot];
        if (weapon.type == 0)
        {
            continue;
        }
        const CWeaponInfo* pInfo = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(weapon.type), 1);
        if (pInfo == nullptr || pInfo->m_nSlot != static_cast<unsigned int>(slot))
        {
            logger::warn("CPlayerStateSave: weapon %d doesn't belong in slot %d, skipped", weapon.type, slot);
            continue;
        }
        for (int model : {pInfo->m_nModelId1, pInfo->m_nModelId2})
        {
            if (model >= 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
            {
                CStreaming::RequestModel(model, KEEP_IN_MEMORY);
            }
        }
        aGive[slot] = true;
    }
    CStreaming::LoadAllRequestedModels(false);

    // streaming ran: take the player and its clump again
    CPlayerPed* pPed = FindPlayerPed(0);
    if (pPed == nullptr || pPed->m_pRwClump == nullptr)
    {
        logger::warn("CPlayerStateSave: the player went away while the weapon models loaded");
        return;
    }

    pPed->ClearWeapons();
    int activeSlot = 0;
    for (int slot = 0; slot < SPlayerState::NUM_WEAPON_SLOTS; slot++)
    {
        if (!aGive[slot])
        {
            continue;
        }
        const SPlayerState::SWeapon& weapon = state.weapons[slot];
        const eWeaponType type = static_cast<eWeaponType>(weapon.type);
        if (!AreWeaponModelsLoaded(CWeaponInfo::GetWeaponInfo(type, 1)))
        {
            logger::warn("CPlayerStateSave: the models of weapon %d didn't load, skipped", weapon.type);
            continue;
        }
        pPed->GiveWeapon(type, weapon.totalAmmo, false);

        CWeapon& given = pPed->m_aWeapons[slot];
        if (given.m_eWeaponType != type)
        {
            continue;
        }
        uint32_t clip = std::min<uint32_t>(weapon.ammoInClip, given.m_nTotalAmmo);
        const CWeaponInfo* pSkillInfo =
            CWeaponInfo::GetWeaponInfo(type, static_cast<unsigned char>(pPed->GetWeaponSkill(type)));
        if (pSkillInfo != nullptr && pSkillInfo->m_nAmmoClip > 0)
        {
            clip = std::min<uint32_t>(clip, pSkillInfo->m_nAmmoClip);
        }
        given.m_nAmmoInClip = clip;
        if (slot == state.activeSlot)
        {
            activeSlot = slot;
        }
    }
    pPed->SetCurrentWeapon(activeSlot);
}

// Stats first: the rebuild takes fat and muscle from them and the clip sizes depend on the weapon skills. The stats go
// straight into the tables: CStats::SetStatValue (0x55A070) keeps only 16 bits of an int stat and pops stat messages.
void ApplyState(const SPlayerState& state)
{
    for (int i = 0; i < SPlayerState::NUM_FLOAT_STATS; i++)
    {
        CStats::StatTypesFloat[SPlayerState::FLOAT_STAT_IDS[i]] = state.floatStats[i];
    }
    for (int i = 0; i < SPlayerState::NUM_INT_STATS; i++)
    {
        CStats::StatTypesInt[SPlayerState::INT_STAT_IDS[i] - FIRST_INT_STAT] = state.intStats[i];
    }
    // The total respect (stat 64) follows in the next frame: CGameLogic::Update recomputes it from these, the money and
    // the muscle. A state from a CAPS v1 file has no respect: this player's stays as it is.
    if (state.hasRespect)
    {
        CStats::StatTypesFloat[SPlayerState::STAT_ID_RESPECT] = state.respect;
        CStats::StatTypesFloat[SPlayerState::STAT_ID_CLOTHES_RESPECT] = state.clothesRespect;
        CStats::StatTypesInt[SPlayerState::STAT_ID_MISSION_RESPECT - FIRST_INT_STAT] = state.missionRespect;
    }

    CPlayerInfo& info = CWorld::Players[0];
    info.m_nMoney = state.money;
    info.m_nDisplayMoney = state.money;
    info.m_nMaxHealth = state.maxHealth;
    info.m_nMaxArmour = state.maxArmour;
    info.m_bDoesNotGetTired = state.neverGetsTired;
    info.m_bFastReload = state.fastReload;
    info.m_bFireProof = state.fireProof;
    info.m_bGetOutOfJailFree = state.getOutOfJailFree;
    info.m_bGetOutOfHospitalFree = state.freeHealthCare;

    // the rebuild sends REBUILD_PLAYER to the others through the Dress hook (StatsHooks.cpp)
    CPlayerPed* pPed = FindPlayerPed(0);
    CPedClothesDesc* pClothes = pPed->m_pPlayerData->m_pPedClothesDesc;
    memcpy(pClothes->m_anModelKeys, state.clothesModelKeys, sizeof(pClothes->m_anModelKeys));
    memcpy(pClothes->m_anTextureKeys, state.clothesTextureKeys, sizeof(pClothes->m_anTextureKeys));
    CClothes::RebuildPlayer(pPed, false);

    pPed = FindPlayerPed(0);
    if (pPed != nullptr && pPed->m_pRwClump != nullptr)
    {
        ApplyWeapons(state);
    }
    else
    {
        logger::warn("CPlayerStateSave: no player clump after the rebuild, weapons skipped");
    }

    pPed = FindPlayerPed(0);
    if (pPed != nullptr)
    {
        pPed->m_fHealth = state.health;
        pPed->m_fArmour = state.armour;
        pPed->m_nFightingStyle = static_cast<char>(state.fightingStyle);
        pPed->m_nAllowedAttackMoves = static_cast<char>(state.allowedAttackMoves);
    }

    // the others see the weapon skills
    CStatsSync::NotifyChanged();
}

void SendRestore(CNetworkPlayer* pNetworkPlayer, const CPlayerStateStore::SRestore& restore)
{
    Packets::PlayerState::PlayerStateRestore packet{};
    packet.targetPlayerId = pNetworkPlayer->m_iPlayerId;
    strncpy_s(packet.targetName, pNetworkPlayer->m_Name, _TRUNCATE);
    packet.restoreId = restore.restoreId;
    packet.state = restore.state;
    GetPacketFactory().Send(packet);

    CChat::AddMessage("{cecedb}[Co-op] Giving %s back their saved player state.", pNetworkPlayer->m_Name);
}

void ProcessHost()
{
    const auto restores =
        ms_store.TakeLoadRestores([](const std::string& name) { return FindNetworkPlayer(name) != nullptr; });
    for (const CPlayerStateStore::SRestore& restore : restores)
    {
        if (CNetworkPlayer* pNetworkPlayer = FindNetworkPlayer(restore.name))
        {
            SendRestore(pNetworkPlayer, restore);
        }
    }
}

void ProcessOtherPlayer()
{
    // a different host (or the first one) gets the current state at once
    const int hostId = GetHostId();
    if (hostId != ms_nLastHostId)
    {
        ms_nLastHostId = hostId;
        ms_client.ForceSend();
    }

    CPlayerPed* pPed = FindPlayerPed(0);
    const bool bSettled = UpdateSettled(pPed);

    if (ms_client.HasPendingRestore())
    {
        if (bSettled && IsOnFoot(pPed))
        {
            const SPlayerState& restore = ms_client.GetPendingRestore();
            ApplyState(restore);
            // the respect values let the players check the restore: a new game has none
            if (restore.hasRespect)
            {
                CChat::AddMessage(
                    "{cecedb}[Co-op] Your saved player state is back (mission respect %d, respect %.1f, clothes %.0f).",
                    restore.missionRespect, restore.respect, restore.clothesRespect);
            }
            else
            {
                CChat::AddMessage("{cecedb}[Co-op] Your saved player state is back (no respect in this older save).");
            }
            ms_client.OnRestoreDone();
        }
        return;
    }

    const uint32_t now = GetTickCount();
    if (!bSettled || !ms_client.IsCaptureDue(now))
    {
        return;
    }
    SPlayerState state;
    if (!CaptureState(pPed, state) || !ms_client.OnCaptured(now, state))
    {
        return;
    }
    Packets::PlayerState::PlayerStateSnapshot packet{};
    packet.connectionNonce = ms_client.GetConnectionNonce();
    packet.ackRestoreId = ms_client.GetAckRestoreId();
    packet.state = state;
    GetPacketFactory().Send(packet);
}

std::string GetSidecarName(const char* saveFileName) { return std::string(saveFileName) + ".coop"; }

bool HashFile(const char* fileName, uint64_t& hash, uint32_t& size)
{
    FILE* pFile = nullptr;
    if (fopen_s(&pFile, fileName, "rb") != 0 || pFile == nullptr)
    {
        return false;
    }
    std::vector<uint8_t> buffer(64 * 1024);
    uint64_t total = 0;
    hash = Fnv1a64(nullptr, 0);
    size_t read = 0;
    while ((read = fread(buffer.data(), 1, buffer.size(), pFile)) > 0)
    {
        hash = Fnv1a64(buffer.data(), read, hash);
        total += read;
    }
    const bool bOk = ferror(pFile) == 0 && total <= UINT32_MAX;
    fclose(pFile);
    size = static_cast<uint32_t>(total);
    return bOk;
}

enum class eReadFile
{
    Ok,
    Missing,
    Failed
};

eReadFile ReadSmallFile(const std::string& fileName, std::vector<uint8_t>& data)
{
    data.clear();
    FILE* pFile = nullptr;
    const errno_t error = fopen_s(&pFile, fileName.c_str(), "rb");
    if (error == ENOENT)
    {
        return eReadFile::Missing;
    }
    if (error != 0 || pFile == nullptr)
    {
        return eReadFile::Failed;
    }
    bool bOk = fseek(pFile, 0, SEEK_END) == 0;
    const long size = bOk ? ftell(pFile) : -1;
    bOk = bOk && size >= 0 && static_cast<size_t>(size) <= PlayerStateSidecar::MAX_FILE_SIZE &&
          fseek(pFile, 0, SEEK_SET) == 0;
    if (bOk)
    {
        data.resize(static_cast<size_t>(size));
        bOk = data.empty() || fread(data.data(), 1, data.size(), pFile) == data.size();
    }
    fclose(pFile);
    return bOk ? eReadFile::Ok : eReadFile::Failed;
}

// temp file, flushed to disk, then renamed over the old one
bool WriteFileAtomically(const std::string& fileName, const std::vector<uint8_t>& data)
{
    const std::string tempName = fileName + ".tmp";
    FILE* pFile = nullptr;
    if (fopen_s(&pFile, tempName.c_str(), "wb") != 0 || pFile == nullptr)
    {
        return false;
    }
    bool bOk = fwrite(data.data(), 1, data.size(), pFile) == data.size();
    bOk = fflush(pFile) == 0 && bOk;
    bOk = _commit(_fileno(pFile)) == 0 && bOk;
    bOk = fclose(pFile) == 0 && bOk;
    bOk = bOk &&
          MoveFileExA(tempName.c_str(), fileName.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!bOk)
    {
        DeleteFileA(tempName.c_str());
    }
    return bOk;
}

bool RemoveFileIfPresent(const std::string& fileName)
{
    if (DeleteFileA(fileName.c_str()))
    {
        return true;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
}  // namespace

void CPlayerStateSave::Process()
{
    for (const std::string& line : ms_chatLines)
    {
        CChat::AddMessage(line);
    }
    ms_chatLines.clear();

    // CTimer::Initialise (0x5617E0) zeroes the frame counter (0xB7CB4C) whenever the game starts over: a new game or a
    // load from the menu, also while connected
    const uint32_t frameCounter = CTimer::m_FrameCounter;
    const bool bRestarted = ms_bLoadedSave || frameCounter < ms_nLastFrameCounter;
    ms_nLastFrameCounter = frameCounter;
    ms_bLoadedSave = false;
    if (bRestarted)
    {
        ms_bSettled = false;
    }

    if (!CNetwork::m_bConnected || !CNetwork::m_bAuthenticated)
    {
        ms_bWasAuthenticated = false;
        return;
    }
    if (!ms_bWasAuthenticated || bRestarted)
    {
        // the host answers the next snapshot from a new connection with the stored state, if it has one
        ms_bWasAuthenticated = true;
        ms_client.Reset(NewConnectionNonce());
    }

    if (CLocalPlayer::m_bIsHost)
    {
        ProcessHost();
    }
    else
    {
        ProcessOtherPlayer();
    }
}

void CPlayerStateSave::OnGameSaved(const char* saveFileName)
{
    if (saveFileName == nullptr || saveFileName[0] == '\0')
    {
        return;
    }
    const std::string sidecarName = GetSidecarName(saveFileName);

    // a save made by a player who isn't the host doesn't hold the story: never pair player states with it
    const bool bHostSave = !CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost;
    PlayerStateSidecar::SFile file;
    if (bHostSave)
    {
        file.entries = ms_store.GetEntries();
    }

    if (file.entries.empty())
    {
        if (!RemoveFileIfPresent(sidecarName))
        {
            logger::warn("CPlayerStateSave: couldn't remove %s (%lu)", sidecarName.c_str(), GetLastError());
        }
        if (bHostSave)
        {
            AddChatLineLater("{cecedb}[Co-op] No other player's state to keep with this save.");
        }
        return;
    }

    std::vector<uint8_t> data;
    if (!HashFile(saveFileName, file.saveHash, file.saveSize) || !PlayerStateSidecar::Write(file, data) ||
        !WriteFileAtomically(sidecarName, data))
    {
        logger::error("CPlayerStateSave: couldn't write %s", sidecarName.c_str());
        RemoveFileIfPresent(sidecarName);
        AddChatLineLater("{cecedb}[Co-op] {ff0000}Couldn't keep the other players' state with this save.");
        return;
    }
    logger::info("CPlayerStateSave: %u player state(s) written to %s", static_cast<unsigned>(file.entries.size()),
        sidecarName.c_str());
    AddChatLineLater("{cecedb}[Co-op] Kept the state of %u other player(s) with this save.",
        static_cast<unsigned>(file.entries.size()));
}

void CPlayerStateSave::OnGameLoaded(const char* saveFileName)
{
    ms_bLoadedSave = true;
    if (saveFileName == nullptr || saveFileName[0] == '\0')
    {
        return;
    }
    const std::string sidecarName = GetSidecarName(saveFileName);

    std::vector<uint8_t> data;
    const eReadFile readFile = ReadSmallFile(sidecarName, data);
    if (readFile == eReadFile::Missing)
    {
        logger::info("CPlayerStateSave: %s has no player state file", saveFileName);
        AddChatLineLater("{cecedb}[Co-op] This save keeps no other player's state.");
        return;
    }
    PlayerStateSidecar::SFile file;
    const PlayerStateSidecar::eReadResult result =
        readFile == eReadFile::Ok ? PlayerStateSidecar::Read(data, file) : PlayerStateSidecar::eReadResult::Corrupt;
    if (result != PlayerStateSidecar::eReadResult::Ok)
    {
        logger::warn("CPlayerStateSave: %s ignored (%s)", sidecarName.c_str(),
            readFile == eReadFile::Ok ? PlayerStateSidecar::ToString(result) : "can't read it or too big");
        AddChatLineLater("{cecedb}[Co-op] The player state file of this save is unreadable, ignored.");
        return;
    }

    uint64_t hash = 0;
    uint32_t size = 0;
    if (!HashFile(saveFileName, hash, size) || hash != file.saveHash || size != file.saveSize)
    {
        logger::warn("CPlayerStateSave: %s belongs to another save, ignored", sidecarName.c_str());
        AddChatLineLater("{cecedb}[Co-op] The player state file next to this save belongs to another save, ignored.");
        return;
    }

    ms_store.OnSaveLoaded(file.entries);
    logger::info("CPlayerStateSave: %u player state(s) read from %s (format v%u%s)",
        static_cast<unsigned>(file.entries.size()), sidecarName.c_str(), static_cast<unsigned>(file.version),
        PlayerStateSidecar::GetStateLayout(file.version) < SPlayerState::LAYOUT_V2 ? ", without respect" : "");
    AddChatLineLater("{cecedb}[Co-op] This save keeps the state of %u other player(s).",
        static_cast<unsigned>(file.entries.size()));
}

void CPlayerStateSave::HandleSnapshot(const Packets::PlayerState::PlayerStateSnapshot& packet)
{
    if (!CLocalPlayer::m_bIsHost)
    {
        return;
    }
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(packet.playerid);
    if (pNetworkPlayer == nullptr || !IsValidPlayerName(pNetworkPlayer->m_Name))
    {
        return;
    }

    CPlayerStateStore::SRestore restore;
    if (ms_store.OnSnapshot(pNetworkPlayer->m_Name, packet.connectionNonce, packet.ackRestoreId, packet.state,
            restore) == CPlayerStateStore::eSnapshotResult::SendRestore)
    {
        SendRestore(pNetworkPlayer, restore);
    }
}

void CPlayerStateSave::HandleRestore(const Packets::PlayerState::PlayerStateRestore& packet)
{
    if (CLocalPlayer::m_bIsHost || packet.targetPlayerId != CNetworkPlayerManager::m_nMyId)
    {
        return;
    }
    char name[Config::MAX_NICKNAME_LENGTH + 1];
    SanitizePlayerName(CLocalPlayer::m_Name, name);
    if (strcmp(name, packet.targetName) != 0)
    {
        return;
    }
    ms_client.OnRestoreReceived(packet.restoreId, packet.state);
    logger::info("CPlayerStateSave: restore %u received", packet.restoreId);
}
