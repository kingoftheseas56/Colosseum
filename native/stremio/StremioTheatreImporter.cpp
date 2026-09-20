#include "StremioTheatreImporter.h"
#include "StremioSync.h"

#include "CollectionStore.h"
#include "ProgressStore.h"
#include "account/CoreStateSyncProjection.h"
#include "account/HistoryStore.h"
#include "account/SyncAdapterRegistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

#include <optional>

struct StremioTheatreImporter::Pending {
    Binding binding;
    StremioLibraryItem item;
    StremioTheatreItemProjection projection;
    QString providerRedoReceipt;
    Completion completion;
};

struct StremioTheatreImporter::EpisodePending {
    Binding binding;
    QStringList watchedIds;
    qsizetype index = 0;
    QString providerRedoReceipt;
    Completion completion;
};

namespace {
QVariantMap collectionExisting(CollectionStore *store, const QString &id) {
    if (!store)
        return {};
    for (const QVariant &candidate : store->items(QStringLiteral("theatre"))) {
        const QVariantMap entry = candidate.toMap();
        if (entry.value(QStringLiteral("id")).toString() == id)
            return entry;
    }
    return {};
}

bool stremioProgressWins(const QVariantMap &existing,
                         const QVariantMap &candidate) {
    if (existing.isEmpty())
        return true;
    const qint64 currentAt = existing.value(QStringLiteral("updatedAt")).toLongLong();
    const qint64 incomingAt = candidate.value(QStringLiteral("updatedAt")).toLongLong();
    // Only a strictly newer provider activity can replace a known local
    // current state. Equal/missing times retain the acknowledged owner value,
    // so repeated pulls cannot oscillate by arrival time or fresh Neon HLC.
    return incomingAt > 0 && (currentAt <= 0 || incomingAt > currentAt);
}

QJsonObject itemRedoProjection(const StremioTheatreItemProjection &projection) {
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("item")},
        {QStringLiteral("hasCollection"), projection.hasCollection},
        {QStringLiteral("hasProgress"), projection.hasProgress},
        {QStringLiteral("hasHistory"), projection.hasHistory},
        {QStringLiteral("hasWatchState"), projection.hasWatchState},
        {QStringLiteral("watched"), projection.watched},
        {QStringLiteral("watchActionAtMs"), QString::number(projection.watchActionAtMs)},
        {QStringLiteral("collection"), QJsonObject::fromVariantMap(projection.collection)},
        {QStringLiteral("progress"), QJsonObject::fromVariantMap(projection.progress)},
        {QStringLiteral("history"), QJsonObject::fromVariantMap(projection.history)}};
}

std::optional<StremioTheatreItemProjection> itemProjectionFromRedo(
    const QJsonObject &redo) {
    if (redo.value(QStringLiteral("kind")).toString() != QLatin1String("item")
        || (redo.size() != 7 && redo.size() != 10)
        || !redo.value(QStringLiteral("hasCollection")).isBool()
        || !redo.value(QStringLiteral("hasProgress")).isBool()
        || !redo.value(QStringLiteral("hasHistory")).isBool()
        || !redo.value(QStringLiteral("collection")).isObject()
        || !redo.value(QStringLiteral("progress")).isObject()
        || !redo.value(QStringLiteral("history")).isObject()) {
        return std::nullopt;
    }
    StremioTheatreItemProjection projection;
    projection.valid = true;
    projection.hasCollection = redo.value(QStringLiteral("hasCollection")).toBool();
    projection.hasProgress = redo.value(QStringLiteral("hasProgress")).toBool();
    projection.hasHistory = redo.value(QStringLiteral("hasHistory")).toBool();
    projection.collection = redo.value(QStringLiteral("collection")).toObject().toVariantMap();
    projection.progress = redo.value(QStringLiteral("progress")).toObject().toVariantMap();
    projection.history = redo.value(QStringLiteral("history")).toObject().toVariantMap();
    // Seven-key projections predate movie current watch state and remain
    // replayable. New projections carry a canonical bounded timestamp string.
    if (redo.size() == 10) {
        const QJsonValue hasWatch = redo.value(QStringLiteral("hasWatchState"));
        const QJsonValue watched = redo.value(QStringLiteral("watched"));
        const QJsonValue action = redo.value(QStringLiteral("watchActionAtMs"));
        bool actionOk = false;
        const qint64 actionAtMs = action.toString().toLongLong(&actionOk);
        if (!hasWatch.isBool() || !watched.isBool() || !action.isString()
            || !actionOk || actionAtMs < 0
            || QString::number(actionAtMs) != action.toString()
            || (!hasWatch.toBool() && (watched.toBool() || actionAtMs != 0))) {
            return std::nullopt;
        }
        projection.hasWatchState = hasWatch.toBool();
        projection.watched = watched.toBool();
        projection.watchActionAtMs = actionAtMs;
    }
    return projection;
}

