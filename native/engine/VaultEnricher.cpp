#include "VaultEnricher.h"

#include "CbzArchive.h"
#include "VaultKit.h"      // CancellationToken
#include "VaultStoreIo.h"
#include "third_party/miniz/miniz.h"
#include "player/MediaAdmissionProbe.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace {

constexpr mz_uint kMaxEpubEntries = 4096;
constexpr mz_uint64 kMaxEpubXmlBytes = 2 * 1024 * 1024;
constexpr mz_uint64 kMaxEpubImageBytes = 24 * 1024 * 1024;
constexpr int kMaxEpubTextChars = 64 * 1024;

struct ZipEntry {
    QString name;
    mz_uint index = 0;
    mz_uint64 uncompressedSize = 0;
    bool directory = false;
};

QString zipError(mz_zip_archive& zip)
{
    return QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
}

bool safeArchivePath(const QString& raw)
{
    const QString path = QDir::fromNativeSeparators(raw.trimmed());
    if (path.isEmpty() || path.startsWith(QLatin1Char('/'))
        || QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(path).hasMatch())
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
        if (part == QLatin1String("..") || part == QLatin1String("."))
            return false;
    }
    return true;
}

QString resolveArchivePath(const QString& baseDir, const QString& rawHref)
{
    QString href = QUrl::fromPercentEncoding(rawHref.toUtf8());
    const int fragment = href.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        href.truncate(fragment);
    href = QDir::fromNativeSeparators(href.trimmed());
    if (!safeArchivePath(href))
        return QString();
    const QString joined = baseDir.isEmpty()
        ? href
        : QDir::cleanPath(baseDir + QLatin1Char('/') + href);
    return safeArchivePath(joined) ? joined : QString();
}

int findEntry(const QVector<ZipEntry>& entries, const QString& name)
{
    for (const ZipEntry& entry : entries) {
        if (!entry.directory && entry.name.compare(name, Qt::CaseInsensitive) == 0)
            return static_cast<int>(entry.index);
    }
    return -1;
}

bool extractEntry(mz_zip_archive& zip, const QVector<ZipEntry>& entries,
                  const QString& name, mz_uint64 maxBytes, QByteArray* out,
                  QString* error)
{
    if (out)
        out->clear();
    const int index = findEntry(entries, name);
    if (index < 0) {
        if (error) *error = QStringLiteral("EPUB entry not found: %1").arg(name);
        return false;
    }
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat)
        || stat.m_is_directory || stat.m_uncomp_size > maxBytes) {
        if (error) *error = QStringLiteral("EPUB entry is unreadable or too large: %1").arg(name);
        return false;
    }
    size_t size = 0;
    void* memory = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0);
    if (!memory || size > maxBytes) {
        if (memory) mz_free(memory);
        if (error) *error = QStringLiteral("cannot extract EPUB entry: %1").arg(name);
        return false;
    }
    if (out)
        *out = QByteArray(static_cast<const char*>(memory), static_cast<qsizetype>(size));
    mz_free(memory);
    return true;
}

bool isImageEntry(const QString& name)
{
    static const QSet<QString> extensions = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
        QStringLiteral("avif")
    };
    return extensions.contains(QFileInfo(name).suffix().toLower());
}

QString imageEntryNamed(const QVector<ZipEntry>& entries, const QString& wanted)
{
    for (const ZipEntry& entry : entries) {
        if (!entry.directory && entry.name.compare(wanted, Qt::CaseInsensitive) == 0
            && isImageEntry(entry.name) && entry.uncompressedSize > 0
            && entry.uncompressedSize <= kMaxEpubImageBytes)
            return entry.name;
    }
    return QString();
}

