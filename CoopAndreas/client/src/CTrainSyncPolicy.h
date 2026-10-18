#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Private build: pure train mirror arithmetic and state decision logic, with no game or plugin-sdk types so
// tests/train_sync_test.cpp can exercise it directly (docs/design/trains.md §5, §7).
namespace TrainSyncPolicy
{
constexpr float SNAP_DISTANCE = 25.0f;       // metres error above which we snap instead of smoothing
constexpr float JUMP_THRESHOLD = 5.0f;       // metres divergence on host to bump jumpCounter
constexpr uint32_t HOLD_MS = 500;            // ms without state before holding display distance
constexpr uint32_t STATE_INTERVAL_MS = 200;  // 5 Hz periodic state broadcast
constexpr float CORRECTION_STEPS = 25.0f;    // ~0.5 s at 50 Hz (25 timeSteps)
constexpr float SPEED_SCALE = 10000.0f;

// Wrap rail distance modulo trackLength into [0, trackLength)
inline float WrapRailDistance(float distance, float trackLength)
{
    if (trackLength <= 0.0f)
    {
        return distance;
    }
    float wrapped = std::fmod(distance, trackLength);
    if (wrapped < 0.0f)
    {
        wrapped += trackLength;
    }
    return wrapped;
}

// Native CTrain::m_fLength is signed by the factory for the train's direction. Never derive it
// from an unsigned model length when reconnecting a retained component.
inline float FollowingCarriageDistance(float previousDistance, float signedLength, float trackLength)
{
    return WrapRailDistance(previousDistance + signedLength, trackLength);
}

// Shortest signed delta (to - from) on a closed loop of length trackLength in [-trackLength/2, +trackLength/2)
inline float ShortestRailDelta(float fromDistance, float toDistance, float trackLength)
{
    if (trackLength <= 0.0f)
    {
        return toDistance - fromDistance;
    }
    float delta = std::fmod(toDistance - fromDistance, trackLength);
    if (delta >= trackLength * 0.5f)
    {
        delta -= trackLength;
    }
    else if (delta < -trackLength * 0.5f)
    {
        delta += trackLength;
    }
    return delta;
}

struct SStepResult
{
    float displayDistance = 0.0f;
    float remainingError = 0.0f;
    float appliedCorrection = 0.0f;
    bool bSnapped = false;
};

// Advance display rail distance by one frame:
// - currentDisplayDist: current display position on the copy
// - targetDist: dead-reckoned target position from the latest host state (hostDist + hostSpeed * elapsedSteps)
// - speedPerStep: host's signed speed (metres per 20ms timeStep)
// - dtSteps: CTimer::ms_fTimeStep (number of 20ms steps this frame)
// - trackLength: total length of the track
// - msSinceLastState: milliseconds since the last TRAIN_STATE arrived
// - bJumpChanged: true when the packet's jumpCounter differs from the previous state's jumpCounter
inline SStepResult StepDisplayRailDistance(float currentDisplayDist, float targetDist, float speedPerStep,
    float dtSteps, float trackLength, uint32_t msSinceLastState, bool bJumpChanged)
{
    SStepResult out{};
    if (trackLength <= 0.0f)
    {
        out.displayDistance = targetDist;
        return out;
    }

    if (msSinceLastState > HOLD_MS && !bJumpChanged)
    {
        out.displayDistance = WrapRailDistance(currentDisplayDist, trackLength);
        out.remainingError = ShortestRailDelta(out.displayDistance, targetDist, trackLength);
        return out;
    }

    const float baseAdvance = speedPerStep * dtSteps;
    const float predicted = WrapRailDistance(currentDisplayDist + baseAdvance, trackLength);
    const float error = ShortestRailDelta(predicted, targetDist, trackLength);

    if (bJumpChanged || std::fabs(error) > SNAP_DISTANCE)
    {
        out.displayDistance = WrapRailDistance(targetDist, trackLength);
        out.remainingError = 0.0f;
        out.appliedCorrection = error;
        out.bSnapped = true;
        return out;
    }

    const float alpha = std::clamp(dtSteps / CORRECTION_STEPS, 0.0f, 1.0f);
    float corr = error * alpha;

    // Never move against the direction of motion when the train is moving
    if (std::fabs(speedPerStep) > 1e-4f)
    {
        const float totalStep = baseAdvance + corr;
        if ((speedPerStep > 0.0f && totalStep < 0.0f) || (speedPerStep < 0.0f && totalStep > 0.0f))
        {
            corr = -baseAdvance;
        }
    }

    out.displayDistance = WrapRailDistance(predicted + corr, trackLength);
    out.appliedCorrection = corr;
    out.remainingError = ShortestRailDelta(out.displayDistance, targetDist, trackLength);
    out.bSnapped = false;
    return out;
}

// Sequence number comparison with uint8 wraparound (returns true if incomingSeq is newer than lastSeq)
inline bool IsSequenceNewer(uint8_t incomingSeq, uint8_t lastSeq, bool bHasLast)
{
    if (!bHasLast)
    {
        return true;
    }
    const uint8_t diff = static_cast<uint8_t>(incomingSeq - lastSeq);
    return diff != 0 && diff < 128;
}

// Incarnations are host-local serial numbers, retained across native game restarts. A state/event must
// match the descriptor exactly; it must never advance a new train's sequence window.
inline bool IsIncarnationNewer(uint32_t incoming, uint32_t previous)
{
    return incoming != 0 && (previous == 0 || (incoming != previous && incoming - previous < 0x80000000u));
}

inline bool AcceptRestartFence(uint32_t incoming, uint32_t current)
{
    return incoming == current || IsIncarnationNewer(incoming, current);
}

inline bool AcceptDescriptor(uint32_t incoming, uint32_t highWater, bool active, uint32_t restartFence)
{
    return IsIncarnationNewer(incoming, restartFence) &&
           (IsIncarnationNewer(incoming, highWater) || (active && incoming == highWater));
}

inline bool AcceptTrainState(uint32_t incoming, uint32_t current, bool active, uint8_t sequence,
    uint8_t previousSequence, bool hasState)
{
    return active && incoming != 0 && incoming == current && IsSequenceNewer(sequence, previousSequence, hasState);
}

inline bool MatchesIncarnation(uint32_t incoming, uint32_t current, bool active)
{
    return active && incoming != 0 && incoming == current;
}

struct SSubchainConfiguration
{
    int config = -1;
    int offset = 0;
    int stockCount = 0;
};

inline bool ValidateConfigurationSubchain(const uint16_t* models, int count, int config, int offset,
    const uint32_t configs[16][16])
{
    if (!models || count < 1 || count > 8 || config < 0 || config >= 16 || offset < 0 || offset + count > 16)
        return false;
    for (int i = 0; i < offset + count; ++i)
        if (!configs[config][i] || (i >= offset && configs[config][i] != models[i - offset]))
            return false;
    return true;
}

// A deleted engine or middle carriage leaves independent, contiguous portions of a stock train.
// Prefer a complete configuration, then a stock prefix, then any interior/suffix portion.
inline SSubchainConfiguration MatchSubchainConfiguration(const uint16_t* models, int count,
    const uint32_t configs[16][16])
{
    SSubchainConfiguration best{};
    int bestRank = 3;
    for (int cfg = 0; cfg < 16; ++cfg)
    {
        int stockCount = 0;
        while (stockCount < 16 && configs[cfg][stockCount]) ++stockCount;
        for (int offset = 0; offset + count <= stockCount; ++offset)
        {
            if (!ValidateConfigurationSubchain(models, count, cfg, offset, configs)) continue;
            const int rank = offset == 0 ? (count == stockCount ? 0 : 1) : 2;
            if (rank < bestRank)
            {
                best = {cfg, offset, stockCount};
                bestRank = rank;
            }
        }
    }
    return best;
}

// The same native carriage may move between train slots after a middle deletion. Network identity,
// model and the captured pool generation must all agree before retaining its binding/occupants.
inline bool CanRetainCarriage(int expectedId, uint16_t expectedModel, int candidateId,
    uint16_t candidateModel, bool poolGenerationMatches)
{
    return expectedId >= 0 && expectedId == candidateId && expectedModel == candidateModel && poolGenerationMatches;
}

// A parked carriage waits for the descriptor of the other portion of a split, which the host sends in the same
// frame as the re-described one. Unclaimed after this long, it would only stand frozen on the track.
constexpr uint32_t HANDOVER_TIMEOUT_MS = 4000;

inline bool IsHandoverExpired(uint32_t parkedAtMs, uint32_t nowMs)
{
    return nowMs - parkedAtMs >= HANDOVER_TIMEOUT_MS;
}

// Host SPAWN order within a frame: the slots re-described after a split go first, then the others in slot order, so
// a follower parks the old copy's other wagons before the descriptor of the new portion looks for them.
inline void OrderHostSpawnSlots(const bool* redescribed, int count, int* outOrder)
{
    int next = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int slot = 0; slot < count; ++slot)
            if (redescribed[slot] == (pass == 0)) outOrder[next++] = slot;
}

