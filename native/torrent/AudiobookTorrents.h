#pragma once
#include "TorrentResult.h"
#include <QObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class TankorentSearchService;

// Tankorent as a second place audiobook FILES can come from (next to AudioBookBay).
// Search only: no download method here -- downloads go through Audiobooks.downloadAudiobook.
// A single app-lifetime instance; a new search supersedes the previous one
// (cancelSearch + handle guard), so stale results never reach the page.
class AudiobookTorrents : public QObject {
    Q_OBJECT
public:
    // searchNam: pinned, UA-stamped, UNCACHED CachingNam for indexer HTTP.
    explicit AudiobookTorrents(QNetworkAccessManager* searchNam, QObject* parent = nullptr);
    // DI seam (tests inject a mock-indexer search service). Takes ownership.
    explicit AudiobookTorrents(TankorentSearchService* search, QObject* parent = nullptr);

    Q_INVOKABLE void search(const QString& title, const QString& author);

    // Pure: audio-only, hash-bearing, ranked rows (title/author agreement, then seeders).
    static QVariantList buildRows(const QString& title, const QString& author,
                                  const QList<TorrentResult>& raw);

signals:
    void resultsReady(const QVariantList& rankedRows);
    void searchFinished();

private slots:
    void onIndexerResults(const QString& handle, const QList<TorrentResult>& r);
    void onSearchDone(const QString& handle);

private:
    void wire();
    TankorentSearchService* m_search;
    QString m_handle, m_title, m_author;
    QList<TorrentResult> m_accum;   // accumulator for the ACTIVE handle; reset per search
};
