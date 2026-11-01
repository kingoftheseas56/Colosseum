#include "server1/http/FileReaderStream.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTcpSocket>
#include <QHostAddress>
#include <QEvent>
#include <functional>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace server1::policy;
void expect(bool b,const char *s){if(!b)throw std::runtime_error(s);}
struct Source : FileReaderSource {
    bool available=false;
    std::map<std::uint64_t,std::pair<std::size_t,Completion>> pending;
    int requests=0,cancels=0;
    bool hasPiece(std::size_t)const override{return available;}
    void readPiece(std::uint64_t t,std::size_t p,Completion c)override{++requests;pending.emplace(t,std::make_pair(p,std::move(c)));}
    bool cancelRead(std::uint64_t t)override{++cancels;return pending.erase(t)!=0;}
    void complete(){auto work=std::move(pending);pending.clear();for(auto &[t,p]:work){ByteBuffer b(4);for(int i=0;i<4;++i)b[i]=static_cast<unsigned char>('a'+p.first*4+i);p.second(t,p.first,std::move(b),{});}}
};
struct Dependencies {Scheduler scheduler{4};Source source;};
struct Response {
    void *router=server1_http_router_create();
    void *value=server1_http_router_dispatch(router,"GET","/","");
    ~Response(){server1_http_response_destroy(value);server1_http_router_destroy(router);}
};
void cases(){
    auto owner=std::make_shared<Dependencies>();
    auto reader=std::make_shared<FileReader>(owner->scheduler,owner->source,TorrentFile{"x","x",16,0},4,FileReadOptions{1,10});
    Response response;
    expect(server1::http::attachFileReader(response.value,reader,owner),"missing FileReader HTTP adapter");
    char out[16]{};
    expect(owner->source.requests==0,"attachment must not prefetch");
    expect(server1_http_response_stream_read(response.value,out,3)==SERVER1_HTTP_WOULD_BLOCK,"missing piece must wait");
    owner->source.available=true;reader->notifyPiece(0);
    expect(owner->source.requests==1,"one demand must schedule one read");
    for(int n=0;n<8;++n)expect(server1_http_response_stream_read(response.value,out,3)==SERVER1_HTTP_WOULD_BLOCK,"repeated blocked reads wait");
    expect(owner->source.requests==1,"blocked polling must not accumulate demand");
    owner->source.complete();
    expect(server1_http_response_stream_read(response.value,out,2)==2 && std::string(out,2)=="bc","range prefix copied exactly");
    expect(server1_http_response_stream_read(response.value,out,2)==1 && out[0]=='d',"partial chunk retained");
    std::string result="bcd";
    for(int attempts=0;result.size()<10 && attempts<20;++attempts){
        auto n=server1_http_response_stream_read(response.value,out,3);
        if(n==SERVER1_HTTP_WOULD_BLOCK)owner->source.complete();
        else {expect(n>0,"unexpected EOF/error");result.append(out,static_cast<std::size_t>(n));}
    }
    expect(result=="bcdefghijk","inclusive range byte parity");
    expect(server1_http_response_stream_read(response.value,out,3)==0,"EOF after exact bytes");
    std::weak_ptr<Dependencies> retained=owner;owner.reset();
    expect(!retained.expired(),"dependencies held until response retirement");
    server1_http_response_destroy(response.value);response.value=nullptr;
    expect(reader->closed(),"retirement closes reader");
    expect(retained.expired(),"retirement releases dependencies");
}
void cancellation(){
    auto owner=std::make_shared<Dependencies>();owner->source.available=true;
    auto reader=std::make_shared<FileReader>(owner->scheduler,owner->source,TorrentFile{"x","x",16,0},4);
    Response response;expect(server1::http::attachFileReader(response.value,reader,owner),"attach cancel");
    char out[4];expect(server1_http_response_stream_read(response.value,out,4)==SERVER1_HTTP_WOULD_BLOCK,"async pending");
    server1_http_response_destroy(response.value);response.value=nullptr;
    expect(reader->closed() && owner->source.cancels==1 && owner->scheduler.selections().empty(),"cancel drops owned selection and pending read");
}
void error(){
    auto owner=std::make_shared<Dependencies>();
    auto reader=std::make_shared<FileReader>(owner->scheduler,owner->source,TorrentFile{"x","x",16,0},4);
    Response response;expect(server1::http::attachFileReader(response.value,reader,owner),"attach error");
    reader->fail("disk failure");char out[4];expect(server1_http_response_stream_read(response.value,out,4)==-1,"reader failure closes HTTP stream");
}

void until(const std::function<bool()> &done){QElapsedTimer timer;timer.start();while(!done() && timer.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}expect(done(),"socket test timed out");}
void socketCases(){
    struct Context {std::shared_ptr<Dependencies> owner=std::make_shared<Dependencies>();std::shared_ptr<FileReader> reader;};
    Context context;context.owner->source.available=true;
    auto *router=server1_http_router_create();
    expect(server1_http_router_add_handler(router,0,0,"GET","/file",+[](const void *,void *response,server1_http_next_fn,void *,void *opaque){
        auto &c=*static_cast<Context *>(opaque);
        c.reader=std::make_shared<FileReader>(c.owner->scheduler,c.owner->source,TorrentFile{"x","x",16,0},4,FileReadOptions{1,10});
        server1_http_response_set_status(response,206);
        server1_http_response_set_header(response,"Content-Range","bytes 1-10/16");
        return server1::http::attachFileReader(response,c.reader,c.owner)?1:0;
    },&context,nullptr),"register socket reader");
    auto *server=server1_http_server_create(router,4096);
    expect(server1_http_server_listen(server,0),"listen");
    const auto port=server1_http_server_port(server);
    {
        QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,port);
        until([&]{return socket.state()==QAbstractSocket::ConnectedState;});
        socket.write("GET /file HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        until([&]{return context.owner->source.requests>0;});
        QByteArray wire;
        until([&]{wire+=socket.readAll();return wire.contains("\r\n\r\n");});
        expect(wire.endsWith("\r\n\r\n"),"missing disk data must not fabricate body");
        until([&]{context.owner->source.complete();wire+=socket.readAll();return socket.state()==QAbstractSocket::UnconnectedState;});
        wire+=socket.readAll();
        expect(wire.endsWith("bcdefghijk"),"real socket must wake and deliver exact range");
        expect(context.reader->closed(),"socket completion closes reader");
    }
    for(bool shutdown:{false,true}){
        QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,port);
        until([&]{return socket.state()==QAbstractSocket::ConnectedState;});
        socket.write("GET /file HTTP/1.1\r\nHost: localhost\r\n\r\n");
        until([&]{return !context.owner->source.pending.empty();});
        if(shutdown)server1_http_server_stop(server);else socket.abort();
        until([&]{return context.reader->closed();});
        expect(context.owner->source.pending.empty() && context.owner->scheduler.selections().empty(),"abort/shutdown cancels pending reader work");
    }
    server1_http_server_destroy(server);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    server1_http_router_destroy(router);
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{cases();cancellation();error();socketCases();std::cout<<"HTTP reader bridge passed\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
