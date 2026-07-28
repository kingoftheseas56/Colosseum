#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/CbzArchive.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace MangaTankoban {
namespace {

// ── Extraction helpers, lifted verbatim from ComicDownloader ─────────────────

bool isImageFile(const QString& name)
{
    static const QSet<QString> kExts = { QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("gif"),
        QStringLiteral("avif"), QStringLiteral("bmp") };
    return kExts.contains(QFileInfo(name).suffix().toLower());
}

QString sevenZipPath()
{
    const QString p = QStringLiteral("C:/Program Files/7-Zip/7z.exe");
    return QFileInfo::exists(p) ? p : QString();
}

QString bsdtarPath()
{
    const QString sys = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sys)) return sys;
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
}

// A cheap "is this a real image" gate: sniff the leading magic bytes. Full pixel
// decoding would pull in Qt::Gui; the payload is trusted archive content, so a
// magic-byte check is enough to reject an empty/HTML/garbage extraction while
// keeping this ingestor Qt::Core-only.
bool looksDecodable(const QString& absPath)
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray h = f.read(16);
    if (h.size() < 3) return false;
    const auto u = [&](int i) { return static_cast<unsigned char>(h.at(i)); };
    if (h.size() >= 8 && u(0) == 0x89 && u(1) == 0x50 && u(2) == 0x4E && u(3) == 0x47
        && u(4) == 0x0D && u(5) == 0x0A && u(6) == 0x1A && u(7) == 0x0A)
        return true;                                        // PNG
    if (u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) return true;   // JPEG
    if (h.startsWith("GIF8")) return true;                  // GIF
    if (h.startsWith("BM")) return true;                    // BMP
    if (h.size() >= 12 && h.startsWith("RIFF") && h.mid(8, 4) == "WEBP")
        return true;                                        // WebP
    return false;
}

bool isSupportedArchive(const QString& path)
{
    static const QSet<QString> allowed{
        QStringLiteral("cbz"), QStringLiteral("cbr"),
        QStringLiteral("cb7"), QStringLiteral("cbt"), QStringLiteral("zip")
    };
    return allowed.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

MangaVolumeArchiveIngestor::MangaVolumeArchiveIngestor(MangaVolumeIndex* index, QObject* parent)
    : QObject(parent), m_index(index)
{
}

MangaVolumeArchiveIngestor::~MangaVolumeArchiveIngestor()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    if (m_active) {
        if (!m_active->extractTmp.isEmpty()) QDir(m_active->extractTmp).removeRecursively();
        delete m_active;
        m_active = nullptr;
    }
}

void MangaVolumeArchiveIngestor::ingestArchive(const VolumeProvenance& record,
                                               const QString& archivePath)
{
    const QString id = record.id.trimmed();
    if (id.isEmpty()) {
        emit failed(id, QStringLiteral("empty volume id"));
        return;
    }
    const QFileInfo archive(QDir::cleanPath(archivePath));
    if (!archive.isFile() || !isSupportedArchive(archive.fileName())) {
        emit failed(id, QStringLiteral("volume archive missing or unsupported"));
        return;
    }
    Job job;
    job.record = record;
    job.record.id = id;
    job.archivePath = archive.absoluteFilePath();
    m_queue.enqueue(job);
    QMetaObject::invokeMethod(this, [this]() { startNext(); }, Qt::QueuedConnection);
}

