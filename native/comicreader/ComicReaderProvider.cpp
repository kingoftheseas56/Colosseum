// native/comicreader/ComicReaderProvider.cpp
#include "comicreader/ComicReaderProvider.h"

#include "comicreader/ComicReaderImageResponse.h"

namespace comicreader {

ComicReaderProvider::ComicReaderProvider(ComicReaderPageCache* cache,
                                         const std::atomic<quint64>* liveGeneration)
    : m_cache(cache),
      m_liveGeneration(liveGeneration) {}

QQuickImageResponse* ComicReaderProvider::requestImageResponse(const QString& id,
                                                               const QSize& requestedSize) {
    auto* response = new ComicReaderImageResponse(m_cache, m_liveGeneration, id, requestedSize);
    m_pool.start(response);
    return response;
}

} // namespace comicreader
