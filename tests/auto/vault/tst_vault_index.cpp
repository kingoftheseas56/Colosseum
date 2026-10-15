// tst_vault_index — Slice 3. Proves VaultIndex: publish/query round-trip over a
// small census, numeric-aware folder-order listing, transactional atomicity (a
// cancelled publish rolls back and leaves the previous contents intact), and
// incremental single-file upsert. SQLite in a QTemporaryDir per run (no committed
// .sqlite, house rule); the qsqlite driver resolves because the target lands in
// build-msvc/ beside the app-deployed plugin (ledger deploy note). GUILESS.

#include "engine/VaultIndex.h"
#include "engine/VaultKit.h" // CancellationToken

#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariantMap>
#include <QtTest>

#include <algorithm>

namespace {
constexpr int kExpectedVaultSchemaVersion = 7;
}

class tst_vault_index : public QObject
{
    Q_OBJECT

private:
    static VaultIndex::FileRow mk(const QString& id, const QString& subtree,
                                  const QString& groupTitle, const QString& kind,
                                  const QString& realName, const QString& subfolder = QString())
    {
        VaultIndex::FileRow r;
        r.id = id;
        r.rootPath = QStringLiteral("D:/lib");
        r.subtreePath = subtree;
        r.groupKey = subtree;
        r.groupTitle = groupTitle;
        r.kind = kind;
        r.path = subtree + QLatin1Char('/') + realName;
        r.displayTitle = realName;
        r.realName = realName;
        r.subfolder = subfolder;
        r.size = 100;
        r.mtimeMs = 1;
        return r;
    }

    // Build a LEGACY Vault DB (pre-admission columns) stamped at `userVersion`, so migration and
    // the fail-closed-on-newer-version path can be exercised against a real on-disk file.
    static bool createLegacyVaultDb(const QString& path, int userVersion)
    {
        const QString connection =
            QStringLiteral("legacy_vault_%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

        bool ok = false;
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(path);
            if (db.open()) {
                QSqlQuery q(db);
                ok = q.exec(QStringLiteral(
                        "CREATE TABLE files ("
                        " id TEXT PRIMARY KEY,"
                        " rootPath TEXT, subtreePath TEXT, groupKey TEXT, groupTitle TEXT,"
                        " kind TEXT, path TEXT, displayTitle TEXT, realName TEXT, subfolder TEXT,"
                        " sortKey TEXT, size INTEGER, mtimeMs INTEGER,"
                        " pages INTEGER, durationSec REAL, author TEXT, format TEXT,"
                        " progressed INTEGER DEFAULT 0, coverRef TEXT)"))
                    && q.exec(QStringLiteral("PRAGMA user_version = %1").arg(userVersion));
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return ok;
    }

    static int userVersionOf(const QString& path)
    {
        const QString connection =
            QStringLiteral("read_vault_version_%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

        int version = -1;
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(path);
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
                    version = q.value(0).toInt();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return version;
    }

private slots:
    void publish_and_query_round_trip();
    void folder_listing_is_natural_order();
    void publish_is_atomic_when_cancelled();
    void incremental_upsert_lands_without_republish();
    void root_away_state_keeps_rows_and_progress();
    void error_state_round_trips_and_groups_surface_it();
    void groups_expose_representative_cover();
    void enrichment_round_trip_via_rows_for_kind_and_upsert_many();
    void files_in_subtree_groups_loose_then_subfolders_no_invented_entries();
    void natural_sort_key_is_numeric_and_case_insensitive();
    // ── browse-face execution plan, Slice 1 ──
    void recent_groups_orders_newest_mtime_first_across_kinds();
    // ── vault-admission slice ──
    void legacy_schema_migrates_and_stamps_current_version();
    void future_schema_fails_closed_without_downgrade();
    // ── browse-face execution plan, Slice 2 ──
    void identity_state_round_trips_and_survives_a_republish_that_still_carries_it();
    void publish_carries_admission_only_for_exact_identity_tuple();
    void publish_explicit_new_verdict_wins_over_carried_verdict();
    void admission_projection_is_video_only_and_omits_unprobed_rows();
    // ── Arc 14: live reconciliation + stale async write barriers ──
    void reconcile_root_removes_obsolete_and_preserves_unchanged_facts();
    void stale_revision_write_cannot_resurrect_removed_row();
};

void tst_vault_index::publish_and_query_round_trip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("index-v1.sqlite")));
    QVERIFY(idx.isOpen());

