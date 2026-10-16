#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "vinyl/VinylLocalSource.h"
#include "engine/VaultIdentity.h"
class Setup : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    VaultIdentity *identity = nullptr;
    VinylLocalSource *source = nullptr;
public slots:
    void refreshManifest() {
        QFile audio(directory.path()+"/two.wav");if(!audio.open(QIODevice::WriteOnly)) qFatal("Cannot create bridge audio");audio.write("RIFF refreshed fixture");audio.close();
        QFile manifest(directory.path()+"/record.vinyl.json");if(!manifest.open(QIODevice::WriteOnly)) qFatal("Cannot refresh bridge manifest");
        manifest.write(R"({"version":1,"artist":{"key":"artist","name":"Native Artist"},"releases":[{"key":"release","title":"Fresh Native Release","editions":[{"key":"standard","tracks":[{"file":"two.wav","title":"New Track","disc":1,"number":1}]}]}]})");manifest.close();
    }

    void qmlEngineAvailable(QQmlEngine *engine) {
        QDir().mkpath(directory.path()+"/identity");
        QFile audio(directory.path()+"/one.wav");if(!audio.open(QIODevice::WriteOnly)) qFatal("Cannot create bridge audio");audio.write("RIFF source bridge fixture");audio.close();
        QFile manifest(directory.path()+"/record.vinyl.json");if(!manifest.open(QIODevice::WriteOnly)) qFatal("Cannot create bridge manifest");
        manifest.write(R"({"version":1,"artist":{"key":"artist","name":"Native Artist"},"releases":[{"key":"release","title":"Native Release","editions":[{"key":"standard","tracks":[{"file":"one.wav","title":"Native Track","disc":1,"number":1,"seconds":2}]}]}]})");manifest.close();
        identity=new VaultIdentity(directory.path()+"/identity");
        source=new VinylLocalSource(identity,this);
        engine->rootContext()->setContextProperty("NativeSource",source);
        engine->rootContext()->setContextProperty("BridgeFixtures",this);
        engine->rootContext()->setContextProperty("ManifestPath",manifest.fileName());
    }
};
QUICK_TEST_MAIN_WITH_SETUP(vinyl_native_session_bridge,Setup)
#include "runner.moc"
