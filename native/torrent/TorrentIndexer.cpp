#include "TorrentIndexer.h"

#include <QNetworkReply>
#include <QSettings>
#include <QVariant>

void TorrentIndexer::startRequestTimer()
{
    m_requestTimer.start();
}

void TorrentIndexer::markSuccess()
{
    if (m_requestTimer.isValid())
        m_lastResponseMs = m_requestTimer.elapsed();
    m_health      = IndexerHealth::Ok;
    m_lastSuccess = QDateTime::currentDateTime();
    m_lastError.clear();
    savePersistedHealth();
}

void TorrentIndexer::markError(QNetworkReply* reply)
{
    if (m_requestTimer.isValid())
        m_lastResponseMs = m_requestTimer.elapsed();

    if (!reply) {
        m_health    = IndexerHealth::Unreachable;
        m_lastError = QStringLiteral("unknown error");
        savePersistedHealth();
        return;
    }

    m_lastError = reply->errorString();

    const auto netErr = reply->error();
    const int  http   = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Cloudflare fingerprint: either the `server: cloudflare` header or a
    // cf-ray header present alongside 403. Keeps the check tight — a generic
    // 403 from a non-CF tracker still classifies as AuthRequired.
    const QByteArray server = reply->rawHeader("Server").toLower();
    const QByteArray cfRay  = reply->rawHeader("CF-Ray");
    const bool isCloudflare = server.contains("cloudflare") || !cfRay.isEmpty();

    if (netErr == QNetworkReply::TimeoutError) {
        m_health = IndexerHealth::Unreachable;
    } else if (http == 429) {
        m_health = IndexerHealth::RateLimited;
    } else if (http == 403 && isCloudflare) {
        m_health = IndexerHealth::CloudflareBlocked;
    } else if (http == 401 || http == 403) {
        m_health = IndexerHealth::AuthRequired;
    } else {
        m_health = IndexerHealth::Unreachable;
    }

    savePersistedHealth();
}

void TorrentIndexer::loadPersistedHealth()
{
    QSettings s;
    const QString base = QStringLiteral("tankorent/indexers/%1/health/").arg(id());

    const QVariant ls = s.value(base + QStringLiteral("lastSuccess"));
    if (ls.isValid()) {
        QDateTime dt = ls.toDateTime();
        if (dt.isValid()) m_lastSuccess = dt;
    }
    m_lastError      = s.value(base + QStringLiteral("lastError")).toString();
    m_lastResponseMs = s.value(base + QStringLiteral("lastResponseMs"), qint64{0}).toLongLong();

    // m_health stays Unknown on boot — no way to know current state without
    // re-querying. Batch 3.2's panel shows "Unknown (last success X ago)"
    // until a live search populates.
}

void TorrentIndexer::savePersistedHealth()
{
    QSettings s;
    const QString base = QStringLiteral("tankorent/indexers/%1/health/").arg(id());
    s.setValue(base + QStringLiteral("lastSuccess"),    m_lastSuccess);
    s.setValue(base + QStringLiteral("lastError"),      m_lastError);
    s.setValue(base + QStringLiteral("lastResponseMs"), m_lastResponseMs);
}

QString TorrentIndexer::healthToString(IndexerHealth h)
{
    switch (h) {
        case IndexerHealth::Unknown:           return QStringLiteral("unknown");
        case IndexerHealth::Ok:                return QStringLiteral("ok");
        case IndexerHealth::Disabled:          return QStringLiteral("disabled");
        case IndexerHealth::AuthRequired:      return QStringLiteral("auth_required");
        case IndexerHealth::CloudflareBlocked: return QStringLiteral("cloudflare_blocked");
        case IndexerHealth::RateLimited:       return QStringLiteral("rate_limited");
        case IndexerHealth::Unreachable:       return QStringLiteral("unreachable");
    }
    return QStringLiteral("unknown");
}

void TorrentIndexer::clearPersistedHealth()
{
    QSettings s;
    const QString base = QStringLiteral("tankorent/indexers/%1/health/").arg(id());
    s.remove(base + QStringLiteral("lastSuccess"));
    s.remove(base + QStringLiteral("lastError"));
    s.remove(base + QStringLiteral("lastResponseMs"));
    m_health         = IndexerHealth::Unknown;
    m_lastSuccess    = QDateTime();
    m_lastError.clear();
    m_lastResponseMs = 0;
}
