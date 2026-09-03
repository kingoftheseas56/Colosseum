#pragma once

#include "RemoteArchive.h"

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

namespace Colosseum::Server::RemoteArchive {

// W36 FTP --------------------------------------------------------------------

struct FtpProbe {
    qint64 size = -1;
    QString mime;
    QDateTime lastModified;
    bool supportsSeek = false;
};

class FtpBackend {
public:
    virtual ~FtpBackend() = default;
    virtual FtpProbe probe(const QUrl &url, RemoteError *error) = 0;
    virtual RemoteRead read(const QUrl &url, qint64 start, std::optional<qint64> endInclusive,
                            CancellationToken *cancel) = 0;
};

// Minimal blocking FTP/explicit-FTPS backend intended for the server worker
// thread. It implements USER/PASS, FEAT, MDTM, SIZE, EPSV/PASV, REST and RETR.
class QtFtpBackend final : public FtpBackend {
public:
    FtpProbe probe(const QUrl &url, RemoteError *error) override;
    RemoteRead read(const QUrl &url, qint64 start, std::optional<qint64> endInclusive,
                    CancellationToken *cancel) override;
};

class FtpService {
public:
    explicit FtpService(std::shared_ptr<FtpBackend> backend = {});
    Response handle(const Request &request);

private:
    struct Session {
        QUrl url;
        FtpProbe probe;
        QString streamPath;
    };

    Response create(const Request &request);
    Response stream(const Request &request);
    QString randomKey() const;

    std::shared_ptr<FtpBackend> m_backend;
    QHash<QString, Session> m_sessions;
};

// W36 NZB / NNTP -------------------------------------------------------------

struct NntpEndpoint {
    bool tls = false;
    QString host;
    quint16 port = 119;
    QString user;
    QString password;
    int connections = 1;
};

[[nodiscard]] std::optional<NntpEndpoint> parseNntpEndpoint(const QString &url);

struct NzbSegment {
    int number = 0;
    qint64 declaredBytes = 0;
    QString group;
    QString article;
    bool syntheticMissing = false;
};

struct NzbFile {
    QString subject;
    QString filename;
    QVector<NzbSegment> segments;
};

class NzbDocumentFetcher {
public:
    virtual ~NzbDocumentFetcher() = default;
    virtual QByteArray fetch(const QUrl &url, RemoteError *error) = 0;
};

class HttpNzbDocumentFetcher final : public NzbDocumentFetcher {
public:
    explicit HttpNzbDocumentFetcher(QNetworkAccessManager *network = nullptr);
    QByteArray fetch(const QUrl &url, RemoteError *error) override;

private:
    QNetworkAccessManager *m_network = nullptr;
    std::unique_ptr<QNetworkAccessManager> m_ownedNetwork;
};

class NntpBackend {
public:
    virtual ~NntpBackend() = default;
    virtual QByteArray fetch(const NntpEndpoint &endpoint, const NzbSegment &segment,
                             RemoteError *error) = 0;
};

class QtNntpBackend final : public NntpBackend {
public:
    QByteArray fetch(const NntpEndpoint &endpoint, const NzbSegment &segment,
                     RemoteError *error) override;
};

using ArchiveBootstrap = std::function<void(ArchiveKind kind, const QString &key,
                                             const QVector<SourceSpec> &sources)>;

class NzbService {
public:
    NzbService(std::shared_ptr<NzbDocumentFetcher> documents = {},
               std::shared_ptr<NntpBackend> nntp = {},
               ArchiveBootstrap archiveBootstrap = {});

    Response handle(const Request &request);
    void setLoopbackEndpoint(QString ip, quint16 port);

private:
    struct Session {
        QVector<NntpEndpoint> endpoints;
        QUrl nzbUrl;
        QString streamPath;
        QString fileName;
        QVector<NzbSegment> segments;
        qint64 size = 0;
        qint64 chunkSize = 0;
        qint64 lastChunkSize = 0;
        bool archive = false;
        ArchiveKind archiveKind = ArchiveKind::Zip;
        QVector<QString> archiveFileNames;
        QHash<QString, QVector<NzbSegment>> archiveSegments;
        QHash<QString, qint64> archiveSizes;
    };

    class NzbRangeSource;

    Response create(const Request &request);
    Response stream(const Request &request);
    Response redirect(const Request &request);
    bool initializeCandidate(const QVector<NntpEndpoint> &endpoints, const QUrl &nzbUrl,
                             const QString &key, Session *session, RemoteError *error);
    QByteArray fetchSegmentWithBackbones(const Session &session, const NzbSegment &segment,
                                         bool zeroFillMissing, RemoteError *error);
    QString randomKey() const;

    std::shared_ptr<NzbDocumentFetcher> m_documents;
    std::shared_ptr<NntpBackend> m_nntp;
    ArchiveBootstrap m_archiveBootstrap;
    QHash<QString, Session> m_sessions;
    QString m_loopbackIp = QStringLiteral("127.0.0.1");
    quint16 m_loopbackPort = 11470;
};

} // namespace Colosseum::Server::RemoteArchive
