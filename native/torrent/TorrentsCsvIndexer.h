#pragma once

#include "TorrentIndexer.h"

class QNetworkAccessManager;
class QNetworkReply;

class TorrentsCsvIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit TorrentsCsvIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "torrentscsv"; }
    QString displayName() const override { return "Torrents-CSV"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_nam;
};
