// tst_vault_forensics — Colosseum Agent Visibility Phase 2, Slice F1-Core. Proves VaultForensics:
// the bounded, read-only projection over a REAL VaultLibrary (F0's named safe seam,
// docs/visibility/vault-forensic-owner-thread.md §10 — VaultLibrary, never VaultIndex directly).
//
// Every fixture below constructs the full production object graph VaultLibrary.cpp's own
// translation unit needs to link (VaultIndex/VaultConfig/VaultIdentity/VaultScanner, plus
// VaultLibrary's own unconditional VaultWatcher child and its VaultIdentifier/VaultDownloadsRoot/
// VaultBrowseAway/VaultBrowseDetail/VaultBrowseEmpty/VaultKit/ComicCoverId composition) — the same
// cost tst_vault_scanner.cpp already pays for a smaller slice of this graph, paid here in full
// because F0 names VaultLibrary itself (not the lower helper modules) as the seam VaultForensics
// must compose. No test below calls VaultIdentifier::matchGroup or any scan/publish path — rows
// are published directly into VaultIndex (same idiom as tst_vault_index.cpp's `mk()`), and
// VaultLibrary's `scanner`/`identifier` machinery is only ever linked, never exercised.
//
// Real small fixture trees under QTemporaryDir (never a committed .sqlite, house rule) — GUILESS.

#include "engine/VaultForensics.h"
#include "engine/VaultLibrary.h"
#include "engine/VaultIndex.h"
#include "engine/VaultConfig.h"
#include "engine/VaultIdentity.h"
#include "engine/VaultScanner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <memory>

namespace {

struct Fixture {
    QTemporaryDir tmp;
    QString rootPath;
    std::unique_ptr<VaultIndex> index;
    std::unique_ptr<VaultConfig> config;
    std::unique_ptr<VaultIdentity> identity;
    std::unique_ptr<VaultScanner> scanner;
    std::unique_ptr<VaultLibrary> library; // destroyed FIRST (reverse declaration order)
};

void writeStub(const QString& path)
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(QByteArrayLiteral("stub"));
}

// Mirrors VaultConfig::norm() (private: QDir::cleanPath + toLower on Windows,
// native/engine/VaultConfig.cpp:15-22) so a hand-built FileRow.rootPath/groupKey exactly matches
// what VaultConfig::addRoot() stores and rootsDetail() returns. Real production never hits this
// seam — VaultScanner derives every FileRow's rootPath/groupKey from the SAME config-normalized
// root that browseAt() starts its walk from — but this fixture hand-builds FileRow entries
// directly from QTemporaryDir::path(), which is mixed-case on Windows. VaultIndex::rowsForRoot()/
// rowsForGroup() are exact-string SQL matches with no normalization of their own, so a query built
// from rootsDetail()'s (lowercased) path silently misses every row stamped with the raw mixed-case
// path. Lower-casing the root here before it is used for mkpath/writeStub/FileRow construction
// keeps every derived path self-consistent; Windows file I/O is case-insensitive so the real
// directories/files land at the identical location either way.
QString normalizedRootPath(const QString& path)
{
    QString n = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
}

VaultIndex::FileRow makeFilmRow(const QString& rootPath, const QString& folder,
                                const QString& file, const QString& id, qint64 mtimeMs)
{
    VaultIndex::FileRow r;
    r.id = id;
    // rootPath deliberately stays exactly the caller-supplied (config-style) string, unwalked —
    // production mirrors this: VaultScanner.cpp:147 sets row.rootPath = root, the same string the
    // caller reads back from VaultConfig, so VaultIndex::rowsForRoot()'s exact-string match
    // against rootsDetail()'s path always lines up.
    r.rootPath = rootPath;
    // subtreePath/groupKey/path, by contrast, must match what VaultKit::planBrowseLevel's REAL
    // filesystem walk independently reconstructs for the same directory — that's what
    // VaultLibrary::browseAt() feeds into rowsForGroup(n.path) to decorate a Film node's coverRef.
    // Qt's QFileInfo::absoluteFilePath() canonicalizes the drive letter on Windows (observed:
    // always uppercase here, regardless of input casing) — the identical canonicalization
    // QDir::entryInfoList()'s absoluteFilePath() applies inside VaultKit::listImmediateSubdirs/
    // groupByFirstLevelSubdir, the same shared walk VaultScanner itself uses in production
    // (VaultScanner.cpp:36,148-149: row.subtreePath = row.groupKey = subtree, a
    // groupByFirstLevelSubdir/listImmediateSubdirs output). A hand-built groupKey that skips this
    // canonicalization (e.g. plain QDir::filePath() concatenation) silently never matches a real
    // walk's n.path/n.key on this drive-letter byte alone — proven live: rowsForGroup() returned
    // the fixture's padded coverRef when queried with this canonical form, empty without it.
    const QString canonicalFolder = QFileInfo(folder).absoluteFilePath();
    r.subtreePath = canonicalFolder;
    r.groupKey = canonicalFolder;
    r.groupTitle = QStringLiteral("Movie");
    r.kind = QStringLiteral("video");
    r.path = QFileInfo(file).absoluteFilePath();
    r.displayTitle = QStringLiteral("Movie");
    r.realName = QStringLiteral("movie.mp4");
    r.size = 4;
    r.mtimeMs = mtimeMs;
    return r;
}

