#include "VinylTorrentProvider.h"

#include "player/streamserver.h"
#include "torrent/MusicTorrentQueryPlanner.h"
#include "torrent/TankorentSearchService.h"

#include <QNetworkAccessManager>
#include <QUuid>
#include <QDebug>
#include <QJsonDocument>
#include <algorithm>

VinylTorrentProvider::VinylTorrentProvider(QNetworkAccessManager *network, StreamServer *stream,
                                         QObject *parent, int deadlineMs)
    : QObject(parent), m_stream(stream), m_deadlineMs(qMax(1, deadlineMs))
{
    m_search = new TankorentSearchService(network, this);
    wire();
}

VinylTorrentProvider::VinylTorrentProvider(TankorentSearchService *search, StreamServer *stream,
                                         QObject *parent, int deadlineMs)
    : QObject(parent), m_search(search), m_stream(stream), m_deadlineMs(qMax(1, deadlineMs))
{
    wire();
}

VinylTorrentProvider::~VinylTorrentProvider() { cancel(); }
void VinylTorrentProvider::trace(const QString &stage,const QVariantMap &facts) const {
    if(!qEnvironmentVariableIsSet("COLOSSEUM_VINYL_TRACE"))return;
    auto data=facts;data["stage"]=stage;data["request"]=m_active;data["elapsedMs"]=m_elapsed.isValid()?m_elapsed.elapsed():0;
    qInfo().noquote()<<"[vinyl-torrent]"<<QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
}

void VinylTorrentProvider::wire()
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        finish({}, QStringLiteral("Music torrent discovery timed out."));
    });
    if (m_search) {
        connect(m_search, &TankorentSearchService::resultsReady, this,
                [this](const QString &handle, const QList<TorrentResult> &rows) {
            if (m_active < 0 || handle != m_searchHandle) return;
            trace("search-results",{{"rows",rows.size()}});
            for (auto row : rows) {
                row.infoHash = canonicalizeInfoHash(row.infoHash);
                if (row.infoHash.isEmpty() || m_seen.contains(row.infoHash)) continue;
                const auto duplicate = std::find_if(m_candidates.cbegin(), m_candidates.cend(),
                    [&row](const TorrentResult &other) { return other.infoHash == row.infoHash; });
                if (duplicate == m_candidates.cend()) m_candidates.append(row);
            }
            std::stable_sort(m_candidates.begin(), m_candidates.end(),
                [](const TorrentResult &a, const TorrentResult &b) { return a.seeders > b.seeders; });
            // Search fanout can return many rows; never retain an unbounded list.
            while (m_candidates.size() > 5) m_candidates.removeLast();
            tryNextCandidate();
        }, Qt::QueuedConnection);
        connect(m_search, &TankorentSearchService::searchFinished, this,
                [this](const QString &handle) {
            if (m_active >= 0 && handle == m_searchHandle) finishSearch();
        }, Qt::QueuedConnection);
    }
    if (m_stream) {
        connect(m_stream, &StreamServer::torrentManifestReady, this,
                [this](const QString &token, const QString &hash, const QVariantList &rows) {
            auto pending = m_pending.find(token);
            if (m_active < 0 || pending == m_pending.end() || hash != pending->torrent.infoHash) return;
            QStringList paths;for(const auto &row:rows)if(paths.size()<24)paths.append(row.toMap().value("path").toString());
            trace("manifest",{{"hash",hash},{"files",rows.size()},{"paths",paths}});
            QList<MusicTorrentFile> files;
            for (const auto &value : rows) {
                const auto row = value.toMap();
                MusicTorrentFile file;
                file.index = row.value(QStringLiteral("fileIdx"), -1).toInt();
                file.path = row.value(QStringLiteral("path")).toString();
                if (file.path.isEmpty()) file.path = row.value(QStringLiteral("name")).toString();
                // Transport metadata is a file manifest, never authoritative ID3 tags.
                files.append(file);
            }
            pending->match = MusicTorrentMatcher::pick(m_track, files);
            const auto match = pending->match;
            trace("match",{{"fileIdx",match.index},{"reason",match.error},{"path",match.path}});
            if (match.index < 0) { retireCandidate(token); tryNextCandidate(); return; }
            m_stream->requestFile(token, hash, match.index);
        });
        connect(m_stream, &StreamServer::torrentStreamReady, this,
                [this](const QString &token, const QString &url, const QString &hash, int index) {
            const auto pending = m_pending.constFind(token);
            if (m_active < 0 || pending == m_pending.cend() || hash != pending->torrent.infoHash ||
                index != pending->match.index) return;
            if (url.isEmpty()) { retireCandidate(token); tryNextCandidate(); return; }
            const QString path = pending->match.path;
            finish({{QStringLiteral("url"), url}, {QStringLiteral("infoHash"), hash},
                    {QStringLiteral("fileIdx"), index}, {QStringLiteral("filePath"), path},
                    {QStringLiteral("provider"), QStringLiteral("torrent")},
                    {QStringLiteral("qualityLabel"), QStringLiteral("Torrent audio")}}, {});
        });
        connect(m_stream, &StreamServer::torrentRequestError, this,
                [this](const QString &token, const QString &) {
            if (m_active >= 0 && m_pending.contains(token)) {
                retireCandidate(token);
                tryNextCandidate();
            }
        });
    }
}

