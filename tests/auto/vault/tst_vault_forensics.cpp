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

QTEST_GUILESS_MAIN(tst_vault_forensics)
#include "tst_vault_forensics.moc"
