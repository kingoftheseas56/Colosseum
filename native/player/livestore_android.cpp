#include "livestore.h"

LiveStore::LiveStore(QObject *parent)
    : QObject(parent)
{
}

LiveStore::~LiveStore() = default;

void LiveStore::setLiveChannel(const QVariantMap &channel)
{
    if (m_activeChannel == channel && m_isLive == !channel.isEmpty())
        return;
    m_activeChannel = channel;
    m_isLive = !channel.isEmpty();
    emit changed();
}

void LiveStore::addChannel(const QVariantMap &channel)
{
    const QString id = channel.value(QStringLiteral("id")).toString();
    for (int i = 0; i < m_channels.size(); ++i) {
        if (!id.isEmpty() && m_channels.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            m_channels[i] = channel;
            emit changed();
            return;
        }
    }
    m_channels.append(channel);
    emit changed();
}

void LiveStore::setGroup(const QString &group)
{
    if (m_group == group)
        return;
    m_group = group;
    emit changed();
}

void LiveStore::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit changed();
}

void LiveStore::switchChannel(const QVariantMap &channel)
{
    setLiveChannel(channel);
    emit channelSwitchRequested(channel);
}

QString LiveStore::startRecording(const QVariantMap &request)
{
    Q_UNUSED(request)
    return {};
}

void LiveStore::stopRecording(const QString &id)
{
    Q_UNUSED(id)
}

void LiveStore::revealRecording(const QString &id)
{
    Q_UNUSED(id)
}
