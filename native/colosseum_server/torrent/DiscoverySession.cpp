#include "DiscoverySession.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace colosseum::server::torrent {
namespace {

constexpr qint64 kDhtStartDelayMs = 1500;
constexpr qint64 kDiscoveryRefreshMs = 30000;
constexpr qint64 kSpeedTickMs = 250;

const QStringList& productTrackers()
{
    static const QStringList trackers{
        QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
        QStringLiteral("udp://open.demonii.com:1337/announce"),
        QStringLiteral("udp://open.stealth.si:80/announce"),
        QStringLiteral("https://torrent.tracker.durukanbal.com:443/announce"),
        QStringLiteral("udp://wepzone.net:6969/announce"),
        QStringLiteral("udp://tracker.wepzone.net:6969/announce"),
        QStringLiteral("udp://tracker.torrent.eu.org:451/announce"),
        QStringLiteral("udp://tracker.theoks.net:6969/announce"),
        QStringLiteral("udp://tracker.t-1.org:6969/announce"),
        QStringLiteral("udp://tracker.darkness.services:6969/announce"),
        QStringLiteral("udp://tracker-udp.gbitt.info:80/announce"),        QStringLiteral("udp://t.overflow.biz:6969/announce"),
        QStringLiteral("udp://open.dstud.io:6969/announce"),
        QStringLiteral("udp://explodie.org:6969/announce"),
        QStringLiteral("udp://exodus.desync.com:6969/announce"),
        QStringLiteral("udp://bittorrent-tracker.e-n-c-r-y-p-t.net:1337/announce"),
        QStringLiteral("https://tracker.zhuqiy.com:443/announce"),
        QStringLiteral("https://tracker.pmman.tech:443/announce"),
        QStringLiteral("https://tracker.moeblog.cn:443/announce"),
        QStringLiteral("https://tracker.bt4g.com:443/announce")
    };
    return trackers;
}

} // namespace

std::optional<PeerDiscoveryCoordinator::SourceState>
PeerDiscoveryCoordinator::parseSource(const QString& source)
{
    SourceState state;
    state.url = source;
    if (source.startsWith(QStringLiteral("dht:"))) {
        state.kind = DiscoverySourceKind::Dht;
        state.value = source.mid(4);
    } else if (source.startsWith(QStringLiteral("tracker:"))) {
        state.kind = DiscoverySourceKind::Tracker;
        state.value = source.mid(8);
    } else {
        return std::nullopt;
    }
    if (state.value.isEmpty()) return std::nullopt;
    return state;
}

// Upstream modules 613/614/625: PeerSearch starts immediately, trackers announce
// immediately, DHT waits 1500 ms, refresh runs every 30 s, and peer uniqueness is global.
PeerDiscoveryCoordinator::PeerDiscoveryCoordinator(
    QStringList sources, PeerSearchOptions options, qint64 nowMs,
    std::function<void(const QString&)> admitPeer)
    : m_options(options), m_admitPeer(std::move(admitPeer)),
      m_nextRefreshMs(nowMs + kDiscoveryRefreshMs)
{
    for (const auto& source : sources) {
        if (auto parsed = parseSource(source)) m_sources.push_back(std::move(*parsed));
    }
    run(nowMs);
}

void PeerDiscoveryCoordinator::run(qint64 nowMs)
{
    if (m_closed) return;
    m_running = true;
    for (int i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        source.lastStartedMs = nowMs;
        if (source.kind == DiscoverySourceKind::Tracker) {
            ++source.numRequests;
            m_actions.push_back({DiscoveryActionKind::StartTracker, i, source.value, 0});
        } else if (!source.dhtActive && source.dhtPendingAtMs < 0) {
            source.dhtPendingAtMs = nowMs + kDhtStartDelayMs;
        }
    }
}
void PeerDiscoveryCoordinator::pause(qint64 nowMs)
{
    Q_UNUSED(nowMs);
    if (!m_running || m_closed) return;
    m_running = false;
    for (int i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        if (source.kind != DiscoverySourceKind::Dht) continue;
        if (source.dhtPendingAtMs >= 0) source.dhtPendingAtMs = -1;
        if (source.dhtActive) {
            m_actions.push_back({DiscoveryActionKind::AbortDht, i, source.value,
                                 kDhtStartDelayMs});
            source.dhtActive = false;
        }
    }
}

