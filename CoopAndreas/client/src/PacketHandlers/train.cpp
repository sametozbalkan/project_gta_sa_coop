#include "network/packets/train.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CTrainSync.h"

// private build: train mirror state and event handlers (client/src/CTrainSync.h; docs/design/trains.md §5, §7)
PACKET_HANDLER(ePacketType::TRAIN_STATE, Packets::Train::TrainState* pTrainState)
{
    CTrainSync::HandleTrainState(*pTrainState);
}

PACKET_HANDLER(ePacketType::TRAIN_EVENT, Packets::Train::TrainEvent* pTrainEvent)
{
    CTrainSync::HandleTrainEvent(*pTrainEvent);
}

PACKET_HANDLER(ePacketType::ENTITY_ATTACH, Packets::Train::EntityAttach* pEntityAttach)
{
    CTrainSync::HandleEntityAttach(*pEntityAttach);
}