QJsonObject episodeRedoProjection(const QStringList &watchedIds) {
    QJsonArray ids;
    for (const QString &id : watchedIds)
        ids.append(id);
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("episodes")},
        {QStringLiteral("episodeIds"), ids}};
}

std::optional<QStringList> episodeIdsFromRedo(const QJsonObject &redo) {
    if (redo.value(QStringLiteral("kind")).toString() != QLatin1String("episodes")
        || redo.size() != 2 || !redo.value(QStringLiteral("episodeIds")).isArray()) {
        return std::nullopt;
    }
    QStringList ids;
    const QJsonArray values = redo.value(QStringLiteral("episodeIds")).toArray();
    if (values.size() > 512)
        return std::nullopt;
    for (const QJsonValue &value : values) {
        const QString id = value.toString().trimmed();
        if (id.isEmpty() || id != value.toString() || id.size() > 512)
            return std::nullopt;
        ids.append(id);
    }
    return ids;
}
}

StremioTheatreImporter::StremioTheatreImporter(
    CollectionStore *collection,
    ProgressStore *progress,
    HistoryStore *history,
    QObject *parent)
    : QObject(parent),
      m_collection(collection),
      m_progress(progress),
      m_history(history) {
}

void StremioTheatreImporter::activate(const QString &profileId) {
    ++m_binding.generation;
    m_binding.profileId = profileId.trimmed();
}

void StremioTheatreImporter::deactivate() {
    ++m_binding.generation;
    m_binding.profileId.clear();
}

void StremioTheatreImporter::setSyncAdapterRegistry(SyncAdapterRegistry *registry) {
    m_registry = registry;
}

void StremioTheatreImporter::setStremioSync(StremioSync *sync) {
    m_sync = sync;
}

void StremioTheatreImporter::setNeonCheckpoint(NeonCheckpoint checkpoint) {
    m_neonCheckpoint = std::move(checkpoint);
}

bool StremioTheatreImporter::bindingCurrent(const Binding &binding) const {
    return !binding.profileId.isEmpty()
        && binding.profileId == m_binding.profileId
        && binding.generation == m_binding.generation;
}

bool StremioTheatreImporter::apply(
    const StremioLibraryItem &item,
    Completion completion) {
    if (!m_collection || !m_progress || !m_history || m_binding.profileId.isEmpty()) {
        if (completion)
            completion(false, QStringLiteral("The active Theatre owners are unavailable."));
        return false;
    }
    StremioTheatreItemProjection projection =
        StremioCodec::projectTheatreItem(item);
    if (!projection.valid) {
        if (completion)
            completion(false, projection.error.isEmpty()
                                  ? QStringLiteral("The Stremio library item is malformed.")
                                  : projection.error);
        return false;
    }
    // A passive provider pull never overrides the user's explicit local-only
    // library removal. Keep the item’s playback/history projection intact;
    // only membership is suppressed, with the durable decision owned by the
    // profile-local Stremio state rather than transient arrival order.
    if (item.libraryMember && m_sync
        && m_sync->suppressesRemoteLibraryMembership(item.id, item.type)) {
        projection.hasCollection = false;
        projection.collection.clear();
    }
    const auto pending = std::make_shared<Pending>(Pending{m_binding, item, projection, {},
                                                            std::move(completion)});
    if (!m_sync) {
        applyAfterRedo(pending);
        return true;
    }
    QPointer<StremioTheatreImporter> self(this);
    if (!m_sync->beginProviderImport(
            item,
            itemRedoProjection(projection),
            [self, pending](bool committed, const QString &receipt) {
                if (!self)
                    return;
                if (!committed || receipt.isEmpty()) {
                    self->finish(
                        pending, false,
                        QStringLiteral("The Stremio provider-import redo was not committed."));
                    return;
                }
                pending->providerRedoReceipt = receipt;
                self->applyAfterRedo(pending);
            })) {
        finish(pending, false,
               QStringLiteral("The Stremio provider-import redo could not start."));
    }
    return true;
}