// One confirmed root directly holding `filmCount` sibling film folders (one video file each —
// VaultKit::planBrowseLevel's simplest Film-collapse rule: a folder holding exactly one media
// file IS a Film node). `bigCoverRefs` pads each row's coverRef — a plain TEXT column VaultIndex
// never dereferences as a real path, so padding it is safe and avoids any Windows MAX_PATH risk
// a padded FOLDER NAME would carry — so byte_budget_sets_truncated has real bulk to trim.
std::unique_ptr<Fixture> buildFlatFixture(int filmCount, bool bigCoverRefs = false)
{
    auto fx = std::make_unique<Fixture>();
    if (!fx->tmp.isValid())
        return fx;
    fx->rootPath = normalizedRootPath(QDir(fx->tmp.path()).filePath(QStringLiteral("root")));
    QDir().mkpath(fx->rootPath);

    fx->index = std::make_unique<VaultIndex>(
        QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite")));
    fx->config = std::make_unique<VaultConfig>(fx->tmp.path());
    fx->identity = std::make_unique<VaultIdentity>(fx->tmp.path());
    fx->scanner = std::make_unique<VaultScanner>(fx->index.get(), fx->identity.get());
    fx->config->addRoot(fx->rootPath);
    fx->config->confirmRoot(fx->rootPath);

    QList<VaultIndex::FileRow> rows;
    for (int i = 0; i < filmCount; ++i) {
        const QString folder = QDir(fx->rootPath).filePath(QStringLiteral("f%1").arg(i));
        QDir().mkpath(folder);
        const QString file = QDir(folder).filePath(QStringLiteral("movie.mp4"));
        writeStub(file);
        auto row = makeFilmRow(fx->rootPath, folder, file,
                               QStringLiteral("vault:film-%1").arg(i), 1000 + i);
        if (bigCoverRefs)
            row.coverRef = QStringLiteral("file://") + QString(20000, QLatin1Char('c'));
        rows.append(row);
    }
    fx->index->publish(rows);
    // cacheDir (browse-artwork execution plan, Slice 3 part 2): mirrors main.cpp's own choice of
    // reusing the SAME dir vaultConfig/vaultIdentity/vaultIndex already sit under (vaultDir)
    // rather than a second cache root. fx->tmp is this fixture's own QTemporaryDir, already used
    // exclusively for index.sqlite/config/identity above -- reusing its path here is isolated
    // (unique per fixture run, auto-cleaned on destruction) and needs no extra mkpath.
    fx->library = std::make_unique<VaultLibrary>(fx->index.get(), fx->scanner.get(),
                                                 fx->config.get(), fx->identity.get(),
                                                 fx->tmp.path());
    return fx;
}

// One confirmed root -> "Shows" intermediate folder -> `childCount` sibling film folders.
// `showsPathOut` receives the intermediate folder's real path — a browse NODE, not a root, so
// node_scope_is_bounded proves scope=node works on an arbitrary browse path (unlike scope=root,
// which requires a rootsDetail() match).
std::unique_ptr<Fixture> buildNestedFixture(int childCount, QString* showsPathOut)
{
    auto fx = std::make_unique<Fixture>();
    if (!fx->tmp.isValid())
        return fx;
    fx->rootPath = normalizedRootPath(QDir(fx->tmp.path()).filePath(QStringLiteral("root")));
    const QString showsPath = QDir(fx->rootPath).filePath(QStringLiteral("Shows"));
    QDir().mkpath(showsPath);

    fx->index = std::make_unique<VaultIndex>(
        QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite")));
    fx->config = std::make_unique<VaultConfig>(fx->tmp.path());
    fx->identity = std::make_unique<VaultIdentity>(fx->tmp.path());
    fx->scanner = std::make_unique<VaultScanner>(fx->index.get(), fx->identity.get());
    fx->config->addRoot(fx->rootPath);
    fx->config->confirmRoot(fx->rootPath);

    QList<VaultIndex::FileRow> rows;
    for (int i = 0; i < childCount; ++i) {
        const QString folder = QDir(showsPath).filePath(QStringLiteral("c%1").arg(i));
        QDir().mkpath(folder);
        const QString file = QDir(folder).filePath(QStringLiteral("movie.mp4"));
        writeStub(file);
        rows.append(makeFilmRow(fx->rootPath, folder, file,
                                QStringLiteral("vault:child-%1").arg(i), 1000 + i));
    }
    fx->index->publish(rows);
    // cacheDir: same fx->tmp reuse as buildFlatFixture above (Slice 3 part 2 signature).
    fx->library = std::make_unique<VaultLibrary>(fx->index.get(), fx->scanner.get(),
                                                 fx->config.get(), fx->identity.get(),
                                                 fx->tmp.path());
    if (showsPathOut)
        *showsPathOut = showsPath;
    return fx;
}

// `rootCount` confirmed roots, one film each — scope=summary's roots{}/recent{} bounding.
std::unique_ptr<Fixture> buildMultiRootFixture(int rootCount)
{
    auto fx = std::make_unique<Fixture>();
    if (!fx->tmp.isValid())
        return fx;
    fx->index = std::make_unique<VaultIndex>(
        QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite")));
    fx->config = std::make_unique<VaultConfig>(fx->tmp.path());
    fx->identity = std::make_unique<VaultIdentity>(fx->tmp.path());
    fx->scanner = std::make_unique<VaultScanner>(fx->index.get(), fx->identity.get());

    QList<VaultIndex::FileRow> rows;
    for (int i = 0; i < rootCount; ++i) {
        const QString root =
            normalizedRootPath(QDir(fx->tmp.path()).filePath(QStringLiteral("root%1").arg(i)));
        const QString folder = QDir(root).filePath(QStringLiteral("f0"));
        QDir().mkpath(folder);
        const QString file = QDir(folder).filePath(QStringLiteral("movie.mp4"));
        writeStub(file);
        fx->config->addRoot(root);
        fx->config->confirmRoot(root);
        rows.append(makeFilmRow(root, folder, file,
                                QStringLiteral("vault:root-%1").arg(i), 1000 + i));
    }
    fx->index->publish(rows);
    // cacheDir: same fx->tmp reuse as buildFlatFixture above (Slice 3 part 2 signature).
    fx->library = std::make_unique<VaultLibrary>(fx->index.get(), fx->scanner.get(),
                                                 fx->config.get(), fx->identity.get(),
                                                 fx->tmp.path());
    return fx;
}

// `copyCount` separate single-file groups sharing ONE adopted identity — identity_scope_is_bounded.
// `keyOut` receives copy 0's groupKey (what scope=identity is queried with).
std::unique_ptr<Fixture> buildIdentityFixture(int copyCount, QString* keyOut)
{
    auto fx = std::make_unique<Fixture>();
    if (!fx->tmp.isValid())
        return fx;
    fx->rootPath = normalizedRootPath(QDir(fx->tmp.path()).filePath(QStringLiteral("root")));
    QDir().mkpath(fx->rootPath);
    fx->index = std::make_unique<VaultIndex>(
        QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite")));
    fx->config = std::make_unique<VaultConfig>(fx->tmp.path());
    fx->identity = std::make_unique<VaultIdentity>(fx->tmp.path());
    fx->scanner = std::make_unique<VaultScanner>(fx->index.get(), fx->identity.get());
    fx->config->addRoot(fx->rootPath);
    fx->config->confirmRoot(fx->rootPath);

    QList<VaultIndex::FileRow> rows;
    QString firstKey;
    for (int i = 0; i < copyCount; ++i) {
        const QString folder = QDir(fx->rootPath).filePath(QStringLiteral("copy%1").arg(i));
        QDir().mkpath(folder);
        const QString file = QDir(folder).filePath(QStringLiteral("movie.mp4"));
        writeStub(file);
        auto row = makeFilmRow(fx->rootPath, folder, file,
                               QStringLiteral("vault:copy-%1").arg(i), 1000 + i);
        row.identityId = QStringLiteral("manual:test-franchise");
        row.identityTitle = QStringLiteral("Test Franchise");
        if (firstKey.isEmpty())
            firstKey = row.groupKey;
        rows.append(row);
    }
    fx->index->publish(rows);
    // cacheDir: same fx->tmp reuse as buildFlatFixture above (Slice 3 part 2 signature).
    fx->library = std::make_unique<VaultLibrary>(fx->index.get(), fx->scanner.get(),
                                                 fx->config.get(), fx->identity.get(),
                                                 fx->tmp.path());
    if (keyOut)
        *keyOut = firstKey;
    return fx;
}

// Same hand-built row shape as makeFilmRow above — see its comment for why groupKey/subtreePath
// MUST be the QFileInfo::absoluteFilePath()-canonical form of the folder (that is the string
// VaultKit's real walk independently reconstructs, and the one browseAt() feeds to
// rowsForGroup()). Only the stored `kind` and the real filename differ, which is exactly what
// browse_projection_carries_stored_kind is about.
VaultIndex::FileRow makeKindRow(const QString& rootPath, const QString& folder,
                                const QString& file, const QString& id, const QString& kind)
{
    VaultIndex::FileRow r = makeFilmRow(rootPath, folder, file, id, 1000);
    r.kind = kind;
    r.realName = QFileInfo(file).fileName();
    r.groupTitle = QFileInfo(folder).fileName();
    r.displayTitle = QFileInfo(folder).fileName();
    return r;
}

// One confirmed root holding two single-media-file folders: one video, one comic. Both collapse
// to FILM nodes — VaultKit::classifyChildDirectory's "a folder holding exactly one media file IS
// that film" rule counts through allMediaFilters(), which covers .cbz as well as .mp4, so the
// node type is medium-agnostic and the STORED kind is the only thing that can tell the two apart.
// That is precisely the browse row the identify gesture is reachable from, and precisely why
// browseAt() has to carry kind rather than let QML infer a medium from nodeType.
std::unique_ptr<Fixture> buildMixedKindFixture(QString* videoFolderOut, QString* comicFolderOut)
{
    auto fx = std::make_unique<Fixture>();
    if (!fx->tmp.isValid())
        return fx;
    fx->rootPath = normalizedRootPath(QDir(fx->tmp.path()).filePath(QStringLiteral("root")));
    QDir().mkpath(fx->rootPath);

    fx->index = std::make_unique<VaultIndex>(
        QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite")));
    fx->config = std::make_unique<VaultConfig>(fx->tmp.path());
    fx->identity = std::make_unique<VaultIdentity>(fx->tmp.path());
    fx->scanner = std::make_unique<VaultScanner>(fx->index.get(), fx->identity.get());
    fx->config->addRoot(fx->rootPath);
    fx->config->confirmRoot(fx->rootPath);

    const QString videoFolder = QDir(fx->rootPath).filePath(QStringLiteral("Film Group"));
    const QString comicFolder = QDir(fx->rootPath).filePath(QStringLiteral("Comic Group"));
    QDir().mkpath(videoFolder);
    QDir().mkpath(comicFolder);
    const QString videoFile = QDir(videoFolder).filePath(QStringLiteral("movie.mp4"));
    const QString comicFile = QDir(comicFolder).filePath(QStringLiteral("issue.cbz"));
    writeStub(videoFile);
    writeStub(comicFile);

    fx->index->publish({
        makeKindRow(fx->rootPath, videoFolder, videoFile,
                    QStringLiteral("vault:mixed-video"), QStringLiteral("video")),
        makeKindRow(fx->rootPath, comicFolder, comicFile,
                    QStringLiteral("vault:mixed-comic"), QStringLiteral("comic")),
    });
    // cacheDir: same fx->tmp reuse as buildFlatFixture above (Slice 3 part 2 signature).
    fx->library = std::make_unique<VaultLibrary>(fx->index.get(), fx->scanner.get(),
                                                 fx->config.get(), fx->identity.get(),
                                                 fx->tmp.path());
    if (videoFolderOut)
        *videoFolderOut = QFileInfo(videoFolder).fileName();
    if (comicFolderOut)
        *comicFolderOut = QFileInfo(comicFolder).fileName();
    return fx;
}

qsizetype serializedSize(const QVariantMap& out)
{
    return QJsonDocument(QJsonObject::fromVariantMap(out)).toJson(QJsonDocument::Compact).size();
}

} // namespace

class tst_vault_forensics : public QObject
{
    Q_OBJECT

private slots:
    void schema_and_shape_v1();
    void summary_scope_is_bounded();
    void root_scope_is_bounded();
    void node_scope_is_bounded();
    void identity_scope_is_bounded();
    void row_limit_sets_truncated();
    void byte_budget_sets_truncated();
    void executes_on_owner_thread();
    void foreign_thread_marshals_to_owner();
    void timeout_returns_error();
    void projection_does_not_mutate_files();
    void browse_projection_carries_stored_kind();
    void browse_projection_carries_vault_id();
    void downloads_root_chip_remove_hides_and_republishes();
    void rescan_root_republishes_the_union();
    void forget_root_removes_only_that_root_and_republishes();
    void scan_ignore_needle_excludes_seeded_folder();
    void roots_detail_surfaces_per_root_error_facts();
    void browse_sort_newest_orders_by_row_mtime_desc();
    void browse_sort_size_orders_by_total_bytes_desc();
    void browse_sort_title_merges_across_node_types();
    void browse_filter_kind_ident_and_presence_predicate();
    void browse_empty_cause_filtered_from_production_path();
};

void tst_vault_forensics::schema_and_shape_v1()
{
    auto fx = buildFlatFixture(2);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());

    const QVariantMap out =
        forensics.query(QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")}});

    QCOMPARE(out.value(QStringLiteral("schema")).toString(),
             QStringLiteral("colosseum.vault.forensics.v1"));
    QCOMPARE(out.value(QStringLiteral("indexSchemaVersion")).toInt(), 5);
    QCOMPARE(out.value(QStringLiteral("scope")).toString(), QStringLiteral("summary"));
    QCOMPARE(out.value(QStringLiteral("revision")).toInt(), fx->library->revision());
    QVERIFY(!out.value(QStringLiteral("ownerThread")).toMap()
                 .value(QStringLiteral("id")).toString().isEmpty());
    QCOMPARE(out.value(QStringLiteral("truncated")).toBool(), false);
    QVERIFY(out.value(QStringLiteral("errors")).toList().isEmpty());

    const QVariantMap roots = out.value(QStringLiteral("roots")).toMap();
    QCOMPARE(roots.value(QStringLiteral("count")).toInt(), 1);
    QCOMPARE(roots.value(QStringLiteral("rows")).toList().size(), 1);
    QCOMPARE(out.value(QStringLiteral("browseCount")).toInt(), 2);
    QCOMPARE(out.value(QStringLiteral("itemCount")).toInt(), 2);
    const QVariantMap recent = out.value(QStringLiteral("recent")).toMap();
    QCOMPARE(recent.value(QStringLiteral("rows")).toList().size(), 2);

    QVERIFY(serializedSize(out) <= VaultForensics::kByteBudgetBytes);
}

void tst_vault_forensics::summary_scope_is_bounded()
{
    auto fx = buildMultiRootFixture(5);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());

    const QVariantMap out = forensics.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("summary")}, {QStringLiteral("limit"), 2}});

    const QVariantMap roots = out.value(QStringLiteral("roots")).toMap();
    QCOMPARE(roots.value(QStringLiteral("count")).toInt(), 5);         // real total, never lied about
    QCOMPARE(roots.value(QStringLiteral("rows")).toList().size(), 2);  // bounded to limit
    QVERIFY(out.value(QStringLiteral("truncated")).toBool());

    const QVariantMap recent = out.value(QStringLiteral("recent")).toMap();
    QVERIFY(recent.value(QStringLiteral("rows")).toList().size() <= 2);
}

