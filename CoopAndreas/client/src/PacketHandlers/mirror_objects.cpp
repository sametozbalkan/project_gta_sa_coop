#include "network/packets/mirror_objects.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMirrorObjects.h>

// private build: the mission object mirror's records from the host and motion from an object's owner (CMirrorObjects.h)
PACKET_HANDLER(ePacketType::MISSION_OBJECTS, Packets::Mirror::MirrorObjects* pMirrorObjects)
{
    CMirrorObjects::HandlePacket(*pMirrorObjects);
}

PACKET_HANDLER(ePacketType::MISSION_OBJECT_MOTION, Packets::Mirror::MirrorObjectMotion* pMotion)
{
    CMirrorObjects::HandleMotion(*pMotion);
}
