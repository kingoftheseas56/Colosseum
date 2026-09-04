#include "ColosseumServerRuntime.h"

#include "integration/FeatureRouteComposition.h"
#include "integration/AsyncMediaExecutor.h"
#include "integration/ProductionTorrentBackend.h"
#include "integration/TorrentHttpRouteAdapter.h"
#include "network_app/NetworkAppServices.h"
#include "remote_archive/RemoteArchive.h"
#include "remote_archive/RemoteServices.h"
#include "settings/ServerSettings.h"

#include "enginefs/EngineFsControlPlane.h"
#include "enginefs/QtEngineFsTimerScheduler.h"
#include "media/MediaPipeline.h"
#include "torrent_http/TorrentHttpSurface.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QSysInfo>

#include <memory>
#include <utility>

namespace colosseum::server::runtime {
namespace {

using namespace colosseum::server;
namespace Media = ::ColosseumServer::Media;
namespace Remote = ::Colosseum::Server::RemoteArchive;
namespace EngineFs = ::Colosseum::Server::EngineFs;

ServerSettings::Platform currentPlatform()
{
#if defined(Q_OS_WIN)
    return ServerSettings::Platform::Windows;
#elif defined(Q_OS_MACOS)
    return ServerSettings::Platform::MacOS;
#elif defined(Q_OS_ANDROID)
    return ServerSettings::Platform::Android;
#elif defined(Q_OS_LINUX)
    return ServerSettings::Platform::Linux;
#else
    return ServerSettings::Platform::Other;
#endif
}

class EmptyCastSession final : public app::CastPlayerSession
{
public:
    QJsonValue invoke(const QString &, const QJsonArray &, QJsonObject &) override
    {
        return QJsonValue(QJsonValue::Null);
    }
};

class EmptyCastSessionFactory final : public app::CastSessionFactory
{
public:
    std::unique_ptr<app::CastPlayerSession> create(const app::CastDevice &) override
    {
        return std::make_unique<EmptyCastSession>();
    }
};

class UnsupportedCastTranscoder final : public app::CastTranscoder
{
public:
    app::AppResponse transcode(const app::AppRequest &, bool) override
    {
        app::AppResponse response;
        response.status = 501;
        response.headers = {{QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("text/plain")}};
        response.body = QByteArrayLiteral("casting transcoder unavailable");
        return response;
    }
};

class EmptyLocalDiscovery final : public app::LocalFileDiscovery
{
public:
    QStringList discover() override { return {}; }
};

class EmptyLocalIndexer final : public app::LocalFileIndexer
{
public:
    std::optional<app::LocalAddonEntry> indexFile(const QString &) override
    {
        return std::nullopt;
    }
};

class EmptyHardwareProfiler final : public app::HardwareAccelerationProfiler
{
public:
    QJsonValue profile(int) override { return QJsonArray{}; }
};

QSslConfiguration configurationFromCertificate(const app::HttpsCertificate &certificate)
{
    const QSslCertificate cert(certificate.cert, QSsl::Pem);
    const QSslKey key(certificate.key, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    if (cert.isNull() || key.isNull())
        return {};
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificate(cert);
    configuration.setPrivateKey(key);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    return configuration;
}

} // namespace

struct ColosseumServerRuntime::Impl final
{
    explicit Impl(const ColosseumServerRuntimeOptions &options,
                  const std::shared_ptr<HttpRouter> &router)
        : settings(options.appPath, currentPlatform(),
                   options.settingsDirectory, options.disableCaching)
        , timerScheduler()
        , torrent(QDir(settings.values().value(QStringLiteral("cacheRoot")).toString()).filePath(
                      QStringLiteral("torrent-engine")), settings, options.torrentEngine)
        , engineFs(torrent, timerScheduler,
                   [](const EngineFs::EngineFsEvent &) {})
        , createSource()
        , torrentSurface(torrent, createSource)
        , executables(Media::ExecutableLocator::locateAll(QCoreApplication::applicationDirPath()))
        , hlsRegistry(executables, settings.values().value(QStringLiteral("transcodeConcurrency")).toInt(1))
        , proxyTransport()
        , proxy(proxyTransport)
        , youtubeResolver()
        , youtube(youtubeResolver)
        , castFactory()
        , castTranscoder()
        , casting(castRegistry, castFactory, castTranscoder)
        , localDiscovery()
        , localIndexer()
        , localAddon(settings.values().value(QStringLiteral("localAddonEnabled")).toBool(),
                     localDiscovery, localIndexer)
        , certificateTransport()
        , certificates(certificateTransport, settings.values().value(QStringLiteral("appPath")).toString(),
                       options.certificateEndpoint)
        , interfaces()
        , profiler()
        , archives()
        , ftp()
        , nzb()
        , router(router)
    {
        torrent.setControlPlane(&engineFs);
        routeDependencies = std::shared_ptr<integration::FeatureRouteDependencies>(
            &torrentDependencies, [](integration::FeatureRouteDependencies *) {});
    }

