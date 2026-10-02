#pragma once

#include "PorticoTrendCache.h"
#include "PorticoTrendModel.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class PorticoTrendAdapter;

class PorticoTrendBroker final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* shelfModel READ shelfModel CONSTANT)
    Q_PROPERTY(QString region READ region WRITE setRegion NOTIFY regionChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(QStringList sources READ sources CONSTANT)
    Q_PROPERTY(QStringList feeds READ feeds NOTIFY feedsChanged)
    Q_PROPERTY(qint64 cacheMaxAgeSeconds READ cacheMaxAgeSeconds WRITE setCacheMaxAgeSeconds NOTIFY cacheMaxAgeSecondsChanged)

public:
    explicit PorticoTrendBroker(QObject *parent = nullptr);

    QAbstractItemModel *shelfModel() { return &m_model; }
    QString region() const { return m_region; }
    QString language() const { return m_language; }
    int limit() const { return m_limit; }
    QStringList sources() const;
    QStringList feeds() const;
    qint64 cacheMaxAgeSeconds() const { return m_cacheMaxAgeSeconds; }

    void setRegion(const QString &region);
    void setLanguage(const QString &language);
    void setLimit(int limit);
    void setCacheMaxAgeSeconds(qint64 seconds);

    void configureFeed(const QString &feedId, const QString &sourceId,
                       const QVariantMap &config = {});
    void configureSource(const QString &sourceId, const QVariantMap &config);
    Q_INVOKABLE void refreshFeed(const QString &feedId);
    Q_INVOKABLE void refreshSource(const QString &sourceId);
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE QVariantMap shelf(const QString &sourceId) const;
    Q_INVOKABLE void clearCache();

signals:
    void regionChanged();
    void languageChanged();
    void limitChanged();
    void cacheMaxAgeSecondsChanged();
    void feedsChanged();
    void sourceReady(const QString &sourceId, const QVariantMap &shelf);
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    struct FeedRuntime {
        QString feedId;
        QString sourceId;
        QVariantMap config;
        QPointer<PorticoTrendAdapter> adapter;
    };

    PorticoTrendAdapter *createAdapter(const QString &sourceId);
    PorticoTrend::Query queryFor(const FeedRuntime &feed) const;
    FeedRuntime *feed(const QString &feedId);
    const FeedRuntime *feed(const QString &feedId) const;

    QNetworkAccessManager m_network;
    PorticoTrendModel m_model;
    PorticoTrendCache m_cache;
    QHash<QString, FeedRuntime> m_feeds;
    QString m_region = QStringLiteral("US");
    QString m_language = QStringLiteral("en");
    int m_limit = 20;
    qint64 m_cacheMaxAgeSeconds = 6 * 60 * 60;
};
