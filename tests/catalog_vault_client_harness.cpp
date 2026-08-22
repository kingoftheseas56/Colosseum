// CatalogVaultClient contract harness (data-vault adoption, Slice 1, 2026-08-22). A local
// QTcpServer fixture stands in for the real `kingoftheseas56/Colosseum-Data` GitHub release
// (`GET /repos/.../releases/latest` + `browser_download_url` asset bytes) — the harness NEVER
// touches the live network. Proves: throttled zero-network reuse under 24h, full-vault cold
// fetch, upstream-tag re-download with the aboutToReplace live-swap hook firing before each
// pre-existing target is replaced, silent cache-keep on manifest failure with a full vault,
// fetchFailed on manifest failure with an empty vault, and clean `.downloading` temp cleanup
// on a truncated download.
//
// Slice 2 (2026-08-22) added setManagedNames(): cases (g)/(h) prove a managed-names filter
// downloads only the named subset, and an explicitly-empty managed set is a total no-op
// (zero network, immediate allFresh) — the shape main.cpp relies on when every catalog
// resolves via its dev-machine override.
//
// House convention: require() prints "FAIL: <msg>" and exits 1; one PASS line per case;
// sentinel CATALOG_VAULT_CLIENT_OK on success.
#include "engine/CatalogVaultClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QVector>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void pass(const char* label)
{
    std::cout << "PASS: " << label << '\n';
}

const QStringList& knownNames()
{
    static const QStringList names{
        QStringLiteral("mal_catalog.db"),
        QStringLiteral("tankoban_catalog.db"),
        QStringLiteral("comics_catalog.db"),
        QStringLiteral("imdb_catalog.db"),
    };
    return names;
}

QHash<QString, QByteArray> fixtureBytesFor(const QString& tag)
{
    QHash<QString, QByteArray> map;
    map.insert(QStringLiteral("mal_catalog.db"), QByteArray("MAL-") + tag.toUtf8() + QByteArray(60, 'm'));
    map.insert(QStringLiteral("tankoban_catalog.db"), QByteArray("TANK-") + tag.toUtf8() + QByteArray(60, 't'));
    map.insert(QStringLiteral("comics_catalog.db"), QByteArray("COMX-") + tag.toUtf8() + QByteArray(60, 'c'));
    map.insert(QStringLiteral("imdb_catalog.db"), QByteArray("IMDB-") + tag.toUtf8() + QByteArray(60, 'i'));
    return map;
}

// ── Local fake GitHub release server ────────────────────────────────────────────────
class FixtureServer final : public QTcpServer {
public:
    QString tag = QStringLiteral("v1.0.0");
    QHash<QString, QByteArray> assetBytes;
    QString truncateName; // non-empty: that asset's download is cut mid-stream
    int requestCount = 0;

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            socket->deleteLater();
            return;
        }
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll();
            if (!request.contains("\r\n\r\n"))
                return;
            const int lineEnd = request.indexOf("\r\n");
            const QByteArray requestLine = lineEnd >= 0 ? request.left(lineEnd) : request;
            const QList<QByteArray> parts = requestLine.split(' ');
            const QByteArray path = parts.size() > 1 ? parts.at(1) : QByteArray{};
            ++requestCount;

            if (path == "/releases/latest") {
                writeResponse(socket, 200, buildLatestJson(), {});
            } else if (path.startsWith("/download/")) {
                const QString name = QString::fromUtf8(path.mid(int(strlen("/download/"))));
                const QByteArray full = assetBytes.value(name);
                if (!truncateName.isEmpty() && name == truncateName) {
                    const QByteArray partial = full.left(full.size() / 2);
                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Length: "
                        + QByteArray::number(full.size()) + "\r\nConnection: close\r\n\r\n" + partial;
                    socket->write(response);
                    socket->flush();
                    socket->disconnectFromHost();
                } else {
                    writeResponse(socket, 200, full, {});
                }
            } else {
                writeResponse(socket, 404, {}, {});
            }
        });
    }

private:
    QByteArray buildLatestJson() const
    {
        QJsonArray assets;
        for (auto it = assetBytes.constBegin(); it != assetBytes.constEnd(); ++it) {
            QJsonObject asset;
            asset.insert(QStringLiteral("name"), it.key());
            asset.insert(QStringLiteral("browser_download_url"),
                        baseUrl() + QStringLiteral("/download/") + it.key());
            asset.insert(QStringLiteral("size"), it.value().size());
            assets.append(asset);
        }
        QJsonObject root;
        root.insert(QStringLiteral("tag_name"), tag);
        root.insert(QStringLiteral("assets"), assets);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    static void writeResponse(QTcpSocket* socket, int status, const QByteArray& body,
                              const QList<QPair<QByteArray, QByteArray>>& headers)
    {
        const QByteArray reason = status == 200 ? "OK" : "Not Found";
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
        for (const auto& header : headers)
            response += header.first + ": " + header.second + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size())
            + "\r\nConnection: close\r\n\r\n" + body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }
};

