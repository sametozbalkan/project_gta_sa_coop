#pragma once

#include <cmath>
#include <cstdint>

// Private build (vehicle sync protocol, 0.1.1): wire values the client, the server and tests/vehicle_sync_test.cpp share,
// without plugin-sdk. The rules are in client/src/CVehicleSyncPolicy.h and server/src/CVehicleSeatPolicy.h.
namespace VehicleSync
{
// Packets::Vehicles::VehicleRemove::reason: who asks for the removal. The server accepts it from the vehicle's syncer
// (upstream's only case), from the game that created the vehicle when that game destroyed it before VEHICLE_CONFIRM (or
// no longer has a record for the id VEHICLE_CONFIRM names), and from the host when a script of its mission deleted a
// vehicle another game syncs. The server relays it to everyone else, who remove the vehicle whatever the reason; a
// removal it rejects sends the vehicle back to its sender (VEHICLE_SPAWN), whose game has already let go of it.
// REMOVE_SYNCER_RESTARTED: the syncer's own game started over (a new game or a load destroys every vehicle); from the
// syncer only, and a car of the host's scripts goes back to the host instead (as when its syncer leaves).
enum eRemoveReason : uint8_t
{
    REMOVE_BY_SYNCER = 0,
    REMOVE_UNCONFIRMED = 1,
    REMOVE_BY_HOST_MISSION = 2,
    REMOVE_SYNCER_RESTARTED = 3,
    REMOVE_REASON_MAX = REMOVE_SYNCER_RESTARTED
};

// for the logs
inline const char* RemoveReasonName(unsigned reason)
{
    switch (reason)
    {
        case REMOVE_BY_SYNCER: return "syncer";
        case REMOVE_UNCONFIRMED: return "unconfirmed";
        case REMOVE_BY_HOST_MISSION: return "host-mission";
        case REMOVE_SYNCER_RESTARTED: return "syncer-restarted";
        default: return "unknown";
    }
}

// VehicleEnter (bPassenger) and VehiclePassengerUpdate seatid: an index into CVehicle::m_apPassengers, or this when the
// game doesn't know the seat (CCarEnterExit::ComputePassengerIndexFromCarDoor 0x64F1E0 returns -1 for a door it doesn't
// map, and a passenger update can't find the local ped among the passengers). The server doesn't record it.
constexpr int SEAT_UNKNOWN = -1;

// eVehicleCreatedBy (plugin-sdk CVehicle.h, the wire values of VehicleSpawn::createdBy and SetVehicleCreatedBy), for
// the rules that compile without plugin-sdk
constexpr uint8_t CREATED_BY_RANDOM = 1;
constexpr uint8_t CREATED_BY_MISSION = 2;
constexpr uint8_t CREATED_BY_PARKED = 3;  // a car generator's (CCarGenerator::DoInternalProcessing, gta-reversed CarGenerator.cpp)

// The heading VehicleSpawn::rot carries (CPlaceable::SetOrientation(0, 0, heading): right (cos, sin, 0), forward
// (-sin, cos, 0)) from a vehicle's forward vector, the `rot` of the idle, driver and NPC driver updates (CMatrix::up); 0
// for a vehicle that points straight up or down. The server's resent vehicles (SendVehicleTo) took the forward vector's z
// for the heading, so they faced north until their first update.
inline float HeadingFromForward(float forwardX, float forwardY)
{
    if (std::fabs(forwardX) < 1e-6f && std::fabs(forwardY) < 1e-6f)
    {
        return 0.0f;
    }
    return std::atan2(-forwardX, forwardY);
}

// The six train and tram models in data\vehicles.ide (449 tram, 537 freight, 538 streak, 569 freiflat, 570 streakc,
// 590 freibox), shared so the server (without CModelInfo) and the policy tests can identify train carriages (T1a).
constexpr bool IsTrainModelId(int modelId) noexcept
{
    switch (modelId)
    {
        case 449:
        case 537:
        case 538:
        case 569:
        case 570:
        case 590:
            return true;
        default:
            return false;
    }
}
}  // namespace VehicleSync

