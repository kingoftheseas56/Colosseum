// native/comicreader/ComicReaderScaleCache.cpp
#include "comicreader/ComicReaderScaleCache.h"

#include <QMutexLocker>

#include <iterator> // std::prev(m_lru.end()) — only transitively available on MSVC otherwise

namespace comicreader {

void DeliveryMetrics::reset() {
    sourceHits.store(0, std::memory_order_relaxed);
    scaledHits.store(0, std::memory_order_relaxed);
    scaleJobs.store(0, std::memory_order_relaxed);
    cancelledJobs.store(0, std::memory_order_relaxed);
    staleDrops.store(0, std::memory_order_relaxed);
    maxDispatchUs.store(0, std::memory_order_relaxed);
    maxResponseMs.store(0, std::memory_order_relaxed);
    maxDecodedResident.store(0, std::memory_order_relaxed);
    maxScaledResident.store(0, std::memory_order_relaxed);
    scaledBytesUsed.store(0, std::memory_order_relaxed);
    scaledEvictions.store(0, std::memory_order_relaxed);
}

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

ComicReaderScaleCache::ComicReaderScaleCache(qint64 hardCeiling, int capacity)
    : m_hardCeiling(hardCeiling), m_capacity(qMax(1, capacity)) {}

void ComicReaderScaleCache::setCapacity(int entries) {
    QMutexLocker lock(&m_mutex);
    m_capacity = qMax(1, entries);
    evictLocked();
    publishLocked();
}

int ComicReaderScaleCache::capacity() const {
    QMutexLocker lock(&m_mutex);
    return m_capacity;
}

void ComicReaderScaleCache::setHardCeiling(qint64 bytes) {
    QMutexLocker lock(&m_mutex);
    m_hardCeiling = bytes;
    evictLocked();
    publishLocked();
}

qint64 ComicReaderScaleCache::hardCeiling() const {
    QMutexLocker lock(&m_mutex);
    return m_hardCeiling;
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
    // A null image is not an answer. Seating one would cost a slot against the
    // capacity forever: every reader rejects it as a miss and re-inserts it.
    if (image.isNull())
        return;

    QMutexLocker lock(&m_mutex);

    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        m_bytesUsed -= existing->image.sizeInBytes();
        m_lru.erase(existing->lruIt);
        m_entries.erase(existing);
    }

    const qint64 bytes = image.sizeInBytes();

    Entry entry;
    entry.image = image;
    m_lru.push_back(key);
    entry.lruIt = std::prev(m_lru.end());
    m_bytesUsed += bytes;
    m_entries.insert(key, entry);

    evictLocked();
    publishLocked();
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
    publishLocked();
}

void ComicReaderScaleCache::clear() {
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    m_lru.clear();
    m_bytesUsed = 0;
    publishLocked();
}

int ComicReaderScaleCache::entryCount() const {
    QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

qint64 ComicReaderScaleCache::bytesUsed() const {
    QMutexLocker lock(&m_mutex);
    return m_bytesUsed;
}

void ComicReaderScaleCache::setMetricsSink(DeliveryMetrics* sink) {
    QMutexLocker lock(&m_mutex);
    m_metrics = sink;
}

void ComicReaderScaleCache::evictLocked() {
    // The capacity is what governs; the ceiling is the stop, and it can only
    // fire when the entries are big enough that the window would not have fit
    // (see the header). Least-recently-used first, and nothing here is pinnable:
    // a lost scale costs a rescale (the header has the honest version of what
    // that can cost), and holding one would mean holding scaled bytes past the
    // window the reader actually asked to keep.
    //
    // The last entry is never dropped — evicting the thing just inserted would
    // make insert() a no-op for an image bigger than the whole ceiling, and
    // serving it once is better than looping.
    while (m_entries.size() > 1
           && (m_entries.size() > m_capacity || m_bytesUsed > m_hardCeiling)) {
        const ScaleKey oldest = m_lru.front();
        auto it = m_entries.find(oldest);
        m_lru.pop_front();
        if (it == m_entries.end())
            continue;   // shouldn't happen: list and map stay in sync
        m_bytesUsed -= it->image.sizeInBytes();
        m_entries.erase(it);
        if (m_metrics)
            m_metrics->scaledEvictions.fetch_add(1, std::memory_order_relaxed);
    }
}

void ComicReaderScaleCache::publishLocked() {
    if (!m_metrics)
        return;
    raiseMax(m_metrics->maxScaledResident, static_cast<quint64>(m_entries.size()));
    // A level, not a maximum: it has to be able to fall, or it could not show a
    // sweep working.
    m_metrics->scaledBytesUsed.store(static_cast<quint64>(m_bytesUsed),
                                     std::memory_order_relaxed);
}

} // namespace comicreader
