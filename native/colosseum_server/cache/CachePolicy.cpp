#include "CachePolicy.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <limits>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace colosseum::server {
namespace {

QJsonObject selection(const QString &name, const QJsonValue &value)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("val"), value}};
}

bool isPriorityDrive(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QChar drive = path.at(0).toUpper();
    return drive == QLatin1Char('E') || drive == QLatin1Char('D');
}

double mathMinLikeJavaScript(double left, double right)
{
    if (std::isnan(left) || std::isnan(right)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::min(left, right);
}

} // namespace

QJsonArray CachePolicy::options(const QStringList &ipv4Addresses,
                                const QStringList &cacheLocations)
{
    // Stremio Server 4.20.17 module 414 getOptions: preserve option ordering,
    // labels, dynamic IPv4 selections, cache sizes and cacheRoot visibility.
    QJsonArray remoteSelections;
    remoteSelections.append(selection(QStringLiteral("Disabled"), QString()));
    for (const QString &address : ipv4Addresses) {
        remoteSelections.append(selection(address, address));
    }

    QJsonArray cacheSelections;
    cacheSelections.append(selection(QStringLiteral("no caching"), 0.0));
    cacheSelections.append(selection(QStringLiteral("2GB"), 2147483648.0));
    cacheSelections.append(selection(QStringLiteral("5GB"), 5368709120.0));
    cacheSelections.append(selection(QStringLiteral("10GB"), 10737418240.0));
    cacheSelections.append(selection(QString::fromUtf8("∞"), QJsonValue::Null));

    QJsonArray result;
    result.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("localAddonEnabled")},
                              {QStringLiteral("label"), QStringLiteral("ENABLE_LOCAL_FILES_ADDON")},
                              {QStringLiteral("type"), QStringLiteral("checkbox")}});
    result.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("remoteHttps")},
                              {QStringLiteral("label"), QStringLiteral("ENABLE_REMOTE_HTTPS_CONN")},
                              {QStringLiteral("type"), QStringLiteral("select")},
                              {QStringLiteral("class"), QStringLiteral("https")},
                              {QStringLiteral("icon"), true},
                              {QStringLiteral("selections"), remoteSelections}});
    result.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("cacheSize")},
                              {QStringLiteral("label"), QStringLiteral("CACHING")},
                              {QStringLiteral("type"), QStringLiteral("select")},
                              {QStringLiteral("class"), QStringLiteral("caching")},
                              {QStringLiteral("icon"), true},
                              {QStringLiteral("selections"), cacheSelections}});

    if (!cacheLocations.isEmpty()) {
        QJsonArray locationSelections;
        for (const QString &location : cacheLocations) {
            locationSelections.append(selection(location.left(2), location));
        }
        result.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("cacheRoot")},
                                  {QStringLiteral("label"), QStringLiteral("SETTINGS_CACHING_DRIVE")},
                                  {QStringLiteral("type"), QStringLiteral("select")},
                                  {QStringLiteral("class"), QStringLiteral("caching")},
                                  {QStringLiteral("selections"), locationSelections}});
    }
    return result;
}

QStringList CachePolicy::discoverCacheLocations()
{
    QStringList locations;
#ifdef Q_OS_WIN
    wchar_t buffer[512] = {};
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buffer)), buffer);
    if (length > 0 && length < std::size(buffer)) {
        const wchar_t *cursor = buffer;
        while (*cursor) {
            const QString root = QString::fromWCharArray(cursor);
            if (GetDriveTypeW(cursor) == DRIVE_FIXED
                && !root.startsWith(QStringLiteral("Q:"), Qt::CaseInsensitive)) {
                locations.append(QDir::toNativeSeparators(root));
            }
            cursor += wcslen(cursor) + 1;
        }
    }
    // Module 414's comparator surprisingly places D:/E: after other fixed drives.
    // Keep that observable order rather than "fixing" the upstream policy.
    std::stable_sort(locations.begin(), locations.end(), [](const QString &a, const QString &b) {
        return isPriorityDrive(a) < isPriorityDrive(b);
    });
#endif
    return locations;
}

std::optional<DiskSpace> CachePolicy::diskSpace(const QString &cachePath)
{
    const QStorageInfo storage(cachePath);
    if (!storage.isValid() || !storage.isReady()) {
        return std::nullopt;
    }
    return DiskSpace{storage.bytesTotal(), storage.bytesAvailable()};
}

