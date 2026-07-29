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
//   3. STALE-SAFE — a request tagged with a retired generation resolves null, so
//      a QML Image still bound to a closed volume can never repaint its pixels.
//      Not quite the synchronous provider's behaviour: the guard is now read on
//      the worker thread, so a generation that retires in flight also nulls
//      (F7). Strictly more conservative, never less — see
//      ComicReaderImageResponse.h.
//
// Cancellation here is DETERMINISTIC, not timing-raced: publication is queued
// back onto the response's own thread, so a cancel() issued from that same
// thread — before the harness pumps — is strictly ordered ahead of it no matter
// how fast the worker finished.
//
// F1-F5 go through the provider and its pool (the integration path), and F1
// pins the property the whole task exists for: the work ran on a DIFFERENT
// thread from the one that asked for it. Nothing else here would catch a
// provider that dropped its pool and ran inline, since queued publication alone
// satisfies every other timing assertion. F6-F8 hold the worker by driving one
// response's run() BY HAND — QRunnable::run() and the
// constructor are both public Qt surface, so this buys determinism without
// adding a single line of production code for testing's sake. Held that way,
// "the worker had not started yet" and "the volume closed while this worker was
// pending" are facts rather than timing hopes.
//
// Where the three cancel checkpoints land:
//   F6 pins the one BEFORE the cache fetch — and proves the fetch really was
//      skipped, using the cache's own LRU as the witness rather than trusting a
//      null result, which every cancelled response produces anyway.
//   F8 pins the one AT PUBLICATION. This is the checkpoint carrying the
//      correctness guarantee: whatever the worker already computed, a cancelled
//      response shows QML nothing.
//   The middle one — after the fetch, before the scale — is deliberately NOT
//      pinned. It has no externally observable effect: a cancel caught there
//      and a cancel caught at publication both serve nothing, so all it ever
//      does is save CPU. Pinning a cancel mid-run() would need a production
//      test hook, which is not worth buying for an unobservable optimisation.
//      (Note it does NOT cover "cancelled mid-scaledToWidth" either — nothing
//      interrupts a running scale; that case is caught at publication, by F8.)
//
// QGuiApplication, not QCoreApplication like this family's other harnesses:
// QQuickTextureFactory::textureFactoryForImage() resolves the scenegraph
// adaptation backend and crashes without a GUI application. Asserting on the
// factory a real request produces is the only way to check what was actually
// served, so the harness pays for a QGuiApplication. No window is ever shown —
// but it does now need a QPA platform, so a headless run wants
// QT_QPA_PLATFORM=offscreen where this harness family previously needed nothing.
//
// No Qt6::Test, despite the plan's snippet: QVERIFY/QTRY_COMPARE expand to
// QTEST_FAIL_ACTION (a bare `return`) and depend on QTest::runningTest(), so
// they only work inside a void QTest slot — not in a main() that collects
// failures, which is the house idiom this file was asked to match. That leaves
// only QSignalSpy, which FinishedCounter below covers in six lines. Linking it
// IS available (native/player2/CMakeLists.txt already does, and find_package is
// re-callable with extra components, so no shared line needs touching) — it is
// declined on merit, not availability.
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
#include <QMetaObject>
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
    explicit FinishedCounter(QQuickImageResponse* response)
        : m_connection(QObject::connect(response, &QQuickImageResponse::finished,
                                        [this] { ++m_count; })) {}
    // The lambda captures `this`, so the connection must not outlive the
    // counter. Today it never would (every fixture outlives its counter), but
    // later tasks extend this file and should not have to notice that.
    ~FinishedCounter() { QObject::disconnect(m_connection); }

    FinishedCounter(const FinishedCounter&) = delete;
    FinishedCounter& operator=(const FinishedCounter&) = delete;

    int count() const { return m_count; }

private:
    QMetaObject::Connection m_connection;
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