void MangaVolumeArchiveIngestor::startNext()
{
    if (m_active || m_queue.isEmpty()) return;
    m_active = new Job(m_queue.dequeue());

    const QString suffix = QFileInfo(m_active->archivePath).suffix().toLower();
    if (suffix == QLatin1String("cbz") || suffix == QLatin1String("zip")) {
        QString archiveError;
        const QVector<CbzPageEntry> sourceEntries =
            CbzArchive::imageEntries(m_active->archivePath, &archiveError);
        if (sourceEntries.isEmpty()) {
            failActive(QStringLiteral("CBZ validation failed: %1").arg(archiveError));
            return;
        }

        const QString finalPath = m_index->archivePathFor(m_active->record);
        const QString partPath = finalPath + QStringLiteral(".part");
        if (QFileInfo::exists(finalPath)) {
            failActive(QStringLiteral("canonical CBZ already exists"));
            return;
        }
        QDir().mkpath(QFileInfo(finalPath).absolutePath());
        QFile::remove(partPath);
        if (!QFile::copy(m_active->archivePath, partPath)) {
            failActive(QStringLiteral("cannot stage downloaded CBZ"));
            return;
        }
        const QVector<CbzPageEntry> stagedEntries =
            CbzArchive::imageEntries(partPath, &archiveError);
        bool stagedMatches = stagedEntries.size() == sourceEntries.size();
        for (int i = 0; stagedMatches && i < stagedEntries.size(); ++i) {
            stagedMatches = stagedEntries.at(i).name == sourceEntries.at(i).name
                            && stagedEntries.at(i).uncompressedBytes
                                == sourceEntries.at(i).uncompressedBytes;
        }
        if (!stagedMatches) {
            QFile::remove(partPath);
            failActive(QStringLiteral("staged CBZ validation differs from source: %1")
                           .arg(archiveError));
            return;
        }
        if (!QFile::rename(partPath, finalPath)) {
            QFile::remove(partPath);
            failActive(QStringLiteral("cannot atomically finalize downloaded CBZ"));
            return;
        }

        QStringList files;
        QList<int> groups;
        qint64 bytes = 0;
        for (const CbzPageEntry& entry : stagedEntries) {
            files.append(entry.name);
            groups.append(0);
            bytes += static_cast<qint64>(entry.uncompressedBytes);
        }
        if (!m_index->publishArchive(m_active->record, finalPath, files, groups, bytes)) {
            QFile::remove(finalPath);
            QFile::remove(finalPath + QStringLiteral(".json"));
            failActive(QStringLiteral("index publish rejected the CBZ"));
            return;
        }

        const QString id = m_active->record.id;
        QFile::remove(m_active->archivePath);
        delete m_active;
        m_active = nullptr;
        emit finished(id);
        startNext();
        return;
    }

    m_active->extractTmp = m_index->pagesDirFor(m_active->record) + QStringLiteral(".extract");
    QDir(m_active->extractTmp).removeRecursively();
    if (!QDir().mkpath(m_active->extractTmp)) {
        failActive(QStringLiteral("cannot create extract dir"));
        return;
    }
    runExtractor(0);
}

void MangaVolumeArchiveIngestor::runExtractor(int which)
{
    if (!m_active) return;
    QString exe;
    QStringList args;
    if (which == 0) {
        exe = bsdtarPath();
        args = { QStringLiteral("-xf"), QDir::toNativeSeparators(m_active->archivePath),
                 QStringLiteral("-C"), QDir::toNativeSeparators(m_active->extractTmp) };
    } else {
        exe = sevenZipPath();
        args = { QStringLiteral("x"), QStringLiteral("-y"),
                 QStringLiteral("-o") + QDir::toNativeSeparators(m_active->extractTmp),
                 QDir::toNativeSeparators(m_active->archivePath) };
    }
    if (exe.isEmpty()) {
        if (which == 0) { runExtractor(1); return; }
        failActive(QStringLiteral("no archive extractor available (probed C:/Windows/System32/tar.exe, "
                                  "PATH tar, C:/Program Files/7-Zip/7z.exe)"));
        return;
    }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setArguments(args);
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, which](int code, QProcess::ExitStatus) { onExtractDone(code, which); });
    m_proc->start();
}

void MangaVolumeArchiveIngestor::onExtractDone(int exitCode, int which)
{
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    if (!m_active) return;
    if (exitCode != 0) {
        if (which == 0 && !sevenZipPath().isEmpty()) {
            QDir(m_active->extractTmp).removeRecursively();
            QDir().mkpath(m_active->extractTmp);
            runExtractor(1);
            return;
        }
        failActive(QStringLiteral("archive extraction failed (not a cbr/cbz?)"));
        return;
    }
    finishActiveSuccess();
}

