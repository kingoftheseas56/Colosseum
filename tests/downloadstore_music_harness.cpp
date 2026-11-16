#include "player/downloadstore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <functional>
#include <cstdio>

namespace {
// Transfer fixture only: these exact bytes do not claim decoder/playback coverage.
const QByteArray audioBytes = QByteArray::fromHex("00000018667479704d344120000000004d34412069736f6d") + QByteArray("vinyl-music-download-fixture");
const QString scopeA = QStringLiteral("music-fixture-profile-A");
const QString scopeB = QStringLiteral("music-fixture-profile-B");
const QString videoA = QStringLiteral("MusicTest01");
const QString videoB = QStringLiteral("MusicTest02");

bool require(bool condition, const char *message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool until(const std::function<bool()> &predicate, int timeoutMs = 4000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs) {
        QEventLoop turn;
        QTimer::singleShot(10, &turn, &QEventLoop::quit);
        turn.exec();
    }
    return predicate();
}

void settle() {
    // Bounded negative-observation window: allow queued network work to reach loopback.
    QEventLoop turn;
    QTimer::singleShot(100, &turn, &QEventLoop::quit);
    turn.exec();
}

QVariantMap album(const QString &video) {
    const QVariantMap track{{"id", "ytm:track:" + video}, {"videoId", video},
        {"title", QStringLiteral("Exact fixture song")}, {"artist", QStringLiteral("Fixture artist")},
        {"artists", QStringList{QStringLiteral("Fixture artist")}}, {"seconds", 180}, {"available", true}};
    return {{"id", "ytm:album:" + video}, {"browseId", "MPREb_" + video},
        {"origin", QStringLiteral("youtube-music")}, {"title", QStringLiteral("Fixture album")},
        {"artists", QStringList{QStringLiteral("Fixture artist")}}, {"year", QStringLiteral("2026")},
        {"cover", QString()}, {"tracks", QVariantList{track}}};
}

struct ByteServer {
    QTcpServer server;
    int requests = 0;
    bool allHeaders = true;
    ByteServer() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (socket->property("answered").toBool()) return;
                    QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n")) return;
                    socket->setProperty("answered", true);
                    ++requests;
                    const bool headers = request.toLower().contains("x-vinyl-fixture: exact-audio\r\n");
                    allHeaders = allHeaders && headers;
                    socket->write(QByteArray(headers ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 403 Forbidden\r\n")
                        + "Content-Type: audio/mp4\r\nContent-Length: " + QByteArray::number(audioBytes.size())
                        + "\r\nConnection: close\r\n\r\n" + audioBytes);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    QVariantMap resource(const QString &video) const {
        return {{"videoId", video}, {"audioOnly", true}, {"extension", QStringLiteral("m4a")},
            {"url", QStringLiteral("http://127.0.0.1:%1/exact-audio.m4a").arg(server.serverPort())},
            {"headers", QVariantMap{{"X-Vinyl-Fixture", QStringLiteral("exact-audio")}}}};
    }
};

struct Resolution {
    QString id, track, medium;
    int count = 0;
    void observe(DownloadStore &store) {
        QObject::connect(&store, &DownloadStore::needResolve, &store,
            [this](const QString &job, const QString &stream, const QString &media) {
                id = job; track = stream; medium = media; ++count;
            });
    }
};

// Each case owns a UUID application test root. Remove only its explicitly known files,
// then empty directories; never recursively traverse a store or follow a link for cleanup.
struct OwnedRoot {
    QString root;
    explicit OwnedRoot(const QString &name) {
        QCoreApplication::setApplicationName(name);
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    ~OwnedRoot() {
        if (root.isEmpty() || !QDir::isAbsolutePath(root)
            || QFileInfo(root).isSymLink() || QFileInfo(root).isJunction()
            || QFileInfo(root).fileName() != QCoreApplication::applicationName()) return;
        for (const auto &path : files) QFile::remove(path);
        for (const auto &path : directories) QDir().rmdir(path);
        QFile::remove(root + "/videos/index.json");
        QFile::remove(root + "/videos/queue.json");
        QDir().rmdir(root + "/videos");
        QDir().rmdir(root + "/music/downloads");
        QDir().rmdir(root + "/music");
        QDir().rmdir(root);
    }
    QStringList files, directories;
    void remember(const DownloadStore &store, const QString &scope, const QString &video) {
        const QString directory = store.musicDownloadDir(scope);
        const QString path = directory + '/' + video + ".m4a";
        files << path << path + ".part";
        if (!directories.contains(directory)) directories << directory;
    }
};

bool completionRestart(const QString &name, ByteServer &server) {
    OwnedRoot owned(name);
    const auto original = album(videoA);
    QString completedPath;
    {
        DownloadStore store;
        store.setMusicProfileScope(scopeA);
        owned.remember(store, scopeA, videoA);
        Resolution requested;
        requested.observe(store);
        const QString id = store.enqueueMusic(original, 0, scopeA);
        if (!require(!id.isEmpty() && requested.id == id && requested.track == "ytm:track:" + videoA
            && requested.medium == "music", "music resolution must bind original track id and medium")) return false;
        if (!require(store.downloadedMusic(scopeA).isEmpty(), "unresolved job must not appear completed")) return false;
        store.feedMusicSource(id, server.resource(videoA));
        if (!require(until([&] { return store.downloadedMusic(scopeA).size() == 1; }), "music transfer did not complete")) return false;
        const auto row = store.downloadedMusic(scopeA).first().toMap();
        completedPath = row.value("path").toString();
        if (!require(QDir::fromNativeSeparators(completedPath) == store.musicDownloadDir(scopeA) + '/' + videoA + ".m4a",
            "music output must use the exact profile AppData directory, never system Downloads")) return false;
        QFile bytes(completedPath);
        if (!require(bytes.open(QIODevice::ReadOnly) && bytes.readAll() == audioBytes && server.allHeaders,
            "completed music must contain exact bytes and custom request headers")) return false;
        const auto metadata = row.value("music").toMap();
        if (!require(row.value("kind") == "music" && row.value("available").toBool()
            && row.value("bytes").toLongLong() == audioBytes.size() && row.value("addedAt").toLongLong() > 0
            && QJsonDocument::fromVariant(metadata.value("album")) == QJsonDocument::fromVariant(original) && metadata.value("trackIndex").toInt() == 0
            && metadata.value("videoId") == videoA && store.downloadedVideos().isEmpty(),
            "music index must retain owner metadata and exclude video projection")) return false;
    }
    {
        DownloadStore restarted;
        restarted.setMusicProfileScope(scopeA);
        const auto resource = restarted.musicResource(scopeA, videoA);
        if (!require(restarted.downloadedMusic(scopeA).size() == 1
            && QUrl(resource.value("url").toString()).isLocalFile()
            && QUrl(resource.value("url").toString()).toLocalFile() == QDir::fromNativeSeparators(completedPath)
            && resource.value("videoId") == videoA && resource.value("qualityLabel") == "Downloaded"
            && resource.value("headers").toMap().isEmpty()
            && !resource.contains("trackId") && !resource.contains("provider"),
            "restart must restore exact provider-video file resource without imported rewrite")) return false;
        if (!require(restarted.downloadedMusic(scopeB).isEmpty() && restarted.musicResource(scopeB, videoA).isEmpty()
            && restarted.musicResource(scopeA, videoB).isEmpty()
            && restarted.enqueueMusic(album(videoB), 0, scopeB).isEmpty(),
            "other scope cannot see resource or enqueue while inactive")) return false;
        restarted.setMusicProfileScope(scopeB);
        if (!require(restarted.downloadedMusic(scopeA).isEmpty() && restarted.musicResource(scopeA, videoA).isEmpty()
            && restarted.enqueueMusic(album(videoB), 0, scopeA).isEmpty(),
            "inactive profile cannot expose completed files or enqueue")) return false;
        restarted.setMusicProfileScope(scopeA);
        if (!require(QFile::remove(completedPath) && restarted.downloadedMusic(scopeA).isEmpty()
            && restarted.musicResource(scopeA, videoA).isEmpty(), "missing file must not remain available")) return false;
    }
    return true;
}

bool profileFence(const QString &name, ByteServer &server) {
    OwnedRoot owned(name);
    DownloadStore store;
    store.setMusicProfileScope(scopeA);
    owned.remember(store, scopeA, videoA);
    owned.remember(store, scopeA, videoB);
    Resolution requested;
    requested.observe(store);
    const QString first = store.enqueueMusic(album(videoA), 0, scopeA);
    const QString second = store.enqueueMusic(album(videoB), 0, scopeA);
    if (!require(!first.isEmpty() && !second.isEmpty() && first != second && requested.count == 1,
        "one resolving job must retain second queued music item")) return false;
    const int before = server.requests;
    store.setMusicProfileScope(scopeB);
    store.feedMusicSource(first, server.resource(videoA));
    settle();
    if (!require(server.requests == before && store.downloadedMusic(scopeA).isEmpty()
        && store.downloadedMusic(scopeB).isEmpty(), "stale prior-profile feed must perform no transfer")) return false;
    store.setMusicProfileScope(scopeA);
    if (!require(until([&] { return requested.count == 2 && requested.id == first; }),
        "returning profile must re-resolve its original pending job")) return false;
    store.feedMusicSource(first, server.resource(videoA));
    if (!require(until([&] { return store.downloadedMusic(scopeA).size() == 1 && requested.id == second; }),
        "queued profile item must resume only after first completes")) return false;
    if (!require(requested.track == "ytm:track:" + videoB && requested.medium == "music",
        "second job resolution must retain its own exact track")) return false;
    store.feedMusicSource(second, server.resource(videoB));
    return require(until([&] { return store.downloadedMusic(scopeA).size() == 2; }), "second music job did not complete");
}

bool rejectedResource(const QString &name, ByteServer &server, int variant) {
    OwnedRoot owned(name);
    DownloadStore store;
    store.setMusicProfileScope(scopeA);
    owned.remember(store, scopeA, videoA);
    const QString id = store.enqueueMusic(album(videoA), 0, scopeA);
    if (!require(!id.isEmpty(), "rejection fixture must begin as valid pending music")) return false;
    auto resource = server.resource(videoA);
    if (variant == 0) resource.remove("audioOnly");
    if (variant == 1) resource["videoId"] = videoB;
    if (variant == 2) resource["extension"] = QStringLiteral("exe");
    const int before = server.requests;
    store.feedMusicSource(id, resource);
    settle();
    const QString output = store.musicDownloadDir(scopeA) + '/' + videoA;
    return require(server.requests == before && store.downloadedMusic(scopeA).isEmpty()
        && !QFileInfo::exists(output + ".m4a") && !QFileInfo::exists(output + ".m4a.part")
        && !QFileInfo::exists(output + ".exe") && !QFileInfo::exists(output + ".exe.part"),
        "malformed audio resource must not issue HTTP or create a payload");
}

bool outsidePath(const QString &name, ByteServer &server) {
    OwnedRoot owned(name);
    DownloadStore store;
    store.setMusicProfileScope(scopeA);
    const QString outside = owned.root + "/outside-music.m4a";
    owned.files << outside << outside + ".part";
    const QString id = "music-" + QString::fromLatin1(QCryptographicHash::hash(
        scopeA.toUtf8() + QByteArray(1, '\0') + videoA.toUtf8(), QCryptographicHash::Sha256).toHex());
    const int before = server.requests;
    Resolution requested;
    requested.observe(store);
    // enqueueMusic intentionally has no custom path; exercise the same queue's full request API.
    QVariantMap request{{"id", id}, {"kind", QStringLiteral("music")}, {"profileScope", scopeA},
        {"music", QVariantMap{{"album", album(videoA)}, {"trackIndex", 0}, {"videoId", videoA}}},
        {"extension", QStringLiteral("m4a")}, {"outputPath", outside}};
    store.enqueue(request);
    store.feedMusicSource(id, server.resource(videoA));
    settle();
    if (!require(requested.count == 0 && store.musicRequest(id).isEmpty() && server.requests == before
        && !QFileInfo::exists(outside) && !QFileInfo::exists(outside + ".part")
        && store.downloadedMusic(scopeA).isEmpty(), "explicit music path outside profile directory must reject before I/O")) return false;
    // Positive control: exactly the same identity/metadata is accepted when only the bad path is removed.
    request.remove("outputPath");
    store.enqueue(request);
    const bool accepted = requested.count == 1 && requested.id == id && !store.musicRequest(id).isEmpty();
    store.cancelJob(id);
    return require(accepted, "outside-path fixture must otherwise be a valid music request");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("ColosseumAudit"));
    const QString base = "VinylMusicDownload-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    ByteServer server;
    if (!require(server.listen(), "loopback fixture listener unavailable")) return 1;
    if (!completionRestart(base + "-restart", server) || !profileFence(base + "-profiles", server)) return 1;
    for (int variant = 0; variant < 3; ++variant)
        if (!rejectedResource(base + "-reject-" + QString::number(variant), server, variant)) return 1;
    if (!outsidePath(base + "-outside", server)) return 1;
    std::printf("PASS: music bytes/headers/restart, profile fencing, malformed resources, contained output.\n");
    return 0;
}
