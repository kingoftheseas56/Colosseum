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
#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QVariantMap>
#include <QtTest>
#include <QtConcurrent>

#include <memory>

namespace {
QString writeMediaFile(const QString& dir, const QString& name);
}

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
    void publish_missing_root_preserves_rows_and_revives();
    void scanner_rename_reattaches_same_id_and_progress();
    // ── Slice 15: the live-shelf watcher (VaultWatcher) ──
    void watcher_touch_files_upserts_exact_arrival_set();
    void watcher_deletion_removes_stale_row_and_preserves_unchanged_facts();
    void watcher_same_path_material_replacement_keeps_one_physical_row();
    void watcher_nested_file_in_existing_subfolder_is_seen();
    void watcher_new_subfolder_is_watched_before_later_file_arrival();
    void watcher_create_then_fill_during_tree_walk_is_not_missed();
    void watcher_refresh_does_not_replay_active_tree_walk();
    void watcher_probe_does_not_rewalk_completed_tree();
    void watcher_directory_cap_marks_root_degraded();
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

void tst_vault_scanner::publish_missing_root_preserves_rows_and_revives()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Series")));
    writeMediaFile(root.path() + QStringLiteral("/Series"), QStringLiteral("Vol 1.cbz"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);

    quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    QCOMPARE(idx.itemCount(), 1);
    auto rows = idx.rowsForRoot(root.path());
    QCOMPARE(rows.size(), 1);
    rows[0].progressed = true;
    QVERIFY(idx.upsertMany(rows));

    const QString awayPath = root.path() + QStringLiteral("-away");
    QVERIFY(QDir().rename(root.path(), awayPath));
    g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    QCOMPARE(idx.itemCount(), 1); // missing root is preserved, not published as empty
    auto away = idx.rowsForRoot(root.path());
    QCOMPARE(away.size(), 1);
    QVERIFY(away.first().away);
    QVERIFY(away.first().progressed);

    QVERIFY(QDir().rename(awayPath, root.path()));
    g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    const auto revived = idx.rowsForRoot(root.path());
    QCOMPARE(revived.size(), 1);
    QVERIFY(!revived.first().away);
    QVERIFY(revived.first().progressed);
}

void tst_vault_scanner::scanner_rename_reattaches_same_id_and_progress()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Series")));
    const QString oldPath = writeMediaFile(root.path() + QStringLiteral("/Series"),
                                           QStringLiteral("old.cbz"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultScanner sc(&idx, &ident);
    quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    auto before = idx.rowsForRoot(root.path());
    QCOMPARE(before.size(), 1);
    const QString oldId = before.first().id;
    before[0].progressed = true;
    QVERIFY(idx.upsertMany(before));

    const QString newPath = root.path() + QStringLiteral("/Series/new.cbz");
    const QDateTime oldMtime = QFileInfo(oldPath).lastModified();
    QVERIFY(QFile::rename(oldPath, newPath));
    // QFile::rename preserves the file timestamp on the target filesystem. Avoid rewriting it
    // here: some Windows filesystem providers reject setFileTime even though rename is lossless.
    QCOMPARE(QFileInfo(newPath).lastModified().toMSecsSinceEpoch(), oldMtime.toMSecsSinceEpoch());

    g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    const auto after = idx.rowsForRoot(root.path());
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().id, oldId);
    QCOMPARE(after.first().path, newPath);
    QVERIFY(after.first().progressed);
}

// ── Slice 15: the live-shelf watcher (VaultWatcher) ───────────────────────────────
// The watcher's debounced handler (processRoot) is driven SYNCHRONOUSLY against a
// QTemporaryDir root: the plan's "touch files → exact upsert set" rows.

