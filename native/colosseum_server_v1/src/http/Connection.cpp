#include <QtCore/QByteArray>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include "server1/http/HttpContract.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>

namespace server1::http::connection_detail {

namespace {

constexpr std::size_t kDefaultMaxBodyBytes = 3U * 1024U * 1024U;
constexpr std::size_t kDefaultMaxQueuedBytes = 1U * 1024U * 1024U;
constexpr qint64 kMaxWriteChunkBytes = 16U * 1024U;

const char *reasonPhrase(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 206:
        return "Partial Content";
    case 416:
        return "Range Not Satisfiable";
    case 301:
        return "Moved Permanently";
    case 307:
        return "Temporary Redirect";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 413:
        return "Payload Too Large";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 505:
        return "HTTP Version Not Supported";
    case 503:
        return "Service Unavailable";
    default:
        return "Response";
    }
}

QByteArray makeResponseHeaders(int status, bool keepAlive, bool closeAfter, void *response,
                               std::size_t contentLength, bool chunked, bool head = false)
{
    QByteArray wire;
    wire += "HTTP/1.1 ";
    wire += QByteArray::number(status);
    wire += ' ';
    wire += reasonPhrase(status);
    wire += "\r\n";
    if (response != nullptr) {
        for (std::size_t index = 0; index < server1_http_response_header_count(response); ++index) {
            const QByteArray name=server1_http_response_header_name_at(response,index);
            if(name.compare("Content-Length",Qt::CaseInsensitive)==0
               || name.compare("Transfer-Encoding",Qt::CaseInsensitive)==0
               || name.compare("Connection",Qt::CaseInsensitive)==0)continue;
            wire += name;
            wire += ": ";
            wire += server1_http_response_header_value_at(response, index);
            wire += "\r\n";
        }
    }
    if (chunked) {
        wire += "Transfer-Encoding: chunked\r\n";
    } else {
        wire += "Content-Length: ";
        const QByteArray explicitLength=response ? server1_http_response_header(response,"Content-Length") : "";
        bool valid=false;
        explicitLength.toULongLong(&valid);
        valid = valid && !explicitLength.isEmpty() && std::all_of(explicitLength.begin(),explicitLength.end(),[](char c){return c >= '0' && c <= '9';});
        wire += head && valid ? explicitLength : QByteArray::number(static_cast<qulonglong>(contentLength));
        wire += "\r\n";
    }
    wire += "Connection: ";
    wire += (!keepAlive || closeAfter) ? "close\r\n" : "keep-alive\r\n";
    wire += "\r\n";
    return wire;
}

QByteArray makeResponse(int status, const std::string &body, const std::string &method,
                        bool keepAlive, bool closeAfter, void *response)
{
    QByteArray wire = makeResponseHeaders(status, keepAlive, closeAfter, response, body.size(),
                                          false, method == "HEAD");
    if (method != "HEAD")
        wire += QByteArray::fromStdString(body);
    return wire;
}

} // namespace

class HttpConnection;

class HttpServer final : public QObject {
public:
    HttpServer(void *router, std::size_t maxQueuedBytes);
    ~HttpServer() override;

    int listen(unsigned short port);
    void stop();
    void requestDestruction();
    void setDrainPaused(bool paused);
    [[nodiscard]] unsigned short port() const;
    [[nodiscard]] const std::string &error() const;
    [[nodiscard]] std::size_t queuedBytes() const;
    [[nodiscard]] std::size_t pendingBytes() const;
    [[nodiscard]] std::size_t maxQueuedBytes() const;
    [[nodiscard]] std::size_t activeConnections() const;
    [[nodiscard]] std::size_t inputBacklogBytes() const;
    [[nodiscard]] void *router() const;
    [[nodiscard]] bool drainPaused() const;
    void addConnection(HttpConnection *connection);
    void removeConnection(HttpConnection *connection);

private:
    void acceptConnections();