    const QList<VaultIndex::FileRow> rows = {
        mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v1.cbz")),
        mk(QStringLiteral("vault:b"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v2.cbz")),
        mk(QStringLiteral("vault:c"), QStringLiteral("D:/lib/Dune"),
           QStringLiteral("Dune"), QStringLiteral("book"), QStringLiteral("Dune.epub")),
        mk(QStringLiteral("vault:d"), QStringLiteral("D:/lib/Show"),
           QStringLiteral("Show"), QStringLiteral("video"),
           QStringLiteral("Show.S01E01.mkv"), QStringLiteral("Season 01")),
    };
    QVERIFY(idx.publish(rows));

    QCOMPARE(idx.itemCount(), 4);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 2);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("book")), 1);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("video")), 1);
    QCOMPARE(idx.kinds(), QStringList({QStringLiteral("book"), QStringLiteral("comic"),
                                       QStringLiteral("video")}));

    const QVariantList comicGroups = idx.groupsForKind(QStringLiteral("comic"));
    QCOMPARE(comicGroups.size(), 1);
    QCOMPARE(comicGroups.first().toMap().value(QStringLiteral("groupTitle")).toString(),
             QStringLiteral("Berserk"));
    QCOMPARE(comicGroups.first().toMap().value(QStringLiteral("count")).toInt(), 2);
}

void tst_vault_index::folder_listing_is_natural_order()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    // Deliberately inserted out of order; the sort key must order them naturally.
    const QList<VaultIndex::FileRow> rows = {
        mk(QStringLiteral("vault:1"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v10.cbz")),
        mk(QStringLiteral("vault:2"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v2.cbz")),
        mk(QStringLiteral("vault:3"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v1.cbz")),
    };
    QVERIFY(idx.publish(rows));

    const QVariantList files = idx.filesInSubtree(QStringLiteral("D:/lib/Berserk"));
    QCOMPARE(files.size(), 3);
    QCOMPARE(files.at(0).toMap().value(QStringLiteral("realName")).toString(),
             QStringLiteral("Berserk v1.cbz"));
    QCOMPARE(files.at(1).toMap().value(QStringLiteral("realName")).toString(),
             QStringLiteral("Berserk v2.cbz"));
    QCOMPARE(files.at(2).toMap().value(QStringLiteral("realName")).toString(),
             QStringLiteral("Berserk v10.cbz")); // v10 after v2, not lexicographic
}

void tst_vault_index::publish_is_atomic_when_cancelled()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    // Set A: two comics.
    QVERIFY(idx.publish({
        mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/A"),
           QStringLiteral("A"), QStringLiteral("comic"), QStringLiteral("a.cbz")),
        mk(QStringLiteral("vault:b"), QStringLiteral("D:/lib/A"),
           QStringLiteral("A"), QStringLiteral("comic"), QStringLiteral("b.cbz")),
    }));
    QCOMPARE(idx.itemCount(), 2);

    // A cancelled republish must roll back the DELETE and leave set A intact.
    VaultKit::CancellationToken tok;
    tok.cancel();
    const QList<VaultIndex::FileRow> setB = {
        mk(QStringLiteral("vault:c"), QStringLiteral("D:/lib/A"),
           QStringLiteral("A"), QStringLiteral("comic"), QStringLiteral("c.cbz")),
    };
    QVERIFY(!idx.publish(setB, &tok));
    QCOMPARE(idx.itemCount(), 2);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 2);
}

void tst_vault_index::incremental_upsert_lands_without_republish()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    QVERIFY(idx.publish({
        mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/A"),
           QStringLiteral("A"), QStringLiteral("comic"), QStringLiteral("a.cbz")),
        mk(QStringLiteral("vault:b"), QStringLiteral("D:/lib/A"),
           QStringLiteral("A"), QStringLiteral("comic"), QStringLiteral("b.cbz")),
    }));
    QCOMPARE(idx.itemCount(), 2);

    QVERIFY(idx.upsert(mk(QStringLiteral("vault:c"), QStringLiteral("D:/lib/A"),
                          QStringLiteral("A"), QStringLiteral("comic"),
                          QStringLiteral("c.cbz"))));
    QCOMPARE(idx.itemCount(), 3);
    QCOMPARE(idx.itemCountForKind(QStringLiteral("comic")), 3);
}