    bool mount(const ColosseumServerRuntimeOptions &options,
               const QUrl &httpUrl, const QUrl &httpsUrl, QString *error)
    {
        if (!QDir().mkpath(QFileInfo(settings.settingsFilePath()).absolutePath())) {
            if (error)
                *error = QStringLiteral("Could not create settings directory");
            return false;
        }
        const QString cacheRoot = settings.values().value(QStringLiteral("cacheRoot")).toString();
        if (!cacheRoot.isEmpty() && !QDir().mkpath(cacheRoot)) {
            if (error)
                *error = QStringLiteral("Could not create cache directory");
            return false;
        }
        if (!settings.save(error))
            return false;

        const quint16 httpPort = static_cast<quint16>(httpUrl.port(options.httpPort));
        const int boundHttpsPort = httpsUrl.isValid()
            ? httpsUrl.port(options.httpsPort)
            : static_cast<int>(options.httpsPort);
        torrentDependencies.media.loopbackBaseUrl = httpUrl;
        torrentDependencies.networkApp.encrypted = false;
        torrentDependencies.networkApp.localPort = httpPort;
        torrentDependencies.networkApp.engineUrl = httpUrl.toString();
        localAddon.setEngineUrl(httpUrl.toString());

        network = std::make_shared<app::NetworkRouteService>(
            certificates, interfaces, profiler,
            static_cast<int>(httpPort), boundHttpsPort,
            options.webUiLocation);

        torrentDependencies.media.executables = executables;
        torrentDependencies.media.hlsV2Registry = &hlsRegistry;
        torrentDependencies.media.defaultMaxAudioChannels = 2;
        const Media::Executables locatedExecutables = executables;
        torrentDependencies.media.legacyProbe = [locatedExecutables](const QString &url,
                                                                Media::LegacyProbeResult *result,
                                                                QString *probeError) {
            return Media::MediaProbe(locatedExecutables).legacyProbe(url, result, probeError);
        };
        torrentDependencies.media.v2Probe = [locatedExecutables](const QString &url,
                                                            Media::V2ProbeResult *result,
                                                            QString *probeError) {
            return Media::MediaProbe(locatedExecutables).probeV2(url, result, probeError);
        };
        torrentDependencies.media.parseTracks = [](const QString &path, QString *trackError) {
            return Media::TrackParser::parseFile(path, 25 * 1024 * 1024, trackError);
        };
        torrentDependencies.media.retrieveSubtitle = [](const QUrl &url, QByteArray *bytes,
                                                          QString *subtitleError) {
            return Media::SubtitleService::retrieve(url, bytes, subtitleError);
        };
        torrentDependencies.media.subtitlesTracks = [](const QUrl &url, QJsonObject *result,
                                                        QString *subtitleError) {
            return Media::SubtitleService::subtitlesTracks(url, result, subtitleError);
        };
        torrentDependencies.media.openSubHash = [](const QUrl &url, QString *hash,
                                                    QString *hashError) {
            return Media::SubtitleService::openSubHash(url, hash, hashError);
        };

        torrentDependencies.networkApp.proxy = &proxy;
        torrentDependencies.networkApp.youtube = &youtube;
        torrentDependencies.networkApp.casting = &casting;
        torrentDependencies.networkApp.localAddon = &localAddon;
        torrentDependencies.networkApp.network = network.get();
        torrentDependencies.networkApp.networkLifetime = network;
        torrentDependencies.remoteArchive.archives = &archives;
        torrentDependencies.remoteArchive.ftp = &ftp;
        torrentDependencies.remoteArchive.nzb = &nzb;
        torrentDependencies.settings = &settings;

        if (!routesMounted) {
            router->use(QStringLiteral("/"), [](HttpRequest &request, HttpResponse response) {
                return applyCorsHeaders(request, response);
            });
            integration::mountTorrentRoutes(*router, torrentSurface, torrent);
            integration::mountFeatureRoutes(*router, routeDependencies);
            routesMounted = true;
        }
        return true;
    }

