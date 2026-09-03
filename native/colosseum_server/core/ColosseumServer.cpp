#include "ColosseumServer.h"

#include "HttpConnection.h"

#include <QHostAddress>
#include <QMetaObject>
#include <QSet>
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

    bool start(quint16 port, QUrl &boundUrl, QString &error)
    {
        // Module 564 owns listener lifecycle; Arc 44 W03 narrows production binding to loopback.
        if (m_server && m_server->isListening()) {
            boundUrl = currentUrl();
            return true;
        }

        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, [this] { acceptPendingConnections(); });
        if (!m_server->listen(QHostAddress::LocalHost, port)) {
            error = m_server->errorString();
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
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort()));
    }

    void acceptPendingConnections()
    {
        while (m_server && m_server->hasPendingConnections()) {
            QTcpSocket *socket = m_server->nextPendingConnection();
            if (!socket)
                continue;
            auto *connection = new HttpConnection(socket, m_router, this);
            m_connections.insert(connection);
            connect(connection, &QObject::destroyed, this, [this, connection] {
                m_connections.remove(connection);
            });
        }
    }

    std::shared_ptr<HttpRouter> m_router;
    QTcpServer *m_server = nullptr;
    QSet<HttpConnection *> m_connections;
};

ColosseumServer::ColosseumServer()
    : m_router(std::make_shared<HttpRouter>())
{
}

ColosseumServer::~ColosseumServer()
{
    stop();
}

bool ColosseumServer::start(quint16 port)
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
    QMetaObject::invokeMethod(m_worker, [this, port, &started] {
        started = m_worker->start(port, m_boundUrl, m_lastError);
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
