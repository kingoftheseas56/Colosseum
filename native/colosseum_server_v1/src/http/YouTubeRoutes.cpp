#include "server1/http/YouTubeRoutes.h"
#include "server1/http/HttpContract.h"
#include <QJsonDocument>
#include <QPointer>
#include <memory>
namespace server1::http {
namespace {
struct Route { QPointer<youtube::YouTubeResolver> resolver; bool json; };
struct Pending {void *handle;~Pending(){server1_http_deferred_destroy(handle);}};
int handler(const void *request,void *response,server1_http_next_fn,void *,void *context) {
    const auto route=*static_cast<Route *>(context);
    const auto *id=server1_http_request_param(request,"id");
    const QString video=QString::fromUtf8(id?id:"");
    auto pending=std::make_shared<Pending>();pending->handle=server1_http_response_defer(response);
    auto complete=[pending,json=route.json](youtube::Result result) {
        auto *res=server1_http_deferred_response(pending->handle);if(!res)return;
        const auto url=result.format.value("url").toString();
        if(json) {
            // M564 overwrites its initial 403 with 404 when the failed callback has no format.
            QJsonObject body=result.error.isEmpty()?QJsonObject{}:QJsonObject{{"err",result.error}};
            if(!url.isEmpty())body=result.format;
            server1_http_response_set_status(res,url.isEmpty()?404:200);
            server1_http_response_set_header(res,"Content-Type","application/json");
            const auto bytes=QJsonDocument(body).toJson(QJsonDocument::Compact);
            server1_http_response_set_body(res,bytes.data(),bytes.size());
        }else{
            server1_http_response_set_body(res,"",0);
            server1_http_response_set_status(res,!result.error.isEmpty()?403:url.isEmpty()?404:301);
            if(result.error.isEmpty()&&!url.isEmpty())server1_http_response_set_header(res,"Location",url.toUtf8().constData());
        }
        server1_http_deferred_complete(pending->handle);
    };
    if(route.resolver)route.resolver->resolve(video,std::move(complete));
    else complete({{},"YouTube resolver unavailable"});
    return 1;
}
}
bool installYouTubeRoutes(void *router,youtube::YouTubeResolver &resolver) {
    for(bool json:{true,false}){
        auto *route=new Route{&resolver,json};
        if(!server1_http_router_add_handler(router,0,0,"GET",json?"/yt/:id.json":"/yt/:id",handler,route,[](void *p){delete static_cast<Route *>(p);})) {delete route;return false;}
    }return true;
}
}
