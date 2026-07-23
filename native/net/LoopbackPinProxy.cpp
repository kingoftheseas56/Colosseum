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
// IPv4 upstream, reply 200, then blind-relay bytes both ways. Self-deletes when
// either side closes or the handshake fails. No Q_OBJECT: it declares no signals
// or slots of its own — every connect() uses a function pointer (moc-free).
class Tunnel : public QObject {
public:
    Tunnel(QTcpSocket* client, const QHash<QString, QString>& pins, QObject* parent)
        : QObject(parent), m_client(client), m_pins(pins) {
        m_client->setParent(this);
        connect(m_client, &QTcpSocket::readyRead, this, &Tunnel::onClientData);
        connect(m_client, &QTcpSocket::disconnected, this, &QObject::deleteLater);
    }

private:
    void onClientData() {
        if (m_connected) { m_upstream->write(m_client->readAll()); return; }
        m_reqBuf += m_client->readAll();
        if (m_reqBuf.indexOf("\r\n\r\n") < 0) {
            if (m_reqBuf.size() > 8192) deleteLater();   // runaway header → bail
            return;
        }
        const QByteArray line = m_reqBuf.left(m_reqBuf.indexOf("\r\n"));
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
        connect(m_upstream, &QTcpSocket::connected, this, [this] {
            m_connected = true;
            m_client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
        });
        connect(m_upstream, &QTcpSocket::readyRead, this, [this] {
            m_client->write(m_upstream->readAll());
        });
        connect(m_upstream, &QTcpSocket::disconnected, this, &QObject::deleteLater);
        connect(m_upstream, &QAbstractSocket::errorOccurred, this, [this] { deleteLater(); });
        m_upstream->connectToHost(QHostAddress(ipv4), port);
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
