// Progressive chapter delivery: pending is not a missing local file.
#include "comicreader/ComicReaderCore.h"
#include "comicreader/ComicReaderStripModel.h"
#include <QGuiApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <atomic>
#include <cstdio>
#include <functional>
using namespace comicreader;
static int failures = 0;
#define CHECK(c, text) do { if (!(c)) { ++failures; std::fprintf(stderr, "FAIL: %s\n", text); } } while (0)
static bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer clock; clock.start();
    while (!predicate() && clock.elapsed() < 4000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}
static bool deliver(ComicReaderCore& core, const QString& id, int page, const QUrl& url) {
    bool result = false;
    const bool invoked = QMetaObject::invokeMethod(&core, "pageArrived", Qt::DirectConnection,
        Q_RETURN_ARG(bool, result), Q_ARG(QString, id), Q_ARG(int, page), Q_ARG(QUrl, url));
    CHECK(invoked, "pageArrived is callable through the QML/native boundary");
    return result;
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid(), "isolated fixture directory exists");
    const QString ready = dir.filePath("page_000.png");
    QImage portrait(240, 400, QImage::Format_RGB32); portrait.fill(Qt::white);
    CHECK(portrait.save(ready), "fixture page saved");
    for (const QString direction : {QStringLiteral("ltr"), QStringLiteral("rtl")}) {
        for (const bool strip : {false, true}) {
            const QString id = direction + (strip ? "-strip" : "-paged");
            const QString later = dir.filePath(id + "_later.png");
            const QUrl laterUrl = QUrl::fromLocalFile(later);
            QVariantList pages;
            pages << QVariantMap{{"url", QUrl::fromLocalFile(ready)}, {"index", 0}};
            for (int i = 1; i < 5; ++i)
                pages << QVariantMap{{"url", laterUrl}, {"index", i}, {"pending", true}};
            std::atomic<int> pendingDecodes{0};
            ComicReaderCore core;
            core.setDecodeWorkerHooksForTest([&](quint64, int page) {
                if (page > 0) ++pendingDecodes;
            }, {});
            core.openEntry(id, pages, direction, {{"couplingMode", "manual"}, {"couplingPhase", "normal"}});
            CHECK(core.pageCount() == 5, "pending pages preserve the complete ordered page count");
            CHECK(core.pageInfo(2).value("pending").toBool(), "undelivered page reports pending");
            CHECK(core.pageInfo(2).value("error").toString() == "none", "pending is not MissingFile");
            core.setVisible({2, 3});
            if (strip) { core.setStripViewportWidth(1000); core.setStripViewport(0, 10000); }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
            CHECK(pendingDecodes.load() == 0, "undelivered pages never enter the decoder or failure cooldown");
            CHECK(core.presentationForPage(2).value("state") == "waiting", "a unit waits rather than showing a broken-page placard");
            if (core.metaObject()->indexOfMethod("pageArrived(QString,int,QUrl)") < 0) {
                CHECK(false, "arrival endpoint exists");
                continue;
            }
            QImage spread(800, 300, QImage::Format_RGB32); spread.fill(Qt::gray);
            CHECK(spread.save(later), "late real landscape image saved");
            CHECK(!deliver(core, "stale-entry", 2, laterUrl), "stale entry callback is rejected");
            CHECK(!deliver(core, id, -1, laterUrl), "negative page rejected");
            CHECK(!deliver(core, id, 99, laterUrl), "past-end page rejected");
            CHECK(!deliver(core, id, 2, QUrl("https://example.invalid/page.png")), "arrival never enables remote image decode");
            CHECK(core.pageInfo(2).value("pending").toBool(), "rejected arrival leaves pending intact");
            CHECK(deliver(core, id, 2, laterUrl), "arrival admits a completed local page");
            CHECK(!core.pageInfo(2).value("pending").toBool(), "arrival clears pending immediately");
            CHECK(waitFor([&] { return core.pageInfo(2).value("decoded").toBool(); }), "visible arrival decodes without reopening the entry");
            CHECK(core.pageInfo(2).value("error").toString() == "none", "late page has no negative-cache/cooldown residue");
            CHECK(core.unitForPage(2).value("spread").toBool(), "late dimensions recompute the canonical pairing");
            if (strip) CHECK(core.stripPageHeight(2) < 500, "long strip uses real landscape height after arrival");
            CHECK(!deliver(core, id, 2, laterUrl), "duplicate arrival is a no-op, not a duplicate decode");
            const bool invoked = QMetaObject::invokeMethod(&core, "pageDownloadFailed", Qt::DirectConnection,
                Q_ARG(QString, id), Q_ARG(int, 3));
            CHECK(invoked, "download failure endpoint exists");
            CHECK(core.pageInfo(3).value("error").toString() == "download_failed", "exhausted transport shows a retryable page error");
            int retried = 0;
            QObject::connect(&core, &ComicReaderCore::pageRetried, &core, [&](int p) { if (p == 3) ++retried; });
            core.retryPage(3);
            CHECK(retried == 1, "pending retry reaches its transport-facing signal exactly once");
            CHECK(core.pageInfo(3).value("pending").toBool(), "retry waits for transport instead of decoding an absent file");
            CHECK(deliver(core, id, 3, laterUrl), "retry can subsequently deliver its image");
            CHECK(waitFor([&] { return core.pageInfo(3).value("decoded").toBool(); }), "retried image is decoded");
        }
    }
    ComicReaderCore oldPath;
    oldPath.openEntry("permanent", {QVariantMap{{"url", QUrl::fromLocalFile(dir.filePath("absent.png"))}}}, "ltr", {});
    CHECK(oldPath.pageInfo(0).value("error") == "missing_file", "ordinary missing downloaded files retain their old behavior");
    CHECK(!oldPath.pageInfo(0).value("pending").toBool(), "normal reader never becomes live implicitly");
    if (!failures) std::puts("COMICREADER_LIVE_PAGES_OK");
    return failures ? 1 : 0;
}
