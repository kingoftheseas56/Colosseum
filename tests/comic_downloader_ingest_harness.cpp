// External-archive ingest contract: two locally produced CBZs enter the existing
// ComicDownloader extraction queue, finish under their original issue IDs, and
// expose reader-shaped localPages through ComicDownloader itself.
//
// Also covers Task 7's C++-only ingestAssembledEdition() boundary (design:
// docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-volume-mode-
// design.md, "ComicDownloader ingest boundary"): a Task-6 ComicEditionAssembler
// staging dir publishes through the SAME index/reader contract GetComics
// downloads use, atomically, queued behind the same single lane, and a
// cancel while queued leaves no index record and no partial comics dir.
#include "engine/ComicDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Real, genuinely decodable JPEG bytes -- placeholder text ("not-decoded-by-
// this-contract-test") was sufficient before Task 4 (CBZ-in-place plan): the
// old finalizeExtract() only ever moved bytes around, never looked at them.
// Task 4's rewrite packs the extraction output into a canonical CBZ and
// VERIFIES it round-trips via CbzArchive::probe() (which byte-sniffs sampled
// entries) before publishing -- so a fixture whose "pages" don't look like
// real images now legitimately fails that check, exactly as it should for a
// truly corrupt source. ingestLocalArchive() still shares this same
// beginExtract()/finalizeExtract() lane (Task 6 is what converges its own
// entry point onto the two-path ingest), so this fixture change is a direct,
// unavoidable consequence of Task 4 landing, not scope creep.
bool makeCbz(const QString& root, const QString& name, QString* archivePath)
{
    const QString pages = root + QLatin1Char('/') + name + QStringLiteral("-pages");
    if (!QDir().mkpath(pages)) return false;
    for (int i = 0; i < 2; ++i) {
        QImage page(40, 40, QImage::Format_ARGB32);
        page.fill(qRgb(10 * i, 80, 160));
        if (!page.save(pages + QStringLiteral("/page%1.jpg").arg(i), "JPEG")) return false;
    }

    const QString zip = root + QLatin1Char('/') + name + QStringLiteral(".zip");
    const int exitCode = QProcess::execute(QStringLiteral("C:/Windows/System32/tar.exe"),
        {QStringLiteral("-a"), QStringLiteral("-cf"), zip,
         QStringLiteral("-C"), pages, QStringLiteral(".")});
    if (exitCode != 0) return false;
    *archivePath = root + QLatin1Char('/') + name + QStringLiteral(".cbz");
    return QFile::rename(zip, *archivePath);
}

// Builds a Task-6-shaped "<id>.staging" dir of page_NNN.<ext> fixture files
// directly under a QTemporaryDir root (the harness's own scratch app-data,
// never the real comics library — that side stays isolated the same way the
// rest of this file already isolates it: a dedicated org/app name plus
// explicit deleteIssue() cleanup before/after).
QString buildAssembledStaging(const QTemporaryDir& root, const QString& id, const QStringList& names)
{
    const QString dir = root.path() + QLatin1Char('/') + id + QStringLiteral(".staging");
    QDir().mkpath(dir);
    for (const QString& name : names) {
        QFile f(dir + QLatin1Char('/') + name);
        if (f.open(QIODevice::WriteOnly)) f.write("assembled-page-bytes");
    }
    return dir;
}

// Runs the assembled-edition success scenario against its own ComicDownloader
// instance. Fully synchronous — publishAssembledEdition() is a validate+move,
// no network/subprocess — so no event loop needs to be pumped. Returns true
// and prints OK, or prints FAIL and returns false.
bool runAssembledIngestSuccessScenario(QNetworkAccessManager* nam)
{
    const QString id = QStringLiteral("assembled-ingest-1");
    ComicDownloader comics(nam);
    comics.deleteIssue(id);

    QTemporaryDir stagingRoot;
    if (!stagingRoot.isValid()) {
        std::printf("FAIL: assembled staging temp dir invalid\n");
        return false;
    }
    const QStringList names = { QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg"),
                                 QStringLiteral("page_002.jpg"), QStringLiteral("page_003.jpg") };
    const QString stagingDir = buildAssembledStaging(stagingRoot, id, names);
    const QList<int> groups = { 0, 0, 1, 1 };

    bool sawFinished = false;
    bool sawFailed = false;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString& reason) {
            if (fid != id) return;
            sawFailed = true;
            std::printf("FAIL: assembled ingest reported failed: %s\n", qPrintable(reason));
        });

    comics.ingestAssembledEdition(id, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                                  QStringLiteral("Assembled Edition One"), stagingDir, names, groups);

    if (sawFailed || !sawFinished) {
        std::printf("FAIL: assembled ingest did not finish cleanly\n");
        return false;
    }
    if (QDir(stagingDir).exists()) {
        std::printf("FAIL: staging dir still present after atomic publish\n");
        return false;
    }
    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: assembled edition not marked downloaded\n");
        return false;
    }

    const QVariantList pages = comics.localPages(id);
    if (pages.size() != names.size()) {
        std::printf("FAIL: expected %d assembled pages, got %d\n", int(names.size()), int(pages.size()));
        return false;
    }
    for (int i = 0; i < pages.size(); ++i) {
        const QVariantMap p = pages.at(i).toMap();
        const QString localFile = QUrl(p.value(QStringLiteral("url")).toString()).toLocalFile();
        if (!QFileInfo::exists(localFile)) {
            std::printf("FAIL: assembled page %d file missing on disk (%s)\n", i, qPrintable(localFile));
            return false;
        }
        if (p.value(QStringLiteral("group")).toInt() != groups.at(i)) {
            std::printf("FAIL: assembled page %d group mismatch (got %d, want %d)\n",
                        i, p.value(QStringLiteral("group")).toInt(), groups.at(i));
            return false;
        }
    }

    int matches = 0;
    for (const QVariant& row : comics.downloadedIssues())
        if (row.toMap().value(QStringLiteral("id")).toString() == id) ++matches;
    if (matches != 1) {
        std::printf("FAIL: expected exactly one index entry for %s, found %d\n", qPrintable(id), matches);
        return false;
    }

    comics.deleteIssue(id);
    std::printf("OK: assembled edition ingest publishes through the Comics contract\n");
    return true;
}

