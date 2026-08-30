#pragma once

#include <QObject>

class ActivityStore;
class HistoryStore;
class ProgressStore;

class ConsumptionHistoryBridge final : public QObject {
    Q_OBJECT
public:
    ConsumptionHistoryBridge(ActivityStore *activity, ProgressStore *progress,
                              HistoryStore *history, QObject *parent = nullptr);

    bool replayExisting(QString *error = nullptr);
    Q_INVOKABLE bool clearAll();

signals:
    void projectionError(const QString &detail);

private:
    bool projectActivityFact(const QVariantMap &event);
    bool projectProgressCompletion(const QString &kind, const QString &id,
                                   qint64 completedAtMs);

    ActivityStore *m_activity = nullptr;
    ProgressStore *m_progress = nullptr;
    HistoryStore *m_history = nullptr;
    bool m_clearInProgress = false;
};
