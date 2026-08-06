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
//
// Task 4 extends this file with the real WRITER: onFinished()'s two-path
// ingest. These scenarios drive the actual public downloadIssue() entry
// point end-to-end against a local loopback mock (a tiny single-response
// QTcpServer for the release-post HTML, plus a QNetworkAccessManager
// subclass that redirects the getcomics.org/dls/ link ComicDlsParse's regex
// requires onto a second local mock serving the archive bytes) -- never a
// shortcut into private internals, matching this file's own house style.
#include "engine/CbzArchive.h"
#include "engine/ComicDownloader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>
#include <functional>
#include <memory>

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

// ─────────────────────────────────────────────────────────────────────────────
// Task 4: the real writer -- onFinished()'s two-path ingest, end-to-end
// ─────────────────────────────────────────────────────────────────────────────

// Pumps processEvents until `pred` is true or `timeoutMs` elapses. Same
// technique comicreader_provider_harness.cpp uses for its own async waits.
bool waitFor(const std::function<bool()>& pred, int timeoutMs = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }
    return true;
}

// Mirrors of ComicDownloader's own private path-construction helpers
// (safeSeg/hash10/baseDir/issueDir, ComicDownloader.cpp) so this harness can
// independently predict where a canonical archive or the flat HTTP staging
// file will land, WITHOUT reaching into private internals to ask. If these
// ever drift from the real implementation, every scenario below fails loudly
// (wrong paths, nothing found) rather than silently passing -- there is no
// way for a mismatch here to produce a false green.
QString baseDirMirror()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/comics");
}
QString hash10Mirror(const QString& v)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}
QString safeSegMirror(const QString& v)
{
    QString out;
    for (const QChar c : v) {
        if (c.isLetterOrNumber() || c == QChar('.') || c == QChar('_') || c == QChar('-')
            || c == QChar(' '))
            out.append(c);
        else
            out.append(QChar('_'));
    }
    out = out.trimmed();
    while (out.endsWith(QChar('.'))) out.chop(1);
    if (out.isEmpty()) out = QStringLiteral("item");
    return out.left(80);
}
QString issueDirMirror(const QString& seriesId, const QString& label, const QString& id)
{
    return baseDirMirror() + QChar('/') + safeSegMirror(seriesId) + QChar('/')
           + safeSegMirror(label) + QChar('-') + hash10Mirror(id);
}
QString issueArchivePathMirror(const QString& seriesId, const QString& label, const QString& id)
{
    return issueDirMirror(seriesId, label, id) + QStringLiteral(".cbz");
}
QString stagingArchivePathMirror(const QString& id)
{
    return baseDirMirror() + QStringLiteral("/dl_") + hash10Mirror(id) + QStringLiteral(".archive");
}

QByteArray buildHttpResponse(const QByteArray& contentType, const QByteArray& body)
{
    QByteArray out = "HTTP/1.1 200 OK\r\n";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    return out;
}

// Single-purpose loopback server: replies to EVERY accepted connection with
// the SAME canned HTTP response, without parsing the client's request --
// sufficient here because a fresh server is spun up per logical endpoint
// (post-page HTML vs. archive bytes) per scenario, so there is never more
// than one possible answer for it to give. It DOES wait for the client's
// request to actually arrive before writing anything: responding the moment
// a connection is merely ACCEPTED (before the client has sent its GET) is a
// genuine, reproducible race -- confirmed empirically writing this harness,
// not theoretical -- that intermittently produces "Connection closed" on
// QNetworkAccessManager's side, roughly matching a half-duplex confusion
// where the write+close races the client still sending its own request on
// the same socket.
class SingleResponseServer : public QObject
{
public:
    QTcpServer server;
    QByteArray response;
    quint16 port = 0;

    bool start(const QByteArray& resp)
    {
        response = resp;
        if (!server.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0)) return false;
        port = server.serverPort();
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                QTcpSocket* sock = server.nextPendingConnection();
                auto responded = std::make_shared<bool>(false);
                connect(sock, &QTcpSocket::readyRead, sock, [this, sock, responded] {
                    if (*responded) return;   // guard: readyRead can fire more than once
                    *responded = true;
                    sock->write(response);
                    connect(sock, &QTcpSocket::bytesWritten, sock, [sock] {
                        if (sock->bytesToWrite() == 0) sock->disconnectFromHost();
                    });
                });
            }
        });
        return true;
    }
};