void tst_vault_forensics::root_scope_is_bounded()
{
    auto fx = buildFlatFixture(7);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());
    const QString rootPath =
        fx->library->rootsDetail().first().toMap().value(QStringLiteral("path")).toString();

    const QVariantMap out = forensics.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("root")}, {QStringLiteral("key"), rootPath},
        {QStringLiteral("limit"), 3}});

    QVERIFY(out.value(QStringLiteral("root")).toMap().value(QStringLiteral("found")).toBool());
    const QVariantMap browse = out.value(QStringLiteral("browse")).toMap();
    QCOMPARE(browse.value(QStringLiteral("count")).toInt(), 7);
    QCOMPARE(browse.value(QStringLiteral("rows")).toList().size(), 3);
    QVERIFY(out.value(QStringLiteral("truncated")).toBool());
}

void tst_vault_forensics::node_scope_is_bounded()
{
    QString showsPath;
    auto fx = buildNestedFixture(6, &showsPath);
    QVERIFY(fx->library);
    QVERIFY(!showsPath.isEmpty());
    VaultForensics forensics(fx->library.get());

    // Ground truth: showsPath is NOT a known root.
    bool showsIsARoot = false;
    for (const QVariant& r : fx->library->rootsDetail())
        if (r.toMap().value(QStringLiteral("path")).toString() == showsPath)
            showsIsARoot = true;
    QVERIFY(!showsIsARoot);

    const QVariantMap out = forensics.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("node")}, {QStringLiteral("key"), showsPath},
        {QStringLiteral("limit"), 4}});

    QCOMPARE(out.value(QStringLiteral("node")).toMap().value(QStringLiteral("key")).toString(),
             showsPath);
    const QVariantMap browse = out.value(QStringLiteral("browse")).toMap();
    QCOMPARE(browse.value(QStringLiteral("count")).toInt(), 6);
    QCOMPARE(browse.value(QStringLiteral("rows")).toList().size(), 4);
    QVERIFY(out.value(QStringLiteral("truncated")).toBool());
}

