#pragma once

#include <QtCore/QtTypes>

#include <mutex>

namespace Colosseum::Player2 {

class PlaybackClock
{
public:
    explicit PlaybackClock(qint64 qpcFrequency = 0);

    void reset(qint64 epochMediaUs, qint64 epochQpc);
    void invalidate();
    qint64 positionAt(qint64 qpcNow) const;
    void setRate(double rate, qint64 qpcNow);
    double rate() const;
    void pause(qint64 qpcNow);
    void resume(qint64 qpcNow);
    bool paused() const;
    bool valid() const;
    qint64 correctToward(qint64 masterMediaUs, qint64 qpcNow,
                         qint64 maximumCorrectionUs);
    qint64 qpcFrequency() const noexcept;

private:
    qint64 positionAtLocked(qint64 qpcNow) const;

    mutable std::mutex m_mutex;
    qint64 m_qpcFrequency = 1;
    qint64 m_epochMediaUs = 0;
    qint64 m_epochQpc = 0;
    qint64 m_pausedMediaUs = 0;
    double m_rate = 1.0;
    bool m_paused = false;
    bool m_valid = false;
};

enum class ClockResync
{
    Slew,      // absorb the reading smoothly over several frames
    HardReset  // snap the epoch to the reading in one step
};

// Policy for how the video master clock absorbs a fresh audio-master reading once audio is already
// the master. On RECOVERY from a gap — a pause or underrun that invalidated the audio clock, so
// `audioWasValid` is false — a discontinuity larger than `hardResetThresholdUs` must SNAP: crawling a
// few ms/frame across a multi-hundred-ms jump is exactly the visible "audio ahead of video" drift.
// In steady state (`audioWasValid`) we always slew, so a lone noisy reading can never jolt the picture.
ClockResync decideClockResync(bool audioWasValid, qint64 discontinuityUs, qint64 hardResetThresholdUs);

} // namespace Colosseum::Player2
