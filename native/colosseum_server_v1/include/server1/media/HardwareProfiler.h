#pragma once
#include "server1/settings/SettingsStore.h"
#include <QObject>
#include <QJsonValue>
#include <QUrl>
#include <functional>
#include <memory>
namespace server1::media {
// M805 tests real conversions through the existing loopback HLS route. A listed
// codec/encoder is never considered evidence of usable hardware acceleration.
class HardwareProfiler final : public QObject {
public:
    // Native bounded-I/O deviation: each real HTTP request has a wall-clock
    // deadline (15 seconds by default). Timeouts never count as success.
    using Result = std::function<void(QJsonValue)>; // array of verified names, or false
    using FetchResult = std::function<void(bool responded, bool ok)>;
    using Fetch = std::function<void(QUrl, FetchResult)>;
    HardwareProfiler(settings::SettingsStore &settings, std::function<void()> settingsChanged,
                     QObject *parent=nullptr, QString platform={}, Fetch fetch={}, int requestTimeoutMs=15000);
    ~HardwareProfiler() override;
    // An absent callback preserves source startup detection/skip semantics.
    void run(unsigned short port, Result callback={});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
