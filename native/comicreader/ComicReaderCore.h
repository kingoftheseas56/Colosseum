// native/comicreader/ComicReaderCore.h
//
// The ONE app-facing backend for the from-scratch Comic Reader (Agent 1, plan
// 2026-07-23, Task 7). This is where the five pure engine modules become a single
// orchestrated whole and are exposed to QML as the `ComicReaderCore` context
// property plus an `image://comicreader/` provider:
//
//   * ComicReaderTypes / ComicReaderPairing — value types + the canonical
//     double-page pairing walk (units for the current coupling phase).
//   * ComicReaderPageCache — pinned, budgeted LRU of decoded QImages, keyed by
//     (generation, page). Owned here.
//   * ComicReaderDecode — the generation-safe decode coordinator. Constructed
//     and destroyed on THIS object's thread (its affinity invariant); its queued
//     results land back on this thread.
//   * ComicReaderCoupling — the pure auto-coupling probe. Once per Auto+unresolved
//     entry, the core decodes candidate pairs under both phases at LOW priority,
//     collects edgeContinuityCost per pair into the FULL per-phase cost vectors
//     (both non-empty), and calls chooseCouplingPhase exactly once. The two phases
//     routinely have DIFFERENT sample counts (each phase's pairing yields its own
//     seams); mean aggregation in chooseCouplingPhase handles that — the vectors
//     are NOT trimmed to equal length (doing so would drop real seams and could
//     flip the verdict).
//   * ComicReaderStripModel — the Long Strip geometry model. Owned here; fed
//     PageMeta as pages decode; drives the decode window and scroll compensation.
//
// Ported from Tankoban 2's ComicReader.cpp as BEHAVIOUR, not code: no QWidget, no
// GUI-thread decode. QML paints; C++ (this) decides.
//
// Compensation surfacing: the strip model's anti-jump compensation is emitted to
// QML as `stripCompensation(double delta)`. The QML strip surface adds the
// delta to its own scroll position after a decode batch shifts pages above the
// fold. (Documented here per the Task 7 brief's "your call" for this seam.)
//
// A handful of extra Q_INVOKABLE getters (persistedState, pinnedPages,
// cacheBudget, couplingProbeDebug) are additive to the brief's minimum surface:
// persistedState is required for the shell (Task 9) to save/restore state (and is
// the round-trip oracle); the other three are cheap, read-only observability the
// harness and QML debugging use. None mutate state.
#pragma once

#include "comicreader/ComicReaderCoupling.h"
#include "comicreader/ComicReaderPageCache.h"
#include "comicreader/ComicReaderRenderProfile.h"
#include "comicreader/ComicReaderScaleCache.h"
#include "comicreader/ComicReaderTypes.h"

#include <QAbstractListModel>   // full type: the stripModel Q_PROPERTY needs its metatype
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>

#include <QtGlobal>

namespace comicreader {

class ComicReaderDecode;
class ComicReaderStripModel;
class ComicReaderProvider;

// Decode priorities (higher runs sooner in QThreadPool). Exposed here (not just the .cpp's
// anonymous namespace) so the strip decode-priority helper below is directly unit-testable.
constexpr int kPrioVisible    = 100;
constexpr int kPrioStripBase  = 70;   // strip window base, falls off with distance from centre

// Priority for a strip page at `page`, given the page currently at the viewport centre
// (`centrePage`, or -1 when the model is empty/unknown). Peaks AT the centre and falls off
// symmetrically with distance, floored at 1 (never 0 — a request must still queue).
int stripDecodePriority(int page, int centrePage);

class ComicReaderCore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qulonglong generation READ generation NOTIFY entryChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY entryChanged)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY progressChanged)
    Q_PROPERTY(QString couplingState READ couplingState NOTIFY pairingChanged)
    Q_PROPERTY(QAbstractListModel* stripModel READ stripModel CONSTANT)
    // Settings-sheet readbacks. A chip can only mark itself active if it can read
    // the value back, so every setter below has one. memorySaver rides cacheChanged
    // (setMemorySaver already emits it — it IS a cache-budget change).
    Q_PROPERTY(bool memorySaver READ memorySaver NOTIFY cacheChanged)
    Q_PROPERTY(int stripWidthPct READ stripWidthPct NOTIFY stripLayoutChanged)
    Q_PROPERTY(int stripGap READ stripGap NOTIFY stripLayoutChanged)
    // Task 7's Image panel. `renderRevision` is the identity every scaled entry
    // is keyed on, exposed so QML can fold it into an image url — WITHOUT it a
    // brightness change would produce a byte-identical url and QML's own pixmap
    // cache would happily serve the pre-adjustment page forever.
    Q_PROPERTY(qulonglong renderRevision READ renderRevision NOTIFY renderProfileChanged)
