// Task 2 (CBZ-in-place plan, docs/superpowers/plans/2026-08-06-comics-cbz-in-
// place.md): Entry::archive field, atomic saveIndex(), and the fixed
// loadIndex() prune condition. This harness constructs archive-shaped index
// rows directly as JSON fixtures (no writer exists yet -- Task 4 adds the
// first one) and proves ComicDownloader's READ side already handles them
// correctly: a valid archive row survives a reload while one whose archive
// file is missing gets pruned, isDownloaded()/deleteIssue() branch on
// usesArchive() rather than the legacy dir-emptiness check (with archive
// winning over a stale dir per the documented precedence rule), and
// saveIndex()'s QSaveFile switch means a failed/interrupted write never
// corrupts the previously-persisted index.
#include "engine/ComicDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString indexPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/comics/index.json");
}

void writeIndexFixture(const QJsonObject& root)
{
    QDir().mkpath(QFileInfo(indexPath()).absolutePath());
    QFile f(indexPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QJsonObject readIndexRaw()
{
    QFile f(indexPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QByteArray readIndexBytes()
{
    QFile f(indexPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

QString makeDummyArchive(QTemporaryDir& root, const QString& name)
{
    const QString path = root.path() + QLatin1Char('/') + name + QStringLiteral(".cbz");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("not-a-real-zip-just-needs-to-exist-for-task2");
    return path;
}

QJsonObject archiveRow(const QString& archive, const QStringList& files,
                        const QString& dir = QString())
{
    QJsonObject o;
    o[QStringLiteral("seriesId")] = QStringLiteral("gc:test");
    o[QStringLiteral("seriesTitle")] = QStringLiteral("Test Series");
    o[QStringLiteral("label")] = QStringLiteral("Issue");
    o[QStringLiteral("archive")] = archive;
    if (!dir.isEmpty()) o[QStringLiteral("dir")] = dir;
    o[QStringLiteral("bytes")] = 1234.0;
    o[QStringLiteral("addedAt")] = 1000.0;
    QJsonArray fa;
    for (const QString& f : files) fa.append(f);
    o[QStringLiteral("files")] = fa;
    o[QStringLiteral("groups")] = QJsonArray();
    return o;
}

// A valid archive-shaped row survives loadIndex()/reload; one whose archive
// file is missing on disk is pruned, same as a legacy dir row whose folder
// vanished. Proves the fixed keep-condition, not just the old dir-only path.
bool runArchiveRowSurvivesReloadScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    const QString keptId = QStringLiteral("archive-row-kept");
    const QString droppedId = QStringLiteral("archive-row-dropped-missing-file");
    const QString archivePath = makeDummyArchive(fixtures, QStringLiteral("kept"));
    const QString missingArchivePath = fixtures.path() + QStringLiteral("/does-not-exist.cbz");

    QJsonObject root;
    root[keptId] = archiveRow(archivePath, {QStringLiteral("p0.jpg"), QStringLiteral("p1.jpg")});
    root[droppedId] = archiveRow(missingArchivePath, {QStringLiteral("p0.jpg")});
    writeIndexFixture(root);

    ComicDownloader comics(nam);   // loadIndex() runs in the constructor

    if (!comics.isDownloaded(keptId)) {
        std::printf("FAIL: archive row with an existing archive file did not survive reload\n");
        return false;
    }
    if (comics.isDownloaded(droppedId)) {
        std::printf("FAIL: archive row whose archive file is missing was NOT pruned on load\n");
        return false;
    }

    int keptMatches = 0, droppedMatches = 0;
    bool keptMissingFlag = true;
    QString keptArt;
    for (const QVariant& r : comics.downloadedIssues()) {
        const QVariantMap m = r.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id == keptId) {
            ++keptMatches;
            keptMissingFlag = m.value(QStringLiteral("missing")).toBool();
            keptArt = m.value(QStringLiteral("art")).toString();
        }
        if (id == droppedId) ++droppedMatches;
    }
    if (keptMatches != 1) {
        std::printf("FAIL: expected the kept archive row in downloadedIssues(), found %d\n", keptMatches);
        return false;
    }
    if (droppedMatches != 0) {
        std::printf("FAIL: the pruned archive row leaked into downloadedIssues()\n");
        return false;
    }
    // Task 3 wired downloadedIssues() to branch on usesArchive(): an archive
    // row with a real archive file on disk now resolves missing==false and an
    // image://comiccover/ art URL, deliberately flipped from the missing==true
    // this scenario pinned pre-Task-3 (see the comment that used to be here).
    if (keptMissingFlag) {
        std::printf("FAIL: downloadedIssues() still reports missing==true for an archive row "
                    "whose archive file exists (Task 3 should have wired this)\n");
        return false;
    }
    if (!keptArt.startsWith(QStringLiteral("image://comiccover/"))) {
        std::printf("FAIL: downloadedIssues() art for an archive row is not an image://comiccover/ URL "
                    "(got \"%s\")\n", qPrintable(keptArt));
        return false;
    }

    comics.deleteIssue(keptId);
    std::printf("OK: archive-shaped index row survives reload; missing-archive row is pruned\n");
    return true;
}

// A row whose `archive` names a file that's gone, but whose `dir` is a real,
// intact legacy folder, must be demoted back to a plain legacy row on load
// (archive cleared) rather than kept half-archive/half-dir -- otherwise
// isDownloaded() (archive-first) and downloadedIssues()/localPages()
// (dir-first, pre-Task-3/4) would disagree about the same row.
bool runLoadIndexDemotesStaleArchiveToLegacyScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("archive-row-demoted-to-legacy");
    const QString missingArchivePath = fixtures.path() + QStringLiteral("/demote-does-not-exist.cbz");
    const QString realDir = fixtures.path() + QStringLiteral("/demote-legacy-dir");
    QDir().mkpath(realDir);
    QFile page(realDir + QStringLiteral("/page0.jpg"));
    if (page.open(QIODevice::WriteOnly)) page.write("legacy-page-bytes");

    QJsonObject root;
    root[id] = archiveRow(missingArchivePath, {QStringLiteral("page0.jpg")}, realDir);
    writeIndexFixture(root);

    ComicDownloader comics(nam);
    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: row with a dead archive but an intact dir was not kept via the dir branch\n");
        return false;
    }
    // downloadedIssues()/localPages() are still dir-first (Task 3/4's job) --
    // if the demotion didn't clear `archive`, isDownloaded() would agree (it
    // already checks usesArchive() first) while these two silently kept
    // reading `dir` underneath, an inconsistency this scenario exists to rule
    // out. Both must resolve normally, proving the row is a clean legacy row.
    bool foundMissingFalse = false;
    for (const QVariant& r : comics.downloadedIssues()) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("id")).toString() != id) continue;
        foundMissingFalse = !m.value(QStringLiteral("missing")).toBool();
    }
    if (!foundMissingFalse) {
        std::printf("FAIL: demoted row's downloadedIssues() art did not resolve via dir\n");
        return false;
    }
    if (comics.localPages(id).size() != 1) {
        std::printf("FAIL: demoted row's localPages() did not resolve via dir\n");
        return false;
    }

    comics.deleteIssue(id);
    std::printf("OK: a row with a dead archive but an intact dir is demoted to a clean legacy row\n");
    return true;
}

