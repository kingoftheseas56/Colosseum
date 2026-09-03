#include "HttpConnection.h"

#include <QJsonParseError>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

namespace colosseum::server {

HttpConnection::HttpConnection(QTcpSocket *socket, std::shared_ptr<HttpRouter> router, QObject *parent)
    : QObject(parent),
      m_socket(socket),
      m_router(std::move(router)),
      m_cancellation(std::make_shared<CancellationToken>())
{
    Q_ASSERT(m_socket);
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this, [this] { onReadyRead(); });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { onDisconnected(); });
    if (m_socket->bytesAvailable() > 0)
        onReadyRead();
}

HttpConnection::~HttpConnection()
{
    if (m_cancellation)
        m_cancellation->cancel();
}

void HttpConnection::abort()
{
    if (m_cancellation)
        m_cancellation->cancel();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
}

void HttpConnection::onReadyRead()
{
    if (m_finished || m_dispatched || !m_socket)
        return;
    m_buffer += m_socket->readAll();
    tryParseAndDispatch();
}

void HttpConnection::onDisconnected()
{
    if (m_cancellation)
        m_cancellation->cancel();
    deleteLater();
}

QString HttpConnection::decodeComponent(QByteArray value)
{
    value.replace('+', ' ');
    return QUrl::fromPercentEncoding(value);
}

QHash<QString, QStringList> HttpConnection::parsePairs(const QByteArray &encoded)
{
    QHash<QString, QStringList> out;
    if (encoded.isEmpty())
        return out;
    for (const QByteArray &part : encoded.split('&')) {
        const qsizetype equals = part.indexOf('=');
        const QByteArray rawKey = equals < 0 ? part : part.left(equals);
        const QByteArray rawValue = equals < 0 ? QByteArray{} : part.mid(equals + 1);
        out[decodeComponent(rawKey)].push_back(decodeComponent(rawValue));
    }
    return out;
}

bool HttpConnection::tryParseAndDispatch()
{
    const qsizetype headerEnd = m_buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (m_buffer.size() > MaxHeaderBytes)
            sendSimpleError(431, "Request Header Fields Too Large");
        return false;
    }
    if (headerEnd > MaxHeaderBytes) {
        sendSimpleError(431, "Request Header Fields Too Large");
        return false;
    }

    const QByteArray headerBlock = m_buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
        sendSimpleError(400, "Bad Request");
        return false;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(2).startsWith("HTTP/1.")) {
        sendSimpleError(400, "Bad Request");
        return false;
    }

    HttpRequest request;
    request.method = QString::fromLatin1(requestLine.at(0).trimmed().toUpper());
    request.rawTarget = requestLine.at(1).trimmed();
    if (request.method.isEmpty() || request.rawTarget.isEmpty() || !request.rawTarget.startsWith('/')) {
        sendSimpleError(400, "Bad Request");
        return false;
    }

    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            sendSimpleError(400, "Bad Request");
            return false;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (request.headers.contains(name))
            request.headers[name] += QByteArray(", ") + value;
        else
            request.headers.insert(name, value);
    }

    if (!request.header("transfer-encoding").isEmpty()
        && request.header("transfer-encoding").toLower() != "identity") {
        sendSimpleError(400, "Unsupported Transfer-Encoding");
        return false;
    }

    qint64 contentLength = 0;
    if (request.headers.contains("content-length")) {
        bool ok = false;
        contentLength = request.header("content-length").toLongLong(&ok);
        if (!ok || contentLength < 0) {
            sendSimpleError(400, "Bad Content-Length");
            return false;
        }
        if (contentLength > MaxBodyBytes) {
            sendSimpleError(413, "Payload Too Large");
            return false;
        }
    }

    const qsizetype bodyOffset = headerEnd + 4;
    if (m_buffer.size() - bodyOffset < contentLength)
        return false;

    request.body = m_buffer.mid(bodyOffset, contentLength);
    const qsizetype question = request.rawTarget.indexOf('?');
    request.rawPath = question < 0 ? request.rawTarget : request.rawTarget.left(question);
    const QByteArray rawQuery = question < 0 ? QByteArray{} : request.rawTarget.mid(question + 1);
    // Match the encoded path first, then decode individual captures like Express/path-to-regexp.
    request.path = QString::fromUtf8(request.rawPath);
    request.query = parsePairs(rawQuery);
    request.cancellation = m_cancellation;

    const QByteArray contentType = request.header("content-type").toLower().split(';').value(0).trimmed();
    if (contentType == "application/json" && !request.body.isEmpty()) {
        QJsonParseError error;
        request.jsonBody = QJsonDocument::fromJson(request.body, &error);
        if (error.error != QJsonParseError::NoError
            || (!request.jsonBody.isObject() && !request.jsonBody.isArray())) {
            sendSimpleError(400, "Invalid JSON body");
            return false;
        }
        request.hasJsonBody = true;
    } else if (contentType == "application/x-www-form-urlencoded") {
        request.formBody = parsePairs(request.body);
    }

    m_dispatched = true;
    HttpResponse response = createResponse(request.method.toLatin1());
    try {
        const bool handled = m_router && m_router->dispatch(request, response);
        if (!handled && !response.isFinished()) {
            response.writeHead(404);
            response.end();
        }
    } catch (const std::exception &error) {
        if (!response.isFinished()) {
            response.writeHead(500);
            response.end(error.what());
        }
    } catch (...) {
        if (!response.isFinished()) {
            response.writeHead(500);
            response.end("Internal Server Error");
        }
    }
    return true;
}

