#pragma once
// PinProxyFactory — routes ONLY the scoped hosts (metahub image CDN) through the
// loopback concierge; everything else gets NoProxy so the rest of the app's
// networking is untouched. Installed app-wide (setApplicationProxyFactory), but
// NoProxy-for-all-but-metahub keeps it safe. Spec 2026-07-23. [A0, foundation]
#include <QNetworkProxyFactory>
#include <QSet>
#include <QString>

class PinProxyFactory : public QNetworkProxyFactory {
public:
    // enabled=false (concierge failed to bind) → NoProxy for everything, so
    // metahub falls back to the URL-rewrite pin path (today's slow-but-working).
    PinProxyFactory(QSet<QString> hosts, quint16 port, bool enabled)
        : m_hosts(std::move(hosts)), m_port(port), m_enabled(enabled) {}

    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override {
        if (m_enabled && m_hosts.contains(query.peerHostName()))
            return { QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), m_port) };
        return { QNetworkProxy(QNetworkProxy::NoProxy) };
    }

private:
    QSet<QString> m_hosts;
    quint16 m_port;
    bool m_enabled;
};
