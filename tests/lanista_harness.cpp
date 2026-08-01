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

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSet>
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
    // Headless by default. Caller wins, and that is load-bearing rather than
    // tidy: offscreen loads the SOFTWARE scene graph backend, so every grab the
    // default run proves is a software-rendered one. QT_QPA_PLATFORM=windows
    // re-runs the same checks through the RHI the real app uses.
    //
    // ⚠ That RHI run needs QSG_NO_VSYNC=1 (measured 2026-08-01, cost an hour):
    //   QT_QPA_PLATFORM=windows QSG_NO_VSYNC=1 lanista_harness.exe
    // Without it every ITEM grab comes back GRAB_TIMEOUT. Not a bridge defect —
    // this scene is inert and its window is unfocused, so the D3D11 present path
    // stalls and the window renders exactly ONE frame for its whole life;
    // grabToImage needs a frame and never gets one. (Same family as A4's
    // "timers stop firing, QSG_NO_VSYNC=1 fixes it".) The real app renders
    // continuously, and item grabs were proven against the SHIPPED binary over
    // this very scene — colosseum.exe takes a QML path as argv[1] — with no
    // vsync knob at all. Window grabs are immune either way: grabWindow()
    // renders on demand rather than waiting for the loop.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
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
    // Offscreen carries no font database of its own, so every grab comes back
    // with □□□ where the text should be (house trap, and Task 8's goldens ride
    // on it). Point it at the system fonts. Caller wins if set.
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");

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

    // A PNG is in DEVICE pixels; the scene (and get-state) speaks LOGICAL ones.
    // On this machine that is a 1.5x difference under the real platform and 1x
    // offscreen, so the grab checks below scale the scene's declared sizes by
    // the live ratio instead of hard-coding either one.
    auto* rootWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    require(rootWindow != nullptr, QStringLiteral("harness root is a Window"));
    const qreal dpr = rootWindow->devicePixelRatio();

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

        // ── THE COMBINED REPLY: state + pixels of the same instant ───────
        // The scene is 800x600 and longList is 300x400 (lanista_harness_scene.qml),
        // so the PNG's own dimensions say WHICH grab actually ran: a window path
        // that quietly answered an item request, or the reverse, cannot pass.
        QJsonObject g = call(pipe, {{"cmd", "get-state"}, {"seq", 30},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "window"}}}}}},
            8000);
        require(g.value("type").toString() == "reply", "grab-carrying reply arrives" + why());
        const QString gp = g.value("grabPath").toString();
        require(!gp.isEmpty(), "reply carries grabPath");
        require(QFileInfo::exists(gp), "the PNG exists on disk: " + gp);
        require(QFileInfo(gp).size() > 1000, "the PNG is not empty");
        QImage windowPng(gp);
        require(windowPng.width() == qRound(800 * dpr)
                    && windowPng.height() == qRound(600 * dpr),
                QStringLiteral("the window PNG is the window (800x600 @%1), got %2x%3")
                    .arg(dpr).arg(windowPng.width()).arg(windowPng.height()));
        require(QDateTime::fromString(g.value("grabbedAt").toString(),
                                      Qt::ISODateWithMs).isValid(),
                "grabbedAt is a real ISO timestamp with ms: "
                    + g.value("grabbedAt").toString());
        // The GRAB DECORATES the reply; it does not replace it. State and
        // pixels arrive together or the whole design is pointless.
        require(g.value("windows").toArray().size() == 1,
                "the command's own body survives alongside the pixels");
        // First artifact written -> the lazy run dir exists NOW (it was proven
        // absent above, so nothing but this grab can have created it).
        require(QFileInfo::exists(runDir),
                "ensureRunDir() created the run dir on the first artifact: " + runDir);
        require(gp.startsWith(runDir + QStringLiteral("/")),
                "the PNG landed inside the run dir get-state announced");

        // Item grab: ASYNC — the server answers from grabToImage's callback.
        QJsonObject ig = call(pipe, {{"cmd", "get-state"}, {"seq", 31},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "longList"}}}}}},
            8000);
        require(ig.value("type").toString() == "reply", "item grab replies" + why());
        const QString ip = ig.value("grabPath").toString();
        require(QFileInfo::exists(ip), "item grab lands on disk: " + ip);
        QImage itemPng(ip);
        require(itemPng.width() == qRound(300 * dpr)
                    && itemPng.height() == qRound(400 * dpr),
                QStringLiteral("the item PNG is the ITEM (300x400 @%1), got %2x%3")
                    .arg(dpr).arg(itemPng.width()).arg(itemPng.height()));

        // ── I3: a DELEGATE item resolves via the VISUAL-tree walk ────────
        // longList's Repeater builds row0, row1, … These delegate items live
        // ONLY in the visual tree (childItems); the QObject-tree findChild()
        // the bridge shipped with could not see them, so every delegate-built
        // objectName (all 110 in production) returned GRAB_TARGET_NOT_FOUND.
        // The row delegate is 300x50 (lanista_harness_scene.qml), so reading the
        // saved PNG's own dimensions off disk with QImage(path) — exactly as the
        // seq 31 item grab does — proves the grab captured the DELEGATE and not
        // just "some item". (The "read back undefined" caution is about the
        // QML-side result.image.width property, NOT the PNG-on-disk dimensions
        // used here.)
        QJsonObject dg = call(pipe, {{"cmd", "get-state"}, {"seq", 36},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "row0"}}}}}},
            8000);
        require(dg.value("type").toString() == "reply",
                "delegate item row0 resolves and grabs (visual-tree walk)" + why());
        const QString dp = dg.value("grabPath").toString();
        require(QFileInfo::exists(dp) && QFileInfo(dp).size() > 0,
                "delegate item row0 lands a non-empty PNG on disk: " + dp);
        QImage rowPng(dp);
        require(rowPng.width() == qRound(300 * dpr)
                    && rowPng.height() == qRound(50 * dpr),
                QStringLiteral("the delegate PNG is the ROW (300x50 @%1), got %2x%3")
                    .arg(dpr).arg(rowPng.width()).arg(rowPng.height()));

        // A grab rides ANY command, not just get-state.
        QJsonObject pg = call(pipe, {{"cmd", "ping"}, {"seq", 32},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "counterButton"}}}}}},
            8000);
        require(pg.value("schema").toString().startsWith("colosseum.dev.v1"),
                "grab on ping: ping's own reply is intact" + why());
        require(QFileInfo::exists(pg.value("grabPath").toString()),
                "grab on ping: pixels too");

        // A bad target is an error, not a hang.
        QJsonObject ng = call(pipe, {{"cmd", "get-state"}, {"seq", 33},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "no-such"}}}}}},
            8000);
        require(ng.value("type").toString() == "error"
                && ng.value("code").toString() == "GRAB_TARGET_NOT_FOUND",
                "unknown grab target errors loudly" + why());

        // An EMPTY target must be refused, never resolved: findChild("") would
        // otherwise match the first unnamed item and photograph a stranger.
        QJsonObject eg = call(pipe, {{"cmd", "get-state"}, {"seq", 34},
            {"payload", QJsonObject{{"grab", QJsonObject{}}}}}, 8000);
        require(eg.value("code").toString() == "GRAB_TARGET_NOT_FOUND",
                "an empty grab target is refused, not resolved" + why());
        require(eg.value("grabPath").toString().isEmpty(), "and photographs nothing");

        // A command that FAILS keeps its own error code — the grab hook must
        // not swallow it, and a refusal is never photographed.
        QJsonObject fg = call(pipe, {{"cmd", "selftest-fail"}, {"seq", 35},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "window"}}}}}},
            8000);
        require(fg.value("code").toString() == "SELFTEST_FAILURE",
                "a failing command still answers with ITS code, grab or not" + why());
        require(fg.value("grabPath").toString().isEmpty(),
                "a refused command carries no pixels");

        // ── I1: a grab whose client vanished writes NO orphan PNG ─────────
        // selftest-slow holds the token ~250ms, then answers through the grab
        // hook. We hang up first. attachGrab must early-out on !canReply(): no
        // framebuffer capture, no orphan file. Same discipline as C2 above,
        // but proving the grab path's OWN artifact is not written. runDir
        // already exists (seq 30's grab created it), so an empty match here
        // means the guard truly suppressed the write.
        {
            QLocalSocket sock;
            QObject::connect(&sock, &QLocalSocket::connected, &sock, [&]() {
                sock.write("{\"cmd\":\"selftest-slow\",\"seq\":40,"
                           "\"payload\":{\"grab\":{\"target\":\"window\"}}}\n");
                sock.flush();
            });
            sock.connectToServer(pipe);
            settle(60);     // server reads, dispatches, arms the 250ms timer
            sock.abort();   // walk away before the timer answers
        }
        settle(400);        // let the slow timer fire the grab hook into the void
        require(QDir(runDir).entryList({QStringLiteral("seq40-*.png")}, QDir::Files).isEmpty(),
                QStringLiteral("I1: a departed client's grab writes no orphan PNG in ") + runDir);

        // ── I2: the grab deadline fires as GRAB_TIMEOUT ──────────────────
        // An item grab is ASYNC by design: grabToImage() answers from `ready`
        // on a LATER event-loop turn, after a full scene-graph render + readback
        // + queued signal. That path measures 13-60ms in this offscreen harness
        // (min 13ms over many samples) and only grows slower under load, while
        // the per-grab timeoutMs override arms a fixed 1ms deadline the instant
        // the grab is requested. The deadline therefore wins by a >10x margin
        // that widens (never narrows) as the machine slows — so a real item
        // grab deterministically returns GRAB_TIMEOUT here. Negative control:
        // deleting attachGrab's singleShot arm lets the grab complete (~13ms+)
        // and the reply lands as `reply`, turning this red. timeoutMs is the
        // seam that makes this fast (1ms vs the 4s default) without shortening
        // the budget for the success grabs above.
        QJsonObject tg = call(pipe, {{"cmd", "get-state"}, {"seq", 41},
            {"payload", QJsonObject{{"grab", QJsonObject{
                {"target", "counterButton"}, {"timeoutMs", 1}}}}}},
            5000);
        require(tg.value("type").toString() == "error"
                    && tg.value("code").toString() == "GRAB_TIMEOUT",
                QStringLiteral("I2: an item grab's deadline fires as GRAB_TIMEOUT, got type=")
                    + tg.value("type").toString() + QStringLiteral(" code=")
                    + tg.value("code").toString() + why());

        // ── Task 3: qml-get — any named item's live properties by name ────
        QJsonObject qg = call(pipe, {{"cmd", "qml-get"}, {"seq", 7},
            {"payload", QJsonObject{{"object", "counterLabel"},
                                    {"props", QJsonArray{"text", "visible"}}}}});
        require(qg.value("props").toObject().value("text").toString()
                    == QStringLiteral("clicks: 0"),
                "qml-get reads a live property" + why());

        // ── Task 3: ui-query — geometry in SCENE units (the clipped killer) ──
        QJsonObject uq = call(pipe, {{"cmd", "ui-query"}, {"seq", 8},
            {"payload", QJsonObject{{"object", "clippedBox"}}}});
        QJsonObject r = uq.value("rect").toObject();
        require(r.value("x").toDouble() + r.value("width").toDouble() > 800.0,
                "ui-query exposes the overflow past the window edge" + why());
        require(uq.value("clippedByWindow").toBool() == true,
                "and names it: clippedByWindow=true" + why());

        // ── Task 3: ui-query reports SCENE coords, not local (nested delegate) ──
        // row0 is a delegate INSIDE longList (a Flickable at scene x=400), so its
        // LOCAL x is 0 but its SCENE x is ~400. This pins mapRectToScene as the
        // headline feature: a regression to item->x() reports 0 and this goes red
        // (clippedBox alone cannot catch it — there scene x == local x).
        QJsonObject uqRow = call(pipe, {{"cmd", "ui-query"}, {"seq", 52},
            {"payload", QJsonObject{{"object", "row0"}}}});
        const double rowX = uqRow.value("rect").toObject().value("x").toDouble();
        require(rowX > 399.0 && rowX < 401.0,
                QStringLiteral("ui-query reports row0's SCENE x (~400), not local 0, got ")
                    + QString::number(rowX) + why());

        // ── Task 3: dump-ui — the named-object tree, checked STRUCTURALLY ─
        // Not a substring smoke check: assert a non-empty tree, then locate the
        // counterButton ENTRY in items[] and assert its fields, so a stray
        // "counterButton" anywhere in the JSON cannot fake a pass.
        QJsonObject du = call(pipe, {{"cmd", "dump-ui"}, {"seq", 9}});
        require(du.value("count").toInt() > 0, "dump-ui reports a non-empty tree" + why());
        const QJsonArray dumpItems = du.value("items").toArray();
        require(dumpItems.size() == du.value("count").toInt(),
                "dump-ui count matches items[] length");
        QJsonObject btnEntry;
        for (const QJsonValue& v : dumpItems) {
            const QJsonObject o = v.toObject();
            if (o.value("objectName").toString() == QStringLiteral("counterButton")) {
                btnEntry = o;
                break;
            }
        }
        require(!btnEntry.isEmpty(),
                "dump-ui has a counterButton entry in items[]" + why());
        require(!btnEntry.value("class").toString().isEmpty(),
                "dump-ui counterButton entry carries a non-empty class");
        require(btnEntry.contains("depth"),
                "dump-ui counterButton entry carries a depth");

        // ── Task 3: negative path — a missing object is NO_SUCH_ITEM ──────
        QJsonObject qgN = call(pipe, {{"cmd", "qml-get"}, {"seq", 50},
            {"payload", QJsonObject{{"object", "no-such-item"},
                                    {"props", QJsonArray{"text"}}}}});
        require(qgN.value("type").toString() == "error"
                    && qgN.value("code").toString() == "NO_SUCH_ITEM",
                "qml-get on a missing object is NO_SUCH_ITEM" + why());
        QJsonObject uqN = call(pipe, {{"cmd", "ui-query"}, {"seq", 51},
            {"payload", QJsonObject{{"object", "no-such-item"}}}});
        require(uqN.value("type").toString() == "error"
                    && uqN.value("code").toString() == "NO_SUCH_ITEM",
                "ui-query on a missing object is NO_SUCH_ITEM" + why());

        // ── Task 4: ui-snapshot — every actionable element, each with a handle ─
        // Playwright's model, QML-native: ONE call returns everything an agent
        // could act on, each carrying an OPAQUE session handle valid only within
        // the snapshot that minted it. centerX/centerY are SCENE/LOGICAL units
        // (documented in cmdUiSnapshot), the same space get-state/ui-query speak.
        QJsonObject sn = call(pipe, {{"cmd", "ui-snapshot"}, {"seq", 60}});
        QJsonArray els = sn.value("elements").toArray();
        require(!els.isEmpty(), "snapshot lists elements" + why());
        bool sawMouse = false, sawList = false;
        QString mouseHandle, buttonHandle;
        for (const QJsonValue& v : els) {
            const QJsonObject el = v.toObject();
            const QString name = el.value("objectName").toString();
            if (name == QStringLiteral("counterMouse")) {
                sawMouse = true; mouseHandle = el.value("handle").toString();
                require(el.value("interactive").toBool(), "MouseArea marked interactive");
            }
            if (name == QStringLiteral("counterButton"))
                buttonHandle = el.value("handle").toString();
            if (name == QStringLiteral("mainList")) {
                sawList = true;
                // The chain-walk pin: ListView's LEAF class "QQuickListView" carries
                // no "Flickable" token; only its QQuickFlickable BASE does. A
                // leaf-only substring check reports interactive:false here.
                require(el.value("interactive").toBool(),
                        "ListView marked interactive via superclass chain (QQuickFlickable base)"
                            + why());
            }
        }
        // Handles are OPAQUE: require non-empty and prove resolvable below; never
        // assume an internal format.
        require(sawMouse && !mouseHandle.isEmpty(),
                "the counter's MouseArea carries a handle" + why());
        require(sawList, "ui-snapshot includes the ListView fixture" + why());

        // count == elements.length, and every handle is non-empty AND unique.
        require(sn.value("count").toInt() == els.size(),
                "ui-snapshot count matches elements[] length" + why());
        {
            QStringList handles;
            for (const QJsonValue& v : els) {
                const QString h = v.toObject().value("handle").toString();
                require(!h.isEmpty(), "every element carries a non-empty handle" + why());
                handles << h;
            }
            require(QSet<QString>(handles.begin(), handles.end()).size() == handles.size(),
                    "ui-snapshot handles are unique" + why());
        }

        // ── Task 4: resolveTarget round-trip — a handle resolves to the SAME item ─
        // Identity-exact: read the objectName back THROUGH the handle (a rect
        // compare is weak — counterMouse and its parent share geometry). Proves the
        // handle resolves in a read AND that there is ONE resolver, not two.
        QJsonObject qgByHandle = call(pipe, {{"cmd", "qml-get"}, {"seq", 61},
            {"payload", QJsonObject{{"object", mouseHandle},
                                    {"props", QJsonArray{"objectName"}}}}});
        require(qgByHandle.value("type").toString() == "reply",
                "qml-get resolves a snapshot handle" + why());
        require(qgByHandle.value("props").toObject().value("objectName").toString()
                    == QStringLiteral("counterMouse"),
                "resolveTarget: the handle resolves to the SAME item (objectName round-trips)"
                    + why());

        // ── Task 4: grab by handle — attachGrab routes through resolveTarget ──
        // A snapshot handle is a grab target too. counterButton grabs cleanly in
        // both configs (offscreen software SG; RHI with QSG_NO_VSYNC=1), so its
        // handle must land a PNG on disk. Negative control: revert attachGrab to
        // findItem() and the handle is not a name -> GRAB_TARGET_NOT_FOUND.
        require(!buttonHandle.isEmpty(), "snapshot carries counterButton's handle" + why());
        QJsonObject hg = call(pipe, {{"cmd", "get-state"}, {"seq", 63},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", buttonHandle}}}}}},
            8000);
        require(hg.value("type").toString() == "reply",
                "grab by a snapshot handle replies" + why());
        require(QFileInfo::exists(hg.value("grabPath").toString()),
                "grab by a snapshot handle lands a PNG on disk: "
                    + hg.value("grabPath").toString() + why());

        // ── Task 4: snapshot-scoped handles — a superseded handle misses cleanly ─
        // A handle is single-snapshot-scoped: taking a NEW snapshot (from ANY
        // client) bumps the epoch and clears m_handles, so a handle from the prior
        // snapshot must now MISS deterministically — never silently resolve to the
        // new snapshot's Nth item. Negative control: drop resolveTarget's epoch
        // gate and the stale handle resolves to snapshot B's same-index item.
        const QString staleHandle = mouseHandle;             // minted by snapshot A (seq 60)
        call(pipe, {{"cmd", "ui-snapshot"}, {"seq", 64}});   // snapshot B: new epoch, m_handles rebuilt
        QJsonObject stale = call(pipe, {{"cmd", "qml-get"}, {"seq", 65},
            {"payload", QJsonObject{{"object", staleHandle},
                                    {"props", QJsonArray{"objectName"}}}}});
        require(stale.value("type").toString() == "error"
                    && stale.value("code").toString() == "NO_SUCH_ITEM",
                "a handle from a superseded snapshot misses cleanly (NO_SUCH_ITEM)" + why());

        std::cout << "LANISTA_OK\n";
        rc = 0;
        app.quit();
    });
    app.exec();
    return rc;
}
