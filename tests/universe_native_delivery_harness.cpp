#include "engine/SourceContentStore.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
#include <QDebug>
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString repo = QString::fromLocal8Bit(argv[1]);
    SourceContentStore content;
    content.configure(nullptr, QUrl("https://content.example"), {}, repo + "/assets/content-starter/bundle.json");
    if (content.document("universes-roster").value("FACETS").metaType().id() != QMetaType::QVariantList) return 3;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("Content", &content);
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit);
    QObject::connect(&engine, &QQmlEngine::exit, &app, [](int code) { QCoreApplication::exit(code); });
    QQmlComponent component(&engine, QUrl::fromLocalFile(repo + "/tests/universe_native_delivery_harness.qml"));
    auto* root = component.create();
    if (!root) { qCritical().noquote() << component.errorString(); return 4; }
    QTimer::singleShot(30000, &app, [] { QCoreApplication::exit(5); });
    const int result = app.exec();
    delete root;
    return result;
}
