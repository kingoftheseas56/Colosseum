#pragma once

#include "TorrentIndexer.h"

#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Native adapter for the shared Tankorent Workers search transport. The source
// id and display name remain source-specific so the existing Tankorent
// settings, routing, provenance, and health keys stay stable.
class TankorentWorkerIndexer final : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit TankorentWorkerIndexer(QNetworkAccessManager *nam,
                                    QString sourceId,
                                    QObject *parent = nullptr);
    ~TankorentWorkerIndexer() override;

    QString id() const override { return m_sourceId; }
    QString displayName() const override;
    void search(const QString &query, int limit = 30,
                const QString &categoryId = {}) override;
    QString categoryFor(const QString &mediaType) const override;

    IndexerHealth health() const override { return m_health; }
    QDateTime lastSuccess() const override { return m_lastSuccess; }
    QString lastError() const override { return m_lastError; }
    qint64 lastResponseMs() const override { return m_lastResponseMs; }

    // Called by TankorentSearchService when a search handle is cancelled. This
    // aborts the shared QNAM reply immediately and suppresses its completion.
    void cancel();

    static bool supportsSource(const QString &sourceId);
    static QUrl endpointForSource(const QString &sourceId);
    static QByteArray buildRequestBody(const QString &query, int limit,
                                      const QString &categoryId);

private:
    void readAvailable(QNetworkReply *reply);
    void onReplyFinished(QNetworkReply *reply);
    void finishError(QNetworkReply *reply, const QString &message,
                     IndexerHealth healthOverride = IndexerHealth::Unknown);
    void finishSuccess(QNetworkReply *reply, const QList<TorrentResult> &results);
    void releaseReply(QNetworkReply *reply);

    QPointer<QNetworkAccessManager> m_nam;
    QString m_sourceId;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_responseBody;
    bool m_responseTooLarge = false;
    bool m_cancelled = false;
    bool m_timedOut = false;
    int m_requestedLimit = 0;
    QTimer m_deadlineTimer;
};
