#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include "ProgressStore.h"
#include "CollectionStore.h"
#include "player/mpvitem.h"
#include "vinyl/VinylProvider.h"

class ProbeControl : public QObject {
    Q_OBJECT
signals:
    void switchProfile(int index);
    void capture();
};

int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        fprintf(stderr, "%s\n", message.toUtf8().constData()); fflush(stderr);
    });
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ColosseumTests");
    app.setApplicationName("VinylLiveHarness");
    const auto args=app.arguments();
    if (args.size()<3) {qCritical("Usage: vinyl_live_harness <isolated-state-dir> <interactive|play|resume>");return 2;}
    const QString state=QDir(args[1]).absolutePath();
    QDir().mkpath(state);
    VinylProvider provider(args[2]=="contracts" ? QStringLiteral(VINYL_SOURCE_ROOT "/tests/vinyl-live/fake_provider.py")
                                               : QStringLiteral(VINYL_SOURCE_ROOT "/scripts/vinyl/provider.py"));
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&provider,&VinylProvider::shuttingDown);
    ProgressStore progress(state+"/progress.ini");
    CollectionStore collection(state+"/collection.ini");
    ProgressStore otherProgress(state+"/other-progress.ini");
    CollectionStore otherCollection(state+"/other-collection.ini");
    ProbeControl control;
    qmlRegisterType<MpvItem>("Colosseum.Player",1,0,"MpvItem");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Progress",&progress);
    engine.rootContext()->setContextProperty("Collection",&collection);
    engine.rootContext()->setContextProperty("VinylProvider",&provider);
    engine.rootContext()->setContextProperty("VinylLiveEnabled",true);
    engine.rootContext()->setContextProperty("ProbeScenario",args[2]);
    engine.rootContext()->setContextProperty("ProbeControl",&control);
    QObject::connect(&control,&ProbeControl::switchProfile,&engine,[&](int index) {
        emit provider.profileLeaving();
        engine.rootContext()->setContextProperty("Progress",index ? &otherProgress : &progress);
        engine.rootContext()->setContextProperty("Collection",index ? &otherCollection : &collection);
        emit provider.profileEntered();
    });
    engine.load(QUrl::fromLocalFile(QStringLiteral(VINYL_SOURCE_ROOT "/tests/vinyl-live/LiveHarness.qml")));
    if (engine.rootObjects().isEmpty()) return 2;
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(&control,&ProbeControl::capture,&engine,[&] {
        if(window) window->grabWindow().save(state+"/last-frame.png");
    });
    if (args[2]!="interactive") QTimer::singleShot(120000,&app,[]{qCritical("VINYL_PROBE_TIMEOUT");QCoreApplication::exit(3);});
    const int result=app.exec();
    progress.flush();
    return result;
}

#include "main.moc"