// archive wins over dir per the documented precedence rule, even when dir is
// set to a nonexistent path -- exactly the shape Task 7's first-boot
// migration produces later (archive valid, dir left alone for one boot).
bool runIsDownloadedPrecedenceScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("archive-row-precedence");
    const QString archivePath = makeDummyArchive(fixtures, QStringLiteral("precedence"));
    const QString staleDir = fixtures.path() + QStringLiteral("/does-not-exist-dir");

    QJsonObject root;
    root[id] = archiveRow(archivePath, {QStringLiteral("p0.jpg")}, staleDir);
    writeIndexFixture(root);

    ComicDownloader comics(nam);
    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: isDownloaded() did not honor archive-wins-over-dir precedence\n");
        return false;
    }
    comics.deleteIssue(id);
    std::printf("OK: isDownloaded() branches on usesArchive(), archive wins over a stale dir\n");
    return true;
}

// deleteIssue() on an archive row must remove the archive FILE (not attempt
// removeTree() on an empty/irrelevant dir), erase the index row, persist that
// removal, and emit removed().
bool runDeleteIssueArchiveRowScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("archive-row-delete");
    const QString archivePath = makeDummyArchive(fixtures, QStringLiteral("delete-me"));

    QJsonObject root;
    root[id] = archiveRow(archivePath, {QStringLiteral("p0.jpg")});
    writeIndexFixture(root);

    ComicDownloader comics(nam);
    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: setup -- archive row not recognized as downloaded before delete\n");
        return false;
    }

    bool sawRemoved = false;
    QObject::connect(&comics, &ComicDownloader::removed, &comics,
        [&](const QString& fid) { if (fid == id) sawRemoved = true; });

    const QVariantMap result = comics.deleteIssue(id);
    if (!result.value(QStringLiteral("success")).toBool() || !sawRemoved) {
        std::printf("FAIL: deleteIssue() on an archive row did not report success/removed\n");
        return false;
    }
    if (QFileInfo::exists(archivePath)) {
        std::printf("FAIL: deleteIssue() left the archive file on disk\n");
        return false;
    }
    if (comics.isDownloaded(id)) {
        std::printf("FAIL: archive row still marked downloaded after delete\n");
        return false;
    }
    if (readIndexRaw().contains(id)) {
        std::printf("FAIL: deleteIssue() did not persist the removal to index.json\n");
        return false;
    }
    std::printf("OK: deleteIssue() removes the archive file and the index row\n");
    return true;
}

