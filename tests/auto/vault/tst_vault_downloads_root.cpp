// tst_vault_downloads_root — Slice 18. Proves VaultDownloadsRoot derives the
// synthetic downloads root's VaultIndex::FileRows from Colosseum's own download
// backbones: videos + CBZ comics + CBZ tankoban volumes + epub/pdf books. Loose
// manga chapters (dirs of .jpg) are omitted — the Vault scanner + launch router
// classify by file extension and cannot open them, so they stay on the
// Downloads page exactly as today.
//
// The backbone calls go through QMetaObject::invokeMethod (the same path QML
// uses), so this test drives the real derivation with lightweight QObject fakes
// that expose matching Q_INVOKABLE slots (downloadedVideos / downloadedBooks /
// downloadedIssues / downloadedVolumes / localPages / localBook). Pure QtCore,
// GUILESS.

#include "engine/VaultDownloadsRoot.h"
#include "engine/VaultIdentity.h" // computeId for the double-count guard

#include <QFileInfo>
#include <QDir>
#include <QObject>
#include <QTemporaryDir>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

namespace {

// A backbone fake that holds a fixture row set and exposes it through the same
// Q_INVOKABLE signatures the real backbones use. Built once per test with the
// exact rows the scenario needs.
class FakeBackbone : public QObject
{
    Q_OBJECT
public:
    explicit FakeBackbone(QObject* parent = nullptr) : QObject(parent) {}

    void setVideos(const QVariantList& rows) { m_videos = rows; }
    void setBooks(const QVariantList& rows) { m_books = rows; }
    void setIssues(const QVariantList& rows) { m_issues = rows; }
    void setVolumeIds(const QVariantList& rows) { m_volumes = rows; }

    // Map issue id → localPages() payload (so the test can encode archive vs loose).
    void setIssuePages(const QString& id, const QVariantList& pages) { m_issuePages[id] = pages; }
    void setVolumePages(const QString& id, const QVariantList& pages) { m_volumePages[id] = pages; }
    void setBookPath(const QString& id, const QString& path) { m_bookPaths[id] = path; }

public slots:
    QVariantList downloadedVideos() const { return m_videos; }
    QVariantList downloadedBooks() const { return m_books; }
    QVariantList downloadedIssues() const { return m_issues; }
    QVariantList downloadedVolumes() const { return m_volumes; }
    QVariantList localPages(const QString& id) const
    {
        // ComicDownloader + MangaVolumeIndex share this signature. Return the
        // union so one fake can stand in for either backbone.
        if (m_issuePages.contains(id)) return m_issuePages[id];
        if (m_volumePages.contains(id)) return m_volumePages[id];
        return {};
    }
    QString localBook(const QString& id) const
    {
        return m_bookPaths.value(id);
    }

private:
    QVariantList m_videos, m_books, m_issues, m_volumes;
    QMap<QString, QVariantList> m_issuePages, m_volumePages;
    QMap<QString, QString> m_bookPaths;
};

// A real on-disk file so QFileInfo sees a non-zero size + mtime — the
// derivation must stamp the row's identity-bearing facts from the file itself
// when the backbone omits bytes/addedAt.
QString seedFile(const QTemporaryDir& tmp, const QString& rel, const QByteArray& bytes)
{
    const QString path = QDir(tmp.path()).filePath(rel);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(bytes);
    f.close();
    return path;
}

QVariantMap pageWithArchive(const QString& archivePath)
{
    // Matches the ComicDownloader/MangaVolumeIndex CBZ branch shape.
    QVariantMap m;
    m[QStringLiteral("index")] = 0;
    m[QStringLiteral("archive")] = archivePath;
    m[QStringLiteral("entry")] = QStringLiteral("page_000.jpg");
    m[QStringLiteral("group")] = -1;
    return m;
}

QVariantMap pageLoose(const QString& fileUrl)
{
    // Matches the ComicDownloader/MangaVolumeIndex loose-dir branch shape: no
    // `archive` key, only a file url. These rows must be SKIPPED by the
    // derivation (not Vault-openable).
    QVariantMap m;
    m[QStringLiteral("index")] = 0;
    m[QStringLiteral("url")] = fileUrl;
    m[QStringLiteral("group")] = -1;
    return m;
}

QString normalizedPath(const QString& path)
{
    QString normalized = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

} // namespace

class tst_vault_downloads_root : public QObject
{
    Q_OBJECT

private slots:
    void derives_container_rows_from_all_backbones();
    void loose_downloaded_movies_each_have_own_group();
    void skips_loose_page_chapters_and_missing_files();
    void null_backbones_are_a_noop();
    void double_count_guard_same_file_two_roots_shelves_once();
    void hasContainerDownloads_gate();
};

void tst_vault_downloads_root::derives_container_rows_from_all_backbones()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // One container download per backbone kind.
    const QString videoPath = seedFile(tmp, "video.mp4", QByteArrayLiteral("videobytes"));
    const QString cbzPath = seedFile(tmp, "issue.cbz", QByteArrayLiteral("cbzbytes"));
    const QString volPath = seedFile(tmp, "vol-1.cbz", QByteArrayLiteral("volbytes"));
    const QString bookPath = seedFile(tmp, "novel.epub", QByteArrayLiteral("epubbytes"));