void tst_vault_index::root_away_state_keeps_rows_and_progress()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto row = mk(QStringLiteral("vault:away"), QStringLiteral("D:/lib/Series"),
                  QStringLiteral("Series"), QStringLiteral("comic"),
                  QStringLiteral("Vol 1.cbz"));
    row.progressed = true;
    QVERIFY(idx.publish({row}));

    QVERIFY(idx.markRootAway(QStringLiteral("D:/lib"), true));
    const auto away = idx.filesInSubtree(QStringLiteral("D:/lib/Series"));
    QCOMPARE(away.size(), 1);
    QCOMPARE(away.first().toMap().value(QStringLiteral("away")).toBool(), true);
    QCOMPARE(away.first().toMap().value(QStringLiteral("progressed")).toBool(), true);
    QCOMPARE(idx.itemCount(), 1); // away rows do not vanish

    QVERIFY(idx.markRootAway(QStringLiteral("D:/lib"), false));
    const auto revived = idx.filesInSubtree(QStringLiteral("D:/lib/Series"));
    QCOMPARE(revived.first().toMap().value(QStringLiteral("away")).toBool(), false);
    QCOMPARE(revived.first().toMap().value(QStringLiteral("progressed")).toBool(), true);
}

void tst_vault_index::error_state_round_trips_and_groups_surface_it()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto row = mk(QStringLiteral("vault:bad"), QStringLiteral("D:/lib/Bad"),
                  QStringLiteral("Bad"), QStringLiteral("comic"),
                  QStringLiteral("bad.cbz"));
    row.errorState = QStringLiteral("corrupt");
    row.errorDetail = QStringLiteral("invalid zip central directory");
    QVERIFY(idx.publish({row}));

    const auto groups = idx.groupsForKind(QStringLiteral("comic"));
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().toMap().value(QStringLiteral("errorCount")).toInt(), 1);
    const auto files = idx.filesInSubtree(QStringLiteral("D:/lib/Bad"));
    QCOMPARE(files.first().toMap().value(QStringLiteral("errorState")).toString(),
             QStringLiteral("corrupt"));
    QCOMPARE(files.first().toMap().value(QStringLiteral("errorDetail")).toString(),
             QStringLiteral("invalid zip central directory"));
}

