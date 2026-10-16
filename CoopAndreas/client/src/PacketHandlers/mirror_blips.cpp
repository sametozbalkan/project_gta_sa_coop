#include "network/packets/mirror_blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMirrorBlips.h>

// private build: the mission mirror layer's blips from the host (CMirrorBlips.h)
PACKET_HANDLER(ePacketType::MISSION_MIRROR_BLIPS, Packets::Mirror::MirrorBlips* pMirrorBlips)
{
    CMirrorBlips::HandlePacket(*pMirrorBlips);
}
