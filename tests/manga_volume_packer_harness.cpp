// WeebCentral volume fallback packer contract (Task 7).
//
// A FAKE MangaScraper replays recorded WeebCentral fetchPages() responses from
// tests/fixtures/tankoban/weeb-pages.json: each chapter's pages are emitted as
// file:// URLs. The tiny 1x1 JPEG behind each page is written to a temp dir at
// runtime from the embedded kJpeg bytes below (so no binary fixture is committed,
// mirroring manga_volume_index_harness's embedded PNG), and the packer's own
// QNetworkAccessManager actually downloads it. The packer names each page
// c<chapterNumber:03d>_<pageInChapter:03d>.<ext> in chapter order then natural
// page order, tags it with the chapter's 0-based volume ordinal (group), and hands
// the prepared directory to the shared MangaVolumeArchiveIngestor publish path so a
// WeebCentral volume lands in MangaVolumeIndex with the SAME canonical "ready"
// shape a nyaa volume does.
//
// Proven here (a green means pages were really fetched, downloaded, validated and
// published — never a stub):
//   * full success -> {c010_001.jpg, c010_002.jpg, c011_001.jpg} / groups {0,0,1}
//                     + index state "ready" + localPages groups {0,0,1} + real JPEG
//   * a failed page download -> complete() stays false, no ready volume published
//   * a mid-pack cancel()    -> staging removed, nothing published
//
// Async note: fetchPages fires synchronously (deterministic in-flight replies for
// the cancel case), image downloads are real & async, so the harness owns a
// QCoreApplication and a waitFor() event-loop pump.
#include "engine/CbzArchive.h"
#include "engine/MangaResult.h"
#include "engine/MangaScraper.h"
#include "engine/MangaSeriesDetail.h"
#include "engine/MangaTankobanLogic.h"
#include "engine/MangaTankobanTypes.h"
#include "engine/MangaVolumeIndex.h"
#include "engine/MangaVolumePacker.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {

// A real, decodable 1x1 baseline JPEG (mandatory quantization/Huffman tables +
// one scan). Embedded so the harness needs no committed binary fixture.
const unsigned char kJpeg[] = {
    0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
    0x00,0x01,0x00,0x00,0xff,0xdb,0x00,0x43,0x00,0x08,0x06,0x06,0x07,0x06,0x05,0x08,
    0x07,0x07,0x07,0x09,0x09,0x08,0x0a,0x0c,0x14,0x0d,0x0c,0x0b,0x0b,0x0c,0x19,0x12,
    0x13,0x0f,0x14,0x1d,0x1a,0x1f,0x1e,0x1d,0x1a,0x1c,0x1c,0x20,0x24,0x2e,0x27,0x20,
    0x22,0x2c,0x23,0x1c,0x1c,0x28,0x37,0x29,0x2c,0x30,0x31,0x34,0x34,0x34,0x1f,0x27,
    0x39,0x3d,0x38,0x32,0x3c,0x2e,0x33,0x34,0x32,0xff,0xdb,0x00,0x43,0x01,0x09,0x09,
    0x09,0x0c,0x0b,0x0c,0x18,0x0d,0x0d,0x18,0x32,0x21,0x1c,0x21,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0xff,0xc0,
    0x00,0x11,0x08,0x00,0x01,0x00,0x01,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,
    0x01,0xff,0xc4,0x00,0x1f,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,
    0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,
    0x05,0x04,0x04,0x00,0x00,0x01,0x7d,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,
    0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,
    0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,
    0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,
    0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,
    0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,
    0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,
    0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,
    0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,
    0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xc4,0x00,0x1f,0x01,0x00,0x03,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
    0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x11,0x00,
    0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,
    0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,
    0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,
    0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,
    0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,0x88,
    0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,
    0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,
    0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,
    0xfa,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3f,0x00,0xf1,
    0x1a,0x28,0xa2,0xb6,0x32,0x3f,0xff,0xd9
};

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

bool writeJpeg(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(reinterpret_cast<const char*>(kJpeg), sizeof(kJpeg))
           == static_cast<qint64>(sizeof(kJpeg));
}

// Spin a QEventLoop until `signal` on `obj` fires, or timeoutMs elapses.
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

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, [&]() { loop.quit(); });
    loop.exec();
}

