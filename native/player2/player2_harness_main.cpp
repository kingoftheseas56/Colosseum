#include "host/HarnessHostServices.h"
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
    parser.setApplicationDescription(
        QStringLiteral("Player 2 lab: --scenario synthetic | --file PATH [--report PATH]"));
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
    const QCommandLineOption soakOption(QStringLiteral("soak-seconds"),
                                         QStringLiteral("Minimum report-mode playback duration"),
                                         QStringLiteral("seconds"), QStringLiteral("0"));
    parser.addOption(scenarioOption);
    parser.addOption(reportOption);
    parser.addOption(fileOption);
    parser.addOption(soakOption);
    parser.process(application);
    if (parser.isSet(scenarioOption) == parser.isSet(fileOption)) {
        std::cerr << "choose exactly one of --scenario synthetic or --file PATH\n";
        return 2;
    }

    HarnessHostServices host;
    host.setReportPath(parser.value(reportOption));
    host.setMinimumRunSeconds(parser.value(soakOption).toInt());
    qmlRegisterType<Player2VideoItem>("Colosseum.Player2", 1, 0, "Player2VideoItem");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("HarnessHost"), &host);
    const QUrl harnessUrl(QStringLiteral("qrc:/player2/Harness.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
                     [] { QCoreApplication::exit(4); }, Qt::QueuedConnection);
    engine.load(harnessUrl);
    if (engine.rootObjects().isEmpty())
        return 4;

    QString error;
    const bool started = parser.isSet(fileOption)
        ? host.startFile(parser.value(fileOption), &error)
        : host.startScenario(parser.value(scenarioOption), &error);
    if (!started) {
        std::cerr << error.toStdString() << '\n';
        return 2;
    }
    return application.exec();
}
