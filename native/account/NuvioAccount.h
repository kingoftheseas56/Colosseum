#pragma once

#include "NuvioApi.h"
#include "NuvioNextUp.h"
#include "NuvioTheatreData.h"

#include <QSet>
#include <QTimer>

#include <array>

class ProfilePreferencesStore;

class NuvioAccount final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool needsReconnect READ needsReconnect NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString primaryProvider READ primaryProvider NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString selectedProfileName READ selectedProfileName NOTIFY changed)
    Q_PROPERTY(QString connectedProfileName READ connectedProfileName NOTIFY changed)
    Q_PROPERTY(QVariantList preview READ preview NOTIFY changed)
    Q_PROPERTY(bool previewBecomesPrimary READ previewBecomesPrimary NOTIFY changed)
    Q_PROPERTY(int removalRevision READ removalRevision NOTIFY removalsChanged)

public:
    using Checkpoint = std::function<void(std::function<void(bool)>)>;

    explicit NuvioAccount(AccountCredentialStore *vault, QObject *parent = nullptr,
                          NuvioApi::Transport transport = {});
    void activate(const QString &profileId, CollectionStore *collection,
        ProgressStore *progress, HistoryStore *history, ProfilePreferencesStore *preferences,
        const QString &cursorPath = {});
    void setExtensions(ExtensionsStore *extensions);
    void setCheckpoint(Checkpoint checkpoint);
    void scheduleSync();

    bool available() const { return m_available; }
    bool connected() const { return m_api.connected(); }
    bool busy() const { return m_busy; }
    bool needsReconnect() const;
    QString phase() const { return m_phase; }
    QString message() const { return m_message; }
    QString primaryProvider() const;
    QVariantList profiles() const;
    QString selectedProfileName() const { return m_selection.name; }
    QString connectedProfileName() const;
    QVariantList preview() const { return m_preview; }
    bool previewBecomesPrimary() const { return m_primaryPreview; }
    int removalRevision() const { return m_removalRevision; }

    Q_INVOKABLE void signIn(const QString &email, const QString &password);
    Q_INVOKABLE void loadProfiles();
    Q_INVOKABLE void previewProfile(int index, bool firstRun = false);
    Q_INVOKABLE void unlockProfile(const QString &pin);
    Q_INVOKABLE void cancelPin();
    Q_INVOKABLE void apply(bool firstRun = false);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void makePrimary();
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE bool canUnwatchEpisode(const QString &id) const;
    Q_INVOKABLE bool unwatchEpisode(const QString &id);
    Q_INVOKABLE bool episodeUnwatched(const QString &id) const;
    Q_INVOKABLE bool nextUpDismissed(const QString &seedEpisodeId) const;
    Q_INVOKABLE bool dismissNextUp(const QString &seedEpisodeId);

signals:
    void changed();
    void canonicalChanged();
    void removalsChanged();
    void nextUpChanged();

private:
    struct RemoteState {
        NuvioTheatre::Snapshot snapshot;
        std::optional<NuvioTheatre::Snapshot> changes;
        std::array<qint64, 3> cursors{};
        QString profileId;
    };
    struct Pull;
    struct Push;

    using ProfilesCompletion = std::function<void(QList<NuvioTheatre::Profile>, int)>;
    using PullCompletion = std::function<void(std::optional<RemoteState>, int)>;

    void setStatus(const QString &phase, bool busy, const QString &message = {});
    void resetFlow();
    void readProfiles(ProfilesCompletion completion);
    bool profileUnlocked(const NuvioTheatre::Profile &profile) const;
    void previewSelection(const NuvioTheatre::Profile &profile, bool primary);
    void readSnapshot(const NuvioTheatre::Profile &profile, bool primary,
        bool addons, bool incremental, PullCompletion completion);
    void readSection(const std::shared_ptr<Pull> &pull);
    void readPage(const std::shared_ptr<Pull> &pull);
    void readDelta(const std::shared_ptr<Pull> &pull);
    void finishSection(const std::shared_ptr<Pull> &pull);
    void failPull(const std::shared_ptr<Pull> &pull, int status);
    void syncApply(const NuvioTheatre::Profile &profile, RemoteState state);
    void writeNext(const std::shared_ptr<Push> &push);
    void readProgressDeletionKeys(const std::shared_ptr<Push> &push);
    void bindRemovalScope();
    void bindNextUpScope();
    void syncNextUp(const std::shared_ptr<Push> &push);
    void restoreNextUpOnPlayback(const QString &episodeId);
    QStringList nextUpKeys(const QString &episodeId) const;
    bool loadRemovals();
    bool saveRemovals(QList<NuvioTheatre::Removal> next);
    bool queueRemoval(NuvioTheatre::RemovalKind kind, const QString &id,
        const QString &type, bool wholeTitle = false);
    bool applyLocalRemovals();
    bool acknowledgeRemoval(const NuvioTheatre::Removal &removal);
    QList<NuvioTheatre::Removal> applicableRemovals(const QString &profileId) const;
    void finishSync(bool ok, int status = 0);
    void checkpoint(std::function<void(bool)> completion);
    std::optional<std::array<qint64, 3>> loadCursors(const QString &sourceProfile) const;
    bool saveCursors(const RemoteState &state) const;
    void primaryChanged();
    NuvioTheatre::Local local() const;
    QString failure(int status) const;

    AccountCredentialStore *m_vault;
    NuvioApi m_api;
    NuvioNextUp m_nextUp;
    bool m_nextUpReady = false;
    TheatreAccountImport m_import;
    QPointer<CollectionStore> m_collection;
    QPointer<ProgressStore> m_progress;
    QPointer<HistoryStore> m_history;
    QPointer<ProfilePreferencesStore> m_preferences;
    QPointer<ExtensionsStore> m_extensions;
    QList<QMetaObject::Connection> m_ownerConnections;
    Checkpoint m_checkpoint;
    QTimer m_periodicTimer;
    QTimer m_dirtyTimer;
    QList<NuvioTheatre::Profile> m_profiles;
    NuvioTheatre::Profile m_selection;
    QSet<QString> m_unlockedProfiles;
    std::optional<RemoteState> m_reviewed;
    std::optional<RemoteState> m_baseline;
    QVariantList m_preview;
    QString m_cursorPath;
    QString m_removalAccount;
    QString m_removalProfile;
    QString m_removalPath;
    QList<NuvioTheatre::Removal> m_removals;
    int m_removalRevision = 0;
    bool m_removalsLoaded = false;
    QString m_phase = QStringLiteral("idle");
    QString m_message;
    quint64 m_generation = 0;
    qint64 m_primarySince = 0;
    bool m_available = false;
    bool m_busy = false;
    bool m_manual = false;
    bool m_commitStarted = false;
    bool m_primaryPreview = false;
    bool m_syncing = false;
    bool m_syncRequested = false;
};