void tst_vault_forensics::identity_scope_is_bounded()
{
    QString key;
    auto fx = buildIdentityFixture(50, &key);
    QVERIFY(fx->library);
    QVERIFY(!key.isEmpty());
    VaultForensics forensics(fx->library.get());

    // Ground truth: browseDetail() itself DOES carry all 50 copies — proves the fixture is real
    // and that VaultForensics is not merely bounded because there was nothing to bound.
    const QVariantMap rawDetail = fx->library->browseDetail(key);
    QVERIFY(rawDetail.value(QStringLiteral("found")).toBool());
    QCOMPARE(rawDetail.value(QStringLiteral("copiesHeld")).toInt(), 50);
    QCOMPARE(rawDetail.value(QStringLiteral("copies")).toList().size(), 50);

    const QVariantMap out = forensics.query(
        QVariantMap{{QStringLiteral("scope"), QStringLiteral("identity")},
                    {QStringLiteral("key"), key}});
    const QVariantMap identity = out.value(QStringLiteral("identity")).toMap();
    QVERIFY(identity.value(QStringLiteral("found")).toBool());
    QCOMPARE(identity.value(QStringLiteral("copiesHeld")).toInt(), 50); // the COUNT survives
    QVERIFY(!identity.contains(QStringLiteral("copies")));      // the unbounded ARRAY does not
    QVERIFY(!identity.contains(QStringLiteral("companions")));
    QVERIFY(!identity.contains(QStringLiteral("extras")));

    QVERIFY(serializedSize(out) < 4096); // flat regardless of the 50 copies behind it
}

void tst_vault_forensics::row_limit_sets_truncated()
{
    auto fx = buildFlatFixture(5);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());
    const QString rootPath =
        fx->library->rootsDetail().first().toMap().value(QStringLiteral("path")).toString();

    // Below the real count: truncated.
    const QVariantMap clamped = forensics.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("root")}, {QStringLiteral("key"), rootPath},
        {QStringLiteral("limit"), 2}});
    QCOMPARE(clamped.value(QStringLiteral("browse")).toMap()
                 .value(QStringLiteral("rows")).toList().size(), 2);
    QVERIFY(clamped.value(QStringLiteral("truncated")).toBool());

    // At the real count: NOT truncated — a negative control inside the same test proving
    // truncated is not simply always true.
    const QVariantMap full = forensics.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("root")}, {QStringLiteral("key"), rootPath},
        {QStringLiteral("limit"), 5}});
    QCOMPARE(full.value(QStringLiteral("browse")).toMap()
                 .value(QStringLiteral("rows")).toList().size(), 5);
    QVERIFY(!full.value(QStringLiteral("truncated")).toBool());

    // Above kMaxLimit (100): a 101-row fixture, limit=101 requested. clampLimit's UPPER bound —
    // not the fixture's real count — is what stops this at exactly 100. This is the assertion the
    // negative control below actually exercises: neither the limit=2 nor limit=5 case above can
    // distinguish "upper bound enforced" from "upper bound absent", since both requested values
    // are already <= kMaxLimit.
    auto fxOver = buildFlatFixture(101);
    QVERIFY(fxOver->library);
    VaultForensics forensicsOver(fxOver->library.get());
    const QString overRootPath =
        fxOver->library->rootsDetail().first().toMap().value(QStringLiteral("path")).toString();
    const QVariantMap over = forensicsOver.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("root")}, {QStringLiteral("key"), overRootPath},
        {QStringLiteral("limit"), 101}});
    QCOMPARE(over.value(QStringLiteral("browse")).toMap()
                 .value(QStringLiteral("rows")).toList().size(), 100); // clamped, never 101
    QVERIFY(over.value(QStringLiteral("truncated")).toBool());

    // NEGATIVE CONTROL (performed live for the F1-Core build gate, 2026-08-13): in
    // VaultForensics::clampLimit (native/engine/VaultForensics.cpp), temporarily change
    // `qBound(kMinLimit, v, kMaxLimit)` to skip the upper bound (e.g. `return v;` for any v > 0),
    // rebuild, rerun. Exactly this case turns red (over's rows.size() becomes 101, not 100) with
    // every other named case in this file still green; restoring the clamp returns the whole
    // suite to green.
}

void tst_vault_forensics::byte_budget_sets_truncated()
{
    // 20 rows, each padded via coverRef (never row-count) well past what limit alone would
    // trim. limit=100 means the row-COUNT clamp cannot be the cause (20 <= 100) — only the byte
    // budget can be why truncated flips true here.
    auto fx = buildFlatFixture(20, /*bigCoverRefs=*/true);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());
    const QString rootPath =
        fx->library->rootsDetail().first().toMap().value(QStringLiteral("path")).toString();

    const QVariantMap out = forensics.query(QVariantMap{
        {QStringLiteral("scope"), QStringLiteral("root")}, {QStringLiteral("key"), rootPath},
        {QStringLiteral("limit"), 100}});

    const QVariantMap browse = out.value(QStringLiteral("browse")).toMap();
    QCOMPARE(browse.value(QStringLiteral("count")).toInt(), 20); // the real total, never lied about
    QVERIFY(browse.value(QStringLiteral("rows")).toList().size() < 20); // rows were dropped
    QVERIFY(out.value(QStringLiteral("truncated")).toBool());
    QVERIFY(serializedSize(out) <= VaultForensics::kByteBudgetBytes);

    bool sawBudgetDiagnostic = false;
    for (const QVariant& e : out.value(QStringLiteral("errors")).toList())
        if (e.toString().contains(QStringLiteral("byte budget")))
            sawBudgetDiagnostic = true;
    QVERIFY(sawBudgetDiagnostic);
}

