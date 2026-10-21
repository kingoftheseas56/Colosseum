#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QFile>
#include <QDateTime>
#include <memory>

// Replaceable, bounded subprocess transport. Only public catalogue metadata is
// cached; playback URLs/headers never cross the durable-cache boundary.
class VinylProvider : public QObject {
    Q_OBJECT
public:
    explicit VinylProvider(QString helper, QObject *parent=nullptr):QObject(parent),m_helper(std::move(helper)) {}
    ~VinylProvider() override {cancelAll();}
    Q_INVOKABLE int request(const QString &lane,const QString &operation,const QString &value) {
        return requestPayload(lane,operation,value,{});
    }
    Q_INVOKABLE int requestPayload(const QString &lane,const QString &operation,const QString &value,const QVariantMap &payload) {
        static const QStringList operations={"search","catalogueSearch","discover","playlist","album","resolve","artist","artistReleases","artistVideos","artistIdentity","artistArtwork"};
        static const QStringList lanes={"browse","queue","play","playFallback","discover-new-releases","discover-trending","discover-artists","discover-charts","artist","artist-albums","artist-singles","artist-videos","artist-identity","artist-artwork"};
        if(!operations.contains(operation)||!lanes.contains(lane)||value.size()>300)return -1;
        cancel(lane);const int id=++m_serial;
        QJsonObject input=QJsonObject::fromVariantMap(payload);input["operation"]=operation;input["value"]=value;
        const auto requestBytes=QJsonDocument(input).toJson(QJsonDocument::Compact);
        if(requestBytes.size()>1024*1024)return -1;
        const bool cacheable=operation!="resolve";
        auto cacheInput=input;cacheInput.remove("refresh");
        const auto cachePath=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/vinyl/catalogue/"+
            QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(cacheInput).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())+".json";
        auto *process=new QProcess(this);m_active.insert(lane,process);
        auto output=std::make_shared<QByteArray>();
        auto finish=[this,process,lane,id,cacheable,cachePath](QVariant data,QString error) {
            if(m_active.value(lane)!=process)return;
            m_active.remove(lane);process->disconnect(this);
            if(process->state()!=QProcess::NotRunning)process->kill();
            process->deleteLater();
            if(cacheable){
                if(error.isEmpty()&&!data.toMap().value("_cached").toBool()){
                    QDir().mkpath(QFileInfo(cachePath).absolutePath());QSaveFile file(cachePath);
                    const auto bytes=QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
                    if(bytes.size()<=4*1024*1024&&file.open(QIODevice::WriteOnly)){file.write(bytes);file.commit();}
                }else if(!error.isEmpty()){
                    QFile file(cachePath);
                    if(file.size()<=4*1024*1024&&file.open(QIODevice::ReadOnly)){
                        auto cached=QJsonDocument::fromJson(file.readAll());
                        if(cached.isObject()){auto map=cached.object().toVariantMap();map["_cached"]=true;map["_cacheError"]=error;data=map;error.clear();}
                    }
                }
            }
            emit completed(lane,id,data,error);
        };
        auto *deadline=new QTimer(process);deadline->setSingleShot(true);
        connect(deadline,&QTimer::timeout,this,[finish]{finish({},"The music catalogue took too long. Retry when connected.");});
        connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish({},"The music catalogue runtime is unavailable. Local music is still available.");});
        connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
        connect(process,&QProcess::readyReadStandardOutput,this,[process,output,finish]{
            output->append(process->readAllStandardOutput());if(output->size()>4*1024*1024)finish({},"The catalogue response exceeded its limit.");
        });
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[process,output,finish](int code,QProcess::ExitStatus status){
            output->append(process->readAllStandardOutput());QJsonParseError parse;
            auto doc=QJsonDocument::fromJson(*output,&parse);auto object=doc.object();
            if(output->size()>4*1024*1024||code!=0||status!=QProcess::NormalExit||parse.error!=QJsonParseError::NoError||!object.value("ok").isBool())finish({},"The music catalogue helper failed.");
            else if(!object.value("ok").toBool())finish({},object.value("error").toString("The music catalogue is unavailable."));
            else finish(object.value("data").toVariant(),{});
        });
#ifdef Q_OS_WIN
        const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/python.exe";
#else
        const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/bin/python3";
#endif
        const auto python=qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON",bundled);
        if(operation=="discover"&&!payload.value("refresh").toBool()){
            QFile cache(cachePath);const auto info=QFileInfo(cache);
            const auto age=info.lastModified().secsTo(QDateTime::currentDateTime());
            if(age>=0&&age<=900&&info.size()<=4*1024*1024&&cache.open(QIODevice::ReadOnly)){
                const auto doc=QJsonDocument::fromJson(cache.readAll());
                if(doc.isObject()){
                    auto map=doc.object().toVariantMap();map["_cached"]=true;
                    QTimer::singleShot(0,process,[finish,map]{finish(map,{});});return id;
                }
            }
        }
        // Explicit development override or app-owned runtime, never PATH python.
        // FailedToStart may be emitted inside start() on Windows. Defer launch
        // so callers can install their request fence before any completion.
        QTimer::singleShot(0,process,[this,process,deadline,lane,python,requestBytes,operation]{
            if(m_active.value(lane)!=process)return;
            deadline->start(operation=="discover"?18000:operation=="artistIdentity"?180000:90000);
            process->start(python,{"-I",m_helper});
            if(m_active.value(lane)==process){process->write(requestBytes);process->closeWriteChannel();}
        });return id;
    }
    Q_INVOKABLE void cancel(const QString &lane){
        auto *process=m_active.take(lane);if(!process)return;
        process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();
    }
    Q_INVOKABLE void cancelAll(){for(const auto &lane:m_active.keys())cancel(lane);}
signals:
    void completed(const QString &lane,int requestId,const QVariant &data,const QString &error);
    void profileLeaving();void profileEntered();void checkpointRequested();void shuttingDown();
private:
    QString m_helper;int m_serial=0;QHash<QString,QProcess *> m_active;
};