    QTcpServer listener_;
    void *router_ = nullptr;
    std::size_t maxQueuedBytes_ = kDefaultMaxQueuedBytes;
    bool drainPaused_ = false;
    bool destructionRequested_ = false;
    std::string error_;
    std::deque<HttpConnection *> connections_;

    friend class HttpConnection;
};

class HttpConnection final : public QObject {
public:
    HttpConnection(HttpServer *server, QTcpSocket *socket)
        : QObject(server)
        , server_(server)
        , socket_(socket)
        , parser_(server1_http_parser_create(kDefaultMaxBodyBytes))
    {
        socket_->setParent(this);
        socket_->setReadBufferSize(static_cast<qint64>(kDefaultMaxBodyBytes + 64U * 1024U));
        QObject::connect(socket_, &QTcpSocket::readyRead, this, [this] { readAvailable(); });
        QObject::connect(socket_, &QTcpSocket::bytesWritten, this,
                         [this](qint64 bytes) { acknowledgeWritten(bytes); });
        QObject::connect(socket_, &QTcpSocket::disconnected, this,
                         [this] { handleDisconnected(); });
        QObject::connect(socket_, &QAbstractSocket::errorOccurred, this,
                         [this](QAbstractSocket::SocketError) { handleSocketError(); });
    }

    ~HttpConnection() override
    {
        releaseConnection();
    }

    void setDrainPaused(bool paused)
    {
        drainPaused_ = paused;
        if (!drainPaused_)
            drain();
    }

    [[nodiscard]] bool closed() const { return closed_; }
    [[nodiscard]] std::size_t queuedBytes() const { return pendingBytes(); }
    [[nodiscard]] std::size_t pendingBytes() const
    {
        const std::size_t socketBytes = socket_ == nullptr || socket_->bytesToWrite() <= 0
            ? 0
            : static_cast<std::size_t>(socket_->bytesToWrite());
        return applicationQueuedBytes_ > std::numeric_limits<std::size_t>::max() - socketBytes
            ? std::numeric_limits<std::size_t>::max()
            : applicationQueuedBytes_ + socketBytes;
    }
    [[nodiscard]] std::size_t inputBacklogBytes() const
    {
        return pendingInput_.size()
            + (socket_ ? static_cast<std::size_t>(std::max<qint64>(0,socket_->bytesAvailable())) : 0);
    }
    [[nodiscard]] bool released() const { return released_; }

    void closeFromServer()
    {
        if (released_)
            return;
        failureHandled_ = true;
        closed_ = true;
        const QPointer<HttpConnection> guard(this);
        clearQueue();
        if (!guard) return;
        if (socket_ != nullptr) socket_->abort();
        if (!guard) return;
        releaseConnection();
    }

private:
    struct PendingWrite final {
        QByteArray data;
        qsizetype accepted = 0;
        qsizetype delivered = 0;
        bool closeAfter = false;
        void *response = nullptr;
        std::uint64_t identity=0;
        bool streamBlocked=false;
        bool streaming = false;
        bool chunked = false;
        bool streamFinished = false;
        std::size_t streamBytes = 0;
    };

