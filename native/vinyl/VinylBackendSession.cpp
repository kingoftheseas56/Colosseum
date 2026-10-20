#include "VinylBackendSession.h"
#include "QueueSessionCoordinator.h"
#include "ColosseumMpvBridge.h"
#include "MpvSidecarReference.h"
#include "VinylSessionPersistence.h"
#include "VinylListenEventLedger.h"
#include "FeedbackTastePersistence.h"
#include "player/mpvitem.h"
#include <QQuickItem>
#include <QJsonDocument>
#include <QDateTime>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QUrl>
#include <QUuid>
#include <chrono>
#include <cmath>
#include <algorithm>

using namespace vinyl;
Q_LOGGING_CATEGORY(vinylActivityLog,"colosseum.vinyl.activity",QtWarningMsg)

struct VinylBackendSession::Backend {
    struct Ports : ColosseumMpvPlayerPort, PlaybackLoadInputProvider {
        VinylBackendSession &owner;
        explicit Ports(VinylBackendSession &owner):owner(owner) {}
        std::optional<PlaybackLoadInput> inputFor(const QueueOccurrence &item) override {return owner.inputFor(item);}
        void loadFile(const PlaybackWorkToken &token,const QString &url,const QString &options) override {owner.loadFile(token,url,options);}
        void loadFileWithHeaders(const PlaybackWorkToken &token,const QString &url,const QVector<PlaybackRequestHeader> &headers,const QString &options) override {owner.loadFileWithHeaders(token,url,headers,options);}
        void setPaused(const PlaybackWorkToken &token,bool paused) override {owner.setPaused(token,paused);}
        void seekExact(const PlaybackSeekCommand &command,double seconds) override {owner.seekExact(command,seconds);}
        void stop(const PlaybackWorkToken &token) override {owner.stop(token);}
    } ports;
    VinylQueue queue;
    PlaybackSession session;
    ColosseumMpvTimeState timeState;
    ColosseumMpvCommands commands;
    PlaybackEngineAdapter adapter;
    QueueSessionCoordinator coordinator;
    ColosseumMpvCallbacks callbacks;
    MpvPlaylistEntryWorkLedger entries;
    SerializedMpvSeekState seeks;
    std::optional<PlaybackSeekCommand> physicalSeek;
    std::optional<PlaybackLoadInput> input;
    std::unique_ptr<ListenEventLedger> listens;
    std::unique_ptr<ListenEventRecorder> listenRecorder;
    qint64 projectedListeningMs=0;
    explicit Backend(VinylBackendSession &owner):ports(owner),commands(ports,timeState),
        adapter(session,commands),coordinator(queue,session,adapter,ports),callbacks(adapter,timeState) {}
};


qint64 VinylBackendSession::monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

VinylBackendSession::VinylBackendSession(QObject *parent)
    : QObject(parent),m_backend(std::make_unique<Backend>(*this)) {
    m_deadline.setInterval(100);
    connect(&m_deadline,&QTimer::timeout,this,[this] {
        if(auto expired=m_backend->seeks.expire(monotonicMs())) {
            m_backend->callbacks.seekFailed(*expired,"Seek acknowledgement timed out.");
            m_backend->physicalSeek.reset();fail("The seek did not complete. Retry this track.");
        }
    });
    m_deadline.start();
}
VinylBackendSession::~VinylBackendSession() { m_shutdown=true;cancelResolution();retirePlayer(); }
void VinylBackendSession::publishState() {
    emit stateChanged();
    // QML aliases observe every native NOTIFY emission, even for equal values.
    // Clock updates must never look like pause transitions to Activity/history.
    if(m_publishedPaused!=m_paused){m_publishedPaused=m_paused;emit pausedChanged();}
}
QObject *VinylBackendSession::player() const { return m_player; }
int VinylBackendSession::activeIndex() const {return m_backend->queue.activeIndex();}
bool VinylBackendSession::seekPending() const {return m_backend->session.pendingSeek().has_value();}

