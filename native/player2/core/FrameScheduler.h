#pragma once

#include <QtCore/QtTypes>

#include <vector>

namespace Colosseum::Player2 {

struct FrameCandidate
{
    quint64 sequence = 0;
    qint64 ptsUs = 0;
};

enum class FrameScheduleAction
{
    Present,
    DropLate,
    RepeatCurrent,
    WaitUntilQpc
};

struct FrameScheduleDecision
{
    FrameScheduleAction action = FrameScheduleAction::RepeatCurrent;
    std::size_t candidateIndex = 0;
    std::size_t dropCount = 0;
    qint64 deadlineQpc = 0;
    qint64 timingErrorUs = 0;
};

struct FrameSchedulerConfig
{
    qint64 earlyToleranceUs = 2'000;
    qint64 lateDropThresholdUs = 40'000;
    std::size_t maximumConsecutiveDrops = 3;
};

class FrameScheduler
{
public:
    explicit FrameScheduler(qint64 qpcFrequency = 0,
                            FrameSchedulerConfig config = {});

    FrameScheduleDecision choose(qint64 masterUs, qint64 qpcNow,
                                 const std::vector<FrameCandidate> &frames);
    void reset();
    qint64 qpcFrequency() const noexcept;

private:
    qint64 m_qpcFrequency = 1;
    FrameSchedulerConfig m_config;
    std::size_t m_consecutiveDrops = 0;
};

} // namespace Colosseum::Player2
