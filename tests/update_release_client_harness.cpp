#include "update/UpdateReleaseClient.h"

#include "update/UpdateTestPublicKey.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdlib>
#include <iostream>

using namespace Colosseum::Update;

namespace {

constexpr auto kManifestAsset = "colosseum-update-v1.json";
constexpr auto kSignatureAsset = "colosseum-update-v1.json.sig";
constexpr auto kInstallerAsset = "Colosseum-1.1.1-setup.exe";
constexpr auto kInstallerDigest = "fbc5fd97006521785cd1aa58917a4e2999e66d835748400dcb47e1df5e5a8226";
constexpr auto kValidSignature =
    "fd461559eea2e67efa57304b9208399fe515e304a1bed3be9e91a99857cd5196"
    "1e0f49d35c2ad280f20dcef199eb58c37f5ac79b1ff8a839eb9d72e3315dcb0e";
constexpr auto kCrossRepoSignature =
    "b459215e958d343fa59e7aad9677e51a2cedaa9fa0d493bc83292cca32af372a"
    "a2c66456f19d9915cb3f4eac9fb129972dd1af27f51c758d63605e98f7814e00";
constexpr auto kMalformedSignature =
    "4e64c3b97cd9e4f50287b8de45a3d9597994ffaba1ef9576cec5612e026e2b89"
    "c1e928b8e864dcff3000159346ef9a8d3e336122f73283ff61a277ba9a3e610f";

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QByteArray hex(const char* text)
{
    return QByteArray::fromHex(text);
}

QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray validManifest()
{
    return QByteArrayLiteral(
        "{\"schemaVersion\":1,\"version\":\"1.1.1\",\"tag\":\"v1.1.1\","
        "\"eyebrow\":\"A NEW CHAPTER IS READY\",\"title\":\"Colosseum 1.1.1\","
        "\"summary\":\"The house keeps itself current.\",\"installer\":{"
        "\"asset\":\"Colosseum-1.1.1-setup.exe\",\"size\":17,"
        "\"sha256\":\"fbc5fd97006521785cd1aa58917a4e2999e66d835748400dcb47e1df5e5a8226\"},"
        "\"minimumUpdaterVersion\":\"1.1.0\","
        "\"notesUrl\":\"https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.1\","
        "\"highlights\":[],\"artwork\":[]}\n");
}

QByteArray crossRepoManifest()
{
    QByteArray bytes = validManifest();
    bytes.replace("https://github.com/kingoftheseas56/Colosseum",
                  "https://github.com/another-owner/another-repo");
    return bytes;
}

struct ApiOptions {
    QString tag = QStringLiteral("v1.1.1");
    bool draft = false;
    bool prerelease = false;
    bool includeManifest = true;
    bool includeSignature = true;
    bool includeInstaller = true;
    bool duplicateInstaller = false;
    QByteArray installerDigest = QByteArray("fbc5fd97006521785cd1aa58917a4e2999e66d835748400dcb47e1df5e5a8226");
};

class FixtureServer final : public QTcpServer {
public:
    QByteArray latestBody;
    QByteArray manifestBody = validManifest();
    QByteArray signatureBody = hex(kValidSignature);
    QByteArray latestRequest;
    QByteArray manifestRequest;
    QByteArray signatureRequest;
    QByteArray installerRequest;
    int latestStatus = 200;
    bool returnNotModified = false;
    bool holdLatest = false;
    QByteArray redirectLocation;

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

