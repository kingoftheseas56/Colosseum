#include "player/streamserver.h"
#include "integration/ProductionTorrentBackend.h"
#include "settings/ServerSettings.h"
#include "torrent/engine/TorrentEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QTimer>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t BlockSize = 16 * 1024;

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::abort();
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

void requireBytes(const QByteArray &actual,
                  const QByteArray &expected,
                  const char *message)
{
    if (actual == expected)
        return;
    const qsizetype common = std::min(actual.size(), expected.size());
    qsizetype mismatch = 0;
    while (mismatch < common && actual.at(mismatch) == expected.at(mismatch))
        ++mismatch;
    std::fprintf(stderr,
                 "BYTES:%s actual=%lld expected=%lld mismatch=%lld got=%u want=%u\n",
                 message, static_cast<long long>(actual.size()),
                 static_cast<long long>(expected.size()),
                 static_cast<long long>(mismatch),
                 mismatch < actual.size()
                     ? static_cast<unsigned char>(actual.at(mismatch)) : 0,
                 mismatch < expected.size()
                     ? static_cast<unsigned char>(expected.at(mismatch)) : 0);
    fail(message);
}

std::vector<char> payload()
{
    std::vector<char> bytes(BlockSize * 3 + 5);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>((i * 29 + 7) & 0xff);
    return bytes;
}

lt::session_params fixtureSessionParams()
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_int(lt::settings_pack::unchoke_slots_limit, -1);
    settings.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_enabled);
    settings.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_enabled);
    settings.set_int(lt::settings_pack::allowed_enc_level, lt::settings_pack::pe_both);
    return lt::session_params(settings);
}

QByteArray makeTorrent(const std::vector<char> &bytes, const QString &root)
{
    const QString seedPath = QDir(root).filePath(QStringLiteral("seed"));
    require(QDir().mkpath(seedPath), "torrent fixture seed directory creation failed");
    std::ofstream file(QDir(seedPath).filePath(QStringLiteral("movie.mp4")).toStdString(),
                       std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(file.good(), "torrent fixture payload write failed");
    file.close();
    require(file.good(), "torrent fixture payload close failed");

    lt::file_storage files;
    files.add_file(std::string("movie.mp4"), static_cast<std::int64_t>(bytes.size()));
    lt::create_torrent creator(files, static_cast<int>(BlockSize),
                               lt::create_torrent::v1_only);
    for (int piece = 0; piece < creator.num_pieces(); ++piece) {
        const auto offset = static_cast<std::size_t>(piece) * BlockSize;
        const auto length = std::min(BlockSize, bytes.size() - offset);
        lt::hasher hasher(bytes.data() + offset, static_cast<int>(length));
        creator.set_hash(lt::piece_index_t{piece}, hasher.final());
    }
    const auto encoded = creator.generate_buf();
    return QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size()));
}

QByteArray makeMultiFileTorrent(const QByteArray &first, const QByteArray &second,
                                const QString &root)
{
    const QString seedRoot = QDir(root).filePath(QStringLiteral("multifile-seed"));
    const QString seedPath = QDir(seedRoot).filePath(QStringLiteral("multifile"));
    require(QDir().mkpath(seedPath), "multifile seed directory creation failed");
    std::ofstream firstFile(QDir(seedPath).filePath(QStringLiteral("first.bin")).toStdString(),
                            std::ios::binary | std::ios::trunc);
    firstFile.write(first.constData(), static_cast<std::streamsize>(first.size()));
    require(firstFile.good(), "multifile first payload write failed");
    firstFile.close();
    require(firstFile.good(), "multifile first payload close failed");
    std::ofstream secondFile(QDir(seedPath).filePath(QStringLiteral("second.mp4")).toStdString(),
                             std::ios::binary | std::ios::trunc);
    secondFile.write(second.constData(), static_cast<std::streamsize>(second.size()));
    require(secondFile.good(), "multifile second payload write failed");
    secondFile.close();
    require(secondFile.good(), "multifile second payload close failed");

    lt::file_storage files;
    files.add_file(std::string("multifile/first.bin"), first.size());
    files.add_file(std::string("multifile/second.mp4"), second.size());
    lt::create_torrent creator(files, static_cast<int>(BlockSize),
                               lt::create_torrent::v1_only);
    const QByteArray concatenated = first + second;
    for (int piece = 0; piece < creator.num_pieces(); ++piece) {
        const auto offset = static_cast<std::size_t>(piece) * BlockSize;
        const auto length = std::min<std::size_t>(BlockSize,
                                                  static_cast<std::size_t>(concatenated.size())
                                                      - offset);
        lt::hasher hasher(concatenated.constData() + static_cast<qsizetype>(offset),
                          static_cast<int>(length));
        creator.set_hash(lt::piece_index_t{piece}, hasher.final());
    }
    const auto encoded = creator.generate_buf();
    return QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size()));
}

