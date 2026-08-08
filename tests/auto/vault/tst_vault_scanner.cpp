// tst_vault_scanner — Slice 4 census + Slice 11 publication seam. Proves
// VaultScanner: the pure census (buildScan) over the fixture tree; the
// generation-guarded candidate DELIVERY (applyResult now emits the card model and
// does NOT publish); the confirm-triggered aggregate PUBLISH (applyPublish) that
// unions every confirmed root in one transactional replace — so adding a second
// root never wipes the first (Hazard-1) and a failed/aborted publish never signals
// "new truth landed" (Hazard-2); plus a light async smoke of the threaded
// scanRoot()/publishConfirmed() wrappers. Heavy correctness is synchronous
// (deterministic); only the thread wrappers are exercised async. GUILESS;
// QTemporaryDir index + identity per run.

#include "engine/VaultScanner.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

#include <memory>

class tst_vault_scanner : public QObject
{
    Q_OBJECT

private:
    static QString mixedRoot()
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/mixed-root");
    }
    // mixed-root shelves 10 dominant-kind files: Berserk (3 comic) + Dune (2
    // book) + The Sopranos (3 video across 2 seasons) + SAMPLE (1 video) +
    // loose (1 comic); 5 kind-pure slices, no leftovers.
    static constexpr int kExpectedItems = 10;
    static constexpr int kExpectedSlices = 5;
    // A SECOND root for the multi-root publish tests: real/ holds one loose book.
    static QString realRoot()
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/real");
    }
    static constexpr int kRealItems = 1; // tiny-book.epub

private slots:
    void build_scan_produces_expected_census();
    void build_scan_precancelled_is_cancelled();
    void apply_result_delivers_candidate_without_publishing();
    void apply_result_cancelled_delivers_cancelled_flag();
    void publish_confirmed_publishes_aggregate();
    void publish_confirmed_multi_root_does_not_wipe_first();
    void publish_confirmed_cancelled_aborts_without_publishing();
    void generation_guard_drops_stale_publish();
    void scan_root_async_delivers_candidate_only();
    void publish_confirmed_async_populates_index();
    void buffered_rescan_runs_after();
};

void tst_vault_scanner::build_scan_produces_expected_census()
{
    const auto r = VaultScanner::buildScan(mixedRoot(), {}, 1, {});
    QVERIFY(!r.cancelled);
    QCOMPARE(r.sliceModel.size(), kExpectedSlices);
    QCOMPARE(r.rows.size(), kExpectedItems);
    QCOMPARE(r.facts.size(), kExpectedItems);

    // A comic row and a season-nested video row are present and shaped right.
    bool sawBerserk = false, sawSeasonedVideo = false;
    for (const VaultIndex::FileRow& row : r.rows) {
        if (row.realName == QStringLiteral("Berserk v01.cbz")) {
            sawBerserk = true;
            QCOMPARE(row.kind, QStringLiteral("comic"));
            QVERIFY(row.subtreePath.endsWith(QStringLiteral("Berserk")));
            QVERIFY(row.subfolder.isEmpty());
        }
        if (row.kind == QStringLiteral("video") && row.subfolder == QStringLiteral("Season 1"))
            sawSeasonedVideo = true;
    }
    QVERIFY(sawBerserk);
    QVERIFY(sawSeasonedVideo);
}

void tst_vault_scanner::build_scan_precancelled_is_cancelled()
{
    auto tok = std::make_shared<VaultKit::CancellationToken>();
    tok->cancel();
    const auto r = VaultScanner::buildScan(mixedRoot(), {}, 1, tok);
    QVERIFY(r.cancelled);
    QVERIFY(r.rows.isEmpty());
}

void tst_vault_scanner::apply_result_delivers_candidate_without_publishing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy finished(&sc, &VaultScanner::scanFinished);
    const quint64 g = sc.nextGeneration();
    const auto r = VaultScanner::buildScan(mixedRoot(), {}, g, {});
    sc.applyResult(r);

    // Delivery emits the candidate card model but does NOT publish (publication is
    // the confirm-triggered aggregate step).
    QCOMPARE(idx.itemCount(), 0);
    QCOMPARE(finished.count(), 1);
    const auto args = finished.takeFirst();
    QCOMPARE(args.at(0).toString(), mixedRoot());
    QCOMPARE(args.at(1).toList().size(), kExpectedSlices); // sliceModel
    QCOMPARE(args.at(2).toBool(), false);                  // not cancelled
}

