// tests/comicreader_decode_harness.cpp
//
// Comic Reader (Agent 1, plan 2026-07-23) — Task 4 fixtures.
// The generation-safe decode coordinator: a pooled QImageReader decode whose
// results are re-checked against the live generation ON THE OWNING THREAD before
// they touch the cache or reach a client, so a rapid entry switch (A -> B) can
// never paint a stale page. A QCoreApplication event loop is required for the
// queued worker -> owning-thread report-back; each test pumps it via a spin-wait.
//
// House CHECK idiom: collect every failure (never abort), print each FAIL, then
// print exactly COMICREADER_DECODE_OK iff zero failures, else return 1.

#include "comicreader/ComicReaderDecode.h"
#include "comicreader/ComicReaderPageCache.h"
#include "comicreader/ComicReaderTypes.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPair>
#include <QSemaphore>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <atomic>
#include <cstdio>
#include <functional>
#include <tuple>

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
// Returns whether the predicate became true. Also used purely to drain events
// (pass a predicate that is deliberately never true with a short timeout).
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

// Drain queued events for a fixed window (no predicate) — used to give a stale
// worker's report-back time to arrive AND be dropped before we assert absence.
static void drainFor(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        QThread::msleep(1);
    }
}

static PageMeta makeMeta(int index, const QString& path) {
    PageMeta m;
    m.index = index;
    m.localPath = path;
    return m;
}

// Records every signal the coordinator emits, tagged with its generation, so a
// test can assert both what fired and — critically — that nothing fired for a
// superseded generation.
struct SignalLog {
    QVector<QPair<quint64, int>> ready;    // (gen, page)
    QVector<QPair<quint64, PageMeta>> meta; // (gen, meta)
    QVector<std::tuple<quint64, int, PageError>> failed;

    void wire(ComicReaderDecode& d) {
        QObject::connect(&d, &ComicReaderDecode::pageReady,
                         [this](quint64 g, int p) { ready.append({g, p}); });
        QObject::connect(&d, &ComicReaderDecode::metaReady,
                         [this](quint64 g, PageMeta m) { meta.append({g, m}); });
        QObject::connect(&d, &ComicReaderDecode::pageFailed,
                         [this](quint64 g, int p, PageError e) {
                             failed.append(std::make_tuple(g, p, e));
                         });
    }
    bool anyForGen(quint64 gen) const {
        for (const auto& r : ready)
            if (r.first == gen) return true;
        for (const auto& m : meta)
            if (m.first == gen) return true;
        for (const auto& f : failed)
            if (std::get<0>(f) == gen) return true;
        return false;
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── Fixtures: real PNGs plus non-image bytes and a nonexistent path ───────
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create QTemporaryDir\n");
        std::puts("1 failure(s)");
        return 1;
    }
    const QString portraitPath = dir.filePath("portrait.png");
    const QString landscapePath = dir.filePath("landscape.png");
    const QString textPath = dir.filePath("text.png");
    const QString missingPath = dir.filePath("does_not_exist.png");

    {
        QImage portrait(900, 1400, QImage::Format_ARGB32);
        portrait.fill(qRgb(40, 80, 160));
        CHECK(portrait.save(portraitPath, "PNG"), "setup: portrait PNG saved");

        QImage landscape(1400, 900, QImage::Format_ARGB32);
        landscape.fill(qRgb(160, 80, 40));
        CHECK(landscape.save(landscapePath, "PNG"), "setup: landscape PNG saved");

        QFile text(textPath);
        CHECK(text.open(QIODevice::WriteOnly), "setup: text file opened");
        text.write("this is plain text pretending to be a png, not image data\n");
        text.close();
    }

    // ── Test 1: valid portrait decodes, publishes real size, lands in cache ───
    {
        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        decode.openGeneration(7, {makeMeta(0, portraitPath)});
        decode.request(0, 100);
        const bool got = waitFor([&] { return !log.ready.isEmpty(); });

        CHECK(got, "T1 pageReady fired for the valid portrait");
        CHECK(log.ready.size() == 1 && log.ready[0].first == 7u && log.ready[0].second == 0,
              "T1 pageReady carries the right gen/page (7,0)");
        CHECK(cache.get(7, 0).has_value(), "T1 decoded image is in the cache at (7,0)");
        CHECK(!log.meta.isEmpty() && log.meta[0].first == 7,
              "T1 metaReady fired with gen 7");
        CHECK(!log.meta.isEmpty() && log.meta[0].second.sourceSize == QSize(900, 1400),
              "T1 metaReady carries the real sourceSize 900x1400");
        CHECK(!log.meta.isEmpty() && log.meta[0].second.decoded,
              "T1 metaReady marks the page decoded");
        CHECK(!log.meta.isEmpty() && log.meta[0].second.detectedSpread == false,
              "T1 portrait is NOT detected as a spread");
        CHECK(log.failed.isEmpty(), "T1 no failure for the valid portrait");
    }

