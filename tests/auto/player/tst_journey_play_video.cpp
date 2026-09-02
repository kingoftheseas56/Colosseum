// tst_journey_play_video — Agent Visibility Phase 2, Slice J1-Video-Seam.
//
// Drives the REAL production MpvItem (native/player/mpvitem.{h,cpp} — the "player" root's mpv
// surface, qml/PlayerPage.qml:3011) over real ffmpeg-generated fixtures shared with the Vault
// admission gate (tests/fixtures/vault/media/), proving decodedWidth/decodedHeight report the
// SAME dwidth/dheight decode truth MediaAdmissionProbe.cpp:59-60 reads from its own headless
// handle — never FILE_LOADED, a route counter, log text, or the admission probe itself.
//
// Same house pattern as tests/http_header_channel_harness.cpp (a real MpvItem, event-driven
// waits, no sleeps) — converted to Qt Test idiom (per-case reporting) rather than the
// sentinel/exit-code harness contract. Two things http_header_channel_harness didn't need,
// which THIS test does (it is the first to depend on a real decoded frame, not just a wire
// request), both confirmed live rather than assumed:
//   1. The Qt Quick graphics API is a process-wide, pre-QGuiApplication boot choice
//      (native/main.cpp:497,503 — mpvqt renders through OpenGL); hence the hand-written main()
//      below instead of QTEST_MAIN, whose generated main() constructs QGuiApplication too soon.
//   2. MpvAbstractItem (mpvqt) is a QQuickFramebufferObject — its render/mpv context is created
//      through the scene graph, so it needs a REAL, live QQuickWindow actually showing it
//      before mpv's video pipeline reports decoded dimensions (confirmed live: without a
//      window, decodedWidth/decodedHeight never left zero for tiny.mp4, no error emitted).
//      windowedPlayer() below hosts every MpvItem in one exactly this way. This does not
//      contradict "Offscreen is broken for this app — QML tests windowed, short" (this
//      slice's own brief) — it is the same constraint, honored here at the native-test layer.
//
// Bridge status note (recorded for the report, not asserted here): PlayerPage.qml itself
// cannot be loaded by the colosseum_qml_tests runner — it `import`s the native
// "Colosseum.Player" QML module, which is hand-registered only in native/main.cpp's real app
// bootstrap (mpvitem.h's own header comment; also documented at
// tests/parity_load_harness.qml:6-9, which excludes PlayerPage.qml from its headless-load gate
// for exactly this reason). This native test therefore proves the seam at the MpvItem layer,
// which needs no such registration (MpvItem is constructed directly in C++, exactly like
// http_header_channel_harness.cpp already does).
//
// playerReadyFrom() below is a DOCUMENTED, test-only mirror of PlayerPage.qml's real
// `playerReady` formula (qml/PlayerPage.qml, "decoded-frame readiness" section) — used only to
// run this slice's mandated negative control (temporarily derive playerReady from
// fileLoaded/FILE_LOADED alone) inside this fast-rebuilding native target, since the QML
// property cannot be exercised by any runnable Quick Test today. It composes no state and is
// never linked into production; a reviewer can diff its one-line body against the cited QML
// property directly.

#include "player/mpvitem.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QString>
#include <QUrl>
#include <QtTest>

#include <memory>

namespace {

QString fixtureDir()
{
    return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/media/");
}

// Hosts a real MpvItem inside a real, shown QQuickWindow — the condition mpvqt's
// QQuickFramebufferObject-based render pipeline needs before it reports decoded video state
// (see this file's header comment, point 2). RAII: the window (and with it the item, its
// child) is torn down when the fixture goes out of scope, one per test case.
struct WindowedPlayer {
    QQuickWindow window;
    MpvItem *item;

