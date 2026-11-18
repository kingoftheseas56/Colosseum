#pragma once
#include "TorrentResult.h"
#include <QObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class TankorentSearchService;

// Manual Tankorent discovery for standalone audiobook works. The picker sends
// a selected row's request through the existing audiobook downloader.
class AudiobookTorrents : public QObject {
    Q_OBJECT
public:
    // searchNam: pinned, UA-stamped, UNCACHED CachingNam for indexer HTTP.
    explicit AudiobookTorrents(QNetworkAccessManager* searchNam, QObject* parent = nullptr);
    // DI seam (tests inject a mock-indexer search service). Takes ownership.
    explicit AudiobookTorrents(TankorentSearchService* search, QObject* parent = nullptr);

    Q_INVOKABLE bool isEligible(const QVariantMap& work) const;
    Q_INVOKABLE void search(const QString& token, const QVariantMap& work,
                            const QString& recordingId, const QString& pairKey);
    Q_INVOKABLE void cancel(const QString& token);

signals:
    void searchFinished(const QString& token, const QVariantList& rows, const QString& error);

private slots:
    void onIndexerResults(const QString& handle, const QList<TorrentResult>& r);
    void onSearchDone(const QString& handle);

private:
    void wire();
    TankorentSearchService* m_search;
    QString m_token, m_handle, m_pairKey;
    QVariantMap m_work;
    QList<TorrentResult> m_accum;
    bool m_receivedResults = false;
    bool m_indexerFailed = false;
};
