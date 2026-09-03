#include "TorrentHttpRouteAdapter.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace colosseum::server::integration {
namespace {

struct StreamState final : std::enable_shared_from_this<StreamState>
{
    server::HttpResponse response;
    std::shared_ptr<server::CancellationToken> cancellation;
    std::shared_ptr<torrent_http::StreamLease> lease;
    std::shared_ptr<TorrentStreamSession> session;
    std::atomic_bool terminal{false};
    std::atomic_bool headSent{false};
    std::mutex mutex;

    void destroy()
    {
        if (terminal.exchange(true, std::memory_order_acq_rel))
            return;
        std::shared_ptr<TorrentStreamSession> current;
        std::shared_ptr<torrent_http::StreamLease> currentLease;
        {
            std::lock_guard lock(mutex);
            current = std::move(session);
            currentLease = std::move(lease);
        }
        if (current)
            current->destroy();
        if (currentLease)
            currentLease->close();
    }

    void fail(std::error_code error)
    {
        if (terminal.exchange(true, std::memory_order_acq_rel))
            return;
        std::shared_ptr<TorrentStreamSession> current;
        std::shared_ptr<torrent_http::StreamLease> currentLease;
        {
            std::lock_guard lock(mutex);
            current = std::move(session);
            currentLease = std::move(lease);
        }
        if (current)
            current->destroy();
        if (currentLease)
            currentLease->close();
        if (!response.isFinished() && !headSent.load(std::memory_order_acquire)) {
            response.writeHead(502);
            response.end(QByteArray::fromStdString(error.message()));
        } else if (!response.isFinished()) {
            // The media head, including its fixed Content-Length, is already
            // on the wire. Never append an error string to that body.
            response.end();
        }
    }

    void end()
    {
        if (terminal.exchange(true, std::memory_order_acq_rel))
            return;
        std::shared_ptr<torrent_http::StreamLease> currentLease;
        {
            std::lock_guard lock(mutex);
            session.reset();
            currentLease = std::move(lease);
        }
        if (!response.isFinished())
            response.end();
        if (currentLease)
            currentLease->close();
    }

    ~StreamState()
    {
        destroy();
    }
};

torrent_http::TorrentHttpRequest convertRequest(const server::HttpRequest &request)
{
    torrent_http::TorrentHttpRequest converted;
    converted.method = request.method.toLatin1();
    converted.path = request.path;
    converted.query = request.query;
    converted.headers = request.headers;
    if (request.hasJsonBody && request.jsonBody.isObject())
        converted.body = request.jsonBody.object();
    return converted;
}

void writeReply(const torrent_http::TorrentHttpReply &reply,
                server::HttpResponse response,
                TorrentStreamFactory &streams,
                const std::shared_ptr<server::CancellationToken> &cancellation)
{
    if (!reply.readPlan) {
        response.writeHead(reply.status, reply.headers);
        response.end(reply.body);
        return;
    }

    auto state = std::make_shared<StreamState>();
    state->response = response;
    state->cancellation = cancellation;
    state->lease = reply.streamLease;
    const std::weak_ptr<StreamState> weak = state;

    TorrentStreamCallbacks callbacks;
    callbacks.onChunk = [state](QByteArray bytes) {
        if (state->terminal.load(std::memory_order_acquire))
            return;
        if (state->cancellation && state->cancellation->isCancelled()) {
            state->destroy();
            return;
        }
        state->response.write(bytes);
    };
    callbacks.onError = [state](std::error_code error) {
        state->fail(error);
    };
    callbacks.onEnd = [state] {
        state->end();
    };

    state->session = streams.open(*reply.readPlan, cancellation,
                                  std::move(callbacks));
    if (!state->session) {
        state->fail(std::make_error_code(std::errc::not_supported));
        return;
    }

    response.writeHead(reply.status, reply.headers);
    // Commit the response head before starting the potentially asynchronous
    // stream. This preserves a fixed Content-Length while allowing a client
    // to observe headers and disconnect before the first piece arrives.
    state->headSent = true;
    response.write(QByteArray{});

    if (cancellation) {
        cancellation->addCancelCallback([weak] {
            if (const auto state = weak.lock())
                state->destroy();
        });
    }
    if (cancellation && cancellation->isCancelled()) {
        state->destroy();
        return;
    }
    const auto session = state->session;
    session->start();
}

} // namespace

void mountTorrentRoutes(server::HttpRouter &router,
                        torrent_http::TorrentHttpSurface &surface,
                        TorrentStreamFactory &streams)
{
    router.all(QStringLiteral("/*"),
               [&surface, &streams](server::HttpRequest &request,
                                    server::HttpResponse response) {
        const auto converted = convertRequest(request);
        const auto cancellation = request.cancellation;
        return surface.dispatch(converted,
            [response, &streams, cancellation](torrent_http::TorrentHttpReply reply) mutable {
                writeReply(reply, response, streams, cancellation);
            });
    });
}

} // namespace colosseum::server::integration