void tst_vault_index::groups_expose_representative_cover()
{
    // Slice 12: groupsForKind returns a representative cover (lowest sortKey with a coverRef)
    // for the shelf tile — comics carry one after enrichment; books/video come back empty.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    VaultIndex::FileRow a = mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/Berserk"),
                              QStringLiteral("Berserk"), QStringLiteral("comic"),
                              QStringLiteral("Berserk v1.cbz"));
    a.coverRef = QStringLiteral("cover.jpg"); // the enriched cover entry
    const VaultIndex::FileRow b = mk(QStringLiteral("vault:b"), QStringLiteral("D:/lib/Berserk"),
                                     QStringLiteral("Berserk"), QStringLiteral("comic"),
                                     QStringLiteral("Berserk v2.cbz")); // no cover
    const VaultIndex::FileRow d = mk(QStringLiteral("vault:d"), QStringLiteral("D:/lib/Dune"),
                                     QStringLiteral("Dune"), QStringLiteral("book"),
                                     QStringLiteral("Dune.epub"));
    QVERIFY(idx.publish({a, b, d}));

    const QVariantList comics = idx.groupsForKind(QStringLiteral("comic"));
    QCOMPARE(comics.size(), 1);
    const QVariantMap g = comics.first().toMap();
    QCOMPARE(g.value(QStringLiteral("coverEntry")).toString(), QStringLiteral("cover.jpg"));
    QCOMPARE(g.value(QStringLiteral("coverPath")).toString(),
             QStringLiteral("D:/lib/Berserk/Berserk v1.cbz"));

    const QVariantList books = idx.groupsForKind(QStringLiteral("book"));
    QCOMPARE(books.size(), 1);
    QVERIFY(books.first().toMap().value(QStringLiteral("coverEntry")).toString().isEmpty());
}

void tst_vault_index::enrichment_round_trip_via_rows_for_kind_and_upsert_many()
{
    // Slice 12: the enrichment pass reads full rows (rowsForKind), fills covers off-thread,
    // and writes them back in one batch (upsertMany) — no dupes, cover then surfaces.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    // Published as a census does — no covers yet.
    QVERIFY(idx.publish({
        mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v1.cbz")),
        mk(QStringLiteral("vault:b"), QStringLiteral("D:/lib/Berserk"),
           QStringLiteral("Berserk"), QStringLiteral("comic"), QStringLiteral("Berserk v2.cbz")),
    }));
    QVERIFY(idx.groupsForKind(QStringLiteral("comic")).first().toMap()
                .value(QStringLiteral("coverEntry")).toString().isEmpty());

    QList<VaultIndex::FileRow> comics = idx.rowsForKind(QStringLiteral("comic"));
    QCOMPARE(comics.size(), 2);
    QCOMPARE(comics.at(0).realName, QStringLiteral("Berserk v1.cbz")); // natural order
    comics[0].coverRef = QStringLiteral("cover.jpg");
    QVERIFY(idx.upsertMany(comics));

    QCOMPARE(idx.itemCount(), 2); // replaced in place — no duplicate rows
    QCOMPARE(idx.groupsForKind(QStringLiteral("comic")).first().toMap()
                 .value(QStringLiteral("coverEntry")).toString(), QStringLiteral("cover.jpg"));
}