// Fake scraper: replays weeb-pages.json. fetchPages(chapterId) emits one PageInfo
// per recorded page, its imageUrl a file:// URL under `imagesDir` (a runtime temp
// dir the harness populated with kJpeg fixtures).
class FakeWeebScraper : public MangaScraper
{
    Q_OBJECT
public:
    FakeWeebScraper(QNetworkAccessManager* nam, const QJsonObject& chapters,
                    const QString& imagesDir, QObject* parent = nullptr)
        : MangaScraper(nam, parent), m_chapters(chapters), m_imagesDir(imagesDir) {}

    QString sourceId() const override { return QStringLiteral("weebcentral"); }
    QString sourceName() const override { return QStringLiteral("WeebCentral"); }
    void search(const QString&, int) override {}
    void fetchChapters(const QString&) override {}
    void fetchDetail(const MangaResult&) override {}

    void fetchPages(const QString& chapterId) override
    {
        fetched << chapterId; // record which chapters were actually requested
        QList<PageInfo> pages;
        const QJsonArray arr =
            m_chapters.value(chapterId).toObject().value(QStringLiteral("pages")).toArray();
        int idx = 0;
        for (const QJsonValue& v : arr) {
            PageInfo p;
            p.index = idx++;
            p.imageUrl = QUrl::fromLocalFile(m_imagesDir + QLatin1Char('/') + v.toString()).toString();
            pages.append(p);
        }
        emit pagesReady(pages); // synchronous replay
    }

    QStringList fetched; // every chapterId passed to fetchPages, in order

private:
    QJsonObject m_chapters;
    QString     m_imagesDir;
};

QByteArray pageBytes(const QVariantMap& page)
{
    return CbzArchive::readEntry(
        page.value(QStringLiteral("archive")).toString(),
        page.value(QStringLiteral("entry")).toString());
}

