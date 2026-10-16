#include "network/packets/mirror_areas.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMirrorAreas.h>

// private build: the areas of the host's mission (mission mirror layer, CMirrorAreas.h)
PACKET_HANDLER(ePacketType::MISSION_MIRROR_AREAS, Packets::Mirror::MirrorAreas* pMirrorAreas)
{
    CMirrorAreas::HandlePacket(*pMirrorAreas);
}
