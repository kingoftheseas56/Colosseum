#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace Colosseum::Server::EngineFs {

inline QString canonicalInfoHash(const QString& infoHash)
{
    return infoHash.toLower();
}

struct EngineFsWireSnapshot {
    bool peerChoking = true;
    int requests = 0;
    QString address;
    bool amInterested = false;
    bool isSeeder = false;
    double downSpeed = 0.0;
    double upSpeed = 0.0;
};

struct EngineFsFileSnapshot {
    qint64 length = 0;
    QString name;
    qint64 offset = 0;
    QString path;
    QJsonObject extra;
};

struct EngineFsBackendSnapshot {
    bool metadataReady = false;
    std::optional<QString> torrentName;
    qint64 pieceLength = 0;
    QVector<EngineFsFileSnapshot> files;
    QSet<int> availablePieces;
    QVector<QJsonObject> selections;
    QVector<EngineFsWireSnapshot> wires;
    int queued = 0;
    int uniquePeers = 0;
    int connectionTries = 0;
    bool swarmPaused = false;
    int swarmConnections = 0;
    int swarmSize = 0;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    double downloadSpeed = 0.0;
    double uploadSpeed = 0.0;
    std::optional<QJsonArray> peerSearchSources;
    std::optional<bool> peerSearchRunning;
};

struct EngineFsBackendCallbacks {
    std::function<void(const QString&)> onError;
    std::function<void(int)> onInvalidPiece;
};

class IEngineFsBackend
{
public:
    virtual ~IEngineFsBackend() = default;
    virtual void resumeSwarm() = 0;
    virtual void pauseSwarm() = 0;
    virtual void destroy(std::function<void()> done) = 0;
    virtual void whenReady(std::function<void(const QVariant&)> callback) = 0;
    virtual void setCallbacks(EngineFsBackendCallbacks callbacks) = 0;
    virtual EngineFsBackendSnapshot statisticsSnapshot() const = 0;
};

class IEngineFsBackendFactory
{
public:
    virtual ~IEngineFsBackendFactory() = default;
    virtual std::shared_ptr<IEngineFsBackend> create(const QString& canonicalHash,
                                                     const QJsonObject& effectiveOptions) = 0;
};

class IEngineFsTimerScheduler
{
public:
    using TimerId = quint64;
    virtual ~IEngineFsTimerScheduler() = default;
    virtual TimerId schedule(std::chrono::milliseconds delay, std::function<void()> callback) = 0;
    virtual void cancel(TimerId id) = 0;
};

using TimerId = IEngineFsTimerScheduler::TimerId;

struct EngineFsEvent {
    QString name;
    QVariantList args;
};

struct EngineFsTimeouts {
    std::chrono::milliseconds stream{20000};
    std::chrono::milliseconds engine{120000};
};

class EngineFsControlPlane
{
public:
    using ReadyCallback = std::function<void(const std::shared_ptr<IEngineFsBackend>&)>;
    using DoneCallback = std::function<void()>;
    using EventSink = std::function<void(const EngineFsEvent&)>;

    EngineFsControlPlane(IEngineFsBackendFactory& factory,
                         IEngineFsTimerScheduler& scheduler,
                         EventSink sink = {},
                         EngineFsTimeouts timeouts = {});
    ~EngineFsControlPlane();

    void createEngine(const QString& infoHash,
                      const QJsonObject& effectiveOptions,
                      ReadyCallback ready = {});
    void removeEngine(const QString& infoHash, DoneCallback done = {});
    void keepConcurrency(const QString& incomingHash, int concurrency, DoneCallback done = {});

    bool exists(const QString& infoHash) const;
    QStringList list() const;
    QVector<QJsonObject> selections(const QString& infoHash) const;

    void noteStreamOpen(const QString& infoHash, int fileIndex);
    void noteStreamClose(const QString& infoHash, int fileIndex);
    void noteStreamCreated(const QString& infoHash, int fileIndex);
    void noteStreamCached(const QString& infoHash, int fileIndex);

    void pauseSwarm(const QString& infoHash);
    void resumeSwarm(const QString& infoHash);

    QJsonValue statistics(const QString& infoHash,
                          std::optional<int> fileIndex = std::nullopt) const;
    QJsonObject statisticsAll() const;

private:
    struct Entry {
        std::shared_ptr<IEngineFsBackend> backend;
        QJsonObject options;
    };

    using CounterMap = QHash<QString, double>;
    using TimerMap = QHash<QString, TimerId>;
    void emitEvent(const QString& name, QVariantList args = {});
    void emitJoined(const QString& name, const QVariantList& args);
    static QString jsJoinText(const QVariant& value);

    void incrementCounter(CounterMap& counters,
                          TimerMap& timers,
                          const QString& key,
                          std::function<void()> onPositive);
    void decrementCounter(CounterMap& counters,
                          TimerMap& timers,
                          const QString& key,
                          std::chrono::milliseconds timeout,
                          std::function<void()> onZero);
    void cancelTimers(TimerMap& timers);

    QJsonObject statisticsFor(const QString& hash,
                              const Entry& entry,
                              std::optional<int> fileIndex) const;
    static QJsonObject fileJson(const EngineFsFileSnapshot& file);
    static QJsonArray selectionsJson(const QVector<QJsonObject>& selections);
    static QJsonArray wiresJson(const QVector<EngineFsWireSnapshot>& wires);

    IEngineFsBackendFactory& factory_;
    IEngineFsTimerScheduler& scheduler_;
    EventSink sink_;
    EngineFsTimeouts timeouts_;
    QHash<QString, Entry> entries_;
    QStringList order_;
    CounterMap streamCounters_;
    CounterMap engineCounters_;
    CounterMap cacheCounters_;
    TimerMap streamTimers_;
    TimerMap engineTimers_;
    TimerMap cacheTimers_;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};

} // namespace Colosseum::Server::EngineFs