// ── Client-run harness: drives one checkAndFetch() pass to completion ──────────────
struct RunResult {
    bool allFreshCalled = false;
    QString allFreshTag;
    QStringList failedNames;
    QStringList failedErrors;
    QStringList events; // "about:<name>" / "updated:<name>" in emission order
    QList<bool> fetchingSequence;
};

RunResult runWithManaged(QNetworkAccessManager& nam, const QString& vaultDir, const QString& apiBase,
                         const QStringList* managedNames, int idleTimeoutMs = 1200)
{
    CatalogVaultClient client(&nam, vaultDir, apiBase);
    if (managedNames)
        client.setManagedNames(*managedNames);
    RunResult r;
    QObject::connect(&client, &CatalogVaultClient::allFresh, [&](QString tag) {
        r.allFreshCalled = true;
        r.allFreshTag = tag;
    });
    QObject::connect(&client, &CatalogVaultClient::databaseUpdated, [&](QString name, QString) {
        r.events << (QStringLiteral("updated:") + name);
    });
    QObject::connect(&client, &CatalogVaultClient::fetchFailed, [&](QString name, QString error) {
        r.failedNames << name;
        r.failedErrors << error;
    });
    QObject::connect(&client, &CatalogVaultClient::aboutToReplace, [&](QString name) {
        r.events << (QStringLiteral("about:") + name);
    });
    QObject::connect(&client, &CatalogVaultClient::fetchingChanged,
                     [&] { r.fetchingSequence << client.isFetching(); });

    QEventLoop loop;
    QObject::connect(&client, &CatalogVaultClient::allFresh, &loop, &QEventLoop::quit);
    QObject::connect(&client, &CatalogVaultClient::fetchFailed, &loop, &QEventLoop::quit);
    QTimer::singleShot(idleTimeoutMs, &loop, &QEventLoop::quit);

    client.checkAndFetch();
    // The throttle path (zero-network reuse) emits allFresh synchronously before
    // checkAndFetch() returns — only pump the loop if nothing terminal fired yet.
    if (!r.allFreshCalled && r.failedNames.isEmpty())
        loop.exec();

    return r;
}