QString fallbackCoverEntry(const QVector<ZipEntry>& entries)
{
    static const QStringList exactNames = {
        QStringLiteral("cover.jpg"), QStringLiteral("cover.jpeg"), QStringLiteral("cover.png"),
        QStringLiteral("OEBPS/cover.jpg"), QStringLiteral("OEBPS/cover.jpeg"), QStringLiteral("OEBPS/cover.png"),
        QStringLiteral("OEBPS/images/cover.jpg"), QStringLiteral("OEBPS/images/cover.jpeg"), QStringLiteral("OEBPS/images/cover.png"),
        QStringLiteral("OPS/cover.jpg"), QStringLiteral("OPS/cover.jpeg"), QStringLiteral("OPS/cover.png"),
        QStringLiteral("OPS/images/cover.jpg"), QStringLiteral("OPS/images/cover.jpeg"), QStringLiteral("OPS/images/cover.png"),
        QStringLiteral("images/cover.jpg"), QStringLiteral("images/cover.jpeg"), QStringLiteral("images/cover.png")
    };
    for (const QString& wanted : exactNames) {
        const QString match = imageEntryNamed(entries, wanted);
        if (!match.isEmpty()) return match;
    }

    auto usable = [](const ZipEntry& entry) {
        return !entry.directory && isImageEntry(entry.name)
            && entry.uncompressedSize > 0
            && entry.uncompressedSize <= kMaxEpubImageBytes;
    };
    for (const ZipEntry& entry : entries) {
        const QString lower = entry.name.toLower();
        if (usable(entry) && lower.contains(QStringLiteral("cover"))) return entry.name;
    }
    for (const ZipEntry& entry : entries) {
        const QString lower = entry.name.toLower();
        const QString base = QFileInfo(lower).fileName();
        if (usable(entry) && base.startsWith(QStringLiteral("folder."))) return entry.name;
    }
    for (const ZipEntry& entry : entries) {
        if (usable(entry) && entry.name.toLower().contains(QStringLiteral("front")))
            return entry.name;
    }
    for (const ZipEntry& entry : entries) {
        if (usable(entry)) return entry.name;
    }
    return QString();
}

QString boundedText(const QString& value)
{
    return value.trimmed().left(kMaxEpubTextChars);
}

// Map the probe's enum to the exact durable verdict string the index/QML contract expects.
QString admissionVerdictName(MediaAdmissionProbe::Verdict verdict)
{
    switch (verdict) {
    case MediaAdmissionProbe::Verdict::Admitted:
        return QStringLiteral("Admitted");
    case MediaAdmissionProbe::Verdict::RejectedNoVideo:
        return QStringLiteral("RejectedNoVideo");
    case MediaAdmissionProbe::Verdict::RejectedError:
        return QStringLiteral("RejectedError");
    case MediaAdmissionProbe::Verdict::RejectedTimeout:
        return QStringLiteral("RejectedTimeout");
    }
    return QStringLiteral("RejectedError");
}

// ── Local artwork adoption (Slice 3) ──
// Adoption priority when more than one convention name is present: poster wins, then folder,
// then cover — the same "most specific first" idea pickCoverEntry already applies to CBZ pages.
const QStringList& artworkBasenames()
{
    static const QStringList names = {
        QStringLiteral("poster"), QStringLiteral("folder"), QStringLiteral("cover")
    };
    return names;
}

const QStringList& artworkExtensions()
{
    static const QStringList exts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png")
    };
    return exts;
}

// A candidate must actually decode as an image, not merely carry the right name — refuses a
// corrupt/truncated file instead of handing QML a ref that only fails later. QImageReader::
// canRead() is a bounded header sniff (no full decode), so a hostile/garbage file returns
// quickly rather than wedging enrichment.
bool isReadableImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(false);
    return reader.canRead() && reader.size().isValid() && !reader.size().isEmpty();
}
} // namespace

VaultEnricher::VaultEnricher(VaultIndex* index, QString cacheDir, QObject* parent)
    : QObject(parent), m_index(index), m_cacheDir(std::move(cacheDir))
{
    loadDurationCache();
}

// ── Comic facts ───────────────────────────────────────────────────────
QString VaultEnricher::pickCoverEntry(const QStringList& imageEntryNames)
{
    if (imageEntryNames.isEmpty())
        return QString();

    // Prefer a cover.*/folder.* basename.
    for (const QString& name : imageEntryNames) {
        const QString base = QFileInfo(name).completeBaseName().toLower();
        if (base == QLatin1String("cover") || base == QLatin1String("folder"))
            return name;
    }
    // Else the first image in natural order.
    QStringList sorted = imageEntryNames;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return VaultIndex::naturalSortKey(a) < VaultIndex::naturalSortKey(b);
    });
    return sorted.first();
}

VaultEnricher::ComicFacts VaultEnricher::readComicFacts(const QString& cbzPath)
{
    ComicFacts f;
    QString err;
    const auto entries = MangaTankoban::CbzArchive::imageEntries(cbzPath, &err);
    if (entries.isEmpty()) {
        f.errorDetail = err.isEmpty() ? QStringLiteral("archive contains no readable pages") : err;
        return f; // ok stays false — corrupt / unreadable / not a comic archive
    }
    QStringList names;
    names.reserve(entries.size());
    for (const auto& e : entries)
        names.append(e.name);
    f.pages = names.size();
    f.coverEntry = pickCoverEntry(names);
    f.ok = true;
    return f;
}