// Shared by SPAWN/SNAPSHOT initialization; retaining a damaged/wrecked host's complete initial state
// prevents a late join from depending on a future property-change event.
template <typename Carriage, typename State>
inline void CopyCarriageProperties(const Carriage& source, State& target)
{
    target.doorLock = source.doorLock;
    target.health = source.health;
    target.bWrecked = source.bWrecked;
}

// Quantize/dequantize speed (m_fTrainSpeed in metres per timeStep)
inline int16_t QuantizeSpeed(float speedPerStep)
{
    const float scaled = std::clamp(speedPerStep * SPEED_SCALE, -32767.0f, 32767.0f);
    return static_cast<int16_t>(std::lround(scaled));
}

inline float DequantizeSpeed(int16_t quantized)
{
    return static_cast<float>(quantized) / SPEED_SCALE;
}

// Quantize/dequantize gas/brake (engine m_fTrainGas/m_fTrainBrake in [0..510] -> uint8_t ÷2)
inline uint8_t QuantizePedal(float pedal)
{
    const float scaled = std::clamp(pedal * 0.5f, 0.0f, 255.0f);
    return static_cast<uint8_t>(std::lround(scaled));
}

inline float DequantizePedal(uint8_t half)
{
    return static_cast<float>(half) * 2.0f;
}

