#include "QtHttpTransport.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QTcpSocket>

#include <algorithm>
#include <cstring>

namespace Colosseum::Player2 {
namespace {

constexpr int kConnectTimeoutMs = 15'000;
constexpr int kReadTimeoutMs = 20'000;
constexpr int kPollMs = 100;        // cancellation granularity for the blocking waits
constexpr int kMaxRedirects = 5;

qint64 parseContentRangeTotal(const QByteArray &value, qint64 *rangeStart)
{
    // "bytes 200-1000/1234"  ->  total 1234, start 200.  A "*" total stays unknown.
    const int slash = value.lastIndexOf('/');
    const int space = value.indexOf(' ');
    const int dash = value.indexOf('-');
    if (space >= 0 && dash > space && rangeStart)
        *rangeStart = value.mid(space + 1, dash - space - 1).trimmed().toLongLong();
    if (slash < 0)
        return -1;
    const QByteArray total = value.mid(slash + 1).trimmed();
    if (total == "*")
        return -1;
    return total.toLongLong();
}

} // namespace

QtHttpTransport::QtHttpTransport() = default;

QtHttpTransport::~QtHttpTransport()
{
    teardown();
}

void QtHttpTransport::teardown()
{
    if (m_socket) {
        m_socket->abort();
        m_socket.reset();
    }
    m_leftover.clear();
    m_bodyRemaining = -1;
    m_chunked = false;
    m_chunkRemaining = 0;
    m_chunkDone = false;
}

void QtHttpTransport::close()
{
    teardown();
}

void QtHttpTransport::cancel()
{
    m_cancelled.store(true, std::memory_order_release);
}

bool QtHttpTransport::waitReadable(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        if (m_cancelled.load(std::memory_order_acquire))
            return false;
        if (!m_socket)
            return false;
        if (m_socket->bytesAvailable() > 0)
            return true;
        if (m_socket->state() == QAbstractSocket::UnconnectedState)
            return m_socket->bytesAvailable() > 0;
        if (m_socket->waitForReadyRead(kPollMs))
            return true;
        if (m_socket->state() == QAbstractSocket::UnconnectedState)
            return m_socket->bytesAvailable() > 0;
        if (timeoutMs >= 0 && timer.elapsed() > timeoutMs)
            return false;
    }
}

bool QtHttpTransport::connectSocket(const QUrl &url, QString *error)
{
    const bool secure = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const int port = url.port(secure ? 443 : 80);
    if (secure) {
        auto *tls = new QSslSocket();
        m_socket.reset(tls);
        tls->connectToHostEncrypted(url.host(), static_cast<quint16>(port));
    } else {
        m_socket = std::make_unique<QTcpSocket>();
        m_socket->connectToHost(url.host(), static_cast<quint16>(port));
    }

    QElapsedTimer timer;
    timer.start();
    while (!m_socket->waitForConnected(kPollMs)) {
        if (m_cancelled.load(std::memory_order_acquire)) {
            if (error)
                *error = QStringLiteral("connection cancelled");
            return false;
        }
        if (m_socket->state() == QAbstractSocket::UnconnectedState ||
            timer.elapsed() > kConnectTimeoutMs) {
            if (error)
                *error = QStringLiteral("could not connect to %1").arg(url.host());
            return false;
        }
    }
    if (secure) {
        auto *tls = static_cast<QSslSocket *>(m_socket.get());
        while (!tls->waitForEncrypted(kPollMs)) {
            if (m_cancelled.load(std::memory_order_acquire) ||
                tls->state() == QAbstractSocket::UnconnectedState ||
                timer.elapsed() > kConnectTimeoutMs) {
                if (error)
                    *error = QStringLiteral("TLS handshake failed for %1").arg(url.host());
                return false;
            }
        }
    }
    return true;
}

