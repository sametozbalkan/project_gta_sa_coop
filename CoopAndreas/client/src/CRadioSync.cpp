#include "stdafx.h"
#include "CRadioSync.h"
#include <cstddef>
#include <CAERadioTrackManager.h>
#include <CTimer.h>

// Private build: car radio sync, see CRadioSync.h.
//
// How gta_sa.exe 1.0 US runs the car radio (read from its bytes; names from plugin-sdk and gta-reversed):
// - CAEVehicleAudioEntity::Service (0x502280) turns the radio on for the vehicle the local player sits in, as driver
//   or passenger (TurnOnRadioForVehicle 0x4F5B20 sets s_pVehicleAudioSettingsForRadio = &m_settings and starts
//   m_settings), and off when they leave (TurnOffRadioForVehicle 0x4F5B60, StopRadio 0x4E9820 saves the station
//   into m_settings.m_nRadioID).
// - StartRadio(tVehicleAudioSettings&) (0x4EB550) starts m_settings.m_nRadioID, or with "radio auto-tune" on the
//   station of the car the player left less than a minute ago.
// - CAERadioTrackManager::StartRadio(id, ...) (0x4EB3C0) stores the station it starts in m_TempSettings right away
//   (written at 0x4EB484); GetCurrentRadioStationID (0x4E83F0) reads m_Settings, which follows once a track plays.
// - CAudioEngine::RetuneRadio (0x507E10) only queues the station (CAERadioTrackManager::RetuneRadio 0x4E8290 writes
//   field_7C at 0x4E82BB). CheckForStationRetune (0x4EB660) takes it like a station key press, for a civilian radio
//   only, plays the retune noise and starts the station about 1.5 s later; radio off is immediate. A retune is in
//   progress while m_nStationsListed or m_nStationsListDown is not 0.

// offsets as CheckForStationRetune (0x4EB6F5-0x4EB70E, 0x4EB775) and StartRadio (0x4EB484) use them
static_assert(offsetof(CAERadioTrackManager, m_nStationsListed) == 0x6C, "CAERadioTrackManager layout");
static_assert(offsetof(CAERadioTrackManager, m_nStationsListDown) == 0x70, "CAERadioTrackManager layout");
static_assert(offsetof(CAERadioTrackManager, field_7C) == 0x7C, "CAERadioTrackManager layout");
static_assert(offsetof(CAERadioTrackManager, m_TempSettings) + offsetof(tRadioSettings, m_nCurrentRadioStation) == 0xAD,
    "CAERadioTrackManager layout");
// StopRadio writes m_nRadioID at +0x1A (0x4E9D0A), TurnOnRadioForVehicle reads m_nRadioType at +0x1B (0x4F5B3B)
static_assert(offsetof(tVehicleAudioSettings, m_nRadioID) == 0x1A, "tVehicleAudioSettings layout");
static_assert(offsetof(tVehicleAudioSettings, m_nRadioType) == 0x1B, "tVehicleAudioSettings layout");

// eRadioID (plugin-sdk eRadioID.h, gta-reversed Enums/eRadioID.h): 0 police scanner, 1-11 stations, 12 user track
// player, 13 radio off
constexpr int8_t STATION_FIRST = RADIO_PLAYBACK_FM;
constexpr int8_t STATION_USER_TRACKS = RADIO_USER_TRACKS;
constexpr int8_t STATION_OFF = RADIO_NONE;

// CAEUserRadioTrackManager::m_nUserTracksCount (gta-reversed: AEUserRadioTrackManager at 0xB6B970, the field at +6).
// RetuneRadio reads it at 0x4E8298 and plays radio off instead of the user track player when it is 0.
static uint16_t& ms_nUserTracksCount = *reinterpret_cast<uint16_t*>(0xB6B976);

// the game may drop a retune (StartRadio 0x4EB427 refuses while an ambience track overrides the radio): retry, but
// stop after a few failures in a row until the driver picks another station
constexpr uint8_t MAX_RETUNE_ATTEMPTS = 3;
constexpr uint32_t RETUNE_RETRY_DELAY_MS = 3000;

