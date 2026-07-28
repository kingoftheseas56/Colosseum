#include "engine/MangaVolumeIndex.h"
#include "engine/CbzArchive.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

#include <utility>

namespace MangaTankoban {
namespace {

// Filesystem-safe path segment (ComicDownloader's convention, lifted).
QString safeSeg(const QString& v)
{
    QString out;
    out.reserve(v.size());
    for (const QChar c : v) {
        if (c.isLetterOrNumber() || c == QChar('.') || c == QChar('_') || c == QChar('-')
            || c == QChar(' '))
            out.append(c);
        else
            out.append(QChar('_'));
    }
    out = out.trimmed();
    while (out.endsWith(QChar('.'))) out.chop(1);
    if (out.isEmpty()) out = QStringLiteral("item");
    return out.left(80);
}

QString hash10(const QString& v)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

} // namespace

MangaVolumeIndex::MangaVolumeIndex(const QString& rootDir, QObject* parent)
    : MangaVolumeIndex(rootDir,
                       [](const QString& path) { return QDir(path).removeRecursively(); },
                       parent)
{
}

MangaVolumeIndex::MangaVolumeIndex(const QString& rootDir,
                                   DownloadFileOps::Remover treeRemover,
                                   QObject* parent)
    : QObject(parent)
    , m_baseDir(QDir::cleanPath(rootDir) + QStringLiteral("/manga-volumes"))
    , m_treeRemover(std::move(treeRemover))
{
    load();
}

QString MangaVolumeIndex::indexPath() const
{
    return m_baseDir + QStringLiteral("/volume-index.json");
}

QString MangaVolumeIndex::pagesDirFor(const VolumeProvenance& record) const
{
    return m_baseDir + QStringLiteral("/pages/") + safeSeg(record.seriesId)
           + QStringLiteral("/vol-") + safeSeg(record.volumeNumber)
           + QChar('-') + hash10(record.id);
}

QString MangaVolumeIndex::archivePathFor(const VolumeProvenance& record) const
{
    return m_baseDir + QStringLiteral("/archives/") + safeSeg(record.seriesId)
           + QStringLiteral("/vol-") + safeSeg(record.volumeNumber)
           + QChar('-') + hash10(record.id) + QStringLiteral(".cbz");
}

void MangaVolumeIndex::load()
{
    m_index.clear();
    QFile f(indexPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.seriesId     = o.value(QStringLiteral("seriesId")).toString();
        e.seriesTitle  = o.value(QStringLiteral("seriesTitle")).toString();
        e.volumeNumber = o.value(QStringLiteral("volumeNumber")).toString();
        e.dir          = o.value(QStringLiteral("dir")).toString();
        e.archive      = o.value(QStringLiteral("archive")).toString();
        e.bytes        = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.sourceKind   = o.value(QStringLiteral("sourceKind")).toString();
        e.releaseTitle = o.value(QStringLiteral("releaseTitle")).toString();
        e.uploader     = o.value(QStringLiteral("uploader")).toString();
        e.infoHash     = o.value(QStringLiteral("infoHash")).toString();
        e.addedAt      = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray())
            e.files.append(v.toString());
        for (const QJsonValue& v : o.value(QStringLiteral("groups")).toArray())
            e.groups.append(v.toInt());
        for (const QJsonValue& v : o.value(QStringLiteral("chapterIds")).toArray())
            e.chapterIds.append(v.toString());
        m_index.insert(it.key(), e);
    }
}

