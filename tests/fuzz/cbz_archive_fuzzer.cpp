// libFuzzer target for the CBZ/ZIP untrusted-input parser.
//
// Colosseum opens comic archives (CBZ = ZIP) downloaded from the network. The
// real parsing lives in vendored miniz (native/third_party/miniz/miniz.c),
// driven by MangaTankoban::CbzArchive. This target feeds arbitrary bytes through
// the SAME production entry points the app uses on a downloaded archive:
//
//   probe()        -> mz_zip_reader_init_file + file_stat loop + sampled
//                     extract_to_heap  (central directory + local header + inflate)
//   imageEntries() -> init_file + file_stat loop
//   readEntry()    -> init_file + locate_file + extract_to_heap  (every entry)
//
// CbzArchive is path-based, so each input is written to one reused temp file and
// parsed from disk — exactly the on-disk archive the app trusts. Built with
// clang-cl -fsanitize=fuzzer,address; a memory-safety defect in miniz's parsing
// of a malformed archive, or in CbzArchive's handling of it, surfaces as an ASan
// report. (An oversized-uncompressed-size header can drive a large allocation in
// extract_to_heap; that shows up as a libFuzzer OOM and is triaged separately
// from a memory-safety crash.)

#include "engine/CbzArchive.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

#include <cstddef>
#include <cstdint>

using namespace MangaTankoban;

namespace {

// One QCoreApplication and one reused temp path for the whole process. QtCore
// path/collation/string services CbzArchive relies on are set up once; each
// iteration only rewrites the file bytes.
QCoreApplication* g_app = nullptr;
QString g_archivePath;

// Guard against a fuzz input that lists an enormous number of central-directory
// entries: we exercise readEntry on every listed entry, so cap the per-input
// extraction work to keep throughput sane. Parsing (probe/imageEntries) is still
// run in full on every input regardless of this cap.
constexpr int kMaxEntriesToExtract = 64;

} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    g_app = new QCoreApplication(*argc, *argv);
    g_archivePath = QDir::tempPath()
        + QStringLiteral("/colosseum_cbz_fuzz_%1.cbz")
              .arg(QCoreApplication::applicationPid());
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Land the raw bytes on disk as the archive the production code will open.
    {
        QFile f(g_archivePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 0; // environment failure, not an input finding
        if (size > 0)
            f.write(reinterpret_cast<const char*>(data),
                    static_cast<qint64>(size));
        f.close();
    }

    QString error;

    // Richest single path: init + stat loop + sampled extract.
    CbzArchive::probe(g_archivePath, &error);

    // Full listing, then extract EVERY listed entry (probe only samples three) to
    // reach the inflate path for entries probe skips.
    const QVector<CbzPageEntry> entries =
        CbzArchive::imageEntries(g_archivePath, &error);
    int extracted = 0;
    for (const CbzPageEntry& entry : entries) {
        if (extracted++ >= kMaxEntriesToExtract)
            break;
        QString entryError;
        CbzArchive::readEntry(g_archivePath, entry.name, &entryError);
    }

    return 0;
}
