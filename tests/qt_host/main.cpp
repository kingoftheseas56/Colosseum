// Test-only native QQml context injector, never linked into Colosseum.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QDebug>
#include <cstdio>
#include <cstdlib>
static void traceMessage(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QByteArray utf8 = msg.toUtf8();
    std::fprintf(stderr, "%s\n", utf8.constData());
    std::fflush(stderr);
    if (type == QtFatalMsg) std::abort();
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "Usage: discover_qml_host <QML-file> <on|off>\n");
        return 2;
    }
    qInstallMessageHandler(traceMessage);
    QQmlApplicationEngine engine;
    const QString mode = QString::fromLocal8Bit(argv[2]);
    if (mode != "on" && mode != "off") return 2;
    engine.rootContext()->setContextProperty("CatalogueTraceEnabled", mode == "on");
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "QML root did not load\n");
        return 3;
    }
    return app.exec();
}
