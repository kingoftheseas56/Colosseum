// Lanista bridge harness. Boots a real QQmlApplicationEngine over the
// deterministic scene, constructs LanistaServer on a TEST pipe, then either:
//   (default)   runs the built-in checks through a real QLocalSocket client
//               and exits 0 with sentinel LANISTA_OK / 1 on first failure;
//   --serve     stays alive so lanista.exe / the .ps1 can drive it.
// Follows the house harness pattern: require() + sentinel + exit code.
//
// The server registers its selftest-* fixture commands only because this harness
// sets COLOSSEUM_LANISTA_SELFTEST=1; the daily app never sees them.
#include "devtools/LanistaServer.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>

static void require(bool cond, const QString& msg)
{
    if (!cond) { std::cerr << "FAIL: " << qUtf8Printable(msg) << "\n"; std::exit(1); }
}

// Why the last exchange came back empty/short. Without this a bridge that never
// came up is indistinguishable from a bridge that answered wrong.
static QString g_lastCallError;
static QString why()
{
    return g_lastCallError.isEmpty()
               ? QString()
               : QStringLiteral(" [") + g_lastCallError + QLatin1Char(']');
}

// Writes `chunks` — one per event-loop turn, so the server really does see them
// as separate readyRead deliveries — and collects EVERY byte the server writes
// back until it hangs up or the timeout expires. The framing tests assert on the
// NUMBER of reply lines, so this must never stop at the first one.
//
// Client and server share this process AND this thread, so the obvious
// waitForConnected()/waitForReadyRead() spelling deadlocks: it blocks the very
// event loop the server needs in order to answer. Everything here is signal-driven.
static QByteArray rawExchange(const QString& pipe, const QList<QByteArray>& chunks,
                              int timeoutMs = 5000)
{
    g_lastCallError.clear();
    QLocalSocket sock;
    QEventLoop loop;
    QByteArray got;

    QObject::connect(&sock, &QLocalSocket::readyRead, &loop,
                     [&]() { got += sock.readAll(); });
    QObject::connect(&sock, &QLocalSocket::disconnected, &loop, [&]() {
        got += sock.readAll();
        loop.quit();
    });
    QObject::connect(&sock, &QLocalSocket::errorOccurred, &loop,
                     [&](QLocalSocket::LocalSocketError e) {
                         if (e != QLocalSocket::PeerClosedError)
                             g_lastCallError = sock.errorString();
                         loop.quit();
                     });
    QObject::connect(&sock, &QLocalSocket::connected, &loop, [&]() {
        for (int i = 0; i < chunks.size(); ++i) {
            const QByteArray chunk = chunks.at(i);
            QTimer::singleShot(i * 80, &sock, [&sock, chunk]() {
                sock.write(chunk);
                sock.flush();
            });
        }
    });
    QTimer::singleShot(timeoutMs, &loop, [&]() {
        if (g_lastCallError.isEmpty())
            g_lastCallError = QStringLiteral("timed out after %1 ms").arg(timeoutMs);
        loop.quit();
    });

    sock.connectToServer(pipe);
    loop.exec();
    return got;
}

static int lineCount(const QByteArray& raw) { return int(raw.count('\n')); }

static QJsonObject firstObject(const QByteArray& raw)
{
    const qsizetype nl = raw.indexOf('\n');
    return QJsonDocument::fromJson(nl < 0 ? raw : raw.left(nl)).object();
}

