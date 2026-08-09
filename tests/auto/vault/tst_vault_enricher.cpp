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

#include "engine/VaultEnricher.h"
#include "engine/VaultIndex.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
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

private slots:
    void pick_cover_entry_prefers_cover_then_first();
    void read_comic_facts_from_real_cbz();
    void corrupt_cbz_is_error_not_wedge();
    void duration_cache_hit_miss_and_persist();
    void enrich_writes_comic_facts_to_index();
    // ── vault-admission slice: probe off the owner thread, commit on it ──
    void video_admission_is_persisted_after_owner_thread_commit();
    void rejected_video_verdict_is_not_promoted();
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
}

QTEST_GUILESS_MAIN(tst_vault_enricher)
#include "tst_vault_enricher.moc"
