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
#include "comicreader/ComicReaderStripModel.h"   // T14 reads the strip geometry roles
#include "comicreader/ComicReaderTypes.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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
    QCoreApplication app(argc, argv);

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
        QSize sz;
        QImage liveA = provider->requestImage(QStringLiteral("%1/0").arg(genA), &sz, QSize());
        CHECK(!liveA.isNull(), "T9 provider returns the live image for gen A page 0");

        // imageUrl encodes the current generation + a rev token.
        CHECK(core.imageUrl(0).startsWith(
                  QStringLiteral("image://comicreader/%1/0?rev=").arg(genA)),
              "T9 imageUrl(0) is image://comicreader/<genA>/0?rev=N");

        // Open a second entry: generation bumps and gen A is now dead.
        core.openEntry(QStringLiteral("genB"), plainPages, QStringLiteral("ltr"), manualNormal());
        const quint64 genB = core.generation();
        CHECK(genB > genA, "T9 opening a new entry bumps the generation");

        QImage deadA = provider->requestImage(QStringLiteral("%1/0").arg(genA), &sz, QSize());
        CHECK(deadA.isNull(),
              "T9 provider returns NULL for the superseded generation A (dead imageUrl)");

        core.setVisible(QVariantList{0});
        const bool dB0 = waitFor([&] {
            return core.pageInfo(0).value(QStringLiteral("decoded")).toBool();
        });
        CHECK(dB0, "T9 entry B page 0 decodes under the new generation");
        QImage liveB = provider->requestImage(QStringLiteral("%1/0").arg(genB), &sz, QSize());
        CHECK(!liveB.isNull(), "T9 provider returns the live image for gen B page 0");

        // A malformed id returns null, never crashes.
        QImage bogus = provider->requestImage(QStringLiteral("not-a-real-id"), &sz, QSize());
        CHECK(bogus.isNull(), "T9 provider returns NULL for a malformed id");

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
        QSize sz;
        CHECK(!provider->requestImage(QStringLiteral("%1/0").arg(gen), &sz, QSize()).isNull(),
              "T10 provider serves the page while the entry is open");
        core.closeEntry();
        CHECK(core.pageCount() == 0, "T10 closeEntry clears the page count");
        CHECK(provider->requestImage(QStringLiteral("%1/0").arg(gen), &sz, QSize()).isNull(),
              "T10 provider returns NULL after closeEntry (generation retired)");
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

    if (g_failures == 0) {
        std::puts("COMICREADER_CORE_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
