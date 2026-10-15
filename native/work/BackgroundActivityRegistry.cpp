// native/work/BackgroundActivityRegistry.cpp
#include "work/BackgroundActivityRegistry.h"

#include "work/BackgroundWorkCoordinator.h"

namespace work {

BackgroundActivityRegistry::BackgroundActivityRegistry(QObject *parent)
    : QObject(parent)
{
}

void BackgroundActivityRegistry::setCoordinator(BackgroundWorkCoordinator *coordinator)
{
    if (m_coordinator == coordinator)
        return;

    QObject::disconnect(m_pauseRequestConnection);
    QObject::disconnect(m_resumeRequestConnection);
    QObject::disconnect(m_pauseStateConnection);
    m_coordinator = coordinator;

    if (!m_coordinator)
        return;

    m_pauseRequestConnection = connect(this, &BackgroundActivityRegistry::pauseRequested,
                                       m_coordinator, &BackgroundWorkCoordinator::pause);
    m_resumeRequestConnection = connect(this, &BackgroundActivityRegistry::resumeRequested,
                                        m_coordinator, &BackgroundWorkCoordinator::resume);
    m_pauseStateConnection = connect(m_coordinator, &BackgroundWorkCoordinator::pauseStateChanged,
                                     this, &BackgroundActivityRegistry::updatePausedState);
}

QVariantList BackgroundActivityRegistry::activities() const
{
    QVariantList list;
    list.reserve(m_rows.size());
    for (const auto &row : m_rows) {
        QVariantMap entry = row.second;
        entry.insert(QStringLiteral("id"), row.first);
        list.append(entry);
    }
    return list;
}

void BackgroundActivityRegistry::publish(const QString &id, const QVariantMap &state)
{
    QVariantMap published = state;
    if (m_coordinator && m_coordinator->status(id) != Status::Unknown)
        published.insert(QStringLiteral("paused"), m_coordinator->isPaused(id));

    for (auto &row : m_rows) {
        if (row.first == id) {
            row.second = published;
            emit activitiesChanged();
            return;
        }
    }
    m_rows.append(qMakePair(id, published));
    emit activitiesChanged();
}

void BackgroundActivityRegistry::updatePausedState(const QString &id, bool paused)
{
    for (auto &row : m_rows) {
        if (row.first != id)
            continue;
        if (row.second.value(QStringLiteral("paused")).toBool() == paused)
            return;
        row.second.insert(QStringLiteral("paused"), paused);
        emit activitiesChanged();
        return;
    }
}

void BackgroundActivityRegistry::remove(const QString &id)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).first == id) {
            m_rows.removeAt(i);
            emit activitiesChanged();
            return;
        }
    }
}

void BackgroundActivityRegistry::requestPause(const QString &id)
{
    emit pauseRequested(id);
}

void BackgroundActivityRegistry::requestResume(const QString &id)
{
    emit resumeRequested(id);
}

} // namespace work
