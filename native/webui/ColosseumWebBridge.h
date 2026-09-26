#pragma once

#include "feeds/WorldFeed.h"

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class CollectionStore;
class ProgressStore;
class QQmlContext;
class SearchHistoryStore;
class WallpaperSchemeHandler;

class ColosseumWebBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString resourceUrl READ resourceUrl CONSTANT)
public:
    explicit ColosseumWebBridge(const WorldFeed::Paths &paths,
                                WallpaperSchemeHandler *wallpapers,
                                QObject *parent = nullptr);

    QString resourceUrl() const;
    void bindPersonalStores(ProgressStore *progress, CollectionStore *collection,
                            SearchHistoryStore *history);
    void bindNativeContext(QQmlContext *context);
    QObject *service(const QString &name) const;
    bool detailActive(const QString &feed, const QString &identity) const;
    bool isDetailSubscription(int subscriptionId, const QString &feed,
                              const QString &identity) const;
    QVariantMap detailParams(const QString &feed, const QString &identity) const;
    QVariantMap detailRow(const QString &feed, const QString &identity,
                          const QString &sectionId, const QString &rowId) const;
    bool updateDetail(const QString &feed, const QString &identity,
                      const QVariantMap &patch);
    void delegateAction(const QString &action, const QVariantMap &payload,
                        std::function<void(const QVariantMap &)> complete);
    void suspendProfile();
    void setAccountPresentation(const QString &mode, const QString &username);
    Q_INVOKABLE void setWallpaper(const QString &url, const QString &kind);
    Q_INVOKABLE void setCovered(bool covered);
    Q_INVOKABLE void setShowExplicit(bool show);

    Q_INVOKABLE QVariantMap subscribe(const QString &feed, const QVariantMap &params);
    Q_INVOKABLE void unsubscribe(int id);
    Q_INVOKABLE QVariantMap more(int id, const QString &sectionId);
    Q_INVOKABLE QFuture<QVariantMap> act(const QString &action, const QVariantMap &payload);
    Q_INVOKABLE void clearSurfaces();
    Q_INVOKABLE bool hasSurface(const QString &name) const;
    Q_INVOKABLE void finishAction(int requestId, bool ok,
                                   const QString &error = QString(),
                                   const QVariant &result = {});
    Q_INVOKABLE QVariantMap shellState() const;

signals:
    void feedEvent(const QVariantMap &event);
    void shellEvent(const QVariantMap &event);
    void actionRequested(const QString &action, const QVariantMap &payload,
                         int requestId);

private:
    struct Subscription {
        QString feed;
        QVariantMap params;
        int generation = 0;
        int seq = 0;
        int requestVersion = 0;
        int visibleCount = 24;
        bool pendingProgress = false;
        QHash<QString, QVariantMap> sections;
    };

    bool validFeed(const QString &feed, const QVariantMap &params) const;
    void publish(int id, const QVariantMap &event);
    void reset(int id);
    void refresh(int id);
    void applySections(int id, int generation, int requestVersion,
                       const QVariantList &sections);
    void storeChanged(bool progress);
    void record(int id, const QVariantMap &event);
    void startRecorderSweep();
    static QVariantMap fail(const QString &error);

    WorldFeed::Paths m_paths;
    QPointer<QQmlContext> m_nativeContext;
    WallpaperSchemeHandler *m_wallpapers = nullptr;
    QPointer<ProgressStore> m_progress;
    QPointer<CollectionStore> m_collection;
    QPointer<SearchHistoryStore> m_history;
    QHash<int, Subscription> m_subscriptions;
    QHash<int, QSharedPointer<QPromise<QVariantMap>>> m_pendingActions;
    QHash<int, std::function<void(const QVariantMap &)>> m_delegatedActions;
    QSet<QString> m_surfaces;
    int m_nextSubscription = 1;
    int m_nextAction = 1;
    int m_profileRevision = 1;
    QString m_accountMode;
    QString m_accountUsername;
    QString m_wallpaperUrl;
    QString m_wallpaperSource;
    QString m_wallpaperKind = QStringLiteral("image");
    bool m_covered = false;
    bool m_showExplicit = false;
    QString m_recordDirectory;
    QHash<int, QVariantList> m_recordedEvents;
    bool m_recorderSweepStarted = false;
};
