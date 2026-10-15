// http_header_channel_harness — deterministic WIRE proof for Theatre House HTTP Source, slice 1.
//
// Drives a REAL MpvItem (not a bare mpv handle — the constructor's forced options, user-agent and
// ytdl=no included, are part of the channel under test) at a local QTcpServer, and records the HTTP
// request headers mpv/ffmpeg actually put on the wire. Proves:
//   1. loadFileWithHeaders installs the addon Referer/Origin and it REACHES the wire (bonus: the
//      forced VLC user-agent arriving confirms this is the production MpvItem config, not a stub).
//   2. a comma inside a header value survives as ONE header (node-array format, not comma-joined).
//   3. loadFile CLEARS the field, so the next plain load carries NO leftover header — the leak guard
//      and the plain-path-sends-no-header negative control in one. If the clear were a no-op, the
//      /plain assertions go red; if the channel were dead, the /withref assertion goes red. The two
//      bracket the behaviour, so neither can pass vacuously.
//   4. ytdl is OFF — else ytdl_hook would install its own http-header-fields and clobber ours in the
//      real app while a bare-handle harness stayed green (the exact vacuity this harness closes).
//
// House contract: prints HTTP_HEADER_CHANNEL_OK on success; "FAIL: <msg>" + exit(1) on any failure.
// Loopback only (no live network); every wait is event-driven (no sleeps).

#include "player/mpvitem.h"

#include <QByteArray>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

#include <cstdio>
#include <cstdlib>

namespace {

struct CapturedRequest {
    QString path;
    QMap<QString, QString> headers; // lower-cased field name -> value
};

// Stable append-only log. We copy out the entry we need immediately after each wait, before any
// further load can append and reallocate the list.
QList<CapturedRequest> g_requests;

void fail(const QString &msg)
{
    std::fprintf(stderr, "FAIL: %s\n", msg.toUtf8().constData());
    std::exit(1);
}

// Minimal loopback HTTP server: per connection, read to the header terminator, record the request
// path + headers, reply with a tiny 200 (enough for mpv to read then fail to decode — we assert on
// the REQUEST, never on playback), and close.
class LoopbackServer : public QObject
{
    Q_OBJECT
public:
    explicit LoopbackServer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &LoopbackServer::onConnection);
    }
    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }

Q_SIGNALS:
    void requestCaptured(const QString &path);

private Q_SLOTS:
    void onConnection()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *sock = m_server.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { onReadyRead(sock); });
            connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        }
    }

    void onReadyRead(QTcpSocket *sock)
    {
        QByteArray buf = sock->property("colosseumRequestBuffer").toByteArray();
        buf += sock->readAll();
        sock->setProperty("colosseumRequestBuffer", buf);
        const int hdrEnd = buf.indexOf("\r\n\r\n");
        if (hdrEnd < 0)
            return; // headers not complete yet
        if (sock->property("colosseumRequestHandled").toBool())
            return;
        sock->setProperty("colosseumRequestHandled", true);

        CapturedRequest req;
        const QList<QByteArray> lines = buf.left(hdrEnd).split('\n');
        if (!lines.isEmpty()) {
            const QList<QByteArray> parts = lines.first().trimmed().split(' ');
            if (parts.size() >= 2)
                req.path = QString::fromUtf8(parts.at(1));
        }
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            req.headers.insert(QString::fromUtf8(line.left(colon)).trimmed().toLower(),
                               QString::fromUtf8(line.mid(colon + 1)).trimmed());
        }
        g_requests.append(req);

        const QByteArray body = "notmedia";
        const QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
            + "\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n" + body;
        sock->write(resp);
        sock->flush();
        sock->disconnectFromHost();
        Q_EMIT requestCaptured(req.path);
    }

private:
    QTcpServer m_server;
};

// Event-driven wait: return once at least one captured request matches `path`, or timeout.
bool waitForRequest(LoopbackServer *server, const QString &path, int timeoutMs)
{
    for (const auto &r : g_requests)
        if (r.path == path)
            return true;

    QEventLoop loop;
    bool got = false;
    QObject::connect(server, &LoopbackServer::requestCaptured, &loop, [&](const QString &p) {
        if (p == path) {
            got = true;
            loop.quit();
        }
    });
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return got;
}

