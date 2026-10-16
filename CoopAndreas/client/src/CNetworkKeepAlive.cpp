#include "stdafx.h"
#include "CNetworkKeepAlive.h"
#include "CServerTime.h"
#include "CKeySync.h"

// Main.cpp: the frame loop's on-foot update of the local player
void SendLocalOnFootUpdate(CPlayerPed* localPlayer);

// 1.0 US WinMain (0x748710) main loop, read from gta_sa.exe 1.0 US (MD5 170b3a9108687b26da2d8901c6948a18) with dumpbin
// /disasm and tools/pe_peek.py; same structure as gta-reversed source/app/platform/win/WinMain.cpp (ProcessGameLogic):
//   0x748A87  39 1D 1C 62 8D 00     cmp ForegroundApp (0x8D621C), ebx(0)
//   0x748A8D  0F 84 20 03 00 00     je 0x748DB3                      -> background branch, no frame this loop
//   0x748D9B  E8 C0 0D ED FF        call RsEventHandler(rsIDLE)      -> foreground branch: one frame
//   0x748DB3  ...                   isForeground (0xC920EC) = 0
//   0x748DC1  6A 64                 push 100
//   0x748DC3  FF 15 C4 80 85 00     call dword ptr ds:[0x8580C4]     -> KERNEL32 IAT entry 38, "Sleep"
// psWindowProc (0x747EB0) sets ForegroundApp to 0 on WM_ACTIVATE/WA_INACTIVE (0x747FB0), which also covers minimizing;
// its WM_KILLFOCUS write (0x748054) is already forced to 1 by CPatch (0x74805A).
//
// The hook replaces the memory operand of that indirect call (0x748DC5) with the address of ms_pfnBackgroundSleep, so
// only this call site changes and the instruction keeps its length and its stdcall contract (one argument, callee
// pops it). The original bytes are checked first.
static constexpr uintptr_t WINMAIN_BACKGROUND_SLEEP_CALL = 0x748DC3;
static constexpr uint8_t aWinMainBackgroundSleepCall[] = {0xFF, 0x15, 0xC4, 0x80, 0x85, 0x00};

// Safety net for hours in the background: past this the session is closed instead of growing the queue further.
// Ten minutes of busy reliable traffic is a few MB. Each kept packet also costs its ENetPacket and two heap blocks.
static constexpr size_t MAX_KEPT_BYTES = 32 * 1024 * 1024;
static constexpr size_t KEPT_PACKET_OVERHEAD = 64;

void CNetworkKeepAlive::InjectHooks()
{
    if (memcmp(reinterpret_cast<const void*>(WINMAIN_BACKGROUND_SLEEP_CALL), aWinMainBackgroundSleepCall,
            sizeof(aWinMainBackgroundSleepCall)) != 0)
    {
        logger::warn("[KeepAlive] unexpected code at 0x%X, the connection is not kept alive in the background",
            WINMAIN_BACKGROUND_SLEEP_CALL);
        return;
    }

    ms_ppfnSleep = *reinterpret_cast<VOID(WINAPI***)(DWORD)>(WINMAIN_BACKGROUND_SLEEP_CALL + 2);
    patch::SetPointer(WINMAIN_BACKGROUND_SLEEP_CALL + 2, &ms_pfnBackgroundSleep);
}

bool CNetworkKeepAlive::IsServiceable()
{
    // same condition as the frame loop in Main.cpp
    return CNetwork::m_bConnected && CNetwork::m_pENetHost != nullptr;
}