bool startsWithJpegMagic(const QVariantMap& page)
{
    const QByteArray head = pageBytes(page).left(3);
    return head.size() == 3
        && static_cast<unsigned char>(head[0]) == 0xFF
        && static_cast<unsigned char>(head[1]) == 0xD8
        && static_cast<unsigned char>(head[2]) == 0xFF;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("MangaVolumePackerHarness"));

    QFile jf(fixturePath(QStringLiteral("weeb-pages.json")));
    require(jf.open(QIODevice::ReadOnly), "open weeb-pages.json fixture");
    const QJsonObject chapters =
        QJsonDocument::fromJson(jf.readAll()).object().value(QStringLiteral("chapters")).toObject();
    require(!chapters.isEmpty(), "weeb-pages.json has chapters");

    QTemporaryDir imagesDir;   // page images (kJpeg), served over file://
    QTemporaryDir indexRoot;   // MangaVolumeIndex ledger + pages
    QTemporaryDir stagingRoot; // packer scratch
    require(imagesDir.isValid() && indexRoot.isValid() && stagingRoot.isValid(),
            "temp dirs created");

    // Materialize every page the fixture references EXCEPT the deliberately-missing
    // one, so its download fails and drives the partial-never-ready case.
    require(writeJpeg(imagesDir.path() + QStringLiteral("/weeb-c10-p1.jpg")), "wrote c10 p1");
    require(writeJpeg(imagesDir.path() + QStringLiteral("/weeb-c10-p2.jpg")), "wrote c10 p2");
    require(writeJpeg(imagesDir.path() + QStringLiteral("/weeb-c11-p1.jpg")), "wrote c11 p1");

    QNetworkAccessManager nam;
    MangaVolumeIndex index(indexRoot.path());
    FakeWeebScraper scraper(&nam, chapters, imagesDir.path());
    MangaVolumePacker packer(&scraper, &nam, &index, stagingRoot.path());

    // Diagnostic (non-fatal): surface any failure reason.
    QObject::connect(&packer, &MangaVolumePacker::failed, &app,
        [](const QString& id, const QString& reason) {
            std::cerr << "info: packer failed " << id.toStdString()
                      << ": " << reason.toStdString() << '\n';
        });

    // ── Full success: two chapters -> one ordered, chapter-grouped volume ─────────
    VolumeRecord v2;
    v2.seriesId   = QStringLiteral("weebcentral:berserk");
    v2.number     = QStringLiteral("2");
    v2.id         = volumeId(v2.seriesId, v2.number);
    v2.title      = QStringLiteral("Berserk Vol. 2");
    v2.chapterIds = QStringList{QStringLiteral("wc-chapter-10"), QStringLiteral("wc-chapter-11")};

    // The SERIES title is distinct from the volume title so Fix 3 can prove the
    // provenance stores the series, not the volume.
    const QString seriesTitle = QStringLiteral("Berserk");
    packer.pack(v2, seriesTitle);
    require(waitFor(&packer, &MangaVolumePacker::finished, 30000),
            "pack(v2) emitted finished before timeout");

    const QStringList savedNames = packer.lastSavedNames();
    const QList<int> groups = packer.lastGroups();
    require(savedNames == QStringList{QStringLiteral("c010_001.jpg"),
                                      QStringLiteral("c010_002.jpg"),
                                      QStringLiteral("c011_001.jpg")},
            "chapter then natural page order");
    require(groups == QList<int>{0, 0, 1}, "chapter boundaries retained");

    // Published into the injected index with the canonical ready shape + groups.
    require(packer.complete(v2), "successful pack becomes ready");
    require(index.statusOf(v2.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("ready"),
            "index reports the volume ready");
    QVariantList lp = index.localPages(v2.id);
    require(lp.size() == 3, "three reader pages published");
    require(lp[0].toMap().value(QStringLiteral("group")).toInt() == 0
                && lp[1].toMap().value(QStringLiteral("group")).toInt() == 0
                && lp[2].toMap().value(QStringLiteral("group")).toInt() == 1,
            "localPages groups preserved as {0,0,1}");
    // Anti-stub: the published page is a REAL downloaded JPEG, not a placeholder.
    require(lp[0].toMap().value(QStringLiteral("archive")).toString()
                    .endsWith(QStringLiteral(".cbz"))
                && lp[0].toMap().value(QStringLiteral("url")).toUrl().isEmpty(),
            "WeebCentral packer publishes CBZ-only descriptors");
    require(startsWithJpegMagic(lp[0].toMap()),
            "published page is a real downloaded JPEG");

    // ── Fix 3: provenance stores the SERIES title, not the volume title ───────────
    {
        const QVariantMap st = index.statusOf(v2.id);
        require(st.value(QStringLiteral("seriesTitle")).toString() == seriesTitle,
                "published provenance seriesTitle is the series title passed to pack()");
        require(st.value(QStringLiteral("seriesTitle")).toString() != v2.title,
                "provenance seriesTitle is NOT the volume title");
        require(!st.value(QStringLiteral("releaseTitle")).toString().isEmpty(),
                "the WeebCentral volume carries a concrete non-empty releaseTitle");
    }

    // ── Partial fallback never becomes ready (a page download fails) ──────────────
    VolumeRecord incompleteV2;
    incompleteV2.seriesId   = QStringLiteral("weebcentral:berserk");
    incompleteV2.number     = QStringLiteral("4");
    incompleteV2.id         = volumeId(incompleteV2.seriesId, incompleteV2.number);
    incompleteV2.title      = QStringLiteral("Berserk Vol. 4");
    // chapter 10 downloads fine; chapter 12's page points at a missing file -> fail.
    incompleteV2.chapterIds = QStringList{QStringLiteral("wc-chapter-10"),
                                          QStringLiteral("wc-chapter-12-missing")};

    const QString incStaging = packer.stagingDirFor(incompleteV2);
    packer.pack(incompleteV2, seriesTitle);
    require(waitFor(&packer, &MangaVolumePacker::failed, 30000),
            "pack(incompleteV2) emitted failed before timeout");
    require(!packer.complete(incompleteV2), "partial fallback never becomes ready");
    require(index.statusOf(incompleteV2.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "no ready volume is ever published for the partial");
    require(!QDir(incStaging).exists(), "failed pack removed its staging dir");

    // ── Mid-pack cancel aborts and removes staging, publishes nothing ─────────────
    VolumeRecord cancelVol;
    cancelVol.seriesId   = QStringLiteral("weebcentral:berserk");
    cancelVol.number     = QStringLiteral("5");
    cancelVol.id         = volumeId(cancelVol.seriesId, cancelVol.number);
    cancelVol.title      = QStringLiteral("Berserk Vol. 5");
    cancelVol.chapterIds = QStringList{QStringLiteral("wc-chapter-10"), QStringLiteral("wc-chapter-11")};

    const QString cancelStaging = packer.stagingDirFor(cancelVol);
    packer.pack(cancelVol, seriesTitle);
    require(QDir(cancelStaging).exists(), "staging created for the in-flight pack");
    packer.cancel(cancelVol.id);
    require(!QDir(cancelStaging).exists(), "cancel removed the staging dir");
    pump(400); // drain any aborted replies
    require(index.statusOf(cancelVol.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "cancelled volume is never published");
    require(!packer.complete(cancelVol), "cancelled volume is not ready");

    // ── Fix 2: concurrent packs are SERIALIZED — the second waits for the first ────
    // A (chapter 10 only) and B (chapter 11 only) use DISJOINT chapters so the fake
    // scraper's fetch log distinguishes them. Packing A then B must NOT begin B until
    // A reaches a terminal state: B's chapter is never fetched and B stages no pages
    // while A runs; then A completes with ITS pages and B runs with ITS page.
    {
        VolumeRecord a;
        a.seriesId   = QStringLiteral("weebcentral:berserk");
        a.number     = QStringLiteral("20");
        a.id         = volumeId(a.seriesId, a.number);
        a.title      = QStringLiteral("Berserk Vol. 20");
        a.chapterIds = QStringList{QStringLiteral("wc-chapter-10")};

        VolumeRecord b;
        b.seriesId   = QStringLiteral("weebcentral:berserk");
        b.number     = QStringLiteral("21");
        b.id         = volumeId(b.seriesId, b.number);
        b.title      = QStringLiteral("Berserk Vol. 21");
        b.chapterIds = QStringList{QStringLiteral("wc-chapter-11")};

        scraper.fetched.clear();
        const QString bStaging = packer.stagingDirFor(b);
        packer.pack(a, seriesTitle);   // becomes the active job
        packer.pack(b, seriesTitle);   // must be queued, NOT started

        // Synchronously (before any event-loop pump) B must not have begun.
        require(!scraper.fetched.contains(QStringLiteral("wc-chapter-11")),
                "the queued volume B is never fetched while A is active");
        require(QDir(bStaging).entryList(QDir::Files).isEmpty(),
                "the queued volume B stages no pages while A is active");

        // Drive A to completion (its OWN pages only — no cross-contamination from B).
        require(waitFor(&packer, &MangaVolumePacker::finished, 30000),
                "the active volume A completes");
        require(packer.complete(a), "A becomes ready");
        require(packer.lastSavedNames() == QStringList{QStringLiteral("c010_001.jpg"),
                                                       QStringLiteral("c010_002.jpg")},
                "A published its own two pages, uncontaminated by B");

        // Now B starts automatically and completes with ITS page.
        require(waitFor(&packer, &MangaVolumePacker::finished, 30000),
                "the queued volume B starts and completes once A is terminal");
        require(scraper.fetched.contains(QStringLiteral("wc-chapter-11")),
                "B's chapter is fetched only after A finishes");
        require(packer.complete(b), "B becomes ready");
        require(packer.lastSavedNames() == QStringList{QStringLiteral("c011_001.jpg")},
                "B published its own single page");
    }

    // ── Fix 2: cancelling a QUEUED volume before the active one finishes removes it ─
    {
        VolumeRecord a;
        a.seriesId   = QStringLiteral("weebcentral:berserk");
        a.number     = QStringLiteral("22");
        a.id         = volumeId(a.seriesId, a.number);
        a.title      = QStringLiteral("Berserk Vol. 22");
        a.chapterIds = QStringList{QStringLiteral("wc-chapter-10")};

        VolumeRecord b;
        b.seriesId   = QStringLiteral("weebcentral:berserk");
        b.number     = QStringLiteral("23");
        b.id         = volumeId(b.seriesId, b.number);
        b.title      = QStringLiteral("Berserk Vol. 23");
        b.chapterIds = QStringList{QStringLiteral("wc-chapter-11")};

        scraper.fetched.clear();
        const QString bStaging = packer.stagingDirFor(b);
        packer.pack(a, seriesTitle);   // active
        packer.pack(b, seriesTitle);   // queued
        packer.cancel(b.id);           // cancel the QUEUED job

        require(!QDir(bStaging).exists(), "cancelling a queued volume removes its staging");

        require(waitFor(&packer, &MangaVolumePacker::finished, 30000),
                "the active volume A still completes after the queued cancel");
        require(packer.complete(a), "A becomes ready");
        require(!scraper.fetched.contains(QStringLiteral("wc-chapter-11")),
                "the cancelled queued volume B never runs (its chapter is never fetched)");
        require(!packer.complete(b), "the cancelled queued volume B never becomes ready");
    }

    std::cout << "MANGA_VOLUME_PACKER_OK\n";
    return 0;
}

#include "manga_volume_packer_harness.moc"