    void readAvailable()
    {
        if (closed_ || socket_ == nullptr || responsePending_)
            return;
        // Leave later requests in Qt's bounded read buffer while the current
        // response is pending. Reading into another buffer defeats backpressure.
        const QByteArray bytes = socket_->readAll();
        feed(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }

    void feed(const char *data, std::size_t size)
    {
        if (closed_ || parser_ == nullptr)
            return;
        const int result = server1_http_parser_feed(parser_, data, size);
        if (result == 0)
            return;
        if (result == 2) {
            const int status = server1_http_parser_error_status(parser_);
            const std::string message = server1_http_parser_error(parser_);
            if (parser_ != nullptr) {
                server1_http_parser_destroy(parser_);
                parser_ = nullptr;
            }
            enqueue(makeResponse(status, message, "GET", false, true, nullptr), true);
            return;
        }
        dispatchParsedRequest();
    }

    void dispatchParsedRequest()
    {
        if (closed_ || parser_ == nullptr || responsePending_)
            return;
        const std::string method = server1_http_parser_method(parser_);
        const bool keepAlive = server1_http_parser_keep_alive(parser_);
        const std::string remaining(server1_http_parser_remaining(parser_),
                                    server1_http_parser_remaining_size(parser_));
        const QPointer<HttpConnection> dispatchGuard(this);
        void *response = server1_http_router_dispatch_parsed(server_->router(), parser_);
        if(!dispatchGuard || closed_ || released_){server1_http_response_destroy(response);return;}
        server1_http_parser_destroy(parser_);
        parser_ = server1_http_parser_create(kDefaultMaxBodyBytes);
        pendingInput_ = remaining;
        responsePending_ = true;
        if(server1_http_response_is_deferred(response)){
            deferredResponse_=response;
            deferredMethod_=method;
            deferredKeepAlive_=keepAlive;
            auto *context=new QPointer<HttpConnection>(this);
            if(!server1_http_response_bind_deferred(response,+[](void *opaque){
                const auto connection=*static_cast<QPointer<HttpConnection> *>(opaque);
                if(connection)QMetaObject::invokeMethod(connection,[connection]{
                    if(!connection || connection->closed_ || !connection->deferredResponse_)return;
                    auto *response=connection->deferredResponse_;connection->deferredResponse_=nullptr;
                    connection->serializeResponse(response,connection->deferredMethod_,connection->deferredKeepAlive_);
                },Qt::QueuedConnection);
            },context,+[](void *opaque){delete static_cast<QPointer<HttpConnection> *>(opaque);})){delete context;handleSocketError();}
            return;
        }
        serializeResponse(response,method,keepAlive);
    }

    void serializeResponse(void *response,const std::string &method,bool keepAlive)
    {
        const int status = server1_http_response_status(response);
        const bool responseClose = server1_http_response_close(response) != 0;
        const bool streaming = server1_http_response_has_stream(response) != 0;
        QByteArray wire;
        bool chunked = false;
        if (streaming) {
            const std::size_t contentLength =
                server1_http_response_stream_content_length(response);
            chunked = contentLength == SERVER1_HTTP_UNKNOWN_CONTENT_LENGTH;
            wire = makeResponseHeaders(status, keepAlive, responseClose, response, contentLength,
                                       chunked, method == "HEAD");
        } else {
            const std::string responseBody(server1_http_response_body(response),
                                           server1_http_response_body_size(response));
            wire = makeResponse(status, responseBody, method, keepAlive, responseClose, response);
        }

        if (streaming)
            enqueueStream(wire, !keepAlive || responseClose, response, chunked, method == "HEAD");
        else
            enqueue(wire, !keepAlive || responseClose, response);
    }

    void enqueue(const QByteArray &data, bool closeAfter, void *response = nullptr)
    {
        if (closed_) {
            server1_http_response_destroy(response);
            return;
        }
        const std::size_t responseBytes = static_cast<std::size_t>(data.size());
        const std::size_t pendingBytes = server_->pendingBytes();
        if (responseBytes > server_->maxQueuedBytes()
            || pendingBytes > server_->maxQueuedBytes() - responseBytes) {
            const QPointer<HttpConnection> guard(this);
            server1_http_response_destroy(response);
            if (guard) handleSocketError();
            return;
        }
        queue_.push_back(PendingWrite{data, 0, 0, closeAfter, response});
        applicationQueuedBytes_ += responseBytes;
        drain();
    }

    void enqueueStream(const QByteArray &headers, bool closeAfter, void *response, bool chunked,
                       bool suppressBody)
    {
        if (closed_) {
            server1_http_response_destroy(response);
            return;
        }
        const std::size_t responseBytes = static_cast<std::size_t>(headers.size());
        const std::size_t pendingBytes = server_->pendingBytes();
        if (response == nullptr || responseBytes > server_->maxQueuedBytes()
            || pendingBytes > server_->maxQueuedBytes() - responseBytes) {
            const QPointer<HttpConnection> guard(this);
            server1_http_response_destroy(response);
            if (guard) handleSocketError();
            return;
        }
        PendingWrite pending;
        pending.data = headers;
        pending.closeAfter = closeAfter;
        pending.response = response;
        pending.identity=++nextResponseIdentity_;
        pending.streaming = true;
        pending.chunked = chunked;
        pending.streamFinished = suppressBody;
        const auto identity=pending.identity;
        if(server1_http_response_stream_can_wait(response)) {
            struct WakeContext {QPointer<HttpConnection> connection;std::uint64_t identity;};
            auto *context=new WakeContext{this,identity};
            const int bound=server1_http_response_bind_wake(response,+[](void *opaque){
                const auto &context=*static_cast<WakeContext *>(opaque);
                if(context.connection)context.connection->scheduleStreamWake(context.identity);
            },context,+[](void *opaque){delete static_cast<WakeContext *>(opaque);});
            if(!bound){delete context;const QPointer<HttpConnection> guard(this);server1_http_response_destroy(response);if(guard)handleSocketError();return;}
        }
        queue_.push_back(std::move(pending));
        applicationQueuedBytes_ += responseBytes;
        drain();
    }

    void scheduleStreamWake(std::uint64_t identity)
    {
        if(closed_ || released_ || wakeQueuedIdentity_==identity)return;
        wakeQueuedIdentity_=identity;
        QMetaObject::invokeMethod(this,[this,identity]{
            if(wakeQueuedIdentity_==identity)wakeQueuedIdentity_=0;
            if(closed_ || released_ || queue_.empty() || queue_.front().identity!=identity)return;
            queue_.front().streamBlocked=false;
            drain();
        },Qt::QueuedConnection);
    }

    bool fillStream(PendingWrite &pending)
    {
        if (!pending.streaming || pending.streamFinished)
            return true;
        if (pending.response == nullptr) {
            handleSocketError();
            return false;
        }

        const std::size_t pendingBytes = server_->pendingBytes();
        if (pendingBytes >= server_->maxQueuedBytes())
            return false;
        const std::size_t available = server_->maxQueuedBytes() - pendingBytes;
        std::size_t capacity = std::min<std::size_t>(
            static_cast<std::size_t>(kMaxWriteChunkBytes), available);
        if (pending.chunked) {
            constexpr std::size_t kChunkOverhead = 24;
            if (available <= kChunkOverhead)
                return false;
            capacity = std::min(capacity, available - kChunkOverhead);
        }
        if (capacity == 0)
            return false;

        QByteArray buffer(static_cast<qsizetype>(capacity), '\0');
        const auto identity=pending.identity;
        const bool canWait=server1_http_response_stream_can_wait(pending.response)!=0;
        const QPointer<HttpConnection> guard(this);
        const std::ptrdiff_t read = server1_http_response_stream_read(
            pending.response, buffer.data(), capacity);
        if(!guard || closed_ || released_ || queue_.empty() || queue_.front().identity!=identity)return false;
        if(canWait && read==SERVER1_HTTP_WOULD_BLOCK){pending.streamBlocked=true;return false;}
        if (read < 0 || static_cast<std::size_t>(read) > capacity) {
            handleSocketError();
            return false;
        }

        pending.data.clear();
        pending.accepted = 0;
        pending.delivered = 0;
        if (pending.chunked) {
            if (read == 0) {
                pending.data = "0\r\n\r\n";
                pending.streamFinished = true;
            } else {
                const QByteArray length = QByteArray::number(read, 16);
                pending.data.reserve(length.size() + static_cast<qsizetype>(read) + 4);
                pending.data += length;
                pending.data += "\r\n";
                pending.data.append(buffer.constData(), static_cast<qsizetype>(read));
                pending.data += "\r\n";
            }
        } else {
            const std::size_t contentLength =
                server1_http_response_stream_content_length(pending.response);
            if (read == 0) {
                if (pending.streamBytes != contentLength) {
                    handleSocketError();
                    return false;
                }
                pending.streamFinished = true;
            } else {
                const std::size_t readBytes = static_cast<std::size_t>(read);
                if (readBytes > contentLength
                    || pending.streamBytes > contentLength - readBytes) {
                    handleSocketError();
                    return false;
                }
                pending.streamBytes += readBytes;
                pending.data = buffer.left(static_cast<qsizetype>(read));
                if (pending.streamBytes == contentLength)
                    pending.streamFinished = true;
            }
        }

        applicationQueuedBytes_ += static_cast<std::size_t>(pending.data.size());
        return true;
    }

    static void destroyPendingResponse(PendingWrite &pending)
    {
        if (pending.response != nullptr) {
            void *response = pending.response;
            pending.response = nullptr;
            server1_http_response_destroy(response);
        }
    }

    void drain()
    {
        if (closed_ || drainPaused_ || socket_ == nullptr)
            return;
        if (queue_.empty()) {
            resumeInput();
            return;
        }

        PendingWrite &pending = queue_.front();
        if (pending.accepted < pending.data.size()) {
            const qint64 remaining = pending.data.size() - pending.accepted;
            const qint64 chunk = std::min(remaining, kMaxWriteChunkBytes);
            const qint64 written = socket_->write(pending.data.constData() + pending.accepted, chunk);
            if (written <= 0) {
                handleSocketError();
                return;
            }
            pending.accepted += static_cast<qsizetype>(written);
            applicationQueuedBytes_ -= static_cast<std::size_t>(written);
            if (socket_->bytesToWrite() == 0) {
                markSocketDrained();
                if (pending.accepted < pending.data.size())
                    scheduleDrain();
                else
                    completeFrontIfDrained();
            }
            return;
        }

        completeFrontIfDrained();
    }

    void acknowledgeWritten(qint64 bytes)
    {
        if (closed_ || released_ || bytes <= 0)
            return;
        qint64 remaining = bytes;
        for (PendingWrite &pending : queue_) {
            const qint64 awaiting = pending.accepted - pending.delivered;
            if (awaiting <= 0)
                continue;
            const qint64 acknowledged = std::min(awaiting, remaining);
            pending.delivered += static_cast<qsizetype>(acknowledged);
            remaining -= acknowledged;
            if (remaining == 0)
                break;
        }
        drain();
    }

    void markSocketDrained()
    {
        for (PendingWrite &pending : queue_)
            pending.delivered = pending.accepted;
    }

    void scheduleDrain()
    {
        if (closed_ || released_)
            return;
        QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection);
    }

