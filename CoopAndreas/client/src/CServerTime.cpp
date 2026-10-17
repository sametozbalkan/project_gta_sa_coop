#include "stdafx.h"
#include "CServerTime.h"
#include "CServerTimePolicy.h"
#include <CWeatherSync.h>

server_time_t g_serverTime = 0;

uint32_t CServerTime::m_requestTick = 0;
uint32_t CServerTime::m_lastUpdated = 0;
int32_t CServerTime::m_serverTimeOffset = 0;
// private build (vehicle sync protocol)
int32_t CServerTime::m_targetOffset = 0;
bool CServerTime::m_bSynced = false;
uint32_t CServerTime::m_lastRecalculated = 0;
unsigned CServerTime::m_nAnswers = 0;
unsigned CServerTime::m_nAnswersIgnored = 0;

void CServerTime::Process()
{
    RecalculateServerTime();

    if (CNetwork::m_bAuthenticated)
    {
        uint32_t clientTick = GetTickCount();
        // private build (vehicle sync protocol): one request in flight at a time, for up to REQUEST_TIMEOUT_MS
        // (ServerTimePolicy::MaySendRequest); upstream sent one every 500 ms whatever was still on its way
        if (clientTick - m_lastUpdated > REQUEST_FREQUENCY && ServerTimePolicy::MaySendRequest(m_requestTick, clientTick))
        {
            SendRequest();
            m_lastUpdated = clientTick;
        }
    }
}

void CServerTime::RecalculateServerTime()
{
    // private build (vehicle sync protocol): after the first answer the stamped clock never runs backwards, a correction
    // that would take it back is spread out (ServerTimePolicy::NextAppliedOffset)
    const uint32_t now = GetTickCount();
    if (m_bSynced)
    {
        m_serverTimeOffset = ServerTimePolicy::NextAppliedOffset(m_serverTimeOffset, m_targetOffset, now - m_lastRecalculated);
        m_lastRecalculated = now;
    }
    g_serverTime = now + m_serverTimeOffset;

    CWeatherSync::ServerTimeRecalculated(g_serverTime);
}

void CServerTime::RecalculateOffset(int32_t newOffset)
{
    // private build (vehicle sync protocol): the first answer replaces the PC's uptime outright (the one step the clock
    // makes, mostly backwards: the server's clock starts at 0 with server.exe); later ones move the estimate by a tenth,
    // and the stamped clock follows it (RecalculateServerTime)
    const bool bFirst = !m_bSynced;
    m_targetOffset = ServerTimePolicy::NextTargetOffset(bFirst, m_targetOffset, newOffset);
    if (bFirst)
    {
        m_bSynced = true;
        m_serverTimeOffset = m_targetOffset;
        m_lastRecalculated = GetTickCount();
    }
}

void CServerTime::SendRequest()
{
    m_requestTick = GetTickCount();
    if (m_requestTick == 0)
    {
        m_requestTick = 1;  // private build: 0 means "none in flight"
    }

    Packets::System::ServerTimeRequest serverTimeRequest{};
    serverTimeRequest.clientTick = m_requestTick;  // private build: echoed in the answer
    GetPacketFactory().Send(serverTimeRequest);
}

PACKET_HANDLER(ePacketType::SERVER_TIME_REQUEST, Packets::System::ServerTimeRequest* pServerTimeRequest)
{
    // private build (vehicle sync protocol): only the answer to the request in flight (ServerTimePolicy::
    // IsAnswerToOutstanding); upstream paired any answer with the newest request
    if (!ServerTimePolicy::IsAnswerToOutstanding(CServerTime::m_requestTick, pServerTimeRequest->clientTick))
    {
        CServerTime::m_nAnswersIgnored++;
        return;
    }

    uint32_t clientTick = GetTickCount();
    uint32_t rtt = clientTick - CServerTime::m_requestTick;

    uint32_t serverTime = pServerTimeRequest->serverTimeRespond + rtt / 2;

    int32_t newOffset = static_cast<int32_t>(static_cast<int64_t>(serverTime) - static_cast<int64_t>(clientTick));

    CServerTime::RecalculateOffset(newOffset);
    CServerTime::RecalculateServerTime();

    // logger::info("tick %u, rtt %u, serverTime(r) %u, newOffset %d, offset %d, serverTime %d", clientTick, rtt,
    //     serverTime, newOffset, CServerTime::m_serverTimeOffset, g_serverTime);

    CServerTime::m_requestTick = 0;
    CServerTime::m_nAnswers++;
}