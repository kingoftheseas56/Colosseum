#pragma once
// LoopbackPinProxy — a tiny 127.0.0.1 HTTP CONNECT proxy that pins the CONNECTION
// (not the URL) to a host's IPv4, so Qt keeps the hostname in the request and
// HTTP/2 (SNI, cert, :authority) negotiates end-to-end over the blind tunnel.
// Spec 2026-07-23 (instant posters). Never terminates TLS — moves opaque bytes.
#include <QHash>
#include <QObject>
#include <QString>
class QTcpServer;

class LoopbackPinProxy : public QObject {
    Q_OBJECT
public:
    // ipv4ByHost: host → pinned IPv4 (reuse main.cpp's boot-resolved map). Hosts
    // absent from the map are resolved IPv4-only on demand.
    explicit LoopbackPinProxy(QHash<QString, QString> ipv4ByHost, QObject* parent = nullptr);
    bool start();                 // bind 127.0.0.1:auto; false if it can't listen
    quint16 port() const { return m_port; }
    bool ok() const { return m_port != 0; }

private:
    void onNewConnection();
    QHash<QString, QString> m_ipv4ByHost;
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;
};