namespace {
// Write a real file under `dir`; returns its absolute path.
QString writeMediaFile(const QString& dir, const QString& name)
{
    QFile f(dir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    if (f.write("stub") != 4) // no archive bytes needed — VaultKit classifies by extension
        return {};
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

void tst_vault_scanner::watcher_deletion_removes_stale_row_and_preserves_unchanged_facts()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString subtree = root.path() + QStringLiteral("/Series");
    QVERIFY(QDir().mkpath(subtree));
    const QString keepPath = writeMediaFile(subtree, QStringLiteral("Keep.cbz"));
    const QString deletePath = writeMediaFile(subtree, QStringLiteral("Delete.cbz"));

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
    auto keepRows = idx.rowsForPath(keepPath);
    QCOMPARE(keepRows.size(), 1);
    keepRows[0].progressed = true;
    keepRows[0].coverRef = QStringLiteral("001.jpg");
    QVERIFY(idx.upsertMany(keepRows));

    QVERIFY(QFile::remove(deletePath));
    const auto landing = w.processRoot(root.path(), {}, {});
    QCOMPARE(landing.landedCount, 0);
    QCOMPARE(landing.removedCount, 1);
    QCOMPARE(idx.itemCount(), 1);
    keepRows = idx.rowsForPath(keepPath);
    QCOMPARE(keepRows.size(), 1);
    QVERIFY(keepRows.first().progressed);
    QCOMPARE(keepRows.first().coverRef, QStringLiteral("001.jpg"));
}

void tst_vault_scanner::watcher_same_path_material_replacement_keeps_one_physical_row()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString subtree = root.path() + QStringLiteral("/Series");
    QVERIFY(QDir().mkpath(subtree));
    const QString path = writeMediaFile(subtree, QStringLiteral("Volume.cbz"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    VaultScanner sc(&idx, &ident);
    VaultWatcher w(&idx, &ident, &cfg);

    const quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    const auto before = idx.rowsForPath(path);
    QCOMPARE(before.size(), 1);
    const QString oldId = before.first().id;

    QFile replacement(path);
    QVERIFY(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(replacement.write("materially-different-replacement-bytes"), qint64(38));
    replacement.close();

    const auto landing = w.processRoot(root.path(), {}, {});
    QCOMPARE(landing.landedCount, 1);
    QCOMPARE(landing.removedCount, 1);
    const auto after = idx.rowsForPath(path);
    QCOMPARE(after.size(), 1);
    QVERIFY(after.first().id != oldId);
    QCOMPARE(idx.itemCount(), 1);
}

void tst_vault_scanner::watcher_nested_file_in_existing_subfolder_is_seen()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString subtree = root.path() + QStringLiteral("/Series");
    QVERIFY(QDir().mkpath(subtree));
    writeMediaFile(subtree, QStringLiteral("Episode 01.mp4"));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    cfg.addRoot(root.path());
    cfg.confirmRoot(root.path());
    VaultScanner sc(&idx, &ident);
    const quint64 g = sc.nextGeneration();
    sc.applyPublish({VaultScanner::buildScan(root.path(), {}, g, {})}, g);
    QCOMPARE(idx.itemCount(), 1);

    VaultWatcher w(&idx, &ident, &cfg);
    QSignalSpy treeReady(&w, &VaultWatcher::watchTreeReconciled);
    QSignalSpy landed(&w, &VaultWatcher::landed);
    w.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(treeReady.count() >= 1, 5000);

    writeMediaFile(subtree, QStringLiteral("Episode 02.mp4"));
    QTRY_VERIFY_WITH_TIMEOUT(landed.count() >= 1, 5000);
    QCOMPARE(idx.itemCount(), 2);
}

void tst_vault_scanner::watcher_new_subfolder_is_watched_before_later_file_arrival()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    cfg.addRoot(root.path());
    cfg.confirmRoot(root.path());
    VaultWatcher w(&idx, &ident, &cfg);
    QSignalSpy treeReady(&w, &VaultWatcher::watchTreeReconciled);
    QSignalSpy landed(&w, &VaultWatcher::landed);
    w.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(treeReady.count() >= 1, 5000);

    const int initialReconciles = treeReady.count();
    const QString subtree = root.path() + QStringLiteral("/NewShow");
    QVERIFY(QDir().mkpath(subtree));
    QTRY_VERIFY_WITH_TIMEOUT(treeReady.count() > initialReconciles, 5000);

    writeMediaFile(subtree, QStringLiteral("Episode 01.mp4"));
    QTRY_VERIFY_WITH_TIMEOUT(landed.count() >= 1, 5000);
    QCOMPARE(idx.itemCount(), 1);
}

void tst_vault_scanner::watcher_create_then_fill_during_tree_walk_is_not_missed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    cfg.addRoot(root.path());
    cfg.confirmRoot(root.path());
    VaultWatcher w(&idx, &ident, &cfg);

    // Hold the only pool worker so refresh()'s recursive walk is definitely in flight while
    // the directory is created. The file is added only after the original 300ms debounce has
    // elapsed; a roots-only watcher loses it unless publication waits for tree registration.
    QSemaphore workerStarted;
    QSemaphore releaseWorker;
    QThreadPool* pool = QThreadPool::globalInstance();
    const int oldMaxThreadCount = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QFuture<void> blocker = QtConcurrent::run([&]() {
        workerStarted.release();
        releaseWorker.acquire();
    });
    QVERIFY(workerStarted.tryAcquire(1, 5000));

    QSignalSpy landed(&w, &VaultWatcher::landed);
    w.refresh();
    const QString subtree = root.path() + QStringLiteral("/NewShow");
    QVERIFY(QDir().mkpath(subtree));
    // Drive the same directory-change slot the QFileSystemWatcher signal uses, but without
    // depending on the platform's delivery timing for the root notification.
    QVERIFY(QMetaObject::invokeMethod(&w, "onDirectoryChanged", Qt::DirectConnection,
                                      Q_ARG(QString, root.path())));
    QTest::qWait(350); // let the original debounce expire while the walk remains blocked
    writeMediaFile(subtree, QStringLiteral("Episode 01.mp4"));

    releaseWorker.release();
    QTRY_COMPARE_WITH_TIMEOUT(idx.itemCount(), 1, 5000);
    QVERIFY(landed.count() >= 1);

    blocker.waitForFinished();
    pool->setMaxThreadCount(oldMaxThreadCount);
}

void tst_vault_scanner::watcher_refresh_does_not_replay_active_tree_walk()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    cfg.addRoot(root.path());
    cfg.confirmRoot(root.path());
    VaultWatcher w(&idx, &ident, &cfg);
    QSignalSpy reconciled(&w, &VaultWatcher::watchTreeReconciled);

