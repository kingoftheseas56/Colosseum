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
    Q_INVOKABLE void setSpreadOverride(int page, QString state);   // "spread"|"single"|"clear"
    Q_INVOKABLE void nudgeCoupling();                              // -> Manual + flipped phase
    // The other half of the settings sheet's Coupling row: hand the phase back to
    // the probe. Clears the manual pin AND re-runs the auto-coupling probe on the
    // open entry, so tapping Auto after a bad nudge actually re-decides — merely
    // clearing the flag would leave the hand-picked phase in place until the next
    // entry-open.
    Q_INVOKABLE void resetCoupling();
    Q_INVOKABLE void setVisible(QVariantList pageIndices);         // pin(+neighbors) + priorities
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
    Q_INVOKABLE QString imageUrl(int page) const;                 // image://comicreader/<gen>/<page>?rev=N
    // The strip Y a page starts at. The QML surface needs this to land a page-accurate seek —
    // switching into Strip, a go-to-page, a chapter jump — and it cannot compute it itself: the
    // ListView only realizes delegates near the viewport, so anything off-screen has no y to read.
    Q_INVOKABLE double stripPageTop(int page) const;
    Q_INVOKABLE void setMemorySaver(bool on);                     // cache 256 vs 512 MiB
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

    // Build a read-only provider wired to this core's cache + live generation.
    // The caller (main.cpp -> engine.addImageProvider) takes ownership.
    ComicReaderProvider* createProvider();

    // For the provider factory in main.cpp; both outlive the QML engine.
    ComicReaderPageCache* pageCache() { return &m_cache; }
    const std::atomic<quint64>* liveGenerationAtomic() const { return &m_liveGeneration; }

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
    ComicReaderPageCache m_cache;                 // declared FIRST: decode captures &m_cache
    ComicReaderDecode* m_decode = nullptr;        // owned (child); this-thread affinity
    ComicReaderStripModel* m_strip = nullptr;     // owned (child)

    std::atomic<quint64> m_liveGeneration{0};     // read by the provider off-thread
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
    QVector<int> m_bookmarks;
    bool m_memorySaver = false;
    qint64 m_cacheBudget = 512LL * 1024 * 1024;

    // progress / provider revs
    int m_readyCount = 0;
    QSet<int> m_readyPages;
    QHash<int, int> m_pageRev;            // page -> imageUrl rev (bumped on (re)decode)

    // viewport / pinning
    QVector<int> m_lastPinned;
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