    void completeFrontIfDrained()
    {
        if (queue_.empty()) {
            resumeInput();
            return;
        }
        PendingWrite &pending = queue_.front();
        if (pending.accepted != pending.data.size() || pending.delivered != pending.data.size()
            || socket_ == nullptr || socket_->bytesToWrite() != 0)
            return;

        if (pending.streaming && !pending.streamFinished) {
            if(pending.streamBlocked)return;
            const QPointer<HttpConnection> guard(this);
            const bool filled = fillStream(pending);
            if (!guard) return;
            if (!filled) {
                if (!closed_ && !released_ && !queue_.empty() && !queue_.front().streamBlocked) scheduleDrain();
                return;
            }
            if (!pending.data.isEmpty()) {
                drain();
                return;
            }
        }

        const bool closeAfter = pending.closeAfter;
        void *response = pending.response;
        pending.response = nullptr;
        queue_.pop_front();
        const QPointer<HttpConnection> guard(this);
        server1_http_response_destroy(response);
        if (!guard || closed_ || released_) return;
        if (closeAfter) {
            responsePending_ = false;
            closed_ = true;
            pendingInput_.clear();
            socket_->disconnectFromHost();
            if (!guard) return;
            if (socket_->state() == QAbstractSocket::UnconnectedState)
                handleDisconnected();
            return;
        }
        responsePending_ = false;
        resumeInput();
    }