    FakeBackbone bb;
    bb.setVideos({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("vid1")},
        {QStringLiteral("title"), QStringLiteral("Pilot")},
        {QStringLiteral("seriesTitle"), QStringLiteral("Foundation")},
        {QStringLiteral("path"), videoPath},
        {QStringLiteral("bytes"), qint64(999)},
        {QStringLiteral("addedAt"), qint64(111)},
        {QStringLiteral("missing"), false} } });
    bb.setBooks({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("md5book")},
        {QStringLiteral("title"), QStringLiteral("Dune")},
        {QStringLiteral("author"), QStringLiteral("Herbert")},
        {QStringLiteral("path"), bookPath},
        {QStringLiteral("bytes"), qint64(888)},
        {QStringLiteral("addedAt"), qint64(222)},
        {QStringLiteral("missing"), false} } });
    bb.setIssues({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("issue1")},
        {QStringLiteral("seriesTitle"), QStringLiteral("Berserk")},
        {QStringLiteral("label"), QStringLiteral("Vol. 1")},
        {QStringLiteral("bytes"), qint64(777)},
        {QStringLiteral("addedAt"), qint64(333)},
        {QStringLiteral("missing"), false} } });
    bb.setIssuePages(QStringLiteral("issue1"), { pageWithArchive(cbzPath) });
    bb.setVolumeIds({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("vol1")},
        {QStringLiteral("seriesTitle"), QStringLiteral("One Piece")},
        {QStringLiteral("label"), QStringLiteral("Vol. 1")},
        {QStringLiteral("bytes"), qint64(666)},
        {QStringLiteral("addedAt"), qint64(444)},
        {QStringLiteral("missing"), false} } });
    bb.setVolumePages(QStringLiteral("vol1"), { pageWithArchive(volPath) });

    VaultDownloadsRoot root(&bb, &bb, &bb, &bb);
    const QList<VaultIndex::FileRow> rows = root.rowsForDownloads(QStringLiteral("D:/Downloads"));

    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows.at(0).kind, QStringLiteral("video"));
    QCOMPARE(rows.at(0).path, videoPath);
    QCOMPARE(rows.at(0).groupTitle, QStringLiteral("Foundation"));
    QCOMPARE(rows.at(0).rootPath, QStringLiteral("D:/Downloads"));
    QVERIFY(rows.at(0).id.isEmpty()); // assigned later by VaultScanner::applyPublish

    QCOMPARE(rows.at(1).kind, QStringLiteral("book"));
    QCOMPARE(rows.at(1).path, bookPath);
    QCOMPARE(rows.at(1).groupTitle, QStringLiteral("Dune"));

    QCOMPARE(rows.at(2).kind, QStringLiteral("comic")); // comic issue CBZ
    QCOMPARE(rows.at(2).path, cbzPath);
    QCOMPARE(rows.at(2).groupTitle, QStringLiteral("Berserk"));
    QCOMPARE(rows.at(2).displayTitle, QStringLiteral("Vol. 1"));

    QCOMPARE(rows.at(3).kind, QStringLiteral("comic")); // tankoban volume CBZ
    QCOMPARE(rows.at(3).path, volPath);
    QCOMPARE(rows.at(3).groupTitle, QStringLiteral("One Piece"));
}

