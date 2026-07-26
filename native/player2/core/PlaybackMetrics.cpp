#include "PlaybackMetrics.h"

#include <algorithm>
#include <cstdlib>

namespace Colosseum::Player2 {

PlaybackMetricsAccumulator::PlaybackMetricsAccumulator(int64_t windowMs)
    : m_windowMs(windowMs > 0 ? windowMs : 10'000)
{
}

void PlaybackMetricsAccumulator::add(const PlaybackMetricSample &sample)
{
    m_samples.push_back(sample);
}

PlaybackMetricsReport PlaybackMetricsAccumulator::report() const
{
    PlaybackMetricsReport report;
    if (m_samples.empty())
        return report;

    // Anchor on the first real-playback sample: a valid audio clock means audio is truly rendering.
    // A video-only file never gets an audio clock, so fall back to the first presented frame. Samples
    // before the anchor are startup (device init, loudnorm priming) and must not dilute the rates.
    std::size_t anchorIndex = m_samples.size();
    for (std::size_t i = 0; i < m_samples.size(); ++i) {
        if (m_samples[i].audioClockValid) {
            anchorIndex = i;
            break;
        }
    }
    if (anchorIndex == m_samples.size()) {
        for (std::size_t i = 0; i < m_samples.size(); ++i) {
            if (m_samples[i].presented > 0) {
                anchorIndex = i;
                break;
            }
        }
    }
    if (anchorIndex == m_samples.size())
        return report;  // never rendered — leave anchored=false so nobody trusts a phantom rate

    const PlaybackMetricSample &anchor = m_samples[anchorIndex];
    const PlaybackMetricSample &last = m_samples.back();

    report.anchored = true;
    report.playbackSeconds = std::max<double>(0.0, (last.monotonicMs - anchor.monotonicMs) / 1000.0);
    if (report.playbackSeconds > 0.0) {
        report.sustainedFps = (last.presented - anchor.presented) / report.playbackSeconds;
        report.decodedFps = (last.decoded - anchor.decoded) / report.playbackSeconds;
    }
    report.underruns = last.audioUnderruns - anchor.audioUnderruns;

    report.minAudioQueueMs = anchor.audioQueueMs;
    report.maxAudioQueueMs = anchor.audioQueueMs;
    report.avErrorMinUs = anchor.avErrorUs;
    report.avErrorMaxUs = anchor.avErrorUs;
    double avErrorSum = 0.0;
    int64_t avCount = 0;

    // Per-window accounting: bucket anchored samples by fixed width so a mid-run collapse is visible.
    int64_t currentBucket = -1;
    int64_t windowFirstMs = 0;
    uint64_t windowFirstPresented = 0;
    for (std::size_t i = anchorIndex; i < m_samples.size(); ++i) {
        const PlaybackMetricSample &s = m_samples[i];
        report.minAudioQueueMs = std::min(report.minAudioQueueMs, s.audioQueueMs);
        report.maxAudioQueueMs = std::max(report.maxAudioQueueMs, s.audioQueueMs);
        report.avErrorMinUs = std::min(report.avErrorMinUs, s.avErrorUs);
        report.avErrorMaxUs = std::max(report.avErrorMaxUs, s.avErrorUs);
        avErrorSum += static_cast<double>(s.avErrorUs);
        ++avCount;

        const int64_t bucket = (s.monotonicMs - anchor.monotonicMs) / m_windowMs;
        if (bucket != currentBucket) {
            report.windows.push_back(
                {(s.monotonicMs - anchor.monotonicMs) / 1000.0, 0.0, s.audioQueueMs});
            windowFirstMs = s.monotonicMs;
            windowFirstPresented = s.presented;
            currentBucket = bucket;
        }
        PlaybackMetricWindow &window = report.windows.back();
        window.minAudioQueueMs = std::min(window.minAudioQueueMs, s.audioQueueMs);
        const double windowSeconds = (s.monotonicMs - windowFirstMs) / 1000.0;
        if (windowSeconds > 0.0)
            window.fps = (s.presented - windowFirstPresented) / windowSeconds;
    }

    report.avErrorMaxAbsUs = std::max(std::llabs(report.avErrorMinUs), std::llabs(report.avErrorMaxUs));
    if (avCount > 0)
        report.avErrorMeanUs = avErrorSum / static_cast<double>(avCount);

    return report;
}

} // namespace Colosseum::Player2
