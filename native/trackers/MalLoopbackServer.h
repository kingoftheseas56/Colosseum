#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

class MalLoopbackServer final : public QObject
{
    Q_OBJECT

public:
    explicit MalLoopbackServer(QObject *parent = nullptr);
    ~MalLoopbackServer() override;

    bool start(const QUrl &redirectUri, QString *error = nullptr);
    void stop();
    bool active() const { return m_server.isListening(); }
    QUrl redirectUri() const { return m_redirectUri; }

signals:
    void authorizationResult(const QByteArray &state,
                             const QByteArray &authorizationCode,
                             const QString &error);

private:
    void acceptConnections();
    void readSocket(QTcpSocket *socket);
    void rejectSocket(QTcpSocket *socket, int status, const QByteArray &reason);
    void completeSocket(QTcpSocket *socket, const QByteArray &body);
    void forgetSocket(QTcpSocket *socket);

    QTcpServer m_server;
    QUrl m_redirectUri;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    bool m_settled = false;
};