// Redirects any getcomics.org request onto a local mock port -- the ONLY way
// to drive downloadIssue()'s real HTTP path without touching the real
// internet, since ComicDlsParse's regex hard-requires a literal
// https://getcomics.org/dls/ href in the resolved post HTML.
class RedirectingNam : public QNetworkAccessManager
{
public:
    quint16 downloadPort = 0;

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override
    {
        QNetworkRequest req = request;
        QUrl url = req.url();
        if (downloadPort != 0
            && url.host().compare(QStringLiteral("getcomics.org"), Qt::CaseInsensitive) == 0) {
            url.setScheme(QStringLiteral("http"));
            url.setHost(QStringLiteral("127.0.0.1"));
            url.setPort(downloadPort);
            req.setUrl(url);
        }
        return QNetworkAccessManager::createRequest(op, req, outgoingData);
    }
};

// A real, genuinely miniz-readable CBZ (writeImagesAtomic -- the same writer
// probe()/readEntry() are built around; a tar.exe-built zip is NOT reliably
// miniz-compatible, a lesson learned the hard way in Task 3).
QString makeRealCbz(QTemporaryDir& root, const QString& name, const QStringList& pageNames)
{
    const QString pages = root.path() + QLatin1Char('/') + name + QStringLiteral("-pages");
    QDir().mkpath(pages);
    for (int i = 0; i < pageNames.size(); ++i) {
        QImage img(40, 40, QImage::Format_ARGB32);
        img.fill(qRgb(10 * i, 60, 200));
        if (!img.save(pages + QLatin1Char('/') + pageNames.at(i), "JPEG")) return QString();
    }
    const QString archivePath = root.path() + QLatin1Char('/') + name + QStringLiteral(".cbz");
    QString error;
    if (!MangaTankoban::CbzArchive::writeImagesAtomic(archivePath, pages, pageNames, &error))
        return QString();
    return archivePath;
}

// A plain (non-zip) tar archive renamed .cbr: CbzArchive::probe() rejects it
// cleanly -- "cannot open CBZ: failed finding central directory" -- exactly
// like a real CBR would, while bsdtar (the SAME tool runExtractor() uses in
// production) auto-detects and extracts it successfully. Verified directly
// against this build's tar.exe/miniz before writing this harness -- a
// genuine RAR fixture would need RAR tooling this repo doesn't vendor.
QString makeCbrFromTar(QTemporaryDir& root, const QString& name, const QStringList& relativeNames)
{
    const QString pages = root.path() + QLatin1Char('/') + name + QStringLiteral("-pages");
    QDir().mkpath(pages);
    for (const QString& n : relativeNames) {
        const QString full = pages + QLatin1Char('/') + n;
        QDir().mkpath(QFileInfo(full).absolutePath());
        QImage img(40, 40, QImage::Format_ARGB32);
        img.fill(qRgb(20, 120, 200));
        if (!img.save(full, "JPEG")) return QString();
    }
    const QString tarPath = root.path() + QLatin1Char('/') + name + QStringLiteral(".tar");
    const int rc = QProcess::execute(QStringLiteral("C:/Windows/System32/tar.exe"),
        {QStringLiteral("-cf"), tarPath, QStringLiteral("-C"), pages, QStringLiteral(".")});
    if (rc != 0) return QString();
    const QString cbr = root.path() + QLatin1Char('/') + name + QStringLiteral(".cbr");
    if (!QFile::rename(tarPath, cbr)) return QString();
    return cbr;
}

