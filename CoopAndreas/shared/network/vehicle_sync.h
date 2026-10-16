#pragma once

#include <cstdint>

// Private build (vehicle sync protocol, 0.1.1): wire values the client, the server and tests/vehicle_sync_test.cpp share,
// without plugin-sdk. The rules are in client/src/CVehicleSyncPolicy.h and server/src/CVehicleSeatPolicy.h.
namespace VehicleSync
{
// Packets::Vehicles::VehicleRemove::reason: who asks for the removal. The server accepts it from the vehicle's syncer
// (upstream's only case), from the game that created the vehicle when that game destroyed it before VEHICLE_CONFIRM, and
// from the host when a script of its mission deleted a vehicle another game syncs. The server relays it to everyone
// else, who remove the vehicle whatever the reason.
enum eRemoveReason : uint8_t
{
    REMOVE_BY_SYNCER = 0,
    REMOVE_UNCONFIRMED = 1,
    REMOVE_BY_HOST_MISSION = 2,
    REMOVE_REASON_MAX = REMOVE_BY_HOST_MISSION
};

// VehicleEnter (bPassenger) and VehiclePassengerUpdate seatid: an index into CVehicle::m_apPassengers, or this when the
// game doesn't know the seat (CCarEnterExit::ComputePassengerIndexFromCarDoor 0x64F1E0 returns -1 for a door it doesn't
// map, and a passenger update can't find the local ped among the passengers). The server doesn't record it.
constexpr int SEAT_UNKNOWN = -1;

// eVehicleCreatedBy (plugin-sdk CVehicle.h, the wire values of VehicleSpawn::createdBy and SetVehicleCreatedBy), for
// the rules that compile without plugin-sdk
constexpr uint8_t CREATED_BY_RANDOM = 1;
constexpr uint8_t CREATED_BY_MISSION = 2;
}  // namespace VehicleSync
