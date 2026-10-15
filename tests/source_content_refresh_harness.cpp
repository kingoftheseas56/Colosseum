#include "engine/SourceContentStore.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static int checks = 0;
static void require(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
template<class Predicate> static void until(Predicate predicate) {
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    require(predicate(), "localhost operation completed within five seconds");
}
static QByteArray bytes(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
static QString digest(const QByteArray& value) { return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex()); }
static QString title(int version) { return QStringLiteral("Published generation %1").arg(version); }
static QStringList documentKeys() {
    return {"universes-roster", "universe-one-piece", "universe-dcau", "universe-star-wars",
        "tribute-dcau", "tribute-star-wars", "tribute-one-piece-east-blue", "tribute-one-piece-paradise",
        "tribute-one-piece-new-world", "tribute-one-piece-final-sea"};
}
static QString artworkHash(int version) { return digest(QByteArray("artwork-") + QByteArray::number(version)); }
static QJsonObject bundle(int version, bool malformed) {
    const auto revision = digest(title(version).toUtf8());
    const QJsonObject data{{"title",title(version)}, {"version",version},
        {"universes",QJsonArray{QJsonObject{{"name","One Piece"}}}}, {"archive",QJsonArray{}}, {"shelfOverlays",QJsonObject{}},
        {"universe",QJsonObject{{"id","test"},{"title",title(version)}, {"sections",QJsonArray{QJsonObject{{"id","screen"},{"title","Pinned works"},{"kind","video"},
            {"entries",QJsonArray{QJsonObject{{"id","tt0388629"},{"type","series"},{"title","One Piece"}}}}}}}}},
        {"story",QJsonObject{{"arcs",QJsonArray{QJsonObject{{"id","romance"},{"title",title(version)}}}}}},
        {"atlas",QJsonObject{}},{"artByArc",QJsonObject{}},
        {"hubs",QJsonArray{QJsonObject{{"id","gotham"}}}},
        {"destinations",QJsonArray{QJsonObject{{"id","high"}}}}, {"skywalkerDestination",QJsonObject{{"id","skywalker"}}}};
    const auto raw = bytes(QJsonObject{{"schemaVersion",1},{"revision",revision},{"data",data}});
    QJsonObject descriptions, documents;
    for (const auto& key : documentKeys()) {
        descriptions.insert(key, QJsonObject{{"revision",revision},{"sha256",digest(raw)}});
        documents.insert(key, QString::fromUtf8(raw));
    }
    if (malformed) documents.insert("tribute-one-piece-final-sea", "{}");
    const auto art = artworkHash(version);
    const QJsonObject assets{{"assets/universes/test.png",QJsonObject{{"path","/assets/content/"+art+".png"},{"sha256",art},{"bytes",100}}}};
    return {{"manifest",QJsonObject{{"schemaVersion",1},{"revision",revision},{"documents",descriptions},{"assets",assets}}}, {"documents",documents}};
}
static QByteArray read(const QString& path) { QFile file(path); require(file.open(QIODevice::ReadOnly), "durable cache readable"); return file.readAll(); }

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary; require(temporary.isValid(), "isolated cache directory");
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    QTcpServer server; require(server.listen(QHostAddress::LocalHost), "bounded localhost fixture listens");
    int published = 1, sessions = 0, bundleRequests = 0;
    bool malformed = false;
    QObject::connect(&server, &QTcpServer::newConnection, &application, [&] {
        while (server.hasPendingConnections()) {
            auto* socket = server.nextPendingConnection();
            const auto received = QSharedPointer<QByteArray>::create();
            const auto handled = QSharedPointer<bool>::create(false);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, received, handled] {
                if (*handled) return;
                received->append(socket->readAll());
                require(received->size() <= 32768, "fixture request bounded to 32 KiB");
                const auto separator = received->indexOf("\r\n\r\n"); if (separator < 0) return;
                const auto header = received->left(separator); int length = 0;
                for (const auto& line : header.split('\n')) if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (received->size() < separator + 4 + length) return;
                *handled = true;
                const auto path = header.split(' ').value(1);
                if(path.startsWith("/catalogue/")){socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return;}
                const auto request = QJsonDocument::fromJson(received->mid(separator + 4, length)).object();
                QJsonObject response;
                if (path == "/v1/guest/session") {
                    ++sessions;
                    require(request == QJsonObject{{"appId","colosseum"},{"apiVersion",1}}, "real owner sends guest lease handshake");
                    response = {{"token","localhost-guest"},{"expiresAt",QDateTime::currentDateTimeUtc().addSecs(300).toString(Qt::ISODate)}};
                } else {
                    require(path == "/v1/catalogue/content/bundle", "only the content RPC requested");
                    require(header.contains("Authorization: Bearer localhost-guest"), "bundle uses issued guest lease");
                    require(request.value("args").isArray() && request.value("args").toArray().isEmpty(), "bundle request carries no SQL or bulk database request");
                    ++bundleRequests;
                    response = {{"apiVersion",1},{"revision",digest(title(published).toUtf8())},{"method","bundle"},{"result",bundle(published,malformed)}};
                }
                const auto body = bytes(response);
                const auto wire = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                    + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
                socket->write(wire); socket->disconnectFromHost();
            });
        }
    });
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    const auto cache = temporary.filePath("content-cache.json");
    SourceContentStore owner;
    int generationSignals = 0, documentSignals = 0;
    const auto coherent = [&] {
        const int version = owner.document("universes-roster").value("version").toInt();
        for (const auto& key : documentKeys())
            require(owner.document(key).value("version").toInt() == version, "every document visible from signal belongs to one generation");
        require(owner.assetUrl("../assets/universes/test.png").endsWith(artworkHash(version)+".png"), "image manifest and document generation visible atomically");
    };
    QObject::connect(&owner, &SourceContentStore::documentChanged, &application, [&](const QString&) { ++documentSignals; coherent(); });
    QObject::connect(&owner, &SourceContentStore::generationChanged, &application, [&] { ++generationSignals; coherent(); });
    owner.configure(&network, endpoint, cache, {});
    until([&] { return owner.document("universes-roster").value("version").toInt() == 1; });
    require(owner.generation() == 1 && generationSignals == 1 && documentSignals == 10, "first publication installs once after guest handshake");
    const auto firstUrl = owner.assetUrl("../assets/universes/test.png");
    published = 2;
    owner.refresh();
    require(owner.document("universes-roster").value("version").toInt() == 1, "old generation remains visible during asynchronous refresh");
    until([&] { return owner.document("universes-roster").value("version").toInt() == 2; });
    require(owner.generation() == 2 && generationSignals == 2 && documentSignals == 20, "same owner refreshes all documents without application restart");
    require(owner.assetUrl("../assets/universes/test.png") != firstUrl, "published artwork change produces new immutable URL");
    const auto validCache = read(cache);
    published = 3; malformed = true;
    owner.refresh();
    until([&] { return !owner.lastError().isEmpty(); });
    require(owner.lastError() == "INVALID_RESULT", "malformed publication rejected before transport persistence");
    require(owner.generation() == 2 && generationSignals == 2 && documentSignals == 20, "malformed generation emits no partial updates");
    require(owner.document("universes-roster").value("version").toInt() == 2, "last valid live generation retained");
    require(read(cache) == validCache, "malformed publication leaves persisted last valid cache unchanged");
    require(sessions == 1 && bundleRequests == 3, "one guest lease and three bounded bundle requests");
    SourceContentStore restored;
    restored.configure(nullptr, endpoint, cache, {});
    require(restored.document("universes-roster").value("version").toInt() == 2, "new offline owner restores persisted generation two");
    require(restored.assetUrl("../assets/universes/test.png") == owner.assetUrl("../assets/universes/test.png"), "offline restoration retains generation two artwork URL");
    require(restored.lastError().isEmpty(), "restored last valid cache healthy");
    std::printf("SourceContentStore live refresh: %d checks PASS (same process v1 -> v2, v3 rejection, offline restore)\n", checks);
    return 0;
}
