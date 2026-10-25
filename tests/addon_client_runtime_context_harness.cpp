#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <cstdio>

class PlatformRuntimeFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool android READ android CONSTANT)
    Q_PROPERTY(QVariantMap videoDecodeCapabilities READ videoDecodeCapabilities CONSTANT)

public:
    bool android() const { return true; }

    QVariantMap videoDecodeCapabilities() const {
        const QVariantList profiles {
            QVariantMap {{"profile", "main"}, {"level", 0}},
            QVariantMap {{"profile", "main-still"}, {"level", 0}}
        };
        const QVariantMap decoder {
            {"name", "fixture-t20-hevc"},
            {"mime", "video/hevc"},
            {"profiles", profiles},
            {"profilesComplete", true}
        };
        return {
            {"available", true},
            {"complete", true},
            {"decoders", QVariantList {decoder}}
        };
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        qCritical() << "usage: addon_client_runtime_context_harness <qml-fixture>";
        return 2;
    }

    QQmlEngine engine;
    engine.addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    PlatformRuntimeFixture runtime;
    engine.rootContext()->setContextProperty("PlatformRuntime", &runtime);

    QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    QObject *root = component.create();
    if (!root) {
        qCritical().noquote() << component.errorString();
        return 2;
    }

    const bool runtimeVisible = root->property("runtimeVisibleHere").toBool();
    const QVariantMap compatibility = root->property("compatibility").toMap();
    const QString state = compatibility.value("state").toString();
    const QString reason = compatibility.value("reason").toString();
    std::printf("rootRuntimeVisible=%s state=%s reason=%s\n",
                runtimeVisible ? "true" : "false", qPrintable(state), qPrintable(reason));

    const bool passed = runtimeVisible && state == QStringLiteral("unsupported");
    delete root;
    return passed ? 0 : 1;
}

#include "addon_client_runtime_context_harness.moc"
