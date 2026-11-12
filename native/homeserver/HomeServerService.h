#pragma once

#include "HomeServerClient.h"
#include "HomeServerCatalog.h"
#include "HomeServerPlayback.h"
#include "HomeServerProgressOwner.h"
#include "HomeServerStore.h"

#include <QJsonArray>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QPointer>
#include <QQueue>

struct HomeServerServiceOptions {
    std::shared_ptr<AccountSecretBackend> secrets;
    QString identityDirectory;
    QUrl plexOrigin = QUrl(QStringLiteral("https://plex.tv"));
    int pinPollIntervalMs = 1500;
    bool refreshOnActivation = true;
    bool android = false;
    QVariantMap videoDecodeCapabilities;
};

// The QML boundary contains display metadata and commands only. Passwords are
// consumed by connectServer; bearer tokens stay in native client/storage code.
class HomeServerService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(QVariantList shelves READ shelves NOTIFY changed)
    Q_PROPERTY(QVariantList plexServers READ plexServers NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString plexCode READ plexCode NOTIFY changed)
    Q_PROPERTY(QString plexUrl READ plexUrl NOTIFY changed)
    Q_PROPERTY(bool httpConsentRequired READ httpConsentRequired NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(quint64 profileGeneration READ profileGeneration NOTIFY profileChanged)

public:
    explicit HomeServerService(QObject *parent = nullptr);
    explicit HomeServerService(HomeServerServiceOptions options, QObject *parent = nullptr);
    ~HomeServerService() override;
    void activate(const QString &profileId, const QString &profileRoot,
                  std::shared_ptr<HomeServerProgressOwner> progress = {});
    void deactivate();

    QVariantList connections() const;
    QVariantList shelves() const;
    Q_INVOKABLE QVariantList shelfTitles(const QString &connectionId) const;
    Q_INVOKABLE QVariantMap titleMetadata(const QVariantMap &identity) const;
    QVariantList plexServers() const;
    bool available() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString plexCode() const { return m_pinCode; }
    QString plexUrl() const { return m_pinUrl; }
    bool httpConsentRequired() const { return m_httpConsentRequired; }
    quint64 revision() const { return m_revision; }
    quint64 profileGeneration() const { return m_profileGeneration; }

    Q_INVOKABLE void connectServer(const QString &provider, const QString &address,
                                   const QString &username, const QString &password,
                                   bool allowHttp = false);
    Q_INVOKABLE void beginPlex();
    Q_INVOKABLE void selectPlexServer(const QString &id, bool allowHttp = false);
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void refresh(const QString &id);
    Q_INVOKABLE int itemCount(const QString &id) const;
    Q_INVOKABLE QVariantList sources(const QVariantMap &identity) const;
    Q_INVOKABLE bool loadPlayback(QObject *backend, const QString &sourceKey,
                                  qint64 resumeMs = 0, bool forceTranscode = false);
    Q_INVOKABLE void cancelPlayback(QObject *backend);
    Q_INVOKABLE void reportPlayback(QObject *backend, const QString &sourceKey,
                                    qint64 positionMs, qint64 durationMs,
                                    bool paused, const QString &event);
    void syncProgress(const QString &canonicalId = QString());
    void progressEntryApplied(const QString &kind, const QString &id);
    Q_INVOKABLE bool isInsecureAddress(const QString &address) const;

signals:
    void changed();
    void profileChanged();
    void connectionAdded(const QString &id);
    void playbackResolved(const QString &sourceKey, bool transcoded);
    void playbackFailed(const QString &sourceKey, const QString &message);

private:
    void publish();
    void finishSignIn(const QString &message);
    void invalidateRequests();
    void pollPlex();
    void probePlex(const QJsonObject &candidate, int endpoint, bool allowHttp,
                   bool skippedHttp, quint64 generation);
    void saveAuthenticated(const QJsonObject &metadata, const QString &token);
    bool canSignIn();
    void cancelRefresh(const QString &id);
    void cancelRefreshes();
    void scheduleRefreshes();
    void drainRefreshQueue();
    void cancelBackend(QObject *backend, bool stop);
    void cancelPlaybacks(const QString &connectionId = QString());
    void reconcileProgress(const QString &connectionId, const QString &canonicalId = QString());
    void hideUnavailableProgress();
    void drainReports();

    struct Playback {
        QPointer<QObject> backend;
        QString sourceKey;
        QString connectionId;
        QJsonObject item;
        HomeServerPlaybackJob::Result resolved;
        HomeServerProgressState state;
        quint64 profileGeneration = 0;
        bool started = false;
        bool ended = false;
        QJsonObject connection;
        QString token;
    };
    struct Report {
        QJsonObject connection, item;
        HomeServerPlaybackJob::Result playback;
        QString token, event;
        HomeServerProgressState state;
        quint64 profileGeneration = 0;
    };
    void enqueueReport(Report report);
    void enqueuePlayback(const Playback &playback, const QString &event);

    HomeServerClient m_client;
    HomeServerClient m_dataClient;
    HomeServerClient m_reportClient;
    HomeServerStore m_store;
    QTimer m_pinTimer;
    QJsonArray m_plexCandidates;
    QHash<QString, QString> m_connectionStatus;
    QHash<QString, QJsonArray> m_index;
    QHash<QString, int> m_serverMajor;
    QHash<QString, QPointer<HomeServerRefresh>> m_refreshes;
    QSet<QString> m_expired;
    QStringList m_refreshQueue;
    QHash<QObject *, QPointer<HomeServerPlaybackJob>> m_playbackJobs;
    QHash<QObject *, QString> m_pendingSources;
    QHash<QObject *, Playback> m_playbacks;
    QHash<QObject *, quint64> m_backendGenerations;
    QSet<QObject *> m_observedBackends;
    mutable QHash<QString, QJsonObject> m_sourceIdentities;
    std::shared_ptr<HomeServerProgressOwner> m_progress;
    QQueue<Report> m_reports;
    QPointer<HomeServerProgressJob> m_reportJob;
    QString m_profileId;
    QString m_profileRoot;
    QString m_status;
    QString m_pinCode;
    QString m_pinUrl;
    qint64 m_pinId = 0;
    qint64 m_pinDeadline = 0;
    quint64 m_generation = 0;
    quint64 m_profileGeneration = 0;
    quint64 m_revision = 0;
    bool m_busy = false;
    bool m_httpConsentRequired = false;
    bool m_refreshOnActivation = true;
    bool m_android = false;
    bool m_applyingProgress = false;
    bool m_externalProgressScheduled = false;
    QVariantMap m_videoDecodeCapabilities;
};
