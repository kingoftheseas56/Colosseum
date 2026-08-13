#include <QtTest>

#include "engine/VaultBrowseDetail.h"
#include "engine/VaultIndex.h"
#include "engine/VaultKit.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

// Vault Browse face execution plan, Slice 7 — the detail sheet's ONE projection,
// `VaultBrowseDetail::detailFor()`. Physical truth only (locked design decision #11): copies you
// hold, companions, extras, and an honest evidence line — never cast, synopsis, or related
// titles. Pulled out of VaultLibrary (same reason as VaultBrowseAway, Slice 6) so this drives a
// real VaultIndex + real fixture folders WITHOUT the full façade's watcher/scanner/identifier
// dependency tree. GUILESS, pure Qt6::Core/Sql.
class VaultBrowseDetailTest final : public QObject
{
    Q_OBJECT

private slots:
    void spiderManFixtureOneCopyTwoCompanionsTwoJunk();
    void extrasAreListedOnTheSheetNeverAsAGridNode();
    void twoRootSameIdentityYieldsTwoCopiesOneSheet();
    void staleKeyReturnsFoundFalse();
    void groupedExtrasRowsNeverCountAsCopies();

private:
    static VaultIndex::FileRow fileRow(const QString& id, const QString& rootPath,
                                       const QString& subtreePath, const QString& path,
                                       const QString& groupTitle)
    {
        VaultIndex::FileRow r;
        r.id = id;
        r.rootPath = rootPath;
        r.subtreePath = subtreePath;
        r.groupKey = subtreePath;
        r.groupTitle = groupTitle;
        r.kind = QStringLiteral("video");
        r.path = path;
        r.displayTitle = QFileInfo(path).fileName();
        r.realName = QFileInfo(path).fileName();
        r.size = 2254857830LL; // ~2.1 GB, mirrors the approved mock's "2.1 GB" copy line
        r.mtimeMs = 1;
        return r;
    }
};

// The real library shape (browse-face execution plan §0 pin): one film folder holding the movie,
// a loose .srt, a Subs/ folder with 2 tracks, an Extras/ trailer, a Featurettes/ making-of, and
// two release-scene junk files that were never media (YIFYStatus.com.txt, www.YTS.MX.jpg).
void VaultBrowseDetailTest::spiderManFixtureOneCopyTwoCompanionsTwoJunk()
{
    const QString folder = QStringLiteral(VAULT_FIXTURES_DIR)
        + QStringLiteral("/browse-film/Spider-Man No Way Home (2021) [1080p] [WEBRip] [5.1] [YTS.MX]");
    QVERIFY(QDir(folder).exists());
    const QString primaryFile = folder
        + QStringLiteral("/Spider-Man.No.Way.Home.2021.1080p.WEBRip.x264-YTS.MX.mp4");
    QVERIFY(QFileInfo(primaryFile).exists());

    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    QVERIFY(index.publish({
        fileRow(QStringLiteral("vault:spiderman"), QStringLiteral("D:/hemanth's folder"), folder,
                primaryFile, QStringLiteral("Spider-Man No Way Home")),
    }));

    const QVariantMap detail = VaultBrowseDetail::detailFor(&index, folder);
    QVERIFY(detail.value(QStringLiteral("found")).toBool());
    QCOMPARE(detail.value(QStringLiteral("copiesHeld")).toInt(), 1);

    const QStringList companions = detail.value(QStringLiteral("companions")).toStringList();
    QCOMPARE(companions.size(), 2);
    QVERIFY(companions.contains(QStringLiteral("Subtitle (.SRT)")));
    QVERIFY(companions.contains(QStringLiteral("Subs · 2 files")));

    const QVariantList extras = detail.value(QStringLiteral("extras")).toList();
    QCOMPARE(extras.size(), 2);
    QStringList extraTitles;
    for (const QVariant& e : extras)
        extraTitles << e.toMap().value(QStringLiteral("title")).toString();
    QVERIFY(extraTitles.contains(QStringLiteral("Spider-Man No Way Home Trailer")));
    QVERIFY(extraTitles.contains(QStringLiteral("Making Of Spider-Man")));

    // THE junk-count contract this fixture exists to prove: exactly the two named release-scene
    // files, never surfaced as a companion, an extra, or anything else.
    QCOMPARE(detail.value(QStringLiteral("ignoredCount")).toInt(), 2);

    // Quality is a plain read of the filename's own release tokens, not a provider lookup.
    QCOMPARE(detail.value(QStringLiteral("bestQualityLine")).toString(),
             QStringLiteral("1080p WEBRip"));

    // Unidentified: honest evidence, no invented certainty.
    QCOMPARE(detail.value(QStringLiteral("identityState")).toString(),
             QStringLiteral("resolving"));
    QCOMPARE(detail.value(QStringLiteral("identityLabel")).toString(),
             QStringLiteral("not yet identified"));
    QVERIFY(detail.value(QStringLiteral("evidence")).toString()
                .contains(QStringLiteral("Vault has not found a confident match yet")));

    // Play routes the exact file, never the containing folder (the Slice-5 bug class this plan
    // explicitly warns about).
    QCOMPARE(detail.value(QStringLiteral("playPath")).toString(), primaryFile);
}

