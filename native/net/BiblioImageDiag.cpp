#include "BiblioImageDiag.h"

#include <QElapsedTimer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

namespace {
// One process-wide monotonic clock for request timing. Function-local static:
// initialized thread-safely on first use, valid for the process lifetime.
qint64 nowNs()
{
    static QElapsedTimer timer = [] { QElapsedTimer t; t.start(); return t; }();
    return timer.nsecsElapsed();
}
} // namespace

void BiblioImageDiag::track(QNetworkReply *reply, const QUrl &requestedUrl)
{
    const qint64 started = nowNs();
    const QString url = requestedUrl.toString();
    // No receiver context on purpose (same contract as PosterScoreboard::record):
    // the lambda runs on the reply's own thread and push() is mutex-guarded.
    QObject::connect(reply, &QNetworkReply::finished, [this, reply, url, started] {
        Row row;
        row.url = url;
        const QUrl final = reply->url();
        if (final.toString() != url)
            row.finalUrl = final.toString();
        row.startedNs = started;
        row.finishedNs = nowNs();
        row.httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        row.networkError = int(reply->error());
        if (reply->error() != QNetworkReply::NoError)
            row.errorString = reply->errorString();
        row.contentType =
            reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QVariant clen = reply->header(QNetworkRequest::ContentLengthHeader);
        row.bytes = clen.isValid() ? clen.toLongLong() : reply->bytesAvailable();
        row.cacheHit =
            reply->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool();
        push(std::move(row));
    });
}

void BiblioImageDiag::push(Row row)
{
    QMutexLocker lock(&m_mutex);
    if (m_rows.size() >= kCapacity)
        m_rows.removeFirst();
    m_rows.append(std::move(row));
}

QVariantMap BiblioImageDiag::rowToMap(const Row &r)
{
    QVariantMap m;
    m.insert(QStringLiteral("url"), r.url);
    m.insert(QStringLiteral("finalUrl"), r.finalUrl);
    m.insert(QStringLiteral("startedNs"), r.startedNs);
    m.insert(QStringLiteral("finishedNs"), r.finishedNs);
    m.insert(QStringLiteral("durationMs"),
             double(r.finishedNs - r.startedNs) / 1e6);
    m.insert(QStringLiteral("httpStatus"), r.httpStatus);
    m.insert(QStringLiteral("networkError"), r.networkError);
    m.insert(QStringLiteral("errorString"), r.errorString);
    m.insert(QStringLiteral("contentType"), r.contentType);
    m.insert(QStringLiteral("bytes"), r.bytes);
    m.insert(QStringLiteral("cacheHit"), r.cacheHit);
    return m;
}

QVariantList BiblioImageDiag::rowsForUrl(const QString &urlFragment) const
{
    QVariantList out;
    if (urlFragment.isEmpty())
        return out;
    QMutexLocker lock(&m_mutex);
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (m_rows[i].url.contains(urlFragment) || m_rows[i].finalUrl.contains(urlFragment))
            out.append(rowToMap(m_rows[i]));
    }
    return out;
}

QVariantList BiblioImageDiag::recentRows(const QString &limitText) const
{
    bool ok = false;
    int limit = limitText.toInt(&ok);
    if (!ok || limit <= 0)
        limit = 25;
    QVariantList out;
    QMutexLocker lock(&m_mutex);
    for (int i = m_rows.size() - 1; i >= 0 && out.size() < limit; --i)
        out.append(rowToMap(m_rows[i]));
    return out;
}