bool StremioTheatreImporter::replayProviderImport(
    const StremioProviderImportRedo &redo,
    Completion completion) {
    if (!m_sync || !bindingCurrent(m_binding)
        || redo.operationId.isEmpty()
        || redo.profileId != m_binding.profileId
        || (redo.type != QLatin1String("movie") && redo.type != QLatin1String("series"))) {
        if (completion)
            completion(false, QStringLiteral("The Stremio provider-import redo is invalid."));
        return false;
    }
    bool stillPending = false;
    for (const StremioProviderImportRedo &candidate : m_sync->pendingProviderImports()) {
        if (candidate.operationId == redo.operationId) {
            stillPending = true;
            break;
        }
    }
    if (!stillPending) {
        if (completion)
            completion(false, QStringLiteral("The Stremio provider-import redo is no longer pending."));
        return false;
    }
    const Binding binding = m_binding;
    if (redo.projection.value(QStringLiteral("kind")).toString() == QLatin1String("episodes")) {
        const auto watchedIds = episodeIdsFromRedo(redo.projection);
        if (!watchedIds.has_value()) {
            if (completion)
                completion(false, QStringLiteral("The Stremio episode redo is malformed."));
            return false;
        }
        const auto pending = std::make_shared<EpisodePending>(
            EpisodePending{binding, *watchedIds, 0, redo.operationId, std::move(completion)});
        applyWatchedAfterRedo(pending);
        return true;
    }
    const auto projection = itemProjectionFromRedo(redo.projection);
    if (!projection.has_value()) {
        if (completion)
            completion(false, QStringLiteral("The Stremio provider-import redo is malformed."));
        return false;
    }
    StremioLibraryItem item;
    item.id = redo.id;
    item.type = redo.type;
    item.libraryMember = redo.libraryMember;
    item.removed = redo.removed;
    const auto pending = std::make_shared<Pending>(
        Pending{binding, item, *projection, redo.operationId, std::move(completion)});
    applyAfterRedo(pending);
    return true;
}

bool StremioTheatreImporter::applyCanonicalProjectionAfterRedo(
    const StremioLibraryItem &item,
    const QJsonObject &projection,
    Completion completion) {
    if (!m_collection || !m_progress || !m_history || m_binding.profileId.isEmpty()) {
        if (completion)
            completion(false, QStringLiteral("The active Theatre owners are unavailable."));
        return false;
    }
    const auto parsed = itemProjectionFromRedo(projection);
    if (!parsed.has_value()) {
        if (completion)
            completion(false, QStringLiteral("The Stremio episode owner redo is malformed."));
        return false;
    }
    const auto pending = std::make_shared<Pending>(
        Pending{m_binding, item, *parsed, {}, std::move(completion)});
    applyAfterRedo(pending);
    return true;
}

void StremioTheatreImporter::applyAfterRedo(const std::shared_ptr<Pending> &pending) {
    const StremioLibraryItem &item = pending->item;
    // A remote `removed` flag is observed, never copied into Collection. When
    // the local owner still has the title, persist that inverse intentional
    // difference before continuing so a later relay cannot infer a provider
    // delete or automatic re-add.
    if (item.removed && !collectionExisting(m_collection, item.id).isEmpty()
        && m_sync) {
        QPointer<StremioTheatreImporter> self(this);
        QPointer<StremioSync> syncGuard(m_sync);
        if (!m_sync->recordRemoteLibraryRemoval(
                item.id,
                item.type,
                [self, syncGuard, pending](bool committed) {
                    if (!self)
                        return;
                    if (!committed || !syncGuard) {
                        self->finish(
                            pending, false,
                            QStringLiteral("The remote library-removal difference was not committed."));
                        return;
                    }
                    self->applyCollection(pending);
                })) {
            finish(pending, false,
                   QStringLiteral("The remote library-removal difference could not start."));
        }
        return;
    }
    applyCollection(pending);
}

