// Atomic local volume index + archive ingestion contract.
//
// A REAL cbz (tests/fixtures/tankoban/tiny-volume.cbz, three valid 1x1 PNGs)
// enters MangaVolumeArchiveIngestor, is extracted by the OS archiver, naturally
// ordered and atomically published into MangaVolumeIndex. The index survives a
// simulated restart (reload) and self-heals a missing payload down to "none".
// The WeebCentral packer's no-archive publish() path yields the same ready
// shape, and its natural-sort ordering is proven with 1/2/10-named inputs whose
// lexical order (1,10,2) differs from the natural order (1,2,10).
//
// Async note: ingestArchive runs the extractor via QProcess, so the harness
// owns a QCoreApplication and a waitFor() event-loop pump.
#include "engine/MangaTankobanLogic.h"
#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/MangaVolumeIndex.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

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

QString fixturePath(const QString& name)
{
    return QStringLiteral(TANKOBAN_FIXTURES_DIR) + QLatin1Char('/') + name;
}

// Spin a QEventLoop until `signal` on `obj` fires, or timeoutMs elapses.
// Returns true iff the signal fired first.
template <typename Obj, typename Signal>
bool waitFor(Obj* obj, Signal signal, int timeoutMs)
{
    QEventLoop loop;
    bool fired = false;
    const QMetaObject::Connection c =
        QObject::connect(obj, signal, &loop, [&]() { fired = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, [&]() { loop.quit(); });
    loop.exec();
    QObject::disconnect(c);
    return fired;
}

// Minimal valid 1x1 truecolour PNG (matches the generator behind tiny-volume.cbz):
// correct magic + a real zlib IDAT so a magic-byte "decodable image" check passes.
const unsigned char kPng[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xde,0x00,0x00,0x00,0x0c,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0x00,
    0x00,0x03,0x01,0x01,0x00,0xc9,0xfe,0x92,0xef,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
    0x44,0xae,0x42,0x60,0x82
};

// Write a valid PNG plus `pad` trailing bytes (ignored by decoders, magic intact)
// so files can be told apart by size after being renamed to page_NNN.
bool writePaddedPng(const QString& path, int pad)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
    if (pad > 0) f.write(QByteArray(pad, '\0'));
    return true;
}

bool startsWithPngMagic(const QString& localFile)
{
    QFile f(localFile);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(8);
    return head == QByteArray(reinterpret_cast<const char*>(kPng), 8);
}

