#include "MalLoopbackServer.h"

#include <QHostAddress>
#include <QList>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {

constexpr qsizetype kMaximumRequestBytes = 16 * 1024;

bool validLoopback(const QUrl &url)
{
    const QString host = url.host().toLower();
    return url.isValid() && url.scheme() == QLatin1String("http")
        && url.port() > 0 && url.port() <= 65535
        && !url.path().isEmpty() && url.query().isEmpty()
        && url.fragment().isEmpty() && url.userName().isEmpty()
        && url.password().isEmpty()
        && (host == QLatin1String("127.0.0.1")
            || host == QLatin1String("localhost")
            || host == QLatin1String("::1"));
}

QHostAddress addressFor(const QString &host)
{
    if (host == QLatin1String("::1"))
        return QHostAddress::LocalHostIPv6;
    return QHostAddress::LocalHost;
}

} // namespace

MalLoopbackServer::MalLoopbackServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection,
            this, &MalLoopbackServer::acceptConnections);
}

MalLoopbackServer::~MalLoopbackServer()
{
    stop();
}

bool MalLoopbackServer::start(const QUrl &redirectUri, QString *error)
{
    stop();
    if (!validLoopback(redirectUri)) {
        if (error)
            *error = QStringLiteral("MAL OAuth redirect URI must be a fixed loopback HTTP URI.");
        return false;
    }
    const QHostAddress address = addressFor(redirectUri.host().toLower());
    if (!m_server.listen(address, static_cast<quint16>(redirectUri.port()))) {
        if (error)
            *error = QStringLiteral("Colosseum could not reserve the MAL OAuth callback port.");
        return false;
    }
    m_redirectUri = redirectUri;
    m_settled = false;
    return true;
}

void MalLoopbackServer::stop()
{
    m_server.close();
    const auto sockets = m_buffers.keys();
    for (QTcpSocket *socket : sockets) {
        if (socket) {
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
        }
    }
    m_buffers.clear();
    m_redirectUri = {};
    m_settled = false;
}

void MalLoopbackServer::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (!socket)
            continue;
        m_buffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { readSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket] { forgetSocket(socket); });
    }
}

void MalLoopbackServer::readSocket(QTcpSocket *socket)
{
    if (!socket || !m_buffers.contains(socket))
        return;
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaximumRequestBytes) {
        rejectSocket(socket, 413, "Payload Too Large");
        return;
    }
    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;

    const qsizetype lineEnd = buffer.indexOf("\r\n");
    if (lineEnd <= 0) {
        rejectSocket(socket, 400, "Bad Request");
        return;
    }
    const QList<QByteArray> requestLine =
        buffer.left(lineEnd).split(' ');
    if (requestLine.size() != 3 || requestLine.at(0) != "GET"
        || !requestLine.at(2).startsWith("HTTP/1.")) {
        rejectSocket(socket, 405, "Method Not Allowed");
        return;
    }

    const QUrl requestUrl(QStringLiteral("http://localhost")
                          + QString::fromUtf8(requestLine.at(1)));
    if (!requestUrl.isValid()
        || requestUrl.path() != m_redirectUri.path()) {
        rejectSocket(socket, 404, "Not Found");
        return;
    }
    if (m_settled) {
        rejectSocket(socket, 409, "Already Completed");
        return;
    }

    const QUrlQuery query(requestUrl);
    const QByteArray state =
        query.queryItemValue(QStringLiteral("state")).toLatin1();
    const QByteArray code =
        query.queryItemValue(QStringLiteral("code")).toLatin1();
    const QString providerError =
        query.queryItemValue(QStringLiteral("error")).left(128);

    m_settled = true;
    const QByteArray body =
        "<!doctype html><meta charset=\"utf-8\">"
        "<title>Colosseum</title>"
        "<p>MyAnimeList authorization returned to Colosseum. "
        "You can close this tab.</p>";
    completeSocket(socket, body);
    m_server.close();

    if (!providerError.isEmpty()) {
        emit authorizationResult(state, {}, providerError);
    } else if (state.isEmpty() || code.isEmpty() || code.size() > 4096) {
        emit authorizationResult(state, {},
                                 QStringLiteral("invalid_callback"));
    } else {
        emit authorizationResult(state, code, {});
    }
}

void MalLoopbackServer::rejectSocket(QTcpSocket *socket,
                                     int status,
                                     const QByteArray &reason)
{
    if (!socket)
        return;
    const QByteArray body = reason + "\n";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status)
        + " " + reason + "\r\n"
        + "Content-Type: text/plain; charset=utf-8\r\n"
        + "Cache-Control: no-store\r\n"
        + "Connection: close\r\n"
        + "Content-Length: " + QByteArray::number(body.size())
        + "\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

void MalLoopbackServer::completeSocket(QTcpSocket *socket,
                                       const QByteArray &body)
{
    if (!socket)
        return;
    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: " + QByteArray::number(body.size())
        + "\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

void MalLoopbackServer::forgetSocket(QTcpSocket *socket)
{
    m_buffers.remove(socket);
    if (socket)
        socket->deleteLater();
}