bool StremioTheatreImporter::applyWatchedEpisodes(
    const StremioLibraryItem &series,
    const QString &encodedWatched,
    const QList<StremioEpisodeIdentity> &videos,
    Completion completion) {
    if (!m_progress || m_binding.profileId.isEmpty()
        || series.type != QLatin1String("series")) {
        if (completion)
            completion(false, QStringLiteral("The active episode-progress owner is unavailable."));
        return false;
    }
    for (const StremioEpisodeIdentity &episode : videos) {
        if (!StremioCodec::episodeBelongsToSeries(series.id, episode)) {
            if (completion) {
                completion(false,
                           QStringLiteral("The Stremio episode map does not belong to its series."));
            }
            return false;
        }
    }
    QSet<QString> watched;
    QString error;
    if (!StremioCodec::decodeWatchedEpisodes(encodedWatched, videos, &watched, &error)) {
        if (completion)
            completion(false, error);
        return false;
    }
    QStringList watchedIds = watched.values();
    watchedIds.sort();
    const auto pending = std::make_shared<EpisodePending>(
        EpisodePending{m_binding, watchedIds, 0, {}, std::move(completion)});
    if (!m_sync) {
        applyWatchedAfterRedo(pending);
        return true;
    }
    QPointer<StremioTheatreImporter> self(this);
    if (!m_sync->beginProviderImport(
            series,
            episodeRedoProjection(watchedIds),
            [self, pending](bool committed, const QString &receipt) {
                if (!self)
                    return;
                if (!committed || receipt.isEmpty()) {
                    self->finishEpisodes(
                        pending, false,
                        QStringLiteral("The Stremio episode-import redo was not committed."));
                    return;
                }
                pending->providerRedoReceipt = receipt;
                self->applyWatchedAfterRedo(pending);
            })) {
        finishEpisodes(pending, false,
                       QStringLiteral("The Stremio episode-import redo could not start."));
    }
    return true;
}

void StremioTheatreImporter::applyWatchedAfterRedo(
    const std::shared_ptr<EpisodePending> &pending) {
    applyNextWatchedEpisode(pending);
}

bool StremioTheatreImporter::removeFromColosseumAndStremio(
    StremioSync *sync,
    const QString &id,
    Completion completion) {
    if (!sync || !m_collection || m_binding.profileId.isEmpty()) {
        if (completion)
            completion(false, QStringLiteral("The active Theatre library owner is unavailable."));
        return false;
    }
    const QString normalizedId = id.trimmed();
    const QVariantMap existing = collectionExisting(m_collection, normalizedId);
    const QString type = existing.value(QStringLiteral("type")).toString();
    if (normalizedId.isEmpty() || (type != QLatin1String("movie")
                                  && type != QLatin1String("series"))) {
        if (completion)
            completion(false, QStringLiteral("The Theatre library record is unavailable."));
        return false;
    }
    const Binding binding = m_binding;
    const auto operation = std::make_shared<QString>();
    QPointer<StremioTheatreImporter> self(this);
    QPointer<StremioSync> syncGuard(sync);
    if (!sync->queueExplicitLibraryRemoval(
            normalizedId,
            type,
            [self, syncGuard, binding, normalizedId, operation, completion](bool journaled) {
                if (!self || !journaled) {
                    if (self && completion)
                        completion(false, QStringLiteral("The Stremio removal intent was not committed."));
                    return;
                }
                if (!self->bindingCurrent(binding) || !syncGuard) {
                    if (completion)
                        completion(false, QStringLiteral("The Stremio removal belongs to an inactive profile."));
                    return;
                }
                if (!self->m_collection->remove(QStringLiteral("theatre"), normalizedId)) {
                    if (completion)
                        completion(false, self->m_collection->persistenceError());
                    return;
                }
                if (!syncGuard->acknowledgeLocalReceipt(*operation)) {
                    if (completion)
                        completion(false, QStringLiteral("The Stremio removal receipt could not be recorded."));
                    return;
                }
                if (completion)
                    completion(true, {});
            },
            operation.get())) {
        return false;
    }
    return true;
}

