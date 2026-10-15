#pragma once

#include <cstdint>
#include <vector>

namespace Colosseum::Player2 {

// One periodic sample of playback state, taken by the harness on each frame tick. All counters are
// cumulative-since-open; the accumulator turns them into playback-anchored rates so a slow startup
// (device init + loudnorm priming) never dilutes the measured throughput. This is the whole reason
// the class exists: the old benchmark divided presented-frames by the wall interval that BEGAN
// before the media even opened, manufacturing a ~17 fps result from a real 24 fps stream.
struct PlaybackMetricSample
{
    int64_t monotonicMs = 0;       // harness monotonic clock (from process/run start)
    uint64_t decoded = 0;          // cumulative frames decoded
    uint64_t presented = 0;        // cumulative frames presented
    double audioQueueMs = 0.0;     // audio queue depth right now
    bool audioClockValid = false;  // is the audio master clock currently valid (real playback)
    int64_t avErrorUs = 0;         // videoPtsUs - audioMasterUs at last present (neg = video behind)
    uint64_t audioUnderruns = 0;   // cumulative audio underruns
};

// Fixed-width slice of the anchored run, so a mid-run regression (the "fractures ~1 minute in"
// symptom) shows up instead of being averaged away by a single sustained number.
struct PlaybackMetricWindow
{
    double startSeconds = 0.0;  // seconds since anchor at the window start
    double fps = 0.0;           // presented fps across this window
    double minAudioQueueMs = 0.0;
};

struct PlaybackMetricsReport
{
    bool anchored = false;          // did real playback ever start? if false, the rates are meaningless
    double playbackSeconds = 0.0;   // measured span since anchor
    double sustainedFps = 0.0;      // presented frames since anchor / playbackSeconds
    double decodedFps = 0.0;        // decoded frames since anchor / playbackSeconds
    double minAudioQueueMs = 0.0;   // low-water audio queue since anchor (crackle detector)
    double maxAudioQueueMs = 0.0;
    int64_t avErrorMaxAbsUs = 0;    // worst |A/V drift|
    int64_t avErrorMinUs = 0;       // most negative (video furthest behind audio — Hemanth's symptom)
    int64_t avErrorMaxUs = 0;       // most positive (video furthest ahead)
    double avErrorMeanUs = 0.0;     // mean signed drift since anchor
    uint64_t underruns = 0;         // audio underruns during the anchored span
    std::vector<PlaybackMetricWindow> windows;
};

// Accumulates harness samples into a playback-anchored report. Anchor = the first sample whose audio
// clock is valid (real audio is being rendered); for a video-only file with no audio clock it falls
// back to the first sample with a presented frame. Samples before the anchor are ignored for rates.
class PlaybackMetricsAccumulator
{
public:
    explicit PlaybackMetricsAccumulator(int64_t windowMs = 10'000);

    void add(const PlaybackMetricSample &sample);
    PlaybackMetricsReport report() const;

private:
    int64_t m_windowMs;
    std::vector<PlaybackMetricSample> m_samples;
};

} // namespace Colosseum::Player2
