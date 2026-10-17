#pragma once

class CServerTime
{
public:
    static constexpr uint32_t REQUEST_FREQUENCY = 500;
    static void Process();
    
    static void RecalculateOffset(int32_t newOffset);
    static void RecalculateServerTime();
    static void SendRequest();

    // private build (vehicle sync protocol): the request in flight (0 = none; CNetworkKeepAlive cancels it with 0), see
    // CServerTimePolicy.h
    static uint32_t m_requestTick;
    static uint32_t m_lastUpdated;
    // private build: the offset g_serverTime uses (0 until the first answer, which the other modules check); it follows
    // m_targetOffset, backwards only gradually (ServerTimePolicy::NextAppliedOffset)
    static int32_t m_serverTimeOffset;
    static int32_t m_targetOffset;   // private build: the estimate from the answers
    static bool m_bSynced;           // private build: the first answer came
    static uint32_t m_lastRecalculated;  // private build: GetTickCount() of the last RecalculateServerTime once synced
    static unsigned m_nAnswers;      // private build (--coopdiag): answers used, and the ones that matched no request
    static unsigned m_nAnswersIgnored;
};

extern server_time_t g_serverTime;