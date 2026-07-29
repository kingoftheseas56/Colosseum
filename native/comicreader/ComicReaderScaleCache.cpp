// native/comicreader/ComicReaderScaleCache.cpp
#include "comicreader/ComicReaderScaleCache.h"

#include <QMutexLocker>

#include <iterator> // std::prev(m_lru.end()) — only transitively available on MSVC otherwise

namespace comicreader {

size_t qHash(const ScaleKey& key, size_t seed) noexcept {
    // Fold every field that participates in operator==. QtPrivate::QHashCombine
    // is the house way to chain these; ::qHash on each part keeps QSize's own
    // hashing rather than re-deriving it.
    seed = ::qHash(key.generation, seed);
    seed = ::qHash(key.page, seed);
    seed = ::qHash(key.targetPixels, seed);
    seed = ::qHash(key.dpr100, seed);
    seed = ::qHash(key.renderRevision, seed);
    seed = ::qHash(static_cast<int>(key.tier), seed);
    return seed;
}

ScaleTier tierFromString(QStringView tier) {
    if (tier == QLatin1String("preview"))
        return ScaleTier::Preview;
    if (tier == QLatin1String("thumbnail"))
        return ScaleTier::Thumbnail;
    return ScaleTier::Hq;
}

QString tierToString(ScaleTier tier) {
    switch (tier) {
    case ScaleTier::Preview:   return QStringLiteral("preview");
    case ScaleTier::Thumbnail: return QStringLiteral("thumbnail");
    case ScaleTier::Hq:        break;
    }
    return QStringLiteral("hq");
}

ComicReaderScaleCache::ComicReaderScaleCache(qint64 budget, int maxEntries)
    : m_budget(budget), m_maxEntries(qMax(1, maxEntries)) {}

void ComicReaderScaleCache::setBudget(qint64 bytes) {
    QMutexLocker lock(&m_mutex);
    m_budget = bytes;
    evictLocked();
}

std::optional<QImage> ComicReaderScaleCache::get(const ScaleKey& key) {
    QMutexLocker lock(&m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return std::nullopt;

    m_lru.erase(it->lruIt);
    m_lru.push_back(key);
    it->lruIt = std::prev(m_lru.end());
    return it->image;
}

void ComicReaderScaleCache::insert(const ScaleKey& key, const QImage& image) {
    QMutexLocker lock(&m_mutex);

    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        m_bytesUsed -= existing->image.sizeInBytes();
        m_lru.erase(existing->lruIt);
        m_entries.erase(existing);
    }

    Entry entry;
    entry.image = image;
    m_lru.push_back(key);
    entry.lruIt = std::prev(m_lru.end());
    m_bytesUsed += image.sizeInBytes();
    m_entries.insert(key, entry);

    evictLocked();
    noteResidentLocked();
}

void ComicReaderScaleCache::retainRange(quint64 gen, int first, int last) {
    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        const ScaleKey& key = it.key();
        if (key.generation == gen && (key.page < first || key.page > last)) {
            m_bytesUsed -= it->image.sizeInBytes();
            m_lru.erase(it->lruIt);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

void ComicReaderScaleCache::clear() {
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    m_lru.clear();
    m_bytesUsed = 0;
}

int ComicReaderScaleCache::entryCount() const {
    QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

qint64 ComicReaderScaleCache::bytesUsed() const {
    QMutexLocker lock(&m_mutex);
    return m_bytesUsed;
}

void ComicReaderScaleCache::setResidentHighWaterSink(std::atomic<quint64>* sink) {
    QMutexLocker lock(&m_mutex);
    m_residentSink = sink;
}

void ComicReaderScaleCache::evictLocked() {
    // Both ceilings, least-recently-used first. Nothing here is pinnable: a lost
    // scale costs one rescale of a page that is still in the decoded tier, so
    // there is no case where holding an entry beats holding the ceiling. The
    // last entry is never dropped — evicting the thing just inserted would make
    // insert() a no-op for an oversized image, and serving it once is better
    // than looping.
    while (m_entries.size() > 1
           && (m_entries.size() > m_maxEntries || m_bytesUsed > m_budget)) {
        const ScaleKey oldest = m_lru.front();
        auto it = m_entries.find(oldest);
        m_lru.pop_front();
        if (it == m_entries.end())
            continue;   // shouldn't happen: list and map stay in sync
        m_bytesUsed -= it->image.sizeInBytes();
        m_entries.erase(it);
    }
}

void ComicReaderScaleCache::noteResidentLocked() {
    if (m_residentSink)
        raiseMax(*m_residentSink, static_cast<quint64>(m_entries.size()));
}

} // namespace comicreader
