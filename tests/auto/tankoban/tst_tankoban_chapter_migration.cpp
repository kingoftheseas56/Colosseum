// tst_tankoban_chapter_migration — Catalogue-independence Slice 5. Proves
// TankobanChapterMigration::run(): the WC-era chapter tree (<appdata>/manga/) is
// deleted outright while manga-volumes/ is left untouched; kind:"manga" progress
// records are purged while kind:"tankoban"/"comic" survive; a second run is a
// true no-op (idempotent); and the marker file lands only on a successful run.
// GUILESS. QTemporaryDir per run for both the disk fixture and the ProgressStore's
// backing ini (never a real AppData root, never the registry).

#include "engine/TankobanChapterMigration.h"
#include "ProgressStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

namespace {

// Builds <root>/manga/<series>/<chapter>/page_000.jpg plus <root>/manga/index.json —
// the exact on-disk shape MangaDownloader.h documents.
void seedChapterTree(const QString &root)
{
    const QString chapterDir = root + QStringLiteral("/manga/berserk-1/ch-1");
    QVERIFY2(QDir().mkpath(chapterDir), "mkpath chapter dir");
    QFile page(chapterDir + QStringLiteral("/page_000.jpg"));
    QVERIFY2(page.open(QIODevice::WriteOnly), "open fixture page");
    page.write("fake-jpg-bytes");
    page.close();

    QFile index(root + QStringLiteral("/manga/index.json"));
    QVERIFY2(index.open(QIODevice::WriteOnly), "open fixture index.json");
    index.write("{}");
    index.close();
}

// Builds <root>/manga-volumes/<series>/<vol>.cbz — a volume archive that must survive
// the migration untouched (different subsystem, MangaVolumeIndex's own layout).
void seedVolumeArchive(const QString &root)
{
    const QString volDir = root + QStringLiteral("/manga-volumes/berserk-1");
    QVERIFY2(QDir().mkpath(volDir), "mkpath manga-volumes dir");
    QFile vol(volDir + QStringLiteral("/1.cbz"));
    QVERIFY2(vol.open(QIODevice::WriteOnly), "open fixture volume archive");
    vol.write("fake-cbz-bytes");
    vol.close();
}

void seedProgressRecords(ProgressStore &store)
{
    QVariantMap manga;
    manga.insert(QStringLiteral("id"), QStringLiteral("berserk-1"));
    manga.insert(QStringLiteral("kind"), QStringLiteral("manga"));
    manga.insert(QStringLiteral("title"), QStringLiteral("Berserk"));
    manga.insert(QStringLiteral("progress"), 0.4);
    store.record(manga);

    QVariantMap tankoban;
    tankoban.insert(QStringLiteral("id"), QStringLiteral("mal:2"));
    tankoban.insert(QStringLiteral("kind"), QStringLiteral("tankoban"));
    tankoban.insert(QStringLiteral("title"), QStringLiteral("Berserk"));
    tankoban.insert(QStringLiteral("progress"), 0.6);
    store.record(tankoban);

    QVariantMap comic;
    comic.insert(QStringLiteral("id"), QStringLiteral("locg:123"));
    comic.insert(QStringLiteral("kind"), QStringLiteral("comic"));
    comic.insert(QStringLiteral("title"), QStringLiteral("Some Comic"));
    comic.insert(QStringLiteral("progress"), 0.2);
    store.record(comic);
}

} // namespace

class tst_tankoban_chapter_migration : public QObject
{
    Q_OBJECT

private slots:
    void disk_purge_deletes_chapter_tree_keeps_volumes();
    void progress_purge_removes_manga_keeps_tankoban_and_comic();
    void marker_written_after_success();
    void idempotent_second_run_is_noop();
    void missing_manga_dir_still_purges_progress_and_writes_marker();
    void null_progress_store_still_purges_disk();
    void sealed_store_purge_deferred_until_durable_rebind();
};

void tst_tankoban_chapter_migration::disk_purge_deletes_chapter_tree_keeps_volumes()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    seedChapterTree(tmp.path());
    seedVolumeArchive(tmp.path());
    QVERIFY(QDir(tmp.path() + QStringLiteral("/manga")).exists());
    QVERIFY(QDir(tmp.path() + QStringLiteral("/manga-volumes")).exists());

    const auto result = TankobanChapterMigration::run(tmp.path(), nullptr);

    QVERIFY(result.ran);
    QVERIFY(result.mangaDirExisted);
    QVERIFY(result.mangaDirDeleted);
    QCOMPARE(result.chapterDirsDeleted, 1);
    QVERIFY(result.indexDeleted);
    QVERIFY(!QDir(tmp.path() + QStringLiteral("/manga")).exists());
    // manga-volumes/ — a DIFFERENT subsystem's storage — must survive untouched.
    QVERIFY(QDir(tmp.path() + QStringLiteral("/manga-volumes")).exists());
    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/manga-volumes/berserk-1/1.cbz")));
}

