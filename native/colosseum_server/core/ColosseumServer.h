#pragma once

#include "HttpRouter.h"

#include <QThread>
#include <QUrl>
#include <QSslConfiguration>

#include <memory>

namespace colosseum::server {

class ServerWorker;

class ColosseumServer final
{
public:
    explicit ColosseumServer(std::shared_ptr<HttpRouter> router = {});
    ~ColosseumServer();

    ColosseumServer(const ColosseumServer &) = delete;
    ColosseumServer &operator=(const ColosseumServer &) = delete;

    bool start(quint16 port = 0);
    bool startTls(quint16 port, const QSslConfiguration &configuration);
    void stop();

    bool isRunning() const noexcept { return m_running; }
    QUrl boundUrl() const { return m_boundUrl; }
    QString lastError() const { return m_lastError; }

    HttpRouter &router() noexcept { return *m_router; }
    const HttpRouter &router() const noexcept { return *m_router; }
    qsizetype activeConnectionCount() const;

private:
    bool startInternal(quint16 port, bool tls, const QSslConfiguration &configuration);

    std::shared_ptr<HttpRouter> m_router;
    QThread *m_thread = nullptr;
    ServerWorker *m_worker = nullptr;
    bool m_running = false;
    QUrl m_boundUrl;
    QString m_lastError;
};

} // namespace colosseum::server
