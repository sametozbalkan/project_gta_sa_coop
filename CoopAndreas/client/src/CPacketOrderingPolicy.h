#pragma once

#include <algorithm>

namespace PacketOrderingPolicy
{
// Equal server stamps retain the reliable channel's arrival order. This includes
// a departing player's detach fence before any attachment of the recycled ID.
template <typename Queue, typename PacketPointer>
void InsertByServerTime(Queue& packets, PacketPointer packet)
{
    if (packets.empty() || packet->serverTime >= packets.back()->serverTime)
    {
        packets.push_back(packet);
        return;
    }
    auto it = std::upper_bound(packets.begin(), packets.end(), packet->serverTime,
        [](auto time, const auto* snapshot) { return time < snapshot->serverTime; });
    packets.insert(it, packet);
}
} // namespace PacketOrderingPolicy
