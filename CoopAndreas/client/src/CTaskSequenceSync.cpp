#include "stdafx.h"
#include "CTaskSequenceSync.h"
#include <game_sa/CTaskSequences.h>
#include <COpCodeSync.h>

// TODO: SET_SEQUENCE_TO_REPEAT

SSyncedOpCode m_syncedTasks[] = // TODO
{
	{COMMAND_TASK_PLAY_ANIM_NON_INTERRUPTABLE, true, eSyncedParamType::PED},
	{COMMAND_TASK_GO_STRAIGHT_TO_COORD, true, eSyncedParamType::PED},
	{COMMAND_TASK_ACHIEVE_HEADING, true, eSyncedParamType::PED},
	{COMMAND_TASK_JUMP, true, eSyncedParamType::PED},
	{COMMAND_TASK_TIRED, true, eSyncedParamType::PED},
	{COMMAND_TASK_PLAY_ANIM_SECONDARY, true, eSyncedParamType::PED},
	{COMMAND_TASK_CHAR_SLIDE_TO_COORD_AND_PLAY_ANIM, true, eSyncedParamType::PED},
	{COMMAND_TASK_PLAY_ANIM, true, eSyncedParamType::PED},
	{COMMAND_TASK_TURN_CHAR_TO_FACE_CHAR, true, eSyncedParamType::PED, eSyncedParamType::PED},
	{COMMAND_TASK_SHOOT_AT_COORD, true, eSyncedParamType::PED},
	{COMMAND_TASK_LOOK_AT_CHAR, true, eSyncedParamType::PED, eSyncedParamType::PED},
	{COMMAND_CLEAR_CHAR_TASKS, true, eSyncedParamType::PED},
	{COMMAND_CLEAR_LOOK_AT, true, eSyncedParamType::PED},
	{COMMAND_TASK_STAND_STILL, true, eSyncedParamType::PED},
	{COMMAND_TASK_LOOK_AT_COORD, true, eSyncedParamType::PED},
	{COMMAND_TASK_LEAVE_CAR, true, eSyncedParamType::PED, eSyncedParamType::VEHICLE},
	{COMMAND_TASK_DIE_NAMED_ANIM, true, eSyncedParamType::PED},
	{COMMAND_TASK_PLAY_ANIM_WITH_FLAGS, true, eSyncedParamType::PED},
	{COMMAND_TASK_LEAVE_ANY_CAR, true, eSyncedParamType::PED},
	{COMMAND_TASK_LOOK_ABOUT, true, eSyncedParamType::PED},
	{COMMAND_TASK_WANDER_STANDARD, true, eSyncedParamType::PED},
};

std::vector<uint8_t> m_serializedSequences[CTaskSequences::NUM_SEQUENCES][CTaskSequences::NUM_TASKS];
int m_sequenceTaskCount = 0;
bool m_bSequenceOpened = false;

// private build: whether the game's sequence in this slot holds a task. CTaskSequences::ms_taskSequence is 0xC178F0, 64
// CTaskComplexSequence of 0x40 (OPEN_SEQUENCE_TASK case 0x49186F: shl ecx,6; add ecx,0C178F0h); a sequence's 8 tasks start
// at +0x10 (CTaskComplexSequence::AddTask 0x632D10, Flush 0x632C10; gta-reversed TaskComplexSequence.h, m_Tasks)
static bool SequenceHasTasks(int slot)
{
	if (slot < 0 || slot >= CTaskSequences::NUM_SEQUENCES)
		return false;
	return *reinterpret_cast<void* const*>(0xC178F0 + slot * 0x40 + 0x10) != nullptr;
}

bool CTaskSequenceSync::IsOpCodeTaskSynced(eScriptCommands opcode)
{
	for (int i = 0; i < ARRAY_SIZE(m_syncedTasks); i++)
	{
		if (m_syncedTasks[i].m_wOpCode == opcode)
		{
			return true;
		}
	}
	return false;
}

bool CTaskSequenceSync::IsNeededToCollectParametes(eScriptCommands opcode)
{
	return !COpCodeSync::bProcessingNetworkOpcode && (
		opcode == COMMAND_PERFORM_SEQUENCE_TASK ||
		opcode == COMMAND_CLEAR_SEQUENCE_TASK ||
		(IsOpCodeTaskSynced(opcode) && m_bSequenceOpened));
}

void OpenSequence()
{
	m_sequenceTaskCount = 0;
	m_bSequenceOpened   = true;

	int sequenceId = CTaskSequences::ms_iActiveSequence;

	// private build: no sequence was free (OPEN_SEQUENCE_TASK, case 0x49186F, left none active)
	if (sequenceId < 0 || sequenceId >= CTaskSequences::NUM_SEQUENCES)
	{
		m_bSequenceOpened = false;
		return;
	}

	for (int i = 0; i < CTaskSequences::NUM_TASKS; ++i) 
	{
		m_serializedSequences[sequenceId][i].clear();
	}
}

