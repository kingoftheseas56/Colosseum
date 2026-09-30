#include "VaultAndroidIndexAdapter.h"

#include "VaultIdentity.h"
#include "VaultKit.h"
#include "VaultLocation.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

VaultAndroidIndexAdapter::VaultAndroidIndexAdapter(VaultIndex* index, VaultIdentity* identity)
    : m_index(index), m_identity(identity)
{
}

QString VaultAndroidIndexAdapter::cleanRelativePath(const QString& path)
{
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList clean;
    for (const QString& segment : normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (segment == QLatin1String("."))
            continue;
        if (segment == QLatin1String("..")) {
            if (!clean.isEmpty())
                clean.removeLast();
            continue;
        }
        clean.append(segment);
    }
    return clean.join(QLatin1Char('/'));
}

QString VaultAndroidIndexAdapter::mediaKind(const MediaEntry& entry)
{
    const QString mime = entry.mimeType.trimmed().toLower();
    if (mime.startsWith(QLatin1String("video/")))
        return QStringLiteral("video");
    if (mime == QLatin1String("application/vnd.comicbook+zip")
        || mime == QLatin1String("application/vnd.comicbook-rar")
        || mime == QLatin1String("application/x-cbz")
        || mime == QLatin1String("application/x-cbr")) {
        return QStringLiteral("comic");
    }
    if (mime == QLatin1String("application/epub+zip")
        || mime == QLatin1String("application/pdf")
        || mime == QLatin1String("application/x-mobipocket-ebook")
        || mime == QLatin1String("application/x-fictionbook+xml")) {
        return QStringLiteral("book");
    }

    const VaultKit::MediaKind kind = VaultKit::kindForFile(entry.displayName);
    return kind == VaultKit::MediaKind::Unknown ? QString() : VaultKit::kindName(kind);
}

VaultIndex::FileRow VaultAndroidIndexAdapter::rowForEntry(
    const QString& rootUri, const MediaEntry& entry) const
{
    VaultIndex::FileRow row;
    const QString root = VaultLocation::normalize(rootUri);
    const QString kind = mediaKind(entry);
    if (root.isEmpty() || entry.uri.isEmpty() || entry.displayName.isEmpty() || kind.isEmpty())
        return row;

    const QString relative = cleanRelativePath(entry.relativePath);
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString baseName = QFileInfo(entry.displayName).completeBaseName();
    const QString groupRaw = parts.isEmpty() ? baseName : parts.first();
    const QString groupTitle = VaultKit::cleanMediaFolderTitle(groupRaw);
    const QString subtree = parts.isEmpty()
        ? root
        : root + (root.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + parts.first();

    row.rootPath = root;
    row.subtreePath = subtree;
    row.groupKey = subtree;
    row.groupTitle = groupTitle.isEmpty() ? groupRaw : groupTitle;
    row.parsedYear = VaultKit::parsedTitleYear(groupRaw);
    row.kind = kind;
    row.path = entry.uri;
    row.realName = entry.displayName;
    const QString displayTitle = VaultKit::cleanMediaFolderTitle(baseName);
    row.displayTitle = displayTitle.isEmpty() ? baseName : displayTitle;
    row.subfolder = parts.size() > 1 ? parts.mid(1).join(QLatin1Char('/')) : QString();
    row.size = entry.sizeBytes;
    row.mtimeMs = entry.modifiedMs;
    row.format = QFileInfo(entry.displayName).suffix().toLower();
    if (kind == QLatin1String("video") && entry.durationMs >= 0)
        row.durationSec = static_cast<double>(entry.durationMs) / 1000.0;
    return row;
}

VaultAndroidIndexAdapter::ApplyResult VaultAndroidIndexAdapter::applySnapshot(
    const SourceSnapshot& snapshot)
{
    ApplyResult result;
    if (!m_index || !m_identity || snapshot.rootUri.isEmpty())
        return result;

    const QString root = VaultLocation::normalize(snapshot.rootUri);
    if (!snapshot.available) {
        m_index->markRootAway(root, true);
        result.ok = true;
        result.sourceAway = true;
        result.indexedCount = m_index->rowsForRoot(root).size();
        return result;
    }

    const QList<VaultIndex::FileRow> oldRootRows = m_index->rowsForRoot(root);
    QSet<QString> oldIds;
    for (const VaultIndex::FileRow& row : oldRootRows)
        oldIds.insert(row.id);

    QList<VaultIndex::FileRow> keptRows;
    for (const QString& kind : {QStringLiteral("comic"), QStringLiteral("book"),
                                QStringLiteral("video")}) {
        for (const VaultIndex::FileRow& row : m_index->rowsForKind(kind)) {
            if (VaultLocation::normalize(row.rootPath) != root)
                keptRows.append(row);
        }
    }

    QSet<QString> seenUris;
    QList<VaultIndex::FileRow> mappedRows;
    for (const MediaEntry& entry : snapshot.entries) {
        if (entry.uri.isEmpty() || seenUris.contains(entry.uri))
            continue;
        seenUris.insert(entry.uri);
        const VaultIndex::FileRow row = rowForEntry(root, entry);
        if (!row.path.isEmpty() && !row.kind.isEmpty())
            mappedRows.append(row);
    }

    QList<VaultIdentity::FileFacts> facts;
    facts.reserve(keptRows.size() + mappedRows.size());
    auto appendFact = [&facts](const VaultIndex::FileRow& row) {
        facts.append(VaultIdentity::FileFacts{row.path, row.size, row.mtimeMs});
    };
    for (const VaultIndex::FileRow& row : keptRows)
        appendFact(row);
    for (const VaultIndex::FileRow& row : mappedRows)
        appendFact(row);
    m_identity->reconcile(facts);

    QHash<QString, VaultIndex::FileRow> oldById;
    oldById.reserve(oldRootRows.size());
    for (const VaultIndex::FileRow& row : oldRootRows)
        oldById.insert(row.id, row);

    QSet<QString> currentIds;
    QList<VaultIndex::FileRow> arrivals;
    arrivals.reserve(mappedRows.size());
    for (VaultIndex::FileRow row : mappedRows) {
        row.id = m_identity->idForFile(row.path, row.size, row.mtimeMs);
        currentIds.insert(row.id);

        const auto old = oldById.constFind(row.id);
        if (old != oldById.cend()) {
            VaultIndex::FileRow merged = *old;
            merged.rootPath = row.rootPath;
            merged.subtreePath = row.subtreePath;
            merged.groupKey = row.groupKey;
            merged.groupTitle = row.groupTitle;
            merged.kind = row.kind;
            merged.path = row.path;
            merged.displayTitle = row.displayTitle;
            merged.realName = row.realName;
            merged.subfolder = row.subfolder;
            merged.sortKey.clear();
            merged.size = row.size;
            merged.mtimeMs = row.mtimeMs;
            merged.format = row.format;
            merged.parsedYear = row.parsedYear;
            if (row.durationSec >= 0)
                merged.durationSec = row.durationSec;
            merged.away = false;
            row = merged;
        } else {
            row.away = false;
        }
        arrivals.append(row);
    }

    int removed = 0;
    if (!m_index->reconcileRoot(root, currentIds, arrivals, &removed))
        return result;

    result.ok = true;
    result.indexedCount = currentIds.size();
    result.removedCount = removed;
    return result;
}
