#pragma once
// Default: keep the existing scoped IPv4 concierge policy. Explicit process-only
// opt-in: let Qt resolve the platform proxy policy, preserving canonical hosts.
#include "net/Ipv4PinStore.h"

#include <QHostAddress>
#include <QNetworkProxyFactory>
#include <QSet>
#include <QString>
#include <functional>

class PinProxyFactory : public QNetworkProxyFactory {
public:
    using SystemProxyQuery = std::function<QList<QNetworkProxy>(const QNetworkProxyQuery&)>;

    // Only this exact value opts in. No proxy endpoints or credentials are parsed,
    // persisted or logged here; Qt owns the platform configuration and bypasses.
    static bool systemProxyEnabled() {
        return qgetenv("COLOSSEUM_USE_SYSTEM_PROXY") == QByteArrayLiteral("1");
    }

    PinProxyFactory(QSet<QString> hosts, Ipv4PinStore* pinStore, quint16 port, bool enabled,
                    SystemProxyQuery systemQuery = QNetworkProxyFactory::systemProxyForQuery)
        : m_hosts(std::move(hosts)), m_pinStore(pinStore), m_port(port), m_enabled(enabled),
          m_useSystemProxy(systemProxyEnabled()), m_systemQuery(std::move(systemQuery)) {}

    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override {
        const QString host = query.peerHostName().toLower();
        if (m_useSystemProxy) {
            QString localHost = host;
            if (localHost.endsWith(QLatin1Char('.')))
                localHost.chop(1);
            // Only literal/explicit loopback services bypass unconditionally.
            // Other private networks and hostnames remain subject to Qt's policy.
            if (localHost == QLatin1String("localhost")
                || localHost.endsWith(QLatin1String(".localhost"))
                || QHostAddress(localHost).isLoopback())
                return { QNetworkProxy(QNetworkProxy::NoProxy) };

            const auto configured = m_systemQuery(query);
            QList<QNetworkProxy> proxies;
            for (const auto& proxy : configured) {
                if (proxy.type() != QNetworkProxy::NoProxy
                    && proxy.type() != QNetworkProxy::DefaultProxy)
                    proxies.append(proxy);
            }
            // An explicit proxy must fail closed: never retry direct after its
            // failure, even if a platform/PAC list included a DIRECT fallback.
            if (!proxies.isEmpty())
                return proxies;
            // No configured proxy (including a platform bypass) means direct.
            return { QNetworkProxy(QNetworkProxy::NoProxy) };
        }
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
    bool m_useSystemProxy = false;
    SystemProxyQuery m_systemQuery;
};