bool MangaVolumeIndex::save() const
{
    QDir().mkpath(m_baseDir);
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        QJsonObject o;
        o[QStringLiteral("seriesId")]     = e.seriesId;
        o[QStringLiteral("seriesTitle")]  = e.seriesTitle;
        o[QStringLiteral("volumeNumber")] = e.volumeNumber;
        o[QStringLiteral("dir")]          = e.dir;
        o[QStringLiteral("archive")]      = e.archive;
        o[QStringLiteral("bytes")]        = static_cast<double>(e.bytes);
        o[QStringLiteral("sourceKind")]   = e.sourceKind;
        o[QStringLiteral("releaseTitle")] = e.releaseTitle;
        o[QStringLiteral("uploader")]     = e.uploader;
        o[QStringLiteral("infoHash")]     = e.infoHash;
        o[QStringLiteral("addedAt")]      = static_cast<double>(e.addedAt);
        QJsonArray files;
        for (const QString& n : e.files) files.append(n);
        o[QStringLiteral("files")] = files;
        QJsonArray groups;
        for (int g : e.groups) groups.append(g);
        o[QStringLiteral("groups")] = groups;
        QJsonArray chapters;
        for (const QString& c : e.chapterIds) chapters.append(c);
        o[QStringLiteral("chapterIds")] = chapters;
        root[it.key()] = o;
    }
    // Atomic ledger write: QSaveFile writes to a temp sibling and commit()s with
    // a rename, so a partial write never clobbers the previous ledger.
    QSaveFile f(indexPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool MangaVolumeIndex::entryIntact(const Entry& e) const
{
    if (!e.archive.isEmpty()) {
        if (e.files.isEmpty() || !QFileInfo::exists(e.archive)) {
            qWarning() << "[manga-volume] CBZ row missing archive/files"
                       << e.archive << e.files.size();
            return false;
        }
        QString error;
        const QVector<CbzPageEntry> entries = CbzArchive::imageEntries(e.archive, &error);
        if (entries.size() != e.files.size()) {
            qWarning() << "[manga-volume] CBZ row page-count mismatch"
                       << e.archive << entries.size() << e.files.size() << error;
            return false;
        }
        for (int i = 0; i < entries.size(); ++i)
            if (entries.at(i).name != e.files.at(i)) {
                qWarning() << "[manga-volume] CBZ row entry mismatch"
                           << e.archive << entries.at(i).name << e.files.at(i);
                return false;
            }
        return true;
    }
    if (e.dir.isEmpty() || e.files.isEmpty()) return false;
    const QDir dir(e.dir);
    if (!dir.exists()) return false;
    for (const QString& name : e.files)
        if (!QFileInfo::exists(dir.absoluteFilePath(name)))
            return false;
    return true;
}

bool MangaVolumeIndex::writeSidecar(const QString& id, const Entry& e) const
{
    if (e.archive.isEmpty()) return false;
    QJsonObject o;
    o[QStringLiteral("volumeId")] = id;
    o[QStringLiteral("seriesId")] = e.seriesId;
    o[QStringLiteral("seriesTitle")] = e.seriesTitle;
    o[QStringLiteral("volumeNumber")] = e.volumeNumber;
    o[QStringLiteral("archive")] = e.archive;
    o[QStringLiteral("bytes")] = static_cast<double>(e.bytes);
    o[QStringLiteral("sourceKind")] = e.sourceKind;
    o[QStringLiteral("releaseTitle")] = e.releaseTitle;
    o[QStringLiteral("uploader")] = e.uploader;
    o[QStringLiteral("infoHash")] = e.infoHash;
    o[QStringLiteral("addedAt")] = static_cast<double>(e.addedAt);
    QJsonArray files;
    for (const QString& name : e.files) files.append(name);
    o[QStringLiteral("files")] = files;
    QJsonArray groups;
    for (int group : e.groups) groups.append(group);
    o[QStringLiteral("groups")] = groups;
    QJsonArray chapters;
    for (const QString& chapter : e.chapterIds) chapters.append(chapter);
    o[QStringLiteral("chapterIds")] = chapters;

    QSaveFile file(e.archive + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool MangaVolumeIndex::reconcileSidecar(const QString& id, Entry& e) const
{
    if (e.archive.isEmpty()) return false;
    QFile file(e.archive + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o.value(QStringLiteral("volumeId")).toString() != id) return false;

    QStringList files;
    for (const QJsonValue& value : o.value(QStringLiteral("files")).toArray())
        files.append(value.toString());
    QString archiveError;
    const QVector<CbzPageEntry> actual = CbzArchive::imageEntries(e.archive, &archiveError);
    if (files.isEmpty() || actual.size() != files.size()) return false;
    for (int i = 0; i < actual.size(); ++i)
        if (actual.at(i).name != files.at(i))
            return false;

    e.seriesId = o.value(QStringLiteral("seriesId")).toString(e.seriesId);
    e.seriesTitle = o.value(QStringLiteral("seriesTitle")).toString(e.seriesTitle);
    e.volumeNumber = o.value(QStringLiteral("volumeNumber")).toString(e.volumeNumber);
    e.bytes = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble(e.bytes));
    e.sourceKind = o.value(QStringLiteral("sourceKind")).toString(e.sourceKind);
    e.releaseTitle = o.value(QStringLiteral("releaseTitle")).toString(e.releaseTitle);
    e.uploader = o.value(QStringLiteral("uploader")).toString(e.uploader);
    e.infoHash = o.value(QStringLiteral("infoHash")).toString(e.infoHash);
    e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble(e.addedAt));
    e.files = files;
    e.groups.clear();
    for (const QJsonValue& value : o.value(QStringLiteral("groups")).toArray())
        e.groups.append(value.toInt());
    while (e.groups.size() < e.files.size()) e.groups.append(0);
    e.chapterIds.clear();
    for (const QJsonValue& value : o.value(QStringLiteral("chapterIds")).toArray())
        e.chapterIds.append(value.toString());
    e.dir.clear();
    return true;
}

bool MangaVolumeIndex::migrateLegacy(const QString& id, Entry& e)
{
    if (e.dir.isEmpty() || !QDir(e.dir).exists()) {
        qWarning() << "[manga-volume] legacy migration missing directory" << id << e.dir;
        return false;
    }
    QFile manifestFile(QDir(e.dir).absoluteFilePath(QStringLiteral("index.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "[manga-volume] legacy migration missing manifest" << id << e.dir;
        return false;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    if (manifest.value(QStringLiteral("volumeId")).toString() != id) {
        qWarning() << "[manga-volume] legacy manifest id mismatch" << id;
        return false;
    }

    QStringList files;
    qint64 bytes = 0;
    for (const QJsonValue& value : manifest.value(QStringLiteral("files")).toArray()) {
        const QString name = value.toString();
        const QFileInfo page(QDir(e.dir).absoluteFilePath(name));
        if (name.isEmpty() || !page.isFile()) {
            qWarning() << "[manga-volume] legacy manifest page missing" << id << name;
            return false;
        }
        files.append(name);
        bytes += page.size();
    }
    if (files.isEmpty()) {
        qWarning() << "[manga-volume] legacy manifest has no pages" << id;
        return false;
    }

    QList<int> groups;
    for (const QJsonValue& value : manifest.value(QStringLiteral("groups")).toArray())
        groups.append(value.toInt());
    if (groups.size() != files.size()) {
        groups = e.groups;
        if (groups.size() != files.size())
            groups = QList<int>(files.size(), 0);
    }

    VolumeProvenance record;
    record.id = id;
    record.seriesId = manifest.value(QStringLiteral("seriesId")).toString(e.seriesId);
    record.seriesTitle =
        manifest.value(QStringLiteral("seriesTitle")).toString(e.seriesTitle);
    record.volumeNumber =
        manifest.value(QStringLiteral("volumeNumber")).toString(e.volumeNumber);
    record.sourceKind =
        manifest.value(QStringLiteral("sourceKind")).toString(e.sourceKind);
    record.releaseTitle = e.releaseTitle;
    record.uploader = e.uploader;
    record.infoHash = e.infoHash;
    record.chapterIds = e.chapterIds;

    const QString archive = archivePathFor(record);
    QString archiveError;
    if (!QFileInfo::exists(archive)
        && !CbzArchive::writeImagesAtomic(archive, e.dir, files, &archiveError)) {
        qWarning() << "[manga-volume] legacy CBZ write failed" << id << archiveError;
        return false;
    }
    const QVector<CbzPageEntry> actual = CbzArchive::imageEntries(archive, &archiveError);
    if (actual.size() != files.size()) {
        qWarning() << "[manga-volume] legacy CBZ page-count mismatch" << id
                   << actual.size() << files.size() << archiveError;
        return false;
    }
    for (int i = 0; i < actual.size(); ++i)
        if (actual.at(i).name != files.at(i)) {
            qWarning() << "[manga-volume] legacy CBZ entry mismatch" << id
                       << actual.at(i).name << files.at(i);
            return false;
        }

    Entry migrated = e;
    migrated.seriesId = record.seriesId;
    migrated.seriesTitle = record.seriesTitle;
    migrated.volumeNumber = record.volumeNumber;
    migrated.sourceKind = record.sourceKind;
    migrated.archive = archive;
    migrated.files = files;
    migrated.groups = groups;
    migrated.bytes = bytes;
    migrated.dir.clear();
    if (migrated.addedAt <= 0)
        migrated.addedAt = QDateTime::currentMSecsSinceEpoch();
    if (!writeSidecar(id, migrated)) {
        qWarning() << "[manga-volume] legacy sidecar write failed" << id;
        return false;
    }

    const Entry previous = e;
    const QString legacyDir = e.dir;
    e = migrated;
    m_index.insert(id, e);
    if (!save()) {
        m_index.insert(id, previous);
        e = previous;
        qWarning() << "[manga-volume] legacy ledger save failed" << id;
        return false;
    }
    if (!m_treeRemover(legacyDir)) {
        qWarning() << "[manga-volume] legacy directory retirement failed" << id << legacyDir;
        // The CBZ, sidecar, and ledger are already durable. Cleanup failure is
        // not a volume failure; retain both copies and retry retirement later.
        return true;
    }
    return true;
}

bool MangaVolumeIndex::publish(const VolumeProvenance& record, const QString& finalDir,
                               const QStringList& orderedFiles, const QList<int>& groups,
                               qint64 bytes)
{
    if (record.id.isEmpty() || orderedFiles.isEmpty()) return false;

    Entry e;
    e.seriesId     = record.seriesId;
    e.seriesTitle  = record.seriesTitle;
    e.volumeNumber = record.volumeNumber;
    e.dir          = QDir::cleanPath(finalDir);
    e.files        = orderedFiles;
    e.groups       = groups;
    e.bytes        = bytes;
    e.sourceKind   = record.sourceKind;
    e.releaseTitle = record.releaseTitle;
    e.uploader     = record.uploader;
    e.infoHash     = record.infoHash;
    e.chapterIds   = record.chapterIds;
    e.addedAt      = QDateTime::currentMSecsSinceEpoch();

    m_index.insert(record.id, e);
    save();
    emit changed();
    return true;
}

bool MangaVolumeIndex::publishArchive(const VolumeProvenance& record,
                                      const QString& archivePath,
                                      const QStringList& orderedFiles,
                                      const QList<int>& groups,
                                      qint64 bytes)
{
    if (record.id.isEmpty() || orderedFiles.isEmpty()
        || !QFileInfo::exists(archivePath))
        return false;

    Entry e;
    e.seriesId = record.seriesId;
    e.seriesTitle = record.seriesTitle;
    e.volumeNumber = record.volumeNumber;
    e.archive = QDir::cleanPath(archivePath);
    e.files = orderedFiles;
    e.groups = groups;
    while (e.groups.size() < e.files.size()) e.groups.append(0);
    e.bytes = bytes;
    e.sourceKind = record.sourceKind;
    e.releaseTitle = record.releaseTitle;
    e.uploader = record.uploader;
    e.infoHash = record.infoHash;
    e.chapterIds = record.chapterIds;
    e.addedAt = QDateTime::currentMSecsSinceEpoch();
    if (!entryIntact(e) || !writeSidecar(record.id, e))
        return false;
    m_index.insert(record.id, e);
    if (!save()) {
        m_index.remove(record.id);
        return false;
    }
    emit changed();
    return true;
}

QVariantList MangaVolumeIndex::localPages(const QString& volumeId) const
{
    QVariantList out;
    auto it = m_index.constFind(volumeId.trimmed());
    if (it == m_index.constEnd()) return out;
    if (!it.value().archive.isEmpty()) {
        if (!entryIntact(it.value())) return out;
        for (int i = 0; i < it.value().files.size(); ++i) {
            out.append(QVariantMap{
                {QStringLiteral("index"), i},
                {QStringLiteral("archive"), it.value().archive},
                {QStringLiteral("entry"), it.value().files.at(i)},
                {QStringLiteral("group"), it.value().groups.value(i, 0)}
            });
        }
        return out;
    }
    const QDir dir(it.value().dir);
    if (!dir.exists()) return out;
    int idx = 0;
    for (int i = 0; i < it.value().files.size(); ++i) {
        const QString abs = dir.absoluteFilePath(it.value().files.at(i));
        if (!QFileInfo::exists(abs)) continue;
        QVariantMap m;
        m[QStringLiteral("index")] = idx++;
        m[QStringLiteral("url")]   = QUrl::fromLocalFile(abs);
        m[QStringLiteral("group")] = it.value().groups.value(i, 0);
        out.append(m);
    }
    return out;
}

QVariantList MangaVolumeIndex::downloadedVolumes() const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        if (!e.archive.isEmpty()) {
            const bool missing = !entryIntact(e);
            out.append(QVariantMap{
                {QStringLiteral("id"), it.key()},
                {QStringLiteral("seriesId"), e.seriesId},
                {QStringLiteral("seriesTitle"), e.seriesTitle},
                {QStringLiteral("label"), QStringLiteral("Vol. %1").arg(e.volumeNumber)},
                {QStringLiteral("pages"), e.files.size()},
                {QStringLiteral("bytes"), e.bytes},
                {QStringLiteral("addedAt"), e.addedAt},
                {QStringLiteral("missing"), missing},
                {QStringLiteral("art"), QString()}
            });
            continue;
        }
        const QDir dir(e.dir);
        const QString first = e.files.isEmpty()
            ? QString() : dir.absoluteFilePath(e.files.first());
        const bool missing = first.isEmpty() || !QFileInfo::exists(first);
        out.append(QVariantMap{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("seriesId"), e.seriesId},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("label"), QStringLiteral("Vol. %1").arg(e.volumeNumber)},
            {QStringLiteral("pages"), e.files.size()},
            {QStringLiteral("bytes"), e.bytes},
            {QStringLiteral("addedAt"), e.addedAt},
            {QStringLiteral("missing"), missing},
            // First page = the volume's own local cover; honest blank when gone.
            {QStringLiteral("art"), missing
                ? QString() : QUrl::fromLocalFile(first).toString()}
        });
    }
    return out;
}