double CachePolicy::legacyNumber(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
        return 0.0;
    case QJsonValue::Bool:
        return value.toBool() ? 1.0 : 0.0;
    case QJsonValue::Double:
        return value.toDouble();
    case QJsonValue::String: {
        const QString text = value.toString().trimmed();
        if (text.isEmpty()) {
            return 0.0;
        }
        if (text == QStringLiteral("Infinity") || text == QStringLiteral("+Infinity")) {
            return std::numeric_limits<double>::infinity();
        }
        if (text == QStringLiteral("-Infinity")) {
            return -std::numeric_limits<double>::infinity();
        }
        bool ok = false;
        double result = text.toDouble(&ok);
        return ok ? result : std::numeric_limits<double>::quiet_NaN();
    }
    case QJsonValue::Undefined:
    case QJsonValue::Array:
    case QJsonValue::Object:
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

QStringList CachePolicy::planDeletions(QVector<CacheEntry> entries,
                                       double toSize,
                                       std::optional<qint64> freeSpace,
                                       qint64 requiredSize)
{
    std::stable_sort(entries.begin(), entries.end(), [](const CacheEntry &a, const CacheEntry &b) {
        return a.accessTimeMs > b.accessTimeMs;
    });

    qint64 cacheSize = 0;
    for (const CacheEntry &entry : entries) {
        cacheSize += entry.size;
    }

    if (freeSpace.has_value()) {
        // Module 414 disk-pressure rule is literally cacheSize + free - requiredSize,
        // with JavaScript's falsy-zero fallback before Math.min.
        const double candidate = static_cast<double>(cacheSize)
            + static_cast<double>(*freeSpace) - static_cast<double>(requiredSize);
        const double rhs = (candidate != 0.0 && !std::isnan(candidate)) ? candidate : toSize;
        toSize = mathMinLikeJavaScript(toSize, rhs);
    }

    QStringList deletions;
    qint64 sizeSum = 0;
    for (const CacheEntry &entry : entries) {
        sizeSum += entry.size;
        if (static_cast<double>(sizeSum) > toSize && !entry.activeEngine) {
            deletions.append(entry.path);
        }
    }
    return deletions;
}

QString CachePolicy::cachePath(const QString &cacheRoot, const QString &key)
{
    QString cacheDirectory = QDir(cacheRoot).filePath(QStringLiteral("stremio-cache"));
    if (!QDir().mkpath(cacheDirectory)) {
        cacheDirectory = QDir::tempPath();
        QDir().mkpath(cacheDirectory);
    }
    return QDir(cacheDirectory).filePath(key);
}

QVector<CacheEntry> CachePolicy::scanEntries(const QString &cachePath,
                                             const QStringList &activeEngineIds)
{
    QVector<CacheEntry> entries;
    QDirIterator it(cachePath,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        qint64 accessTimeMs = info.lastRead().toMSecsSinceEpoch();
        if (accessTimeMs <= 0) {
            accessTimeMs = info.lastModified().toMSecsSinceEpoch();
        }
        const QString parentName = QDir(info.absolutePath()).dirName();
        entries.append(CacheEntry{info.absoluteFilePath(),
                                  info.size(),
                                  accessTimeMs,
                                  activeEngineIds.contains(parentName)});
    }
    return entries;
}

CacheTrimResult CachePolicy::clearCache(const QString &cachePath,
                                        const QJsonValue &toSizeValue,
                                        qint64 requiredSize,
                                        const QStringList &activeEngineIds,
                                        std::optional<DiskSpace> space,
                                        bool querySystemDiskSpace)
{
    CacheTrimResult result;
    double toSize = legacyNumber(toSizeValue);
    if (std::isinf(toSize) && toSize > 0.0) {
        return result;
    }

    const QVector<CacheEntry> entries = scanEntries(cachePath, activeEngineIds);
    for (const CacheEntry &entry : entries) {
        result.current += entry.size;
    }

    if (!space.has_value() && querySystemDiskSpace) {
        space = diskSpace(cachePath);
    }
    if (space.has_value()) {
        const double candidate = static_cast<double>(result.current)
            + static_cast<double>(space->free) - static_cast<double>(requiredSize);
        const double rhs = (candidate != 0.0 && !std::isnan(candidate)) ? candidate : toSize;
        toSize = mathMinLikeJavaScript(toSize, rhs);
    }
    result.target = toSize;

    const QStringList deletions = planDeletions(entries, toSize);
    for (const QString &path : deletions) {
        ++result.deleted;
        result.deletedPaths.append(path);
        QFile::remove(path);
    }
    return result;
}

CacheCleanupDebouncer::CacheCleanupDebouncer(
    std::function<void(const QJsonValue &)> callback,
    int delayMs)
    : m_callback(std::move(callback))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(delayMs);
    QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this]() {
        if (m_callback) {
            m_callback(m_latestCacheSize);
        }
    });
}

void CacheCleanupDebouncer::setOptionValues(const QJsonObject &values)
{
    // Module 414 setOptionValues cancels the prior 10s cleanup and lets only
    // the latest dynamic cacheSize value reach the eventual clearCache call.
    m_timer.stop();
    m_latestCacheSize = values.value(QStringLiteral("cacheSize"));
    m_timer.start();
}

} // namespace colosseum::server
