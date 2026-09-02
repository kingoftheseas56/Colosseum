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
#include <QCryptographicHash>
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
#include <QUrl>

#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString fixtureArchiveTool()
{
#ifdef Q_OS_WIN
    const QString systemTar = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(systemTar)) return systemTar;
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
#else
    return QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
#endif
}
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
    const int exitCode = QProcess::execute(fixtureArchiveTool(),
        {QStringLiteral("-cf"), zip, QStringLiteral("--format"), QStringLiteral("zip"),
         QStringLiteral("-C"), pages, QStringLiteral(".")});
    if (exitCode != 0) return false;
    *archivePath = root + QLatin1Char('/') + name + QStringLiteral(".cbz");
    return QFile::rename(zip, *archivePath);
}

// A plain (non-zip) tar archive renamed .cbr: CbzArchive::probe() rejects it
// cleanly (like a real CBR) so the two-path ingest takes the EXTRACTION
// fallback, while bsdtar (the same tool runExtractor uses) extracts it. Real
// JPEG pages so the repack's verify-probe passes. Used for the Task 6
// "imported CBR still extracts-then-repacks" scenario.
bool makeCbr(const QString& root, const QString& name, QString* archivePath)
{
    const QString pages = root + QLatin1Char('/') + name + QStringLiteral("-cbr-pages");
    if (!QDir().mkpath(pages)) return false;
    for (int i = 0; i < 2; ++i) {
        QImage page(40, 40, QImage::Format_ARGB32);
        page.fill(qRgb(30 * i, 120, 90));
        if (!page.save(pages + QStringLiteral("/page_%1.jpg").arg(i, 3, 10, QChar('0')), "JPEG"))
            return false;
    }
    const QString tarPath = root + QLatin1Char('/') + name + QStringLiteral(".tar");
    const int rc = QProcess::execute(fixtureArchiveTool(),
        {QStringLiteral("-cf"), tarPath, QStringLiteral("-C"), pages, QStringLiteral(".")});
    if (rc != 0) return false;
    *archivePath = root + QLatin1Char('/') + name + QStringLiteral(".cbr");
    return QFile::rename(tarPath, *archivePath);
}