QString pageLocalFile(const QVariantMap& page)
{
    return page.value(QStringLiteral("url")).toUrl().toLocalFile();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("MangaVolumeIndexHarness"));

    QTemporaryDir root;
    require(root.isValid(), "temp index root created");

    MangaVolumeIndex index(root.path());
    MangaVolumeArchiveIngestor ingestor(&index);

    // A hard failure aborts immediately with its reason rather than idling to timeout.
    QObject::connect(&ingestor, &MangaVolumeArchiveIngestor::failed, &app,
        [](const QString& id, const QString& reason) {
            std::cerr << "FAIL: ingest failed " << id.toStdString()
                      << ": " << reason.toStdString() << '\n';
            std::exit(1);
        });

    // ── Nyaa archive ingestion ────────────────────────────────────────────────
    VolumeProvenance record;
    record.id           = volumeId(QStringLiteral("mangadex:berserk"), QStringLiteral("2"));
    record.seriesId     = QStringLiteral("mangadex:berserk");
    record.seriesTitle  = QStringLiteral("Berserk");
    record.volumeNumber = QStringLiteral("2");
    record.sourceKind   = QStringLiteral("nyaa");
    record.releaseTitle = QStringLiteral("Berserk v02 (Digital) (danke)");
    record.uploader     = QStringLiteral("danke");
    record.infoHash     = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

    // Copy the committed fixture: the ingestor takes ownership and DELETES the
    // source archive, so it must never touch the repo's checked-in fixture.
    const QString inputCbz = root.path() + QStringLiteral("/ingest-input.cbz");
    require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")), inputCbz),
            "fixture copied to a disposable ingest input");

    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "unknown volume reports state none before ingest");

    ingestor.ingestArchive(record, inputCbz);
    require(waitFor(&ingestor, &MangaVolumeArchiveIngestor::finished, 30000),
            "ingestArchive emitted finished before timeout");

    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("ready"),
            "published only after extraction");
    QVariantList pages = index.localPages(record.id);
    require(pages.size() == 3, "three naturally ordered pages");

    // Reader shape: index ascending 0..2, page_000 first, page_002 last.
    require(pages[0].toMap().value(QStringLiteral("index")).toInt() == 0
                && pages[1].toMap().value(QStringLiteral("index")).toInt() == 1
                && pages[2].toMap().value(QStringLiteral("index")).toInt() == 2,
            "page index runs ascending from zero");
    require(pages[0].toMap().value(QStringLiteral("group")).toInt() == 0,
            "single-source volume pages carry chapter-group 0");
    require(pageLocalFile(pages[0].toMap()).contains(QStringLiteral("page_000"))
                && pageLocalFile(pages[2].toMap()).contains(QStringLiteral("page_002")),
            "pages named page_000..page_002 in order");

    // Anti-stub: the published page is the REAL extracted PNG, not a placeholder.
    require(startsWithPngMagic(pageLocalFile(pages[0].toMap())),
            "published page is a real extracted image (PNG magic)");
    // Ownership transfer: the disposable source archive is gone.
    require(!QFile::exists(inputCbz), "source archive deleted after publish");

    // ── Atomic index survives a restart ───────────────────────────────────────
    index.reload();
    require(index.localPages(record.id).size() == 3, "atomic index survives restart");
    // Independent proof: a fresh index over the same root reads the same payload.
    {
        MangaVolumeIndex reopened(root.path());
        require(reopened.localPages(record.id).size() == 3,
                "fresh index instance reads the persisted volume");
    }

    // ── Self-heal prunes a missing payload ────────────────────────────────────
    const QString pagePath = pageLocalFile(index.localPages(record.id).at(1).toMap());
    require(QFile::remove(pagePath), "removed one published page file from disk");
    // I1: statusOf is file-aware. A surviving dir with one page gone is NOT ready
    // even BEFORE heal() runs — statusOf agrees with localPages, no pre-heal needed.
    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "missing page reports state none without heal()");
    require(index.localPages(record.id).size() == 2,
            "localPages drops the missing page (statusOf agrees)");
    index.heal();
    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "missing payload is pruned");
    // remove() is idempotent on an already-pruned id.
    require(!index.remove(record.id), "remove of an already-pruned id is a no-op");

    // ── WeebCentral publish: no archive, same ready shape, natural sort ────────
    VolumeProvenance wc;
    wc.id           = volumeId(QStringLiteral("weebcentral:vinland-saga"), QStringLiteral("2"));
    wc.seriesId     = QStringLiteral("weebcentral:vinland-saga");
    wc.seriesTitle  = QStringLiteral("Vinland Saga");
    wc.volumeNumber = QStringLiteral("2");
    wc.sourceKind   = QStringLiteral("weebcentral");
    wc.releaseTitle = QStringLiteral("Vinland Saga Vol. 2");
    wc.chapterIds   = QStringList{QStringLiteral("ch-6"), QStringLiteral("ch-7")};

    QTemporaryDir prepared;
    require(prepared.isValid(), "prepared page dir created");
    // Lexical order would be 1,10,2 — natural order must be 1,2,10. Pad by ordinal
    // so page byte-sizes rise strictly in the CORRECT (natural) order.
    require(writePaddedPng(prepared.path() + QStringLiteral("/img1.png"), 1), "wrote img1");
    require(writePaddedPng(prepared.path() + QStringLiteral("/img2.png"), 2), "wrote img2");
    require(writePaddedPng(prepared.path() + QStringLiteral("/img10.png"), 10), "wrote img10");

    require(ingestor.publish(wc, prepared.path()), "publish of a prepared page dir succeeds");
    require(index.statusOf(wc.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("ready"),
            "prepared-dir publish yields the same ready shape");
    QVariantList wcPages = index.localPages(wc.id);
    require(wcPages.size() == 3, "prepared-dir publish yields three pages");
    require(startsWithPngMagic(pageLocalFile(wcPages[0].toMap())),
            "published prepared page is a real image");
    // Natural sort proof: sizes strictly ascend (img1 < img2 < img10). Lexical
    // sort (1,10,2) would put the +10 pad in the middle and break this.
    const qint64 s0 = QFileInfo(pageLocalFile(wcPages[0].toMap())).size();
    const qint64 s1 = QFileInfo(pageLocalFile(wcPages[1].toMap())).size();
    const qint64 s2 = QFileInfo(pageLocalFile(wcPages[2].toMap())).size();
    require(s0 < s1 && s1 < s2, "pages ordered by natural (numeric) sort, not lexical");

    // ── remove() deletes the pages dir + entry, and is idempotent ─────────────
    const QString wcDir = QFileInfo(pageLocalFile(wcPages[0].toMap())).absolutePath();
    require(QDir(wcDir).exists(), "published volume has an on-disk pages dir");
    require(index.remove(wc.id), "remove() reports it removed the volume");
    require(!QDir(wcDir).exists(), "remove() deleted the pages dir");
    require(index.statusOf(wc.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "removed volume reports state none");
    require(!index.remove(wc.id), "second remove() of the same id is a no-op");

    // ── I2: publish carries per-page chapter groups (WeebCentral multi-chapter) ─
    VolumeProvenance grp;
    grp.id           = volumeId(QStringLiteral("weebcentral:berserk"), QStringLiteral("3"));
    grp.seriesId     = QStringLiteral("weebcentral:berserk");
    grp.seriesTitle  = QStringLiteral("Berserk");
    grp.volumeNumber = QStringLiteral("3");
    grp.sourceKind   = QStringLiteral("weebcentral");
    grp.chapterIds   = QStringList{QStringLiteral("ch-8"), QStringLiteral("ch-9")};

    QTemporaryDir grpDir;
    require(grpDir.isValid(), "grouped prepared dir created");
    require(writePaddedPng(grpDir.path() + QStringLiteral("/img1.png"), 0), "wrote grouped img1");
    require(writePaddedPng(grpDir.path() + QStringLiteral("/img2.png"), 0), "wrote grouped img2");
    require(writePaddedPng(grpDir.path() + QStringLiteral("/img3.png"), 0), "wrote grouped img3");

    // Two chapters packed into one volume: pages 1-2 are chapter group 0, page 3
    // is group 1. Groups are given in natural-sorted page order (1,2,3).
    require(ingestor.publish(grp, grpDir.path(), QVector<int>{0, 0, 1}),
            "publish honours a per-page chapter-group list");
    QVariantList grpPages = index.localPages(grp.id);
    require(grpPages.size() == 3, "grouped publish yields three pages");
    require(grpPages[0].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[1].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[2].toMap().value(QStringLiteral("group")).toInt() == 1,
            "chapter groups stored in natural page order (0,0,1)");
    // Groups survive the atomic ledger round-trip.
    index.reload();
    grpPages = index.localPages(grp.id);
    require(grpPages.size() == 3, "grouped volume survives reload");
    require(grpPages[0].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[1].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[2].toMap().value(QStringLiteral("group")).toInt() == 1,
            "chapter groups preserved across reload");

    std::cout << "MANGA_VOLUME_INDEX_OK\n";
    return 0;
}