// One command per connection, exactly like a real client.
static QJsonObject call(const QString& pipe, const QJsonObject& req, int timeoutMs = 5000)
{
    const QByteArray raw = rawExchange(
        pipe, {QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n"}, timeoutMs);
    return raw.isEmpty() ? QJsonObject() : firstObject(raw);
}

static int dispatchCount(const QString& pipe)
{
    return call(pipe, {{"cmd", "selftest-dispatches"}, {"seq", 0}})
        .value("dispatches").toInt(-1);
}

// Runs a nested event loop for `ms` so the server can get on with its work.
static void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, [&]() { loop.quit(); });
    loop.exec();
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
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    // Short idle timeout so the always-on listener's hang-up is testable in
    // about a second instead of ten. Every real connection below speaks at once.
    qputenv("COLOSSEUM_LANISTA_IDLE_MS", "1200");
    // The gates must start CLOSED: the denial tests are the point.
    qunsetenv("COLOSSEUM_LANISTA_DRIVE");
    qunsetenv("COLOSSEUM_LANISTA_WRITE");

    QQmlApplicationEngine engine;
#ifdef LANISTA_SCENE_DIR
    const QString scene = QDir::cleanPath(
        QStringLiteral(LANISTA_SCENE_DIR "/lanista_harness_scene.qml"));
#else
    const QString scene = QFileInfo(QStringLiteral(__FILE__)).absolutePath()
                          + QStringLiteral("/lanista_harness_scene.qml");
#endif
    engine.load(QUrl::fromLocalFile(scene));
    require(!engine.rootObjects().isEmpty(),
            QStringLiteral("harness scene loads: ") + scene);

    auto* server = new LanistaServer(&engine, &app);
    require(server->isListening(),
            QStringLiteral("lanista listens on ") + pipe + QStringLiteral(": ")
                + (server->listenError().isEmpty() ? QStringLiteral("(no error reported)")
                                                   : server->listenError()));

    if (serve) {
        std::cout << "LANISTA_SERVING " << pipe.toStdString() << "\n" << std::flush;
        return app.exec();
    }

    int rc = 1;
    QTimer::singleShot(0, &app, [&]() {
        // ── ping: schema + command list ──────────────────────────────────
        QJsonObject pong = call(pipe, {{"cmd", "ping"}, {"seq", 1}});
        require(pong.value("type").toString() == "reply", "ping replies" + why());
        require(pong.value("seq").toInt() == 1, "seq echoed");
        require(pong.value("schema").toString().startsWith("colosseum.dev.v1"),
                "schema is colosseum.dev.v1.x");
        require(pong.value("commands").toArray().contains(QJsonValue("get-state")),
                "command list present");

        // command list is sorted — QHash order is randomised per process
        QStringList names;
        const QJsonArray cmdArray = pong.value("commands").toArray();
        for (const QJsonValue& v : cmdArray) names << v.toString();
        QStringList sorted = names;
        std::sort(sorted.begin(), sorted.end());
        require(names == sorted, "ping's command list is sorted");

        // ping reports the gate state so a client need not know our env names
        const QJsonObject gates = pong.value("gates").toObject();
        require(gates.value("read").toBool(), "ping: read gate open");
        require(!gates.value("drive").toBool(), "ping: drive gate reported closed");
        require(!gates.value("write").toBool(), "ping: write gate reported closed");

        // ── get-state: window geometry ───────────────────────────────────
        QJsonObject st = call(pipe, {{"cmd", "get-state"}, {"seq", 2}});
        require(st.value("type").toString() == "reply", "get-state replies" + why());
        require(st.value("windows").toArray().size() == 1, "one window reported");
        QJsonObject w = st.value("windows").toArray().first().toObject();
        require(w.value("width").toInt() == 800 && w.value("height").toInt() == 600,
                "window geometry is real");

        // runDir is a PATH, not a directory the always-on app litters per launch
        const QString runDir = st.value("runDir").toString();
        require(!runDir.isEmpty(), "get-state reports a runDir");
        require(!QFileInfo::exists(runDir),
                QStringLiteral("runDir is not created until an artifact is written: ") + runDir);

        // ── unknown command errors loudly, never hangs ───────────────────
        QJsonObject bad = call(pipe, {{"cmd", "no-such"}, {"seq", 3}});
        require(bad.value("type").toString() == "error", "unknown cmd is an error" + why());
        require(bad.value("code").toString() == "UNKNOWN_CMD", "with the code");

        // ── malformed input ──────────────────────────────────────────────
        QByteArray raw = rawExchange(pipe, {QByteArray("this is not json\n")});
        require(lineCount(raw) == 1, "BAD_JSON: exactly one reply line" + why());
        require(firstObject(raw).value("code").toString() == "BAD_JSON",
                "BAD_JSON: with the code");

        // ── framing: a single line split across two writes ───────────────
        raw = rawExchange(pipe, {QByteArray("{\"cmd\":\"ping\",\"se"),
                                 QByteArray("q\":7}\n")});
        require(lineCount(raw) == 1, "split line: exactly one reply" + why());
        require(firstObject(raw).value("seq").toInt() == 7, "split line: reassembled");

        // ── C1 REGRESSION: a consumed line is never dispatched twice ─────
        // Trailing bytes arrive in their own readyRead while an ASYNC command
        // still holds the connection open. Before the fix the buffer was never
        // consumed, so that second delivery re-found the same newline and ran
        // the same command again. The duplicate reply is INVISIBLE on the wire
        // (it lands after the first reply has already closed the socket), which
        // is exactly why this asserts on the server's dispatch counter too.
        const int before = dispatchCount(pipe);
        require(before >= 0, "selftest-dispatches is available" + why());
        raw = rawExchange(pipe, {QByteArray("{\"cmd\":\"selftest-slow\",\"seq\":10}\n"),
                                 QByteArray("zzzz")});
        require(lineCount(raw) == 1, "trailing bytes: exactly ONE reply" + why());
        require(firstObject(raw).value("slow").toBool(),
                "trailing bytes: the reply is the command's own");
        const int after = dispatchCount(pipe);
        require(after == before + 2,
                QStringLiteral("C1: the consumed line is never re-dispatched "
                               "(dispatches %1 -> %2, expected +2)").arg(before).arg(after));

        // ── framing: pipelining is refused explicitly, not dropped in silence ──
        raw = rawExchange(pipe, {QByteArray("{\"cmd\":\"ping\",\"seq\":8}\n"
                                            "{\"cmd\":\"ping\",\"seq\":9}\n")});
        require(lineCount(raw) == 1, "pipelined: exactly one reply" + why());
        require(firstObject(raw).value("code").toString() == "EXTRA_INPUT",
                "pipelined: EXTRA_INPUT rather than silence");

        // ── I3: an unbounded line is refused ─────────────────────────────
        raw = rawExchange(pipe, {QByteArray(1100000, 'x')});
        require(lineCount(raw) == 1, "oversize line: exactly one reply" + why());
        require(firstObject(raw).value("code").toString() == "LINE_TOO_LONG",
                "oversize line: LINE_TOO_LONG");

        // ── handlers own their error codes ───────────────────────────────
        QJsonObject failed = call(pipe, {{"cmd", "selftest-fail"}, {"seq", 11}});
        require(failed.value("type").toString() == "error", "selftest-fail errors" + why());
        require(failed.value("code").toString() == "SELFTEST_FAILURE",
                "a handler's own UPPER_SNAKE code reaches the client");

        // ── C2 REGRESSION: the client may vanish mid-command ─────────────
        // selftest-orphan answers ~250ms later through a Replier it captured. We
        // hang up immediately. The token must refuse to touch the dead socket —
        // a raw QLocalSocket* held across those turns is a use-after-free.
        //
        // Do NOT assert this by expecting a crash: writing to the freed socket
        // usually succeeds quietly, so "the harness survived" proves nothing.
        // Ask the server what the token itself decided.
        {
            // NB: connectToServer() can emit connected() synchronously, so this
            // must NOT be driven by quitting a nested loop from that handler —
            // the quit would land before exec() and we would hang around for the
            // timeout instead of hanging up. Fixed short wait, then abort.
            QLocalSocket sock;
            QObject::connect(&sock, &QLocalSocket::connected, &sock, [&]() {
                sock.write("{\"cmd\":\"selftest-orphan\",\"seq\":12}\n");
                sock.flush();
            });
            sock.connectToServer(pipe);
            settle(60);     // long enough for the server to read and dispatch
            sock.abort();   // walk away, ~200ms before the handler answers
        }
        settle(600);        // let the orphaned handler fire into the void
        QJsonObject orphan = call(pipe, {{"cmd", "selftest-orphan-result"}, {"seq", 13}});
        require(orphan.value("checked").toBool(),
                "C2: the orphaned handler did run" + why());
        require(!orphan.value("couldReply").toBool(),
                "C2: a Replier whose client vanished refuses to touch the socket");
        QJsonObject alive = call(pipe, {{"cmd", "ping"}, {"seq", 14}});
        require(alive.value("type").toString() == "reply",
                "server survives a client that vanished mid-command" + why());

        // ── the gates: the whole safety model ────────────────────────────
        QJsonObject drive = call(pipe, {{"cmd", "selftest-drive"}, {"seq", 20}});
        require(drive.value("code").toString() == "DRIVE_DISABLED",
                "drive gate is CLOSED by default" + why());
        qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
        drive = call(pipe, {{"cmd", "selftest-drive"}, {"seq", 21}});
        require(drive.value("drove").toBool(), "drive gate OPENS with the env var" + why());
        qunsetenv("COLOSSEUM_LANISTA_DRIVE");
        drive = call(pipe, {{"cmd", "selftest-drive"}, {"seq", 22}});
        require(drive.value("code").toString() == "DRIVE_DISABLED",
                "drive gate CLOSES again" + why());

        QJsonObject write = call(pipe, {{"cmd", "selftest-write"}, {"seq", 23}});
        require(write.value("code").toString() == "WRITE_DISABLED",
                "write gate is CLOSED by default" + why());
        qputenv("COLOSSEUM_LANISTA_WRITE", "1");
        write = call(pipe, {{"cmd", "selftest-write"}, {"seq", 24}});
        require(write.value("wrote").toBool(), "write gate OPENS with the env var" + why());
        qunsetenv("COLOSSEUM_LANISTA_WRITE");
        write = call(pipe, {{"cmd", "selftest-write"}, {"seq", 25}});
        require(write.value("code").toString() == "WRITE_DISABLED",
                "write gate CLOSES again" + why());

        // a read command is never caught by either gate
        require(call(pipe, {{"cmd", "ping"}, {"seq", 26}}).value("type").toString() == "reply",
                "reads stay open with both gates closed" + why());

        // ── I3: a silent connection is hung up on ────────────────────────
        raw = rawExchange(pipe, {}, 5000);
        require(lineCount(raw) == 1, "idle connection: exactly one reply" + why());
        require(firstObject(raw).value("code").toString() == "IDLE_TIMEOUT",
                "idle connection: IDLE_TIMEOUT");

        std::cout << "LANISTA_OK\n";
        rc = 0;
        app.quit();
    });
    app.exec();
    return rc;
}
