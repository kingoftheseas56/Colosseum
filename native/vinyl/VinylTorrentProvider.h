#pragma once

#include "torrent/MusicTorrentMatcher.h"
#include "torrent/TorrentResult.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantMap>

class QNetworkAccessManager;
class StreamServer;
class TankorentSearchService;

// One cancellable music acquisition lane. Catalogue identity belongs to Vinyl;
// search, exact-file admission and the existing streaming transport live here.
class VinylTorrentProvider : public QObject {
    Q_OBJECT
public:
    explicit VinylTorrentProvider(QNetworkAccessManager *network, StreamServer *stream,
                                  QObject *parent = nullptr, int deadlineMs = 4000);
    // Inject a search service (including its mock-indexer factory) and stream
    // service for deterministic tests. Neither injected service is owned here.
    VinylTorrentProvider(TankorentSearchService *search, StreamServer *stream,
                         QObject *parent, int deadlineMs = 4000);
    ~VinylTorrentProvider() override;

    Q_INVOKABLE int resolve(const QVariantMap &track);
    Q_INVOKABLE void cancel();

signals:
    void completed(int requestId, const QVariantMap &data, const QString &error);

private:
    void wire();
    void startNextQuery();
    void finishSearch();
    void tryNextCandidate();
    void finish(const QVariantMap &data, const QString &error);
    void clearTransport();
    void trace(const QString &stage,const QVariantMap &facts={}) const;

    QPointer<TankorentSearchService> m_search;
    QPointer<StreamServer> m_stream;
    QTimer m_deadline, m_stage;
    QElapsedTimer m_elapsed;
    int m_deadlineMs = 4000;
    int m_serial = 0, m_active = -1, m_queryIndex = 0, m_attempted = 0;
    MusicTorrentTrack m_track;
    QStringList m_queries;
    QString m_searchHandle, m_streamToken;
    QList<TorrentResult> m_candidates;
    QSet<QString> m_seen;
    TorrentResult m_candidate;
    MusicTorrentMatch m_match;
};