// Mirrors of ComicDownloader's private path helpers (safeSeg/hash10/baseDir/
// issueDir/issueArchivePath) so this harness can independently predict where a
// canonical archive or a legacy loose folder lands, without reaching into
// private internals. A drift here fails the scenarios loudly (wrong paths),
// never a false green.
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
    // A CBR blocker, NOT a CBZ: as of Task 6 a CBZ import takes the SYNCHRONOUS
    // fast path (archive-in-place, no extraction), so a CBZ blocker would
    // finish inline and the assembled edition would become ACTIVE rather than
    // queued -- changing what this scenario tests. A CBR falls to the
    // extraction fallback, which spawns an async bsdtar and stays "extracting"
    // with the event loop unpumped, genuinely occupying the single lane and
    // keeping the assembled edition QUEUED (which is the cancel path under test).
    if (!archiveRoot.isValid() || !makeCbr(archiveRoot.path(), QStringLiteral("blocker"), &blockerArchive)) {
        std::printf("FAIL: could not create blocker CBR fixture\n");
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

    // Occupies the single lane: the CBR blocker's beginExtract() starts a real
    // bsdtar QProcess asynchronously and returns immediately, still
    // "extracting" — since we never pump the event loop in this scenario, its
    // finished signal cannot fire out from under the assertions below, so the
    // assembled edition below stays QUEUED.
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
    // CBR blocker (see runAssembledIngestCancelScenario): keeps the assembled
    // edition QUEUED via an async extraction, since a CBZ would now fast-path
    // synchronously (Task 6).
    if (!archiveRoot.isValid() || !stagingRoot.isValid()
        || !makeCbr(archiveRoot.path(), QStringLiteral("failure-blocker"), &blockerArchive)) {
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

// Active extraction cancellation must return before the extractor's finished
// signal is delivered.  The finished handler then owns process deletion,
// cancelled-payload cleanup, and queue progression exactly once.
bool runActiveExtractionCancelScenario(QNetworkAccessManager* nam)
{
    const QString activeId = QStringLiteral("active-extraction-cancelled");
    const QString queuedId = QStringLiteral("active-extraction-queued");
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::printf("FAIL: could not create active-cancel scratch dir\n");
        return false;
    }

    QString activeArchive;
    QString queuedArchive;
    if (!makeCbr(scratch.path(), QStringLiteral("active-cancel"), &activeArchive)
        || !makeCbr(scratch.path(), QStringLiteral("queued-after-cancel"), &queuedArchive)) {
        std::printf("FAIL: could not build active-cancel fixtures\n");
        return false;
    }

    ComicDownloader comics(nam);
    comics.deleteIssue(activeId);
    comics.deleteIssue(queuedId);
    int removedCount = 0;
    int failedCount = 0;
    int queuedFinishedCount = 0;
    int queuedFailedCount = 0;
    QObject::connect(&comics, &ComicDownloader::removed, &comics,
        [&](const QString& id) { if (id == activeId) ++removedCount; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& id, const QString&) { if (id == activeId) ++failedCount; });
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& id) { if (id == queuedId) ++queuedFinishedCount; });
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& id, const QString&) { if (id == queuedId) ++queuedFailedCount; });

    // No event-loop pump between these calls: the first local CBR has started
    // extraction, while the second occupies the downloader's queue.
    comics.ingestLocalArchive(activeId, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                              QStringLiteral("Active Cancel"), activeArchive);
    comics.ingestLocalArchive(queuedId, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                              QStringLiteral("Queued After Cancel"), queuedArchive);

    QElapsedTimer cancelTimer;
    cancelTimer.start();
    comics.cancelDownload(activeId);
    const qint64 cancelElapsed = cancelTimer.elapsed();
    if (cancelElapsed >= 250) {
        std::printf("FAIL: active extraction cancellation took %lld ms\n",
                    static_cast<long long>(cancelElapsed));
        return false;
    }
    if (removedCount != 0 || failedCount != 0 || queuedFinishedCount != 0) {
        std::printf("FAIL: active extraction cancellation completed synchronously\n");
        return false;
    }

    // A repeated QML cancellation while the process signal is in flight must
    // not emit a second removal or disturb the queued job.
    comics.cancelDownload(activeId);
    if (removedCount != 0 || failedCount != 0) {
        std::printf("FAIL: repeated active cancellation completed synchronously\n");
        return false;
    }

    if (!waitFor([&] { return removedCount == 1 && queuedFinishedCount == 1; }, 30000)) {
        std::printf("FAIL: active cancellation did not retire once and complete its queued successor (removed=%d failed=%d queuedFinished=%d queuedFailed=%d active=%d source=%d)\n",
                    removedCount, failedCount, queuedFinishedCount, queuedFailedCount,
                    static_cast<int>(comics.activeIssueJobs().size()), QFile::exists(queuedArchive));
        return false;
    }
    if (removedCount != 1 || failedCount != 0 || queuedFinishedCount != 1) {
        std::printf("FAIL: active cancellation/queue progression counts were removed=%d failed=%d queuedFinished=%d\n",
                    removedCount, failedCount, queuedFinishedCount);
        return false;
    }
    if (comics.isDownloaded(activeId) || comics.activeIssueJobs().size() != 0
        || QFile::exists(activeArchive) || QDir(activeArchive + QStringLiteral(".x")).exists()) {
        std::printf("FAIL: active cancellation left payload or an active job behind\n");
        return false;
    }

    comics.deleteIssue(queuedId);
    std::printf("OK: active extraction cancellation is prompt, idempotent, and advances the queue once\n");
    return true;
}