    WindowedPlayer()
        : item(new MpvItem(window.contentItem()))
    {
        item->setWidth(320);
        item->setHeight(240);
        window.resize(320, 240);
        window.show();
    }
};

// Mirrors qml/PlayerPage.qml's `playerReady` property:
//   root.fileReady && !root.errored && mpv.decodedWidth > 0 && mpv.decodedHeight > 0
// `sessionFileLoaded` stands in for root.fileReady's real trigger (PlayerPage.qml's
// onFileLoaded handler sets root.fileReady = true the instant MpvItem::fileLoaded() fires —
// the same signal this test observes below). `sessionErrored` stands in for root.errored.
//
// NEGATIVE CONTROL (run manually, not part of the default gate): comment out the
// `&& decodedWidth > 0 && decodedHeight > 0` clause below (leaving only
// `sessionFileLoaded && !sessionErrored`) to reproduce a FILE_LOADED-derived readiness gate.
// Rebuild just this target and rerun colosseum.qttest.journey_play_video: exactly
// `audio_only_never_ready` must go red (the audio-only fixture fires fileLoaded with no
// decoded frame ever following), every other case must stay green. Restore before committing.
bool playerReadyFrom(bool sessionFileLoaded, bool sessionErrored, int decodedWidth, int decodedHeight)
{
    return sessionFileLoaded && !sessionErrored && decodedWidth > 0 && decodedHeight > 0;
}

void resolveSingleLocalXDisplay()
{
#if defined(Q_OS_LINUX)
    if (!qEnvironmentVariableIsEmpty("DISPLAY"))
        return;

    const QDir socketDir(QStringLiteral("/tmp/.X11-unix"));
    const QStringList sockets = socketDir.entryList(
        QStringList{QStringLiteral("X*")},
        QDir::System | QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name);
    if (sockets.size() != 1)
        return;

    const QString socket = sockets.constFirst();
    bool displayNumberOk = false;
    const int displayNumber = socket.mid(1).toInt(&displayNumberOk);
    if (!displayNumberOk || displayNumber < 0)
        return;

    qputenv("DISPLAY", QStringLiteral(":%1").arg(displayNumber).toUtf8());
#endif
}

} // namespace

class tst_journey_play_video : public QObject
{
    Q_OBJECT

private slots:
    void route_is_not_ready();
    void audio_only_never_ready();
    void decoded_fixture_reports_exact_dimensions();
    void source_identity_matches();
};

// A freshly constructed, shown player carries no source and no decoded frame — the closest
// native analogue of "the app has routed to the player page" with nothing loaded yet.
// Attaching/showing the item (the route) must not, by itself, read as ready.
void tst_journey_play_video::route_is_not_ready()
{
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));

    QCOMPARE(p.item->decodedWidth(), 0);
    QCOMPARE(p.item->decodedHeight(), 0);
    QVERIFY2(p.item->currentUrl().isEmpty(),
             "a freshly routed player must carry no source identity yet");
    QVERIFY2(!playerReadyFrom(/*sessionFileLoaded=*/false, /*sessionErrored=*/false,
                               p.item->decodedWidth(), p.item->decodedHeight()),
             "playerReady must be false before any route/load has happened");
}

// The audio-only fixture (tiny-audio.m4a — the SAME file vault_admission_probe_harness proves
// mpv FILE_LOADs but never decodes a video frame for) reaches mpv's fileLoaded — the real
// trigger PlayerPage.qml's root.fileReady binds to — and later reaches endFile (it plays out
// in a couple of seconds), yet decodedWidth/decodedHeight must never go positive at any point.
// This is the exact vacuity MediaAdmissionProbe.cpp:99-123 documents for the Vault gate,
// reproduced here for the live player: FILE_LOADED (or a route/session-ready flag derived from
// it alone) is not decode proof.
void tst_journey_play_video::audio_only_never_ready()
{
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));

    QSignalSpy loadedSpy(p.item, &MpvItem::fileLoaded);
    QSignalSpy endSpy(p.item, &MpvItem::endFile);
    QSignalSpy dimsSpy(p.item, &MpvItem::decodedDimensionsChanged);

    p.item->loadFile(fixtureDir() + QStringLiteral("tiny-audio.m4a"));

    QVERIFY2(loadedSpy.wait(15000),
             "tiny-audio.m4a never reached fileLoaded within 15s — test fixture unusable");
    // The session reached the SAME state PlayerPage.qml's root.fileReady turns true on, yet:
    QCOMPARE(p.item->decodedWidth(), 0);
    QCOMPARE(p.item->decodedHeight(), 0);
    QVERIFY2(!playerReadyFrom(/*sessionFileLoaded=*/true, /*sessionErrored=*/false,
                               p.item->decodedWidth(), p.item->decodedHeight()),
             "playerReady must stay false for an audio-only source even once the session is "
             "otherwise 'loaded' — this is the case the mandated negative control (FILE_LOADED-"
             "derived playerReady) must flip red");

    // Bounded, event-driven wait for the fixture to actually finish (a real mpv completion
    // signal, never a sleep) — confirms decoded dims never fired positive anywhere in between,
    // not merely at the one moment already checked above.
    QVERIFY2(endSpy.wait(15000), "tiny-audio.m4a never reached endFile within 15s");
    QCOMPARE(p.item->decodedWidth(), 0);
    QCOMPARE(p.item->decodedHeight(), 0);
    QCOMPARE(dimsSpy.count(), 0); // decodedDimensionsChanged never fires for a value that stays 0
}

