#pragma once
#include "TorrentResult.h"
#include <QObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class TorrentEngine;
class TankorentSearchService;
class BookTorrentDownloader;

// One QML-facing object (`BookTorrents`) composing search + rank + download.
// A single app-lifetime instance serves every book page; opening book B supersedes
// book A's in-flight search (cancelSearch + handle guard), so results never cross pages.
class BookTorrents : public QObject {
    Q_OBJECT
public:
    // searchNam: pinned, UA-stamped, UNCACHED CachingNam for indexer HTTP.
    // engine: the imported libtorrent TorrentEngine that carries the download bytes.
    BookTorrents(QNetworkAccessManager* searchNam, TorrentEngine* engine, QObject* parent = nullptr);

    Q_INVOKABLE void search(const QString& title, const QString& author);
    Q_INVOKABLE void download(const QString& infoHash, const QString& title, const QString& author);
    Q_INVOKABLE bool    isDownloaded(const QString& infoHash) const;
    Q_INVOKABLE QString localFile(const QString& infoHash) const;
    Q_INVOKABLE QVariantMap statusOf(const QString& infoHash) const;

signals:
    void resultsReady(const QVariantList& rankedRows);
    void searchFinished();
    void resolving(const QString& infoHash);
    void progress(const QString& infoHash, double received, double total);
    void finished(const QString& infoHash, const QString& path);
    void failed(const QString& infoHash, const QString& reason);

private slots:
    void onIndexerResults(const QString& handle, const QList<TorrentResult>& r);
    void onSearchDone(const QString& handle);

private:
    TankorentSearchService* m_search;
    BookTorrentDownloader*  m_dl;
    QString m_handle, m_title, m_author;
    QList<TorrentResult> m_accum;   // accumulator for the ACTIVE handle; reset per search — no heap, no lambda capture
};
