#pragma once
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>
namespace server1::archive {
enum class SourceFamily { Rar, Zip, SevenZip, Tar, Gzip };
struct SourceDescriptor { QString url, name; std::optional<qint64> length; QString error; };
SourceDescriptor describeSource(SourceFamily family, const QJsonValue &url, const QList<qint64> &priorLengths = {}, bool isLast = false);
QByteArray sourceRangeHeader(qint64 length, const QJsonObject &range);
QString decompressUriQuery(QString input, QString *error = nullptr);
struct KeyResult { int status = 200; QString key, location, error; };
class ArchiveKeys {
public:
    explicit ArchiveKeys(SourceFamily family);
    KeyResult create(const QString &method, const QJsonValue &body, const QString &requestedKey = {}, const QString &lz = {});
    std::optional<QJsonArray> get(const QString &key) const;
private:
    SourceFamily family_; QHash<QString,QJsonArray> keys_;
};
struct PartRange { int part; qint64 start, end; };
// M277 arithmetic intentionally reports sum(part length - 1), not sum(length).
qint64 multipartLength(const QList<qint64> &lengths);
QList<PartRange> multipartRanges(const QList<qint64> &lengths, qint64 start, qint64 end, QString *error = nullptr);
struct ReadResult { QByteArray bytes; QString error; int status = 0; qint64 totalLength = -1; };
// Serialized Qt owner lane. At most 1 MiB per read, no eager body cache. Exact
// simultaneous reads share one request. Cancelling detaches before aborting.
class RemoteByteSource : public QObject {
public:
    using Token = quint64;
    using Completion = std::function<void(ReadResult)>;
    explicit RemoteByteSource(QUrl url, QObject *parent = nullptr);
    ~RemoteByteSource() override;
    Token read(qint64 offset, qint64 count, Completion completion);
    Token discoverLength(Completion completion);
    void cancel(Token token);
    int pendingRequests() const;
private:
    struct State; std::shared_ptr<State> state_;
};
}
