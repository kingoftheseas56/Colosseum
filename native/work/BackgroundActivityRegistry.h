// native/work/BackgroundActivityRegistry.h
#pragma once

#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace work {

// App-owned bulletin board for long-running background jobs (guided comic
// analysis, audiobook text sync, whatever comes next). Native services publish
// presentation-shaped state; QML renders rows and requests pause/resume.
// GUI-thread only: worker threads marshal publish() via queued invokeMethod.
class BackgroundActivityRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
public:
    explicit BackgroundActivityRegistry(QObject *parent = nullptr);

    QVariantList activities() const;

    // Required state keys: title, stage, progress (0..1), paused, canPause.
    // Publishing an existing id updates that row in place.
    void publish(const QString &id, const QVariantMap &state);
    void remove(const QString &id);

    Q_INVOKABLE void requestPause(const QString &id);
    Q_INVOKABLE void requestResume(const QString &id);

signals:
    void activitiesChanged();
    void pauseRequested(const QString &id);
    void resumeRequested(const QString &id);

private:
    QVector<QPair<QString, QVariantMap>> m_rows; // insertion order = display order
};

} // namespace work
