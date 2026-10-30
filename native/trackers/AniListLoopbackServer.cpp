#include "AniListLoopbackServer.h"

#include <QList>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {

constexpr qsizetype kMaximumRequestBytes = 16 * 1024;
constexpr qsizetype kMaximumBodyBytes = 8 * 1024;
constexpr qsizetype kMaximumTokenChars = 4096;
constexpr qint64 kMaximumExpiresInSeconds = 10LL * 365 * 24 * 60 * 60;
constexpr int kMaximumConnections = 8;
constexpr int kConnectionIdleMs = 10 * 1000;

bool validRedirect(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("http")
        && url.host() == QLatin1String("127.0.0.1")
        && url.port() > 0 && url.port() <= 65535
        && !url.path().isEmpty() && url.path() != QLatin1String("/")
        && !url.path().endsWith(QLatin1Char('/'))
        && url.query().isEmpty() && url.fragment().isEmpty()
        && url.userName().isEmpty() && url.password().isEmpty();
}

bool constantTimeEqual(const QByteArray &left, const QByteArray &right)
{
    // Length is not secret (the state has a fixed length), so an early length
    // check is fine; the content comparison never short-circuits.
    if (left.size() != right.size() || left.isEmpty())
        return false;
    unsigned char difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index)
        difference |= static_cast<unsigned char>(left.at(index))
            ^ static_cast<unsigned char>(right.at(index));
    return difference == 0;
}

// Static page. No external resources; the only script is inline and the CSP
// allows nothing else. It moves the fragment into a same-origin POST, clears
// the fragment from the address bar, then tells the user to close the tab.
const QByteArray &callbackPage()
{
    static const QByteArray page =
        "<!doctype html><meta charset=\"utf-8\">"
        "<meta name=\"referrer\" content=\"no-referrer\">"
        "<title>Colosseum</title>"
        "<p id=\"m\">Finishing the AniList sign-in...</p>"
        "<script>"
        "(function(){"
        "var m=document.getElementById('m');"
        "var h=location.hash.replace(/^#/,'');"
        "try{history.replaceState(null,'',location.pathname);}catch(e){}"
        "if(!h){m.textContent='Nothing to send. Start the AniList sign-in again from Colosseum.';return;}"
        "fetch(location.pathname+'/token',{method:'POST',"
        "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:h,credentials:'omit',cache:'no-store',referrerPolicy:'no-referrer'})"
        ".then(function(r){m.textContent=r.ok"
        "?'You can close this tab and return to Colosseum.'"
        ":'Colosseum could not complete the sign-in. Return to Colosseum and try again.';})"
        ".catch(function(){m.textContent="
        "'Colosseum could not complete the sign-in. Return to Colosseum and try again.';});"
        "})();"
        "</script>";
    return page;
}

const QByteArray &pageSecurityHeaders()
{
    static const QByteArray headers =
        "Content-Security-Policy: default-src 'none'; "
        "script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; "
        "form-action 'none'; frame-ancestors 'none'\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n";
    return headers;
}

bool printableToken(const QByteArray &token)
{
    if (token.isEmpty() || token.size() > kMaximumTokenChars)
        return false;
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9._~+/=-]+$"));
    return pattern.match(QString::fromLatin1(token)).hasMatch();
}

} // namespace

AniListLoopbackServer::AniListLoopbackServer(QObject *parent)
    : QObject(parent)
{
    m_lifetime.setSingleShot(true);
    connect(&m_server, &QTcpServer::newConnection,
            this, &AniListLoopbackServer::acceptConnections);
    connect(&m_lifetime, &QTimer::timeout, this, [this] {
        if (m_settled || !m_server.isListening())
            return;
        settle();
        emit signInFailed(QStringLiteral("timeout"));
    });
}

AniListLoopbackServer::~AniListLoopbackServer()
{
    stop();
}

