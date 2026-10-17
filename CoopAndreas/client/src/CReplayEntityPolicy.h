#pragma once

// Private build: the rules for the entity parameters of replayed script commands (COpCodeSync, CTaskSequenceSync) and for a
// CTaskComplexLeaveCar that has no vehicle (Hooks/TaskHooks.cpp), without GTA, so tests/replay_entity_test.cpp checks
// them. On the wire a PED or VEHICLE parameter carries a network id, or -1 for "no entity": the ped of a task inside a
// sequence, TASK_DRIVE_BY without a target ped or car.
namespace ReplayEntityPolicy
{
// Sender: SerializeOpcode left a parameter at -1 although the script named an entity (scriptValue isn't -1). The entity has
// no network id: not yet (a mission creates the car a task names in the frame it defines the sequence, before
// VEHICLE_CONFIRM; Drive-By's Voodoos), or not any more. Such a command doesn't go out and such a task of a sequence is
// left out, because the others would replay the -1 as "no entity".
constexpr bool IsUnnamed(int scriptValue, int sentValue)
{
    return scriptValue != -1 && sentValue == -1;
}

// Receiver: after COpCodeSync::HandlePacket resolved a PED or VEHICLE parameter (resolvedValue: this game's handle, or -1).
// The command is dropped (a task of a sequence is left out) when the sender named an entity this game doesn't have, and
// when the ped a task is for (parameter 0) is -1 outside a sequence: the game adds a task for ped -1 to the sequence being
// built, and none is open then. A -1 the sender sent stays "no entity" where the receiver allows it.
constexpr bool MustDrop(int sentValue, int resolvedValue, int index, bool bInSequence)
{
    if (resolvedValue != -1)
    {
        return false;
    }
    if (sentValue != -1)
    {
        return true;
    }
    return index == 0 && !bInSequence;
}

// A CTaskComplexLeaveCar creating its first or next subtask while it has no vehicle (never given one, or its vehicle was
// deleted and the registered reference cleared)
enum class eLeaveCar
{
    ORIGINAL,          // the game's own code: the task has its vehicle, or the ped is on foot at the task's start
    TAKE_PED_VEHICLE,  // the task takes the car the ped sits in, then the game's own code
    NO_SUBTASK,        // no subtask: the task ends
};

// bFirstSubTask: CreateFirstSubTask, whose "ped not in car" path reads no vehicle; CreateNextSubTask's branches read it
constexpr eLeaveCar DecideLeaveCar(bool bHasVehicle, bool bPedInVehicle, bool bPedHasVehicle, bool bFirstSubTask)
{
    if (bHasVehicle)
    {
        return eLeaveCar::ORIGINAL;
    }
    if (bPedInVehicle && bPedHasVehicle)
    {
        return eLeaveCar::TAKE_PED_VEHICLE;
    }
    if (bFirstSubTask && !bPedInVehicle)
    {
        return eLeaveCar::ORIGINAL;
    }
    return eLeaveCar::NO_SUBTASK;
}
}  // namespace ReplayEntityPolicy
