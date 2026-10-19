#include "VinylTorrentProvider.h"

#include "player/streamserver.h"
#include "torrent/MusicTorrentQueryPlanner.h"
#include "torrent/TankorentSearchService.h"

#include <QNetworkAccessManager>
#include <QUuid>
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

void VinylTorrentProvider::wire()
{
    m_deadline.setSingleShot(true);
    m_stage.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        finish({}, QStringLiteral("Music torrent discovery timed out."));
    });
    connect(&m_stage, &QTimer::timeout, this, [this] {
        if (!m_searchHandle.isEmpty()) finishSearch();
        else if (!m_streamToken.isEmpty()) tryNextCandidate();
    });
    if (m_search) {
        connect(m_search, &TankorentSearchService::resultsReady, this,
                [this](const QString &handle, const QList<TorrentResult> &rows) {
            if (m_active < 0 || handle != m_searchHandle) return;
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
        }, Qt::QueuedConnection);
        connect(m_search, &TankorentSearchService::searchFinished, this,
                [this](const QString &handle) {
            if (m_active >= 0 && handle == m_searchHandle) finishSearch();
        }, Qt::QueuedConnection);
    }
    if (m_stream) {
        connect(m_stream, &StreamServer::torrentManifestReady, this,
                [this](const QString &token, const QString &hash, const QVariantList &rows) {
            if (m_active < 0 || token != m_streamToken || hash != m_candidate.infoHash) return;
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
            m_match = MusicTorrentMatcher::pick(m_track, files);
            if (m_match.index < 0) { tryNextCandidate(); return; }
            m_stream->requestFile(m_streamToken, m_candidate.infoHash, m_match.index);
        });
        connect(m_stream, &StreamServer::torrentStreamReady, this,
                [this](const QString &token, const QString &url, const QString &hash, int index) {
            if (m_active < 0 || token != m_streamToken || hash != m_candidate.infoHash ||
                index != m_match.index) return;
            if (url.isEmpty()) { tryNextCandidate(); return; }
            finish({{QStringLiteral("url"), url}, {QStringLiteral("infoHash"), hash},
                    {QStringLiteral("fileIdx"), index}, {QStringLiteral("filePath"), m_match.path},
                    {QStringLiteral("provider"), QStringLiteral("torrent")},
                    {QStringLiteral("qualityLabel"), QStringLiteral("Torrent audio")}}, {});
        });
        connect(m_stream, &StreamServer::torrentRequestError, this,
                [this](const QString &token, const QString &) {
            if (m_active >= 0 && token == m_streamToken) tryNextCandidate();
        });
    }
}

int VinylTorrentProvider::resolve(const QVariantMap &track)
{
    cancel();
    const int id = m_active = ++m_serial;
    m_track = {};
    m_track.artist = track.value(QStringLiteral("artist")).toString().trimmed();
    m_track.album = track.value(QStringLiteral("album")).toString().trimmed();
    m_track.title = track.value(QStringLiteral("title")).toString().trimmed();
    m_track.trackNumber = track.value(QStringLiteral("trackNumber")).toInt();
    m_track.discNumber = track.value(QStringLiteral("discNumber")).toInt();
    m_track.durationMs = track.value(QStringLiteral("durationMs")).toLongLong();
    m_queries = MusicTorrentQueryPlanner::automaticQueries(m_track);
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
    if (m_active < 0) return;
    if (!m_search || m_queryIndex >= m_queries.size() || m_attempted >= 3) {
        finish({}, QStringLiteral("No exact music torrent file was found."));
        return;
    }
    // Search signal delivery is queued so even injected synchronous indexers
    // cannot complete before this handle is assigned.
    m_searchHandle = m_search->startSearch(QStringLiteral("music"), QStringLiteral("all"),
                                         m_queries.at(m_queryIndex++), 5);
    if (m_searchHandle.isEmpty()) { startNextQuery(); return; }
    m_stage.start(qMin(3000, m_deadlineMs));
}

void VinylTorrentProvider::finishSearch()
{
    m_stage.stop();
    const QString handle = m_searchHandle;
    m_searchHandle.clear();
    if (m_search && !handle.isEmpty()) m_search->cancelSearch(handle);
    tryNextCandidate();
}

void VinylTorrentProvider::tryNextCandidate()
{
    if (m_active < 0) return;
    m_stage.stop();
    const QString token = m_streamToken;
    m_streamToken.clear();
    if (m_stream && !token.isEmpty()) m_stream->cancelRequest(token);
    if (!m_stream) { finish({}, QStringLiteral("Music torrent stream service is unavailable.")); return; }
    if (m_candidates.isEmpty() || m_attempted >= 3) { startNextQuery(); return; }
    m_candidate = m_candidates.takeFirst();
    m_seen.insert(m_candidate.infoHash);
    ++m_attempted;
    m_match = {};
    m_streamToken = QStringLiteral("vinyl-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    // Give metadata more of the bounded budget than catalogue discovery. Cold
    // swarms can still miss the total deadline; the caller owns that fallback.
    m_stage.start(qMin(6000, m_deadlineMs));
    m_stream->requestManifest(m_streamToken, m_candidate.infoHash);
}

void VinylTorrentProvider::clearTransport()
{
    m_deadline.stop();
    m_stage.stop();
    const QString handle = m_searchHandle, token = m_streamToken;
    m_searchHandle.clear();
    m_streamToken.clear();
    if (m_search && !handle.isEmpty()) m_search->cancelSearch(handle);
    if (m_stream && !token.isEmpty()) m_stream->cancelRequest(token);
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
    m_active = -1;
    clearTransport();
    emit completed(id, data, error);
}
