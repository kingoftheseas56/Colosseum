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
#include "engine/VaultWatcher.h"
#include "engine/VaultConfig.h"

#include <QDir>
#include <QFile>
#include <QMap>
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
    // A mixed LEAF (Akira: 2 comic + 1 book leftover) for the chip-reassignment tests.
    static QString mixedLeafRoot()
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/mixed-leaf");
    }
    // Mirror VaultConfig::norm so a test override keys the same way buildScan looks it up.
    static QString normForTest(const QString& p)
    {
        QString n = QDir::cleanPath(p);
#ifdef Q_OS_WIN
        n = n.toLower();
#endif
        return n;
    }
    static QString subtreeEndingWith(const QVariantList& slices, const QString& suffix)
    {
        for (const QVariant& v : slices) {
            const QString s = v.toMap().value(QStringLiteral("subtreePath")).toString();
            if (s.endsWith(suffix))
                return s;
        }
        return QString();
    }

private slots:
    void build_scan_produces_expected_census();
    void build_scan_enriches_slice_model();
    void override_reassigns_mixed_leaf_to_present_kind();
    void override_relabels_folder_to_absent_kind();
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
    // ── Slice 15: the live-shelf watcher (VaultWatcher) ──
    void watcher_touch_files_upserts_exact_arrival_set();
    void watcher_new_kind_raises_card_and_lands();
    void watcher_override_law_raises_card_for_known_kind_arrival();
    void watcher_degraded_flag_on_failed_watch_and_recovers();
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

void tst_vault_scanner::build_scan_enriches_slice_model()
{
    // Thread B: every card slice carries the enrichment fields, and a season-nested
    // show reports its distinct 2nd-level subgroups as the "· N series" count.
    const auto r = VaultScanner::buildScan(mixedRoot(), {}, 1, {});
    QCOMPARE(r.sliceModel.size(), kExpectedSlices);
    for (const QVariant& v : r.sliceModel) {
        const QVariantMap m = v.toMap();
        QVERIFY(m.contains(QStringLiteral("seriesCount")));
        QVERIFY(m.contains(QStringLiteral("sample")));
        QVERIFY(m.contains(QStringLiteral("sizeBytes")));
    }
    bool sawSopranos = false;
    for (const QVariant& v : r.sliceModel) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("subtreePath")).toString().endsWith(QStringLiteral("The Sopranos"))) {
            sawSopranos = true;
            QCOMPARE(m.value(QStringLiteral("seriesCount")).toInt(), 2); // Season 1 + Season 2
            QVERIFY(!m.value(QStringLiteral("sample")).toString().isEmpty());
        }
    }
    QVERIFY(sawSopranos);
}

void tst_vault_scanner::override_reassigns_mixed_leaf_to_present_kind()
{
    // Thread A, primary case: the mixed leaf Akira (2 comic + 1 book) reassigned to books
    // → the book shelves, the two comics fall to the leftover line.
    const auto plain = VaultScanner::buildScan(mixedLeafRoot(), {}, 1, {});
    QCOMPARE(plain.sliceModel.size(), 1);
    QCOMPARE(plain.sliceModel.first().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("comic"));
    const QString subtree = subtreeEndingWith(plain.sliceModel, QStringLiteral("Akira"));
    QVERIFY(!subtree.isEmpty());

    QMap<QString, QString> ov;
    ov.insert(normForTest(subtree), QStringLiteral("book"));
    const auto r = VaultScanner::buildScan(mixedLeafRoot(), {}, 1, {}, ov);

    QCOMPARE(r.sliceModel.size(), 1);
    const QVariantMap sm = r.sliceModel.first().toMap();
    QCOMPARE(sm.value(QStringLiteral("kind")).toString(), QStringLiteral("book"));
    QCOMPARE(sm.value(QStringLiteral("count")).toInt(), 1);          // the one book shelves
    QCOMPARE(sm.value(QStringLiteral("leftoverCount")).toInt(), 2);  // the two comics leftover
    QCOMPARE(r.rows.size(), 1);
    QCOMPARE(r.rows.first().kind, QStringLiteral("book"));
    // a289133 (2026-08-09) re-fixtured the Akira leftover notes.txt -> notes.epub
    QVERIFY(r.rows.first().realName.endsWith(QStringLiteral(".epub")));
}

