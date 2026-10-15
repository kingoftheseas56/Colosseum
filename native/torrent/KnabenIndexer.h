#pragma once

#include "TorrentIndexer.h"
#include "TorrentResult.h"

#include <QList>

class QNetworkAccessManager;
class QNetworkReply;

// Meta-search AGGREGATOR (knaben.org): one query fans out across dozens of
// underlying trackers in a single request — including 1337x, whose Cloudflare
// wall we could never crack directly on TB2. Knaben's API is Cloudflare-fronted
// but NOT challenge-walled for Qt's QNetworkAccessManager (proven in-process by
// tests/knaben_probe.cpp — curl clearing CF proves nothing about our fingerprint).
// Keyless JSON POST API, so it slots into the keyless indexer family.
//
// Ranking discipline (load-bearing): we ask knaben for RELEVANCE-ordered results
// and NEVER order_by=seeders — that returns the global top-seeded firehose and
// ignores the query, surfacing SEO-spam cracks. hide_xxx/hide_unsafe filter
// adult + malware AT THE SOURCE; our own ComicTorrentRanker/BookTorrentRanker
// then rank by identity (ISBN / exact title / format coverage), with seeders as
// the final tie-break only.
class KnabenIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit KnabenIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("knaben"); }
    QString displayName() const override { return QStringLiteral("Knaben"); }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

    // Pure parse of a knaben API response body into TorrentResults — extracted so
    // it is directly unit-testable without a live reply (knaben_indexer_harness).
    // Rows without a usable 40-hex infohash are dropped; each row keeps its origin
    // tracker (e.g. "1337x") in `category` so a pick's provenance stays visible.
    static QList<TorrentResult> parseHits(const QByteArray& body);

    // The request body for a query. Static so the harness can assert we send a
    // relevance search and NEVER order_by=seeders.
    static QByteArray buildRequestBody(const QString& query, int limit,
                                       const QString& categoryId = {});

private:
    void onReplyFinished(QNetworkReply* reply);

    QNetworkAccessManager* m_nam;
};