// The fast path: a freshly downloaded CBZ that probe()s nativelyReadable is
// moved into the library archive-in-place -- no extraction, no loose page
// folder. Pins the shape localPages()/downloadedIssues() must produce (D6:
// archive+entry+url) and functionally round-trips the moved archive's pages
// through CbzArchive::readEntry(), not just checking paths exist.
bool runFastPathScenario(RedirectingNam* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("task4-fast-path");
    const QString seriesId = QStringLiteral("gc:task4");
    const QString seriesTitle = QStringLiteral("Task4 Series");
    const QString label = QStringLiteral("Issue Fast");
    const QStringList pageNames{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString srcArchive = makeRealCbz(fixtures, QStringLiteral("fast-src"), pageNames);
    if (srcArchive.isEmpty()) { std::printf("FAIL: could not build fast-path fixture CBZ\n"); return false; }
    QByteArray archiveBytes;
    {
        QFile f(srcArchive);
        if (!f.open(QIODevice::ReadOnly)) { std::printf("FAIL: could not read fixture CBZ\n"); return false; }
        archiveBytes = f.readAll();
    }

    SingleResponseServer postServer, downloadServer;
    if (!postServer.start(buildHttpResponse("text/html",
            "<a href=\"https://getcomics.org/dls/task4fast:sig\">DOWNLOAD NOW</a>"))
        || !downloadServer.start(buildHttpResponse("application/octet-stream", archiveBytes))) {
        std::printf("FAIL: could not start fast-path mock servers\n");
        return false;
    }
    nam->downloadPort = downloadServer.port;
    const QString postUrl = QStringLiteral("http://127.0.0.1:%1/post").arg(postServer.port);

    ComicDownloader comics(nam);
    comics.deleteIssue(id);

    bool sawFinished = false, sawFailed = false;
    QString failReason;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString& reason) { if (fid == id) { sawFailed = true; failReason = reason; } });

    comics.downloadIssue(id, postUrl, seriesId, seriesTitle, label, 0);

    if (!waitFor([&] { return sawFinished || sawFailed; })) {
        std::printf("FAIL: fast-path download did not complete within timeout\n");
        return false;
    }
    if (sawFailed) { std::printf("FAIL: fast-path download failed: %s\n", qPrintable(failReason)); return false; }
    if (!sawFinished) { std::printf("FAIL: fast-path download neither finished nor failed\n"); return false; }

    // No extraction ever spawned: beginExtract()'s first observable side
    // effect is creating extractTmp (staging path + ".x") -- its absence is
    // an honest proxy for "no extractor subprocess ran" (runExtractor() has
    // no injectable spawn-count seam to assert on directly).
    if (QDir(stagingArchivePathMirror(id) + QStringLiteral(".x")).exists()) {
        std::printf("FAIL: extraction temp dir exists -- the fallback path ran instead of the fast path\n");
        return false;
    }
    if (QDir(issueDirMirror(seriesId, label, id)).exists()) {
        std::printf("FAIL: a loose page folder was created -- not archive-in-place\n");
        return false;
    }
    if (QFile::exists(stagingArchivePathMirror(id))) {
        std::printf("FAIL: the staging download file was not moved into the library\n");
        return false;
    }
    if (!comics.isDownloaded(id)) { std::printf("FAIL: fast-path comic not marked downloaded\n"); return false; }

    const QVariantList pages = comics.localPages(id);
    if (pages.size() != pageNames.size()) {
        std::printf("FAIL: expected %d local pages, got %d\n", (int)pageNames.size(), (int)pages.size());
        return false;
    }
    for (int i = 0; i < pages.size(); ++i) {
        const QVariantMap p = pages.at(i).toMap();
        const QString archive = p.value(QStringLiteral("archive")).toString();
        const QString entry = p.value(QStringLiteral("entry")).toString();
        const QString url = p.value(QStringLiteral("url")).toString();
        if (archive.isEmpty() || entry.isEmpty()) {
            std::printf("FAIL: localPages()[%d] missing archive/entry (D6 shape)\n", i);
            return false;
        }
        if (entry != pageNames.at(i)) {
            std::printf("FAIL: localPages()[%d].entry mismatch: got %s want %s\n", i,
                        qPrintable(entry), qPrintable(pageNames.at(i)));
            return false;
        }
        if (!url.startsWith(QStringLiteral("image://comiccover/"))) {
            std::printf("FAIL: localPages()[%d].url is not an image://comiccover/ URL (D6 regression)\n", i);
            return false;
        }
        QString readErr;
        const QByteArray decoded = MangaTankoban::CbzArchive::readEntry(archive, entry, &readErr);
        if (decoded.isEmpty()) {
            std::printf("FAIL: localPages()[%d] archive/entry does not actually decode: %s\n",
                        i, qPrintable(readErr));
            return false;
        }
    }

    bool foundMissingFalse = false;
    QString art;
    for (const QVariant& r : comics.downloadedIssues()) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("id")).toString() != id) continue;
        foundMissingFalse = !m.value(QStringLiteral("missing")).toBool();
        art = m.value(QStringLiteral("art")).toString();
    }
    if (!foundMissingFalse || !art.startsWith(QStringLiteral("image://comiccover/"))) {
        std::printf("FAIL: downloadedIssues() shape wrong for the fast-path row\n");
        return false;
    }

    comics.deleteIssue(id);
    std::printf("OK: fast path moves a probe-readable CBZ into the library with no extraction\n");
    return true;
}

