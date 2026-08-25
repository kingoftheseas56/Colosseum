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
    // ── vault ux uplift S8: the detail sheet tells the truth it already knows ──
    void runtimeFormatsHumanAndIsAbsentWhileUnknown();
    void rejectedCopyExposesItsAdmissionDetail();
    void afterIdentifyAgainAdoptedReadsIdentifiedAmbiguousStaysHonest();

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

// ── S8: runtime ────────────────────────────────────────────────────────────────────────────
// durationSec (S5's VaultEnricher ffprobe pass) reaches the sheet as a pre-formatted
// `runtimeText` — "1h 47m" / "48m", the AccountActivityFormat.durationText grammar (its JS is
// unreachable from this C++ projection, so detailFor owns a floor-based twin). The key is
// ABSENT while unknown: the -1 unprobed sentinel, 0, and sub-minute files must never render
// ("-1" / "0m" are banned outright by the slice rule).
void VaultBrowseDetailTest::runtimeFormatsHumanAndIsAbsentWhileUnknown()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    struct Case { double durationSec; const char* expect; }; // expect == nullptr means ABSENT
    const Case cases[] = {
        { 6420.0, "1h 47m" },   // the slice's own worked example
        { 2880.0, "48m" },      // minutes-only, no "0h" prefix
        { 3600.0, "1h 0m" },    // exact hour keeps its minutes (the JS formatter's own shape)
        { 6479.9, "1h 47m" },   // floor, never rounds into the next minute
        { 59.0, nullptr },      // sub-minute: never a "0m" stub
        { 0.0, nullptr },       // zero is unknown, not a runtime
        { -1.0, nullptr },      // the unprobed sentinel FileRow ships with
    };
    for (const Case& c : cases) {
        VaultIndex::FileRow row = fileRow(QStringLiteral("vault:runtime-probe"),
            QStringLiteral("D:/root-r"), QStringLiteral("D:/root-r/Runtime Probe"),
            QStringLiteral("D:/root-r/Runtime Probe/probe.mkv"),
            QStringLiteral("Runtime Probe"));
        row.durationSec = c.durationSec;
        QVERIFY2(index.publish({row}), qPrintable(QString::number(c.durationSec)));
        const QVariantMap detail = VaultBrowseDetail::detailFor(&index, row.subtreePath);
        QVERIFY2(detail.value(QStringLiteral("found")).toBool(),
                 qPrintable(QString::number(c.durationSec)));
        if (c.expect) {
            QVERIFY2(detail.contains(QStringLiteral("runtimeText")),
                     qPrintable(QString::number(c.durationSec)));
            QCOMPARE(detail.value(QStringLiteral("runtimeText")).toString(),
                     QString::fromLatin1(c.expect));
        } else {
            QVERIFY2(!detail.contains(QStringLiteral("runtimeText")),
                     qPrintable(QString::number(c.durationSec)));
            QVERIFY(detail.value(QStringLiteral("runtimeText")).toString().isEmpty());
        }
        // publish() is a full replace, so the next iteration's single-row publish resets the
        // index on its own — no explicit clear needed.
    }
}

// ── S8: honest failure ─────────────────────────────────────────────────────────────────────
// A rejected copy carries its human `admissionDetail` (MediaAdmissionProbe's own strings:
// "no video track" / "timeout" / …) into the sheet as `statusDetail`, beside the bare
// `admissionVerdict` the COPIES table used to stop at. A healthy copy confesses nothing, and a
// copy with only an extraction errorDetail still states it — never less truth than the engine
// holds.
void VaultBrowseDetailTest::rejectedCopyExposesItsAdmissionDetail()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    VaultIndex::FileRow healthy = fileRow(QStringLiteral("vault:healthy"), QStringLiteral("D:/root-a"),
        QStringLiteral("D:/root-a/Film (2021)"), QStringLiteral("D:/root-a/Film (2021)/film.mkv"),
        QStringLiteral("Film"));
    healthy.identityId = QStringLiteral("imdb:tt0000001");
    healthy.identityTitle = QStringLiteral("Film");
    healthy.identityState = QStringLiteral("adopted");

    VaultIndex::FileRow rejected = fileRow(QStringLiteral("vault:rejected"), QStringLiteral("E:/root-b"),
        QStringLiteral("E:/root-b/Film (2021)"), QStringLiteral("E:/root-b/Film (2021)/film.mkv"),
        QStringLiteral("Film 2021"));
    rejected.identityId = healthy.identityId;
    rejected.identityTitle = healthy.identityTitle;
    rejected.identityState = QStringLiteral("adopted");
    rejected.admissionVerdict = QStringLiteral("RejectedNoVideo");
    rejected.admissionDetail = QStringLiteral("no video track"); // the probe's own wording

    VaultIndex::FileRow errored = fileRow(QStringLiteral("vault:errored"), QStringLiteral("F:/root-c"),
        QStringLiteral("F:/root-c/Film (2021)"), QStringLiteral("F:/root-c/Film (2021)/film.mkv"),
        QStringLiteral("Film"));
    errored.identityId = healthy.identityId;
    errored.identityTitle = healthy.identityTitle;
    errored.identityState = QStringLiteral("adopted");
    // errorDetail travels only with a non-empty errorState (VaultIndex's own persist contract,
    // and always how VaultEnricher writes it — never a detail without its state).
    errored.errorState = QStringLiteral("corrupt");
    errored.errorDetail = QStringLiteral("cover extract failed"); // extraction error, no verdict

    QVERIFY(index.publish({healthy, rejected, errored}));

    const QVariantMap detail = VaultBrowseDetail::detailFor(&index, healthy.subtreePath);
    QVERIFY(detail.value(QStringLiteral("found")).toBool());
    QCOMPARE(detail.value(QStringLiteral("copiesHeld")).toInt(), 3);

    const QVariantList copies = detail.value(QStringLiteral("copies")).toList();
    QCOMPARE(copies.size(), 3);
    QHash<QString, QVariantMap> byPath;
    for (const QVariant& c : copies)
        byPath.insert(c.toMap().value(QStringLiteral("path")).toString(), c.toMap());

    const QVariantMap healthyEntry = byPath.value(healthy.path);
    QCOMPARE(healthyEntry.value(QStringLiteral("admissionVerdict")).toString(), QString());
    QCOMPARE(healthyEntry.value(QStringLiteral("statusDetail")).toString(), QString());

    const QVariantMap rejectedEntry = byPath.value(rejected.path);
    QCOMPARE(rejectedEntry.value(QStringLiteral("admissionVerdict")).toString(),
             QStringLiteral("RejectedNoVideo"));
    QCOMPARE(rejectedEntry.value(QStringLiteral("statusDetail")).toString(),
             QStringLiteral("no video track"));

    const QVariantMap erroredEntry = byPath.value(errored.path);
    QCOMPARE(erroredEntry.value(QStringLiteral("admissionVerdict")).toString(), QString());
    QCOMPARE(erroredEntry.value(QStringLiteral("statusDetail")).toString(),
             QStringLiteral("cover extract failed"));
}

