#include "SessionStore.h"
#include "feria/FeriaAccountStore.h"
#include "feria/FeriaBrowserPolicy.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QHostAddress>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <cmath>

class FeriaRuntimeFixture : public QObject {
    Q_OBJECT
public:
    Q_PROPERTY(QString evidenceRoot READ evidenceRoot CONSTANT)
    QString evidenceRoot() const { return qEnvironmentVariable("FERIA_SESSION_EVIDENCE_DIR"); }
    Q_INVOKABLE bool captureWindow(QQuickItem *item, const QString &name) {
        if (evidenceRoot().isEmpty() || !item || !item->window()
            || name.contains('/') || name.contains('\\')) return false;
        return item->window()->grabWindow().save(QDir(evidenceRoot()).filePath(name));
    }
    Q_INVOKABLE void switchProfile(const QString &name) {
        const QString root = m_temp.filePath(name);
        m_sessions->setBrowserProfile(root);
        m_account->setStoragePath(root + QStringLiteral("/account.json"));
        m_policy->setStorageRoot(root + QStringLiteral("/browser"));
    }
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName(QStringLiteral("ColosseumTest"));
        QCoreApplication::setApplicationName(QStringLiteral("feria-component-fixture"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temp.filePath("settings"));
        if (!evidenceRoot().isEmpty()) QDir().mkpath(evidenceRoot());
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket,
                        [socket, buffer = QByteArray{}, replied = false]() mutable {
                    buffer += socket->readAll();
                    if (replied || !buffer.contains("\r\n\r\n")) return;
                    replied = true;
                    const QByteArray path = buffer.split(' ').value(1);
                    QByteArray body;
                    QByteArray mime("text/html");
                    if (path == "/tone.wav") {
                        mime = "audio/wav";
                        QDataStream out(&body, QIODevice::WriteOnly);
                        out.setByteOrder(QDataStream::LittleEndian);
                        constexpr int frames = 8000 * 4;
                        out.writeRawData("RIFF", 4); out << quint32(36 + frames * 2);
                        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
                        out << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
                        out.writeRawData("data", 4); out << quint32(frames * 2);
                        for (int i = 0; i < frames; ++i)
                            out << qint16(1200 * std::sin(6.283185307179586 * 440 * i / 8000));
                    } else {
                        body = "<!doctype html><title>Fixture " + QString::fromUtf8(path).toHtmlEscaped().toUtf8() + "</title>"
                            "<body style='background:#173044;color:white'><h1>Retained page " + QString::fromUtf8(path).toHtmlEscaped().toUtf8()
                            + "</h1><a href='/b'>Next page</a><audio id='audio' loop controls src='/tone.wav'></audio>"
                              "<p>Comet in the reading room. Comet in the listening room. Comet in Feria.</p>"
                              "<p>Search this local fixture with the native browser Find controls.</p>"
                              "<div style='height:1400px'></div><p>Page bottom fixture.</p>"
                              "<script>window.sessionMarker=0</script></body>";
                    }
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + mime + "\r\nContent-Length: "
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
    FeriaRuntimeFixture setup;
    return quick_test_main_with_setup(argc, argv, "feria_retained_sessions", nullptr, &setup);
}
#include "feria_session_runtime_test.moc"