void StremioTheatreImporter::applyCollection(const std::shared_ptr<Pending> &pending) {
    if (!bindingCurrent(pending->binding)) {
        finish(pending, false, QStringLiteral("The Stremio import belongs to an inactive profile."));
        return;
    }
    if (!pending->projection.hasCollection) {
        applyProgress(pending);
        return;
    }
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::collection(pending->projection.collection);
    if (projected.disposition != CoreStateSyncProjection::Disposition::Portable) {
        finish(pending, false, QStringLiteral("The Stremio Collection projection is not portable."));
        return;
    }
    if (m_registry) {
        if (!m_registry->contains(QStringLiteral("collection"))) {
            finish(pending, false, QStringLiteral("The active Collection sync owner is unavailable."));
            return;
        }
        SyncAdapterMutation mutation;
        mutation.categoryId = QStringLiteral("collection");
        mutation.recordKey = projected.recordKey;
        mutation.schemaVersion = 1;
        mutation.operation = SyncWireOperation::Put;
        mutation.payload = projected.payload;
        SyncAdapterRegistryError error;
        QPointer<StremioTheatreImporter> self(this);
        if (!m_registry->applyRemoteAsync(
                mutation,
                [self, pending](const SyncAdapterRegistryError &result) {
                    if (!self)
                        return;
                    if (!result.isEmpty()) {
                        self->finish(pending, false,
                                     result.detail.isEmpty() ? result.code : result.detail);
                        return;
                    }
                    self->applyProgress(pending);
                },
                &error)) {
            finish(pending, false, error.detail.isEmpty() ? error.code : error.detail);
        }
        return;
    }
    const QVariantMap entry = CoreStateSyncProjection::mergePortableIntoLocal(
        collectionExisting(m_collection, pending->projection.collection.value(QStringLiteral("id")).toString()),
        projected.payload);
    QPointer<StremioTheatreImporter> self(this);
    if (!m_collection->applySyncedEntryAsync(
            QStringLiteral("theatre"), entry,
            [self, pending](bool committed, const QString &error) {
                if (!self)
                    return;
                if (!committed) {
                    self->finish(pending, false, error);
                    return;
                }
                self->applyProgress(pending);
            })) {
        finish(pending, false, m_collection->persistenceError());
    }
}

void StremioTheatreImporter::applyProgress(const std::shared_ptr<Pending> &pending) {
    if (!bindingCurrent(pending->binding)) {
        finish(pending, false, QStringLiteral("The Stremio import belongs to an inactive profile."));
        return;
    }
    if (!pending->projection.hasProgress) {
        applyWatchState(pending);
        return;
    }
    const QString id = pending->projection.progress.value(QStringLiteral("id")).toString();
    const QVariantMap existing = m_progress->get(QStringLiteral("video"), id);
    if (!stremioProgressWins(existing, pending->projection.progress)) {
        applyWatchState(pending);
        return;
    }
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::progress(pending->projection.progress);
    if (projected.disposition != CoreStateSyncProjection::Disposition::Portable) {
        finish(pending, false, QStringLiteral("The Stremio progress projection is not portable."));
        return;
    }
    if (m_registry) {
        if (!m_registry->contains(QStringLiteral("continue_progress"))) {
            finish(pending, false, QStringLiteral("The active progress sync owner is unavailable."));
            return;
        }
        SyncAdapterMutation mutation;
        mutation.categoryId = QStringLiteral("continue_progress");
        mutation.recordKey = projected.recordKey;
        mutation.schemaVersion = 1;
        mutation.operation = SyncWireOperation::Put;
        mutation.payload = projected.payload;
        SyncAdapterRegistryError error;
        QPointer<StremioTheatreImporter> self(this);
        if (!m_registry->applyRemoteAsync(
                mutation,
                [self, pending](const SyncAdapterRegistryError &result) {
                    if (!self)
                        return;
                    if (!result.isEmpty()) {
                        self->finish(pending, false,
                                     result.detail.isEmpty() ? result.code : result.detail);
                        return;
                    }
                    self->applyWatchState(pending);
                },
                &error)) {
            finish(pending, false, error.detail.isEmpty() ? error.code : error.detail);
        }
        return;
    }
    const QVariantMap entry = CoreStateSyncProjection::mergePortableIntoLocal(
        existing, projected.payload);
    QPointer<StremioTheatreImporter> self(this);
    if (!m_progress->applySyncedEntryAsync(
            entry,
            [self, pending](bool committed, const QString &error) {
                if (!self)
                    return;
                if (!committed) {
                    self->finish(pending, false, error);
                    return;
                }
                self->applyWatchState(pending);
            })) {
        finish(pending, false, m_progress->persistenceError());
    }
}

