#include "PlaybackClock.h"

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Colosseum::Player2 {
namespace {

qint64 platformQpcFrequency()
{
#ifdef Q_OS_WIN
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
        return frequency.QuadPart;
#endif
    return 1'000'000;
}

} // namespace

PlaybackClock::PlaybackClock(qint64 qpcFrequency)
    : m_qpcFrequency(qpcFrequency > 0 ? qpcFrequency : platformQpcFrequency())
{
}

void PlaybackClock::reset(qint64 epochMediaUs, qint64 epochQpc)
{
    std::scoped_lock lock(m_mutex);
    m_epochMediaUs = epochMediaUs;
    m_epochQpc = epochQpc;
    m_pausedMediaUs = epochMediaUs;
    m_paused = false;
    m_valid = true;
}

void PlaybackClock::invalidate()
{
    std::scoped_lock lock(m_mutex);
    m_epochMediaUs = 0;
    m_epochQpc = 0;
    m_pausedMediaUs = 0;
    m_paused = false;
    m_valid = false;
}

qint64 PlaybackClock::positionAtLocked(qint64 qpcNow) const
{
    if (!m_valid)
        return 0;
    if (m_paused)
        return m_pausedMediaUs;
    const long double elapsedUs = static_cast<long double>(qpcNow - m_epochQpc) *
                                  1'000'000.0L / m_qpcFrequency;
    return m_epochMediaUs + static_cast<qint64>(std::llround(elapsedUs * m_rate));
}

qint64 PlaybackClock::positionAt(qint64 qpcNow) const
{
    std::scoped_lock lock(m_mutex);
    return positionAtLocked(qpcNow);
}

void PlaybackClock::setRate(double rate, qint64 qpcNow)
{
    std::scoped_lock lock(m_mutex);
    const qint64 current = positionAtLocked(qpcNow);
    m_rate = std::clamp(rate, 0.1, 8.0);
    if (m_paused)
        m_pausedMediaUs = current;
    else {
        m_epochMediaUs = current;
        m_epochQpc = qpcNow;
    }
}

double PlaybackClock::rate() const
{
    std::scoped_lock lock(m_mutex);
    return m_rate;
}

void PlaybackClock::pause(qint64 qpcNow)
{
    std::scoped_lock lock(m_mutex);
    if (!m_valid || m_paused)
        return;
    m_pausedMediaUs = positionAtLocked(qpcNow);
    m_paused = true;
}

void PlaybackClock::resume(qint64 qpcNow)
{
    std::scoped_lock lock(m_mutex);
    if (!m_valid || !m_paused)
        return;
    m_epochMediaUs = m_pausedMediaUs;
    m_epochQpc = qpcNow;
    m_paused = false;
}

bool PlaybackClock::paused() const
{
    std::scoped_lock lock(m_mutex);
    return m_paused;
}

bool PlaybackClock::valid() const
{
    std::scoped_lock lock(m_mutex);
    return m_valid;
}

qint64 PlaybackClock::correctToward(qint64 masterMediaUs, qint64 qpcNow,
                                    qint64 maximumCorrectionUs)
{
    std::scoped_lock lock(m_mutex);
    if (!m_valid)
        return 0;
    const qint64 error = masterMediaUs - positionAtLocked(qpcNow);
    const qint64 applied = std::clamp(error, -std::llabs(maximumCorrectionUs),
                                     std::llabs(maximumCorrectionUs));
    if (m_paused)
        m_pausedMediaUs += applied;
    else
        m_epochMediaUs += applied;
    return applied;
}

qint64 PlaybackClock::qpcFrequency() const noexcept { return m_qpcFrequency; }

} // namespace Colosseum::Player2
