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
// ── The bound, and why it is a HARD one ──────────────────────────────────────
// This is a SECOND image cache. The decoded tier already runs 512 MiB (256 in
// memory saver); a naive scaled tier doubles peak residency on a 1,452-page
// volume and turns a stutter bug into an out-of-memory bug. So the tier is
// bounded three ways, and the first two hold even if nobody ever calls
// retainRange (which is the live situation: Task 8 wires Long Strip's viewport
// to requestRange, and until then no surface drives it):
//
//   1. kMaxEntries resident entries, LRU beyond that. This is the bound that
//      actually bites: a scaled entry is viewport-sized, so with small pages the
//      byte budget would never trigger and the tier could hold hundreds. It is
//      also the number Task 12's gate reads (maxScaledResident), and the reason
//      that metric is a COUNT rather than a byte figure — a synthetic gate runs
//      tiny fixtures, where bytes would say nothing about whether the window
//      held. Raising this constant means moving that gate threshold with it.
//   2. A byte budget, as the ceiling for genuinely large scales: 64 MiB, an
//      EIGHTH of the decoded tier, so the scaled tier adds ~12% to peak
//      residency rather than doubling it. 32 MiB under memory saver, mirroring
//      the decoded tier's halving.
//   3. retainRange, once a surface drives it: everything outside the reader's
//      moving neighbourhood goes regardless of recency.
//
// There is no pinning here, deliberately. A pin exists so an on-screen page can
// never blank; a scaled entry that goes missing costs one rescale of a page
// that is, by definition, still in the decoded tier. The decoded tier is where
// blanking is prevented.
//
// ── Range eviction vs the byte budget: which wins ────────────────────────────
// They are two policies over one store, so the precedence is written down here
// rather than left to whichever ran last: THE BUDGET WINS. retainRange is a
// one-shot sweep that only ever removes MORE — being inside the range does not
// protect an entry from the standing budget/LRU eviction that runs on every
// insert. Range is a tightening hint; the budget is the memory ceiling, and a
// ceiling that a wide range could lift would not be a ceiling. The identical
// rule holds for ComicReaderPageCache::retainRange, where the pin set — not the
// range — is the only thing that outranks eviction.
//
// Thread-safe the same way ComicReaderPageCache is: one mutex over the map and
// the LRU list, nothing heavier than list/hash bookkeeping under it (QImage is
// implicitly shared, so storing or copying one while holding the lock is cheap).
// Provider workers insert and read from pool threads; the core sweeps from its
// own thread.
#pragma once

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
// about. Every field is an atomic because provider workers on the scale pool,
// decode workers on the decode pool, and the core's own thread all write here.
// Nothing in this struct changes what the reader DECIDES — it is observation
// only, which is what keeps the provider a read-only seam.
//
// Never reset by reading (ComicReaderCore::deliveryMetrics() only loads), so two
// readers can never disagree about what happened.
struct DeliveryMetrics {
    std::atomic<quint64> sourceHits{0};     // requests that read the full-resolution page
    std::atomic<quint64> scaledHits{0};     // requests served from this cache — the win
    std::atomic<quint64> scaleJobs{0};      // scales actually computed
    std::atomic<quint64> cancelledJobs{0};  // responses cancelled before publication
    std::atomic<quint64> staleDrops{0};     // requests for a retired generation
    std::atomic<quint64> maxDispatchUs{0};  // longest requestImageResponse() call
    std::atomic<quint64> maxResponseMs{0};  // longest construction -> served image
    std::atomic<quint64> maxDecodedResident{0}; // high-water ENTRY count, decoded tier
    std::atomic<quint64> maxScaledResident{0};  // high-water ENTRY count, scaled tier
};

// Raise `target` to `value` if `value` is larger. Lock-free, and safe when
// several workers race — compare_exchange_weak reloads `seen` with whatever the
// winner stored, so the loop re-tests against the new truth.
//
// Inline on purpose: ComicReaderPageCache publishes its own high-water mark
// through this, and it must not have to LINK against the scaled tier to do so
// (the decode harness builds the page cache with no scaled tier in sight).
inline void raiseMax(std::atomic<quint64>& target, quint64 value) {
    quint64 seen = target.load(std::memory_order_relaxed);
    while (value > seen
           && !target.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

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
    // See the header note: the entry cap is the bound that actually bites, and
    // the one Task 12's maxScaledResident gate reads.
    static constexpr int kMaxEntries = 8;
    static constexpr qint64 kBudgetNormal = 64LL * 1024 * 1024;
    static constexpr qint64 kBudgetSaver  = 32LL * 1024 * 1024;

    explicit ComicReaderScaleCache(qint64 budget = kBudgetNormal,
                                   int maxEntries = kMaxEntries);

    // 32 MiB under memory saver, 64 otherwise. Shrinking evicts immediately.
    void setBudget(qint64 bytes);

    // Marks the entry most-recently-used, like the decoded tier's get().
    std::optional<QImage> get(const ScaleKey& key);

    // Replaces any existing entry at `key`; bytes are never double counted. May
    // evict least-recently-used entries to stay inside BOTH ceilings.
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

    // Where to publish the resident-entry high-water mark (a
    // DeliveryMetrics::maxScaledResident). Observer pointer, may be null; set
    // once at construction time by the owner, before any worker can reach this.
    void setResidentHighWaterSink(std::atomic<quint64>* sink);

private:
    struct Entry {
        QImage image;
        std::list<ScaleKey>::iterator lruIt;
    };

    // Evicts least-recently-used entries until BOTH ceilings are satisfied.
    // Caller must hold m_mutex.
    void evictLocked();
    // Caller must hold m_mutex.
    void noteResidentLocked();

    mutable QMutex m_mutex;
    qint64 m_budget;
    int m_maxEntries;
    qint64 m_bytesUsed = 0;
    QHash<ScaleKey, Entry> m_entries;
    std::list<ScaleKey> m_lru;   // front = least recently used
    std::atomic<quint64>* m_residentSink = nullptr;
};

} // namespace comicreader