void tst_vault_scanner::override_relabels_folder_to_absent_kind()
{
    // Thread A, relabel case: Berserk (3 comics, no books) declared "book" → all three
    // shelve re-labelled as book. Proven end-to-end through the aggregate publish: the
    // item total is unchanged, only the shelf moves.
    const auto plain = VaultScanner::buildScan(mixedRoot(), {}, 1, {});
    const QString berserk = subtreeEndingWith(plain.sliceModel, QStringLiteral("Berserk"));
    QVERIFY(!berserk.isEmpty());

    QMap<QString, QString> ov;
    ov.insert(normForTest(berserk), QStringLiteral("book"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    const quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(mixedRoot(), {}, g, {}, ov)}, g);

    QCOMPARE(idx.itemCount(), kExpectedItems);                    // nothing lost — reshelved
    QCOMPARE(idx.itemCountForKind(QStringLiteral("book")), 2 + 3); // Dune 2 + Berserk 3
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 1);   // only the loose comic left
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
    // Slice 18 signature: (confirmedRoots, scanIgnore, kindOverrides, extraRows).
    sc.publishConfirmed({mixedRoot()}, {}, {}, {});
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

// ── Slice 15: the live-shelf watcher (VaultWatcher) ───────────────────────────────
// The watcher's debounced handler (processRoot) is driven SYNCHRONOUSLY against a
// QTemporaryDir root: the plan's "touch files → exact upsert set" rows.

namespace {
// Write a real file under `dir`; returns its absolute path.
QString writeMediaFile(const QString& dir, const QString& name)
{
    QFile f(dir + QLatin1Char('/') + name);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write("stub"); // no archive bytes needed — VaultKit classifies by extension
    f.close();
    return f.fileName();
}
} // namespace

void tst_vault_scanner::watcher_touch_files_upserts_exact_arrival_set()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Series A")));
    const QString subtree = root.path() + QStringLiteral("/Series A");
    writeMediaFile(subtree, QStringLiteral("Vol 1.cbz")); // the pre-existing shelved truth

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    VaultScanner sc(&idx, &ident);
    VaultWatcher w(&idx, &ident, &cfg);

    // Publish the pre-state through the census path (identity reconcile → the same ids
    // the watcher will compute).
    const quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    QCOMPARE(idx.itemCount(), 1);

    // A BURST of three arrivals (the debounce coalesces the trigger, never the rows):
    // one upsert per file — the exact arrival set, nothing re-upserted.
    writeMediaFile(subtree, QStringLiteral("Vol 2.cbz"));
    writeMediaFile(subtree, QStringLiteral("Vol 3.cbz"));
    writeMediaFile(subtree, QStringLiteral("Vol 4.cbz"));

    const auto landing = w.processRoot(root.path(), {}, {});
    QCOMPARE(landing.landedCount, 3);            // one upsert per file in the burst
    QCOMPARE(idx.itemCount(), 4);                // pre 1 + burst 3
    QCOMPARE(landing.newKindSlices.size(), 0);   // same-kind arrivals never raise a card
    const QSet<QString> ids = idx.fileIdsInRoot(root.path());
    QCOMPARE(ids.size(), 4);                     // the exact set: pre + the three arrivals

    // A second pass sees nothing new — the diff is idempotent.
    QCOMPARE(w.processRoot(root.path(), {}, {}).landedCount, 0);
}

