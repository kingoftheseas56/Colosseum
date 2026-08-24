#pragma once
// Routes scoped hosts through the loopback concierge only when a live/cached IPv4
// pin exists. Cold first-run requests therefore use NoProxy until Ipv4PinStore has
// asynchronously produced a pin instead of being black-holed by an empty proxy.
#include "net/Ipv4PinStore.h"

#include <QNetworkProxyFactory>
#include <QSet>
#include <QString>

class PinProxyFactory : public QNetworkProxyFactory {
public:
    PinProxyFactory(QSet<QString> hosts, Ipv4PinStore* pinStore, quint16 port, bool enabled)
        : m_hosts(std::move(hosts)), m_pinStore(pinStore), m_port(port), m_enabled(enabled) {}

    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override {
        const QString host = query.peerHostName().toLower();
        if (m_enabled && m_pinStore && m_hosts.contains(host)
            && !m_pinStore->pinForHost(host).isEmpty()) {
            return { QNetworkProxy(QNetworkProxy::HttpProxy,
                                   QStringLiteral("127.0.0.1"), m_port) };
        }
        return { QNetworkProxy(QNetworkProxy::NoProxy) };
    }

private:
    QSet<QString> m_hosts;
    Ipv4PinStore* m_pinStore = nullptr;
    quint16 m_port = 0;
    bool m_enabled = false;
};