QVariantMap MangaVolumeIndex::statusOf(const QString& volumeId) const
{
    const QString id = volumeId.trimmed();
    QVariantMap s;
    auto it = m_index.constFind(id);
    // File-aware liveness: a row whose dir OR any recorded page file is missing is
    // NOT ready. Reusing entryIntact (per-file stat) makes statusOf agree with
    // localPages, so a broken volume is never silently reported ready — and no
    // caller has to pre-run heal() to get an honest answer.
    if (it == m_index.constEnd() || !entryIntact(it.value())) {
        s[QStringLiteral("state")]    = QStringLiteral("none");
        s[QStringLiteral("progress")] = 0.0;
        return s;
    }
    const Entry& e = it.value();
    s[QStringLiteral("state")]        = QStringLiteral("ready");
    s[QStringLiteral("progress")]     = 1.0;
    s[QStringLiteral("volumeId")]     = id;
    s[QStringLiteral("seriesId")]     = e.seriesId;
    s[QStringLiteral("seriesTitle")]  = e.seriesTitle;
    s[QStringLiteral("volumeNumber")] = e.volumeNumber;
    s[QStringLiteral("sourceKind")]   = e.sourceKind;
    s[QStringLiteral("releaseTitle")] = e.releaseTitle;
    s[QStringLiteral("uploader")]     = e.uploader;
    s[QStringLiteral("infoHash")]     = e.infoHash;
    s[QStringLiteral("chapterIds")]   = QVariant(e.chapterIds);
    s[QStringLiteral("dir")]          = e.dir;
    s[QStringLiteral("archive")]      = e.archive;
    s[QStringLiteral("pages")]        = e.files.size();
    s[QStringLiteral("bytes")]        = static_cast<double>(e.bytes);
    s[QStringLiteral("addedAt")]      = static_cast<double>(e.addedAt);
    return s;
}

