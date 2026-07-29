// native/comicreader/ComicReaderScaleCache.h
//
// The SCALED tier for the Comic Reader (Agent 1, overhaul plan 2026-07-28,
// Task 2), plus the delivery counters and the read-only bundle a provider and
// its responses serve from.
//
// What it is, plainly: the decoded page cache answers "what does this page look
// like"; this answers "what does this page look like AT THIS SIZE". Task 1 moved
// the SmoothTransformation downscale off the GUI thread but every provider hit
// still recomputed it from the full-resolution page — scroll back one page and
// the identical scale ran again. That recomputation is the remaining hot path
// behind the long-strip stutter, and this cache is what removes it.
//
// ── Why an app-side tier at all, when QML already has one ────────────────────
// QQuickPixmapCache is a scaled cache too: with `cache: true` an Image keyed by
// (url, sourceSize) keeps its result, which is why the surfaces bother to hold
// sourceSize still. It is not enough for two reasons, and they are worth stating
// because it is the first question any reader of this file will have:
//   1. Its unreferenced-entry budget is small and it is process-global — the
//      posters, covers and chrome art of the rest of Colosseum evict reader
//      pages out of it. We cannot size it for a comic reader without sizing it
//      for everything.
//   2. It only ever holds what an Image still points at or recently released.
//      A page scrolled two screens away is unreferenced immediately, so the
//      exact motion this task exists to fix — scroll away and come back — is
//      the motion its policy is worst at.
// This tier is reader-owned, reader-sized, and swept by the reader's own
// viewport, which is what makes retainRange meaningful at all.
//
// ── The bound: the WINDOW governs, bytes are the safety net ──────────────────
// This is a SECOND image cache, so it has to be bounded, but the first cut of
// this file got the bounding backwards and it is worth writing down why, because
// the mistake is easy to repeat.
//
// That version made a fixed 64 MiB byte budget the operative ceiling and an
// entry cap the "bound that actually bites", on the premise that a scaled entry
// is viewport-sized and therefore small. THE PREMISE WAS FALSE. Scaled entries
// are not viewport-sized — they are `srcCapW`-sized, and srcCapW is a SCREEN-
// and-zoom constant that deliberately does not follow the window:
//   - ComicReaderStripSurface.qml: max(1100, min(2048, screen rounded to 256)).
//     On a 1920-wide screen that is 2048, and its comment explains that tying it
//     to the window re-decoded the whole visible column on every fullscreen
//     toggle (Hemanth, 2026-07-26: "incredibly rough"). It is fixed by design.
//   - ComicReaderDoubleSurface.qml: 1400 / 2048 / 2800 by zoom.
// A 2048-wide colour page of ordinary manga proportions is ~24 MB. So 64 MiB
// held TWO entries, not eight — and one entry under memory saver, or for a
// zoomed pair. The tier disabled itself precisely where it was needed most: a
// three-page viewport missed on every scroll step, and the two halves of a
// spread evicted each other. Worse, which bound bit flipped with PIXEL FORMAT
// (an 8-bit grayscale scan is ~6 MB, where the entry cap did bite), so the same
// code behaved differently on different books. No fixture caught it because
// every fixture uses 4 MiB or 24 KB images, regimes where the entry cap really
// does bite.
//
// So the bounding is now the other way round, and there is one rule:
//
//   THE RETAINED WINDOW GOVERNS RESIDENCY. The capacity — how many scaled
//   entries the reader wants to keep — is the ONE operating bound, and the
//   reader sets it from the page range it actually asked to retain (setCapacity,
//   driven by ComicReaderCore::requestRange). The byte figure is demoted to a
//   pure safety ceiling that must not bite in normal reading.
//
// Two bounds, then, and it is worth being exact about when each one can fire,
// because "derive the budget from entry size × window" was tried here first and
// is a longer way of writing the capacity cap. Let S be the largest entry the
// tier holds. Residency obeys bytesUsed <= count * S <= capacity * S, so ANY
// byte budget at or above capacity * S is arithmetically incapable of evicting
// anything the capacity cap has not already evicted. A derived budget is
// therefore not a second bound at all — it is decoration. What remains is a
// fixed ceiling BELOW that product, which is exactly what a safety net is:
//
//   count > capacity      -> the normal, governing bound (the window)
//   bytes > hardCeiling   -> the stop, and it only fires when entries are big
//                            enough that the window would not have fit in RAM
//
// The ceiling is sized so the retained PAGE window always fits at one scale per
// page even in the worst realistic case, with the per-page replacement
// allowance (see kDefaultCapacity) on top wherever there is room:
//   - strip, 3 pages visible -> 6-page window, colour at srcCapW 2048
//     (2048x3072 ARGB32 = 24 MiB): 144 MiB held, 240 MiB with the allowance,
//     both under the 256 MiB ceiling — the window is held whole.
//   - double page, zoom >= 180% -> 5-page window at srcCapW 2800
//     (2800x4200 = 44.9 MiB): 224 MiB, under the ceiling. The allowance is what
//     gets trimmed here, never the window.
// Memory saver (128 MiB) DOES make the ceiling the operative bound, and that is
// the point of the setting rather than a regression: it holds 5 of a 6-page
// colour strip window, and exactly 2 entries of a zoomed spread — which is both
// halves of what is on screen, so the failure the first cut had (the two halves
// of a spread evicting each other) does not come back even at the tightest
// setting.
//
// Every eviction is counted (DeliveryMetrics::scaledEvictions) and the live byte
// total is published (scaledBytesUsed), so Task 12 settles the sizing by
// measurement instead of by arithmetic in a header comment — including this one.
//
// There is no pinning here, deliberately. A pin exists so an on-screen page can
// never blank; a scaled entry that goes missing costs a rescale, and the decoded
// tier is where blanking is actually prevented. Note the honest version of that
// claim: the rescale needs the full-resolution page, and the decoded tier's own
// budget may ALREADY have evicted it (its eviction skips pinned entries only, not
// in-window ones — the two windows overlap but overlap is not co-residency). When
// that happens the miss costs a full re-decode from disk, not a rescale. It is
// still not worth pinning here — a pin would hold scaled bytes hostage to fix a
// decoded-tier residency problem — but "by definition still decoded" would be a
// false guarantee, so it is not made.
//
// Thread-safe the same way ComicReaderPageCache is: one mutex over the map and
// the LRU list, nothing heavier than list/hash bookkeeping under it (QImage is
// implicitly shared, so storing or copying one while holding the lock is cheap).
#pragma once