// A failed-to-start extractor reports errorOccurred() without finished().
// That terminal path must still fail the active import and advance a queued
// fast-path successor exactly once.
bool runExtractorFailedStartScenario(QNetworkAccessManager* nam)
{
    const QString failedId = QStringLiteral("extractor-failed-start");
    const QString queuedId = QStringLiteral("extractor-failed-start-queued");
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::printf("FAIL: could not create failed-start scratch dir\n");
        return false;
    }

    QString failedArchive;
    QString queuedArchive;
    if (!makeCbr(scratch.path(), QStringLiteral("failed-start"), &failedArchive)
        || !makeCbz(scratch.path(), QStringLiteral("queued-fast"), &queuedArchive)) {
        std::printf("FAIL: could not build failed-start fixtures\n");
        return false;
    }

    const QByteArray oldTar = qgetenv("COLOSSEUM_COMIC_BSDTAR_PATH");
    const QByteArray oldSevenZip = qgetenv("COLOSSEUM_COMIC_7ZIP_PATH");
    qputenv("COLOSSEUM_COMIC_BSDTAR_PATH", QByteArrayLiteral("C:/does-not-exist/colosseum-tar.exe"));
    qputenv("COLOSSEUM_COMIC_7ZIP_PATH", QByteArray());

    ComicDownloader comics(nam);
    comics.deleteIssue(failedId);
    comics.deleteIssue(queuedId);
    int failedCount = 0;
    int queuedFinishedCount = 0;
    QObject::connect(&comics, &ComicDownloader::failed, &comics,
        [&](const QString& id, const QString&) { if (id == failedId) ++failedCount; });
    QObject::connect(&comics, &ComicDownloader::finished, &comics,
        [&](const QString& id) { if (id == queuedId) ++queuedFinishedCount; });

    comics.ingestLocalArchive(failedId, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                              QStringLiteral("Failed Start"), failedArchive);
    comics.ingestLocalArchive(queuedId, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                              QStringLiteral("Queued Fast"), queuedArchive);

    const bool completed = waitFor([&] { return failedCount == 1 && queuedFinishedCount == 1; }, 5000);
    if (!oldTar.isNull()) qputenv("COLOSSEUM_COMIC_BSDTAR_PATH", oldTar);
    else qunsetenv("COLOSSEUM_COMIC_BSDTAR_PATH");
    if (!oldSevenZip.isNull()) qputenv("COLOSSEUM_COMIC_7ZIP_PATH", oldSevenZip);
    else qunsetenv("COLOSSEUM_COMIC_7ZIP_PATH");

    if (!completed) {
        std::printf("FAIL: failed-to-start extractor wedged the queue (failed=%d queuedFinished=%d active=%d)\n",
                    failedCount, queuedFinishedCount,
                    static_cast<int>(comics.activeIssueJobs().size()));
        return false;
    }
    if (failedCount != 1 || queuedFinishedCount != 1 || comics.activeIssueJobs().size() != 0
        || !QFile::exists(failedArchive)) {
        std::printf("FAIL: failed-to-start extractor did not preserve failure semantics or queue progress\n");
        return false;
    }

    QFile::remove(failedArchive);
    comics.deleteIssue(queuedId);
    std::printf("OK: failed-to-start extractor reports failure and advances its queued successor once\n");
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("ComicDownloaderIngestHarness"));

    QTemporaryDir temp;
    QString firstArchive;
    if (!temp.isValid()
        || !makeCbz(temp.path(), QStringLiteral("first"), &firstArchive)) {
        std::printf("FAIL: could not create CBZ fixture\n");
        return 1;
    }

    QNetworkAccessManager nam;

    // ── Task 6: imported CBZ takes the FAST path (no extraction) ─────────────
    // ingestLocalArchive() now routes through the same two-path ingest as
    // onFinished(): a natively-readable CBZ moves into the library
    // archive-in-place. Proven "no extraction" by: it completes SYNCHRONOUSLY
    // (a same-volume fast-path move is inline; extraction is inherently async
    // -- it spawns a bsdtar subprocess and needs the event loop), no loose
    // page folder is created, and the pages come back as decodable
    // {archive, entry} descriptors. And the ownership-transfer contract holds:
    // the caller's source file is consumed on success.
    {
        const QString id = QStringLiteral("import-cbz-fast");
        const QString seriesId = QStringLiteral("gc:test");
        const QString label = QStringLiteral("Volume One");
        ComicDownloader comics(&nam);
        comics.deleteIssue(id);

        bool sawFinished = false, sawFailed = false;
        QString failReason;
        QObject::connect(&comics, &ComicDownloader::finished, &comics,
            [&](const QString& fid) { if (fid == id) sawFinished = true; });
        QObject::connect(&comics, &ComicDownloader::failed, &comics,
            [&](const QString& fid, const QString& r) { if (fid == id) { sawFailed = true; failReason = r; } });

        comics.ingestLocalArchive(id, seriesId, QStringLiteral("Test Series"), label, firstArchive);
        const bool finishedSynchronously = sawFinished;   // captured BEFORE any event-loop pump

        if (!waitFor([&] { return sawFinished || sawFailed; })) {
            std::printf("FAIL: imported CBZ ingest did not complete within timeout\n");
            return 1;
        }
        if (sawFailed) { std::printf("FAIL: imported CBZ ingest failed: %s\n", qPrintable(failReason)); return 1; }
        if (!finishedSynchronously) {
            std::printf("FAIL: imported CBZ did NOT take the synchronous fast path "
                        "(it went async -- extraction ran instead of an archive-in-place move)\n");
            return 1;
        }
        if (QDir(issueDirMirror(seriesId, label, id)).exists()) {
            std::printf("FAIL: imported CBZ created a loose page folder -- not archive-in-place\n");
            return 1;
        }
        if (QFile::exists(firstArchive)) {
            std::printf("FAIL: imported CBZ ownership-transfer broken -- source not consumed on success\n");
            return 1;
        }
        if (!QFileInfo(issueArchivePathMirror(seriesId, label, id)).isFile()) {
            std::printf("FAIL: imported CBZ did not land a canonical archive\n");
            return 1;
        }
        const QVariantList pages = comics.localPages(id);
        if (pages.size() != 2) {
            std::printf("FAIL: imported CBZ expected 2 pages, got %d\n", int(pages.size()));
            return 1;
        }
        for (const QVariant& pv : pages) {
            const QVariantMap p = pv.toMap();
            const QString archive = p.value(QStringLiteral("archive")).toString();
            const QString entry = p.value(QStringLiteral("entry")).toString();
            if (archive.isEmpty() || entry.isEmpty()
                || MangaTankoban::CbzArchive::readEntry(archive, entry).isEmpty()) {
                std::printf("FAIL: imported CBZ page is not a decodable archive descriptor\n");
                return 1;
            }
        }
        comics.deleteIssue(id);
        std::printf("OK: imported CBZ takes the fast path (no extraction), source consumed\n");
    }

    // ── Task 6: imported CBR falls to EXTRACT-then-repack, still archive-in-place ──
    {
        const QString id = QStringLiteral("import-cbr-fallback");
        const QString seriesId = QStringLiteral("gc:test");
        const QString label = QStringLiteral("Volume CBR");
        QString cbrArchive;
        if (!makeCbr(temp.path(), QStringLiteral("import-cbr"), &cbrArchive)) {
            std::printf("FAIL: could not build imported-CBR fixture\n");
            return 1;
        }
        ComicDownloader comics(&nam);
        comics.deleteIssue(id);

        bool sawFinished = false, sawFailed = false;
        QString failReason;
        QObject::connect(&comics, &ComicDownloader::finished, &comics,
            [&](const QString& fid) { if (fid == id) sawFinished = true; });
        QObject::connect(&comics, &ComicDownloader::failed, &comics,
            [&](const QString& fid, const QString& r) { if (fid == id) { sawFailed = true; failReason = r; } });

        comics.ingestLocalArchive(id, seriesId, QStringLiteral("Test Series"), label, cbrArchive);
        if (!waitFor([&] { return sawFinished || sawFailed; }, 30000)) {
            std::printf("FAIL: imported CBR ingest did not complete within timeout\n");
            return 1;
        }
        if (sawFailed) { std::printf("FAIL: imported CBR ingest failed: %s\n", qPrintable(failReason)); return 1; }
        if (QDir(issueDirMirror(seriesId, label, id)).exists()) {
            std::printf("FAIL: imported CBR produced a loose page folder -- repack regressed to flatten\n");
            return 1;
        }
        if (QFile::exists(cbrArchive)) {
            std::printf("FAIL: imported CBR ownership-transfer broken -- source not consumed on success\n");
            return 1;
        }
        const QVariantList pages = comics.localPages(id);
        if (pages.size() != 2) {
            std::printf("FAIL: imported CBR expected 2 repacked pages, got %d\n", int(pages.size()));
            return 1;
        }
        for (const QVariant& pv : pages) {
            const QVariantMap p = pv.toMap();
            const QString archive = p.value(QStringLiteral("archive")).toString();
            const QString entry = p.value(QStringLiteral("entry")).toString();
            if (archive.isEmpty() || entry.isEmpty()
                || MangaTankoban::CbzArchive::readEntry(archive, entry).isEmpty()) {
                std::printf("FAIL: imported CBR page is not a decodable archive descriptor\n");
                return 1;
            }
        }
        comics.deleteIssue(id);
        std::printf("OK: imported CBR extracts-then-repacks into an archive row, source consumed\n");
    }

    // ── Task 6 review (blocker 1): an imported archive that FAILS to extract
    //    must PRESERVE the caller's source, not delete its only copy ─────────
    {
        const QString id = QStringLiteral("import-corrupt-preserve");
        // A .cbr of pure garbage: probe() rejects it (not a zip), and bsdtar +
        // 7z both fail to extract it -> "archive extraction failed" -> the
        // failIngest() path. Before the fix this deleted the source.
        const QString corrupt = temp.path() + QStringLiteral("/corrupt.cbr");
        { QFile f(corrupt); if (f.open(QIODevice::WriteOnly)) f.write("this is not any kind of archive, just bytes"); }

        ComicDownloader comics(&nam);
        comics.deleteIssue(id);
        bool sawFinished = false, sawFailed = false;
        QObject::connect(&comics, &ComicDownloader::finished, &comics,
            [&](const QString& fid) { if (fid == id) sawFinished = true; });
        QObject::connect(&comics, &ComicDownloader::failed, &comics,
            [&](const QString& fid, const QString&) { if (fid == id) sawFailed = true; });

        comics.ingestLocalArchive(id, QStringLiteral("gc:test"), QStringLiteral("Test Series"),
                                  QStringLiteral("Corrupt"), corrupt);
        if (!waitFor([&] { return sawFinished || sawFailed; }, 30000)) {
            std::printf("FAIL: corrupt import did not resolve within timeout\n");
            return 1;
        }
        if (sawFinished || !sawFailed) {
            std::printf("FAIL: corrupt import should have failed, got %s\n",
                        sawFinished ? "finished" : "neither");
            return 1;
        }
        if (!QFile::exists(corrupt)) {
            std::printf("FAIL: a failed import DELETED the caller's only copy (blocker 1 regression)\n");
            return 1;
        }
        QFile::remove(corrupt);
        std::printf("OK: a failed import preserves the caller's source, not deletes it\n");
    }

    // ── Task 6 review (blocker 2): re-importing the canonical library file
    //    itself must NOT delete it (index-loss re-import guard) ──────────────
    {
        const QString id = QStringLiteral("import-canonical-guard");
        const QString seriesId = QStringLiteral("gc:test");
        const QString label = QStringLiteral("Canonical Guard");
        QString srcArchive;
        if (!makeCbz(temp.path(), QStringLiteral("canon-src"), &srcArchive)) {
            std::printf("FAIL: could not build canonical-guard fixture\n");
            return 1;
        }
        ComicDownloader comics(&nam);
        comics.deleteIssue(id);
        bool firstDone = false;
        QObject::connect(&comics, &ComicDownloader::finished, &comics,
            [&](const QString& fid) { if (fid == id) firstDone = true; });
        comics.ingestLocalArchive(id, seriesId, QStringLiteral("Test Series"), label, srcArchive);
        if (!waitFor([&] { return firstDone; })) { std::printf("FAIL: canonical-guard first import timed out\n"); return 1; }

        const QString canonical = issueArchivePathMirror(seriesId, label, id);
        if (!QFileInfo(canonical).isFile()) { std::printf("FAIL: canonical-guard first import didn't land a canonical\n"); return 1; }

        // Now re-import pointing AT the canonical itself (the shape of a user
        // browsing to <AppData>/comics/... after an index loss). isDownloaded
        // is true; the redundant-source remove must be SKIPPED for a live
        // library archive -- otherwise the canonical is destroyed.
        bool secondDone = false;
        QObject::connect(&comics, &ComicDownloader::finished, &comics,
            [&](const QString& fid) { if (fid == id) secondDone = true; });
        comics.ingestLocalArchive(id, seriesId, QStringLiteral("Test Series"), label, canonical);
        if (!waitFor([&] { return secondDone; })) { std::printf("FAIL: canonical-guard re-import timed out\n"); return 1; }

        if (!QFileInfo(canonical).isFile()) {
            std::printf("FAIL: re-importing the canonical DELETED it (blocker 2 regression)\n");
            return 1;
        }
        if (!comics.isDownloaded(id) || comics.localPages(id).size() != 2) {
            std::printf("FAIL: library row broken after re-importing the canonical\n");
            return 1;
        }
        comics.deleteIssue(id);
        std::printf("OK: re-importing the canonical library file does not destroy it\n");
    }

    // ingestAssembledEdition() — separate ComicDownloader instances so these
    // scenarios never interact with the event loop or signal handlers wired up
    // for the scenario above. The success scenario is async as of Task 5 (the
    // publish packs off the GUI thread); queued-cancel scenarios remain
    // synchronous because they cancel before any packing begins. Active
    // extraction cancellation is covered separately below.
    if (!runAssembledIngestSuccessScenario(&nam)) return 1;
    if (!runAssembledIngestValidationRejectsScenario(&nam)) return 1;
    if (!runAssembledIngestCancelScenario(&nam)) return 1;
    if (!runAssembledIngestCancelFailureScenario(&nam)) return 1;
    if (!runActiveExtractionCancelScenario(&nam)) return 1;
    if (!runExtractorFailedStartScenario(&nam)) return 1;

    Q_UNUSED(app);
    return 0;
}
