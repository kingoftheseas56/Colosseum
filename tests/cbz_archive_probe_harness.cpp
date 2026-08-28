// CbzArchive::probe() — proves a CBZ's pages are actually decodable, not merely that
// the zip's central directory lists them (2026-08-06 comics CBZ-in-place arc).
//
// Fixtures for the "lies about its compression method" and "claims encryption" cases
// are built by writing a normal store-mode CBZ via writeImagesAtomic (which the
// existing cbz_archive_harness already proves correct), then surgically patching the
// two-byte compression-method / bit-flag fields inside the CENTRAL DIRECTORY entry —
// confirmed against miniz.c directly: mz_zip_file_stat_internal reads m_method from
// MZ_ZIP_CDH_METHOD_OFS=10 and the encryption bit from MZ_ZIP_CDH_BIT_FLAG_OFS=8,
// both relative to the "PK\x01\x02" central-directory-header signature. miniz's writer
// cannot itself produce LZMA (it only implements store/deflate), so this is the only
// way to exercise the rejection path without a second compression library.
#include "engine/CbzArchive.h"
#include "third_party/miniz/miniz.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// A minimal but real-looking JPEG: SOI marker + enough bytes to pass the sniff.
QByteArray fakeJpeg()
{
    return QByteArray("\xFF\xD8\xFF\xE0page-bytes-enough-to-look-like-a-real-jpeg", 42);
}

// Patch the 2-byte field at `fieldOffset` (relative to the central-directory-header
// signature) of the FIRST central directory entry found. Returns false if the
// signature can't be located (fixture construction bug, not a probe() bug).
bool patchFirstCentralDirectoryField(const QString& path, int fieldOffset, quint16 value)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return false;
    QByteArray data = f.readAll();
    const char sig[4] = { 0x50, 0x4B, 0x01, 0x02 }; // "PK\x01\x02"
    const int pos = data.indexOf(QByteArray(sig, 4));
    if (pos < 0) return false;
    const int at = pos + fieldOffset;
    if (at + 2 > data.size()) return false;
    data[at] = static_cast<char>(value & 0xFF);
    data[at + 1] = static_cast<char>((value >> 8) & 0xFF);
    f.seek(0);
    f.write(data);
    f.resize(data.size());
    return true;
}

