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
#include "engine/CbzArchive.h"
#include "engine/ComicDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
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
#include <functional>

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
    for (int i = 0; i < names.size(); ++i) {
        // Real JPEG bytes, not placeholder text: Task 5 packs the staging dir
        // into a canonical CBZ and verifies it round-trips via
        // CbzArchive::probe() (which byte-sniffs) before publishing, so the
        // fixture pages must be genuinely decodable -- the old flatten/move
        // path never looked at their content.
        QImage page(40, 40, QImage::Format_ARGB32);
        page.fill(qRgb(10 * i, 90, 170));
        page.save(dir + QLatin1Char('/') + names.at(i), "JPEG");
    }
    return dir;
}

// Pumps the event loop until `pred` is true or the timeout hits. Task 5 made
// publishAssembledEdition() pack the staging dir off the GUI thread (was a
// synchronous validate+move), so its completion now arrives through a
// QFutureWatcher on the event loop -- this scenario is async where it was
// synchronous before.
bool waitFor(const std::function<bool()>& pred, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    }
    return true;
}

// Runs the assembled-edition success scenario. Task 5: the edition now
// publishes as an ARCHIVE row (a real canonical CBZ, `archive` set / `dir`
// empty), not a loose page folder -- so its pages come back from localPages()
// as {archive, entry} descriptors that must genuinely decode via
// CbzArchive::readEntry(), the assembler's page ORDER and parallel `groups`
// must be preserved exactly (probe()'s collator-sort must NOT have reordered
// them), and no loose folder may exist on disk.
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
    QString failReason;
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& fid) { if (fid == id) sawFinished = true; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& fid, const QString& reason) {
            if (fid != id) return;
            sawFailed = true;
            failReason = reason;
        });

    comics.ingestAssembledEdition(id, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                                  QStringLiteral("Assembled Edition One"), stagingDir, names, groups);

    if (!waitFor([&] { return sawFinished || sawFailed; })) {
        std::printf("FAIL: assembled ingest did not complete within timeout\n");
        return false;
    }
    if (sawFailed) {
        std::printf("FAIL: assembled ingest reported failed: %s\n", qPrintable(failReason));
        return false;
    }
    if (QDir(stagingDir).exists()) {
        std::printf("FAIL: staging dir still present after publish\n");
        return false;
    }
    if (QDir(stagingRoot.path() + QStringLiteral("/gc_test")).exists()) {
        std::printf("FAIL: a loose page folder was created -- assembled edition did not become an archive row\n");
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
        const QString archive = p.value(QStringLiteral("archive")).toString();
        const QString entry = p.value(QStringLiteral("entry")).toString();
        if (archive.isEmpty() || entry.isEmpty()) {
            std::printf("FAIL: assembled page %d not an archive descriptor (archive/entry empty)\n", i);
            return false;
        }
        // Page ORDER must be exactly the assembler's, not probe()'s
        // collator-sort -- names[i] must still be page i.
        if (entry != names.at(i)) {
            std::printf("FAIL: assembled page %d order/name drift: got %s want %s\n",
                        i, qPrintable(entry), qPrintable(names.at(i)));
            return false;
        }
        QString readErr;
        const QByteArray decoded = MangaTankoban::CbzArchive::readEntry(archive, entry, &readErr);
        if (decoded.isEmpty()) {
            std::printf("FAIL: assembled page %d does not decode from the archive: %s\n", i, qPrintable(readErr));
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
    std::printf("OK: assembled edition publishes as an archive row, page order + groups preserved\n");
    return true;
}

// Task 5: the traversal-safety / missing-page / duplicate-page validation must
// still reject a bad edition BEFORE any archive is written -- a rejected
// edition leaves no index row and, critically, no half-written canonical CBZ.
bool runAssembledIngestValidationRejectsScenario(QNetworkAccessManager* nam)
{
    ComicDownloader comics(nam);

    QTemporaryDir stagingRoot;
    if (!stagingRoot.isValid()) {
        std::printf("FAIL: validation-scenario staging temp dir invalid\n");
        return false;
    }

    struct Case {
        QString id;
        QStringList declared;   // what the caller claims the pages are
        QStringList onDisk;     // what actually exists in the staging dir
        const char* label;
    };
    const Case cases[] = {
        // A declared page that isn't on disk (missing).
        { QStringLiteral("assembled-reject-missing"),
          { QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg") },
          { QStringLiteral("page_000.jpg") },
          "a missing page is rejected before any archive write" },
        // The same page listed twice (duplicate).
        { QStringLiteral("assembled-reject-dup"),
          { QStringLiteral("page_000.jpg"), QStringLiteral("page_000.jpg") },
          { QStringLiteral("page_000.jpg") },
          "a duplicate page is rejected before any archive write" },
        // A path escaping the staging dir (traversal).
        { QStringLiteral("assembled-reject-escape"),
          { QStringLiteral("../escapee.jpg") },
          { QStringLiteral("page_000.jpg") },
          "an escaping page path is rejected before any archive write" },
    };

    for (const Case& c : cases) {
        comics.deleteIssue(c.id);
        const QString stagingDir = buildAssembledStaging(stagingRoot, c.id, c.onDisk);

        bool sawFinished = false, sawFailed = false;
        QObject::connect(&comics, &ComicDownloader::finished, &comics,
            [&](const QString& fid) { if (fid == c.id) sawFinished = true; });
        QObject::connect(&comics, &ComicDownloader::failed, &comics,
            [&](const QString& fid, const QString&) { if (fid == c.id) sawFailed = true; });

        comics.ingestAssembledEdition(c.id, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                                      QStringLiteral("Reject Case"), stagingDir, c.declared, {});

        // Validation is synchronous and runs before the background pack is ever
        // dispatched, so the reject is observable without pumping -- but pump
        // briefly anyway to prove no async publish sneaks in behind it.
        waitFor([&] { return sawFinished; }, 300);

        if (sawFinished || !sawFailed) {
            std::printf("FAIL: %s -- expected failed(), got %s\n", c.label,
                        sawFinished ? "finished()" : "neither signal");
            return false;
        }
        if (comics.isDownloaded(c.id)) {
            std::printf("FAIL: %s -- a rejected edition was still marked downloaded\n", c.label);
            return false;
        }
        // No index row means no archive path is even known to the app -- the
        // reject happened in the synchronous validation loop, before the
        // background pack (and thus writeImagesAtomic) was ever dispatched.
        // The staging dir is cleaned up by failAndCleanup() on the reject.
        if (QDir(stagingDir).exists()) {
            std::printf("FAIL: %s -- rejected edition left its staging dir behind\n", c.label);
            return false;
        }
        QObject::disconnect(&comics, nullptr, &comics, nullptr);
    }

    std::printf("OK: assembled-edition validation rejects missing/duplicate/escaping pages before any archive write\n");
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

    // ingestAssembledEdition() — separate ComicDownloader instances so these
    // scenarios never interact with the event loop or signal handlers wired up
    // for the scenario above. The success scenario is async as of Task 5 (the
    // publish packs off the GUI thread); the cancel scenarios stay
    // synchronous (they cancel a QUEUED edition, before any packing begins).
    if (!runAssembledIngestSuccessScenario(&nam)) return 1;
    if (!runAssembledIngestValidationRejectsScenario(&nam)) return 1;
    if (!runAssembledIngestCancelScenario(&nam)) return 1;
    if (!runAssembledIngestCancelFailureScenario(&nam)) return 1;

    return 0;
}