// Cancel-before-publication: force the assembled ingest to QUEUE behind a
// still-extracting blocker job (a real archive ingest, async QProcess, event
// loop never pumped here — so it cannot possibly finish out from under this
// scenario), then cancel the queued edition. No index record, no partial
// comics dir, and the staging dir this call brought is cleaned up too.
bool runAssembledIngestCancelScenario(QNetworkAccessManager* nam)
{
    const QString blockerId = QStringLiteral("assembled-ingest-blocker");
    const QString id = QStringLiteral("assembled-ingest-cancelled");
    ComicDownloader comics(nam);
    comics.deleteIssue(blockerId);
    comics.deleteIssue(id);

    QTemporaryDir archiveRoot;
    QString blockerArchive;
    if (!archiveRoot.isValid() || !makeCbz(archiveRoot.path(), QStringLiteral("blocker"), &blockerArchive)) {
        std::printf("FAIL: could not create blocker CBZ fixture\n");
        return false;
    }

    QTemporaryDir stagingRoot;
    if (!stagingRoot.isValid()) {
        std::printf("FAIL: cancel-scenario staging temp dir invalid\n");
        return false;
    }
    const QStringList names = { QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg") };
    const QString stagingDir = buildAssembledStaging(stagingRoot, id, names);

    bool sawFinished = false;
    bool sawFailed = false;
    bool sawRemoved = false;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString&) { if (fid == id) sawFailed = true; });
    QObject::connect(&comics, &ComicDownloader::removed, &comics,
        [&](const QString& fid) { if (fid == id) sawRemoved = true; });

    // Occupies the single lane: beginExtract() starts a real bsdtar QProcess
    // asynchronously and returns immediately, still "extracting" — since we
    // never call app.exec() in this scenario, its finished signal cannot fire
    // out from under the assertions below.
    comics.ingestLocalArchive(blockerId, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                              QStringLiteral("Blocker"), blockerArchive);
    comics.ingestAssembledEdition(id, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                                  QStringLiteral("Assembled Edition Cancelled"), stagingDir, names,
                                  QList<int>{});
    comics.cancelDownload(id);

    if (sawFinished || sawFailed || !sawRemoved) {
        std::printf("FAIL: queued assembled ingest was not cleanly cancelled\n");
        return false;
    }
    if (comics.isDownloaded(id)) {
        std::printf("FAIL: cancelled assembled edition still marked downloaded\n");
        return false;
    }
    if (comics.localPages(id).size() != 0) {
        std::printf("FAIL: cancelled assembled edition still has reader pages\n");
        return false;
    }
    if (QDir(stagingDir).exists()) {
        std::printf("FAIL: cancelled assembled ingest left a staging dir behind\n");
        return false;
    }

    comics.cancelDownload(blockerId);   // stop the blocker too — nothing left running
    comics.deleteIssue(id);
    comics.deleteIssue(blockerId);
    std::printf("OK: cancelled/queued assembled ingest leaves no index record or partial dir\n");
    return true;
}