void PeerDiscoveryCoordinator::onSwarmState(int queuedPeers, bool swarmPaused,
                                             qint64 nowMs)
{
    if (m_closed) return;
    if (swarmPaused) {
        if (m_running) pause(nowMs);
        return;
    }
    if (queuedPeers < m_options.minQueuedPeers && !m_running) {
        run(nowMs);
    } else if (queuedPeers > m_options.maxQueuedPeers && m_running) {
        pause(nowMs);
    }
}
void PeerDiscoveryCoordinator::advanceDht(qint64 nowMs)
{
    if (!m_running || m_closed) return;
    for (int i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        if (source.kind != DiscoverySourceKind::Dht) continue;
        if (source.dhtPendingAtMs < 0 || source.dhtPendingAtMs > nowMs) continue;
        source.dhtPendingAtMs = -1;
        source.dhtActive = true;
        ++source.numRequests;
        m_actions.push_back({DiscoveryActionKind::StartDht, i, source.value, 0});
    }
}

void PeerDiscoveryCoordinator::advanceTo(qint64 nowMs)
{
    if (m_closed) return;
    advanceDht(nowMs);
    while (m_nextRefreshMs <= nowMs) {
        const qint64 refreshAt = m_nextRefreshMs;
        m_nextRefreshMs += kDiscoveryRefreshMs;
        if (m_running) run(refreshAt);
        advanceDht(nowMs);
    }
}

void PeerDiscoveryCoordinator::reportPeer(int sourceIndex, const QString& address)
{
    if (m_closed || sourceIndex < 0 || sourceIndex >= m_sources.size()) return;
    auto& source = m_sources[sourceIndex];
    ++source.numFound;
    if (!m_uniquePeers.contains(address)) {
        m_uniquePeers.insert(address);
        ++source.numFoundUniq;
    }
    if (m_admitPeer) m_admitPeer(address);
}
QVector<DiscoverySourceStats> PeerDiscoveryCoordinator::stats() const
{
    QVector<DiscoverySourceStats> result;
    result.reserve(m_sources.size());
    for (const auto& source : m_sources) {
        result.push_back({source.kind, source.url, source.numFound,
                          source.numFoundUniq, source.numRequests,
                          source.lastStartedMs});
    }
    return result;
}

QVector<DiscoveryAction> PeerDiscoveryCoordinator::takeActions()
{
    QVector<DiscoveryAction> result;
    result.swap(m_actions);
    return result;
}

void PeerDiscoveryCoordinator::close()
{
    if (m_closed) return;
    m_running = false;
    m_closed = true;
    for (int i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        source.dhtPendingAtMs = -1;
        if (source.kind == DiscoverySourceKind::Dht) {
            m_actions.push_back({DiscoveryActionKind::CloseDht, i, source.value, 0});
            source.dhtActive = false;
        }
    }
}

QStringList PeerDiscoveryCoordinator::productDefaultSources(const QString& infoHash)
{
    QStringList result;
    result.reserve(productTrackers().size() + 1);    for (const auto& tracker : productTrackers())
        result.push_back(QStringLiteral("tracker:") + tracker);
    result.push_back(QStringLiteral("dht:") + infoHash);
    return result;
}

QStringList PeerDiscoveryCoordinator::sourcesForTorrent(
    const QStringList& announceUrls, const QString& infoHash,
    const QStringList& configuredSources)
{
    if (announceUrls.isEmpty()) return configuredSources;
    QStringList result;
    result.reserve(announceUrls.size() + 1);
    for (const auto& announce : announceUrls)
        result.push_back(QStringLiteral("tracker:") + announce);
    result.push_back(QStringLiteral("dht:") + infoHash);
    return result;
}

// Upstream modules 105/564/820: shipped settings supply 55 connections,
// 20 s handshake and 4 s request timeouts; p2p-swarm retains a 3 s connect timeout.
SwarmSessionSettings SwarmSessionSettings::stremioProductDefaults()
{
    SwarmSessionSettings settings;
    settings.maxConnections = 55;
    settings.handshakeTimeoutMs = 20000;
    settings.requestTimeoutMs = 4000;
    settings.connectTimeoutMs = 3000;
    settings.reconnectWaitMs = {4000, 8000, 12000};
    return settings;
}