#ifdef Q_OS_WIN
// The atomicity property under test: saveIndex() switched to QSaveFile, which
// writes a temp file and only replaces index.json on a successful commit(). A
// process kill mid-write can't be simulated faithfully in-process, but
// locking the destination file so commit()'s replace can never succeed is the
// same class of failure (interrupted before the atomic swap) and is honestly
// checkable: the pre-existing file must survive byte-for-byte.
bool runSaveIndexAtomicFailureScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    // Two rows: `keeper` is never touched -- its continued, correct presence
    // across the whole scenario is the proof that a failed commit left
    // index.json byte-for-byte intact (not truncated/corrupted). `victim` is
    // the row we delete while the file is locked, purely to trigger a
    // saveIndex() call; deleteIssue() removing its archive FILE regardless of
    // whether the index write commits is a separate, expected concern, not
    // what this scenario is testing -- so this test doesn't assert the victim
    // row survives reload, only that the keeper row and the raw bytes do.
    const QString keeperId = QStringLiteral("archive-row-atomic-keeper");
    const QString victimId = QStringLiteral("archive-row-atomic-victim");
    const QString keeperArchive = makeDummyArchive(fixtures, QStringLiteral("atomic-keeper"));
    const QString victimArchive = makeDummyArchive(fixtures, QStringLiteral("atomic-victim"));

    QJsonObject root;
    root[keeperId] = archiveRow(keeperArchive, {QStringLiteral("p0.jpg")});
    root[victimId] = archiveRow(victimArchive, {QStringLiteral("p0.jpg")});
    writeIndexFixture(root);
    const QByteArray beforeBytes = readIndexBytes();

    ComicDownloader comics(nam);   // loads both rows into memory

    const std::wstring lockedPath = indexPath().toStdWString();
    const HANDLE lockedFile = CreateFileW(lockedPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lockedFile == INVALID_HANDLE_VALUE) {
        std::printf("FAIL: could not lock index.json to simulate an interrupted write\n");
        return false;
    }

    // deleteIssue() on a row already in memory mutates m_index AND calls
    // saveIndex() -- the narrowest public trigger available without a real
    // network/extraction job. Delete the victim while the destination file is
    // locked (no FILE_SHARE_DELETE), forcing its saveIndex() commit to fail.
    comics.deleteIssue(victimId);
    CloseHandle(lockedFile);

    const QByteArray afterBytes = readIndexBytes();
    if (afterBytes != beforeBytes) {
        std::printf("FAIL: index.json changed even though saveIndex()'s commit should have failed\n");
        return false;
    }
    if (comics.isDownloaded(victimId)) {
        std::printf("FAIL: in-memory state did not reflect the delete despite the failed disk commit\n");
        return false;
    }

    // Normal saves resume once the lock is released: reload from the
    // (still-intact, un-corrupted) on-disk file -- the keeper row must still
    // parse and check out exactly as it did before the failed write -- then a
    // real, unlocked save must persist correctly.
    ComicDownloader reloaded(nam);
    if (!reloaded.isDownloaded(keeperId)) {
        std::printf("FAIL: the untouched keeper row did not survive the failed-commit window intact\n");
        return false;
    }
    reloaded.deleteIssue(keeperId);
    if (readIndexRaw().contains(keeperId)) {
        std::printf("FAIL: a real (unlocked) saveIndex() failed to persist the delete\n");
        return false;
    }

    std::printf("OK: a failed saveIndex() commit never corrupts the previously-persisted index\n");
    return true;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("ComicDownloaderArchiveIngestHarness"));

    QNetworkAccessManager nam;
    QTemporaryDir fixtures;
    if (!fixtures.isValid()) {
        std::printf("FAIL: could not create fixtures temp dir\n");
        return 1;
    }

    int failures = 0;
    if (!runArchiveRowSurvivesReloadScenario(&nam, fixtures)) ++failures;
    if (!runLoadIndexDemotesStaleArchiveToLegacyScenario(&nam, fixtures)) ++failures;
    if (!runIsDownloadedPrecedenceScenario(&nam, fixtures)) ++failures;
    if (!runDeleteIssueArchiveRowScenario(&nam, fixtures)) ++failures;
#ifdef Q_OS_WIN
    if (!runSaveIndexAtomicFailureScenario(&nam, fixtures)) ++failures;
#endif

    Q_UNUSED(app);
    return failures == 0 ? 0 : 1;
}
