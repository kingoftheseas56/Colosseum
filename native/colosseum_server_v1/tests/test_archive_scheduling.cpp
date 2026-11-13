#include "server1/http/ArchiveRoutes.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void check(bool value,const QString& message){++checks;if(!value)throw std::runtime_error(message.toStdString());}
void until(std::function<bool()> condition){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<10000)QCoreApplication::processEvents();check(condition(),"async timeout");}
void spin(int milliseconds=30){QElapsedTimer timer;timer.start();while(timer.elapsed()<milliseconds)QCoreApplication::processEvents();}
QByteArray read(const QString& filename){QFile file(filename);check(file.open(QIODevice::ReadOnly),"fixture open "+filename);return file.readAll();}
struct Fixture {
    struct Held {QString path;QPointer<QTcpSocket> socket;QByteArray wire;};
    QTcpServer listener;QHash<QString,QByteArray> files;QSet<QString> blocked,blockedLocal;
    QList<Held> held;QStringList requests;int connected=0;
    Fixture(){
        check(listener.listen(QHostAddress::LocalHost),"fixture listen");
        QObject::connect(&listener,&QTcpServer::newConnection,&listener,[this]{while(auto socket=listener.nextPendingConnection()){
            ++connected;auto input=std::make_shared<QByteArray>();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,[this,socket]{--connected;socket->deleteLater();});
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,input]{
                *input+=socket->readAll();if(!input->contains("\r\n\r\n"))return;
                socket->disconnect(socket,&QTcpSocket::readyRead,nullptr,nullptr);
                auto path=QString::fromUtf8(input->split(' ').value(1));requests.append(path);
                check(files.contains(path),"fixture path "+path);auto bytes=files[path];QByteArray headers="HTTP/1.1 200 OK\r\n";
                auto range=QRegularExpression("range: bytes=(\\d+)-(\\d*)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(*input));
                if(range.hasMatch()){
                    auto start=range.captured(1).toLongLong(),end=range.captured(2).isEmpty()?bytes.size()-1:range.captured(2).toLongLong();
                    check(start>=0&&end>=start&&end<bytes.size(),"fixture bounded range");
                    headers="HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(start)+"-"+QByteArray::number(end)+"/"+QByteArray::number(bytes.size())+"\r\n";
                    bytes=bytes.mid(start,end-start+1);
                }
                Held response{path,socket,headers+"Content-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes};
                if(blocked.contains(path)||(blockedLocal.contains(path)&&range.hasMatch()&&range.captured(1)=="0"&&range.captured(2)=="29"))held.append(response);else send(response);
            });
        }});
    }
    static void send(const Held& response){if(response.socket&&response.socket->state()==QAbstractSocket::ConnectedState){response.socket->write(response.wire);response.socket->disconnectFromHost();}}
    void release(const QString& path){blocked.remove(path);blockedLocal.remove(path);auto old=std::move(held);held.clear();for(const auto& response:old)if(response.path==path)send(response);else held.append(response);}
    int heldCount(const QString& path) const {int count=0;for(const auto& h:held)if(h.path==path&&h.socket&&h.socket->state()==QAbstractSocket::ConnectedState)++count;return count;}
    QString url(const QString& path) const {return QString("http://127.0.0.1:%1%2").arg(listener.serverPort()).arg(path);}
};
void* request(void* router,QByteArray method,QByteArray target,QByteArray bytes={},QByteArray range={}){
    auto parser=server1_http_parser_create(1024*1024);auto raw=method+" "+target+" HTTP/1.1\r\nHost: localhost\r\n";
    if(!range.isEmpty())raw+="Range: "+range+"\r\n";
    if(!bytes.isEmpty())raw+="Content-Type: application/json\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\n";
    raw+="\r\n"+bytes;check(server1_http_parser_feed(parser,raw.constData(),size_t(raw.size()))==1,"parse request");
    auto response=server1_http_router_dispatch_parsed(router,parser);server1_http_parser_destroy(parser);return response;
}
void* router(){auto value=server1_http_router_create();server1::http::mountArchiveRoutes(value);return value;}
QByteArray target(const QString& family,const QString& key,int index=0){return "/"+family.toUtf8()+"/stream?key="+key.toUtf8()+"&o="+QUrl::toPercentEncoding(QString("{\"fileIdx\":%1}").arg(index));}
void create(void* router,Fixture& fixture,const QString& family,const QString& key,const QString& path){
    QJsonArray urls{QJsonObject{{"url",fixture.url(path)},{"bytes",fixture.files[path].size()}}};
    auto response=request(router,"POST","/"+family.toUtf8()+"/create/"+key.toUtf8(),QJsonDocument(urls).toJson(QJsonDocument::Compact));
    check(server1_http_response_status(response)==200,"create archive key");server1_http_response_destroy(response);
}
void wait(void* response){until([&]{return !server1_http_response_is_deferred(response);});}
QByteArray drain(void* response){
    QByteArray bytes;char output[8192];QElapsedTimer timer;timer.start();
    while(timer.elapsed()<10000){auto n=server1_http_response_stream_read(response,output,sizeof output);if(n==SERVER1_HTTP_WOULD_BLOCK){QCoreApplication::processEvents();continue;}check(n>=0,"stream failure");if(!n)return bytes;bytes.append(output,n);}throw std::runtime_error("drain timeout");
}
QString extension(const QString& family){return family=="tgz"?"tgz":family=="7zip"?"7z":family;}
QByteArray first(const QString& family){return family=="rar"||family=="7zip"?QByteArray("ABCDEFGHIJ"):QByteArray(200000,'A');}
QByteArray second(const QString& family){return family=="rar"||family=="7zip"?QByteArray("0123456789abcde"):QByteArray(210000,'B');}
void fanoutAndCache(Fixture& fixture,const QString& root,const QString& family,bool compressed=false){
    auto r=router();auto path="/fanout-"+family+(compressed?"-compressed":"")+"."+extension(family);
    fixture.files[path]=read(root+"/multi"+(compressed?"-deflated":"")+"."+extension(family));
    fixture.blocked.insert(path);create(r,fixture,family,"shared",path);
    auto a=request(r,"HEAD",target(family,"shared",0));until([&]{return fixture.heldCount(path)==1;});
    auto b=request(r,"GET",target(family,"shared",1));auto c=request(r,"GET",target(family,"shared",1));auto ranged=request(r,"GET",target(family,"shared",1),{},"bytes=1-3");spin();
    check(fixture.heldCount(path)==1,"same key joins one preparation: "+family);
    fixture.release(path);wait(a);wait(b);wait(c);wait(ranged);
    check(QByteArray(server1_http_response_header(a,"content-length"))==QByteArray::number(first(family).size()),"HEAD inherited first selector: "+family);
    check(drain(b)==first(family)&&drain(c)==first(family),"concurrent different selectors inherit first result: "+family);
    if(family=="tgz"||compressed)check(server1_http_response_status(ranged)==405,"nonseekable subscriber independently rejects range");
    else check(server1_http_response_status(ranged)==206&&drain(ranged)==first(family).mid(1,3),"range headers remain per subscriber");
    server1_http_response_destroy(ranged);server1_http_response_destroy(a);server1_http_response_destroy(b);server1_http_response_destroy(c);
    auto before=fixture.requests.count(path);a=request(r,"HEAD",target(family,"shared",0));wait(a);
    if(family!="tgz"&&!compressed)check(fixture.requests.count(path)==before,"completed metadata avoids source work: "+family);
    server1_http_response_destroy(a);
    b=request(r,"GET",target(family,"shared",1));wait(b);
    check(drain(b)==((family=="zip"&&!compressed)||family=="tar"?first(family):second(family)),"completed cache identity: "+family);
    server1_http_response_destroy(b);
    if((family=="zip"&&!compressed)||family=="tar"){
        // Source keys are immutable: duplicate creation must neither replace the
        // source snapshot nor invalidate the completed key selection.
        auto rebound=path+"-rebound";fixture.files[rebound]=read(root+(family=="zip"?"/stored.zip":"/video.tar"));
        create(r,fixture,family,"shared",rebound);b=request(r,"GET",target(family,"shared",1));wait(b);
        check(drain(b)==first(family),"cached source survives duplicate key creation");check(!fixture.requests.contains(rebound),"duplicate key did not discover replacement source");server1_http_response_destroy(b);
    }
    server1_http_router_destroy(r);until([&]{return fixture.connected==0;});
}
void cancellation(Fixture& fixture,const QString& root,const QString& family,bool compressed=false){
    auto r=router();auto path="/cancel-"+family+(compressed?"-compressed":"")+"."+extension(family);fixture.files[path]=read(root+"/multi"+(compressed?"-deflated":"")+"."+extension(family));
    fixture.blocked.insert(path);create(r,fixture,family,"cancel",path);
    auto a=request(r,"GET",target(family,"cancel",0));until([&]{return fixture.heldCount(path)==1;});
    auto b=request(r,"GET",target(family,"cancel",1));server1_http_response_destroy(a);spin();
    check(fixture.heldCount(path)==1,"first subscriber cancellation preserves preparation");fixture.release(path);wait(b);check(drain(b)==first(family),"peer retains original selector after owner response cancellation");server1_http_response_destroy(b);
    // Independent per-client cursors, read tokens, gzip state, and backpressure.
    auto newKey="payload-"+family;a=request(r,"GET",target(family,newKey,0));b=request(r,"GET",target(family,newKey,0));
    create(r,fixture,family,newKey,path);wait(a);wait(b);fixture.blocked.insert(path);char out[16];
    check(server1_http_response_stream_read(a,out,sizeof out)==SERVER1_HTTP_WOULD_BLOCK,"first lazy read");
    check(server1_http_response_stream_read(b,out,sizeof out)==SERVER1_HTTP_WOULD_BLOCK,"second lazy read");
    until([&]{return fixture.heldCount(path)==1;});spin();check(fixture.heldCount(path)==1,"identical byte reads share transport but keep independent tokens");server1_http_response_destroy(a);spin();check(fixture.heldCount(path)==1,"peer read token remains live");
    fixture.release(path);check(drain(b)==first(family),"cancelled payload cannot cancel peer: "+family);server1_http_response_destroy(b);
    // A final subscriber must retire its task and cancel upstream work; a retry
    // uses its own selector and receives no stale completion from the old owner.
    fixture.blocked.insert(path);create(r,fixture,family,"retire",path);a=request(r,"GET",target(family,"retire",0));
    until([&]{return fixture.heldCount(path)==1;});auto before=fixture.requests.count(path);server1_http_response_destroy(a);until([&]{return fixture.heldCount(path)==0;});
    b=request(r,"GET",target(family,"retire",1));until([&]{return fixture.heldCount(path)==1;});check(fixture.requests.count(path)==before+1,"fresh owner after final cancellation");
    fixture.release(path);wait(b);check(drain(b)==second(family),"retry ignores retired selection");server1_http_response_destroy(b);
    server1_http_router_destroy(r);until([&]{return fixture.connected==0;});
}
void admission(Fixture& fixture,const QString& root,const QJsonObject& oracle,const QString& family){
    auto r=router();const auto ext=extension(family),prefix="/fifo-"+family+"-";
    auto bytes=read(root+"/multi"+(family=="zip"?"-deflated":"")+"."+ext);QList<void*> responses;QStringList starts;
    const auto cachedPath="/admission-cached-"+family+"."+ext;fixture.files[cachedPath]=read(root+"/multi."+ext);create(r,fixture,family,"cached",cachedPath);
    auto cached=request(r,"HEAD",target(family,"cached"));wait(cached);server1_http_response_destroy(cached);const auto begin=fixture.requests.size();
    for(int i=0;i<12;++i){auto key=QString::number(i),path=prefix+key+"."+ext;fixture.files[path]=bytes;fixture.blocked.insert(path);create(r,fixture,family,key,path);responses.append(request(r,"HEAD",target(family,key)));}
    until([&]{return fixture.requests.size()-begin==10;});spin();check(fixture.requests.size()-begin==10,"at most 10 preparations: "+family);
    if(family!="tgz"){
        cached=request(r,"HEAD",target(family,"cached"));spin();
        check(bool(server1_http_response_is_deferred(cached))==(family=="rar"||family=="7zip"),"ZIP/TAR cache bypasses queue; RAR/7zip cache is inside queue: "+family);server1_http_response_destroy(cached);
    }
    const auto otherFamily=family=="tar"?QString("zip"):QString("tar"),otherPath="/independent-"+family+"."+otherFamily;
    fixture.files[otherPath]=read(root+"/multi."+otherFamily);create(r,fixture,otherFamily,"independent",otherPath);auto other=request(r,"HEAD",target(otherFamily,"independent"));wait(other);check(server1_http_response_status(other)==200,"per-family admission independence");server1_http_response_destroy(other);
    fixture.release(prefix+"0."+ext);wait(responses[0]);until([&]{return fixture.heldCount(prefix+"10."+ext)==1;});check(!fixture.requests.contains(prefix+"11."+ext),"FIFO first new slot");
    fixture.release(prefix+"1."+ext);wait(responses[1]);until([&]{return fixture.heldCount(prefix+"11."+ext)==1;});
    for(const auto& path:fixture.requests.mid(begin))if(path.startsWith(prefix)&&!starts.contains(path))starts.append(path);
    // The initial ten starts are parallel: TCP arrival order is not task order.
    auto cohort=starts.mid(0,10);std::sort(cohort.begin(),cohort.end());starts=cohort+starts.mid(10);
    QJsonArray labels;for(const auto& path:starts)labels.append(path.mid(prefix.size()).section('.',0,0));check(labels==oracle["saturated"].toObject()["starts"].toArray(),"native FIFO admission matches pinned M72 trace: "+family);
    for(auto response:responses)server1_http_response_destroy(response);until([&]{return fixture.connected==0;});
    for(int i=0;i<12;++i)fixture.release(prefix+QString::number(i)+"."+ext);server1_http_router_destroy(r);
}
void saturatedDuplicate(Fixture& fixture,const QString& root){
    auto r=router();auto bytes=read(root+"/multi-deflated.zip");QList<void*> responses;
    for(int i=0;i<10;++i){auto key=QString::number(i),path="/boundary-"+key+".zip";fixture.files[path]=bytes;fixture.blocked.insert(path);create(r,fixture,"zip",key,path);responses.append(request(r,"HEAD",target("zip",key,0)));}
    // These are queued before the pump admits any task. With ten occupied slots,
    // M72 cannot inspect this duplicate until the earlier key-9 task completes.
    auto duplicate=request(r,"GET",target("zip","9",1));until([&]{return fixture.heldCount("/boundary-9.zip")==1;});
    fixture.release("/boundary-9.zip");wait(responses[9]);wait(duplicate);check(drain(duplicate)==second("zip"),"saturated unadmitted duplicate starts fresh selection");server1_http_response_destroy(duplicate);
    // Retire all ten slots plus an abandoned queued response before admission.
    for(auto response:responses)server1_http_response_destroy(response);until([&]{return fixture.connected==0;});
    for(int i=0;i<10;++i)fixture.release("/boundary-"+QString::number(i)+".zip");server1_http_router_destroy(r);
}
void queueCancellation(Fixture& fixture,const QString& root){
    auto r=router();auto bytes=read(root+"/multi-deflated.zip");QList<void*> responses;
    for(int i=0;i<12;++i){auto key=QString::number(i),path="/retire-"+key+".zip";fixture.files[path]=bytes;fixture.blocked.insert(path);create(r,fixture,"zip",key,path);responses.append(request(r,"HEAD",target("zip",key)));}
    until([&]{return fixture.heldCount("/retire-9.zip")==1;});server1_http_response_destroy(responses[10]);responses[10]=nullptr;
    server1_http_response_destroy(responses[0]);responses[0]=nullptr;until([&]{return fixture.heldCount("/retire-11.zip")==1;});check(!fixture.requests.contains("/retire-10.zip"),"abandoned queued work removed without opening source");
    for(auto response:responses)if(response)server1_http_response_destroy(response);until([&]{return fixture.connected==0;});
    for(int i=0;i<12;++i)fixture.release("/retire-"+QString::number(i)+".zip");
    auto before=fixture.requests.size();spin();check(fixture.requests.size()==before,"retired tasks produce no followup reads");server1_http_router_destroy(r);
}
void zipPreparationBoundary(Fixture& fixture,const QString& root){
    auto r=router();auto bytes=read(root+"/multi.zip");QList<void*> responses;
    for(int i=0;i<12;++i){auto key=QString::number(i),path="/local-header-"+key+".zip";fixture.files[path]=bytes;fixture.blockedLocal.insert(path);create(r,fixture,"zip",key,path);responses.append(request(r,"HEAD",target("zip",key)));}
    until([&]{for(int i=0;i<12;++i)if(fixture.heldCount("/local-header-"+QString::number(i)+".zip")!=1)return false;return true;});
    check(true,"ZIP local-entry resolution does not occupy preparation slots");
    // Directory selection has finished, but the first local header/cache has not.
    // A new same-key selector must therefore start a separate preparation.
    auto next=request(r,"GET",target("zip","0",1));wait(next);check(drain(next)==second("zip"),"ZIP shares only through directory selection, not local-entry resolution");server1_http_response_destroy(next);
    for(auto response:responses)server1_http_response_destroy(response);until([&]{return fixture.connected==0;});
    for(int i=0;i<12;++i)fixture.release("/local-header-"+QString::number(i)+".zip");
    fixture.files["/compressed-head.zip"]=read(root+"/multi-deflated.zip");fixture.blockedLocal.insert("/compressed-head.zip");create(r,fixture,"zip","compressed-head","/compressed-head.zip");
    auto head=request(r,"HEAD",target("zip","compressed-head"));wait(head);check(server1_http_response_status(head)==204,"compressed HEAD uses directory metadata");check(fixture.heldCount("/compressed-head.zip")==0,"compressed HEAD skips local header");server1_http_response_destroy(head);
    auto rejected=request(r,"GET",target("zip","compressed-head"),{},"bytes=1-");wait(rejected);check(server1_http_response_status(rejected)==405&&fixture.heldCount("/compressed-head.zip")==0,"rejected compressed range skips local header");server1_http_response_destroy(rejected);
    fixture.release("/compressed-head.zip");server1_http_router_destroy(r);
}
void errorFanoutAndCachedReentrancy(Fixture& fixture,const QString& root){
    for(bool synchronous:{false,true}){
        auto r=router();const auto suffix=synchronous?"sync":"async";auto path=QString("/error-%1.zip").arg(suffix),otherPath=QString("/error-other-%1.zip").arg(suffix);
        // An empty known source causes ZipSource::open to fail synchronously
        // inside ArchiveByteSource's open callback; corrupt nonempty bytes fail
        // later inside the network/parser callback stack.
        fixture.files[path]=synchronous?QByteArray():QByteArray(200,'b');fixture.files[otherPath]=read(root+"/multi.zip");fixture.blocked.insert(otherPath);
        create(r,fixture,"zip","error",path);create(r,fixture,"zip","other",otherPath);
        struct State {void* router;void* a;void* b;void* other;bool done=false;};
        State state{r,request(r,"HEAD",target("zip","error")),request(r,"GET",target("zip","error",1)),request(r,"GET",target("zip","other"))};
        check(server1_http_response_bind_deferred(state.a,[](void* opaque){auto& s=*static_cast<State*>(opaque);
            if(server1_http_response_status(s.a)!=500)std::terminate();
            server1_http_response_destroy(s.b);server1_http_response_destroy(s.other);server1_http_response_destroy(s.a);server1_http_router_destroy(s.router);s.done=true;
        },&state,nullptr),"bind destructive error callback");
        until([&]{return state.done;});until([&]{return fixture.connected==0;});auto before=fixture.requests.size();fixture.release(otherPath);spin();check(fixture.requests.size()==before,"error fanout teardown cancels all owners without late reads");
    }
    for(const auto& family:{QString("rar"),QString("7zip")}){
        auto r=router();auto path="/cached-reentrant."+extension(family);fixture.files[path]=read(root+"/multi."+extension(family));create(r,fixture,family,"cached",path);
        auto prime=request(r,"HEAD",target(family,"cached",0));wait(prime);server1_http_response_destroy(prime);auto before=fixture.requests.size();
        struct State {void* router;void* owner;void* sibling;void* next=nullptr;QString family;};
        State state{r,request(r,"HEAD",target(family,"cached",0)),request(r,"GET",target(family,"cached",1)),nullptr,family};
        check(server1_http_response_bind_deferred(state.owner,[](void* opaque){auto& s=*static_cast<State*>(opaque);
            s.next=request(s.router,"GET",target(s.family,"cached",1));server1_http_response_destroy(s.sibling);server1_http_response_destroy(s.owner);server1_http_router_destroy(s.router);
        },&state,nullptr),"bind cached reentrant callback");
        until([&]{return state.next!=nullptr;});wait(state.next);check(drain(state.next)==second(family),"synchronous cached owner can reenter and tear down router");server1_http_response_destroy(state.next);until([&]{return fixture.connected==0;});check(fixture.requests.size()>before,"reentrant distinct URL prepares a new stored selection");
    }
}
void reentrancyAndTeardown(Fixture& fixture,const QString& root){
    auto r=router();fixture.files["/reentrant.zip"]=read(root+"/multi-deflated.zip");create(r,fixture,"zip","reentrant","/reentrant.zip");
    struct State {void* router;void* owner;void* sibling;void* next=nullptr;};
    State state{r,request(r,"HEAD",target("zip","reentrant",0)),request(r,"HEAD",target("zip","reentrant",0))};
    check(server1_http_response_bind_deferred(state.owner,[](void* opaque){auto& s=*static_cast<State*>(opaque);
        s.next=request(s.router,"GET",target("zip","reentrant",1));server1_http_response_destroy(s.sibling);s.sibling=nullptr;server1_http_response_destroy(s.owner);s.owner=nullptr;
    },&state,nullptr),"bind reentrant callback");
    until([&]{return state.next!=nullptr;});wait(state.next);check(drain(state.next)==second("zip"),"reentrant new request is not lost; sibling destruction is safe");server1_http_response_destroy(state.next);
    fixture.files["/teardown.tgz"]=read(root+"/multi.tgz");fixture.blocked.insert("/teardown.tgz");create(r,fixture,"tgz","teardown","/teardown.tgz");
    auto response=request(r,"GET",target("tgz","teardown",1));until([&]{return fixture.heldCount("/teardown.tgz")==1;});server1_http_router_destroy(r);fixture.release("/teardown.tgz");wait(response);check(drain(response)==second("tgz"),"outstanding response owns registry after router teardown");server1_http_response_destroy(response);until([&]{return fixture.connected==0;});
    r=router();fixture.blocked.insert("/teardown.tgz");create(r,fixture,"tgz","teardown","/teardown.tgz");response=request(r,"GET",target("tgz","teardown"));until([&]{return fixture.heldCount("/teardown.tgz")==1;});server1_http_router_destroy(r);server1_http_response_destroy(response);until([&]{return fixture.connected==0;});fixture.release("/teardown.tgz");spin();
}
void failureAndMissingKey(Fixture& fixture,const QString& root){
    auto r=router();auto good=read(root+"/multi.zip");fixture.files["/retry.zip"]=QByteArray(good.size(),'b');fixture.blocked.insert("/retry.zip");create(r,fixture,"zip","retry","/retry.zip");
    auto a=request(r,"GET",target("zip","retry")),b=request(r,"GET",target("zip","retry",1));fixture.release("/retry.zip");wait(a);wait(b);
    check(server1_http_response_status(a)==500&&server1_http_response_status(b)==500,"shared failure reaches all subscribers");server1_http_response_destroy(a);server1_http_response_destroy(b);
    fixture.files["/retry.zip"]=good;fixture.files["/retry-good.zip"]=good;a=request(r,"GET",target("zip","retry",1));wait(a);check(drain(a)==second("zip"),"failure is not cached and retry succeeds");server1_http_response_destroy(a);
    fixture.blocked.insert("/retry-good.zip");a=request(r,"HEAD",target("zip","late",0));b=request(r,"GET",target("zip","late",1));auto retired=request(r,"GET",target("zip","late",1));server1_http_response_destroy(retired);create(r,fixture,"zip","late","/retry-good.zip");until([&]{return fixture.heldCount("/retry-good.zip")==1;});fixture.release("/retry-good.zip");wait(a);wait(b);check(drain(b)==first("zip"),"late-key waiters enter shared preparation in order");server1_http_response_destroy(a);server1_http_response_destroy(b);server1_http_router_destroy(r);
}
void plainGzipIndependence(Fixture& fixture,const QString& root){
    auto r=router();const QString path="/plain-concurrent.gz";fixture.files[path]=read(root+"/video.gz");create(r,fixture,"tgz","plain",path);
    auto a=request(r,"GET",target("tgz","plain",0)),b=request(r,"GET",target("tgz","plain",1));wait(a);wait(b);
    check(QByteArray(server1_http_response_header(a,"content-length")).isEmpty(),"plain gzip keeps unknown response length");fixture.blocked.insert(path);char bytes[8];
    check(server1_http_response_stream_read(a,bytes,sizeof bytes)==SERVER1_HTTP_WOULD_BLOCK,"first plain gzip pull");check(server1_http_response_stream_read(b,bytes,sizeof bytes)==SERVER1_HTTP_WOULD_BLOCK,"second plain gzip pull");
    until([&]{return fixture.heldCount(path)==1;});server1_http_response_destroy(a);spin();check(fixture.heldCount(path)==1,"plain gzip cancellation leaves peer token");server1_http_router_destroy(r);fixture.release(path);
    check(drain(b)==QByteArray(200000,'x'),"plain gzip retains independent inflater after owner/router teardown");server1_http_response_destroy(b);until([&]{return fixture.connected==0;});
}
void wireClients(Fixture& fixture,const QString& root){
    auto r=router();fixture.files["/wire.tgz"]=read(root+"/multi.tgz");fixture.blocked.insert("/wire.tgz");create(r,fixture,"tgz","wire","/wire.tgz");
    auto server=server1_http_server_create(r,128*1024);check(server1_http_server_listen(server,0),"wire listen");QNetworkAccessManager client;
    auto url=[&](int index){return QUrl(QString("http://127.0.0.1:%1").arg(server1_http_server_port(server))+QString::fromLatin1(target("tgz","wire",index)));};
    auto a=client.get(QNetworkRequest(url(0)));until([&]{return fixture.heldCount("/wire.tgz")==1;});auto b=client.get(QNetworkRequest(url(1)));spin();a->abort();delete a;spin();check(fixture.heldCount("/wire.tgz")==1,"wire disconnect leaves peer preparation live");fixture.release("/wire.tgz");until([&]{return b->isFinished();});check(b->error()==QNetworkReply::NoError&&b->readAll()==first("tgz"),"wire peer receives first selection after disconnect");delete b;check(server1_http_server_max_queued_bytes(server)<=128*1024,"wire stream stays bounded");
    server1_http_server_destroy(server);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);server1_http_router_destroy(r);until([&]{return fixture.connected==0;});
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;try{
    const auto root=QString::fromLocal8Bit(argv[1]);auto oracle=QJsonDocument::fromJson(read(root+"/queue.json")).object();check(oracle["families"].toArray().size()==5,"five source families observed");Fixture fixture;
    for(const auto& family:{QString("zip"),QString("tar"),QString("tgz"),QString("rar"),QString("7zip")}){fanoutAndCache(fixture,root,family);cancellation(fixture,root,family);admission(fixture,root,oracle,family);}
    fanoutAndCache(fixture,root,"zip",true);cancellation(fixture,root,"zip",true);saturatedDuplicate(fixture,root);queueCancellation(fixture,root);zipPreparationBoundary(fixture,root);errorFanoutAndCachedReentrancy(fixture,root);reentrancyAndTeardown(fixture,root);failureAndMissingKey(fixture,root);plainGzipIndependence(fixture,root);wireClients(fixture,root);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);std::cout<<"PASS archive preparation scheduling/lifecycle checks="<<checks<<" failures=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
