#include "ColosseumServer.h"

#include "HttpConnection.h"

#include <QHostAddress>
#include <QMetaObject>
#include <QSet>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>

#include <utility>

namespace colosseum::server {

class ServerWorker final : public QObject
{
public:
    explicit ServerWorker(std::shared_ptr<HttpRouter> router)
        : m_router(std::move(router))
    {
    }

    bool start(quint16 port, QUrl &boundUrl, QString &error,
               bool tls, const QSslConfiguration &configuration)
    {
        // Module 564 owns listener lifecycle; Arc 44 W03 narrows production binding to loopback.
        if (m_server && m_server->isListening()) {
            boundUrl = currentUrl();
            return true;
        }

        if (tls && (configuration.localCertificate().isNull()
                    || configuration.privateKey().isNull())) {
            error = QStringLiteral("TLS certificate or private key is invalid");
            return false;
        }

        if (tls) {
            auto *sslServer = new QSslServer(this);
            sslServer->setSslConfiguration(configuration);
            // This server presents a certificate; it does not authenticate
            // clients. QSslServer emits sslErrors for peer-side issues, but
            // no client verification is requested by this configuration.
            connect(sslServer, &QSslServer::sslErrors, this,
                    [](QSslSocket *, const QList<QSslError> &) {});
            m_server = sslServer;
        } else {
            m_server = new QTcpServer(this);
        }
        m_tls = tls;
        if (m_tls) {
            connect(m_server, &QTcpServer::pendingConnectionAvailable,
                    this, [this] { acceptPendingConnections(); });
        } else {
            connect(m_server, &QTcpServer::newConnection,
                    this, [this] { acceptPendingConnections(); });
        }
        constexpr int maxPortProbes = 32;
        const quint32 firstPort = port;
        const int probeCount = port == 0 ? 1 : maxPortProbes;
        for (int probe = 0; probe < probeCount; ++probe) {
            const quint32 candidate = firstPort + static_cast<quint32>(probe);
            if (candidate > 65535)
                break;
            if (m_server->listen(QHostAddress::LocalHost, static_cast<quint16>(candidate)))
                break;

            const bool canTryNext = m_server->serverError() == QAbstractSocket::AddressInUseError
                && probe + 1 < probeCount && candidate < 65535;
            error = m_server->errorString();
            if (canTryNext)
                continue;

            delete m_server;
            m_server = nullptr;
            return false;
        }

        if (!m_server || !m_server->isListening()) {
            if (error.isEmpty())
                error = QStringLiteral("Could not bind loopback listener");
            delete m_server;
            m_server = nullptr;
            return false;
        }

        boundUrl = currentUrl();
        return true;
    }

    void stop()
    {
        if (m_server)
            m_server->close();

        const auto connections = m_connections.values();
        for (HttpConnection *connection : connections) {
            if (!connection)
                continue;
            connection->abort();
            delete connection;
        }
        m_connections.clear();

        delete m_server;
        m_server = nullptr;
    }
    qsizetype activeConnectionCount() const noexcept
    {
        return m_connections.size();
    }

private:
    QUrl currentUrl() const
    {
        if (!m_server || !m_server->isListening())
            return {};
        return QUrl(QStringLiteral("%1://127.0.0.1:%2")
                        .arg(m_tls ? QStringLiteral("https") : QStringLiteral("http"))
                        .arg(m_server->serverPort()));
    }

    void acceptPendingConnections()
    {
        while (m_server && m_server->hasPendingConnections()) {
            QTcpSocket *socket = m_server->nextPendingConnection();
            if (!socket)
                continue;
            if (m_tls && !qobject_cast<QSslSocket *>(socket)) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            auto *connection = new HttpConnection(socket, m_router, this);
            m_connections.insert(connection);
            connect(connection, &QObject::destroyed, this, [this, connection] {
                m_connections.remove(connection);
            });
        }
    }

    std::shared_ptr<HttpRouter> m_router;
    QTcpServer *m_server = nullptr;
    bool m_tls = false;
    QSet<HttpConnection *> m_connections;
};

ColosseumServer::ColosseumServer(std::shared_ptr<HttpRouter> router)
    : m_router(router ? std::move(router) : std::make_shared<HttpRouter>())
{
}

ColosseumServer::~ColosseumServer()
{
    stop();
}

bool ColosseumServer::start(quint16 port)
{
    return startInternal(port, false, {});
}

bool ColosseumServer::startTls(quint16 port, const QSslConfiguration &configuration)
{
    return startInternal(port, true, configuration);
}

bool ColosseumServer::startInternal(quint16 port, bool tls,
                                    const QSslConfiguration &configuration)
{
    if (m_running)
        return true;

    m_lastError.clear();
    m_boundUrl = {};
    m_thread = new QThread;
    m_worker = new ServerWorker(m_router);
    m_worker->moveToThread(m_thread);
    QObject::connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();

    bool started = false;
    QMetaObject::invokeMethod(m_worker, [this, port, tls, configuration, &started] {
        started = m_worker->start(port, m_boundUrl, m_lastError, tls, configuration);
    }, Qt::BlockingQueuedConnection);

    if (!started) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
        m_boundUrl = {};
        return false;
    }

    m_running = true;
    return true;
}

void ColosseumServer::stop()
{
    if (!m_thread)
        return;
    if (m_worker && m_thread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, [this] {
            m_worker->stop();
        }, Qt::BlockingQueuedConnection);
    }
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
    m_running = false;
    m_boundUrl = {};
}

qsizetype ColosseumServer::activeConnectionCount() const
{
    if (!m_running || !m_worker || !m_thread || !m_thread->isRunning())
        return 0;

    qsizetype count = 0;
    QMetaObject::invokeMethod(m_worker, [this, &count] {
        count = m_worker->activeConnectionCount();
    }, Qt::BlockingQueuedConnection);
    return count;
}

} // namespace colosseum::server
