#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStringList>
#include <QUrl>

namespace DownloadMediaPaths {

inline QString rootForSelection(const QUrl &folder)
{
    if (!folder.isLocalFile() || !QDir::isAbsolutePath(folder.toLocalFile()))
        return {};
    const QString path = QDir::cleanPath(folder.toLocalFile());
    if (QFileInfo(path).fileName().compare(QStringLiteral("Colosseum"), Qt::CaseInsensitive) == 0)
        return path;
    return QDir(path).filePath(QStringLiteral("Colosseum"));
}

inline QString defaultRoot()
{
    return rootForSelection(QUrl::fromLocalFile(
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
}

inline QString component(QString text, const QString &fallback)
{
    static const QRegularExpression invalid(QStringLiteral("[\\\\/:*?\"<>|\\p{Cc}]+"));
    static const QRegularExpression reserved(
        QStringLiteral("^(?:CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    text = text.normalized(QString::NormalizationForm_C).simplified();
    text.replace(invalid, QStringLiteral("-"));
    while (text.startsWith(QLatin1Char('.')))
        text.remove(0, 1);
    while (text.toUtf8().size() > 110) {
        if (text.back().isLowSurrogate())
            text.chop(2);
        else
            text.chop(1);
    }
    while (text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    if (text.isEmpty())
        text = fallback;
    if (reserved.match(text).hasMatch())
        text.prepend(QLatin1Char('_'));
    return text;
}

inline QString fileName(const QString &title, const QString &fallback, const QString &suffix)
{
    const QString stem = component(title, fallback);
    if (suffix.isEmpty())
        return stem;
    return stem + QLatin1Char('.') + component(suffix, QStringLiteral("file"));
}

inline QString mangaTitlePath(const QString &series, const QString &unit, const QString &label,
                              const QString &suffix = {})
{
    return QStringLiteral("Manga/") + component(series, QStringLiteral("Unknown series"))
        + QLatin1Char('/') + fileName(label, unit, suffix);
}

inline QString bookPath(const QString &title, const QString &author, const QString &suffix)
{
    // Author distinguishes independent works that share a title without exposing provider keys.
    const QString name = title.isEmpty() ? QStringLiteral("Book") : title;
    const QString stem = author.isEmpty() ? name : name + QStringLiteral(" - ") + author;
    return QStringLiteral("Books/") + fileName(stem, QStringLiteral("Book"), suffix);
}

inline QString audiobookPath(const QString &title, const QString &author)
{
    const QString name = title.isEmpty() ? QStringLiteral("Audiobook") : title;
    const QString stem = author.isEmpty() ? name : name + QStringLiteral(" - ") + author;
    return QStringLiteral("Audiobooks/") + component(stem, QStringLiteral("Audiobook"));
}

inline QString comicPath(const QString &series, const QString &issue, const QString &suffix = {})
{
    const QString title = component(series, QStringLiteral("Unknown series"));
    const QString stem = issue.isEmpty() ? title : issue;
    return QStringLiteral("Comics/") + title + QLatin1Char('/') + fileName(stem, title, suffix);
}

inline QString videoPath(const QString &kind, const QString &title, const QString &subtitle,
                         const QString &seriesTitle, int season, int episode, const QString &suffix)
{
    if (kind == QStringLiteral("movie") || (episode <= 0 && season <= 0 && seriesTitle.isEmpty())) {
        const QString movie = component(title, QStringLiteral("Movie"));
        const QString stem = subtitle.isEmpty() ? movie : movie + QStringLiteral(" - ") + subtitle;
        return QStringLiteral("Movies/") + movie + QLatin1Char('/') + fileName(stem, movie, suffix);
    }
    const QString series = component(seriesTitle.isEmpty() ? title : seriesTitle, QStringLiteral("Series"));
    const QString folder = season > 0 ? QStringLiteral("Season %1").arg(season) : QStringLiteral("Episodes");
    QString stem = series;
    if (episode > 0) {
        if (season > 0)
            stem += QStringLiteral(" - S%1E%2").arg(season, 2, 10, QLatin1Char('0')).arg(episode, 2, 10, QLatin1Char('0'));
        else
            stem += QStringLiteral(" - Episode %1").arg(episode);
    }
    if (!subtitle.isEmpty())
        stem += QStringLiteral(" - ") + subtitle;
    else if (!title.isEmpty() && title != seriesTitle && title != series)
        stem += QStringLiteral(" - ") + title;
    return QStringLiteral("TV/") + series + QLatin1Char('/') + folder + QLatin1Char('/') + fileName(stem, series, suffix);
}

inline QString pathKey(const QString &path)
{
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

inline bool isWithin(const QString &parent, const QString &path)
{
    QString prefix = pathKey(parent);
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    return pathKey(path).startsWith(prefix);
}

inline bool destinationParentsSafe(const QString &root, const QString &path)
{
    if (!QDir::isAbsolutePath(root) || !isWithin(root, path))
        return false;
    const QString rootKey = pathKey(root);
    QString parent = QFileInfo(path).absolutePath();
    while (pathKey(parent) == rootKey || isWithin(root, parent)) {
        const QFileInfo info(parent);
        if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isDir()))
            return false;
        if (pathKey(parent) == rootKey)
            return true;
        parent = QFileInfo(parent).absolutePath();
    }
    return false;
}

// File-system entries and allocations in the current transaction get the same
// collision treatment. A numeric suffix keeps unrelated files and editions visible.
inline QString availablePath(const QString &root, const QString &relativePath,
                             bool directory, const QSet<QString> &reservedPaths = {},
                             const QStringList &suffixes = {})
{
    const QString candidate = QDir::cleanPath(QDir(root).filePath(relativePath));
    if (!destinationParentsSafe(root, candidate))
        return {};
    const QFileInfo desired(candidate);
    const QDir parent(desired.absolutePath());
    const QString suffix = directory ? QString() : desired.suffix();
    const QString stem = suffix.isEmpty() ? desired.fileName() : desired.completeBaseName();
    const QString extension = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
    QSet<QString> occupied = reservedPaths;
    for (const QString &name : parent.entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                | QDir::NoDotAndDotDot)) {
        occupied.insert(pathKey(parent.filePath(name)).toCaseFolded());
    }
    for (int edition = 1; edition <= 10000; ++edition) {
        QString name = stem;
        if (edition > 1)
            name += QStringLiteral(" (%1)").arg(edition);
        const QString path = parent.filePath(name + extension);
        bool conflict = occupied.contains(pathKey(path).toCaseFolded())
            || (!directory && occupied.contains(pathKey(path + QStringLiteral(".part")).toCaseFolded()));
        for (const QString &extraSuffix : suffixes)
            conflict = conflict || occupied.contains(pathKey(path + extraSuffix).toCaseFolded());
        if (!conflict) {
            return path;
        }
    }
    return {};
}

inline QString markerPath(const QString &root)
{
    return QDir(root).filePath(QStringLiteral(".colosseum-downloads"));
}

inline QByteArray fileDigest(const QString &path, qint64 expectedSize)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() != expectedSize)
        return {};
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return hash.result();
}

inline bool copyVerified(const QString &source, const QString &destination)
{
    const qint64 size = QFileInfo(source).size();
    const QByteArray original = fileDigest(source, size);
    if (original.isEmpty() || !QFile::copy(source, destination))
        return false;
    if (fileDigest(destination, size) == original)
        return true;
    QFile::remove(destination);
    return false;
}

inline bool available(const QString &root, const QByteArray &marker)
{
    if (root.isEmpty())
        return false;
    const QFileInfo rootInfo(root);
    if (rootInfo.isSymLink() || rootInfo.isJunction() || (rootInfo.exists() && !rootInfo.isDir()))
        return false;
    const QStorageInfo storage(rootInfo.exists() ? root : rootInfo.absolutePath());
    if (!storage.isValid() || !storage.isReady() || storage.isReadOnly())
        return false;
    if (marker.isEmpty())
        return true;
    const QFileInfo markerInfo(markerPath(root));
    if (!markerInfo.isFile() || markerInfo.isSymLink() || markerInfo.size() != marker.size())
        return false;
    QFile file(markerInfo.filePath());
    return file.open(QIODevice::ReadOnly) && file.readAll() == marker;
}

// The app installs its durable location before constructing the download owners.
// A snapshot is read by the worker-backed pack and archive publishers as well.
struct State {
    QMutex mutex;
    bool enabled = false;
    bool moving = false;
    QString root;
    QByteArray marker;
    QHash<QString, QString> aliases;
    QHash<QString, QByteArray> knownLocations;
    QSet<QString> reserved;
    QString reservationFile;
    QHash<QString, QString> jobPaths;
};

inline State &state()
{
    static State value;
    return value;
}

inline void install(const QString &root, const QByteArray &marker,
                    const QHash<QString, QString> &aliases,
                    const QHash<QString, QByteArray> &knownLocations = {})
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    s.enabled = true;
    s.root = root;
    s.marker = marker;
    s.aliases = aliases;
    s.knownLocations = knownLocations;
    s.knownLocations.insert(root, marker);
    s.reserved.clear();
}

inline void setMoving(bool moving)
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    s.moving = moving;
}

