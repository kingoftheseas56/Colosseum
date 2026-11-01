#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

class QTcpSocket;

// One-shot loopback receiver for AniList's implicit grant. AniList returns the
// token in the URL fragment, which browsers never send to a server, so the
// flow has two requests on the fixed redirect port:
//   GET  <redirect path>          -> static page that reads location.hash
//   POST <redirect path>/token    -> the page forwards the fragment here
// The server accepts at most one valid token (bound to the one-time `state`
// of this sign-in), then stops listening. It never logs the token and never
// puts it in a URL.
class AniListLoopbackServer final : public QObject
{
    Q_OBJECT

public:
    explicit AniListLoopbackServer(QObject *parent = nullptr);
    ~AniListLoopbackServer() override;

    // `redirectUri` must be http://127.0.0.1:<port>/<path>. `expectedState`
    // is compared in constant time against the state the page posts back.
    bool start(const QUrl &redirectUri,
               const QByteArray &expectedState,
               QString *error = nullptr);
    void stop();
    bool active() const { return m_server.isListening(); }
    QUrl redirectUri() const { return m_redirectUri; }
    QHostAddress listenAddress() const { return m_server.serverAddress(); }
    quint16 listenPort() const { return m_server.serverPort(); }

    // How long one sign-in may stay open before it fails with "timeout".
    void setLifetimeMs(int milliseconds) { m_lifetimeMs = milliseconds; }

signals:
    // Exactly one of these is emitted per start(), then the server is closed.
    void tokenReceived(const QByteArray &accessToken, qint64 expiresInSeconds);
    void signInFailed(const QString &error);

private:
    struct Connection {
        QByteArray buffer;
    };

    void acceptConnections();
    void readSocket(QTcpSocket *socket);
    void handleRequest(QTcpSocket *socket,
                       const QByteArray &method,
                       const QByteArray &path,
                       const QHash<QByteArray, QByteArray> &headers,
                       const QByteArray &body);
    void handleTokenPost(QTcpSocket *socket,
                         const QHash<QByteArray, QByteArray> &headers,
                         const QByteArray &body);
    void respond(QTcpSocket *socket,
                 int status,
                 const QByteArray &reason,
                 const QByteArray &contentType,
                 const QByteArray &body,
                 const QByteArray &extraHeaders = {});
    void reject(QTcpSocket *socket, int status, const QByteArray &reason);
    void forgetSocket(QTcpSocket *socket);
    void settle();

    QTcpServer m_server;
    QTimer m_lifetime;
    QUrl m_redirectUri;
    QByteArray m_expectedState;
    QByteArray m_expectedOrigin;
    QByteArray m_expectedHost;
    QHash<QTcpSocket *, Connection> m_connections;
    int m_lifetimeMs = 5 * 60 * 1000;
    bool m_settled = false;
};
