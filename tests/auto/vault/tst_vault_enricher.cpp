// tst_vault_enricher — Slice 5. Proves VaultEnricher: cover-entry selection and
// page count from a real CBZ (reusing CbzArchive), the corrupt-archive error
// state (never a wedge), the triple-keyed duration cache (hit / miss-on-change /
// persist), and the enrich() orchestration writing comic facts back to the
// index. Comic covers are served by the existing image://comiccover/ provider,
// so no decoding happens here. GUILESS; real fixtures + QTemporaryDir.
//
// Deferred (gradient-fallback until their slices): epub cover ladder + author,
// video thumbnails, page dimensions. The live ffprobe path is exercised at
// Slice 6 (the decodable-MP4 fixture); here only the cache is tested.
//
// Browse-face execution plan Slice 3 (local artwork adoption) added
// VaultEnricher::findLocalArtwork(): a video group's folder is checked for a
// conventionally-named companion image (poster./folder./cover., jpg/jpeg/png)
// and, when found and genuinely decodable, adopted as a namespaced "file://"
// coverRef. Adoption is allow-list only — the real release-site junk image
// "www.YTS.MX.jpg" from the Slice 1 Spider-Man fixture is the guard's proof.

#include "engine/VaultEnricher.h"
#include "engine/ComicCoverId.h"
#include "engine/VaultBookCoverProvider.h"
#include "engine/VaultIndex.h"
#include "third_party/miniz/miniz.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantMap>
#include <QtTest>

class tst_vault_enricher : public QObject
{
    Q_OBJECT

private:
    static QString tinyCbz()
    {
        return QStringLiteral(TANKOBAN_FIXTURES_DIR) + QStringLiteral("/tiny-volume.cbz");
    }
    static QString corruptCbz()
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/corrupt/bad.cbz");
    }
    static QString mediaFixture(const QString& name)
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/media/") + name;
    }

    // The real folder shape from Hemanth's own library (Slice 1 fixture): one film, its
    // Extras/Featurettes, subs, and the release-site junk image `www.YTS.MX.jpg` that a
    // junk-name guard must never mistake for cover art.
    static QString spiderManFolder()
    {
        return QStringLiteral(VAULT_FIXTURES_DIR)
            + QStringLiteral("/browse-film/Spider-Man No Way Home (2021) [1080p] [WEBRip] [5.1] [YTS.MX]");
    }
    static QString spiderManVideo()
    {
        return spiderManFolder()
            + QStringLiteral("/Spider-Man.No.Way.Home.2021.1080p.WEBRip.x264-YTS.MX.mp4");
    }

    static QByteArray coverPng()
    {
        QImage image(64, 96, QImage::Format_ARGB32);
        image.fill(qRgba(180, 40, 80, 255));
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return buffer.data();
    }

    static QByteArray coverJpeg()
    {
        QImage image(64, 96, QImage::Format_RGB32);
        image.fill(qRgb(40, 90, 160));
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "JPG");
        return buffer.data();
    }

    static void writeFile(const QString& path, const QByteArray& bytes)
    {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
        QVERIFY2(f.write(bytes) == bytes.size(), qPrintable(path));
        f.close();
    }

    static bool writeEpub(const QString& path, bool withCover)
    {
        const QByteArray container =
            "<?xml version=\"1.0\"?><container version=\"1.0\" "
            "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
            "<rootfile full-path=\"OPS/content.opf\" "
            "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
        const QByteArray opf = withCover
            ? QByteArray(
                "<?xml version=\"1.0\"?><package xmlns=\"http://www.idpf.org/2007/opf\" "
                "version=\"3.0\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                "<dc:title>Embedded Book Title</dc:title><dc:creator>Embedded Author</dc:creator>"
                "<dc:description>Embedded synopsis from OPF.</dc:description>"
                "</metadata><manifest><item id=\"cover-image\" href=\"images/cover.png\" "
                "media-type=\"image/png\" properties=\"cover-image\"/></manifest></package>")
            : QByteArray(
                "<?xml version=\"1.0\"?><package xmlns=\"http://www.idpf.org/2007/opf\" "
                "version=\"3.0\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                "<dc:title>Coverless Book Title</dc:title><dc:creator>Coverless Author</dc:creator>"
                "</metadata><manifest/></package>");

        mz_zip_archive zip{};
        const QByteArray nativePath = QDir::toNativeSeparators(path).toUtf8();
        if (!mz_zip_writer_init_file(&zip, nativePath.constData(), 0))
            return false;
        bool ok = mz_zip_writer_add_mem(&zip, "mimetype", "application/epub+zip", 20,
                                        MZ_NO_COMPRESSION);
        ok = ok && mz_zip_writer_add_mem(&zip, "META-INF/container.xml",
                                          container.constData(), container.size(), MZ_BEST_SPEED);
        ok = ok && mz_zip_writer_add_mem(&zip, "OPS/content.opf",
                                          opf.constData(), opf.size(), MZ_BEST_SPEED);
        const QByteArray image = coverPng();
        if (withCover)
            ok = ok && mz_zip_writer_add_mem(&zip, "OPS/images/cover.png",
                                             image.constData(), image.size(), MZ_NO_COMPRESSION);
        if (ok)
            ok = mz_zip_writer_finalize_archive(&zip) != 0;
        mz_zip_writer_end(&zip);
        return ok;
    }

