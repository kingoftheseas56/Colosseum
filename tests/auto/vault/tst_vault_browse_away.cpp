#include <QtTest>

#include "engine/VaultBrowseAway.h"
#include "engine/VaultConfig.h"
#include "engine/VaultIndex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

// Vault Browse face execution plan, Slice 6 — the projection's away-propagation contract.
// Slice 5 hardcoded away=false for every non-Film node type in VaultLibrary::browseAt() (its own
// comment: "Away/per-episode identity nuance is Slice 6/8's explicit business"); this proves the
// fix in VaultBrowseAway (the small seam VaultLibrary::browseAt() now calls): away is a
// ROOT-WIDE fact (VaultIndex::markRootAway() flips every row under one confirmed root in one
// statement), so EVERY node type inherits its owning root's away state, and — when the owning
// root's directory can no longer be walked at all (a genuinely vanished drive) — the durable
// index still answers instead of the level reading as empty, honoring the locked design's
// "nothing disappears" contract (§4.7).
//
// VaultBrowseAway was pulled out of VaultLibrary specifically so this test can construct a real
// VaultIndex + VaultConfig WITHOUT the full VaultLibrary façade (whose constructor
// unconditionally builds a VaultWatcher and drags in the scanner/downloads-root/identifier
// dependency tree). Real small fixture trees under QTemporaryDir. GUILESS, pure Qt6::Core/Sql.
class VaultBrowseAwayTest final : public QObject
{
    Q_OBJECT

private slots:
    void ownerRootPathResolvesNestedPathsAndShowSentinels();
    void ownerRootAwayFlipsWithMarkRootAwayAndReverts();
    void offlineBrowseAtServesGroupsWhenRootDirectoryIsGone();
    void offlineBrowseAtIsEmptyWithoutPriorPublishedRows();

private:
    static VaultIndex::FileRow fileRow(const QString& id, const QString& rootPath,
                                       const QString& subtreePath, const QString& path)
    {
        VaultIndex::FileRow r;
        r.id = id;
        r.rootPath = rootPath;
        r.subtreePath = subtreePath;
        r.groupKey = subtreePath;
        r.groupTitle = QFileInfo(subtreePath).fileName();
        r.kind = QStringLiteral("video");
        r.path = path;
        r.displayTitle = QFileInfo(path).fileName();
        r.realName = QFileInfo(path).fileName();
        r.size = 4;
        r.mtimeMs = 1;
        return r;
    }
};

void VaultBrowseAwayTest::ownerRootPathResolvesNestedPathsAndShowSentinels()
{
    QVariantList roots;
    QVariantMap confirmed;
    confirmed[QStringLiteral("path")] = QStringLiteral("C:/vault/root");
    confirmed[QStringLiteral("confirmed")] = true;
    roots.append(confirmed);
    QVariantMap unconfirmed;
    unconfirmed[QStringLiteral("path")] = QStringLiteral("C:/vault/root/deeper-but-unconfirmed");
    unconfirmed[QStringLiteral("confirmed")] = false;
    roots.append(unconfirmed);

    // Exact match.
    QCOMPARE(VaultBrowseAway::ownerRootPath(roots, QStringLiteral("C:/vault/root")),
             QStringLiteral("C:/vault/root"));
    // A real descendant path resolves to the confirmed root that owns it.
    QCOMPARE(VaultBrowseAway::ownerRootPath(roots, QStringLiteral("C:/vault/root/Show/Season 1")),
             QStringLiteral("C:/vault/root"));
    // A drilled-in show sentinel resolves via its recoverable parent path, not literally.
    QCOMPARE(VaultBrowseAway::ownerRootPath(
                 roots, QStringLiteral("C:/vault/root::show::loki")),
             QStringLiteral("C:/vault/root"));
    // A path outside every confirmed root resolves to nothing.
    QVERIFY(VaultBrowseAway::ownerRootPath(roots, QStringLiteral("D:/elsewhere")).isEmpty());
}

void VaultBrowseAwayTest::ownerRootAwayFlipsWithMarkRootAwayAndReverts()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    // VaultConfig::addRoot normalizes (cleanPath + lowercase-on-Windows) before storing — a
    // FileRow's rootPath column must be written with the SAME normalized string, or
    // rowsForRoot()'s exact SQL match silently finds nothing (rootPath()/rowsForRoot() already
    // share this convention in production; the test must too). Add to config FIRST, then read
    // the normalized string back, so both sides of the comparison genuinely agree.
    VaultConfig config(vaultDir.path());
    config.addRoot(QStringLiteral("D:/hemanth's folder"));
    config.confirmRoot(QStringLiteral("D:/hemanth's folder"));
    const QVariantList roots = config.roots();
    QVERIFY(!roots.isEmpty());
    const QString rootPath = roots.first().toMap().value(QStringLiteral("path")).toString();
    const QString subtree = rootPath + QStringLiteral("/Loki");
    QVERIFY(index.publish({
        fileRow(QStringLiteral("vault:e1"), rootPath, subtree, subtree + QStringLiteral("/e1.mkv")),
        fileRow(QStringLiteral("vault:e2"), rootPath, subtree, subtree + QStringLiteral("/e2.mkv")),
    }));

    QCOMPARE(VaultBrowseAway::ownerRootAway(&index, roots, subtree), false);

    QVERIFY(index.markRootAway(rootPath, true));
    QCOMPARE(VaultBrowseAway::ownerRootAway(&index, roots, subtree), true);
    // A path drilled deeper than the seeded rows still resolves to the SAME root-wide verdict —
    // away is a root fact, not a per-subfolder lookup.
    QCOMPARE(VaultBrowseAway::ownerRootAway(&index, roots, subtree + QStringLiteral("/Season 1")),
             true);

    QVERIFY(index.markRootAway(rootPath, false));
    QCOMPARE(VaultBrowseAway::ownerRootAway(&index, roots, subtree), false);
}

