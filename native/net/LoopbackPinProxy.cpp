// LoopbackPinProxy.cpp — see header. [Agent 0 (Claude), foundation]
#include "LoopbackPinProxy.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QTcpServer>
#include <QTcpSocket>

#include <utility>

namespace {

// One CONNECT tunnel: read the CONNECT line from the client, dial the pinned
// IPv4 upstream, reply 200, then blind-relay bytes both ways. TEARDOWN IS THE
// SUBTLE PART: when one side closes we must DRAIN the last bytes and let the
// other side's write buffer FLUSH before deleting — deleting eagerly discards
// unflushed bytes and truncates the final image ("Unsupported image format").
// No Q_OBJECT: it declares no signals/slots; every connect() uses a PMF (moc-free).
class Tunnel : public QObject {
public:
    Tunnel(QTcpSocket* client, const QHash<QString, QString>& pins, QObject* parent)
        : QObject(parent), m_client(client), m_pins(pins) {
        m_client->setParent(this);
        connect(m_client, &QTcpSocket::readyRead, this, &Tunnel::onClientData);
        connect(m_client, &QTcpSocket::disconnected, this, &Tunnel::onClientClosed);
    }

private:
    void onClientData() {
        if (m_connected) { if (m_upstream) m_upstream->write(m_client->readAll()); return; }
        m_reqBuf += m_client->readAll();
        const int hdrEnd = m_reqBuf.indexOf("\r\n\r\n");
        if (hdrEnd < 0) {
            if (m_reqBuf.size() > 8192) deleteLater();   // runaway header → bail
            return;
        }
        const QByteArray line = m_reqBuf.left(m_reqBuf.indexOf("\r\n"));
        const QByteArray leftover = m_reqBuf.mid(hdrEnd + 4);   // bytes after the header (if pipelined)
        const QList<QByteArray> parts = line.split(' ');   // "CONNECT host:port HTTP/1.1"
        if (parts.size() < 2 || parts.first().toUpper() != "CONNECT") { deleteLater(); return; }
        const QByteArray hostport = parts.at(1);
        const int colon = hostport.lastIndexOf(':');
        if (colon < 0) { deleteLater(); return; }
        const QString host = QString::fromUtf8(hostport.left(colon));
        const quint16 port = hostport.mid(colon + 1).toUShort();
        const QString ipv4 = resolve(host);
        if (ipv4.isEmpty()) { deleteLater(); return; }

        m_upstream = new QTcpSocket(this);
        connect(m_upstream, &QTcpSocket::connected, this, [this, leftover] {
            m_connected = true;
            m_client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
            if (!leftover.isEmpty()) m_upstream->write(leftover);
        });
        connect(m_upstream, &QTcpSocket::readyRead, this, [this] {
            if (m_client) m_client->write(m_upstream->readAll());
        });
        connect(m_upstream, &QTcpSocket::disconnected, this, &Tunnel::onUpstreamClosed);
        connect(m_upstream, &QAbstractSocket::errorOccurred, this, [this] {
            if (!m_connected) deleteLater();   // pre-connect failure; a normal close fires disconnected
        });
        m_upstream->connectToHost(QHostAddress(ipv4), port);
    }

    // Upstream done: hand the client its final bytes, then close the client
    // GRACEFULLY (disconnectFromHost flushes the write buffer, then fires
    // disconnected → we delete). This is what keeps the last image whole.
    void onUpstreamClosed() {
        if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
            m_client->write(m_upstream->readAll());
            m_client->disconnectFromHost();
        } else {
            deleteLater();
        }
    }

    // Client gone (either it closed, or our graceful disconnect finished): flush
    // anything pending upstream, then tear down.
    void onClientClosed() {
        if (m_upstream && m_upstream->state() == QAbstractSocket::ConnectedState) {
            m_upstream->write(m_client->readAll());
            m_upstream->disconnectFromHost();
        }
        deleteLater();
    }

    // Map first (the boot-resolved pin), else a synchronous IPv4-only lookup. For
    // the scoped metahub use the host is always in the map, so no blocking lookup
    // runs on the event loop in practice.
    QString resolve(const QString& host) const {
        const auto it = m_pins.constFind(host);
        if (it != m_pins.constEnd()) return it.value();
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) return a.toString();
        return {};
    }

    QTcpSocket* m_client;
    QTcpSocket* m_upstream = nullptr;
    QHash<QString, QString> m_pins;
    QByteArray m_reqBuf;
    bool m_connected = false;
};

} // namespace

LoopbackPinProxy::LoopbackPinProxy(QHash<QString, QString> ipv4ByHost, QObject* parent)
    : QObject(parent), m_ipv4ByHost(std::move(ipv4ByHost)) {}

bool LoopbackPinProxy::start() {
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &LoopbackPinProxy::onNewConnection);
    m_port = m_server->serverPort();
    return true;
}

void LoopbackPinProxy::onNewConnection() {
    while (m_server->hasPendingConnections())
        new Tunnel(m_server->nextPendingConnection(), m_ipv4ByHost, this);
}