private slots:
    void pick_cover_entry_prefers_cover_then_first();
    void read_comic_facts_from_real_cbz();
    void corrupt_cbz_is_error_not_wedge();
    void read_book_facts_and_provider();
    void coverless_epub_is_valid_gradient_fallback();
    void corrupt_epub_is_error_not_wedge();
    void non_epub_book_stays_filename_honest();
    void enrich_writes_epub_metadata_and_republishes_durably();
    void duration_cache_hit_miss_and_persist();
    void enrich_writes_comic_facts_to_index();
    // ── vault-admission slice: probe off the owner thread, commit on it ──
    void video_admission_is_persisted_after_owner_thread_commit();
    void rejected_video_verdict_is_not_promoted();
    void enrich_stale_writeback_is_dropped_after_index_mutation_supersedes_it();
    // ── local artwork adoption slice (browse-face execution plan Slice 3) ──
    void find_local_artwork_prefers_poster_then_folder_then_cover();
    void find_local_artwork_accepts_jpg_and_jpeg_variants();
    void find_local_artwork_refuses_the_real_release_site_junk_image();
    void find_local_artwork_returns_empty_without_any_artwork();
    void find_local_artwork_refuses_corrupt_image();
    void enrich_adopts_local_artwork_for_video_group();
    void enrich_refuses_the_real_release_site_junk_image_for_video_group();
};

void tst_vault_enricher::pick_cover_entry_prefers_cover_then_first()
{
    QCOMPARE(VaultEnricher::pickCoverEntry(
                 {QStringLiteral("003.png"), QStringLiteral("001.png"), QStringLiteral("cover.jpg")}),
             QStringLiteral("cover.jpg"));
    QCOMPARE(VaultEnricher::pickCoverEntry(
                 {QStringLiteral("003.png"), QStringLiteral("001.png"), QStringLiteral("002.png")}),
             QStringLiteral("001.png")); // natural-first when no cover.*/folder.*
}

void tst_vault_enricher::read_comic_facts_from_real_cbz()
{
    const auto f = VaultEnricher::readComicFacts(tinyCbz());
    QVERIFY(f.ok);
    QCOMPARE(f.pages, 3);
    QCOMPARE(f.coverEntry, QStringLiteral("001.png"));
}

void tst_vault_enricher::corrupt_cbz_is_error_not_wedge()
{
    const auto f = VaultEnricher::readComicFacts(corruptCbz());
    QVERIFY(!f.ok); // honest error state, no crash, no hang
    QCOMPARE(f.pages, 0);
    QVERIFY(!f.errorDetail.isEmpty());
}

