#include "server1/http/AncillaryRoutes.h"
#include "server1/http/HttpContract.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <memory>
namespace server1::http {
namespace {
// M934 names, MIME types and external-file integrity metadata only.
// GeneratedDefault embeds independently generated payloads below, not M934 bytes.
struct Sample {const char *name;const char *mime;qsizetype size;const char *sha256;};
const Sample samples[]={
    {"aac-6chan.wav","audio/x-wav",232,"99c1c9ff7074a1438e44232009f5e1f1ce2db283c66092ef412f77ada563707d"},
    {"ac3-2chan.wav","audio/x-wav",4270,"24af006f3142b9c3614b916491f33fd0a9bb16daee70fbbfc6a29d5f16f3ed9a"},
    {"hevc.mkv","video/x-matroska",3955,"8f4075a670ebea23ddbe0b54ede2a2d47b722a5aebfb2709cfc951ff082c8b9c"}
};
struct GeneratedSample {const char *name;qsizetype size;const char *sha256;const char *base64;};
const GeneratedSample generatedSamples[]={
#include "GeneratedSamples.inc"
};
struct SampleRoute {const Sample *sample;QString directory;bool generated;};
struct Route { QPointer<media::HardwareProfiler> profiler;std::function<unsigned short()> port;bool device; };
struct Pending {void *handle;~Pending(){server1_http_deferred_destroy(handle);}};
int sampleHandler(const void *,void *res,server1_http_next_fn,void *,void *context) {
    const auto &route=*static_cast<const SampleRoute *>(context);const auto *sample=route.sample;
    const auto unavailable=[&]{server1_http_response_set_status(res,404);server1_http_response_set_body(res,"",0);return 1;};
    if(route.generated) {
        for(const auto &generated:generatedSamples)if(QByteArray(generated.name)==sample->name) {
            const auto bytes=QByteArray::fromBase64(generated.base64);
            if(bytes.size()!=generated.size || QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=generated.sha256)return unavailable();
            server1_http_response_set_status(res,200);server1_http_response_set_header(res,"Content-Type",sample->mime);
            server1_http_response_set_body(res,bytes.data(),bytes.size());return 1;
        }
        return unavailable();
    }
    if(route.directory.isEmpty())return unavailable();
    const QFileInfo info(QDir(route.directory).filePath(QString::fromLatin1(sample->name)));
    // Serve only these explicitly supplied fixture files, never a directory listing.
    if(!info.isFile() || info.isSymLink() || info.size()!=sample->size)return unavailable();
    QFile file(info.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))return unavailable();
    const auto bytes=file.read(sample->size+1);
    if(bytes.size()!=sample->size || QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=sample->sha256)return unavailable();
    server1_http_response_set_status(res,200);server1_http_response_set_header(res,"Content-Type",sample->mime);
    server1_http_response_set_body(res,bytes.data(),bytes.size());return 1;
}
int profileHandler(const void *,void *res,server1_http_next_fn,void *,void *context) {
    const auto &route=*static_cast<Route *>(context);
    if(!route.profiler){server1_http_response_set_status(res,500);return 1;}
    auto pending=std::make_shared<Pending>();pending->handle=server1_http_response_defer(res);
    const bool device=route.device;
    route.profiler->run(route.port?route.port():11470,[pending,device](QJsonValue result) {
        auto *response=server1_http_deferred_response(pending->handle);if(!response)return;
        server1_http_response_set_status(response,200);
        QByteArray body;
        if(device) {
            body=QJsonDocument(QJsonObject{{"availableHardwareAccelerations",result}}).toJson(QJsonDocument::Compact);
            server1_http_response_set_header(response,"Content-Type","application/json");
            server1_http_response_set_header(response,"Cache-Control","no-store");
        } else if(result.isArray()) body=QJsonDocument(result.toArray()).toJson(QJsonDocument::Compact);
        else {
            body="No viable hardware acceleration profiles detected";
            server1_http_response_set_status(response,500);
            server1_http_response_set_header(response,"Content-Type","text/plain");
            server1_http_response_set_header(response,"Content-Length",QByteArray::number(body.size()).constData());
        }
        server1_http_response_set_body(response,body.data(),body.size());server1_http_deferred_complete(pending->handle);
    });return 1;
}
}
bool installAncillaryRoutes(void *router,media::HardwareProfiler &profiler,std::function<unsigned short()> port,QString sampleDirectory,SampleMode mode) {
    for(const auto &sample:samples) {
        const auto path=QByteArray("/samples/")+sample.name;
        auto *route=new SampleRoute{&sample,sampleDirectory,sampleDirectory.isEmpty() && mode==SampleMode::GeneratedDefault};
        if(!server1_http_router_add_handler(router,0,0,"GET",path.constData(),sampleHandler,route,[](void *p){delete static_cast<SampleRoute *>(p);})) {delete route;return false;}
    }
    for(bool device:{true,false}) {
        auto *route=new Route{&profiler,port,device};
        if(!server1_http_router_add_handler(router,0,0,"GET",device?"/device-info":"/hwaccel-profiler",profileHandler,route,[](void*p){delete static_cast<Route*>(p);})) {delete route;return false;}
    }
    return true;
}
}
