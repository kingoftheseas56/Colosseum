#pragma once
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>
namespace server1::media {
struct ProbeResult {
    QJsonObject value;
    QJsonObject error;
    bool cached = false;
    bool ok() const { return error.isEmpty(); }
};
struct RemoteProbeResult {
    QByteArray data;
    QString error;
};
struct RemoteProbeTransport {
    // M861 ffprobe bridge carries a result payload, not a listening ffmpeg
    // socket.
    std::function<void(const QString& id, const QStringList& arguments,
        std::function<void(const RemoteProbeResult&)>)>
        dispatch;
};
class Probe : public QObject {
public:
    using Callback = std::function<void(const ProbeResult&)>;
    explicit Probe(QString executable, QObject* parent = nullptr);
    Probe(QString executable, RemoteProbeTransport transport, QObject* parent = nullptr);
    ~Probe() override;
    static QJsonObject normalize(const QJsonObject& input);
    static QStringList arguments(const QString& url);
    quint64 start(const QString& url, Callback callback, int timeoutMs = 120000);
    bool cancel(quint64 id);
    bool contains(const QString& url) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class LegacyProbe : public QObject {
public:
    explicit LegacyProbe(QString executable, QObject* parent = nullptr);
    ~LegacyProbe() override;
    static QJsonObject normalize(const QByteArray& stderrData);
    quint64 start(const QString& url, Probe::Callback callback);
    bool cancel(quint64 id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace server1::media
