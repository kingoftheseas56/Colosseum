// Hermetic tests for PlaybackMetricsAccumulator — the pure accounting that turns cumulative harness
// samples into PLAYBACK-ANCHORED rates. This is the regression guard for the exact bug that cost a
// wake: the old benchmark started its wall clock before the media opened, then divided 320 presented
// frames by the whole ~19.6 s interval (which included ~6 s of device init + loudnorm priming),
// manufacturing a ~16 fps result from a real 24 fps stream. Every case below fixes one honest signal.

#include "player2/core/PlaybackMetrics.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void requireNear(double actual, double expected, double tolerance, const std::string &message)
{
    if (std::fabs(actual - expected) > tolerance)
        throw std::runtime_error(message + " (got " + std::to_string(actual) + ", expected ~" +
                                 std::to_string(expected) + ")");
}

// A run: `startupMs` of priming where the audio clock is invalid and presentation is frozen, then
// `playbackMs` of real playback at `fps` with a valid audio clock. Sampled every `dtMs`. This is the
// literal shape of the failure Codex found.
std::vector<PlaybackMetricSample> makeAnchoredRun(int64_t startupMs, int64_t playbackMs, double fps,
                                                  int64_t dtMs = 250)
{
    std::vector<PlaybackMetricSample> samples;
    const uint64_t framesPerTick = static_cast<uint64_t>(std::llround(fps * dtMs / 1000.0));
    uint64_t presented = 2;  // a couple of frames drawn during priming, then frozen
    uint64_t decoded = 2;
    for (int64_t t = 0; t <= startupMs + playbackMs; t += dtMs) {
        PlaybackMetricSample sample;
        sample.monotonicMs = t;
        const bool playing = t >= startupMs;
        sample.audioClockValid = playing;
        sample.audioQueueMs = playing ? 1800.0 : 0.0;
        sample.presented = presented;
        sample.decoded = decoded;
        samples.push_back(sample);
        if (playing) {
            presented += framesPerTick;
            decoded += framesPerTick;
        }
    }
    return samples;
}

// CASE A — the ghost-killer: fps is measured from the first valid audio clock, not process start.
void anchorsThroughputAtFirstValidAudioClock()
{
    PlaybackMetricsAccumulator acc;
    for (const auto &sample : makeAnchoredRun(/*startupMs=*/6000, /*playbackMs=*/14000, /*fps=*/24.0))
        acc.add(sample);
    const PlaybackMetricsReport report = acc.report();

    require(report.anchored, "run with a valid audio clock must anchor");
    requireNear(report.playbackSeconds, 14.0, 0.3, "playback span must exclude the 6 s of priming");
    requireNear(report.sustainedFps, 24.0, 0.6, "sustained fps must reflect the real 24 fps stream");
    // The naive process-start number would be ~16.9 fps. Prove we are nowhere near that ghost.
    require(report.sustainedFps > 21.0, "must not dilute fps with the pre-open startup interval");
}

// CASE B — low-water audio queue is the crackle detector, and only the anchored span counts.
void tracksAudioQueueLowWaterAfterAnchor()
{
    PlaybackMetricsAccumulator acc;
    std::vector<PlaybackMetricSample> samples = makeAnchoredRun(2000, 10000, 24.0);
    // Drive one mid-run dip to 40 ms (near starvation) well after the anchor.
    for (auto &sample : samples) {
        if (sample.monotonicMs == 8000)
            sample.audioQueueMs = 40.0;
    }
    for (const auto &sample : samples)
        acc.add(sample);
    const PlaybackMetricsReport report = acc.report();

    requireNear(report.minAudioQueueMs, 40.0, 0.01, "low-water must catch the mid-run starvation dip");
    require(report.maxAudioQueueMs >= 1800.0, "high-water must reflect the healthy steady state");
    // Pre-anchor queue was 0 ms; it must not poison the low-water mark.
    require(report.minAudioQueueMs > 0.0, "pre-anchor priming must not count as starvation");
}