QByteArray readResponse(QTcpSocket &socket)
{
    QByteArray wire;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        if (socket.waitForReadyRead(100))
            wire += socket.readAll();
        QCoreApplication::processEvents();
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
    }
    wire += socket.readAll();
    return wire;
}

QByteArray header(const QByteArray &wire, const QByteArray &name)
{
    const qsizetype end = wire.indexOf("\r\n\r\n");
    require(end >= 0, "torrent HTTP response must contain a complete head");
    for (const QByteArray &line : wire.left(end).split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

QByteArray body(const QByteArray &wire)
{
    const qsizetype end = wire.indexOf("\r\n\r\n");
    require(end >= 0, "torrent HTTP response must contain a body separator");
    return wire.mid(end + 4);
}

QByteArray issueHttpRequest(quint16 port, const QByteArray &request)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    require(socket.waitForConnected(3000), "torrent HTTP client failed to connect");
    require(socket.write(request) == request.size(), "torrent HTTP request write was partial");
    require(socket.waitForBytesWritten(3000), "torrent HTTP request was not written");
    return readResponse(socket);
}

QByteArray mediaRequest(const QString &hash, const QByteArray &method,
                        const QByteArray &range = {})
{
    QByteArray request = method + " /" + hash.toUtf8()
        + QByteArrayLiteral("/0/movie.mp4 HTTP/1.1\r\nHost: 127.0.0.1\r\n");
    if (!range.isEmpty())
        request += QByteArrayLiteral("Range: ") + range + QByteArrayLiteral("\r\n");
    request += QByteArrayLiteral("Connection: close\r\n\r\n");
    return request;
}

bool waitForSignal(QSignalSpy &spy, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (spy.isEmpty() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !spy.isEmpty();
}

template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

bool waitForPiece(TorrentEngine &engine, const QString &hash, int piece, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (engine.torrentHandle(hash).have_piece(lt::piece_index_t{piece}))
            return true;
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return engine.torrentHandle(hash).have_piece(lt::piece_index_t{piece});
}

void testProductionStreamUsesSchedulerTransport()
{
    QTemporaryDir root;
    require(root.isValid(), "production stream fixture directory must be valid");

    const auto bytes = payload();
    const QByteArray metainfo = makeTorrent(bytes, root.path());
    lt::session seed(fixtureSessionParams());
    lt::error_code error;
    lt::add_torrent_params seedParams;
    seedParams.ti = std::make_shared<lt::torrent_info>(metainfo.constData(), metainfo.size(), error);
    require(!error && seedParams.ti, "production stream seed metadata decode failed");
    seedParams.save_path = QDir(root.path()).filePath(QStringLiteral("seed")).toStdString();
    seedParams.flags &= ~lt::torrent_flags::paused;
    const auto seedHandle = seed.add_torrent(std::move(seedParams), error);
    require(!error && seedHandle.is_valid(), "production stream seed add failed");
    require(waitFor([&] { return seedHandle.status().is_seeding; }, 5000),
            "production stream seed must finish checking before the client connects");

    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("engine")));
    engine.start();
    colosseum::server::ServerSettings settings(
        root.path(), colosseum::server::ServerSettings::Platform::Windows,
        root.path(), true);
    colosseum::server::integration::ProductionTorrentBackend backend(
        QDir(root.path()).filePath(QStringLiteral("backend")), settings, &engine);
    backend.start();

    const QString hash = engine.addTorrentBytes(
        metainfo, QDir(root.path()).filePath(QStringLiteral("torrent")), false);
    require(!hash.isEmpty(), "production stream fixture torrent creation failed");
    const auto clientHandle = engine.torrentHandle(hash);
    require(clientHandle.is_valid(), "production stream engine handle must be valid");
    clientHandle.pause();
    const auto originalPriorities = clientHandle.get_piece_priorities();
    require(!clientHandle.have_piece(lt::piece_index_t{0}),
            "production stream fixture must begin without the requested piece");
    // Keep the production engine's persisted peer policy and live DHT from
    // introducing unrelated peers into this local transport qualification.
    clientHandle.apply_ip_filter(false);
    clientHandle.set_flags(lt::torrent_flags::disable_dht
                           | lt::torrent_flags::disable_lsd
                           | lt::torrent_flags::disable_pex);
    const auto loopback = lt::make_address("127.0.0.1", error);
    require(!error, "production stream loopback address construction failed");
    colosseum::server::torrent_http::TorrentReadPlan plan;
    plan.infoHash = hash;
    plan.fileIndex = 0;
    plan.start = 0;
    plan.end = static_cast<qint64>(bytes.size() - 1);

    colosseum::server::torrent_http::TorrentReadPlan firstReaderPlan = plan;
    firstReaderPlan.start = 0;
    firstReaderPlan.end = static_cast<qint64>(BlockSize - 1);
    colosseum::server::torrent_http::TorrentReadPlan tailReaderPlan = plan;
    tailReaderPlan.start = static_cast<qint64>(BlockSize * 2);
    tailReaderPlan.end = static_cast<qint64>(bytes.size() - 1);
    const auto noCancel = std::make_shared<colosseum::server::CancellationToken>();
    auto firstReader = backend.open(
        firstReaderPlan, noCancel,
        colosseum::server::integration::TorrentStreamCallbacks{});
    auto tailReader = backend.open(
        tailReaderPlan, noCancel,
        colosseum::server::integration::TorrentStreamCallbacks{});
    require(firstReader != nullptr && tailReader != nullptr,
            "concurrent production readers must acquire scheduler priority scopes");
    const bool unionReady = waitFor([&] {
                const auto priorities = clientHandle.get_piece_priorities();
                return priorities.size() == originalPriorities.size()
                    && priorities.at(0) == lt::top_priority
                    && priorities.at(1) == lt::dont_download
                    && priorities.at(2) == lt::top_priority
                    && priorities.at(3) == lt::top_priority;
            }, 1000);
    require(unionReady, "concurrent production readers must expose the union of selected pieces");
    firstReader->destroy();
    require(waitFor([&] {
                const auto priorities = clientHandle.get_piece_priorities();
                return priorities.size() == originalPriorities.size()
                    && priorities.at(0) == lt::dont_download
                    && priorities.at(2) == lt::top_priority
                    && priorities.at(3) == lt::top_priority;
            }, 1000),
            "closing one production reader must preserve the other reader's pieces");
    tailReader->destroy();
    require(waitFor([&] { return clientHandle.get_piece_priorities() == originalPriorities; }, 1000),
            "closing the final production reader must restore original priorities");
    firstReader.reset();
    tailReader.reset();
    const auto cancellation = std::make_shared<colosseum::server::CancellationToken>();
    QByteArray received;
    std::error_code streamError;
    bool ended = false;
    auto session = backend.open(plan, cancellation,
        colosseum::server::integration::TorrentStreamCallbacks{
            [&](QByteArray chunk) { received += chunk; },
            [&](std::error_code errorValue) { streamError = errorValue; },
            [&] { ended = true; }});
    require(session != nullptr, "production stream session must be created");
    session->start();
    engine.forceStart(hash);
    clientHandle.connect_peer({loopback, seed.listen_port()});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    int reconnectTicks = 0;
    while (!ended && std::chrono::steady_clock::now() < deadline) {
        if (++reconnectTicks % 10 == 0) {
            clientHandle.connect_peer({loopback, seed.listen_port()});
            seedHandle.connect_peer({loopback, engine.listenPort()});
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(ended && !streamError,
            "production stream must complete through scheduler transport");
    const QByteArray expectedStream(bytes.data(), static_cast<qsizetype>(bytes.size()));
    requireBytes(received, expectedStream,
                 "scheduler transport production stream bytes must match the seed");
    require(clientHandle.have_piece(lt::piece_index_t{0}),
            "scheduler transport production stream must verify the first piece");
    session->destroy();
    session.reset();
    require(waitFor([&] {
        return clientHandle.get_piece_priorities() == originalPriorities;
    }, 1000),
            "production stream must restore piece priorities after its last reader closes");
    engine.removeTorrent(hash);
    backend.stop();
    engine.stop();
}

void testProductionStreamKeepsMultiFileBoundaries()
{
    QTemporaryDir root;
    require(root.isValid(), "multifile torrent fixture directory must be valid");

    const QByteArray first(static_cast<int>(BlockSize * 2), 'A');
    QByteArray second(static_cast<int>(BlockSize + 5), Qt::Uninitialized);
    for (qsizetype i = 0; i < second.size(); ++i)
        second[i] = static_cast<char>((i * 17 + 3) & 0xff);
    const QByteArray metainfo = makeMultiFileTorrent(first, second, root.path());

    lt::session seed(fixtureSessionParams());
    lt::error_code error;
    lt::add_torrent_params seedParams;
    seedParams.ti = std::make_shared<lt::torrent_info>(metainfo.constData(), metainfo.size(), error);
    require(!error && seedParams.ti, "multifile seed metadata decode failed");
    seedParams.save_path = QDir(root.path()).filePath(QStringLiteral("multifile-seed")).toStdString();
    seedParams.flags &= ~lt::torrent_flags::paused;
    const auto seedHandle = seed.add_torrent(std::move(seedParams), error);
    require(!error && seedHandle.is_valid(), "multifile seed add failed");
    require(waitFor([&] { return seedHandle.status().is_seeding; }, 5000),
            "multifile seed must finish checking before the client connects");

    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("multifile-engine")));
    engine.start();
    colosseum::server::ServerSettings settings(
        root.path(), colosseum::server::ServerSettings::Platform::Windows,
        root.path(), true);
    colosseum::server::integration::ProductionTorrentBackend backend(
        QDir(root.path()).filePath(QStringLiteral("multifile-backend")), settings, &engine);
    backend.start();

    const QString hash = engine.addTorrentBytes(
        metainfo, QDir(root.path()).filePath(QStringLiteral("multifile-torrent")), false);
    require(!hash.isEmpty(), "multifile production backend must accept fixture torrent bytes");
    const auto clientHandle = engine.torrentHandle(hash);
    require(clientHandle.is_valid(), "multifile client handle must be valid");
    clientHandle.pause();
    clientHandle.apply_ip_filter(false);
    clientHandle.set_flags(lt::torrent_flags::disable_dht
                           | lt::torrent_flags::disable_lsd
                           | lt::torrent_flags::disable_pex);
    const auto loopback = lt::make_address("127.0.0.1", error);
    require(!error, "multifile loopback address construction failed");
    const auto originalPriorities = clientHandle.get_piece_priorities();

    colosseum::server::torrent_http::TorrentReadPlan plan;
    plan.infoHash = hash;
    plan.fileIndex = 1;
    plan.start = 0;
    plan.end = second.size() - 1;
    const auto cancellation = std::make_shared<colosseum::server::CancellationToken>();
    QByteArray received;
    std::error_code streamError;
    bool ended = false;
    auto session = backend.open(plan, cancellation,
        colosseum::server::integration::TorrentStreamCallbacks{
            [&](QByteArray chunk) { received += chunk; },
            [&](std::error_code errorValue) { streamError = errorValue; },
            [&] { ended = true; }});
    require(session != nullptr, "multifile production stream session must be created");
    session->start();
    engine.forceStart(hash);
    clientHandle.connect_peer({loopback, seed.listen_port()});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    int reconnectTicks = 0;
    while (!ended && std::chrono::steady_clock::now() < deadline) {
        if (++reconnectTicks % 10 == 0) {
            clientHandle.connect_peer({loopback, seed.listen_port()});
            seedHandle.connect_peer({loopback, engine.listenPort()});
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    require(ended && !streamError,
            "multifile production stream must complete through scheduler transport");
    requireBytes(received, second,
                 "multifile production stream must return only the selected file bytes");
    require(!clientHandle.have_piece(lt::piece_index_t{0})
                && !clientHandle.have_piece(lt::piece_index_t{1}),
            "multifile stream must not download pieces belonging only to the first file");
    require(clientHandle.have_piece(lt::piece_index_t{2})
                && clientHandle.have_piece(lt::piece_index_t{3}),
            "multifile stream must verify all pieces covering the selected second file");
    session->destroy();
    require(waitFor([&] {
        return clientHandle.get_piece_priorities() == originalPriorities;
    }, 1000),
            "multifile stream must restore piece priorities after close");
    engine.removeTorrent(hash);
    backend.stop();
    engine.stop();
}

void testProductionBackendAppliesPeerSearchSources()
{
    QTemporaryDir root;
    require(root.isValid(), "peer-search fixture directory must be valid");

    const auto bytes = payload();
    const QByteArray metainfo = makeTorrent(bytes, root.path());
    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("engine")));
    engine.start();
    colosseum::server::ServerSettings settings(
        root.path(), colosseum::server::ServerSettings::Platform::Windows,
        root.path(), true);
    auto backend = std::make_unique<colosseum::server::integration::ProductionTorrentBackend>(
        QDir(root.path()).filePath(QStringLiteral("backend")), settings, &engine);
    backend->start();

    QString hash;
    QString createError;
    backend->createFromTorrent(metainfo, [&](const QString &createdHash,
                                             const QString &error) {
        hash = createdHash;
        createError = error;
    });
    require(createError.isEmpty() && !hash.isEmpty(),
            "peer-search fixture torrent must be accepted by libtorrent");
    const auto handle = engine.torrentHandle(hash);
    require(handle.is_valid(), "peer-search fixture handle must be valid");
    handle.pause();
    handle.set_flags(lt::torrent_flags::disable_dht
                     | lt::torrent_flags::disable_lsd
                     | lt::torrent_flags::disable_pex);
    const QJsonObject defaults = backend->defaultEngineOptions(hash);
    const QJsonArray defaultSources = defaults.value(QStringLiteral("peerSearch"))
                                         .toObject()
                                         .value(QStringLiteral("sources"))
                                         .toArray();
    const QJsonObject defaultPeerSearch = defaults.value(QStringLiteral("peerSearch"))
                                             .toObject();
    require(defaultPeerSearch.value(QStringLiteral("min")).toInt() == 40
                && defaultPeerSearch.value(QStringLiteral("max")).toInt() == 150,
            "production defaults must preserve server.js peer-search bounds");
    const QStringList expectedTrackers{
        QStringLiteral("tracker:udp://tracker.opentrackr.org:1337/announce"),
        QStringLiteral("tracker:udp://open.demonii.com:1337/announce"),
        QStringLiteral("tracker:udp://open.stealth.si:80/announce"),
        QStringLiteral("tracker:https://torrent.tracker.durukanbal.com:443/announce"),
        QStringLiteral("tracker:udp://wepzone.net:6969/announce"),
        QStringLiteral("tracker:udp://tracker.wepzone.net:6969/announce"),
        QStringLiteral("tracker:udp://tracker.torrent.eu.org:451/announce"),
        QStringLiteral("tracker:udp://tracker.theoks.net:6969/announce"),
        QStringLiteral("tracker:udp://tracker.t-1.org:6969/announce"),
        QStringLiteral("tracker:udp://tracker.darkness.services:6969/announce"),
        QStringLiteral("tracker:udp://tracker-udp.gbitt.info:80/announce"),
        QStringLiteral("tracker:udp://t.overflow.biz:6969/announce"),
        QStringLiteral("tracker:udp://open.dstud.io:6969/announce"),
        QStringLiteral("tracker:udp://explodie.org:6969/announce"),
        QStringLiteral("tracker:udp://exodus.desync.com:6969/announce"),
        QStringLiteral("tracker:udp://bittorrent-tracker.e-n-c-r-y-p-t.net:1337/announce"),
        QStringLiteral("tracker:https://tracker.zhuqiy.com:443/announce"),
        QStringLiteral("tracker:https://tracker.pmman.tech:443/announce"),
        QStringLiteral("tracker:https://tracker.moeblog.cn:443/announce"),
        QStringLiteral("tracker:https://tracker.bt4g.com:443/announce")};
    require(defaultSources.size() == expectedTrackers.size() + 1,
            "production defaults must carry exactly the server.js tracker pool and DHT source");
    for (qsizetype index = 0; index < expectedTrackers.size(); ++index)
        require(defaultSources.at(index).toString() == expectedTrackers.at(index),
                "production default tracker order must match server.js");
    require(defaultSources.last().toString()
                == QStringLiteral("dht:") + hash.toLower(),
            "production defaults must append the torrent-specific DHT source");
    const auto swarmCap = defaults.value(QStringLiteral("swarmCap")).toObject();
    const auto growler = defaults.value(QStringLiteral("growler")).toObject();
    require(defaults.value(QStringLiteral("dht")).toBool() == false
                && defaults.value(QStringLiteral("tracker")).toBool() == false
                && defaults.value(QStringLiteral("connections")).toInt() == 55
                && defaults.value(QStringLiteral("handshakeTimeout")).toInt() == 20000
                && defaults.value(QStringLiteral("timeout")).toInt() == 4000
                && defaults.value(QStringLiteral("virtual")).toBool(),
            "production defaults must preserve server.js engine controls");
    require(growler.value(QStringLiteral("flood")).toInt() == 0
                && growler.value(QStringLiteral("pulse")).toInt() == 3670016,
            "production defaults must preserve server.js growler limits");
    require(defaults.value(QStringLiteral("buffer")).toInt() == 15728640
                && defaults.value(QStringLiteral("circularBuffer")).toObject()
                       .value(QStringLiteral("type")).toString() == QStringLiteral("memory")
                && defaults.value(QStringLiteral("circularBuffer")).toObject()
                       .value(QStringLiteral("size")).toInt() == 47185920
                && swarmCap.value(QStringLiteral("minPeers")).toInt() == 5
                && qFuzzyCompare(swarmCap.value(QStringLiteral("maxBuffer")).toDouble(), 0.75),
            "no-cache production defaults must preserve server.js buffer policy");

    const auto createdTrackers = engine.trackersFor(hash);
    require(createdTrackers.size() == expectedTrackers.size(),
            "trackerless /create must install the server.js default tracker pool");
    for (const auto &tracker : createdTrackers)
        require(std::any_of(expectedTrackers.cbegin(), expectedTrackers.cend(),
                            [&](const QString &source) {
                                return source.mid(QStringLiteral("tracker:").size())
                                       == tracker.url;
                            }),
                "trackerless /create tracker URL must match server.js defaults");

    bool ready = false;
    QString error;
    const QJsonObject options{
        {QStringLiteral("peerSearch"),
         QJsonObject{{QStringLiteral("sources"),
                      QJsonArray{QStringLiteral("udp://127.0.0.1:1/announce"),
                                 QStringLiteral("udp://127.0.0.1:1/announce"),
                                 QStringLiteral("tracker:udp://127.0.0.1:2/announce")}}}}};
    backend->ensureEngine(hash, options, [&](const QString &value) {
        ready = true;
        error = value;
    });
    require(ready && error.isEmpty(),
            "an existing metadata-ready production engine must complete immediately");

    const auto trackers = engine.trackersFor(hash);
    require(trackers.size() == 2,
            "request peerSearch sources must replace the existing libtorrent tracker set");
    QSet<QString> actualUrls;
    for (const auto &tracker : trackers)
        actualUrls.insert(tracker.url);
    require(actualUrls.contains(QStringLiteral("udp://127.0.0.1:1/announce"))
                && actualUrls.contains(QStringLiteral("udp://127.0.0.1:2/announce")),
            "production libtorrent trackers must match the deduplicated request sources");

    engine.removeTorrent(hash);
    backend->stop();
    backend.reset();
    engine.stop();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    qputenv("COLOSSEUM_TORRENT_LISTEN_INTERFACES", QByteArrayLiteral("127.0.0.1:0"));
    qputenv("COLOSSEUM_TORRENT_DISABLE_DISCOVERY", QByteArrayLiteral("1"));
    testProductionStreamUsesSchedulerTransport();
    testProductionStreamKeepsMultiFileBoundaries();
    testProductionBackendAppliesPeerSearchSources();
    QTemporaryDir root;
    require(root.isValid(), "torrent HTTP fixture directory must be valid");

    const auto bytes = payload();
    const QByteArray metainfo = makeTorrent(bytes, root.path());
    lt::session seed(fixtureSessionParams());
    lt::error_code error;
    lt::add_torrent_params seedParams;
    seedParams.ti = std::make_shared<lt::torrent_info>(metainfo.constData(), metainfo.size(), error);
    require(!error && seedParams.ti, "torrent HTTP seed metadata decode failed");
    seedParams.save_path = QDir(root.path()).filePath(QStringLiteral("seed")).toStdString();
    seedParams.flags &= ~lt::torrent_flags::paused;
    const auto seedHandle = seed.add_torrent(std::move(seedParams), error);
    require(!error && seedHandle.is_valid(), "torrent HTTP seed add failed");
    require(waitFor([&] { return seedHandle.status().is_seeding; }, 5000),
            "torrent HTTP seed must finish checking before the client connects");

    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("engine")));
    StreamServer stream(&engine);
    stream.warmUp();
    require(stream.ready(), "native StreamServer must be ready before torrent registration");

    const QString hash = engine.addTorrentBytes(
        metainfo, QDir(root.path()).filePath(QStringLiteral("client")), true);
    require(!hash.isEmpty(), "native engine must accept fixture torrent bytes");
    const auto clientHandle = engine.torrentHandle(hash);
    require(clientHandle.is_valid(), "native engine must expose the fixture torrent handle");
    const auto loopback = lt::make_address("127.0.0.1", error);
    require(!error, "torrent HTTP loopback address construction failed");
    clientHandle.apply_ip_filter(false);
    clientHandle.set_flags(lt::torrent_flags::disable_dht
                           | lt::torrent_flags::disable_lsd
                           | lt::torrent_flags::disable_pex);
    clientHandle.connect_peer({loopback, seed.listen_port()});

    QSignalSpy streamReady(&stream, &StreamServer::streamReady);
    stream.play(hash, 0);
    require(waitForSignal(streamReady, 5000),
            "native stream registration must emit streamReady");
    require(streamReady.count() == 1, "native stream registration must emit one streamReady");
    engine.forceStart(hash);
    clientHandle.connect_peer({loopback, seed.listen_port()});
    seedHandle.connect_peer({loopback, engine.listenPort()});
    QTimer peerRetry;
    QObject::connect(&peerRetry, &QTimer::timeout, [&] {
        clientHandle.connect_peer({loopback, seed.listen_port()});
        seedHandle.connect_peer({loopback, engine.listenPort()});
    });
    peerRetry.start(100);

    const QUrl nativeUrl(streamReady.at(0).at(0).toString());
    const quint16 port = static_cast<quint16>(nativeUrl.port());
    const QByteArray expected(bytes.data(), static_cast<qsizetype>(bytes.size()));

    const QByteArray wire = issueHttpRequest(
        port, mediaRequest(hash, QByteArrayLiteral("GET"),
                           QByteArrayLiteral("bytes=0-")
                               + QByteArray::number(bytes.size() - 1)));

    require(wire.startsWith("HTTP/1.1 206 "), "real torrent range response must be 206");
    require(header(wire, "content-length") == QByteArray::number(bytes.size()),
            "real torrent range length must be exact");
    require(header(wire, "content-range")
                == QByteArray("bytes 0-") + QByteArray::number(bytes.size() - 1)
                    + "/" + QByteArray::number(bytes.size()),
            "real torrent content range must be exact");
    require(header(wire, "transfer-encoding").isEmpty(),
            "real torrent fixed-length response must not be chunked");
    requireBytes(body(wire), expected,
                 "real torrent HTTP body bytes must match the seeded payload");
    require(waitForPiece(engine, hash, 0, 10000),
            "real torrent HTTP request must verify the first piece through libtorrent");

    const QByteArray head = issueHttpRequest(
        port, mediaRequest(hash, QByteArrayLiteral("HEAD")));
    require(head.startsWith("HTTP/1.1 200 "), "real torrent HEAD response must be 200");
    require(header(head, "accept-ranges") == QByteArrayLiteral("bytes"),
            "real torrent HEAD must advertise byte ranges");
    require(header(head, "content-length") == QByteArray::number(bytes.size()),
            "real torrent HEAD length must describe the complete file");
    require(header(head, "content-range").isEmpty(),
            "real torrent HEAD without Range must not advertise Content-Range");
    require(body(head).isEmpty(), "real torrent HEAD must not deliver a body");

    const qint64 boundedStart = static_cast<qint64>(BlockSize + 13);
    const qint64 boundedEnd = boundedStart + 127;
    const QByteArray bounded = issueHttpRequest(
        port, mediaRequest(hash, QByteArrayLiteral("GET"),
                           QByteArrayLiteral("bytes=") + QByteArray::number(boundedStart)
                               + '-' + QByteArray::number(boundedEnd)));
    require(bounded.startsWith("HTTP/1.1 206 "),
            "real torrent bounded seek response must be 206");
    require(header(bounded, "content-range")
                == QByteArrayLiteral("bytes ") + QByteArray::number(boundedStart) + '-'
                    + QByteArray::number(boundedEnd) + '/'
                    + QByteArray::number(bytes.size()),
            "real torrent bounded seek Content-Range must be exact");
    requireBytes(body(bounded), expected.mid(static_cast<qsizetype>(boundedStart),
                                               static_cast<qsizetype>(boundedEnd - boundedStart + 1)),
                 "real torrent bounded seek bytes must match the seeded payload");

    const qint64 tailLength = 91;
    const QByteArray tail = issueHttpRequest(
        port, mediaRequest(hash, QByteArrayLiteral("GET"),
                           QByteArrayLiteral("bytes=-") + QByteArray::number(tailLength)));
    const qint64 tailStart = bytes.size() - tailLength;
    require(tail.startsWith("HTTP/1.1 206 "), "real torrent suffix range response must be 206");
    require(header(tail, "content-range")
                == QByteArrayLiteral("bytes ") + QByteArray::number(tailStart) + '-'
                    + QByteArray::number(bytes.size() - 1) + '/'
                    + QByteArray::number(bytes.size()),
            "real torrent suffix range Content-Range must be exact");
    requireBytes(body(tail), expected.right(static_cast<qsizetype>(tailLength)),
                 "real torrent suffix range bytes must match the seeded payload");

    const QByteArray invalid = issueHttpRequest(
        port, mediaRequest(hash, QByteArrayLiteral("GET"),
                           QByteArrayLiteral("bytes=999999999999-1000000000000")));
    require(invalid.startsWith("HTTP/1.1 200 "),
            "real torrent invalid range must fall back to 200");
    require(header(invalid, "content-range").isEmpty(),
            "real torrent invalid range fallback must omit Content-Range");
    require(header(invalid, "content-length") == QByteArray::number(bytes.size()),
            "real torrent invalid range fallback must use the full length");
    requireBytes(body(invalid), expected,
                 "real torrent invalid range fallback bytes must match the complete file");

    QTcpSocket firstReader;
    QTcpSocket secondReader;
    firstReader.connectToHost(QHostAddress::LocalHost, port);
    secondReader.connectToHost(QHostAddress::LocalHost, port);
    require(firstReader.waitForConnected(3000) && secondReader.waitForConnected(3000),
            "concurrent torrent HTTP clients failed to connect");
    const QByteArray firstRequest = mediaRequest(
        hash, QByteArrayLiteral("GET"), QByteArrayLiteral("bytes=0-255"));
    const QByteArray secondRequest = mediaRequest(
        hash, QByteArrayLiteral("GET"), QByteArrayLiteral("bytes=256-511"));
    require(firstReader.write(firstRequest) == firstRequest.size()
                && secondReader.write(secondRequest) == secondRequest.size(),
            "concurrent torrent HTTP requests were partially written");
    require(firstReader.waitForBytesWritten(3000) && secondReader.waitForBytesWritten(3000),
            "concurrent torrent HTTP requests were not written");
    const QByteArray firstWire = readResponse(firstReader);
    const QByteArray secondWire = readResponse(secondReader);
    require(firstWire.startsWith("HTTP/1.1 206 ") && secondWire.startsWith("HTTP/1.1 206 "),
            "concurrent torrent HTTP readers must both receive 206");
    requireBytes(body(firstWire), expected.left(256),
                 "first concurrent torrent reader bytes must match the seeded payload");
    requireBytes(body(secondWire), expected.mid(256, 256),
                 "second concurrent torrent reader bytes must match the seeded payload");

    stream.unwatchStats();
    peerRetry.stop();
    std::puts("RUNTIME_TORRENT_HTTP_E2E_OK");
    return 0;
}