constexpr int kCdhBitFlagOffset = 8;
constexpr int kCdhMethodOffset = 10;
// High 16 bits of the 4-byte central-directory uncompressed-size field (CDH+24).
constexpr int kCdhUncompSizeHighOffset = 26;
constexpr quint16 kLzmaMethod = 14;
constexpr quint16 kEncryptedBit = 0x1;

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary root created");

    // ---- 1. a plain store-mode CBZ probes nativelyReadable ---------------------------

    {
        const QString pages = temp.path() + QStringLiteral("/ok-pages");
        require(QDir().mkpath(pages), "ok pages dir created");
        require(writeBytes(pages + QStringLiteral("/page_1.jpg"), fakeJpeg()), "page 1 written");
        require(writeBytes(pages + QStringLiteral("/page_2.jpg"), fakeJpeg()), "page 2 written");
        require(writeBytes(pages + QStringLiteral("/page_3.jpg"), fakeJpeg()), "page 3 written");
        const QString archive = temp.path() + QStringLiteral("/ok.cbz");
        QString error;
        require(CbzArchive::writeImagesAtomic(
                    archive, pages,
                    { QStringLiteral("page_1.jpg"), QStringLiteral("page_2.jpg"),
                      QStringLiteral("page_3.jpg") }, &error),
                "ok fixture written");

        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(result.nativelyReadable, "a plain store-mode CBZ probes nativelyReadable");
        require(result.entries.size() == 3, "probe lists all three pages");
    }

    // ---- 2. an entry claiming an unsupported compression method is REJECTED ----------

    {
        const QString pages = temp.path() + QStringLiteral("/lzma-pages");
        require(QDir().mkpath(pages), "lzma pages dir created");
        require(writeBytes(pages + QStringLiteral("/page_1.jpg"), fakeJpeg()), "lzma page written");
        const QString archive = temp.path() + QStringLiteral("/lzma.cbz");
        QString error;
        require(CbzArchive::writeImagesAtomic(archive, pages, { QStringLiteral("page_1.jpg") }, &error),
                "lzma-fixture base write succeeds");
        require(patchFirstCentralDirectoryField(archive, kCdhMethodOffset, kLzmaMethod),
                "central directory method field patched to LZMA");

        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(!result.nativelyReadable,
                "an entry claiming LZMA (method 14) must be rejected — miniz cannot inflate it");
        require(!error.isEmpty(), "probe reports a reason for the rejection");
    }

    // ---- 3. an entry with the encrypted bit set is REJECTED --------------------------

    {
        const QString pages = temp.path() + QStringLiteral("/enc-pages");
        require(QDir().mkpath(pages), "encrypted pages dir created");
        require(writeBytes(pages + QStringLiteral("/page_1.jpg"), fakeJpeg()), "encrypted page written");
        const QString archive = temp.path() + QStringLiteral("/enc.cbz");
        QString error;
        require(CbzArchive::writeImagesAtomic(archive, pages, { QStringLiteral("page_1.jpg") }, &error),
                "encrypted-fixture base write succeeds");
        require(patchFirstCentralDirectoryField(archive, kCdhBitFlagOffset, kEncryptedBit),
                "central directory bit-flag field patched to encrypted");

        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(!result.nativelyReadable, "an entry with the encrypted bit set must be rejected");
    }

    // ---- 4. duplicate entry names are REJECTED ----------------------------------------

    {
        const QString pages = temp.path() + QStringLiteral("/dup-pages");
        require(QDir().mkpath(pages), "dup pages dir created");
        require(writeBytes(pages + QStringLiteral("/page_1.jpg"), fakeJpeg()), "dup page A written");
        require(QDir().mkpath(pages + QStringLiteral("/sub")), "dup sub dir created");
        require(writeBytes(pages + QStringLiteral("/sub/page_1.jpg"), fakeJpeg()), "dup page B written");
        // Two DIFFERENT source files, same in-archive entry NAME — writeImagesAtomic has
        // no uniqueness check on entry names, only on source-file existence, so packing
        // both under the identical archive name "page_1.jpg" is a legitimate fixture.
        const QString archive = temp.path() + QStringLiteral("/dup.cbz");
        mz_zip_archive zip{};
        // Hand-build via miniz directly (bypassing writeImagesAtomic, which takes ONE
        // relative-path list and can't express two sources sharing one archive name).
        require(mz_zip_writer_init_file(&zip, archive.toUtf8().constData(), 0),
                "dup zip writer init");
        const QByteArray srcA = (pages + QStringLiteral("/page_1.jpg")).toUtf8();
        const QByteArray srcB = (pages + QStringLiteral("/sub/page_1.jpg")).toUtf8();
        require(mz_zip_writer_add_file(&zip, "page_1.jpg", srcA.constData(), nullptr, 0, MZ_NO_COMPRESSION),
                "dup entry A added");
        require(mz_zip_writer_add_file(&zip, "page_1.jpg", srcB.constData(), nullptr, 0, MZ_NO_COMPRESSION),
                "dup entry B added under the same name");
        require(mz_zip_writer_finalize_archive(&zip), "dup zip finalized");
        mz_zip_writer_end(&zip);

        QString error;
        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(!result.nativelyReadable,
                "two entries sharing one name must be rejected — locate_file resolves "
                "to the first match only, so readEntry() would silently serve the wrong page");
    }

    // ---- 5. an entry that doesn't sniff as a real image is REJECTED ------------------

    {
        const QString pages = temp.path() + QStringLiteral("/garbage-pages");
        require(QDir().mkpath(pages), "garbage pages dir created");
        require(writeBytes(pages + QStringLiteral("/page_1.jpg"),
                            QByteArray("this is not image data, just text with a .jpg name")),
                "garbage page written");
        const QString archive = temp.path() + QStringLiteral("/garbage.cbz");
        QString error;
        require(CbzArchive::writeImagesAtomic(archive, pages, { QStringLiteral("page_1.jpg") }, &error),
                "garbage-fixture write succeeds (writeImagesAtomic doesn't sniff content)");

        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(!result.nativelyReadable,
                "an entry whose bytes don't sniff as a real image must be rejected");
    }

    // ---- 6. a corrupt/non-zip file is REJECTED (entries empty, no crash) -------------

    {
        const QString archive = temp.path() + QStringLiteral("/corrupt.cbz");
        require(writeBytes(archive, QByteArray("not a zip file at all")), "corrupt fixture written");
        QString error;
        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(!result.nativelyReadable, "a non-zip file must be rejected");
        require(result.entries.isEmpty(), "a non-zip file yields no entries");
    }

    // ---- 7. an entry declaring an implausibly huge uncompressed size is REJECTED ----
    //         Guards against a hostile archive that forces a multi-GB allocation on
    //         open (DoS). Surfaced by the CBZ fuzz target as an out-of-memory.

    {
        const QString pages = temp.path() + QStringLiteral("/huge-pages");
        require(QDir().mkpath(pages), "huge pages dir created");
        require(writeBytes(pages + QStringLiteral("/page_1.jpg"), fakeJpeg()), "huge page written");
        const QString archive = temp.path() + QStringLiteral("/huge.cbz");
        QString error;
        require(CbzArchive::writeImagesAtomic(archive, pages, { QStringLiteral("page_1.jpg") }, &error),
                "huge-fixture base write succeeds");
        // Set the high word of the 4-byte uncompressed-size field to 0x7FFF so the
        // entry declares ~2GB — huge, but not the ZIP64 sentinel (0xFFFFFFFF).
        require(patchFirstCentralDirectoryField(archive, kCdhUncompSizeHighOffset, 0x7FFF),
                "central directory uncompressed-size high word patched to a huge value");

        // readEntry must refuse BEFORE allocating the declared size.
        const QByteArray bytes = CbzArchive::readEntry(archive, QStringLiteral("page_1.jpg"), &error);
        require(bytes.isEmpty(),
                "readEntry refuses an entry declaring an implausible uncompressed size");
        require(error.contains(QStringLiteral("implausible uncompressed size")),
                "the refusal comes from the declared-size guard (not an incidental miniz error)");

        // probe() samples via readEntry, so the archive must be rejected too.
        const CbzProbeResult result = CbzArchive::probe(archive, &error);
        require(!result.nativelyReadable,
                "an archive with an oversized-declared entry is not nativelyReadable");
    }

    std::cout << "CBZ_ARCHIVE_PROBE_HARNESS_OK\n";
    return 0;
}