// The SAME fixture proves the design's other Extras contract from this angle: Extras/Featurettes
// never contribute a grid node (VaultKit::planBrowseLevel folds them out entirely — the folder
// still collapses to exactly one Film node), while the detail sheet lists both, playable.
void VaultBrowseDetailTest::extrasAreListedOnTheSheetNeverAsAGridNode()
{
    const QString parent = QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/browse-film");
    const QList<VaultKit::BrowseNode> nodes = VaultKit::planBrowseLevel(parent);
    QCOMPARE(nodes.size(), 1);
    QCOMPARE(nodes.first().nodeType, VaultKit::BrowseNodeType::Film);

    const QString folder = nodes.first().path;
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    const QString primaryFile = folder
        + QStringLiteral("/Spider-Man.No.Way.Home.2021.1080p.WEBRip.x264-YTS.MX.mp4");
    QVERIFY(index.publish({
        fileRow(QStringLiteral("vault:spiderman"), QStringLiteral("D:/hemanth's folder"), folder,
                primaryFile, QStringLiteral("Spider-Man No Way Home")),
    }));

    const QVariantMap detail = VaultBrowseDetail::detailFor(&index, folder);
    const QVariantList extras = detail.value(QStringLiteral("extras")).toList();
    QCOMPARE(extras.size(), 2); // Extras/Trailer + Featurettes/Making-of, listed, never gridded
}

// Two separate physical groups (different roots) sharing ONE adopted canonical identity present
// as 2 copies on ONE sheet — the locked design's "same canonical identity across roots" rule.
void VaultBrowseDetailTest::twoRootSameIdentityYieldsTwoCopiesOneSheet()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    VaultIndex::FileRow rowA = fileRow(QStringLiteral("vault:copyA"), QStringLiteral("D:/root-a"),
        QStringLiteral("D:/root-a/Spider-Man No Way Home"),
        QStringLiteral("D:/root-a/Spider-Man No Way Home/movie.mp4"),
        QStringLiteral("Spider-Man No Way Home"));
    rowA.identityId = QStringLiteral("imdb:tt10872600");
    rowA.identityTitle = QStringLiteral("Spider-Man: No Way Home");
    rowA.identityYear = 2021;
    rowA.identityState = QStringLiteral("adopted");

    VaultIndex::FileRow rowB = fileRow(QStringLiteral("vault:copyB"), QStringLiteral("E:/root-b"),
        QStringLiteral("E:/root-b/Spider-Man.No.Way.Home.2021"),
        QStringLiteral("E:/root-b/Spider-Man.No.Way.Home.2021/movie.mkv"),
        QStringLiteral("Spider-Man No Way Home 2021"));
    rowB.identityId = rowA.identityId;
    rowB.identityTitle = rowA.identityTitle;
    rowB.identityYear = rowA.identityYear;
    rowB.identityState = QStringLiteral("adopted");

    QVERIFY(index.publish({rowA, rowB}));

    const QVariantMap detail = VaultBrowseDetail::detailFor(&index, rowA.subtreePath);
    QVERIFY(detail.value(QStringLiteral("found")).toBool());
    QCOMPARE(detail.value(QStringLiteral("identityState")).toString(),
             QStringLiteral("identified"));
    QCOMPARE(detail.value(QStringLiteral("copiesHeld")).toInt(), 2);

    const QVariantList copies = detail.value(QStringLiteral("copies")).toList();
    QCOMPARE(copies.size(), 2);
    QStringList paths;
    for (const QVariant& c : copies)
        paths << c.toMap().value(QStringLiteral("path")).toString();
    QVERIFY(paths.contains(rowA.path));
    QVERIFY(paths.contains(rowB.path));

    // Play routes the copy the user actually clicked (rowA's group key), not whichever copy
    // happens to sort first.
    QCOMPARE(detail.value(QStringLiteral("playPath")).toString(), rowA.path);
}

