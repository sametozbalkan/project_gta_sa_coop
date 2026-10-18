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

void IncarnationsAndCompleteCarriageState()
{
    using namespace TrainSyncPolicy;
    // Reused trainId: an old, apparently newer sequence must not poison the new window.
    CHECK(!AcceptTrainState(100, 101, true, 110, 0, true));
    CHECK(AcceptTrainState(101, 101, true, 1, 0, true));
    CHECK(!MatchesIncarnation(100, 101, true)); // delayed REMOVE / CARRIAGES / station request
    CHECK(!MatchesIncarnation(101, 101, false));
    CHECK(MatchesIncarnation(101, 101, true));
    CHECK(!AcceptDescriptor(100, 101, true, 0));
    CHECK(AcceptDescriptor(101, 101, true, 0)); // current live resnapshot
    CHECK(!AcceptDescriptor(101, 101, false, 0)); // tombstone after REMOVE
    CHECK(AcceptDescriptor(102, 101, false, 0));
    CHECK(AcceptRestartFence(0, 0)); // first session snapshot, before a native reset
    CHECK(AcceptRestartFence(102, 0));
    CHECK(AcceptRestartFence(102, 102)); // another page/current resnapshot
    CHECK(!AcceptRestartFence(0, 102));
    CHECK(!AcceptRestartFence(101, 102));
    CHECK(!AcceptDescriptor(101, 101, true, 102)); // before restart fence
    CHECK(!AcceptTrainState(101, 103, true, 120, 0, true));
    CHECK(AcceptDescriptor(103, 101, false, 102));
    CHECK(IsIncarnationNewer(1, 0xFFFFFFFFu));
    CHECK(!IsIncarnationNewer(0xFFFFFFFFu, 1));
    CHECK(!IsIncarnationNewer(0, 0));

    struct Properties { uint8_t doorLock; uint16_t health; bool bWrecked; };
    const Properties damaged{2, 431, false};
    const Properties wrecked{4, 0, true};
    Properties initial{0, 1000, false};
    CopyCarriageProperties(damaged, initial);
    CHECK(initial.doorLock == 2 && initial.health == 431 && !initial.bWrecked);
    CopyCarriageProperties(wrecked, initial);
    CHECK(initial.doorLock == 4 && initial.health == 0 && initial.bWrecked);
}

