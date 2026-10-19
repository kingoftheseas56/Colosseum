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
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QPointer>
#include <QProcess>
#include <QSignalSpy>
#include <QSemaphore>
#include <QScopeGuard>
#include <QThread>
#include <MpvController>
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
    void same_size_warm_reloads_refresh_dimensions();
    void queued_decode_replacement_data();
    void queued_decode_replacement();
    void late_native_replacement_data();
    void late_native_replacement();
    void dynamic_dimensions_are_coherent();
    void playlist_redirect_retains_request();
    void native_queue_entry_order();
    void teardown_during_worker_exit();
    void teardown_data();
    void teardown();
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

// Exercise actual warm replacement of both the same URL and an equal-size second
// file. Cache-zero at load is required, but it must recover from native decode even
// if mpv coalesces equal dwidth/dheight values across replacement. Raw observations
// in a failure distinguish a stale application cache from failed native decoding.
void tst_journey_play_video::same_size_warm_reloads_refresh_dimensions()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString ffmpeg = MpvItem::findFfmpeg();
    QVERIFY2(!ffmpeg.isEmpty(), "ffmpeg is required to generate bounded warm-reload fixtures");
    const QString first = temp.filePath(QStringLiteral("blue.mp4"));
    const QString second = temp.filePath(QStringLiteral("red.mp4"));
    for (const QString &color : {QStringLiteral("blue"), QStringLiteral("red")}) {
        QProcess generator;
        generator.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("color=c=%1:s=64x64:r=10").arg(color),
            QStringLiteral("-t"), QStringLiteral("20"), QStringLiteral("-an"),
            QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
            temp.filePath(color + QStringLiteral(".mp4"))});
        QVERIFY(generator.waitForFinished(15000));
        QCOMPARE(generator.exitCode(), 0);
    }
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));
    p.item->setProperty(QStringLiteral("ao"), QStringLiteral("null"));
    // Use a long playing source: the original 1s fixture could reach EOF
    // during a failing wait. Require actual advancement on each loaded source.
    p.item->setPause(false);
    const QPointer<MpvItem> originalPlayer(p.item);
    QSignalSpy loadedSpy(p.item, &MpvItem::fileLoaded);
    for (int pass = 0; pass < 8; ++pass) {
        const QString path = (pass % 4 < 2) ? first : second;
        QVERIFY(originalPlayer);
        QCOMPARE(p.item, originalPlayer.data());
        loadedSpy.clear();
        p.item->loadFile(path);
        QCOMPARE(p.item->currentUrl(), QUrl::fromUserInput(path));
        QCOMPARE(p.item->decodedWidth(), 0);
        QCOMPARE(p.item->decodedHeight(), 0);
        const bool ready = QTest::qWaitFor([&] {
            return !loadedSpy.isEmpty() && p.item->duration() > 0 && p.item->position() > 0.2
                && p.item->decodedWidth() > 0 && p.item->decodedHeight() > 0;
        }, 3000);
        if (!ready) {
            qWarning() << "warm pass" << pass << "cache" << p.item->decodedWidth()
                       << p.item->decodedHeight() << "native"
                       << p.item->getProperty(QStringLiteral("dwidth"))
                       << p.item->getProperty(QStringLiteral("dheight"))
                       << "clock" << p.item->position() << "duration" << p.item->duration();
        }
        QVERIFY2(ready, "same-size native reload did not restore decoded-dimension cache");
        QCOMPARE(p.item->decodedWidth(), 64);
        QCOMPARE(p.item->decodedHeight(), 64);
        QVERIFY(!p.item->pause());
    }
    // A rapid video replacement followed by audio must not inherit the prior
    // frame cache from a queued reconfiguration. Audio stays paused for inspection.
    p.item->loadFile(first);
    p.item->setPause(true);
    loadedSpy.clear();
    const QString audio = fixtureDir() + QStringLiteral("tiny-audio.m4a");
    p.item->loadFile(audio);
    QTRY_VERIFY_WITH_TIMEOUT(!loadedSpy.isEmpty() && p.item->pause(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(p.item->getProperty(QStringLiteral("path")).toString(), QDir::toNativeSeparators(audio), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(p.item->decodedWidth(), 0, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(p.item->decodedHeight(), 0, 3000);
    QCOMPARE(p.item, originalPlayer.data());
    QSignalSpy endedSpy(p.item, &MpvItem::endFile);
    p.item->command(QStringList{QStringLiteral("stop")});
    QTRY_VERIFY_WITH_TIMEOUT(!endedSpy.isEmpty(), 3000);
    QCOMPARE(p.item->decodedWidth(), 0);
    QCOMPARE(p.item->decodedHeight(), 0);
}

void tst_journey_play_video::queued_decode_replacement_data()
{
    QTest::addColumn<QSize>("secondSize");
    QTest::addColumn<bool>("returnToFirst");
    QTest::newRow("rapid-A-B-different-sizes") << QSize(96, 80) << false;
    QTest::newRow("A-B-A-same-size") << QSize(64, 64) << true;
    QTest::newRow("video-audio-handoff") << QSize() << false;
}

// Leave real A events in the GUI queue while the native worker finishes decoding.
// No synthetic mpv signals or private MpvItem state: the semaphore models a busy
// GUI thread, and the already shown/warmed surface keeps native decoding active.
void tst_journey_play_video::queued_decode_replacement()
{
    QFETCH(QSize, secondSize);
    QFETCH(bool, returnToFirst);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString ffmpeg = MpvItem::findFfmpeg();
    QVERIFY(!ffmpeg.isEmpty());
    const QString seed = temp.filePath(QStringLiteral("seed.mp4"));
    const QString first = temp.filePath(QStringLiteral("A.mp4"));
    const QString second = secondSize.isValid()
        ? temp.filePath(QStringLiteral("B.mp4"))
        : fixtureDir() + QStringLiteral("tiny-audio.m4a");
    const QList<QPair<QString, QSize>> fixtures = {
        {seed, QSize(32, 32)}, {first, QSize(64, 64)},
        {temp.filePath(QStringLiteral("B.mp4")), secondSize.isValid() ? secondSize : QSize(96, 80)}};
    for (const auto &fixture : fixtures) {
        QProcess generator;
        generator.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("color=c=blue:s=%1x%2:r=10")
                .arg(fixture.second.width()).arg(fixture.second.height()),
            QStringLiteral("-t"), QStringLiteral("20"), QStringLiteral("-an"),
            QStringLiteral("-c:v"), QStringLiteral("mpeg4"), fixture.first});
        QVERIFY(generator.waitForFinished(15000));
        QCOMPARE(generator.exitCode(), 0);
    }
    struct InspectablePlayer : MpvItem { using MpvItem::mpvController; };
    QQuickWindow window;
    auto *item = new InspectablePlayer;
    item->setParent(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setWidth(320);
    item->setHeight(240);
    window.resize(320, 240);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    item->setProperty(QStringLiteral("ao"), QStringLiteral("null"));
    item->loadFile(seed);
    QTRY_COMPARE_WITH_TIMEOUT(item->decodedWidth(), 32, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(item->decodedHeight(), 32, 5000);

    MpvController *controller = item->mpvController();
    QVERIFY(controller->thread() != QThread::currentThread());
    QSemaphore decoded;
    // All accesses to these two flags run on the controller thread.
    bool loaded = false;
    bool reported = false;
    auto observeDecode = [&] {
        if (!reported && loaded
            && controller->getProperty(QStringLiteral("path")).toString()
                == QDir::toNativeSeparators(first)
            && controller->getProperty(QStringLiteral("dwidth")).toInt() == 64
            && controller->getProperty(QStringLiteral("dheight")).toInt() == 64) {
            reported = true;
            decoded.release();
        }
    };
    const auto loadConnection = connect(controller, &MpvController::fileLoaded,
        controller, [&] { loaded = true; observeDecode(); }, Qt::DirectConnection);
    const auto reconfigConnection = connect(controller, &MpvController::videoReconfig,
        controller, observeDecode, Qt::DirectConnection);
    const auto propertyConnection = connect(controller, &MpvController::propertyChanged,
        controller, [&](const QString &name, const QVariant &) {
            if (name == QLatin1String("dwidth") || name == QLatin1String("dheight"))
                observeDecode();
        }, Qt::DirectConnection);
    item->loadFile(first);
    // Deliberately do not dispatch GUI events until A has really decoded.
    const bool nativeDecoded = decoded.tryAcquire(1, 5000);
    QMetaObject::invokeMethod(controller, [&] {
        disconnect(loadConnection);
        disconnect(reconfigConnection);
        disconnect(propertyConnection);
    }, Qt::BlockingQueuedConnection);
    QVERIFY2(nativeDecoded, "A must decode natively while its GUI notifications remain queued");
    QCOMPARE(item->decodedWidth(), 0);
    QCOMPARE(item->decodedHeight(), 0);

    QSize expected = secondSize.isValid() ? secondSize : QSize(0, 0);
    QString expectedPath = second;
    QStringList wrongAnnouncements;
    QObject observation;
    connect(item, &MpvItem::decodedDimensionsChanged, &observation, [&] {
        const QSize announced(item->decodedWidth(), item->decodedHeight());
        if ((announced.width() > 0 && announced.width() != expected.width())
            || (announced.height() > 0 && announced.height() != expected.height())) {
            wrongAnnouncements << QStringLiteral("%1: %2x%3 expected %4x%5")
                .arg(expectedPath).arg(announced.width()).arg(announced.height())
                .arg(expected.width()).arg(expected.height());
        }
    });
    QSignalSpy loadedSpy(item, &MpvItem::fileLoaded);
    item->loadFile(second);
    QCOMPARE(item->decodedWidth(), 0);
    QCOMPARE(item->decodedHeight(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(item->getProperty(QStringLiteral("path")).toString(),
                             QDir::toNativeSeparators(second), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!loadedSpy.isEmpty(), 5000);
    if (secondSize.isValid()) {
        QTRY_COMPARE_WITH_TIMEOUT(item->getProperty(QStringLiteral("dwidth")).toInt(), expected.width(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(item->getProperty(QStringLiteral("dheight")).toInt(), expected.height(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(item->decodedWidth(), expected.width(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(item->decodedHeight(), expected.height(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(item->position() > 0.2, 5000);
    } else {
        QSignalSpy endedSpy(item, &MpvItem::endFile);
        QTRY_VERIFY_WITH_TIMEOUT(!endedSpy.isEmpty(), 5000);
        QCOMPARE(item->decodedWidth(), 0);
        QCOMPARE(item->decodedHeight(), 0);
    }
    if (returnToFirst) {
        expected = QSize(64, 64);
        expectedPath = first;
        loadedSpy.clear();
        item->loadFile(first);
        QCOMPARE(item->decodedWidth(), 0);
        QCOMPARE(item->decodedHeight(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(item->getProperty(QStringLiteral("path")).toString(),
                                 QDir::toNativeSeparators(first), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!loadedSpy.isEmpty() && item->position() > 0.2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(item->decodedWidth(), 64, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(item->decodedHeight(), 64, 5000);
    }
    QVERIFY2(wrongAnnouncements.isEmpty(), qPrintable(wrongAnnouncements.join(QLatin1Char('\n'))));
}

void tst_journey_play_video::late_native_replacement_data()
{
    QTest::addColumn<int>("eventType");
    QTest::addColumn<int>("reason");
    QTest::newRow("late-A-error") << int(MPV_EVENT_END_FILE) << int(MPV_END_FILE_REASON_ERROR);
    QTest::newRow("late-A-eof") << int(MPV_EVENT_END_FILE) << int(MPV_END_FILE_REASON_EOF);
    QTest::newRow("late-A-stop") << int(MPV_EVENT_END_FILE) << int(MPV_END_FILE_REASON_STOP);
    QTest::newRow("late-A-reconfig") << int(MPV_EVENT_VIDEO_RECONFIG) << 0;
    QTest::newRow("late-A-loaded") << int(MPV_EVENT_FILE_LOADED) << 0;
}

void tst_journey_play_video::late_native_replacement()
{
    QFETCH(int, eventType);
    QFETCH(int, reason);
    struct InspectablePlayer : MpvItem { using MpvItem::mpvController; };
    QQuickWindow window;
    auto *item = new InspectablePlayer;
    item->setParent(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(320, 240));
    window.resize(320, 240);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    item->setPause(true);
    const QString path = fixtureDir() + QStringLiteral("tiny.mp4");
    item->loadFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(item->decodedWidth(), 64, 5000);
    auto *controller = item->mpvController();
    qint64 oldId = -1;
    QMetaObject::invokeMethod(controller, [&] {
        oldId = item->m_decodeEvents->expectedEntry;
    }, Qt::BlockingQueuedConnection);
    QVERIFY(oldId >= 0);

    QSignalSpy endSpy(item, &MpvItem::endFile);
    QSignalSpy errorSpy(item, &MpvItem::playbackError);
    QSignalSpy reconfigSpy(item, &MpvItem::videoReconfig);
    QSignalSpy loadedSpy(item, &MpvItem::fileLoaded);
    // Park the producer so the injection is queued immediately AFTER B's
    // load-command boundary and BEFORE any queued native drain for B. This is
    // deliberately a native-adapter injection, not an attempt to make libmpv
    // naturally reproduce a scheduling race. Same URL, new native entry id.
    QSemaphore parked, resume;
    QMetaObject::invokeMethod(controller, [&] {
        parked.release();
        resume.acquire();
    }, Qt::QueuedConnection);
    QVERIFY(parked.tryAcquire(1, 5000));
    item->loadFile(path);
    bool checked = false;
    bool distinctId = false;
    bool leaked = true;
    const auto inject = [&] {
        mpv_event_end_file end{};
        end.playlist_entry_id = oldId;
        end.reason = mpv_end_file_reason(reason);
        mpv_event event{};
        event.event_id = mpv_event_id(eventType);
        event.data = &end;
        item->handlePlaybackEvent(event);
    };
    QMetaObject::invokeMethod(controller, [&] {
        distinctId = item->m_decodeEvents->expectedEntry != oldId;
        inject();
        QMetaObject::invokeMethod(item, [&] {
            leaked = !endSpy.isEmpty() || !errorSpy.isEmpty()
                || !reconfigSpy.isEmpty() || !loadedSpy.isEmpty();
            checked = true;
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    resume.release();
    QTRY_VERIFY_WITH_TIMEOUT(checked, 5000);
    QVERIFY(distinctId);
    QVERIFY2(!leaked, "late A event reached B handlers after B load boundary");
    QTRY_VERIFY_WITH_TIMEOUT(!loadedSpy.isEmpty(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(item->decodedWidth(), 64, 5000);
    // END_FILE has an explicit id: even a delayed A END after B START/LOAD
    // must neither terminate B nor clear its decoded dimensions.
    if (eventType == MPV_EVENT_END_FILE) {
        QMetaObject::invokeMethod(controller, inject, Qt::BlockingQueuedConnection);
        QCoreApplication::sendPostedEvents(item, QEvent::MetaCall);
        QVERIFY(endSpy.isEmpty());
        QVERIFY(errorSpy.isEmpty());
        QCOMPARE(item->decodedWidth(), 64);
    }
}

void tst_journey_play_video::dynamic_dimensions_are_coherent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString ffmpeg = MpvItem::findFfmpeg();
    QVERIFY(!ffmpeg.isEmpty());
    QFile stream(temp.filePath(QStringLiteral("dynamic.ts")));
    QVERIFY(stream.open(QIODevice::WriteOnly));
    for (const QSize size : {QSize(64, 48), QSize(96, 80)}) {
        const QString segment = temp.filePath(QString::number(size.width()) + QStringLiteral(".ts"));
        QProcess generator;
        generator.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("color=c=blue:s=%1x%2:r=10").arg(size.width()).arg(size.height()),
            QStringLiteral("-t"), QStringLiteral("2"), QStringLiteral("-an"),
            QStringLiteral("-c:v"), QStringLiteral("mpeg2video"), QStringLiteral("-f"),
            QStringLiteral("mpegts"), segment});
        QVERIFY(generator.waitForFinished(15000));
        QCOMPARE(generator.exitCode(), 0);
        QFile input(segment);
        QVERIFY(input.open(QIODevice::ReadOnly));
        const auto bytes = input.readAll();
        QCOMPARE(stream.write(bytes), bytes.size());
    }
    stream.close();
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));
    QStringList mixed;
    connect(p.item, &MpvItem::decodedDimensionsChanged, this, [&] {
        const QSize size(p.item->decodedWidth(), p.item->decodedHeight());
        if (size != QSize(0, 0) && size != QSize(64, 48) && size != QSize(96, 80))
            mixed << QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
    });
    p.item->loadFile(stream.fileName());
    QTRY_COMPARE_WITH_TIMEOUT(p.item->decodedWidth(), 64, 5000);
    QCOMPARE(p.item->decodedHeight(), 48);
    QTRY_COMPARE_WITH_TIMEOUT(p.item->decodedWidth(), 96, 5000);
    QCOMPARE(p.item->decodedHeight(), 80);
    QVERIFY2(mixed.isEmpty(), qPrintable(mixed.join(QLatin1Char(','))));
}

// Assertion-level native queue seam: deliberately leave an independent client
// undrained across A -> B -> A (same A URL, distinct native IDs), then consume
// its backlog on the controller callback seam. The source proof in mpvitem.cpp
// rules out producer-late old untagged events; this tests real queue delivery,
// not a fabricated old event relabelled with the current playlist property.
void tst_journey_play_video::native_queue_entry_order()
{
    struct InspectablePlayer : MpvItem { using MpvItem::mpvController; };
    QQuickWindow window;
    auto *item = new InspectablePlayer;
    item->setParent(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(320, 240));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *controller = item->mpvController();
    mpv_handle *client = nullptr;
    QMetaObject::invokeMethod(controller, [&] {
        client = mpv_create_client(controller->mpv(), "entry-order-test");
    }, Qt::BlockingQueuedConnection);
    QVERIFY(client);
    const auto clientGuard = qScopeGuard([&] { mpv_destroy(client); });
    item->setPause(true);
    QSignalSpy loaded(item, &MpvItem::fileLoaded);
    QList<qint64> requested;
    for (const auto &file : {QStringLiteral("tiny.mp4"),
                             QStringLiteral("tiny-audio.m4a"), QStringLiteral("tiny.mp4")}) {
        const auto count = loaded.size();
        item->loadFile(fixtureDir() + file);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), count + 1, 5000);
        QMetaObject::invokeMethod(controller, [&] {
            requested.append(item->m_decodeEvents->expectedEntry);
        }, Qt::BlockingQueuedConnection);
    }
    QVERIFY(requested[0] != requested[1] && requested[1] != requested[2]
            && requested[0] != requested[2]);
    QList<qint64> starts, loads, ends;
    bool valid = true;
    int reconfigs = 0;
    QMetaObject::invokeMethod(controller, [&] {
        qint64 active = -1;
        bool seenLoad = false;
        for (;;) {
            const auto *event = mpv_wait_event(client, 0);
            if (event->event_id == MPV_EVENT_NONE)
                break;
            if (event->event_id == MPV_EVENT_START_FILE) {
                valid &= active == -1;
                active = static_cast<mpv_event_start_file *>(event->data)->playlist_entry_id;
                starts.append(active);
                seenLoad = false;
            } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
                valid &= active >= 0 && !seenLoad;
                loads.append(active);
                seenLoad = true;
            } else if (event->event_id == MPV_EVENT_END_FILE) {
                const auto id = static_cast<mpv_event_end_file *>(event->data)->playlist_entry_id;
                valid &= active == id;
                ends.append(id);
                active = -1;
            } else if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) {
                ++reconfigs; // Includes teardown/idle notifications, which need not have an active entry.
            } else if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW) {
                valid = false;
            }
        }
    }, Qt::BlockingQueuedConnection);
    QVERIFY(valid);
    QCOMPARE(starts, requested);
    QCOMPARE(loads, requested);
    QCOMPARE(ends, requested.mid(0, 2));
    QVERIFY(reconfigs > 0);
    QTRY_COMPARE_WITH_TIMEOUT(item->decodedWidth(), 64, 5000);
}

void tst_journey_play_video::teardown_during_worker_exit()
{
    struct InspectablePlayer : MpvItem { using MpvItem::mpvController; };
    for (int attempt = 0; attempt < 16; ++attempt) {
        auto *item = new InspectablePlayer;
        auto *controller = item->mpvController();
        auto *worker = controller->thread();
        const auto state = item->m_decodeEvents;
        QSemaphore parked, resume;
        QMetaObject::invokeMethod(controller, [&] {
            parked.release();
            resume.acquire();
        }, Qt::QueuedConnection);
        const auto unblock = qScopeGuard([&] { resume.release(); });
        QVERIFY(parked.tryAcquire(1, 5000));
        bool sawRetirement = false;
        // The worker is alive at derived-destructor entry. Finish it only AFTER
        // target retirement, while deletion is still in progress (base join).
        std::unique_ptr<QThread> finisher(QThread::create([&] {
            QElapsedTimer timeout;
            timeout.start();
            while (timeout.elapsed() < 5000) {
                {
                    QMutexLocker lock(&state->lifetime);
                    if (!state->target) {
                        sawRetirement = true;
                        break;
                    }
                }
                QThread::yieldCurrentThread();
            }
            worker->quit();
            resume.release();
        }));
        finisher->start();
        QElapsedTimer elapsed;
        elapsed.start();
        delete item;
        QVERIFY(finisher->wait(5000));
        QVERIFY(sawRetirement);
        QVERIFY2(elapsed.elapsed() < 3000, "worker-exit race stranded deletion");
    }
}

void tst_journey_play_video::teardown_data()
{
    QTest::addColumn<bool>("stopWorker");
    QTest::newRow("pending-native-events") << false;
    QTest::newRow("worker-already-exited") << true;
}

void tst_journey_play_video::playlist_redirect_retains_request()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QFile playlist(temp.filePath(QStringLiteral("source.m3u")));
    QVERIFY(playlist.open(QIODevice::WriteOnly));
    const auto path = (fixtureDir() + QStringLiteral("tiny.mp4\n")).toUtf8();
    playlist.write("#EXTM3U\n");
    playlist.write(path);
    playlist.write(path);
    playlist.close();
    WindowedPlayer p;
    QVERIFY(QTest::qWaitForWindowExposed(&p.window));
    QSignalSpy loadedSpy(p.item, &MpvItem::fileLoaded);
    p.item->loadFile(playlist.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.size() >= 2, 10000);
    QCOMPARE(p.item->currentUrl(), QUrl::fromLocalFile(playlist.fileName()));
}

void tst_journey_play_video::teardown()
{
    QFETCH(bool, stopWorker);
    struct InspectablePlayer : MpvItem { using MpvItem::mpvController; };
    auto *item = new InspectablePlayer;
    item->loadFile(fixtureDir() + QStringLiteral("tiny-audio.m4a"));
    if (stopWorker) {
        auto *worker = item->mpvController()->thread();
        worker->quit();
        QVERIFY(worker->wait(5000));
        QVERIFY(!worker->isRunning());
    }
    QElapsedTimer elapsed;
    elapsed.start();
    delete item;
    QVERIFY2(elapsed.elapsed() < 3000, "destructor blocked on worker acknowledgement");
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