    void resumeInput()
    {
        if (closed_ || responsePending_)
            return;
        if (!pendingInput_.empty()) {
            const std::string input = std::move(pendingInput_);
            pendingInput_.clear();
            feed(input.data(), input.size());
            if (closed_ || responsePending_)
                return;
        }
        // readyRead is not repeated for bytes already buffered while paused.
        if (socket_ && socket_->bytesAvailable() > 0) readAvailable();
    }

    void handleDisconnected()
    {
        if (released_)
            return;
        closed_ = true;
        const QPointer<HttpConnection> guard(this);
        clearQueue();
        if (guard) releaseConnection();
    }

    void handleSocketError()
    {
        if (released_ || failureHandled_)
            return;
        failureHandled_ = true;
        closed_ = true;
        const QPointer<HttpConnection> guard(this);
        clearQueue();
        if (!guard) return;
        if (socket_ != nullptr) socket_->abort();
        if (!guard) return;
        releaseConnection();
    }

    void clearQueue()
    {
        void *deferred=deferredResponse_;
        deferredResponse_=nullptr;
        std::deque<PendingWrite> retired;
        retired.swap(queue_);
        applicationQueuedBytes_ = 0;
        pendingInput_.clear();
        responsePending_ = false;
        server1_http_response_destroy(deferred);
        for (PendingWrite &pending : retired)
            destroyPendingResponse(pending);
    }

