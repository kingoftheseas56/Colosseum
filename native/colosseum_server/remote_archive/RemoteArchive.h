#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace Colosseum::Server::RemoteArchive {

enum class RemoteErrorCode {
    None,
    Cancelled,
    InvalidRequest,
    NotFound,
    Transport,
    Protocol,
    Parser,
    Unsupported,
    RangeNotSatisfiable,
};

struct RemoteError {
    RemoteErrorCode code = RemoteErrorCode::None;
    QString message;
    int transportStatus = 0;

    [[nodiscard]] bool ok() const { return code == RemoteErrorCode::None; }
};

class CancellationToken {
public:
    void cancel() { m_cancelled.store(true, std::memory_order_release); }
    [[nodiscard]] bool isCancelled() const { return m_cancelled.load(std::memory_order_acquire); }

private:
    std::atomic_bool m_cancelled{false};
};

struct RemoteRead {
    QByteArray bytes;
    qint64 totalLength = -1;
    qint64 start = 0;
    qint64 end = -1;
    RemoteError error;
};

class RemoteRangeSource {
public:
    virtual ~RemoteRangeSource() = default;
    virtual qint64 length(RemoteError *error, CancellationToken *cancel = nullptr) = 0;
    virtual RemoteRead read(qint64 start, std::optional<qint64> endInclusive,
                            CancellationToken *cancel = nullptr) = 0;
    [[nodiscard]] virtual QString name() const = 0;
};

class MemoryRangeSource final : public RemoteRangeSource {
public:
    MemoryRangeSource(QString name, QByteArray bytes);
    qint64 length(RemoteError *error, CancellationToken *cancel = nullptr) override;
    RemoteRead read(qint64 start, std::optional<qint64> endInclusive,
                    CancellationToken *cancel = nullptr) override;
    [[nodiscard]] QString name() const override { return m_name; }

private:
    QString m_name;
    QByteArray m_bytes;
};

// W32: Qt-native counterpart to modules 1125/1281/1288/1296/1303. It discovers
// length, performs bounded or open-ended HTTP byte reads, follows redirects,
// normalizes errors, retries transient failures, and aborts on cancellation.
class HttpRangeSource final : public RemoteRangeSource {
public:
    HttpRangeSource(QNetworkAccessManager *network, QUrl url, QString displayName = {});
    qint64 length(RemoteError *error, CancellationToken *cancel = nullptr) override;
    RemoteRead read(qint64 start, std::optional<qint64> endInclusive,
                    CancellationToken *cancel = nullptr) override;
    [[nodiscard]] QString name() const override;

private:
    struct ReplyData;
    ReplyData perform(const QByteArray &method, const QByteArray &range,
                      CancellationToken *cancel, int maxAttempts);

    QNetworkAccessManager *m_network = nullptr;
    QUrl m_url;
    QString m_displayName;
    qint64 m_length = -1;
};

// Native equivalent of module 276 MergeStreams. The logical file is the exact
// concatenation of the ordered source volumes and remains random-access.
class ConcatenatedRangeSource final : public RemoteRangeSource {
public:
    ConcatenatedRangeSource(QString name,
                            QVector<std::shared_ptr<RemoteRangeSource>> parts);
    qint64 length(RemoteError *error, CancellationToken *cancel = nullptr) override;
    RemoteRead read(qint64 start, std::optional<qint64> endInclusive,
                    CancellationToken *cancel = nullptr) override;
    [[nodiscard]] QString name() const override { return m_name; }

private:
    bool ensureLengths(RemoteError *error, CancellationToken *cancel);

    QString m_name;
    QVector<std::shared_ptr<RemoteRangeSource>> m_parts;
    QVector<qint64> m_lengths;
    qint64 m_total = -1;
};

struct Request {
    QByteArray method = "GET";
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    // The HTTP connection owns this lifetime. Archive work may run on a
    // worker thread, so the cancellation signal is carried explicitly rather
    // than reaching back into the server/router types.
    std::shared_ptr<CancellationToken> cancellation;

    [[nodiscard]] QByteArray header(const QByteArray &name) const;
};

struct Response {
    int status = 200;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;

    [[nodiscard]] QByteArray header(const QByteArray &name) const;
    void setHeader(const QByteArray &name, const QByteArray &value);
};

enum class ArchiveKind { Rar, Zip, SevenZip, Tar, Tgz };

struct SourceSpec {
    QUrl url;
    qint64 bytes = -1;
};

struct ArchiveOptions {
    QStringList fileMustInclude;
    std::optional<int> fileIndex;
    std::optional<int> maxFiles;
};

using SourceFactory = std::function<std::shared_ptr<RemoteRangeSource>(const SourceSpec &, RemoteError *)>;

// W33-W35 lane-local route contract. Paths are relative to the mounted archive
// prefix, e.g. /create/key and /stream. Lead can adapt Request/Response to the
// shared server router without this lane editing shared HTTP/kernel files.
class ArchiveService {
public:
    explicit ArchiveService(SourceFactory factory = {});

    Response handle(ArchiveKind kind, const Request &request);
    [[nodiscard]] QVector<SourceSpec> sourcesForTesting(ArchiveKind kind, const QString &key) const;

private:
    struct StoredArchive {
        ArchiveKind kind = ArchiveKind::Zip;
        QVector<SourceSpec> sources;
    };
    struct SelectedEntry;

    Response handleCreate(ArchiveKind kind, const Request &request);
    Response handleStream(ArchiveKind kind, const Request &request);
    QString storeSources(ArchiveKind kind, QVector<SourceSpec> sources, const QString &requestedKey);
    std::shared_ptr<RemoteRangeSource> sourceFor(const SourceSpec &spec, RemoteError *error);
    std::shared_ptr<RemoteRangeSource> mergedSource(const StoredArchive &stored, RemoteError *error);
    SelectedEntry selectEntry(ArchiveKind kind, const StoredArchive &stored,
                              const ArchiveOptions &options, RemoteError *error,
                              CancellationToken *cancel);

    SourceFactory m_factory;
    std::unique_ptr<QNetworkAccessManager> m_ownedNetwork;
    QHash<QString, StoredArchive> m_store;
};

[[nodiscard]] QString archiveKindName(ArchiveKind kind);
[[nodiscard]] QByteArray contentTypeForName(const QString &name);
[[nodiscard]] ArchiveOptions parseArchiveOptions(const QString &json, RemoteError *error = nullptr);

} // namespace Colosseum::Server::RemoteArchive