void VaultBrowseAwayTest::offlineBrowseAtServesGroupsWhenRootDirectoryIsGone()
{
    // The realistic "unplugged drive" shape: the root was scanned successfully once (real
    // published rows exist under a REAL directory), then the directory is physically removed
    // (simulating the drive letter vanishing) and the index is marked away — exactly the state a
    // confirmed root is in the moment VaultWatcher::refresh() first notices it at boot. Without
    // this fallback, VaultKit::planBrowseLevel would bail at QDir::exists()==false and
    // VaultLibrary::browseAt() would return an EMPTY list — the design's own "nothing
    // disappears" contract (§4.7) would be false. This proves the fallback keeps the tiles.
    QTemporaryDir fixtureDir;
    QVERIFY(fixtureDir.isValid());
    const QString rawRootPath = fixtureDir.path();
    QDir().mkpath(rawRootPath + QStringLiteral("/FilmFolder"));
    QDir().mkpath(rawRootPath + QStringLiteral("/PlainFolder"));
    auto touch = [](const QString& path) { QFile f(path); (void)f.open(QIODevice::WriteOnly); f.write("x"); };
    touch(rawRootPath + QStringLiteral("/FilmFolder/only.mp4"));
    touch(rawRootPath + QStringLiteral("/PlainFolder/clip1.mp4"));
    touch(rawRootPath + QStringLiteral("/PlainFolder/clip2.mp4"));

    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    // See the sibling test's own comment: VaultConfig normalizes a root path before storing it,
    // so every FileRow.rootPath this test writes must match that SAME normalized string, or
    // rowsForRoot()'s exact SQL match finds nothing.
    VaultConfig config(vaultDir.path());
    config.addRoot(rawRootPath);
    config.confirmRoot(rawRootPath);
    const QVariantList roots = config.roots();
    QVERIFY(!roots.isEmpty());
    const QString rootPath = roots.first().toMap().value(QStringLiteral("path")).toString();
    const QString filmSubtree = rootPath + QStringLiteral("/FilmFolder");
    const QString plainSubtree = rootPath + QStringLiteral("/PlainFolder");
    QVERIFY(index.publish({
        fileRow(QStringLiteral("vault:film"), rootPath, filmSubtree,
                filmSubtree + QStringLiteral("/only.mp4")),
        fileRow(QStringLiteral("vault:clip1"), rootPath, plainSubtree,
                plainSubtree + QStringLiteral("/clip1.mp4")),
        fileRow(QStringLiteral("vault:clip2"), rootPath, plainSubtree,
                plainSubtree + QStringLiteral("/clip2.mp4")),
    }));

    QVERIFY(QDir(rawRootPath).removeRecursively()); // the drive is gone (case-insensitive on disk)
    QVERIFY(index.markRootAway(rootPath, true));

    const QVariantList rows = VaultBrowseAway::offlineBrowseAt(&index, roots, rootPath);
    // THE CONTRACT: two tiles (one per group the index remembers), not zero.
    QCOMPARE(rows.size(), 2);
    int totalItems = 0;
    bool sawFilm = false, sawFolder = false;
    for (const QVariant& rv : rows) {
        const QVariantMap m = rv.toMap();
        QCOMPARE(m.value(QStringLiteral("away")).toBool(), true);
        QVERIFY(!m.value(QStringLiteral("key")).toString().isEmpty());
        QVERIFY(!m.value(QStringLiteral("displayTitle")).toString().isEmpty());
        const QString nodeType = m.value(QStringLiteral("nodeType")).toString();
        if (nodeType == QLatin1String("film")) sawFilm = true;
        if (nodeType == QLatin1String("folder")) sawFolder = true;
        totalItems += m.value(QStringLiteral("counts")).toMap().value(QStringLiteral("items")).toInt();
    }
    QVERIFY(sawFilm);
    QVERIFY(sawFolder);
    QCOMPARE(totalItems, 3); // 1 film group + 2-file plain-folder group == 3 rows total
}

void VaultBrowseAwayTest::offlineBrowseAtIsEmptyWithoutPriorPublishedRows()
{
    // A root that was NEVER scanned (added then immediately vanished, or the index was wiped)
    // has nothing to fall back to — offlineBrowseAt() must return empty honestly, never invent
    // rows. Also the negative-control-adjacent shape: an unconfirmed root owns nothing at all.
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    VaultConfig config(vaultDir.path());
    config.addRoot(QStringLiteral("D:/never-scanned"));
    config.confirmRoot(QStringLiteral("D:/never-scanned"));

    const QVariantList rows =
        VaultBrowseAway::offlineBrowseAt(&index, config.roots(), QStringLiteral("D:/never-scanned"));
    QCOMPARE(rows.size(), 0);
}

QTEST_MAIN(VaultBrowseAwayTest)
#include "tst_vault_browse_away.moc"
