#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include "vinyl/VinylLocalSource.h"
#include "vinyl/VinylBackendSession.h"
#include "engine/VaultIdentity.h"
class Setup : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    VaultIdentity *identity = nullptr;
    VinylLocalSource *source = nullptr;
    int identitySerial = 0;
public slots:
    void applicationAvailable() { QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL); }
    QString createIdentityAudio() {
        const QString path=directory.path()+QString("/identity-%1.wav").arg(++identitySerial);
        changeIdentityAudio(path,160);
        return path;
    }
    QString createTaggedAudio(const QString &title,const QString &artist,const QString &album) {
        const auto path=createIdentityAudio();changeIdentityAudio(path,16000);
        QByteArray info("INFO",4);
        const auto append=[&info](const char *key,const QString &value){
            const auto text=value.toUtf8()+char(0);QByteArray field;
            QDataStream bytes(&field,QIODevice::WriteOnly);bytes.setByteOrder(QDataStream::LittleEndian);
            bytes.writeRawData(key,4);bytes<<quint32(text.size());bytes.writeRawData(text.constData(),text.size());
            if(text.size()%2)bytes<<quint8(0);info+=field;
        };
        append("INAM",title);append("IART",artist);append("IPRD",album);
        QFile file(path);if(!file.open(QIODevice::ReadWrite))qFatal("Cannot tag audio fixture");
        file.seek(file.size());QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("LIST",4);stream<<quint32(info.size());stream.writeRawData(info.constData(),info.size());
        const auto length=file.size();file.seek(4);stream<<quint32(length-8);return path;
    }
    void changeIdentityAudio(const QString &path, int frames) {
        QFile file(path);if(!file.open(QIODevice::WriteOnly)) qFatal("Cannot write identity audio");
        QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF",4);stream << quint32(36+frames*2);stream.writeRawData("WAVEfmt ",8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data",4);stream << quint32(frames*2);
        for(int i=0;i<frames;++i) stream << qint16(i%2 ? 1000 : -1000);
    }
    void refreshManifest() {
        changeIdentityAudio(directory.path()+"/two.wav",16000);
        QFile manifest(directory.path()+"/record.vinyl.json");if(!manifest.open(QIODevice::WriteOnly)) qFatal("Cannot refresh bridge manifest");
        manifest.write(R"({"version":1,"artist":{"key":"artist","name":"Native Artist"},"releases":[{"key":"release","title":"Fresh Native Release","editions":[{"key":"standard","tracks":[{"file":"two.wav","title":"New Track","disc":1,"number":1}]}]}]})");manifest.close();
    }

    void qmlEngineAvailable(QQmlEngine *engine) {
        qmlRegisterType<VinylBackendSession>("Colosseum.Vinyl",1,0,"VinylBackendSession");
        engine->addImportPath(QStringLiteral(VINYL_CONTRACT_IMPORTS));
        QDir().mkpath(directory.path()+"/identity");
        changeIdentityAudio(directory.path()+"/one.wav",16000);
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