// The fallback: a source that fails probe() extracts via the existing
// bsdtar path, then REPACKS into a real canonical CBZ (not loose page_NNN.ext
// files). Also the D8 regression: a Mac-authored __MACOSX/._page sibling
// must be excluded from the packed archive's page count, not just silently
// mismatched between what was packed and what the index claims.
bool runFallbackPathScenario(RedirectingNam* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("task4-fallback-path");
    const QString seriesId = QStringLiteral("gc:task4");
    const QString seriesTitle = QStringLiteral("Task4 Series");
    const QString label = QStringLiteral("Issue Fallback");

    const QString cbrArchive = makeCbrFromTar(fixtures, QStringLiteral("fallback-src"),
        {QStringLiteral("page_001.jpg"), QStringLiteral("page_000.jpg"),
         QStringLiteral("__MACOSX/._page_000.jpg")});
    if (cbrArchive.isEmpty()) { std::printf("FAIL: could not build fallback fixture (tar-as-cbr)\n"); return false; }
    QByteArray archiveBytes;
    {
        QFile f(cbrArchive);
        if (!f.open(QIODevice::ReadOnly)) { std::printf("FAIL: could not read cbr fixture\n"); return false; }
        archiveBytes = f.readAll();
    }

    SingleResponseServer postServer, downloadServer;
    if (!postServer.start(buildHttpResponse("text/html",
            "<a href=\"https://getcomics.org/dls/task4fallback:sig\">DOWNLOAD NOW</a>"))
        || !downloadServer.start(buildHttpResponse("application/octet-stream", archiveBytes))) {
        std::printf("FAIL: could not start fallback-path mock servers\n");
        return false;
    }
    nam->downloadPort = downloadServer.port;
    const QString postUrl = QStringLiteral("http://127.0.0.1:%1/post").arg(postServer.port);

    ComicDownloader comics(nam);
    comics.deleteIssue(id);

    bool sawFinished = false, sawFailed = false;
    QString failReason;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString& reason) { if (fid == id) { sawFailed = true; failReason = reason; } });

    comics.downloadIssue(id, postUrl, seriesId, seriesTitle, label, 0);

    if (!waitFor([&] { return sawFinished || sawFailed; }, 30000)) {
        std::printf("FAIL: fallback-path download did not complete within timeout\n");
        return false;
    }
    if (sawFailed) { std::printf("FAIL: fallback-path download failed: %s\n", qPrintable(failReason)); return false; }
    if (!sawFinished) { std::printf("FAIL: fallback-path download neither finished nor failed\n"); return false; }

    if (QDir(issueDirMirror(seriesId, label, id)).exists()) {
        std::printf("FAIL: loose page_NNN.ext files were written -- repack-to-CBZ regressed to flatten behavior\n");
        return false;
    }
    if (!comics.isDownloaded(id)) { std::printf("FAIL: fallback comic not marked downloaded\n"); return false; }

    const QVariantList pages = comics.localPages(id);
    if (pages.size() != 2) {
        std::printf("FAIL: expected 2 real pages (D8: Mac sibling excluded), got %d\n", (int)pages.size());
        return false;
    }
    for (int i = 0; i < pages.size(); ++i) {
        const QVariantMap p = pages.at(i).toMap();
        const QString archive = p.value(QStringLiteral("archive")).toString();
        const QString entry = p.value(QStringLiteral("entry")).toString();
        if (archive.isEmpty() || entry.isEmpty()) {
            std::printf("FAIL: fallback localPages()[%d] missing archive/entry -- not repacked to a CBZ\n", i);
            return false;
        }
        if (entry.contains(QStringLiteral("__MACOSX"), Qt::CaseInsensitive) || entry.startsWith(QChar('.'))) {
            std::printf("FAIL: fallback packed a __MACOSX/dot-file entry into the index (D8)\n");
            return false;
        }
        QString readErr;
        const QByteArray decoded = MangaTankoban::CbzArchive::readEntry(archive, entry, &readErr);
        if (decoded.isEmpty()) {
            std::printf("FAIL: fallback localPages()[%d] does not decode: %s\n", i, qPrintable(readErr));
            return false;
        }
    }

    comics.deleteIssue(id);
    std::printf("OK: fallback path extracts a probe-rejected source, repacks into a real CBZ (not loose files)\n");
    return true;
}