#include "comicreader/ComicReaderTypes.h"   // raiseMax — the shared lock-free high-water helper

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QStringView>

#include <atomic>
#include <list>
#include <optional>

#include <QtGlobal>

namespace comicreader {

class ComicReaderPageCache;
class ComicReaderScaleCache;

// Which shape of answer a request wants. Carried in the image URL's query
// (`&tier=`), produced by ComicReaderCore::imageUrl().
enum class ScaleTier {
    Preview,    // fast transform: first pixels on screen, replaced by Hq
    Hq,         // the reader's real page (the default, and every existing caller)
    Thumbnail,  // capped to the filmstrip's size, whatever the caller asked for
};

// The url grammar's one source of truth, so the side that WRITES `&tier=`
// (ComicReaderCore::imageUrl) and the side that READS it
// (ComicReaderImageResponse) can never drift into disagreeing about a spelling.
// Anything unrecognised — including absent — is Hq: the default, and what every
// pre-Task-2 url means.
ScaleTier tierFromString(QStringView tier);
QString tierToString(ScaleTier tier);

// Identity of one scaled image. Every field here changes the PIXELS, which is
// the only test for whether a field belongs: same key must mean "serving the
// cached copy is indistinguishable from redoing the work".
//
// `targetPixels` carries the EFFECTIVE target width and a zero height: the scale
// is width-driven (Task 1's rule, unchanged — a page is fitted to a column and
// its height follows), and the width is post-clamp, so a thumbnail asked for at
// 900px and one asked for unsized both land on the same capped entry instead of
// two identical ones. Task 7's render profile, which can rotate, is what will
// give the height a job.
//
// `tier` is an ADDITION to the plan's five-field ScaleKey, and it is load
// bearing: Task 8 stacks a preview under an hq image at the SAME size in the
// same delegate, so without it whichever landed first would be served to both —
// the reader would keep the fast, aliased preview and never see hq arrive. It
// sits last with a default so the plan's own five-field construction still means
// what it says (hq).
//
// `renderRevision` is Task 7's seam. The core publishes a monotonic value that
// is CONSTANT today (there is no render profile yet); this cache simply respects
// it, so bumping it there invalidates every scale here for free.
struct ScaleKey {
    quint64 generation = 0;
    int page = -1;
    QSize targetPixels;
    int dpr100 = 100;
    quint64 renderRevision = 0;
    ScaleTier tier = ScaleTier::Hq;

