#include "server1/http/FtpRoutes.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <iostream>
#include <stdexcept>
#include <functional>
namespace {
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void until(std::function<bool()> condition){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<5000)QCoreApplication::processEvents();check(condition(),"async timeout");}
void* request(void*router,QByteArray method,QByteArray target,QByteArray bytes={},QByteArray range={}){
    auto parser=server1_http_parser_create(1024*1024);auto raw=method+" "+target+" HTTP/1.1\r\nHost: localhost\r\n";
    if(!range.isEmpty())raw+="Range: "+range+"\r\n";
    if(!bytes.isEmpty())raw+="Content-Type: application/json\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\n";
    raw+="\r\n"+bytes;check(server1_http_parser_feed(parser,raw.constData(),size_t(raw.size()))==1,"request parse");
    auto response=server1_http_router_dispatch_parsed(router,parser);server1_http_parser_destroy(parser);return response;
}
QByteArray drain(void*response){QByteArray bytes;char output[8192];QElapsedTimer timer;timer.start();while(timer.elapsed()<5000){auto count=server1_http_response_stream_read(response,output,sizeof output);if(count==SERVER1_HTTP_WOULD_BLOCK){QCoreApplication::processEvents();continue;}check(count>=0,"stream failed");if(!count)return bytes;bytes.append(output,count);}throw std::runtime_error("stream timeout");}
void wait(void*response){until([&](){return !server1_http_response_is_deferred(response);});}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;try{
 auto router=server1_http_router_create();server1::http::mountFtpRoutes(router);
 auto missing=request(router,"POST","/ftp/create","{}");check(server1_http_response_status(missing)==500,"invalid creation");server1_http_response_destroy(missing);
 auto waiting=request(router,"GET","/ftp/stream?key=movie");check(server1_http_response_is_deferred(waiting),"pending key");
 for(auto key:{QByteArray("movie"),QByteArray("noseek")}){
 auto payload=QJsonDocument(QJsonObject{{"ftpUrl",QString::fromUtf8(argv[1])+"/"+QString::fromUtf8(key)+".mp4"}}).toJson(QJsonDocument::Compact);
 auto create=request(router,"POST","/ftp/create/"+key,payload);wait(create);check(server1_http_response_status(create)==200,"create success");server1_http_response_destroy(create);
 auto head=request(router,"HEAD","/ftp/stream/"+key+"/movie.mp4");wait(head);check(server1_http_response_status(head)==200,"HEAD status");check(QByteArray(server1_http_response_header(head,"content-length"))=="200000","HEAD length");check(!server1_http_response_has_stream(head),"HEAD no producer");server1_http_response_destroy(head);
 for(auto range:{QByteArray(),QByteArray("bytes=1-9"),QByteArray("bytes=0-0")}){
 auto response=request(router,"GET","/ftp/stream/"+key+"/movie.mp4",{},range);wait(response);if(key=="noseek"&&!range.isEmpty()){check(server1_http_response_status(response)==405,"nonseek range");}else{check(server1_http_response_status(response)==(range.isEmpty()?200:206),"stream status");auto bytes=drain(response);int start=range=="bytes=1-9"?1:0;check(bytes.size()==(start?9:200000),"stream size");for(int i=0;i<bytes.size();++i)check(bytes[i]==char((i+start)%251),"stream bytes");}server1_http_response_destroy(response);
 }
 }
 wait(waiting);check(server1_http_response_status(waiting)==302,"late key redirect");check(QByteArray(server1_http_response_header(waiting,"location"))=="/ftp/stream/movie/movie.mp4","redirect target");server1_http_response_destroy(waiting);
 auto abandoned=request(router,"GET","/ftp/stream/movie/movie.mp4");wait(abandoned);char byte;check(server1_http_response_stream_read(abandoned,&byte,1)==SERVER1_HTTP_WOULD_BLOCK,"lazy read");server1_http_response_destroy(abandoned);QCoreApplication::processEvents();
 server1_http_router_destroy(router);std::cout<<"FTP routes passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
