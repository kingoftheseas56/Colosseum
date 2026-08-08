// tst_vault_scanner — Slice 4. Proves VaultScanner: the pure census (buildScan)
// over the fixture tree, the generation-guarded commit (applyResult) including
// the stale-drop and cancelled-no-publish paths, and — via a light async smoke —
// the threaded scanRoot() and the buffered-rescan-runs-after guarantee. The
// heavy correctness is synchronous (deterministic); only the thread wrapper is
// exercised async. GUILESS; QTemporaryDir index + identity per run.

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

private slots:
    void build_scan_produces_expected_census();
    void build_scan_precancelled_is_cancelled();
    void apply_result_publishes_index();
    void apply_result_cancelled_does_not_publish();
    void generation_guard_drops_stale_result();
    void scan_root_async_populates_index();
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

void tst_vault_scanner::apply_result_publishes_index()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    const quint64 g = sc.nextGeneration();
    const auto r = VaultScanner::buildScan(mixedRoot(), {}, g, {});
    sc.applyResult(r);
    QCOMPARE(idx.itemCount(), kExpectedItems);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 4); // Berserk 3 + loose 1
    QCOMPARE(idx.itemCountForKind(QStringLiteral("book")), 2);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("video")), 4); // Sopranos 3 + SAMPLE 1
}

void tst_vault_scanner::apply_result_cancelled_does_not_publish()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    const quint64 g = sc.nextGeneration();
    VaultScanner::RawResult r;
    r.root = mixedRoot();
    r.generation = g;
    r.cancelled = true;
    sc.applyResult(r);
    QCOMPARE(idx.itemCount(), 0); // a cancelled census never publishes
}

void tst_vault_scanner::generation_guard_drops_stale_result()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    const quint64 g1 = sc.nextGeneration();
    const auto stale = VaultScanner::buildScan(mixedRoot(), {}, g1, {});
    const quint64 g2 = sc.nextGeneration(); // a newer scan supersedes g1

    sc.applyResult(stale);       // stale generation — must be dropped
    QCOMPARE(idx.itemCount(), 0);

    const auto fresh = VaultScanner::buildScan(mixedRoot(), {}, g2, {});
    sc.applyResult(fresh);       // current generation — publishes
    QCOMPARE(idx.itemCount(), kExpectedItems);
}

void tst_vault_scanner::scan_root_async_populates_index()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    QSignalSpy published(&sc, &VaultScanner::indexPublished);
    sc.scanRoot(mixedRoot());
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