void CloseSequence()
{
	m_sequenceTaskCount = 0;
	m_bSequenceOpened   = false;
}

void ClearSequence()
{
	int sequenceId = CTheScripts::GetActualScriptThingIndex(COpCodeSync::scriptParamsBuffer[0].value, SCRIPT_THING_SEQUENCE_TASK);

	// private build: this runs after CLEAR_SEQUENCE_TASK (case 0x491A4A), which marks the sequence's script thing unused
	// (0x491A95), so the handle gives -1 here and the loop below wrote before m_serializedSequences on every
	// Sequence.Clear. OpenSequence empties a sequence's tasks when its slot is opened again.
	if (sequenceId < 0 || sequenceId >= CTaskSequences::NUM_SEQUENCES)
	{
		return;
	}

	for (int i = 0; i < CTaskSequences::NUM_TASKS; ++i)
	{
		m_serializedSequences[sequenceId][i].clear();
	}
}

void PerformSequence()
{
	int pedHandle = COpCodeSync::scriptParamsBuffer[0].value;
	// private build: read before COpCodeSync::SerializeStoredOpcode below reuses the command state
	const int sequenceHandle = COpCodeSync::scriptParamsBuffer[1].value;
	auto ped = CPools::GetPed(pedHandle);
	
	if (!ped)
		return;

	int pedid = -1;

	if (!ped->IsPlayer())
	{
		if (auto networkPed = CNetworkPedManager::GetPed(ped))
		{
			pedid = networkPed->m_nPedId;
		}

		if (pedid == -1) return;
	}

	int sequenceId = CTheScripts::GetActualScriptThingIndex(sequenceHandle, SCRIPT_THING_SEQUENCE_TASK);

	// private build: an unknown handle gives -1 (PERFORM_SEQUENCE_TASK, case 0x49194B, checks it too)
	if (sequenceId < 0 || sequenceId >= CTaskSequences::NUM_SEQUENCES)
		return;

	/*
	packet structure:
	// header:
	int playerid
	int pedid
	uint8_t taskCount
	// for each task:
	uint8_t size
	uint8_t serializedData[size]
	*/

	int packetSize = 0;
	uint8_t taskCount = 0;

	// private build: the tasks were stored with the script's handles (AddNewTask) and their entities are named now
	// (COpCodeSync::SerializeStoredOpcode): Drive-By creates its Voodoos and defines the Task.LeaveCar sequences in one
	// frame, before VEHICLE_CONFIRM, and the others built CTaskComplexLeaveCar with no vehicle from the -1 that went out.
	// A task whose entity still has no network id (or none any more) is left out (ReplayEntityPolicy::IsUnnamed).
	std::vector<uint8_t> namedTasks[CTaskSequences::NUM_TASKS];

	for (int i = 0; i < CTaskSequences::NUM_TASKS; i++)
	{
		if (m_serializedSequences[sequenceId][i].empty())
			break;

		if (!COpCodeSync::SerializeStoredOpcode(m_serializedSequences[sequenceId][i], namedTasks[taskCount]))
		{
			uint16_t opcode = 0;
			memcpy(&opcode, m_serializedSequences[sequenceId][i].data(), sizeof(opcode));
			CNetworkPedManager::LogEvent("sequence-task-left-out", pedid, "opcode=%04X task=%d", opcode, i);
			continue;
		}

		packetSize += sizeof(uint8_t) + namedTasks[taskCount].size(); // size + task
		taskCount++;
	}

	if (taskCount == 0)
		return;

	packetSize += sizeof(int) + sizeof(int) + sizeof(uint8_t); // playerid + pedid + taskCount

	std::vector<uint8_t> vBuffer;
	vBuffer.clear();
	vBuffer.reserve(packetSize);
	
	int playerid = -1; // let the server fill this variable
	vBuffer.insert(vBuffer.end(), (uint8_t*)&playerid, (uint8_t*)&playerid + sizeof(int));

	vBuffer.insert(vBuffer.end(), (uint8_t*)&pedid, (uint8_t*)&pedid + sizeof(int)); // if pedid == -1 then perfom the sequence on the player

	vBuffer.push_back(taskCount);

	for (int i = 0; i < taskCount; i++)
	{
		// private build: the named tasks
		vBuffer.push_back((uint8_t)namedTasks[i].size());
		vBuffer.insert(vBuffer.end(), namedTasks[i].begin(), namedTasks[i].end());
	}

	static Packets::Scripts::PerformTaskSequence packet{};
	packet.size = packetSize;
	memcpy(packet.buffer, vBuffer.data(), packetSize);
	GetPacketFactory().Send(packet);
}