RunResult run(QNetworkAccessManager& nam, const QString& vaultDir, const QString& apiBase,
             int idleTimeoutMs = 1200)
{
    return runWithManaged(nam, vaultDir, apiBase, nullptr, idleTimeoutMs);
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    require(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture file opens for writing");
    f.write(bytes);
}

void writeStateJson(const QString& vaultDir, const QString& tag, const QDateTime& fetchedAtUtc,
                    const QHash<QString, qint64>& sizes)
{
    QJsonObject assets;
    for (auto it = sizes.constBegin(); it != sizes.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("size"), it.value());
        assets.insert(it.key(), entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("tag"), tag);
    root.insert(QStringLiteral("fetchedAt"), fetchedAtUtc.toString(Qt::ISODate));
    root.insert(QStringLiteral("assets"), assets);
    writeFile(vaultDir + QStringLiteral("/state.json"),
             QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool anyDownloadingResidue(const QString& vaultDir)
{
    const QStringList entries =
        QDir(vaultDir).entryList(QStringList{QStringLiteral("*.downloading")}, QDir::Files);
    return !entries.isEmpty();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QNetworkAccessManager nam;
    FixtureServer server;
    require(server.listen(QHostAddress::LocalHost), "fixture server listens");

    // ── (a) empty vault → full cold fetch ───────────────────────────────────────
    QTemporaryDir dirA;
    require(dirA.isValid(), "temp vault dir A created");
    server.tag = QStringLiteral("v1.0.0");
    server.assetBytes = fixtureBytesFor(server.tag);
    server.truncateName.clear();

    const RunResult a = run(nam, dirA.path(), server.baseUrl());
    require(a.allFreshCalled && a.allFreshTag == QStringLiteral("v1.0.0"), "(a) allFresh(v1.0.0) emitted");
    int updatedCountA = 0;
    for (const QString& e : a.events)
        if (e.startsWith(QStringLiteral("updated:")))
            ++updatedCountA;
    require(updatedCountA == 4, "(a) all four databases reported updated");
    for (const QString& name : knownNames()) {
        QFile f(dirA.path() + QLatin1Char('/') + name);
        require(f.open(QIODevice::ReadOnly), "(a) downloaded db file opens");
        require(f.readAll() == server.assetBytes.value(name), "(a) downloaded bytes match fixture");
    }
    {
        QFile state(dirA.path() + QStringLiteral("/state.json"));
        require(state.open(QIODevice::ReadOnly), "(a) state.json written");
        const QJsonObject root = QJsonDocument::fromJson(state.readAll()).object();
        require(root.value(QStringLiteral("tag")).toString() == QStringLiteral("v1.0.0"),
                "(a) state.json carries the fetched tag");
        require(root.value(QStringLiteral("assets")).toObject().size() == 4,
                "(a) state.json carries all four asset sizes");
    }
    require(a.fetchingSequence.size() == 2 && a.fetchingSequence.first() == true
                && a.fetchingSequence.last() == false,
            "(a) fetching flipped true then false");
    pass("(a) empty vault cold fetch");

    // ── (b) fresh state + files present → zero network ──────────────────────────
    server.requestCount = 0;
    const RunResult b = run(nam, dirA.path(), server.baseUrl());
    require(b.allFreshCalled && b.allFreshTag == QStringLiteral("v1.0.0"), "(b) allFresh emitted from cache");
    require(server.requestCount == 0, "(b) throttle makes zero network requests");
    require(b.fetchingSequence.isEmpty(), "(b) fetching never toggled — no network happened");
    pass("(b) fresh state throttles to zero network");

    // ── (c) new upstream tag past the 24h throttle → re-download + live-swap hook ─
    QTemporaryDir dirC;
    require(dirC.isValid(), "temp vault dir C created");
    const QHash<QString, QByteArray> v1Bytes = fixtureBytesFor(QStringLiteral("v1.0.0"));
    QHash<QString, qint64> v1Sizes;
    for (const QString& name : knownNames()) {
        writeFile(dirC.path() + QLatin1Char('/') + name, v1Bytes.value(name));
        v1Sizes.insert(name, v1Bytes.value(name).size());
    }
    writeStateJson(dirC.path(), QStringLiteral("v1.0.0"),
                  QDateTime::currentDateTimeUtc().addSecs(-25 * 3600), v1Sizes);

    server.tag = QStringLiteral("v2.0.0");
    server.assetBytes = fixtureBytesFor(server.tag);
    const RunResult c = run(nam, dirC.path(), server.baseUrl());
    require(c.allFreshCalled && c.allFreshTag == QStringLiteral("v2.0.0"), "(c) allFresh(v2.0.0) emitted");
    int updatedCountC = 0;
    for (const QString& e : c.events)
        if (e.startsWith(QStringLiteral("updated:")))
            ++updatedCountC;
    require(updatedCountC == 4, "(c) all four databases re-downloaded on tag change");
    for (const QString& name : knownNames()) {
        QFile f(dirC.path() + QLatin1Char('/') + name);
        require(f.open(QIODevice::ReadOnly), "(c) re-downloaded db file opens");
        require(f.readAll() == server.assetBytes.value(name), "(c) file now holds the v2 bytes");
        const int aboutIdx = c.events.indexOf(QStringLiteral("about:") + name);
        const int updatedIdx = c.events.indexOf(QStringLiteral("updated:") + name);
        require(aboutIdx >= 0 && updatedIdx > aboutIdx,
                "(c) aboutToReplace fired before its replacement, per name");
    }
    require(!anyDownloadingResidue(dirC.path()), "(c) no .downloading residue after success");
    pass("(c) new upstream tag re-downloads with live-swap hook ordering");

    // ── (d) manifest unreachable + full local cache → silent cache-keep ─────────
    QTemporaryDir dirD;
    require(dirD.isValid(), "temp vault dir D created");
    const QHash<QString, QByteArray> cacheBytes = fixtureBytesFor(QStringLiteral("cached"));
    for (const QString& name : knownNames())
        writeFile(dirD.path() + QLatin1Char('/') + name, cacheBytes.value(name));
    // No state.json — forces a network check despite the full cache.
    server.close();
    const RunResult d = run(nam, dirD.path(), server.baseUrl(), 600);
    require(!d.allFreshCalled, "(d) no allFresh when the manifest is unreachable");
    require(d.failedNames.isEmpty(), "(d) no fetchFailed when the full cache covers it");
    for (const QString& name : knownNames()) {
        QFile f(dirD.path() + QLatin1Char('/') + name);
        require(f.open(QIODevice::ReadOnly), "(d) cached db file still present");
        require(f.readAll() == cacheBytes.value(name), "(d) cached db file bytes untouched");
    }
    pass("(d) unreachable manifest + full cache keeps cache silently");

    // ── (e) manifest unreachable + empty vault → fetchFailed, no state.json ─────
    QTemporaryDir dirE;
    require(dirE.isValid(), "temp vault dir E created");
    const RunResult e = run(nam, dirE.path(), server.baseUrl(), 600);
    require(!e.allFreshCalled, "(e) no allFresh when nothing is cached and network fails");
    require(e.failedNames == QStringList{QStringLiteral("manifest")},
            "(e) fetchFailed(\"manifest\", ...) emitted");
    require(!QFile::exists(dirE.path() + QStringLiteral("/state.json")),
            "(e) no state.json written on manifest failure");
    pass("(e) unreachable manifest + empty vault fails loudly");

    require(server.listen(QHostAddress::LocalHost), "fixture server re-listens for (f)");

    // ── (f) truncated download mid-stream → temp cleaned, target absent, fetchFailed ─
    QTemporaryDir dirF;
    require(dirF.isValid(), "temp vault dir F created");
    server.tag = QStringLiteral("v3.0.0");
    server.assetBytes = fixtureBytesFor(server.tag);
    server.truncateName = QStringLiteral("comics_catalog.db");

    const RunResult f = run(nam, dirF.path(), server.baseUrl());
    require(f.failedNames == QStringList{QStringLiteral("comics_catalog.db")},
            "(f) fetchFailed for the truncated asset");
    require(!QFile::exists(dirF.path() + QStringLiteral("/comics_catalog.db")),
            "(f) truncated target was never landed");
    require(!anyDownloadingResidue(dirF.path()), "(f) .downloading temp cleaned up");
    require(!QFile::exists(dirF.path() + QStringLiteral("/state.json")),
            "(f) state.json left unwritten after a mid-pass failure");
    server.truncateName.clear();
    pass("(f) truncated download cleans up and fails loudly");

    // ── (g) setManagedNames filters which assets are ever fetched (Slice 2, 2026-08-22) ─
    // server is still listening from (f) — no re-listen needed here.
    QTemporaryDir dirG;
    require(dirG.isValid(), "temp vault dir G created");
    server.tag = QStringLiteral("v4.0.0");
    server.assetBytes = fixtureBytesFor(server.tag);
    server.requestCount = 0;
    const QStringList managed{QStringLiteral("mal_catalog.db"), QStringLiteral("imdb_catalog.db")};
    const RunResult g = runWithManaged(nam, dirG.path(), server.baseUrl(), &managed);
    require(g.allFreshCalled && g.allFreshTag == QStringLiteral("v4.0.0"),
            "(g) allFresh emitted for the managed-subset fetch");
    int updatedCountG = 0;
    for (const QString& e : g.events)
        if (e.startsWith(QStringLiteral("updated:")))
            ++updatedCountG;
    require(updatedCountG == 2, "(g) only the two managed names were downloaded");
    require(QFile::exists(dirG.path() + QStringLiteral("/mal_catalog.db")),
            "(g) managed name mal_catalog.db landed on disk");
    require(QFile::exists(dirG.path() + QStringLiteral("/imdb_catalog.db")),
            "(g) managed name imdb_catalog.db landed on disk");
    require(!QFile::exists(dirG.path() + QStringLiteral("/tankoban_catalog.db")),
            "(g) unmanaged name tankoban_catalog.db was never fetched");
    require(!QFile::exists(dirG.path() + QStringLiteral("/comics_catalog.db")),
            "(g) unmanaged name comics_catalog.db was never fetched");
    pass("(g) setManagedNames restricts the fetch to a named subset");

    // ── (h) setManagedNames({}) manages nothing — zero network, immediate allFresh ─────
    QTemporaryDir dirH;
    require(dirH.isValid(), "temp vault dir H created");
    server.requestCount = 0;
    const QStringList none;
    const RunResult h = runWithManaged(nam, dirH.path(), server.baseUrl(), &none, 600);
    require(h.allFreshCalled, "(h) allFresh emitted immediately with an empty managed set");
    require(server.requestCount == 0, "(h) empty managed set makes zero network requests");
    pass("(h) setManagedNames({}) is a total no-op fetch");

    std::cout << "CATALOG_VAULT_CLIENT_OK\n";
    return 0;
}
