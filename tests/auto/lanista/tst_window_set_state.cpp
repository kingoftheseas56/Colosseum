// tst_window_set_state — Agent Visibility Phase 2, Slice J1-Tray-Bridge.
//
// Drives the REAL LanistaServer::dispatch() path (native/devtools/LanistaServer.{h,cpp}) over a
// real QLocalSocket, exactly like tests/lanista_harness.cpp's own client does — this file just
// keeps it self-contained (one Qt Test target, one TU) rather than growing that shared harness.
// Each case constructs its OWN QQmlApplicationEngine + LanistaServer against one or two inline
// QML `Window { }` roots (no scene file on disk — a window this narrow needs no fixture), so
// state from one case can never leak into the next.
//
// window-set-state calls QWindow's own showNormal()/showMinimized()/hide() — the SAME API a
// titlebar minimize or a taskbar/tray restore invokes in production — never a simulated OS
// click. offscreen QPA genuinely tracks these transitions (QWindow::visibility()): the same
// house pattern already proven for minimize/restore by tests/window_shell_gui_harness.cpp
// ("Test E: restore-from-minimize"), reused rather than re-derived.
//
// Named cases (plan-mandated): read_gate_refuses_window_set_state, drive_gate_accepts_normal,
// drive_gate_accepts_minimized, drive_gate_accepts_hidden, bad_state_is_rejected,
// only_first_root_is_addressed.

#include "devtools/LanistaServer.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtTest>

namespace {

// The ONE test pipe name this TU's process ever listens on. Never the daily app's default —
// each test case constructs and destroys its own LanistaServer, so the pipe frees between
// cases within this single sequential process.
const char* kPipe = "ColosseumLanistaWindowSetStateTest";

// One inline root `Window` component per call — LanistaServer sees a REAL QQuickWindow root
// exactly like main.cpp's real ApplicationWindow, with no QML file on disk needed. Each call
// loads a NEW top-level component into `engine`, so engine.rootObjects() grows by one, in
// load order — the same order mainWindow() (LanistaServer.cpp) walks when picking "first".
QQuickWindow* loadWindowRoot(QQmlApplicationEngine& engine, const QString& objectName)
{
    static int counter = 0;
    const QByteArray qml = QByteArrayLiteral(
        "import QtQuick\nimport QtQuick.Window\nWindow {\n    objectName: \"")
        + objectName.toUtf8() + QByteArrayLiteral("\"\n    visible: true\n"
        "    width: 200\n    height: 200\n}\n");
    const int before = engine.rootObjects().size();
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                      [](const QList<QQmlError>& warnings) {
                          for (const QQmlError& e : warnings)
                              qWarning() << "QML warning:" << e.toString();
                      });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &engine,
                      [](const QUrl& url) { qWarning() << "QML objectCreationFailed:" << url; });
    ++counter;
    engine.loadData(qml, QUrl());
    for (int i = 0; i < 10 && engine.rootObjects().size() == before; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (engine.rootObjects().size() == before)
        qWarning() << "loadWindowRoot: no new root object after loadData(); qml=" << qml;
    return qobject_cast<QQuickWindow*>(engine.rootObjects().isEmpty()
                                            ? nullptr : engine.rootObjects().last());
}

// Lets the offscreen QPA backend actually process a show/hide/minimize request before the
// test reads visibility() back — mirrors window_shell_gui_harness.cpp's own settleEvents().
void settle()
{
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// One request, one reply, over a real QLocalSocket — LanistaServer::Replier::send() writes its
// line and immediately disconnectFromServer()s, so `disconnected` is the correct completion
// signal (matches tests/lanista_harness.cpp's own rawExchange()/call() shape, trimmed to a
// single non-chunked send since this file drives no framing edge cases).
QJsonObject sendCmd(const QString& cmd, const QJsonObject& payload = {})
{
    QLocalSocket sock;
    QEventLoop loop;
    QByteArray got;
    bool timedOut = false;

    QObject::connect(&sock, &QLocalSocket::readyRead, &loop, [&]() { got += sock.readAll(); });
    QObject::connect(&sock, &QLocalSocket::disconnected, &loop, [&]() {
        got += sock.readAll();
        loop.quit();
    });
    QObject::connect(&sock, &QLocalSocket::errorOccurred, &loop,
                      [&](QLocalSocket::LocalSocketError e) {
                          if (e != QLocalSocket::PeerClosedError)
                              loop.quit();
                      });
    QObject::connect(&sock, &QLocalSocket::connected, &loop, [&]() {
        QJsonObject req{{QStringLiteral("cmd"), cmd}, {QStringLiteral("seq"), 1}};
        if (!payload.isEmpty())
            req.insert(QStringLiteral("payload"), payload);
        sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
        sock.flush();
    });
    QTimer::singleShot(5000, &loop, [&]() { timedOut = true; loop.quit(); });

    sock.connectToServer(QString::fromLatin1(kPipe));
    loop.exec();

    if (timedOut || got.isEmpty())
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                           {QStringLiteral("code"), QStringLiteral("TEST_NO_REPLY")}};
    const qsizetype nl = got.indexOf('\n');
    return QJsonDocument::fromJson(nl < 0 ? got : got.left(nl)).object();
}

} // namespace

