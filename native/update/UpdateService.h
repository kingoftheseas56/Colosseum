#pragma once

#include "update/InstalledChronicle.h"
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
    using RequestShutdown = std::function<void()>;
    using ArtworkCompleted = std::function<void(const QByteArray&)>;
    using ArtworkFailed = std::function<void(const QString&)>;
    using FetchArtwork = std::function<void(const QString&, const QUrl&, qint64,
                                            ArtworkCompleted, ArtworkFailed)>;

    CheckLatest checkLatest;
    StartDownload startDownload;
    CancelDownload cancelDownload;
    Clock nowMs;
    InstallLauncher installLauncher;
    // Fires after a successful launch + persist() in restartAndUpdate(), so the
    // installer's /WAITPID=<our PID> wait resolves instead of running out its
    // 120s timeout. Unset is a no-op (null-checked before invoke).
    RequestShutdown requestShutdown;
    FetchArtwork fetchArtwork;

    // Installed-chronicle seed paths. Production main.cpp points these at the
    // bundled qrc-extracted manifest + signature + artwork dir so the gallery
    // renders the installed release's chapters at rest. Empty = no installed
    // chronicle (gallery falls back to the empty-list fallback at rest). The
    // harness sets these to the test fixture or leaves them empty to exercise
    // the no-chronicle path.
    QString installedChronicleManifestPath;
    QString installedChronicleSignaturePath;
    QString installedChronicleArtworkRoot;
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

#ifdef COLOSSEUM_UPDATE_TESTING
    void setTestingPresentationState(State state, qint64 received, qint64 total);
#endif

signals:
    void changed();
    // Fires ONLY when the offered-release identity flips between the installed
    // chronicle and a verified newer release (or back). Never fires on
    // state-only transitions (Idle→Checking, progress ticks, etc.). Drives the
    // gallery's chronicle-swap crossfade + chapter-index reset — including the
    // same-chapter-count path (5↔5) that onChapterCountChanged does not cover.
    void offeredReleaseChanged();

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

    // Offered-release selector: true when a verified newer release is active
    // (Available/Downloading/Paused/Verifying/Ready with a chronicle), false at
    // rest (Idle/Checking/UpToDate/VerificationFailure/ManualUpdateRequired, or
    // no chronicle at all). When true, rebuildPresentation() renders the newer
    // release's chapters; when false, it renders the installed chronicle's.
    bool newerReleaseOffered() const;
    void loadInstalledChronicle();

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
    bool m_offeredIsNewer = false;  // tracks the last-emitted offeredReleaseChanged state
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
    // Installed chronicle — the bundled signed seed loaded at construction.
    // Empty when no bundle is configured or it failed trust verification
    // (honest degradation: gallery renders the empty-list fallback at rest).
    std::optional<LoadedChronicle> m_installedChronicle;
};

} // namespace Colosseum::Update
