#pragma once

// Arc 44 W09 provenance: Stremio 4.20.17 server.js SHA-256
// 567a397bb11b788571bf1750fd05dd78927f97bec0c9ddeaa6d9cc1eccee3922.
// W27: module 805. W28: modules 564/937. W29: 944/947/953/962/482.
// W30: 1024/1025/499/1054/1055/1056/1078/1079. W31: 804/564.

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <atomic>
#include <map>
#include <memory>
#include <optional>

namespace colosseum::server::app {

using HeaderList = QList<QPair<QByteArray, QByteArray>>;

struct AppRequest {
    QByteArray method = "GET";
    QString path;
    QUrlQuery query;
    HeaderList headers;
    QByteArray body;
    bool encrypted = false;
    quint16 localPort = 0;
};
struct AppResponse {
    int status = 404;
    HeaderList headers;
    QByteArray body;
};

QByteArray headerValue(const HeaderList &headers, QByteArrayView name);
bool hasHeader(const HeaderList &headers, QByteArrayView name);
void setHeader(HeaderList &headers, QByteArray name, QByteArray value);

struct ProxyFetchRequest {
    QByteArray method;
    QUrl url;
    HeaderList headers;
    QByteArray body;
};

struct ProxyFetchResponse {
    int status = 0;
    HeaderList headers;
    QByteArray body;
    QString error;
};

class ProxyTransport {
public:
    virtual ~ProxyTransport() = default;
    virtual ProxyFetchResponse fetch(const ProxyFetchRequest &request,
                                     const std::atomic_bool *cancelled = nullptr) = 0;
};

class QtProxyTransport final : public ProxyTransport {
public:
    ProxyFetchResponse fetch(const ProxyFetchRequest &request,
                             const std::atomic_bool *cancelled = nullptr) override;
};

class ProxyService {
public:
    explicit ProxyService(ProxyTransport &transport) : transport_(transport) {}
    AppResponse handle(const AppRequest &request,
                       const std::atomic_bool *cancelled = nullptr);
private:
    ProxyTransport &transport_;
};
struct YouTubeResolution {
    QJsonObject format;
    QString error;
};

class YouTubeResolver {
public:
    virtual ~YouTubeResolver() = default;
    virtual YouTubeResolution resolveAudioVideo(const QString &id) = 0;
};

class ProcessYouTubeResolver final : public YouTubeResolver {
public:
    explicit ProcessYouTubeResolver(QString executable = QStringLiteral("yt-dlp"),
                                    int timeoutMs = 20000);
    YouTubeResolution resolveAudioVideo(const QString &id) override;
private:
    QString executable_;
    int timeoutMs_;
};

class YouTubeService {
public:
    explicit YouTubeService(YouTubeResolver &resolver) : resolver_(resolver) {}
    AppResponse handle(const AppRequest &request);
private:
    YouTubeResolver &resolver_;
};

struct CastDevice {
    QString facility;
    QString id;
    QString name;
    QString host;
    QString location;
    QString type;
    QString icon;
    QStringList playerUIRoles;
    bool usePlayerUI = true;
    bool onlyHtml5Formats = false;
};

QJsonObject castDeviceToJson(const CastDevice &device);
class CastDiscoveryRegistry {
public:
    void collect(const CastDevice &device);
    QList<CastDevice> devices() const { return devices_; }
    std::optional<CastDevice> device(const QString &id) const;
    static std::optional<CastDevice> fromSsdpDescription(const QUrl &location,
                                                         const QByteArray &xml);
    static std::optional<CastDevice> fromMdnsRecords(const QJsonObject &records);
private:
    QList<CastDevice> devices_;
};

class CastPlayerSession {
public:
    virtual ~CastPlayerSession() = default;
    virtual QJsonValue invoke(const QString &method, const QJsonArray &args,
                              QJsonObject &mediaStatus) = 0;
};

class CastSessionFactory {
public:
    virtual ~CastSessionFactory() = default;
    virtual std::unique_ptr<CastPlayerSession> create(const CastDevice &device) = 0;
};

class CastTranscoder {
public:
    virtual ~CastTranscoder() = default;
    virtual AppResponse transcode(const AppRequest &request, bool fmp4) = 0;
};

class CastingService {
public:
    CastingService(CastDiscoveryRegistry &registry, CastSessionFactory &factory,
                   CastTranscoder &transcoder);
    AppResponse handle(const AppRequest &request);
private:
    CastDiscoveryRegistry &registry_;
    CastSessionFactory &factory_;
    CastTranscoder &transcoder_;
    std::map<QString, std::unique_ptr<CastPlayerSession>> sessions_;
    QHash<QString, QJsonObject> mediaStatus_;
};struct LocalAddonFile {
    QString path;
    QString name;
    qint64 length = 0;
    QString parsedName;
    QString type;
    QString imdbId;
    int season = 0;
    int episode = 0;
    int index = 0;
};

struct LocalAddonEntry {
    QString primaryKey;
    QString itemId;
    QString infoHash;
    QString name;
    QDateTime dateModified;
    QList<LocalAddonFile> files;
    QStringList sources;
};

class LocalFileDiscovery {
public:
    virtual ~LocalFileDiscovery() = default;
    virtual QStringList discover() = 0;
};
class LocalFileIndexer {
public:
    virtual ~LocalFileIndexer() = default;
    virtual std::optional<LocalAddonEntry> indexFile(const QString &path) = 0;
};

class LocalAddonService {
public:
    LocalAddonService(bool catalogEnabled, LocalFileDiscovery &discovery,
                      LocalFileIndexer &indexer);
    QByteArray manifestBytes() const;
    bool startIndexing(const QString &dbPath);
    qsizetype entryCount() const { return entries_.size(); }
    void setEngineUrl(QString engineUrl) { engineUrl_ = std::move(engineUrl); }
    AppResponse handle(const AppRequest &request) const;
private:
    bool loadStorage(const QString &dbPath);
    bool persistEntry(const QString &dbPath, const LocalAddonEntry &entry);
    QJsonObject catalogResponse() const;
    QJsonObject metaResponse(const QString &id) const;
    QJsonObject streamResponse(const QString &type, const QString &id) const;