    ServerSettings settings;
    EngineFs::QtEngineFsTimerScheduler timerScheduler;
    integration::ProductionTorrentBackend torrent;
    EngineFs::EngineFsControlPlane engineFs;
    integration::ProductionTorrentCreateSource createSource;
    torrent_http::TorrentHttpSurface torrentSurface;

    Media::Executables executables;
    Media::HlsV2Registry hlsRegistry;

    app::QtProxyTransport proxyTransport;
    app::ProxyService proxy;
    app::ProcessYouTubeResolver youtubeResolver;
    app::YouTubeService youtube;
    app::CastDiscoveryRegistry castRegistry;
    EmptyCastSessionFactory castFactory;
    UnsupportedCastTranscoder castTranscoder;
    app::CastingService casting;
    EmptyLocalDiscovery localDiscovery;
    EmptyLocalIndexer localIndexer;
    app::LocalAddonService localAddon;
    app::QtCertificateTransport certificateTransport;
    app::HttpsCertificateService certificates;
    app::SystemNetworkInterfaceProvider interfaces;
    EmptyHardwareProfiler profiler;
    Remote::ArchiveService archives;
    Remote::FtpService ftp;
    Remote::NzbService nzb;
    std::shared_ptr<app::NetworkRouteService> network;
    integration::FeatureRouteDependencies torrentDependencies;
    std::shared_ptr<integration::FeatureRouteDependencies> routeDependencies;
    std::shared_ptr<HttpRouter> router;
    bool routesMounted = false;
};

ColosseumServerRuntime::ColosseumServerRuntime(ColosseumServerRuntimeOptions options)
    : options_(std::move(options))
    , router_(std::make_shared<HttpRouter>())
    , impl_(std::make_shared<Impl>(options_, router_))
    , http_(router_)
    , https_(router_)
{
}

ColosseumServerRuntime::~ColosseumServerRuntime()
{
    stop();
    if (impl_)
        integration::AsyncMediaExecutor::retainUntilIdle(
            std::static_pointer_cast<void>(std::move(impl_)));
}

bool ColosseumServerRuntime::start()
{
    if (running_)
        return true;
    lastError_.clear();

    impl_->torrent.start();
    if (!http_.start(options_.httpPort)) {
        lastError_ = http_.lastError();
        impl_->torrent.stop();
        return false;
    }

    if (options_.enableTls) {
        QSslConfiguration configuration = options_.tlsConfiguration;
        if (configuration.localCertificate().isNull() || configuration.privateKey().isNull()) {
            QString certificateError;
            const auto certificate = impl_->certificates.cachedCertificate(&certificateError);
            if (certificate)
                configuration = configurationFromCertificate(*certificate);
        }
        if (configuration.localCertificate().isNull() || configuration.privateKey().isNull()) {
            lastError_ = QStringLiteral("HTTPS requested but no valid certificate is available");
            http_.stop();
            impl_->torrent.stop();
            return false;
        }
        if (!https_.startTls(options_.httpsPort, configuration)) {
            lastError_ = https_.lastError();
            http_.stop();
            impl_->torrent.stop();
            return false;
        }
    }

    if (!impl_->mount(options_, http_.boundUrl(), https_.boundUrl(), &lastError_)) {
        https_.stop();
        http_.stop();
        impl_->torrent.stop();
        return false;
    }
    running_ = true;
    return true;
}

void ColosseumServerRuntime::stop()
{
    if (!impl_)
        return;
    https_.stop();
    http_.stop();
    impl_->torrent.stop();
    // HTTP stop cancels each connection, but the route work it launched can
    // still be unwinding on the global pool. Keep the service graph alive
    // until every native job has returned.
    if (!integration::AsyncMediaExecutor::waitForIdle(30000))
        qWarning("ColosseumServerRuntime stopped with native route work still unwinding; "
                 "the service graph will be retired after the executor drains");
    running_ = false;
}

} // namespace colosseum::server::runtime
