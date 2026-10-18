// Host-side unit tests for TrainSyncPolicy (CoopAndreas/client/src/CTrainSyncPolicy.h).
#include "../client/src/CTrainSyncPolicy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;

void Check(bool ok, const char* expr, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
        ++failures;
    }
}

bool Near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

void WrapAndShortestDelta()
{
    constexpr float trackLen = 18100.0f;
    CHECK(Near(TrainSyncPolicy::WrapRailDistance(0.0f, trackLen), 0.0f));
    CHECK(Near(TrainSyncPolicy::WrapRailDistance(18105.5f, trackLen), 5.5f));
    CHECK(Near(TrainSyncPolicy::WrapRailDistance(-10.0f, trackLen), 18090.0f));

    // Across the 0 / trackLength seam in both directions
    CHECK(Near(TrainSyncPolicy::ShortestRailDelta(18090.0f, 15.0f, trackLen), 25.0f));
    CHECK(Near(TrainSyncPolicy::ShortestRailDelta(15.0f, 18090.0f, trackLen), -25.0f));
    CHECK(Near(TrainSyncPolicy::ShortestRailDelta(1000.0f, 1050.0f, trackLen), 50.0f));
}

void StepDisplayRailDistanceSmoothingAndMonotonicity()
{
    constexpr float trackLen = 18100.0f;

    // Smooth convergence without snapping when error <= 25 m
    float display = 100.0f;
    float target = 110.0f;
    const float speed = 0.5f;  // +0.5 m per step
    const float dt = 1.0f;
    for (int i = 0; i < 60; i++)
    {
        target = TrainSyncPolicy::WrapRailDistance(target + speed * dt, trackLen);
        const auto step = TrainSyncPolicy::StepDisplayRailDistance(display, target, speed, dt, trackLen, 50, false);
        CHECK(!step.bSnapped);
        // Moving forward: display must never move backward
        CHECK(TrainSyncPolicy::ShortestRailDelta(display, step.displayDistance, trackLen) >= -1e-5f);
        display = step.displayDistance;
    }
    CHECK( std::fabs(TrainSyncPolicy::ShortestRailDelta(display, target, trackLen)) < 1.5f );

    // Never moves against the direction of motion when target is slightly behind current display
    {
        const float cur = 500.0f;
        const float behindTarget = 490.0f;  // 10 m behind
        const auto step = TrainSyncPolicy::StepDisplayRailDistance(cur, behindTarget, 0.2f, 1.0f, trackLen, 40, false);
        CHECK(!step.bSnapped);
        // Delta from cur must be >= 0 (held or forward, never backward while speed > 0)
        CHECK(TrainSyncPolicy::ShortestRailDelta(cur, step.displayDistance, trackLen) >= -1e-5f);
    }

    // Same for counter-clockwise (negative speed) when target is slightly ahead
    {
        const float cur = 500.0f;
        const float aheadTarget = 510.0f;
        const auto step = TrainSyncPolicy::StepDisplayRailDistance(cur, aheadTarget, -0.2f, 1.0f, trackLen, 40, false);
        CHECK(!step.bSnapped);
        CHECK(TrainSyncPolicy::ShortestRailDelta(cur, step.displayDistance, trackLen) <= 1e-5f);
    }
}

void StepDisplaySnapAndHold()
{
    constexpr float trackLen = 5200.0f;

    // Error > 25 m snaps immediately
    {
        const auto step = TrainSyncPolicy::StepDisplayRailDistance(100.0f, 140.0f, 0.4f, 1.0f, trackLen, 50, false);
        CHECK(step.bSnapped);
        CHECK(Near(step.displayDistance, 140.0f));
        CHECK(Near(step.remainingError, 0.0f));
    }

    // Jump counter change snaps even for small error
    {
        const auto step = TrainSyncPolicy::StepDisplayRailDistance(100.0f, 106.0f, 0.0f, 1.0f, trackLen, 50, true);
        CHECK(step.bSnapped);
        CHECK(Near(step.displayDistance, 106.0f));
    }

    // Hold when msSinceLastState > 500 ms
    {
        const auto step = TrainSyncPolicy::StepDisplayRailDistance(250.0f, 270.0f, 0.5f, 1.0f, trackLen, 600, false);
        CHECK(!step.bSnapped);
        CHECK(Near(step.displayDistance, 250.0f));
        CHECK(Near(step.appliedCorrection, 0.0f));
    }
}

void SequenceAndQuantization()
{
    CHECK(TrainSyncPolicy::IsSequenceNewer(0, 0, false));
    CHECK(!TrainSyncPolicy::IsSequenceNewer(5, 5, true));
    CHECK(TrainSyncPolicy::IsSequenceNewer(6, 5, true));
    CHECK(!TrainSyncPolicy::IsSequenceNewer(4, 5, true));
    // Wraparound across 255 -> 0
    CHECK(TrainSyncPolicy::IsSequenceNewer(0, 255, true));
    CHECK(TrainSyncPolicy::IsSequenceNewer(10, 250, true));
    CHECK(!TrainSyncPolicy::IsSequenceNewer(250, 10, true));

    // Speed quantization round-trip
    for (float s : {-1.25f, -0.6f, 0.0f, 0.1f, 0.6f, 1.5f})
    {
        const int16_t q = TrainSyncPolicy::QuantizeSpeed(s);
        CHECK(Near(TrainSyncPolicy::DequantizeSpeed(q), s, 1e-4f));
    }

    // Pedal quantization round-trip (÷2 for [0..510])
    CHECK(TrainSyncPolicy::QuantizePedal(0.0f) == 0);
    CHECK(TrainSyncPolicy::QuantizePedal(510.0f) == 255);
    CHECK(Near(TrainSyncPolicy::DequantizePedal(TrainSyncPolicy::QuantizePedal(254.0f)), 254.0f, 0.01f));
}