void VinylBackendSession::setSource(QObject *source) {
    if(m_source==source)return;
    suspend();
    if(m_source)disconnect(m_source,nullptr,this,nullptr);
    m_source=source;
    if(m_source)connect(m_source,SIGNAL(completed(QString,int,QVariant,QString)),
                       this,SLOT(sourceCompleted(QString,int,QVariant,QString)),Qt::QueuedConnection);
    emit sourceChanged();
}
void VinylBackendSession::setProfileScope(const QString &profile) {
    if(m_profile==profile){m_profileActive=ProfileScopeId::parse(profile).ok();return;}
    clearProfile();m_profile=profile;
    m_profileActive=ProfileScopeId::parse(profile).ok();
    emit profileScopeChanged();publishState();
}
QVariantList VinylBackendSession::rows() const {
    QVariantList out;
    for(const auto &item:m_backend->queue.items())out.append(m_rows.value(item.occurrenceId().value()));
    return out;
}
QVariantMap VinylBackendSession::activeRow() const {
    const auto *active=m_backend->queue.activeOccurrence();
    return active?m_rows.value(active->occurrenceId().value()):QVariantMap{};
}
double VinylBackendSession::position() const {
    if(!m_loaded||m_backend->session.pendingSeek())return m_resume;
    return m_backend->adapter.positionMs().value_or(qRound64(m_resume*1000))/1000.0;
}
bool VinylBackendSession::validRow(const QVariantMap &row) const {
    const auto id=row.value("trackId").toString();
    const auto track=row.value("track").toMap(),edition=row.value("edition").toMap();
    const auto discs=edition.value("discs").toList();
    bool discOk=false,trackOk=false;
    int disc=row.value("discIndex").toInt(&discOk),index=row.value("trackIndex").toInt(&trackOk);
    return !id.isEmpty()&&id.size()<=1024&&id.trimmed()==id&&track.value("id").toString()==id
        &&!edition.value("id").toString().isEmpty()&&discOk&&trackOk&&disc>=0&&disc<discs.size()
        &&index>=0&&index<discs[disc].toList().size()
        &&discs[disc].toList()[index].toMap().value("id").toString()==id;
}
std::optional<QueueOccurrence> VinylBackendSession::occurrence(const QVariantMap &row) const {
    if(!validRow(row))return std::nullopt;
    auto id=QueueOccurrenceId::parse(row.value("queueId").toString());
    if(!id.ok())return std::nullopt;
    std::optional<RecordingMbid> mapping;
    const auto text=row.value("track").toMap().value("recordingMbid").toString();
    if(!text.isEmpty()) {
        auto parsed=RecordingMbid::parse(text);if(!parsed.ok())return std::nullopt;
        mapping=*parsed.value;
    }
    auto parsed=QueueOccurrence::createLocal(*id.value,row.value("trackId").toString(),mapping);
    return parsed.ok()?parsed.value:std::nullopt;
}
bool VinylBackendSession::replaceRows(const QVariantList &input,int index,bool shuffle) {
    if(m_shutdown||!m_profileActive||input.size()>10000||index< -1||index>=input.size())return false;
    VinylQueue queue;QHash<QString,QVariantMap> metadata;
    for(const auto &value:input) {
        auto row=value.toMap();row["queueId"]="queue:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto item=occurrence(row);if(!item||!queue.append(*item).ok)return false;
        metadata.insert(item->occurrenceId().value(),row);
    }
    emit playbackEnding(false);cancelResolution();m_backend->coordinator.clear();
    m_backend->queue=std::move(queue);m_rows=std::move(metadata);m_duration=0;m_resume=0;m_state="idle";
    if(shuffle)m_backend->queue.shuffle(QRandomGenerator::global()->generate64());
    m_shuffleEnabled=shuffle;
    if(index>=0)m_backend->queue.activate(m_backend->queue.items()[shuffle?0:index].occurrenceId());
    emit queueChanged();
    if(index>=0)beginActive();
    emit queueChanged();publishState();return true;
}
bool VinylBackendSession::appendRows(const QVariantList &input,bool playNext) {
    if(m_shutdown||!m_profileActive||input.isEmpty()||input.size()+m_backend->queue.size()>10000)return false;
    QVector<QueueOccurrence> items;QHash<QString,QVariantMap> metadata;
    for(const auto &value:input) {
        auto row=value.toMap();row["queueId"]="queue:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto item=occurrence(row);if(!item)return false;
        items.append(*item);metadata.insert(item->occurrenceId().value(),row);
    }
    int at=playNext&&activeIndex()>=0?activeIndex()+1:m_backend->queue.size();
    for(const auto &item:items)m_backend->coordinator.insert(at++,item);
    m_rows.insert(metadata);emit queueChanged();publishState();return true;
}
void VinylBackendSession::playQueue(int index) {
    if(m_shutdown||!m_profileActive||index<0||index>=m_backend->queue.size())return;
    emit playbackEnding(false);cancelResolution();
    if(!m_backend->coordinator.stop().ok){fail("The previous queue occurrence could not be retired.");return;}
    m_backend->queue.activate(m_backend->queue.items()[index].occurrenceId());emit queueChanged();beginActive();
}
void VinylBackendSession::removeQueue(int index) {
    if(m_shutdown||!m_profileActive||index<0||index>=m_backend->queue.size())return;
    const bool current=index==activeIndex();const bool paused=m_paused;
    const auto id=m_backend->queue.items()[index].occurrenceId();
    if(current){
        emit playbackEnding(false);cancelResolution();
        if(!m_backend->coordinator.stop().ok){fail("The previous queue occurrence could not be retired.");return;}
    }
    m_backend->queue.remove(id);m_rows.remove(id.value());
    if(m_backend->queue.empty()){clearQueue();return;}
    if(current)beginActive(0,!paused);
    emit queueChanged();publishState();
}
void VinylBackendSession::moveQueue(int from,int to) {
    if(m_shutdown||!m_profileActive||from<0||to<0||from>=m_backend->queue.size()||to>=m_backend->queue.size())return;
    m_backend->coordinator.move(m_backend->queue.items()[from].occurrenceId(),to);emit queueChanged();publishState();
}
void VinylBackendSession::clearQueue() {
    if(m_shutdown||!m_profileActive)return;
    emit playbackEnding(false);cancelResolution();m_backend->coordinator.clear();m_rows.clear();
    m_resume=0;m_duration=0;m_paused=true;m_state="idle";m_error.clear();emit queueChanged();publishState();
}
void VinylBackendSession::setShuffle(bool enabled) {
    if(m_shutdown||!m_profileActive||enabled==shuffled())return;
    if(enabled)m_backend->queue.shuffle(QRandomGenerator::global()->generate64());else m_backend->queue.unshuffle();
    m_shuffleEnabled=enabled;
    emit queueChanged();publishState();
}
void VinylBackendSession::cycleRepeat() {
    if(m_shutdown||!m_profileActive)return;
    m_repeat=m_repeat=="off"?"all":m_repeat=="all"?"one":"off";
    m_backend->coordinator.setRepeatMode(m_repeat=="all"?CoordinatorRepeatMode::Queue:CoordinatorRepeatMode::Off);
    publishState();
}
void VinylBackendSession::beginActive(double seconds,bool play) {
    if(m_shutdown||!m_profileActive||!m_backend->queue.activeOccurrence())return;
    m_resume=seconds;m_duration=activeRow().value("seconds").toDouble();m_paused=!play;
    m_error.clear();m_state="finding";m_backend->input.reset();
    emit playbackRequested();
    const auto result=m_backend->coordinator.awaitInput(*m_backend->queue.activeOccurrenceId(),play);
    if(!result.ok){fail("The queue could not start playback.");return;}
    inputFor(*m_backend->queue.activeOccurrence());publishState();
}
std::optional<PlaybackLoadInput> VinylBackendSession::inputFor(const QueueOccurrence &item) {
    if(m_backend->input)return m_backend->input;
    if(m_request>=0)return std::nullopt;
    const auto generation=m_epoch;
    m_resolvingId=item.occurrenceId().value();
    QTimer::singleShot(0,this,[this,generation,id=m_resolvingId] {
        if(m_shutdown||!m_profileActive||generation!=m_epoch||id!=m_resolvingId)return;
        if(!m_source){fail("Local audio is unavailable.");return;}
        const auto track=activeRow().value("trackId").toString();
        if(activeRow().value("track").toMap().value("available",true)==false){fail("This track is unavailable.");return;}
        int result=-1;
        const auto method=m_source->metaObject()->indexOfMethod("request(QString,QString,QString)");
        if(method<0){fail("The audio source cannot resolve tracks.");return;}
        auto request=m_source->metaObject()->method(method);
        if(request.returnMetaType().id()==QMetaType::QVariant){
            QVariant value;
            request.invoke(m_source,Qt::DirectConnection,Q_RETURN_ARG(QVariant,value),
                Q_ARG(QString,QString("play")),Q_ARG(QString,QString("resolve")),Q_ARG(QString,track));
            result=value.toInt();
        }else request.invoke(m_source,Qt::DirectConnection,Q_RETURN_ARG(int,result),
                Q_ARG(QString,QString("play")),Q_ARG(QString,QString("resolve")),Q_ARG(QString,track));
        m_request=result;
        if(result<0)fail("The audio source could not be requested.");
        else QTimer::singleShot(30000,this,[this,generation,result] {
            if(!m_shutdown&&generation==m_epoch&&m_request==result) {
                cancelResolution();fail("The audio source did not respond. Retry this track.");
            }
        });
    });
    return std::nullopt;
}
void VinylBackendSession::sourceCompleted(const QString &lane,int request,const QVariant &data,const QString &error) {
    if(sender()!=m_source||m_shutdown||!m_profileActive||lane!="play"||request<0||request!=m_request)return;
    m_request=-1;
    const auto *active=m_backend->queue.activeOccurrence();const auto result=data.toMap();
    if(!active||active->occurrenceId().value()!=m_resolvingId)return;
    if(!error.isEmpty()||result.value("trackId")!=activeRow().value("trackId")) {
        fail(error.isEmpty()?"The source did not match this track.":error);return;
    }
    QVector<PlaybackRequestHeader> headers;
    const auto fields=result.value("headers").toMap();
    for(auto it=fields.begin();it!=fields.end();++it)headers.append({it.key(),it.value().toString()});
    auto input=PlaybackLoadInput::create(result.value("url").toString(),headers);
    if(!input.ok()){fail("The audio source is invalid.");return;}
    m_backend->input=*input.value;m_quality=result.value("qualityLabel","Local audio").toString();
    auto started=m_backend->coordinator.restartActive(active->occurrenceId(),*m_backend->input);
    if(!started.ok)fail("The audio could not start.");
    publishState();
}
void VinylBackendSession::cancelResolution() {
    ++m_epoch;m_request=-1;m_resolvingId.clear();m_backend->input.reset();
    if(m_source)QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QString,QString("play")));
}
void VinylBackendSession::retirePlayer() {
    m_loaded=false;m_backend->entries.clear();m_backend->seeks.clear();m_backend->physicalSeek.reset();m_seekAccepted=false;
    m_backend->listenRecorder.reset();m_backend->listens.reset();
    auto *old=m_player.data();m_player=nullptr;
    if(old){disconnect(old,nullptr,this,nullptr);old->setPause(true);old->stopPlayback();old->deleteLater();}
}
void VinylBackendSession::loadFile(const PlaybackWorkToken &token,const QString &resource,const QString &options) {
    loadFileWithHeaders(token,resource,{},options);
}
void VinylBackendSession::loadFileWithHeaders(const PlaybackWorkToken &token,const QString &resource,
    const QVector<PlaybackRequestHeader> &headers,const QString &options) {
    retirePlayer();
    auto *item=new MpvItem(qobject_cast<QQuickItem *>(m_surface.data()));
    if(!item->parent())item->setParent(this);
    item->setObjectName("vinylMpvPlayer");item->setSize({1,1});item->setVisible(false);m_player=item;
    m_backend->seeks.bindWork(token);
    const auto generation=m_epoch;
    connect(item,&MpvItem::nativePlaybackEvent,this,[this,item,token,generation](
        const QString &kind,qint64 entry,double position,double duration,quint64 load,qint64 observedMs) {
        if(m_shutdown||!m_profileActive||m_player!=item||generation!=m_epoch||load!=item->playbackGeneration())return;
        if(kind=="loaded") {
            if(!m_backend->entries.bind(entry,token)){fail("The playback identity could not be confirmed.");return;}
            if(!m_backend->coordinator.engineLoaded(token).ok)return;
            m_loaded=true;m_state="playing";
            if(std::isfinite(duration)&&duration>0){m_duration=duration;m_backend->callbacks.durationObserved(token,duration);}
            if(m_resume>0)m_backend->adapter.seek(qRound64(m_resume*1000));
            publishState();
        }else if(kind=="seek-start"&&m_seekAccepted)m_backend->seeks.observeSeekStart(token);
        else if(kind=="restart") {
            if(!m_seekAccepted)return;
            auto work=m_backend->entries.tokenFor(entry);if(!work||*work!=token)return;
            auto advance=m_backend->seeks.observePlaybackRestart(token,millisecondsFromMpvSeconds(position),observedMs);
            if(advance){
                m_backend->callbacks.seekApplied(advance->completed,position);
                m_resume=position;emit discontinuity(position);
                m_backend->physicalSeek.reset();m_seekAccepted=false;
                if(advance->nextPhysical)dispatchSeek(*advance->nextPhysical);
                publishState();
            }
        }else if(kind.startsWith("ended-")){
            auto work=m_backend->entries.tokenFor(entry);if(!work||*work!=token)return;
            if(kind=="ended-eof")naturalEnd(*work);
            else if(kind=="ended-error")fail("The audio could not be decoded or played. Retry this track.");
        }
    });
    connect(item,&MpvItem::trackedSeekReply,this,[this,item,token](quint64 serial,bool success,quint64 load) {
        if(m_player!=item||load!=item->playbackGeneration()||!m_backend->physicalSeek
            ||m_backend->physicalSeek->sequence()!=serial||m_backend->physicalSeek->workToken()!=token)return;
        if(success){m_seekAccepted=true;return;}
        auto failed=m_backend->seeks.failActive(*m_backend->physicalSeek);
        if(failed){m_backend->callbacks.seekFailed(*failed,"mpv rejected seek");m_backend->physicalSeek.reset();fail("The seek failed. Retry this track.");}
    });
    connect(item,&MpvItem::positionChanged,this,[this,item,token,generation] {
        if(m_player!=item||generation!=m_epoch||!m_loaded)return;
        m_backend->callbacks.positionObserved(token,item->position());publishState();
    });
    connect(item,&MpvItem::durationChanged,this,[this,item,token,generation] {
        if(m_player!=item||generation!=m_epoch||!m_loaded||item->duration()<=0)return;
        m_duration=item->duration();m_backend->callbacks.durationObserved(token,m_duration);publishState();
    });
    connect(item,&MpvItem::pauseChanged,this,[this,item,token,generation] {
        if(m_player!=item||generation!=m_epoch||!m_loaded||m_state!="playing")return;
        if(m_backend->callbacks.pauseObserved(token,item->pause()).ok) {
            listeningPauseTransition(item->pause());
            m_paused=item->pause();publishState();
        }
    });
    connect(item,&MpvItem::playbackError,this,[this,item](const QString &,const QString &) {
        if(m_player==item)fail("The audio could not be decoded or played. Retry this track.");
    });
    QVariantMap fields;for(const auto &header:headers)fields.insert(header.name,header.value);
    // This first host adapter admits whole local files. Never silently discard a
    // future bounded source's start/end options and play the wrong album span.
    if(!options.isEmpty()){fail("This bounded source is not connected to the local player yet.");retirePlayer();return;}
    item->loadFileWithHeaders(resource,fields);item->setPause(m_paused);
    QTimer::singleShot(15000,this,[this,item,generation] {
        if(m_player==item&&generation==m_epoch&&!m_loaded)fail("The audio did not start in time. Retry this track.");
    });
    publishState();
}
void VinylBackendSession::setPaused(const PlaybackWorkToken &,bool paused) { if(m_player)m_player->setPause(paused); }
void VinylBackendSession::seekExact(const PlaybackSeekCommand &command,double) {
    auto result=m_backend->seeks.submit(command,monotonicMs());if(result.dispatch)dispatchSeek(*result.dispatch);
}
void VinylBackendSession::dispatchSeek(const PlaybackSeekCommand &command) {
    if(!m_player)return;
    m_backend->physicalSeek=command;m_seekAccepted=false;
    const auto transform=m_backend->timeState.transformFor(command.workToken());
    if(!transform)return;
    const auto physical=transform->physicalPositionForTrack(command.targetMs());
    if(physical)m_player->seekExactTracked(*physical/1000.0,command.sequence());
}
void VinylBackendSession::stop(const PlaybackWorkToken &) { retirePlayer(); }
void VinylBackendSession::setPaused(bool paused) {
    if(m_shutdown||!m_profileActive||activeIndex()<0)return;
    if(!paused&&!m_loaded&&m_state!="finding"){beginActive(m_state=="ended"?0:position());return;}
    listeningPauseTransition(paused);m_paused=paused;
    if(m_state=="finding" && m_backend->queue.activeOccurrenceId())m_backend->coordinator.awaitInput(*m_backend->queue.activeOccurrenceId(),!paused);
    if(m_loaded){if(paused)m_backend->adapter.pause();else m_backend->adapter.play();}
    publishState();
}
void VinylBackendSession::seek(double seconds) {
    if(m_shutdown||!m_profileActive||!m_loaded||m_state!="playing"||!std::isfinite(seconds))return;
    const auto target=std::clamp(seconds,0.0,m_duration),before=position();
    auto result=m_backend->adapter.seek(qRound64(target*1000));
    if(result.ok()){
        if(m_backend->listenRecorder&&m_backend->session.activeWorkToken())m_backend->listenRecorder->seek(
            *m_backend->session.activeWorkToken(),QDateTime::currentMSecsSinceEpoch(),monotonicMs(),
            qRound64(before*1000),qRound64(target*1000),1000);
        m_resume=target;emit discontinuity(target);publishState();
    }
}
void VinylBackendSession::next(int delta) {
    if(m_shutdown||activeIndex()<0||m_backend->queue.empty())return;
    int index=activeIndex()+(delta<0?-1:1);
    if(index<0||index>=m_backend->queue.size()){
        if(m_repeat!="all")return;index=(index+m_backend->queue.size())%m_backend->queue.size();
    }
    playQueue(index);
}
void VinylBackendSession::naturalEnd(const PlaybackWorkToken &token) {
    if(!m_backend->adapter.validateWorkToken(token).ok)return;
    emit playbackEnding(true);
    if(m_repeat=="one"){playQueue(activeIndex());return;}
    if(activeIndex()+1<m_backend->queue.size()||m_repeat=="all"){next(1);return;}
    m_backend->callbacks.ended(token);m_resume=m_duration;m_loaded=false;m_paused=true;m_state="ended";publishState();
}
void VinylBackendSession::fail(const QString &message) {
    emit playbackEnding(false);m_error=message;m_state="error";m_paused=true;
    if(m_backend->session.activeWorkToken())m_backend->callbacks.failed(*m_backend->session.activeWorkToken(),"host_failure",message);
    if(m_player)m_player->setPause(true);publishState();
}
void VinylBackendSession::retry() { if(activeIndex()>=0){const auto at=position();cancelResolution();
    if(!m_backend->coordinator.stop().ok){fail("The previous queue occurrence could not be retired.");return;}beginActive(at);} }