void SurvivingStockSubchains()
{
    using namespace TrainSyncPolicy;
    uint32_t configs[16][16]{};
    const uint32_t freight[] = {537, 569, 569, 590};
    const uint32_t streak[] = {538, 570, 570};
    for (int i = 0; i < 4; ++i) configs[10][i] = freight[i];
    for (int i = 0; i < 3; ++i) configs[11][i] = streak[i];
    // Every possible contiguous component after engine, middle, or tail deletion.
    // Model matching is independent of direction; native geometry still needs gameplay acceptance.
    for (const int cfg : {10, 11})
    {
        const int n = cfg == 10 ? 4 : 3;
        for (int first = 0; first < n; ++first)
            for (int count = 1; count <= n - first; ++count)
            {
                uint16_t models[8]{};
                for (int i = 0; i < count; ++i) models[i] = static_cast<uint16_t>(configs[cfg][first + i]);
                const auto result = MatchSubchainConfiguration(models, count, configs);
                CHECK(result.config == cfg);
                CHECK(ValidateConfigurationSubchain(models, count, result.config, result.offset, configs));
                CHECK(result.stockCount == n);
                if (first > 0) CHECK(result.offset > 0); // wagon-only head must retain non-front semantics
            }
    }
    // Production placement uses the factory's signed length in either direction, including track wrap.
    CHECK(Near(FollowingCarriageDistance(10.0f, -20.0f, 1000.0f), 990.0f));
    CHECK(Near(FollowingCarriageDistance(990.0f, 20.0f, 1000.0f), 10.0f));
    CHECK(Near(FollowingCarriageDistance(-10.0f, -20.0f, 1000.0f), 970.0f));
    CHECK(Near(FollowingCarriageDistance(-10.0f, 20.0f, 1000.0f), 10.0f));
    // A valid retained suffix of a 16-car stock chain still needs the complete stock allocation/load.
    for (int i = 0; i < 16; ++i) configs[15][i] = i == 0 ? 537 : 569;
    const uint16_t longSuffix[8] = {569, 569, 569, 569, 569, 569, 569, 569};
    const auto longPlan = MatchSubchainConfiguration(longSuffix, 8, configs);
    CHECK(longPlan.config == 15 && longPlan.stockCount == 16 && longPlan.offset == 1);
    CHECK(ValidateConfigurationSubchain(longSuffix, 8, 15, 8, configs));
    CHECK(!ValidateConfigurationSubchain(longSuffix, 8, 15, 9, configs));
    // A middle deletion leaves both engine/prefix and tail/rider identities available. Slot ownership
    // may change; identity/model/generation is the production criterion for reusing the native object.
    CHECK(CanRetainCarriage(42, 537, 42, 537, true));
    CHECK(CanRetainCarriage(44, 569, 44, 569, true)); // secondary component claims a parked native wagon
    CHECK(!CanRetainCarriage(44, 569, 43, 569, true)); // removed middle identity cannot replace it
    CHECK(!CanRetainCarriage(44, 569, 44, 569, false)); // reused native pool slot
    CHECK(!CanRetainCarriage(44, 570, 44, 569, true));
    CHECK(!CanRetainCarriage(-1, 569, -1, 569, true));
    const uint16_t impossible[] = {538, 569};
    CHECK(MatchSubchainConfiguration(impossible, 2, configs).config == -1);
    const uint16_t suffix[] = {569, 590};
    CHECK(ValidateConfigurationSubchain(suffix, 2, 10, 2, configs));
    CHECK(!ValidateConfigurationSubchain(suffix, 2, 10, 3, configs));
    CHECK(!ValidateConfigurationSubchain(suffix, 2, 16, 0, configs));
    CHECK(!ValidateConfigurationSubchain(suffix, 9, 10, 0, configs));
}

void SplitDescriptorOrderAndHandoverExpiry()
{
    using namespace TrainSyncPolicy;
    // Slot 5 re-described as [c0] while the pool scan put [c2,c3] into the lower free slot 2: slot 5's SPAWN goes
    // first, so followers park c2/c3 before slot 2's descriptor claims them. Otherwise slot order is kept.
    bool redescribed[8]{};
    int order[8]{};
    redescribed[5] = true;
    OrderHostSpawnSlots(redescribed, 8, order);
    const int expected[8] = {5, 0, 1, 2, 3, 4, 6, 7};
    for (int i = 0; i < 8; ++i) CHECK(order[i] == expected[i]);
    redescribed[1] = true;
    OrderHostSpawnSlots(redescribed, 8, order);
    const int expectedTwo[8] = {1, 5, 0, 2, 3, 4, 6, 7};
    for (int i = 0; i < 8; ++i) CHECK(order[i] == expectedTwo[i]);
    bool none[3]{};
    int plain[3]{};
    OrderHostSpawnSlots(none, 3, plain);
    CHECK(plain[0] == 0 && plain[1] == 1 && plain[2] == 2);

    // Unclaimed handovers expire after HANDOVER_TIMEOUT_MS, also across the 32-bit tick wrap.
    CHECK(!IsHandoverExpired(1000, 1000));
    CHECK(!IsHandoverExpired(1000, 1000 + HANDOVER_TIMEOUT_MS - 1));
    CHECK(IsHandoverExpired(1000, 1000 + HANDOVER_TIMEOUT_MS));
    uint32_t parkedBeforeWrap = 0xFFFFFF00u;
    CHECK(!IsHandoverExpired(parkedBeforeWrap, 0x00000100u));
    CHECK(IsHandoverExpired(parkedBeforeWrap, parkedBeforeWrap + HANDOVER_TIMEOUT_MS));
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
    IncarnationsAndCompleteCarriageState();
    SurvivingStockSubchains();
    SplitDescriptorOrderAndHandoverExpiry();
    AttachmentMirrorPolicy();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("train_sync_test: ok\n");
    return EXIT_SUCCESS;
}