void tst_vault_downloads_root::loose_downloaded_movies_each_have_own_group()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString jurassic = seedFile(tmp, "Jurassic Park.mp4", QByteArrayLiteral("jurassic"));
    const QString spiderMan = seedFile(tmp, "Spider-Man.mp4", QByteArrayLiteral("spider-man"));
    const QString terminator = seedFile(tmp, "Terminator 2.mp4", QByteArrayLiteral("terminator"));
    const QString episodeOne = seedFile(tmp, "S01E01.mkv", QByteArrayLiteral("episode-1"));
    const QString episodeTwo = seedFile(tmp, "S01E02.mkv", QByteArrayLiteral("episode-2"));
    QVERIFY(!jurassic.isEmpty());
    QVERIFY(!spiderMan.isEmpty());
    QVERIFY(!terminator.isEmpty());
    QVERIFY(!episodeOne.isEmpty());
    QVERIFY(!episodeTwo.isEmpty());

    FakeBackbone bb;
    bb.setVideos({
        QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Jurassic Park")},
            {QStringLiteral("seriesTitle"), QString()},
            {QStringLiteral("path"), jurassic},
            {QStringLiteral("missing"), false} },
        QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Spider-Man")},
            {QStringLiteral("seriesTitle"), QString()},
            {QStringLiteral("path"), spiderMan},
            {QStringLiteral("missing"), false} },
        QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Terminator 2")},
            {QStringLiteral("seriesTitle"), QString()},
            {QStringLiteral("path"), terminator},
            {QStringLiteral("missing"), false} },
        QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Pilot")},
            {QStringLiteral("seriesTitle"), QStringLiteral("Foundation")},
            {QStringLiteral("path"), episodeOne},
            {QStringLiteral("missing"), false} },
        QVariantMap{
            {QStringLiteral("title"), QStringLiteral("The Next Stage")},
            {QStringLiteral("seriesTitle"), QStringLiteral("Foundation")},
            {QStringLiteral("path"), episodeTwo},
            {QStringLiteral("missing"), false} }
    });

    const QString rootPath = QStringLiteral("D:/Downloads");
    VaultDownloadsRoot root(&bb, nullptr, nullptr, nullptr);
    const QList<VaultIndex::FileRow> rows = root.rowsForDownloads(rootPath);

    QCOMPARE(rows.size(), 5);
    QSet<QString> looseGroups;
    int seriesRows = 0;
    for (const VaultIndex::FileRow& row : rows) {
        if (row.groupTitle == QStringLiteral("Foundation")) {
            ++seriesRows;
            QCOMPARE(row.groupKey, QStringLiteral("Foundation"));
            QCOMPARE(row.subtreePath, QStringLiteral("Foundation"));
            continue;
        }
        looseGroups.insert(row.groupKey);
        QCOMPARE(row.groupKey, normalizedPath(row.path));
        QCOMPARE(row.subtreePath, normalizedPath(row.path));
        QVERIFY(row.groupKey != rootPath);
        QVERIFY(row.id.isEmpty());
    }

    QCOMPARE(looseGroups.size(), 3);
    QCOMPARE(seriesRows, 2);
}