// CASE C — A/V drift extremes: negative = video behind audio (Hemanth's exact complaint).
void capturesSignedAvDriftExtremes()
{
    PlaybackMetricsAccumulator acc;
    std::vector<PlaybackMetricSample> samples = makeAnchoredRun(1000, 4000, 24.0);
    // Inject drift: video 30 ms ahead early, then 90 ms behind later.
    for (auto &sample : samples) {
        if (sample.monotonicMs == 2000)
            sample.avErrorUs = 30'000;
        if (sample.monotonicMs == 4000)
            sample.avErrorUs = -90'000;
    }
    for (const auto &sample : samples)
        acc.add(sample);
    const PlaybackMetricsReport report = acc.report();

    require(report.avErrorMaxUs == 30'000, "max drift must capture video furthest ahead");
    require(report.avErrorMinUs == -90'000, "min drift must capture video furthest behind audio");
    require(report.avErrorMaxAbsUs == 90'000, "worst-abs drift must be the 90 ms lag");
}

// CASE D — windows expose a mid-run throughput collapse a single average would hide.
void windowsExposeMidRunRegression()
{
    PlaybackMetricsAccumulator acc(/*windowMs=*/10'000);
    // 10 s healthy at 24 fps, then 10 s degraded to 12 fps — the "fractures a minute in" shape.
    std::vector<PlaybackMetricSample> fast = makeAnchoredRun(0, 10'000, 24.0);
    uint64_t presented = fast.back().presented;
    uint64_t decoded = fast.back().decoded;
    int64_t base = fast.back().monotonicMs;
    std::vector<PlaybackMetricSample> samples = fast;
    for (int64_t t = base + 250; t <= base + 10'000; t += 250) {
        PlaybackMetricSample sample;
        sample.monotonicMs = t;
        sample.audioClockValid = true;
        sample.audioQueueMs = 1800.0;
        presented += 3;  // 12 fps
        decoded += 3;
        sample.presented = presented;
        sample.decoded = decoded;
        samples.push_back(sample);
    }
    for (const auto &sample : samples)
        acc.add(sample);
    const PlaybackMetricsReport report = acc.report();

    require(report.windows.size() >= 2, "a 20 s run must produce at least two 10 s windows");
    requireNear(report.windows[0].fps, 24.0, 1.0, "first window must read the healthy 24 fps");
    requireNear(report.windows[1].fps, 12.0, 1.5, "second window must expose the 12 fps collapse");
}

// CASE E — an all-priming run that never plays must report anchored=false so no one trusts the rate.
void reportsUnanchoredWhenPlaybackNeverStarts()
{
    PlaybackMetricsAccumulator acc;
    for (int64_t t = 0; t <= 5000; t += 250) {
        PlaybackMetricSample sample;
        sample.monotonicMs = t;
        sample.audioClockValid = false;
        sample.presented = 0;
        acc.add(sample);
    }
    const PlaybackMetricsReport report = acc.report();

    require(!report.anchored, "a run that never renders must not be reported as anchored");
    require(report.sustainedFps == 0.0, "an unanchored run has no meaningful fps");
}

// CASE F — video-only files (no audio clock) fall back to anchoring on the first presented frame.
void fallsBackToFirstPresentedForVideoOnly()
{
    PlaybackMetricsAccumulator acc;
    uint64_t presented = 0;
    for (int64_t t = 0; t <= 10'000; t += 250) {
        PlaybackMetricSample sample;
        sample.monotonicMs = t;
        sample.audioClockValid = false;  // no audio track at all
        if (t >= 2000)
            presented += 6;  // 24 fps once the pipeline warms up
        sample.presented = presented;
        sample.decoded = presented;
        acc.add(sample);
    }
    const PlaybackMetricsReport report = acc.report();

    require(report.anchored, "a video-only run must anchor on the first presented frame");
    requireNear(report.sustainedFps, 24.0, 1.0, "video-only fps must measure from first present");
}

struct Case
{
    const char *name;
    void (*run)();
};

const Case kCases[] = {
    {"anchorsThroughputAtFirstValidAudioClock", anchorsThroughputAtFirstValidAudioClock},
    {"tracksAudioQueueLowWaterAfterAnchor", tracksAudioQueueLowWaterAfterAnchor},
    {"capturesSignedAvDriftExtremes", capturesSignedAvDriftExtremes},
    {"windowsExposeMidRunRegression", windowsExposeMidRunRegression},
    {"reportsUnanchoredWhenPlaybackNeverStarts", reportsUnanchoredWhenPlaybackNeverStarts},
    {"fallsBackToFirstPresentedForVideoOnly", fallsBackToFirstPresentedForVideoOnly},
};

} // namespace

int main()
{
    int failures = 0;
    for (const Case &testCase : kCases) {
        try {
            testCase.run();
            std::cout << "[pass] " << testCase.name << '\n';
        } catch (const std::exception &error) {
            std::cerr << "[FAIL] " << testCase.name << ": " << error.what() << '\n';
            ++failures;
        }
    }
    if (failures != 0) {
        std::cerr << failures << " playback-metrics case(s) failed\n";
        return 1;
    }
    std::cout << "all playback-metrics cases passed\n";
    return 0;
}