void tst_tankoban_chapter_migration::progress_purge_removes_manga_keeps_tankoban_and_comic()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    seedChapterTree(tmp.path());
    const QString iniPath = tmp.path() + QStringLiteral("/progress-store.ini");
    ProgressStore store(iniPath);
    seedProgressRecords(store);
    QVERIFY(!store.get(QStringLiteral("manga"), QStringLiteral("berserk-1")).isEmpty());
    QVERIFY(!store.get(QStringLiteral("tankoban"), QStringLiteral("mal:2")).isEmpty());
    QVERIFY(!store.get(QStringLiteral("comic"), QStringLiteral("locg:123")).isEmpty());

    const auto result = TankobanChapterMigration::run(tmp.path(), &store);

    QCOMPARE(result.progressRecordsPurged, 1);
    QVERIFY(store.get(QStringLiteral("manga"), QStringLiteral("berserk-1")).isEmpty());
    QVERIFY(!store.get(QStringLiteral("tankoban"), QStringLiteral("mal:2")).isEmpty());
    QVERIFY(!store.get(QStringLiteral("comic"), QStringLiteral("locg:123")).isEmpty());
}

void tst_tankoban_chapter_migration::marker_written_after_success()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString marker = tmp.path() + QStringLiteral("/tankoban-chapter-migration.v1.done");
    QVERIFY(!QFile::exists(marker));

    TankobanChapterMigration::run(tmp.path(), nullptr);

    QVERIFY2(QFile::exists(marker), "marker file must land after a successful run");
}

void tst_tankoban_chapter_migration::idempotent_second_run_is_noop()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    seedChapterTree(tmp.path());
    const QString iniPath = tmp.path() + QStringLiteral("/progress-store.ini");
    ProgressStore store(iniPath);
    seedProgressRecords(store);

    const auto first = TankobanChapterMigration::run(tmp.path(), &store);
    QVERIFY(first.ran);
    QCOMPARE(first.progressRecordsPurged, 1);

    // Prove the second call genuinely does nothing: reseed a fresh manga-kind record
    // and a fresh chapter dir AFTER the marker exists — a true no-op must leave both
    // alone (only the FIRST successful run is allowed to touch anything).
    seedChapterTree(tmp.path());
    QVariantMap laterManga;
    laterManga.insert(QStringLiteral("id"), QStringLiteral("one-piece-1"));
    laterManga.insert(QStringLiteral("kind"), QStringLiteral("manga"));
    laterManga.insert(QStringLiteral("title"), QStringLiteral("One Piece"));
    laterManga.insert(QStringLiteral("progress"), 0.1);
    store.record(laterManga);

    const auto second = TankobanChapterMigration::run(tmp.path(), &store);

    QVERIFY2(!second.ran, "a marker already on disk must make the second run a no-op");
    QCOMPARE(second.progressRecordsPurged, 0);
    QVERIFY2(QDir(tmp.path() + QStringLiteral("/manga")).exists(),
             "the no-op run must not delete a tree reseeded after the marker landed");
    QVERIFY2(!store.get(QStringLiteral("manga"), QStringLiteral("one-piece-1")).isEmpty(),
             "the no-op run must not purge a record added after the marker landed");
}

void tst_tankoban_chapter_migration::missing_manga_dir_still_purges_progress_and_writes_marker()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // No seedChapterTree() call — manga/ never existed (a fresh install, or a
    // series-less profile). The migration must still succeed cleanly.
    const QString iniPath = tmp.path() + QStringLiteral("/progress-store.ini");
    ProgressStore store(iniPath);
    seedProgressRecords(store);

    const auto result = TankobanChapterMigration::run(tmp.path(), &store);

    QVERIFY(result.ran);
    QVERIFY(!result.mangaDirExisted);
    QCOMPARE(result.chapterDirsDeleted, 0);
    QCOMPARE(result.progressRecordsPurged, 1);
    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/tankoban-chapter-migration.v1.done")));
}

void tst_tankoban_chapter_migration::null_progress_store_still_purges_disk()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    seedChapterTree(tmp.path());

    const auto result = TankobanChapterMigration::run(tmp.path(), nullptr);

    QVERIFY(result.ran);
    QVERIFY(result.mangaDirDeleted);
    QCOMPARE(result.progressRecordsPurged, 0);   // no store handed in — nothing to purge, no crash
    QVERIFY(!QDir(tmp.path() + QStringLiteral("/manga")).exists());
}

