#include "AudiobookTorrentTransport.h"
#include "../torrent/BookTorrentMagnet.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <QCollator>
#include <QDebug>
#include <algorithm>
#include <limits>
namespace {
QString canonicalDirectory(const QString& path) { return QFileInfo(path).canonicalFilePath(); }
bool contained(const QString& root,const QString& relative) {
    QString cursor=root;
    for(const QString& part:relative.split('/')) {
        cursor=QDir(cursor).filePath(part);const QFileInfo info(cursor);
        if(info.isSymLink() || (info.exists() && !info.canonicalFilePath().startsWith(root+'/'))) return false;
    }
    return true;
}
bool audio(const QString& path) {
    static const QSet<QString> extensions={"m4b","mp3","m4a","flac","ogg","opus","aac","wav"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
// The consumer can die/cancel during addMagnet. Retire a positively-owned late
// admission using captured values, without dereferencing that consumer again.
void retireLateAdmission(QPointer<IAudiobookTorrentEngine> engine,const QString& hash,const QString& path,const QString& added) {
    if(!engine || added!=hash || !engine->hasTorrent(hash)) return;
    if(!engine || canonicalDirectory(engine->actualSavePath(hash))!=path) return;
    if(engine) engine->requestRemoval(QUuid::createUuid().toString(QUuid::WithoutBraces),hash,path,false);
}
}
AudiobookTorrentTransport::AudiobookTorrentTransport(IAudiobookTorrentEngine* e,QObject* parent)
 : AudiobookTorrentTransport(e,Timeouts{},parent) {}
AudiobookTorrentTransport::AudiobookTorrentTransport(IAudiobookTorrentEngine* e,Timeouts t,QObject* parent)
 : QObject(parent),m_engine(e),m_timeouts(t) {
    connect(&m_poll,&QTimer::timeout,this,[this] { if(m_attempt) poll(m_attempt->generation); });
    if(e) {
        connect(e,&IAudiobookTorrentEngine::changed,this,[this](const QString& hash) {
            if(m_attempt && (hash.isEmpty() || hash==m_attempt->hash)) poll(m_attempt->generation);
        },Qt::QueuedConnection);
        connect(e,&IAudiobookTorrentEngine::removalSettled,this,[this](const QString& token,bool success,int error) {
            if(m_attempt && !m_attempt->token.isEmpty() && m_attempt->token==token) finish(success,error);
        },Qt::QueuedConnection);
        connect(e,&IAudiobookTorrentEngine::unavailable,this,&AudiobookTorrentTransport::engineLost);
        connect(e,&QObject::destroyed,this,&AudiobookTorrentTransport::engineLost);
    }
}
AudiobookTorrentTransport::~AudiobookTorrentTransport() { if(m_attempt) cancel(m_attempt->generation); }
bool AudiobookTorrentTransport::current(quint64 g) const {return m_attempt && m_attempt->generation==g;}
bool AudiobookTorrentTransport::owns(const Attempt& a) const {
    // Copy all state before external methods, which may synchronously cancel us.
    const QString hash=a.hash,path=a.path;const bool added=a.added;QPointer<IAudiobookTorrentEngine> engine=m_engine;
    if(!added || !engine || path.isEmpty() || !engine->hasTorrent(hash)) return false;
    return engine && canonicalDirectory(engine->actualSavePath(hash))==path;
}
bool AudiobookTorrentTransport::setPhase(quint64 g,const QString& phase) {
    if(!current(g)) return false;
    m_attempt->phase=phase;QPointer<AudiobookTorrentTransport> self(this);
    emit phaseChanged(g,phase);return self && current(g) && m_attempt->phase==phase;
}
void AudiobookTorrentTransport::engineLost() {
    if(!m_attempt)return;
    m_poll.stop();auto retired=std::move(m_attempt);
    emit failed(retired->generation,retired->ending
        ? QStringLiteral("Audiobook engine stopped before cleanup was confirmed; removal outcome is unknown.")
        : QStringLiteral("Audiobook torrent engine is unavailable."));
}
void AudiobookTorrentTransport::cancel(quint64 g) {
    if(!current(g) || m_attempt->ending) return; // a requested KEEP may finish normally
    end(g,{},true);
}
void AudiobookTorrentTransport::end(quint64 g,const QString& failure,bool cancel,const QStringList& result) {
    if(!current(g) || m_attempt->ending) return;
    m_attempt->ending=true;m_attempt->failure=failure;m_attempt->cancelRequested=cancel;m_attempt->resultFiles=result;
    m_attempt->settling.start();
    if(!setPhase(g,result.isEmpty()?QStringLiteral("stopping"):QStringLiteral("finishing"))) return;
    if(m_attempt->inAdd)return; // the captured late admission must return before we can retire it
    requestSettlement(g);
}
void AudiobookTorrentTransport::requestSettlement(quint64 g) {
    if(!current(g) || !m_attempt->ending || !m_attempt->token.isEmpty() || m_attempt->inAdd)return;
    if(!m_attempt->added){finish(true,0);return;}
    const Attempt snapshot=*m_attempt;QPointer<AudiobookTorrentTransport> self(this);
    const bool owner=owns(snapshot);
    if(!self || !current(g))return;
    if(!owner) {
        if(!m_attempt->settlementWarned) {
            m_attempt->settlementWarned=true;
            qWarning()<<"[AudiobookDownloader] cleanup ownership could not be confirmed; keeping job live";
        }
        return; // unknown outcome is live, never permission to sweep directories
    }
    bool erase=snapshot.resultFiles.isEmpty() && snapshot.validManifest;
    for(const QString& path:snapshot.manifestPaths)erase=erase && contained(snapshot.path,path);
    const QString token=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_attempt->token=token;QPointer<IAudiobookTorrentEngine> engine=m_engine;
    const bool accepted=engine && engine->requestRemoval(token,snapshot.hash,snapshot.path,erase);
    if(!self || !current(g))return;
    if(!accepted) {
        m_attempt->token.clear();
        if(!m_attempt->settlementWarned){m_attempt->settlementWarned=true;qWarning()<<"[AudiobookDownloader] owned cleanup request rejected; outcome remains unknown";}
    }
}
void AudiobookTorrentTransport::finish(bool success,int error) {
    if(!m_attempt)return;
    m_poll.stop();auto retired=std::move(m_attempt);
    if(!success) {emit failed(retired->generation,QStringLiteral("Audiobook file cleanup failed (error %1).").arg(error));return;}
    if(!retired->failure.isEmpty()){emit failed(retired->generation,retired->failure);return;}
    if(retired->cancelRequested){emit cancelled(retired->generation);return;}
    emit completed(retired->generation,retired->path,retired->resultFiles,retired->total);
}
void AudiobookTorrentTransport::start(quint64 g,const QString& hash,const QString& root) {
    if(m_attempt)return;
    m_attempt=std::make_unique<Attempt>();m_attempt->generation=g;m_attempt->hash=hash;m_attempt->age.start();m_attempt->activity.start();
    QTimer::singleShot(0,this,[this,g,hash,root] {
        if(!current(g))return;QPointer<AudiobookTorrentTransport> self(this);QPointer<IAudiobookTorrentEngine> engine=m_engine;
        if(!engine){end(g,"Audiobook torrent engine is unavailable.");return;}
        const bool busy=engine->hasTorrent(hash);
        if(!self || !current(g))return;
        if(busy){end(g,"This torrent is already in use by another download.");return;}
        if(!QDir().mkpath(root) || QFileInfo(root).isSymLink()){end(g,"Cannot prepare audiobook storage.");return;}
        const QString name="attempt-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        if(!QDir(root).mkdir(name)){end(g,"Cannot prepare audiobook storage.");return;}
        const QString path=canonicalDirectory(QDir(root).filePath(name));m_attempt->path=path;
        if(path.isEmpty()){end(g,"Cannot prepare audiobook storage.");return;}
        const bool running=engine->isRunning();if(!self || !current(g) || m_attempt->ending)return;
        if(!running)engine->start();if(!self || !current(g) || m_attempt->ending)return;
        m_attempt->inAdd=true;
        const QString added=engine ? engine->addMagnet(BookTorrentMagnet::buildMagnet(hash),path,true) : QString();
        if(!self){retireLateAdmission(engine,hash,path,added);return;}
        if(!current(g)){retireLateAdmission(engine,hash,path,added);return;}
        m_attempt->inAdd=false;m_attempt->added=added==hash;
        if(!m_attempt->added){if(m_attempt->ending)finish(true,0);else end(g,"Torrent engine rejected the audiobook request.");return;}
        const bool owner=owns(*m_attempt);if(!self || !current(g))return;
        if(!owner){m_attempt->added=false;if(m_attempt->ending){m_attempt->failure="Torrent storage is owned elsewhere; no files were deleted.";finish(true,0);}else end(g,"Torrent storage is already owned elsewhere; audiobook request stopped.");return;}
        m_poll.start(qMax(1,m_timeouts.pollMs));
        if(m_attempt->ending)requestSettlement(g);else poll(g);
    });
}
void AudiobookTorrentTransport::admit(quint64 g,const QJsonArray& files) {
    if(!current(g) || m_attempt->ending)return;
    if(files.size()>20000){end(g,"Audiobook file manifest is invalid.");return;}
    const QString root=m_attempt->path;QSet<QString> paths;QVector<int> priorities(files.size(),0);QList<File> selected;QStringList allPaths;qint64 total=0;
    for(int i=0;i<files.size();++i) {
        const auto row=files[i].toObject();QString path=row.value("name").toString();path.replace('\\','/');
        const qint64 size=row.value("size").toVariant().toLongLong();const auto parts=path.split('/');
        if(!row.value("index").isDouble() || row.value("index").toDouble()!=i || !row.value("size").isDouble()
           || row.value("size").toDouble()!=double(size) || path.isEmpty() || path.size()>2048 || QDir::isAbsolutePath(path)
           || path.contains(':') || path.contains(QChar::Null) || parts.contains("..") || parts.contains(".") || parts.contains("")
           || size<0 || paths.contains(path.toCaseFolded()) || !contained(root,path)) {
            end(g,"Audiobook file manifest contains an invalid storage path or index.");return;
        }
        paths.insert(path.toCaseFolded());allPaths.append(path);if(!audio(path))continue;
        if(size<=0 || size>std::numeric_limits<qint64>::max()-total){end(g,"Audiobook file sizes are invalid.");return;}
        selected.append({i,QDir(root).filePath(path),size});total+=size;priorities[i]=4;
    }
    m_attempt->validManifest=true;m_attempt->manifestPaths=allPaths;
    if(selected.isEmpty()){end(g,"This torrent has no audio files.");return;}
    QCollator order;order.setNumericMode(true);order.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(selected.begin(),selected.end(),[&](const File& x,const File& y){return order.compare(x.path,y.path)<0;});
    m_attempt->selected=selected;m_attempt->total=total;QPointer<AudiobookTorrentTransport> self(this);QPointer<IAudiobookTorrentEngine> engine=m_engine;const QString hash=m_attempt->hash;
    const bool owner=owns(*m_attempt);if(!self || !current(g) || m_attempt->ending)return;
    if(!owner){end(g,"Audiobook torrent storage ownership changed.");return;}
    engine->setPriorities(hash,priorities);if(!self || !current(g) || m_attempt->ending)return;
    const bool stillOwner=owns(*m_attempt);if(!self || !current(g) || m_attempt->ending)return;
    if(!stillOwner){end(g,"Audiobook torrent storage ownership changed.");return;}
    if(engine)engine->startPayload(hash);if(!self || !current(g) || m_attempt->ending)return;
    m_attempt->admitted=true;m_attempt->activity.restart();setPhase(g,"downloading");
}
void AudiobookTorrentTransport::poll(quint64 g) {
    if(!current(g))return;
    if(m_attempt->ending) {
        if(m_attempt->token.isEmpty())requestSettlement(g);
        if(current(g) && m_attempt->settling.elapsed()>=m_timeouts.inactivityMs && !m_attempt->settlementWarned){m_attempt->settlementWarned=true;qWarning()<<"[AudiobookDownloader] waiting for confirmed removal; outcome remains unknown";}
        return;
    }
    QPointer<AudiobookTorrentTransport> self(this);QPointer<IAudiobookTorrentEngine> engine=m_engine;
    const bool owner=owns(*m_attempt);if(!self || !current(g) || m_attempt->ending)return;
    if(!owner){end(g,"Audiobook torrent engine or storage is no longer available.");return;}
    if(!m_attempt->admitted) {
        const QString hash=m_attempt->hash;const auto files=engine->files(hash);
        if(!self || !current(g) || m_attempt->ending)return;
        if(files.isEmpty()){if(m_attempt->age.elapsed()>=m_timeouts.metadataMs)end(g,"Audiobook torrent metadata timed out.");return;}
        admit(g,files);if(!self || !current(g) || m_attempt->ending || !m_attempt->admitted)return;
    }
    const QString hash=m_attempt->hash;const auto counts=engine->verifiedBytes(hash);
    if(!self || !current(g) || m_attempt->ending)return;
    qint64 received=0;bool complete=true;
    for(const auto& f:m_attempt->selected){const qint64 n=f.index<counts.size()?counts[f.index].toVariant().toLongLong():0;received+=qBound<qint64>(0,n,f.bytes);complete=complete&&n>=f.bytes;}
    if(received!=m_attempt->received){m_attempt->received=received;m_attempt->activity.restart();const qint64 total=m_attempt->total;emit progress(g,received,total);if(!self || !current(g) || m_attempt->ending)return;}
    const bool stillOwner=owns(*m_attempt);if(!self || !current(g) || m_attempt->ending)return;
    if(!stillOwner){end(g,"Audiobook torrent storage ownership changed.");return;}
    if(complete) {
        const Attempt snapshot=*m_attempt;QStringList paths;
        for(const auto& f:snapshot.selected){const QFileInfo info(f.path);if(!contained(snapshot.path,QDir(snapshot.path).relativeFilePath(f.path)) || !info.isFile() || info.isSymLink() || info.size()!=f.bytes || !info.canonicalFilePath().startsWith(snapshot.path+'/')){end(g,"Verified audiobook files are missing, incomplete, or outside private storage.");return;}paths.append(info.absoluteFilePath());}
        end(g,{},false,paths);
    } else if(m_attempt->activity.elapsed()>=m_timeouts.inactivityMs)end(g,"Audiobook download timed out without new verified file bytes.");
}
