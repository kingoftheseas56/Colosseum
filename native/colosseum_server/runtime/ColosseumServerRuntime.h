#pragma once

#include "core/ColosseumServer.h"

#include <QByteArray>
#include <QSslConfiguration>
#include <QString>
#include <QUrl>

#include <memory>

class TorrentEngine;

namespace colosseum::server::runtime {

struct ColosseumServerRuntimeOptions final
{
    QString appPath;
    QString settingsDirectory;
    quint16 httpPort = 11470;
    quint16 httpsPort = 12470;
    bool enableTls = false;
    bool disableCaching = false;
    TorrentEngine *torrentEngine = nullptr;
    QSslConfiguration tlsConfiguration;
    QUrl certificateEndpoint{QStringLiteral("https://api.strem.io/api/certificateGet")};
    QUrl webUiLocation{QStringLiteral("https://app.strem.io/shell-v4.4/")};
};

// Owns the native equivalent of server.js's process-level composition. HTTP
// and HTTPS share one router and one service graph; the torrent engine is
// started before either listener accepts a request and is stopped after both
// listeners have drained their connection cancellation paths.
class ColosseumServerRuntime final
{
public:
    explicit ColosseumServerRuntime(ColosseumServerRuntimeOptions options = {});
    ~ColosseumServerRuntime();

    ColosseumServerRuntime(const ColosseumServerRuntime &) = delete;
    ColosseumServerRuntime &operator=(const ColosseumServerRuntime &) = delete;

    bool start();
    void stop();

    bool isRunning() const noexcept { return running_; }
    QString lastError() const { return lastError_; }
    QUrl httpUrl() const { return http_.boundUrl(); }
    QUrl httpsUrl() const { return https_.boundUrl(); }

private:
    struct Impl;

    ColosseumServerRuntimeOptions options_;
    std::shared_ptr<HttpRouter> router_;
    std::shared_ptr<Impl> impl_;
    ColosseumServer http_;
    ColosseumServer https_;
    bool running_ = false;
    QString lastError_;
};

} // namespace colosseum::server::runtime