int VinylTorrentProvider::resolve(const QVariantMap &track)
{
    cancel();
    const int id = m_active = ++m_serial;
    m_elapsed.start();
    m_track = {};
    m_track.artist = track.value(QStringLiteral("artist")).toString().trimmed();
    m_track.album = track.value(QStringLiteral("album")).toString().trimmed();
    m_track.title = track.value(QStringLiteral("title")).toString().trimmed();
    m_track.trackNumber = track.value(QStringLiteral("trackNumber")).toInt();
    m_track.discNumber = track.value(QStringLiteral("discNumber")).toInt();
    m_track.durationMs = track.value(QStringLiteral("durationMs")).toLongLong();
    m_track.releaseYear = track.value(QStringLiteral("releaseYear")).toInt();
    m_queries = MusicTorrentQueryPlanner::automaticQueries(m_track);
    trace("resolve",{{"queries",m_queries},{"title",m_track.title},{"durationMs",m_track.durationMs}});
    m_queryIndex = m_attempted = 0;
    m_seen.clear();
    m_candidates.clear();
    m_deadline.start(m_deadlineMs);
    // Public completion is always asynchronous, including validation failure.
    QTimer::singleShot(0, this, [this, id] {
        if (m_active != id) return;
        if (!m_search || !m_stream || m_queries.isEmpty()) {
            finish({}, QStringLiteral("No exact music torrent query is available."));
            return;
        }
        startNextQuery();
    });
    return id;
}

void VinylTorrentProvider::startNextQuery()
{
    if (m_active < 0 || !m_searchHandle.isEmpty()) return;
    if (!m_search || m_queryIndex >= m_queries.size() || m_attempted >= 3) {
        if (m_pending.isEmpty()) finish({}, QStringLiteral("No exact music torrent file was found."));
        return;
    }
    // Search signal delivery is queued so even injected synchronous indexers
    // cannot complete before this handle is assigned.
    m_searchHandle = m_search->startSearch(QStringLiteral("music"), QStringLiteral("all"),
                                         m_queries.at(m_queryIndex++), 5);
    if (m_searchHandle.isEmpty()) { startNextQuery(); return; }
    const int request = m_active;
    const QString handle = m_searchHandle;
    // An empty query can yield its remaining budget to the next query. Once a
    // candidate exists, keep collecting: slow indexers may return a better swarm.
    const int collectionMs = qMin(2000, qMax(1, m_deadlineMs / qMax(1, int(m_queries.size()))));
    QTimer::singleShot(collectionMs, this, [this, request, handle] {
        if (m_active == request && m_searchHandle == handle &&
            m_pending.isEmpty() && m_candidates.isEmpty()) finishSearch();
    });
}

void VinylTorrentProvider::finishSearch()
{
    trace("search-settled",{{"candidates",m_candidates.size()}});
    const QString handle = m_searchHandle;
    m_searchHandle.clear();
    if (m_search && !handle.isEmpty()) m_search->cancelSearch(handle);
    tryNextCandidate();
}

void VinylTorrentProvider::tryNextCandidate()
{
    if (m_active < 0) return;
    if (!m_stream) { finish({}, QStringLiteral("Music torrent stream service is unavailable.")); return; }
    if (m_attempted >= 3) {
        if (m_pending.isEmpty()) finish({}, QStringLiteral("No exact music torrent file was found."));
        return;
    }
    if (m_candidates.isEmpty()) {
        if (m_pending.size() < 2 && m_searchHandle.isEmpty()) startNextQuery();
        return;
    }
    if (m_pending.size() >= 2) return;
    if (m_pending.size() == 1) {
        const auto &first = m_pending.cbegin().value();
        // Give the first swarm a short head start. A stronger late result may
        // race immediately; otherwise hedge after one second, never serially
        // spending the entire discovery budget on an unresponsive manifest.
        if (m_candidates.first().seeders <= first.torrent.seeders &&
            m_elapsed.elapsed() - first.startedMs < 1000) return;
    }
    const auto candidate = m_candidates.takeFirst();
    trace("manifest-request",{{"hash",candidate.infoHash},{"title",candidate.title}});
    m_seen.insert(candidate.infoHash);
    ++m_attempted;
    const QString token = QStringLiteral("vinyl-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_pending.insert(token, {candidate, {}, m_elapsed.elapsed()});
    const int request = m_active;
    QTimer::singleShot(1000, Qt::PreciseTimer, this, [this, request, token] {
        if (m_active == request && m_pending.contains(token)) tryNextCandidate();
    });
    QTimer::singleShot(qMin(3000, m_deadlineMs), this, [this, request, token] {
        if (m_active != request || !m_pending.contains(token)) return;
        retireCandidate(token);
        tryNextCandidate();
    });
    if(candidate.magnetUri.isEmpty())m_stream->requestManifest(token,candidate.infoHash);
    else m_stream->requestMagnetManifest(token,candidate.infoHash,candidate.magnetUri);
}

void VinylTorrentProvider::retireCandidate(const QString &token)
{
    if (!m_pending.remove(token)) return;
    if (m_stream) m_stream->cancelRequest(token);
}

void VinylTorrentProvider::clearTransport()
{
    m_deadline.stop();
    const QString handle = m_searchHandle;
    const auto tokens = m_pending.keys();
    m_searchHandle.clear();
    m_pending.clear();
    if (m_search && !handle.isEmpty()) m_search->cancelSearch(handle);
    if (m_stream) for (const auto &token : tokens) m_stream->cancelRequest(token);
}

void VinylTorrentProvider::cancel()
{
    m_active = -1;
    clearTransport();
}

void VinylTorrentProvider::finish(const QVariantMap &data, const QString &error)
{
    if (m_active < 0) return;
    const int id = m_active;
    trace("finish",{{"provider",data.value("provider")},{"error",error}});
    m_active = -1;
    clearTransport();
    emit completed(id, data, error);
}