    // ── Test 2: landscape sets detectedSpread (spreadRatioExceeded) ───────────
    {
        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        decode.openGeneration(8, {makeMeta(0, landscapePath)});
        decode.request(0, 100);
        const bool got = waitFor([&] { return !log.meta.isEmpty(); });

        CHECK(got, "T2 metaReady fired for the landscape page");
        CHECK(!log.meta.isEmpty() && log.meta[0].second.sourceSize == QSize(1400, 900),
              "T2 metaReady carries the real sourceSize 1400x900");
        CHECK(!log.meta.isEmpty() && log.meta[0].second.detectedSpread == true,
              "T2 landscape IS detected as a spread (width >= 1.08*height)");
        CHECK(cache.get(8, 0).has_value(), "T2 landscape image is cached");
    }

    // ── Test 3: missing path → pageFailed(MissingFile), nothing cached ────────
    {
        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        decode.openGeneration(9, {makeMeta(0, missingPath)});
        decode.request(0, 100);
        const bool got = waitFor([&] { return !log.failed.isEmpty(); });

        CHECK(got, "T3 pageFailed fired for the missing path");
        CHECK(!log.failed.isEmpty() && std::get<2>(log.failed[0]) == PageError::MissingFile,
              "T3 error is MissingFile");
        CHECK(!log.failed.isEmpty() && std::get<0>(log.failed[0]) == 9,
              "T3 failure tagged with gen 9");
        CHECK(!cache.get(9, 0).has_value(), "T3 nothing cached for a missing file");
        CHECK(log.ready.isEmpty(), "T3 no pageReady for a missing file");
    }

    // ── Test 4: text bytes in a .png → pageFailed(DecodeFailed) ───────────────
    {
        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        decode.openGeneration(10, {makeMeta(0, textPath)});
        decode.request(0, 100);
        const bool got = waitFor([&] { return !log.failed.isEmpty(); });

        CHECK(got, "T4 pageFailed fired for the non-image bytes");
        CHECK(!log.failed.isEmpty() && std::get<2>(log.failed[0]) == PageError::DecodeFailed,
              "T4 error is DecodeFailed for undecodable bytes");
        CHECK(!cache.get(10, 0).has_value(), "T4 nothing cached for undecodable bytes");
    }

    // ── Test 5: STALE-GENERATION GUARD (the critical one), real latch ─────────
    // Gen A's worker is held on a semaphore latch inside its onEnter hook. While
    // it is blocked we openGeneration(B) and decode B's page cleanly. Only THEN
    // do we release the latch so A's worker finishes and reports back — and the
    // stale guard must drop it: no signal, and no cache entry, ever tagged genA.
    {
        constexpr quint64 genA = 100;
        constexpr quint64 genB = 101;

        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        QSemaphore latch(0);
        std::atomic<bool> enteredA{false};
        std::atomic<bool> exitedA{false};
        decode.setWorkerHooksForTest(
            [&](quint64 gen, int) {
                if (gen == genA) {
                    enteredA.store(true);
                    latch.acquire();      // block gen A's worker until released
                }
            },
            [&](quint64 gen, int) {
                if (gen == genA)
                    exitedA.store(true);
            });

        decode.openGeneration(genA, {makeMeta(0, portraitPath)});
        decode.request(0, 100);
        const bool aStarted = waitFor([&] { return enteredA.load(); });
        CHECK(aStarted, "T5 gen A's worker started and is blocked on the latch");

        // Supersede A with B while A is still in flight, then decode B cleanly.
        decode.openGeneration(genB, {makeMeta(0, landscapePath)});
        decode.request(0, 100);
        const bool bReady = waitFor([&] {
            for (const auto& r : log.ready)
                if (r.first == genB) return true;
            return false;
        });
        CHECK(bReady, "T5 gen B's page decodes and reports while A is still blocked");

        // Release A: it now finishes and reports a SUPERSEDED result.
        latch.release();
        const bool aDone = waitFor([&] { return exitedA.load(); });
        CHECK(aDone, "T5 gen A's worker finished after release");
        drainFor(150); // let A's queued report-back arrive AND be dropped

        CHECK(!log.anyForGen(genA),
              "T5 STALE GUARD: no signal was EVER emitted for the superseded gen A");
        CHECK(!cache.get(genA, 0).has_value(),
              "T5 STALE GUARD: nothing tagged gen A was ever inserted into the cache");
        CHECK(cache.get(genB, 0).has_value(), "T5 gen B's page is cached");
    }

