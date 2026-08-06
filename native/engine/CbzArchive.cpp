#include "engine/CbzArchive.h"

#include "third_party/miniz/miniz.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cstdlib>

namespace MangaTankoban {
namespace {

void setError(QString* error, const QString& value)
{
    if (error) *error = value;
}

QByteArray nativePath(const QString& path)
{
    return QFile::encodeName(QDir::toNativeSeparators(path));
}

bool imageNameAccepted(const QString& name)
{
    return CbzArchive::isAcceptedImageEntryName(name);
}

QString zipError(mz_zip_archive& zip)
{
    return QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
}

// A cheap "is this a real image" gate: sniff the leading magic bytes rather than
// fully decoding — the payload is trusted archive content, so this is enough to
// reject a corrupt/garbage/HTML-interstitial entry. Mirrors the equivalent check
// in MangaVolumeArchiveIngestor.cpp (looksDecodable), adapted to take bytes
// already in hand rather than reopening a path, since probe() samples entries it
// has already extracted to memory via readEntry().
bool sniffLooksDecodable(const QByteArray& h)
{
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

// ZIP general-purpose bit flag 0 (per the spec): entry is encrypted.
constexpr mz_uint16 kZipEncryptedBit = 0x1;

} // namespace

bool CbzArchive::isAcceptedImageEntryName(const QString& name)
{
    static const QSet<QString> extensions{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
        QStringLiteral("avif")
    };
    const QString clean = QDir::fromNativeSeparators(name);
    if (clean.startsWith(QStringLiteral("__MACOSX/"), Qt::CaseInsensitive))
        return false;
    const QString leaf = QFileInfo(clean).fileName();
    return !leaf.startsWith(QLatin1Char('.'))
        && extensions.contains(QFileInfo(leaf).suffix().toLower());
}

QVector<CbzPageEntry> CbzArchive::imageEntries(const QString& archivePath, QString* error)
{
    QVector<CbzPageEntry> result;
    mz_zip_archive zip{};
    const QByteArray path = nativePath(archivePath);
    if (!mz_zip_reader_init_file(&zip, path.constData(), 0)) {
        setError(error, QStringLiteral("cannot open CBZ: %1").arg(zipError(zip)));
        return result;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    result.reserve(static_cast<int>(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)
            || stat.m_is_directory
            || !stat.m_filename)
            continue;
        const QString name = QString::fromUtf8(stat.m_filename);
        if (!imageNameAccepted(name))
            continue;
        result.append(CbzPageEntry{name, static_cast<quint64>(stat.m_uncomp_size)});
    }
    mz_zip_reader_end(&zip);

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const CbzPageEntry& a,
                                                        const CbzPageEntry& b) {
        return collator.compare(a.name, b.name) < 0;
    });
    setError(error, QString());
    return result;
}

QByteArray CbzArchive::readEntry(const QString& archivePath,
                                 const QString& entryName,
                                 QString* error)
{
    mz_zip_archive zip{};
    const QByteArray path = nativePath(archivePath);
    if (!mz_zip_reader_init_file(&zip, path.constData(), 0)) {
        setError(error, QStringLiteral("cannot open CBZ: %1").arg(zipError(zip)));
        return {};
    }

    const QByteArray entry = entryName.toUtf8();
    const int index = mz_zip_reader_locate_file(&zip, entry.constData(), nullptr, 0);
    if (index < 0) {
        setError(error, QStringLiteral("CBZ entry not found: %1").arg(entryName));
        mz_zip_reader_end(&zip);
        return {};
    }

    size_t size = 0;
    void* memory = mz_zip_reader_extract_to_heap(
        &zip, static_cast<mz_uint>(index), &size, 0);
    if (!memory) {
        setError(error, QStringLiteral("cannot extract CBZ entry %1: %2")
                            .arg(entryName, zipError(zip)));
        mz_zip_reader_end(&zip);
        return {};
    }
    const QByteArray bytes(static_cast<const char*>(memory), static_cast<qsizetype>(size));
    mz_free(memory);
    mz_zip_reader_end(&zip);
    setError(error, QString());
    return bytes;
}