// driver: what we sent last
static int ms_nSentVehicleId = -1;
static int8_t ms_nSentStation = -1;
static uint8_t ms_nSentPassengers = 0;  // seats held by network players (bit per seat)

// passenger: the station we follow
static int ms_nFollowedVehicleId = -1;
static int8_t ms_nFollowedStation = -1;
static uint8_t ms_nRetuneAttempts = 0;
static uint32_t ms_nLastRetuneTime = 0;

// the station the radio plays or is starting; none (-1) counts as radio off, like GetCurrentRadioStationID does
static int8_t GetRequestedStation()
{
    int8_t station = AERadioTrackManager.m_TempSettings.m_nCurrentRadioStation;
    return station < 0 ? STATION_OFF : station;
}

static bool IsRetuneInProgress()
{
    return static_cast<int32_t>(AERadioTrackManager.field_7C) >= 0 || AERadioTrackManager.m_nStationsListed != 0 ||
           AERadioTrackManager.m_nStationsListDown != 0;
}

// the station this game can play for the one the driver picked
static int8_t ToLocalStation(int8_t station)
{
    if (station == STATION_USER_TRACKS && ms_nUserTracksCount == 0)
    {
        return STATION_OFF;
    }

    return station;
}

// the radio playing is this vehicle's and can be retuned (CheckForStationRetune retunes civilian radios only)
static bool IsVehicleRadioPlaying(CVehicle* pVehicle)
{
    tVehicleAudioSettings& settings = pVehicle->m_vehicleAudio.m_settings;
    return CAEVehicleAudioEntity::s_pVehicleAudioSettingsForRadio == &settings && settings.m_nRadioType == RADIO_CIVILIAN;
}

// the network vehicle in which the local player rides with a network player driving, or nullptr
static CNetworkVehicle* GetVehicleWithNetworkDriver()
{
    CPlayerPed* pLocalPed = FindPlayerPed(0);
    if (pLocalPed == nullptr || !pLocalPed->m_nPedFlags.bInVehicle)
    {
        return nullptr;
    }

    CVehicle* pVehicle = pLocalPed->m_pVehicle;
    if (pVehicle == nullptr || pVehicle->m_pDriver == nullptr || pVehicle->m_pDriver == pLocalPed)
    {
        return nullptr;
    }

    if (CNetworkPlayerManager::GetPlayer(pVehicle->m_pDriver) == nullptr)
    {
        return nullptr;
    }

    return CNetworkVehicleManager::GetVehicle(pVehicle);
}

// CAERadioTrackManager::CheckForStationRetune (0x4EB660) reads the station keys at 0x4EB72A
// (CPad::NextStationJustUp, 0x5405B0) and 0x4EB753 (CPad::LastStationJustUp, 0x5405E0).
// The passenger of a network player can't retune: the driver picks the station.
static bool __fastcall CPad__NextStationJustUp_Hook(CPad* This, SKIP_EDX)
{
    if (GetVehicleWithNetworkDriver())
    {
        return false;
    }

    return This->NextStationJustUp();
}

static bool __fastcall CPad__LastStationJustUp_Hook(CPad* This, SKIP_EDX)
{
    if (GetVehicleWithNetworkDriver())
    {
        return false;
    }

    return This->LastStationJustUp();
}

// sends the station when the local driver gets in, retunes, or a network player gets in as a passenger
static void ProcessDriver(CVehicle* pVehicle)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
    if (pNetworkVehicle == nullptr || !IsVehicleRadioPlaying(pVehicle))
    {
        ms_nSentVehicleId = -1;
        ms_nSentPassengers = 0;
        return;
    }

    uint8_t passengers = 0;
    for (uint8_t i = 0; i < ARRAY_SIZE(pVehicle->m_apPassengers); i++)
    {
        if (pVehicle->m_apPassengers[i] && CNetworkPlayerManager::GetPlayer(pVehicle->m_apPassengers[i]))
        {
            passengers |= static_cast<uint8_t>(1u << i);
        }
    }

    bool bNewPassenger = (passengers & ~ms_nSentPassengers) != 0;
    ms_nSentPassengers = passengers;

    int8_t station = GetRequestedStation();
    if (station < STATION_FIRST || station > STATION_OFF)
    {
        return;
    }

    if (pNetworkVehicle->m_nVehicleId == ms_nSentVehicleId && station == ms_nSentStation && !bNewPassenger)
    {
        return;
    }

    Packets::Vehicles::VehicleRadioStation packet{};
    packet.vehicleid = pNetworkVehicle->m_nVehicleId;
    packet.station = station;
    GetPacketFactory().Send(packet);

    pNetworkVehicle->m_nRadioStation = station;
    ms_nSentVehicleId = pNetworkVehicle->m_nVehicleId;
    ms_nSentStation = station;
}