void tst_vault_enricher::read_book_facts_and_provider()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString epub = tmp.filePath(QStringLiteral("embedded.epub"));
    QVERIFY(writeEpub(epub, true));

    const VaultEnricher::BookFacts facts = VaultEnricher::readBookFacts(epub);
    QVERIFY(facts.ok);
    QCOMPARE(facts.title, QStringLiteral("Embedded Book Title"));
    QCOMPARE(facts.author, QStringLiteral("Embedded Author"));
    QCOMPARE(facts.synopsis, QStringLiteral("Embedded synopsis from OPF."));
    QCOMPARE(facts.coverEntry, QStringLiteral("OPS/images/cover.png"));

    Colosseum::VaultBookCoverProvider provider;
    QSize size;
    const QImage image = provider.requestImage(
        Colosseum::buildComicCoverId(epub, facts.coverEntry), &size, QSize(32, 32));
    QVERIFY(!image.isNull());
    QCOMPARE(size, image.size());
    QVERIFY(image.width() <= 32);
    QVERIFY(image.height() <= 32);
}

void tst_vault_enricher::coverless_epub_is_valid_gradient_fallback()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString epub = tmp.filePath(QStringLiteral("coverless.epub"));
    QVERIFY(writeEpub(epub, false));

    const VaultEnricher::BookFacts facts = VaultEnricher::readBookFacts(epub);
    QVERIFY(facts.ok);
    QCOMPARE(facts.title, QStringLiteral("Coverless Book Title"));
    QVERIFY(facts.coverEntry.isEmpty());
    QVERIFY(facts.errorDetail.isEmpty());
}

void tst_vault_enricher::corrupt_epub_is_error_not_wedge()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString epub = tmp.filePath(QStringLiteral("corrupt.epub"));
    QFile file(epub);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("not a zip") > 0);
    file.close();

    const VaultEnricher::BookFacts facts = VaultEnricher::readBookFacts(epub);
    QVERIFY(!facts.ok);
    QVERIFY(!facts.errorDetail.isEmpty());
}

void tst_vault_enricher::non_epub_book_stays_filename_honest()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString pdf = tmp.filePath(QStringLiteral("filename-honest.pdf"));
    QFile file(pdf);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("not a real pdf") > 0);
    file.close();

    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultEnricher enricher(&index, tmp.path());
    VaultIndex::FileRow row;
    row.id = QStringLiteral("vault:pdf");
    row.rootPath = tmp.path();
    row.subtreePath = tmp.path();
    row.groupKey = tmp.path();
    row.kind = QStringLiteral("book");
    row.path = pdf;
    row.displayTitle = QStringLiteral("filename-honest");
    row.realName = QStringLiteral("filename-honest.pdf");
    QVERIFY(index.publish({row}));
    enricher.enrich({row});

    const auto rows = index.rowsForKind(QStringLiteral("book"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().displayTitle, QStringLiteral("filename-honest"));
    QCOMPARE(rows.first().format, QStringLiteral("pdf"));
    QVERIFY(rows.first().metadataSource.isEmpty());
    QVERIFY(rows.first().errorState.isEmpty());
}

