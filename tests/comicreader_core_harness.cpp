// tests/comicreader_core_harness.cpp
//
// Comic Reader (Agent 1, plan 2026-07-23) — Task 7 fixtures.
// ComicReaderCore is the ONE app-facing backend: it owns and orchestrates the
// five pure engine modules (types+pairing, cache, decode coordinator, coupling
// probe, strip model) behind the QML-facing surface, and hands out a read-only
// image://comicreader/ provider. This harness drives the orchestration as a
// plain state machine over REAL decoded PNG pages (decode is async, so a
// QCoreApplication event loop is pumped for its queued report-backs).
//
// House CHECK idiom: collect every failure (never abort), print each FAIL, then
// print exactly COMICREADER_CORE_OK iff zero failures, else return 1.

#include "comicreader/ComicReaderCore.h"
#include "comicreader/ComicReaderProvider.h"
#include "comicreader/ComicReaderRenderProfile.h"  // T32-T34: the Image panel's transform
#include "comicreader/ComicReaderScaleCache.h"   // T26 reads the scaled tier's residents
#include "comicreader/ComicReaderStripModel.h"   // T14 reads the strip geometry roles
#include "comicreader/ComicReaderTypes.h"
#include "engine/CbzArchive.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMutex>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSemaphore>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

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

// Ask the (now async) provider for `id` and report what it finally SERVED:
// true when a real image landed, false when it resolved to nothing — a retired
// generation, an uncached page, or an id that does not parse. Pumps this
// thread's event loop, because the response publishes back onto it.
static bool providerServes(comicreader::ComicReaderProvider* provider, const QString& id) {
    QQuickImageResponse* response = provider->requestImageResponse(id, QSize());
    bool done = false;
    QObject::connect(response, &QQuickImageResponse::finished, [&done] { done = true; });
    const bool finished = waitFor([&] { return done; });
    QQuickTextureFactory* factory = finished ? response->textureFactory() : nullptr;
    const bool served = factory != nullptr;
    delete factory;
    delete response;
    return served;
}

// The same request, but hand back the PIXELS the provider served (a null QImage
// when it resolved to nothing). T34 needs this: "the render profile reaches the
// delivered page" is only provable by looking at what was delivered.
static QImage providerServedImage(comicreader::ComicReaderProvider* provider, const QString& id) {
    QQuickImageResponse* response = provider->requestImageResponse(id, QSize());
    bool done = false;
    QObject::connect(response, &QQuickImageResponse::finished, [&done] { done = true; });
    const bool finished = waitFor([&] { return done; });
    QQuickTextureFactory* factory = finished ? response->textureFactory() : nullptr;
    const QImage served = factory ? factory->image() : QImage();
    delete factory;
    delete response;
    return served;
}

// Write a solid-gray PNG (portrait by default) whose edge luminance is `lum`,
// so edgeContinuityCost between two solid pages is |lumA - lumB| / 255.
static bool writeSolidPng(const QString& path, int lum, int w = 400, int h = 600) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(qRgb(lum, lum, lum));
    return img.save(path, "PNG");
}

// Build the openEntry() pages payload (QVariantList of {index,url,group}) from a
// list of local file paths, as file:// URLs (exactly what pageStore.localPages
// yields in production).
static QVariantList pagesFromPaths(const QStringList& paths) {
    QVariantList out;
    for (int i = 0; i < paths.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("url"), QUrl::fromLocalFile(paths[i]).toString());
        m.insert(QStringLiteral("group"), -1);
        out.append(m);
    }
    return out;
}

// persisted map that pins coupling to MANUAL/NORMAL so the auto-coupling probe
// never runs — used by every fixture that wants a deterministic pairing with no
// async probe rebuild racing the assertions.
static QVariantMap manualNormal() {
    QVariantMap p;
    p.insert(QStringLiteral("couplingMode"), QStringLiteral("manual"));
    p.insert(QStringLiteral("couplingPhase"), QStringLiteral("normal"));
    return p;
}

