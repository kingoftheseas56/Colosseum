// Throwaway alpha-composition experiment. The underlay is not video.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QtTest/QTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

class Probe final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool startTransparent READ startTransparent CONSTANT)
public:
    bool startTransparent() const { return qEnvironmentVariableIsSet("ALPHA_START_TRANSPARENT"); }
    QQuickWindow *window = nullptr;
    QJsonArray snapshots;
    QString directory;
    explicit Probe(QString output) : directory(std::move(output)) { QDir().mkpath(directory); }
    Q_INVOKABLE void capture(const QString &name, const QVariantMap &dom, double zoom) {
        const QImage image = window->grabWindow();
        const QString filename = directory + '/' + name + ".png";
        if (image.isNull() || !image.save(filename)) {
            qCritical() << "CAPTURE FAIL" << name;
            QCoreApplication::exit(2);
            return;
        }
        QJsonObject frame = QJsonObject::fromVariantMap({
            {"name", name}, {"qtCompiledVersion", QString::fromLatin1(QT_VERSION_STR)},
            {"qtRuntimeVersion", QString::fromLatin1(qVersion())}, {"dom", dom}, {"zoom", zoom},
            {"windowWidth", window->width()}, {"windowHeight", window->height()},
            {"imageWidth", image.width()}, {"imageHeight", image.height()},
            {"devicePixelRatio", window->devicePixelRatio()},
            {"graphicsApi", int(window->rendererInterface()->graphicsApi())},
            {"exposed", window->isExposed()}
        });
        snapshots.append(frame);
        QFile report(directory + "/snapshots.json");
        if (!report.open(QIODevice::WriteOnly)) { QCoreApplication::exit(3); return; }
        report.write(QJsonDocument(snapshots).toJson());
        qInfo().noquote() << "CAPTURE" << name << image.size() << "exposed=" << window->isExposed()
                          << "graphicsApi=" << int(window->rendererInterface()->graphicsApi());
    }
    Q_INVOKABLE void click(double x, double y) {
        qInfo() << "NATIVE POINTER CLICK" << x << y;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(x), qRound(y)));
    }
    Q_INVOKABLE void done() { qInfo() << "CAPTURE SEQUENCE COMPLETE (pixel verifier required)"; QCoreApplication::quit(); }
};

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    Probe probe(argc > 1 ? QString::fromLocal8Bit(argv[1]) : "evidence");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("probe", &probe);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    probe.window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!probe.window) return 1;
    QObject::connect(probe.window, &QQuickWindow::sceneGraphInitialized, probe.window, [] {
        if (auto *gl = QOpenGLContext::currentContext()) {
            auto *f = gl->functions();
            qInfo() << "GL_VENDOR" << reinterpret_cast<const char *>(f->glGetString(GL_VENDOR));
            qInfo() << "GL_RENDERER" << reinterpret_cast<const char *>(f->glGetString(GL_RENDERER));
            qInfo() << "GL_VERSION" << reinterpret_cast<const char *>(f->glGetString(GL_VERSION));
        }
    }, Qt::DirectConnection);
    qInfo() << "START_TRANSPARENT" << probe.startTransparent();
    qInfo() << "QTWEBENGINE_CHROMIUM_FLAGS" << qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qInfo() << "Qt compiled" << QT_VERSION_STR << "runtime" << qVersion() << "platform" << app.platformName()
            << "AA_ShareOpenGLContexts" << QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts);
    QTimer::singleShot(45000, &app, [&app] { qCritical() << "PROBE TIMEOUT"; app.exit(4); });
    return app.exec();
}
#include "main.moc"
