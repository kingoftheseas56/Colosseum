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
        QStringLiteral("Player 2 lab: --scenario synthetic [--report PATH]"));
    parser.addHelpOption();
    const QCommandLineOption scenarioOption(QStringLiteral("scenario"),
                                             QStringLiteral("Lab scenario to run"),
                                             QStringLiteral("name"));
    const QCommandLineOption reportOption(QStringLiteral("report"),
                                           QStringLiteral("Write deterministic JSON and exit"),
                                           QStringLiteral("path"));
    parser.addOption(scenarioOption);
    parser.addOption(reportOption);
    parser.process(application);
    if (!parser.isSet(scenarioOption)) {
        std::cerr << "missing required --scenario synthetic\n";
        return 2;
    }

    HarnessHostServices host;
    host.setReportPath(parser.value(reportOption));
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
    if (!host.startScenario(parser.value(scenarioOption), &error)) {
        std::cerr << error.toStdString() << '\n';
        return 2;
    }
    return application.exec();
}
