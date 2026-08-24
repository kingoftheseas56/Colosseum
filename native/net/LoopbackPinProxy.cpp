#include "net/LoopbackPinProxy.h"

#include "net/Ipv4PinStore.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

class Tunnel : public QObject {
public:
    Tunnel(QTcpSocket* client, Ipv4PinStore* pinStore, QObject* parent)
        : QObject(parent), m_client(client), m_pinStore(pinStore) {
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
            if (m_reqBuf.size() > 8192) deleteLater();
            return;
        }
        const QByteArray line = m_reqBuf.left(m_reqBuf.indexOf("\r\n"));
        const QByteArray leftover = m_reqBuf.mid(hdrEnd + 4);
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 2 || parts.first().toUpper() != "CONNECT") { deleteLater(); return; }
        const QByteArray hostport = parts.at(1);
        const int colon = hostport.lastIndexOf(':');
        if (colon < 0) { deleteLater(); return; }
        const QString host = QString::fromUtf8(hostport.left(colon));
        const quint16 port = hostport.mid(colon + 1).toUShort();
        const QString ipv4 = m_pinStore ? m_pinStore->pinForHost(host) : QString();
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
            if (!m_connected) deleteLater();
        });
        m_upstream->connectToHost(QHostAddress(ipv4), port);
    }

    void onUpstreamClosed() {
        if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
            m_client->write(m_upstream->readAll());
            m_client->disconnectFromHost();
        } else {
            deleteLater();
        }
    }

    void onClientClosed() {
        if (m_upstream && m_upstream->state() == QAbstractSocket::ConnectedState) {
            m_upstream->write(m_client->readAll());
            m_upstream->disconnectFromHost();
        }
        deleteLater();
    }

    QTcpSocket* m_client;
    Ipv4PinStore* m_pinStore = nullptr;
    QTcpSocket* m_upstream = nullptr;
    QByteArray m_reqBuf;
    bool m_connected = false;
};

} // namespace
LoopbackPinProxy::LoopbackPinProxy(Ipv4PinStore* pinStore, QObject* parent)
    : QObject(parent), m_pinStore(pinStore) {}

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
        new Tunnel(m_server->nextPendingConnection(), m_pinStore, this);
}