// A cancelled queued job may fail to delete its staging payload.  On Windows a
// file handle which omits FILE_SHARE_DELETE is a deterministic, real-world
// deletion denial (the same shape as a previewer or scanner holding a file).
// The downloader must report that failure and must not claim the item was
// removed while payload remains on disk.
bool runAssembledIngestCancelFailureScenario(QNetworkAccessManager* nam)
{
#ifndef Q_OS_WIN
    Q_UNUSED(nam);
    return true;
#else
    const QString blockerId = QStringLiteral("assembled-ingest-failure-blocker");
    const QString id = QStringLiteral("assembled-ingest-cancel-failure");
    ComicDownloader comics(nam);
    comics.deleteIssue(blockerId);
    comics.deleteIssue(id);

    QTemporaryDir archiveRoot;
    QString blockerArchive;
    QTemporaryDir stagingRoot;
    if (!archiveRoot.isValid() || !stagingRoot.isValid()
        || !makeCbz(archiveRoot.path(), QStringLiteral("failure-blocker"), &blockerArchive)) {
        std::printf("FAIL: could not create cancellation-failure fixtures\n");
        return false;
    }

    const QStringList names = { QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg") };
    const QString stagingDir = buildAssembledStaging(stagingRoot, id, names);
    const std::wstring lockedPath = (stagingDir + QStringLiteral("/page_000.jpg")).toStdWString();
    const HANDLE lockedFile = CreateFileW(lockedPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lockedFile == INVALID_HANDLE_VALUE) {
        std::printf("FAIL: could not lock staged page for cancellation-failure scenario\n");
        return false;
    }

    bool sawFailed = false;
    bool sawRemoved = false;
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString&) { if (fid == id) sawFailed = true; });
    QObject::connect(&comics, &ComicDownloader::removed, &comics,
        [&](const QString& fid) { if (fid == id) sawRemoved = true; });

    comics.ingestLocalArchive(blockerId, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                              QStringLiteral("Failure Blocker"), blockerArchive);
    comics.ingestAssembledEdition(id, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                                  QStringLiteral("Assembled Edition Cancel Failure"), stagingDir, names,
                                  QList<int>{});
    comics.cancelDownload(id);
    CloseHandle(lockedFile);

    const bool ok = sawFailed && !sawRemoved && !comics.isDownloaded(id)
        && QDir(stagingDir).exists();
    if (!ok)
        std::printf("FAIL: cancellation deletion failure was not surfaced truthfully\n");

    QDir(stagingDir).removeRecursively();
    comics.cancelDownload(blockerId);
    comics.deleteIssue(id);
    comics.deleteIssue(blockerId);
    if (!ok) return false;
    std::printf("OK: failed cancellation preserves payload and emits failure, not removed\n");
    return true;
#endif
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("ComicDownloaderIngestHarness"));

    QTemporaryDir temp;
    QString firstArchive;
    QString secondArchive;
    if (!temp.isValid()
        || !makeCbz(temp.path(), QStringLiteral("first"), &firstArchive)
        || !makeCbz(temp.path(), QStringLiteral("second"), &secondArchive)) {
        std::printf("FAIL: could not create CBZ fixtures\n");
        return 1;
    }

    QNetworkAccessManager nam;
    ComicDownloader comics(&nam);
    comics.deleteIssue(QStringLiteral("torrent-ingest-1"));
    comics.deleteIssue(QStringLiteral("torrent-ingest-2"));

    int finishedCount = 0;
    QObject::connect(&comics, &ComicDownloader::failed, &app,
        [&app](const QString& id, const QString& reason) {
            std::printf("FAIL: %s: %s\n", qPrintable(id), qPrintable(reason));
            app.exit(1);
        });
    QObject::connect(&comics, &ComicDownloader::finished, &app,
        [&](const QString& id) {
            if (comics.localPages(id).size() != 2
                || comics.statusOf(id).value(QStringLiteral("state")).toString() != QStringLiteral("done")) {
                std::printf("FAIL: %s did not surface two reader pages as done\n", qPrintable(id));
                app.exit(1);
                return;
            }
            if (++finishedCount == 2) app.exit(0);
        });

    comics.ingestLocalArchive(QStringLiteral("torrent-ingest-1"), QStringLiteral("gc:test"),
                              QStringLiteral("Test Series"), QStringLiteral("Volume One"), firstArchive);
    comics.ingestLocalArchive(QStringLiteral("torrent-ingest-2"), QStringLiteral("gc:test"),
                              QStringLiteral("Test Series"), QStringLiteral("Volume Two"), secondArchive);

    QTimer::singleShot(30000, &app, [&app]() {
        std::printf("FAIL: extraction timeout\n");
        app.exit(2);
    });
    const int code = app.exec();
    comics.deleteIssue(QStringLiteral("torrent-ingest-1"));
    comics.deleteIssue(QStringLiteral("torrent-ingest-2"));
    if (code != 0) return code;

    // Task 7: ingestAssembledEdition() — separate ComicDownloader instances so
    // these fully-synchronous scenarios never interact with the event loop or
    // signal handlers wired up for the scenario above.
    if (!runAssembledIngestSuccessScenario(&nam)) return 1;
    if (!runAssembledIngestCancelScenario(&nam)) return 1;
    if (!runAssembledIngestCancelFailureScenario(&nam)) return 1;

    return 0;
}
