// native/comicreader/ComicReaderProvider.cpp
#include "comicreader/ComicReaderProvider.h"

#include "comicreader/ComicReaderImageResponse.h"

#include <QElapsedTimer>

namespace comicreader {
namespace {

// Two scaling lanes, mirroring the decode coordinator's own two-lane shape. The
// reader has at most a couple of pages genuinely in flight at once (the strip
// windows ~1.5 screens), so more lanes buy no responsiveness — they just hand
// more cores to scaling while decode and the GUI are competing for the same
// ones. Deliberately conservative; Task 2's metrics should tune it.
constexpr int kScaleLanes = 2;

// Queue order within the pool. A preview exists to put pixels on screen and be
// replaced, so it goes ahead of hq work; a filmstrip thumbnail is never what the
// reader is waiting on, so it yields to both. This biases the QUEUE only — two
// lanes mean an hq already running keeps running.
int queuePriorityFor(ScaleTier tier) {
    switch (tier) {
    case ScaleTier::Preview:   return 2;
    case ScaleTier::Hq:        return 1;
    case ScaleTier::Thumbnail: return 0;
    }
    return 1;
}

} // namespace

ComicReaderProvider::ComicReaderProvider(const DeliveryContext& ctx)
    : m_ctx(ctx) {
    m_pool.setMaxThreadCount(kScaleLanes);
}

QQuickImageResponse* ComicReaderProvider::requestImageResponse(const QString& id,
                                                               const QSize& requestedSize) {
    // Qt calls this on its image thread, and anything slow here stalls every
    // other page's request behind it — so how long the dispatch itself takes is
    // worth knowing, not assuming. It should be construction + a queue push.
    QElapsedTimer dispatch;
    dispatch.start();

    auto* response = new ComicReaderImageResponse(m_ctx, id, requestedSize);
    m_pool.start(response, queuePriorityFor(response->tier()));

    if (m_ctx.metrics)
        raiseMax(m_ctx.metrics->maxDispatchUs,
                 static_cast<quint64>(dispatch.nsecsElapsed() / 1000));
    return response;
}

} // namespace comicreader
