// native/comicreader/ComicReaderStripModel.h
//
// Pure geometry model for the Comic Reader (Agent 1, plan 2026-07-23) Long
// Strip surface (Task 10). A QAbstractListModel exposing one row per page
// with its vertical layout (top offset, display width/height) — no image
// decode, no cache, no I/O, no QML registration here. The backend (Task 7)
// owns one instance, feeds it PageMeta via rebuild()/updatePage() as pages
// decode, and drives its own decode-window/pixel-cache decisions from
// window()/pageAtCenter().
//
// Geometry law — ported from Tankoban 2's ScrollStripCanvas
// (rebuildYOffsets/targetPageWidth/firstVisiblePage) and
// TankobanQTGroundWork's comic_reader.py on_page_loaded (see the .cpp for the
// exact line-by-line mapping):
//   - A page whose real size has never been learned displays at an ESTIMATED
//     1600x2400 (portrait) source size.
//   - Once a page decodes with a real sourceSize, that real size is LOCKED IN
//     for this model's lifetime (until the next rebuild()). A later
//     updatePage() that reports decoded=false again (a page-cache eviction,
//     not a fresh decode) must NOT revert the page back to the estimate — TB2
//     has no "undecode" event either; ScrollStripCanvas's per-page dimension
//     slots are sticky, and only its SEPARATE scaled-pixmap cache evicts
//     (evictScaledOutsideZone). `ReadyRole` still tracks the live decoded flag
//     so QML can show a placeholder frame while pixels are evicted, even
//     though the page's box in the strip keeps its already-known height.
//   - Spread pages (effective spread = spreadOverride if set, else
//     detectedSpread) span the full viewport width; portrait pages span
//     portraitWidthPct% of it.
//   - top(i) is the running sum of every earlier page's (displayHeight +
//     gap); contentHeight is that same sum through the last page, WITHOUT a
//     trailing gap after it (matches ScrollStripCanvas::totalHeight()).
#pragma once

#include "comicreader/ComicReaderTypes.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QSize>
#include <QVector>

namespace comicreader {

class ComicReaderStripModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PageIndexRole = Qt::UserRole + 1,
        TopRole,
        DisplayWidthRole,
        DisplayHeightRole,
        ReadyRole,
        ErrorCodeRole,
    };

    struct Options {
        int viewportWidth = 0;
        int portraitWidthPct = 78;
        int gap = 0;
    };

    explicit ComicReaderStripModel(QObject* parent = nullptr);

    // Full reset: replaces every page and its geometry from scratch. Any page
    // already carrying decoded=true plus a valid sourceSize locks in its real
    // size immediately (as if updatePage() had just delivered it). Clears any
    // pending anti-jump compensation left over from before this call.
    //
    // Precondition: pages[i].index == i for every i. updatePage() keys
    // m_entries by meta.index while window()/pageAtCenter()/pageTop() key by
    // array position — the two only name the same slot for a dense, in-order
    // feed. Debug builds Q_ASSERT this; since Q_ASSERT compiles out under
    // NDEBUG/Release, a violation ALSO hard-early-outs to an empty model
    // (rowCount() == 0) rather than silently mis-keying rows in Release too.
    void rebuild(const QVector<PageMeta>& pages, const Options& opt);

    // A single page's meta changed (its real size arrived, an error was set,
    // or its cached pixels were evicted). Recomputes that one page's display
    // size; a page whose real size was already locked in keeps that size
    // regardless of what `meta.decoded` says now — see the class comment.
    void updatePage(const PageMeta& meta);

    // Viewport width changed (window resize / fullscreen). Rescales every page's
    // geometry to the new width IN PLACE — same rows, same order, only sizes
    // change — and emits dataChanged, NOT a model reset. A reset tears down and
    // recreates every ListView delegate (a visible blink) and zeroes the bound
    // ListView's contentY (a scroll jump); dataChanged keeps the delegates and the
    // scroll, so the reader reflows smoothly the way the rest of the app does.
    // Locked-in real page sizes are preserved; only the fit-to-width changes.
    // No-op if width is unchanged or non-positive.
    void setViewportWidth(int width);

    // Portrait width % and inter-page gap changed (the settings sheet's LONG
    // STRIP section). Same in-place contract as setViewportWidth: recompute
    // every page's fit and re-sum the column, then dataChanged — never a model
    // reset, so the bound ListView keeps its delegates and its scroll. Locked-in
    // real page sizes are preserved. Callers pass already-clamped values; this
    // no-ops if neither number actually changed.
    void setLayout(int portraitWidthPct, int gap);

    // Pages whose [top, bottom] band intersects [top - margin, top + vpHeight
    // + margin], margin = marginScreens * vpHeight (Task 7 uses 1.5). Returns
    // indices in ascending, contiguous order.
    QVector<int> window(double top, double vpHeight, double marginScreens) const;

    // Binary search: the page whose [top, bottom) band contains top + vpHeight/2.
    // Returns -1 on an empty model (no phantom page 0 for a caller to act on).
    int pageAtCenter(double top, double vpHeight) const;

    double pageTop(int page) const;
    double pageHeight(int page) const;
    double contentHeight() const;

    // Anti-jump compensation — ACCUMULATE AND CLEAR. Ports QTGW's
    // comic_reader.py on_page_loaded gate (`widget.pos().y() < scrollbar.
    // value()`), generalized for Task 7's decode coordinator delivering
    // several finished pages in a burst before it queries once: every
    // updatePage() call whose height changed appends (oldTop, delta) to an
    // internal accumulator instead of only remembering the latest one. Call
    // this after a batch of updatePage() calls, passing the viewport top the
    // caller is CURRENTLY scrolled to (before applying the returned delta).
    // Returns the SUM of delta for every accumulated entry whose oldTop sits
    // strictly above viewportTop (a page at/below the viewport needs no
    // compensation — nothing above the reader shifted), then CLEARS the
    // accumulator unconditionally — including entries that did NOT
    // contribute (below-fold ones), since their scroll relevance was already
    // decided by this call. Add the returned delta to the caller's own
    // scroll position. Order-independent: a page's own height change never
    // moves its own top, so summing per-page deltas is correct regardless of
    // the order updatePage() was called in.
    double takePendingCompensation(double viewportTop);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Entry {
        PageMeta meta;
        bool sizeKnown = false;    // sticky: true once a real decoded size has been locked in
        QSize knownSize;           // sticky real source size (valid only if sizeKnown)
        bool knownSpread = false;  // sticky effective spread, captured at lock-in time
        double displayWidth = 0.0;
        double displayHeight = 0.0;
        double top = 0.0;
    };

    // One page's contribution to the next takePendingCompensation() call.
    struct PendingCompensation {
        double oldTop;
        double delta;
    };

    void recomputeGeometry(Entry& e) const;
    void recomputeTops();
    int firstBandStart(double y) const;

    QVector<Entry> m_entries;
    Options m_options;
    double m_contentHeight = 0.0;

    // Anti-jump accumulator: one entry per updatePage() call that changed a
    // page's height, since the last takePendingCompensation() drained it.
    QVector<PendingCompensation> m_pendingCompensations;
};

} // namespace comicreader