HttpResponse HttpConnection::createResponse(const QByteArray &method)
{
    auto state = std::make_shared<HttpResponse::State>();
    state->suppressBody = method == "HEAD";
    QPointer<HttpConnection> weak(this);
    const bool suppressBody = state->suppressBody;

    state->onHead = [weak](int status, const QHash<QByteArray, QByteArray> &headers, bool streaming) {
        if (!weak)
            return;
        auto invoke = [weak, status, headers, streaming] {
            if (weak)
                weak->sendHead(status, headers, streaming);
        };
        if (QThread::currentThread() == weak->thread())
            invoke();
        else
            QMetaObject::invokeMethod(weak, std::move(invoke), Qt::QueuedConnection);
    };
    state->onData = [weak, suppressBody](const QByteArray &data, bool streaming) {
        if (!weak || suppressBody)
            return;
        auto invoke = [weak, data, streaming] {
            if (weak)
                weak->sendData(data, streaming);
        };
        if (QThread::currentThread() == weak->thread())
            invoke();
        else
            QMetaObject::invokeMethod(weak, std::move(invoke), Qt::QueuedConnection);
    };
    state->onEnd = [weak](bool streaming) {
        if (!weak)
            return;
        auto invoke = [weak, streaming] {
            if (weak)
                weak->sendEnd(streaming);
        };
        if (QThread::currentThread() == weak->thread())
            invoke();
        else
            QMetaObject::invokeMethod(weak, std::move(invoke), Qt::QueuedConnection);
    };
    return HttpResponse(std::move(state));
}

void HttpConnection::sendSimpleError(int status, const QByteArray &message)
{
    if (m_finished)
        return;
    m_dispatched = true;
    HttpResponse response = createResponse("GET");
    response.writeHead(status);
    response.end(message);
}

void HttpConnection::sendHead(int status, QHash<QByteArray, QByteArray> headers, bool streaming)
{
    if (m_finished || m_headWritten || !m_socket)
        return;
    m_headWritten = true;
    headers.insert("connection", "close");
    if (streaming && !headers.contains("content-length"))
        headers.insert("transfer-encoding", "chunked");

    QByteArray wire = "HTTP/1.1 " + QByteArray::number(status) + " " + reasonPhrase(status) + "\r\n";
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        wire += it.key() + ": " + it.value() + "\r\n";
    wire += "\r\n";
    m_socket->write(wire);
}

void HttpConnection::sendData(const QByteArray &data, bool streaming)
{
    if (m_finished || !m_socket || data.isEmpty())
        return;
    if (streaming) {
        m_socket->write(QByteArray::number(data.size(), 16));
        m_socket->write("\r\n");
        m_socket->write(data);
        m_socket->write("\r\n");
    } else {
        m_socket->write(data);
    }
}

void HttpConnection::sendEnd(bool streaming)
{
    if (m_finished || !m_socket)
        return;
    m_finished = true;
    if (streaming)
        m_socket->write("0\r\n\r\n");
    m_socket->flush();
    m_socket->disconnectFromHost();
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        onDisconnected();
}

QByteArray HttpConnection::reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 416: return "Range Not Satisfiable";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "Status";
    }
}

} // namespace colosseum::server
