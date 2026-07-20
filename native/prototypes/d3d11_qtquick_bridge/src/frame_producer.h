#pragma once

#include <atomic>
#include <functional>
#include <thread>

class SharedBridge;

class FrameProducer
{
public:
    explicit FrameProducer(SharedBridge *bridge);
    ~FrameProducer();

    void startSynthetic(double framesPerSecond, std::function<void()> wakeConsumer);
    void stop();

private:
    SharedBridge *m_bridge = nullptr;
    std::thread m_thread;
    std::atomic_bool m_stop{false};
};