// Runs on the main thread in place of WinMain's Sleep(100) while the game is in the background. Instead of sleeping
// blind it waits on the ENet socket for the same time, so acks and pings go out as traffic arrives.
void __stdcall CNetworkKeepAlive::BackgroundSleep(DWORD dwMilliseconds)
{
    if (!IsServiceable())
    {
        (*ms_ppfnSleep)(dwMilliseconds);
        return;
    }

    if (!ms_bInBackground)
    {
        ms_bInBackground = true;
        ms_nBackgroundSince = GetTickCount();
        ms_nDroppedPackets = 0;
        logger::info("[KeepAlive] the game is in the background, keeping the connection alive");
        SendKeysReleased();
    }

    const enet_uint32 start = enet_time_get();
    ENetEvent event{};
    while (IsServiceable())
    {
        const enet_uint32 elapsed = enet_time_get() - start;
        if (elapsed > 2 * dwMilliseconds)
        {
            return;  // a burst of traffic: let WinMain handle its window messages, the next call takes the rest
        }

        const int result =
            enet_host_service(CNetwork::m_pENetHost, &event, elapsed < dwMilliseconds ? dwMilliseconds - elapsed : 0);

        if (result > 0)
        {
            OnBackgroundEvent(event);
            continue;
        }

        if (result == 0)  // the wait is over and nothing is pending
        {
            return;
        }

        break;  // socket error, as in CNetwork::ProcessReceive: try again on the next loop
    }

    // disconnected or a socket error: sleep what is left, like the original code
    const enet_uint32 elapsed = enet_time_get() - start;
    if (elapsed < dwMilliseconds)
    {
        (*ms_ppfnSleep)(dwMilliseconds - elapsed);
    }
}

// The other games replay a player's last keys in every frame until the next update arrives (CPlayerPed__ProcessControl_Hook
// in Hooks/PlayerHooks.cpp, CVehicle__ProcessControl_Hook in Hooks/VehicleHooks.cpp), and none arrives while this game
// is in the background: a key held at that moment (walking, accelerating, aiming, firing) went on for the whole time.
// So the player's update goes out once more in the form of its current state, built by the frame loop's own senders,
// with every key released (old and new state, so the others see no press or release edge either). It runs on the main
// thread like those senders (WinMain's loop calls BackgroundSleep between frames), only queues packets on the peer
// (enet_peer_send) that the enet_host_service below sends, and touches nothing of the packets kept for the next frame.
// It carries the last frame's server time, so the others handle it after that frame's updates. Snapshots are
// unreliable, so the key event's packet (Hooks/GameHooks.cpp) goes with it as a second carrier of the released keys.
// The first update after the game is back carries the keys again.
void CNetworkKeepAlive::SendKeysReleased()
{
    // the frame loop's senders wait for the same (Main.cpp)
    if (!CNetwork::m_bAuthenticated)
    {
        return;
    }

    CPlayerPed* pPlayerPed = FindPlayerPed(0);
    if (pPlayerPed == nullptr)
    {
        return;
    }

    CKeySync::CReleasedKeysScope releasedKeys;

    // the frame loop's choice between the driver, passenger and on-foot updates (Main.cpp)
    CVehicle* pVehicle = pPlayerPed->m_nPedFlags.bInVehicle ? pPlayerPed->m_pVehicle : nullptr;
    if (pVehicle != nullptr && pVehicle->m_pDriver == pPlayerPed)
    {
        CNetworkVehicleManager::UpdateDriver(pVehicle);
    }
    else if (pVehicle != nullptr)
    {
        CNetworkVehicleManager::UpdatePassenger(pVehicle, pPlayerPed);
    }
    else
    {
        SendLocalOnFootUpdate(pPlayerPed);
    }

    Packets::Players::KeyPressed keyPressed{};
    CKeySync::CollectState(keyPressed.keySnapshot);
    keyPressed.currentRotation = pPlayerPed->m_fCurrentRotation;
    keyPressed.aimingRotation = pPlayerPed->m_fAimingRotation;
    GetPacketFactory().Send(keyPressed);
}

