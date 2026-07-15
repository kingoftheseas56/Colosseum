// AnimeOrderService cache / refresh / worker-parse contract.
//
// Drives the service against a local QTcpServer (no external network) with an
// isolated QTemporaryDir cache root per scenario. Proves: cold-start download +
// atomic activation, hash-verified pointer, offline restart, seven-day refresh,
// non-advancing failure modes (http error, http redirect, oversize, bad json,
// bad xml, hash mismatch), unpointed-generation rejection, prune-to-two,
// refresh coalescing, and stale-vs-error end states.
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe).
#include "anime/AnimeOrderService.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <iostream>

namespace {

constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: cannot open " << path.toStdString() << '\n';
        std::exit(1);
    }
    return f.readAll();
}

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString genIdFor(const QByteArray& fribb, const QByteArray& xml)
{
    return sha256Hex((sha256Hex(fribb) + sha256Hex(xml)).toUtf8());
}

// ── A tiny, controllable HTTP/1.1 server over loopback ───────────────────────
class LocalServer : public QTcpServer {
public:
    struct Route {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/octet-stream";
        QByteArray location;       // non-empty → 302 redirect
        qint64 fakeContentLength = -1; // >=0 → advertise this length (oversize test)
        int hits = 0;
    };

    Route fribb;
    Route xml;

    explicit LocalServer(QByteArray fribbBody, QByteArray xmlBody)
    {
        fribb.body = std::move(fribbBody);
        fribb.contentType = "application/json";
        xml.body = std::move(xmlBody);
        xml.contentType = "application/xml";
    }

    AnimeOrderService::Sources sources() const
    {
        AnimeOrderService::Sources s;
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
        s.fribb = QUrl(base + QStringLiteral("/fribb.json"));
        s.mappings = QUrl(base + QStringLiteral("/anime-list.xml"));
        s.allowHttpForTests = true;
        return s;
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->readAll();
            const int lineEnd = request.indexOf("\r\n");
            const QByteArray line = lineEnd >= 0 ? request.left(lineEnd) : request;
            Route* route = nullptr;
            if (line.contains("/fribb.json"))
                route = &fribb;
            else if (line.contains("/anime-list.xml"))
                route = &xml;
            if (!route) {
                socket->write("HTTP/1.1 404 NF\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->flush();
                socket->disconnectFromHost();
                return;
            }
            route->hits++;
            QByteArray response;
            if (!route->location.isEmpty()) {
                response = "HTTP/1.1 302 Found\r\nLocation: " + route->location
                    + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            } else {
                const qint64 advertised =
                    route->fakeContentLength >= 0 ? route->fakeContentLength : route->body.size();
                response = "HTTP/1.1 " + QByteArray::number(route->status) + " S\r\n";
                response += "Content-Type: " + route->contentType + "\r\n";
                response += "Content-Length: " + QByteArray::number(advertised) + "\r\n";
                response += "Connection: close\r\n\r\n";
                response += route->body;
            }
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};

// Spin the event loop until predicate() is true or timeout elapses, waking on
// each AnimeOrderService::changed() emission.
template <typename Pred>
bool waitUntil(AnimeOrderService& svc, Pred predicate, int timeoutMs = 20000)
{
    if (predicate())
        return true;
    QEventLoop loop;
    bool done = false;
    const QMetaObject::Connection c =
        QObject::connect(&svc, &AnimeOrderService::changed, &loop, [&]() {
            if (predicate()) {
                done = true;
                loop.quit();
            }
        });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    return done || predicate();
}

bool waitForState(AnimeOrderService& svc, const QString& want, int timeoutMs = 20000)
{
    return waitUntil(svc, [&] { return svc.state() == want; }, timeoutMs);
}

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Write a complete, valid cache generation + pointer directly on disk.
QString seedGeneration(const QString& cacheRoot, const QByteArray& fribb, const QByteArray& xml,
                       qint64 fetchedAt, bool writePointer = true, bool corruptHash = false)
{
    const QString genId = genIdFor(fribb, xml);
    const QString genDir = cacheRoot + QStringLiteral("/generations/") + genId;
    QDir().mkpath(genDir);

    QFile ff(genDir + QStringLiteral("/fribb-anime-list.json"));
    ff.open(QIODevice::WriteOnly);
    ff.write(fribb);
    ff.close();
    QFile xf(genDir + QStringLiteral("/anime-list-master.xml"));
    xf.open(QIODevice::WriteOnly);
    xf.write(xml);
    xf.close();

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("fetchedAt"), fetchedAt);
    manifest.insert(QStringLiteral("fribbUrl"), QStringLiteral("http://seed/fribb.json"));
    manifest.insert(QStringLiteral("mappingsUrl"), QStringLiteral("http://seed/anime-list.xml"));
    manifest.insert(QStringLiteral("fribbBytes"), fribb.size());
    manifest.insert(QStringLiteral("mappingsBytes"), xml.size());
    manifest.insert(QStringLiteral("fribbSha256"),
                    corruptHash ? QStringLiteral("deadbeef") : sha256Hex(fribb));
    manifest.insert(QStringLiteral("mappingsSha256"), sha256Hex(xml));
    QFile gf(genDir + QStringLiteral("/generation.json"));
    gf.open(QIODevice::WriteOnly);
    gf.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    gf.close();

    if (writePointer) {
        QJsonObject pointer;
        pointer.insert(QStringLiteral("schemaVersion"), 1);
        pointer.insert(QStringLiteral("active"), genId);
        QFile pf(cacheRoot + QStringLiteral("/current.json"));
        pf.open(QIODevice::WriteOnly);
        pf.write(QJsonDocument(pointer).toJson(QJsonDocument::Compact));
        pf.close();
    }
    return genId;
}

int generationCount(const QString& cacheRoot)
{
    return QDir(cacheRoot + QStringLiteral("/generations"))
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
        .size();
}

bool hasGeneration(const QString& cacheRoot, const QString& genId)
{
    return QDir(cacheRoot + QStringLiteral("/generations/") + genId).exists();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimeOrderServiceHarness"));

    const QStringList args = app.arguments();
    require(args.size() >= 3, "usage: anime_order_service_harness <fribb.json> <anime-list.xml>");
    const QByteArray fribb = readFile(args.at(1));
    const QByteArray xml = readFile(args.at(2));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QNetworkAccessManager nam;

    // ── 1. Cold start: download both once, reach ready, revision 1 ────────────
    {
        QTemporaryDir cache;
        require(cache.isValid(), "cold cache dir valid");
        LocalServer server(fribb, xml);
        require(server.listen(QHostAddress::LocalHost), "cold server listening");
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "ready"), "cold start reaches ready");
        require(svc.revision() == 1, "cold start revision is 1");
        require(server.fribb.hits == 1 && server.xml.hits == 1, "each source downloaded exactly once");
        require(svc.resolve({{"sourceId", "mal:21"}}, {}).value("status").toString() == "mapped",
                "resolves a work once ready");