class tst_window_set_state : public QObject
{
    Q_OBJECT

private slots:
    void init();   // runs before EVERY case: gates start closed, always.

    void read_gate_refuses_window_set_state();
    void drive_gate_accepts_normal();
    void drive_gate_accepts_minimized();
    void drive_gate_accepts_hidden();
    void bad_state_is_rejected();
    void only_first_root_is_addressed();
};

void tst_window_set_state::init()
{
    // The denial case is the point: every case must start from a CLOSED gate, and env vars
    // persist across QTest's private slots within one process, so this cannot be assumed —
    // it must be reasserted before each case.
    qunsetenv("COLOSSEUM_LANISTA_DRIVE");
}

// The gate refusal itself: no COLOSSEUM_LANISTA_DRIVE means DRIVE_DISABLED, exactly like the
// four existing "hands" (ui-click et al.) — dispatch()'s central gate, never a per-handler
// re-check. This is also the negative control's OWN target: temporarily reclassify
// window-set-state as addRead in the registration block, and only this case must turn red.
void tst_window_set_state::read_gate_refuses_window_set_state()
{
    QQmlApplicationEngine engine;
    QVERIFY(loadWindowRoot(engine, QStringLiteral("rootA")));
    settle();
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject res = sendCmd(QStringLiteral("window-set-state"),
                                     QJsonObject{{QStringLiteral("state"), QStringLiteral("normal")}});
    QCOMPARE(res.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(res.value(QStringLiteral("code")).toString(), QStringLiteral("DRIVE_DISABLED"));
}

// Drive-gated, real transition: minimize first (proving the fixture actually leaves "normal"),
// THEN restore — the reply's own state/visible fields AND a live visibility() readback on the
// QQuickWindow itself must both agree the window is back to normal, not merely "not minimized".
void tst_window_set_state::drive_gate_accepts_normal()
{
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QQuickWindow* w = loadWindowRoot(engine, QStringLiteral("rootA"));
    QVERIFY(w);
    settle();
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject minimized = sendCmd(QStringLiteral("window-set-state"),
        QJsonObject{{QStringLiteral("state"), QStringLiteral("minimized")}});
    QCOMPARE(minimized.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    settle();
    QCOMPARE(w->visibility(), QWindow::Minimized);

    const QJsonObject normal = sendCmd(QStringLiteral("window-set-state"),
        QJsonObject{{QStringLiteral("state"), QStringLiteral("normal")}});
    QCOMPARE(normal.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(normal.value(QStringLiteral("state")).toString(), QStringLiteral("normal"));
    QCOMPARE(normal.value(QStringLiteral("visible")).toBool(), true);
    settle();
    QCOMPARE(w->visibility(), QWindow::Windowed);
    QVERIFY(w->isVisible());
}

// Drive-gated, real transition into minimized — the reply AND the live QWindow both report it.
void tst_window_set_state::drive_gate_accepts_minimized()
{
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QQuickWindow* w = loadWindowRoot(engine, QStringLiteral("rootA"));
    QVERIFY(w);
    settle();
    QCOMPARE(w->visibility(), QWindow::Windowed);   // baseline: starts normal
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject res = sendCmd(QStringLiteral("window-set-state"),
        QJsonObject{{QStringLiteral("state"), QStringLiteral("minimized")}});
    QCOMPARE(res.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(res.value(QStringLiteral("state")).toString(), QStringLiteral("minimized"));
    settle();
    QCOMPARE(w->visibility(), QWindow::Minimized);
}

// Drive-gated, real transition into hidden — hide() clears isVisible() outright, unlike
// minimize (a minimized window is still "visible" in Qt's own model; only hide() flips it).
void tst_window_set_state::drive_gate_accepts_hidden()
{
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QQuickWindow* w = loadWindowRoot(engine, QStringLiteral("rootA"));
    QVERIFY(w);
    settle();
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject res = sendCmd(QStringLiteral("window-set-state"),
        QJsonObject{{QStringLiteral("state"), QStringLiteral("hidden")}});
    QCOMPARE(res.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(res.value(QStringLiteral("state")).toString(), QStringLiteral("hidden"));
    QCOMPARE(res.value(QStringLiteral("visible")).toBool(), false);
    settle();
    QVERIFY(!w->isVisible());
    QCOMPARE(w->visibility(), QWindow::Hidden);
}

// An unrecognized state name is rejected BEFORE touching the window at all — the window's
// visibility must read back byte-identical to its pre-call state, proving no partial mutation
// on a refused request (not merely "the command returned an error").
void tst_window_set_state::bad_state_is_rejected()
{
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QQuickWindow* w = loadWindowRoot(engine, QStringLiteral("rootA"));
    QVERIFY(w);
    settle();
    const QWindow::Visibility before = w->visibility();
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject res = sendCmd(QStringLiteral("window-set-state"),
        QJsonObject{{QStringLiteral("state"), QStringLiteral("bogus")}});
    QCOMPARE(res.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(res.value(QStringLiteral("code")).toString(), QStringLiteral("BAD_STATE"));
    settle();
    QCOMPARE(w->visibility(), before);
}

// TWO root windows: window-set-state must move only the FIRST (mainWindow()'s own "first, not
// necessarily main" law — the same scope every Task 2/3/5 command already honors). The second
// root is proven UNCHANGED, not merely "not asserted on" — a global sweep across every root
// window would fail this case, which is exactly the bug this proves absent.
void tst_window_set_state::only_first_root_is_addressed()
{
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QQuickWindow* first = loadWindowRoot(engine, QStringLiteral("rootFirst"));
    QQuickWindow* second = loadWindowRoot(engine, QStringLiteral("rootSecond"));
    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(first != second);
    settle();
    QCOMPARE(first->visibility(), QWindow::Windowed);
    QCOMPARE(second->visibility(), QWindow::Windowed);
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject res = sendCmd(QStringLiteral("window-set-state"),
        QJsonObject{{QStringLiteral("state"), QStringLiteral("minimized")}});
    QCOMPARE(res.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(res.value(QStringLiteral("objectName")).toString(), QStringLiteral("rootFirst"));
    settle();
    QCOMPARE(first->visibility(), QWindow::Minimized);
    QCOMPARE(second->visibility(), QWindow::Windowed);
    QVERIFY2(second->isVisible(), "the second root window must be untouched by a "
                                   "first-root-only command");
}

// Hand-written main() instead of QTEST_MAIN, mirroring tests/lanista_harness.cpp's own ctor
// order (documented there): the windeployqt'd platforms/ dir beside the exe carries ONLY
// qwindows.dll and shadows the real Qt install's plugin dir, so plain QT_QPA_PLATFORM=offscreen
// fails to load unless QT_QPA_PLATFORM_PLUGIN_PATH is repointed FIRST — both must be set before
// QGuiApplication's constructor resolves the platform plugin. Caller (env) wins either way.
int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef WINDOW_SET_STATE_QT_PLATFORMS_DIR
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH"))
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", WINDOW_SET_STATE_QT_PLATFORMS_DIR);
#endif
    // NEVER the default pipe name: LanistaServer::pipeName() falls back to
    // "ColosseumLanista" — the SAME pipe the daily app's always-on bridge binds
    // (LanistaServer.h's own header note) — when COLOSSEUM_LANISTA_PIPE is unset.
    // This MUST be set before the first LanistaServer is constructed, in every
    // test case, or this TU would contend for the daily app's own pipe name.
    qputenv("COLOSSEUM_LANISTA_PIPE", kPipe);
    QGuiApplication app(argc, argv);
    tst_window_set_state tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_window_set_state.moc"