void tst_vault_enricher::enrich_writes_epub_metadata_and_republishes_durably()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString epub = tmp.filePath(QStringLiteral("metadata.epub"));
    QVERIFY(writeEpub(epub, true));

    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultEnricher enricher(&index, tmp.path());
    VaultIndex::FileRow source;
    source.id = QStringLiteral("vault:epub");
    source.rootPath = tmp.path();
    source.subtreePath = tmp.path();
    source.groupKey = tmp.path();
    source.groupTitle = QStringLiteral("Books");
    source.kind = QStringLiteral("book");
    source.path = epub;
    source.displayTitle = QStringLiteral("metadata");
    source.realName = QStringLiteral("metadata.epub");
    source.size = QFileInfo(epub).size();
    source.mtimeMs = 77;
    QVERIFY(index.publish({source}));

    enricher.enrich({source});
    const auto enriched = index.rowsForKind(QStringLiteral("book"));
    QCOMPARE(enriched.size(), 1);
    QCOMPARE(enriched.first().displayTitle, QStringLiteral("Embedded Book Title"));
    QCOMPARE(enriched.first().author, QStringLiteral("Embedded Author"));
    QCOMPARE(enriched.first().synopsis, QStringLiteral("Embedded synopsis from OPF."));
    QCOMPARE(enriched.first().coverRef, QStringLiteral("OPS/images/cover.png"));
    QCOMPARE(enriched.first().metadataSource, QStringLiteral("EPUB"));
    QCOMPARE(enriched.first().format, QStringLiteral("epub"));

    VaultIndex::FileRow rescan = source; // scanner rows do not carry enrichment facts
    rescan.format.clear();
    QVERIFY(index.publish({rescan}));
    const auto durable = index.rowsForKind(QStringLiteral("book"));
    QCOMPARE(durable.size(), 1);
    QCOMPARE(durable.first().displayTitle, QStringLiteral("Embedded Book Title"));
    QCOMPARE(durable.first().synopsis, QStringLiteral("Embedded synopsis from OPF."));
    QCOMPARE(durable.first().metadataSource, QStringLiteral("EPUB"));
}

void tst_vault_enricher::duration_cache_hit_miss_and_persist()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultEnricher e(&idx, tmp.path());

    e.putDuration(QStringLiteral("D:/v.mkv"), 100, 5, 42.5);
    QCOMPARE(e.cachedDuration(QStringLiteral("D:/v.mkv"), 100, 5), 42.5);
    QCOMPARE(e.cachedDuration(QStringLiteral("D:/v.mkv"), 100, 6), -1.0); // triple changed -> miss
    e.saveDurationCache();

    VaultEnricher reloaded(&idx, tmp.path()); // reads durations.json
    QCOMPARE(reloaded.cachedDuration(QStringLiteral("D:/v.mkv"), 100, 5), 42.5); // persisted
}

void tst_vault_enricher::enrich_writes_comic_facts_to_index()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultEnricher e(&idx, tmp.path());

    VaultIndex::FileRow r;
    r.id = QStringLiteral("vault:tiny");
    r.rootPath = QStringLiteral("D:/lib");
    r.subtreePath = QStringLiteral("D:/lib/Tiny");
    r.groupKey = QStringLiteral("D:/lib/Tiny");
    r.groupTitle = QStringLiteral("Tiny");
    r.kind = QStringLiteral("comic");
    r.path = tinyCbz();
    r.realName = QStringLiteral("tiny-volume.cbz");
    r.pages = -1; // unenriched
    QVERIFY(idx.publish({r}));

    e.enrich({r});

    const QVariantList files = idx.filesInSubtree(QStringLiteral("D:/lib/Tiny"));
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.first().toMap().value(QStringLiteral("pages")).toInt(), 3);
    QCOMPARE(files.first().toMap().value(QStringLiteral("coverRef")).toString(),
             QStringLiteral("001.png"));
    QVERIFY(files.first().toMap().value(QStringLiteral("errorState")).toString().isEmpty());
}