    void releaseConnection()
    {
        if (released_)
            return;
        released_ = true;
        const QPointer<HttpConnection> guard(this);
        clearQueue();
        if (!guard) return;
        if (parser_ != nullptr) {
            server1_http_parser_destroy(parser_);
            parser_ = nullptr;
        }
        server_->removeConnection(this);
        if (!closed_)
            closed_ = true;
        deleteLater();
    }

    HttpServer *server_ = nullptr;
    QTcpSocket *socket_ = nullptr;
    void *parser_ = nullptr;
    std::deque<PendingWrite> queue_;
    void *deferredResponse_=nullptr;
    std::string deferredMethod_;
    bool deferredKeepAlive_=false;
    std::size_t applicationQueuedBytes_ = 0;
    std::string pendingInput_;
    std::uint64_t nextResponseIdentity_=0;
    std::uint64_t wakeQueuedIdentity_=0;
    bool responsePending_ = false;
    bool drainPaused_ = false;
    bool closed_ = false;
    bool released_ = false;
    bool failureHandled_ = false;
};

HttpServer::HttpServer(void *router, std::size_t maxQueuedBytes)
    : QObject(nullptr)
    , router_(router)
    , maxQueuedBytes_(maxQueuedBytes == 0 ? kDefaultMaxQueuedBytes : maxQueuedBytes)
{
    QObject::connect(&listener_, &QTcpServer::newConnection, this,
                     [this] { acceptConnections(); });
}

HttpServer::~HttpServer()
{
    stop();
    for (HttpConnection *connection : connections_)
        delete connection;
    connections_.clear();
}

int HttpServer::listen(unsigned short port)
{
    error_.clear();
    if (listener_.isListening())
        listener_.close();
    if (!listener_.listen(QHostAddress::LocalHost, port)) {
        error_ = listener_.errorString().toStdString();
        return 0;
    }
    return 1;
}

void HttpServer::requestDestruction()
{
    if (destructionRequested_) return;
    destructionRequested_ = true;
    stop();
    deleteLater();
}

void HttpServer::stop()
{
    listener_.close();
    const QPointer<HttpServer> guard(this);
    std::deque<QPointer<HttpConnection>> active;
    for (auto *connection : connections_) active.emplace_back(connection);
    for (const auto &connection : active) {
        if (connection) connection->closeFromServer();
        if (!guard) return;
    }
}

void HttpServer::setDrainPaused(bool paused)
{
    drainPaused_ = paused;
    const QPointer<HttpServer> guard(this);
    std::deque<QPointer<HttpConnection>> active;
    for (auto *connection : connections_) active.emplace_back(connection);
    for (const auto &connection : active) {
        if (connection) connection->setDrainPaused(paused);
        if (!guard) return;
    }
}