void tst_vault_index::files_in_subtree_groups_loose_then_subfolders_no_invented_entries()
{
    // Slice 13 folder-view contract: filesInSubtree returns loose files first (empty subfolder),
    // then named subfolders in case-insensitive lexical order, natural order within each group —
    // and EXACTLY the on-disk files (no invented entries: set of paths matches).
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    // Deliberately published out of order; the query must order them.
    QVERIFY(idx.publish({
        mk(QStringLiteral("vault:e10"), QStringLiteral("D:/lib/Show"), QStringLiteral("Show"),
           QStringLiteral("comic"), QStringLiteral("Extra 10.cbz"), QStringLiteral("Extras")),
        mk(QStringLiteral("vault:l10"), QStringLiteral("D:/lib/Show"), QStringLiteral("Show"),
           QStringLiteral("comic"), QStringLiteral("Loose 10.cbz")),
        mk(QStringLiteral("vault:s2"), QStringLiteral("D:/lib/Show"), QStringLiteral("Show"),
           QStringLiteral("comic"), QStringLiteral("Ep 2.cbz"), QStringLiteral("Season 01")),
        mk(QStringLiteral("vault:l2"), QStringLiteral("D:/lib/Show"), QStringLiteral("Show"),
           QStringLiteral("comic"), QStringLiteral("Loose 2.cbz")),
        mk(QStringLiteral("vault:e2"), QStringLiteral("D:/lib/Show"), QStringLiteral("Show"),
           QStringLiteral("comic"), QStringLiteral("Extra 2.cbz"), QStringLiteral("Extras")),
        mk(QStringLiteral("vault:s10"), QStringLiteral("D:/lib/Show"), QStringLiteral("Show"),
           QStringLiteral("comic"), QStringLiteral("Ep 10.cbz"), QStringLiteral("Season 01")),
    }));

    const QVariantList files = idx.filesInSubtree(QStringLiteral("D:/lib/Show"));
    QCOMPARE(files.size(), 6);
    auto sf = [&](int i) { return files.at(i).toMap().value(QStringLiteral("subfolder")).toString(); };
    auto rn = [&](int i) { return files.at(i).toMap().value(QStringLiteral("realName")).toString(); };

    // loose first (empty subfolder), natural within
    QCOMPARE(sf(0), QString());                    QCOMPARE(rn(0), QStringLiteral("Loose 2.cbz"));
    QCOMPARE(sf(1), QString());                    QCOMPARE(rn(1), QStringLiteral("Loose 10.cbz"));
    // then subfolders in case-insensitive lexical order: Extras before Season 01
    QCOMPARE(sf(2), QStringLiteral("Extras"));     QCOMPARE(rn(2), QStringLiteral("Extra 2.cbz"));
    QCOMPARE(sf(3), QStringLiteral("Extras"));     QCOMPARE(rn(3), QStringLiteral("Extra 10.cbz"));
    QCOMPARE(sf(4), QStringLiteral("Season 01"));  QCOMPARE(rn(4), QStringLiteral("Ep 2.cbz"));
    QCOMPARE(sf(5), QStringLiteral("Season 01"));  QCOMPARE(rn(5), QStringLiteral("Ep 10.cbz"));

    // no invented entries: the exact set of paths comes back (a count alone would miss a dup+drop)
    QSet<QString> got;
    for (const QVariant& v : files)
        got.insert(v.toMap().value(QStringLiteral("path")).toString());
    const QSet<QString> want = {
        QStringLiteral("D:/lib/Show/Loose 2.cbz"), QStringLiteral("D:/lib/Show/Loose 10.cbz"),
        QStringLiteral("D:/lib/Show/Extra 2.cbz"), QStringLiteral("D:/lib/Show/Extra 10.cbz"),
        QStringLiteral("D:/lib/Show/Ep 2.cbz"), QStringLiteral("D:/lib/Show/Ep 10.cbz")};
    QCOMPARE(got, want);
}

void tst_vault_index::natural_sort_key_is_numeric_and_case_insensitive()
{
    QVERIFY(VaultIndex::naturalSortKey(QStringLiteral("v2"))
            < VaultIndex::naturalSortKey(QStringLiteral("v10")));
    QVERIFY(VaultIndex::naturalSortKey(QStringLiteral("Berserk v2"))
            < VaultIndex::naturalSortKey(QStringLiteral("Berserk v10")));
    QVERIFY(VaultIndex::naturalSortKey(QStringLiteral("Apple"))
            < VaultIndex::naturalSortKey(QStringLiteral("banana"))); // case-insensitive
}

