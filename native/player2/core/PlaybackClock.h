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

} // namespace Colosseum::Player2