void tst_vault_enricher::video_admission_is_persisted_after_owner_thread_commit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());
    VaultEnricher enricher(&idx, tmp.path());
    QSignalSpy finished(&enricher, &VaultEnricher::enrichmentFinished);

    QThread* changedThread = nullptr;
    QObject::connect(
        &idx, &VaultIndex::changed, &idx,
        [&changedThread]() { changedThread = QThread::currentThread(); },
        Qt::DirectConnection);

    VaultIndex::FileRow row;
    row.id = QStringLiteral("vault:tiny-video");
    row.kind = QStringLiteral("video");
    row.path = mediaFixture(QStringLiteral("tiny.mp4"));
    row.rootPath = tmp.path();
    row.subtreePath = tmp.path();
    row.groupKey = tmp.path();
    row.groupTitle = QStringLiteral("Tiny");
    row.realName = QStringLiteral("tiny.mp4");
    row.size = 1;
    row.mtimeMs = 1;
    row.durationSec = 1.0; // keep this test on admission, not ffprobe duration

    QThread worker;
    QObject gate;
    gate.moveToThread(&worker);
    worker.start();

    QMetaObject::invokeMethod(
        &gate,
        [&enricher, row]() { enricher.enrich({row}); },
        Qt::QueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
    worker.quit();
    QVERIFY(worker.wait(5000));

    const auto rows = idx.rowsForKind(QStringLiteral("video"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().admissionVerdict, QStringLiteral("Admitted"));
    QCOMPARE(changedThread, QThread::currentThread()); // committed on the owner thread, not the worker
}

void tst_vault_enricher::rejected_video_verdict_is_not_promoted()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());
    VaultEnricher enricher(&idx, tmp.path());

    VaultIndex::FileRow row;
    row.id = QStringLiteral("vault:not-video");
    row.kind = QStringLiteral("video");
    row.path = mediaFixture(QStringLiteral("not-a-video.mp4"));
    row.rootPath = tmp.path();
    row.subtreePath = tmp.path();
    row.groupKey = tmp.path();
    row.groupTitle = QStringLiteral("Bad");
    row.realName = QStringLiteral("not-a-video.mp4");
    row.size = 1;
    row.mtimeMs = 1;
    row.durationSec = 1.0;

    enricher.enrich({row}); // same-thread path commits directly

    const auto rows = idx.rowsForKind(QStringLiteral("video"));
    QCOMPARE(rows.size(), 1);
    QVERIFY(!rows.first().admissionVerdict.isEmpty());
    QVERIFY(rows.first().admissionVerdict != QStringLiteral("Admitted"));
    QCOMPARE(rows.first().errorState, QStringLiteral("rejected"));
}

void tst_vault_enricher::enrich_stale_writeback_is_dropped_after_index_mutation_supersedes_it()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());
    VaultEnricher enricher(&idx, tmp.path());
    QSignalSpy finished(&enricher, &VaultEnricher::enrichmentFinished);

    VaultIndex::FileRow row;
    row.id = QStringLiteral("vault:stale-tiny");
    row.rootPath = tmp.path();
    row.subtreePath = tmp.path();
    row.groupKey = tmp.path();
    row.groupTitle = QStringLiteral("Tiny");
    row.kind = QStringLiteral("comic");
    row.path = tinyCbz();
    row.realName = QStringLiteral("tiny-volume.cbz");
    row.pages = -1; // unenriched
    QVERIFY(idx.publish({row}));
    QCOMPARE(idx.itemCount(), 1);

    // Simulate a competing authoritative mutation (a scanner reconcile/watcher publish) landing
    // WHILE this enrich() pass is still in flight on a worker thread: the row is deleted from
    // the index entirely before the enrich job's write-back arrives — the exact stale-async-write
    // shape d4eaaba fixed in main.cpp's inline cover enrichment, now exercised against
    // VaultEnricher::enrich()/commitRowsOnIndexThread() directly. The reconcile runs on the
    // index's OWNER thread (never called cross-thread) via a Qt::QueuedConnection hop off
    // VaultEnricher::progress — both that hop and the eventual commit hop are posted, in that
    // order, from the SAME worker thread onto the SAME main-thread queue, so the mutation is
    // guaranteed to land before the write-back is evaluated.
    int reconciledRemoved = -1;
    QObject::connect(
        &enricher, &VaultEnricher::progress, &idx,
        [&idx, &reconciledRemoved, root = tmp.path()](int done, int total) {
            Q_UNUSED(total);
            if (done != 1)
                return;
            int removed = 0;
            idx.reconcileRoot(root, QSet<QString>(), {}, &removed);
            reconciledRemoved = removed;
        },
        Qt::QueuedConnection);

    QThread worker;
    QObject gate;
    gate.moveToThread(&worker);
    worker.start();

    QMetaObject::invokeMethod(
        &gate,
        [&enricher, row]() { enricher.enrich({row}); },
        Qt::QueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
    worker.quit();
    QVERIFY(worker.wait(5000));

    QCOMPARE(reconciledRemoved, 1); // the mutation genuinely landed before the assertion below
    // The stale write-back must NOT resurrect the row the index has since moved past.
    QCOMPARE(idx.itemCount(), 0);
    QCOMPARE(idx.rowsForKind(QStringLiteral("comic")).size(), 0);
}

