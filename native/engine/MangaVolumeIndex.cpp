#include "engine/MangaVolumeIndex.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

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
    : QObject(parent)
    , m_baseDir(QDir::cleanPath(rootDir) + QStringLiteral("/manga-volumes"))
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

void MangaVolumeIndex::save() const
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
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

bool MangaVolumeIndex::entryIntact(const Entry& e) const
{
    if (e.dir.isEmpty() || e.files.isEmpty()) return false;
    const QDir dir(e.dir);
    if (!dir.exists()) return false;
    for (const QString& name : e.files)
        if (!QFileInfo::exists(dir.absoluteFilePath(name)))
            return false;
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

QVariantList MangaVolumeIndex::localPages(const QString& volumeId) const
{
    QVariantList out;
    auto it = m_index.constFind(volumeId.trimmed());
    if (it == m_index.constEnd()) return out;
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
    s[QStringLiteral("pages")]        = e.files.size();
    s[QStringLiteral("bytes")]        = static_cast<double>(e.bytes);
    s[QStringLiteral("addedAt")]      = static_cast<double>(e.addedAt);
    return s;
}

bool MangaVolumeIndex::remove(const QString& volumeId)
{
    auto it = m_index.find(volumeId.trimmed());
    if (it == m_index.end()) return false;
    if (!it.value().dir.isEmpty())
        QDir(it.value().dir).removeRecursively();
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
    QStringList doomed;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it)
        if (!entryIntact(it.value()))
            doomed.append(it.key());
    if (doomed.isEmpty()) return;
    for (const QString& id : doomed) {
        auto it = m_index.find(id);
        if (it == m_index.end()) continue;
        if (!it.value().dir.isEmpty())
            QDir(it.value().dir).removeRecursively();
        m_index.erase(it);
    }
    save();
    emit changed();
}

} // namespace MangaTankoban
