#include "AudiobookTorrentEngineAdapter.h"
#include "../torrent/engine/TorrentEngine.h"
AudiobookTorrentEngineAdapter::AudiobookTorrentEngineAdapter(TorrentEngine* engine,QObject* parent)
    : IAudiobookTorrentEngine(parent), m_engine(engine) {
    connect(engine,&TorrentEngine::metadataReady,this,&IAudiobookTorrentEngine::changed,Qt::QueuedConnection);
    connect(engine,&TorrentEngine::torrentProgress,this,&IAudiobookTorrentEngine::changed,Qt::QueuedConnection);
    connect(engine,&TorrentEngine::torrentFinished,this,&IAudiobookTorrentEngine::changed,Qt::QueuedConnection);
    connect(engine,&TorrentEngine::torrentError,this,&IAudiobookTorrentEngine::changed,Qt::QueuedConnection);
    connect(engine,&TorrentEngine::torrentAddFailed,this,&IAudiobookTorrentEngine::changed,Qt::QueuedConnection);
    connect(engine,&TorrentEngine::ownedRemovalSettled,this,&IAudiobookTorrentEngine::removalSettled,Qt::QueuedConnection);
    connect(engine,&QObject::destroyed,this,[this] { emit unavailable(); });
}
bool AudiobookTorrentEngineAdapter::isRunning() const { return m_engine && m_engine->isRunning(); }
void AudiobookTorrentEngineAdapter::start() { if(m_engine) m_engine->start(); }
bool AudiobookTorrentEngineAdapter::hasTorrent(const QString& h) const { return m_engine && m_engine->hasTorrent(h); }
QString AudiobookTorrentEngineAdapter::addMagnet(const QString& m,const QString& p,bool only) { return m_engine ? m_engine->addMagnet(m,p,only) : QString(); }
QString AudiobookTorrentEngineAdapter::actualSavePath(const QString& h) const { return m_engine ? m_engine->torrentDetails(h).savePath : QString(); }
QJsonArray AudiobookTorrentEngineAdapter::files(const QString& h) const { return m_engine ? m_engine->torrentFiles(h) : QJsonArray(); }
QJsonArray AudiobookTorrentEngineAdapter::verifiedBytes(const QString& h) const { return m_engine ? m_engine->torrentFileProgress(h) : QJsonArray(); }
void AudiobookTorrentEngineAdapter::setPriorities(const QString& h,const QVector<int>& p) { if(m_engine) m_engine->setFilePriorities(h,p); }
void AudiobookTorrentEngineAdapter::startPayload(const QString& h) {
    // Clears metadata-only upload_mode; empty destination never moves live storage.
    if(m_engine) m_engine->startTorrent(h,{});
}
bool AudiobookTorrentEngineAdapter::requestRemoval(const QString& token,const QString& h,const QString& path,bool erase) { return m_engine && m_engine->requestOwnedRemoval(token,h,path,erase); }