// ── Video duration cache ──────────────────────────────────────────────
VaultEnricher::BookFacts VaultEnricher::readBookFacts(const QString& epubPath)
{
    BookFacts f;
    mz_zip_archive zip{};
    const QByteArray nativePath = QDir::toNativeSeparators(epubPath).toUtf8();
    if (!mz_zip_reader_init_file(&zip, nativePath.constData(), 0)) {
        f.errorDetail = QStringLiteral("cannot open EPUB: %1").arg(zipError(zip));
        return f;
    }

    QVector<ZipEntry> entries;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count == 0 || count > kMaxEpubEntries) {
        f.errorDetail = QStringLiteral("EPUB has an invalid entry count");
        mz_zip_reader_end(&zip);
        return f;
    }
    entries.reserve(static_cast<int>(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        const QString name = QString::fromUtf8(stat.m_filename);
        entries.append(ZipEntry{name, i, stat.m_uncomp_size, stat.m_is_directory != 0});
    }

    QByteArray containerBytes;
    QString error;
    if (!extractEntry(zip, entries, QStringLiteral("META-INF/container.xml"),
                      kMaxEpubXmlBytes, &containerBytes, &error)) {
        f.errorDetail = error;
        mz_zip_reader_end(&zip);
        return f;
    }

    QString opfPath;
    QXmlStreamReader containerXml(containerBytes);
    while (!containerXml.atEnd()) {
        containerXml.readNext();
        if (containerXml.isStartElement()
            && containerXml.name() == QLatin1String("rootfile")) {
            opfPath = resolveArchivePath(QString(),
                                         containerXml.attributes().value(QStringLiteral("full-path")).toString());
            break;
        }
    }
    if (containerXml.hasError() || opfPath.isEmpty()) {
        f.errorDetail = QStringLiteral("EPUB container has no safe OPF rootfile");
        mz_zip_reader_end(&zip);
        return f;
    }

    QByteArray opfBytes;
    if (!extractEntry(zip, entries, opfPath, kMaxEpubXmlBytes, &opfBytes, &error)) {
        f.errorDetail = error;
        mz_zip_reader_end(&zip);
        return f;
    }

    struct ManifestItem {
        QString id;
        QString href;
        QString properties;
    };
    QVector<ManifestItem> manifest;
    QString coverId;
    QString coverFromMeta;
    const QString opfDir = QFileInfo(opfPath).path() == QLatin1String(".")
        ? QString() : QFileInfo(opfPath).path();
    QXmlStreamReader opfXml(opfBytes);
    bool inMetadata = false;
    while (!opfXml.atEnd()) {
        opfXml.readNext();
        if (opfXml.isStartElement()) {
            const QString name = opfXml.name().toString();
            if (name == QLatin1String("metadata")) {
                inMetadata = true;
            } else if (inMetadata && (name == QLatin1String("title")
                                      || name == QLatin1String("creator")
                                      || name == QLatin1String("description"))) {
                const QString text = boundedText(opfXml.readElementText(
                    QXmlStreamReader::SkipChildElements));
                if (name == QLatin1String("title") && f.title.isEmpty())
                    f.title = text;
                else if (name == QLatin1String("creator") && f.author.isEmpty())
                    f.author = text;
                else if (name == QLatin1String("description") && f.synopsis.isEmpty())
                    f.synopsis = text;
            } else if (inMetadata && name == QLatin1String("meta")) {
                const auto attrs = opfXml.attributes();
                const QString metaName = attrs.value(QStringLiteral("name")).toString();
                const QString property = attrs.value(QStringLiteral("property")).toString();
                if (metaName.compare(QStringLiteral("cover"), Qt::CaseInsensitive) == 0)
                    coverId = attrs.value(QStringLiteral("content")).toString().trimmed();
                if (property.compare(QStringLiteral("cover-image"), Qt::CaseInsensitive) == 0)
                    coverFromMeta = attrs.value(QStringLiteral("content")).toString().trimmed();
            } else if (name == QLatin1String("item")) {
                const auto attrs = opfXml.attributes();
                manifest.append(ManifestItem{
                    attrs.value(QStringLiteral("id")).toString(),
                    attrs.value(QStringLiteral("href")).toString(),
                    attrs.value(QStringLiteral("properties")).toString()});
            }
        } else if (opfXml.isEndElement() && opfXml.name() == QLatin1String("metadata")) {
            inMetadata = false;
        }
    }
    if (opfXml.hasError()) {
        f.errorDetail = QStringLiteral("EPUB OPF XML is malformed");
        mz_zip_reader_end(&zip);
        return f;
    }

    auto manifestCover = [&](const QString& id) {
        for (const ManifestItem& item : manifest) {
            if (item.id == id) {
                const QString resolved = resolveArchivePath(opfDir, item.href);
                if (!resolved.isEmpty())
                    return imageEntryNamed(entries, resolved);
            }
        }
        return QString();
    };
    QString coverEntry;
    if (!coverFromMeta.isEmpty())
        coverEntry = manifestCover(coverFromMeta);
    if (coverEntry.isEmpty() && !coverId.isEmpty())
        coverEntry = manifestCover(coverId);
    if (coverEntry.isEmpty()) {
        for (const ManifestItem& item : manifest) {
            if (item.properties.split(QRegularExpression(QStringLiteral("\\s+")),
                                      Qt::SkipEmptyParts).contains(QStringLiteral("cover-image"))) {
                const QString resolved = resolveArchivePath(opfDir, item.href);
                coverEntry = imageEntryNamed(entries, resolved);
                if (!coverEntry.isEmpty())
                    break;
            }
        }
    }
    if (coverEntry.isEmpty())
        coverEntry = fallbackCoverEntry(entries);

    f.coverEntry = coverEntry;
    f.ok = true;
    mz_zip_reader_end(&zip);
    return f;
}