    bool catalogEnabled_ = false;
    LocalFileDiscovery &discovery_;
    LocalFileIndexer &indexer_;
    QMap<QString, LocalAddonEntry> entries_;
    QString engineUrl_ = QStringLiteral("http://127.0.0.1:11470");
};
struct HttpsCertificate {
    QString domain;
    QByteArray key;
    QByteArray cert;
    QDateTime notBefore;
    QDateTime notAfter;
};

class CertificateTransport {
public:
    virtual ~CertificateTransport() = default;
    virtual QJsonObject request(const QUrl &endpoint, const QJsonObject &payload,
                                QString *error) = 0;
};

class QtCertificateTransport final : public CertificateTransport {
public:
    QJsonObject request(const QUrl &endpoint, const QJsonObject &payload,
                        QString *error) override;
};

class HttpsCertificateService {
public:
    HttpsCertificateService(CertificateTransport &transport, QString appPath,
                            QUrl apiEndpoint);
    std::optional<HttpsCertificate> requestNewCertificate(
        const QString &ipAddress, const QString &authKey, QString *error = nullptr);
    std::optional<HttpsCertificate> cachedCertificate(QString *error = nullptr) const;
private:
    std::optional<HttpsCertificate> parseAndValidate(const QJsonObject &value,
                                                     QString *error) const;
    QString certificatePath() const;

    CertificateTransport &transport_;
    QString appPath_;
    QUrl apiEndpoint_;
};

class NetworkInterfaceProvider {
public:
    virtual ~NetworkInterfaceProvider() = default;
    virtual QStringList ipv4Interfaces(QString *error = nullptr) = 0;
};

class SystemNetworkInterfaceProvider final : public NetworkInterfaceProvider {
public:
    QStringList ipv4Interfaces(QString *error = nullptr) override;
};

class HardwareAccelerationProfiler {
public:
    virtual ~HardwareAccelerationProfiler() = default;
    virtual QJsonValue profile(int httpPort) = 0;
};

class NetworkRouteService {
public:
    NetworkRouteService(HttpsCertificateService &certificates,
                        NetworkInterfaceProvider &interfaces,
                        HardwareAccelerationProfiler &profiler,
                        int httpPort, int httpsPort, QUrl webUiLocation);
    AppResponse handle(const AppRequest &request);
private:
    AppResponse heartbeat() const;
    AppResponse root(const AppRequest &request) const;
    AppResponse networkInfo();
    AppResponse deviceInfo();
    AppResponse profilerResult();
    AppResponse getHttps(const AppRequest &request);

    HttpsCertificateService &certificates_;
    NetworkInterfaceProvider &interfaces_;
    HardwareAccelerationProfiler &profiler_;
    int httpPort_ = 0;
    int httpsPort_ = 0;
    QUrl webUiLocation_;
};

} // namespace colosseum::server::app