public:
    explicit ComicReaderCore(QObject* parent = nullptr);
    ~ComicReaderCore() override;

    qulonglong generation() const { return m_generation; }
    int pageCount() const { return m_pages.size(); }
    int readyCount() const { return m_readyCount; }
    QString couplingState() const;
    QAbstractListModel* stripModel() const;
    bool memorySaver() const { return m_memorySaver; }
    int stripWidthPct() const { return m_portraitWidthPct; }
    int stripGap() const { return m_stripGap; }
    qulonglong renderRevision() const {
        return static_cast<qulonglong>(m_renderProfile.revision());
    }

    // Open an entry. `pages` is a QVariantList of {index,url,group}; each url must
    // be a local file:// that exists (a remote/undownloaded page is rejected with
    // PageError::MissingFile and the entry is not analyzed). `persisted` carries
    // spread overrides + coupling mode/phase + bookmarks + memory saver and
    // round-trips losslessly (see persistedState()).
    Q_INVOKABLE void openEntry(QString entryId, QVariantList pages, QString direction,
                               QVariantMap persisted);
    Q_INVOKABLE void closeEntry();

    Q_INVOKABLE QVariantMap pageInfo(int page) const;
    Q_INVOKABLE QVariantMap unitForPage(int page) const;
    // THE UNIT AS ONE THING (Task 4, overhaul plan 2026-07-28). unitForPage says
    // WHAT a unit is; this says whether that whole unit may be painted yet.
    //
    // A paired spread is one piece of paper. Painting the half that decoded first
    // and leaving the other half black is the defect this exists to kill
    // (Hemanth: "A paired spread appears as one complete unit. We never flash the
    // left page first and leave the right half black."). So the answer is the
    // unit's, never a page's:
    //   "waiting" — some member still has no pixels. Nothing of the unit paints.
    //   "error"   — a member has a TERMINAL error. Checked FIRST, because a unit
    //               whose partner can never arrive must stop waiting and say so.
    //   "ready"   — every member has pixels. Only now does the unit paint.
    // The map is unitForPage()'s (rightIndex/leftIndex/spread/coverAlone) plus
    // `state` and `errorCode` (the snake_case PageError wire code; "none" unless
    // the state is "error"). Empty — exactly like unitForPage() — when there is
    // no entry, so QML normalises both through one path.
    //
    // It READS the canonical pairing walk (m_units) and re-derives nothing: the
    // membership question was already answered by ComicReaderPairing::buildUnits
    // and this must never be a second, weaker opinion about it.
    //
    // NOT for Single Page. There the reader sees one page, and gating page 5 on
    // its pair partner would make a layout that shows one page wait for two. The
    // single surface asks pageInfo() per page instead.
    Q_INVOKABLE QVariantMap presentationForPage(int page) const;
    // RE-READ one broken page (Task 11, overhaul plan 2026-07-28). The reader's
    // way out of a dead spot: a damaged entry currently leaves an error card
    // that nothing can clear short of closing the book.
    //
    // THE ARCHIVE IS NEVER TOUCHED, and that is not a detail — it is the whole
    // contract. There is direct history: an earlier reader EXTRACTED CBZ pages
    // to loose folders and its ledger drifted out of sync with the real files,
    // which cost a recovery arc. The rule now is CBZ-only, read in place, never
    // mutated, so a retry may only re-READ. Nothing in this call, or anywhere
    // below it, opens a file for writing — and the core harness pins that with a
    // SHA-256 of the fixture archive taken before and after.
    //
    // What it actually does, and nothing more:
    //   * clears THIS page's error verdict (so pageInfo/presentationForPage stop
    //     reporting it and the placard can come down),
    //   * clears the decode coordinator's per-generation failure memo for THIS
    //     page — without that step request() returns early and the retry is a
    //     silent no-op, because the memo exists precisely to stop a failed page
    //     being re-decoded on every frame,
    //   * re-requests THAT page alone, at the CURRENT visible priority in the
    //     LIVE generation, so it lands ahead of the strip's background window
    //     and a result from a retired book can never paint into this one.
    // No pin set moves, no neighbour is queued, no unit is rebuilt: a retry is
    // one page's business.
    //
    // Out of range, or with no entry open, it does nothing at all.
    Q_INVOKABLE void retryPage(int page);
    Q_INVOKABLE void setSpreadOverride(int page, QString state);   // "spread"|"single"|"clear"
    Q_INVOKABLE void nudgeCoupling();                              // -> Manual + flipped phase
    // The other half of the settings sheet's Coupling row: hand the phase back to
    // the probe. Clears the manual pin AND re-runs the auto-coupling probe on the
    // open entry, so tapping Auto after a bad nudge actually re-decides — merely
    // clearing the flag would leave the hand-picked phase in place until the next
    // entry-open.
    Q_INVOKABLE void resetCoupling();
    Q_INVOKABLE void setVisible(QVariantList pageIndices);         // pin(+neighbors) + priorities
    // The reader's viewport, as a page range, and what the two image tiers are
    // therefore allowed to hold. Task 2 (overhaul plan 2026-07-28).
    //
    // This does NOT replace setVisible, and the two are not competing owners:
    //   setVisible    owns WHAT GETS DECODED and in what order — the pin set and
    //                 the latest-wins decode priority wave. It asks for work.
    //   requestRange  owns WHAT MAY STAY RESIDENT — the retention window on both
    //                 caches. It releases memory. It requests nothing, decodes
    //                 nothing, and changes no priority.
    // They meet at exactly one place: the pin set setVisible records is what
    // requestRange hands retainRange as the never-evict list, so a page on
    // screen survives even when the range moves off it. A surface may call
    // either, both, or neither; calling only setVisible is the pre-Task-2
    // behaviour (budget/LRU alone), which is what Single and Pair still do until
    // Task 8 wires the viewport up.
    //
    // Margins: decoded pages are kept ±2 around the visible run, comfortably
    // wider than the Long Strip decode window actually asks for (1.5 screens
    // either side, and a page is ≈2 screens at the default 78% portrait width),
    // so the sweep can never fight the prefetch. Scaled pages are kept from 1
    // behind to 2 ahead — tighter behind, because a scale is cheap to redo and
    // the reader is going forward — and that scaled window is also what sets the
    // scaled tier's capacity, so the range you ask for is literally the number
    // of scales kept.
    //
    // ⚠ COST, for whoever wires this up (Task 8): a call whose clamped range
    // DIFFERS from the last one walks both cache hashes under both mutexes and
    // frees QImages ON THE CALLING (GUI) THREAD, and the decoded walk contends
    // the same mutex every provider worker takes for every page fetch. A repeat
    // of the same range is a two-integer compare and returns immediately, so
    // driving this from a per-frame scroll signal is safe but wasteful at the
    // edges — debounce or send it on settle, and send PAGE indices, not pixels.
    Q_INVOKABLE void requestRange(int firstVisible, int lastVisible);
    Q_INVOKABLE void setStripViewport(double top, double height); // drive strip decode window
    Q_INVOKABLE void setStripViewportWidth(int width);            // Task 10: strip geometry width
    // Long Strip taste: portrait page width as a % of the viewport (clamped
    // 40..100) and the gap between pages in px (clamped 0..80). Applied in place
    // — the strip reflows without a model reset.
    //
    // Rescaling the column moves every page, so a viewport top that pointed at
    // page N now points somewhere else. Pass the caller's current viewport and
    // this ANCHORS: it remembers the page under the viewport centre plus the
    // fraction down that page, and RETURNS the new top to scroll to so the
    // reader keeps their place. A plain ratio-scale (what a viewport RESIZE
    // uses) cannot do this job — a portrait-width change leaves spreads
    // untouched, since they always span the full width, and a gap change shifts
    // tops by a per-page constant; neither scales the column uniformly.
    //
    // Pass a non-positive viewportHeight (the default) when there is nothing to
    // anchor to; the given viewportTop is returned unchanged.
    Q_INVOKABLE double setStripLayout(int portraitWidthPct, int gap,
                                      double viewportTop = 0.0,
                                      double viewportHeight = 0.0);
    // image://comicreader/<gen>/<page>?rev=N&tier=<tier>
    //
    // `tier` is "preview" (fast transform, first pixels on screen), "hq" (the
    // reader's real page) or "thumbnail" (capped to the filmstrip size); an
    // unrecognised value normalises to hq rather than emitting a url nothing can
    // parse. It rides on a DEFAULT ARGUMENT so the three existing QML call sites
    // keep working unchanged — Qt's meta-object system registers both arities,
    // which the core harness proves by asking QML itself rather than assuming.
    Q_INVOKABLE QString imageUrl(int page, QString tier = QLatin1String("hq")) const;
    // The strip Y a page starts at. The QML surface needs this to land a page-accurate seek —
    // switching into Strip, a go-to-page, a chapter jump — and it cannot compute it itself: the
    // ListView only realizes delegates near the viewport, so anything off-screen has no y to read.
    Q_INVOKABLE double stripPageTop(int page) const;
    // The page whose band holds the viewport's vertical CENTRE — the geometry-honest answer the
    // scrub bubble needs while hovering in Long Strip: pages have different heights, so a linear
    // pages*fraction estimate lies about where a fraction actually lands. -1 when there is no open
    // entry (never a crash on a degenerate/empty viewport). Mirrors stripPageTop's guard shape.
    Q_INVOKABLE int stripPageAtCenter(double top, double viewportHeight) const;
    // ...and how TALL that page's band is, so the surface can say how far down a page the viewport
    // centre sits without a realized delegate to measure. Same reason as stripPageTop: after a resume
    // or a layout switch the column has just been jumped thousands of pixels and the ListView has no
    // item there yet, so the drawn column cannot answer. setStripLayout already computes exactly this
    // fraction internally (pageAtCenter + pageTop + pageHeight) to anchor a relayout — this exposes
    // the one piece of it QML was missing. 0 for an out-of-range page, never a crash.
    Q_INVOKABLE double stripPageHeight(int page) const;
    Q_INVOKABLE void setMemorySaver(bool on);                     // cache 256 vs 512 MiB

    // ---- the Image panel's adjustments (Task 7, overhaul plan 2026-07-28) ----
    // ONE door in, ONE door out, and the map is VALIDATED on the way in: the
    // profile is persisted per series, so what arrives here may have been written
    // by a future version or hand-edited. normalizeRenderProfile clamps every
    // field (see ComicReaderRenderProfile.h for the ranges); an unknown key is
    // ignored and a missing one keeps its default.
    //
    // What a change costs, exactly:
    //   * a PIXEL-affecting change (brightness/contrast/gamma/rotation/autoCrop/
    //     quality) bumps renderRevision and CLEARS THE SCALED TIER. Every entry
    //     in it was computed under the previous revision, so all of them are
    //     stale by construction — clear() IS "invalidate the old revision", and
    //     it needs no new cache API to say so.
    //   * the DECODED tier is deliberately untouched. Nudging brightness must
    //     never send the reader back to disk for pixels it already has; that is
    //     the difference between a live control and a stutter.
    //   * nightFilter alone changes NOTHING here: it is a composited veil the
    //     shell paints, not a pixel operation, so it emits the signal and leaves
    //     the revision and the scaled tier exactly where they were.
    // The profile deliberately SURVIVES entry crossings (like the strip measure):
    // it is a property of how you want to read this series, not of one chapter.
    Q_INVOKABLE void setRenderProfile(QVariantMap profile);
    // The normalised profile — every key present, canonical types. This is the
    // shape the shell persists, so a stored record is already normalised and
    // re-reading it is a fixed point.
    Q_INVOKABLE QVariantMap renderProfile() const;
    // Bookmarks (0-based page). Toggling an in-range page inserts it (kept sorted)
    // or removes it if already present; out-of-range pages are ignored. bookmarks()
    // is the live view the HUD's scrub-bar ticks bind to (persistedState()'s
    // "bookmarks" entry is a point-in-time snapshot taken at load).
    Q_INVOKABLE void toggleBookmark(int page);
    Q_INVOKABLE QVariantList bookmarks() const;

    // Additive observability / persistence (read-only, no state mutation).
    Q_INVOKABLE QVariantMap persistedState() const;   // the round-trippable blob openEntry accepts
    Q_INVOKABLE QVariantList pinnedPages() const;      // the pin set from the last setVisible()
    Q_INVOKABLE qulonglong cacheBudget() const { return static_cast<qulonglong>(m_cacheBudget); }
    Q_INVOKABLE QVariantMap couplingProbeDebug() const;
    // What the image delivery path actually did — scaled-tier reuse, scales
    // computed, cancellations, stale drops, worst dispatch/response times, the
    // high-water resident ENTRY counts of both tiers, and the scaled tier's live
    // bytes / evictions / current derived ceiling. Reading never resets, so two
    // readers can never disagree about what happened; openEntry DOES reset,
    // because these describe the delivery of one volume. Task 12's performance
    // gate is the consumer.
    //
    // On reading maxScaledResident: it is a monotone maximum, so it pins to the
    // tier's ceiling once the ceiling is touched once and cannot distinguish a
    // held window from a thrashing one. For "is the tier earning its memory",
    // use the reuse ratio scaledHits / (scaledHits + scaleJobs) together with
    // scaledEvictions.
    Q_INVOKABLE QVariantMap deliveryMetrics() const;

    // Build a read-only provider wired to this core's two image tiers, live
    // generation, render revision and metrics. The caller (main.cpp ->
    // engine.addImageProvider) takes ownership. It reads the members directly —
    // the accessors below are not involved.
    ComicReaderProvider* createProvider();

    const std::atomic<quint64>* liveGenerationAtomic() const { return &m_liveGeneration; }

    // ---- Test seam (no production caller) -----------------------------------
    // The two image tiers, handed out so a harness can seed them and observe
    // what a sweep left behind. Non-const on purpose: the fixtures insert
    // through them. Nothing in production calls these — createProvider() takes
    // the members directly — so they grant a harness reach, not a second owner.
    ComicReaderPageCache* pageCache() { return &m_cache; }
    ComicReaderScaleCache* scaleCache() { return &m_scaleCache; }

    // Forwards straight to ComicReaderDecode::setWorkerHooksForTest, so a harness
    // driving THIS object can hold the two decode lanes busy and then observe the
    // ORDER queued work is dequeued — the only way to test decode priority from
    // the core's own surface (m_decode is private and rightly so). Owning thread
    // only, and set before any request. Mutates no core state.
    void setDecodeWorkerHooksForTest(std::function<void(quint64, int)> onEnter,
                                     std::function<void(quint64, int)> onExit);