bool QtHttpTransport::sendRequest(const QUrl &url, const RequestHeaders &headers, qint64 byteOffset,
                                  QString *error)
{
    QByteArray path = url.path(QUrl::FullyEncoded).toUtf8();
    if (path.isEmpty())
        path = "/";
    const QByteArray query = url.query(QUrl::FullyEncoded).toUtf8();
    if (!query.isEmpty()) {
        path += '?';
        path += query;
    }

    QByteArray request;
    request += "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + url.host().toUtf8();
    if (url.port() > 0)
        request += ':' + QByteArray::number(url.port());
    request += "\r\n";
    request += "Range: bytes=" + QByteArray::number(std::max<qint64>(0, byteOffset)) + "-\r\n";
    request += "Accept: */*\r\n";
    request += "Connection: keep-alive\r\n";

    bool hasUserAgent = false;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QByteArray key = it.key();
        if (key.compare("host", Qt::CaseInsensitive) == 0 ||
            key.compare("range", Qt::CaseInsensitive) == 0 ||
            key.compare("connection", Qt::CaseInsensitive) == 0)
            continue; // never let a caller override transport framing
        if (key.compare("user-agent", Qt::CaseInsensitive) == 0)
            hasUserAgent = true;
        request += key + ": " + it.value() + "\r\n"; // values are applied but never logged
    }
    if (!hasUserAgent)
        request += "User-Agent: Colosseum-Player2/1.0\r\n";
    request += "\r\n";

    if (m_socket->write(request) != request.size()) {
        if (error)
            *error = QStringLiteral("failed to write the HTTP request");
        return false;
    }
    while (m_socket->bytesToWrite() > 0) {
        if (m_cancelled.load(std::memory_order_acquire)) {
            if (error)
                *error = QStringLiteral("request write cancelled");
            return false;
        }
        if (!m_socket->waitForBytesWritten(kPollMs) &&
            m_socket->state() == QAbstractSocket::UnconnectedState) {
            if (error)
                *error = QStringLiteral("connection dropped while sending the request");
            return false;
        }
    }
    return true;
}

bool QtHttpTransport::readHeaderBlock(QByteArray *block, QString *error)
{
    block->clear();
    for (;;) {
        const int marker = block->indexOf("\r\n\r\n");
        if (marker >= 0) {
            m_leftover = block->mid(marker + 4);
            block->truncate(marker);
            return true;
        }
        if (!waitReadable(kReadTimeoutMs)) {
            if (error)
                *error = m_cancelled.load(std::memory_order_acquire)
                    ? QStringLiteral("cancelled") : QStringLiteral("timed out reading headers");
            return false;
        }
        block->append(m_socket->readAll());
        if (block->size() > 64 * 1024) {
            if (error)
                *error = QStringLiteral("response header block too large");
            return false;
        }
    }
}

bool QtHttpTransport::start(const QUrl &requestUrl, const RequestHeaders &headers, qint64 byteOffset,
                            HttpResponse *response, QString *error)
{
    m_cancelled.store(false, std::memory_order_release);
    QUrl url = requestUrl;

    for (int redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        teardown();
        m_cancelled.store(false, std::memory_order_release);
        if (!connectSocket(url, error))
            return false;
        if (!sendRequest(url, headers, byteOffset, error))
            return false;

        QByteArray headerBlock;
        if (!readHeaderBlock(&headerBlock, error))
            return false;

        const QList<QByteArray> lines = headerBlock.split('\n');
        if (lines.isEmpty()) {
            if (error)
                *error = QStringLiteral("empty HTTP response");
            return false;
        }
        const QByteArray statusLine = lines.first().trimmed();
        const int firstSpace = statusLine.indexOf(' ');
        const int statusCode = firstSpace >= 0
            ? statusLine.mid(firstSpace + 1, 3).trimmed().toInt() : 0;

        HttpResponse result;
        result.statusCode = statusCode;
        qint64 contentLength = -1;
        qint64 rangeStart = 0;
        qint64 contentRangeTotal = -1;
        QByteArray location;
        m_chunked = false;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (key == "content-length")
                contentLength = value.toLongLong();
            else if (key == "accept-ranges")
                result.acceptRanges = value.toLower() != "none" && !value.isEmpty();
            else if (key == "content-range")
                contentRangeTotal = parseContentRangeTotal(value, &rangeStart);
            else if (key == "location")
                location = value;
            else if (key == "transfer-encoding" && value.toLower().contains("chunked"))
                m_chunked = true;
        }

        if (statusCode >= 300 && statusCode < 400 && !location.isEmpty()) {
            url = url.resolved(QUrl(QString::fromUtf8(location)));
            continue; // follow the redirect
        }
        if (statusCode < 200 || statusCode >= 300) {
            if (error)
                *error = QStringLiteral("HTTP status %1").arg(statusCode);
            return false;
        }

        result.partial = statusCode == 206;
        if (result.partial)
            result.acceptRanges = true;
        if (contentRangeTotal >= 0) {
            result.totalBytes = contentRangeTotal;
            result.rangeStart = rangeStart;
        } else if (contentLength >= 0) {
            // 200 means the server ignored the range and served from zero.
            result.rangeStart = result.partial ? byteOffset : 0;
            result.totalBytes = result.rangeStart + contentLength;
        } else {
            result.rangeStart = result.partial ? byteOffset : 0;
            result.totalBytes = -1;
        }

        m_bodyRemaining = m_chunked ? -1 : contentLength;
        m_chunkRemaining = 0;
        m_chunkDone = false;
        *response = result;
        return true;
    }

    if (error)
        *error = QStringLiteral("too many redirects");
    return false;
}