bool CbzArchive::writeImagesAtomic(const QString& archivePath,
                                   const QString& sourceDir,
                                   const QStringList& orderedRelativeFiles,
                                   QString* error)
{
    if (orderedRelativeFiles.isEmpty()) {
        setError(error, QStringLiteral("cannot write an empty CBZ"));
        return false;
    }
    for (const QString& relative : orderedRelativeFiles) {
        if (!QFileInfo::exists(QDir(sourceDir).absoluteFilePath(relative))) {
            setError(error, QStringLiteral("source page missing: %1").arg(relative));
            return false;
        }
    }

    const QString partPath = archivePath + QStringLiteral(".part");
    QFile::remove(partPath);
    if (!QDir().mkpath(QFileInfo(archivePath).absolutePath())) {
        setError(error, QStringLiteral("cannot create CBZ parent directory"));
        return false;
    }

    mz_zip_archive zip{};
    const QByteArray part = nativePath(partPath);
    if (!mz_zip_writer_init_file(&zip, part.constData(), 0)) {
        setError(error, QStringLiteral("cannot create CBZ: %1").arg(zipError(zip)));
        return false;
    }

    bool ok = true;
    for (const QString& relative : orderedRelativeFiles) {
        const QByteArray entry = QDir::fromNativeSeparators(relative).toUtf8();
        const QByteArray source =
            nativePath(QDir(sourceDir).absoluteFilePath(relative));
        if (!mz_zip_writer_add_file(&zip, entry.constData(), source.constData(),
                                    nullptr, 0, MZ_NO_COMPRESSION)) {
            setError(error, QStringLiteral("cannot add %1 to CBZ: %2")
                                .arg(relative, zipError(zip)));
            ok = false;
            break;
        }
    }
    if (ok && !mz_zip_writer_finalize_archive(&zip)) {
        setError(error, QStringLiteral("cannot finalize CBZ: %1").arg(zipError(zip)));
        ok = false;
    }
    mz_zip_writer_end(&zip);

    if (!ok) {
        QFile::remove(partPath);
        return false;
    }

    QString validationError;
    const QVector<CbzPageEntry> reopened = imageEntries(partPath, &validationError);
    if (reopened.size() != orderedRelativeFiles.size()) {
        setError(error, validationError.isEmpty()
                            ? QStringLiteral("CBZ validation page count mismatch")
                            : validationError);
        QFile::remove(partPath);
        return false;
    }

    if (QFileInfo::exists(archivePath)) {
        setError(error, QStringLiteral("refusing to replace an existing CBZ"));
        QFile::remove(partPath);
        return false;
    }
    if (!QFile::rename(partPath, archivePath)) {
        setError(error, QStringLiteral("cannot atomically finalize CBZ"));
        QFile::remove(partPath);
        return false;
    }
    setError(error, QString());
    return true;
}

CbzProbeResult CbzArchive::probe(const QString& archivePath, QString* error)
{
    CbzProbeResult result;

    mz_zip_archive zip{};
    const QByteArray path = nativePath(archivePath);
    if (!mz_zip_reader_init_file(&zip, path.constData(), 0)) {
        setError(error, QStringLiteral("cannot open CBZ: %1").arg(zipError(zip)));
        return result;
    }

    QSet<QString> seenLower;
    bool methodOrEncryptionRejected = false;
    bool duplicateRejected = false;

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    result.entries.reserve(static_cast<int>(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)
            || stat.m_is_directory
            || !stat.m_filename)
            continue;
        const QString name = QString::fromUtf8(stat.m_filename);
        if (!imageNameAccepted(name))
            continue;
        if (stat.m_method != 0 && stat.m_method != MZ_DEFLATED)
            methodOrEncryptionRejected = true;
        if (stat.m_bit_flag & kZipEncryptedBit)
            methodOrEncryptionRejected = true;
        const QString lower = name.toLower();
        if (seenLower.contains(lower))
            duplicateRejected = true;
        seenLower.insert(lower);
        result.entries.append(CbzPageEntry{name, static_cast<quint64>(stat.m_uncomp_size)});
    }
    mz_zip_reader_end(&zip);

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.entries.begin(), result.entries.end(),
              [&collator](const CbzPageEntry& a, const CbzPageEntry& b) {
        return collator.compare(a.name, b.name) < 0;
    });

    if (result.entries.isEmpty()) {
        setError(error, QStringLiteral("CBZ contains no accepted image entries"));
        return result;
    }
    if (methodOrEncryptionRejected) {
        setError(error, QStringLiteral(
            "CBZ uses an unsupported compression method or is encrypted"));
        return result;
    }
    if (duplicateRejected) {
        setError(error, QStringLiteral(
            "CBZ has duplicate entry names — readEntry() would resolve them to the "
            "first match only"));
        return result;
    }

    // Sample first/middle/last rather than every page — a corrupt archive that
    // fails the method/encryption check above never reaches this point, and a
    // well-formed store/deflate zip is either consistently readable or not; a
    // spot check catches a truncated/garbage entry without paying to decode-sniff
    // every page of a 60-issue omnibus.
    QSet<int> sampleIdx{0, static_cast<int>(result.entries.size() / 2),
                        static_cast<int>(result.entries.size() - 1)};
    for (int idx : sampleIdx) {
        QString sampleError;
        const QByteArray bytes = readEntry(archivePath, result.entries.at(idx).name, &sampleError);
        if (bytes.isEmpty() || !sniffLooksDecodable(bytes)) {
            setError(error, QStringLiteral("CBZ entry does not look decodable: %1")
                                .arg(result.entries.at(idx).name));
            return result;
        }
    }

    setError(error, QString());
    result.nativelyReadable = true;
    return result;
}

} // namespace MangaTankoban