    QSemaphore workerStarted;
    QSemaphore releaseWorker;
    QThreadPool* pool = QThreadPool::globalInstance();
    const int oldMaxThreadCount = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QFuture<void> blocker = QtConcurrent::run([&]() {
        workerStarted.release();
        releaseWorker.acquire();
    });
    QVERIFY(workerStarted.tryAcquire(1, 5000));

    w.refresh();
    w.refresh(); // probe-style reconciliation while the original walk is still active
    releaseWorker.release();
    QTRY_COMPARE_WITH_TIMEOUT(reconciled.count(), 1, 5000);

    blocker.waitForFinished();
    pool->setMaxThreadCount(oldMaxThreadCount);
}

void tst_vault_scanner::watcher_probe_does_not_rewalk_completed_tree()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/Season 01")));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    cfg.addRoot(root.path());
    cfg.confirmRoot(root.path());
    VaultWatcher w(&idx, &ident, &cfg);
    QSignalSpy reconciled(&w, &VaultWatcher::watchTreeReconciled);

    w.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(reconciled.count(), 1, 5000);
    // Cross at least TWO 1-second probe ticks (m_probe->setInterval(1000)) — a single 1250ms
    // wait is not a reliable proof the probe fired at all on a loaded machine (the tick could
    // simply not have run yet, passing vacuously). 2500ms guarantees at least two ticks landed
    // while keeping the case well under the 6s budget. A healthy, already-registered tree must
    // not recursively walk again just because availability was probed.
    QTest::qWait(2500);
    QCOMPARE(reconciled.count(), 1);
    w.refresh();
    QTest::qWait(250);
    QCOMPARE(reconciled.count(), 1);
}

void tst_vault_scanner::watcher_directory_cap_marks_root_degraded()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    for (int i = 0; i < 513; ++i)
        QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/Dir%1").arg(i)));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    VaultIdentity ident(tmp.path());
    VaultConfig cfg(tmp.path());
    cfg.addRoot(root.path());
    cfg.confirmRoot(root.path());
    VaultWatcher w(&idx, &ident, &cfg);
    QSignalSpy treeReady(&w, &VaultWatcher::watchTreeReconciled);

    w.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(treeReady.count() >= 1, 5000);
    QVERIFY(w.isRootDegraded(root.path()));

    // A later refresh must not clear the cap state just because the root itself is watchable;
    // VaultLibrary needs to observe the preserved degraded flag and run its silent fallback.
    w.refresh();
    QVERIFY(w.isRootDegraded(root.path()));

    // A capped root must stay RE-WALKABLE — self-healing (a later relief of the watch budget,
    // directories removed, etc.) can only be discovered by another attempt. A healthy completed
    // walk sets m_treeInitialized and refresh()'s gate skips it forever; a capped/degraded walk
    // must NOT set that bit, so a later refresh() re-triggers scheduleTreeWatch and fires another
    // watchTreeReconciled — proving the root gets more than the "one walk attempt per session"
    // regression this test guards against.
    const int beforeRewalk = treeReady.count();
    w.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(treeReady.count() > beforeRewalk, 5000);
    QVERIFY(w.isRootDegraded(root.path()));
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
    QSignalSpy availability(&w, &VaultWatcher::rootAvailabilityChanged);

    w.refresh();
    QVERIFY(w.isRootDegraded(missing)); // the simulated failure flag is per-root
    QVERIFY(availability.count() >= 1);
    QCOMPARE(availability.last().at(0).toString(), normForTest(missing));
    QCOMPARE(availability.last().at(1).toBool(), false);

    // The root appears (drive reconnects): a re-arm clears the flag.
    QVERIFY(QDir().mkpath(missing));
    w.refresh();
    QVERIFY(!w.isRootDegraded(missing));
    QVERIFY(availability.count() >= 2);
    QCOMPARE(availability.last().at(1).toBool(), true);

    // A healthy confirmed root watches cleanly from the start.
    cfg.addRoot(mixedRoot());
    cfg.confirmRoot(mixedRoot());
    w.refresh();
    QVERIFY(!w.isRootDegraded(mixedRoot()));
}

QTEST_GUILESS_MAIN(tst_vault_scanner)
#include "tst_vault_scanner.moc"
