#include "PorticoTrendCache.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

PorticoTrendCache::PorticoTrendCache(QString directory)
{
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/portico-trends");
    }
    setDirectory(directory);
}

void PorticoTrendCache::setDirectory(const QString &directory)
{
    m_directory = QDir::cleanPath(directory);
    if (!m_directory.isEmpty())
        QDir().mkpath(m_directory);
}

QString PorticoTrendCache::filePath(const QString &sourceId) const
{
    QString safe = sourceId;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                 QStringLiteral("_"));
    return QDir(m_directory).filePath(safe + QStringLiteral(".json"));
}

bool PorticoTrendCache::save(const PorticoTrend::Shelf &shelf, QString *error) const
{
    if (m_directory.isEmpty()) {
        if (error) *error = QStringLiteral("Cache directory is empty.");
        return false;
    }
    QDir().mkpath(m_directory);
    QSaveFile file(filePath(shelf.id.isEmpty() ? shelf.sourceId : shelf.id));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray json = QJsonDocument::fromVariant(shelf.toVariantMap())
                                .toJson(QJsonDocument::Compact);
    if (file.write(json) != json.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

PorticoTrendCache::Result PorticoTrendCache::load(
    const QString &sourceId, qint64 maxAgeSeconds, bool allowExpired) const
{
    Result result;
    QFile file(filePath(sourceId));
    if (!file.exists())
        return result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("Invalid cache JSON: %1").arg(parseError.errorString());
        return result;
    }

    result.shelf = PorticoTrend::shelfFromVariantMap(document.object().toVariantMap());
    if (result.shelf.sourceId.isEmpty()) {
        result.error = QStringLiteral("Cached shelf has no source id.");
        return result;
    }

    const qint64 age = result.shelf.fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
    result.stale = maxAgeSeconds >= 0 && age > maxAgeSeconds;
    if (result.stale && !allowExpired)
        return result;

    result.found = true;
    result.shelf.stale = result.stale;
    return result;
}

bool PorticoTrendCache::remove(const QString &sourceId) const
{
    const QString path = filePath(sourceId);
    return !QFile::exists(path) || QFile::remove(path);
}

bool PorticoTrendCache::clear() const
{
    QDir dir(m_directory);
    bool ok = true;
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        ok = dir.remove(name) && ok;
    return ok;
}
