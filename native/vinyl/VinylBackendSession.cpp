#include "VinylBackendSession.h"
#include "QueueSessionCoordinator.h"
#include "ColosseumMpvBridge.h"
#include "MpvSidecarReference.h"
#include "VinylSessionPersistence.h"
#include "VinylListenEventLedger.h"
#include "FeedbackTastePersistence.h"
#include "VinylRecommendationIdentity.h"
#include "VinylPlaylist.h"
#include "player/mpvitem.h"
#include <QQuickItem>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDateTime>
#include <QMetaMethod>
#include <QMetaType>
#include <QScopedValueRollback>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QScopedValueRollback>
#include <QUuid>
#include <chrono>
#include <cmath>
#include <algorithm>

using namespace vinyl;
Q_LOGGING_CATEGORY(vinylActivityLog,"colosseum.vinyl.activity",QtWarningMsg)

namespace {
QString musicIdentity(QString text) {
    text=text.normalized(QString::NormalizationForm_C).toCaseFolded();
    text.replace(QRegularExpression("[._]+")," ");
    return text.simplified();
}
bool normalizeRadioContext(const QVariantMap &input,QVariantMap *normalized) {
    if(input.isEmpty()) {
        if(normalized)*normalized={};
        return true;
    }
    const auto seed=input.value("seed").toMap();
    const auto providerId=seed.value("providerId").toString().trimmed();
    const auto name=seed.value("name").toString().trimmed();
    static const QRegularExpression channelId(QStringLiteral("^UC[A-Za-z0-9_-]{22}$"));
    if(input.value("kind").toString()!=QStringLiteral("artist-radio")
        ||seed.value("provider").toString()!=QStringLiteral("youtube-music")
        ||seed.value("kind").toString()!=QStringLiteral("artist")
        ||!channelId.match(providerId).hasMatch()||name.isEmpty()||name.size()>256)return false;
    if(normalized) {
        const QVariantMap normalizedSeed{{"provider","youtube-music"},{"providerId",providerId},
            {"kind","artist"},{"name",name}};
        *normalized={{"kind","artist-radio"},{"seed",normalizedSeed}};
    }
    return true;
}
QVariantMap selectedTrack(const QVariantMap &row) {
    const auto edition=row.value("edition").toMap(),album=edition.value("sourceAlbum").toMap();
    auto source=album;
    const auto editions=album.value("editions").toList();
    if(!editions.isEmpty()) {
        source.clear();
        for(const auto &candidate:editions)
            if(candidate.toMap().value("id")==edition.value("id")){source=candidate.toMap();break;}
    }
    for(const auto &value:source.value("tracks").toList())
        if(value.toMap().value("id")==row.value("trackId"))return value.toMap();
    return row.value("track").toMap();
}
bool resourceExpired(const QString &resource) {
    const QUrlQuery query{QUrl(resource)};
    const auto expires=query.queryItemValue("expire");
    if(!query.hasQueryItem("expire"))return false;
    bool ok=false;const auto seconds=expires.toLongLong(&ok);
    return !ok||seconds<=QDateTime::currentSecsSinceEpoch()+15;
}
bool videoResourceUrl(const QString &resource) {
    const QUrl url(resource,QUrl::StrictMode);
    return !resource.isEmpty()&&resource.size()<=16384&&url.isValid()&&url.scheme()=="https"
        &&url.host().endsWith(".googlevideo.com")&&url.userInfo().isEmpty()
        &&(url.port()==-1||url.port()==443)&&!url.hasFragment()
        &&!resource.contains(QRegularExpression("[\\s\\x00-\\x1f\\x7f]"))&&!resourceExpired(resource);
}
}

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
    struct SeekRestart {qint64 entry;double position;qint64 observedMs;quint64 observationSerial;};
    std::optional<SeekRestart> earlySeekRestart;
    quint64 seekObservationFence=0;
    quint64 seekStartSerial=0;
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
            const auto target=position();
            m_backend->callbacks.seekFailed(*expired,"Seek acknowledgement timed out.");
            m_backend->physicalSeek.reset();
            if(!tryFallback("Torrent audio seek did not complete.",target))fail("The seek did not complete. Retry this track.");
        }
        const auto now=monotonicMs();
        if(m_sourceKind=="torrent"&&m_loaded&&m_state=="playing") {
            const auto at=position();
            if(m_paused||seekPending()||at>m_lastProgress+0.05){m_lastProgress=at;m_lastProgressMs=now;}
            else if(now-m_lastProgressMs>15000)tryFallback("Torrent audio stalled.");
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
void VinylBackendSession::setVideoSurface(QObject *surface) {
    if(m_videoSurface==surface)return;
    if(m_videoSurface)disconnect(m_videoSurface,nullptr,this,nullptr);
    m_videoSurface=qobject_cast<QQuickItem *>(surface);
    if(auto *item=qobject_cast<QQuickItem *>(m_videoSurface.data())) {
        connect(item,&QQuickItem::widthChanged,this,&VinylBackendSession::attachVideoSurface);
        connect(item,&QQuickItem::heightChanged,this,&VinylBackendSession::attachVideoSurface);
        connect(item,&QQuickItem::visibleChanged,this,&VinylBackendSession::attachVideoSurface);
        connect(item,&QObject::destroyed,this,[this]{QTimer::singleShot(0,this,&VinylBackendSession::attachVideoSurface);});
    }
    attachVideoSurface();emit videoSurfaceChanged();
}
void VinylBackendSession::attachVideoSurface() {
    if(!m_player)return;
    auto *target=qobject_cast<QQuickItem *>((m_videoActive&&m_videoSurface)?m_videoSurface.data():m_surface.data());
    m_player->setParentItem(target);
    m_player->QQuickItem::setPosition(QPointF(0,0));
    m_player->setSize(m_videoActive&&m_videoSurface&&target?target->size():QSizeF(1,1));
    m_player->setVisible(m_videoActive&&m_videoSurface&&target&&target->isVisible());
}
void VinylBackendSession::setProfileScope(const QString &profile) {
    if(m_profile==profile){m_profileActive=ProfileScopeId::parse(profile).ok();return;}
    clearProfile();m_profile=profile;
    m_profileActive=ProfileScopeId::parse(profile).ok();
    emit profileScopeChanged();publishState();
}
void VinylBackendSession::setRadioContext(const QVariantMap &context) {
    QVariantMap normalized;
    if(!normalizeRadioContext(context,&normalized)||m_radioContext==normalized)return;
    m_radioContext=std::move(normalized);
    publishState();
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
    endPlayback("stop");m_listeningOrigin="manual";m_lastPlaybackAtMs=0;m_radioContext.clear();
    cancelResolution();m_backend->coordinator.clear();
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
    m_rows.insert(metadata);
    // A queued-only collection still needs a visible, paused selection so the
    // existing player bar and Queue action can reach it without starting audio.
    if(activeIndex()<0){
        m_backend->queue.activate(m_backend->queue.items().first().occurrenceId());
        m_paused=true;m_resume=0;m_duration=activeRow().value("seconds").toDouble();m_state="paused";
    }
    emit queueChanged();publishState();return true;
}
void VinylBackendSession::playQueue(int index) {
    if(m_shutdown||!m_profileActive||index<0||index>=m_backend->queue.size())return;
    endPlayback(m_loaded && !m_paused && m_state=="playing" && !seekPending()
        && index!=activeIndex() && !m_naturalAdvancing && !m_navigationBack?"skip":"stop");
    m_listeningOrigin=m_naturalAdvancing
        ?(m_radioContext.isEmpty()?"album-continuation":"radio"):"manual";
    cancelResolution();
    if(!m_backend->coordinator.stop().ok){fail("The previous queue occurrence could not be retired.");return;}
    m_backend->queue.activate(m_backend->queue.items()[index].occurrenceId());emit queueChanged();beginActive();
}
void VinylBackendSession::removeQueue(int index) {
    if(m_shutdown||!m_profileActive||index<0||index>=m_backend->queue.size())return;
    const bool current=index==activeIndex();const bool paused=m_paused;
    const auto id=m_backend->queue.items()[index].occurrenceId();
    if(current){
        endPlayback("stop");cancelResolution();
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
    endPlayback("stop");cancelResolution();m_backend->coordinator.clear();m_rows.clear();
    m_listeningOrigin="manual";m_lastPlaybackAtMs=0;m_radioContext.clear();
    m_sourceKind.clear();m_fallbackReason.clear();m_forceFallback=false;m_fallbackAttempted=false;
    m_resume=0;m_duration=0;m_paused=true;m_shuffleEnabled=false;m_state="idle";m_error.clear();emit queueChanged();publishState();
}
bool VinylBackendSession::continueRadio() {
    if(m_shutdown||!m_profileActive||m_radioContext.isEmpty()||m_state!="ended"
        ||activeIndex()<0||activeIndex()+1>=m_backend->queue.size())return false;
    QScopedValueRollback<bool> automatic(m_naturalAdvancing,true);
    next(1);setPaused(false);return true;
}
void VinylBackendSession::setShuffle(bool enabled) {
    if(m_shutdown||!m_profileActive||enabled==shuffled()||(enabled&&m_backend->queue.empty()))return;
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
void VinylBackendSession::beginActive(double seconds,bool play,bool fallback) {
    if(m_shutdown||!m_profileActive||!m_backend->queue.activeOccurrence())return;
    resetVideo();
    m_resume=seconds;m_duration=activeRow().value("seconds").toDouble();m_paused=!play;
    m_error.clear();m_state="finding";m_backend->input.reset();
    m_forceFallback=fallback;
    if(!fallback){m_fallbackAttempted=false;m_sourceKind.clear();m_fallbackReason.clear();}
    emit playbackRequested();
    const auto result=m_backend->coordinator.awaitInput(*m_backend->queue.activeOccurrenceId(),play);
    if(!result.ok){fail("The queue could not start playback.");return;}
    inputFor(*m_backend->queue.activeOccurrence());discoverVideo();publishState();
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
        auto method=m_source->metaObject()->indexOfMethod("request(QString,QString,QString,QVariantMap)");
        const bool contextual=method>=0;
        if(!contextual)method=m_source->metaObject()->indexOfMethod("request(QString,QString,QString)");
        if(method<0){fail("The audio source cannot resolve tracks.");return;}
        auto request=m_source->metaObject()->method(method);
        const auto edition=activeRow().value("edition").toMap();
        QVariantMap options;
        if(edition.value("origin","local").toString()!="local"){
            const auto browseId=edition.value("browseId",edition.value("sourceAlbum").toMap().value("browseId")).toString();
            if(!browseId.isEmpty())options.insert("browseId",browseId);
        }
        const QString lane="play",operation=m_forceFallback?"resolveFallback":"resolve";
        const QGenericArgument laneArg("QString",&lane),operationArg("QString",&operation),trackArg("QString",&track);
        const QGenericArgument extra=contextual?QGenericArgument("QVariantMap",&options):QGenericArgument();
        if(request.returnMetaType().id()==QMetaType::QVariant){
            QVariant value;
            request.invoke(m_source,Qt::DirectConnection,QGenericReturnArgument("QVariant",&value),laneArg,operationArg,trackArg,extra);
            result=value.toInt();
        }else request.invoke(m_source,Qt::DirectConnection,QGenericReturnArgument("int",&result),laneArg,operationArg,trackArg,extra);
        m_request=result;
        if(result<0)fail("The audio source could not be requested.");
        else QTimer::singleShot(120000,this,[this,generation,result] {
            if(!m_shutdown&&generation==m_epoch&&m_request==result) {
                cancelResolution();fail("The audio source did not respond. Retry this track.");
            }
        });
    });
    return std::nullopt;
}
void VinylBackendSession::sourceCompleted(const QString &lane,int request,const QVariant &data,const QString &error) {
    if(sender()!=m_source||m_shutdown||!m_profileActive)return;
    if(lane=="video-match"||lane=="video-stream"){videoCompleted(lane,request,data.toMap(),error);return;}
    if(lane!="play"||request<0||request!=m_request)return;
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
    m_sourceKind=result.value("provider",QUrl(result.value("url").toString()).isLocalFile()?"local":"youtube").toString();
    m_audioResource=result;m_audioResource["provider"]=m_sourceKind;
    if(!result.value("fallbackReason").toString().isEmpty())m_fallbackReason=result.value("fallbackReason").toString();
    if(m_sourceKind=="youtube")m_fallbackAttempted=true;
    auto started=m_backend->coordinator.restartActive(active->occurrenceId(),*m_backend->input);
    if(!started.ok)fail("The audio could not start.");
    publishState();
}
void VinylBackendSession::cancelResolution() {
    resetVideo();
    ++m_epoch;m_request=-1;m_resolvingId.clear();m_backend->input.reset();
    if(m_source)QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QString,QString("play")));
}
void VinylBackendSession::resetVideo() {
    m_videoMatchRequest=-1;m_videoRequest=-1;m_videoOccurrence.clear();
    m_videoCandidate.clear();m_audioResource.clear();m_videoAudioUrl.clear();m_videoError.clear();
    m_videoActive=false;m_videoBusy=false;m_targetVideo=false;m_resumeListeningFacts=false;
    if(m_source)for(const auto &lane:{QString("video-match"),QString("video-stream")})
        QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QString,lane));
    attachVideoSurface();
}
int VinylBackendSession::requestSource(const QString &lane,const QString &operation,const QString &value,const QVariantMap &options) {
    if(!m_source)return -1;
    auto index=m_source->metaObject()->indexOfMethod("request(QString,QString,QString,QVariantMap)");
    const bool contextual=index>=0;
    if(!contextual)index=m_source->metaObject()->indexOfMethod("request(QString,QString,QString)");
    if(index<0)return -1;
    const auto method=m_source->metaObject()->method(index);
    const QGenericArgument extra=contextual?QGenericArgument("QVariantMap",&options):QGenericArgument();
    const QGenericArgument laneArg("QString",&lane),operationArg("QString",&operation),valueArg("QString",&value);
    int result=-1;
    if(method.returnMetaType().id()==QMetaType::QVariant) {
        QVariant response;
        if(method.invoke(m_source,Qt::DirectConnection,QGenericReturnArgument("QVariant",&response),
            laneArg,operationArg,valueArg,extra))result=response.toInt();
    }else method.invoke(m_source,Qt::DirectConnection,QGenericReturnArgument("int",&result),
        laneArg,operationArg,valueArg,extra);
    return result;
}
void VinylBackendSession::discoverVideo() {
    const auto track=selectedTrack(activeRow());
    const auto video=track.value("videoId").toString();
    if(!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(video).hasMatch())return;
    m_videoOccurrence=activeRow().value("queueId").toString();
    const auto generation=m_epoch;const auto id=m_videoOccurrence;
    QTimer::singleShot(0,this,[this,generation,id,track] {
        if(m_shutdown||generation!=m_epoch||id!=m_videoOccurrence||id!=activeRow().value("queueId"))return;
        m_videoMatchRequest=requestSource("video-match","relatedVideo",activeRow().value("trackId").toString(),
            {{"title",track.value("title")},{"artists",track.value("artists")},{"artistIds",track.value("artistIds")}});
    });
}
void VinylBackendSession::videoCompleted(const QString &lane,int request,const QVariantMap &data,const QString &error) {
    if(request<0||m_videoOccurrence!=activeRow().value("queueId").toString())return;
    if(lane=="video-match") {
        if(request!=m_videoMatchRequest)return;
        m_videoMatchRequest=-1;
        const auto id=data.value("videoId").toString();
        if(error.isEmpty()&&data.value("status")=="ready"&&data.value("version").toInt()==1
            &&data.value("trackId")==activeRow().value("trackId")
            &&data.value("sourceVideoId")==selectedTrack(activeRow()).value("videoId")
            &&QRegularExpression("^[A-Za-z0-9_-]{11}$").match(id).hasMatch())m_videoCandidate=data;
        publishState();return;
    }
    if(request!=m_videoRequest)return;
    m_videoRequest=-1;
    if(!error.isEmpty()||data.value("trackId")!=activeRow().value("trackId")||!commitVideoMode(m_targetVideo,data)) {
        m_videoBusy=false;
        m_videoError=m_targetVideo?"The video is unavailable. Audio is still playing.":"Audio could not be restored. Try again.";
        publishState();
    }
}
bool VinylBackendSession::videoSwitchCancellable() const {
    if(!m_videoBusy)return false;
    if(m_videoRequest>=0)return m_loaded;
    return m_videoActive&&!m_audioResource.isEmpty()
        &&!resourceExpired(m_audioResource.value("url").toString());
}
void VinylBackendSession::toggleVideo() {
    if(m_shutdown||!m_profileActive)return;
    if(m_videoBusy&&m_videoRequest>=0) {
        if(!videoSwitchCancellable())return;
        QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QString,QString("video-stream")));
        m_videoRequest=-1;m_videoBusy=false;publishState();return;
    }
    if((m_videoBusy&&!videoSwitchCancellable())||(!m_videoBusy&&!m_loaded)
        ||(!m_videoActive&&!hasVideo()))return;
    if(m_videoActive&&!m_audioResource.isEmpty()&&!resourceExpired(m_audioResource.value("url").toString()))
        commitVideoMode(false,m_audioResource);
    else resolveVideoMode(!m_videoActive);
}
void VinylBackendSession::resolveVideoMode(bool video) {
    if(m_shutdown||!m_profileActive||activeIndex()<0)return;
    m_targetVideo=video;m_videoBusy=true;m_videoError.clear();
    const auto value=activeRow().value("trackId").toString();
    m_videoRequest=requestSource("video-stream",video?"resolveVideo":"resolveFallback",value,
        video?QVariantMap{{"videoId",m_videoCandidate.value("videoId")}}:QVariantMap{});
    if(m_videoRequest<0){m_videoBusy=false;m_videoError="This source cannot play the video.";}
    else {
        const auto generation=m_epoch;const auto request=m_videoRequest;
        QTimer::singleShot(95000,this,[this,generation,request] {
            if(generation!=m_epoch||request!=m_videoRequest)return;
            QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QString,QString("video-stream")));
            m_videoRequest=-1;m_videoBusy=false;m_videoError="The source did not respond. Try again.";publishState();
        });
    }
    publishState();
}
bool VinylBackendSession::commitVideoMode(bool video,const QVariantMap &resource) {
    const auto *active=m_backend->queue.activeOccurrence();if(!active)return false;
    const auto url=resource.value("url").toString(),audio=resource.value("audioUrl").toString();
    if(video) {
        if(resource.value("version").toInt()!=1||resource.value("videoId")!=m_videoCandidate.value("videoId")
            ||resource.value("hasVideo").metaType().id()!=QMetaType::Bool||!resource.value("hasVideo").toBool()
            ||resource.value("hasAudio").metaType().id()!=QMetaType::Bool||!resource.value("hasAudio").toBool()
            ||!videoResourceUrl(url)||(!audio.isEmpty()&&!videoResourceUrl(audio)))return false;
    }
    QVector<PlaybackRequestHeader> headers;
    const auto fields=resource.value("headers").toMap();
    for(auto it=fields.cbegin();it!=fields.cend();++it) {
        if(it.value().metaType().id()!=QMetaType::QString)return false;
        headers.append({it.key(),it.value().toString()});
    }
    const auto input=PlaybackLoadInput::create(url,headers);if(!input.ok())return false;
    // Capture the latest intent when resolution finishes, not when it begins.
    // The queue occurrence and durable listening session do not change.
    m_resume=position();m_resumeListeningFacts=bool(m_backend->listenRecorder);
    if(m_resumeListeningFacts)endListeningFacts(false);
    m_videoActive=video;m_videoBusy=true;m_videoAudioUrl=video?audio:QString();
    m_backend->input=*input.value;m_quality=resource.value("qualityLabel").toString();
    m_sourceKind=video?"youtube":resource.value("provider","youtube").toString();
    m_state="finding";
    const auto result=m_backend->coordinator.restartActive(active->occurrenceId(),*input.value);
    if(!result.ok){m_videoBusy=false;fail("The source could not be switched.");return false;}
    emit discontinuity(m_resume);publishState();return true;
}
void VinylBackendSession::retirePlayer() {
    const bool wasLoaded=m_loaded;
    m_loaded=false;m_backend->entries.clear();m_backend->seeks.clear();m_backend->physicalSeek.reset();m_backend->earlySeekRestart.reset();m_seekAccepted=false;
    m_backend->listenRecorder.reset();m_backend->listens.reset();
    auto *old=m_player.data();m_player=nullptr;
    if(old){
        // A new core still reports constructor defaults until its observers run.
        if(wasLoaded){m_playerVolume=old->volume();m_playerMute=old->mute();m_playerSpeed=old->speed();}
        disconnect(old,nullptr,this,nullptr);old->setPause(true);old->stopPlayback();old->deleteLater();}
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
    attachVideoSurface();
    m_backend->seeks.bindWork(token);
    const auto generation=m_epoch;
    connect(item,&MpvItem::nativePlaybackEvent,this,[this,item,token,generation](
        const QString &kind,qint64 entry,double position,double duration,quint64 load,qint64 observedMs,quint64 observationSerial) {
        if(m_shutdown||!m_profileActive||m_player!=item||generation!=m_epoch||load!=item->playbackGeneration())return;
        if(kind=="loaded") {
            if(!m_backend->entries.bind(entry,token)){fail("The playback identity could not be confirmed.");return;}
            if(m_sourceKind=="torrent") {
                const auto row=activeRow(),track=row.value("track").toMap(),edition=row.value("edition").toMap();
                const auto metadata=item->getProperty("metadata").toMap();QVariantMap tags;
                for(auto it=metadata.cbegin();it!=metadata.cend();++it)tags.insert(it.key().toLower(),it.value());
                const auto title=tags.value("title").toString(),album=tags.value("album").toString(),artist=tags.value("artist").toString();
                auto expectedArtist=track.value("artists").toStringList();
                if(expectedArtist.isEmpty())expectedArtist=track.value("artist").toString().split(" · ",Qt::SkipEmptyParts);
                const bool artistMismatch=!artist.isEmpty()&&!expectedArtist.isEmpty()
                    &&std::none_of(expectedArtist.cbegin(),expectedArtist.cend(),[&artist](const QString &value){return musicIdentity(value)==musicIdentity(artist);});
                const bool mismatch=(!title.isEmpty()&&musicIdentity(title)!=musicIdentity(track.value("title").toString()))
                    ||(!album.isEmpty()&&musicIdentity(album)!=musicIdentity(edition.value("name").toString()))||artistMismatch
                    ||(duration>0&&row.value("seconds").toDouble()>0&&std::abs(duration-row.value("seconds").toDouble())>2.0);
                if(mismatch){
                    QTimer::singleShot(0,this,[this,item,generation]{
                        if(m_player==item&&generation==m_epoch&&!tryFallback("Torrent audio metadata did not match the requested recording."))
                            fail("The audio did not match this recording.");
                    });return;
                }
            }
            if(!m_backend->coordinator.engineLoaded(token).ok)return;
            m_loaded=true;m_state="playing";m_videoBusy=false;
            m_lastProgress=0;m_lastProgressMs=monotonicMs();
            if(std::isfinite(duration)&&duration>0){m_duration=duration;m_backend->callbacks.durationObserved(token,duration);}
            m_resume=std::clamp(m_resume,0.0,std::max(0.0,m_duration-0.1));
            if(m_resume>0&&!m_backend->physicalSeek)m_backend->adapter.seek(qRound64(m_resume*1000));
            if(m_resumeListeningFacts){m_resumeListeningFacts=false;beginListeningFacts();}
            publishState();
        }else if(kind=="seek-start"||kind=="seek-loaded") {
            if(observationSerial>m_backend->seekObservationFence) {
                m_backend->seekStartSerial=observationSerial;
                m_backend->seeks.observeSeekStart(token);
            }
        }
        else if(kind=="restart") {
            if(!m_backend->physicalSeek||observationSerial<=m_backend->seekObservationFence)return;
            // The decoder and controller have different event queues. Retain
            // early restart evidence, but never complete a rejected command.
            if(!m_seekAccepted){m_backend->earlySeekRestart=Backend::SeekRestart{entry,position,observedMs,observationSerial};return;}
            if(m_backend->seekStartSerial<=m_backend->seekObservationFence)return;
            observeSeekRestart(token,entry,position,observedMs);
        }else if(kind.startsWith("ended-")){
            auto work=m_backend->entries.tokenFor(entry);if(!work||*work!=token)return;
            if(kind=="ended-eof")naturalEnd(*work);
            else if(kind=="ended-error"&&!tryFallback("Torrent audio could not be decoded or played."))fail("The audio could not be decoded or played. Retry this track.");
        }
    });
    connect(item,&MpvItem::trackedSeekReply,this,[this,item,token](quint64 serial,bool success,quint64 load,quint64 observationFence) {
        if(m_player!=item||load!=item->playbackGeneration()||!m_backend->physicalSeek
            ||m_backend->physicalSeek->sequence()!=serial||m_backend->physicalSeek->workToken()!=token)return;
        if(success){
            m_seekAccepted=true;
            m_backend->seekObservationFence=observationFence;
            if(const auto early=m_backend->earlySeekRestart){
                m_backend->earlySeekRestart.reset();
                if(early->observationSerial>observationFence&&m_backend->seekStartSerial>observationFence)
                    observeSeekRestart(token,early->entry,early->position,early->observedMs);
            }return;
        }
        auto failed=m_backend->seeks.failActive(*m_backend->physicalSeek);
        if(failed){
            const auto target=position();
            m_backend->callbacks.seekFailed(*failed,"mpv rejected seek");m_backend->physicalSeek.reset();
            if(!tryFallback("Torrent audio seek failed.",target))fail("The seek failed. Retry this track.");
        }
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
    connect(item,&MpvItem::playbackError,this,[this,item,generation](const QString &,const QString &) {
        if(m_player==item&&generation==m_epoch&&!tryFallback("Torrent audio could not be decoded or played."))fail("The audio could not be decoded or played. Retry this track.");
    });
    QVariantMap fields;for(const auto &header:headers)fields.insert(header.name,header.value);
    // This first host adapter admits whole local files. Never silently discard a
    // future bounded source's start/end options and play the wrong album span.
    if(!options.isEmpty()){fail("This bounded source is not connected to the local player yet.");retirePlayer();return;}
    // mpv skips forward by reading when the jump fits its stream buffer. The
    // shared 32 MiB video setting can therefore read most of a song instead of
    // requesting its target range, and block on missing torrent pieces. Use
    // mpv's ordinary 128 KiB stream buffer in Vinyl's private audio core.
    if(QUrl(resource).scheme().startsWith("http"))
        item->setProperty(QStringLiteral("stream-buffer-size"),128*1024);
    item->setProperty(QStringLiteral("audio-files"),m_videoActive&&!m_videoAudioUrl.isEmpty()?QStringList{m_videoAudioUrl}:QStringList{});
    item->setVolume(m_playerVolume);item->setMute(m_playerMute);item->setSpeed(m_playerSpeed);
    const auto load=[this,item,generation,resource,fields] {
        if(m_player!=item||generation!=m_epoch||m_shutdown)return;
        item->loadFileWithHeaders(resource,fields);item->setPause(m_paused);
    };
    // libmpv's video output needs the FBO/render context. An invisible audio
    // core never creates that context; the new video surface reports it ready.
    if(m_videoActive) {
        connect(item,&MpvItem::ready,this,load,
            Qt::ConnectionType(Qt::QueuedConnection|Qt::SingleShotConnection));
        item->update();
    }else load();
    QTimer::singleShot(15000,this,[this,item,generation] {
        if(m_player==item&&generation==m_epoch&&!m_loaded&&!tryFallback("Torrent audio did not start in time."))fail("The audio did not start in time. Retry this track.");
    });
    publishState();
}
void VinylBackendSession::setPaused(const PlaybackWorkToken &,bool paused) {
    // mpv's observed pause value may still predate a queued pause command.
    // Always send the adapter's latest intent so rapid pause/resume cannot drop
    // the resume because the observer cache still reports "playing".
    if(m_player)m_player->setPropertyAsync(QStringLiteral("pause"),paused);
}
bool VinylBackendSession::tryFallback(const QString &reason,std::optional<double> resume) {
    if(m_videoActive&&!m_shutdown&&m_profileActive&&activeIndex()>=0) {
        m_videoError="The video could not play. Returning to audio.";
        // The old audio input is transient and was already validated by its owner.
        // Expired signed resources are resolved again while the queue stays put.
        if(!m_audioResource.isEmpty()&&!resourceExpired(m_audioResource.value("url").toString()))
            return commitVideoMode(false,m_audioResource);
        if(!m_videoBusy)resolveVideoMode(false);
        return m_videoBusy;
    }
    if(m_shutdown||!m_profileActive||m_sourceKind!="torrent"||m_fallbackAttempted||activeIndex()<0)return false;
    const auto at=resume.value_or(position());const bool play=!m_paused;
    endPlayback("stop");cancelResolution();
    if(!m_backend->coordinator.stop().ok)return false;
    retirePlayer();m_fallbackAttempted=true;m_fallbackReason=reason;m_sourceKind="youtube";
    beginActive(at,play,true);return true;
}
void VinylBackendSession::seekExact(const PlaybackSeekCommand &command,double) {
    auto result=m_backend->seeks.submit(command,monotonicMs());if(result.dispatch)dispatchSeek(*result.dispatch);
}
void VinylBackendSession::dispatchSeek(const PlaybackSeekCommand &command) {
    if(!m_player)return;
    m_backend->physicalSeek=command;m_backend->earlySeekRestart.reset();m_backend->seekStartSerial=0;m_seekAccepted=false;
    m_backend->seekObservationFence=m_player->nativeObservationSerial();
    const auto transform=m_backend->timeState.transformFor(command.workToken());
    if(!transform)return;
    const auto physical=transform->physicalPositionForTrack(command.targetMs());
    if(physical)m_player->seekExactTracked(*physical/1000.0,command.sequence());
}
void VinylBackendSession::observeSeekRestart(const PlaybackWorkToken &token,qint64 entry,double position,qint64 observedMs) {
    auto work=m_backend->entries.tokenFor(entry);if(!work||*work!=token)return;
    auto advance=m_backend->seeks.observePlaybackRestart(token,millisecondsFromMpvSeconds(position),observedMs);
    if(!advance)return;
    m_backend->callbacks.seekApplied(advance->completed,position);
    m_resume=position;emit discontinuity(position);
    m_lastProgress=position;m_lastProgressMs=monotonicMs();
    m_backend->physicalSeek.reset();m_backend->earlySeekRestart.reset();m_seekAccepted=false;
    if(advance->nextPhysical)dispatchSeek(*advance->nextPhysical);
    publishState();
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
    const bool wasPaused=m_paused;
    QScopedValueRollback<bool> direction(m_navigationBack,delta<0);
    playQueue(index);
    if(wasPaused)setPaused(true);
}
void VinylBackendSession::naturalEnd(const PlaybackWorkToken &token) {
    if(!m_backend->adapter.validateWorkToken(token).ok)return;
    endPlayback("natural-eof");
    QScopedValueRollback<bool> continuation(m_naturalAdvancing,true);
    if(m_repeat=="one"){playQueue(activeIndex());return;}
    if(activeIndex()+1<m_backend->queue.size()||m_repeat=="all"){next(1);return;}
    m_backend->callbacks.ended(token);m_resume=m_duration;m_loaded=false;m_paused=true;m_state="ended";publishState();
}
void VinylBackendSession::fail(const QString &message) {
    endPlayback("failure");m_error=message;m_state="error";m_paused=true;
    if(m_backend->session.activeWorkToken())m_backend->callbacks.failed(*m_backend->session.activeWorkToken(),"host_failure",message);
    if(m_player)m_player->setPause(true);publishState();
}
void VinylBackendSession::retry() { if(activeIndex()>=0){const auto at=position();cancelResolution();
    if(!m_backend->coordinator.stop().ok){fail("The previous queue occurrence could not be retired.");return;}beginActive(at);} }
void VinylBackendSession::suspend() {
    const auto at=position();endPlayback("stop");cancelResolution();m_backend->coordinator.stop();retirePlayer();
    m_resume=at;m_paused=true;m_state=activeIndex()>=0?"paused":"idle";publishState();
}
void VinylBackendSession::clearProfile() {
    suspend();m_backend->coordinator.clear();m_rows.clear();m_profileActive=false;
    m_sourceKind.clear();m_fallbackReason.clear();m_forceFallback=false;m_fallbackAttempted=false;
    m_resume=0;m_duration=0;m_repeat="off";m_shuffleEnabled=false;m_state="idle";m_error.clear();
    m_listeningOrigin="manual";m_lastPlaybackAtMs=0;m_radioContext.clear();emit queueChanged();publishState();
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
        {"position",position()},{"duration",m_duration},
        {"lastPlaybackAtMs",m_lastPlaybackAtMs},{"radioContext",m_radioContext}};
}
bool VinylBackendSession::inspectCheckpoint(const QVariantMap &checkpoint,QVariantList *decodedRows,QString *activeId) const {
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
    for(int i=0;i<rows.size();++i){
        const auto row=rows[i].toMap();auto item=occurrence(row);
        if(!item||item->occurrenceId()!=restored.value->queue.items()[i].occurrenceId()
            ||!item->sameMusicIdentity(restored.value->queue.items()[i]))return false;
    }
    const double position=checkpoint.value("position").toDouble(),duration=checkpoint.value("duration").toDouble();
    const auto repeat=checkpoint.value("repeatMode").toString();
    if(!std::isfinite(position)||position<0||!std::isfinite(duration)||duration<0
        ||!QStringList{"off","all","one"}.contains(repeat))return false;
    if(checkpoint.contains("lastPlaybackAtMs")) {
        bool timestampOk=false;
        const auto timestamp=checkpoint.value("lastPlaybackAtMs").toLongLong(&timestampOk);
        if(!timestampOk||timestamp<0)return false;
    }
    if(checkpoint.contains("radioContext")) {
        const auto storedContext=checkpoint.value("radioContext");
        if(storedContext.metaType().id()!=QMetaType::QVariantMap
            ||!normalizeRadioContext(storedContext.toMap(),nullptr))return false;
    }
    if(decodedRows)*decodedRows=rows;
    if(activeId){const auto *active=restored.value->queue.activeOccurrence();*activeId=active ? active->occurrenceId().value() : QString();}
    return true;
}
QVariantMap VinylBackendSession::checkpointSummary(const QVariantMap &checkpoint) const {
    QVariantList decoded;QString active;
    if(!inspectCheckpoint(checkpoint,&decoded,&active))return {};
    QVariantMap summaryContext;
    if(!normalizeRadioContext(checkpoint.value("radioContext").toMap(),&summaryContext))return {};
    QVariantMap summary{{"valid",true},{"empty",decoded.isEmpty()},
        {"lastPlaybackAtMs",checkpoint.value("lastPlaybackAtMs",0).toLongLong()},
        {"radioContext",summaryContext}};
    if(active.isEmpty())return summary;
    for(const auto &value:decoded)if(value.toMap().value("queueId").toString()==active)
        return {{"valid",true},{"empty",false},{"row",value},{"position",checkpoint.value("position")},
            {"duration",checkpoint.value("duration")},{"lastPlaybackAtMs",summary.value("lastPlaybackAtMs")},
            {"radioContext",summary.value("radioContext")}};
    return {};
}
bool VinylBackendSession::restoreCheckpoint(const QVariantMap &checkpoint) {
    QVariantList rows;
    if(!inspectCheckpoint(checkpoint,&rows,nullptr))return false;
    auto profile=ProfileScopeId::parse(m_profile);
    auto restored=VinylSessionPersistence::restore(*profile.value,checkpoint.value("backendState").toString().toUtf8());
    QHash<QString,QVariantMap> metadata;
    for(const auto &value:rows){const auto row=value.toMap();metadata.insert(row.value("queueId").toString(),row);}
    const double position=checkpoint.value("position").toDouble(),duration=checkpoint.value("duration").toDouble();
    const auto repeat=checkpoint.value("repeatMode").toString();
    const auto lastPlaybackAtMs=checkpoint.value("lastPlaybackAtMs",0).toLongLong();
    QVariantMap radioContext;
    if(!normalizeRadioContext(checkpoint.value("radioContext").toMap(),&radioContext))return false;
    suspend();m_backend->coordinator.clear();m_backend->queue=std::move(restored.value->queue);m_rows=std::move(metadata);
    m_repeat=repeat;m_resume=position;m_duration=duration;m_paused=true;m_error.clear();
    m_lastPlaybackAtMs=lastPlaybackAtMs;m_radioContext=std::move(radioContext);
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
    m_resume=position;m_duration=duration;m_shuffleEnabled=shuffle;m_state=index>=0?"paused":"idle";
    m_listeningOrigin="manual";m_lastPlaybackAtMs=0;m_radioContext.clear();emit queueChanged();publishState();return true;
}

void VinylBackendSession::endPlayback(const QString &reason) {
    m_endingReason=reason;emit playbackEnding(reason=="natural-eof");
    endListeningFacts(reason=="natural-eof");
}
QVariantMap VinylBackendSession::lyricsContext() const {
    if(!m_profileActive||m_shutdown)return {};
    const auto row=activeRow(),edition=row.value("edition").toMap(),album=edition.value("sourceAlbum").toMap();
    if(row.isEmpty())return {};
    const auto track=selectedTrack(row);
    const auto recording=listeningIdentity().value("entityRef").toMap();
    if(recording.isEmpty())return {};
    auto artists=track.value("artists").toStringList();
    if(artists.isEmpty())artists=edition.value("artists").toStringList();
    auto albumName=track.value("albumName").toString();
    if(albumName.isEmpty()&&album.value("type")!="Song"&&album.value("type")!="Playlist")
        albumName=edition.value("name").toString();
    QVariantMap result{{"recording",recording},{"occurrenceId",row.value("queueId")},{"profileScope",m_profile},
        {"trackName",track.value("title")},{"artistName",artists.join(", ")},{"artistNames",artists},
        {"albumName",albumName},{"duration",track.value("seconds",row.value("seconds"))},
        {"editionId",row.value("editionId")}};
    if(edition.value("origin")=="local"&&track.contains("lyrics"))result["embeddedLyrics"]=track.value("lyrics");
    return result;
}
QVariant VinylBackendSession::recordingTimeOffsetMs() const {
    if(!m_loaded||m_shutdown||!m_profileActive)return {};
    // A catalogue relationship is not a measured musical time offset.
    if(m_videoActive)return {};
    const auto row=activeRow(),edition=row.value("edition").toMap(),album=edition.value("sourceAlbum").toMap();
    const auto track=selectedTrack(row);
    // Direct local timed text describes this local file's clock. A resolved
    // catalogue recording is aligned only for explicitly identified album audio.
    if(m_sourceKind=="local"&&track.contains("lyrics"))return qint64(0);
    if(m_sourceKind=="youtube"&&track.value("videoType")=="MUSIC_VIDEO_TYPE_ATV"
        &&m_duration>0&&row.value("seconds").toDouble()>0
        &&std::abs(m_duration-row.value("seconds").toDouble())<=2.0)return qint64(0);
    return {};
}
QVariantMap VinylBackendSession::listeningIdentity() const {
    const auto row=activeRow(),edition=row.value("edition").toMap(),album=edition.value("sourceAlbum").toMap();
    const bool yt=album.value("origin")=="youtube-music";
    const auto track=selectedTrack(row);
    auto convert=[&](QVariantMap raw,const QString &kind,const QString &id) {
        raw["kind"]=kind;raw["provider"]=yt?"youtube-music":"local";raw["providerId"]=id;
        if(yt)raw["id"]=(kind=="song"?"ytm:track:":"ytm:album:")+id;
        return VinylRecommendationIdentity::fromCatalogue(raw).value("ref").toMap();
    };
    const auto recording=convert(track,"song",yt?track.value("videoId").toString():row.value("trackId").toString());
    if(recording.isEmpty())return {};
    QVariantMap result{{"entityRef",recording}};
    const auto albumType=album.value("type").toString();
    if(albumType!="Song" && albumType!="Playlist") {
        const auto release=convert(album,"album",yt?album.value("providerIds").toMap().value("youtube-music",album.value("browseId")).toString():album.value("id").toString());
        if(!release.isEmpty())result["releaseRef"]=release;
    }
    QVariantList artists;
    auto ids=track.value("artistIds").toStringList();if(ids.isEmpty())ids=album.value("artistIds").toStringList();
    for(const auto &id:ids) {
        const bool artistYt=id.startsWith("ytm:artist:");
        const auto ref=VinylRecommendationIdentity::fromCatalogue({{"kind","artist"},{"provider",artistYt?"youtube-music":"local"},
            {"providerId",artistYt?id.mid(11):id}}).value("ref").toMap();
        if(!ref.isEmpty() && !artists.contains(ref))artists.append(ref);
    }
    result["artistRefs"]=artists;return result;
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
    if(m_shutdown||!m_profileActive||!m_loaded)return false;
    // Resume progress remains usable when durable listening history is off.
    if(!m_backend->listenRecorder&&consuming&&!m_paused&&!seekPending()&&m_state=="playing")beginListeningFacts();
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
    const bool advanced=m_backend->projectedListeningMs>before;
    if(advanced) {
        m_lastPlaybackAtMs=wall;
        publishState();
    }
    return consuming&&advanced;
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

namespace {
// Collection remains the single transaction/persistence owner. Support both its
// native port and the QVariant ports used by the existing QML contract fixtures.
QVariantList personalEntries(QObject *store,bool *ok) {
    *ok=false;QVariantList entries;if(!store)return entries;
    const auto meta=store->metaObject();int index=meta->indexOfMethod("items(QString)");
    if(index>=0) {
        auto method=meta->method(index);
        if(method.returnMetaType().id()==QMetaType::QVariant) {
            QVariant value;*ok=method.invoke(store,Qt::DirectConnection,Q_RETURN_ARG(QVariant,value),Q_ARG(QString,QString("Vinyl")));entries=value.toList();
        } else *ok=method.invoke(store,Qt::DirectConnection,Q_RETURN_ARG(QVariantList,entries),Q_ARG(QString,QString("Vinyl")));
    } else if((index=meta->indexOfMethod("items(QVariant)"))>=0) {
        QVariant value;*ok=meta->method(index).invoke(store,Qt::DirectConnection,Q_RETURN_ARG(QVariant,value),Q_ARG(QVariant,QVariant("Vinyl")));entries=value.toList();
    }
    return entries;
}
bool commitPersonal(QObject *store,const QStringList &removed,const QVariantList &entries) {
    if(!store)return false;
    const auto meta=store->metaObject();int index=meta->indexOfMethod("replaceItems(QString,QStringList,QVariantList)");
    if(index>=0) {
        bool result=false;auto method=meta->method(index);
        if(method.returnMetaType().id()==QMetaType::QVariant) {
            QVariant value;method.invoke(store,Qt::DirectConnection,Q_RETURN_ARG(QVariant,value),Q_ARG(QString,QString("Vinyl")),Q_ARG(QStringList,removed),Q_ARG(QVariantList,entries));result=value.toBool();
        } else method.invoke(store,Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(QString,QString("Vinyl")),Q_ARG(QStringList,removed),Q_ARG(QVariantList,entries));
        return result;
    }
    if((index=meta->indexOfMethod("replaceItems(QVariant,QVariant,QVariant)"))<0)return false;
    QVariant result;meta->method(index).invoke(store,Qt::DirectConnection,Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant("Vinyl")),Q_ARG(QVariant,QVariant(removed)),Q_ARG(QVariant,QVariant(entries)));return result.toBool();
}
QString legacyItemId(const QString &playlist,int index) {
    return "item:"+QString::fromLatin1(QCryptographicHash::hash((playlist+":"+QString::number(index)).toUtf8(),QCryptographicHash::Sha256).toHex());
}
QVariantMap normalizedPlaylist(QVariantMap entry) {
    auto payload=entry.value("payload").toMap();auto refs=payload.value("occurrences").toList();
    for(int i=0;i<refs.size();++i){auto ref=refs[i].toMap();if(ref.value("itemId").toString().isEmpty())ref["itemId"]=legacyItemId(entry.value("id").toString(),i);refs[i]=ref;}
    payload["occurrences"]=refs;entry["payload"]=payload;return entry;
}
bool wholeCoordinate(const QVariant &value) {
    bool ok=false;const double number=value.toDouble(&ok);
    return ok && std::isfinite(number) && number>=0 && number<=1000000 && number==std::floor(number);
}
QVariantMap personalFailure(const QString &error) {return {{"ok",false},{"error",error}};}
bool personalSourceMatches(const QVariantMap &album,const QVariantMap &row,int flat) {
    QVariantMap source=album;
    const auto editions=album.value("editions").toList();
    if(!editions.isEmpty()) {
        source.clear();
        for(const auto &value:editions)if(value.toMap().value("id")==row.value("editionId")){source=value.toMap();break;}
        if(source.isEmpty())return false;
    } else if(album.value("id")!=row.value("editionId"))return false;
    auto tracks=source.value("tracks").toList();
    std::stable_sort(tracks.begin(),tracks.end(),[](const QVariant &a,const QVariant &b){
        return qMax(1,a.toMap().value("disc",1).toInt())<qMax(1,b.toMap().value("disc",1).toInt());
    });
    return flat>=0&&flat<tracks.size()&&tracks[flat].toMap().value("id")==row.value("trackId");
}
}

QString VinylBackendSession::personalSongId(const QVariantMap &row) const {
    if(!validRow(row)||!wholeCoordinate(row.value("discIndex"))||!wholeCoordinate(row.value("trackIndex")))return {};
    const auto album=row.value("albumId").toString(),edition=row.value("editionId").toString();
    if(album.isEmpty()||edition.isEmpty()||album.size()>4096||edition.size()>4096)return {};
    QJsonArray coordinate{album,edition,row.value("discIndex").toInt(),row.value("trackIndex").toInt(),row.value("trackId").toString()};
    return "vinyl:song:"+QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(coordinate).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
bool VinylBackendSession::personalRowAvailable(const QVariantMap &row) const {
    if(personalSongId(row).isEmpty())return false;
    const auto track=row.value("track").toMap();if(track.value("available",true).toBool()==false)return false;
    const auto path=track.value("localPath").toString();
    if(path.isEmpty())return row.value("edition").toMap().value("origin").toString()=="youtube-music";
    const QUrl url(path);return QFileInfo(url.isLocalFile()?url.toLocalFile():path).isFile();
}
QVariantMap VinylBackendSession::personalRecords() const {
    if(m_shutdown||!m_profileActive)return {{"songs",QVariantList{}},{"playlists",QVariantList{}}};
    bool ok=false;const auto entries=personalEntries(m_collection,&ok);QVariantList songs,playlists;
    if(ok)for(const auto &value:entries){auto entry=value.toMap();const auto type=entry.value("type").toString();if(type=="song")songs.append(entry);else if(type=="playlist")playlists.append(normalizedPlaylist(entry));}
    return {{"songs",songs},{"playlists",playlists}};
}
QVariantMap VinylBackendSession::mutatePersonal(const QString &operation,const QVariantMap &args) {
    if(m_shutdown||!m_profileActive||!m_collection)return personalFailure("Your music library is unavailable.");
    if(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact).size()>8*1024*1024)return personalFailure("The selected music is too large to save in one operation.");
    const auto health=m_collection->property("healthy");if(health.isValid()&&!health.toBool())return personalFailure(m_collection->property("persistenceError").toString());
    bool readable=false;const auto entries=personalEntries(m_collection,&readable);if(!readable)return personalFailure("The music library could not be read.");
    QHash<QString,QVariantMap> byId;for(const auto &value:entries){auto entry=value.toMap();byId.insert(entry.value("id").toString(),entry);}
    const auto failure=[](){return personalFailure("The selected track or playlist is invalid. Your library was kept.");};
    if(operation=="removeSavedSong") {
        const auto id=args.value("id").toString();
        if(!byId.contains(id)||byId[id].value("type").toString()!="song")return failure();
        if(!commitPersonal(m_collection,{id},{}))return personalFailure("The song could not be removed. Your library was kept.");
        return {{"ok",true},{"id",id}};
    }
    QVariantList writes;QStringList removed;QString id=args.value("id").toString();
    const bool creating=operation=="createPlaylist",saving=operation=="saveSong";
    QVariantMap playlist;
    if(!saving) {
        if(creating) {
            id="vinyl:playlist:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            playlist={{"id",id},{"type","playlist"},{"payload",QVariantMap{{"version",2},{"artistId",args.value("artistId").toString()},{"occurrences",QVariantList{}}}}};
        } else {
            if(!byId.contains(id)||byId[id].value("type").toString()!="playlist")return failure();
            playlist=normalizedPlaylist(byId[id]);
        }
        if(creating||operation=="renamePlaylist") {
            auto profile=ProfileScopeId::parse(m_profile);auto playlistId=PlaylistId::parse(id);
            if(!profile.ok()||!playlistId.ok())return failure();
            auto model=Playlist::create(*profile.value,*playlistId.value,args.value("name").toString().trimmed(),QDateTime::currentMSecsSinceEpoch(),PlaylistProvenanceKind::Manual);
            if(!model.ok())return personalFailure("Enter a playlist name of 1–256 characters without line breaks.");
            playlist["title"]=model.value->name();
        }
    }
    if(saving||creating||operation=="addPlaylistRows") {
        QVariantList rows=saving ? QVariantList{args.value("row")} : args.value("rows").toList();
        if(rows.size()>4096 || (!saving&&!creating&&rows.isEmpty()))return failure();
        auto payload=playlist.value("payload").toMap();auto refs=payload.value("occurrences").toList();
        if(refs.size()+rows.size()>4096)return personalFailure("This playlist has reached its track limit.");
        QHash<QString,QVariantMap> sources;
        for(const auto &value:rows) {
            const auto row=value.toMap();const auto songId=personalSongId(row);
            if(songId.isEmpty())return failure();
            const auto edition=row.value("edition").toMap(),track=row.value("track").toMap(),album=edition.value("sourceAlbum").toMap();
            const auto albumId=row.value("albumId").toString();
            if(album.value("id").toString()!=albumId||edition.value("id").toString()!=row.value("editionId").toString()
                || (byId.contains(albumId)&&byId[albumId].value("type").toString()!="album")
                || QJsonDocument::fromVariant(album).toJson(QJsonDocument::Compact).size()>8*1024*1024)return failure();
            int flat=row.value("trackIndex").toInt();const auto discs=edition.value("discs").toList();for(int d=0;d<row.value("discIndex").toInt();++d)flat+=discs[d].toList().size();
            if(!personalSourceMatches(album,row,flat)||(saving&&byId.contains(songId)&&byId[songId].value("type").toString()!="song"))return failure();
            if(byId.contains(albumId)&&!personalSourceMatches(byId[albumId].value("payload").toMap().value("album").toMap(),row,flat))return failure();
            QVariantMap ref{{"albumId",albumId},{"editionId",row.value("editionId")},{"trackId",row.value("trackId")},{"discIndex",row.value("discIndex")},{"trackIndex",row.value("trackIndex")},{"index",flat},
                {"title",track.value("title")},{"artist",track.value("artist",row.value("artist"))},{"cover",edition.value("cover")},{"seconds",track.value("seconds")}};
            if(saving) {
                id=songId;
                if(!args.value("saved").toBool()){removed.append(id);continue;}
                writes.append(QVariantMap{{"id",id},{"type","song"},{"title",ref.value("title")},{"cover",ref.value("cover")},{"payload",QVariantMap{{"version",1},{"ref",ref}}}});
            } else {ref["itemId"]="item:"+QUuid::createUuid().toString(QUuid::WithoutBraces);refs.append(ref);}
            auto source=byId.value(albumId);auto sourcePayload=source.value("payload").toMap();
            if(source.isEmpty())source={{"id",albumId},{"type","album"},{"title",album.value("title")},{"cover",album.value("cover")},{"artist",album.value("artists").toStringList().join(" · ")},{"browseId",albumId}};
            if(!sourcePayload.contains("saved"))sourcePayload["saved"]=byId.contains(albumId);
            sourcePayload["version"]=1;
            // An older queue snapshot must not replace freshly imported metadata.
            if(!sourcePayload.contains("album"))sourcePayload["album"]=album;
            source["payload"]=sourcePayload;sources.insert(albumId,source);
        }
        for(auto it=sources.cbegin();it!=sources.cend();++it)writes.append(it.value());
        if(!saving){payload["version"]=2;payload["occurrences"]=refs;playlist["payload"]=payload;}
    } else if(operation=="removePlaylistItem"||operation=="movePlaylistItem") {
        auto payload=playlist.value("payload").toMap();const auto refs=payload.value("occurrences").toList();
        if(refs.size()>4096)return failure();
        VinylQueue order;QHash<QString,QVariantMap> metadata;
        for(const auto &value:refs) {
            const auto ref=value.toMap();auto item=PlaylistItemId::parse(ref.value("itemId").toString());auto queueId=QueueOccurrenceId::parse(ref.value("itemId").toString());
            if(!item.ok()||!queueId.ok())return failure();
            auto occurrence=QueueOccurrence::createLocal(*queueId.value,ref.value("trackId").toString());
            if(!occurrence.ok()||!order.append(*occurrence.value).ok)return failure();metadata.insert(item.value->value(),ref);
        }
        auto item=QueueOccurrenceId::parse(args.value("itemId").toString());if(!item.ok())return failure();
        if(operation=="removePlaylistItem") {if(!order.remove(*item.value).ok)return failure();}
        else {if(!wholeCoordinate(args.value("index"))||!order.move(*item.value,args.value("index").toInt()).ok)return failure();}
        QVariantList reordered;for(const auto &entry:order.items())reordered.append(metadata.value(entry.occurrenceId().value()));
        payload["version"]=2;payload["occurrences"]=reordered;playlist["payload"]=payload;
    } else if(operation=="deletePlaylist") removed.append(id);
    else if(operation!="renamePlaylist")return failure();
    if(!saving && operation!="deletePlaylist")writes.append(playlist);
    if(!commitPersonal(m_collection,removed,writes))return personalFailure(m_collection->property("persistenceError").toString().isEmpty()?"The music library could not be saved. Your previous library was kept.":m_collection->property("persistenceError").toString());
    return {{"ok",true},{"id",id}};
}