    // ── Test 6: pool never exceeds 2 concurrent workers ───────────────────────
    // onEnter increments a live counter (updating a high-water mark) then sleeps
    // to force overlap; onExit decrements. With maxThreadCount(2), the high-water
    // mark must reach 2 (real concurrency) yet never exceed it.
    {
        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        std::atomic<int> active{0};
        std::atomic<int> highWater{0};
        decode.setWorkerHooksForTest(
            [&](quint64, int) {
                const int cur = ++active;
                int prev = highWater.load();
                while (cur > prev && !highWater.compare_exchange_weak(prev, cur)) {
                    // prev is refreshed by compare_exchange_weak on failure
                }
                QThread::msleep(35); // hold the lane so peers overlap
            },
            [&](quint64, int) { --active; });

        QVector<PageMeta> pages;
        for (int i = 0; i < 8; ++i)
            pages.append(makeMeta(i, portraitPath)); // all point at the same real PNG
        decode.openGeneration(200, pages);
        for (int i = 0; i < 8; ++i)
            decode.request(i, 100 - i);

        const bool allDone = waitFor([&] { return log.ready.size() == 8; }, 8000);
        CHECK(allDone, "T6 all 8 pages eventually decoded");
        CHECK(highWater.load() <= 2, "T6 pool NEVER exceeds 2 concurrent workers");
        CHECK(highWater.load() == 2, "T6 pool actually reaches 2 concurrent workers (real parallelism)");
    }

    // ── Test 7: dedup — a duplicate request while inflight enqueues one worker ─
    {
        ComicReaderPageCache cache;
        SignalLog log;                 // declared before `decode` so decode tears
        ComicReaderDecode decode(&cache); // down first, while log's &-captures live
        log.wire(decode);

        std::atomic<int> started{0};
        decode.setWorkerHooksForTest(
            [&](quint64, int) { ++started; },
            std::function<void(quint64, int)>());

        decode.openGeneration(300, {makeMeta(0, portraitPath)});
        decode.request(0, 100);
        decode.request(0, 100); // duplicate of an inflight (gen,page) → must be dropped

        const bool got = waitFor([&] { return !log.ready.isEmpty(); });
        CHECK(got, "T7 the deduped page still decodes once");
        drainFor(50); // catch any erroneous second worker/report
        CHECK(started.load() == 1, "T7 exactly one worker started for a doubly-requested page");
        CHECK(log.ready.size() == 1, "T7 pageReady fired exactly once");
    }

    // ── Test 8: QUEUE FLUSH on generation switch ──────────────────────────────
    // openGeneration must flush already-start()ed-but-not-yet-running gen-A
    // runnables so they never run ahead of gen-B's priority-100 page. We hold the
    // 2 running lanes on a latch so the rest stay queued, switch to gen B, then
    // release — the queued gen-A work must never have run.
    {
        constexpr quint64 genA = 400;
        constexpr quint64 genB = 401;

        ComicReaderPageCache cache;
        SignalLog log;
        ComicReaderDecode decode(&cache);
        log.wire(decode);

        QSemaphore latch(0);
        std::atomic<int> enteredA{0};
        decode.setWorkerHooksForTest(
            [&](quint64 gen, int) {
                if (gen == genA) {
                    ++enteredA;
                    latch.acquire(); // hold the running lane so peers stay queued
                }
            },
            std::function<void(quint64, int)>());

        QVector<PageMeta> pagesA;
        for (int i = 0; i < 6; ++i)
            pagesA.append(makeMeta(i, portraitPath));
        decode.openGeneration(genA, pagesA);
        for (int i = 0; i < 6; ++i)
            decode.request(i, 100); // 2 lanes start+block; the other 4 stay queued

        const bool twoRunning = waitFor([&] { return enteredA.load() == 2; });
        CHECK(twoRunning, "T8 two gen-A lanes are running, the other 4 requests are queued");

        // Switch generations while the 4 are still queued: this must flush them.
        QVector<PageMeta> pagesB;
        for (int i = 0; i < 6; ++i)
            pagesB.append(makeMeta(i, landscapePath));
        decode.openGeneration(genB, pagesB);
        decode.request(0, 100); // gen-B visible page (queues behind the 2 held lanes)

        latch.release(8); // release the 2 held gen-A workers; queued gen-A were flushed
        const bool bReady = waitFor([&] {
            for (const auto& r : log.ready)
                if (r.first == genB) return true;
            return false;
        });
        CHECK(bReady, "T8 gen-B visible page decodes after the switch");
        drainFor(150);

        CHECK(enteredA.load() == 2,
              "T8 QUEUE FLUSH: only the 2 already-running gen-A workers ever ran; "
              "the 4 queued were cleared by openGeneration");
        CHECK(!log.anyForGen(genA),
              "T8 no gen-A signal ever fired (running ones stale-dropped, queued never ran)");
        bool anyGenACached = false;
        for (int i = 0; i < 6; ++i)
            if (cache.get(genA, i).has_value())
                anyGenACached = true;
        CHECK(!anyGenACached, "T8 no gen-A page was ever inserted into the cache");
    }

