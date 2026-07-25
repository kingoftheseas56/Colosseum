#include "host/HarnessHostServices.h"
#include "host/Player2SubtitleImageProvider.h"
#include "video/Player2VideoItem.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/qqml.h>
#include <QtQuick/QQuickWindow>

#include <iostream>

using namespace Colosseum::Player2;

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Colosseum"));
    QCoreApplication::setApplicationName(QStringLiteral("Player2Lab"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Player 2 lab: --scenario synthetic | --file PATH | --url URL [--report PATH]"));
    parser.addHelpOption();
    const QCommandLineOption scenarioOption(QStringLiteral("scenario"),
                                             QStringLiteral("Lab scenario to run"),
                                             QStringLiteral("name"));
    const QCommandLineOption reportOption(QStringLiteral("report"),
                                           QStringLiteral("Write deterministic JSON and exit"),
                                           QStringLiteral("path"));
    const QCommandLineOption fileOption(QStringLiteral("file"),
                                         QStringLiteral("Open a local media file"),
                                         QStringLiteral("path"));
    const QCommandLineOption urlOption(QStringLiteral("url"),
                                        QStringLiteral("Open an HTTP(S) stream URL"),
                                        QStringLiteral("url"));
    const QCommandLineOption headersOption(QStringLiteral("headers-json"),
                                            QStringLiteral("JSON object of request headers"),
                                            QStringLiteral("path"));
    const QCommandLineOption liveOption(QStringLiteral("live"),
                                         QStringLiteral("Treat the stream as live"));
    const QCommandLineOption soakOption(QStringLiteral("soak-seconds"),
                                         QStringLiteral("Minimum report-mode playback duration"),
                                         QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption normalizationOption(
        QStringLiteral("normalization"),
        QStringLiteral("Loudness mode: smooth | light | full"),
        QStringLiteral("mode"), QStringLiteral("smooth"));
    const QCommandLineOption speedOption(
        QStringLiteral("speed"),
        QStringLiteral("Playback speed (0.5-2.0); exercises the atempo path for the sync gate"),
        QStringLiteral("rate"), QStringLiteral("1.0"));
    const QCommandLineOption seekCountOption(
        QStringLiteral("seek-count"),
        QStringLiteral("Scripted deterministic seeks to run (Task 16 seek soak)"),
        QStringLiteral("count"), QStringLiteral("0"));
    const QCommandLineOption seekIntervalOption(
        QStringLiteral("seek-interval-ms"),
        QStringLiteral("Milliseconds between scripted seeks"),
        QStringLiteral("ms"), QStringLiteral("1500"));
    const QCommandLineOption cyclesOption(
        QStringLiteral("cycles"),
        QStringLiteral("Close/reopen cycles of the same file (Task 16 memory soak)"),
        QStringLiteral("count"), QStringLiteral("0"));
    const QCommandLineOption cycleDwellOption(
        QStringLiteral("cycle-dwell-seconds"),
        QStringLiteral("Seconds of playback between cycles"),
        QStringLiteral("seconds"), QStringLiteral("8"));
    parser.addOption(scenarioOption);
    parser.addOption(reportOption);
    parser.addOption(fileOption);
    parser.addOption(urlOption);
    parser.addOption(headersOption);
    parser.addOption(liveOption);
    parser.addOption(soakOption);
    parser.addOption(normalizationOption);
    parser.addOption(speedOption);
    parser.addOption(seekCountOption);
    parser.addOption(seekIntervalOption);
    parser.addOption(cyclesOption);
    parser.addOption(cycleDwellOption);
    parser.process(application);
    const int sourceCount = (parser.isSet(scenarioOption) ? 1 : 0) +
        (parser.isSet(fileOption) ? 1 : 0) + (parser.isSet(urlOption) ? 1 : 0);
    if (sourceCount != 1) {
        std::cerr << "choose exactly one of --scenario synthetic, --file PATH or --url URL\n";
        return 2;
    }

    HarnessHostServices host;
    host.setReportPath(parser.value(reportOption));
    host.setMinimumRunSeconds(parser.value(soakOption).toInt());
    const QString normalization = parser.value(normalizationOption).toLower();
    if (normalization == QStringLiteral("light"))
        host.setNormalizationMode(NormalizationMode::Light);
    else if (normalization == QStringLiteral("full"))
        host.setNormalizationMode(NormalizationMode::Full);
    else
        host.setNormalizationMode(NormalizationMode::Smooth);
    qmlRegisterType<Player2VideoItem>("Colosseum.Player2", 1, 0, "Player2VideoItem");

    QQmlApplicationEngine engine;
    // Serves PGS/DVD bitmap subtitle pictures to the QML SubtitleLayer (image://player2subtitle/<id>).
    engine.addImageProvider(QStringLiteral("player2subtitle"),
                            new Player2SubtitleImageProvider(host.playbackSession()));
    engine.rootContext()->setContextProperty(QStringLiteral("HarnessHost"), &host);
    const QUrl harnessUrl(QStringLiteral("qrc:/player2/Harness.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
                     [] { QCoreApplication::exit(4); }, Qt::QueuedConnection);
    engine.load(harnessUrl);
    if (engine.rootObjects().isEmpty())
        return 4;

    // Playback speed for this run; carried into the session so open() applies it from the first frame
    // (exercises the atempo path under the A/V sync gate).
    const double speed = parser.value(speedOption).toDouble();
    if (speed > 0.0 && speed != 1.0)
        host.setStartupSpeed(speed);
    // Task 16 soak scripts (scripted seeks / open-close cycles); report success requires completion.
    host.setSeekScript(parser.value(seekCountOption).toInt(),
                       parser.value(seekIntervalOption).toInt());
    host.setCycleScript(parser.value(cyclesOption).toInt(),
                        parser.value(cycleDwellOption).toInt());

    QString error;
    bool started = false;
    if (parser.isSet(urlOption))
        started = host.startUrl(parser.value(urlOption), parser.value(headersOption),
                                parser.isSet(liveOption), &error);
    else if (parser.isSet(fileOption))
        started = host.startFile(parser.value(fileOption), &error);
    else
        started = host.startScenario(parser.value(scenarioOption), &error);
    if (!started) {
        std::cerr << error.toStdString() << '\n';
        return 2;
    }
    return application.exec();
}