// Host immediate-state trigger check (start/stop moving, jump > 5m, direction change, derailment, station stop)
inline bool ShouldSendImmediateState(bool bPrevMoving, bool bNowMoving, bool bPrevClockwise, bool bNowClockwise,
    bool bPrevDerailed, bool bNowDerailed, bool bPrevStoppedAtStation, bool bNowStoppedAtStation,
    float expectedRailDist, float actualRailDist, float trackLength, bool& outJumpDetected)
{
    const float jumpDelta = std::fabs(ShortestRailDelta(expectedRailDist, actualRailDist, trackLength));
    outJumpDetected = jumpDelta > JUMP_THRESHOLD;
    return outJumpDetected || bPrevMoving != bNowMoving || bPrevClockwise != bNowClockwise ||
           bPrevDerailed != bNowDerailed || bPrevStoppedAtStation != bNowStoppedAtStation;
}

// Match a carriage model chain against the 16 engine configurations (returns 0..15 or -1 if unmatched)
inline int MatchConfiguration(const uint16_t* models, int count, const uint32_t configs[16][16])
{
    if (models == nullptr || count < 1 || count > 16)
    {
        return -1;
    }
    for (int cfg = 0; cfg < 16; cfg++)
    {
        bool bMatch = true;
        for (int i = 0; i < count; i++)
        {
            if (configs[cfg][i] != static_cast<uint32_t>(models[i]))
            {
                bMatch = false;
                break;
            }
        }
        if (bMatch && (count == 16 || configs[cfg][count] == 0))
        {
            return cfg;
        }
    }
    return -1;
}

// FNV-1a hash over track node counts, track lengths, and configuration table
inline uint32_t ComputeLayoutHash(
    const uint32_t nodeCounts[4], const float trackLengths[4], const uint32_t configs[16][16])
{
    uint32_t hash = 2166136261u;
    auto mixBytes = [&hash](const void* data, size_t len)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; i++)
        {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
    };
    mixBytes(nodeCounts, sizeof(uint32_t) * 4);
    mixBytes(trackLengths, sizeof(float) * 4);
    mixBytes(configs, sizeof(uint32_t) * 16 * 16);
    return hash;
}

// Package T2: Entity-to-vehicle attachment state & policy helpers (docs/design/trains.md §3.5, §5.6, §6.5)
struct SAttachmentSpec
{
    bool bAttached = false;
    int targetVehicleId = -1;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float offsetZ = 0.0f;

    // Ped attachment fields (PLAYER / PED)
    uint8_t turretPosition = 0;
    float angleLimit = 0.0f;
    uint8_t weaponType = 0;

    // Physical attachment fields (OBJECT / VEHICLE)
    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
};

inline bool FloatNear(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

// Two attachment specs represent the same attachment when both are unattached, or when both are attached to the same
// target vehicle at the same offset and turret/rotation parameters. (Weapon changes after attachment are carried by
// regular weapon snapshots rather than re-running AttachPedToEntity.)
inline bool AttachmentStateEquals(const SAttachmentSpec& a, const SAttachmentSpec& b, bool bIsPedKind)
{
    if (!a.bAttached && !b.bAttached)
    {
        return true;
    }
    if (a.bAttached != b.bAttached)
    {
        return false;
    }
    if (a.targetVehicleId != b.targetVehicleId || !FloatNear(a.offsetX, b.offsetX) ||
        !FloatNear(a.offsetY, b.offsetY) || !FloatNear(a.offsetZ, b.offsetZ))
    {
        return false;
    }
    if (bIsPedKind)
    {
        return a.turretPosition == b.turretPosition && FloatNear(a.angleLimit, b.angleLimit);
    }
    return FloatNear(a.rotX, b.rotX) && FloatNear(a.rotY, b.rotY) && FloatNear(a.rotZ, b.rotZ);
}

// Owner polling rule: never send an initial unattached packet on startup; send whenever an entity attaches, changes
// target/offset/orientation, or detaches after having been attached.
inline bool ShouldSendAttachmentUpdate(
    bool bHadSentState, const SAttachmentSpec& lastSent, const SAttachmentSpec& current, bool bIsPedKind)
{
    if (!bHadSentState)
    {
        return current.bAttached;
    }
    return !AttachmentStateEquals(lastSent, current, bIsPedKind);
}

// Receivers skip writing world positions from on-foot / idle packets while the entity is attached to a vehicle,
// letting the engine's PositionAttachedPed / PositionAttachedEntity lock the entity to the target copy.
inline bool ShouldApplyOnFootPosition(bool bIsAttached)
{
    return !bIsAttached;
}
}  // namespace TrainSyncPolicy
