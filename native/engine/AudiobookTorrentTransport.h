#pragma once
#include "AudiobookTorrentEngineAdapter.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QStringList>
#include <memory>
class AudiobookTorrentTransport final : public QObject {
    Q_OBJECT
public:
    struct Timeouts { int metadataMs=120000; int inactivityMs=120000; int pollMs=250; };
    explicit AudiobookTorrentTransport(IAudiobookTorrentEngine*,QObject* parent=nullptr);
    AudiobookTorrentTransport(IAudiobookTorrentEngine*,Timeouts,QObject* parent=nullptr);
    ~AudiobookTorrentTransport() override;
    void start(quint64 generation,const QString& hash,const QString& privateRoot);
    void cancel(quint64 generation);
signals:
    void phaseChanged(quint64 generation,const QString& phase);
    void progress(quint64 generation,qint64 received,qint64 total);
    void completed(quint64 generation,const QString& directory,const QStringList& files,qint64 bytes);
    void cancelled(quint64 generation);
    void failed(quint64 generation,const QString& reason);
private:
    struct File { int index=0; QString path; qint64 bytes=0; };
    struct Attempt {
        quint64 generation=0; QString hash,path; QList<File> selected;
        bool added=false,admitted=false,validManifest=false,inAdd=false;
        bool ending=false,cancelRequested=false,settlementWarned=false;
        QString phase="resolving",token,failure;QStringList manifestPaths,resultFiles;
        qint64 total=0,received=-1; QElapsedTimer age,activity,settling;
    };
    bool current(quint64 generation) const;
    bool owns(const Attempt&) const;
    bool setPhase(quint64,const QString&);
    void admit(quint64,const QJsonArray&);
    void poll(quint64);
    void end(quint64,const QString& failure={},bool cancel=false,const QStringList& result={});
    void requestSettlement(quint64);
    void finish(bool success,int errorCode);
    void engineLost();
    QPointer<IAudiobookTorrentEngine> m_engine;
    Timeouts m_timeouts;
    QTimer m_poll;
    std::unique_ptr<Attempt> m_attempt;
};
