#pragma once

#include "HttpMediaSource.h"

#include <QtCore/QByteArray>
#include <QtCore/QUrl>

#include <atomic>
#include <memory>

class QTcpSocket;

namespace Colosseum::Player2 {

// The real network implementation of IHttpTransport: a single-threaded blocking HTTP(S) client.
// HttpMediaSource guarantees every call arrives from its fetch thread, so no socket ever crosses a
// thread boundary. cancel() only flips an atomic; the blocking waits poll it on a short timeout, so
// no cross-thread socket operation is ever needed.
//
// Scope: ranged GET, bounded redirects, identity (Content-Length) and chunked bodies, and a
// read-until-close fallback. Request headers are applied verbatim and never written to a log or a
// diagnostic — credentials stay inside the transport.
class QtHttpTransport final : public IHttpTransport
{
public:
    QtHttpTransport();
    ~QtHttpTransport() override;

    bool start(const QUrl &url, const RequestHeaders &headers, qint64 byteOffset,
               HttpResponse *response, QString *error) override;
    int read(char *dst, int maxLen) override;
    void close() override;
    void cancel() override;
    void setStallTimeoutMs(int milliseconds) override;

private:
    bool connectSocket(const QUrl &url, QString *error);
    bool sendRequest(const QUrl &url, const RequestHeaders &headers, qint64 byteOffset,
                     QString *error);
    bool readHeaderBlock(QByteArray *block, QString *error);
    bool waitReadable(int timeoutMs);
    int stallTimeoutMs() const;
    bool readChunkLine(QByteArray *line);
    int readChunked(char *dst, int maxLen);
    int readIdentity(char *dst, int maxLen);
    void teardown();

    std::unique_ptr<QTcpSocket> m_socket;
    std::atomic_bool m_cancelled{false};
    // Patience for a connected-but-silent origin, set by HttpMediaSource. Written from the fetch
    // thread and also from seek() (any thread), hence atomic.
    std::atomic<int> m_stallTimeoutMs{20'000};
    QByteArray m_leftover;         // body bytes already pulled off the socket while parsing headers
    qint64 m_bodyRemaining = -1;   // Content-Length countdown; -1 means read until the peer closes
    bool m_chunked = false;        // Transfer-Encoding: chunked
    qint64 m_chunkRemaining = 0;   // bytes left in the current chunk (chunked mode)
    bool m_chunkDone = false;      // saw the terminating zero-length chunk
};

} // namespace Colosseum::Player2
