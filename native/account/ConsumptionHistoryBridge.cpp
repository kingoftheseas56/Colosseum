#include "ConsumptionHistoryBridge.h"

#include "ActivityStore.h"
#include "HistoryStore.h"
#include "../ProgressStore.h"

#include <QVariantMap>

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
    }
    if (m_progress) {
        connect(m_progress, &ProgressStore::completionCrossed, this,
                [this](const QString &kind, const QString &id, qint64 at) {
                    if (projectProgressCompletion(kind, id, at))
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
                                                          qint64 completedAtMs) {
    return m_history && m_history->markCompleted(kind, id, completedAtMs);
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
