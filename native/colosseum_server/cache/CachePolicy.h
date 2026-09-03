#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>
#include <optional>

namespace colosseum::server {

struct CacheEntry
{
    QString path;
    qint64 size = 0;
    qint64 accessTimeMs = 0;
    bool activeEngine = false;
};

struct DiskSpace
{
    qint64 size = 0;
    qint64 free = 0;
};

struct CacheTrimResult
{
    qint64 current = 0;
    double target = 0.0;
    int deleted = 0;
    QStringList deletedPaths;
};

class CachePolicy
{
public:
    static QJsonArray options(const QStringList &ipv4Addresses,
                              const QStringList &cacheLocations);
    static QStringList discoverCacheLocations();
    static std::optional<DiskSpace> diskSpace(const QString &cachePath);

    static double legacyNumber(const QJsonValue &value);
    static QStringList planDeletions(QVector<CacheEntry> entries,
                                     double toSize,
                                     std::optional<qint64> freeSpace = std::nullopt,
                                     qint64 requiredSize = 0);

    static QString cachePath(const QString &cacheRoot, const QString &key);
    static QVector<CacheEntry> scanEntries(const QString &cachePath,
                                           const QStringList &activeEngineIds);
    static CacheTrimResult clearCache(const QString &cachePath,
                                      const QJsonValue &toSize,
                                      qint64 requiredSize,
                                      const QStringList &activeEngineIds,
                                      std::optional<DiskSpace> space = std::nullopt,
                                      bool querySystemDiskSpace = true);
};

class CacheCleanupDebouncer
{
public:
    static constexpr int legacyDelayMs = 10000;

    explicit CacheCleanupDebouncer(std::function<void(const QJsonValue &)> callback,
                                   int delayMs = legacyDelayMs);
    void setOptionValues(const QJsonObject &values);

private:
    std::function<void(const QJsonValue &)> m_callback;
    QJsonValue m_latestCacheSize;
    QTimer m_timer;
};

} // namespace colosseum::server