// ── Local artwork adoption ──
QString VaultEnricher::findLocalArtwork(const QString& folderPath)
{
    if (folderPath.isEmpty())
        return QString();
    QDir dir(folderPath);
    if (!dir.exists())
        return QString();

    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QString& base : artworkBasenames()) {
        for (const QString& ext : artworkExtensions()) {
            for (const QFileInfo& entry : entries) {
                if (entry.completeBaseName().compare(base, Qt::CaseInsensitive) != 0
                    || entry.suffix().compare(ext, Qt::CaseInsensitive) != 0)
                    continue;
                if (isReadableImage(entry.absoluteFilePath()))
                    return QUrl::fromLocalFile(entry.absoluteFilePath()).toString();
            }
        }
    }
    return QString();
}

QString VaultEnricher::durationKey(const QString& path, qint64 size, qint64 mtimeMs)
{
    QString n = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n + QStringLiteral("::") + QString::number(size)
        + QStringLiteral("::") + QString::number(mtimeMs);
}

void VaultEnricher::loadDurationCache()
{
    const QJsonObject o = VaultStoreIo::load(m_cacheDir, QStringLiteral("durations.json"));
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        m_durationCache.insert(it.key(), it.value().toDouble());
}

void VaultEnricher::saveDurationCache()
{
    QJsonObject o;
    for (auto it = m_durationCache.constBegin(); it != m_durationCache.constEnd(); ++it)
        o.insert(it.key(), it.value());
    VaultStoreIo::save(m_cacheDir, QStringLiteral("durations.json"), o);
}

double VaultEnricher::cachedDuration(const QString& path, qint64 size, qint64 mtimeMs) const
{
    return m_durationCache.value(durationKey(path, size, mtimeMs), -1.0);
}

void VaultEnricher::putDuration(const QString& path, qint64 size, qint64 mtimeMs, double sec)
{
    m_durationCache.insert(durationKey(path, size, mtimeMs), sec);
}

double VaultEnricher::durationForVideo(const QString& path, qint64 size, qint64 mtimeMs)
{
    const double hit = cachedDuration(path, size, mtimeMs);
    if (hit >= 0.0)
        return hit;
    const double probed = probeDurationSec(path);
    if (probed >= 0.0)
        putDuration(path, size, mtimeMs, probed);
    return probed;
}

QString VaultEnricher::findFfprobe()
{
    const QString exe =
#ifdef Q_OS_WIN
        QStringLiteral("ffprobe.exe");
#else
        QStringLiteral("ffprobe");
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& cand : {appDir + QLatin1Char('/') + exe,
                                appDir + QStringLiteral("/tools/") + exe}) {
        if (QFileInfo::exists(cand))
            return cand;
    }
    return exe; // fall back to PATH
}

double VaultEnricher::probeDurationSec(const QString& path)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(findFfprobe(), {QStringLiteral("-v"), QStringLiteral("quiet"),
                               QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                               QStringLiteral("-of"),
                               QStringLiteral("default=noprint_wrappers=1:nokey=1"), path});
    if (!proc.waitForFinished(5000)) {
        proc.kill();            // TB2's pattern leaked the process on timeout — kill it
        proc.waitForFinished(1000);
        return -1.0;
    }
    bool ok = false;
    const double sec = QString::fromLatin1(proc.readAll()).trimmed().toDouble(&ok);
    return (ok && sec > 0.0) ? sec : -1.0;
}