#ifdef Q_OS_WIN
// D1 regression: an ORDINARY (non-crash) safe-move failure must preserve the
// source exactly as faithfully as a simulated kill does. Pre-locks the
// canonical destination so the safe-move's "replace the stale leftover"
// step fails -- exercising failPreservingSource(), not failAndCleanup(),
// which would have deleted the only good source over a transient lock.
bool runOrdinaryFailurePreservesSourceScenario(RedirectingNam* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("task4-ordinary-failure");
    const QString seriesId = QStringLiteral("gc:task4");
    const QString seriesTitle = QStringLiteral("Task4 Series");
    const QString label = QStringLiteral("Issue Locked");
    const QStringList pageNames{QStringLiteral("page_000.jpg")};

    const QString srcArchive = makeRealCbz(fixtures, QStringLiteral("locked-src"), pageNames);
    if (srcArchive.isEmpty()) { std::printf("FAIL: could not build locked-canonical fixture CBZ\n"); return false; }
    QByteArray archiveBytes;
    {
        QFile f(srcArchive);
        if (!f.open(QIODevice::ReadOnly)) { std::printf("FAIL: could not read fixture CBZ\n"); return false; }
        archiveBytes = f.readAll();
    }

    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    { QFile f(canonical); if (f.open(QIODevice::WriteOnly)) f.write("stale-leftover-bytes"); }
    const std::wstring lockedPath = canonical.toStdWString();
    const HANDLE lockedFile = CreateFileW(lockedPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lockedFile == INVALID_HANDLE_VALUE) {
        std::printf("FAIL: could not lock the canonical destination to simulate an ordinary failure\n");
        return false;
    }

    SingleResponseServer postServer, downloadServer;
    if (!postServer.start(buildHttpResponse("text/html",
            "<a href=\"https://getcomics.org/dls/task4ordinaryfail:sig\">DOWNLOAD NOW</a>"))
        || !downloadServer.start(buildHttpResponse("application/octet-stream", archiveBytes))) {
        CloseHandle(lockedFile);
        std::printf("FAIL: could not start ordinary-failure mock servers\n");
        return false;
    }
    nam->downloadPort = downloadServer.port;
    const QString postUrl = QStringLiteral("http://127.0.0.1:%1/post").arg(postServer.port);

    ComicDownloader comics(nam);

    bool sawFinished = false, sawFailed = false;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString&) { if (fid == id) sawFailed = true; });

    comics.downloadIssue(id, postUrl, seriesId, seriesTitle, label, 0);

    const bool completed = waitFor([&] { return sawFinished || sawFailed; });
    CloseHandle(lockedFile);

    if (!completed) { std::printf("FAIL: ordinary-failure scenario did not complete within timeout\n"); return false; }
    if (sawFinished) { std::printf("FAIL: expected the locked canonical to make this fail, but it finished\n"); return false; }
    if (!sawFailed) { std::printf("FAIL: expected failed(id), got neither\n"); return false; }

    // The point: the source survives an ORDINARY failure, not just a
    // simulated crash. Same-volume rename already consumed the staging
    // file, so the surviving artifact is the renamed-to ".incoming" file.
    const QString staging = stagingArchivePathMirror(id);
    const QString incoming = canonical + QStringLiteral(".incoming");
    if (!QFile::exists(staging) && !QFile::exists(incoming)) {
        std::printf("FAIL: an ordinary verification/finalize failure destroyed the only good source "
                    "(neither the staging download nor the renamed .incoming file survived)\n");
        QFile::remove(canonical);
        return false;
    }

    // Regression coverage for the blocker the advisor caught: a leftover
    // .incoming from THIS failed attempt would make both QFile::rename() and
    // QFile::copy() fail on every future attempt too (both refuse an
    // existing destination) -- permanently bricking the issue. Retry the
    // SAME download now that the lock is released (the stale canonical is
    // still sitting there, unlocked, and the .incoming from above is too) --
    // it must succeed, not fail a second time.
    sawFinished = false;
    sawFailed = false;
    comics.downloadIssue(id, postUrl, seriesId, seriesTitle, label, 0);
    if (!waitFor([&] { return sawFinished || sawFailed; })) {
        std::printf("FAIL: the retry after an ordinary failure did not complete within timeout\n");
        return false;
    }
    if (!sawFinished || sawFailed) {
        std::printf("FAIL: retrying after an ordinary safe-move failure did not succeed -- "
                    "a leftover .incoming/.canonical permanently bricked this issue\n");
        QFile::remove(canonical);
        QFile::remove(staging);
        QFile::remove(incoming);
        return false;
    }
    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: the retry reported finished() but the comic isn't marked downloaded\n");
        return false;
    }

    comics.deleteIssue(id);
    QFile::remove(staging);
    QFile::remove(incoming);
    std::printf("OK: an ordinary (non-crash) safe-move failure preserves the source AND a retry succeeds\n");
    return true;
}
#endif