void tst_vault_downloads_root::skips_loose_page_chapters_and_missing_files()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // A loose-page manga chapter: localPages() has a file:// url, no archive.
    // The derivation must SKIP it (not Vault-openable as a container).
    FakeBackbone bb;
    bb.setIssues({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("loose")},
        {QStringLiteral("seriesTitle"), QStringLiteral("Naruto")},
        {QStringLiteral("label"), QStringLiteral("Ch. 1")},
        {QStringLiteral("missing"), false} } });
    bb.setIssuePages(QStringLiteral("loose"),
                     { pageLoose(QStringLiteral("file:///D:/n/000.jpg")) });

    // A video whose file was deleted on disk: `missing` must skip it.
    bb.setVideos({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("gone")},
        {QStringLiteral("path"), QStringLiteral("D:/deleted/vid.mp4")},
        {QStringLiteral("missing"), true} } });

    VaultDownloadsRoot root(&bb, nullptr, &bb, nullptr);
    const QList<VaultIndex::FileRow> rows = root.rowsForDownloads(QStringLiteral("D:/DL"));
    QVERIFY(rows.isEmpty()); // loose chapter + missing video → zero rows
}

void tst_vault_downloads_root::null_backbones_are_a_noop()
{
    VaultDownloadsRoot root(nullptr, nullptr, nullptr, nullptr);
    QVERIFY(root.rowsForDownloads(QStringLiteral("D:/DL")).isEmpty());
    QVERIFY(!root.hasContainerDownloads());
}

void tst_vault_downloads_root::double_count_guard_same_file_two_roots_shelves_once()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // The same CBZ on disk, reached both as a user-root scan fact AND as a
    // downloads-root backbone row. VaultIdentity::computeId is the key — the
    // id is a pure function of (normalizedPath, size, mtimeMs), so the same
    // file yields the same id regardless of which root surfaced it. The index's
    // INSERT OR REPLACE then dedupes → ONE shelf item, not two.
    const QString cbz = seedFile(tmp, "shared.cbz", QByteArrayLiteral("sharedbytes"));
    const qint64 size = QFileInfo(cbz).size();
    const qint64 mtime = QFileInfo(cbz).lastModified().toMSecsSinceEpoch();

    FakeBackbone bb;
    bb.setIssues({ QVariantMap{
        {QStringLiteral("id"), QStringLiteral("issue")},
        {QStringLiteral("seriesTitle"), QStringLiteral("Berserk")},
        {QStringLiteral("label"), QStringLiteral("Vol. 1")},
        {QStringLiteral("missing"), false} } });
    bb.setIssuePages(QStringLiteral("issue"), { pageWithArchive(cbz) });

    VaultDownloadsRoot downloadsRoot(nullptr, nullptr, &bb, nullptr);
    const QList<VaultIndex::FileRow> dlRows =
        downloadsRoot.rowsForDownloads(QStringLiteral("D:/Downloads"));

    QCOMPARE(dlRows.size(), 1);
    // The id the scanner WOULD assign (it runs the same computeId in applyPublish):
    const QString dlId = VaultIdentity::computeId(dlRows.at(0).path, dlRows.at(0).size, dlRows.at(0).mtimeMs);

    // Simulate the user-root side: the same file under a user folder.
    const QString userId = VaultIdentity::computeId(cbz, size, mtime);

    QCOMPARE(dlId, userId); // the double-count guard hinges on this equality
}

void tst_vault_downloads_root::hasContainerDownloads_gate()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString cbz = seedFile(tmp, "v.cbz", QByteArrayLiteral("x"));

    {
        FakeBackbone empty;
        VaultDownloadsRoot root(&empty, nullptr, nullptr, nullptr);
        QVERIFY(!root.hasContainerDownloads());
    }
    {
        FakeBackbone one;
        one.setIssues({ QVariantMap{ {QStringLiteral("id"), QStringLiteral("i")},
                                     {QStringLiteral("missing"), false} } });
        one.setIssuePages(QStringLiteral("i"), { pageWithArchive(cbz) });
        VaultDownloadsRoot root(nullptr, nullptr, &one, nullptr);
        QVERIFY(root.hasContainerDownloads());
    }
}

QTEST_GUILESS_MAIN(tst_vault_downloads_root)
#include "tst_vault_downloads_root.moc"
