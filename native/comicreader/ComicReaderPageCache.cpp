// native/comicreader/ComicReaderPageCache.cpp
#include "comicreader/ComicReaderPageCache.h"

#include <QMutexLocker>

#include <iterator> // std::prev(m_lru.end()) — only transitively available on MSVC otherwise

namespace comicreader {

ComicReaderPageCache::ComicReaderPageCache(qint64 budget)
    : m_budget(budget) {}

void ComicReaderPageCache::setBudget(qint64 bytes) {
    QMutexLocker lock(&m_mutex);
    m_budget = bytes;
    evictLocked();
}

void ComicReaderPageCache::insert(quint64 gen, int page, const QImage& img) {
    QMutexLocker lock(&m_mutex);
    const Key key(gen, page);

    // Replace-in-place: an existing entry at this key is fully removed first
    // so bytesUsed and the LRU list never double-count the key.
    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        m_bytesUsed -= existing->image.sizeInBytes();
        m_lru.erase(existing->lruIt);
        m_entries.erase(existing);
    }

    Entry entry;
    entry.image = img;
    entry.pinned = m_pinnedPages.value(gen).contains(page);
    m_lru.push_back(key);
    entry.lruIt = std::prev(m_lru.end());
    m_bytesUsed += img.sizeInBytes();
    m_entries.insert(key, entry);

    evictLocked();
}

std::optional<QImage> ComicReaderPageCache::get(quint64 gen, int page) {
    QMutexLocker lock(&m_mutex);
    const Key key(gen, page);
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return std::nullopt;

    // Refresh recency: move this key to the back (most-recently-used) of the
    // LRU list.
    m_lru.erase(it->lruIt);
    m_lru.push_back(key);
    it->lruIt = std::prev(m_lru.end());

    return it->image;
}

void ComicReaderPageCache::setPinned(quint64 gen, const QVector<int>& pages) {
    QMutexLocker lock(&m_mutex);
    m_pinnedPages.insert(gen, pages);

    // Sync the cached pinned flag on any already-live entries for this
    // generation; pages not listed become evictable again.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it.key().first == gen)
            it->pinned = pages.contains(it.key().second);
    }
}

void ComicReaderPageCache::clearGeneration(quint64 gen) {
    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (it.key().first == gen) {
            m_bytesUsed -= it->image.sizeInBytes();
            m_lru.erase(it->lruIt);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    m_pinnedPages.remove(gen);
}

qint64 ComicReaderPageCache::bytesUsed() const {
    QMutexLocker lock(&m_mutex);
    return m_bytesUsed;
}

void ComicReaderPageCache::evictLocked() {
    // Least-recently-used first (front of m_lru), skipping pinned entries.
    // Pinned pages never evict — if every remaining candidate is pinned, the
    // cache is allowed to exceed budget rather than blank a visible page.
    while (m_bytesUsed > m_budget) {
        bool evictedOne = false;
        for (auto it = m_lru.begin(); it != m_lru.end(); ++it) {
            const Key key = *it;
            auto entryIt = m_entries.find(key);
            if (entryIt == m_entries.end())
                continue; // shouldn't happen: list and map stay in sync
            if (entryIt->pinned)
                continue;

            m_bytesUsed -= entryIt->image.sizeInBytes();
            m_entries.erase(entryIt);
            m_lru.erase(it);
            evictedOne = true;
            break; // m_lru iterator invalidated; restart the scan
        }
        if (!evictedOne)
            break; // everything left is pinned — stop, even over budget
    }
}

} // namespace comicreader