inline void loadJobPaths(const QString &path)
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    s.reservationFile = path;
    s.jobPaths.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        s.jobPaths.insert(it.key(), it.value().toString());
}

inline bool moving()
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    return s.moving;
}

inline bool enabled()
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    return s.enabled;
}

inline bool waitingForDrive(const QString &path)
{
    if (path.isEmpty())
        return false;
    State &s = state();
    QString root;
    QByteArray marker;
    {
        const QMutexLocker lock(&s.mutex);
        for (auto it = s.knownLocations.cbegin(); it != s.knownLocations.cend(); ++it) {
            if (it.key().size() > root.size() && isWithin(it.key(), path)) {
                root = it.key();
                marker = it.value();
            }
        }
    }
    return !root.isEmpty() && !available(root, marker);
}

inline QString resolved(const QString &path)
{
    if (path.isEmpty())
        return {};
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    return s.aliases.value(pathKey(path), path);
}

inline QString root()
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    return s.root;
}

inline bool ready()
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    return !s.enabled || available(s.root, s.marker);
}

inline QString allocate(const QString &relativePath, bool directory = false, const QStringList &suffixes = {},
                        const QString &jobKey = {})
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    if (!available(s.root, s.marker))
        return {};
    QSet<QString> claimed = s.reserved;
    for (auto it = s.jobPaths.cbegin(); it != s.jobPaths.cend(); ++it) {
        if (it.key() != jobKey)
            claimed.insert(pathKey(it.value()).toCaseFolded());
    }
    QString path;
    const QString pinned = s.jobPaths.value(jobKey);
    if (destinationParentsSafe(s.root, pinned)) {
        const QString relativePinned = QDir(s.root).relativeFilePath(pinned);
        if (availablePath(s.root, relativePinned, directory, claimed, suffixes) == pinned)
            path = pinned;
    }
    if (path.isEmpty())
        path = availablePath(s.root, relativePath, directory, claimed, suffixes);
    if (!path.isEmpty()) {
        if (!jobKey.isEmpty() && !s.reservationFile.isEmpty() && path != pinned) {
            QJsonObject object;
            for (auto it = s.jobPaths.cbegin(); it != s.jobPaths.cend(); ++it)
                object.insert(it.key(), it.value());
            object.insert(jobKey, path);
            const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
            QSaveFile file(s.reservationFile);
            if (!QDir().mkpath(QFileInfo(s.reservationFile).absolutePath()) || !file.open(QIODevice::WriteOnly)
                || file.write(payload) != payload.size() || !file.commit())
                return {};
            s.jobPaths.insert(jobKey, path);
        }
        s.reserved.insert(pathKey(path).toCaseFolded());
        for (const QString &suffix : suffixes)
            s.reserved.insert(pathKey(path + suffix).toCaseFolded());
    }
    return path;
}

} // namespace DownloadMediaPaths
