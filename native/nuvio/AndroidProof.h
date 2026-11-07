#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>

class ExtensionsStore;
class NuvioRepositoryStore;
class NuvioSourceResolver;
class QQmlApplicationEngine;
class QQuickItem;

// Instrumentation for emulator proof APKs only. Production APKs do not compile
// this class, its fault fixtures, its launch extras, or its QML overlay.
class AndroidProof final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString mediaId READ mediaId CONSTANT)
    Q_PROPERTY(QString imdbId READ imdbId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QObject *sourceService READ sourceService NOTIFY sourceServiceChanged)

public:
    AndroidProof(QQmlApplicationEngine *engine, NuvioRepositoryStore *repositories,
                 NuvioSourceResolver *sources, ExtensionsStore *extensions,
                 QObject *parent = nullptr);
    void startFromIntent(const QString &qmlPath);

    QString status() const { return m_status; }
    QString mediaId() const { return m_mediaId; }
    QString imdbId() const { return m_imdbId; }
    QString title() const { return m_title; }
    QObject *sourceService() const;

    Q_INVOKABLE void observeSources(const QVariantList &stremioRows,
                                    const QVariantList &nuvioRows,
                                    bool nuvioPending, bool stremioPending,
                                    int askedCount);

signals:
    void statusChanged();
    void sourceServiceChanged();
    void showSourcesRequested();
    void runFaultRequested();

private:
    enum class Phase { Idle, Adding, Enabling, AddingStremio, Resolving, Between, Crash, Timeout, Recovering, Rechecking, Profiling, Complete, Failed };
    bool createOverlay(const QString &qmlPath);
    bool prepareFaultStore();
    void repositorySettled();
    void installStremioFixture();
    void beginFault(const QString &fault);
    void finishFault(const QVariantList &stremioRows);
    void beginRecovery();
    void finishRecovery(const QVariantList &stremioRows, const QVariantList &nuvioRows);
    void beginProfile(int concurrency);
    void finishProfile();
    void record(const QString &stage, QVariantMap details = {});
    void fail(const QString &reason);
    void setStatus(const QString &status);
    QVariantMap integrityEvidence(const QVariantList &stremioRows) const;
    QByteArray registryFingerprint() const;
    static QByteArray fingerprint(const QVariant &value);
    static QString intentString(const char *key);

    QQmlApplicationEngine *m_engine;
    NuvioRepositoryStore *m_repositories;
    NuvioSourceResolver *m_sources;
    ExtensionsStore *m_extensions;
    NuvioRepositoryStore *m_faultRepositories = nullptr;
    NuvioSourceResolver *m_faultSources = nullptr;
    NuvioSourceResolver *m_currentSources;
    QPointer<QQuickItem> m_overlay;
    QTemporaryDir m_faultDirectory;
    QTimer m_watchdog;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_faultElapsed;
    Phase m_phase = Phase::Idle;
    QString m_status;
    QString m_nonce;
    QString m_repository;
    QString m_repositoryId;
    QString m_pluginId;
    QString m_stremioManifest;
    QString m_mediaId = QStringLiteral("10331");
    QString m_imdbId = QStringLiteral("tt0063350");
    QString m_title = QStringLiteral("Night of the Living Dead (1968)");
    QString m_faultRepositoryId;
    QString m_fault;
    QString m_faultOutcome;
    QVariantList m_directStreams;
    QVariantList m_observedStremioRows;
    QString m_publicStreamUrl;
    qint64 m_failedWorkerPid = 0;
    qint64 m_recoveryWorkerPid = 0;
    int m_recoverySlot = -1;
    QString m_recoveryOutcome;
    QStringList m_recoveredFaults;
    QVariantMap m_finalEvidence;
    int m_profileConcurrency = 0;
    int m_profileMaxConcurrent = 0;
    QSet<qint64> m_profilePids;
    QSet<qint64> m_profileFinishedPids;
    QSet<int> m_profileSlots;
    QVariantList m_profilesCompleted;
    QByteArray m_stremioFingerprint;
    QByteArray m_registryFingerprint;
    int m_stremioRows = 0;
    qint64 m_workerPid = 0;
    bool m_liveFinished = false;
    bool m_faultFinished = false;
};
