// tests/comicreader_provider_harness.cpp
//
// Comic Reader overhaul (plan 2026-07-28) — Task 1 fixtures.
// ComicReaderProvider is now an ASYNC provider: every image://comicreader/ hit
// hands QML a ComicReaderImageResponse that does the cache lookup and the
// SmoothTransformation downscale on a worker thread, and can be cancelled the
// moment the user scrolls the page out of the window.
//
// The three properties this harness locks:
//   1. ASYNC — nothing is published while the caller is still on its own stack;
//      the result only lands once the response's thread pumps its event loop.
//   2. CANCELLABLE — a cancel() that arrives before publication wins, and still
//      produces exactly ONE finished() so QML's own bookkeeping never dangles.
//   3. STALE-SAFE — the read-only generation guard the synchronous provider had
//      is unchanged: a request tagged with a retired generation resolves null,
//      so a QML Image still bound to a closed volume can never repaint its
//      pixels.
//
// Cancellation here is DETERMINISTIC, not timing-raced: publication is queued
// back onto the response's own thread, so a cancel() issued from that same
// thread — before the harness pumps — is strictly ordered ahead of it no matter
// how fast the worker finished.
//
// QGuiApplication, not QCoreApplication like this family's other harnesses:
// QQuickTextureFactory::textureFactoryForImage() resolves the scenegraph
// adaptation backend and crashes without a GUI application. Asserting on the
// factory a real request produces is the only way to check what was actually
// served, so the harness pays for a QGuiApplication. No window is ever shown.
//
// House CHECK idiom: collect every failure (never abort), print each FAIL, then
// print exactly COMICREADER_PROVIDER_OK iff zero failures, else return 1.

#include "comicreader/ComicReaderImageResponse.h"
#include "comicreader/ComicReaderPageCache.h"
#include "comicreader/ComicReaderProvider.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <functional>

using namespace comicreader;

