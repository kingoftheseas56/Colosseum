#include <QtTest>
#include <QTemporaryDir>
#include "tools/LanistaCapture.h"

class CaptureRunnerTest : public QObject
{
    Q_OBJECT
private slots:
    void gifArgsUseFixedPresentationPreset();
    void sceneRecordingArgsUseObservedFrameRate();
    void controllerRejectsUnsafeNamesBeforeLaunching();
};

void CaptureRunnerTest::gifArgsUseFixedPresentationPreset()
{
    lanista::CaptureSpec spec;
    spec.mp4Path = QStringLiteral("C:/captures/home.mp4");
    spec.gifPath = QStringLiteral("C:/captures/home.gif");
    const QStringList args = lanista::gifArgs(spec);

    QCOMPARE(args.at(args.indexOf(QStringLiteral("-i")) + 1), spec.mp4Path);
    const QString filter = args.at(args.indexOf(QStringLiteral("-filter_complex")) + 1);
    QVERIFY(filter.contains(QStringLiteral("fps=15")));
    QVERIFY(filter.contains(QStringLiteral("scale=1280:720")));
    QVERIFY(filter.contains(QStringLiteral("palettegen")));
    QVERIFY(filter.contains(QStringLiteral("paletteuse")));
    QCOMPARE(args.constLast(), spec.gifPath);
}

void CaptureRunnerTest::sceneRecordingArgsUseObservedFrameRate()
{
    lanista::CaptureSpec spec;
    spec.mp4Path = QStringLiteral("C:/captures/home.mp4");
    const QString pattern = QStringLiteral("C:/frames/frame-%06d.bmp");
    const QStringList args = lanista::sceneRecordingArgs(spec, pattern, 2.5);

    QCOMPARE(args.at(args.indexOf(QStringLiteral("-framerate")) + 1), QStringLiteral("2.500"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-i")) + 1), pattern);
    QVERIFY(args.contains(QStringLiteral(
        "scale=1280:720:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=1280:720:(ow-iw)/2:(oh-ih)/2:color=black")));
    QCOMPARE(args.constLast(), spec.mp4Path);
}

void CaptureRunnerTest::controllerRejectsUnsafeNamesBeforeLaunching()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    lanista::CaptureController controller(dir.path(), [](QString*) { return QImage(); });
    QString why;
    QVERIFY(!controller.start(QStringLiteral("../home"), &why));
    QVERIFY(why.contains(QStringLiteral("name")));
    QVERIFY(!controller.stop(&why));
    QVERIFY(why.contains(QStringLiteral("active")));
}

QTEST_APPLESS_MAIN(CaptureRunnerTest)
#include "tst_capture_runner.moc"