bool QtHttpTransport::readChunkLine(QByteArray *line)
{
    line->clear();
    for (;;) {
        const int newline = m_leftover.indexOf('\n');
        if (newline >= 0) {
            *line = m_leftover.left(newline).trimmed();
            m_leftover.remove(0, newline + 1);
            return true;
        }
        if (!waitReadable(kReadTimeoutMs))
            return false;
        m_leftover.append(m_socket->readAll());
        if (m_leftover.size() > 64 * 1024)
            return false; // a chunk-size line should never be this long
    }
}

int QtHttpTransport::readChunked(char *dst, int maxLen)
{
    if (m_chunkDone)
        return 0;
    if (m_chunkRemaining == 0) {
        // Read the next chunk-size line, skipping the empty CRLF that trails a data chunk.
        QByteArray sizeLine;
        do {
            if (!readChunkLine(&sizeLine))
                return -1;
        } while (sizeLine.isEmpty());
        const int semicolon = sizeLine.indexOf(';');
        if (semicolon >= 0)
            sizeLine.truncate(semicolon); // drop any chunk extension
        bool ok = false;
        m_chunkRemaining = sizeLine.trimmed().toLongLong(&ok, 16);
        if (!ok)
            return -1;
        if (m_chunkRemaining == 0) {
            m_chunkDone = true;
            return 0; // terminating chunk; trailers are ignored
        }
    }
    // Serve from leftover first, then the socket, bounded by the current chunk and maxLen.
    const int want = static_cast<int>(std::min<qint64>(maxLen, m_chunkRemaining));
    int copied = 0;
    if (!m_leftover.isEmpty()) {
        const int take = std::min(want, static_cast<int>(m_leftover.size()));
        memcpy(dst, m_leftover.constData(), static_cast<size_t>(take));
        m_leftover.remove(0, take);
        copied = take;
    } else {
        if (!waitReadable(kReadTimeoutMs))
            return -1;
        copied = static_cast<int>(m_socket->read(dst, want));
        if (copied <= 0)
            return -1;
    }
    m_chunkRemaining -= copied;
    return copied; // the trailing CRLF is consumed as an empty line before the next size line
}

int QtHttpTransport::readIdentity(char *dst, int maxLen)
{
    if (m_bodyRemaining == 0)
        return 0; // consumed the whole Content-Length
    const int want = m_bodyRemaining > 0
        ? static_cast<int>(std::min<qint64>(maxLen, m_bodyRemaining)) : maxLen;

    if (!m_leftover.isEmpty()) {
        const int take = std::min(want, static_cast<int>(m_leftover.size()));
        memcpy(dst, m_leftover.constData(), static_cast<size_t>(take));
        m_leftover.remove(0, take);
        if (m_bodyRemaining > 0)
            m_bodyRemaining -= take;
        return take;
    }
    if (!waitReadable(kReadTimeoutMs)) {
        if (m_cancelled.load(std::memory_order_acquire))
            return -1;
        // No more data: a clean EOF for a length-unknown body, otherwise a premature disconnect.
        if (m_bodyRemaining < 0 && m_socket &&
            m_socket->state() == QAbstractSocket::UnconnectedState)
            return 0;
        return -1;
    }
    const int n = static_cast<int>(m_socket->read(dst, want));
    if (n < 0)
        return -1;
    if (n == 0)
        return m_bodyRemaining < 0 ? 0 : -1; // unknown length -> EOF; known length -> disconnect
    if (m_bodyRemaining > 0)
        m_bodyRemaining -= n;
    return n;
}

int QtHttpTransport::read(char *dst, int maxLen)
{
    if (maxLen <= 0)
        return 0;
    if (m_cancelled.load(std::memory_order_acquire))
        return -1;
    if (!m_socket)
        return -1;
    return m_chunked ? readChunked(dst, maxLen) : readIdentity(dst, maxLen);
}

} // namespace Colosseum::Player2
