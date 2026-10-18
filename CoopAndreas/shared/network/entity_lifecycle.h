#pragma once

#include <cstdint>

#include "packet_types.h"  // next to this header, so the policy tests need no include path for it

// Private build (vehicle sync protocol, 0.1.1): the order in which every game handles the lifecycle of network vehicles
// and peds, and the ids the server hands out. Pure logic without plugin-sdk or ENet: the server applies it
// (CPacketFactory, CNetworkVehicleManager, CNetworkPedManager, the packet handlers), the client's temp vehicle ids use
// NextFreeId, and tests/vehicle_sync_test.cpp and tests/ped_sync_state_test.cpp check it.
//
// A game sorts the packets it waits for by their stamp (client/src/CPacketBuffer.cpp; equal stamps keep their arrival
// order, which on one ENet channel is the order the server sent them in). What the server makes itself (VEHICLE_CONFIRM,
// ASSIGN_VEHICLE, the join snapshot, PED_CONFIRM, ASSIGN_PED) carries the server's clock; a relayed packet carried its
// sender's, an estimate of the server's clock that can run ahead of it. So a relayed VEHICLE_REMOVE could be handled
// after the VEHICLE_CONFIRM that gave its freed id to a new vehicle in the same server tick (and removed the new one), or
// before the join snapshot's VEHICLE_SPAWN of the vehicle it removed (a ghost on the joiner).
namespace EntityLifecycle
{
// The relayed packets the server stamps with its own clock (CPacketFactory, BuildPacketStream), so that every game
// handles them in the order the server handled them, together with what the server makes: the spawns and removals, and
// the vehicle state the join snapshot carries as well (damage, components, created-by). A game's own following updates of
// an entity still come after them: it sends them only once it has handled the server-stamped spawn or confirmation, which
// it does no earlier than its own clock reaches that stamp plus INTERP_BUFFER_DELAY_MS. The per-frame updates (idle,
// driver, passenger, on foot, shots), VEHICLE_ENTER/EXIT and the radio keep their sender's clock: they are that player's
// own stream, ordered against its keys and positions. ENTITY_ATTACH takes the server's clock too: an attachment names
// network ids, so it must line up with their removals (a removed target or subject drops the attachment, and a late
// attachment must not come after it) and with the detach fence the server sends for a departing player before his id
// can be handed out again (CNetwork::HandlePlayerDisconnected; equal stamps keep the server's send order).
inline bool UsesServerTime(ePacketType type)
{
    switch (type)
    {
        case ePacketType::VEHICLE_SPAWN:
        case ePacketType::VEHICLE_REMOVE:
        case ePacketType::VEHICLE_DAMAGE:
        case ePacketType::VEHICLE_COMPONENT_ADD:
        case ePacketType::VEHICLE_COMPONENT_REMOVE:
        case ePacketType::SET_VEHICLE_CREATED_BY:
        case ePacketType::PED_SPAWN:
        case ePacketType::PED_REMOVE:
        case ePacketType::ENTITY_ATTACH:
            return true;
        default:
            return false;
    }
}

// A packet about an entity stamped before the server created it is about an older entity that had the same id: a game
// learns an id from the server-stamped spawn or confirmation, handles it no earlier than its own clock reaches that stamp
// (plus INTERP_BUFFER_DELAY_MS), and stamps everything it sends afterwards with that clock. The server drops such a packet
// instead of applying it to the new entity (upstream seated a late driver in it and made that game its syncer). Only up
// to LATE_PACKET_WINDOW_MS before: a packet that late is long gone (ENet gives a silent peer up after 30 s), while a game
// whose clock hasn't had its first server-time answer yet stamps with its PC's uptime (CServerTime), anywhere on the
// circle; its packets stay accepted, as before. The 32-bit millisecond clocks wrap: the difference is read as signed.
constexpr uint32_t LATE_PACKET_WINDOW_MS = 60000;

inline bool IsFromBeforeCreation(uint32_t stamp, uint32_t createdAt)
{
    const int32_t sinceCreation = static_cast<int32_t>(stamp - createdAt);
    return sinceCreation < 0 && sinceCreation > -static_cast<int32_t>(LATE_PACKET_WINDOW_MS);
}

// The next free id after the last one handed out (round robin) instead of the lowest free one: an id comes back only after
// every other free id has been used, so a late packet about the entity that had it doesn't meet a new one. last is -1
// before the first id; -1 = all count ids are taken.
template <class IsFree> int NextFreeId(int last, int count, IsFree isFree)
{
    for (int step = 1; step <= count; step++)
    {
        const int id = (last + step) % count;
        if (isFree(id))
            return id;
    }
    return -1;
}
}  // namespace EntityLifecycle