// Copy the first captured request for `path`. Call right after waitForRequest, before more loads.
bool firstFor(const QString &path, CapturedRequest &out)
{
    for (const auto &r : g_requests) {
        if (r.path == path) {
            out = r;
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    LoopbackServer server;
    if (!server.listen())
        fail(QStringLiteral("loopback server could not listen on 127.0.0.1"));
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());

    MpvItem item; // real player surface — constructor forces user-agent=VLC and ytdl=no

    // (4) ytdl OFF, or ytdl_hook overrides our headers in the real app. `ytdl` is an mpv FLAG
    // option, so getProperty returns a bool (false = disabled) — we set it to "no" in the
    // constructor and confirm it reads back OFF.
    const QVariant ytdlVal = item.getProperty(QStringLiteral("ytdl"));
    if (ytdlVal.toBool())
        fail(QStringLiteral("ytdl expected OFF, got '%1' — ytdl_hook would clobber http-header-fields")
                 .arg(ytdlVal.toString()));

    CapturedRequest r;

    // (1) A header-carrying load: the Referer must reach the wire, and the forced VLC UA proves the
    // production config is live.
    QVariantMap withRef;
    withRef.insert(QStringLiteral("Referer"), QStringLiteral("https://prov.example/"));
    item.loadFileWithHeaders(base + QStringLiteral("/withref.bin"), withRef);
    if (!waitForRequest(&server, QStringLiteral("/withref.bin"), 8000))
        fail(QStringLiteral("no request for /withref.bin — the header load never hit the wire"));
    if (!firstFor(QStringLiteral("/withref.bin"), r))
        fail(QStringLiteral("/withref.bin request vanished"));
    if (r.headers.value(QStringLiteral("referer")) != QStringLiteral("https://prov.example/"))
        fail(QStringLiteral("Referer not on the wire (got '%1')").arg(r.headers.value(QStringLiteral("referer"))));
    if (r.headers.value(QStringLiteral("user-agent")) != QStringLiteral("VLC/3.0.20 LibVLC/3.0.20"))
        fail(QStringLiteral("User-Agent not the production VLC UA (got '%1') — not the real MpvItem config")
                 .arg(r.headers.value(QStringLiteral("user-agent"))));

    // (2) A comma inside a value stays ONE header (node-array, not comma-joined).
    QVariantMap comma;
    comma.insert(QStringLiteral("X-Thing"), QStringLiteral("a,b"));
    item.loadFileWithHeaders(base + QStringLiteral("/comma.bin"), comma);
    if (!waitForRequest(&server, QStringLiteral("/comma.bin"), 8000))
        fail(QStringLiteral("no request for /comma.bin"));
    if (!firstFor(QStringLiteral("/comma.bin"), r))
        fail(QStringLiteral("/comma.bin request vanished"));
    if (r.headers.value(QStringLiteral("x-thing")) != QStringLiteral("a,b"))
        fail(QStringLiteral("comma value split or mangled: X-Thing='%1' (expected 'a,b')")
                 .arg(r.headers.value(QStringLiteral("x-thing"))));

    // (3) The plain path AFTER header loads: no leftover Referer or X-Thing on ANY /plain request.
    // This is both the leak guard and the plain-path-sends-no-header negative control.
    item.loadFile(base + QStringLiteral("/plain.bin"));
    if (!waitForRequest(&server, QStringLiteral("/plain.bin"), 8000))
        fail(QStringLiteral("no request for /plain.bin — the plain load never hit the wire"));
    for (const auto &req : g_requests) {
        if (req.path != QStringLiteral("/plain.bin"))
            continue;
        if (req.headers.contains(QStringLiteral("referer")))
            fail(QStringLiteral("leak: /plain.bin carried a leftover Referer '%1'")
                     .arg(req.headers.value(QStringLiteral("referer"))));
        if (req.headers.contains(QStringLiteral("x-thing")))
            fail(QStringLiteral("leak: /plain.bin carried a leftover X-Thing '%1'")
                     .arg(req.headers.value(QStringLiteral("x-thing"))));
    }

    std::printf("HTTP_HEADER_CHANNEL_OK\n");
    return 0;
}

#include "http_header_channel_harness.moc"
