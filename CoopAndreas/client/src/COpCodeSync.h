#pragma once

#define NUM_SYNCED_PARAMS (15)

enum class eSyncedParamType
{
    NONE,

    // any `CPed` (aka actor, character), examples: `$ACTOR_SMOKE`
    PED,

    // `$PLAYER_CHAR` (internal playerId for the `CWorld::Players` array)
    // also used as `CPed` in cases where it is necessary to handle 
    // a player as a ped rather than a player (very often, e.g. `$PLAYER_ACTOR`)
    PLAYER,

    // any `CVehicle` (aka car)
    VEHICLE,

    // not in use yet
    OBJECT,

    MAX_PARAMS
};

struct SSyncedOpCode
{
    uint16_t m_wOpCode;

    // means the opcode has parameters that are hard to sync, 
    // like entities from the `eSyncedParamType`
    bool m_bHasComplexParams = false;

    // yes, only the first 4 parameters contain entity handles, 
    // i checked and did not find any opcode where a handle is in 
    // the 5th or later parameters
    eSyncedParamType m_aParamTypes[4] = { eSyncedParamType::NONE, eSyncedParamType::NONE, eSyncedParamType::NONE, eSyncedParamType::NONE };
};

struct OpcodeSyncHeader
{
    uint16_t opcode;
    uint8_t intParamCount : 4;
    uint8_t stringParamCount : 4;
};

struct OpcodeParameter
{
    union
    {
        struct
        {
            eSyncedParamType entityType : 4;
            int entityId : 28;
        };
        int value;
    };
};

class COpCodeSync
{
public:
	static inline bool ms_bSyncingEnabled = true;
	static inline bool ms_bLoadingCutscene = true;
	static inline bool ms_abLoadingMissionAudio[4];
	
    static inline uint32_t ms_iFreeSyncedScript = 0;
    static inline char ms_aszSyncedScripts[256][8];

    static inline OpcodeParameter scriptParamsBuffer[NUM_SYNCED_PARAMS];
    static inline bool ms_bProcessingTaskSequence = false;
    static inline bool bProcessingNetworkOpcode = false;

	static void Init();
	static void HandlePacket(const uint8_t* buffer, int bufferSize);
	// private build: bNameEntities false keeps the script's entity handles (CTaskSequenceSync names them when the sequence
	// is performed, SerializeStoredOpcode)
	static std::vector<uint8_t> COpCodeSync::SerializeOpcode(int idx, int& outSize, bool bNameEntities = true);
	// private build: a command stored with SerializeOpcode(idx, size, false), serialized with its entities named now; false
	// when one of them has no network id (ReplayEntityPolicy::IsUnnamed): that task is left out
	static bool SerializeStoredOpcode(const std::vector<uint8_t>& stored, std::vector<uint8_t>& out);
	static bool COpCodeSync::IsOpcodeSyncable(int opcode, int* opcodeIdx = nullptr, bool ignoreOpCodeSync = false, bool* pbMirrorOnly = nullptr);

	// private build: the script whose command CRunningScript::Process runs (the 0x469FF3 redirect sets it around the
	// command handler and the post-command processing), nullptr otherwise; commands run with
	// CRunningScript::ProcessOneCommand (plugin-sdk's Command<>) and network replays don't set it. For hooks inside
	// command handlers (the mission mirror layer's locate spheres).
	static CRunningScript* GetScriptRunningCommand();
};