#include "VaultDownloadsRoot.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QVariantList>
#include <QVariantMap>

namespace {
// Normalize a path the same way VaultConfig::norm does — clean + lowercase on
// Windows — so the synthetic root's path compares consistently across the
// library, the config, and the QML chip.
QString normPath(const QString& p)
{
    QString n = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
}

// Fill the identity-neutral fields of a FileRow for a container download. The
// `id` is intentionally left empty — VaultScanner::applyPublish assigns it via
// VaultIdentity::idForFile, which is what makes the double-count guard work
// (same file under two roots → same id → INSERT OR REPLACE dedupes).
VaultIndex::FileRow baseRow(const QString& rootPath, const QString& seriesTitle,
                            const QString& kind, const QString& filePath,
                            qint64 size, qint64 mtimeMs,
                            const QString& displayTitle)
{
    const QFileInfo fi(filePath);
    // Group episodes under their series, while each standalone download gets
    // its own filesystem-shaped group. The group key doubles as the subtree
    // lookup path, so a title or download id would either collide or require
    // a broader split between grouping and filesystem lookup.
    const QString group = seriesTitle.isEmpty() ? normPath(filePath) : seriesTitle;
    VaultIndex::FileRow r;
    r.rootPath = rootPath;
    r.subtreePath = group;
    r.groupKey = group;
    r.groupTitle = seriesTitle.isEmpty() ? displayTitle : seriesTitle;
    r.kind = kind;
    r.path = filePath;
    r.displayTitle = displayTitle;
    r.realName = fi.fileName();
    r.size = (size > 0) ? size : fi.size();
    r.mtimeMs = (mtimeMs > 0) ? mtimeMs : fi.lastModified().toMSecsSinceEpoch();
    r.format = fi.suffix().toLower();
    return r;
}

QString titleOr(const QVariantMap& m, const QString& fallback)
{
    const QString t = m.value(QStringLiteral("title")).toString();
    return t.isEmpty() ? fallback : t;
}
} // namespace

QVariantList VaultDownloadsRoot::invokeList(QObject* obj, const char* method)
{
    if (!obj)
        return {};
    // The backbone Q_INVOKABLE methods return QVariantList directly; request it
    // exactly (not QVariant) so Qt's metatype match doesn't reject the call.
    QVariantList ret;
    if (!QMetaObject::invokeMethod(obj, method, Qt::DirectConnection,
                                   Q_RETURN_ARG(QVariantList, ret))) {
        return {};
    }
    return ret;
}

QVariantList VaultDownloadsRoot::invokeListWithString(QObject* obj, const char* method,
                                                      const QString& arg)
{
    if (!obj)
        return {};
    QVariantList ret;
    if (!QMetaObject::invokeMethod(obj, method, Qt::DirectConnection,
                                   Q_RETURN_ARG(QVariantList, ret),
                                   Q_ARG(QString, arg))) {
        return {};
    }
    return ret;
}

QList<VaultIndex::FileRow>
VaultDownloadsRoot::rowsFromVideos(QObject* videos, const QString& rootPath)
{
    QList<VaultIndex::FileRow> out;
    if (!videos)
        return out;
    const QVariantList rows = invokeList(videos, "downloadedVideos");
    for (const QVariant& v : rows) {
        const QVariantMap m = v.toMap();
        // `missing` flags a video whose file was deleted on disk since the
        // download — skip it so the Vault never shelves a dead pointer.
        if (m.value(QStringLiteral("missing")).toBool())
            continue;
        const QString path = m.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        out.append(baseRow(rootPath,
                           m.value(QStringLiteral("seriesTitle")).toString(),
                           QStringLiteral("video"),
                           path,
                           m.value(QStringLiteral("bytes")).toLongLong(),
                           m.value(QStringLiteral("addedAt")).toLongLong(),
                           titleOr(m, QFileInfo(path).completeBaseName())));
    }
    return out;
}

