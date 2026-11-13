#include "server1/http/RootRoutes.h"
#include "server1/http/HttpContract.h"
#include "RootFormParser.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace server1::http {
namespace {
using policy::Value;
Value value(const QJsonValue &v) {
    if(v.isNull() || v.isUndefined()) return Value::null();
    if(v.isBool()) return Value::boolean(v.toBool());
    if(v.isDouble()) return Value::number(v.toDouble());
    if(v.isString()) return Value::string(v.toString().toStdString());
    if(v.isArray()) { Value::Array a; for(const auto &x:v.toArray()) a.push_back(value(x)); return Value::array(std::move(a)); }
    Value::Object o; const auto object=v.toObject(); for(auto i=object.begin();i!=object.end();++i) o.emplace_back(i.key().toStdString(),value(i.value())); return Value::object(std::move(o));
}
std::vector<std::string> interfaces() {
    std::vector<std::string> result;
    if(!qgetenv("NO_NETWORK_INTERFACES").isEmpty()) return result;
    for(const auto &nic:QNetworkInterface::allInterfaces())
        for(const auto &entry:nic.addressEntries())
            if(entry.ip().protocol()==QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback()) result.push_back(entry.ip().toString().toStdString());
    return result;
}
std::vector<std::string> locations() {
    std::vector<std::string> result;
#ifdef Q_OS_WIN
    // M414's physical-drive enumeration, including its E/D deprioritization.
    QProcess process; process.start("wmic", {"logicaldisk", "where", "drivetype=3", "get", "caption"});
    if(process.waitForFinished(10000)) {
        auto words=QString::fromLocal8Bit(process.readAllStandardOutput()).split(QRegularExpression("[\\n\\r ]+"),Qt::SkipEmptyParts);
        for(int i=1;i<words.size();++i) if(!words[i].startsWith('Q')) result.push_back(words[i].left(1).toStdString()+":\\");
        std::stable_sort(result.begin(),result.end(),[](const auto &a,const auto &b){return (a[0]=='E'||a[0]=='D') < (b[0]=='E'||b[0]=='D');});
    }
#endif
    return result;
}
Value options(const RootRoutesConfig &cfg) {
    Value::Array addresses{Value::object({{"name",Value::string("Disabled")},{"val",Value::string("")}})};
    for(const auto &address:cfg.networkInterfaces()) addresses.push_back(Value::object({{"name",Value::string(address)},{"val",Value::string(address)}}));
    Value::Array sizes;
    for(const auto &p:std::vector<std::pair<std::string,Value>>{{"no caching",Value::number(0)},{"2GB",Value::number(2147483648.)},{"5GB",Value::number(5368709120.)},{"10GB",Value::number(10737418240.)},{"∞",Value::null()}})
        sizes.push_back(Value::object({{"name",Value::string(p.first)},{"val",p.second}}));
    Value::Array opts{
        Value::object({{"id",Value::string("localAddonEnabled")},{"label",Value::string("ENABLE_LOCAL_FILES_ADDON")},{"type",Value::string("checkbox")}}),
        Value::object({{"id",Value::string("remoteHttps")},{"label",Value::string("ENABLE_REMOTE_HTTPS_CONN")},{"type",Value::string("select")},{"class",Value::string("https")},{"icon",Value::boolean(true)},{"selections",Value::array(std::move(addresses))}}),
        Value::object({{"id",Value::string("cacheSize")},{"label",Value::string("CACHING")},{"type",Value::string("select")},{"class",Value::string("caching")},{"icon",Value::boolean(true)},{"selections",Value::array(std::move(sizes))}})};
    Value::Array drives; for(const auto &drive:cfg.cacheLocations()) drives.push_back(Value::object({{"val",Value::string(drive)},{"name",Value::string(drive.substr(0,2))}}));
    if(!drives.empty()) opts.push_back(Value::object({{"id",Value::string("cacheRoot")},{"label",Value::string("SETTINGS_CACHING_DRIVE")},{"type",Value::string("select")},{"class",Value::string("caching")},{"selections",Value::array(std::move(drives))}}));
    return Value::array(std::move(opts));
}
void reply(void *res,int status,const std::string &body,const char *type=nullptr,bool noStore=false) {
    server1_http_response_set_status(res,status);
    if(type) server1_http_response_set_header(res,"content-type",type);
    if(noStore) server1_http_response_set_header(res,"Cache-Control","no-store");
    server1_http_response_set_body(res,body.data(),body.size());
}
struct Route { std::shared_ptr<RootRoutesConfig> config; int kind; };
int handle(const void *req,void *res,server1_http_next_fn,void *,void *context) {
    const auto &route=*static_cast<Route *>(context); auto &cfg=*route.config;
    try {
        if(route.kind==0) reply(res,200,"{\"success\":true}","application/json");
        else if(route.kind==1) {
            Value::Array addresses; for(const auto &a:cfg.networkInterfaces()) addresses.push_back(Value::string(a));
            reply(res,200,policy::jsonStringify(Value::object({{"availableInterfaces",Value::array(std::move(addresses))}})),"application/json",true);
        } else if(route.kind==2) {
            reply(res,200,policy::jsonStringify(Value::object({{"options",options(cfg)},{"values",cfg.settings->value()},{"baseUrl",Value::string(cfg.baseUrl?cfg.baseUrl():"")}})),"application/json",true);
        } else if(route.kind==3) {
            const auto body=QByteArray(server1_http_request_body(req),server1_http_request_body_size(req));
            const char *raw=server1_http_request_header_value_at(req,"content-type",0);
            const QByteArray contentType=raw?QByteArray(raw).toLower():QByteArray();
            Value extension=Value::object({});
            if(contentType.startsWith("application/json") && !body.isEmpty()) {
                QJsonParseError error; auto doc=QJsonDocument::fromJson(body,&error);
                if(error.error!=QJsonParseError::NoError) {reply(res,400,"Invalid JSON","text/plain");return 1;}
                extension=doc.isArray()?value(doc.array()):value(doc.object());
            } else if(contentType.startsWith("application/x-www-form-urlencoded")) {
                extension=detail::parseRootForm(body);
            }
            cfg.settings->extend(extension);
            if(cfg.settingsChanged) cfg.settingsChanged();
            (void)cfg.settings->save(); // M564 deliberately ignores the save callback's error.
            reply(res,200,"{\"success\":true}","application/json",true);
        } else {
            const char *host=server1_http_request_header_value_at(req,"host",0);
            if(!host || !*host) {reply(res,500,"No host header","text/plain");return 1;}
            // Current transport is TCP only. Do not infer TLS from untrusted forwarding headers.
            const auto encoded=QUrl::toPercentEncoding(QString::fromUtf8((std::string("http://")+host).c_str()),"!'()*-._~");
            const auto location=cfg.webUILocation+(cfg.webUILocation.find('?')==std::string::npos?"?":"&")+"streamingServer="+encoded.toStdString();
            server1_http_response_set_header(res,"location",location.c_str()); reply(res,307,"");
        }
    } catch(const std::exception &e) {
        if(route.kind==1) server1_http_response_set_header(res,"Content-Length",QByteArray::number(QString::fromUtf8(e.what()).size()).constData());
        reply(res,500,e.what(),"text/plain",true);
    }
    return 1;
}
}
bool installRootRoutes(void *router,RootRoutesConfig config) {
    if(!router || !config.settings) return false;
    if(!config.networkInterfaces) config.networkInterfaces=interfaces;
    if(!config.cacheLocations) config.cacheLocations=locations;
    if(config.webUILocation.empty()) config.webUILocation=qEnvironmentVariable("WEBUI_LOCATION").toStdString();
    if(config.webUILocation.empty()) config.webUILocation="https://app.strem.io/shell-v4.4/";
    auto shared=std::make_shared<RootRoutesConfig>(std::move(config));
    const char *paths[]={"/heartbeat","/network-info","/settings","/settings","/"};
    for(int i=0;i<5;++i) {
        auto *route=new Route{shared,i};
        if(!server1_http_router_add_handler(router,0,0,i==3?"POST":"GET",paths[i],handle,route,[](void *p){delete static_cast<Route *>(p);})) {delete route;return false;}
    }
    return true;
}
}
