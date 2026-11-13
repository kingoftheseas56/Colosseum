#pragma once
#include "server1/media/HardwareProfiler.h"
#include "server1/media/HlsV2Service.h"
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <vector>
#include <algorithm>
namespace server1::media {
// Deliberate lifecycle isolation from M805: private HLS registry, same sample
// URLs and conversion options. Never competes with playback's source LRU.
// Each attempt owns a fresh service, so repeated IDs and overlapping runs cannot
// reuse destroyed converters. Completion tears down that service; the following
// M805 destroy fetch is an acknowledged cleanup that has already happened.
// Dropping the Fetch cancels all active attempts without calling their callbacks.
inline HardwareProfiler::Fetch hardwareProbeFetch(std::function<std::shared_ptr<HlsV2Service>()> createService, int timeoutMs=15000) {
    struct Pending {std::shared_ptr<HlsV2Service> service;bool done=false;HardwareProfiler::FetchResult callback;QPointer<QTimer> timer;};
    struct Lifetime {
        std::vector<std::weak_ptr<Pending>> active;
        ~Lifetime(){for(auto &weak:active)if(auto p=weak.lock()){
            p->done=true;if(p->timer){p->timer->stop();p->timer->disconnect();}
            p->callback={};p->service.reset();
        }}
    };
    auto lifetime=std::make_shared<Lifetime>();
    return [createService=std::move(createService),timeoutMs,lifetime](QUrl url,HardwareProfiler::FetchResult callback) {
        const auto parts=url.path().split('/',Qt::SkipEmptyParts);
        if(parts.size()!=3 || parts[0]!="hlsv2"){callback(false,false);return;}
        const auto id=parts[1],action=parts[2];
        if(action=="destroy"){callback(true,true);return;}
        auto service=createService();
        if(!service){callback(false,false);return;}
        QPointer<HlsV2Service> guard(service.get());
        if(action!="video0.m3u8" && action!="audio0.m3u8"){callback(false,false);return;}
        const QUrlQuery query(url);
        QJsonObject options{{"mediaURL",query.queryItemValue("mediaURL",QUrl::FullyDecoded)},
                            {"profile",query.queryItemValue("profile")},{"maxWidth",1200}};
        QString error;auto converter=guard->acquire(id,options,&error);
        if(!converter){callback(true,false);return;}
        auto p=std::make_shared<Pending>();p->service=std::move(service);p->callback=std::move(callback);
        auto &active=lifetime->active;
        active.erase(std::remove_if(active.begin(),active.end(),[](const auto &v){return v.expired();}),active.end());
        active.push_back(p);
        p->timer=new QTimer(guard);p->timer->setSingleShot(true);
        QObject::connect(p->timer,&QTimer::timeout,guard,[p,guard,id]{
            if(p->done)return;p->done=true;p->timer->deleteLater();
            guard->destroy(id);auto callback=std::move(p->callback);p->service.reset();callback(false,false);
        });
        p->timer->start(qMax(1,timeoutMs));
        converter->playlist(action.left(action.indexOf('.')),[p](const TrackConversionResult &result){
            if(p->done)return;p->done=true;
            if(p->timer){p->timer->stop();p->timer->deleteLater();}
            const bool ok=result.ok();
            auto callback=std::move(p->callback);p->service.reset();
            callback(true,ok);
        });
    };
}
}