void tst_vault_index::recent_groups_orders_newest_mtime_first_across_kinds()
{
    // The Browse face's carousel truth (browse-face execution plan, Slice 1): newest-mtime
    // GROUPS (not files), across every kind, most-recent first.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto oldest = mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/Berserk"),
                     QStringLiteral("Berserk"), QStringLiteral("comic"),
                     QStringLiteral("Berserk v1.cbz"));
    oldest.mtimeMs = 1000;
    auto oldestSibling = mk(QStringLiteral("vault:a2"), QStringLiteral("D:/lib/Berserk"),
                            QStringLiteral("Berserk"), QStringLiteral("comic"),
                            QStringLiteral("Berserk v2.cbz"));
    oldestSibling.mtimeMs = 1500; // same group as `oldest` — MAX(mtimeMs) per group, not per file

    auto newest = mk(QStringLiteral("vault:b"), QStringLiteral("D:/lib/Show"),
                     QStringLiteral("Show"), QStringLiteral("video"),
                     QStringLiteral("Show.S01E01.mkv"));
    newest.mtimeMs = 9000;

    auto middle = mk(QStringLiteral("vault:c"), QStringLiteral("D:/lib/Dune"),
                     QStringLiteral("Dune"), QStringLiteral("book"), QStringLiteral("Dune.epub"));
    middle.mtimeMs = 5000;

    QVERIFY(idx.publish({oldest, oldestSibling, newest, middle}));

    const QVariantList recent = idx.recentGroups(2);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.at(0).toMap().value(QStringLiteral("groupKey")).toString(),
             QStringLiteral("D:/lib/Show"));
    QCOMPARE(recent.at(0).toMap().value(QStringLiteral("mtimeMs")).toLongLong(), 9000LL);
    QCOMPARE(recent.at(1).toMap().value(QStringLiteral("groupKey")).toString(),
             QStringLiteral("D:/lib/Dune"));

    // A limit larger than the group count returns every group, still newest-first; the
    // Berserk group's MAX(mtimeMs) is 1500 (its newer sibling file), not 1000.
    const QVariantList all = idx.recentGroups(10);
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.last().toMap().value(QStringLiteral("groupKey")).toString(),
             QStringLiteral("D:/lib/Berserk"));
    QCOMPARE(all.last().toMap().value(QStringLiteral("mtimeMs")).toLongLong(), 1500LL);
}

void tst_vault_index::legacy_schema_migrates_and_stamps_current_version()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("legacy.sqlite"));
    QVERIFY(createLegacyVaultDb(path, 0));

    {
        VaultIndex idx(path);
        QVERIFY(idx.isOpen());
    }

    // The migration is monotonic all the way to the schema owned by this build. Keep this
    // expectation in lockstep with VaultIndex's kVaultSchemaVersion.
    QCOMPARE(userVersionOf(path), kExpectedVaultSchemaVersion);
}

void tst_vault_index::future_schema_fails_closed_without_downgrade()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("future.sqlite"));
    const int futureVersion = kExpectedVaultSchemaVersion + 1;
    QVERIFY(createLegacyVaultDb(path, futureVersion));

    {
        VaultIndex idx(path);
        QVERIFY(!idx.isOpen()); // must refuse to open, never downgrade
    }

    QCOMPARE(userVersionOf(path), futureVersion); // version untouched
}

void tst_vault_index::identity_state_round_trips_and_survives_a_republish_that_still_carries_it()
{
    // Slice 2 (browse-face execution plan): identityState/identityCandidateCount are a plain
    // column pair — VaultIndex does not know WHO set them (VaultIdentifier does). This proves
    // the schema/insertRow/SELECT wiring round-trips them, and that a second publish() for the
    // SAME stable (id, size, mtimeMs) tuple keeps the value WHEN THE CALLER SUPPLIES IT AGAIN —
    // deliberately NOT via publish()'s identity-carry snapshot (that mechanism is a known hazard
    // owned by a different arc and is not widened here). A real filesystem rescan that does NOT
    // re-supply identityState resets it to "" (none) until VaultIdentifier's auto-identify pass
    // (already scheduled on every VaultIndex::changed()) re-derives it — an accepted, reported
    // gap, not a silent one.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto row = mk(QStringLiteral("vault:ambiguous"), QStringLiteral("D:/lib/The Matrix"),
                  QStringLiteral("The Matrix"), QStringLiteral("video"),
                  QStringLiteral("The Matrix.mp4"));
    row.identityState = QStringLiteral("ambiguous");
    row.identityCandidateCount = 2;
    QVERIFY(idx.publish({row}));

    auto rows = idx.rowsForGroup(row.groupKey);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().identityState, QStringLiteral("ambiguous"));
    QCOMPARE(rows.first().identityCandidateCount, 2);
    QCOMPARE(idx.filesInSubtree(row.subtreePath).first().toMap()
                 .value(QStringLiteral("identityState")).toString(),
             QStringLiteral("ambiguous"));

    // Same stable (id, size, mtimeMs) tuple, republished by a caller that still carries the
    // ambiguity fact on the row it hands to publish() — survives.
    QVERIFY(idx.publish({row}));
    rows = idx.rowsForGroup(row.groupKey);
    QCOMPARE(rows.first().identityState, QStringLiteral("ambiguous"));
    QCOMPARE(rows.first().identityCandidateCount, 2);

    // A fresh scan's row (the production shape — VaultScanner never sets identity fields) does
    // NOT carry it — the honest, reported gap above, proven rather than assumed.
    auto rescanned = row;
    rescanned.identityState.clear();
    rescanned.identityCandidateCount = 0;
    QVERIFY(idx.publish({rescanned}));
    rows = idx.rowsForGroup(row.groupKey);
    QVERIFY(rows.first().identityState.isEmpty());
    QCOMPARE(rows.first().identityCandidateCount, 0);
}

