// native/comicreader/ComicReaderProvider.cpp
#include "comicreader/ComicReaderProvider.h"

#include "comicreader/ComicReaderImageResponse.h"

namespace comicreader {
namespace {

// Two scaling lanes, mirroring the decode coordinator's own two-lane shape. The
// reader has at most a couple of pages genuinely in flight at once (the strip
// windows ~1.5 screens), so more lanes buy no responsiveness — they just hand
// more cores to scaling while decode and the GUI are competing for the same
// ones. Deliberately conservative; Task 2's metrics should tune it.
constexpr int kScaleLanes = 2;

} // namespace

ComicReaderProvider::ComicReaderProvider(ComicReaderPageCache* cache,
                                         const std::atomic<quint64>* liveGeneration)
    : m_cache(cache),
      m_liveGeneration(liveGeneration) {
    m_pool.setMaxThreadCount(kScaleLanes);
}

QQuickImageResponse* ComicReaderProvider::requestImageResponse(const QString& id,
                                                               const QSize& requestedSize) {
    auto* response = new ComicReaderImageResponse(m_cache, m_liveGeneration, id, requestedSize);
    m_pool.start(response);
    return response;
}

} // namespace comicreader
