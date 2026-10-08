#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHostAddress>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>
#include <QtQuickTest/quicktest.h>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

// A loopback response gate only. No Colosseum owner, private Qt API, source
// pointer inspection, renderer termination, or download implementation.
class SourceBindingFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl CONSTANT)
    Q_PROPERTY(QString downloadRoot READ downloadRoot CONSTANT)
public:
    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    QString downloadRoot() const { return m_directory.filePath("downloads"); }
    Q_INVOKABLE QVariantList requestLog() const { return m_log; }
    Q_INVOKABLE int requestCount(const QString &key) const {
        int count = 0;
        for (const auto &entry : m_log)
            if (entry.toMap().value("gate").toString() == key) ++count;
        return count;
    }
    Q_INVOKABLE void reset() {
        for (const auto &pending : m_pending)
            if (pending.socket) pending.socket->abort();
        m_pending.clear();
        m_log.clear();
    }
    Q_INVOKABLE int release(const QString &key) {
        int released = 0;
        for (auto &pending : m_pending) {
            if (pending.key != key || !pending.socket
                || pending.socket->state() != QAbstractSocket::ConnectedState)
                continue;
            pending.socket->write(pending.response);
            pending.socket->disconnectFromHost();
            auto entry = m_log[pending.logIndex].toMap();
            entry.insert("bytesSent", pending.bodySize);
            m_log[pending.logIndex] = entry;
            pending.socket.clear();
            ++released;
        }
        return released;
    }
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName("QtSourceBindingReproducer");
        QCoreApplication::setApplicationName("source-binding-reproducer");
        if (!m_directory.isValid() || !QDir().mkpath(downloadRoot()))
            qFatal("Cannot prepare temporary fixture directory");
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket,
                    [this, socket, buffer = QByteArray{}, replied = false]() mutable {
                        buffer += socket->readAll();
                        if (replied || !buffer.contains("\r\n\r\n")) return;
                        replied = true;
                        serve(socket, buffer);
                    });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        if (!m_server.listen(QHostAddress::LocalHost, 0))
            qFatal("Cannot start loopback fixture listener");
    }
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("Fixture", this);
    }
private:
    struct Pending {
        QPointer<QTcpSocket> socket;
        QString key;
        QByteArray response;
        qsizetype logIndex;
        qsizetype bodySize;
    };
    void serve(QTcpSocket *socket, const QByteArray &request) {
        const auto firstLine = request.left(request.indexOf("\r\n")).split(' ');
        const QUrl url = QUrl::fromEncoded(firstLine.value(1));
        const QString path = url.path();
        const QString gate = QUrlQuery(url).queryItemValue("gate");
        QString referer;
        for (const auto &line : request.left(request.indexOf("\r\n\r\n")).split('\n'))
            if (line.toLower().startsWith("referer:"))
                referer = QString::fromLatin1(line.mid(8).trimmed());
        const qsizetype logIndex = m_log.size();
        m_log.append(QVariantMap{{"method", QString::fromLatin1(firstLine.value(0))},
            {"path", path}, {"url", url.toString()}, {"gate", gate},
            {"referer", referer}, {"bytesSent", 0}});
        const bool download = path == "/gated-download";
        const QByteArray body = download ? QByteArray(1024 * 1024, 'x')
            : (QStringLiteral("<!doctype html><title>Fixture ") + path.toHtmlEscaped()
                + QStringLiteral("</title><body>Local source-binding fixture</body>")).toUtf8();
        QByteArray response = "HTTP/1.1 200 OK\r\nCache-Control: no-store\r\nConnection: close\r\n";
        response += download
            ? "Content-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=source-proof.bin\r\n"
            : "Content-Type: text/html; charset=utf-8\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        if (download) {
            m_pending.append(Pending{socket, gate, response, logIndex, body.size()});
            return; // No attachment headers/body leave until release(gate).
        }
        socket->write(response);
        socket->disconnectFromHost();
    }
    QTemporaryDir m_directory;
    QTcpServer m_server;
    QVariantList m_log;
    QList<Pending> m_pending;
};

int main(int argc, char **argv) {
    QtWebEngineQuick::initialize();
    qInfo().noquote() << "Runtime versions: Qt" << qVersion()
        << "WebEngine" << qWebEngineVersion()
        << "Chromium" << qWebEngineChromiumVersion()
        << "security-patch" << qWebEngineChromiumSecurityPatchVersion();
    SourceBindingFixture fixture;
    return quick_test_main_with_setup(argc, argv, "source_binding_reproducer",
                                     REPRODUCER_SOURCE_DIR, &fixture);
}
#include "main.moc"
