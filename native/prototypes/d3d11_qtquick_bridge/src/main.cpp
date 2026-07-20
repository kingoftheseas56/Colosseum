#include "video_bridge_item.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("D3D11 Qt Quick Bridge"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("source"), QStringLiteral("Frame source: synthetic or hevc"),
                      QStringLiteral("source"), QStringLiteral("synthetic")});
    parser.addOption({QStringLiteral("file"), QStringLiteral("Media file for --source hevc"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("duration"), QStringLiteral("Automatic run duration in seconds"),
                      QStringLiteral("seconds"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("report"), QStringLiteral("Write JSON report on exit"),
                      QStringLiteral("path")});
    parser.process(app);
    const QString source = parser.value(QStringLiteral("source"));
    if (source != QStringLiteral("synthetic") && source != QStringLiteral("hevc"))
        qFatal("--source must be synthetic or hevc");
    if (source == QStringLiteral("hevc") && parser.value(QStringLiteral("file")).isEmpty())
        qFatal("--source hevc requires --file");
    if (source == QStringLiteral("hevc") &&
        !QFileInfo::exists(parser.value(QStringLiteral("file"))))
        qFatal("--file does not exist");

    qmlRegisterType<VideoBridgeItem>("Colosseum.Bridge", 1, 0, "VideoBridgeItem");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("bridgeSource"), source);
    engine.rootContext()->setContextProperty(QStringLiteral("bridgeFile"),
                                              parser.value(QStringLiteral("file")));
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