SwarmSessionPolicy::SpeedMeter::SpeedMeter(qint64 nowMs)
{
    const int tick = static_cast<int>((nowMs / kSpeedTickMs) & 0xffff);
    m_lastTick = (tick - 1) & 0xffff;
}
double SwarmSessionPolicy::SpeedMeter::sample(qint64 delta, qint64 nowMs)
{
    const int tick = static_cast<int>((nowMs / kSpeedTickMs) & 0xffff);
    int distance = (tick - m_lastTick) & 0xffff;
    distance = std::min(distance, kSize);
    m_lastTick = tick;

    while (distance-- > 0) {
        if (m_pointer == kSize) m_pointer = 0;
        const int previous = m_pointer == 0 ? kSize - 1 : m_pointer - 1;
        const qint64 previousValue = previous < m_buffer.size() ? m_buffer[previous] : 0;
        if (m_pointer >= m_buffer.size()) m_buffer.resize(m_pointer + 1);
        m_buffer[m_pointer] = previousValue;
        ++m_pointer;
    }

    const int current = m_pointer == 0 ? kSize - 1 : m_pointer - 1;
    if (current >= m_buffer.size()) m_buffer.resize(current + 1);
    if (delta > 0) m_buffer[current] += delta;
    const qint64 top = m_buffer[current];
    const qint64 bottom = m_buffer.size() < kSize
        ? 0 : m_buffer[m_pointer == kSize ? 0 : m_pointer];
    return m_buffer.size() < 4
        ? static_cast<double>(top)
        : 4.0 * static_cast<double>(top - bottom) / m_buffer.size();
}

// Upstream modules 820/416: numeric-priority FIFO connection queues, successful-
// handshake-only reconnects at 4/8/12 s, explicit peer counters, and a 5 s speed window.
SwarmSessionPolicy::SwarmSessionPolicy(SwarmSessionSettings settings, qint64 nowMs)
    : m_settings(std::move(settings)), m_nowMs(nowMs),
      m_downloadMeter(nowMs), m_uploadMeter(nowMs)
{
    if (m_settings.maxConnections <= 0) m_settings.maxConnections = 1;
}

bool SwarmSessionPolicy::validAddress(const QString& address)
{
    const int colon = address.indexOf(QLatin1Char(':'));
    if (colon <= 0) return false;
    bool ok = false;
    const int port = address.mid(colon + 1).toInt(&ok);
    return ok && port > 0 && port < 65535;
}

void SwarmSessionPolicy::queuePeer(PeerState& peer)
{
    peer.queued = true;
    peer.connecting = false;
    peer.wire = false;
    peer.retryDue = -1;
    peer.connectDeadline = -1;
    peer.handshakeDeadline = -1;
    peer.sequence = ++m_sequence;
}

bool SwarmSessionPolicy::addPeer(const QString& address)
{
    if (!validAddress(address) || m_peers.contains(address)) return false;
    PeerState peer;
    peer.sequence = ++m_sequence;
    m_peers.insert(address, peer);
    return true;
}

void SwarmSessionPolicy::removePeer(const QString& address)
{
    const auto it = m_peers.find(address);
    if (it == m_peers.end()) return;
    if ((it->connecting || it->wire) && m_connections > 0) --m_connections;
    m_peers.erase(it);
}

bool SwarmSessionPolicy::setPriority(const QString& address, int level)
{
    auto it = m_peers.find(address);
    if (it == m_peers.end()) return false;
    if (it->priority == level) return true;
    it->priority = level;
    if (it->queued) it->sequence = ++m_sequence;
    return true;
}
std::optional<QString> SwarmSessionPolicy::beginNextConnection()
{
    if (m_paused || m_connections >= m_settings.maxConnections) return std::nullopt;

    auto best = m_peers.end();
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (!it->queued) continue;
        if (best == m_peers.end()
            || it->priority > best->priority
            || (it->priority == best->priority && it->sequence < best->sequence)) {
            best = it;
        }
    }
    if (best == m_peers.end()) return std::nullopt;

    best->queued = false;
    best->connecting = true;
    best->retryDue = -1;
    best->connectDeadline = m_nowMs + m_settings.connectTimeoutMs;
    best->handshakeDeadline = m_nowMs + m_settings.handshakeTimeoutMs;
    ++m_connections;
    ++m_tries;
    return best.key();
}

