#include "server1/media/HardwareProfiler.h"
#include "server1/media/FfmpegArguments.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <algorithm>
namespace server1::media {
namespace {
QString platformName() {
#if defined(Q_OS_ANDROID)
    return "android";
#elif defined(Q_OS_IOS)
    return "ios";
#elif defined(Q_OS_MACOS)
    return "darwin";
#elif defined(Q_OS_WIN)
    return "win32";
#elif defined(Q_OS_LINUX)
    return "linux";
#else
    return "unknown";
#endif
}
QStringList candidates(const QString &platform) {
    if(platform=="darwin")return {"videotoolbox"};
    if(platform=="win32")return {"qsv-win","nvenc-win","amf"};
    if(platform=="linux")return {"qsv-linux","nvenc-linux","vaapi-renderD128"};
    if(platform=="rpi")return {"v4l2m2m"};
    return {};
}
}
struct HardwareProfiler::Impl {
    settings::SettingsStore &settings;
    std::function<void()> changed;
    QNetworkAccessManager network;
    QString platform;
    Fetch fetch;
    explicit Impl(settings::SettingsStore &s,std::function<void()> c,QString p):settings(s),changed(std::move(c)),platform(std::move(p)){}
    void save(policy::Value value) {
        settings.extend(value);
        if(changed)changed();
        (void)settings.save();
    }
};
HardwareProfiler::HardwareProfiler(settings::SettingsStore &settings,std::function<void()> changed,QObject *parent,QString platform,Fetch fetch,int requestTimeoutMs)
    :QObject(parent),impl_(std::make_unique<Impl>(settings,std::move(changed),platform.isEmpty()?platformName():platform)) {
    if(fetch)impl_->fetch=std::move(fetch);
    else impl_->fetch=[this,timeout=std::max(1,requestTimeoutMs)](QUrl url,FetchResult cb) {
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        auto *reply=impl_->network.get(request);
        auto *deadline=new QTimer(reply);deadline->setSingleShot(true);
        connect(deadline,&QTimer::timeout,reply,[reply]{reply->abort();});deadline->start(timeout);
        connect(reply,&QNetworkReply::finished,this,[reply,deadline,cb=std::move(cb)] {
            deadline->stop();
            const auto code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const bool responded=code.isValid();
            const int status=code.toInt();
            reply->deleteLater();
            cb(responded,responded && reply->error()==QNetworkReply::NoError && status>=200 && status<300);
        });
    };
}
HardwareProfiler::~HardwareProfiler()=default;
void HardwareProfiler::run(unsigned short port,Result callback) {
    const auto platform=!qgetenv("IS_IOS").isEmpty()?QString("ios"):impl_->platform;
    const auto profiles=candidates(platform);
    const auto *hardware=impl_->settings.find("transcodeHardwareAccel");
    const auto *known=impl_->settings.find("allTranscodeProfiles");
    const bool enabled=hardware && policy::jsTruthy(*hardware);
    bool empty=true;
    if(known && known->kind()==policy::Value::Kind::Array)empty=known->asArray().empty();
    else if(known && known->kind()==policy::Value::Kind::String)empty=known->asString().empty();
    else if(known && known->kind()==policy::Value::Kind::Object) {
        const auto *length=known->find("length");empty=!length || !policy::jsTruthy(*length);
    }
    if(profiles.empty()) {
        QPointer<HardwareProfiler> guard(this);
        if(callback)callback(false);
        if(!guard)return;
        hardware=impl_->settings.find("transcodeHardwareAccel");
        if(hardware && policy::jsTruthy(*hardware))impl_->save(policy::Value::object({{"transcodeHardwareAccel",policy::Value::boolean(false)}}));
        return;
    }
    if(qgetenv("HLS_DEBUG").isEmpty() && !(enabled && empty) && !callback)return;
    struct Run {
        QPointer<HardwareProfiler> owner;
        QStringList profiles;
        QJsonArray supported;
        unsigned short port;
        Result callback;
        int index=0;
        bool audio=false;
    };
    auto state=std::make_shared<Run>();state->owner=this;state->profiles=profiles;state->port=port?port:11470;state->callback=std::move(callback);
    auto next=std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakNext=next;
    *next=[state,weakNext] {
        auto next=weakNext.lock();if(!next || !state->owner)return;
        const auto profile=state->profiles[state->index];
        const auto definition=FfmpegArguments::profiles()[profile].toObject();
        auto attempt=[state,profile](QString type,std::function<void(bool)> done) {
            if(!state->owner)return;
            const auto sample=type=="video"?QString("hevc.mkv"):QString("ac3-2chan.wav");
            const auto id=QString::number(state->port)+"-"+profile+"-"+type+"-"+sample;
            const auto base=QString("http://127.0.0.1:%1").arg(state->port);
            const auto target=base+"/hlsv2/"+id+"/"+type+"0.m3u8?mediaURL=http%3A%2F%2F127.0.0.1%3A"+QString::number(state->port)+"%2Fsamples%2F"+sample+"&profile="+profile+"&maxWidth=1200";
            state->owner->impl_->fetch(QUrl(target),[state,base,id,done](bool responded,bool ok) {
                if(!state->owner)return;
                if(!responded){done(false);return;}
                state->owner->impl_->fetch(QUrl(base+"/hlsv2/"+id+"/destroy"),[state,ok,done](bool,bool){if(state->owner)done(ok);});
            });
        };
        auto complete=[state,next,profile](bool video) {
            if(!state->owner)return;
            auto *impl=state->owner->impl_.get();
            if(state->audio && video) {
                if(state->supported.isEmpty())impl->save(policy::Value::object({{"transcodeHardwareAccel",policy::Value::boolean(true)}}));
                state->supported.append(profile);
            }
            if(++state->index<state->profiles.size()){(*next)();return;}
            if(!state->supported.isEmpty()) {
                if(state->callback)state->callback(state->supported);
                if(!state->owner)return;
                policy::Value::Array names;for(const auto &v:state->supported)names.push_back(policy::Value::string(v.toString().toStdString()));
                state->owner->impl_->save(policy::Value::object({{"allTranscodeProfiles",policy::Value::array(std::move(names))}}));
            } else {
                if(state->callback)state->callback(false);
                if(state->owner)state->owner->impl_->save(policy::Value::object({{"transcodeHardwareAccel",policy::Value::boolean(false)}}));
            }
        };
        auto video=[state,definition,attempt,complete](bool audio) {
            state->audio=audio;
            const auto v=definition["video"].toObject();
            if(!v["encoders"].toObject()["libx264"].toString().isEmpty() || !v["decoders"].toObject()["hevc"].toString().isEmpty())attempt("video",complete);else complete(true);
        };
        const auto audio=definition["audio"].toObject();
        if(!audio["encoders"].toObject()["aac"].toString().isEmpty() || !audio["decoders"].toObject()["ac3"].toString().isEmpty())attempt("audio",video);else video(true);
    };
    (*next)();
}
}