// ── local artwork adoption slice (browse-face execution plan Slice 3) ──

void tst_vault_enricher::find_local_artwork_prefers_poster_then_folder_then_cover()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString coverPath = tmp.filePath(QStringLiteral("cover.png"));
    const QString folderPath = tmp.filePath(QStringLiteral("folder.png"));
    const QString posterPath = tmp.filePath(QStringLiteral("poster.png"));

    // Only cover.png present -> it is adopted, exact ref value asserted (never merely
    // "non-empty" — a loose non-empty check is exactly how a prior fixture went silently
    // vacuous, per Slice 2's finding).
    writeFile(coverPath, coverPng());
    QCOMPARE(VaultEnricher::findLocalArtwork(tmp.path()),
             QUrl::fromLocalFile(QFileInfo(coverPath).absoluteFilePath()).toString());

    // folder.png joins -> folder outranks cover.
    writeFile(folderPath, coverPng());
    QCOMPARE(VaultEnricher::findLocalArtwork(tmp.path()),
             QUrl::fromLocalFile(QFileInfo(folderPath).absoluteFilePath()).toString());

    // poster.png joins -> poster outranks both.
    writeFile(posterPath, coverPng());
    QCOMPARE(VaultEnricher::findLocalArtwork(tmp.path()),
             QUrl::fromLocalFile(QFileInfo(posterPath).absoluteFilePath()).toString());
}

void tst_vault_enricher::find_local_artwork_accepts_jpg_and_jpeg_variants()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString posterJpg = tmp.filePath(QStringLiteral("poster.jpg"));
    writeFile(posterJpg, coverJpeg());
    QCOMPARE(VaultEnricher::findLocalArtwork(tmp.path()),
             QUrl::fromLocalFile(QFileInfo(posterJpg).absoluteFilePath()).toString());

    QTemporaryDir tmp2;
    QVERIFY(tmp2.isValid());
    const QString posterJpeg = tmp2.filePath(QStringLiteral("poster.jpeg"));
    writeFile(posterJpeg, coverJpeg());
    QCOMPARE(VaultEnricher::findLocalArtwork(tmp2.path()),
             QUrl::fromLocalFile(QFileInfo(posterJpeg).absoluteFilePath()).toString());

    // Case-insensitive basename+extension match (a real download often ships "Poster.JPG").
    QTemporaryDir tmp3;
    QVERIFY(tmp3.isValid());
    const QString posterUpper = tmp3.filePath(QStringLiteral("Poster.JPG"));
    writeFile(posterUpper, coverJpeg());
    QCOMPARE(VaultEnricher::findLocalArtwork(tmp3.path()),
             QUrl::fromLocalFile(QFileInfo(posterUpper).absoluteFilePath()).toString());
}