void SwarmSessionPolicy::onTransportConnected(const QString& address)
{
    auto it = m_peers.find(address);
    if (it == m_peers.end() || !it->connecting) return;
    it->connectDeadline = -1;
}

void SwarmSessionPolicy::onHandshakeSucceeded(const QString& address)
{
    auto it = m_peers.find(address);
    if (it == m_peers.end()) return;
    it->connecting = false;
    it->wire = true;
    it->reconnectEligible = true;
    it->retries = 0;
    it->retryDue = -1;
    it->connectDeadline = -1;
    it->handshakeDeadline = -1;
    it->peerChoking = true;
}

void SwarmSessionPolicy::onConnectionClosed(const QString& address, qint64 nowMs)
{
    auto it = m_peers.find(address);
    if (it == m_peers.end()) return;
    m_nowMs = std::max(m_nowMs, nowMs);
    if ((it->connecting || it->wire) && m_connections > 0) --m_connections;
    it->connecting = false;
    it->wire = false;
    it->connectDeadline = -1;
    it->handshakeDeadline = -1;
    it->peerChoking = true;
    if (it->reconnectEligible
        && it->retries < static_cast<int>(m_settings.reconnectWaitMs.size())) {
        const qint64 wait = m_settings.reconnectWaitMs[static_cast<size_t>(it->retries)];
        ++it->retries;
        it->retryDue = nowMs + wait;
        it->queued = false;
        return;
    }
    m_peers.erase(it);
}

void SwarmSessionPolicy::advanceTo(qint64 nowMs)
{
    m_nowMs = std::max(m_nowMs, nowMs);
    QStringList timedOut;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        const bool connectTimedOut = it->connectDeadline >= 0 && it->connectDeadline <= nowMs;
        const bool handshakeTimedOut = it->handshakeDeadline >= 0 && it->handshakeDeadline <= nowMs;
        if (connectTimedOut || handshakeTimedOut) timedOut.push_back(it.key());
    }
    for (const auto& address : timedOut) {
        m_timedOutPeers.push_back(address);
        onConnectionClosed(address, nowMs);
    }
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (it->retryDue >= 0 && it->retryDue <= nowMs) queuePeer(it.value());
    }
}

QStringList SwarmSessionPolicy::takeTimedOutPeers()
{
    QStringList result;
    result.swap(m_timedOutPeers);
    return result;
}

void SwarmSessionPolicy::setPeerChoking(const QString& address, bool choking)
{
    auto it = m_peers.find(address);
    if (it != m_peers.end() && it->wire) it->peerChoking = choking;
}

void SwarmSessionPolicy::recordDownload(qint64 bytes, qint64 nowMs)
{
    if (bytes <= 0) return;
    m_downloaded += bytes;
    m_downloadMeter.sample(bytes, nowMs);
}

void SwarmSessionPolicy::recordUpload(qint64 bytes, qint64 nowMs)
{
    if (bytes <= 0) return;
    m_uploaded += bytes;
    m_uploadMeter.sample(bytes, nowMs);
}

double SwarmSessionPolicy::downloadSpeed(qint64 nowMs)
{
    return m_downloadMeter.sample(0, nowMs);
}

double SwarmSessionPolicy::uploadSpeed(qint64 nowMs)
{
    return m_uploadMeter.sample(0, nowMs);
}
int SwarmSessionPolicy::queuedCount() const
{
    int count = 0;
    for (const auto& peer : m_peers) if (peer.queued) ++count;
    return count;
}

int SwarmSessionPolicy::wireCount() const
{
    int count = 0;
    for (const auto& peer : m_peers) if (peer.wire) ++count;
    return count;
}

int SwarmSessionPolicy::unchokedPeerCount() const
{
    int count = 0;
    for (const auto& peer : m_peers) {
        if (peer.wire && !peer.peerChoking) ++count;
    }
    return count;
}

qint64 SwarmSessionPolicy::retryDueMs(const QString& address) const
{
    const auto it = m_peers.constFind(address);
    return it == m_peers.constEnd() ? -1 : it->retryDue;
}

} // namespace colosseum::server::torrent
