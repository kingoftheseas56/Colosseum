#include "roomstore.h"

#include <QDateTime>
#include <QUuid>

RoomStore::RoomStore(QObject *parent)
    : QObject(parent),
      m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

QString RoomStore::createLocalRoom(const QString &displayName) {
    resetRoom();
    m_active = true;
    m_isHost = true;
    m_started = false;
    m_roomId = QStringLiteral("local-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    m_displayName = cleanedName(displayName);
    m_participants.append(makeParticipant(m_clientId, m_displayName, true, false));
    emit changed();
    return m_roomId;
}

void RoomStore::joinLocalRoom(const QString &roomId, const QString &displayName) {
    resetRoom();
    m_active = true;
    m_isHost = false;
    m_started = false;
    m_roomId = roomId.trimmed().isEmpty() ? QStringLiteral("local-room") : roomId.trimmed();
    m_displayName = cleanedName(displayName);
    m_participants.append(makeParticipant(QStringLiteral("host"), QStringLiteral("Host"), true, true));
    m_participants.append(makeParticipant(m_clientId, m_displayName, false, false));
    emit changed();
}

void RoomStore::markReady(bool ready) {
    if (!m_active)
        return;
    upsertSelf(ready);
    emit changed();
}

void RoomStore::sendChat(const QString &message) {
    if (!m_active)
        return;
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty())
        return;
    QVariantMap row;
    row.insert(QStringLiteral("participantId"), m_clientId);
    row.insert(QStringLiteral("name"), m_displayName);
    row.insert(QStringLiteral("message"), trimmed.left(500));
    row.insert(QStringLiteral("sentAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_chat.append(row);
    while (m_chat.size() > 80)
        m_chat.removeFirst();
    emit changed();
}

void RoomStore::publishState(const QVariantMap &state) {
    if (!m_active)
        return;
    QVariantMap next = state;
    next.insert(QStringLiteral("participantId"), m_clientId);
    next.insert(QStringLiteral("roomId"), m_roomId);
    next.insert(QStringLiteral("publishedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!next.contains(QStringLiteral("position")))
        next.insert(QStringLiteral("position"), 0);
    if (!next.contains(QStringLiteral("playing")))
        next.insert(QStringLiteral("playing"), false);
    m_syncState = next;
    m_started = true;
    emit changed();
    emit syncCommand(m_syncState);
}

void RoomStore::leaveRoom() {
    if (!m_active)
        return;
    resetRoom();
    emit changed();
}

QVariantMap RoomStore::makeParticipant(const QString &participantId,
                                       const QString &name,
                                       bool host,
                                       bool ready) const {
    QVariantMap participant;
    participant.insert(QStringLiteral("participantId"), participantId);
    participant.insert(QStringLiteral("name"), cleanedName(name));
    participant.insert(QStringLiteral("host"), host);
    participant.insert(QStringLiteral("ready"), ready);
    return participant;
}

void RoomStore::upsertSelf(bool ready) {
    for (int i = 0; i < m_participants.size(); ++i) {
        QVariantMap participant = m_participants.at(i).toMap();
        if (participant.value(QStringLiteral("participantId")).toString() == m_clientId) {
            participant.insert(QStringLiteral("ready"), ready);
            m_participants[i] = participant;
            return;
        }
    }
    m_participants.append(makeParticipant(m_clientId, m_displayName, m_isHost, ready));
}

QString RoomStore::cleanedName(const QString &name) const {
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Me") : trimmed.left(40);
}

void RoomStore::resetRoom() {
    m_active = false;
    m_isHost = false;
    m_started = false;
    m_roomId.clear();
    m_displayName.clear();
    m_participants.clear();
    m_chat.clear();
    m_syncState.clear();
}
