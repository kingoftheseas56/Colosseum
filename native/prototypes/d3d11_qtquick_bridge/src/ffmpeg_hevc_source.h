#pragma once

#include <QString>

#include <atomic>
#include <functional>

class SharedBridge;

class FfmpegHevcSource
{
public:
    explicit FfmpegHevcSource(SharedBridge *bridge);
    bool run(const QString &filePath, const std::atomic_bool &stop,
             const std::function<void()> &wakeConsumer);

private:
    SharedBridge *m_bridge = nullptr;
};