QList<VaultIndex::FileRow>
VaultDownloadsRoot::rowsFromBooks(QObject* books, const QString& rootPath)
{
    QList<VaultIndex::FileRow> out;
    if (!books)
        return out;
    const QVariantList rows = invokeList(books, "downloadedBooks");
    for (const QVariant& v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("missing")).toBool())
            continue;
        // downloadedBooks() carries a `path`; fall back to localBook(id) for
        // resilience if the row's path is empty (the backbone's canonical accessor).
        QString path = m.value(QStringLiteral("path")).toString();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (path.isEmpty() && !id.isEmpty()) {
            QString lb;
            if (QMetaObject::invokeMethod(books, "localBook", Qt::DirectConnection,
                                          Q_RETURN_ARG(QString, lb),
                                          Q_ARG(QString, id))) {
                path = lb;
            }
        }
        if (path.isEmpty())
            continue;
        out.append(baseRow(rootPath,
                           m.value(QStringLiteral("title")).toString(),
                           QStringLiteral("book"),
                           path,
                           m.value(QStringLiteral("bytes")).toLongLong(),
                           m.value(QStringLiteral("addedAt")).toLongLong(),
                           titleOr(m, QFileInfo(path).completeBaseName())));
    }
    return out;
}

QList<VaultIndex::FileRow>
VaultDownloadsRoot::rowsFromComics(QObject* comics, const QString& rootPath)
{
    QList<VaultIndex::FileRow> out;
    if (!comics)
        return out;
    const QVariantList rows = invokeList(comics, "downloadedIssues");
    for (const QVariant& v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("missing")).toBool())
            continue;
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        // The CBZ path is only reachable via localPages(id)[0].archive. A row
        // whose issue downloaded as a loose-page dir (no archive key) is NOT
        // Vault-openable — the scanner shelves container files only — so it is
        // skipped here and stays reachable via the Downloads page exactly as
        // today.
        const QVariantList pages = invokeListWithString(comics, "localPages", id);
        if (pages.isEmpty())
            continue;
        const QString archive = pages.first().toMap().value(QStringLiteral("archive")).toString();
        if (archive.isEmpty())
            continue; // loose-page chapter — not Vault-openable
        out.append(baseRow(rootPath,
                           m.value(QStringLiteral("seriesTitle")).toString(),
                           QStringLiteral("comic"),
                           archive,
                           m.value(QStringLiteral("bytes")).toLongLong(),
                           m.value(QStringLiteral("addedAt")).toLongLong(),
                           m.value(QStringLiteral("label")).toString()));
    }
    return out;
}

QList<VaultIndex::FileRow>
VaultDownloadsRoot::rowsFromVolumes(QObject* volumes, const QString& rootPath)
{
    QList<VaultIndex::FileRow> out;
    if (!volumes)
        return out;
    const QVariantList rows = invokeList(volumes, "downloadedVolumes");
    for (const QVariant& v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("missing")).toBool())
            continue;
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        // Same shape as ComicDownloader: the CBZ path is localPages(id)[0].archive.
        const QVariantList pages = invokeListWithString(volumes, "localPages", id);
        if (pages.isEmpty())
            continue;
        const QString archive = pages.first().toMap().value(QStringLiteral("archive")).toString();
        if (archive.isEmpty())
            continue; // legacy loose-page volume — not Vault-openable
        out.append(baseRow(rootPath,
                           m.value(QStringLiteral("seriesTitle")).toString(),
                           QStringLiteral("comic"),
                           archive,
                           m.value(QStringLiteral("bytes")).toLongLong(),
                           m.value(QStringLiteral("addedAt")).toLongLong(),
                           m.value(QStringLiteral("label")).toString()));
    }
    return out;
}

VaultDownloadsRoot::VaultDownloadsRoot(QObject* videos, QObject* books, QObject* comics,
                                       QObject* volumes, QObject* parent)
    : QObject(parent), m_videos(videos), m_books(books), m_comics(comics), m_volumes(volumes)
{
}

QList<VaultIndex::FileRow> VaultDownloadsRoot::rowsForDownloads(const QString& rootPath) const
{
    QList<VaultIndex::FileRow> out;
    out += rowsFromVideos(m_videos, rootPath);
    out += rowsFromBooks(m_books, rootPath);
    out += rowsFromComics(m_comics, rootPath);
    out += rowsFromVolumes(m_volumes, rootPath);
    return out;
}

bool VaultDownloadsRoot::hasContainerDownloads() const
{
    return !rowsForDownloads(QStringLiteral("probe")).isEmpty();
}