void tst_vault_enricher::find_local_artwork_refuses_the_real_release_site_junk_image()
{
    // The real fixture shape (Slice 1): the ONLY image in the Spider-Man folder is the
    // release-site junk "www.YTS.MX.jpg" — not a conventional name, so it must never be
    // mistaken for cover art. Adoption is by allow-list convention, never "any jpg present".
    QVERIFY(QFileInfo::exists(spiderManFolder() + QStringLiteral("/www.YTS.MX.jpg")));
    QVERIFY(VaultEnricher::findLocalArtwork(spiderManFolder()).isEmpty());
}

void tst_vault_enricher::find_local_artwork_returns_empty_without_any_artwork()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    writeFile(tmp.filePath(QStringLiteral("movie.mp4")), QByteArrayLiteral("not-a-real-video"));
    QVERIFY(VaultEnricher::findLocalArtwork(tmp.path()).isEmpty());
}

void tst_vault_enricher::find_local_artwork_refuses_corrupt_image()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // Conventionally named, but truncated/garbage bytes — must be refused, not adopted, and
    // must return promptly (QImageReader::canRead() is a bounded header sniff, never a full
    // decode attempt on hostile input).
    writeFile(tmp.filePath(QStringLiteral("poster.jpg")), QByteArrayLiteral("not a real image"));
    QVERIFY(VaultEnricher::findLocalArtwork(tmp.path()).isEmpty());
}

void tst_vault_enricher::enrich_adopts_local_artwork_for_video_group()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString video = tmp.filePath(QStringLiteral("movie.mp4"));
    QVERIFY(QFile::copy(mediaFixture(QStringLiteral("tiny.mp4")), video));
    const QString poster = tmp.filePath(QStringLiteral("poster.png"));
    writeFile(poster, coverPng());

    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultEnricher enricher(&idx, tmp.path());

    VaultIndex::FileRow row;
    row.id = QStringLiteral("vault:artwork-video");
    row.kind = QStringLiteral("video");
    row.path = video;
    row.rootPath = tmp.path();
    row.subtreePath = tmp.path(); // the group's own folder — one file, one group
    row.groupKey = tmp.path();
    row.groupTitle = QStringLiteral("Movie");
    row.realName = QStringLiteral("movie.mp4");
    row.size = QFileInfo(video).size();
    row.mtimeMs = 1;
    QVERIFY(idx.publish({row}));

    enricher.enrich({row});

    const auto rows = idx.rowsForKind(QStringLiteral("video"));
    QCOMPARE(rows.size(), 1);
    // Exact value, not merely non-empty: a namespaced file:// ref, distinct from the bare
    // comic/book in-archive entry names coverRef otherwise carries.
    QCOMPARE(rows.first().coverRef,
             QUrl::fromLocalFile(QFileInfo(poster).absoluteFilePath()).toString());
    QVERIFY(rows.first().coverRef.startsWith(QStringLiteral("file://")));
}

void tst_vault_enricher::enrich_refuses_the_real_release_site_junk_image_for_video_group()
{
    VaultIndex::FileRow row;
    row.id = QStringLiteral("vault:spiderman-video");
    row.kind = QStringLiteral("video");
    row.path = spiderManVideo();
    row.rootPath = QFileInfo(spiderManFolder()).absolutePath();
    row.subtreePath = spiderManFolder(); // the real group folder holding www.YTS.MX.jpg
    row.groupKey = spiderManFolder();
    row.groupTitle = QStringLiteral("Spider-Man No Way Home");
    row.realName = QFileInfo(spiderManVideo()).fileName();
    row.size = QFileInfo(spiderManVideo()).size();
    row.mtimeMs = 1;

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultEnricher enricher(&idx, tmp.path());
    QVERIFY(idx.publish({row}));

    enricher.enrich({row}); // the stub .mp4 correctly fails admission; coverRef is this test's point

    const auto rows = idx.rowsForKind(QStringLiteral("video"));
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.first().coverRef.isEmpty()); // www.YTS.MX.jpg must never be adopted
}

QTEST_GUILESS_MAIN(tst_vault_enricher)
#include "tst_vault_enricher.moc"
