#include "bridge.h"
#include "mpvitem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QCommandLineParser>
#include <QSet>
#include <QTemporaryDir>
#include <QFile>
class LocalOnly final : public QWebEngineUrlRequestInterceptor {
    QSet<QUrl> allowed;
public:
    explicit LocalOnly(Bridge *b, const QString &pageDirectory) {
        for (const auto &id : {"h264", "ffv1"}) allowed.insert(QUrl::fromLocalFile(b->fixturePath(id)));
        allowed.insert(QUrl("qrc:///qtwebchannel/qwebchannel.js"));
        for (const auto &name : {"index.html", "adapter.js", "selftest.js"}) allowed.insert(QUrl::fromLocalFile(pageDirectory + "/" + name));
    }
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        QUrl url = info.requestUrl(); url.setFragment({});
        if (!allowed.contains(url)) { qWarning() << "BLOCKED resource" << url; info.block(true); }
    }
};
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    app.setApplicationName("feria-html-mpv-composition");
    QCommandLineParser cli; cli.addHelpOption();
    cli.addOptions({QCommandLineOption("fixtures", "Explicit fixture directory", "directory"), QCommandLineOption("evidence", "Evidence directory", "directory", "evidence"), QCommandLineOption("self-test", "Run finite controlled HTML interaction tests")});
    cli.process(app);
    if (!cli.isSet("fixtures")) { qCritical() << "--fixtures is required"; return 1; }
    qInfo() << "Creating isolated bridge; Qt compiled" << QT_VERSION_STR << "runtime" << qVersion();
    Bridge bridge(cli.value("fixtures"), cli.isSet("self-test"));
    PlayerFacade facade(&bridge);
    TestProbe probe(&bridge, cli.value("evidence"));
    QTemporaryDir pageDirectory;
    if (!pageDirectory.isValid()) return 1;
    for (const auto &name : {"index.html", "adapter.js", "selftest.js"})
        if (!QFile::copy(QString(":/") + name, pageDirectory.path() + "/" + name)) return 1;
    LocalOnly interceptor(&bridge, pageDirectory.path());
    qInfo() << "Creating off-the-record local-only WebEngine profile";
    QQuickWebEngineProfile profile; profile.setUrlRequestInterceptor(&interceptor);
    QQmlApplicationEngine engine;
    qmlRegisterType<MpvItem>("Experiment.Player", 1, 0, "MpvItem");
    engine.rootContext()->setContextProperty("trustedPageUrl", QUrl::fromLocalFile(pageDirectory.path()+"/index.html"));
    engine.rootContext()->setContextProperty("nativeHost", &bridge);
    engine.rootContext()->setContextProperty("playerFacade", &facade);
    engine.rootContext()->setContextProperty("testProbe", &probe);
    engine.rootContext()->setContextProperty("testEnabled", cli.isSet("self-test"));
    engine.rootContext()->setContextProperty("localProfile", &profile);
    qInfo() << "Loading QML composition";
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    bridge.window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(bridge.window, &QQuickWindow::frameSwapped, &bridge, [&bridge]{ ++bridge.renderedFrames; });
    qInfo() << "Qt compile" << QT_VERSION_STR << "runtime" << qVersion() << "platform" << app.platformName() << "MpvItem unchanged; OpenGL composition; null audio output";
    if (cli.isSet("self-test")) QTimer::singleShot(110000, &probe, [&probe]{ probe.fail("110 second watchdog"); });
    const int result = app.exec();
    bridge.navigate();
    return result;
}
