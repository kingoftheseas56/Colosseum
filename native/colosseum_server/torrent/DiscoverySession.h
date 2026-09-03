#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <functional>
#include <optional>

namespace colosseum::server::torrent {

// Stremio 4.20.17 module 564 product wiring overrides module 172's generic
// max=200 fallback with the shipped min=40/max=150 discovery gate.
struct PeerSearchOptions {
    int minQueuedPeers = 40;
    int maxQueuedPeers = 150;
};

enum class DiscoverySourceKind { Dht, Tracker };
enum class DiscoveryActionKind { StartDht, StartTracker, AbortDht, CloseDht };

struct DiscoveryAction {
    DiscoveryActionKind kind;
    int sourceIndex = -1;
    QString value;
    qint64 delayMs = 0;
};

struct DiscoverySourceStats {
    DiscoverySourceKind kind = DiscoverySourceKind::Dht;
    QString url;
    int numFound = 0;
    int numFoundUniq = 0;
    int numRequests = 0;
    qint64 lastStartedMs = -1;
};

class PeerDiscoveryCoordinator {
public:
    PeerDiscoveryCoordinator(QStringList sources, PeerSearchOptions options, qint64 nowMs,
                             std::function<void(const QString&)> admitPeer = {});

    void onSwarmState(int queuedPeers, bool swarmPaused, qint64 nowMs);
    void advanceTo(qint64 nowMs);
    void reportPeer(int sourceIndex, const QString& address);
    void run(qint64 nowMs);
    void pause(qint64 nowMs);
    void close();

    bool isRunning() const { return m_running; }
    QVector<DiscoverySourceStats> stats() const;
    QVector<DiscoveryAction> takeActions();

    static QStringList productDefaultSources(const QString& infoHash);
    static QStringList sourcesForTorrent(const QStringList& announceUrls,
                                         const QString& infoHash,
                                         const QStringList& configuredSources);

private:
    struct SourceState {
        DiscoverySourceKind kind = DiscoverySourceKind::Dht;
        QString url;
        QString value;
        int numFound = 0;
        int numFoundUniq = 0;
        int numRequests = 0;
        qint64 lastStartedMs = -1;
        qint64 dhtPendingAtMs = -1;
        bool dhtActive = false;
    };

    QVector<SourceState> m_sources;
    PeerSearchOptions m_options;
    std::function<void(const QString&)> m_admitPeer;
    QSet<QString> m_uniquePeers;
    QVector<DiscoveryAction> m_actions;
    bool m_running = false;
    bool m_closed = false;
    qint64 m_nextRefreshMs = 0;

    static std::optional<SourceState> parseSource(const QString& source);
    void advanceDht(qint64 nowMs);
};

struct SwarmSessionSettings {
    int maxConnections = 55;
    qint64 handshakeTimeoutMs = 20000;
    qint64 requestTimeoutMs = 4000;
    qint64 connectTimeoutMs = 3000;
    std::array<qint64, 3> reconnectWaitMs{4000, 8000, 12000};

    static SwarmSessionSettings stremioProductDefaults();
};

class SwarmSessionPolicy {
public:
    explicit SwarmSessionPolicy(SwarmSessionSettings settings = {}, qint64 nowMs = 0);

    bool addPeer(const QString& address);
    void removePeer(const QString& address);
    bool setPriority(const QString& address, int level);
    void pause() { m_paused = true; }
    void resume() { m_paused = false; }

    std::optional<QString> beginNextConnection();
    void onTransportConnected(const QString& address);
    void onHandshakeSucceeded(const QString& address);
    void onConnectionClosed(const QString& address, qint64 nowMs);
    void advanceTo(qint64 nowMs);
    QStringList takeTimedOutPeers();
    void setPeerChoking(const QString& address, bool choking);

    void recordDownload(qint64 bytes, qint64 nowMs);
    void recordUpload(qint64 bytes, qint64 nowMs);
    double downloadSpeed(qint64 nowMs);
    double uploadSpeed(qint64 nowMs);

    bool hasPeer(const QString& address) const { return m_peers.contains(address); }
    bool isPaused() const { return m_paused; }
    int queuedCount() const;
    int connectionCount() const { return m_connections; }
    int wireCount() const;
    int unchokedPeerCount() const;
    qint64 tries() const { return m_tries; }
    qint64 downloadedBytes() const { return m_downloaded; }
    qint64 uploadedBytes() const { return m_uploaded; }
    qint64 retryDueMs(const QString& address) const;
    const SwarmSessionSettings& settings() const { return m_settings; }

private:
    class SpeedMeter {
    public:
        explicit SpeedMeter(qint64 nowMs);
        double sample(qint64 delta, qint64 nowMs);
    private:
        static constexpr int kSize = 20;
        QVector<qint64> m_buffer{0};
        int m_pointer = 1;
        int m_lastTick = 0;
    };

    struct PeerState {
        int priority = 0;
        bool queued = true;
        bool connecting = false;
        bool wire = false;
        bool reconnectEligible = false;
        bool peerChoking = true;
        int retries = 0;
        qint64 retryDue = -1;
        qint64 connectDeadline = -1;
        qint64 handshakeDeadline = -1;
        quint64 sequence = 0;
    };

    SwarmSessionSettings m_settings;
    QHash<QString, PeerState> m_peers;
    bool m_paused = false;
    int m_connections = 0;
    qint64 m_tries = 0;
    qint64 m_downloaded = 0;
    qint64 m_uploaded = 0;
    quint64 m_sequence = 0;
    qint64 m_nowMs = 0;
    QStringList m_timedOutPeers;
    SpeedMeter m_downloadMeter;
    SpeedMeter m_uploadMeter;

    static bool validAddress(const QString& address);
    void queuePeer(PeerState& peer);
};

} // namespace colosseum::server::torrent