bool AniListLoopbackServer::start(const QUrl &redirectUri,
                                  const QByteArray &expectedState,
                                  QString *error)
{
    stop();
    if (!validRedirect(redirectUri)) {
        if (error)
            *error = QStringLiteral("AniList OAuth redirect URI must be a fixed 127.0.0.1 HTTP URI.");
        return false;
    }
    if (expectedState.size() < 16) {
        if (error)
            *error = QStringLiteral("AniList sign-in needs a random state.");
        return false;
    }
    if (!m_server.listen(QHostAddress::LocalHost,
                         static_cast<quint16>(redirectUri.port()))) {
        if (error)
            *error = QStringLiteral("Colosseum could not reserve the AniList OAuth callback port.");
        return false;
    }
    m_redirectUri = redirectUri;
    m_expectedState = expectedState;
    m_expectedHost = QByteArrayLiteral("127.0.0.1:")
        + QByteArray::number(redirectUri.port());
    m_expectedOrigin = QByteArrayLiteral("http://") + m_expectedHost;
    m_settled = false;
    m_lifetime.start(m_lifetimeMs);
    return true;
}

void AniListLoopbackServer::stop()
{
    m_lifetime.stop();
    m_server.close();
    const auto sockets = m_connections.keys();
    for (QTcpSocket *socket : sockets) {
        if (socket) {
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
        }
    }
    m_connections.clear();
    m_redirectUri = QUrl{};
    m_expectedState.clear();
    m_expectedOrigin.clear();
    m_expectedHost.clear();
    m_settled = false;
}

void AniListLoopbackServer::settle()
{
    m_settled = true;
    m_expectedState.clear();
    m_lifetime.stop();
    m_server.close();
}

void AniListLoopbackServer::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (!socket)
            continue;
        if (m_connections.size() >= kMaximumConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_connections.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { readSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket] { forgetSocket(socket); });
        QPointer<QTcpSocket> guard(socket);
        QTimer::singleShot(kConnectionIdleMs, this, [this, guard] {
            if (guard && m_connections.contains(guard.data()))
                guard->abort();
        });
    }
}

void AniListLoopbackServer::readSocket(QTcpSocket *socket)
{
    if (!socket || !m_connections.contains(socket))
        return;
    QByteArray &buffer = m_connections[socket].buffer;
    buffer += socket->readAll();
    if (buffer.size() > kMaximumRequestBytes) {
        reject(socket, 413, "Payload Too Large");
        return;
    }
    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;

    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine =
        lines.first().trimmed().split(' ');
    if (requestLine.size() != 3
        || !requestLine.at(2).startsWith("HTTP/1.")
        || !requestLine.at(1).startsWith('/')) {
        reject(socket, 400, "Bad Request");
        return;
    }
    QHash<QByteArray, QByteArray> headers;
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            reject(socket, 400, "Bad Request");
            return;
        }
        headers.insert(line.left(colon).trimmed().toLower(),
                       line.mid(colon + 1).trimmed());
    }
    if (headers.contains("transfer-encoding")) {
        reject(socket, 400, "Bad Request");
        return;
    }

    qsizetype contentLength = 0;
    if (headers.contains("content-length")) {
        bool ok = false;
        const qint64 value = headers.value("content-length").toLongLong(&ok);
        if (!ok || value < 0) {
            reject(socket, 400, "Bad Request");
            return;
        }
        if (value > kMaximumBodyBytes) {
            reject(socket, 413, "Payload Too Large");
            return;
        }
        contentLength = static_cast<qsizetype>(value);
    }
    const qsizetype bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < contentLength)
        return; // Body still arriving.

    const QByteArray body = buffer.mid(bodyStart, contentLength);
    // Path only: a query string is never used for anything.
    QByteArray path = requestLine.at(1);
    const qsizetype query = path.indexOf('?');
    if (query >= 0)
        path.truncate(query);
    handleRequest(socket, requestLine.at(0), path, headers, body);
}

void AniListLoopbackServer::handleRequest(
    QTcpSocket *socket,
    const QByteArray &method,
    const QByteArray &path,
    const QHash<QByteArray, QByteArray> &headers,
    const QByteArray &body)
{
    const QByteArray pagePath = m_redirectUri.path().toUtf8();
    const QByteArray tokenPath = pagePath + "/token";
    if (path != pagePath && path != tokenPath) {
        reject(socket, 404, "Not Found");
        return;
    }
    // A foreign Host header means DNS rebinding or a proxy; refuse it.
    if (headers.value("host") != m_expectedHost) {
        reject(socket, 403, "Forbidden");
        return;
    }
    if (path == pagePath) {
        if (method != "GET") {
            reject(socket, 405, "Method Not Allowed");
            return;
        }
        if (m_settled) {
            reject(socket, 409, "Already Completed");
            return;
        }
        respond(socket, 200, "OK", "text/html; charset=utf-8",
                callbackPage(), pageSecurityHeaders());
        return;
    }
    if (method != "POST") {
        reject(socket, 405, "Method Not Allowed");
        return;
    }
    if (m_settled) {
        reject(socket, 409, "Already Completed");
        return;
    }
    handleTokenPost(socket, headers, body);
}

