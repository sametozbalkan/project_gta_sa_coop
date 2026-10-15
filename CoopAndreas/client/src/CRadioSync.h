#pragma once

// Private build: car radio sync. The driver's station is the vehicle's station: while a network player drives, the
// local player's radio in that vehicle follows the station the driver sends (VEHICLE_RADIO_STATION) and the
// passenger's next/previous station keys do nothing. With an NPC driver or no driver the game's own radio runs.
// The song position is not synced.
class CRadioSync
{
public:
    static void InjectHooks();
    static void Process();
    static void OnStationReceived(int vehicleid, int8_t station);
};
