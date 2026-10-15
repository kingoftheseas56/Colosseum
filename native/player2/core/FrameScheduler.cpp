#include "FrameScheduler.h"

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
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

FrameScheduler::FrameScheduler(qint64 qpcFrequency, FrameSchedulerConfig config)
    : m_qpcFrequency(qpcFrequency > 0 ? qpcFrequency : platformQpcFrequency()),
      m_config(config)
{
    m_config.earlyToleranceUs = std::max<qint64>(0, m_config.earlyToleranceUs);
    m_config.lateDropThresholdUs = std::max<qint64>(0, m_config.lateDropThresholdUs);
    m_config.maximumConsecutiveDrops = std::max<std::size_t>(1, m_config.maximumConsecutiveDrops);
}

FrameScheduleDecision FrameScheduler::choose(qint64 masterUs, qint64 qpcNow,
                                             const std::vector<FrameCandidate> &frames)
{
    if (frames.empty()) {
        m_consecutiveDrops = 0;
        return {FrameScheduleAction::RepeatCurrent};
    }
    const qint64 errorUs = frames.front().ptsUs - masterUs;
    if (errorUs > m_config.earlyToleranceUs) {
        const qint64 qpcDelta = static_cast<qint64>(std::llround(
            static_cast<long double>(errorUs) * m_qpcFrequency / 1'000'000.0L));
        return {FrameScheduleAction::WaitUntilQpc, 0, 0, qpcNow + qpcDelta, errorUs};
    }

    std::size_t drops = 0;
    while (drops < frames.size() && drops < m_config.maximumConsecutiveDrops &&
           masterUs - frames[drops].ptsUs > m_config.lateDropThresholdUs) {
        ++drops;
    }
    if (drops > 0 && m_consecutiveDrops < m_config.maximumConsecutiveDrops) {
        const std::size_t allowed = std::min(drops,
            m_config.maximumConsecutiveDrops - m_consecutiveDrops);
        m_consecutiveDrops += allowed;
        return {FrameScheduleAction::DropLate, allowed, allowed, 0,
                frames.front().ptsUs - masterUs};
    }
    m_consecutiveDrops = 0;
    return {FrameScheduleAction::Present, 0, 0, 0, errorUs};
}

void FrameScheduler::reset() { m_consecutiveDrops = 0; }
qint64 FrameScheduler::qpcFrequency() const noexcept { return m_qpcFrequency; }

} // namespace Colosseum::Player2