    void rebuildLatest(const ApiOptions& options = {})
    {
        QJsonArray assets;
        const auto addAsset = [&](const QString& name, const QString& path, qint64 size,
                                  const QByteArray& digest) {
            QJsonObject asset;
            asset.insert(QStringLiteral("name"), name);
            asset.insert(QStringLiteral("browser_download_url"), baseUrl() + path);
            asset.insert(QStringLiteral("size"), size);
            asset.insert(QStringLiteral("digest"), QStringLiteral("sha256:")
                + QString::fromLatin1(digest));
            assets.append(asset);
        };
        if (options.includeManifest)
            addAsset(QString::fromLatin1(kManifestAsset), QStringLiteral("/download/manifest"),
                     manifestBody.size(), sha256Hex(manifestBody).toLatin1());
        if (options.includeSignature)
            addAsset(QString::fromLatin1(kSignatureAsset), QStringLiteral("/download/signature"),
                     signatureBody.size(), sha256Hex(signatureBody).toLatin1());
        if (options.includeInstaller)
            addAsset(QString::fromLatin1(kInstallerAsset), QStringLiteral("/download/installer"),
                     17, options.installerDigest);
        if (options.duplicateInstaller)
            addAsset(QString::fromLatin1(kInstallerAsset), QStringLiteral("/download/installer2"),
                     17, options.installerDigest);

        QJsonObject release;
        release.insert(QStringLiteral("tag_name"), options.tag);
        release.insert(QStringLiteral("draft"), options.draft);
        release.insert(QStringLiteral("prerelease"), options.prerelease);
        release.insert(QStringLiteral("assets"), assets);
        latestBody = QJsonDocument(release).toJson(QJsonDocument::Compact);
    }

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
            if (path == "/repos/kingoftheseas56/Colosseum/releases/latest") {
                latestRequest = request;
                if (holdLatest)
                    return;
                if (!redirectLocation.isEmpty()) {
                    writeResponse(socket, 302, {}, {{"Location", redirectLocation}});
                    return;
                }
                if (returnNotModified && request.contains("If-None-Match: \"release-1\"")) {
                    writeResponse(socket, 304, {}, {{"ETag", "\"release-1\""}});
                    return;
                }
                writeResponse(socket, latestStatus, latestBody, {{"ETag", "\"release-1\""}});
            } else if (path == "/download/manifest") {
                manifestRequest = request;
                writeResponse(socket, 200, manifestBody, {});
            } else if (path == "/download/signature") {
                signatureRequest = request;
                writeResponse(socket, 200, signatureBody, {});
            } else if (path == "/download/installer" || path == "/download/installer2") {
                installerRequest = request;
                writeResponse(socket, 200, QByteArrayLiteral("installer-payload"), {});
            } else {
                writeResponse(socket, 404, {}, {});
            }
        });
    }