    bool operator==(const ScaleKey& other) const {
        return generation == other.generation && page == other.page
               && targetPixels == other.targetPixels && dpr100 == other.dpr100
               && renderRevision == other.renderRevision && tier == other.tier;
    }
    bool operator!=(const ScaleKey& other) const { return !(*this == other); }
};

size_t qHash(const ScaleKey& key, size_t seed = 0) noexcept;

// What the delivery path did, counted so it can be gated rather than argued
// about. Every field is an atomic because provider workers on the scale pool and
// the core's own thread both write here. Nothing in this struct changes what the
// reader DECIDES — it is observation only, which is what keeps the provider a
// read-only seam.
//
// deliveryMetrics() never resets by reading, so two readers can never disagree
// about what happened. ComicReaderCore::openEntry DOES reset the whole struct:
// these describe the delivery of ONE volume, and a cold-open outlier would
// otherwise poison maxResponseMs for the life of the process.
struct DeliveryMetrics {
    std::atomic<quint64> sourceHits{0};     // requests that read the full-resolution page
    std::atomic<quint64> scaledHits{0};     // requests served from this cache — the win
    std::atomic<quint64> scaleJobs{0};      // scales actually computed
    std::atomic<quint64> cancelledJobs{0};  // responses cancelled before publication
    std::atomic<quint64> staleDrops{0};     // requests for a retired generation
    std::atomic<quint64> maxDispatchUs{0};  // longest requestImageResponse() call
    std::atomic<quint64> maxResponseMs{0};  // longest construction -> served image
    std::atomic<quint64> maxDecodedResident{0}; // high-water ENTRY count, decoded tier
    // High-water ENTRY count of the scaled tier. Read it for what it is: a
    // monotone maximum that PINS TO THE CEILING once the ceiling is touched
    // once, so a held window and a thrashing one look identical here. It is kept
    // because the plan names it, and it does answer "did the tier ever fill".
    // The question Task 12 actually wants — is the tier EARNING its memory — is
    // the reuse ratio scaledHits / (scaledHits + scaleJobs), which needs no new
    // field. Thrash shows up there as a ratio that collapses while scaleJobs
    // climbs, and in scaledEvictions below.
    std::atomic<quint64> maxScaledResident{0};
    // Live byte total of the scaled tier (a level, not a maximum) and the number
    // of entries its ceilings have dropped. Together these are what say whether
    // the bound is sized right: evictions climbing during ordinary scrolling
    // means the window does not fit, which is the failure the first cut of this
    // file shipped with and nothing could see.
    std::atomic<quint64> scaledBytesUsed{0};
    std::atomic<quint64> scaledEvictions{0};

    // Back to construction state, field by field, relaxed. It is NOT atomic as a
    // whole and its one caller (ComicReaderCore::openEntry) cannot promise the
    // pool is idle — a worker from the volume that just closed may still be
    // finishing. So the honest contract is: a concurrent worker's increment may
    // land either side of the reset, which costs at most a stray count or two
    // from a book nobody is reading. That is acceptable for observation-only
    // counters and is the whole reason not to reset from a reader.
    void reset();
};

// The read-only stores a provider and its responses serve from. Bundled because
// they always travel together, and because five bare pointers as positional
// constructor arguments would include two of the SAME type (the two atomics) —
// swapping those would compile and silently key every scale on the wrong thing.
//
// Every member is an observer pointer: ComicReaderCore owns all of them and
// outlives the QML engine that owns the provider.
struct DeliveryContext {
    ComicReaderPageCache* pageCache = nullptr;
    ComicReaderScaleCache* scaleCache = nullptr;
    const std::atomic<quint64>* liveGeneration = nullptr;
    // Null means revision 0 — a harness that does not care about Task 7's seam
    // need not invent an atomic to say so.
    const std::atomic<quint64>* renderRevision = nullptr;
    DeliveryMetrics* metrics = nullptr;
};

class ComicReaderScaleCache {
public:
    // Entries, not pages: ONE page can hold more than one scaled image at a
    // time. That is not hypothetical — the double-page surface changes srcCapW
    // with zoom (1400/2048/2800), so a zoom step leaves the old width resident
    // beside the new one, and Task 8 stacks a preview under its hq at the same
    // width. Both are the same shape, "the live scale plus the one replacing
    // it", which is why ComicReaderCore sizes the capacity at two entries per
    // retained page (kScaledEntriesPerPage) and lets LRU sort out the rest.
    //
    // This default is what production runs on until Task 8 wires the viewport to
    // requestRange: an ordinary Long Strip window (a 1-2 page view makes
    // [first-1, last+2] a 4-page window) at two scales each.
    static constexpr int kDefaultCapacity = 8;