void VaultBrowseDetailTest::staleKeyReturnsFoundFalse()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    const QVariantMap detail =
        VaultBrowseDetail::detailFor(&index, QStringLiteral("D:/never-published"));
    QVERIFY(!detail.value(QStringLiteral("found")).toBool());
}

// Real-shape regression (found live, Lanista replay against the browse-face-smoke fixture):
// VaultScanner::groupByFirstLevelSubdir groups EVERY video nested under a film's folder —
// including its Extras/Featurettes files — into the SAME group/subtree (the scanner predates
// the browse-collapse planner and has no Extras-folding of its own). Once the main file is
// identified, a naive read of the whole group (or a naive rowsForIdentity() join) would count
// its own trailer/making-of as second and third "copies". This is the negative-control-adjacent
// case that proves the fold: 3 rows share one groupKey, only 1 (subfolder == "") is a copy.
void VaultBrowseDetailTest::groupedExtrasRowsNeverCountAsCopies()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    const QString root = QStringLiteral("D:/hemanth's folder");
    const QString subtree = root + QStringLiteral("/Spider-Man No Way Home (2021)");

    VaultIndex::FileRow main = fileRow(QStringLiteral("vault:main"), root, subtree,
        subtree + QStringLiteral("/Spider-Man.No.Way.Home.2021.mp4"),
        QStringLiteral("Spider-Man No Way Home"));
    main.identityId = QStringLiteral("imdb:tt10872600");
    main.identityTitle = QStringLiteral("Spider-Man: No Way Home");
    main.identityState = QStringLiteral("adopted");
    // subfolder stays "" — VaultScanner's own convention for a file directly in the subtree.

    VaultIndex::FileRow trailer = fileRow(QStringLiteral("vault:trailer"), root, subtree,
        subtree + QStringLiteral("/Extras/Trailer.mp4"), QStringLiteral("Spider-Man No Way Home"));
    trailer.subfolder = QStringLiteral("Extras");
    // The real bug: VaultIdentifier can adopt the SAME identity for the extra's own row too
    // (it shares the group's title). Seeded here to prove the fold holds even then.
    trailer.identityId = main.identityId;
    trailer.identityTitle = main.identityTitle;
    trailer.identityState = QStringLiteral("adopted");

    VaultIndex::FileRow featurette = fileRow(QStringLiteral("vault:making-of"), root, subtree,
        subtree + QStringLiteral("/Featurettes/Making Of.mp4"),
        QStringLiteral("Spider-Man No Way Home"));
    featurette.subfolder = QStringLiteral("Featurettes");
    featurette.identityId = main.identityId;
    featurette.identityTitle = main.identityTitle;
    featurette.identityState = QStringLiteral("adopted");

    QVERIFY(index.publish({main, trailer, featurette}));

    const QVariantMap detail = VaultBrowseDetail::detailFor(&index, subtree);
    QVERIFY(detail.value(QStringLiteral("found")).toBool());
    QCOMPARE(detail.value(QStringLiteral("copiesHeld")).toInt(), 1);
    const QVariantList copies = detail.value(QStringLiteral("copies")).toList();
    QCOMPARE(copies.size(), 1);
    QCOMPARE(copies.first().toMap().value(QStringLiteral("path")).toString(), main.path);
    QCOMPARE(detail.value(QStringLiteral("playPath")).toString(), main.path);
}

QTEST_MAIN(VaultBrowseDetailTest)
#include "tst_vault_browse_detail.moc"