// Whether a packet that arrives in the background is kept for the first frame after the game is back.
bool CNetworkKeepAlive::ShouldKeep(const ENetEvent& event)
{
    // Unreliable snapshots (positions, keys, camera): the protocol already treats each of them as droppable and the
    // senders repeat them every 50-333 ms while anything changes, so the state is current again right after resume.
    if (event.channelID == static_cast<enet_uint8>(ePacketChannel::SYNC))
    {
        return false;
    }

    // A clock sample answered while the game was away. Handled after resume, CServerTime would pair it with the
    // request of the first frame back and pull the server time offset back by a tenth of the pause.
    if (event.channelID == static_cast<enet_uint8>(ePacketChannel::SYSTEM))
    {
        // the same header read as CPacketFactory::Receive
        serialize::ReadStream readStream(event.packet->data, static_cast<int>(event.packet->dataLength));
        uint32_t packetType = 0;
        if (readStream.SerializeBits(packetType, 16) &&
            static_cast<ePacketType>(packetType) == ePacketType::SERVER_TIME_REQUEST)
        {
            return false;
        }
    }

    return true;
}

void CNetworkKeepAlive::OnBackgroundEvent(ENetEvent& event)
{
    switch (event.type)
    {
        case ENET_EVENT_TYPE_RECEIVE:
        {
            if (!ShouldKeep(event))
            {
                ms_nDroppedPackets++;
                enet_packet_destroy(event.packet);
                break;
            }

            const size_t nPacketBytes = event.packet->dataLength + KEPT_PACKET_OVERHEAD;
            if (ms_nKeptBytes + nPacketBytes > MAX_KEPT_BYTES)
            {
                enet_packet_destroy(event.packet);
                logger::warn("[KeepAlive] %u packets kept in the background, closing the session", ms_nKeptPackets);
                CChat::AddMessage("{cecedb}[Network] Too much happened while the game was in the background.");
                FreeKeptPackets();
                CNetwork::Disconnect();
                break;
            }

            ms_keptPackets.push_back(event.packet);
            ms_nKeptBytes += nPacketBytes;
            ms_nKeptPackets++;
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
        {
            // the session is over, as in CNetwork::ProcessReceive; Main.cpp reports it in the next frame
            logger::info("[KeepAlive] disconnected while the game was in the background");
            FreeKeptPackets();
            CNetwork::Disconnect();
            break;
        }
        case ENET_EVENT_TYPE_NONE:
        case ENET_EVENT_TYPE_CONNECT:
            break;
    }
}

void CNetworkKeepAlive::ProcessDeferred()
{
    if (!ms_bInBackground)
    {
        return;
    }
    ms_bInBackground = false;

    logger::info("[KeepAlive] back after %u s in the background, %u packets kept, %u sync packets dropped",
        (GetTickCount() - ms_nBackgroundSince) / 1000, ms_nKeptPackets, ms_nDroppedPackets);

    if (ms_keptPackets.empty())
    {
        return;
    }

    // The clock request of this frame (CServerTime::Process runs first) is flushed only after the hand-over below, so
    // its answer would count the hand-over as network delay. CServerTime skips an answer while m_requestTick is 0; the
    // next request goes out within 500 ms, and the offset from before the pause stays valid until then because both
    // clocks kept running.
    CServerTime::m_requestTick = 0;

    while (!ms_keptPackets.empty())
    {
        ENetPacket* pPacket = ms_keptPackets.front();
        ms_keptPackets.pop_front();

        // a handler may close the session (nickname taken, version mismatch); like ProcessReceive, stop handling then
        if (CNetwork::m_bConnected)
        {
            GetPacketFactory().Receive(pPacket->data, static_cast<int>(pPacket->dataLength));
        }

        enet_packet_destroy(pPacket);
    }

    ms_nKeptBytes = 0;
    ms_nKeptPackets = 0;
}

void CNetworkKeepAlive::FreeKeptPackets()
{
    for (ENetPacket* pPacket : ms_keptPackets)
    {
        enet_packet_destroy(pPacket);
    }
    ms_keptPackets.clear();
    ms_nKeptBytes = 0;
    ms_nKeptPackets = 0;
    ms_bInBackground = false;
}