void VinylBackendSession::suspend() {
    const auto at=position();emit playbackEnding(false);cancelResolution();m_backend->coordinator.stop();retirePlayer();
    m_resume=at;m_paused=true;m_state=activeIndex()>=0?"paused":"idle";publishState();
}
void VinylBackendSession::clearProfile() {
    suspend();m_backend->coordinator.clear();m_rows.clear();m_profileActive=false;
    m_resume=0;m_duration=0;m_repeat="off";m_shuffleEnabled=false;m_state="idle";m_error.clear();emit queueChanged();publishState();
}
void VinylBackendSession::shutdown() { if(m_shutdown)return;suspend();m_shutdown=true;m_deadline.stop(); }

QVariantMap VinylBackendSession::checkpoint() const {
    auto profile=ProfileScopeId::parse(m_profile);if(!profile.ok())return {};
    SessionCoordinatorIntent intent;
    intent.repeatMode=m_repeat=="all"?DurableRepeatMode::Queue:DurableRepeatMode::Off;
    if(m_backend->coordinator.pendingInput())intent.pendingInput=SessionPendingInputIntent{
        m_backend->coordinator.pendingInput()->occurrenceId,m_backend->coordinator.pendingInput()->playWhenReady};
    auto captured=VinylSessionPersistence::capture(*profile.value,m_backend->queue,m_backend->session,intent);
    if(!captured.ok())return {};
    // Editions are stored once, preserving the host's compact checkpoint shape.
    QVariantList compact,editions;QHash<QString,QList<int>> editionKeys;
    for(const auto &value:rows()) {
        auto row=value.toMap();const auto edition=row.take("edition").toMap();
        int key=-1;const auto editionId=edition.value("id").toString();
        for(int candidate:editionKeys.value(editionId))
            if(editions[candidate].toMap()==edition){key=candidate;break;}
        if(key<0){key=editions.size();editions.append(edition);editionKeys[editionId].append(key);}
        row.remove("track");row["editionKey"]=key;compact.append(row);
    }
    return {{"version",3},{"backendState",QString::fromUtf8(*captured.value)},
        {"rows",compact},{"editionTable",editions},{"repeatMode",m_repeat},{"shuffleEnabled",m_shuffleEnabled},
        {"position",position()},{"duration",m_duration}};
}
bool VinylBackendSession::restoreCheckpoint(const QVariantMap &checkpoint) {
    if(m_shutdown||!m_profileActive||checkpoint.value("version").toInt()!=3)return false;
    auto profile=ProfileScopeId::parse(m_profile);if(!profile.ok())return false;
    auto restored=VinylSessionPersistence::restore(*profile.value,checkpoint.value("backendState").toString().toUtf8());
    if(!restored.ok())return false;
    auto rows=checkpoint.value("rows").toList();if(rows.size()!=restored.value->queue.size())return false;
    const auto editions=checkpoint.value("editionTable").toList();
    if(checkpoint.contains("editionTable"))for(int i=0;i<rows.size();++i) {
        auto row=rows[i].toMap();bool ok=false;const auto key=row.take("editionKey").toInt(&ok);
        if(!ok||key<0||key>=editions.size())return false;
        row["edition"]=editions[key];
        const auto discs=editions[key].toMap().value("discs").toList();
        const int disc=row.value("discIndex",-1).toInt(),track=row.value("trackIndex",-1).toInt();
        if(disc<0||disc>=discs.size()||track<0||track>=discs[disc].toList().size())return false;
        row["track"]=discs[disc].toList()[track];rows[i]=row;
    }
    QHash<QString,QVariantMap> metadata;
    for(int i=0;i<rows.size();++i){
        const auto row=rows[i].toMap();auto item=occurrence(row);
        if(!item||item->occurrenceId()!=restored.value->queue.items()[i].occurrenceId()
            ||!item->sameMusicIdentity(restored.value->queue.items()[i]))return false;
        metadata.insert(item->occurrenceId().value(),row);
    }
    const double position=checkpoint.value("position").toDouble(),duration=checkpoint.value("duration").toDouble();
    const auto repeat=checkpoint.value("repeatMode").toString();
    if(!std::isfinite(position)||position<0||!std::isfinite(duration)||duration<0
        ||!QStringList{"off","all","one"}.contains(repeat))return false;
    suspend();m_backend->coordinator.clear();m_backend->queue=std::move(restored.value->queue);m_rows=std::move(metadata);
    m_repeat=repeat;m_resume=position;m_duration=duration;m_paused=true;m_error.clear();
    m_shuffleEnabled=checkpoint.value("shuffleEnabled",m_backend->queue.isShuffled()).toBool();
    m_state=activeIndex()>=0?"paused":"idle";emit queueChanged();publishState();return true;
}
bool VinylBackendSession::restoreLegacy(const QVariantList &input,int index,bool shuffle,
    const QStringList &originalOrder,const QString &repeat,double position,double duration) {
    if(m_shutdown||!m_profileActive||input.size()>10000||index< -1||index>=input.size()
        ||!std::isfinite(position)||position<0||!std::isfinite(duration)||duration<0
        ||!QStringList{"off","all","one"}.contains(repeat))return false;
    VinylQueue queue;QHash<QString,QVariantMap> metadata;
    for(const auto &value:input){auto row=value.toMap();auto item=occurrence(row);
        if(!item||!queue.append(*item).ok)return false;metadata.insert(item->occurrenceId().value(),row);}
    if(index>=0)queue.activate(queue.items()[index].occurrenceId());
    if(shuffle){QVector<QueueOccurrenceId> ids;for(const auto &id:originalOrder){auto parsed=QueueOccurrenceId::parse(id);if(!parsed.ok())return false;ids.append(*parsed.value);}
        if(!queue.restoreShuffleOriginalOrder(ids).ok)return false;}
    suspend();m_backend->coordinator.clear();m_backend->queue=std::move(queue);m_rows=std::move(metadata);m_repeat=repeat;
    m_resume=position;m_duration=duration;m_shuffleEnabled=shuffle;m_state=index>=0?"paused":"idle";emit queueChanged();publishState();return true;
}