// D2 regression: crash-recovery adoption. A canonical archive from an
// interrupted prior attempt (safe-move completed on disk, saveIndex() never
// ran) sits with no index row -- must be adopted directly on the next
// downloadIssue() call, with NO network touched at all.
bool runCrashRecoveryAdoptionScenario(RedirectingNam* nam, QTemporaryDir& fixtures)
{
    const QString id = QStringLiteral("task4-adoption");
    const QString seriesId = QStringLiteral("gc:task4");
    const QString seriesTitle = QStringLiteral("Task4 Series");
    const QString label = QStringLiteral("Issue Adopt");
    const QStringList pageNames{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    {
        const QString stagingPages = fixtures.path() + QStringLiteral("/adopt-pages");
        QDir().mkpath(stagingPages);
        for (const QString& n : pageNames) {
            QImage img(30, 30, QImage::Format_ARGB32);
            img.fill(qRgb(5, 5, 5));
            img.save(stagingPages + QLatin1Char('/') + n, "JPEG");
        }
        QString buildErr;
        if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, stagingPages, pageNames, &buildErr)) {
            std::printf("FAIL: could not build leftover-canonical fixture: %s\n", qPrintable(buildErr));
            return false;
        }
    }

    ComicDownloader comics(nam);
    if (comics.isDownloaded(id)) {
        std::printf("FAIL: setup -- id was unexpectedly already downloaded before the adoption check\n");
        return false;
    }

    // An address nothing is listening on: if adoption truly skips the
    // network, this is never touched and finished() arrives promptly; if it
    // DOES try to fetch, connection-refused turns into failed(), which this
    // scenario catches as a genuine regression.
    const QString unreachablePostUrl = QStringLiteral("http://127.0.0.1:1/unreachable");

    bool sawFinished = false, sawFailed = false;
    QString failReason;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString& reason) { if (fid == id) { sawFailed = true; failReason = reason; } });

    comics.downloadIssue(id, unreachablePostUrl, seriesId, seriesTitle, label, 0);

    if (!waitFor([&] { return sawFinished || sawFailed; }, 5000)) {
        std::printf("FAIL: adoption scenario did not resolve within timeout\n");
        return false;
    }
    if (sawFailed) {
        std::printf("FAIL: adoption should have skipped the network entirely; got failed(): %s\n",
                    qPrintable(failReason));
        return false;
    }
    if (!sawFinished) { std::printf("FAIL: adoption did not emit finished()\n"); return false; }

    if (!comics.isDownloaded(id)) { std::printf("FAIL: adopted comic not marked downloaded\n"); return false; }
    const QVariantList pages = comics.localPages(id);
    if (pages.size() != pageNames.size()) {
        std::printf("FAIL: adopted comic has %d pages, want %d\n", (int)pages.size(), (int)pageNames.size());
        return false;
    }
    if (!readIndexRaw().contains(id)) {
        std::printf("FAIL: adoption did not persist an index row\n");
        return false;
    }

    comics.deleteIssue(id);
    std::printf("OK: a leftover canonical with no index row is adopted directly, no network touched\n");
    return true;
}

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

    // Task 4: the real writer, driven end-to-end through the public
    // downloadIssue() entry point against a local loopback mock.
    RedirectingNam nam4;
    if (!runFastPathScenario(&nam4, fixtures)) ++failures;
    if (!runFallbackPathScenario(&nam4, fixtures)) ++failures;
#ifdef Q_OS_WIN
    if (!runOrdinaryFailurePreservesSourceScenario(&nam4, fixtures)) ++failures;
#endif
    if (!runCrashRecoveryAdoptionScenario(&nam4, fixtures)) ++failures;

    Q_UNUSED(app);
    return failures == 0 ? 0 : 1;
}
