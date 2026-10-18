#pragma once

#include <cstdint>

#include "network/vehicle_sync.h"

// Private build (vehicle sync protocol, 0.1.1): the server's rules for vehicle seats, syncers and removals. Pure logic
// without ENet, so tests/vehicle_sync_test.cpp checks it; CNetworkVehicle, CNetworkVehicleManager and
// PacketHandlers/vehicles.cpp apply it.
//
// Seats: CNetworkVehicle::m_pPlayers[0] is the driver's seat, [1..7] are the passenger seats 0..6 of the packets
// (seatid + 1). A player holds at most one seat in one vehicle; CNetworkPlayer::m_nVehicleId/m_nSeatId name it (-1 = none)
// and agree with the seat tables. Upstream recorded an unknown passenger seat (-1) as the driver's seat and never cleared
// a player's previous seat, so one player could hold two seats and a stale driver kept "no driver: a passenger becomes the
// syncer" from running.
namespace VehicleSeatPolicy
{
constexpr int NUM_SEATS = 8;
constexpr int DRIVER_SEAT = 0;

// the seat of passenger seat seatid (VehiclePassengerUpdate, VehicleEnter with bPassenger); -1 = unknown, not recorded
inline int SeatFromPassenger(int seatid)
{
    return seatid >= 0 && seatid + 1 < NUM_SEATS ? seatid + 1 : -1;
}

// the seat a VEHICLE_ENTER names
inline int SeatFromEnter(bool bPassenger, int seatid)
{
    return bPassenger ? SeatFromPassenger(seatid) : DRIVER_SEAT;
}

// The player leaves the seat it holds, in whatever vehicle. Vehicle: m_nVehicleId, m_pPlayers[NUM_SEATS]; Player:
// m_nVehicleId, m_nSeatId; findVehicle(int id) returns the Vehicle* with that id, nullptr when it is gone.
template <class Vehicle, class Player, class FindVehicle>
void LeaveSeat(Player* player, FindVehicle findVehicle)
{
    if (player == nullptr)
    {
        return;
    }
    if (player->m_nVehicleId >= 0)
    {
        if (Vehicle* previous = findVehicle(player->m_nVehicleId))
        {
            // its seat, and a stale second one an older build left
            for (auto& occupant : previous->m_pPlayers)
            {
                if (occupant == player)
                {
                    occupant = nullptr;
                }
            }
        }
    }
    player->m_nVehicleId = -1;
    player->m_nSeatId = -1;
}

// Puts the player into the seat (nullptr empties it): the player first leaves the seat it held, and whoever held this
// seat loses it. Returns that player (nullptr when the seat was free). An unknown seat changes nothing.
template <class Vehicle, class Player, class FindVehicle>
Player* SetOccupant(Vehicle& vehicle, int seat, Player* player, FindVehicle findVehicle)
{
    if (seat < 0 || seat >= NUM_SEATS)
    {
        return nullptr;
    }

    LeaveSeat<Vehicle>(player, findVehicle);
    if (player != nullptr)
    {
        // a stale entry in this vehicle even when the player's ids named another one
        for (auto& occupant : vehicle.m_pPlayers)
        {
            if (occupant == player)
            {
                occupant = nullptr;
            }
        }
    }

    Player* bumped = vehicle.m_pPlayers[seat];
    if (bumped != nullptr && bumped->m_nVehicleId == vehicle.m_nVehicleId && bumped->m_nSeatId == seat)
    {
        bumped->m_nVehicleId = -1;
        bumped->m_nSeatId = -1;
    }

    vehicle.m_pPlayers[seat] = player;
    if (player != nullptr)
    {
        player->m_nVehicleId = vehicle.m_nVehicleId;
        player->m_nSeatId = static_cast<decltype(player->m_nSeatId)>(seat);
    }
    return bumped;
}

// The vehicle is removed: every occupant loses its seat.
template <class Vehicle>
void ClearSeats(Vehicle& vehicle)
{
    for (auto& occupant : vehicle.m_pPlayers)
    {
        if (occupant != nullptr && occupant->m_nVehicleId == vehicle.m_nVehicleId)
        {
            occupant->m_nVehicleId = -1;
            occupant->m_nSeatId = -1;
        }
        occupant = nullptr;
    }
}

// Under T-D4, the host always remains the syncer of train carriages even when another player enters the driver or
// passenger seat.
inline bool ShouldDriverBecomeSyncer(uint16_t modelId) noexcept
{
    return !VehicleSync::IsTrainModelId(static_cast<int>(modelId));
}

// After a passenger update: the passenger who becomes the syncer of a vehicle without a driver (upstream's rule, now on
// the corrected seats); nullptr = the syncer stays. A mission vehicle, a train carriage and one a ped drives keep theirs.
template <class Player, class Vehicle>
Player* ChoosePassengerSyncer(const Vehicle& vehicle, bool bMissionVehicle, bool bUsedByPed, uint16_t modelId = 0)
{
    if (bMissionVehicle || bUsedByPed || !ShouldDriverBecomeSyncer(modelId) || vehicle.m_pPlayers[DRIVER_SEAT] != nullptr)
    {
        return nullptr;
    }
    for (int seat = DRIVER_SEAT + 1; seat < NUM_SEATS; seat++)
    {
        if (vehicle.m_pPlayers[seat] != nullptr)
        {
            return vehicle.m_pPlayers[seat];
        }
    }
    return nullptr;
}

// Whether the server removes a vehicle for a VEHICLE_REMOVE (VehicleSync::eRemoveReason)
inline bool AcceptRemove(uint8_t reason, bool bSenderIsSyncer, bool bSenderIsCreator, bool bSenderIsHost)
{
    switch (reason)
    {
        case VehicleSync::REMOVE_BY_SYNCER:
        case VehicleSync::REMOVE_SYNCER_RESTARTED:
            return bSenderIsSyncer;
        case VehicleSync::REMOVE_UNCONFIRMED:
            return bSenderIsCreator;
        case VehicleSync::REMOVE_BY_HOST_MISSION:
            return bSenderIsHost;
        default:
            return false;
    }
}

// What the server does with a VEHICLE_REMOVE for a vehicle it has
enum class eRemove
{
    DROP_STALE,        // stamped before the vehicle was created: about an older vehicle with this id
                       // (EntityLifecycle::IsFromBeforeCreation), nothing happens
    REMOVE,            // removed on the server and relayed to everyone else
    GIVE_TO_HOST,      // REMOVE_SYNCER_RESTARTED of a car of the host's scripts while the host is another player: the host
                       // syncs it now, the restarting game gets it back as a copy (VEHICLE_SPAWN)
    REJECT_AND_RESEND  // not that player's to remove: the vehicle stays, and since every game lets go of its record before
                       // it sends a removal, the sender gets the vehicle again (VEHICLE_SPAWN with the server's state);
                       // typically a game that destroyed a car it still synced while the server had already given it
                       // to a player who started driving it
};

inline eRemove DecideRemove(uint8_t reason, bool bStale, bool bSenderIsSyncer, bool bSenderIsCreator, bool bSenderIsHost,
    bool bHostMission, bool bHostIsAnotherPlayer)
{
    if (bStale)
    {
        return eRemove::DROP_STALE;
    }
    if (!AcceptRemove(reason, bSenderIsSyncer, bSenderIsCreator, bSenderIsHost))
    {
        return eRemove::REJECT_AND_RESEND;
    }
    // as when its syncer leaves (OnSyncerLeft): the host's mission keeps its car
    if (reason == VehicleSync::REMOVE_SYNCER_RESTARTED && bHostMission && bHostIsAnotherPlayer)
    {
        return eRemove::GIVE_TO_HOST;
    }
    return eRemove::REMOVE;
}

// Whether a vehicle belongs to the host's scripts: MISSION_VEHICLE from the host's game, as its VEHICLE_SPAWN or its
// SET_VEHICLE_CREATED_BY said (the host releasing it, MARK_CAR_AS_NO_LONGER_NEEDED, ends it). A mission vehicle another
// game spawned belongs to that game's own scripts.
inline bool IsHostMission(uint8_t createdBy, bool bFromHost)
{
    return bFromHost && createdBy == VehicleSync::CREATED_BY_MISSION;
}

// A player leaves: what happens to each vehicle it synced. A car of the host's scripts goes back to the host, so the
// host's mission keeps it (unless the host is the one leaving); everything else is removed on every game, as upstream did.
enum class eSyncerLeft
{
    REMOVE,
    GIVE_TO_HOST
};

inline eSyncerLeft OnSyncerLeft(bool bHostMission, bool bHostStays)
{
    return bHostMission && bHostStays ? eSyncerLeft::GIVE_TO_HOST : eSyncerLeft::REMOVE;
}

// A seat while the player's game reports it on foot. A game sends on-foot updates only while its player isn't in a
// vehicle (client Main.cpp) and nothing while it gets out (VehicleSyncPolicy::ChooseLocalUpdate), and every VEHICLE_ENTER,
// driver and passenger update that takes a seat restamps CNetworkPlayer::m_nSeatClaimedAt. So a seat held through on-foot
// updates for OnFootSeatTimeoutMs since then belongs to an entry that never completed (the door was locked, the car drove
// off, the player gave up) or to an exit no VEHICLE_EXIT reported (dragged out, a script's task): it is freed, so it no
// longer blocks "no driver: a passenger becomes the syncer". While the player is still getting in (walking to the door,
// pulling the driver out: seconds) the seat stays. Unsigned elapsed time also handles the wrap of the server clock.
constexpr uint32_t OnFootSeatTimeoutMs = 10000;

inline bool ShouldFreeSeatOnFoot(bool bHoldsSeat, uint32_t now, uint32_t seatClaimedAt)
{
    return bHoldsSeat && static_cast<uint32_t>(now - seatClaimedAt) >= OnFootSeatTimeoutMs;
}

// A rejected idle update: the game that sent it still thinks it syncs the vehicle, so it is told that it doesn't, at most
// once per CorrectionMs per vehicle. Unsigned elapsed time also handles the wrap of the server clock.
class CSyncerCorrection
{
    uint32_t m_lastSent = 0;
    bool m_bSent = false;

public:
    static constexpr uint32_t CorrectionMs = 1000;

    bool ShouldSend(uint32_t now)
    {
        if (m_bSent && static_cast<uint32_t>(now - m_lastSent) < CorrectionMs)
        {
            return false;
        }
        m_bSent = true;
        m_lastSent = now;
        return true;
    }
};
}  // namespace VehicleSeatPolicy
