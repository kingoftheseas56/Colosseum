// native/comicreader/ComicReaderDecode.h
//
// Generation-safe decode coordinator for the Comic Reader (Agent 1, plan
// 2026-07-23, Task 4). This is the reader's concurrency crux: it guarantees a
// rapid entry switch (chapter A -> chapter B) can NEVER paint a stale page,
// because a decode result tagged with a superseded generation is dropped before
// it can ever be inserted into the cache or emitted.
//
// Threading model (the whole point):
//   * The coordinator has ONE owning thread (GUI thread in production, the
//     test's main/event thread in the harness). openGeneration(), request() and
//     onWorkerResult() all run on that thread; ALL coordinator state
//     (m_currentGen, m_pageByIndex, m_inflight) is touched ONLY there. No mutex
//     is needed because nothing else touches that state.
//   * request() enqueues a QRunnable on a QThreadPool capped at 2 threads. The
//     worker is PURE over its captured values (gen, page, localPath): it reads
//     the file bytes and decodes with QImageReader, computes an image-or-error,
//     and reports the result BACK to the owning thread via a QUEUED delivery
//     (QMetaObject::invokeMethod(..., Qt::QueuedConnection)). The worker NEVER
//     touches the cache, the inflight set, or any coordinator state directly.
//   * The worker posts TWO reports on that same queued path, in order: first a
//     HEADER-ONLY dimension hint (QImageReader::size() over the bytes it just
//     read — TB2 DecodeTask's dimensionsReady), then the finished decode. The
//     hint costs a few KB of parsing and lets the strip column snap to real
//     geometry, and pairing learn a spread, without waiting for pixels. It is
//     posted BEFORE the decode runs, so it lands even when the decode FAILS.
//   * onWorkerResult() and onWorkerDimensions() (owning thread) are the STALE
//     GUARD: if the report's gen is not the live generation, it is dropped
//     silently.
//
// Affinity invariant: CONSTRUCT AND DESTROY this object on its owning thread.
// The destructor waits for in-flight workers to finish; any report-back still
// queued after teardown is discarded by ~QObject (the receiver is gone), so a
// stale result can never run against a dead object.
#pragma once

#include "comicreader/ComicReaderTypes.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QVector>

#include <functional>

#include <QtGlobal>

namespace comicreader {

class ComicReaderPageCache;

class ComicReaderDecode final : public QObject {
    Q_OBJECT
public:
    explicit ComicReaderDecode(ComicReaderPageCache* cache, QObject* parent = nullptr);
    ~ComicReaderDecode() override;

    // Owning thread only. Becomes the live generation and cancels the prior one:
    // its still-running workers are dropped when they report back (the stale
    // guard), never before. Stores the page list, clears the inflight set, and
    // drops the previous generation's cached pages. `gen` is expected monotonic.
    void openGeneration(quint64 gen, const QVector<PageMeta>& pages);

    // Owning thread only. Decodes (currentGen, page) unless it is a no-op:
    // already inflight, already cached, or already recorded failed for this
    // generation (a permanently-failing page is not re-decoded until the next
    // openGeneration resets the failed set). A request for an already-cached
    // page intentionally bumps that page's LRU recency via the cache get.
    // `priority` is forwarded to QThreadPool::start (higher runs sooner): the
    // visible band starts at 100 and CLIMBS per visible wave (ComicReaderCore::
    // setVisible — a newer wave must out-rank an older one still queued, and a
    // queued page can never be re-prioritized because the inflight dedup above
    // drops the second request); the strip window is 70 and below; probe 10.
    void request(int page, int priority);

    // Owning thread only, but posted from a worker thread as the target of a
    // QMetaObject::invokeMethod queued call — it is never invoked directly by a
    // client. Public solely so the worker's queued lambda can reach it.
    void onWorkerResult(quint64 gen, int page, const QImage& image, PageError error);

    // Same contract as onWorkerResult (owning thread, posted from the worker as
    // a queued call, public only so the worker's lambda can reach it): the
    // worker's HEADER-ONLY size hint, published before the full decode runs.
    // Emits metaReady with decoded=false — real geometry, no pixels.
    void onWorkerDimensions(quint64 gen, int page, const QSize& dims);

    // ---- Test seam (empty/no-op in production) -----------------------------
    // Owning thread only, and set before any request(). Copied by value into
    // each worker at request() time and run ON THE WORKER THREAD: `onEnter` at
    // the very start (before any file I/O — the latch and high-water observation
    // point), `onExit` just before the queued report-back (the high-water
    // decrement point). They close over the TEST's own state, never the
    // coordinator's, so the worker stays pure.
    void setWorkerHooksForTest(std::function<void(quint64, int)> onEnter,
                               std::function<void(quint64, int)> onExit);

signals:
    // Fires TWICE per page: the header-only size hint (decoded=false), then the
    // finished decode (decoded=true). Both carry the real sourceSize + spread verdict.
    void metaReady(quint64 gen, comicreader::PageMeta meta);
    void pageReady(quint64 gen, int page);                     // decoded image now in the cache
    void pageFailed(quint64 gen, int page, comicreader::PageError error);

private:
    ComicReaderPageCache* m_cache = nullptr;   // not owned
    QThreadPool m_pool;
    quint64 m_currentGen = 0;
    QHash<int, PageMeta> m_pageByIndex;         // current generation's pages, by index
    QSet<int> m_inflight;                       // pages inflight for the current generation
    QSet<int> m_failed;                         // pages that failed under the current generation
    // MissingFile is NOT latched like the others (C6). A corrupt or unsupported page will fail the
    // same way every time, so re-decoding garbage heals nothing and the latch is correct. A MISSING
    // file is different: the usual cause is a page touched while the volume is still being written
    // to disk, and it appears moments later. Latching that showed "Page missing" for the life of
    // the generation - the only cure was closing and reopening the book. Instead the page cools
    // down and gets exactly one fresh attempt per cooldown window, which self-heals without a
    // per-frame stat storm against a file that genuinely is not there.
    QHash<int, qint64> m_missingRetryAt;        // page -> earliest ms-since-epoch to try again
    static constexpr qint64 kMissingRetryMs = 2000;
    std::function<void(quint64, int)> m_testOnWorkerEnter;
    std::function<void(quint64, int)> m_testOnWorkerExit;
};

} // namespace comicreader