void AniListLoopbackServer::handleTokenPost(
    QTcpSocket *socket,
    const QHash<QByteArray, QByteArray> &headers,
    const QByteArray &body)
{
    // Browsers always send Origin on a POST. Require it and require it exact,
    // so another web page cannot feed this endpoint.
    if (headers.value("origin") != m_expectedOrigin) {
        reject(socket, 403, "Forbidden");
        return;
    }
    const QByteArray contentType =
        headers.value("content-type").split(';').first().trimmed().toLower();
    if (contentType != "application/x-www-form-urlencoded") {
        reject(socket, 415, "Unsupported Media Type");
        return;
    }

    const QUrlQuery fields{QString::fromLatin1(body)};
    const QByteArray state = fields.queryItemValue(
        QStringLiteral("state"), QUrl::FullyDecoded).toLatin1();
    // State is mandatory for a token and for an error alike; a mismatch is
    // rejected without ending the sign-in so a stray request cannot cancel it.
    if (!constantTimeEqual(m_expectedState, state)) {
        reject(socket, 400, "Bad Request");
        return;
    }

    const QString providerError = fields.queryItemValue(
        QStringLiteral("error"), QUrl::FullyDecoded).left(128);
    if (!providerError.isEmpty()) {
        respond(socket, 200, "OK", "text/plain; charset=utf-8", "ok\n");
        settle();
        // Emit once the reply has flushed: the owner usually calls stop(),
        // which aborts sockets and would drop an unsent reply.
        connect(socket, &QTcpSocket::disconnected, this,
                [this, providerError] { emit signInFailed(providerError); });
        return;
    }

    const QByteArray token = fields.queryItemValue(
        QStringLiteral("access_token"), QUrl::FullyDecoded).toLatin1();
    const QString tokenType = fields.queryItemValue(
        QStringLiteral("token_type"), QUrl::FullyDecoded);
    bool expiresOk = false;
    const qint64 expiresIn = fields.queryItemValue(
        QStringLiteral("expires_in"), QUrl::FullyDecoded)
                                 .toLongLong(&expiresOk);
    if (!printableToken(token)
        || tokenType.compare(QLatin1String("bearer"), Qt::CaseInsensitive) != 0
        || !expiresOk || expiresIn <= 0
        || expiresIn > kMaximumExpiresInSeconds) {
        reject(socket, 400, "Bad Request");
        return;
    }

    respond(socket, 200, "OK", "text/plain; charset=utf-8", "ok\n");
    settle();
    connect(socket, &QTcpSocket::disconnected, this,
            [this, token, expiresIn] { emit tokenReceived(token, expiresIn); });
}

void AniListLoopbackServer::respond(QTcpSocket *socket,
                                    int status,
                                    const QByteArray &reason,
                                    const QByteArray &contentType,
                                    const QByteArray &body,
                                    const QByteArray &extraHeaders)
{
    if (!socket)
        return;
    const QByteArray response = "HTTP/1.1 " + QByteArray::number(status)
        + " " + reason + "\r\n"
        + "Content-Type: " + contentType + "\r\n"
        + "Cache-Control: no-store\r\n"
        + extraHeaders
        + "Connection: close\r\n"
        + "Content-Length: " + QByteArray::number(body.size())
        + "\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

void AniListLoopbackServer::reject(QTcpSocket *socket,
                                   int status,
                                   const QByteArray &reason)
{
    respond(socket, status, reason, "text/plain; charset=utf-8",
            reason + "\n");
}

void AniListLoopbackServer::forgetSocket(QTcpSocket *socket)
{
    m_connections.remove(socket);
    if (socket)
        socket->deleteLater();
}
