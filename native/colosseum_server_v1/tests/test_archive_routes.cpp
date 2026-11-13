#include "server1/http/ArchiveRoutes.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void until(std::function<bool()> condition){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<5000)QCoreApplication::processEvents();check(condition(),"async timeout");}
struct Fixture {
    QTcpServer listener;QString directory;int requests=0;qint64 rangedBytes=0;
    explicit Fixture(QString root):directory(root){
        check(listener.listen(QHostAddress::LocalHost),"listen");
        QObject::connect(&listener,&QTcpServer::newConnection,&listener,[this](){while(auto socket=listener.nextPendingConnection()){
            auto input=std::make_shared<QByteArray>();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,input](){
                *input+=socket->readAll();if(!input->contains("\r\n\r\n"))return;++requests;
                auto path=QString::fromUtf8(input->split(' ').value(1));QFile file(directory+path);check(file.open(QIODevice::ReadOnly),"fixture open");auto bytes=file.readAll();
                auto range=QRegularExpression("range: bytes=(\\d+)-(\\d+)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(*input));
                QByteArray headers="HTTP/1.1 200 OK\r\n";
                if(range.hasMatch()){
                    auto start=range.captured(1).toLongLong(),end=range.captured(2).toLongLong();check(start>=0&&end<bytes.size(),"upstream interval");
                    headers="HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(start)+"-"+QByteArray::number(end)+"/"+QByteArray::number(bytes.size())+"\r\n";
                    bytes=bytes.mid(start,end-start+1);rangedBytes+=bytes.size();
                }
                socket->write(headers+"Content-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes);socket->disconnectFromHost();
            });
        }});
    }
    QString url(const QString&file){return QString("http://127.0.0.1:%1/%2").arg(listener.serverPort()).arg(file);}
};
void* request(void*router,QByteArray method,QByteArray target,QByteArray bytes={},QByteArray range={}){
    auto parser=server1_http_parser_create(1024*1024);auto raw=method+" "+target+" HTTP/1.1\r\nHost: localhost\r\n";
    if(!range.isEmpty())raw+="Range: "+range+"\r\n";
    if(!bytes.isEmpty())raw+="Content-Type: application/json\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\n";
    raw+="\r\n"+bytes;check(server1_http_parser_feed(parser,raw.constData(),size_t(raw.size()))==1,"request parse");
    auto response=server1_http_router_dispatch_parsed(router,parser);server1_http_parser_destroy(parser);return response;
}
QByteArray drain(void*response){QByteArray bytes;char output[8192];QElapsedTimer timer;timer.start();while(timer.elapsed()<5000){auto count=server1_http_response_stream_read(response,output,sizeof output);if(count==SERVER1_HTTP_WOULD_BLOCK){QCoreApplication::processEvents();continue;}check(count>=0,"stream failed");if(!count)return bytes;bytes.append(output,count);}throw std::runtime_error("stream timeout");}
void wait(void*response){until([&](){return !server1_http_response_is_deferred(response);});}
void create(void*router,const QString&key,const QJsonArray&urls,QString family="zip"){auto response=request(router,"POST","/"+family.toUtf8()+"/create/"+key.toUtf8(),QJsonDocument(urls).toJson(QJsonDocument::Compact));check(server1_http_response_status(response)==200,"create status");auto parsed=QJsonDocument::fromJson(QByteArray(server1_http_response_body(response),server1_http_response_body_size(response))).object();check(parsed["key"]==key,"created key");server1_http_response_destroy(response);}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc<2)return 2;
    try {
        Fixture fixture(QString::fromLocal8Bit(argv[1]));auto router=server1_http_router_create();server1::http::mountArchiveRoutes(router);
        for(auto target:{"/zip/stream","/zip/create"}){auto r=request(router,"GET",target);check(server1_http_response_status(r)==500,"missing inputs");server1_http_response_destroy(r);}
        auto invalid=request(router,"POST","/zip/create","{}");check(server1_http_response_status(invalid)==500,"nonarray key payload");server1_http_response_destroy(invalid);
        auto pending=request(router,"GET","/zip/stream?key=later");check(server1_http_response_is_deferred(pending),"wait for key");
        create(router,"later",{fixture.url("stored.zip")});wait(pending);check(server1_http_response_status(pending)==200,"late key resolved");check(drain(pending)==QByteArray(200000,'x'),"late key payload");server1_http_response_destroy(pending);
        create(router,"stored",{fixture.url("stored.zip")});auto before=fixture.rangedBytes;
        auto head=request(router,"HEAD","/zip/stream?key=stored");wait(head);check(server1_http_response_status(head)==200&&!server1_http_response_has_stream(head),"stored HEAD");check(QByteArray(server1_http_response_header(head,"content-length"))=="200000","HEAD length");check(fixture.rangedBytes-before<1024,"HEAD downloaded archive body");server1_http_response_destroy(head);
        for(auto range:{QByteArray(),QByteArray("bytes=1-9"),QByteArray("bytes=-3"),QByteArray("bytes=0-0")}){
            auto r=request(router,"GET","/zip/stream?key=stored",{},range);wait(r);check(server1_http_response_status(r)==(range.isEmpty()?200:206),"stored range status");auto expected=range.isEmpty()?200000:range=="bytes=1-9"?9:range=="bytes=-3"?3:1;check(drain(r)==QByteArray(expected,'x'),"stored range exact payload");server1_http_response_destroy(r);
        }
        auto r=request(router,"GET","/zip/stream?key=stored",{},"bytes=200000-");wait(r);check(server1_http_response_status(r)==416,"unsatisfiable range");server1_http_response_destroy(r);
        create(router,"compressed",{fixture.url("deflated.zip")});r=request(router,"HEAD","/zip/stream?key=compressed");wait(r);check(server1_http_response_status(r)==204&&!server1_http_response_has_stream(r),"compressed HEAD");server1_http_response_destroy(r);
        r=request(router,"GET","/zip/stream?key=compressed",{},"bytes=1-");wait(r);check(server1_http_response_status(r)==405,"compressed seek forbidden");server1_http_response_destroy(r);
        r=request(router,"GET","/zip/stream?key=compressed",{},"bytes=0-");wait(r);check(server1_http_response_status(r)==200,"compressed full range");check(drain(r)==QByteArray(200000,'x'),"native HTTP inflate");server1_http_response_destroy(r);
        create(router,"split",{fixture.url("split.zip"),fixture.url("split.z01")});r=request(router,"GET","/zip/stream?key=split",{},"bytes=199995-");wait(r);check(server1_http_response_status(r)==206,"split disk status");check(drain(r)==QByteArray(5,'x'),"split disk offsets");server1_http_response_destroy(r);
        create(router,"cross",{fixture.url("cross.zip"),fixture.url("cross.z01")});r=request(router,"GET","/zip/stream?key=cross");wait(r);check(server1_http_response_status(r)==200,"cross-volume status");check(drain(r)==QByteArray(200000,'x'),"cross-volume bounded reads");server1_http_response_destroy(r);
        create(router,"selected",{fixture.url("multi.zip")});auto selectTarget=QByteArray("/zip/stream?key=selected&o=")+QUrl::toPercentEncoding("{\"fileIdx\":0}");r=request(router,"GET",selectTarget);wait(r);check(drain(r)=="note","explicit ZIP index");server1_http_response_destroy(r);
        r=request(router,"GET","/zip/stream?key=selected");wait(r);check(drain(r)=="note","source stored selection cache");server1_http_response_destroy(r);
        create(router,"pattern",{fixture.url("multi.zip")});selectTarget=QByteArray("/zip/stream?key=pattern&o=")+QUrl::toPercentEncoding("{\"fileMustInclude\":[\"/video[.]mkv$/i\"]}");r=request(router,"GET",selectTarget);wait(r);check(drain(r)==QByteArray(200000,'x'),"ECMAScript file selection");server1_http_response_destroy(r);
        r=request(router,"GET","/zip/stream?key=stored");wait(r);char abandoned[4];check(server1_http_response_stream_read(r,abandoned,sizeof abandoned)==SERVER1_HTTP_WOULD_BLOCK,"producer starts lazily");server1_http_response_destroy(r);QCoreApplication::processEvents();
        create(router,"bad",{fixture.url("bad.zip")});r=request(router,"GET","/zip/stream?key=bad");wait(r);check(server1_http_response_status(r)==500,"bad archive response");server1_http_response_destroy(r);
        r=request(router,"GET","/zip/stream?key=never");server1_http_response_destroy(r);create(router,"never",{fixture.url("stored.zip")});
        r=request(router,"GET","/zip/stream?key=stored");server1_http_response_destroy(r);QCoreApplication::processEvents();
        for(const auto family:{QString("tar"),QString("tgz")}) {
            create(router,"video",{fixture.url(family=="tar"?"video.tar":"video.tgz")},family);
            auto target="/"+family.toUtf8()+"/stream?key=video";
            auto h=request(router,"HEAD",target);wait(h);check(server1_http_response_status(h)==(family=="tar"?200:204),"TAR/GZIP HEAD");check(QByteArray(server1_http_response_header(h,"content-length"))=="200000","TAR/GZIP length");server1_http_response_destroy(h);
            auto full=request(router,"GET",target);wait(full);check(server1_http_response_status(full)==200,"TAR/GZIP GET");check(drain(full)==QByteArray(200000,'x'),"TAR/GZIP native HTTP payload");server1_http_response_destroy(full);
            auto partial=request(router,"GET",target,{},"bytes=1-9");wait(partial);check(server1_http_response_status(partial)==(family=="tar"?206:405),"TAR/GZIP seek status");if(family=="tar")check(drain(partial)==QByteArray(9,'x'),"TAR partial payload");server1_http_response_destroy(partial);
        }
        create(router,"plain",{fixture.url("video.gz")},"tgz");r=request(router,"GET","/tgz/stream?key=plain",{},"bytes=0-");wait(r);check(server1_http_response_status(r)==200,"plain gzip status");check(QByteArray(server1_http_response_header(r,"accept-ranges"))=="none","plain gzip nonseekable");check(drain(r)==QByteArray(200000,'x'),"plain gzip native HTTP bytes");server1_http_response_destroy(r);
        auto server=server1_http_server_create(router,128*1024);check(server1_http_server_listen(server,0),"archive HTTP listener");
        QNetworkAccessManager client;
        for(const auto target:{"/zip/stream?key=stored","/zip/stream?key=stored","/tar/stream?key=video","/tgz/stream?key=video","/tgz/stream?key=plain"}){
            auto reply=client.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1%2").arg(server1_http_server_port(server)).arg(target))));
            until([&](){return reply->isFinished();});check(reply->error()==QNetworkReply::NoError,"wire archive GET");check(reply->readAll()==QByteArray(200000,'x'),"wire exact archive bytes");delete reply;
            check(server1_http_server_max_queued_bytes(server)<=128*1024,"bounded archive HTTP queue");
        }
        server1_http_server_destroy(server);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        server1_http_router_destroy(router);std::cout<<"PASS ZIP/TAR/GZIP HTTP create, deferred keys, HEAD, range, native inflate, cancellation\n";return 0;
    }catch(const std::exception&error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