    // ── Test 9: per-generation failure memoization + reset ────────────────────
    // A permanently-failing page is decoded once, then re-requests in the same
    // generation are no-ops (no re-decode, no repeat signal). A new generation
    // resets the failed set, so the page gets one fresh attempt.
    {
        ComicReaderPageCache cache;
        SignalLog log;
        ComicReaderDecode decode(&cache);
        log.wire(decode);

        std::atomic<int> started{0};
        decode.setWorkerHooksForTest(
            [&](quint64, int) { ++started; },
            std::function<void(quint64, int)>());

        decode.openGeneration(500, {makeMeta(0, missingPath)});
        decode.request(0, 100);
        const bool failed1 = waitFor([&] { return !log.failed.isEmpty(); });
        CHECK(failed1, "T9 the missing page fails once");
        const int startedAfterFirst = started.load();
        CHECK(startedAfterFirst == 1, "T9 exactly one worker ran for the first attempt");

        // Re-request the SAME page in the SAME generation → memoized no-op.
        decode.request(0, 100);
        decode.request(0, 100);
        drainFor(80);
        CHECK(started.load() == startedAfterFirst,
              "T9 MEMOIZE: a re-request in the same generation starts NO new worker");
        CHECK(log.failed.size() == 1, "T9 MEMOIZE: no second pageFailed in the same generation");

        // New generation with the same page list → failed set reset → retried.
        decode.openGeneration(501, {makeMeta(0, missingPath)});
        decode.request(0, 100);
        const bool failed2 = waitFor([&] { return log.failed.size() == 2; });
        CHECK(failed2, "T9 RESET: a new generation retries the page and it fails once more");
        CHECK(started.load() == startedAfterFirst + 1,
              "T9 RESET: exactly one new worker ran for the retry");
    }

    // ── Test 10: recognized-but-unsupported format → UnsupportedImage ─────────
    // A minimal TIFF header. This Qt build ships no TIFF handler, so QImageReader
    // returns UnsupportedFormatError; because the bytes carry the TIFF magic, the
    // coordinator must classify it UnsupportedImage (not DecodeFailed). Guarded by
    // supportedImageFormats() so a Qt that DOES bundle tiff simply skips (there
    // the fixture would decode) — the assertion only runs where it is meaningful.
    {
        const bool tiffUnsupported =
            !QImageReader::supportedImageFormats().contains(QByteArray("tiff"));
        if (!tiffUnsupported) {
            std::fprintf(stderr,
                         "NOTE: T10 skipped — this Qt has a tiff handler; "
                         "UnsupportedImage mapping not exercised here\n");
        } else {
            const QString tiffPath = dir.filePath("mini.tiff");
            {
                QFile f(tiffPath);
                CHECK(f.open(QIODevice::WriteOnly), "T10 setup: tiff fixture opened");
                const unsigned char hdr[] = {0x49, 0x49, 0x2A, 0x00,
                                             0x08, 0x00, 0x00, 0x00}; // "II*\0" + IFD offset
                f.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
                f.close();
            }

            ComicReaderPageCache cache;
            SignalLog log;
            ComicReaderDecode decode(&cache);
            log.wire(decode);

            decode.openGeneration(600, {makeMeta(0, tiffPath)});
            decode.request(0, 100);
            const bool got = waitFor([&] { return !log.failed.isEmpty(); });

            CHECK(got, "T10 the tiff page fails (no tiff handler in this Qt)");
            CHECK(!log.failed.isEmpty() &&
                      std::get<2>(log.failed[0]) == PageError::UnsupportedImage,
                  "T10 a recognized-but-unhandled format maps to UnsupportedImage");
            CHECK(log.ready.isEmpty(), "T10 no pageReady for an unsupported format");
        }
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_DECODE_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