static int g_failures = 0;
#define CHECK(cond, label)                                        \
    do {                                                          \
        if (!(cond)) {                                            \
            std::fprintf(stderr, "FAIL: %s\n", (label));          \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

// Pump the owning thread's event loop until `pred` is true or the timeout hits.
static bool waitFor(const std::function<bool()>& pred, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        QThread::msleep(1);
    }
    return true;
}

// Counts finished() emissions on one response (the QSignalSpy role, without
// dragging Qt6::Test into a tree whose harnesses have never linked it).
class FinishedCounter {
public:
    explicit FinishedCounter(QQuickImageResponse* response) {
        QObject::connect(response, &QQuickImageResponse::finished,
                         [this] { ++m_count; });
    }
    int count() const { return m_count; }

private:
    int m_count = 0;
};

// requestImageResponse() returns Qt's base type; this provider only ever builds
// ComicReaderImageResponse, and the harness needs the concrete type to read
// wasCancelled().
static ComicReaderImageResponse* request(ComicReaderProvider& provider, const QString& id,
                                         const QSize& requestedSize) {
    return static_cast<ComicReaderImageResponse*>(
        provider.requestImageResponse(id, requestedSize));
}

// A full-resolution page: 2400px wide, the width the reader actually caches.
static QImage fullResPage(QRgb color) {
    QImage img(2400, 1600, QImage::Format_ARGB32);
    img.fill(color);
    return img;
}

// The size of what the response actually served, or an invalid QSize when it
// served nothing. Qt hands texture-factory ownership to the caller.
static QSize serveAndTakeSize(QQuickImageResponse* response) {
    QQuickTextureFactory* factory = response->textureFactory();
    if (!factory)
        return QSize();
    const QSize size = factory->textureSize();
    delete factory;
    return size;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    constexpr quint64 kLiveGen = 7;
    constexpr int kPage = 3;

    // ── Fixture 1: a cached page is served ASYNCHRONOUSLY, and downscaled ────
    // The request asks for 900px of a 2400px page. Nothing may be published
    // while main() is still on its own stack — the response only resolves once
    // the event loop runs — and what finally lands is the SCALED image, which
    // is the whole point of moving the scale off the GUI thread.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, kPage, fullResPage(qRgb(200, 40, 40)));
        ComicReaderProvider provider(&cache, &live);

        ComicReaderImageResponse* response = request(
            provider, QStringLiteral("%1/%2?rev=1&dpr=1").arg(kLiveGen).arg(kPage), QSize(900, 0));
        FinishedCounter finished(response);

        CHECK(finished.count() == 0, "F1 nothing has finished before the event loop runs");
        CHECK(response->textureFactory() == nullptr,
              "F1 the result is NOT published synchronously (async, not a disguised blocking call)");

        CHECK(waitFor([&] { return finished.count() > 0; }), "F1 the response finishes");
        CHECK(finished.count() == 1, "F1 exactly one finished()");
        CHECK(!response->wasCancelled(), "F1 an uncancelled response reports so");
        CHECK(serveAndTakeSize(response) == QSize(900, 600),
              "F1 the served page is the cached 2400px page scaled to the requested 900px width");
        delete response;
    }

    // ── Fixture 2: cancel() before publication wins, deterministically ───────
    // Cancel is issued on the response's own thread with the event loop NOT yet
    // pumped, so the queued publication cannot have run — the check the worker
    // makes immediately before publishing is the one under test here. Exactly
    // one finished() must still arrive (QML unblocks its Image either way) and
    // it must carry NOTHING.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, kPage, fullResPage(qRgb(40, 200, 40)));
        ComicReaderProvider provider(&cache, &live);

        ComicReaderImageResponse* response = request(
            provider, QStringLiteral("%1/%2?rev=1&dpr=1").arg(kLiveGen).arg(kPage), QSize(900, 0));
        FinishedCounter finished(response);

        response->cancel(); // before any pumping — strictly ahead of publication

        CHECK(waitFor([&] { return finished.count() > 0; }), "F2 a cancelled response still finishes");
        CHECK(finished.count() == 1, "F2 exactly one finished() after cancel");
        CHECK(response->wasCancelled(), "F2 the response reports itself cancelled");
        CHECK(response->textureFactory() == nullptr,
              "F2 a cancelled response publishes NOTHING even though the page was cached");

        // And it stays finished exactly once — no late second emission.
        waitFor([] { return false; }, 60);
        CHECK(finished.count() == 1, "F2 no second finished() arrives later");
        delete response;
    }

    // ── Fixture 3: a retired generation resolves null (the stale guard) ──────
    // The load-bearing guard carried over from the synchronous provider: bump
    // the live generation, then ask for the old one. The page is STILL sitting
    // in the cache, so only the guard can make this null.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, kPage, fullResPage(qRgb(40, 40, 200)));
        ComicReaderProvider provider(&cache, &live);

        live.store(kLiveGen + 1); // the volume closed; generation 7 is retired

        ComicReaderImageResponse* stale = request(
            provider, QStringLiteral("%1/%2?rev=1&dpr=1").arg(kLiveGen).arg(kPage), QSize(900, 0));
        FinishedCounter finished(stale);

        CHECK(waitFor([&] { return finished.count() > 0; }), "F3 the stale response finishes");
        CHECK(finished.count() == 1, "F3 exactly one finished() for the stale request");
        CHECK(stale->textureFactory() == nullptr,
              "F3 a superseded generation serves NULL even though its page is still cached");
        CHECK(!stale->wasCancelled(), "F3 stale is not the same as cancelled");
        delete stale;
    }

    // ── Fixture 4: cache miss and malformed ids finish cleanly with null ─────
    // A page the decoder has not published yet, and ids that do not parse at
    // all. None may hang, none may crash, all resolve to a null result.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, kPage, fullResPage(qRgb(120, 120, 120)));
        ComicReaderProvider provider(&cache, &live);

        const QString ids[] = {
            QStringLiteral("%1/99").arg(kLiveGen),  // live generation, page never decoded
            QStringLiteral("not-a-real-id"),        // no slash at all
            QStringLiteral("/4"),                   // empty generation
            QStringLiteral("%1/abc").arg(kLiveGen), // unparseable page
            QStringLiteral(""),                     // empty id
        };
        for (const QString& id : ids) {
            const QByteArray tag = QStringLiteral("F4 id \"%1\": ").arg(id).toUtf8();
            ComicReaderImageResponse* response = request(provider, id, QSize(900, 0));
            // Never a null response — QML dereferences whatever comes back.
            CHECK(response != nullptr, (tag + "the provider still hands back a response").constData());
            FinishedCounter finished(response);
            CHECK(waitFor([&] { return finished.count() > 0; }),
                  (tag + "finishes (never hangs QML's Image)").constData());
            CHECK(finished.count() == 1, (tag + "exactly one finished()").constData());
            CHECK(response->textureFactory() == nullptr, (tag + "serves NULL").constData());
            delete response;
        }
    }

    // ── Fixture 5: a page narrower than the request is served untouched ──────
    // The synchronous provider only ever scaled DOWN; upscaling a decoded page
    // to a bigger requested width would be pure waste. Same rule, async path.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        QImage small(600, 400, QImage::Format_ARGB32);
        small.fill(qRgb(10, 10, 10));
        cache.insert(kLiveGen, kPage, small);
        ComicReaderProvider provider(&cache, &live);

        ComicReaderImageResponse* response = request(
            provider, QStringLiteral("%1/%2").arg(kLiveGen).arg(kPage), QSize(900, 0));
        FinishedCounter finished(response);
        CHECK(waitFor([&] { return finished.count() > 0; }), "F5 the response finishes");
        CHECK(serveAndTakeSize(response) == QSize(600, 400),
              "F5 a page already narrower than the requested width is served unscaled");
        delete response;

        // No requested width at all (QSize()) is the same no-scale case.
        ComicReaderImageResponse* unsized = request(
            provider, QStringLiteral("%1/%2").arg(kLiveGen).arg(kPage), QSize());
        FinishedCounter unsizedFinished(unsized);
        CHECK(waitFor([&] { return unsizedFinished.count() > 0; }), "F5 the unsized response finishes");
        CHECK(serveAndTakeSize(unsized) == QSize(600, 400),
              "F5 a request with no width is served at full cached resolution");
        delete unsized;
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_PROVIDER_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