bool MangaVolumeIndex::remove(const QString& volumeId)
{
    auto it = m_index.find(volumeId.trimmed());
    if (it == m_index.end()) return false;
    if (!it.value().archive.isEmpty()) {
        if (!QFile::remove(it.value().archive))
            return false;
        const QString sidecar = it.value().archive + QStringLiteral(".json");
        if (QFileInfo::exists(sidecar) && !QFile::remove(sidecar))
            return false;
    }
    if (!it.value().dir.isEmpty()) {
        const auto result = DownloadFileOps::removeTree(it.value().dir, m_treeRemover);
        if (!result.success)
            return false;
    }
    m_index.erase(it);
    save();
    emit changed();
    return true;
}

void MangaVolumeIndex::reload()
{
    load();
    emit changed();
}

void MangaVolumeIndex::heal()
{
    bool mutated = false;
    const QStringList ids = m_index.keys();
    for (const QString& id : ids) {
        auto it = m_index.find(id);
        if (it == m_index.end()) continue;

        if (!it->archive.isEmpty()) {
            Entry repaired = it.value();
            if (reconcileSidecar(id, repaired)) {
                if (!entryIntact(it.value()) || repaired.files != it->files
                    || repaired.groups != it->groups) {
                    it.value() = repaired;
                    mutated = true;
                }
                continue;
            }
        }
        if (it->archive.isEmpty()) {
            Entry legacy = it.value();
            if (migrateLegacy(id, legacy)) {
                mutated = true;
                continue;
            }
            // Migration is an upgrade, never a reason to destroy a still-valid
            // loose-page volume. Preserve it for a later retry if CBZ creation
            // or ledger publication failed.
            if (entryIntact(it.value()))
                continue;
        }
        if (!entryIntact(it.value())) {
            qWarning() << "[manga-volume] heal pruned unrecoverable row" << id;
            // Remove only the false-ready lookup row. Unvalidated payload stays
            // on disk for manual recovery instead of being recursively deleted.
            m_index.erase(it);
            mutated = true;
        }
    }
    if (!mutated) return;
    save();
    emit changed();
}

} // namespace MangaTankoban
