#include "ConsumptionHistoryBridge.h"

#include "ActivityStore.h"
#include "HistoryStore.h"
#include "../ProgressStore.h"

#include <QVariantMap>

namespace {

bool compatibleVideoCompletionKinds(const QString &left, const QString &right)
{
    const auto isVideoKind = [](const QString &kind) {
        return kind == QLatin1String("video")
            || kind == QLatin1String("movie")
            || kind == QLatin1String("episode");
    };
    return left == right || (isVideoKind(left) && isVideoKind(right));
}

} // namespace

ConsumptionHistoryBridge::ConsumptionHistoryBridge(ActivityStore *activity,
                                                   ProgressStore *progress,
                                                   HistoryStore *history,
                                                   QObject *parent)
    : QObject(parent), m_activity(activity), m_progress(progress), m_history(history) {
    if (m_activity) {
        connect(m_activity, &ActivityStore::factCommitted, this,
                [this](const QVariantMap &event) {
                    if (m_clearInProgress || projectActivityFact(event))
                        return;
                    emit projectionError(QStringLiteral("Activity fact could not be projected"));
                }, Qt::DirectConnection);
        connect(m_activity, &ActivityStore::resetApplied, this,
                [this](quint64, qint64 resetAtMs) {
                    if (m_clearInProgress)
                        return;
                    if (!m_history || m_history->clearSyncedAll(resetAtMs))
                        return;
                    emit projectionError(QStringLiteral("Activity reset could not clear History"));
                }, Qt::DirectConnection);
    }
    if (m_progress) {
        connect(m_progress, &ProgressStore::completionCrossed, this,
                [this](const QString &kind, const QString &id, qint64 at,
                       const QString &activityEventId, const QString &activitySessionId) {
                    if (projectProgressCompletion(kind, id, at,
                                                  activityEventId, activitySessionId))
                        return;
                    emit projectionError(QStringLiteral("Progress completion could not enter History."));
                }, Qt::DirectConnection);
    }
}

bool ConsumptionHistoryBridge::projectActivityFact(const QVariantMap &event) {
    if (!event.value(QStringLiteral("syncable")).toBool())
        return true;
    const QString kind = event.value(QStringLiteral("kind")).toString().trimmed();
    const QString id = event.value(QStringLiteral("itemKey")).toString().trimmed();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (!m_history)
        return false;
    if (type == QLatin1String("playback_delta"))
        return m_history->recordActivityRange(kind, id,
            event.value(QStringLiteral("startAtMs")).toLongLong(),
            event.value(QStringLiteral("endAtMs")).toLongLong());
    if (type == QLatin1String("reading_delta"))
        return m_history->recordActivity(kind, id, event.value(QStringLiteral("atMs")).toLongLong());
    if (type == QLatin1String("media_completed"))
        return m_history->markCompleted(kind, id, event.value(QStringLiteral("atMs")).toLongLong());
    return true;
}

bool ConsumptionHistoryBridge::projectProgressCompletion(const QString &kind, const QString &id,
                                                          qint64 completedAtMs,
                                                          const QString &activityEventId,
                                                          const QString &activitySessionId) {
    if (m_activity && !m_activity->retentionEnabled())
        return true;
    QString verifiedActivityEventId;
    QString verifiedActivitySessionId = activitySessionId;
    if (m_activity && !activityEventId.isEmpty()) {
        const QVariantMap event = m_activity->historyProjectionFact(activityEventId);
        if (event.value(QStringLiteral("type")).toString() == QLatin1String("media_completed")
            && compatibleVideoCompletionKinds(
                event.value(QStringLiteral("kind")).toString(), kind)
            && event.value(QStringLiteral("itemKey")).toString() == id
            && event.value(QStringLiteral("_trackerOrigin")).toString()
                == QLatin1String("native_local")) {
            verifiedActivityEventId = activityEventId;
            if (!verifiedActivitySessionId.isEmpty()
                && verifiedActivitySessionId
                    != event.value(QStringLiteral("sessionId")).toString())
                verifiedActivitySessionId.clear();
        }
    }
    return m_history && m_history->markProgressCompleted(
        kind, id, completedAtMs, verifiedActivityEventId, verifiedActivitySessionId);
}

bool ConsumptionHistoryBridge::replayExisting(QString *error) {
    if (!m_activity || !m_history) {
        if (error) *error = QStringLiteral("Consumption history bridge has no stores");
        return false;
    }
    for (const QVariantMap &event : m_activity->historyProjectionFacts()) {
        if (!projectActivityFact(event)) {
            if (error) *error = QStringLiteral("An Activity fact could not be projected");
            emit projectionError(error ? *error : QStringLiteral("An Activity fact could not be projected"));
            return false;
        }
    }
    return true;
}

bool ConsumptionHistoryBridge::clearAll() {
    if (!m_activity || !m_history)
        return false;
    m_clearInProgress = true;
    const bool activityCleared = m_activity->clearAll();
    m_clearInProgress = false;
    if (!activityCleared)
        return false;
    return m_history->clearAll();
}
