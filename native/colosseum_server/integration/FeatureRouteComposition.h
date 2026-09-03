#pragma once

#include "core/HttpRouter.h"
#include "media/MediaPipeline.h"
#include "network_app/NetworkAppServices.h"
#include "remote_archive/RemoteArchive.h"
#include "remote_archive/RemoteServices.h"
#include "settings/ServerSettings.h"

#include <QUrl>

#include <functional>

namespace colosseum::server::integration {

struct MediaRouteDependencies final
{
    ::ColosseumServer::Media::Executables executables;
    ::ColosseumServer::Media::HlsV2Registry *hlsV2Registry = nullptr;
    QUrl loopbackBaseUrl{QStringLiteral("http://127.0.0.1:11470")};
    int defaultMaxAudioChannels = 2;

    std::function<bool(const QString &, ::ColosseumServer::Media::LegacyProbeResult *, QString *)>
        legacyProbe;
    std::function<bool(const QString &, ::ColosseumServer::Media::V2ProbeResult *, QString *)>
        v2Probe;
    std::function<QVector<::ColosseumServer::Media::TrackInfo>(const QString &, QString *)>
        parseTracks;
    std::function<bool(const QUrl &, QByteArray *, QString *)> retrieveSubtitle;
    std::function<bool(const QUrl &, QJsonObject *, QString *)> subtitlesTracks;
    std::function<bool(const QUrl &, QString *, QString *)> openSubHash;
};

struct NetworkAppRouteDependencies final
{
    app::ProxyService *proxy = nullptr;
    app::YouTubeService *youtube = nullptr;
    app::CastingService *casting = nullptr;
    app::LocalAddonService *localAddon = nullptr;
    app::NetworkRouteService *network = nullptr;
    bool encrypted = false;
    quint16 localPort = 0;
    QString engineUrl{QStringLiteral("http://127.0.0.1:11470")};
};

struct RemoteArchiveRouteDependencies final
{
    Colosseum::Server::RemoteArchive::ArchiveService *archives = nullptr;
    Colosseum::Server::RemoteArchive::FtpService *ftp = nullptr;
    Colosseum::Server::RemoteArchive::NzbService *nzb = nullptr;
};

struct FeatureRouteDependencies final
{
    MediaRouteDependencies media;
    NetworkAppRouteDependencies networkApp;
    RemoteArchiveRouteDependencies remoteArchive;
    server::ServerSettings *settings = nullptr;
};

void mountFeatureRoutes(server::HttpRouter &router,
                        const FeatureRouteDependencies &dependencies);

} // namespace colosseum::server::integration
