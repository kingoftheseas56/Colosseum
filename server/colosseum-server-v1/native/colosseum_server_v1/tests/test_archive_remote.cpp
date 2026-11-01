#include "server1/archive/ArchiveSources.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <iostream>
using namespace server1::archive;
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTcpServer fixture;int requests=0;QList<QByteArray> wire;fixture.listen(QHostAddress::LocalHost);QObject::connect(&fixture,&QTcpServer::newConnection,[&](){while(auto *socket=fixture.nextPendingConnection()){auto bytes=std::make_shared<QByteArray>();QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);QObject::connect(socket,&QTcpSocket::readyRead,[&,socket,bytes](){*bytes+=socket->readAll();if(!bytes->contains("\r\n\r\n"))return;wire.append(*bytes);++requests;QByteArray response;if(bytes->startsWith("GET /redirect"))response="HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n";else if(bytes->startsWith("GET /unknown"))response="HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabc";else if(bytes->startsWith("GET /short"))response="HTTP/1.1 206 Partial Content\r\nContent-Length: 2\r\nContent-Range: bytes 2-5/10\r\n\r\ncd";else if(bytes->startsWith("GET /ignore"))response="HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcdefghij";else if(bytes->contains("Range: bytes=2-5"))response="HTTP/1.1 206 Partial Content\r\nContent-Length: 4\r\nContent-Range: bytes 2-5/10\r\n\r\ncdef";else response="HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcdefghij";QPointer<QTcpSocket> safe=socket;QTimer::singleShot(30,&app,[safe,response](){if(safe){safe->write(response);safe->disconnectFromHost();}});});}});
 auto url=[&](QString p){return QUrl("http://127.0.0.1:"+QString::number(fixture.serverPort())+p);};int fail=0;auto check=[&](bool b,const char*m){if(!b){++fail;std::cerr<<m<<'\n';}};auto until=[&](auto predicate){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<1000)app.processEvents(QEventLoop::AllEvents,10);check(predicate(),"timeout");};
 RemoteByteSource source(url("/ok"));check(requests==0,"construction must be lazy");int done=0;auto a=source.read(2,4,[&](ReadResult){done+=100;});source.read(2,4,[&](ReadResult r){check(r.bytes=="cdef"&&r.status==206&&r.error.isEmpty()&&r.totalLength==10,"range response");++done;});source.cancel(a);until([&]{return done>0;});check(done==1&&requests==1&&source.pendingRequests()==0,"sharing and cancellation");
 auto c=source.read(2,4,[&](ReadResult){done+=100;});auto d=source.read(2,4,[&](ReadResult){done+=100;});source.cancel(c);source.cancel(d);check(source.pendingRequests()==0,"all consumers cancelled");
 int meta=0;source.discoverLength([&](ReadResult r){check(r.totalLength==10&&r.bytes.isEmpty(),"metadata headers only");++meta;});until([&]{return meta==1;});
 for(auto p:{"/short","/ignore","/unknown"}){RemoteByteSource bad(url(p));bool got=false;bad.read(2,4,[&](ReadResult r){check(!r.error.isEmpty(),"bad upstream must not fake seek");got=true;});until([&]{return got;});}
 RemoteByteSource unknown(url("/unknown"));bool got=false;unknown.discoverLength([&](ReadResult r){check(!r.error.isEmpty()&&r.totalLength==-1,"unknown length");got=true;});until([&]{return got;});
 RemoteByteSource redirect(url("/redirect"));got=false;redirect.read(2,4,[&](ReadResult r){check(r.bytes=="cdef"&&r.error.isEmpty(),"redirect range");got=true;});until([&]{return got;});
 auto dying=std::make_unique<RemoteByteSource>(url("/ok"));got=false;dying->read(2,4,[&](ReadResult){dying.reset();got=true;});dying->read(2,4,[&](ReadResult){check(false,"destroyed sibling callback");});until([&]{return got;});
 std::cout<<"failures="<<fail<<" requests="<<requests<<'\n';return fail?1:0;}