// ── Orchestration ─────────────────────────────────────────────────────
void VaultEnricher::enrich(const QList<VaultIndex::FileRow>& rows,
                           const VaultKit::CancellationToken* cancel)
{
    // Buffer the enriched rows and hand them to the owner thread in ONE batch at the end, instead
    // of an owner-thread DB write per file from this (possibly worker) thread.
    QList<VaultIndex::FileRow> enrichedRows;
    enrichedRows.reserve(rows.size());

    int done = 0;
    const int total = rows.size();
    for (const VaultIndex::FileRow& r0 : rows) {
        if (cancel && cancel->isCancelled())
            break;
        VaultIndex::FileRow row = r0;
        // A drive-away row is a truthful unavailable state, not an extraction failure. The
        // filesystem check also covers the narrow boot race before the watcher emits away=true.
        if (row.away || !QFileInfo::exists(row.path)) {
            enrichedRows.push_back(row);
            ++done;
            emit progress(done, total);
            continue;
        }
        if (row.kind == QLatin1String("comic")) {
            const ComicFacts cf = readComicFacts(row.path);
            if (cf.ok) {
                row.pages = cf.pages;
                row.coverRef = cf.coverEntry;
                row.errorState.clear();
                row.errorDetail.clear();
            } else {
                row.errorState = QStringLiteral("corrupt");
                row.errorDetail = cf.errorDetail;
            }
        } else if (row.kind == QLatin1String("video")) {
            row.durationSec = durationForVideo(row.path, row.size, row.mtimeMs);
            if (row.admissionVerdict.isEmpty()) {
                // Blocking by contract; MediaAdmissionProbe exposes no cancel token, so cancellation
                // is honored only BETWEEN files (the loop guard), never mid-probe.
                const MediaAdmissionProbe::Result admission =
                    MediaAdmissionProbe::probe(row.path);
                row.admissionVerdict = admissionVerdictName(admission.verdict);
                row.admissionDetail = admission.detail;
                if (row.admissionVerdict != QLatin1String("Admitted")) {
                    row.errorState = QStringLiteral("rejected");
                    row.errorDetail = row.admissionDetail;
                } else if (row.errorState == QLatin1String("rejected")) {
                    row.errorState.clear();
                    row.errorDetail.clear();
                }
            }
            // Local artwork adoption (Slice 3): the group's own folder is subtreePath (a video
            // group is one file, one folder — VaultScanner's own convention, the same one
            // browseAt's Film-node decoration already relies on). Convention-only, never
            // re-adopting once a ref is already recorded (a stale/removed companion is not
            // this pass's business to clear).
            if (row.coverRef.isEmpty()) {
                const QString folder = !row.subtreePath.isEmpty()
                    ? row.subtreePath : QFileInfo(row.path).absolutePath();
                row.coverRef = findLocalArtwork(folder);
            }
        } else if (row.kind == QLatin1String("book")) {
            row.format = QFileInfo(row.path).suffix().toLower();
            if (row.format == QLatin1String("epub")) {
                const BookFacts bf = readBookFacts(row.path);
                if (bf.ok) {
                    if (!bf.title.isEmpty())
                        row.displayTitle = bf.title;
                    row.author = bf.author;
                    row.synopsis = bf.synopsis;
                    row.coverRef = bf.coverEntry;
                    row.metadataSource = QStringLiteral("EPUB");
                    row.errorState.clear();
                    row.errorDetail.clear();
                } else {
                    row.errorState = QStringLiteral("corrupt");
                    row.errorDetail = bf.errorDetail;
                }
            }
        }
        enrichedRows.push_back(row);
        ++done;
        emit progress(done, total);
        if (done % 20 == 0)
            saveDurationCache();
    }
    saveDurationCache();
    commitRowsOnIndexThread(std::move(enrichedRows));
}

void VaultEnricher::commitRowsOnIndexThread(QList<VaultIndex::FileRow> rows)
{
    QPointer<VaultIndex> index(m_index);
    QPointer<VaultEnricher> self(this);

    auto commit = [index, self, rows = std::move(rows)]() mutable {
        if (index && !rows.isEmpty())
            index->upsertMany(rows);
        if (self)
            emit self->enrichmentFinished();
    };

    if (!m_index || QThread::currentThread() == m_index->thread()) {
        commit();
        return;
    }

    // Never fall back to a worker-thread QSqlDatabase write: hop to the index's thread.
    if (!QMetaObject::invokeMethod(m_index, std::move(commit), Qt::QueuedConnection))
        emit enrichmentFinished();
}
