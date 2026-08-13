#include "VaultPosterFetcher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

VaultPosterFetcher::VaultPosterFetcher(QString cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(std::move(cacheDir)), m_nam(new QNetworkAccessManager(this))
{
    QDir().mkpath(postersDir());
}

VaultPosterFetcher::~VaultPosterFetcher()
{
    // No tmp files are ever created before a reply finishes (bytes are held in the
    // reply itself, not streamed to disk mid-flight), so teardown is just aborting
    // whatever is still in the air — nothing on disk to clean up.
    const QList<QNetworkReply*> inFlight = m_jobs.keys();
    for (QNetworkReply* reply : inFlight) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_jobs.clear();
}

QString VaultPosterFetcher::postersDir() const
{
    return QDir(m_cacheDir).filePath(QStringLiteral("posters"));
}

QString VaultPosterFetcher::outputPathForId(const QString& identityId) const
{
    const QByteArray hash = QCryptographicHash::hash(identityId.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(postersDir()).filePath(QString::fromLatin1(hash) + QStringLiteral(".jpg"));
}

QString VaultPosterFetcher::cachedPosterPath(const QString& identityId) const
{
    if (identityId.isEmpty())
        return QString();
    const QString out = outputPathForId(identityId);
    return QFileInfo::exists(out) ? out : QString();
}

QString VaultPosterFetcher::requestPoster(const QString& identityId, const QString& url)
{
    if (identityId.isEmpty() || url.isEmpty())
        return QString();

    const QString out = outputPathForId(identityId);

    // Idempotent cache-hit short-circuit — the negative control disables exactly this
    // branch to prove the "no re-download on a hit" test actually depends on it.
    if (QFileInfo::exists(out))
        return out;

    if (m_activeIds.contains(identityId))
        return QString(); // already downloading — caller waits on posterReady

    m_activeIds.insert(identityId);

    QNetworkRequest req{QUrl(url)};
    QNetworkReply* reply = m_nam->get(req);
    m_jobs.insert(reply, identityId);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishJob(reply); });

    return QString();
}

void VaultPosterFetcher::finishJob(QNetworkReply* reply)
{
    if (!m_jobs.contains(reply))
        return; // already reaped by teardown
    const QString identityId = m_jobs.take(reply);
    m_activeIds.remove(identityId);

    const bool netOk = reply->error() == QNetworkReply::NoError;
    const QByteArray bytes = netOk ? reply->readAll() : QByteArray();
    reply->deleteLater();

    // Honest failure on ANY of: network/HTTP error (404 maps to a QNetworkReply error,
    // both for http(s):// and the file:// backend used in tests), or an empty body —
    // never a zero-byte or half-written file promoted to the cache.
    if (bytes.isEmpty())
        return;

    QDir().mkpath(postersDir());
    const QString out = outputPathForId(identityId);
    const QString tmp = out + QStringLiteral(".tmp");
    QFile::remove(tmp);

    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly))
        return; // couldn't even stage the tmp file — nothing to clean up, nothing promoted
    const bool wrote = f.write(bytes) == bytes.size();
    f.close();
    if (!wrote) {
        QFile::remove(tmp);
        return;
    }

    QFile::remove(out); // clear any stale leftover before the promote-rename
    if (QFile::rename(tmp, out))
        emit posterReady(identityId, out);
    else
        QFile::remove(tmp); // honest failure — no half-written file left behind
}