int main(int argc, char** argv) {
    // QGuiApplication, not QCoreApplication: since the overhaul plan's Task 1 the
    // provider is async, and reading what a response SERVED means asking it for a
    // QQuickTextureFactory — which resolves the scenegraph adaptation backend and
    // needs a GUI application. No window is ever shown, but this harness now needs
    // a QPA platform where it previously needed none: headless runs want
    // QT_QPA_PLATFORM=offscreen.
    QGuiApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create QTemporaryDir\n");
        std::puts("1 failure(s)");
        return 1;
    }

    // ── Continuity fixture: 7 solid portrait pages engineered so that the
    //    SHIFTED pairing's touching edges are seamless and NORMAL's are not.
    //    Pairing anchors on TWO leading singles (0 and 1 — the cover and the lone
    //    first recto, ComicReaderPairing::buildUnits), so pairing opens at index 2:
    //      normal  -> singles 0,1; pairs (2,3),(4,5); single 6
    //      shifted -> singles 0,1,2; pairs (3,4),(5,6)
    //    Luminances: p2=0 p3=255 p4=255 p5=0 p6=0  ->
    //      normal costs |0-255|,|255-0| ~ 1.0,1.0 (high); shifted |255-255|,|0-0| = 0,0 (low)
    //    => the probe must adopt Shifted.
    //    The two leading pages are never sampled (a single has no touching seam),
    //    so their luminance is arbitrary.
    QStringList cont;
    for (int i = 0; i < 7; ++i) cont << dir.filePath(QStringLiteral("cont%1.png").arg(i));
    CHECK(writeSolidPng(cont[0], 128), "setup: cont0");
    CHECK(writeSolidPng(cont[1], 128), "setup: cont1");
    CHECK(writeSolidPng(cont[2], 0),   "setup: cont2");
    CHECK(writeSolidPng(cont[3], 255), "setup: cont3");
    CHECK(writeSolidPng(cont[4], 255), "setup: cont4");
    CHECK(writeSolidPng(cont[5], 0),   "setup: cont5");
    CHECK(writeSolidPng(cont[6], 0),   "setup: cont6");
    const QVariantList contPages = pagesFromPaths(cont);

    // ── Plain 6-page portrait fixture (all mid-gray, no spreads) ──────────────
    QStringList plain;
    for (int i = 0; i < 6; ++i) plain << dir.filePath(QStringLiteral("plain%1.png").arg(i));
    for (int i = 0; i < 6; ++i) CHECK(writeSolidPng(plain[i], 120), "setup: plain page");
    const QVariantList plainPages = pagesFromPaths(plain);

    // ── Wave fixture (T19): 16 plain portrait pages. Big enough that four visible
    //    waves land on pages FAR enough apart that no wave re-requests a page an
    //    earlier wave already put inflight (see T19's note on request() dedup), and
    //    that the two stall pages (14, 15) are nowhere near the measured ones.
    QStringList wave;
    for (int i = 0; i < 16; ++i) wave << dir.filePath(QStringLiteral("wave%1.png").arg(i));
    for (int i = 0; i < 16; ++i) CHECK(writeSolidPng(wave[i], 140), "setup: wave page");
    const QVariantList wavePages = pagesFromPaths(wave);

    // ── Window fixture (T26): 32 plain portrait pages — long enough that a
    //    viewport around page 20 has real book on BOTH sides of it, so an
    //    eviction window has something to be wrong about in either direction.
    QStringList win;
    for (int i = 0; i < 32; ++i) win << dir.filePath(QStringLiteral("win%1.png").arg(i));
    for (int i = 0; i < 32; ++i) CHECK(writeSolidPng(win[i], 150), "setup: window page");
    const QVariantList winPages = pagesFromPaths(win);

    // ── Spread-discovery fixture: 5 pages, page 2 is LANDSCAPE (1200x600) so it
    //    decodes as a detected spread; the rest are portrait. ─────────────────
    QStringList spr;
    for (int i = 0; i < 5; ++i) spr << dir.filePath(QStringLiteral("spr%1.png").arg(i));
    CHECK(writeSolidPng(spr[0], 100), "setup: spr0");
    CHECK(writeSolidPng(spr[1], 100), "setup: spr1");
    CHECK(writeSolidPng(spr[2], 100, 1200, 600), "setup: spr2 landscape");
    CHECK(writeSolidPng(spr[3], 100), "setup: spr3");
    CHECK(writeSolidPng(spr[4], 100), "setup: spr4");
    const QVariantList sprPages = pagesFromPaths(spr);

    const qint64 kBudgetNormal = 512LL * 1024 * 1024;
    const qint64 kBudgetSaver  = 256LL * 1024 * 1024;

    // ── Test 1: openEntry publishes count + a fresh generation; strip populated ─
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("entryA"), plainPages, QStringLiteral("rtl"), manualNormal());
        CHECK(core.pageCount() == 6, "T1 pageCount == 6 after openEntry");
        CHECK(core.generation() >= 1, "T1 generation bumped to a live value");
        CHECK(core.stripModel() != nullptr && core.stripModel()->rowCount() == 6,
              "T1 strip model rebuilt with 6 rows");
        CHECK(core.readyCount() == 0, "T1 readyCount starts at 0 (nothing decoded yet)");
        // pageInfo estimates: undecoded page reports decoded=false, error none.
        const QVariantMap pi = core.pageInfo(0);
        CHECK(pi.value(QStringLiteral("decoded")).toBool() == false,
              "T1 pageInfo(0).decoded == false before any decode");
        CHECK(pi.value(QStringLiteral("error")).toString() == QStringLiteral("none"),
              "T1 pageInfo(0).error == none for a valid local page");
    }

    // ── Test 2: a remote / nonexistent entry is REJECTED, never analyzed ──────
    {
        ComicReaderCore core;
        QVariantList remote;
        {
            QVariantMap m0; m0.insert(QStringLiteral("index"), 0);
            m0.insert(QStringLiteral("url"), QStringLiteral("https://example.com/p0.jpg"));
            m0.insert(QStringLiteral("group"), -1);
            QVariantMap m1; m1.insert(QStringLiteral("index"), 1);
            m1.insert(QStringLiteral("url"),
                      QUrl::fromLocalFile(dir.filePath(QStringLiteral("nope.png"))).toString());
            m1.insert(QStringLiteral("group"), -1);
            remote << m0 << m1;
        }
        core.openEntry(QStringLiteral("remote"), remote, QStringLiteral("ltr"), QVariantMap());
        CHECK(core.generation() >= 1, "T2 generation still bumped for a rejected entry");
        CHECK(core.pageCount() == 2, "T2 pageCount reflects the (rejected) page list");
        CHECK(core.pageInfo(0).value(QStringLiteral("error")).toString()
                  == QStringLiteral("missing_file"),
              "T2 remote (non-file://) page gets error missing_file");
        CHECK(core.pageInfo(1).value(QStringLiteral("error")).toString()
                  == QStringLiteral("missing_file"),
              "T2 nonexistent file:// page gets error missing_file");
        // NOT analyzed: the auto-coupling probe must never have run.
        CHECK(core.couplingProbeDebug().value(QStringLiteral("called")).toBool() == false,
              "T2 a rejected entry is NOT analyzed (probe never called chooseCouplingPhase)");
        CHECK(core.readyCount() == 0, "T2 nothing decoded for a rejected entry");
    }

    // ── Test 3: setVisible pins visible+neighbors and drives the decode ───────
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("vis"), plainPages, QStringLiteral("rtl"), manualNormal());
        core.setVisible(QVariantList{2});
        // Pin set = {2} ∪ neighbors {1,3}.
        const QVariantList pinned = core.pinnedPages();
        CHECK(pinned.size() == 3 && pinned.contains(1) && pinned.contains(2) && pinned.contains(3),
              "T3 setVisible pins the visible page and its two neighbors {1,2,3}");
        // The visible page is requested at top priority and must decode.
        const bool decoded = waitFor([&] {
            return core.pageInfo(2).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(decoded, "T3 the visible page (2) actually decodes (decode driven)");
        CHECK(core.readyCount() >= 1, "T3 readyCount advanced as pages decoded");
    }

    // ── Test 4: metadata-driven spread discovery rebuilds units AND the visible
    //            page's unit still contains it ─────────────────────────────────
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("spread"), sprPages, QStringLiteral("ltr"), manualNormal());
        // Before decode, page 2 is not yet known as a spread: it pairs normally.
        CHECK(core.unitForPage(2).value(QStringLiteral("spread")).toBool() == false,
              "T4 page 2 is NOT a spread unit before it decodes");
        core.setVisible(QVariantList{2});
        const bool sawSpread = waitFor([&] {
            return core.pageInfo(2).value(QStringLiteral("detectedSpread")).toBool();
        });
        CHECK(sawSpread, "T4 page 2 decodes and reports detectedSpread");
        // The unit list rebuilt: page 2 is now its own full-width spread unit.
        const QVariantMap u2 = core.unitForPage(2);
        CHECK(u2.value(QStringLiteral("spread")).toBool() == true,
              "T4 spread discovery rebuilt units: page 2 is now a spread unit");
        CHECK(u2.value(QStringLiteral("rightIndex")).toInt() == 2,
              "T4 the spread unit still names page 2 (unitForPage(2) contains it)");
    }

    // ── Test 5: setSpreadOverride spread/single/clear mutates the unit ────────
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("ovr"), plainPages, QStringLiteral("ltr"), manualNormal());
        CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == false,
              "T5 page 3 starts non-spread");
        core.setSpreadOverride(3, QStringLiteral("spread"));
        CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == true,
              "T5 override 'spread' makes page 3 a spread unit");
        CHECK(core.pageInfo(3).value(QStringLiteral("spreadOverride")).toBool() == true,
              "T5 override 'spread' records spreadOverride=true on the page");
        core.setSpreadOverride(3, QStringLiteral("single"));
        CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == false,
              "T5 override 'single' forces page 3 non-spread");
        CHECK(core.pageInfo(3).value(QStringLiteral("spreadOverride")).toBool() == false,
              "T5 override 'single' records spreadOverride=false");
        core.setSpreadOverride(3, QStringLiteral("clear"));
        CHECK(core.pageInfo(3).contains(QStringLiteral("spreadOverride")) == false,
              "T5 override 'clear' removes the override entirely (defers to detection)");
    }

    // ── Test 6: nudgeCoupling flips phase and reports MANUAL ──────────────────
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("nudge"), plainPages, QStringLiteral("rtl"), manualNormal());
        CHECK(core.couplingState().startsWith(QStringLiteral("manual:normal")),
              "T6 couplingState starts manual:normal (from persisted)");
        core.nudgeCoupling();
        CHECK(core.couplingState().startsWith(QStringLiteral("manual:shifted")),
              "T6 nudgeCoupling flips to shifted and reports manual");
        core.nudgeCoupling();
        CHECK(core.couplingState().startsWith(QStringLiteral("manual:normal")),
              "T6 a second nudge flips back to normal, still manual");
    }

    // ── Test 7: the auto-coupling probe picks Shifted on a continuity fixture and
    //            feeds the FULL (both non-empty) per-phase cost vectors to choose ─
    {
        ComicReaderCore core;
        // Empty persisted -> Auto + unresolved -> the probe runs.
        core.openEntry(QStringLiteral("auto"), contPages, QStringLiteral("ltr"), QVariantMap());
        const bool resolved = waitFor([&] {
            return core.couplingProbeDebug().value(QStringLiteral("resolved")).toBool();
        });
        CHECK(resolved, "T7 the auto-coupling probe resolved");
        CHECK(core.couplingState().startsWith(QStringLiteral("auto:shifted")),
              "T7 the probe adopted Shifted on the continuity fixture");
        const QVariantMap dbg = core.couplingProbeDebug();
        CHECK(dbg.value(QStringLiteral("called")).toBool() == true,
              "T7 chooseCouplingPhase WAS called (both phases had samples)");
        // BOTH vectors must be non-empty. Equal counts are NOT a correctness
        // property (chooseCouplingPhase aggregates by MEAN, not a paired compare)
        // — T7b proves an asymmetric fixture is decided from the FULL vectors.
        CHECK(dbg.value(QStringLiteral("normalSamples")).toInt() > 0,
              "T7 the normal cost vector is non-empty");
        CHECK(dbg.value(QStringLiteral("shiftedSamples")).toInt() > 0,
              "T7 the shifted cost vector is non-empty");
    }

    // ── Test 7b: ASYMMETRIC per-phase sample counts — the probe must feed the
    //            FULL vectors to chooseCouplingPhase (MEAN aggregation), NEVER a
    //            positional min()-trim (which would flip this verdict). ─────────
    // 8 pages, pairing anchored on the two leading singles (buildUnits opens at
    // index 2) -> normal pairs (2,3),(4,5),(6,7) = 3 seams; shifted single 2,
    // pairs (3,4),(5,6), single 7 = 2 seams. Solid-gray luminances make each seam
    // cost |La-Lb|/255:
    //   normal:  |204-178|,|102-76|,|0-255| ~ [0.10, 0.10, 1.00] -> mean 0.40
    //   shifted: |178-102|,|76-0|           ~ [0.30, 0.30]        -> mean 0.30
    // Full-vector mean -> SHIFTED (conf ~0.15). A min(3,2)=2 positional trim would
    // drop normal's (6,7)=1.00 seam -> normal scores [0.10,0.10]=0.10 vs shifted
    // 0.30 -> wrongly NORMAL. That dropped seam is the very proof normal is wrong.
    {
        QStringList asym;
        for (int i = 0; i < 8; ++i)
            asym << dir.filePath(QStringLiteral("asym%1.png").arg(i));
        // [0] and [1] are the leading singles — never sampled, value arbitrary.
        const int lum[8] = {128, 128, 204, 178, 102, 76, 0, 255};
        for (int i = 0; i < 8; ++i)
            CHECK(writeSolidPng(asym[i], lum[i]), "setup: asym page");
        const QVariantList asymPages = pagesFromPaths(asym);

        ComicReaderCore core;
        core.openEntry(QStringLiteral("asym"), asymPages, QStringLiteral("ltr"), QVariantMap());
        const bool resolved = waitFor([&] {
            return core.couplingProbeDebug().value(QStringLiteral("resolved")).toBool();
        });
        CHECK(resolved, "T7b the probe resolved on the asymmetric fixture");
        // The FAITHFUL (full-vector mean) verdict is SHIFTED; a min()-trim = Normal.
        CHECK(core.couplingState().startsWith(QStringLiteral("auto:shifted")),
              "T7b full-vector mean adopts SHIFTED (a min()-trim would wrongly pick Normal)");
        const QVariantMap dbg = core.couplingProbeDebug();
        CHECK(dbg.value(QStringLiteral("called")).toBool() == true,
              "T7b chooseCouplingPhase WAS called");
        // The probe fed UNEQUAL, UN-trimmed sample counts (3 normal, 2 shifted).
        CHECK(dbg.value(QStringLiteral("normalSamples")).toInt() == 3,
              "T7b normalSamples == 3 (full, untrimmed)");
        CHECK(dbg.value(QStringLiteral("shiftedSamples")).toInt() == 2,
              "T7b shiftedSamples == 2 (full, untrimmed — NOT trimmed down to match normal)");
    }

    // ── Test 8: setMemorySaver flips the cache budget live; persisted honored ─
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("mem"), plainPages, QStringLiteral("ltr"), manualNormal());
        CHECK(core.cacheBudget() == static_cast<qulonglong>(kBudgetNormal),
              "T8 default cache budget is 512 MiB");
        core.setMemorySaver(true);
        CHECK(core.cacheBudget() == static_cast<qulonglong>(kBudgetSaver),
              "T8 setMemorySaver(true) shrinks the budget to 256 MiB");
        core.setMemorySaver(false);
        CHECK(core.cacheBudget() == static_cast<qulonglong>(kBudgetNormal),
              "T8 setMemorySaver(false) restores 512 MiB");

        // persisted memory saver applies at open.
        ComicReaderCore core2;
        QVariantMap p = manualNormal();
        p.insert(QStringLiteral("memorySaver"), true);
        core2.openEntry(QStringLiteral("mem2"), plainPages, QStringLiteral("ltr"), p);
        CHECK(core2.cacheBudget() == static_cast<qulonglong>(kBudgetSaver),
              "T8 persisted memorySaver=true opens at the 256 MiB budget");
    }

    // ── Test 9: a new entry invalidates the old generation for the provider ───
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("genA"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setVisible(QVariantList{0});
        const bool d0 = waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(d0, "T9 entry A page 0 decodes");
        const quint64 genA = core.generation();

        ComicReaderProvider* provider = core.createProvider();
        CHECK(providerServes(provider, QStringLiteral("%1/0").arg(genA)),
              "T9 provider serves the live image for gen A page 0");

        // imageUrl encodes the current generation + a rev token.
        CHECK(core.imageUrl(0).startsWith(
                  QStringLiteral("image://comicreader/%1/0?rev=").arg(genA)),
              "T9 imageUrl(0) is image://comicreader/<genA>/0?rev=N");

        // Open a second entry: generation bumps and gen A is now dead.
        core.openEntry(QStringLiteral("genB"), plainPages, QStringLiteral("ltr"), manualNormal());
        const quint64 genB = core.generation();
        CHECK(genB > genA, "T9 opening a new entry bumps the generation");

        CHECK(!providerServes(provider, QStringLiteral("%1/0").arg(genA)),
              "T9 provider serves NOTHING for the superseded generation A (dead imageUrl)");

        core.setVisible(QVariantList{0});
        const bool dB0 = waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(dB0, "T9 entry B page 0 decodes under the new generation");
        CHECK(providerServes(provider, QStringLiteral("%1/0").arg(genB)),
              "T9 provider serves the live image for gen B page 0");

        // A malformed id serves nothing, never crashes.
        CHECK(!providerServes(provider, QStringLiteral("not-a-real-id")),
              "T9 provider serves NOTHING for a malformed id");

        delete provider;
    }

    // ── Test 10: closeEntry invalidates the live generation for the provider ──
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("close"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setVisible(QVariantList{0});
        const bool d0 = waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(d0, "T10 page 0 decodes before close");
        const quint64 gen = core.generation();
        ComicReaderProvider* provider = core.createProvider();
        CHECK(providerServes(provider, QStringLiteral("%1/0").arg(gen)),
              "T10 provider serves the page while the entry is open");
        core.closeEntry();
        CHECK(core.pageCount() == 0, "T10 closeEntry clears the page count");
        CHECK(!providerServes(provider, QStringLiteral("%1/0").arg(gen)),
              "T10 provider serves NOTHING after closeEntry (generation retired)");
        delete provider;
    }

    // ── Test 11: setStripViewport drives the window decode ────────────────────
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("strip"), plainPages, QStringLiteral("ltr"), manualNormal());
        // A viewport that spans the whole strip requests every windowed page.
        core.setStripViewport(0.0, 100000.0);
        const bool decoded = waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(decoded, "T11 setStripViewport drives decode of the windowed pages");
    }

    // ── Test 12: persisted round-trip is LOSSLESS ─────────────────────────────
    {
        ComicReaderCore core;
        QVariantMap persisted;
        QVariantMap so;
        so.insert(QStringLiteral("3"), true);
        so.insert(QStringLiteral("5"), false);
        persisted.insert(QStringLiteral("spreadOverrides"), so);
        persisted.insert(QStringLiteral("couplingMode"), QStringLiteral("manual"));
        persisted.insert(QStringLiteral("couplingPhase"), QStringLiteral("shifted"));
        persisted.insert(QStringLiteral("couplingResolved"), true);
        persisted.insert(QStringLiteral("couplingConfidence"), 0.42);
        QVariantList bm;
        bm.append(2);
        bm.append(7);
        persisted.insert(QStringLiteral("bookmarks"), bm);
        persisted.insert(QStringLiteral("memorySaver"), true);

        core.openEntry(QStringLiteral("rt"), plainPages, QStringLiteral("rtl"), persisted);

        // Overrides took effect on the pairing.
        CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == true,
              "T12 persisted spreadOverride for page 3 applied to the units");
        CHECK(core.couplingState().startsWith(QStringLiteral("manual:shifted")),
              "T12 persisted coupling (manual:shifted) applied");
        // Round-trip must be byte-identical.
        const QVariantMap out = core.persistedState();
        CHECK(out == persisted,
              "T12 persistedState() round-trips the persisted blob losslessly");
        // No probe (a resolved manual state is never re-analyzed).
        CHECK(core.couplingProbeDebug().value(QStringLiteral("called")).toBool() == false,
              "T12 a resolved persisted coupling skips the auto-coupling probe");
    }

    // ── Test 13: resetCoupling un-pins a manual entry back to Auto + RE-PROBES ─
    // The settings sheet's Coupling row is Auto | Nudge: Nudge pins the phase by
    // hand (T6), and tapping Auto must hand the decision back to the probe. The
    // continuity fixture is the oracle — the probe demonstrably picks Shifted on
    // it (T7), so a reset that merely cleared the flag without re-running would
    // leave the phase on Normal and fail here.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("reset"), contPages, QStringLiteral("ltr"), manualNormal());
        CHECK(core.couplingState().startsWith(QStringLiteral("manual:normal")),
              "T13 opens manual:normal from the persisted blob");
        CHECK(core.couplingProbeDebug().value(QStringLiteral("called")).toBool() == false,
              "T13 a manual entry never probed in the first place");

        core.resetCoupling();
        CHECK(core.couplingState().startsWith(QStringLiteral("auto:normal:0.00")),
              "T13 resetCoupling reports auto immediately, phase + confidence cleared");

        const bool resolved = waitFor([&] {
            return core.couplingProbeDebug().value(QStringLiteral("resolved")).toBool();
        });
        CHECK(resolved, "T13 resetCoupling RE-RUNS the auto-coupling probe");
        CHECK(core.couplingState().startsWith(QStringLiteral("auto:shifted")),
              "T13 the re-run probe re-decides the phase (Shifted on the continuity fixture)");
        CHECK(core.persistedState().value(QStringLiteral("couplingMode")).toString()
                  == QStringLiteral("auto"),
              "T13 the reset rides out in persistedState as auto");
    }

    // ── Test 14: setStripLayout drives the strip geometry, in place ───────────
    // Portrait width % and inter-page gap were fixed at 78/0 with no way in; the
    // settings sheet's LONG STRIP section needs both a setter and a readback.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("layout"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);

        CHECK(core.stripWidthPct() == 78, "T14 default portrait width is 78%");
        CHECK(core.stripGap() == 0, "T14 default gap is 0");

        QAbstractListModel* m = core.stripModel();
        const auto roleOf = [&](int row, int role) {
            return m->data(m->index(row, 0), role).toDouble();
        };
        const int wRole = ComicReaderStripModel::DisplayWidthRole;
        const int hRole = ComicReaderStripModel::DisplayHeightRole;
        const int tRole = ComicReaderStripModel::TopRole;

        CHECK(qAbs(roleOf(0, wRole) - 780.0) < 1.0,
              "T14 a portrait page displays at 78% of the viewport width");
        CHECK(qAbs(roleOf(1, tRole) - roleOf(0, hRole)) < 0.5,
              "T14 with gap 0, page 1 starts exactly where page 0 ends");

        core.setStripLayout(60, 24);
        CHECK(core.stripWidthPct() == 60 && core.stripGap() == 24,
              "T14 the readbacks follow setStripLayout");
        CHECK(qAbs(roleOf(0, wRole) - 600.0) < 1.0,
              "T14 page width follows the new portrait pct");
        CHECK(qAbs(roleOf(1, tRole) - (roleOf(0, hRole) + 24.0)) < 0.5,
              "T14 the gap separates consecutive pages");
        CHECK(m->rowCount() == 6,
              "T14 the layout change is in place — same rows, no model teardown");

        // Out-of-range input is clamped, never applied raw (a 0% width would
        // collapse every page to nothing).
        core.setStripLayout(5, -10);
        CHECK(core.stripWidthPct() == 40 && core.stripGap() == 0,
              "T14 an under-range layout clamps to 40% / gap 0");
        core.setStripLayout(400, 5000);
        CHECK(core.stripWidthPct() == 100 && core.stripGap() == 80,
              "T14 an over-range layout clamps to 100% / gap 80");
    }

    // ── Test 15: a strip layout change HOLDS THE READER'S PLACE ──────────────
    // Changing portrait width or gap rescales the column, so a viewport top that pointed at page N
    // now points somewhere else entirely. A ratio-scale (what a viewport RESIZE uses) is the wrong
    // tool: a portrait-width change leaves SPREADS untouched — they always span the full width — and
    // a gap change shifts tops by a per-page constant, so the column does not scale uniformly.
    // setStripLayout therefore anchors the page under the viewport centre, and the fraction down
    // that page, and returns the top to scroll to.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("anchor"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);

        QAbstractListModel* m = core.stripModel();
        const auto roleOf = [&](int row, int role) {
            return m->data(m->index(row, 0), role).toDouble();
        };
        const int hRole = ComicReaderStripModel::DisplayHeightRole;
        const int tRole = ComicReaderStripModel::TopRole;

        const double vpTop = 2400.0;
        const double vpH = 800.0;
        const double centre = vpTop + vpH / 2.0;

        // find the page under the centre, and how far down it we are, BEFORE the change
        int anchorRow = -1;
        double frac = 0.0;
        for (int r = 0; r < m->rowCount(); ++r) {
            const double t = roleOf(r, tRole);
            const double h = roleOf(r, hRole);
            if (centre >= t && centre < t + h) {
                anchorRow = r;
                frac = (centre - t) / h;
                break;
            }
        }
        CHECK(anchorRow > 0, "T15 the fixture places the viewport centre inside a real page");

        const double newTop = core.setStripLayout(100, 0, vpTop, vpH);

        // THE invariant: the same point of the same page is still under the viewport centre.
        const double newCentre = newTop + vpH / 2.0;
        const double t2 = roleOf(anchorRow, tRole);
        const double h2 = roleOf(anchorRow, hRole);
        CHECK(newCentre >= t2 && newCentre <= t2 + h2,
              "T15 the anchor page is STILL under the viewport centre after the width change");
        CHECK(h2 > 0 && qAbs((newCentre - t2) / h2 - frac) < 0.01,
              "T15 the same fraction down that page is still centred");
        CHECK(qAbs(newTop - vpTop) > 1.0,
              "T15 holding the place actually MOVED the viewport top (the column really rescaled)");

        // a gap change anchors too — its shift is per-page, so a ratio could never express it
        const double gapTop = core.setStripLayout(100, 40, newTop, vpH);
        const double gapCentre = gapTop + vpH / 2.0;
        const double t3 = roleOf(anchorRow, tRole);
        const double h3 = roleOf(anchorRow, hRole);
        CHECK(gapCentre >= t3 && gapCentre <= t3 + h3,
              "T15 a GAP change also holds the anchor page under the centre");

        // no viewport given -> nothing to anchor to; the caller's top comes back untouched
        CHECK(qAbs(core.setStripLayout(62, 0, 1234.0, 0.0) - 1234.0) < 1e-9,
              "T15 with no viewport height there is nothing to anchor — the top is returned as-is");
        // a no-op layout change must not move anything either
        CHECK(qAbs(core.setStripLayout(62, 0, 999.0, 800.0) - 999.0) < 1e-9,
              "T15 a layout change that changes nothing must not move the reader");
        // never scroll past the end of the book
        const double clamped = core.setStripLayout(40, 0, 1e9, vpH);
        CHECK(clamped >= 0.0 && clamped <= qMax(0.0, m->rowCount() * roleOf(0, hRole)),
              "T15 the returned top is clamped inside the book");
    }

    // ── Test 15b: 78 -> 92 -> 78 puts the reader back EXACTLY where they were ──
    // Task 8, and the sharper form of T15. T15 proves ONE width change holds the
    // anchor; this proves the anchor is not merely "close" but reversible, which is
    // what a reader dragging the portrait-width control back and forth actually
    // experiences. Hemanth named the portrait width himself while this was being
    // designed and confirmed 78 by name, so 78 is the start AND the finish.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("roundtrip"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);
        CHECK(core.stripWidthPct() == 78, "T15b a freshly opened entry reads at the approved 78% default");

        QAbstractListModel* m = core.stripModel();
        const auto topOf = [&](int row) {
            return m->data(m->index(row, 0), ComicReaderStripModel::TopRole).toDouble();
        };
        const auto heightOf = [&](int row) {
            return m->data(m->index(row, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
        };

        const double vpH = 800.0;
        // Park mid-book, deliberately NOT on a page boundary, so the anchor has a
        // real fraction to preserve rather than a trivial 0.
        const double vpTop = topOf(3) + heightOf(3) * 0.37 - vpH / 2.0;
        const double centre = vpTop + vpH / 2.0;
        const int anchorRow = 3;
        const double frac0 = (centre - topOf(anchorRow)) / heightOf(anchorRow);
        CHECK(frac0 > 0.05 && frac0 < 0.95, "T15b the fixture parks the centre INSIDE page 3, not on its edge");

        const double atWide = core.setStripLayout(92, 0, vpTop, vpH);
        CHECK(core.stripWidthPct() == 92, "T15b the width really moved to 92");
        CHECK(qAbs(atWide - vpTop) > 1.0, "T15b 92% actually rescaled the column (else the round trip is vacuous)");
        const double wideCentre = atWide + vpH / 2.0;
        CHECK(qAbs((wideCentre - topOf(anchorRow)) / heightOf(anchorRow) - frac0) < 0.01,
              "T15b at 92% the same fraction of the same page is still centred");

        const double back = core.setStripLayout(78, 0, atWide, vpH);
        CHECK(core.stripWidthPct() == 78, "T15b the width came back to 78");
        CHECK(qAbs(back - vpTop) < 1.0,
              "T15b 78 -> 92 -> 78 returns the reader to the top they started at");
        CHECK(qAbs((back + vpH / 2.0 - topOf(anchorRow)) / heightOf(anchorRow) - frac0) < 1e-6,
              "T15b ...and to the same fraction of the same page");
    }

    // ── Test 15c: a quarter turn reflows the LONG STRIP's bands ──────────────
    // Task 7 added rotation to the render profile and the provider applies it
    // BEFORE it scales, so the delivered page at 90/270 is the transpose of its
    // source. Single and Pair discover that from the delivered pixmap; the strip
    // cannot — its delegates take their height from the model — so without this
    // wiring a turned page was drawn PreserveAspectFit inside a portrait-shaped
    // band and every page carried dead margins above and below it.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t15c"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);
        QAbstractListModel* m = core.stripModel();
        const auto heightOf = [&](int row) {
            return m->data(m->index(row, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
        };
        const auto widthOf = [&](int row) {
            return m->data(m->index(row, 0), ComicReaderStripModel::DisplayWidthRole).toDouble();
        };
        const double h0 = heightOf(0);
        const double w0 = widthOf(0);
        CHECK(h0 > 0.0, "T15c the fixture has a real band height to start from");

        QVariantMap turn;
        turn.insert(QStringLiteral("rotation"), 90);
        core.setRenderProfile(turn);
        CHECK(qAbs(heightOf(0) - h0) > 0.5,
              "T15c a quarter turn must change the strip band's HEIGHT (it was letterboxing before)");
        CHECK(qAbs(widthOf(0) - w0) < 0.5,
              "T15c ...and must NOT change its width — the portrait width contract is untouched by a turn");
        // The transpose, exactly: band height = displayWidth * (srcW / srcH).
        const QVariantMap info = core.pageInfo(0);
        const double srcW = info.value(QStringLiteral("sourceWidth")).toDouble();
        const double srcH = info.value(QStringLiteral("sourceHeight")).toDouble();
        if (srcW > 0.0 && srcH > 0.0)
            CHECK(qAbs(heightOf(0) - w0 * (srcW / srcH)) < 0.5,
                  "T15c the turned band is the TRANSPOSE of the source aspect");

        QVariantMap back;
        back.insert(QStringLiteral("rotation"), 0);
        core.setRenderProfile(back);
        CHECK(qAbs(heightOf(0) - h0) < 0.5, "T15c turning back restores the original band exactly");

        // ...and the turn survives an entry crossing, because the profile does.
        core.setRenderProfile(turn);
        const double turned = heightOf(0);
        core.openEntry(QStringLiteral("t15c-next"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);
        CHECK(qAbs(heightOf(0) - turned) < 0.5,
              "T15c the NEXT entry opens already turned (the profile survives a crossing, so the bands must too)");
    }

    // ── Test 16: stripPageTop is the strip model's own top for that page ─────────
    // The strip restore (B2) seeks by asking the BACKEND where a page starts, because the ListView
    // only realizes delegates near the viewport — the page you are resuming TO has no y to read yet.
    // So this answer must be the model's own geometry, not a second estimate that can drift from it.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t16"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);
        QAbstractListModel* m = core.stripModel();
        const double top3 = m->data(m->index(3, 0), ComicReaderStripModel::TopRole).toDouble();
        CHECK(qAbs(core.stripPageTop(3) - top3) < 0.5, "T16 stripPageTop(3) matches the model's TopRole");
        CHECK(core.stripPageTop(-1) == 0.0 && core.stripPageTop(999) == 0.0,
              "T16 out-of-range stripPageTop is 0, never a crash");

        // ...and the same for the page's HEIGHT, for the same reason. The strip reports presented()
        // after a resume — a move that has just jumped the column thousands of pixels, so the drawn
        // column has no item to measure — and it needs the page's height to say how far down that
        // page the viewport centre sits. Without this the report either goes silent or carries a
        // fraction of 0, and Task 11 would bank a worse position than the one it just restored from.
        const double h3 = m->data(m->index(3, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
        CHECK(h3 > 0.0, "T16 precondition: the model reports a real display height for page 3");
        CHECK(qAbs(core.stripPageHeight(3) - h3) < 0.5,
              "T16 stripPageHeight(3) matches the model's DisplayHeightRole");
        CHECK(core.stripPageHeight(-1) == 0.0 && core.stripPageHeight(999) == 0.0,
              "T16 out-of-range stripPageHeight is 0, never a crash");
        ComicReaderCore emptyGeom;
        CHECK(emptyGeom.stripPageHeight(0) == 0.0,
              "T16 an entry-less core's stripPageHeight is 0, never a crash");
    }

    // ── Test 17: toggleBookmark writes, sorts, de-dupes, persists ────────────────
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("bm"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.toggleBookmark(4);
        core.toggleBookmark(1);
        QVariantList bm = core.bookmarks();
        CHECK(bm.size() == 2 && bm[0].toInt() == 1 && bm[1].toInt() == 4,
              "T17 bookmarks are stored SORTED regardless of the order they were added");
        core.toggleBookmark(4);
        CHECK(core.bookmarks().size() == 1, "T17 a second toggle on the same page REMOVES it");
        core.toggleBookmark(999);
        CHECK(core.bookmarks().size() == 1, "T17 an out-of-range page is ignored, never appended");
        core.toggleBookmark(-1);
        CHECK(core.bookmarks().size() == 1, "T17 a negative page is ignored");
        CHECK(core.persistedState().value(QStringLiteral("bookmarks")).toList().size() == 1,
              "T17 the persisted blob carries the LIVE bookmarks");
    }

    // ── Test 17b: opening an entry with persisted bookmarks makes them visible via
    //    the new live getter too — the backend already read that key (T12 covers the
    //    lossless round-trip of persistedState()); this pins that bookmarks() itself
    //    sees them, not just the blob.
    {
        ComicReaderCore core;
        QVariantMap persisted = manualNormal();
        QVariantList bm;
        bm.append(2);
        bm.append(5);
        persisted.insert(QStringLiteral("bookmarks"), bm);
        core.openEntry(QStringLiteral("bm2"), plainPages, QStringLiteral("ltr"), persisted);
        QVariantList out = core.bookmarks();
        CHECK(out.size() == 2 && out[0].toInt() == 2 && out[1].toInt() == 5,
              "T17b bookmarks() reflects a persisted-on-open bookmark set");
    }

    // ── Test 18: stripDecodePriority — priority peaks AT the viewport centre and falls off
    //    symmetrically with distance, never underflowing below 1; a centre of -1 (empty strip
    //    model) hands back the flat base for every page. Pure function, no ComicReaderCore
    //    instance needed.
    {
        CHECK(stripDecodePriority(10, 10) == kPrioStripBase, "T18 the centre page gets the base priority");
        CHECK(stripDecodePriority(9, 10) == stripDecodePriority(11, 10), "T18 priority is symmetric around the centre");
        CHECK(stripDecodePriority(10, 10) > stripDecodePriority(13, 10), "T18 priority falls off with distance");
        CHECK(stripDecodePriority(0, 200) >= 1, "T18 priority never underflows below 1");
        CHECK(stripDecodePriority(5, -1) == kPrioStripBase, "T18 with no centre (empty model) every page gets the flat base");
    }

    // ── Test 19: LATEST-WINS — the page you LAND on decodes BEFORE the ones you
    //    flew past.
    //
    // Hold the page-turn key through a dozen pages and stop. Every page you passed
    // queued a decode, and with one flat visible priority equal-priority is FIFO —
    // so the page now on screen sits at the BACK of the queue behind a dozen pages
    // nobody is looking at any more. The reader stares at a blank frame AFTER the
    // input already stopped. Each setVisible wave must therefore outrank every
    // earlier one.
    //
    // Shape: hold BOTH decode lanes on a latch with two pages nobody measures
    // (14, 15), fire four visible waves while everything else can only queue, then
    // release and record the ORDER pages report ready.
    //
    // Why the waves are 3 pages apart, not adjacent: ComicReaderDecode::request()
    // dedups against m_inflight, and a page goes into m_inflight the moment it is
    // REQUESTED — queued counts, not just running. So a wave on page N+1 right
    // after a wave on page N would request nothing new at all (N+1 and N+2 were
    // already queued as that wave's next1/next2 prefetch) and the test would prove
    // nothing. Spacing by 3 gives every wave its own untouched triple: {0,1,2},
    // {3,4,5}, {6,7,8}, {9,10,11}. (That same dedup is why the boost has to be
    // MONOTONIC rather than reset per wave: a page already queued can never be
    // re-prioritized, only out-ranked by newer work.)
    //
    // The stall is set up with an explicit two-page visible set rather than one page
    // plus a prefetch, so this test depends on the priority rule ALONE — not on how
    // far the backwards prefetch happens to reach (T20's concern).
    {
        // Declared BEFORE `core` so the core (and the decode pool inside it) tears
        // down first, while these captures are still alive — the decode harness's
        // house rule for &-capturing worker hooks.
        QSemaphore latch(0);
        std::atomic<int> held{0};
        QVector<int> readyOrder;
        // The order pages are DEQUEUED, recorded in the worker hook (two worker
        // threads write it, hence the mutex). This is the thing the priority
        // actually controls; completion order is that plus thread-scheduling
        // noise — a lane can lag several slots when the machine is busy, so the
        // fine-grained cross-wave assertion below reads dequeue order, and only
        // the coarse reader-facing claim reads completion order.
        QMutex startMutex;
        QVector<int> startOrder;

        ComicReaderCore core;
        core.setDecodeWorkerHooksForTest(
            [&](quint64, int page) {
                { QMutexLocker lock(&startMutex); startOrder.append(page); }
                if (page >= 14) {          // the two stall pages, and only those
                    ++held;
                    latch.acquire();       // hold the lane so everything else queues
                }
            },
            std::function<void(quint64, int)>());
        QObject::connect(&core, &ComicReaderCore::pageReady,
                         [&](int page) { readyOrder.append(page); });

        core.openEntry(QStringLiteral("waves"), wavePages, QStringLiteral("ltr"), manualNormal());

        // Occupy both lanes with the last two pages: a two-page visible set starts
        // exactly two workers, and both block in the hook.
        core.setVisible(QVariantList{14, 15});
        const bool stalled = waitFor([&] { return held.load() == 2; });
        CHECK(stalled, "T19 both decode lanes are held, so every later request can only queue");

        // The flip: four waves, newest last. Under one flat visible priority this is
        // exactly FIFO — 0 first, 9 last.
        core.setVisible(QVariantList{0});
        core.setVisible(QVariantList{3});
        core.setVisible(QVariantList{6});
        core.setVisible(QVariantList{9});

        latch.release(8);
        const bool allDone = waitFor([&] {
            return readyOrder.contains(0) && readyOrder.contains(3)
                   && readyOrder.contains(6) && readyOrder.contains(9);
        }, 8000);
        CHECK(allDone, "T19 every waved page still decodes eventually (a stale wave is out-ranked, never cancelled)");

        CHECK(readyOrder.indexOf(9) < readyOrder.indexOf(0),
              "T19 the most recently requested visible page decodes BEFORE an older queued one");

        // The full order, not just the extremes: one pair could come out right by
        // luck with two lanes running; a strictly reversed four-wave order cannot.
        QVector<int> starts;
        { QMutexLocker lock(&startMutex); starts = startOrder; }
        const bool newestFirst = starts.indexOf(9) < starts.indexOf(6)
                                 && starts.indexOf(6) < starts.indexOf(3)
                                 && starts.indexOf(3) < starts.indexOf(0);
        if (!newestFirst) {   // a scheduling mystery here is unreadable without the orders
            QString s, r;
            for (int p : starts) s += QString::number(p) + QStringLiteral(" ");
            for (int p : readyOrder) r += QString::number(p) + QStringLiteral(" ");
            std::fprintf(stderr, "  T19 dequeue order: %s\n  T19 ready order:   %s\n",
                         s.toUtf8().constData(), r.toUtf8().constData());
        }
        CHECK(newestFirst,
              "T19 the whole queue is newest-first — every wave out-ranks every earlier one");
    }

    // ── Test 20: the BACKWARDS prefetch is UNIT-aware ────────────────────────────
    // In double-page mode you do not read pages, you read UNITS. Prefetching a
    // single page backwards lands the reader on a pair whose OTHER half was never
    // asked for: one side is instant, the other pops in a beat later. Most visible
    // re-reading backwards in RTL manga, which is normal there.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("prevunit"), plainPages, QStringLiteral("rtl"), manualNormal());

        // Pin the fixture's real layout before leaning on it: two leading singles,
        // then pairs — [0][1][2,3][4,5].
        const QVariantMap u4 = core.unitForPage(4);
        const QVariantMap u3 = core.unitForPage(3);
        CHECK(u4.value(QStringLiteral("rightIndex")).toInt() == 4
                  && u4.value(QStringLiteral("leftIndex")).toInt() == 5,
              "T20 the fixture really pairs page 4 with page 5");
        CHECK(u3.value(QStringLiteral("rightIndex")).toInt() == 2
                  && u3.value(QStringLiteral("leftIndex")).toInt() == 3,
              "T20 ... and the unit BEFORE it really is the pair [2,3]");

        // Visible unit [4,5] -> forward prefetch has nowhere to go (6 pages), so the
        // only other work is the backwards one: 4, 5 and BOTH halves of [2,3] = 4.
        core.setVisible(QVariantList{4});
        const bool both = waitFor([&] { return core.readyCount() >= 4; });
        CHECK(both, "T20 the backwards prefetch fetches BOTH halves of the previous unit");
        CHECK(core.pageInfo(2).value(QStringLiteral("decoded")).toBool(),
              "T20 the FAR half of the previous unit (page 2) is prefetched, not just the near one");
    }

    // ── Test 21: the EARLY DIMENSION HINT — the strip learns a page's REAL
    //    geometry from the file HEADER, independently of the full decode.
    //
    // Until a page decodes, the strip model sizes it at a flat 1600x2400
    // estimate; when the truth lands the column re-lays out and the anti-jump
    // machinery pays a correction. Learning the size from the header (a few KB)
    // instead of the whole file is what lets a fast scroll settle on true
    // geometry almost at once instead of trickling corrections.
    //
    // The fixture is a valid LANDSCAPE PNG truncated to its first 256 bytes: the
    // IHDR (real size) survives, the image body does not. So the full decode
    // MUST fail while the hint still lands — proving the hint is genuinely
    // independent of the decode, not merely a beat ahead of it.
    //
    // Landscape is load-bearing, not decoration. The estimate is 1600x2400
    // (ratio 1.5); a portrait fixture could land on the same displayed height by
    // coincidence and prove nothing. 1200x600 is ratio 0.5 AND trips
    // spreadRatioExceeded, so BOTH the height and the spread verdict differ
    // visibly from the estimate:
    //   estimate -> portrait, 78% of a 1000px viewport = 780 wide, h = 780*1.5 = 1170
    //   hint     -> spread,   FULL viewport width      = 1000 wide, h = 1000*0.5 = 500
    {
        const QString whole = dir.filePath(QStringLiteral("truncsrc.png"));
        CHECK(writeSolidPng(whole, 90, 1200, 600), "setup: T21 landscape source");
        QByteArray head;
        {
            QFile in(whole);
            CHECK(in.open(QIODevice::ReadOnly), "setup: T21 source opened for truncation");
            head = in.read(256);   // the PNG signature + IHDR live in the first ~33 bytes
        }
        const QString trunc = dir.filePath(QStringLiteral("trunc.png"));
        {
            QFile out(trunc);
            CHECK(out.open(QIODevice::WriteOnly), "setup: T21 truncated file opened");
            out.write(head);
        }

        QStringList hintPaths;
        hintPaths << trunc << plain[1] << plain[2];

        ComicReaderCore core;
        QVector<int> failedPages;
        QObject::connect(&core, &ComicReaderCore::pageFailed,
                         [&](int page, QString) { failedPages.append(page); });

        core.openEntry(QStringLiteral("hint"), pagesFromPaths(hintPaths),
                       QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);

        QAbstractListModel* m = core.stripModel();
        const int hRole = ComicReaderStripModel::DisplayHeightRole;
        const int wRole = ComicReaderStripModel::DisplayWidthRole;
        const int rRole = ComicReaderStripModel::ReadyRole;
        const auto row0 = [&](int role) { return m->data(m->index(0, 0), role); };

        // Baseline: before any decode work, page 0 is the flat estimate.
        CHECK(qAbs(row0(hRole).toDouble() - 1170.0) < 1.0,
              "T21 baseline: page 0 starts at the 1600x2400 estimate (780 x 1.5 = 1170)");

        core.setStripViewport(0.0, 800.0);

        // The decode really does fail — this is "despite a failed decode", not
        // "just before it".
        const bool failed = waitFor([&] { return failedPages.contains(0); });
        CHECK(failed, "T21 page 0's FULL decode genuinely fails (truncated body)");

        CHECK(qAbs(row0(hRole).toDouble() - 500.0) < 1.0,
              "T21 the strip learned page 0's REAL 1200x600 geometry from the header (h == 500, not the estimate's 1170)");
        CHECK(qAbs(row0(wRole).toDouble() - 1000.0) < 1.0,
              "T21 the header hint also carried the SPREAD verdict — page 0 spans the full viewport width");
        CHECK(row0(rRole).toBool() == false,
              "T21 a header hint is NOT pixels: ReadyRole stays false while the size is locked");
        CHECK(core.pageInfo(0).value(QStringLiteral("decoded")).toBool() == false,
              "T21 pageInfo agrees — the page never decoded");
    }

    // ── Test 22: stripPageAtCenter resolves the page whose band holds the
    //    viewport's vertical centre — the geometry-honest answer the FIX 2 scrub
    //    bubble needs in strip mode (a linear pages*fraction estimate lies once
    //    pages have different heights). A degenerate/empty viewport never crashes. ──
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t22"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setStripViewportWidth(1000);
        QAbstractListModel* m = core.stripModel();
        const double top3 = m->data(m->index(3, 0), ComicReaderStripModel::TopRole).toDouble();
        // a small viewport centred just inside page 3's band must resolve to page 3
        CHECK(core.stripPageAtCenter(top3, 10.0) == 3,
              "T22 stripPageAtCenter resolves the band holding the viewport centre (page 3)");
        const double top0 = m->data(m->index(0, 0), ComicReaderStripModel::TopRole).toDouble();
        CHECK(core.stripPageAtCenter(top0, 10.0) == 0,
              "T22 stripPageAtCenter resolves the first page's band too");

        // degenerate viewport (zero/negative height) must never crash — just answers *some*
        // in-range page for the given top.
        const int deg = core.stripPageAtCenter(top3, 0.0);
        CHECK(deg >= 0 && deg < core.pageCount(), "T22 a zero-height viewport never crashes");
        const int negDeg = core.stripPageAtCenter(-50.0, -10.0);
        CHECK(negDeg >= 0 && negDeg < core.pageCount(), "T22 a negative top/height never crashes");
    }
    {
        // an entry-less core (no pages at all) must answer -1, never crash.
        ComicReaderCore emptyCore;
        CHECK(emptyCore.stripPageAtCenter(0.0, 100.0) == -1,
              "T22 an entry-less core's stripPageAtCenter is -1, never a crash");
    }

    // ── Test 23: the decoder's LEARNED spreads persist — pairing cannot reshuffle
    // between opens, and the FIRST paint is already correct instead of settling as
    // decodes trickle in. (E6)
    {
        ComicReaderCore core;
        QVariantMap p = manualNormal();
        QVariantList ds; ds.append(3);
        p.insert(QStringLiteral("detectedSpreads"), ds);
        core.openEntry(QStringLiteral("t23"), plainPages, QStringLiteral("ltr"), p);
        CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == true,
              "T23 a persisted detected spread shapes the pairing BEFORE any decode");
        const QVariantList out =
            core.persistedState().value(QStringLiteral("detectedSpreads")).toList();
        CHECK(out.size() == 1 && out[0].toInt() == 3, "T23 detectedSpreads round-trips");
    }
    {
        // A book with no learned spreads must emit NO key at all, so absence round-trips as
        // absence and T12's byte-identical persisted-state check stays green.
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t23b"), plainPages, QStringLiteral("ltr"), manualNormal());
        CHECK(!core.persistedState().contains(QStringLiteral("detectedSpreads")),
              "T23 a book with no detected spreads emits no detectedSpreads key");
    }
    {
        // The USER's explicit verdict still wins over the machine's remembered observation:
        // a persisted detectedSpread of page 3 plus an override saying "not a spread" = single.
        ComicReaderCore core;
        QVariantMap p = manualNormal();
        QVariantList ds; ds.append(3);
        p.insert(QStringLiteral("detectedSpreads"), ds);
        QVariantMap so;
        so.insert(QStringLiteral("3"), false);
        p.insert(QStringLiteral("spreadOverrides"), so);
        core.openEntry(QStringLiteral("t23c"), plainPages, QStringLiteral("ltr"), p);
        CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == false,
              "T23 an explicit user override beats a remembered detected spread");
    }

    // ── Test 24: a MissingFile page HEALS on a later request — it is a cooldown,
    // not a life sentence. The real case is a page that vanishes or is unreadable at
    // decode time (typically still being written), which used to latch for the life of
    // the generation with no cure but closing and reopening the book. Corrupt and
    // unsupported pages stay latched, because re-decoding garbage heals nothing. (C6)
    //
    // SCOPE, stated honestly: the file must have EXISTED at openEntry. parsePages leaves
    // localPath empty for a file that is absent at open, so such a page has no path to
    // retry with and the cooldown cannot reach it - self-healing an at-open absence would
    // mean changing what an empty localPath means, which is a different task than this one.
    {
        const QString flaky = dir.filePath(QStringLiteral("flaky.png"));
        CHECK(writeSolidPng(flaky, 90), "T24 setup: the page exists when the book opens");
        const QVariantList pages =
            pagesFromPaths(QStringList() << plain[0] << plain[1] << flaky);

        ComicReaderCore core;
        core.openEntry(QStringLiteral("t24"), pages, QStringLiteral("ltr"), manualNormal());

        // it vanishes before the first request — the decode reports MissingFile
        CHECK(QFile::remove(flaky), "T24 setup: the page vanishes before it is ever requested");
        core.setVisible(QVariantList{ 2 });
        const bool failed = waitFor([&] { return core.pageInfo(2)
                                            .value(QStringLiteral("error")).toString()
                                            == QLatin1String("missing_file"); });
        CHECK(failed, "T24 a vanished page reports missing_file");

        // Back on disk, but INSIDE the cooldown: no re-attempt yet. This is also what stops
        // a per-frame stat storm against a file that genuinely is not there.
        CHECK(writeSolidPng(flaky, 90), "T24 setup: the file comes back");
        core.setVisible(QVariantList{ 2 });
        QThread::msleep(250);
        CHECK(core.pageInfo(2).value(QStringLiteral("error")).toString()
                  == QLatin1String("missing_file"),
              "T24 a request INSIDE the cooldown must not re-attempt the page");

        // ...and once the cooldown elapses, one fresh attempt succeeds and the page heals.
        QThread::msleep(2000);                       // kMissingRetryMs = 2000
        core.setVisible(QVariantList{ 2 });
        // NB: PageError::None serialises to the STRING "none", never "". An earlier draft asserted
        // isEmpty() here, which no healthy page can ever satisfy - the check could only fail.
        const bool healed = waitFor([&] {
            return core.pageInfo(2).value(QStringLiteral("error")).toString()
                       == QLatin1String("none");
        });
        CHECK(healed, "T24 the page decodes once the file is back - MissingFile is not a life sentence");
    }

    // Test 25: the app-facing descriptor accepts archive+entry and decodes it
    // directly, proving the CBZ source survives the QML/core boundary.
    {
        const QString cbzPath = dir.filePath(QStringLiteral("core-reader.cbz"));
        QString archiveError;
        CHECK(MangaTankoban::CbzArchive::writeImagesAtomic(
                  cbzPath, dir.path(),
                  QStringList{QStringLiteral("plain0.png"),
                              QStringLiteral("plain1.png")},
                  &archiveError),
              "T25 setup: core reader CBZ written");

        QVariantList pages;
        for (int i = 0; i < 2; ++i) {
            QVariantMap page;
            page.insert(QStringLiteral("index"), i);
            page.insert(QStringLiteral("archive"), cbzPath);
            page.insert(QStringLiteral("entry"),
                        QStringLiteral("plain%1.png").arg(i));
            page.insert(QStringLiteral("group"), 0);
            pages.append(page);
        }

        ComicReaderCore core;
        core.openEntry(QStringLiteral("t25-cbz"), pages,
                       QStringLiteral("ltr"), manualNormal());
        CHECK(core.pageCount() == 2,
              "T25 archive descriptors populate the reader page model");
        CHECK(core.pageInfo(0).value(QStringLiteral("error")).toString()
                  == QLatin1String("none"),
              "T25 a valid archive descriptor is analyzable");
        core.setVisible(QVariantList{0});
        const bool decoded = waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(decoded, "T25 core decodes a CBZ page without extracting it");
        CHECK(core.pageInfo(0).value(QStringLiteral("sourceKind")).toString()
                  == QLatin1String("cbz_entry"),
              "T25 pageInfo preserves the CBZ source kind");
    }

    // ══ Task 2 (overhaul plan 2026-07-28): windowed delivery ══════════════════

    // ── Test 26: requestRange makes the reader OWN a moving neighbourhood ────
    // Before this, both tiers held whatever LRU happened to keep — on a 1,452
    // page volume that is an arbitrary set with no relationship to where the
    // reader is. requestRange says it outright: keep the decoded pages within
    // ±2 of the viewport and the scaled pages from one behind to two ahead,
    // drop the rest, and never drop a pinned page (it is on screen).
    //
    // Asserted through the caches themselves rather than through a getter for
    // the window bounds: what matters is which pages SURVIVE, not what two
    // integers say.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t26"), winPages, QStringLiteral("ltr"), manualNormal());
        const quint64 gen = core.generation();

        // A real pin, far outside the window we are about to declare.
        core.setVisible(QVariantList{5});
        CHECK(waitFor([&] {
            return core.pageInfo(5).value(QStringLiteral("decoded")).toBool();
        }), "T26 the pinned page decodes so there is a real entry to protect");

        QImage tile(64, 96, QImage::Format_ARGB32);
        tile.fill(qRgb(30, 30, 30));
        for (const int p : {16, 17, 18, 20, 24, 25, 26})
            core.pageCache()->insert(gen, p, tile);
        for (const int p : {18, 19, 20, 24, 25})
            core.scaleCache()->insert({gen, p, QSize(400, 0), 100, 0}, tile);

        core.requestRange(20, 22);

        // Decoded retention: 18..24 (visible ±2).
        CHECK(core.pageCache()->get(gen, 18).has_value(), "T26 decoded 18 (first in window) retained");
        CHECK(core.pageCache()->get(gen, 20).has_value(), "T26 decoded 20 (visible) retained");
        CHECK(core.pageCache()->get(gen, 24).has_value(), "T26 decoded 24 (last in window) retained");
        CHECK(!core.pageCache()->get(gen, 17).has_value(), "T26 decoded 17 is one page too far behind");
        CHECK(!core.pageCache()->get(gen, 16).has_value(), "T26 decoded 16 is further behind still");
        CHECK(!core.pageCache()->get(gen, 25).has_value(), "T26 decoded 25 is one page too far ahead");
        CHECK(!core.pageCache()->get(gen, 26).has_value(), "T26 decoded 26 is further ahead still");
        CHECK(core.pageCache()->get(gen, 5).has_value(),
              "T26 the PINNED page survives the sweep from far outside the window");

        // Scaled retention: 19..24 (one behind, two ahead) — tighter behind,
        // because a scale is cheap to redo and the reader is going forward.
        const auto scaledAt = [&](int page) {
            return core.scaleCache()->get({gen, page, QSize(400, 0), 100, 0}).has_value();
        };
        CHECK(scaledAt(19), "T26 scaled 19 (one behind the viewport) retained");
        CHECK(scaledAt(20), "T26 scaled 20 (visible) retained");
        CHECK(scaledAt(24), "T26 scaled 24 (two ahead) retained");
        CHECK(!scaledAt(18), "T26 scaled 18 is outside the tighter scaled window");
        CHECK(!scaledAt(25), "T26 scaled 25 is beyond the prefetch");

        // The scaled tier's capacity is DRIVEN by the window, not a guess: the
        // scaled range here is [19, 24], six pages, at two entries per page.
        // This is the assertion that keeps the rework honest — if the capacity
        // ever stops following the window, the tier silently goes back to being
        // sized by something unrelated to where the reader is.
        CHECK(core.scaleCache()->capacity() == 12,
              "T26 the scaled capacity IS the retained window (6 pages x 2 entries)");
        core.requestRange(20, 20);
        CHECK(core.scaleCache()->capacity() == 8,
              "T26 a narrower window narrows the capacity with it (4 pages x 2)");

        // ⚠ The repeat-call guard. Task 8 wires this to a per-frame scroll
        // signal, and an unguarded call walks both cache hashes under both
        // mutexes and frees QImages on the GUI thread — contending the very
        // mutex every provider worker takes for every page fetch. A repeat of an
        // identical clamped range must be a two-integer compare and nothing
        // else. Nothing asserted this before, which is how a per-frame cascade
        // gets into a build (it is what the video player's stutter turned out to
        // be earlier this month).
        core.requestRange(20, 22);
        const int capacityAfterFirst = core.scaleCache()->capacity();
        core.pageCache()->insert(gen, 26, QImage(8, 8, QImage::Format_ARGB32));
        core.requestRange(20, 22);   // byte-identical repeat
        CHECK(core.pageCache()->get(gen, 26).has_value(),
              "T26 a REPEATED identical range returns early — it did not sweep again");
        CHECK(core.scaleCache()->capacity() == capacityAfterFirst,
              "T26 and it left the capacity alone");
        core.requestRange(21, 23);   // a real move must still sweep
        CHECK(!core.pageCache()->get(gen, 26).has_value(),
              "T26 a CHANGED range sweeps normally — the guard is not a lock-out");

        // Degenerate input must clamp, never index out of the book.
        core.requestRange(-40, 9999);
        core.requestRange(28, 3);   // inverted
        CHECK(core.pageCount() == 32, "T26 a degenerate range never disturbs the entry");
    }
    {
        // An entry-less core has no range to own and must not crash on one.
        ComicReaderCore empty;
        empty.requestRange(0, 4);
        CHECK(empty.pageCount() == 0, "T26 requestRange on an entry-less core is a no-op, never a crash");
    }

    // ── Test 27: imageUrl carries a TIER, and QML can still call it one-armed ─
    // Three live QML call sites use the single-argument form. The tier rides on
    // a default argument, which only works if Qt's meta-object system really
    // registers BOTH arities — so this asks QML itself rather than trusting it.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t27"), plainPages, QStringLiteral("ltr"), manualNormal());

        CHECK(core.imageUrl(0).contains(QStringLiteral("tier=hq")),
              "T27 the default tier is hq");
        CHECK(core.imageUrl(0, QStringLiteral("preview")).contains(QStringLiteral("tier=preview")),
              "T27 preview is carried in the url");
        CHECK(core.imageUrl(0, QStringLiteral("thumbnail")).contains(QStringLiteral("tier=thumbnail")),
              "T27 thumbnail is carried in the url");
        CHECK(core.imageUrl(0, QStringLiteral("nonsense")).contains(QStringLiteral("tier=hq")),
              "T27 an unknown tier normalises to hq rather than emitting a url nothing can parse");
        CHECK(core.imageUrl(0).startsWith(QStringLiteral("image://comicreader/%1/0?rev=")
                                              .arg(core.generation())),
              "T27 the existing grammar is unchanged ahead of the new query key");

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("core"), &core);
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(
                              "import QtQml\n"
                              "QtObject {\n"
                              "    property string oneArg: core.imageUrl(0)\n"
                              "    property string twoArg: core.imageUrl(0, \"preview\")\n"
                              "}\n"),
                          QUrl());
        QObject* probe = component.create();
        if (!probe)
            std::fprintf(stderr, "  T27 QML error: %s\n",
                         component.errorString().toUtf8().constData());
        CHECK(probe != nullptr, "T27 the QML probe builds");
        if (probe) {
            CHECK(probe->property("oneArg").toString() == core.imageUrl(0),
                  "T27 QML CAN call the one-argument form — the default argument is registered");
            CHECK(probe->property("twoArg").toString()
                      == core.imageUrl(0, QStringLiteral("preview")),
                  "T27 QML can call the two-argument form too");
            delete probe;
        }
    }

    // ── Test 28: deliveryMetrics reports, and reporting does not reset ───────
    // Task 12's performance gate reads these. A counter that cleared itself on
    // read would make two gates disagree for no reason anybody could find.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t28"), plainPages, QStringLiteral("ltr"), manualNormal());
        core.setVisible(QVariantList{0});
        CHECK(waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        }), "T28 a page decodes so there is something to measure");

        const QVariantMap first = core.deliveryMetrics();
        for (const char* key : {"sourceHits", "scaledHits", "scaleJobs", "cancelledJobs",
                                "staleDrops", "maxDispatchUs", "maxResponseMs",
                                "maxDecodedResident", "maxScaledResident"}) {
            CHECK(first.contains(QLatin1String(key)),
                  QByteArray("T28 deliveryMetrics reports ").append(key).constData());
        }
        CHECK(first.value(QStringLiteral("maxDecodedResident")).toULongLong() >= 1,
              "T28 maxDecodedResident counts resident ENTRIES (what Task 12's gate reads), not bytes");

        const QVariantMap second = core.deliveryMetrics();
        CHECK(second.value(QStringLiteral("maxDecodedResident")).toULongLong()
                  >= first.value(QStringLiteral("maxDecodedResident")).toULongLong(),
              "T28 reading the metrics does not reset them");
    }

    // ══ Task 4 (overhaul plan 2026-07-28): the unit as ONE presentation ═══════

    // ── Test 29: presentationForPage NEVER re-derives the pairing law ────────
    // The whole risk of a second unit-shaped query is that it becomes a second,
    // weaker opinion about what a unit is. It must read m_units and nothing else,
    // so for EVERY page the two queries have to agree on all four unit fields —
    // and the law itself (cover alone, page 1 alone, pairing from index 2, a
    // spread standing alone with no pair straddling it) has to survive the trip.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t29"), plainPages, QStringLiteral("ltr"), manualNormal());

        auto agrees = [&](int page) {
            const QVariantMap u = core.unitForPage(page);
            const QVariantMap p = core.presentationForPage(page);
            for (const char* key : {"rightIndex", "leftIndex", "spread", "coverAlone"}) {
                if (u.value(QLatin1String(key)) != p.value(QLatin1String(key)))
                    return false;
            }
            return true;
        };
        bool allAgree = true;
        for (int page = 0; page < core.pageCount(); ++page)
            allAgree = allAgree && agrees(page);
        CHECK(allAgree, "T29 presentationForPage reports the CANONICAL unit for every page");

        // The law, read back through the new query (Hemanth's pairing rule).
        const QVariantMap cover = core.presentationForPage(0);
        CHECK(cover.value(QStringLiteral("rightIndex")).toInt() == 0
                  && cover.value(QStringLiteral("leftIndex")).toInt() == -1
                  && cover.value(QStringLiteral("coverAlone")).toBool(),
              "T29 the cover is one page, alone");
        const QVariantMap first = core.presentationForPage(1);
        CHECK(first.value(QStringLiteral("rightIndex")).toInt() == 1
                  && first.value(QStringLiteral("leftIndex")).toInt() == -1,
              "T29 page 1 is one page, alone (the lone recto facing the inside cover)");
        const QVariantMap pair = core.presentationForPage(2);
        CHECK(pair.value(QStringLiteral("rightIndex")).toInt() == 2
                  && pair.value(QStringLiteral("leftIndex")).toInt() == 3,
              "T29 pairing begins at index 2");
        CHECK(core.presentationForPage(3).value(QStringLiteral("rightIndex")).toInt() == 2,
              "T29 either page of a pair describes the SAME unit");

        // A spread interrupts the walk: it stands alone and nothing pairs across it.
        core.setSpreadOverride(2, QStringLiteral("spread"));
        const QVariantMap spread = core.presentationForPage(2);
        CHECK(spread.value(QStringLiteral("spread")).toBool()
                  && spread.value(QStringLiteral("leftIndex")).toInt() == -1,
              "T29 a spread is one full-width unit, never half of a pair");
        CHECK(core.presentationForPage(3).value(QStringLiteral("rightIndex")).toInt() == 3
                  && core.presentationForPage(3).value(QStringLiteral("leftIndex")).toInt() == 4,
              "T29 the pages AFTER a spread re-pair among themselves, never across it");

        // Shape: the map always carries the two presentation fields, so QML can
        // read `state` without first testing whether the key exists.
        CHECK(pair.contains(QStringLiteral("state")) && pair.contains(QStringLiteral("errorCode")),
              "T29 every presentation carries state + errorCode");

        // No entry: the SAME empty answer unitForPage gives, so QML normalises once.
        ComicReaderCore empty;
        CHECK(empty.presentationForPage(0).isEmpty(),
              "T29 with no entry open the presentation is empty, exactly like unitForPage");
        CHECK(empty.unitForPage(0).isEmpty(), "T29 precondition: unitForPage is empty with no entry");
    }

    // ── Test 30: a pair is WHOLE or it is nothing ────────────────────────────
    // The defect: the double surface paints whichever half decoded first, so a
    // spread arrives as one page plus a black rectangle. The unit must stay
    // "waiting" while EITHER member is still without pixels, and flip to "ready"
    // as one thing.
    //
    // Both decode lanes are held on a gate — but ONLY for the two pages under
    // test, so the prefetched neighbours run free and nothing is left blocked at
    // teardown. (Declared before `core` so the pool tears down while the capture
    // is still alive — the decode harness's house rule.)
    {
        QSemaphore decodeGate;
        ComicReaderCore waitingCore;
        waitingCore.setDecodeWorkerHooksForTest(
            [&](quint64, int page) { if (page == 2 || page == 3) decodeGate.acquire(); },
            std::function<void(quint64, int)>());
        waitingCore.openEntry(QStringLiteral("pair-wait"), plainPages, QStringLiteral("ltr"),
                              manualNormal());
        waitingCore.setVisible(QVariantList{2, 3});

        CHECK(waitingCore.presentationForPage(2).value(QStringLiteral("state"))
                  == QLatin1String("waiting"),
              "T30 a pair remains whole while either page waits");

        // Let ONE half through and hold the other: the half that landed must NOT
        // promote the unit on its own. This is the assertion the defect breaks.
        decodeGate.release(1);
        const bool oneLanded = waitFor([&] {
            return waitingCore.pageInfo(2).value(QStringLiteral("decoded")).toBool()
                   || waitingCore.pageInfo(3).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(oneLanded, "T30 setup: one half of the pair decodes while the other is held");
        CHECK(waitingCore.presentationForPage(2).value(QStringLiteral("state"))
                  == QLatin1String("waiting"),
              "T30 one decoded half does NOT make the unit paintable (this is the half-black bug)");

        decodeGate.release(1);
        CHECK(waitFor([&] {
                  return waitingCore.presentationForPage(2).value(QStringLiteral("state"))
                         == QLatin1String("ready");
              }),
              "T30 the pair becomes ready as one unit");
        CHECK(waitingCore.presentationForPage(3).value(QStringLiteral("state"))
                  == QLatin1String("ready"),
              "T30 both pages of the pair report the SAME unit state");
        // (No assertion here that a FARTHER unit is still waiting: setVisible pins
        // visible+neighbours, so the (4,5) unit is prefetched by this very call and
        // is legitimately ready. An earlier draft asserted otherwise and failed —
        // the prefetch is the feature, not the bug.)
        decodeGate.release(8);   // nothing should be parked, but never leave a lane held
    }

    // ── Test 31: one failed partner is ONE deliberate error, not a half-paint ─
    // A page that can never arrive must stop the unit waiting forever — and the
    // verdict belongs to the unit, so the good half does not quietly paint alone
    // as if the spread were complete.
    {
        const QString corruptPath = dir.filePath(QStringLiteral("corrupt.png"));
        QFile corrupt(corruptPath);
        CHECK(corrupt.open(QIODevice::WriteOnly), "T31 setup: corrupt fixture opened");
        corrupt.write("not an image");
        corrupt.close();

        QStringList errorPaths = plain;
        errorPaths[3] = corruptPath;             // the LEFT half of the (2,3) pair
        ComicReaderCore errorCore;
        errorCore.openEntry(QStringLiteral("pair-error"), pagesFromPaths(errorPaths),
                            QStringLiteral("ltr"), manualNormal());
        errorCore.setVisible(QVariantList{2, 3});

        CHECK(waitFor([&] {
                  return errorCore.presentationForPage(2).value(QStringLiteral("state"))
                         == QLatin1String("error");
              }),
              "T31 one failed partner yields one deliberate error unit");
        const QString code = errorCore.presentationForPage(2)
                                 .value(QStringLiteral("errorCode")).toString();
        CHECK(!code.isEmpty() && code != QLatin1String("none"),
              QByteArray("T31 the error unit names WHY, got '")
                  .append(code.toUtf8()).append("'").constData());
        CHECK(errorCore.presentationForPage(3).value(QStringLiteral("state"))
                  == QLatin1String("error"),
              "T31 both pages of a broken pair report the unit's error");
        // Now let the GOOD half land, so "error" is proved to be the UNIT's verdict
        // overriding a ready member rather than an artefact of nothing having decoded
        // yet. (The error verdict arrives first — a corrupt file fails faster than a
        // real page decodes — so this has to be waited for, not assumed.)
        CHECK(waitFor([&] {
                  return errorCore.pageInfo(2).value(QStringLiteral("decoded")).toBool();
              }),
              "T31 setup: the surviving half of the broken pair decodes");
        CHECK(errorCore.presentationForPage(2).value(QStringLiteral("state"))
                  == QLatin1String("error"),
              "T31 a decoded half NEVER promotes a broken unit to paintable");
        // A healthy unit in the SAME book is unaffected: the failure is this unit's.
        errorCore.setVisible(QVariantList{4, 5});
        CHECK(waitFor([&] {
                  return errorCore.presentationForPage(4).value(QStringLiteral("state"))
                         == QLatin1String("ready");
              }),
              "T31 a broken pair does not poison the rest of the book");
    }

    // ── Test 32: the render profile as a PURE transform (Task 7) ─────────────
    // The four laws that everything downstream leans on: identity is byte-stable
    // (not "looks the same" — the same object), tone actually moves luminance,
    // rotation actually moves geometry, and nothing here can crash on a
    // degenerate input.
    {
        // 4x4 mid-grey with ONE marked corner. The mark matters: a square fixture
        // cannot tell a real 90-degree rotation from a no-op, so without it the
        // size assertion below would pass on a function that did nothing at all.
        QImage src(4, 4, QImage::Format_ARGB32);
        src.fill(qRgb(128, 128, 128));
        src.setPixel(0, 0, qRgb(255, 255, 255));

        const auto luma = [](const QImage& image) { return qGray(image.pixel(1, 1)); };

        RenderProfile identity;
        CHECK(applyRenderProfile(src, identity) == src, "T32 identity profile is byte-stable");
        // ...and byte-stable means THE SAME OBJECT, not a copy that happens to
        // compare equal. This assertion exists because the equality one above is
        // NOT enough on its own: `src` is already Format_ARGB32, so a version of
        // applyRenderProfile that unconditionally convertToFormat(ARGB32)'d its
        // input passed every content comparison here — measured, not assumed. The
        // shared data pointer is what actually pins "a no-op profile costs
        // nothing", which is what the delivery worker's untouched path relies on.
        CHECK(applyRenderProfile(src, identity).constBits() == src.constBits(),
              "T32 identity profile returns the SAME image, not an equal copy");
        // A fixture in a DIFFERENT format, for the same reason: an unconditional
        // conversion is invisible to a fixture that is already in the target
        // format, so the identity law needs one that is not.
        QImage srcRgb(4, 4, QImage::Format_RGB32);
        srcRgb.fill(qRgb(128, 128, 128));
        CHECK(applyRenderProfile(srcRgb, identity).format() == QImage::Format_RGB32,
              "T32 identity profile does not even change the pixel FORMAT");
        CHECK(applyRenderProfile(srcRgb, identity).constBits() == srcRgb.constBits(),
              "T32 identity profile is the same object whatever the source format");
        QImage srcGray(4, 4, QImage::Format_Grayscale8);
        srcGray.fill(128);
        CHECK(applyRenderProfile(srcGray, identity).format() == QImage::Format_Grayscale8,
              "T32 identity profile leaves an 8-bit grayscale scan 8-bit");

        RenderProfile brightProfile;
        brightProfile.brightness = 20;
        CHECK(luma(applyRenderProfile(src, brightProfile)) > luma(src),
              "T32 brightness raises luma");
        RenderProfile darkProfile;
        darkProfile.brightness = -20;
        CHECK(luma(applyRenderProfile(src, darkProfile)) < luma(src),
              "T32 negative brightness lowers luma");

        // Gamma and contrast are exercised on a DARK sample, because mid-grey is
        // contrast's fixed point — a contrast fixture at 128 passes whatever the
        // function does with it.
        QImage dark(4, 4, QImage::Format_ARGB32);
        dark.fill(qRgb(64, 64, 64));
        RenderProfile gammaUp;
        gammaUp.gamma = 200;
        CHECK(qGray(applyRenderProfile(dark, gammaUp).pixel(0, 0)) > 64,
              "T32 gamma above 100 lifts the shadows");
        RenderProfile gammaDown;
        gammaDown.gamma = 50;
        CHECK(qGray(applyRenderProfile(dark, gammaDown).pixel(0, 0)) < 64,
              "T32 gamma below 100 deepens the shadows");
        RenderProfile contrastUp;
        contrastUp.contrast = 100;
        CHECK(qGray(applyRenderProfile(dark, contrastUp).pixel(0, 0)) < 64,
              "T32 contrast pushes a below-mid tone further down");

        RenderProfile rotate90;
        rotate90.rotation = 90;
        const QImage rotated = applyRenderProfile(src, rotate90);
        CHECK(rotated.size() == QSize(4, 4), "T32 rotation keeps square fixture size");
        CHECK(qGray(rotated.pixel(0, 0)) != qGray(src.pixel(0, 0)),
              "T32 a 90-degree rotation actually MOVES the pixels");
        // ...and on a non-square page it swaps the dimensions, which is the whole
        // reason geometry has to run before a width-driven scale.
        QImage tall(4, 6, QImage::Format_ARGB32);
        tall.fill(qRgb(128, 128, 128));
        CHECK(applyRenderProfile(tall, rotate90).size() == QSize(6, 4),
              "T32 rotation swaps the dimensions of a non-square page");
        RenderProfile rotate180;
        rotate180.rotation = 180;
        CHECK(applyRenderProfile(tall, rotate180).size() == QSize(4, 6),
              "T32 a 180-degree rotation leaves the dimensions alone");

        // Stage split: Geometry must not touch tone, Tone must not touch geometry.
        RenderProfile both;
        both.rotation = 90;
        both.brightness = 40;
        const QImage geoOnly = applyRenderProfile(tall, both, RenderStage::Geometry);
        CHECK(geoOnly.size() == QSize(6, 4), "T32 the Geometry stage rotates");
        CHECK(qGray(geoOnly.pixel(0, 0)) == 128, "T32 the Geometry stage leaves tone alone");
        const QImage toneOnly = applyRenderProfile(tall, both, RenderStage::Tone);
        CHECK(toneOnly.size() == QSize(4, 6), "T32 the Tone stage leaves geometry alone");
        CHECK(qGray(toneOnly.pixel(0, 0)) > 128, "T32 the Tone stage brightens");

        // Degenerate inputs are answers, not crashes.
        CHECK(applyRenderProfile(QImage(), brightProfile).isNull(),
              "T32 a null image survives the transform");
        QImage speck(2, 2, QImage::Format_ARGB32);
        speck.fill(qRgb(200, 200, 200));
        RenderProfile crop;
        crop.autoCrop = true;
        CHECK(applyRenderProfile(speck, crop) == speck,
              "T32 auto-crop declines a page too small to probe rather than eating it");
        QImage blank(64, 64, QImage::Format_ARGB32);
        blank.fill(qRgb(255, 255, 255));
        CHECK(applyRenderProfile(blank, crop) == blank,
              "T32 auto-crop declines a uniform page (nothing to crop TO)");
        // ...and on a page that DOES have margins it finds the content box.
        QImage margined(64, 64, QImage::Format_ARGB32);
        margined.fill(qRgb(255, 255, 255));
        for (int y = 16; y < 48; ++y)
            for (int x = 16; x < 48; ++x)
                margined.setPixel(x, y, qRgb(10, 10, 10));
        const QImage cropped = applyRenderProfile(margined, crop);
        CHECK(cropped.width() < 64 && cropped.height() < 64,
              "T32 auto-crop trims a page that really does have margins");
        CHECK(cropped.width() >= 32 && cropped.height() >= 32,
              "T32 auto-crop never bites into the ink it found");
    }

    // ── Test 33: validation at the boundary (Task 7) ──────────────────────────
    // The profile is persisted, so what reaches normalizeRenderProfile may be
    // hand-edited or written by a future version. Out of range clamps; garbage
    // keeps the DEFAULT (never 0 — gamma 0 is a black page); unknown keys are
    // ignored; and the canonical map is a fixed point.
    {
        QVariantMap wild;
        wild.insert(QStringLiteral("brightness"), 5000);
        wild.insert(QStringLiteral("contrast"), -5000);
        wild.insert(QStringLiteral("gamma"), 99999);
        wild.insert(QStringLiteral("rotation"), 45);
        wild.insert(QStringLiteral("quality"), QStringLiteral("nonsense"));
        wild.insert(QStringLiteral("somethingFromTheFuture"), QStringLiteral("ignore me"));
        const RenderProfile clamped = normalizeRenderProfile(wild);
        CHECK(clamped.brightness == 100, "T33 brightness clamps to +100");
        CHECK(clamped.contrast == -100, "T33 contrast clamps to -100");
        CHECK(clamped.gamma == 300, "T33 gamma clamps to 300");
        CHECK(clamped.rotation == 90, "T33 an off-grid rotation snaps to the nearest quarter turn");
        CHECK(clamped.quality == RenderQuality::Balanced, "T33 an unknown quality falls back to balanced");

        QVariantMap low;
        low.insert(QStringLiteral("gamma"), 0);
        CHECK(normalizeRenderProfile(low).gamma == 10, "T33 gamma 0 clamps UP to 10, never a black page");

        QVariantMap junk;
        junk.insert(QStringLiteral("gamma"), QStringLiteral("not a number"));
        junk.insert(QStringLiteral("brightness"), QStringLiteral(""));
        CHECK(normalizeRenderProfile(junk).gamma == 100,
              "T33 an unparseable gamma keeps the DEFAULT (100), not 0");
        CHECK(normalizeRenderProfile(junk).brightness == 0,
              "T33 an unparseable brightness keeps the default");

        QVariantMap turns;
        turns.insert(QStringLiteral("rotation"), -90);
        CHECK(normalizeRenderProfile(turns).rotation == 270, "T33 -90 folds to 270");
        turns.insert(QStringLiteral("rotation"), 450);
        CHECK(normalizeRenderProfile(turns).rotation == 90, "T33 450 folds to 90");
        turns.insert(QStringLiteral("rotation"), 10);
        CHECK(normalizeRenderProfile(turns).rotation == 0, "T33 a near-zero angle snaps to 0");

        CHECK(normalizeRenderProfile(QVariantMap{}).isIdentity(),
              "T33 an EMPTY map is the identity profile");

        RenderProfile round;
        round.brightness = -37;
        round.contrast = 12;
        round.gamma = 175;
        round.rotation = 270;
        round.autoCrop = true;
        round.nightFilter = true;
        round.quality = RenderQuality::Best;
        CHECK(normalizeRenderProfile(renderProfileToVariantMap(round)) == round,
              "T33 the canonical map is a FIXED POINT through normalization");

        // nightFilter is carried and validated, but it is NOT a pixel operation:
        // it must never move the revision or change a delivered byte.
        RenderProfile day;
        RenderProfile night;
        night.nightFilter = true;
        CHECK(day.samePixelsAs(night), "T33 the night filter is not a pixel-affecting field");
        CHECK(day != night, "T33 ...but it IS part of the profile's identity");
        QImage sample(4, 4, QImage::Format_ARGB32);
        sample.fill(qRgb(90, 90, 90));
        CHECK(applyRenderProfile(sample, night) == sample,
              "T33 the night filter changes no pixel here — the shell paints the veil");
    }

    // ── Test 34: the profile through the CORE (Task 7) ────────────────────────
    // The two costs that decide whether this is a live control or a stutter: a
    // change invalidates the SCALED tier (that is what the revision is for) and
    // never the DECODED one (adjusting brightness must not send the reader back
    // to disk). Plus the url must actually change, or QML's own pixmap cache
    // would serve the pre-adjustment page forever and the slider would do
    // nothing visible at all.
    {
        ComicReaderCore core;
        core.openEntry(QStringLiteral("t34"), plainPages, QStringLiteral("ltr"), manualNormal());
        const quint64 gen = core.generation();
        core.setVisible(QVariantList{0});
        CHECK(waitFor([&] { return core.pageInfo(0).value(QStringLiteral("decoded")).toBool(); }),
              "T34 setup: page 0 decodes so there is a decoded entry to protect");

        QImage tile(64, 96, QImage::Format_ARGB32);
        tile.fill(qRgb(30, 30, 30));
        core.scaleCache()->insert({gen, 0, QSize(400, 0), 100, core.renderRevision()}, tile);
        CHECK(core.scaleCache()->entryCount() == 1, "T34 setup: the scaled tier holds the seeded entry");

        const quint64 before = core.renderRevision();
        const QString urlBefore = core.imageUrl(0);
        QVariantMap p = core.renderProfile();
        p.insert(QStringLiteral("brightness"), 20);
        core.setRenderProfile(p);

        CHECK(core.renderRevision() == before + 1, "T34 profile increments render revision");
        CHECK(core.pageCache()->get(core.generation(), 0).has_value(), "T34 profile keeps decoded source");
        CHECK(core.scaleCache()->entryCount() == 0,
              "T34 a pixel-affecting change drops the entries for the OLD render revision");
        CHECK(core.imageUrl(0) != urlBefore,
              "T34 the image url changes, so QML's own pixmap cache cannot serve the old pixels");
        CHECK(core.renderProfile().value(QStringLiteral("brightness")).toInt() == 20,
              "T34 the profile reads back what was set");

        // Idempotent: re-applying the same profile is not a change, so it costs
        // nothing. Without this a QML binding that re-pushes on every frame would
        // clear the scaled tier 60 times a second.
        const quint64 after = core.renderRevision();
        core.scaleCache()->insert({gen, 0, QSize(400, 0), 100, after}, tile);
        core.setRenderProfile(core.renderProfile());
        CHECK(core.renderRevision() == after, "T34 re-applying the SAME profile does not bump the revision");
        CHECK(core.scaleCache()->entryCount() == 1, "T34 ...and does not drop a scaled entry");

        // The night filter is free: a real change (it reads back) that costs no
        // revision and no scaled entry, because the shell paints it as a veil.
        QVariantMap night = core.renderProfile();
        night.insert(QStringLiteral("nightFilter"), true);
        core.setRenderProfile(night);
        CHECK(core.renderProfile().value(QStringLiteral("nightFilter")).toBool(),
              "T34 the night filter is stored in the profile");
        CHECK(core.renderRevision() == after, "T34 the night filter NEVER bumps the render revision");
        CHECK(core.scaleCache()->entryCount() == 1, "T34 the night filter NEVER drops a scaled entry");

        // setRenderProfile REPLACES — a partial map resets the fields it omits.
        // That is the contract the shell is built on (it always sends a whole
        // map, built from renderProfile()), and it has to be pinned or a caller
        // will one day send `{brightness: 10}` and silently lose the rotation.
        QVariantMap partial;
        partial.insert(QStringLiteral("contrast"), 15);
        core.setRenderProfile(partial);
        CHECK(core.renderProfile().value(QStringLiteral("contrast")).toInt() == 15,
              "T34 a partial map applies what it names");
        CHECK(core.renderProfile().value(QStringLiteral("brightness")).toInt() == 0,
              "T34 setRenderProfile REPLACES: an omitted field returns to its default");

        // Clamping is enforced at the core's own door, not only in the helper.
        QVariantMap wild;
        wild.insert(QStringLiteral("gamma"), 99999);
        wild.insert(QStringLiteral("rotation"), 45);
        core.setRenderProfile(wild);
        CHECK(core.renderProfile().value(QStringLiteral("gamma")).toInt() == 300,
              "T34 gamma clamps at the core boundary");
        CHECK(core.renderProfile().value(QStringLiteral("rotation")).toInt() == 90,
              "T34 rotation snaps at the core boundary");

        // ...and the whole thing reaches the DELIVERED page. Nothing above proves
        // the worker actually consults the profile; this does.
        core.setRenderProfile(QVariantMap{});
        ComicReaderProvider* provider = core.createProvider();
        const QString id = QStringLiteral("%1/0").arg(core.generation());
        const QImage plainServed = providerServedImage(provider, id);
        CHECK(!plainServed.isNull(), "T34 setup: the provider serves page 0 unadjusted");
        QVariantMap bright;
        bright.insert(QStringLiteral("brightness"), 60);
        core.setRenderProfile(bright);
        const QImage brightServed = providerServedImage(provider, id);
        CHECK(!brightServed.isNull(), "T34 the provider still serves page 0 with a profile applied");
        if (!plainServed.isNull() && !brightServed.isNull()) {
            CHECK(qGray(brightServed.pixel(0, 0)) > qGray(plainServed.pixel(0, 0)),
                  "T34 the delivered page is actually brighter — the profile reaches the worker");
        }
        // A rotation reaches it too, and it changes the delivered GEOMETRY.
        QVariantMap turned;
        turned.insert(QStringLiteral("rotation"), 90);
        core.setRenderProfile(turned);
        const QImage turnedServed = providerServedImage(provider, id);
        CHECK(!turnedServed.isNull() && turnedServed.width() > turnedServed.height(),
              "T34 a 90-degree rotation delivers a LANDSCAPE page from a portrait scan");
        delete provider;
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_CORE_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