signals:
    void entryChanged();
    void pageReady(int page);
    void pageFailed(int page, QString code);
    void pairingChanged();
    void progressChanged();
    void cacheChanged();
    void stripLayoutChanged();              // portrait width % / gap changed
    void stripCompensation(double delta);   // QML strip adds this to its scroll pos
    void bookmarksChanged();                // toggleBookmark() mutated the live bookmark set
    // retryPage() cleared this page's error and re-queued its decode. The
    // surfaces need it because clearing the verdict changes pageInfo()'s answer
    // and NOTHING else QML can see — no property moves, no other signal fires —
    // so without this the placard would sit there, still showing the old error,
    // until the retry happened to succeed. Folded into each surface's existing
    // failure-refresh dependency, so the card comes down and the quiet
    // placeholder takes its place while the re-read runs.
    void pageRetried(int page);
    // The Image panel changed something. Fired for ANY real change, including a
    // night-filter toggle that moved no revision — the panel reads its own state
    // back through renderProfile(), and the surfaces use this as their refresh
    // dependency exactly as they use pageReady().
    void renderProfileChanged();

private:
    void resetEntryState();
    void parsePages(const QVariantList& pages);
    void applyPersisted(const QVariantMap& persisted);
    void rebuildUnits();
    void flushStripCompensation();

    void onMetaReady(quint64 gen, const PageMeta& meta);
    void onPageReady(quint64 gen, int page);
    void onPageFailed(quint64 gen, int page, PageError error);

    // Auto-coupling probe.
    struct ProbePair { int a = -1; int b = -1; };   // consecutive; a < b
    void startAutoCouplingProbe();
    void onProbePageResolved(int page);
    void finalizeProbe();

    // ── owned engine units ──
    // m_delivery is declared before the caches so it is destroyed AFTER them:
    // both publish their resident high-water mark into it, and the sink must not
    // outlive the thing it points at.
    DeliveryMetrics m_delivery;
    ComicReaderPageCache m_cache;                 // declared before m_decode: decode captures &m_cache
    ComicReaderScaleCache m_scaleCache;           // the delivery-side scaled tier (Task 2)
    ComicReaderDecode* m_decode = nullptr;        // owned (child); this-thread affinity
    ComicReaderStripModel* m_strip = nullptr;     // owned (child)

    std::atomic<quint64> m_liveGeneration{0};     // read by the provider off-thread
    // The Image panel's live adjustments (Task 7). Every scaled entry is keyed on
    // this store's revision, so a pixel-affecting change invalidates the scaled
    // tier for free; the workers read the profile and that revision together,
    // under the store's own lock.
    RenderProfileStore m_renderProfile;

    // The last window requestRange actually swept, POST-clamp, so a repeat can
    // return without touching either cache. -1/-1 means "nothing swept yet" and
    // is restored on every entry open/close, so a new book always sweeps even if
    // it opens on the page numbers the last one closed at.
    int m_rangeFirst = -1;
    int m_rangeLast = -1;
    quint64 m_generation = 0;

    // ── current entry ──
    QString m_entryId;
    Direction m_direction = Direction::Ltr;
    QVector<PageMeta> m_pages;
    QVector<PairUnit> m_units;
    bool m_analyzable = false;

    // coupling
    CouplingMode m_couplingMode = CouplingMode::Auto;
    CouplingPhase m_couplingPhase = CouplingPhase::Normal;
    bool m_couplingResolved = false;
    double m_couplingConfidence = 0.0;

    // persisted-shape state
    QHash<int, bool> m_spreadOverrides;   // page -> forced spread(true)/single(false)
    // Spreads the DECODER learned last time this book was open. Without persisting them, pairing is
    // rebuilt from scratch on every open and only settles as decodes trickle in — so the same book
    // could pair differently between sessions, and the pages visibly re-shuffle while you read the
    // first few. Distinct from m_spreadOverrides, which is the user's explicit verdict and always
    // wins; this is only the machine's observation, replayed so the FIRST paint is already right.
    QVector<int> m_persistedDetectedSpreads;
    QVector<int> m_bookmarks;
    bool m_memorySaver = false;
    qint64 m_cacheBudget = 512LL * 1024 * 1024;

    // progress / provider revs
    int m_readyCount = 0;
    QSet<int> m_readyPages;
    QHash<int, int> m_pageRev;            // page -> imageUrl rev (bumped on (re)decode)

    // viewport / pinning
    QVector<int> m_lastPinned;
    // Climbs by a fixed step on every setVisible() so the newest wave of visible
    // pages outranks every earlier one still queued (see setVisible). Reset per
    // entry in resetEntryState().
    int m_visibleBoost = 0;
    double m_stripViewportTop = 0.0;
    double m_stripViewportHeight = 0.0;
    int m_stripViewportWidth = 1000;      // nominal; strip geometry is ratio-driven
    int m_portraitWidthPct = 78;
    int m_stripGap = 0;

    // probe state
    bool m_probeActive = false;
    QVector<ProbePair> m_probeNormalPairs;
    QVector<ProbePair> m_probeShiftedPairs;
    QSet<int> m_probePendingPages;
    bool m_probeCalledChoose = false;
    int m_probeNormalSamples = 0;
    int m_probeShiftedSamples = 0;
};

} // namespace comicreader
