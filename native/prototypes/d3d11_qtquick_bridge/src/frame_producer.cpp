#include "frame_producer.h"

#include "shared_bridge.h"

#include <chrono>

FrameProducer::FrameProducer(SharedBridge *bridge)
    : m_bridge(bridge)
{
}

FrameProducer::~FrameProducer()
{
    stop();
}

void FrameProducer::startSynthetic(double framesPerSecond, std::function<void()> wakeConsumer)
{
    stop();
    m_stop = false;
    m_thread = std::thread([this, framesPerSecond, wakeConsumer = std::move(wakeConsumer)] {
        using Clock = std::chrono::steady_clock;
        const auto period = std::chrono::duration<double>(1.0 / framesPerSecond);
        auto next = Clock::now();
        std::uint64_t sequence = 0;
        while (!m_stop.load()) {
            next += std::chrono::duration_cast<Clock::duration>(period);
            if (const auto slot = m_bridge->claimProducerSlot()) {
                ++sequence;
                if (m_bridge->fillSynthetic(*slot, sequence, sequence * 0.08))
                    wakeConsumer();
            } else {
                m_bridge->noteProducerStarved();
            }
            std::this_thread::sleep_until(next);
        }
    });
}

void FrameProducer::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}
