#include "player/livestore.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QTest>

namespace {
constexpr auto kFakeRecorderMarker = "COLOSSEUM_LIVESTORE_FAKE_RECORDER";
constexpr auto kFakeRecorderExitMs = "COLOSSEUM_LIVESTORE_FAKE_RECORDER_EXIT_MS";
}

class tst_livestore : public QObject
{
    Q_OBJECT

private slots:
    void stopIsPromptAndTerminalStateIsIdempotent();
    void naturalExitFinalizesExactlyOnce();
    void startErrorFinalizesExactlyOnce();
};

void tst_livestore::stopIsPromptAndTerminalStateIsIdempotent()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    qputenv("COLOSSEUM_MPV", QCoreApplication::applicationFilePath().toUtf8());
    qputenv(kFakeRecorderMarker, "1");
    qunsetenv(kFakeRecorderExitMs);

    LiveStore store;
    QSignalSpy changed(&store, &LiveStore::changed);
    QElapsedTimer startElapsed;
    startElapsed.start();
    const QString id = store.startRecording({
        {QStringLiteral("url"), QStringLiteral("https://example.test/live")},
        {QStringLiteral("outputPath"), output.filePath(QStringLiteral("capture.ts"))},
        {QStringLiteral("durationSec"), 3600},
    });
    QVERIFY(!id.isEmpty());
    QVERIFY2(startElapsed.elapsed() < 250,
             "interactive start must return without waiting for recorder startup");
    QCOMPARE(changed.count(), 1);

    QElapsedTimer elapsed;
    elapsed.start();
    store.stopRecording(id);
    QVERIFY2(elapsed.elapsed() < 250,
             "interactive stop must not wait for the recorder process to exit");
    QCOMPARE(changed.count(), 2);

    const auto terminal = store.recordings().first().toMap();
    QCOMPARE(terminal.value(QStringLiteral("state")).toString(), QStringLiteral("done"));

    store.stopRecording(id);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(store.recordings().first().toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("done"));

    QTRY_VERIFY_WITH_TIMEOUT(store.recordings().first().toMap().value(QStringLiteral("state"))
                                 .toString() == QStringLiteral("done"), 2000);
    // Allow the bounded kill fallback and asynchronous QProcess::finished cleanup
    // to drain before the LiveStore fixture is destroyed.
    QTest::qWait(1400);
}

void tst_livestore::naturalExitFinalizesExactlyOnce()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    qputenv("COLOSSEUM_MPV", QCoreApplication::applicationFilePath().toUtf8());
    qputenv(kFakeRecorderMarker, "1");
    qputenv(kFakeRecorderExitMs, "25");

    LiveStore store;
    QSignalSpy changed(&store, &LiveStore::changed);
    const QString id = store.startRecording({
        {QStringLiteral("url"), QStringLiteral("https://example.test/live")},
        {QStringLiteral("outputPath"), output.filePath(QStringLiteral("capture.ts"))},
        {QStringLiteral("durationSec"), 3600},
    });
    QVERIFY(!id.isEmpty());
    QCOMPARE(changed.count(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(store.recordings().first().toMap()
                                  .value(QStringLiteral("state")).toString(),
                              QStringLiteral("done"), 2000);
    QCOMPARE(changed.count(), 2);
}

void tst_livestore::startErrorFinalizesExactlyOnce()
{
    QTemporaryDir output;
    QTemporaryFile recorder;
    QVERIFY(output.isValid());
    QVERIFY(recorder.open());
    recorder.write("not an executable");
    recorder.close();
    qputenv("COLOSSEUM_MPV", recorder.fileName().toUtf8());
    qputenv(kFakeRecorderMarker, "1");
    qunsetenv(kFakeRecorderExitMs);

    LiveStore store;
    QSignalSpy changed(&store, &LiveStore::changed);
    const QString id = store.startRecording({
        {QStringLiteral("url"), QStringLiteral("https://example.test/live")},
        {QStringLiteral("outputPath"), output.filePath(QStringLiteral("capture.ts"))},
        {QStringLiteral("durationSec"), 3600},
    });
    QVERIFY(!id.isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(store.recordings().first().toMap()
                                  .value(QStringLiteral("state")).toString(),
                              QStringLiteral("error"), 2000);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(store.recordings().first().toMap().value(QStringLiteral("error")).toString(),
             QStringLiteral("Could not start mpv for DVR recording."));
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet(kFakeRecorderMarker)) {
        QCoreApplication app(argc, argv);
        const int exitMs = qEnvironmentVariableIntValue(kFakeRecorderExitMs);
        if (exitMs > 0)
            QTimer::singleShot(exitMs, &app, &QCoreApplication::quit);
        return app.exec();
    }

    QCoreApplication app(argc, argv);
    tst_livestore test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_livestore.moc"
