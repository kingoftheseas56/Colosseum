// tst_vault_index — Slice 3. Proves VaultIndex: publish/query round-trip over a
// small census, numeric-aware folder-order listing, transactional atomicity (a
// cancelled publish rolls back and leaves the previous contents intact), and
// incremental single-file upsert. SQLite in a QTemporaryDir per run (no committed
// .sqlite, house rule); the qsqlite driver resolves because the target lands in
// build-msvc/ beside the app-deployed plugin (ledger deploy note). GUILESS.

#include "engine/VaultIndex.h"
#include "engine/VaultKit.h" // CancellationToken

#include <QSet>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

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

private slots:
    void publish_and_query_round_trip();
    void folder_listing_is_natural_order();
    void publish_is_atomic_when_cancelled();
    void incremental_upsert_lands_without_republish();
    void groups_expose_representative_cover();
    void enrichment_round_trip_via_rows_for_kind_and_upsert_many();
    void files_in_subtree_groups_loose_then_subfolders_no_invented_entries();
    void natural_sort_key_is_numeric_and_case_insensitive();
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

QTEST_GUILESS_MAIN(tst_vault_index)
#include "tst_vault_index.moc"
