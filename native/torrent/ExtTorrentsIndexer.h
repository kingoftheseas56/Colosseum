#pragma once

#include "TorrentIndexer.h"
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

class ExtTorrentsIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit ExtTorrentsIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "exttorrents"; }
    QString displayName() const override { return "ExtraTorrents"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

private:
    struct ListRow {
        QString title;
        QString detailPath;
        QString magnetUri;
        qint64  sizeBytes = 0;
        int     seeders = 0;
        int     leechers = 0;
        QString category;
    };

    void tryNextUrl();
    void onListPageFetched(QNetworkReply* reply);
    QList<ListRow> parseListPage(const QString& html);
    void fetchDetailPages();
    void onDetailFetched(QNetworkReply* reply, int index);
    void checkComplete();
    static qint64 parseSize(const QString& text);
    static QString categoryFromCss(const QString& cssClass);

    QNetworkAccessManager* m_nam;
    QList<ListRow> m_rows;
    QStringList m_candidateUrls;
    int m_urlIndex = 0;
    int m_pendingDetails = 0;
    int m_limit = 30;
};