void StremioTheatreImporter::applyWatchState(
    const std::shared_ptr<Pending> &pending) {
    if (!bindingCurrent(pending->binding)) {
        finish(pending, false, QStringLiteral("The Stremio import belongs to an inactive profile."));
        return;
    }
    if (!pending->projection.hasWatchState) {
        applyHistory(pending);
        return;
    }

    const QString id = pending->item.id;
    const int mark = pending->projection.watched ? 1 : -1;
    const qint64 actionAtMs = pending->projection.watchActionAtMs;
    if (m_registry) {
        if (!m_registry->contains(QStringLiteral("watch_state"))) {
            finish(pending, false, QStringLiteral("The active watch-state sync owner is unavailable."));
            return;
        }
        SyncAdapterMutation mutation;
        mutation.categoryId = QStringLiteral("watch_state");
        mutation.recordKey = CoreStateSyncProjection::watchedMarkKey(id);
        mutation.schemaVersion = 1;
        mutation.operation = SyncWireOperation::Put;
        QJsonObject payload{
            {QStringLiteral("id"), id},
            {QStringLiteral("mark"), mark},
            {QStringLiteral("manual"), false}};
        if (actionAtMs > 0)
            payload.insert(QStringLiteral("actionAtMs"), QString::number(actionAtMs));
        mutation.payload = payload;
        SyncAdapterRegistryError error;
        QPointer<StremioTheatreImporter> self(this);
        if (!m_registry->applyRemoteAsync(
                mutation,
                [self, pending](const SyncAdapterRegistryError &result) {
                    if (!self)
                        return;
                    if (!result.isEmpty()) {
                        self->finish(pending, false,
                                     result.detail.isEmpty() ? result.code : result.detail);
                        return;
                    }
                    self->applyHistory(pending);
                },
                &error)) {
            finish(pending, false, error.detail.isEmpty() ? error.code : error.detail);
        }
        return;
    }

    if (!m_progress->applySyncedWatchedMark(id, mark, actionAtMs)) {
        finish(pending, false, m_progress->persistenceError());
        return;
    }
    applyHistory(pending);
}

void StremioTheatreImporter::applyHistory(const std::shared_ptr<Pending> &pending) {
    if (!bindingCurrent(pending->binding)) {
        finish(pending, false, QStringLiteral("The Stremio import belongs to an inactive profile."));
        return;
    }
    if (pending->projection.hasHistory) {
        if (m_registry) {
            if (!m_registry->contains(QStringLiteral("full_history"))) {
                finish(pending, false, QStringLiteral("The active History sync owner is unavailable."));
                return;
            }
            const CoreStateSyncProjection projected =
                CoreStateSyncProjection::history(pending->projection.history);
            if (projected.disposition != CoreStateSyncProjection::Disposition::Portable) {
                finish(pending, false, QStringLiteral("The Stremio History projection is not portable."));
                return;
            }
            SyncAdapterMutation mutation;
            mutation.categoryId = QStringLiteral("full_history");
            mutation.recordKey = projected.recordKey;
            mutation.schemaVersion = 1;
            mutation.operation = SyncWireOperation::Put;
            mutation.payload = projected.payload;
            SyncAdapterRegistryError error;
            if (!m_registry->applyRemote(mutation, &error)) {
                finish(pending, false, error.detail.isEmpty() ? error.code : error.detail);
                return;
            }
        } else if (!m_history->applySyncedRecord(pending->projection.history)) {
            finish(pending, false, QStringLiteral("The Stremio History record could not be committed."));
            return;
        }
    }
    if (m_neonCheckpoint) {
        QPointer<StremioTheatreImporter> self(this);
        m_neonCheckpoint([self, pending](bool committed, const QString &error) {
            if (!self)
                return;
            self->finish(pending, committed, error);
        });
        return;
    }
    finish(pending, true);
}

