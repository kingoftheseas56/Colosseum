#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace MangaTankoban {

struct CbzPageEntry
{
    QString name;
    quint64 uncompressedBytes = 0;
};

// imageEntries() proves a file is a well-formed ZIP whose central directory lists
// image-suffixed entries — it does NOT prove those entries are actually decodable.
// miniz only inflates store (method 0) and deflate (method 8); a CBZ packed with
// LZMA (7-Zip's default for "Ultra" profiles) or bzip2/PPMd, or one with encrypted
// entries, lists perfectly through imageEntries() and then fails at every
// readEntry() call. probe() is the gate a caller must pass before trusting a
// downloaded archive as the durable, un-extracted library artifact (2026-08-06
// comics CBZ-in-place arc).
struct CbzProbeResult
{
    QVector<CbzPageEntry> entries;      // populated even when nativelyReadable is false, for diagnostics
    bool nativelyReadable = false;
};

class CbzArchive
{
public:
    static QVector<CbzPageEntry> imageEntries(const QString& archivePath,
                                               QString* error = nullptr);
    static QByteArray readEntry(const QString& archivePath,
                                const QString& entryName,
                                QString* error = nullptr);
    static bool writeImagesAtomic(const QString& archivePath,
                                  const QString& sourceDir,
                                  const QStringList& orderedRelativeFiles,
                                  QString* error = nullptr);

    // Deliberately NOT gated by file extension — a source may be misnamed (a
    // ".cbr" that's actually a plain zip, or vice versa); probe by content only.
    // nativelyReadable is true only when: the zip opens, entries are non-empty,
    // every entry uses store/deflate compression with no encryption bit set, no
    // two entries share a (case-insensitive) name — mz_zip_reader_locate_file
    // resolves a duplicate to its FIRST match, so readEntry() would silently
    // return the wrong page for every entry after the first collision — and a
    // first/middle/last sample of entries byte-sniffs as a real image. Anything
    // that fails any check should be treated as "extract, don't trust in place".
    static CbzProbeResult probe(const QString& archivePath, QString* error = nullptr);

    // The exact entry-name filter probe()/imageEntries() apply internally
    // (image-suffixed, not under a __MACOSX/ tree, not a dot-leading leaf name)
    // — exposed publicly so any caller collecting files to PACK into a CBZ
    // (e.g. ComicDownloader's repack-from-extraction path) can filter its
    // source list through the SAME rule before writeImagesAtomic, rather than
    // duplicating it and risking drift: a source list built via a plain
    // suffix check would pack `__MACOSX/._page01.jpg` (real, from Mac-authored
    // CBRs), which probe() then silently drops on readback — an index/archive
    // page-count mismatch with no error.
    static bool isAcceptedImageEntryName(const QString& name);
};

} // namespace MangaTankoban