// Reproduces the closing-sweep's ground-truthed gap (2026-08-21): boot always purges
// against ProfileStoreRuntime's Sealed placeholder first (a throwaway store, never the
// one QML's `Progress` ends up bound to), and only later does the user's onboarding
// choice ("continue local") rebind to the real, durable store. A migration that purges
// once against whatever is bound at boot and burns its marker there never reaches the
// real store's manga-kind records. This fixture mirrors that exact shape: a `sealedStore`
// (the ephemeral pre-onboarding placeholder) and a SEPARATE `realStore` (the durable
// store the app rebinds Progress to post-onboarding), both seeded independently, run()
// called first against the sealed store (progressStoreIsDurable=false, as main.cpp does
// at boot) and then against the real store (progressStoreIsDurable=true, as main.cpp's
// storesChanged-triggered retry does after the rebind).
void tst_tankoban_chapter_migration::sealed_store_purge_deferred_until_durable_rebind()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    seedChapterTree(tmp.path());
    const QString marker = tmp.path() + QStringLiteral("/tankoban-chapter-migration.v1.done");

    // The Sealed placeholder: its own ini, seeded the same shape a stray manga record
    // could take there, but this store is NEVER the one that persists to disk long-term.
    QTemporaryDir sealedDir;
    QVERIFY(sealedDir.isValid());
    ProgressStore sealedStore(sealedDir.path() + QStringLiteral("/progress.ini"));
    seedProgressRecords(sealedStore);

    // The real, durable store — the one the app's "continue local" rebind swaps
    // `Progress` to. This is where a real user's pre-existing manga-kind chapter
    // progress actually lives on disk.
    const QString realIniPath = tmp.path() + QStringLiteral("/progress-store.ini");
    ProgressStore realStore(realIniPath);
    seedProgressRecords(realStore);
    QVERIFY(!realStore.get(QStringLiteral("manga"), QStringLiteral("berserk-1")).isEmpty());

    // Boot-time call: bound to the Sealed placeholder. Disk purge still runs (it has no
    // ProgressStore dependency), but the progress purge and the marker must both be
    // withheld — burning the marker here is exactly the defect: it would permanently
    // skip the real store below.
    const auto sealedPass = TankobanChapterMigration::run(tmp.path(), &sealedStore,
                                                            /*progressStoreIsDurable=*/false);
    QVERIFY2(sealedPass.mangaDirDeleted, "the disk-side purge has no ProgressStore dependency "
             "and must still run while sealed");
    QVERIFY2(!QFile::exists(marker), "the marker must NOT land while only the Sealed placeholder "
             "was purged -- burning it here is the defect this case reproduces");
    QCOMPARE(sealedPass.progressRecordsPurged, 0);
    QVERIFY2(!sealedStore.get(QStringLiteral("manga"), QStringLiteral("berserk-1")).isEmpty(),
             "the sealed placeholder's own record is left alone (never the target)");
    QVERIFY2(!realStore.get(QStringLiteral("manga"), QStringLiteral("berserk-1")).isEmpty(),
             "the real store must be completely untouched by the sealed-pass call");

    // The rebind retry: bound to the real, durable store (main.cpp's storesChanged
    // handler). This must be the pass that actually purges and writes the marker.
    const auto realPass = TankobanChapterMigration::run(tmp.path(), &realStore,
                                                          /*progressStoreIsDurable=*/true);
    QVERIFY2(QFile::exists(marker), "the marker must land once the REAL store has been purged");
    QCOMPARE(realPass.progressRecordsPurged, 1);
    QVERIFY2(realStore.get(QStringLiteral("manga"), QStringLiteral("berserk-1")).isEmpty(),
             "the real store's manga-kind record must be gone after the durable pass");
    QVERIFY2(!realStore.get(QStringLiteral("tankoban"), QStringLiteral("mal:2")).isEmpty(),
             "tankoban-kind records on the real store survive");
    QVERIFY2(!realStore.get(QStringLiteral("comic"), QStringLiteral("locg:123")).isEmpty(),
             "comic-kind records on the real store survive");

    // A third call, now that the marker exists, is the ordinary idempotent no-op.
    const auto thirdPass = TankobanChapterMigration::run(tmp.path(), &realStore,
                                                           /*progressStoreIsDurable=*/true);
    QVERIFY2(!thirdPass.ran, "once the real purge has landed, later calls are true no-ops");
}

QTEST_GUILESS_MAIN(tst_tankoban_chapter_migration)
#include "tst_tankoban_chapter_migration.moc"