// 1024x1024 ARGB32 — exactly 4 MiB, so a budget can be stated in whole pages.
static QImage squarePage(QRgb color) {
    QImage img(1024, 1024, QImage::Format_ARGB32);
    img.fill(color);
    return img;
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
        // THE point of Task 1. Without this the whole suite would still pass
        // with the pool deleted and run() called inline, because queued
        // publication alone satisfies every other timing assertion here.
        CHECK(response->servedOn() != nullptr && response->servedOn() != QThread::currentThread(),
              "F1 the cache read and the scale ran OFF the requesting thread");
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
        QThread::msleep(60);
        QCoreApplication::processEvents();
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
            QStringLiteral("%1/-1").arg(kLiveGen),  // negative page (see note below)
            QStringLiteral(""),                     // empty id
        };
        // On "-1": parseKey rejects it outright, but that rejection is NOT what
        // this fixture observes — a negative page could never be in the cache
        // either, so the miss path produces the same null. The check earns its
        // place defensively (it makes the guard say what it means, and keeps a
        // negative index away from any future keying); this case only pins that
        // the id resolves cleanly instead of crashing.
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

    // ── Fixture 6: a cancel that beats the worker skips the cache entirely ───
    // Cancel checkpoint ONE. The worker is HELD — run() is called by hand, so
    // "the cancel landed before the worker started" is a fact. A null result
    // alone would prove nothing (every cancelled response is null), so the
    // cache's own LRU is the witness: get() marks a page most-recently-used, so
    // if page 0 was never fetched it stays the OLDEST and is the one evicted
    // when a fourth page arrives. Had the worker fetched it, page 1 would go
    // instead. Same technique as comicreader_cache_harness Fixture 1, read
    // backwards.
    //
    // TWO ASSUMPTIONS THIS WITNESS RIDES ON, both pinned rather than trusted:
    //  (a) squarePage() is exactly 4 MiB with no row padding, so three of them
    //      fill the budget precisely and a fourth forces exactly one eviction.
    //      The bytesUsed() CHECK below pins that (as comicreader_cache_harness
    //      does); without it an image-format change would silently turn this
    //      fixture into a pass-by-accident.
    //  (b) ComicReaderPageCache::get() promotes to most-recently-used. If a
    //      future task adds a non-promoting peek to that unit — Task 2 is
    //      scheduled to — and the response is moved onto it, this fixture will
    //      start failing for a reason that has nothing to do with cancellation.
    //      That is the diagnosis, not a mystery: re-express the witness.
    {
        constexpr qint64 kPageBytes = 4LL * 1024 * 1024; // one squarePage()
        ComicReaderPageCache cache(3 * kPageBytes);      // exactly three fit
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, 0, squarePage(qRgb(1, 0, 0))); // oldest
        cache.insert(kLiveGen, 1, squarePage(qRgb(0, 1, 0)));
        cache.insert(kLiveGen, 2, squarePage(qRgb(0, 0, 1)));
        CHECK(cache.bytesUsed() == 3 * kPageBytes,
              "F6 setup: three pages exactly fill the budget (pins squarePage() at 4 MiB, unpadded)");

        ComicReaderImageResponse response(&cache, &live, QStringLiteral("%1/0").arg(kLiveGen),
                                          QSize(900, 0));
        FinishedCounter finished(&response);

        response.cancel(); // lands before the worker body — that is the whole point
        response.run();

        CHECK(waitFor([&] { return finished.count() > 0; }), "F6 a worker cancelled before it ran still finishes");
        CHECK(finished.count() == 1, "F6 exactly one finished()");
        CHECK(response.textureFactory() == nullptr, "F6 it serves NULL");

        cache.insert(kLiveGen, 3, squarePage(qRgb(1, 1, 0))); // forces one eviction
        CHECK(!cache.get(kLiveGen, 0).has_value(),
              "F6 page 0 was never fetched — it stayed least-recently-used and took the eviction");
        CHECK(cache.get(kLiveGen, 1).has_value(),
              "F6 page 1 survives (it is the one that would have gone had the worker touched page 0)");
    }

    // ── Fixture 7: the generation retires WHILE a worker is held ─────────────
    // The request is built against a live generation 7, the volume then closes,
    // and only afterwards does the worker run. This is the case a real fast
    // entry switch produces, and the reason the guard cannot be hoisted back to
    // request time: Fixture 3 covers the generation already being dead when the
    // request arrives; this covers it dying in flight.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, kPage, fullResPage(qRgb(70, 70, 70)));

        ComicReaderImageResponse held(&cache, &live,
                                      QStringLiteral("%1/%2?rev=1").arg(kLiveGen).arg(kPage),
                                      QSize(900, 0));
        FinishedCounter finished(&held);

        live.store(kLiveGen + 1); // the volume closes with this worker still pending
        held.run();

        CHECK(waitFor([&] { return finished.count() > 0; }), "F7 the held worker finishes once released");
        CHECK(finished.count() == 1, "F7 exactly one finished()");
        CHECK(!held.wasCancelled(), "F7 a retired generation is not a cancellation");
        CHECK(held.textureFactory() == nullptr,
              "F7 a generation retired AFTER the request serves NULL — the guard runs at WORK time");
    }

    // ── Fixture 8: a cancel landing after the work, before publication ───────
    // Cancel checkpoint THREE, pinned exactly. The worker has already fetched
    // AND scaled a real image; the cancel still wins, because publication is
    // queued behind it. This is the checkpoint that carries the correctness
    // guarantee. It is also where a cancel arriving mid-scale would land — not
    // because this fixture stages one, but because nothing interrupts a running
    // scaledToWidth, so no earlier checkpoint could ever catch it.
    {
        ComicReaderPageCache cache;
        std::atomic<quint64> live{kLiveGen};
        cache.insert(kLiveGen, kPage, fullResPage(qRgb(90, 90, 90)));

        ComicReaderImageResponse response(&cache, &live,
                                          QStringLiteral("%1/%2").arg(kLiveGen).arg(kPage),
                                          QSize(900, 0));
        FinishedCounter finished(&response);

        response.run();    // the full fetch + 2400px -> 900px scale really happens
        CHECK(finished.count() == 0, "F8 publication is still queued when run() returns");
        response.cancel(); // ...and only now does the cancel land

        CHECK(waitFor([&] { return finished.count() > 0; }), "F8 the response finishes");
        CHECK(finished.count() == 1, "F8 exactly one finished()");
        CHECK(response.wasCancelled(), "F8 the response reports itself cancelled");
        CHECK(response.textureFactory() == nullptr,
              "F8 a scaled image already in hand is STILL withheld from a cancelled response");
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_PROVIDER_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
