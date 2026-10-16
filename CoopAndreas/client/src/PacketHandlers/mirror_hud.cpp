#include "network/packets/mirror_hud.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMirrorHud.h>

// private build: mission mirror layer, the host's on-screen timers and counters (CMirrorHud.h)
PACKET_HANDLER(ePacketType::MISSION_MIRROR_HUD, Packets::Mirror::MirrorHud* pMirrorHud)
{
    CMirrorHud::HandlePacket(*pMirrorHud);
}
