#include "player2/core/FrameScheduler.h"
#include "player2/core/PlaybackClock.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Colosseum::Player2;

namespace {

constexpr qint64 QpcFrequency = 10'000'000;

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

qint64 qpcForUs(qint64 microseconds)
{
    return microseconds * QpcFrequency / 1'000'000;
}

void clockSupportsPauseRateAndCorrection()
{
    PlaybackClock clock(QpcFrequency);
    clock.reset(2'000'000, 0);
    require(clock.positionAt(qpcForUs(500'000)) == 2'500'000,
            "clock did not advance at 1x");
    clock.pause(qpcForUs(500'000));
    require(clock.positionAt(qpcForUs(2'000'000)) == 2'500'000,
            "paused clock advanced");
    clock.resume(qpcForUs(2'000'000));
    clock.setRate(1.5, qpcForUs(2'000'000));
    require(clock.positionAt(qpcForUs(3'000'000)) == 4'000'000,
            "clock rate change lost its epoch");
    const qint64 applied = clock.correctToward(4'050'000, qpcForUs(3'000'000), 10'000);
    require(applied == 10'000 && clock.positionAt(qpcForUs(3'000'000)) == 4'010'000,
            "drift correction was not bounded");
    clock.pause(qpcForUs(3'000'000));
    clock.reset(9'000'000, qpcForUs(4'000'000));
    require(!clock.paused() && clock.positionAt(qpcForUs(4'100'000)) == 9'150'000,
            "reset retained stale pause state or epoch");
    clock.invalidate();
    require(!clock.valid(), "clock invalidation retained the prior media epoch");
}

// The audio clock going valid->invalid->valid is an underrun (or a pause). On recovery the video
// master must SNAP to the fresh audio position when the gap is large, not crawl 5 ms/frame across it
// — the crawl is the visible "audio comes way earlier than the video" drift Hemanth reported.
void clockResyncSnapsAfterUnderrunGapOnly()
{
    constexpr qint64 threshold = 50'000;  // 50 ms
    // Steady state (audio was valid last frame): always slew, even on a big lone reading, so a single
    // noisy sample can never jolt the picture.
    require(decideClockResync(true, 4'000, threshold) == ClockResync::Slew,
            "steady-state jitter must slew");
    require(decideClockResync(true, 400'000, threshold) == ClockResync::Slew,
            "a spurious steady-state error must not hard-reset mid-play");
    // Recovering from an underrun (audio was invalid last frame) with a large discontinuity: snap.
    require(decideClockResync(false, 300'000, threshold) == ClockResync::HardReset,
            "a large post-underrun discontinuity must hard-reset, not crawl");
    require(decideClockResync(false, -280'000, threshold) == ClockResync::HardReset,
            "hard-reset must trigger on discontinuity magnitude regardless of sign");
    // Recovering, but the gap is tiny: slew smoothly — there is no visible jump to snap over.
    require(decideClockResync(false, 8'000, threshold) == ClockResync::Slew,
            "a tiny post-underrun gap should still slew");
}

void schedulerHandlesCommonFrameRates()
{
    FrameScheduler scheduler(QpcFrequency);
    for (const double fps : {23.976, 24.0, 25.0, 29.97, 60.0}) {
        scheduler.reset();
        const qint64 frameDuration = static_cast<qint64>(std::llround(1'000'000.0 / fps));
        const FrameCandidate frame{1, frameDuration};
        const auto wait = scheduler.choose(0, 0, {frame});
        require(wait.action == FrameScheduleAction::WaitUntilQpc,
                "early frame was not assigned a deadline");
        require(std::llabs(wait.deadlineQpc - qpcForUs(frameDuration)) <= 10,
                "deadline conversion was inaccurate");
        const auto present = scheduler.choose(frameDuration, qpcForUs(frameDuration), {frame});
        require(present.action == FrameScheduleAction::Present,
                "on-time frame was not presented");
    }
}

void schedulerBoundsLateDropsAndRepeats()
{
    FrameScheduler scheduler(QpcFrequency, FrameSchedulerConfig{2'000, 40'000, 3});
    require(scheduler.choose(0, 0, {}).action == FrameScheduleAction::RepeatCurrent,
            "empty queue did not repeat the current frame");
    const std::vector<FrameCandidate> late{{1, 0}, {2, 20'000}, {3, 40'000}, {4, 60'000}};
    const auto drop = scheduler.choose(100'000, qpcForUs(100'000), late);
    require(drop.action == FrameScheduleAction::DropLate && drop.dropCount == 3,
            "late-drop policy was not bounded to three frames");
    const auto present = scheduler.choose(100'000, qpcForUs(100'000), {{4, 60'000}});
    require(present.action == FrameScheduleAction::Present,
            "scheduler caused an unbounded late-frame blackout");
}

void thirtyMinuteAcceleratedSimulationHasNoDrift()
{
    PlaybackClock clock(QpcFrequency);
    clock.reset(0, 0);
    std::vector<qint64> errors;
    for (qint64 mediaUs = 0; mediaUs <= 30LL * 60 * 1'000'000; mediaUs += 1'000'000) {
        const qint64 driftingQpc = qpcForUs(mediaUs) + mediaUs / 1'000;
        clock.correctToward(mediaUs, driftingQpc, 2'000);
        errors.push_back(std::llabs(clock.positionAt(driftingQpc) - mediaUs));
    }
    std::sort(errors.begin(), errors.end());
    const qint64 p95 = errors[static_cast<std::size_t>(errors.size() * 0.95)];
    require(p95 <= 40'000, "30-minute clock simulation exceeded 40 ms p95");
    require(errors.back() <= 40'000, "clock simulation accumulated monotonic drift");
}

} // namespace

int main()
{
    try {
        clockSupportsPauseRateAndCorrection();
        clockResyncSnapsAfterUnderrunGapOnly();
        schedulerHandlesCommonFrameRates();
        schedulerBoundsLateDropsAndRepeats();
        thirtyMinuteAcceleratedSimulationHasNoDrift();
    } catch (const std::exception &error) {
        std::cerr << "player2_clock_scheduler_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_clock_scheduler_test: PASS\n";
    return EXIT_SUCCESS;
}