void tst_vault_scanner::watcher_new_kind_raises_card_and_lands()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Manga")));
    const QString subtree = root.path() + QStringLiteral("/Manga");
    writeMediaFile(subtree, QStringLiteral("Berserk v01.cbz"));
    writeMediaFile(subtree, QStringLiteral("Berserk v02.cbz")); // the root's law: comic

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    VaultScanner sc(&idx, &ident);
    VaultWatcher w(&idx, &ident, &cfg);

    const quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    QCOMPARE(idx.itemCount(), 2);

    // A NEW-KIND arrival (epub in a comic-law subtree): it lands on the BOOK shelf AND
    // raises the one-slice card (S11 law).
    writeMediaFile(subtree, QStringLiteral("Berserk v03.epub"));
    const auto landing = w.processRoot(root.path(), {}, {});
    QCOMPARE(landing.landedCount, 1);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("book")), 1); // landed on the right shelf
    QCOMPARE(landing.newKindSlices.size(), 1);
    const QVariantMap slice = landing.newKindSlices.first().toMap();
    QCOMPARE(slice.value(QStringLiteral("kind")).toString(), QStringLiteral("book"));
    QCOMPARE(slice.value(QStringLiteral("count")).toInt(), 1);
    QVERIFY(slice.value(QStringLiteral("subtreePath")).toString().endsWith(QStringLiteral("Manga")));
    QVERIFY(slice.value(QStringLiteral("sample")).toString().contains(QStringLiteral("Berserk v03")));

    // A same-kind arrival after the card is gone raises NO card.
    writeMediaFile(subtree, QStringLiteral("Berserk v04.cbz"));
    const auto again = w.processRoot(root.path(), {}, {});
    QCOMPARE(again.landedCount, 1);
    QCOMPARE(again.newKindSlices.size(), 0);
}

void tst_vault_scanner::watcher_override_law_raises_card_for_known_kind_arrival()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Library")));
    const QString subtree = root.path() + QStringLiteral("/Library");
    writeMediaFile(subtree, QStringLiteral("Art 01.cbz"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    VaultScanner sc(&idx, &ident);
    VaultWatcher w(&idx, &ident, &cfg);

    const quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    QCOMPARE(idx.itemCount(), 1);

    // The user's chip override re-declares the subtree a BOOK folder — the law is now
    // book, so a comic arrival is a NEW-KIND arrival even though comics dominated it.
    QVariantMap ov;
    ov.insert(normForTest(subtree), QStringLiteral("book"));
    writeMediaFile(subtree, QStringLiteral("Art 02.cbz"));

    const auto landing = w.processRoot(root.path(), {}, ov);
    QCOMPARE(landing.landedCount, 1);
    QCOMPARE(landing.newKindSlices.size(), 1);
    QCOMPARE(landing.newKindSlices.first().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("comic"));
}

void tst_vault_scanner::watcher_degraded_flag_on_failed_watch_and_recovers()
{
    QTemporaryDir cfgDir;
    QVERIFY(cfgDir.isValid());
    VaultConfig cfg(cfgDir.path());

    // A confirmed root that does not exist: QFileSystemWatcher::addPath fails → degraded.
    const QString missing = cfgDir.path() + QStringLiteral("/gone");
    cfg.addRoot(missing);
    cfg.confirmRoot(missing);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultWatcher w(&idx, &ident, &cfg);

    w.refresh();
    QVERIFY(w.isRootDegraded(missing)); // the simulated failure flag is per-root

    // The root appears (drive reconnects): a re-arm clears the flag.
    QVERIFY(QDir().mkpath(missing));
    w.refresh();
    QVERIFY(!w.isRootDegraded(missing));

    // A healthy confirmed root watches cleanly from the start.
    cfg.addRoot(mixedRoot());
    cfg.confirmRoot(mixedRoot());
    w.refresh();
    QVERIFY(!w.isRootDegraded(mixedRoot()));
}

QTEST_GUILESS_MAIN(tst_vault_scanner)
#include "tst_vault_scanner.moc"