// retunes the local radio to the station of the network driver
static void ProcessPassenger(CNetworkVehicle* pNetworkVehicle)
{
    if (pNetworkVehicle->m_nRadioStation < 0 || !IsVehicleRadioPlaying(pNetworkVehicle->m_pVehicle))
    {
        ms_nFollowedVehicleId = -1;
        return;
    }

    int8_t station = ToLocalStation(pNetworkVehicle->m_nRadioStation);
    if (pNetworkVehicle->m_nVehicleId != ms_nFollowedVehicleId || station != ms_nFollowedStation)
    {
        ms_nFollowedVehicleId = pNetworkVehicle->m_nVehicleId;
        ms_nFollowedStation = station;
        ms_nRetuneAttempts = 0;
    }

    if (GetRequestedStation() == station)
    {
        ms_nRetuneAttempts = 0;
        return;
    }

    if (IsRetuneInProgress())
    {
        return;
    }

    if (ms_nRetuneAttempts >= MAX_RETUNE_ATTEMPTS ||
        (ms_nRetuneAttempts > 0 && CTimer::m_snTimeInMilliseconds - ms_nLastRetuneTime < RETUNE_RETRY_DELAY_MS))
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("RADIO retune to %d (vehicle %d)", station, pNetworkVehicle->m_nVehicleId);
#endif

    AudioEngine.RetuneRadio(station);
    ms_nRetuneAttempts++;
    ms_nLastRetuneTime = CTimer::m_snTimeInMilliseconds;
}

void CRadioSync::InjectHooks()
{
    patch::RedirectCall(0x4EB72A, CPad__NextStationJustUp_Hook);
    patch::RedirectCall(0x4EB753, CPad__LastStationJustUp_Hook);
}

void CRadioSync::Process()
{
    if (!CNetwork::m_bAuthenticated)
    {
        ms_nSentVehicleId = -1;
        ms_nSentPassengers = 0;
        ms_nFollowedVehicleId = -1;
        return;
    }

    // while paused RetuneRadio queues a pause menu request instead; carry on once the game runs again
    if (CTimer::m_UserPause || CTimer::m_CodePause)
    {
        return;
    }

    CPlayerPed* pLocalPed = FindPlayerPed(0);
    if (pLocalPed && pLocalPed->m_nPedFlags.bInVehicle && pLocalPed->m_pVehicle &&
        pLocalPed->m_pVehicle->m_pDriver == pLocalPed)
    {
        ProcessDriver(pLocalPed->m_pVehicle);
    }
    else
    {
        ms_nSentVehicleId = -1;
        ms_nSentPassengers = 0;
    }

    if (CNetworkVehicle* pNetworkVehicle = GetVehicleWithNetworkDriver())
    {
        ProcessPassenger(pNetworkVehicle);
    }
    else
    {
        ms_nFollowedVehicleId = -1;
    }
}

void CRadioSync::OnStationReceived(int vehicleid, int8_t station)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle == nullptr || !pVehicle->IsVTableValid())
    {
        return;
    }

    // we drive it: our radio is its station
    if (pVehicle->m_pDriver == FindPlayerPed(0))
    {
        return;
    }

    station = ToLocalStation(station);
    pNetworkVehicle->m_nRadioStation = station;

    // the station the radio starts with when the local player gets in; while it plays, Process retunes it instead
    tVehicleAudioSettings& settings = pVehicle->m_vehicleAudio.m_settings;
    if (settings.m_nRadioType == RADIO_CIVILIAN && CAEVehicleAudioEntity::s_pVehicleAudioSettingsForRadio != &settings)
    {
        settings.m_nRadioID = static_cast<eRadioID>(station);
    }
}