void ImmediateStateTriggersAndConfigMatching()
{
    bool bJump = false;
    // Normal cruise: no immediate trigger
    CHECK(!TrainSyncPolicy::ShouldSendImmediateState(
        true, true, true, true, false, false, false, false, 1000.0f, 1001.0f, 18100.0f, bJump));
    CHECK(!bJump);

    // Jump > 5 m
    CHECK(TrainSyncPolicy::ShouldSendImmediateState(
        true, true, true, true, false, false, false, false, 1000.0f, 1008.0f, 18100.0f, bJump));
    CHECK(bJump);

    // Derailment transition
    CHECK(TrainSyncPolicy::ShouldSendImmediateState(
        true, true, true, true, false, true, false, false, 1000.0f, 1000.2f, 18100.0f, bJump));
    CHECK(!bJump);

    // Configuration matching and layout hash
    uint32_t configs[16][16]{};
    configs[10][0] = 537;
    configs[10][1] = 569;
    configs[11][0] = 538;
    configs[11][1] = 570;
    configs[11][2] = 570;

    const uint16_t streakChain[3] = {538, 570, 570};
    const uint16_t freightChain[2] = {537, 569};
    const uint16_t unknownChain[2] = {538, 569};
    CHECK(TrainSyncPolicy::MatchConfiguration(streakChain, 3, configs) == 11);
    CHECK(TrainSyncPolicy::MatchConfiguration(freightChain, 2, configs) == 10);
    CHECK(TrainSyncPolicy::MatchConfiguration(unknownChain, 2, configs) == -1);

    const uint32_t nodeCounts[4] = {926, 300, 67, 656};
    const float trackLengths[4] = {18100.0f, 5200.0f, 1000.0f, 13600.0f};
    const uint32_t h1 = TrainSyncPolicy::ComputeLayoutHash(nodeCounts, trackLengths, configs);
    const uint32_t h2 = TrainSyncPolicy::ComputeLayoutHash(nodeCounts, trackLengths, configs);
    CHECK(h1 == h2 && h1 != 0);

    uint32_t alteredCounts[4] = {927, 300, 67, 656};
    CHECK(TrainSyncPolicy::ComputeLayoutHash(alteredCounts, trackLengths, configs) != h1);
}

void AttachmentMirrorPolicy()
{
    using TrainSyncPolicy::SAttachmentSpec;

    SAttachmentSpec unattached{};
    CHECK(TrainSyncPolicy::AttachmentStateEquals(unattached, unattached, true));
    CHECK(!TrainSyncPolicy::ShouldSendAttachmentUpdate(false, unattached, unattached, true));

    // Attach ped to vehicle 42 (e.g. Vagos on Brown Streak roof or CJ on caboose)
    SAttachmentSpec vagos{};
    vagos.bAttached = true;
    vagos.targetVehicleId = 42;
    vagos.offsetX = 0.0f;
    vagos.offsetY = -2.5f;
    vagos.offsetZ = 1.8f;
    vagos.turretPosition = 0;
    vagos.angleLimit = 6.283185f;
    vagos.weaponType = 28;

    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(false, unattached, vagos, true));
    CHECK(!TrainSyncPolicy::ShouldSendAttachmentUpdate(true, vagos, vagos, true));

    // Weapon change alone while already attached does not re-trigger AttachPedToEntity
    SAttachmentSpec vagosDifferentWeapon = vagos;
    vagosDifferentWeapon.weaponType = 29;
    CHECK(!TrainSyncPolicy::ShouldSendAttachmentUpdate(true, vagos, vagosDifferentWeapon, true));

    // Offset or target vehicle change triggers an update
    SAttachmentSpec vagosMoved = vagos;
    vagosMoved.offsetY = -1.0f;
    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(true, vagos, vagosMoved, true));

    SAttachmentSpec vagosOtherCar = vagos;
    vagosOtherCar.targetVehicleId = 43;
    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(true, vagos, vagosOtherCar, true));

    // Detach transition triggers an update, then stays silent while unattached
    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(true, vagos, unattached, true));
    CHECK(!TrainSyncPolicy::ShouldSendAttachmentUpdate(true, unattached, unattached, true));

    // Vehicle-to-vehicle attachment (#74 Green Goo car on flatbed)
    SAttachmentSpec carOnFlatbed{};
    carOnFlatbed.bAttached = true;
    carOnFlatbed.targetVehicleId = 12;
    carOnFlatbed.offsetY = -0.5f;
    carOnFlatbed.offsetZ = 0.8f;
    carOnFlatbed.rotZ = 3.14159f;
    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(false, unattached, carOnFlatbed, false));
    CHECK(!TrainSyncPolicy::ShouldSendAttachmentUpdate(true, carOnFlatbed, carOnFlatbed, false));

    SAttachmentSpec carRotated = carOnFlatbed;
    carRotated.rotZ = 0.0f;
    CHECK(TrainSyncPolicy::ShouldSendAttachmentUpdate(true, carOnFlatbed, carRotated, false));

    // On-foot / idle world position writes are suppressed while attached
    CHECK(TrainSyncPolicy::ShouldApplyOnFootPosition(false));
    CHECK(!TrainSyncPolicy::ShouldApplyOnFootPosition(true));
}
}  // namespace

int main()
{
    WrapAndShortestDelta();
    StepDisplayRailDistanceSmoothingAndMonotonicity();
    StepDisplaySnapAndHold();
    SequenceAndQuantization();
    ImmediateStateTriggersAndConfigMatching();
    AttachmentMirrorPolicy();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("train_sync_test: ok\n");
    return EXIT_SUCCESS;
}