void tst_vault_scanner::apply_result_cancelled_delivers_cancelled_flag()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy finished(&sc, &VaultScanner::scanFinished);
    const quint64 g = sc.nextGeneration();
    VaultScanner::RawResult r;
    r.root = mixedRoot();
    r.generation = g;
    r.cancelled = true;
    sc.applyResult(r);

    QCOMPARE(idx.itemCount(), 0); // a cancelled census never publishes
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.takeFirst().at(2).toBool(), true); // cancelled flag delivered
}

void tst_vault_scanner::publish_confirmed_publishes_aggregate()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy published(&sc, &VaultScanner::indexPublished);
    const quint64 g = sc.nextGeneration();
    const auto r = VaultScanner::buildScan(mixedRoot(), {}, g, {});
    sc.applyPublish({r}, g);

    QCOMPARE(idx.itemCount(), kExpectedItems);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 4); // Berserk 3 + loose 1
    QCOMPARE(idx.itemCountForKind(QStringLiteral("book")), 2);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("video")), 4); // Sopranos 3 + SAMPLE 1
    QCOMPARE(published.count(), 1);
}

void tst_vault_scanner::publish_confirmed_multi_root_does_not_wipe_first()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    // Confirm root A alone.
    const quint64 g1 = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(mixedRoot(), {}, g1, {})}, g1);
    QCOMPARE(idx.itemCount(), kExpectedItems);

    // Now confirm a SECOND root: the aggregate re-censuses ALL confirmed roots and
    // publishes their union, so A must be preserved and B ADDED — never A wiped.
    const quint64 g2 = sc.nextGeneration();
    const auto a = VaultScanner::buildScan(mixedRoot(), {}, g2, {});
    const auto b = VaultScanner::buildScan(realRoot(), {}, g2, {});
    sc.applyPublish({a, b}, g2);

    QCOMPARE(idx.itemCount(), kExpectedItems + kRealItems);      // 11 — A kept, B added
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 4);  // A's comics still present
    QCOMPARE(idx.itemCountForKind(QStringLiteral("book")), 2 + kRealItems); // mixed 2 + real 1
}

void tst_vault_scanner::publish_confirmed_cancelled_aborts_without_publishing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    // Establish a published truth.
    const quint64 g1 = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(mixedRoot(), {}, g1, {})}, g1);
    QCOMPARE(idx.itemCount(), kExpectedItems);

    // A later aggregate that contains a cancelled census aborts — no partial publish,
    // no indexPublished; the previous truth stands.
    QSignalSpy published(&sc, &VaultScanner::indexPublished);
    const quint64 g2 = sc.nextGeneration();
    VaultScanner::RawResult cancelled;
    cancelled.root = realRoot();
    cancelled.generation = g2;
    cancelled.cancelled = true;
    sc.applyPublish({cancelled}, g2);

    QCOMPARE(idx.itemCount(), kExpectedItems); // previous truth intact
    QCOMPARE(published.count(), 0);            // aborted publish emits nothing
}

void tst_vault_scanner::generation_guard_drops_stale_publish()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    const quint64 g1 = sc.nextGeneration();
    const auto stale = VaultScanner::buildScan(mixedRoot(), {}, g1, {});
    const quint64 g2 = sc.nextGeneration(); // a newer scan/publish supersedes g1

    sc.applyPublish({stale}, g1); // stale generation — dropped
    QCOMPARE(idx.itemCount(), 0);

    const auto fresh = VaultScanner::buildScan(mixedRoot(), {}, g2, {});
    sc.applyPublish({fresh}, g2); // current generation — publishes
    QCOMPARE(idx.itemCount(), kExpectedItems);
}

void tst_vault_scanner::scan_root_async_delivers_candidate_only()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy finished(&sc, &VaultScanner::scanFinished);
    sc.scanRoot(mixedRoot());
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(idx.itemCount(), 0); // an async scan delivers a candidate; it does NOT publish
}

void tst_vault_scanner::publish_confirmed_async_populates_index()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy published(&sc, &VaultScanner::indexPublished);
    sc.publishConfirmed({mixedRoot()});
    QVERIFY(published.wait(10000));
    QCOMPARE(idx.itemCount(), kExpectedItems);
}

void tst_vault_scanner::buffered_rescan_runs_after()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy finished(&sc, &VaultScanner::scanFinished);
    sc.scanRoot(mixedRoot());
    sc.scanRoot(mixedRoot()); // requested during the first — buffers, never dropped
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
}

QTEST_GUILESS_MAIN(tst_vault_scanner)
#include "tst_vault_scanner.moc"
