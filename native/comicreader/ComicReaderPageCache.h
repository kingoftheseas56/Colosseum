// native/comicreader/ComicReaderPageCache.h
//
// Pinned, budgeted LRU page cache for the Comic Reader (Agent 1, plan
// 2026-07-23). Later tasks feed decoded pages in here (Task 4's decode
// coordinator) and pin the visible/neighbor pages so the on-screen frame never
// blanks under memory pressure (Task 7's backend). Standalone: depends only on
// Qt Core/Gui (QImage) plus std — no other comicreader/ unit.
//
// Keyed by (generation, page). `generation` is a monotonic counter the caller
// bumps each time a new entry (chapter/volume) opens, so a stale generation's
// pages can be dropped wholesale via clearGeneration without touching the live
// one. Thread-safe: one mutex guards the map + LRU list; nothing heavier than
// list/hash bookkeeping ever runs while it's held (QImage is implicitly shared,
// so storing/copying one under lock is cheap — no decode or scaling lives here).
#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPair>
#include <QVector>

#include <list>
#include <optional>

#include <QtGlobal>

namespace comicreader {

class ComicReaderPageCache {
public:
    explicit ComicReaderPageCache(qint64 budget = 512LL * 1024 * 1024);

    // 256 MiB when memory saver is on, 512 MiB otherwise (the house numbers).
    // Shrinking evicts immediately, unpinned entries first, down to the new
    // budget where legal (an all-pinned generation may still exceed it).
    void setBudget(qint64 bytes);

    // Replaces any existing entry at (gen, page) — bytesUsed is never double
    // counted for the same key. May trigger eviction of unpinned entries if
    // the budget would be exceeded. Contract: `img` must be a decoded,
    // non-null QImage — a null image stores as a live 0-byte entry (the
    // decode coordinator only ever publishes successful decodes here).
    void insert(quint64 gen, int page, const QImage& img);

    // Returns the cached image (if present) and marks it most-recently-used.
    std::optional<QImage> get(quint64 gen, int page);

    // Replaces the pinned set for `gen` wholesale: pages listed here never
    // evict; any page previously pinned for this generation but absent from
    // `pages` becomes evictable again. Does not require the pages to already
    // be cached (a pin recorded ahead of insert still takes effect on insert).
    void setPinned(quint64 gen, const QVector<int>& pages);

    // Drops every entry for `gen` (and its pinned set). Other generations are
    // untouched.
    void clearGeneration(quint64 gen);

    // Sum of QImage::sizeInBytes() over every live entry.
    qint64 bytesUsed() const;

private:
    using Key = QPair<quint64, int>; // (generation, page); QPair<->QHash via QtCore's qHash

    struct Entry {
        QImage image;
        bool pinned = false;
        std::list<Key>::iterator lruIt;
    };

    // Evicts least-recently-used UNPINNED entries (front of m_lru first) until
    // bytesUsed fits the budget, or until only pinned entries remain — pinned
    // pages never evict even if that leaves the cache over budget. Caller must
    // hold m_mutex.
    void evictLocked();

    mutable QMutex m_mutex;
    qint64 m_budget;
    qint64 m_bytesUsed = 0;
    QHash<Key, Entry> m_entries;
    QHash<quint64, QVector<int>> m_pinnedPages; // gen -> pinned page indices
    std::list<Key> m_lru; // front = least recently used, back = most recently used
};

} // namespace comicreader
