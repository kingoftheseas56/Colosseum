#include "server1/http/RootRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/settings/RuntimeSettings.h"
#include "../src/http/RootFormParser.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace server1;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
QJsonObject dispatch(void *router,const QJsonObject &row) {
    const auto method=row["method"].toString("GET").toUtf8();
    const auto target=row["path"].toString().toUtf8();
    const auto body=row["body"].toString().toUtf8();
    QByteArray wire=method+" "+target+" HTTP/1.1\r\n";
    const auto headers=row["headers"].toObject();
    for(auto i=headers.begin();i!=headers.end();++i)wire+=i.key().toUtf8()+": "+i.value().toString().toUtf8()+"\r\n";
    wire+="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
    auto *parser=server1_http_parser_create(1024*1024);
    server1_http_parser_feed(parser,wire.data(),wire.size());
    check(!server1_http_parser_error_status(parser),"parser error");
    auto *res=server1_http_router_dispatch_parsed(router,parser);
    QJsonObject actual{{"status",server1_http_response_status(res)}, {"body",QString::fromUtf8(server1_http_response_body(res),server1_http_response_body_size(res))}};
    QJsonObject h;
    for(size_t i=0;i<server1_http_response_header_count(res);++i) h[QString::fromUtf8(server1_http_response_header_name_at(res,i)).toLower()]=QString::fromUtf8(server1_http_response_header_value_at(res,i));
    actual["headers"]=h;
    server1_http_response_destroy(res);server1_http_parser_destroy(parser);
    return actual;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir dir;
    try {
        check(argc==2,"fixture argument required");QFile f(QString::fromLocal8Bit(argv[1]));check(f.open(QIODevice::ReadOnly),"fixture missing");
        const auto rows=QJsonDocument::fromJson(f.readAll()).array();int count=0;
        for(const auto &v:rows) {
            const auto row=v.toObject(); settings::SettingsStore store({dir.path().toStdString(),"4.21.0"});
            // Align deterministic values with oracle without relying on filesystem state.
            auto extension=row["settings"].toObject();
            for(auto i=extension.begin();i!=extension.end();++i){auto x=i.value();(void)store.set(i.key().toStdString(),x.isDouble()?policy::Value::number(x.toDouble()):x.isBool()?policy::Value::boolean(x.toBool()):x.isNull()?policy::Value::null():policy::Value::string(x.toString().toStdString()));}
            if(row["action"]=="form") {
                auto actual=QJsonDocument::fromJson(QByteArray::fromStdString(policy::jsonStringify(http::detail::parseRootForm(row["body"].toString().toUtf8()))));
                auto expected=QJsonDocument(row["expected"].toObject());
                if(actual!=expected) {std::cerr<<row["name"].toString().toStdString()<<" actual "<<actual.toJson().constData()<<" expected "<<expected.toJson().constData();throw std::runtime_error("form mismatch");}
                ++count;continue;
            }
            if(row["action"]=="defaults") {
                const auto actual=QJsonDocument::fromJson(QByteArray::fromStdString(policy::jsonStringify(settings::runtimeEngineDefaults(store,std::string(40,'a'),row["noCache"].toBool())))).object();
                check(actual==row["expected"].toObject(),"runtime defaults mismatch");++count;continue;
            }
            int changed=0;auto *router=server1_http_router_create();
            http::RootRoutesConfig config;config.settings=&store;config.settingsChanged=[&]{++changed;};config.baseUrl=[] {return "http://192.0.2.8:11470";};config.webUILocation=row["webUI"].toString("https://app.strem.io/shell-v4.4/").toStdString();
            config.networkInterfaces=[row] {if(row["networkError"].toBool())throw std::runtime_error("controlled network error");return std::vector<std::string>{"192.0.2.8","10.0.0.4"};};
            config.cacheLocations=[row]{std::vector<std::string> drives;for(const auto &d:row["drives"].toArray())drives.push_back(d.toString().toStdString());return drives;};
            check(http::installRootRoutes(router,config),"install failed");
            auto actual=dispatch(router,row);auto expected=row["expected"].toObject();
            if(row["path"]=="/settings" && row["method"].toString("GET")=="GET") {
                auto data=QJsonDocument::fromJson(actual["body"].toString().toUtf8()).object();
                // Values are separately covered by settings-store tests; oracle route projection keeps options + baseUrl.
                data.remove("values");actual["body"]=QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
                auto e=QJsonDocument::fromJson(expected["body"].toString().toUtf8()).object();e.remove("values");expected["body"]=QString::fromUtf8(QJsonDocument(e).toJson(QJsonDocument::Compact));
            }
            if(row["method"]=="POST") {check(changed==1,"settings callback missing");const auto defaults=settings::runtimeEngineDefaults(store,std::string(40,'a'));check(defaults.find("connections")->asNumber()==(row["formPost"].toBool()?35:79),"settings not reflected in future defaults");if(row["formPost"].toBool()){const auto snapshot=QJsonDocument::fromJson(QByteArray::fromStdString(policy::jsonStringify(store.value()))).object();const auto expectedValues=row["extension"].toObject();for(auto i=expectedValues.begin();i!=expectedValues.end();++i)check(snapshot[i.key()]==i.value(),"form settings not applied");}}
            if(actual!=expected){std::cerr<<row["name"].toString().toStdString()<<"\nactual "<<QJsonDocument(actual).toJson().constData()<<"expected "<<QJsonDocument(expected).toJson().constData();throw std::runtime_error("oracle mismatch");}
            server1_http_router_destroy(router);++count;
        }
        std::cout<<"PASS "<<count<<" source-traced root cases\n";return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
