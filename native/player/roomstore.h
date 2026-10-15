#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class RoomStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool isHost READ isHost NOTIFY changed)
    Q_PROPERTY(bool started READ started NOTIFY changed)
    Q_PROPERTY(QString roomId READ roomId NOTIFY changed)
    Q_PROPERTY(QString clientId READ clientId NOTIFY changed)
    Q_PROPERTY(QVariantList participants READ participants NOTIFY changed)
    Q_PROPERTY(QVariantList chat READ chat NOTIFY changed)
    Q_PROPERTY(QVariantMap syncState READ syncState NOTIFY changed)

public:
    explicit RoomStore(QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool isHost() const { return m_isHost; }
    bool started() const { return m_started; }
    QString roomId() const { return m_roomId; }
    QString clientId() const { return m_clientId; }
    QVariantList participants() const { return m_participants; }
    QVariantList chat() const { return m_chat; }
    QVariantMap syncState() const { return m_syncState; }

    Q_INVOKABLE QString createLocalRoom(const QString &displayName);
    Q_INVOKABLE void joinLocalRoom(const QString &roomId, const QString &displayName);
    Q_INVOKABLE void markReady(bool ready);
    Q_INVOKABLE void sendChat(const QString &message);
    Q_INVOKABLE void publishState(const QVariantMap &state);
    Q_INVOKABLE void leaveRoom();

signals:
    void changed();
    void syncCommand(const QVariantMap &state);

private:
    QVariantMap makeParticipant(const QString &participantId,
                                const QString &name,
                                bool host,
                                bool ready) const;
    void upsertSelf(bool ready);
    QString cleanedName(const QString &name) const;
    void resetRoom();

    bool m_active = false;
    bool m_isHost = false;
    bool m_started = false;
    QString m_roomId;
    QString m_clientId;
    QString m_displayName;
    QVariantList m_participants;
    QVariantList m_chat;
    QVariantMap m_syncState;
};
