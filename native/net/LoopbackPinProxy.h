#pragma once
// LoopbackPinProxy — a tiny 127.0.0.1 HTTP CONNECT proxy that pins the CONNECTION
// (not the URL) to a host's IPv4, so Qt keeps the hostname in the request and
// HTTP/2 (SNI, cert, :authority) negotiates end-to-end over the blind tunnel.
// Never performs blocking DNS: a host without a live/cached pin fails fast while
// Ipv4PinStore refreshes asynchronously.
#include <QObject>
#include <QString>
class Ipv4PinStore;
class QTcpServer;

class LoopbackPinProxy : public QObject {
public:
    explicit LoopbackPinProxy(Ipv4PinStore* pinStore, QObject* parent = nullptr);
    bool start();
    quint16 port() const { return m_port; }
    bool ok() const { return m_port != 0; }

private:
    void onNewConnection();
    Ipv4PinStore* m_pinStore = nullptr;
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;
};
