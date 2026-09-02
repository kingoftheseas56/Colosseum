#include "WatchStateSyncAdapter.h"

#include "CoreStateSyncProjection.h"
#include "ProgressStore.h"

#include <QHash>
#include <QJsonObject>
#include <QStringList>

#include <cmath>
#include <limits>

WatchStateSyncAdapter::WatchStateSyncAdapter(
    ProgressStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(store);

    setObjectName(
        QStringLiteral(
            "watchStateSyncAdapter"));

    if (!store)
        return;

    m_revision =
        static_cast<quint64>(
            qMax(
                0,
                store->revision()));

    connect(
        store,
        &ProgressStore::changed,
        this,
        &WatchStateSyncAdapter::
            handleStoreChanged);
}

QString WatchStateSyncAdapter::
categoryId() const {
    return QStringLiteral("watch_state");
}

int WatchStateSyncAdapter::
schemaVersion() const {
    return 1;
}

quint64 WatchStateSyncAdapter::
revision() const {
    if (!m_store)
        return m_revision;

    return qMax(
        m_revision,
        static_cast<quint64>(
            qMax(
                0,
                m_store->revision())));
}

bool WatchStateSyncAdapter::
exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot) {
        return fail(
            error,
            QStringLiteral(
                "Watch-state sync requires an export output object."));
    }

    if (!m_store) {
        return fail(
            error,
            QStringLiteral(
                "The watch-state owner is no longer available."));
    }

    snapshot->revision = revision();
    snapshot->records.clear();

    const QHash<QString, int> watched =
        m_store->syncWatchedMarks();
    QStringList watchedIds = watched.keys();
    watchedIds.sort();

    for (const QString &id : watchedIds) {
        const int mark = watched.value(id);
        if (id.isEmpty()
            || (mark != -1 && mark != 1)) {
            return fail(
                error,
                QStringLiteral(
                    "The watch-state owner returned a noncanonical watched mark."));
        }

        const QString recordKey =
            CoreStateSyncProjection::
                watchedMarkKey(id);
        if (recordKey.isEmpty()) {
            return fail(
                error,
                QStringLiteral(
                    "The watch-state owner returned an invalid watched identity."));
        }

        snapshot->records.append(
            SyncAdapterRecord{
                recordKey,
                QJsonObject{
                    {
                        QStringLiteral("id"),
                        id
                    },
                    {
                        QStringLiteral("mark"),
                        mark
                    }
                }});
    }

    const QHash<QString, int> seasons =
        m_store->syncLastSeasons();
    QStringList seriesIds = seasons.keys();
    seriesIds.sort();

    for (const QString &seriesId : seriesIds) {
        const int season = seasons.value(seriesId);
        if (seriesId.isEmpty() || season <= 0) {
            return fail(
                error,
                QStringLiteral(
                    "The watch-state owner returned a noncanonical last-season value."));
        }

        const QString recordKey =
            CoreStateSyncProjection::
                lastSeasonKey(seriesId);
        if (recordKey.isEmpty()) {
            return fail(
                error,
                QStringLiteral(
                    "The watch-state owner returned an invalid series identity."));
        }

        snapshot->records.append(
            SyncAdapterRecord{
                recordKey,
                QJsonObject{
                    {
                        QStringLiteral("seriesId"),
                        seriesId
                    },
                    {
                        QStringLiteral("season"),
                        season
                    }
                }});
    }

    return true;
}

bool WatchStateSyncAdapter::
applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    QString *error) {
    if (!m_store) {
        return fail(
            error,
            QStringLiteral(
                "The watch-state owner is no longer available."));
    }

    if (schemaVersion != 1) {
        return fail(
            error,
            QStringLiteral(
                "The watch-state sync schema is unsupported."));
    }

    QString id;
    const bool watchedRecord =
        CoreStateSyncProjection::
            decodeWatchedMarkKey(
                recordKey,
                &id);

    QString seriesId;
    const bool seasonRecord =
        !watchedRecord
        && CoreStateSyncProjection::
               decodeLastSeasonKey(
                   recordKey,
                   &seriesId);

    if (!watchedRecord && !seasonRecord) {
        return fail(
            error,
            QStringLiteral(
                "The watch-state sync record key is invalid."));
    }

    if (operation == SyncWireOperation::Delete) {
        m_applyingRemote = true;
        const bool applied = watchedRecord
            ? m_store->removeSyncedWatchedMark(id)
            : m_store->removeSyncedLastSeason(seriesId);
        m_applyingRemote = false;

        if (!applied) {
            return fail(
                error,
                QStringLiteral(
                    "The watch-state owner rejected the remote delete."));
        }
        return true;
    }

    if (!payload.isObject()) {
        return fail(
            error,
            QStringLiteral(
                "A watch-state PUT requires an object payload."));
    }

    const QJsonObject object = payload.toObject();

    if (watchedRecord) {
        if (object.size() != 2
            || !object.value(
                    QStringLiteral("id"))
                    .isString()
            || object.value(
                   QStringLiteral("id"))
                   .toString()
                   != id
            || !object.value(
                    QStringLiteral("mark"))
                    .isDouble()) {
            return fail(
                error,
                QStringLiteral(
                    "The watched-mark payload is not canonical for its record key."));
        }

        const double rawMark =
            object.value(
                QStringLiteral("mark"))
                .toDouble();
        if (rawMark != -1.0 && rawMark != 1.0) {
            return fail(
                error,
                QStringLiteral(
                    "A watched mark must be exactly -1 or 1."));
        }

        m_applyingRemote = true;
        const bool applied =
            m_store->applySyncedWatchedMark(
                id,
                static_cast<int>(rawMark));
        m_applyingRemote = false;

        if (!applied) {
            return fail(
                error,
                QStringLiteral(
                    "The watch-state owner rejected the remote watched mark."));
        }
        return true;
    }

    if (object.size() != 2
        || !object.value(
                QStringLiteral("seriesId"))
                .isString()
        || object.value(
               QStringLiteral("seriesId"))
               .toString()
               != seriesId
        || !object.value(
                QStringLiteral("season"))
                .isDouble()) {
        return fail(
            error,
            QStringLiteral(
                "The last-season payload is not canonical for its record key."));
    }

    const double rawSeason =
        object.value(
            QStringLiteral("season"))
            .toDouble();
    if (!std::isfinite(rawSeason)
        || rawSeason <= 0.0
        || rawSeason
               > static_cast<double>(
                   std::numeric_limits<int>::max())
        || std::floor(rawSeason) != rawSeason) {
        return fail(
            error,
            QStringLiteral(
                "A last-season value must be a positive integer."));
    }

    m_applyingRemote = true;
    const bool applied =
        m_store->applySyncedLastSeason(
            seriesId,
            static_cast<int>(rawSeason));
    m_applyingRemote = false;

    if (!applied) {
        return fail(
            error,
            QStringLiteral(
                "The watch-state owner rejected the remote last-season value."));
    }
    return true;
}

void WatchStateSyncAdapter::
handleStoreChanged() {
    if (!m_store)
        return;

    m_revision =
        qMax(
            m_revision,
            static_cast<quint64>(
                qMax(
                    0,
                    m_store->revision())));

    if (m_applyingRemote)
        return;

    emit localMutationAvailable(
        revision());
}

bool WatchStateSyncAdapter::fail(
    QString *error,
    const QString &detail) {
    if (error)
        *error = detail;
    return false;
}
