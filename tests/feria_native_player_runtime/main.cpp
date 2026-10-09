#include "../../native/player/mpvitem.h"
#include "../../native/feria/FeriaBrowserPolicy.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QtTest/QTest>
class RuntimeProbe : public QObject {
    Q_OBJECT
public:
    QQuickWindow *window=nullptr;
    Q_INVOKABLE bool capture(const QString &path) { return window && window->grabWindow().save(path); }
    Q_INVOKABLE void click(double x,double y) {
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window,2000)) return;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(x),qRound(y)));
    }
    Q_INVOKABLE void finish(const QString &path,const QVariantMap &result) {
        QFile file(path); if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument::fromVariant(result).toJson());
        QTimer::singleShot(300,qApp,[result]{qApp->exit(result.value("passed").toBool()?0:2);});
    }
};
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc,argv);
    if(argc!=4)return 64;
    qmlRegisterType<MpvItem>("Colosseum.Player",1,0,"MpvItem");
    FeriaBrowserPolicy::registerTypes();
    FeriaBrowserPolicy policy;
    QQmlApplicationEngine engine;
    RuntimeProbe probe;
    engine.rootContext()->setContextProperty("runtimeProbe",&probe);
    engine.rootContext()->setContextProperty("FeriaBrowserPolicy",&policy);
    engine.rootContext()->setContextProperty("fixtureUrl",QString::fromLocal8Bit(argv[2]));
    engine.rootContext()->setContextProperty("evidenceDirectory",QString::fromLocal8Bit(argv[3]));
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if(engine.rootObjects().isEmpty())return 1;
    probe.window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QTimer::singleShot(150000,&app,[]{qApp->exit(3);});
    return app.exec();
}
#include "main.moc"
