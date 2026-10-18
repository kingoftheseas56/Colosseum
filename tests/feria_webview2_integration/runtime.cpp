#include "SessionStore.h"
#include "feria/FeriaAccountStore.h"
#include "feria/FeriaBrowserPolicy.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QHostAddress>
#include <QQmlContext>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QtQuickTest/quicktest.h>
#include <QFileInfo>

// Focused Windows host for the WebView2 side of the shared navigation and
// consented-close layer. It instantiates the real Colosseum owners
// (SessionStore, FeriaAccountStore, FeriaBrowserPolicy, the shared operation
// seam) and the real deck/QML adapters, exactly like the foundation runtime,
// but its spaces select the WebView2 engine through descriptor engineId.
// A standalone raw controller or adapter double does not run here.
class FeriaWebView2Fixture : public QObject {
    Q_OBJECT
public:
    Q_PROPERTY(QString evidenceRoot READ evidenceRoot CONSTANT)
    QString evidenceRoot() const { return qEnvironmentVariable("FERIA_SESSION_EVIDENCE_DIR"); }
    Q_INVOKABLE void switchProfile(const QString &name) {
        const QString root = m_temp.filePath(name);
        m_sessions->setBrowserProfile(root);
        m_account->setStoragePath(root + QStringLiteral("/account.json"));
        m_policy->setStorageRoot(root + QStringLiteral("/browser"));
    }
    // Trusted renderer input through the probe-qualified path: native
    // MoveFocus then CDP Input.dispatchMouseEvent. Test windows only.
    Q_INVOKABLE bool trustedClick(QObject *view, int x, int y) {
        if (!view)
            return false;
        QMetaObject::invokeMethod(view, "dispatchTrustedClick",
                                  Qt::DirectConnection,
                                  Q_ARG(int, x), Q_ARG(int, y));
        return true;
    }
    Q_INVOKABLE bool captureWindow(QQuickItem *item, const QString &name) {
        if (evidenceRoot().isEmpty() || !item || !item->window()
            || name.contains('/') || name.contains('\\')) return false;
        return item->window()->grabWindow().save(QDir(evidenceRoot()).filePath(name));
    }
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName(QStringLiteral("ColosseumTest"));
        QCoreApplication::setApplicationName(QStringLiteral("feria-webview2-close-fixture"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temp.filePath("settings"));
        if (!evidenceRoot().isEmpty()) QDir().mkpath(evidenceRoot());
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket,
                        [this, socket, buffer = QByteArray{}, replied = false]() mutable {
                    buffer += socket->readAll();
                    if (replied || !buffer.contains("\r\n\r\n")) return;
                    const auto headerEnd = buffer.indexOf("\r\n\r\n");
                    int contentLength = 0;
                    for (const auto &line : buffer.left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            contentLength = line.mid(15).trimmed().toInt();
                    if (buffer.size() < headerEnd + 4 + contentLength) return;
                    replied = true;
                    const QByteArray path = buffer.split(' ').value(1);
                    QByteArray body;
                    if (path == "/redirect") {
                        socket->write("HTTP/1.1 302 Found\r\nLocation: /b\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    if (path == "/broken") {
                        socket->disconnectFromHost();
                        return;
                    }
                    if (path == "/slow") {
                        QTimer::singleShot(5000, socket, [socket] {
                            const QByteArray page("<title>Fixture /slow</title>");
                            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(page.size())
                                + "\r\nConnection: close\r\n\r\n" + page);
                            socket->disconnectFromHost();
                        });
                        return;
                    }
                    if (path == "/guard") {
                        body = R"HTML(<!doctype html><title>Fixture /guard</title>
<body style="background:#173044;color:white;margin:0">
<button id="arm" style="position:fixed;left:20px;top:20px;width:220px;height:44px"
 onclick="window.onbeforeunload=function(e){e.preventDefault();e.returnValue='guard';return 'guard'};document.getElementById('draft').value='unsaved work';document.title='guarded'">Protect changes</button>
<input id="draft" style="position:fixed;left:20px;top:80px;width:220px;height:36px" value="">
<a id="next" href="/b" style="position:fixed;left:20px;top:140px;width:220px;height:36px;display:block">Next page</a>
<script>window.fixtureMarker='guard'</script></body>)HTML";
                    } else {
                        body = "<!doctype html><title>Fixture " + QString::fromUtf8(path).toHtmlEscaped().toUtf8()
                            + "</title><body style='background:#173044;color:white'>"
                              "<h1>Retained page " + QString::fromUtf8(path).toHtmlEscaped().toUtf8() + "</h1>"
                              "<a href='/b' style='position:fixed;left:20px;top:70px;width:180px;height:30px'>Next page</a>"
                              "<p>Comet in the WebView2 listening room.</p></body>";
                    }
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        if (!m_server.listen(QHostAddress::LocalHost, 0)) qFatal("Fixture listener failed");
    }
    void qmlEngineAvailable(QQmlEngine *engine) {
        m_sessions = new SessionStore(engine);
        m_account = new FeriaAccountStore(engine);
        m_policy = new FeriaBrowserPolicy(engine);
        // Registers Colosseum.FeriaHost/FeriaHostItem when the WebView2 SDK
        // is available; the deck's WebView2 factory depends on it.
        FeriaBrowserPolicy::registerTypes();
        engine->rootContext()->setContextProperty("Sessions", m_sessions);
        engine->rootContext()->setContextProperty("FeriaAccount", m_account);
        engine->rootContext()->setContextProperty("FeriaBrowserPolicy", m_policy);
        engine->rootContext()->setContextProperty("Fixture", this);
        engine->rootContext()->setContextProperty("FixtureUrl",
            QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
        switchProfile(QStringLiteral("initial"));
    }
private:
    QTemporaryDir m_temp;
    QTcpServer m_server;
    SessionStore *m_sessions = nullptr;
    FeriaAccountStore *m_account = nullptr;
    FeriaBrowserPolicy *m_policy = nullptr;
};
int main(int argc, char **argv) {
    QtWebEngineQuick::initialize();
    qInfo().noquote() << "Runtime versions: Qt" << qVersion();
    FeriaWebView2Fixture setup;
    return quick_test_main_with_setup(argc, argv, "feria_webview2_close", nullptr, &setup);
}
#include "runtime.moc"
