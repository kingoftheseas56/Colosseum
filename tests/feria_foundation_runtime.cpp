#include "SessionStore.h"
#include "feria/FeriaAccountStore.h"
#include "feria/FeriaBrowserPolicy.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDataStream>
#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QHostAddress>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QDebug>
#include <QScopeGuard>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtQuickTest/quicktest.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <cmath>
#ifdef Q_OS_LINUX
#include <sys/types.h>
#include <signal.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

class FeriaRuntimeFixture : public QObject {
    Q_OBJECT
public:
    Q_PROPERTY(QString evidenceRoot READ evidenceRoot CONSTANT)
    Q_PROPERTY(QString downloadRoot READ downloadRoot CONSTANT)
    QString evidenceRoot() const { return qEnvironmentVariable("FERIA_SESSION_EVIDENCE_DIR"); }
    QString downloadRoot() const { return m_temp.filePath("downloads"); }
    Q_INVOKABLE QByteArray downloadedContent(const QString &path) const {
        if (!QDir::cleanPath(path).startsWith(downloadRoot() + '/')) return {};
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    // Isolated renderer lifecycle probe only: never a process-name/broad kill.
    // The one registered test view supplies its public renderer PID, and its
    // ancestry must resolve to this exact fixture process before termination.
    Q_INVOKABLE bool terminateOnlyRenderer(QObject *view) const {
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
        if (!view || !m_policy || m_policy->runtimeViewCount() != 1) return false;
        bool webEngineView = false;
        for (auto *meta = view->metaObject(); meta; meta = meta->superClass())
            if (QByteArray(meta->className()) == "QQuickWebEngineView") webEngineView = true;
        if (!webEngineView) return false;
        const qint64 pid = view->property("renderProcessPid").toLongLong();
        const qint64 self = QCoreApplication::applicationPid();
        if (pid <= 1 || pid == self) return false;
        qint64 cursor = pid;
        bool owned = false;
#ifdef Q_OS_WIN
        // Pin the object before taking ancestry evidence, preventing PID reuse
        // between the snapshot and termination from targeting another process.
        const HANDLE renderer = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
        if (!renderer) return false;
        const auto rendererGuard = qScopeGuard([renderer] { CloseHandle(renderer); });
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        QHash<qint64, qint64> parents;
        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        if (Process32FirstW(snapshot, &process)) {
            do { parents.insert(process.th32ProcessID, process.th32ParentProcessID); }
            while (Process32NextW(snapshot, &process));
        }
        CloseHandle(snapshot);
        for (int depth = 0; depth < 32 && cursor > 1; ++depth) {
            if (cursor == self) { owned = true; break; }
            const qint64 parent = parents.value(cursor);
            if (parent <= 0 || parent == cursor) return false;
            cursor = parent;
        }
        if (!owned || view->property("renderProcessPid").toLongLong() != pid) return false;
        // Also verify creation time against the fixture. No process name or
        // broad kill is used; the retained handle identifies the exact object.
        FILETIME rendererCreated{}, selfCreated{}, exitTime{}, kernelTime{}, userTime{};
        const bool valid = GetProcessId(renderer) == DWORD(pid)
            && GetProcessTimes(renderer, &rendererCreated, &exitTime, &kernelTime, &userTime)
            && GetProcessTimes(GetCurrentProcess(), &selfCreated, &exitTime, &kernelTime, &userTime)
            && CompareFileTime(&rendererCreated, &selfCreated) >= 0
            && view->property("renderProcessPid").toLongLong() == pid;
        const bool terminated = valid && TerminateProcess(renderer, 9);
        if (terminated) qInfo() << "Fixture terminated verified owned renderer" << pid;
        return terminated;
#else
        for (int depth = 0; depth < 32 && cursor > 1; ++depth) {
            if (cursor == self) { owned = true; break; }
            QFile status(QStringLiteral("/proc/%1/status").arg(cursor));
            if (!status.open(QIODevice::ReadOnly)) return false;
            qint64 parent = 0;
            for (const auto &line : status.readAll().split('\n'))
                if (line.startsWith("PPid:")) parent = line.mid(5).trimmed().toLongLong();
            if (parent <= 0 || parent == cursor) return false;
            cursor = parent;
        }
        if (!owned || view->property("renderProcessPid").toLongLong() != pid) return false;
        qInfo() << "Fixture SIGTERM verified owned renderer" << pid;
        return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
#endif
#else
        Q_UNUSED(view);
        return false;
#endif
    }
    Q_INVOKABLE bool sameScreen(QObject *first, QObject *second) const {
        auto *origin = qobject_cast<QWindow *>(first);
        auto *host = qobject_cast<QWindow *>(second);
        return origin && host && origin->screen() == host->screen();
    }
    Q_INVOKABLE bool hostFrameWithinAvailableScreen(QObject *object) const {
        auto *window = qobject_cast<QWindow *>(object);
        return window && window->isVisible() && window->screen()
            && window->screen()->availableGeometry().contains(window->frameGeometry());
    }
    Q_INVOKABLE bool focusWindowMatches(QObject *object) const {
        return qobject_cast<QWindow *>(object) == QGuiApplication::focusWindow();
    }
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
        QDir().mkpath(downloadRoot());
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
                    const QByteArray requestBody = buffer.mid(headerEnd + 4, contentLength);
                    const QByteArray path = buffer.split(' ').value(1);
                    QByteArray body;
                    QByteArray mime("text/html");
                    if (path.startsWith("/reopen-redirect") && ++m_routeHits[path] > 1) {
                        socket->write("HTTP/1.1 302 Found\r\nLocation: /hosting\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    if (path == "/broken") {
                        socket->disconnectFromHost();
                        return;
                    }
                    if (path == "/reopen-slow" && ++m_routeHits[path] > 1) {
                        QTimer::singleShot(5000, socket, [socket] {
                            const QByteArray page("<title>Fixture /reopen-slow</title>");
                            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(page.size())
                                + "\r\nConnection: close\r\n\r\n" + page);
                            socket->disconnectFromHost();
                        });
                        return;
                    }
                    if (path == "/slow") {
                        QTimer::singleShot(5000, socket, [socket] {
                            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 17\r\nConnection: close\r\n\r\n<title>ok</title>");
                            socket->disconnectFromHost();
                        });
                        return;
                    }
                    if (path == "/hosting") {
                        body = R"HTML(<!doctype html><title>Fixture /hosting</title>
<body style="background:#173044;color:white"><input id="edit" style="position:fixed;left:20px;top:20px;width:230px;height:40px">
<button id="guard" style="position:fixed;left:20px;top:80px;width:230px;height:40px" onclick="window.onbeforeunload=function(e){document.title='guard-fired';e.preventDefault();e.returnValue='guard';return 'guard'}">Protect unsaved changes</button>
<form method="post" action="/post" target="hosting-dialog" rel="opener"><input type="hidden" name="marker" value="original-post-body"><button id="dialog" style="position:fixed;left:20px;top:140px;width:230px;height:40px" onclick="window.open('', 'hosting-dialog', 'width=420,height=300,left=-9000,top=-9000')">POST dialog</button></form>
<audio id="audio" loop src="/tone.wav"></audio><div style="height:2400px"></div><script>window.sessionMarker=0</script></body>)HTML";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                        return;
                    }
                    if (path == "/post") {
                        body = "<title>POST result</title><script>window.postBody="
                            + QJsonDocument(QJsonArray{QString::fromUtf8(requestBody)}).toJson(QJsonDocument::Compact)
                            + "[0];window.openerMarker=opener?opener.sessionMarker:null;</script>";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                        return;
                    }
                    if (path == "/download") {
                        body = "Colosseum browser download fixture\n";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                            "Content-Disposition: attachment; filename=browser-fixture.txt\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                        QTimer::singleShot(1500, socket, [socket, body] {
                            socket->write(body);
                            socket->disconnectFromHost();
                        });
                        return;
                    }
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
                            + "</h1><a href='/b' style='position:fixed;left:20px;top:70px;width:180px;height:30px'>Next page</a><audio id='audio' style='position:fixed;left:220px;top:110px;width:300px;height:54px' loop controls src='/tone.wav'></audio>"
                              "<p>Comet in the reading room. Comet in the listening room. Comet in Feria.</p>"
                              "<p>Search this local fixture with the native browser Find controls.</p>"
                              "<button style='position:fixed;left:20px;top:200px;width:180px;height:40px' "
                              "onclick=\"window.open('/popup','_blank')\">Open popup tab</button>"
                              "<a href='/download' style='position:fixed;left:220px;top:200px;width:180px;height:40px'>Download fixture</a>"
                              "<div style='height:1400px'></div><p>Page bottom fixture.</p>"
                              "<form method='post' action='/post' target='_blank' rel='opener' "
                              "style='position:fixed;left:20px;top:260px'>"
                              "<input type='hidden' name='marker' value='original-post-body'><button style='width:180px;height:40px'>Post target</button></form>"
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
    QHash<QByteArray, int> m_routeHits;
    SessionStore *m_sessions = nullptr;
    FeriaAccountStore *m_account = nullptr;
    FeriaBrowserPolicy *m_policy = nullptr;
};
int main(int argc, char **argv) {
    QtWebEngineQuick::initialize();
    qInfo().noquote() << "Runtime versions: Qt" << qVersion()
        << "WebEngine" << qWebEngineVersion()
        << "Chromium" << qWebEngineChromiumVersion()
        << "security-patch" << qWebEngineChromiumSecurityPatchVersion();
    FeriaRuntimeFixture setup;
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--preview") {
        QGuiApplication app(argc, argv);
        setup.applicationAvailable();
        QQmlApplicationEngine engine;
        setup.qmlEngineAvailable(&engine);
        engine.load(QUrl::fromLocalFile(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath()));
        if (engine.rootObjects().isEmpty()) return 2;
        // Optional bounded visual evidence, using this window's actual render.
        const QString capture = qEnvironmentVariable("FERIA_PREVIEW_CAPTURE");
        if (!capture.isEmpty()) {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window) return 3;
            auto *poll = new QTimer(window);
            poll->setInterval(100);
            QObject::connect(poll, &QTimer::timeout, window, [window, poll, capture] {
                if (!window->property("previewReady").toBool()) return;
                poll->stop();
                QTimer::singleShot(200, window, [window, capture] {
                    QCoreApplication::exit(window->grabWindow().save(capture) ? 0 : 4);
                });
            });
            poll->start();
            QTimer::singleShot(20000, &app, [] { QCoreApplication::exit(5); });
        }
        return app.exec();
    }
    return quick_test_main_with_setup(argc, argv, "feria_foundation", nullptr, &setup);
}
#include "feria_foundation_runtime.moc"
