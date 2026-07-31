// Lanista bridge harness. Boots a real QQmlApplicationEngine over the
// deterministic scene, constructs LanistaServer on a TEST pipe, then either:
//   (default)   runs the built-in checks through a real QLocalSocket client
//               and exits 0 with sentinel LANISTA_OK / 1 on first failure;
//   --serve     stays alive so lanista.exe / the .ps1 can drive it.
// Follows the house harness pattern: require() + sentinel + exit code.
#include "devtools/LanistaServer.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QEventLoop>
#include <QLibraryInfo>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QTimer>
#include <cstdlib>
#include <iostream>

static void require(bool cond, const char* msg)
{
    if (!cond) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

// One command per connection, exactly like a real client.
//
// The client and the server share this process AND this thread, so the obvious
// QLocalSocket::waitForConnected()/waitForReadyRead() spelling DEADLOCKS: those
// block the very event loop the server needs to run in order to read the request
// and answer it. Pump a nested QEventLoop instead — both ends make progress and
// the timeout still bounds every call.
static QJsonObject call(const QString& pipe, const QJsonObject& req, int timeoutMs = 5000)
{
    QLocalSocket sock;
    QEventLoop loop;
    QByteArray buf;
    bool done = false;

    auto drain = [&]() {
        buf += sock.readAll();
        if (buf.contains('\n')) { done = true; loop.quit(); }
    };
    QObject::connect(&sock, &QLocalSocket::connected, &loop, [&]() {
        sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
    });
    QObject::connect(&sock, &QLocalSocket::readyRead, &loop, drain);
    QObject::connect(&sock, &QLocalSocket::disconnected, &loop, [&]() {
        drain();
        loop.quit();
    });
    QObject::connect(&sock, &QLocalSocket::errorOccurred, &loop,
                     [&](QLocalSocket::LocalSocketError) { loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, [&]() { loop.quit(); });

    sock.connectToServer(pipe);
    loop.exec();
    if (!done) return {};
    return QJsonDocument::fromJson(buf.left(buf.indexOf('\n'))).object();
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // House trap (see tests/window_shell_gui_harness.cpp): the windeployqt'd
    // platforms/ dir beside the exe carries ONLY qwindows.dll and shadows the Qt
    // install's plugin dir, so "offscreen" cannot load and the process fail-fasts
    // silently. Point the QPA loader back at the real Qt plugins dir. The path is
    // baked in by CMake (LANISTA_QT_PLATFORMS_DIR) rather than read from
    // QLibraryInfo, which resolves relative to the DEPLOYED Qt6Core.dll and so
    // hands back the very directory that is shadowing us. Caller wins if set.
#ifdef LANISTA_QT_PLATFORMS_DIR
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH"))
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", LANISTA_QT_PLATFORMS_DIR);
#endif
    QGuiApplication app(argc, argv);
    const bool serve = app.arguments().contains(QStringLiteral("--serve"));

    // Test pipe — NEVER the default, so a running daily Colosseum is untouched.
    const QString pipe = QStringLiteral("ColosseumLanistaTest");
    qputenv("COLOSSEUM_LANISTA_PIPE", pipe.toUtf8());

    QQmlApplicationEngine engine;
    const QString scene = QFileInfo(QStringLiteral(__FILE__)).absolutePath()
                          + QStringLiteral("/lanista_harness_scene.qml");
    engine.load(QUrl::fromLocalFile(scene));
    require(!engine.rootObjects().isEmpty(), "harness scene loads");

    auto* server = new LanistaServer(&engine, &app);
    Q_UNUSED(server);

    if (serve) {
        std::cout << "LANISTA_SERVING " << pipe.toStdString() << "\n" << std::flush;
        return app.exec();
    }

    int rc = 1;
    QTimer::singleShot(0, &app, [&]() {
        // ── ping: schema + command list ──────────────────────────────────
        QJsonObject pong = call(pipe, {{"cmd", "ping"}, {"seq", 1}});
        require(pong.value("type").toString() == "reply", "ping replies");
        require(pong.value("seq").toInt() == 1, "seq echoed");
        require(pong.value("schema").toString().startsWith("colosseum.dev.v1"),
                "schema is colosseum.dev.v1.x");
        require(pong.value("commands").toArray().contains(QJsonValue("get-state")),
                "command list present");

        // ── get-state: window geometry ───────────────────────────────────
        QJsonObject st = call(pipe, {{"cmd", "get-state"}, {"seq", 2}});
        require(st.value("type").toString() == "reply", "get-state replies");
        require(st.value("windows").toArray().size() == 1, "one window reported");
        QJsonObject w = st.value("windows").toArray().first().toObject();
        require(w.value("width").toInt() == 800 && w.value("height").toInt() == 600,
                "window geometry is real");

        // ── unknown command errors loudly, never hangs ───────────────────
        QJsonObject bad = call(pipe, {{"cmd", "no-such"}, {"seq", 3}});
        require(bad.value("type").toString() == "error", "unknown cmd is an error");
        require(bad.value("code").toString() == "UNKNOWN_CMD", "with the code");

        std::cout << "LANISTA_OK\n";
        rc = 0;
        app.quit();
    });
    app.exec();
    return rc;
}
