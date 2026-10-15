#include "stdafx.h"
#include "CDiagLog.h"
#include "CNetworkKeepAlive.h"
#include <CCamera.h>
#include <CGame.h>
#include <CMenuManager.h>
#include <CTheScripts.h>
#include <CTimer.h>
#include <CWorld.h>

static char ms_szPath[MAX_PATH];

static void WriteSnapshot(FILE* file)
{
    HWND hForeground = GetForegroundWindow();
    char szForeground[64] = "";
    GetWindowTextA(hForeground, szForeground, sizeof(szForeground));
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(hForeground, &foregroundPid);

    CPlayerInfo& player = CWorld::Players[0];
    fprintf(file,
        "t=%lu gameState=%d frame=%u timeMs=%u userPause=%d codePause=%d menu=%d fadeAlpha=%.0f fadeFlag=%d "
        "connected=%d auth=%d host=%d bgKept=%u bgDropped=%u playerState=%d ped=%p focus='%s'%s\n",
        GetTickCount(), gGameState, CTimer::m_FrameCounter, CTimer::m_snTimeInMilliseconds, CTimer::m_UserPause,
        CTimer::m_CodePause, FrontEndMenuManager.m_bMenuActive, TheCamera.m_fFadeAlpha, TheCamera.m_nFadeInOutFlag,
        CNetwork::m_bConnected, CNetwork::m_bAuthenticated, CLocalPlayer::m_bIsHost, CNetworkKeepAlive::GetKeptCount(),
        CNetworkKeepAlive::GetDroppedCount(), player.m_nPlayerState, player.m_pPed, szForeground,
        foregroundPid == GetCurrentProcessId() ? "" : " (NOT FOCUSED)");

    // scripts: name, IP offset into the script space, ms until it wakes up
    fprintf(file, "  scripts:");
    int count = 0;
    for (CRunningScript* script = CTheScripts::pActiveScripts; script && count < 96; script = script->m_pNext, count++)
    {
        char name[9] = {};
        memcpy(name, script->m_szName, 8);
        fprintf(file, " %s@%X/%d%s", name, (unsigned)(script->m_pCurrentIP - (unsigned char*)CTheScripts::ScriptSpace),
            script->m_nWakeTime - (int)CTimer::m_snTimeInMilliseconds, script->m_bIsMission ? "M" : "");
    }
    fprintf(file, "\n");
}

static DWORD WINAPI DiagThread(LPVOID)
{
    while (true)
    {
        if (FILE* file = fopen(ms_szPath, "a"))
        {
            WriteSnapshot(file);
            fclose(file);
        }
        Sleep(2000);
    }
}

void CDiagLog::Start()
{
    GetModuleFileNameA(NULL, ms_szPath, MAX_PATH);
    strcpy_s(strrchr(ms_szPath, '\\') + 1, MAX_PATH - (strrchr(ms_szPath, '\\') + 1 - ms_szPath), "CoopAndreas_diag.log");

    if (FILE* file = fopen(ms_szPath, "a"))
    {
        fprintf(file, "==== start %lu, command line: %s\n", GetTickCount(), GetCommandLineA());
        fclose(file);
    }
    CreateThread(NULL, 0, DiagThread, NULL, 0, NULL);
}