void StremioTheatreImporter::applyNextWatchedEpisode(
    const std::shared_ptr<EpisodePending> &pending) {
    if (!bindingCurrent(pending->binding)) {
        finishEpisodes(pending, false,
                       QStringLiteral("The Stremio import belongs to an inactive profile."));
        return;
    }
    if (pending->index >= pending->watchedIds.size()) {
        if (m_neonCheckpoint) {
            QPointer<StremioTheatreImporter> self(this);
            m_neonCheckpoint([self, pending](bool committed, const QString &error) {
                if (self)
                    self->finishEpisodes(pending, committed, error);
            });
        } else {
            finishEpisodes(pending, true);
        }
        return;
    }

    const QString id = pending->watchedIds.at(pending->index++);
    const QVariantMap existing = m_progress->get(QStringLiteral("video"), id);
    // A watched bit has no per-episode activity time. Do not use provider
    // arrival time to replace an acknowledged local state; new exact episodes
    // are the only safe application until a real timestamp is available.
    if (!existing.isEmpty()) {
        applyNextWatchedEpisode(pending);
        return;
    }
    const QVariantMap entry{
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("id"), id},
        {QStringLiteral("progress"), 1.0}};
    if (m_registry) {
        if (!m_registry->contains(QStringLiteral("continue_progress"))) {
            finishEpisodes(pending, false,
                           QStringLiteral("The active progress sync owner is unavailable."));
            return;
        }
        const CoreStateSyncProjection projected =
            CoreStateSyncProjection::progress(entry);
        if (projected.disposition != CoreStateSyncProjection::Disposition::Portable) {
            finishEpisodes(pending, false,
                           QStringLiteral("The Stremio episode progress is not portable."));
            return;
        }
        SyncAdapterMutation mutation;
        mutation.categoryId = QStringLiteral("continue_progress");
        mutation.recordKey = projected.recordKey;
        mutation.schemaVersion = 1;
        mutation.operation = SyncWireOperation::Put;
        mutation.payload = projected.payload;
        SyncAdapterRegistryError error;
        QPointer<StremioTheatreImporter> self(this);
        if (!m_registry->applyRemoteAsync(
                mutation,
                [self, pending](const SyncAdapterRegistryError &result) {
                    if (!self)
                        return;
                    if (!result.isEmpty()) {
                        self->finishEpisodes(
                            pending, false,
                            result.detail.isEmpty() ? result.code : result.detail);
                        return;
                    }
                    self->applyNextWatchedEpisode(pending);
                },
                &error)) {
            finishEpisodes(pending, false,
                           error.detail.isEmpty() ? error.code : error.detail);
        }
        return;
    }
    QPointer<StremioTheatreImporter> self(this);
    if (!m_progress->applySyncedEntryAsync(
            entry,
            [self, pending](bool committed, const QString &error) {
                if (!self)
                    return;
                if (!committed) {
                    self->finishEpisodes(pending, false, error);
                    return;
                }
                self->applyNextWatchedEpisode(pending);
            })) {
        finishEpisodes(pending, false, m_progress->persistenceError());
    }
}

void StremioTheatreImporter::finish(
    const std::shared_ptr<Pending> &pending,
    bool committed,
    const QString &error) {
    const bool activeAndCommitted = committed && bindingCurrent(pending->binding);
    if (!activeAndCommitted || pending->providerRedoReceipt.isEmpty() || !m_sync) {
        if (pending->completion)
            pending->completion(activeAndCommitted, error);
        return;
    }
    QPointer<StremioTheatreImporter> self(this);
    QPointer<StremioSync> syncGuard(m_sync);
    if (!m_sync->settleProviderImport(
            pending->providerRedoReceipt,
            [self, syncGuard, pending, error](bool settled) {
                if (!self || !pending->completion)
                    return;
                pending->completion(
                    settled && syncGuard && self->bindingCurrent(pending->binding),
                    settled ? QString() : QStringLiteral("The Stremio provider-import redo could not settle."));
            })) {
        if (pending->completion)
            pending->completion(false,
                                QStringLiteral("The Stremio provider-import redo could not settle."));
    }
}

void StremioTheatreImporter::finishEpisodes(
    const std::shared_ptr<EpisodePending> &pending,
    bool committed,
    const QString &error) {
    const bool activeAndCommitted = committed && bindingCurrent(pending->binding);
    if (!activeAndCommitted || pending->providerRedoReceipt.isEmpty() || !m_sync) {
        if (pending->completion)
            pending->completion(activeAndCommitted, error);
        return;
    }
    QPointer<StremioTheatreImporter> self(this);
    QPointer<StremioSync> syncGuard(m_sync);
    if (!m_sync->settleProviderImport(
            pending->providerRedoReceipt,
            [self, syncGuard, pending](bool settled) {
                if (!self || !pending->completion)
                    return;
                pending->completion(
                    settled && syncGuard && self->bindingCurrent(pending->binding),
                    settled ? QString() : QStringLiteral("The Stremio episode-import redo could not settle."));
            })) {
        if (pending->completion)
            pending->completion(false,
                                QStringLiteral("The Stremio episode-import redo could not settle."));
    }
}
