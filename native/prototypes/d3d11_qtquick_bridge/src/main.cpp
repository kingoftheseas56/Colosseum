#include "video_bridge_item.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("D3D11 Qt Quick Bridge"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("source"), QStringLiteral("Frame source (synthetic only in Gate A)"),
                      QStringLiteral("source"), QStringLiteral("synthetic")});
    parser.addOption({QStringLiteral("duration"), QStringLiteral("Automatic run duration in seconds"),
                      QStringLiteral("seconds"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("report"), QStringLiteral("Write JSON report on exit"),
                      QStringLiteral("path")});
    parser.process(app);
    if (parser.value(QStringLiteral("source")) != QStringLiteral("synthetic"))
        qFatal("Gate A accepts only --source synthetic");

    qmlRegisterType<VideoBridgeItem>("Colosseum.Bridge", 1, 0, "VideoBridgeItem");
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;

    auto *item = engine.rootObjects().first()->findChild<VideoBridgeItem *>(QStringLiteral("bridgeItem"));
    const QString reportPath = parser.value(QStringLiteral("report"));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [item, reportPath] {
        if (item && !reportPath.isEmpty())
            item->writeReport(reportPath);
    });

    bool ok = false;
    const int duration = parser.value(QStringLiteral("duration")).toInt(&ok);
    if (ok && duration > 0)
        QTimer::singleShot(duration * 1000, &app, &QCoreApplication::quit);
    return app.exec();
}
