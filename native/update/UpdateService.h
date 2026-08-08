#pragma once

#include "update/UpdateReleaseClient.h"
#include "update/UpdateDownload.h"
#include "update/UpdateVersion.h"

#include <QObject>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace Colosseum::Update {

struct UpdateServiceHooks final {
    using CheckLatest = std::function<void(const QString&, UpdateReleaseClient::Callback)>;
    using DownloadProgress = std::function<void(qint64, qint64, qint64)>;
    using DownloadCompleted = std::function<void(const QString&)>;
    using DownloadFailed = std::function<void(const QString&, bool)>;
    using StartDownload = std::function<void(const DownloadRequest&, DownloadProgress,
                                              DownloadCompleted, DownloadFailed)>;
    using CancelDownload = std::function<void()>;
    using Clock = std::function<qint64()>;
    using InstallLauncher = std::function<bool(const QString&, const Version&, QString*)>;
    using ArtworkCompleted = std::function<void(const QByteArray&)>;
    using ArtworkFailed = std::function<void(const QString&)>;
    using FetchArtwork = std::function<void(const QString&, const QUrl&, qint64,
                                            ArtworkCompleted, ArtworkFailed)>;

    CheckLatest checkLatest;
    StartDownload startDownload;
    CancelDownload cancelDownload;
    Clock nowMs;
    InstallLauncher installLauncher;
    FetchArtwork fetchArtwork;
};

class UpdateService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString installedVersion READ installedVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool unseenUpdate READ unseenUpdate NOTIFY changed)
    Q_PROPERTY(qint64 receivedBytes READ receivedBytes NOTIFY changed)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantMap release READ release NOTIFY changed)
    Q_PROPERTY(QVariantList highlights READ highlights NOTIFY changed)

public:
    enum State {
        Idle,
        Checking,
        UpToDate,
        Available,
        Downloading,
        Paused,
        Verifying,
        Ready,
        Installing,
        RecoverableError,
        VerificationFailure,
        ManualUpdateRequired,
    };
    Q_ENUM(State)

    UpdateService(Version installedVersion, QString cacheRoot,
                  UpdateServiceHooks hooks = {}, QObject* parent = nullptr);

    State state() const { return m_state; }
    QString installedVersion() const { return m_installedVersion.canonical(); }
    QString latestVersion() const { return m_latestVersion; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool unseenUpdate() const { return m_unseenUpdate; }
    qint64 receivedBytes() const { return m_receivedBytes; }
    qint64 totalBytes() const { return m_totalBytes; }
    double progress() const;
    QVariantMap release() const { return m_release; }
    QVariantList highlights() const { return m_highlights; }

    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void markSeen();
    Q_INVOKABLE void restartAndUpdate();
    void startAutomaticChecks();

signals:
    void changed();

private:
    void loadPersisted();
    void loadSeedState();
    bool restoreManifest(const QByteArray& manifestBytes, const QByteArray& signatureBytes,
                         State requestedState, bool seen, QString* error);
    bool applyManifest(const Manifest& manifest, const QByteArray& manifestBytes,
                       const QByteArray& signatureBytes, State requestedState, bool seen,
                       QString* error);
    void persist();
    void setState(State state);
    void emitChanged();
    void handleRelease(ReleaseCheckResult result);
    void handleDownloadFailed(const QString& errorCode, bool resumable);
    void rebuildPresentation();
    void fetchMissingArtwork();
    qint64 nowMs() const;
    bool withinRoot(const QString& path) const;
    static QString stateName(State state);
    static State stateFromName(const QString& name);

    Version m_installedVersion;
    QString m_cacheRoot;
    UpdateCache m_cache;
    UpdateServiceHooks m_hooks;
    State m_state = Idle;
    State m_stateBeforeCheck = Idle;
    bool m_checkInFlight = false;
    bool m_hasChronicle = false;
    bool m_updateAvailable = false;
    bool m_unseenUpdate = false;
    QString m_latestVersion;
    QString m_seenVersion;
    QString m_failedVersion;
    QString m_etag;
    qint64 m_lastCheckMs = -1;
    qint64 m_receivedBytes = 0;
    qint64 m_totalBytes = 0;
    QString m_installerPath;
    QHash<QString, QUrl> m_assetUrls;
    Manifest m_manifest;
    QByteArray m_verifiedManifestBytes;
    QByteArray m_verifiedSignatureBytes;
    QVariantMap m_release;
    QVariantList m_highlights;
};

} // namespace Colosseum::Update