    // The stop, not the operating budget — see the header note for the sizing
    // arithmetic and for exactly when it can fire. Half the decoded tier's
    // 512 MiB, halving with it under memory saver.
    static constexpr qint64 kHardCeilingNormal = 256LL * 1024 * 1024;
    static constexpr qint64 kHardCeilingSaver  = 128LL * 1024 * 1024;

    explicit ComicReaderScaleCache(qint64 hardCeiling = kHardCeilingNormal,
                                   int capacity = kDefaultCapacity);

    // The GOVERNING bound: how many scaled entries the reader wants resident.
    // ComicReaderCore::requestRange drives this from the page window it retains,
    // which is what makes the window — not a byte guess — decide residency.
    // Shrinking evicts immediately. Clamped to at least 1.
    void setCapacity(int entries);
    int capacity() const;

    // The safety net: 128 MiB under memory saver, 256 otherwise. Sized to sit
    // ABOVE a full retained window of the largest pages the surfaces can ask
    // for, so in normal reading it never fires; it exists to stop a pathological
    // page, and under memory saver to be the tighter of the two bounds on
    // purpose.
    void setHardCeiling(qint64 bytes);
    qint64 hardCeiling() const;

    // Marks the entry most-recently-used, like the decoded tier's get().
    std::optional<QImage> get(const ScaleKey& key);

    // Replaces any existing entry at `key`; bytes are never double counted. A
    // null image is ignored — it would seat a 0-byte entry that every reader
    // rejects and then re-inserts on the next hit, spending a slot forever.
    // May evict least-recently-used entries to stay inside both ceilings.
    //
    // An insert may land just after a retainRange sweep (or just after
    // ComicReaderCore::openEntry bumped the generation and cleared the tier) and
    // seat an entry outside the new window. That is benign and deliberate: the
    // entry is keyed by generation, so it can only ever be served to the request
    // that computed it, and the next sweep removes it.
    void insert(const ScaleKey& key, const QImage& image);

    // Drop every entry for `gen` whose page falls outside [first, last]. Other
    // generations are untouched — a retired volume is cleared wholesale by
    // clear(), not swept page by page.
    void retainRange(quint64 gen, int first, int last);

    // Everything, every generation. What entry-open and entry-close use: only
    // one generation is ever live, so a new volume makes the whole tier dead.
    void clear();

    int entryCount() const;
    qint64 bytesUsed() const;

    // Where to publish maxScaledResident / scaledBytesUsed / scaledEvictions.
    // Observer pointer, may be null; set once at construction time by the owner,
    // before any worker can reach this.
    void setMetricsSink(DeliveryMetrics* sink);

private:
    struct Entry {
        QImage image;
        std::list<ScaleKey>::iterator lruIt;
    };

    // Evicts least-recently-used entries until BOTH bounds are satisfied.
    // Caller must hold m_mutex.
    void evictLocked();
    // Caller must hold m_mutex.
    void publishLocked();

    mutable QMutex m_mutex;
    qint64 m_hardCeiling;
    int m_capacity;
    qint64 m_bytesUsed = 0;
    QHash<ScaleKey, Entry> m_entries;
    std::list<ScaleKey> m_lru;   // front = least recently used
    DeliveryMetrics* m_metrics = nullptr;
};

} // namespace comicreader