private:
    static void writeResponse(QTcpSocket* socket, int status, const QByteArray& body,
                              const QList<QPair<QByteArray, QByteArray>>& headers)
    {
        const QByteArray reason = status == 200 ? "OK" : (status == 302 ? "Found" :
            (status == 304 ? "Not Modified" : "Error"));
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

ReleaseClientConfig configFor(const FixtureServer& server)
{
    ReleaseClientConfig config;
    config.latestReleaseUrl = QUrl(server.baseUrl()
        + QStringLiteral("/repos/kingoftheseas56/Colosseum/releases/latest"));
    config.publicKey = QByteArray(
        reinterpret_cast<const char*>(kUpdateTestPublicKey.data()),
        static_cast<qsizetype>(kUpdateTestPublicKey.size()));
    config.allowHttpForTests = true;
    config.timeoutMs = 250;
    return config;
}

ReleaseCheckResult check(QNetworkAccessManager& nam, FixtureServer& server,
                         const QString& priorEtag = {})
{
    UpdateReleaseClient client(&nam, configFor(server));
    ReleaseCheckResult result;
    bool called = false;
    QEventLoop loop;
    client.checkLatest(priorEtag, [&](ReleaseCheckResult value) {
        result = std::move(value);
        called = true;
        loop.quit();
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    require(called, "release check callback completes");
    return result;
}

void expectRejected(QNetworkAccessManager& nam, FixtureServer& server, const char* code)
{
    const ReleaseCheckResult result = check(nam, server);
    require(result.status == ReleaseCheckResult::Status::Rejected, "invalid release is rejected");
    require(result.errorCode == QString::fromLatin1(code), "rejection code is stable");
}

void expectNetworkError(QNetworkAccessManager& nam, FixtureServer& server, const char* code)
{
    const ReleaseCheckResult result = check(nam, server);
    require(result.status == ReleaseCheckResult::Status::NetworkError,
            "transport failure is not an available release");
    require(result.errorCode == QString::fromLatin1(code), "network error code is stable");
}

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("UpdateReleaseClientHarness"));

    QNetworkAccessManager nam;
    FixtureServer server;
    require(server.listen(QHostAddress::LocalHost), "fixture server listens");
    server.rebuildLatest();

    const ReleaseCheckResult valid = check(nam, server);
    require(valid.status == ReleaseCheckResult::Status::Valid, "valid stable release accepted");
    require(valid.manifest.tag == QStringLiteral("v1.1.1"), "manifest tag preserved");
    require(valid.assetUrls.size() == 3, "exact manifest/signature/installer assets selected");
    require(valid.etag == QStringLiteral("\"release-1\""), "ETag captured");
    require(server.latestRequest.contains("Accept: application/vnd.github+json"),
            "GitHub JSON accept header sent");
    require(server.latestRequest.contains("X-Github-Api-Version: 2022-11-28"),
            "GitHub API version header sent");
    require(server.latestRequest.contains("User-Agent: Colosseum/1.1.3"),
            "installed-version user agent sent");

    server.returnNotModified = true;
    const ReleaseCheckResult notModified = check(nam, server, valid.etag);
    require(notModified.status == ReleaseCheckResult::Status::NotModified,
            "matching ETag returns not modified");
    require(server.latestRequest.contains("If-None-Match: \"release-1\""),
            "prior ETag sent on second request");
    server.returnNotModified = false;

    {
        ApiOptions options;
        options.includeManifest = false;
        server.rebuildLatest(options);
        expectRejected(nam, server, "missing_manifest_or_signature");
    }
    {
        ApiOptions options;
        options.includeSignature = false;
        server.rebuildLatest(options);
        expectRejected(nam, server, "missing_manifest_or_signature");
    }
    {
        ApiOptions options;
        options.includeInstaller = false;
        server.rebuildLatest(options);
        expectRejected(nam, server, "missing_installer");
    }
    {
        ApiOptions options;
        options.duplicateInstaller = true;
        server.rebuildLatest(options);
        expectRejected(nam, server, "duplicate_asset_name");
    }
    {
        ApiOptions options;
        options.draft = true;
        server.rebuildLatest(options);
        expectRejected(nam, server, "release_not_stable");
    }
    {
        ApiOptions options;
        options.prerelease = true;
        server.rebuildLatest(options);
        expectRejected(nam, server, "release_not_stable");
    }
    {
        ApiOptions options;
        options.tag = QStringLiteral("v1.2.0");
        server.rebuildLatest(options);
        expectRejected(nam, server, "api_manifest_tag_mismatch");
    }
    {
        ApiOptions options;
        options.installerDigest = QByteArray(64, '0');
        server.rebuildLatest(options);
        expectRejected(nam, server, "api_digest_mismatch");
    }
    {
        server.manifestBody = crossRepoManifest();
        server.signatureBody = hex(kCrossRepoSignature);
        server.rebuildLatest();
        expectRejected(nam, server, "invalid_manifest");
        server.manifestBody = validManifest();
        server.signatureBody = hex(kValidSignature);
        server.rebuildLatest();
    }
    {
        server.manifestBody = QByteArrayLiteral("not-json\n");
        server.signatureBody = hex(kMalformedSignature);
        server.rebuildLatest();
        expectRejected(nam, server, "invalid_manifest");
        server.manifestBody = validManifest();
        server.signatureBody = hex(kValidSignature);
        server.rebuildLatest();
    }
    {
        server.latestBody = QByteArray(2 * 1024 * 1024 + 1, ' ');
        expectRejected(nam, server, "body_too_large");
        server.rebuildLatest();
    }
    {
        server.manifestBody = QByteArray(512 * 1024 + 1, 'x');
        server.rebuildLatest();
        expectRejected(nam, server, "manifest_body_too_large");
        server.manifestBody = validManifest();
        server.rebuildLatest();
    }
    {
        server.signatureBody = QByteArray(129, 'x');
        server.rebuildLatest();
        expectRejected(nam, server, "signature_body_too_large");
        server.signatureBody = hex(kValidSignature);
        server.rebuildLatest();
    }
    {
        server.latestStatus = 503;
        expectNetworkError(nam, server, "network_error");
        server.latestStatus = 200;
        server.rebuildLatest();
    }
    {
        server.redirectLocation = QByteArray("file:///unsafe");
        expectNetworkError(nam, server, "network_error");
        server.redirectLocation.clear();
    }
    {
        server.holdLatest = true;
        expectNetworkError(nam, server, "timeout");
        server.holdLatest = false;
    }
    {
        server.holdLatest = true;
        UpdateReleaseClient* client = new UpdateReleaseClient(&nam, configFor(server));
        bool called = false;
        client->checkLatest({}, [&](ReleaseCheckResult) { called = true; });
        delete client;
        pump(500);
        require(!called, "destroyed client does not invoke a dangling callback");
        server.holdLatest = false;
    }

    std::cout << "UPDATE_RELEASE_CLIENT_OK\n";
}
