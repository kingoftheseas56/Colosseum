#pragma once

#include "core/HttpRouter.h"
#include "torrent_http/TorrentHttpSurface.h"

#include <memory>
#include <system_error>

namespace colosseum::server::integration {

struct TorrentStreamCallbacks final {
    std::function<void(QByteArray)> onChunk;
    std::function<void(std::error_code)> onError;
    std::function<void()> onEnd;
};

class TorrentStreamSession
{
public:
    virtual ~TorrentStreamSession() = default;
    virtual void start() = 0;
    virtual void destroy() = 0;
};

class TorrentStreamFactory
{
public:
    virtual ~TorrentStreamFactory() = default;
    virtual std::shared_ptr<TorrentStreamSession> open(
        const torrent_http::TorrentReadPlan &plan,
        const std::shared_ptr<server::CancellationToken> &cancellation,
        TorrentStreamCallbacks callbacks) = 0;
};

// Mounts the complete W07 surface as one fall-through router adapter. A
// route belongs to W07 only when TorrentHttpSurface::dispatch accepts it;
// other mounted feature families continue through HttpRouter in order.
void mountTorrentRoutes(server::HttpRouter &router,
                        torrent_http::TorrentHttpSurface &surface,
                        TorrentStreamFactory &streams);

} // namespace colosseum::server::integration
