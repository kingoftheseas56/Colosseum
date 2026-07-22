// native/work/BackgroundActivityRegistry.cpp
#include "work/BackgroundActivityRegistry.h"

namespace work {

BackgroundActivityRegistry::BackgroundActivityRegistry(QObject *parent)
    : QObject(parent)
{
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
    for (auto &row : m_rows) {
        if (row.first == id) {
            row.second = state;
            emit activitiesChanged();
            return;
        }
    }
    m_rows.append(qMakePair(id, state));
    emit activitiesChanged();
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