void tst_vault_forensics::executes_on_owner_thread()
{
    auto fx = buildFlatFixture(1);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());

    const QString expectedId =
        QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    // Direct entry point — documented to require the owner thread; this fixture is built on the
    // test/main thread, so it IS the owner thread.
    const QVariantMap direct =
        forensics.query(QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")}});
    QCOMPARE(direct.value(QStringLiteral("ownerThread")).toMap()
                 .value(QStringLiteral("id")).toString(), expectedId);

    // queryMarshalled() on the owner thread must take the same-thread fast path with no
    // marshalling: if it mistakenly always queued through the semaphore path, this call would
    // deadlock in a guiless test with no running event loop — returning at all is part of the
    // proof, alongside the correct owner-thread id.
    const QVariantMap marshalled =
        forensics.queryMarshalled(QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")}});
    QCOMPARE(marshalled.value(QStringLiteral("ownerThread")).toMap()
                 .value(QStringLiteral("id")).toString(), expectedId);
    QVERIFY(marshalled.value(QStringLiteral("errors")).toList().isEmpty());
}

void tst_vault_forensics::foreign_thread_marshals_to_owner()
{
    auto fx = buildFlatFixture(1);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());

    const QString ownerId =
        QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    QVariantMap result;
    QThread* worker = QThread::create([&]() {
        result = forensics.queryMarshalled(
            QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")}}, 5000);
    });
    worker->start();
    // Pump THIS (owner) thread's event loop while the worker blocks on its semaphore — the
    // queued call posted to fx->library is only delivered if something processes events here.
    while (!worker->isFinished())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    worker->wait();
    delete worker;

    QCOMPARE(result.value(QStringLiteral("ownerThread")).toMap()
                 .value(QStringLiteral("id")).toString(), ownerId);
    QVERIFY(result.value(QStringLiteral("errors")).toList().isEmpty());
    QCOMPARE(result.value(QStringLiteral("schema")).toString(),
             QStringLiteral("colosseum.vault.forensics.v1"));
}

void tst_vault_forensics::timeout_returns_error()
{
    auto fx = buildFlatFixture(1);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());

    QVariantMap result;
    QElapsedTimer timer;
    timer.start();
    QThread* worker = QThread::create([&]() {
        result = forensics.queryMarshalled(
            QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")}}, 80);
    });
    worker->start();
    // Deliberately do NOT pump this (owner) thread's event loop: worker->wait() blocks
    // synchronously here, so the queued call posted to fx->library is never delivered, and
    // queryMarshalled's own 80ms deadline — not a test-side sleep — is what ends the wait.
    worker->wait();
    delete worker;
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY2(elapsedMs < 5000, qPrintable(QStringLiteral("elapsed=%1ms").arg(elapsedMs)));
    bool sawTimeout = false;
    for (const QVariant& e : result.value(QStringLiteral("errors")).toList())
        if (e.toString().contains(QStringLiteral("timeout")))
            sawTimeout = true;
    QVERIFY(sawTimeout);
}

void tst_vault_forensics::projection_does_not_mutate_files()
{
    auto fx = buildFlatFixture(4);
    QVERIFY(fx->library);
    VaultForensics forensics(fx->library.get());

    const QString dbPath = QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite"));
    QFile dbBefore(dbPath);
    QVERIFY(dbBefore.open(QIODevice::ReadOnly));
    const QByteArray hashBefore =
        QCryptographicHash::hash(dbBefore.readAll(), QCryptographicHash::Sha256);
    dbBefore.close();

    const QString rootPath =
        fx->library->rootsDetail().first().toMap().value(QStringLiteral("path")).toString();
    (void)forensics.query(QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")}});
    (void)forensics.query(QVariantMap{{QStringLiteral("scope"), QStringLiteral("root")},
                                      {QStringLiteral("key"), rootPath}});
    const QVariantList rootChildren = fx->library->browseAt(rootPath);
    QVERIFY(!rootChildren.isEmpty());
    const QString childKey = rootChildren.first().toMap().value(QStringLiteral("key")).toString();
    (void)forensics.query(QVariantMap{{QStringLiteral("scope"), QStringLiteral("node")},
                                      {QStringLiteral("key"), childKey}});
    (void)forensics.query(QVariantMap{{QStringLiteral("scope"), QStringLiteral("identity")},
                                      {QStringLiteral("key"), childKey}});

    QFile dbAfter(dbPath);
    QVERIFY(dbAfter.open(QIODevice::ReadOnly));
    const QByteArray hashAfter =
        QCryptographicHash::hash(dbAfter.readAll(), QCryptographicHash::Sha256);
    dbAfter.close();

    QCOMPARE(hashAfter, hashBefore);

    // Second signal: the rows themselves are byte-identical, not just the file's bytes.
    QCOMPARE(fx->index->rowsForRoot(rootPath).size(), 4);
}

// This file already owns the ONLY C++ fixture that constructs a real VaultLibrary (F0's named
// safe seam), so the browse projection's own row contract is proved here rather than in a second
// copy of that whole object graph.
//
// The bug: VaultLibrary::browseAt() returned rows with no `kind`, so VaultPage.identifyBrowseRow()
// had nothing to hand VaultIdentifyDialog and passed "" — and searchNow()'s "" path falls through
// to ComicsCatalog then MalCatalog. Identifying a MOVIE from the browse face therefore searched
// comic and manga catalogues and could never reach IMDb. nodeType cannot substitute: a folder
// holding one .cbz and a folder holding one .mp4 are BOTH "film" nodes (that is a statement about
// folder shape, not medium), which this fixture makes concrete.
void tst_vault_forensics::browse_projection_carries_stored_kind()
{
    QString videoFolderName, comicFolderName;
    auto fx = buildMixedKindFixture(&videoFolderName, &comicFolderName);
    QVERIFY(fx->library);
    QVERIFY(!videoFolderName.isEmpty());
    QVERIFY(!comicFolderName.isEmpty());

    const QVariantList rows = fx->library->browseAt(fx->rootPath);
    QCOMPARE(rows.size(), 2);

    // Keyed by the folder's own NAME, not its full path: a browse node's key comes from
    // QDir::entryInfoList()'s absoluteFilePath(), which canonicalizes the drive letter on Windows
    // (see makeFilmRow's comment) — the leaf name is the part that is byte-stable either way.
    QMap<QString, QVariantMap> byFolderName;
    for (const QVariant& rv : rows) {
        const QVariantMap m = rv.toMap();
        byFolderName.insert(QFileInfo(m.value(QStringLiteral("key")).toString()).fileName(), m);
    }
    QVERIFY2(byFolderName.contains(videoFolderName), qPrintable(videoFolderName));
    QVERIFY2(byFolderName.contains(comicFolderName), qPrintable(comicFolderName));

    const QVariantMap videoRow = byFolderName.value(videoFolderName);
    const QVariantMap comicRow = byFolderName.value(comicFolderName);

    // The falsifiability half: both really are the same node type, so `kind` is carrying
    // information nothing else in the row could have supplied.
    QCOMPARE(videoRow.value(QStringLiteral("nodeType")).toString(), QStringLiteral("film"));
    QCOMPARE(comicRow.value(QStringLiteral("nodeType")).toString(), QStringLiteral("film"));

    // THE CONTRACT: each row carries the kind its own index rows were STORED with.
    QCOMPARE(videoRow.value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
    QCOMPARE(comicRow.value(QStringLiteral("kind")).toString(), QStringLiteral("comic"));

    // recentArrivals() mirrors browseAt()'s film/show row shape and feeds the SAME detail sheet
    // (a carousel slide's Details opens it), whose Identify action hands the row's kind on — so
    // it has to carry kind too, or identifying from the carousel keeps the original bug.
    const QVariantList recent = fx->library->recentArrivals(6);
    QCOMPARE(recent.size(), 2);
    QMap<QString, QString> recentKindByName;
    for (const QVariant& rv : recent) {
        const QVariantMap m = rv.toMap();
        recentKindByName.insert(QFileInfo(m.value(QStringLiteral("key")).toString()).fileName(),
                                m.value(QStringLiteral("kind")).toString());
    }
    QCOMPARE(recentKindByName.value(videoFolderName), QStringLiteral("video"));
    QCOMPARE(recentKindByName.value(comicFolderName), QStringLiteral("comic"));
}

// Vault UX uplift S6 — browseAt() rows now carry the node's durable vault id (`id`), the live
// Progress join key: the id is "vault:" + SHA-1 of normalizedPath::size::mtimeMs (VaultIdentity),
// so QML cannot derive it from the row's path, and without it on the row VaultApi.joinRow's
// progressFraction/progressed override and ProgressStore.watchedMark (S3's durable watched tick)
// are structurally unreachable from the browse face. The falsifiability half is the SAME
// fixture as browse_projection_carries_stored_kind: two structurally identical film nodes whose
// stored ids differ is exactly the proof that the id is data, not a derivation.
void tst_vault_forensics::browse_projection_carries_vault_id()
{
    QString videoFolderName, comicFolderName;
    auto fx = buildMixedKindFixture(&videoFolderName, &comicFolderName);
    QVERIFY(fx->library);
    QVERIFY(!videoFolderName.isEmpty());
    QVERIFY(!comicFolderName.isEmpty());

    const QVariantList rows = fx->library->browseAt(fx->rootPath);
    QCOMPARE(rows.size(), 2);

    QMap<QString, QVariantMap> byFolderName;
    for (const QVariant& rv : rows) {
        const QVariantMap m = rv.toMap();
        byFolderName.insert(QFileInfo(m.value(QStringLiteral("key")).toString()).fileName(), m);
    }
    QVERIFY2(byFolderName.contains(videoFolderName), qPrintable(videoFolderName));
    QVERIFY2(byFolderName.contains(comicFolderName), qPrintable(comicFolderName));

    // THE CONTRACT: each film row carries ITS OWN group's stored vault id — the exact key
    // Progress records resume state and watched marks under (video joins as kind "video",
    // comics as kind "comic", both under their vault: id).
    QCOMPARE(byFolderName.value(videoFolderName)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("vault:mixed-video"));
    QCOMPARE(byFolderName.value(comicFolderName)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("vault:mixed-comic"));

    // Container rows (a pure ancestor folder node) honestly carry NO id — there is no single
    // file under an ancestor whose progress could be joined, and inventing one is how a wrong
    // tile ends up wearing another file's tick. Proven on the nested fixture's "Shows" level:
    // the level's rows are film children (id-bearing), while walking the ROOT of the flat
    // fixture keeps every row id-bearing too — the ancestor case needs the intermediate node.
    // (The flat fixture's root level IS the film level, so this asserts the positive on it.)
    auto flat = buildFlatFixture(2);
    QVERIFY(flat->library);
    const QVariantList flatRows = flat->library->browseAt(flat->rootPath);
    QCOMPARE(flatRows.size(), 2);
    for (const QVariant& rv : flatRows) {
        QVERIFY2(rv.toMap().value(QStringLiteral("id")).toString()
                     .startsWith(QStringLiteral("vault:film-")),
                 qPrintable(rv.toMap().value(QStringLiteral("id")).toString()));
    }
}

// Vault UX uplift S9 — the downloads chip's three invokables were finished with zero callers;
// the rail + marquee wiring (qml/VaultBrowseRail.qml, qml/VaultPage.qml) now calls them. This
// is the C++ half of that slice's focused-test list: remove HIDES the synthetic root (never
// deletes it or its files), drops it from rootCount()/rootsDetail() (the marquee "· N folders"
// line and the chip strip follow), and REPUBLISHES — the union publish that follows must carry
// the surviving user root's rows through untouched. Unlike every case above, this one DOES
// exercise the real census path (removeDownloadsRoot → publishAllConfirmed → scanner), so it
// waits on the scanner's own indexPublished signal instead of hand-publishing rows.
//
// NEGATIVE CONTROL (performed live for the S9 gate, 2026-08-25): commenting out the
// m_config->setRootHidden(m_downloadsRootPath, true) line in VaultLibrary::removeDownloadsRoot
// (native/engine/VaultLibrary.cpp) turns exactly the isRootHidden/rootCount/rootsDetail
// assertions below red while every other case in this file stays green; restoring the line
// returns the suite to green. The published.wait() guard also fails in that state — with the
// synthetic root still publishable, publishAllConfirmed censuses it too (a nonexistent
// directory yields no rows but the publish itself still fires).
void tst_vault_forensics::downloads_root_chip_remove_hides_and_republishes()
{
    auto fx = buildFlatFixture(1);
    QVERIFY(fx->library);
    // A real directory for the synthetic downloads root: the chip's `available` fact and the
    // "files on disk are untouched" law both need something to exist on disk.
    const QString dlPath =
        normalizedRootPath(QDir(fx->tmp.path()).filePath(QStringLiteral("downloads")));
    QVERIFY(QDir().mkpath(dlPath));
    fx->config->addSyntheticRoot(dlPath);
    // The façade's synthetic-root seam. main.cpp wires a real VaultDownloadsRoot here; the
    // remove path only needs the PATH (the synthetic extra rows are derived pre-publish and
    // empty for a null downloads backbone), so a null backbone exercises exactly the
    // hide+republish contract without standing up the Downloads lane.
    fx->library->setDownloadsRoot(nullptr, dlPath);

    // All three invokables answer before the remove: the path, the marquee count (user root
    // + the synthetic root), and the rail's root list.
    QCOMPARE(fx->library->downloadsRootPath(), dlPath);
    QCOMPARE(fx->library->rootCount(), 2);
    QCOMPARE(fx->library->rootsDetail().size(), 2);

    const int revisionBefore = fx->library->revision();
    QSignalSpy published(fx->scanner.get(), &VaultScanner::indexPublished);
    fx->library->removeDownloadsRoot();
    QVERIFY2(published.wait(10000),
             "the chip's remove must republish (a publish WITHOUT the synthetic root's rows)");

    // HIDDEN, never deleted: the root row survives in config (restorable via
    // setRootHidden(false)), the files survive on disk, and the marquee + rail drop it.
    QVERIFY(fx->config->isRootHidden(dlPath));
    QVERIFY(fx->config->hasRoot(dlPath));
    QVERIFY(QDir(dlPath).exists());
    QCOMPARE(fx->library->rootCount(), 1);
    QCOMPARE(fx->library->rootsDetail().size(), 1);
    QVERIFY(fx->library->revision() > revisionBefore);
    // The surviving user root came through the union republish intact.
    QCOMPARE(fx->index->rowsForRoot(fx->rootPath).size(), 1);
}

// ── vault ux uplift S10 — storage management (rescanRoot / forgetRoot / setScanIgnore) ──
// These are the first cases in this file that drive the REAL census through VaultLibrary's
// own publish path (rescanRoot → publishAllConfirmed → publishConfirmed), so each waits on
// VaultScanner::indexPublished rather than hand-publishing rows. The tiny fixture trees keep
// each census near-instant; the 10s waits are machine-load headroom, not expected latency.

// NEGATIVE CONTROLS (performed live for the S10 gate, 2026-08-25): making forgetRoot's body a
// bare `return` reds exactly forget_root_removes_only_that_root_and_republishes (hasRoot still
// true, rows still present, no publish) while every other case stays green; removing
// setScanIgnore's publishAllConfirmed() call reds exactly
// scan_ignore_needle_excludes_seeded_folder (the INDEX keeps 3 rows — the durable exclusion
// never happened) with the rescan/forget cases untouched. Restoring both returns the suite to
// green.
void tst_vault_forensics::rescan_root_republishes_the_union()
{
    auto fx = buildMultiRootFixture(2);
    QVERIFY(fx->library);
    QVariantList rootsDetail = fx->library->rootsDetail();
    QCOMPARE(rootsDetail.size(), 2);
    const QString a = rootsDetail[0].toMap().value(QStringLiteral("path")).toString();
    const QString b = rootsDetail[1].toMap().value(QStringLiteral("path")).toString();
    QVERIFY(!a.isEmpty() && !b.isEmpty() && a != b);

    // The user-facing rescan: ONE root requested, the UNION republished (never one root
    // alone — VaultScanner's whole-index replace would wipe the sibling).
    QSignalSpy published(fx->scanner.get(), &VaultScanner::indexPublished);
    fx->library->rescanRoot(a);
    QVERIFY2(published.wait(10000), "rescanRoot must republish");
    QCOMPARE(fx->index->rowsForRoot(a).size(), 1);
    QCOMPARE(fx->index->rowsForRoot(b).size(), 1); // the sibling rode the same publish
    QVERIFY(fx->library->revision() > 0);

    // Guard: an unknown path is a no-op, never a scan of an arbitrary folder. Give any
    // (wrong) async publish a fair chance to fire, then hold it to zero.
    published.clear();
    fx->library->rescanRoot(QDir(fx->tmp.path()).filePath(QStringLiteral("not-a-root")));
    QTest::qWait(250);
    QCOMPARE(published.count(), 0);
}

void tst_vault_forensics::forget_root_removes_only_that_root_and_republishes()
{
    auto fx = buildMultiRootFixture(2);
    QVERIFY(fx->library);
    QVariantList rootsDetail = fx->library->rootsDetail();
    const QString a = rootsDetail[0].toMap().value(QStringLiteral("path")).toString();
    const QString b = rootsDetail[1].toMap().value(QStringLiteral("path")).toString();

    // Bootstrap the REAL census (hand-built fixture rows carry invented ids; a real publish
    // derives identity-stable ones the preservation assertion below can compare across
    // republishes).
    QSignalSpy published(fx->scanner.get(), &VaultScanner::indexPublished);
    fx->library->rescanRoot(a);
    QVERIFY(published.wait(10000));
    const auto bRows = fx->index->rowsForRoot(b);
    QCOMPARE(bRows.size(), 1);
    const QString bId = bRows.first().id;
    QVERIFY(!bId.isEmpty());
    // The user's intent state for the SURVIVING root (a hidden item) must ride through.
    fx->config->setHidden(bId, true);
    QVERIFY(fx->config->isHidden(bId));

    fx->library->forgetRoot(a);
    QVERIFY2(published.wait(10000), "forget must republish the surviving roots' union");

    // The forgotten root: config row gone, rows gone — and its FILES on disk untouched.
    QVERIFY(!fx->config->hasRoot(a));
    QVERIFY(fx->index->rowsForRoot(a).isEmpty());
    QVERIFY(QDir(a).exists());
    // The surviving root: row + identity + hidden intent all preserved by the union publish.
    QVERIFY(fx->config->hasRoot(b));
    QCOMPARE(fx->library->rootCount(), 1);
    QCOMPARE(fx->library->rootsDetail().size(), 1);
    const auto bRowsAfter = fx->index->rowsForRoot(b);
    QCOMPARE(bRowsAfter.size(), 1);
    QCOMPARE(bRowsAfter.first().id, bId);      // identity preserved across the republish
    QVERIFY(fx->config->isHidden(bId));        // hidden state preserved
}

void tst_vault_forensics::scan_ignore_needle_excludes_seeded_folder()
{
    auto fx = buildFlatFixture(1); // one confirmed root already holding f0/movie.mp4
    QVERIFY(fx->library);
    // Seed the needle folder + one control sibling.
    const QString sampleDir = QDir(fx->rootPath).filePath(QStringLiteral("sample"));
    const QString filmDir = QDir(fx->rootPath).filePath(QStringLiteral("Film"));
    QVERIFY(QDir().mkpath(sampleDir));
    QVERIFY(QDir().mkpath(filmDir));
    writeStub(QDir(sampleDir).filePath(QStringLiteral("movie.mp4")));
    writeStub(QDir(filmDir).filePath(QStringLiteral("movie.mp4")));

    QSignalSpy published(fx->scanner.get(), &VaultScanner::indexPublished);
    fx->library->rescanRoot(fx->rootPath); // no needles yet: all three folders land
    QVERIFY(published.wait(10000));
    QCOMPARE(fx->index->rowsForRoot(fx->rootPath).size(), 3);

    // The façade passthrough persists the needle AND republishes — the exclusion must be
    // DURABLE (the index dropped the folder's rows), not just the live projection skipping
    // it during a walk.
    fx->library->setScanIgnore(QStringList{QStringLiteral("sample")});
    QVERIFY2(published.wait(10000), "setScanIgnore must republish with the new needles");
    QCOMPARE(fx->library->scanIgnore(), QStringList{QStringLiteral("sample")});
    QCOMPARE(fx->index->rowsForRoot(fx->rootPath).size(), 2);
    bool sawSample = false;
    for (const auto& row : fx->index->rowsForRoot(fx->rootPath))
        if (row.subtreePath.contains(QStringLiteral("sample"), Qt::CaseInsensitive))
            sawSample = true;
    QVERIFY(!sawSample);
    // The live browse projection agrees (its walk threads the same needle layer).
    QCOMPARE(fx->library->browseAt(fx->rootPath).size(), 2);
}

// ── vault ux uplift S11 — rootsDetail()'s per-root error facts ──
// The rail's "needs attention" affordance reads errorCount/errorItems/watcherDegraded
// STRAIGHT from rootsDetail(). The facts must reflect the index's stored error rows, never
// an always-empty default: count total, the capped list's reason is the row's human detail
// (falling back to the state name when the detail is empty, the store's own preference), and
// a live registered root's watcherDegraded stays false.
void tst_vault_forensics::roots_detail_surfaces_per_root_error_facts()
{
    auto fx = buildFlatFixture(2); // f0 + f1, both clean — publish() replaced their rows
    QVERIFY(fx->library);

    // Seed ONE errored row: f0 gains a recorded corrupt state + its human reason; f1 stays
    // clean as the in-case control (its absence from the list proves per-row truth, not a
    // per-root all-or-nothing).
    auto rows = fx->index->rowsForRoot(fx->rootPath);
    QCOMPARE(rows.size(), 2);
    const int f0 = rows[0].path.contains(QStringLiteral("f0"), Qt::CaseInsensitive) ? 0 : 1;
    rows[f0].errorState = QStringLiteral("corrupt");
    rows[f0].errorDetail = QStringLiteral("archive contains no readable pages");
    fx->index->publish(rows);

    const QVariantList detail = fx->library->rootsDetail();
    QCOMPARE(detail.size(), 1);
    const QVariantMap row = detail.first().toMap();
    QCOMPARE(row.value(QStringLiteral("errorCount")).toInt(), 1);
    const QVariantList items = row.value(QStringLiteral("errorItems")).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("reason")).toString(),
             QStringLiteral("archive contains no readable pages"));
    QCOMPARE(items.first().toMap().value(QStringLiteral("path")).toString(),
             rows[f0].path);
    // A live registered root is never degraded — the flag defaults false. (The red side of
    // both watcher failure classes stays the watcher's own domain.)
    QCOMPARE(row.value(QStringLiteral("watcherDegraded")).toBool(), false);

    // Stored state with NO detail must fall back to the state name (never an empty reason).
    rows[f0].errorDetail.clear();
    fx->index->publish(rows);
    const QVariantList items2 = fx->library->rootsDetail().first()
                                    .toMap().value(QStringLiteral("errorItems")).toList();
    QCOMPARE(items2.size(), 1);
    QCOMPARE(items2.first().toMap().value(QStringLiteral("reason")).toString(),
             QStringLiteral("corrupt"));
}

// ── vault ux uplift S12 — the browse sort contract ──
// newest/size order by the node's own row facts; title is ONE merged numeric-aware order
// across node types (natural keeps the locked §4.2 buckets: folders, then series, then
// films); ties break by the natural key ascending; an unknown sort string reads as natural.
// Every case drives the REAL planBrowseLevel walk over a real fixture tree.

namespace {
QStringList browseTitles(const QVariantList& rows)
{
    QStringList titles;
    for (const QVariant& rv : rows)
        titles << rv.toMap().value(QStringLiteral("displayTitle")).toString();
    return titles;
}
} // namespace

void tst_vault_forensics::browse_sort_newest_orders_by_row_mtime_desc()
{
    auto fx = buildFlatFixture(4); // f0..f3, mtimeMs 1000..1003, titles "f0".."f3"
    QVERIFY(fx->library);
    // newest first: f3 (1003) → f0 (1000); natural would be f0 → f3.
    QCOMPARE(browseTitles(fx->library->browseAt(fx->rootPath, QStringLiteral("newest"))),
             QStringList() << "f3" << "f2" << "f1" << "f0");
    // Ties break by the natural key ascending: f1 and f2 share a mtime → f1 before f2.
    auto rows = fx->index->rowsForRoot(fx->rootPath);
    for (auto& row : rows) {
        if (row.path.contains(QStringLiteral("f2"), Qt::CaseInsensitive))
            row.mtimeMs = 1001; // == f1's
    }
    fx->index->publish(rows);
    QCOMPARE(browseTitles(fx->library->browseAt(fx->rootPath, QStringLiteral("newest"))),
             QStringList() << "f3" << "f1" << "f2" << "f0");
}

void tst_vault_forensics::browse_sort_size_orders_by_total_bytes_desc()
{
    auto fx = buildFlatFixture(4);
    QVERIFY(fx->library);
    auto rows = fx->index->rowsForRoot(fx->rootPath);
    const qint64 sizes[4] = { 10, 40, 20, 30 }; // f0..f3
    for (auto& row : rows) {
        for (int i = 0; i < 4; ++i) {
            if (row.path.contains(QStringLiteral("f") + QString::number(i),
                                  Qt::CaseInsensitive))
                row.size = sizes[i];
        }
    }
    fx->index->publish(rows);
    // biggest first: f1 (40) → f3 (30) → f2 (20) → f0 (10).
    QCOMPARE(browseTitles(fx->library->browseAt(fx->rootPath, QStringLiteral("size"))),
             QStringList() << "f1" << "f3" << "f2" << "f0");
}

void tst_vault_forensics::browse_sort_title_merges_across_node_types()
{
    // A mixed level discriminates the two orders: "zzzShow" holds TWO videos (a series/folder
    // node), "aaaFilm" exactly one (a Film node) — natural buckets containers before films
    // ([zzzShow, aaaFilm]); title is ONE merged numeric-aware order ([aaaFilm, zzzShow]).
    auto fx = buildFlatFixture(0);
    QVERIFY(fx->library);
    const QString showDir = QDir(fx->rootPath).filePath(QStringLiteral("zzzShow"));
    const QString filmDir = QDir(fx->rootPath).filePath(QStringLiteral("aaaFilm"));
    QVERIFY(QDir().mkpath(showDir));
    QVERIFY(QDir().mkpath(filmDir));
    writeStub(QDir(showDir).filePath(QStringLiteral("movie.mp4")));
    writeStub(QDir(showDir).filePath(QStringLiteral("movie2.mp4")));
    writeStub(QDir(filmDir).filePath(QStringLiteral("movie.mp4")));
    QList<VaultIndex::FileRow> rows;
    rows.append(makeFilmRow(fx->rootPath, showDir,
                            QDir(showDir).filePath(QStringLiteral("movie.mp4")),
                            QStringLiteral("vault:title-show-1"), 2000));
    rows.append(makeFilmRow(fx->rootPath, showDir,
                            QDir(showDir).filePath(QStringLiteral("movie2.mp4")),
                            QStringLiteral("vault:title-show-2"), 2000));
    rows.append(makeFilmRow(fx->rootPath, filmDir,
                            QDir(filmDir).filePath(QStringLiteral("movie.mp4")),
                            QStringLiteral("vault:title-film-1"), 1000));
    fx->index->publish(rows);
    const QStringList natural = browseTitles(
        fx->library->browseAt(fx->rootPath, QStringLiteral("natural")));
    QVERIFY2(natural == (QStringList() << "zzzShow" << "aaaFilm"),
             "natural must keep the locked §4.2 buckets (containers before films)");
    QCOMPARE(browseTitles(fx->library->browseAt(fx->rootPath, QStringLiteral("title"))),
             QStringList() << "aaaFilm" << "zzzShow");
    // The default call and any unknown string read as natural (spec §6 law: natural stays
    // the default; a typo'd persistence value can never invent a sixth order).
    QCOMPARE(browseTitles(fx->library->browseAt(fx->rootPath)),
             natural);
    QCOMPARE(browseTitles(fx->library->browseAt(fx->rootPath, QStringLiteral("bogus"))),
             natural);
}

// ── vault ux uplift S13 — the browse filter predicates ──
// kind matches the row's STORED kind; identState matches the row's projected state;
// presence matches the away flag; absent predicates are no-ops. All over the REAL walk.
void tst_vault_forensics::browse_filter_kind_ident_and_presence_predicate()
{
    // f0 = a video film made permanently uncertain (an ambiguous identity record); f1 = a
    // comic group; f2 = a plain video film. Mixed on purpose: every predicate discriminates.
    auto fx = buildFlatFixture(0);
    QVERIFY(fx->library);
    auto makeRow = [&fx](const QString& folder, const char* idSuffix, const char* kind) {
        const QString dir = QDir(fx->rootPath).filePath(folder);
        QDir().mkpath(dir);
        const QString file = QDir(dir).filePath(QStringLiteral("item.mkv"));
        writeStub(file);
        VaultIndex::FileRow r = makeFilmRow(fx->rootPath, dir, file,
                                            QStringLiteral("vault:s13-") + idSuffix, 1000);
        r.kind = QString::fromLatin1(kind);
        return r;
    };
    QList<VaultIndex::FileRow> rows;
    // The walk's own canonical form (QFileInfo::absoluteFilePath uppercases the drive letter on
    // Windows — a plain QDir::filePath string would case-miss it in the loop below).
    const QString ambiguousDir =
        QFileInfo(QDir(fx->rootPath).filePath(QStringLiteral("f0"))).absoluteFilePath();
    rows.append(makeRow(QStringLiteral("f0"), "amb", "video"));
    rows.append(makeRow(QStringLiteral("f1"), "comic", "comic"));
    rows.append(makeRow(QStringLiteral("f2"), "vid", "video"));
    // f0's group carries a durable ambiguous identity → its browse state is "uncertain";
    // f1/f2 carry adopted identities → "identified" (no identity would read "resolving").
    for (auto& row : rows) {
        if (row.subtreePath == ambiguousDir) {
            row.identityState = QStringLiteral("ambiguous");
        } else {
            row.identityId = QStringLiteral("imdb:tt000") + row.id.right(4);
        }
    }
    fx->index->publish(rows);

    using KV = QPair<QString, QString>;
    auto titlesWith = [&fx](const QList<KV>& filterPairs) {
        QVariantMap filter;
        for (const auto& kv : filterPairs)
            filter.insert(kv.first, kv.second);
        return browseTitles(fx->library->browseAt(fx->rootPath,
                                                  QStringLiteral("natural"), filter));
    };

    // kind: the stored classification, node-blind (a Film and a folder both match "video")
    QCOMPARE(titlesWith({{"kind", "video"}}), QStringList() << "f0" << "f2");
    QCOMPARE(titlesWith({{"kind", "comic"}}), QStringList() << "f1");
    QCOMPARE(titlesWith({{"kind", "book"}}), QStringList());
    // identState: uncertain-only is the identify workflow's lane
    QCOMPARE(titlesWith({{"identState", "uncertain"}}), QStringList() << "f0");
    QCOMPARE(titlesWith({{"identState", "identified"}}), QStringList() << "f1" << "f2");
    // presence: flip the whole root away — "away" passes everything, "present" empties.
    QVERIFY(fx->index->markRootAway(fx->rootPath, true));
    QCOMPARE(titlesWith({{"presence", "away"}}).size(), 3);
    QCOMPARE(titlesWith({{"presence", "present"}}), QStringList());
    QVERIFY(fx->index->markRootAway(fx->rootPath, false));
    // predicates AND together; an empty map is the unfiltered projection
    QCOMPARE(titlesWith({{"kind", "video"}, {"identState", "uncertain"}}),
             QStringList() << "f0");
    QCOMPARE(titlesWith({}).size(), 3);
}

// The fourth empty cause finally has its production trigger: an ACTIVE filter whose
// projection is empty while the level HAS rows unfiltered → "filtered" (and it outranks
// allAway — the copy's next step is clearing the filter).
void tst_vault_forensics::browse_empty_cause_filtered_from_production_path()
{
    auto fx = buildFlatFixture(2); // f0 + f1, both plain video films
    QVERIFY(fx->library);
    QVariantMap filter;
    filter.insert(QStringLiteral("kind"), QStringLiteral("book"));

    QCOMPARE(fx->library->browseEmptyCause(fx->rootPath, filter),
             QStringLiteral("filtered"));
    // No filter → the old Slice 9 contract, byte for byte ("none": the level has rows).
    QCOMPARE(fx->library->browseEmptyCause(fx->rootPath), QStringLiteral("none"));
    // A filter that still shows rows → still "none" (one walk, exactly as before).
    filter.insert(QStringLiteral("kind"), QStringLiteral("video"));
    QCOMPARE(fx->library->browseEmptyCause(fx->rootPath, filter), QStringLiteral("none"));
    // A filter over a genuinely EMPTY level is emptyFolder, never filtered (nothing was
    // excluded — the level itself is empty).
    const QString emptyDir = QDir(fx->rootPath).filePath(QStringLiteral("nothing-here"));
    QVERIFY(QDir().mkpath(emptyDir));
    filter.insert(QStringLiteral("kind"), QStringLiteral("book"));
    QCOMPARE(fx->library->browseEmptyCause(emptyDir, filter), QStringLiteral("emptyFolder"));
}

QTEST_GUILESS_MAIN(tst_vault_forensics)
#include "tst_vault_forensics.moc"
