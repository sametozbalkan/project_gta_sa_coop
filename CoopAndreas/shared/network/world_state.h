#pragma once

#include <cstdint>

// Private build: the wire format of the world-state registry (client/src/CWorldStateRegistry.h), shared by the
// WORLD_STATE packet (network/packets/world.h) and the registry's policy (client/src/CWorldStatePolicy.h), which has no
// game types so that tests/world_state_test.cpp can check it.
//
// A WORLD_STATE packet carries one entry's state from the host. Its data starts with the layout header of the entry's
// blob, little-endian:
//    0  uint16  record count (0 for an entry that holds one value instead of records)
//    2  uint32  layout hash: FNV-1a 32 of what identifies the records in index order (names, labels), or of the
//               value's format for an entry without records
// FULL:    the header, then the entry's payload (record count x record size bytes for an entry with records)
// RECORDS: the header, then {uint16 record index, record size bytes} for each record that changed
namespace WorldStateWire
{
constexpr uint8_t KIND_FULL = 0;
constexpr uint8_t KIND_RECORDS = 1;

constexpr int LAYOUT_HEADER_SIZE = 6;

// The most data one packet carries: the client and the server serialize packets into 10 KB buffers (CPacketFactory.cpp
// on both sides). An entry whose FULL blob ever needs more has to split it into numbered parts that the other games use
// once all have arrived (docs/design/world-state.md 3.5); no entry does yet, and the registry refuses to send a bigger
// one (logged once per entry).
constexpr int MAX_DATA_SIZE = 8000;
}  // namespace WorldStateWire