void AddNewTask(eScriptCommands opcode)
{
	int sequenceId = CTaskSequences::ms_iActiveSequence;
	int idx = -1;
	COpCodeSync::IsOpcodeSyncable(opcode, &idx, true);

	assert(idx != -1 && "an opcode exists in 'm_serializedSequences' but not found in 'syncedOpcodes'");

	// private build: a sequence holds 8 tasks (CTaskComplexSequence::AddTask 0x632D10 deletes a ninth)
	if (m_sequenceTaskCount >= CTaskSequences::NUM_TASKS)
		return;

	int dataSize = 0;
	// private build: with the script's handles; PerformSequence names the entities (COpCodeSync::SerializeStoredOpcode)
	m_serializedSequences[sequenceId][m_sequenceTaskCount] = COpCodeSync::SerializeOpcode(idx, dataSize, false);

	m_sequenceTaskCount++;
}


bool CTaskSequenceSync::OnOpCodeExecuted(eScriptCommands opcode)
{
	if (opcode == COMMAND_OPEN_SEQUENCE_TASK)
	{
		//CChat::AddMessage("OpenSequence");
		OpenSequence();
		return false;
	}

	if (opcode == COMMAND_CLOSE_SEQUENCE_TASK)
	{
		//CChat::AddMessage("CloseSequence");
		CloseSequence();
		return false;
	}

	if (opcode == COMMAND_CLEAR_SEQUENCE_TASK)
	{
		//CChat::AddMessage("ClearSequence");
		ClearSequence();
		return false;
	}

	if (opcode == COMMAND_PERFORM_SEQUENCE_TASK)
	{
		//CChat::AddMessage("PerformSequence");
		PerformSequence();
		return false;
	}

	if (CTaskSequences::ms_iActiveSequence != -1 
		&& m_bSequenceOpened 
		&& IsOpCodeTaskSynced(opcode) 
		&& COpCodeSync::scriptParamsBuffer[0].value == -1)
	{
		//CChat::AddMessage("AddNewTask");
		AddNewTask(opcode);
		return false;
	}

	return true;
}

void CTaskSequenceSync::HandlePacket(void* pData, int size)
{
	if (size < 9)
    {
        return;
    }

	int playerid = *(int*)pData;
	int pedid = *(int*)((int)pData + 4);
	uint8_t count = *(uint8_t*)((int)pData + 8);

	if (count == 0) return;

	// private build: the ped is looked up before a sequence is opened (returning without a ped left it open), and a
	// network ped or player gets the sequence only while it has a clump and intelligence
	CPed* ped = nullptr;

	if (pedid == -1)
	{
		if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(playerid))
		{
			ped = networkPlayer->GetReadyPed();
		}
	}
	else
	{
		if (auto networkPed = CNetworkPedManager::GetPed(pedid))
		{
			ped = networkPed->IsModelReady() ? networkPed->GetLivePed() : nullptr;
		}
	}

	if (ped == nullptr)
	{
		if (playerid != CNetworkPlayerManager::m_nMyId)
		{
			return;
		}
		ped = FindPlayerPed(0);
	}

	int sequenceId = -1;
	Command<Commands::OPEN_SEQUENCE_TASK>(&sequenceId);

	if (sequenceId == -1)
	{
		//CChat::AddMessage("CTaskSequenceSync::HandlePacket: can't open a new sequence");
		return;
	}

	// private build: the sequence slot OPEN_SEQUENCE_TASK opened (case 0x49186F stores it in ms_iActiveSequence, 0x4918B4)
	const int slot = CTaskSequences::ms_iActiveSequence;

	CTaskSequenceSync::ms_bFailedToProcessSequence = false;

	int ptr = 9;
	for (int i = 0; i < count; i++)
	{
		uint8_t len = *(uint8_t*)((int)pData + ptr);
		ptr++;

		COpCodeSync::ms_bProcessingTaskSequence = true;
		// private build: a task naming an entity this game doesn't have is left out (ReplayEntityPolicy::MustDrop)
		COpCodeSync::HandlePacket((uint8_t*)((int)pData + ptr), len);

		if (CTaskSequenceSync::ms_bFailedToProcessSequence == true)
		{
			// private build: it stayed set, and the replays until the retry took a sequence's rules (a ped -1 outside a
			// sequence, a missing entity replayed as none)
			COpCodeSync::ms_bProcessingTaskSequence = false;
			if (pData && size > 0)
			{
				ms_vTaskSequences.emplace_back((uint8_t*)pData, (uint8_t*)pData + size);
			}
			Command<Commands::CLOSE_SEQUENCE_TASK>(sequenceId);
			Command<Commands::CLEAR_SEQUENCE_TASK>(sequenceId);
			return;
		}

		COpCodeSync::ms_bProcessingTaskSequence = false;


		ptr += len;
	}

	Command<Commands::CLOSE_SEQUENCE_TASK>(sequenceId);
	// private build: when every task was left out the ped keeps what it does (performing an empty sequence would replace its
	// primary task)
	if (SequenceHasTasks(slot))
	{
		Command<Commands::PERFORM_SEQUENCE_TASK>(CPools::GetPedRef(ped), sequenceId);
	}
	Command<Commands::CLEAR_SEQUENCE_TASK>(sequenceId);
}