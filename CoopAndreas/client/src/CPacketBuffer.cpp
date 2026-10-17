#include "CServerTime.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/packets/system.h"
#include "stdafx.h"
#include "CPacketBuffer.h"
#include "CServerTimePolicy.h"

void CPacketBuffer::Receive(Packet* pPacket)
{
    if (pPacket->GetChannel() == ePacketChannel::SYSTEM)
    {
        GetPacketHandler().ProcessPacket(pPacket);
        delete pPacket;
        return;
    }

    if (m_packets.empty() || pPacket->serverTime >= m_packets.back()->serverTime)
    {
        m_packets.push_back(pPacket);
        return;
    }

    auto it = std::upper_bound(m_packets.begin(), m_packets.end(), pPacket->serverTime,
        [](server_time_t time, const Packet* snapshot) { return time < snapshot->serverTime; });

    m_packets.insert(it, pPacket);
}

// private build (vehicle sync protocol): packets stamped far ahead of this game's clock came from a game whose clock hadn't
// had its first server-time answer (its PC's uptime, ServerTimePolicy::IsStampFromUnsyncedClock): they wait at the end of
// the buffer, which is sorted by stamp, for hours or days. Once this game's clock is synced they get its current time and
// go back in, in their order, to be handled with the next ones.
void CPacketBuffer::RestampFromUnsyncedClocks()
{
    if (!CServerTime::m_bSynced || m_packets.empty() ||
        !ServerTimePolicy::IsStampFromUnsyncedClock(m_packets.back()->serverTime, g_serverTime))
    {
        return;
    }
    std::deque<Packet*> restamped;
    while (!m_packets.empty() && ServerTimePolicy::IsStampFromUnsyncedClock(m_packets.back()->serverTime, g_serverTime))
    {
        restamped.push_front(m_packets.back());
        m_packets.pop_back();
    }
    for (Packet* pPacket : restamped)
    {
        pPacket->serverTime = g_serverTime;
        Receive(pPacket);
        m_nRestamped++;
    }
}

void CPacketBuffer::Process()
{
    RestampFromUnsyncedClocks(); // private build

    uint32_t renderTime = GetRenderTime(g_serverTime);

    for (auto it = m_packets.begin(); it != m_packets.end();)
    {
        Packet* pPacket = *it;
        if (pPacket->serverTime > renderTime)
        {
            break;
        }

        it = m_packets.erase(it);

        GetPacketHandler().ProcessPacket(pPacket);

        SPacketRecord packetRecord{};
        packetRecord.m_nSize = pPacket->GetBytesRead();
        packetRecord.m_bInbound = true;
        packetRecord.m_bOutbound = false;
        packetRecord.m_packetType = pPacket->GetType();
        packetRecord.m_serverTime = pPacket->serverTime;
        packetRecord.m_sContent = pPacket->ToString();
        GetPacketFactory().AddPacketRecord(packetRecord, renderTime);

        delete pPacket;
    }
}