        // ── 2. current.json points to a generation whose files match hashes ───
        const QByteArray pointerBytes = readFile(cache.path() + QStringLiteral("/current.json"));
        const QString activeId =
            QJsonDocument::fromJson(pointerBytes).object().value(QStringLiteral("active")).toString();
        require(!activeId.isEmpty(), "current.json names an active generation");
        require(hasGeneration(cache.path(), activeId), "active generation directory exists");
        const QString gd = cache.path() + QStringLiteral("/generations/") + activeId;
        require(sha256Hex(readFile(gd + QStringLiteral("/fribb-anime-list.json"))) == sha256Hex(fribb),
                "stored fribb matches downloaded bytes");
        require(sha256Hex(readFile(gd + QStringLiteral("/anime-list-master.xml"))) == sha256Hex(xml),
                "stored xml matches downloaded bytes");

        // ── 3. Restart loads the generation and resolves while offline ────────
        server.close();
        AnimeOrderService restarted(&nam, cache.path(), server.sources());
        require(waitForState(restarted, "ready"), "restart loads cache and reaches ready offline");
        require(restarted.revision() == 1, "restart revision is 1");
        require(restarted.resolve({{"sourceId", "mal:21"}}, {}).value("status").toString() == "mapped",
                "resolves from cache with no server");
    }

    // ── 4. A stale generation refreshes exactly once per process ──────────────
    {
        QTemporaryDir cache;
        // Seed a stale generation with slightly different bytes so the refresh
        // produces a genuinely new generation id.
        const QByteArray staleFribb = fribb + " ";
        seedGeneration(cache.path(), staleFribb, xml, now - 8 * kDayMs);
        LocalServer server(fribb, xml);
        require(server.listen(QHostAddress::LocalHost), "stale server listening");
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "ready"), "stale cache loads to ready");
        require(waitUntil(svc, [&] { return svc.revision() >= 2; }),
                "stale generation refreshes and bumps revision");
        require(server.fribb.hits == 1 && server.xml.hits == 1, "refresh downloads each source once");
        pump(300);
        require(svc.revision() == 2, "refresh happens at most once per process");
    }

    // ── 5. Failure modes never advance current.json or revision ───────────────
    const auto expectNoAdvance = [&](LocalServer& server, const char* label) {
        QTemporaryDir cache;
        require(server.listen(QHostAddress::LocalHost), "failure server listening");
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "error"), label);
        require(svc.revision() == 0, "failed download leaves revision at 0");
        require(!QFile::exists(cache.path() + QStringLiteral("/current.json")),
                "failed download writes no current.json");
        require(svc.resolve({{"sourceId", "mal:21"}}, {}).value("status").toString() == "unavailable",
                "resolve is unavailable after a failed cold start");
    };
    {
        LocalServer s(fribb, xml);
        s.fribb.status = 500;
        expectNoAdvance(s, "http error does not advance");
    }
    {
        // Production https-only rule: a non-https source is refused outright.
        QTemporaryDir cache;
        AnimeOrderService::Sources httpSources;
        httpSources.fribb = QUrl(QStringLiteral("http://127.0.0.1:9/fribb.json"));
        httpSources.mappings = QUrl(QStringLiteral("http://127.0.0.1:9/anime-list.xml"));
        httpSources.allowHttpForTests = false;
        AnimeOrderService svc(&nam, cache.path(), httpSources);
        require(waitForState(svc, "error"), "non-https source is refused (https-only)");
        require(svc.revision() == 0, "https-only rejection leaves revision at 0");
        require(!QFile::exists(cache.path() + QStringLiteral("/current.json")),
                "https-only rejection writes no current.json");
    }
    {
        LocalServer s(fribb, QByteArray(13 * 1024 * 1024, 'x')); // xml body > 12 MiB cap
        expectNoAdvance(s, "oversize xml is refused");
    }
    {
        LocalServer s("{ this is not json", xml);
        expectNoAdvance(s, "malformed json does not advance");
    }
    {
        LocalServer s(fribb, "<not-anime-list/>");
        expectNoAdvance(s, "malformed xml does not advance");
    }

    // ── 6. Hash mismatch on load: corrupt cache ignored, offline → error ──────
    {
        QTemporaryDir cache;
        seedGeneration(cache.path(), fribb, xml, now, /*writePointer=*/true, /*corruptHash=*/true);
        LocalServer server(fribb, xml); // not listening → offline
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "error"), "hash-mismatched cache is ignored and offline load errors");
        require(svc.revision() == 0, "corrupt cache does not install an index");
    }

    // ── 7. Unpointed (interrupted) generation is ignored on restart ───────────
    {
        QTemporaryDir cache;
        seedGeneration(cache.path(), fribb, xml, now, /*writePointer=*/false);
        LocalServer server(fribb, xml); // offline
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "error"), "generation with no pointer is ignored → offline error");
        require(svc.revision() == 0, "unpointed generation installs no index");
    }

    // ── 8. Successful replacement keeps active + previous, prunes older ───────
    {
        QTemporaryDir cache;
        // Three stale generations, current → the newest valid one.
        seedGeneration(cache.path(), fribb + "  ", xml, now - 30 * kDayMs, false); // oldest orphan
        seedGeneration(cache.path(), fribb + " ", xml, now - 20 * kDayMs, false);  // orphan
        const QString prev = seedGeneration(cache.path(), fribb, xml, now - 8 * kDayMs); // active+stale
        require(generationCount(cache.path()) == 3, "three generations seeded");
        LocalServer server(fribb + "\n", xml); // distinct new payload
        require(server.listen(QHostAddress::LocalHost), "prune server listening");
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "ready"), "prune scenario loads to ready");
        const QString fresh = genIdFor(fribb + "\n", xml);
        require(waitUntil(svc,
                          [&] { return svc.revision() >= 2 && hasGeneration(cache.path(), fresh); }),
                "refresh installs a new generation");
        pump(200);
        require(hasGeneration(cache.path(), fresh), "new active generation retained");
        require(hasGeneration(cache.path(), prev), "immediately previous generation retained");
        require(generationCount(cache.path()) == 2, "older generations pruned to active + previous");
    }

    // ── 9. Concurrent refreshIfStale coalesces into a single refresh ──────────
    {
        QTemporaryDir cache;
        // A trailing space keeps the JSON valid but distinct, so the refresh is
        // a genuine new generation rather than a re-parse of the same bytes.
        seedGeneration(cache.path(), fribb + " ", xml, now - 8 * kDayMs);
        LocalServer server(fribb, xml);
        require(server.listen(QHostAddress::LocalHost), "coalesce server listening");
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "ready"), "coalesce cache loads to ready");
        for (int i = 0; i < 8; ++i)
            svc.refreshIfStale();
        require(waitUntil(svc, [&] { return svc.revision() >= 2; }),
                "coalesced refresh completes");
        pump(300);
        require(server.fribb.hits == 1, "concurrent refreshIfStale calls coalesce into one download");
    }

    // ── 9b. Stale cache + failed refresh ends in state=stale, index retained ──
    {
        QTemporaryDir cache;
        seedGeneration(cache.path(), fribb, xml, now - 8 * kDayMs);
        LocalServer server(fribb, xml);
        server.fribb.status = 500;
        require(server.listen(QHostAddress::LocalHost), "stale-fail server listening");
        AnimeOrderService svc(&nam, cache.path(), server.sources());
        require(waitForState(svc, "stale"), "stale cache with failed refresh ends in state=stale");
        require(svc.revision() == 1, "failed refresh keeps the loaded revision");
        require(svc.resolve({{"sourceId", "mal:21"}}, {}).value("status").toString() == "mapped",
                "loaded index is retained after a failed refresh");
    }

    std::cout << "PASS AnimeOrderService cache, refresh, and worker parsing\n";
    return 0;
}
