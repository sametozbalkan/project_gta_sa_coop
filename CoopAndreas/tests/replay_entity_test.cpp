// Private build: policy test for the entity parameters of replayed script commands and the leave-car safety net
// (client/src/CReplayEntityPolicy.h) without GTA. Build and run: see CLAUDE.md (Fork changes); no include path needed.
#include "../client/src/CReplayEntityPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace ReplayEntityPolicy;

namespace
{
// COpCodeSync.h's OpcodeParameter: a network entity is {type in the low 4 bits, id in the high 28}; -1 is "no entity"
enum class eType { NONE, PED, PLAYER, VEHICLE, OBJECT };
struct EntityBits
{
    eType type : 4;
    int id : 28;
};
static_assert(sizeof(EntityBits) == sizeof(int), "an entity parameter is one int on the wire");

int Encode(eType type, int id)
{
    EntityBits bits{};
    bits.type = type;
    bits.id = id;
    int value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void TestEncodingNeverReadsAsNone()
{
    // a named entity (PED, PLAYER or VEHICLE with id 0 and up) never reads as -1, so -1 on the wire is always "no entity"
    for (int id = 0; id < 4096; id++)
    {
        CHECK(Encode(eType::PED, id) != -1);
        CHECK(Encode(eType::PLAYER, id) != -1);
        CHECK(Encode(eType::VEHICLE, id) != -1);
    }
    CHECK(Encode(eType::VEHICLE, (1 << 27) - 1) != -1);
}

void TestSenderUnnamed()
{
    constexpr int VOODOO_HANDLE = 0x1F05;  // a script handle (pool index << 8 | generation)
    // the script gave no entity: -1 goes out as "no entity"
    CHECK(!IsUnnamed(-1, -1));
    // a named entity with a network id
    CHECK(!IsUnnamed(VOODOO_HANDLE, Encode(eType::VEHICLE, 183)));
    CHECK(!IsUnnamed(0, Encode(eType::PED, 0)));  // handle 0 is a valid handle
    // a car created in the frame the sequence is defined, before VEHICLE_CONFIRM: SerializeOpcode leaves -1
    CHECK(IsUnnamed(VOODOO_HANDLE, -1));
    CHECK(IsUnnamed(0, -1));
}

void TestReceiverDrop()
{
    constexpr int HANDLE = 0x2A07;
    const int voodoo = Encode(eType::VEHICLE, 183);
    const int ballas = Encode(eType::PED, 201);

    // resolved on this game: replayed, in a sequence or not, whatever the parameter
    CHECK(!MustDrop(voodoo, HANDLE, 1, true));
    CHECK(!MustDrop(voodoo, HANDLE, 1, false));
    CHECK(!MustDrop(ballas, HANDLE, 0, false));

    // Drive-By: Task.LeaveCar(-1, voodoo) inside a sequence whose car this game doesn't have: the task is left out (the
    // game would build CTaskComplexLeaveCar with no vehicle, which crashed at 0x642067)
    CHECK(MustDrop(voodoo, -1, 1, true));
    // the same outside a sequence (TASK_DRIVE_BY, the only non-strict single command): dropped, not a coordinate drive-by
    CHECK(MustDrop(voodoo, -1, 2, false));
    CHECK(MustDrop(ballas, -1, 1, false));
    // the ped a task is for, named but missing
    CHECK(MustDrop(ballas, -1, 0, false));
    CHECK(MustDrop(ballas, -1, 0, true));

    // -1 from the sender: the ped placeholder of a sequence's task stays
    CHECK(!MustDrop(-1, -1, 0, true));
    // TASK_DRIVE_BY without a target ped or car: stays "no entity"
    CHECK(!MustDrop(-1, -1, 1, false));
    CHECK(!MustDrop(-1, -1, 2, false));
    CHECK(!MustDrop(-1, -1, 1, true));
    // the ped a task is for is never -1 outside a sequence (the game would add the task to a sequence nobody opened)
    CHECK(MustDrop(-1, -1, 0, false));
}

void TestLeaveCar()
{
    // the task has its vehicle: the game's own code, first or next subtask
    CHECK(DecideLeaveCar(true, true, true, true) == eLeaveCar::ORIGINAL);
    CHECK(DecideLeaveCar(true, false, false, false) == eLeaveCar::ORIGINAL);
    // no vehicle, the ped sits in a car: it leaves that car (the Drive-By Balla in its Voodoo)
    CHECK(DecideLeaveCar(false, true, true, true) == eLeaveCar::TAKE_PED_VEHICLE);
    CHECK(DecideLeaveCar(false, true, true, false) == eLeaveCar::TAKE_PED_VEHICLE);
    // no vehicle, the ped on foot: the start takes the game's "ped not in car" path, a later subtask ends the task
    CHECK(DecideLeaveCar(false, false, false, true) == eLeaveCar::ORIGINAL);
    CHECK(DecideLeaveCar(false, false, true, true) == eLeaveCar::ORIGINAL);
    CHECK(DecideLeaveCar(false, false, false, false) == eLeaveCar::NO_SUBTASK);
    // in a car without a vehicle pointer: the game's code would read the missing vehicle
    CHECK(DecideLeaveCar(false, true, false, true) == eLeaveCar::NO_SUBTASK);
    CHECK(DecideLeaveCar(false, true, false, false) == eLeaveCar::NO_SUBTASK);
}
}  // namespace

int main()
{
    TestEncodingNeverReadsAsNone();
    TestSenderUnnamed();
    TestReceiverDrop();
    TestLeaveCar();
    std::printf("replay_entity_test: ok\n");
    return 0;
}