void MangaVolumeArchiveIngestor::finishActiveSuccess()
{
    if (!m_active) return;
    const QString reason = finalizeInto(m_active->record, m_active->extractTmp);
    if (!reason.isEmpty()) {
        // A partial/failed extraction NEVER publishes — leave the source archive
        // for the caller to inspect or retry.
        failActive(reason);
        return;
    }
    const QString id = m_active->record.id;
    QDir(m_active->extractTmp).removeRecursively();
    // Published atomically — now (and only now) drop the consumed source archive.
    QFile::remove(m_active->archivePath);
    delete m_active;
    m_active = nullptr;
    emit finished(id);
    startNext();
}

void MangaVolumeArchiveIngestor::failActive(const QString& reason)
{
    if (!m_active) return;
    if (!m_active->extractTmp.isEmpty()) QDir(m_active->extractTmp).removeRecursively();
    const QString id = m_active->record.id;
    delete m_active;
    m_active = nullptr;
    emit failed(id, reason);
    startNext();
}

bool MangaVolumeArchiveIngestor::publish(const VolumeProvenance& record, const QString& preparedDir,
                                         const QVector<int>& groups)
{
    const QString id = record.id.trimmed();
    if (id.isEmpty()) {
        emit failed(id, QStringLiteral("empty volume id"));
        return false;
    }
    const QString src = QDir::cleanPath(preparedDir);
    if (!QDir(src).exists()) {
        emit failed(id, QStringLiteral("prepared page directory missing"));
        return false;
    }
    VolumeProvenance rec = record;
    rec.id = id;
    const QString reason = finalizeInto(rec, src, groups);
    if (!reason.isEmpty()) {
        emit failed(id, reason);
        return false;
    }
    QDir(src).removeRecursively();   // prepared dir consumed
    emit finished(id);
    return true;
}

QString MangaVolumeArchiveIngestor::finalizeInto(const VolumeProvenance& record,
                                                 const QString& sourceDir,
                                                 const QVector<int>& groupsIn)
{
    // 1. Collect images recursively (many archives nest a single folder).
    QStringList rel;
    QDirIterator it(sourceDir, QDir::Files, QDirIterator::Subdirectories);
    const int prefixLen = sourceDir.length() + 1;
    while (it.hasNext()) {
        const QString abs = it.next();
        if (isImageFile(abs)) rel.append(abs.mid(prefixLen));
    }
    if (rel.isEmpty())
        return QStringLiteral("archive contained no pages");

    // 2. Validate at least one decodable image (magic-byte gate).
    bool anyDecodable = false;
    for (const QString& r : rel) {
        if (looksDecodable(sourceDir + QChar('/') + r)) { anyDecodable = true; break; }
    }
    if (!anyDecodable)
        return QStringLiteral("no decodable image in payload");

    // 3. Natural sort so "…10" follows "…2" and case never reorders.
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(rel.begin(), rel.end(), [&coll](const QString& a, const QString& b) {
        return coll.compare(a, b) < 0;
    });

    // 4. Write the naturally ordered source pages into a same-directory .part
    //    archive without recompression, reopen it, then atomically rename it.
    const QString finalArchive = m_index->archivePathFor(record);
    if (QFileInfo::exists(finalArchive))
        return QStringLiteral("canonical CBZ already exists");
    QString archiveError;
    if (!CbzArchive::writeImagesAtomic(finalArchive, sourceDir, rel, &archiveError))
        return QStringLiteral("CBZ publish failed: %1").arg(archiveError);

    const QStringList files = rel;
    qint64 bytes = 0;
    for (const QString& name : files)
        bytes += QFileInfo(QDir(sourceDir).absoluteFilePath(name)).size();

    // Per-page chapter-group ordinals. A caller-supplied list (WeebCentral's
    // multi-chapter volume) is honored only when it matches the final page count
    // in natural-sorted order; otherwise every page falls in group 0 (the
    // single-archive Nyaa path).
    QList<int> groups;
    if (groupsIn.size() == files.size())
        for (int g : groupsIn) groups.append(g);
    else
        groups = QList<int>(files.size(), 0);

    // Publish recovery metadata beside the validated archive, then commit the
    // durable ledger row. A ledger failure removes the just-created payload.
    if (!m_index->publishArchive(record, finalArchive, files, groups, bytes)) {
        QFile::remove(finalArchive);
        QFile::remove(finalArchive + QStringLiteral(".json"));
        return QStringLiteral("index publish rejected the volume");
    }
    return QString();
}

} // namespace MangaTankoban
