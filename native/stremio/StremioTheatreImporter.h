#pragma once

#include "StremioCodec.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class CollectionStore;
class ProgressStore;
class HistoryStore;
class SyncAdapterRegistry;
class StremioSync;
struct StremioProviderImportRedo;

// Native-only provider-import boundary. It accepts an already decoded Stremio
// item, applies only canonical Theatre owners, and waits for every async owner
// receipt before declaring the import complete. It never owns credentials,
// transport requests, or a QML projection.
class StremioTheatreImporter final : public QObject {
public:
    using Completion = std::function<void(bool, const QString &)>;
    using NeonCheckpoint = std::function<void(Completion)>;

    explicit StremioTheatreImporter(
        CollectionStore *collection,
        ProgressStore *progress,
        HistoryStore *history,
        QObject *parent = nullptr);

    void activate(const QString &profileId);
    void deactivate();
    // AccountRuntime supplies its active ordinary-sync registry. When present,
    // provider imports must use it for owner admission, remote receipts, and
    // echo suppression; the null seam is limited to owner-only tests.
    void setSyncAdapterRegistry(SyncAdapterRegistry *registry);
    void setStremioSync(StremioSync *sync);
    void setNeonCheckpoint(NeonCheckpoint checkpoint);

    bool apply(const StremioLibraryItem &item, Completion completion);
    // Replays the private, projection-only redo after a crash. AccountRuntime
    // supplies only a redo returned by the active StremioSync binding.
    bool replayProviderImport(
        const StremioProviderImportRedo &redo,
        Completion completion);
    // Replays the bounded canonical owner projection embedded in an
    // AccountRuntime episode-pending redo. The outer redo already exists, so
    // this deliberately does not create or settle a second provider receipt.
    bool applyCanonicalProjectionAfterRedo(
        const StremioLibraryItem &item,
        const QJsonObject &projection,
        Completion completion);
    bool applyWatchedEpisodes(
        const StremioLibraryItem &series,
        const QString &encodedWatched,
        const QList<StremioEpisodeIdentity> &videos,
        Completion completion);
    bool removeFromColosseumAndStremio(
        StremioSync *sync,
        const QString &id,
        Completion completion);

private:
    struct Binding {
        QString profileId;
        quint64 generation = 0;
    };
    struct Pending;
    struct EpisodePending;

    bool bindingCurrent(const Binding &binding) const;
    void applyAfterRedo(const std::shared_ptr<Pending> &pending);
    void applyCollection(const std::shared_ptr<Pending> &pending);
    void applyProgress(const std::shared_ptr<Pending> &pending);
    void applyWatchState(const std::shared_ptr<Pending> &pending);
    void applyHistory(const std::shared_ptr<Pending> &pending);
    void applyWatchedAfterRedo(const std::shared_ptr<EpisodePending> &pending);
    void applyNextWatchedEpisode(const std::shared_ptr<EpisodePending> &pending);
    void finish(const std::shared_ptr<Pending> &pending, bool committed,
                const QString &error = QString());
    void finishEpisodes(const std::shared_ptr<EpisodePending> &pending,
                        bool committed, const QString &error = QString());

    CollectionStore *m_collection = nullptr;
    ProgressStore *m_progress = nullptr;
    HistoryStore *m_history = nullptr;
    SyncAdapterRegistry *m_registry = nullptr;
    StremioSync *m_sync = nullptr;
    Binding m_binding;
    NeonCheckpoint m_neonCheckpoint;
};