void tst_vault_index::publish_carries_admission_only_for_exact_identity_tuple()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto admitted =
        mk(QStringLiteral("vault:v"), QStringLiteral("D:/lib/Show"),
           QStringLiteral("Show"), QStringLiteral("video"),
           QStringLiteral("Show.S01E01.mp4"));
    admitted.size = 1000;
    admitted.mtimeMs = 5000;
    admitted.admissionVerdict = QStringLiteral("Admitted");
    admitted.admissionDetail = QStringLiteral("decoded-frame");
    QVERIFY(idx.publish({admitted}));

    auto unchanged = admitted;
    unchanged.admissionVerdict.clear();
    unchanged.admissionDetail.clear();
    QVERIFY(idx.publish({unchanged}));

    auto rows = idx.rowsForKind(QStringLiteral("video"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().admissionVerdict, QStringLiteral("Admitted"));

    auto changedSize = unchanged;
    changedSize.size += 1; // negative control: changed size drops the inheritance
    QVERIFY(idx.publish({changedSize}));
    rows = idx.rowsForKind(QStringLiteral("video"));
    QVERIFY(rows.first().admissionVerdict.isEmpty());

    changedSize.admissionVerdict = QStringLiteral("RejectedError");
    changedSize.admissionDetail = QStringLiteral("fresh-probe");
    QVERIFY(idx.publish({changedSize}));

    auto changedMtime = changedSize;
    changedMtime.admissionVerdict.clear();
    changedMtime.admissionDetail.clear();
    changedMtime.mtimeMs += 1; // second negative control: changed mtime drops it too
    QVERIFY(idx.publish({changedMtime}));
    rows = idx.rowsForKind(QStringLiteral("video"));
    QVERIFY(rows.first().admissionVerdict.isEmpty());
}

void tst_vault_index::publish_explicit_new_verdict_wins_over_carried_verdict()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto row =
        mk(QStringLiteral("vault:v"), QStringLiteral("D:/lib/Show"),
           QStringLiteral("Show"), QStringLiteral("video"),
           QStringLiteral("Show.mp4"));
    row.size = 1000;
    row.mtimeMs = 5000;
    row.admissionVerdict = QStringLiteral("RejectedError");
    QVERIFY(idx.publish({row}));

    row.admissionVerdict = QStringLiteral("Admitted");
    row.admissionDetail = QStringLiteral("new-probe");
    QVERIFY(idx.publish({row}));

    const auto rows = idx.rowsForKind(QStringLiteral("video"));
    QCOMPARE(rows.first().admissionVerdict, QStringLiteral("Admitted"));
    QCOMPARE(rows.first().admissionDetail, QStringLiteral("new-probe"));
}