void VinylBackendSession::beginListeningFacts() {
    m_backend->listenRecorder.reset();m_backend->listens.reset();
    m_backend->projectedListeningMs=0;
    const auto profile=ProfileScopeId::parse(m_profile);
    qCDebug(vinylActivityLog)<<"begin"<<profile.ok()<<m_loaded
        <<m_backend->session.activeWorkToken().has_value()<<m_backend->session.activeOccurrence().has_value()<<m_paused;
    if(!profile.ok()||!m_loaded||!m_backend->session.activeWorkToken()||!m_backend->session.activeOccurrence())return;
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto session=ListenSessionId::parse(id);
    const auto instance=PlayInstanceId::parse(id);
    if(!session.ok()||!instance.ok())return;
    auto context=ListenContext::create(*profile.value,*session.value,*m_backend->session.activeOccurrence(),
        *m_backend->session.activeWorkToken(),*instance.value);
    if(!context.ok()){qCDebug(vinylActivityLog)<<"context rejected";return;}
    m_backend->listens=std::make_unique<ListenEventLedger>(*profile.value);
    m_backend->listenRecorder=std::make_unique<ListenEventRecorder>(*m_backend->listens);
    const auto now=monotonicMs(),wall=QDateTime::currentMSecsSinceEpoch();
    const auto started=m_backend->listenRecorder->start(*context.value,wall,now,qRound64(position()*1000),1000);
    qCDebug(vinylActivityLog)<<"started"<<started.ok;
    if(!started.ok){m_backend->listenRecorder.reset();m_backend->listens.reset();return;}
    // BA4 start is consuming. A paused load must immediately close that interval.
    if(m_paused)m_backend->listenRecorder->pause(*m_backend->session.activeWorkToken(),wall,now,qRound64(position()*1000),1000);
}
bool VinylBackendSession::qualifyListeningSample(bool consuming) {
    if(!m_backend->listenRecorder||!m_backend->listens||!m_backend->session.activeWorkToken()){
        qCDebug(vinylActivityLog)<<"sample without context";return false;
    }
    const auto before=m_backend->projectedListeningMs;
    const auto now=monotonicMs(),wall=QDateTime::currentMSecsSinceEpoch(),pos=qRound64(position()*1000);
    const auto token=*m_backend->session.activeWorkToken();
    // BA4 is transient qualification. ActivityPlaybackTracker remains the only
    // durable fact producer, retaining its activation, privacy and retention law.
    consuming=consuming&&!seekPending()&&m_state=="playing";
    const auto status=(!consuming||m_paused)?m_backend->listenRecorder->pause(token,wall,now,pos,1000)
        :!m_backend->listenRecorder->consuming()?m_backend->listenRecorder->resume(token,wall,now,pos,1000)
        :m_backend->listenRecorder->sample(token,wall,now,pos,1000);
    m_backend->projectedListeningMs=m_backend->listens->totalListeningMs();
    qCDebug(vinylActivityLog)<<"sample"<<pos<<consuming<<m_paused<<status.ok<<before<<m_backend->projectedListeningMs;
    return consuming&&m_backend->projectedListeningMs>before;
}
void VinylBackendSession::listeningPauseTransition(bool paused) {
    if(!m_backend->listenRecorder||!m_backend->session.activeWorkToken())return;
    const auto token=*m_backend->session.activeWorkToken();
    const auto now=monotonicMs(),wall=QDateTime::currentMSecsSinceEpoch(),pos=qRound64(position()*1000);
    if(paused)m_backend->listenRecorder->pause(token,wall,now,pos,1000);
    else if(!m_backend->listenRecorder->consuming())m_backend->listenRecorder->resume(token,wall,now,pos,1000);
}
void VinylBackendSession::endListeningFacts(bool natural) {
    if(m_backend->listenRecorder&&m_backend->session.activeWorkToken()) {
        const auto now=monotonicMs(),wall=QDateTime::currentMSecsSinceEpoch(),pos=qRound64(position()*1000);
        if(natural)m_backend->listenRecorder->complete(*m_backend->session.activeWorkToken(),wall,now,pos,1000);
        else m_backend->listenRecorder->stop(*m_backend->session.activeWorkToken(),wall,now,pos,1000);
    }
    m_backend->listenRecorder.reset();m_backend->listens.reset();
}
bool VinylBackendSession::validateFeedbackSnapshot(const QString &snapshot) const {
    if(snapshot.isEmpty())return true;
    const auto profile=ProfileScopeId::parse(m_profile);
    return profile.ok()&&FeedbackTastePersistence::restore(*profile.value,snapshot.toUtf8()).ok();
}
QVariantMap VinylBackendSession::feedbackState() const {
    if(!m_profileActive||m_shutdown||!m_preferences)return {{"ok",false},{"error","Preferences are unavailable."}};
    const auto snapshot=m_preferences->property("vinylFeedbackSnapshot").toString();
    if(!validateFeedbackSnapshot(snapshot))return {{"ok",false},{"error","Saved feedback is invalid for this profile."}};
    return {{"ok",true},{"snapshot",snapshot}};
}
bool VinylBackendSession::storeFeedbackSnapshot(const QString &snapshot) {
    if(m_shutdown||!m_profileActive||!m_preferences||!validateFeedbackSnapshot(snapshot))return false;
    bool saved=false;
    QMetaObject::invokeMethod(m_preferences,"setVinylFeedbackSnapshot",Qt::DirectConnection,
        Q_RETURN_ARG(bool,saved),Q_ARG(QString,snapshot));
    return saved;
}
bool VinylBackendSession::setSavedAlbum(const QVariantMap &input,bool saved) {
    if(m_shutdown||!m_profileActive||!m_collection)return false;
    auto entry=input;auto payload=entry.value("payload").toMap();
    const auto album=payload.value("album").toMap();
    if(entry.value("id").toString().isEmpty()||entry.value("id")!=album.value("id"))return false;
    // Saving changes membership, never deletes the local catalogue/source record.
    payload["saved"]=saved;entry["payload"]=payload;
    const auto index=m_collection->metaObject()->indexOfMethod("add(QString,QVariantMap)");
    if(index<0)return false;
    auto method=m_collection->metaObject()->method(index);
    bool result=false;
    if(method.returnMetaType().id()==QMetaType::QVariant){
        QVariant value;method.invoke(m_collection,Qt::DirectConnection,Q_RETURN_ARG(QVariant,value),
            Q_ARG(QString,QString("Vinyl")),Q_ARG(QVariantMap,entry));result=value.toBool();
    }else method.invoke(m_collection,Qt::DirectConnection,Q_RETURN_ARG(bool,result),
        Q_ARG(QString,QString("Vinyl")),Q_ARG(QVariantMap,entry));
    return result;
}