// tiny.mp4 (the SAME fixture vault_admission_probe_harness proves ADMITS with dwidth>0) must
// report its exact known decoded dimensions — 64x64, the avc1 sample entry's stored width/
// height (tests/fixtures/vault/media/tiny.mp4's stsd box) — through decodedWidth/decodedHeight,
// not merely ">0".
void tst_journey_play_video::decoded_fixture_reports_exact_dimensions()
{
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));

    p.item->loadFile(fixtureDir() + QStringLiteral("tiny.mp4"));

    // dwidth/dheight are TWO INDEPENDENT mpv property-change events and may arrive in either
    // order or on separate event-loop turns — waiting on width alone races height (confirmed
    // live: vault_admission_probe_harness's own run reported "tiny.mp4 -> Admitted 64x0" for
    // this exact fixture, latching its verdict the instant dwidth arrived, before dheight had).
    // Wait for BOTH before asserting either.
    QTRY_VERIFY_WITH_TIMEOUT(p.item->decodedWidth() > 0 && p.item->decodedHeight() > 0, 15000);
    QCOMPARE(p.item->decodedWidth(), 64);
    QCOMPARE(p.item->decodedHeight(), 64);
    QVERIFY2(playerReadyFrom(/*sessionFileLoaded=*/true, /*sessionErrored=*/false,
                              p.item->decodedWidth(), p.item->decodedHeight()),
             "playerReady must be true once a real frame has decoded on an otherwise-loaded session");
}

// The stable source identity (mpv's currentUrl, which qml/PlayerPage.qml's sourceIdentity
// falls back to when no cross-source mediaId is set) must match the exact fixture that was
// loaded — real session state, not a route counter.
void tst_journey_play_video::source_identity_matches()
{
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));

    const QString path = fixtureDir() + QStringLiteral("tiny.mp4");
    p.item->loadFile(path);

    const QUrl expected = QUrl::fromUserInput(path);
    QCOMPARE(p.item->currentUrl(), expected);
    QVERIFY2(p.item->currentUrl().isLocalFile(), "the loaded fixture is a local file");
    QCOMPARE(QDir::toNativeSeparators(p.item->currentUrl().toLocalFile()),
             QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

// Hand-written main() instead of QTEST_MAIN: mpvqt (MpvAbstractItem) renders through OpenGL,
// and Qt Quick's graphics API is a PROCESS-WIDE choice that must be set before the first
// QQuickWindow — and before QGuiApplication itself constructs (native/main.cpp:497,503, the
// same two calls, same order). QTEST_MAIN's generated main() constructs QGuiApplication as
// its very first statement, which is too late: confirmed live — without this, every case
// still ran, but decoded_fixture_reports_exact_dimensions failed (decodedWidth/decodedHeight
// stayed 0) behind a QCRITICAL "The graphics api must be set to opengl or mpv won't be able
// to render the video." on every case.
int main(int argc, char **argv)
{
    // CTest/remote shells can lose DISPLAY even while the local desktop X server is alive.
    // This test must use a real shown OpenGL window, so only recover an unambiguous local
    // X socket. Multiple/no sockets remain a hard environment failure rather than guessing.
    resolveSingleLocalXDisplay();
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    tst_journey_play_video tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_journey_play_video.moc"