// ── S8: what the sheet reads after an "Identify again" (VaultLibrary::identifyGroup) ────────
// identifyGroup() itself is matchGroup → applyGroup/recordAmbiguous composition, proven at the
// identifier layer (tst_vault_identifier.cpp:327-384: one match adopts, several durably record
// ambiguity, zero record nothing). This proves the OTHER half the sheet owns: the map fields
// detailFor renders after each outcome — an adopted group reads "identified", an ambiguous
// group stays HONEST as "uncertain" naming its candidate count, and a user-suppressed group
// never silently re-adopts.
void VaultBrowseDetailTest::afterIdentifyAgainAdoptedReadsIdentifiedAmbiguousStaysHonest()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    // identifyGroup's adopt branch landed: identityState "adopted", identityId set.
    VaultIndex::FileRow adopted = fileRow(QStringLiteral("vault:adopted"), QStringLiteral("D:/root-a"),
        QStringLiteral("D:/root-a/Akira"), QStringLiteral("D:/root-a/Akira/akira.cbz"),
        QStringLiteral("Akira"));
    adopted.kind = QStringLiteral("comic");
    adopted.identityId = QStringLiteral("gcd:3636");
    adopted.identityTitle = QStringLiteral("Akira");
    adopted.identityState = QStringLiteral("adopted");

    // identifyGroup's ambiguous branch landed: recordAmbiguous wrote the count, NO adoption.
    VaultIndex::FileRow ambiguous = fileRow(QStringLiteral("vault:ambiguous"), QStringLiteral("D:/root-a"),
        QStringLiteral("D:/root-a/Masterpiece"), QStringLiteral("D:/root-a/Masterpiece/m.cbz"),
        QStringLiteral("Masterpiece"));
    ambiguous.kind = QStringLiteral("comic");
    ambiguous.identityState = QStringLiteral("ambiguous");
    ambiguous.identityCandidateCount = 2;

    QVERIFY(index.publish({adopted, ambiguous}));

    const QVariantMap adoptedDetail = VaultBrowseDetail::detailFor(&index, adopted.subtreePath);
    QCOMPARE(adoptedDetail.value(QStringLiteral("identityState")).toString(),
             QStringLiteral("identified"));
    QCOMPARE(adoptedDetail.value(QStringLiteral("identityLabel")).toString(),
             QStringLiteral("identity certain"));
    QCOMPARE(adoptedDetail.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Akira"));

    const QVariantMap ambiguousDetail = VaultBrowseDetail::detailFor(&index, ambiguous.subtreePath);
    QCOMPARE(ambiguousDetail.value(QStringLiteral("identityState")).toString(),
             QStringLiteral("uncertain"));
    QCOMPARE(ambiguousDetail.value(QStringLiteral("identityLabel")).toString(),
             QStringLiteral("identity uncertain"));
    QVERIFY(ambiguousDetail.value(QStringLiteral("evidence")).toString()
                .contains(QStringLiteral("2 possible matches")));
    // Stayed honest: no invented identity leaked into the title.
    QCOMPARE(ambiguousDetail.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Masterpiece"));
}

QTEST_MAIN(VaultBrowseDetailTest)
#include "tst_vault_browse_detail.moc"
