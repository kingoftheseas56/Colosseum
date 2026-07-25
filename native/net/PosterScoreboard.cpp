#include "PosterScoreboard.h"

PosterScoreboard::Bucket PosterScoreboard::classify(int httpStatus, const QString &contentType,
                                                    bool networkError, bool webpDecoderPresent)
{
    if (networkError || httpStatus >= 400)
        return Bucket::NetworkFailed;
    // A finished reply with no error and no status is a wrapped reply (e.g. GunzipReply's
    // inner before attribute forwarding) — treat as arrived rather than inventing a failure.
    const QString ct = contentType.toLower();
    if (ct.startsWith(QLatin1String("image/webp")) && !webpDecoderPresent)
        return Bucket::Undecodable;
    return Bucket::Arrived;
}

void PosterScoreboard::record(const QString &host, int httpStatus, const QString &contentType,
                              qint64 bytes, bool networkError)
{
    const Bucket b = classify(httpStatus, contentType, networkError, m_webpPresent);
    QMutexLocker lock(&m_mutex);
    Row &row = m_rows[host];
    switch (b) {
    case Bucket::Arrived:       ++row.arrived; break;
    case Bucket::NetworkFailed: ++row.failed; break;
    case Bucket::Undecodable:   ++row.undecodable; break;
    }
    if (bytes > 0)
        row.bytes += bytes;
}

QVariantMap PosterScoreboard::summary() const
{
    QMutexLocker lock(&m_mutex);
    QVariantMap out;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        QVariantMap row;
        row.insert(QStringLiteral("arrived"), it->arrived);
        row.insert(QStringLiteral("failed"), it->failed);
        row.insert(QStringLiteral("undecodable"), it->undecodable);
        row.insert(QStringLiteral("bytes"), it->bytes);
        out.insert(it.key(), row);
    }
    return out;
}

QString PosterScoreboard::summaryText() const
{
    QMutexLocker lock(&m_mutex);
    QString out;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        out += QStringLiteral("  %1  arrived=%2 failed=%3 undecodable=%4 bytes=%5\n")
                   .arg(it.key()).arg(it->arrived).arg(it->failed)
                   .arg(it->undecodable).arg(it->bytes);
    }
    return out;
}