void tst_vault_index::admission_projection_is_video_only_and_omits_unprobed_rows()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto admitted =
        mk(QStringLiteral("vault:a"), QStringLiteral("D:/lib/Video"),
           QStringLiteral("Video"), QStringLiteral("video"),
           QStringLiteral("a.mp4"));
    admitted.admissionVerdict = QStringLiteral("Admitted");

    auto rejected = admitted;
    rejected.id = QStringLiteral("vault:r");
    rejected.path = QStringLiteral("D:/lib/Video/r.mp4");
    rejected.realName = QStringLiteral("r.mp4");
    rejected.admissionVerdict = QStringLiteral("RejectedNoVideo");

    auto unprobed = admitted;
    unprobed.id = QStringLiteral("vault:u");
    unprobed.admissionVerdict.clear();

    auto comic =
        mk(QStringLiteral("vault:c"), QStringLiteral("D:/lib/Comic"),
           QStringLiteral("Comic"), QStringLiteral("comic"),
           QStringLiteral("c.cbz"));
    comic.admissionVerdict = QStringLiteral("Admitted"); // negative control: non-video is omitted

    QVERIFY(idx.publish({admitted, rejected, unprobed, comic}));

    const QVariantMap map = idx.admissionById();
    QCOMPARE(map.value(QStringLiteral("vault:a")).toString(), QStringLiteral("Admitted"));
    QCOMPARE(map.value(QStringLiteral("vault:r")).toString(), QStringLiteral("RejectedNoVideo"));
    QVERIFY(!map.contains(QStringLiteral("vault:u")));
    QVERIFY(!map.contains(QStringLiteral("vault:c")));
}

void tst_vault_index::reconcile_root_removes_obsolete_and_preserves_unchanged_facts()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto keep = mk(QStringLiteral("vault:keep"), QStringLiteral("D:/lib/Series"),
                   QStringLiteral("Series"), QStringLiteral("comic"), QStringLiteral("keep.cbz"));
    keep.progressed = true;
    keep.coverRef = QStringLiteral("001.jpg");
    auto gone = mk(QStringLiteral("vault:gone"), QStringLiteral("D:/lib/Series"),
                   QStringLiteral("Series"), QStringLiteral("comic"), QStringLiteral("gone.cbz"));
    QVERIFY(idx.publish({keep, gone}));

    auto arrival = mk(QStringLiteral("vault:new"), QStringLiteral("D:/lib/Series"),
                      QStringLiteral("Series"), QStringLiteral("comic"), QStringLiteral("new.cbz"));
    int removed = -1;
    QVERIFY(idx.reconcileRoot(QStringLiteral("D:/lib"),
                              QSet<QString>{keep.id, arrival.id}, {arrival}, &removed));
    QCOMPARE(removed, 1);
    QCOMPARE(idx.itemCount(), 2);
    const auto rows = idx.rowsForRoot(QStringLiteral("D:/lib"));
    const auto kept = std::find_if(rows.cbegin(), rows.cend(), [&](const auto& r) { return r.id == keep.id; });
    QVERIFY(kept != rows.cend());
    QVERIFY(kept->progressed);
    QCOMPARE(kept->coverRef, QStringLiteral("001.jpg"));
}

void tst_vault_index::stale_revision_write_cannot_resurrect_removed_row()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex idx(tmp.filePath(QStringLiteral("i.sqlite")));
    QVERIFY(idx.isOpen());

    auto row = mk(QStringLiteral("vault:old"), QStringLiteral("D:/lib/Series"),
                  QStringLiteral("Series"), QStringLiteral("comic"), QStringLiteral("old.cbz"));
    QVERIFY(idx.publish({row}));
    const quint64 staleRevision = idx.revision();
    row.coverRef = QStringLiteral("001.jpg"); // async enrichment result derived from stale snapshot

    int removed = -1;
    QVERIFY(idx.reconcileRoot(QStringLiteral("D:/lib"), {}, {}, &removed));
    QCOMPARE(removed, 1);
    QCOMPARE(idx.itemCount(), 0);
    QVERIFY(!idx.upsertManyIfRevision({row}, staleRevision));
    QCOMPARE(idx.itemCount(), 0);
}

QTEST_GUILESS_MAIN(tst_vault_index)
#include "tst_vault_index.moc"