unsigned short HttpServer::port() const
{
    return listener_.serverPort();
}

const std::string &HttpServer::error() const
{
    return error_;
}

std::size_t HttpServer::queuedBytes() const
{
    return pendingBytes();
}

std::size_t HttpServer::pendingBytes() const
{
    std::size_t total = 0;
    for (const HttpConnection *connection : connections_) {
        if (connection == nullptr)
            continue;
        const std::size_t pending = connection->pendingBytes();
        if (pending > std::numeric_limits<std::size_t>::max() - total)
            return std::numeric_limits<std::size_t>::max();
        total += pending;
    }
    return total;
}

std::size_t HttpServer::inputBacklogBytes() const
{
    std::size_t total = 0;
    for (const auto *connection : connections_)
        if (connection && !connection->released()) total += connection->inputBacklogBytes();
    return total;
}

std::size_t HttpServer::maxQueuedBytes() const
{
    return maxQueuedBytes_;
}

std::size_t HttpServer::activeConnections() const
{
    std::size_t total = 0;
    for (const HttpConnection *connection : connections_) {
        if (connection != nullptr && !connection->released())
            ++total;
    }
    return total;
}

void *HttpServer::router() const
{
    return router_;
}

bool HttpServer::drainPaused() const
{
    return drainPaused_;
}

void HttpServer::addConnection(HttpConnection *connection)
{
    connections_.push_back(connection);
    connection->setDrainPaused(drainPaused_);
    QObject::connect(connection, &QObject::destroyed, this, [this, connection] {
        removeConnection(connection);
    });
}

void HttpServer::removeConnection(HttpConnection *connection)
{
    connections_.erase(std::remove(connections_.begin(), connections_.end(), connection),
                       connections_.end());
}

void HttpServer::acceptConnections()
{
    while (listener_.hasPendingConnections()) {
        QTcpSocket *socket = listener_.nextPendingConnection();
        if (socket == nullptr)
            continue;
        addConnection(new HttpConnection(this, socket));
    }
}

} // namespace server1::http::connection_detail

extern "C" {

void *server1_http_server_create(void *router, std::size_t maxQueuedBytes)
{
    return new server1::http::connection_detail::HttpServer(router, maxQueuedBytes);
}

void server1_http_server_destroy(void *opaqueServer)
{
    auto *server = static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer);
    if (!server) return;
    // A response finalizer may run inside QAbstractSocket's readyRead stack.
    // Revoke I/O immediately, but let Qt finish notification before freeing it.
    server->requestDestruction();
}

int server1_http_server_listen(void *opaqueServer, unsigned short port)
{
    return opaqueServer == nullptr
        ? 0
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->listen(port);
}

void server1_http_server_stop(void *opaqueServer)
{
    if (opaqueServer != nullptr)
        static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->stop();
}

unsigned short server1_http_server_port(void *opaqueServer)
{
    return opaqueServer == nullptr
        ? 0
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->port();
}

const char *server1_http_server_error(void *opaqueServer)
{
    return opaqueServer == nullptr
        ? "null server"
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->error().c_str();
}

void server1_http_server_set_drain_paused(void *opaqueServer, int paused)
{
    if (opaqueServer != nullptr)
        static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)
            ->setDrainPaused(paused != 0);
}

std::size_t server1_http_server_queued_bytes(void *opaqueServer)
{
    return opaqueServer == nullptr
        ? 0
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->queuedBytes();
}

std::size_t server1_http_server_max_queued_bytes(void *opaqueServer)
{
    return opaqueServer == nullptr
        ? 0
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->maxQueuedBytes();
}

std::size_t server1_http_server_active_connections(void *opaqueServer)
{
    return opaqueServer == nullptr
        ? 0
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->activeConnections();
}

std::size_t server1_http_server_input_backlog_bytes(void *opaqueServer)
{
    return opaqueServer == nullptr ? 0
        : static_cast<server1::http::connection_detail::HttpServer *>(opaqueServer)->inputBacklogBytes();
}

}
