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
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void check(bool value,const QString &message){++checks;if(!value)throw std::runtime_error(message.toStdString());}
void until(std::function<bool()> condition){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<10000)QCoreApplication::processEvents();check(condition(),"async timeout");}
struct Fixture {
 QTcpServer listener;QHash<QString,QByteArray> files;int requests=0,active=0,closed=0;bool stallNext=false;
 Fixture(){check(listener.listen(QHostAddress::LocalHost),"fixture listen");QObject::connect(&listener,&QTcpServer::newConnection,&listener,[this]{while(auto socket=listener.nextPendingConnection()){
  ++active;auto input=std::make_shared<QByteArray>();QObject::connect(socket,&QTcpSocket::disconnected,socket,[this,socket]{--active;++closed;socket->deleteLater();});
  QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,input]{*input+=socket->readAll();if(!input->contains("\r\n\r\n"))return;socket->disconnect(socket,&QTcpSocket::readyRead,nullptr,nullptr);++requests;QString path=QString::fromUtf8(input->split(' ').value(1));check(files.contains(path),"fixture path "+path);auto bytes=files[path];auto match=QRegularExpression("range: bytes=(\\d+)-(\\d*)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(*input));QByteArray headers="HTTP/1.1 200 OK\r\n";
   if(match.hasMatch()){auto start=match.captured(1).toLongLong(),end=match.captured(2).isEmpty()?bytes.size()-1:match.captured(2).toLongLong();check(start>=0&&end<bytes.size()&&end>=start,"fixture range");headers="HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(start)+"-"+QByteArray::number(end)+"/"+QByteArray::number(bytes.size())+"\r\n";bytes=bytes.mid(start,end-start+1);}
   auto wire=headers+"Content-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes;QPointer<QTcpSocket> safe=socket;auto send=[safe,wire]{if(safe&&safe->state()==QAbstractSocket::ConnectedState){safe->write(wire);safe->disconnectFromHost();}};if(stallNext){stallNext=false;QTimer::singleShot(200,&listener,send);}else send();
  });
 }});}
 QString url(QString path){return QString("http://127.0.0.1:%1%2").arg(listener.serverPort()).arg(path);}
};
struct Reply {int status=0;QByteArray body,length,ranges,contentRange;};
Reply fetch(QNetworkAccessManager &client,quint16 port,QByteArray method,QByteArray path,QByteArray body={},QByteArray range={}){
 QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1").arg(port)+QString::fromUtf8(path)));if(!range.isEmpty())req.setRawHeader("Range",range);if(method=="POST")req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");auto reply=method=="POST"?client.post(req,body):method=="HEAD"?client.head(req):client.get(req);until([&]{return reply->isFinished();});Reply result{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),reply->readAll(),reply->rawHeader("Content-Length"),reply->rawHeader("Accept-Ranges"),reply->rawHeader("Content-Range")};delete reply;return result;
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;try{
 QFile file(argv[1]);check(file.open(QIODevice::ReadOnly),"oracle observations open");auto rows=QJsonDocument::fromJson(file.readAll()).object()["rows"].toArray();Fixture fixture;auto router=server1_http_router_create();server1::http::mountArchiveRoutes(router);auto server=server1_http_server_create(router,128*1024);check(server1_http_server_listen(server,0),"route listen");auto port=server1_http_server_port(server);QNetworkAccessManager client;QByteArray cancelTarget,bodyCancelTarget;
 for(int rowIndex=0;rowIndex<rows.size();++rowIndex){auto row=rows[rowIndex].toObject(),expected=row["expected"].toObject();auto family=row["family"].toString();auto key=QString("fixture-%1").arg(rowIndex);QJsonArray urls;int partIndex=0;for(auto value:row["parts"].toArray()){auto bytes=QByteArray::fromBase64(value.toString().toLatin1());auto suffix=family=="rar"?(partIndex?QString("r%1").arg(partIndex-1,2,10,QChar('0')):QString("rar")):QString("7z.%1").arg(partIndex+1,3,10,QChar('0'));auto path="/"+key+"."+suffix;fixture.files[path]=bytes;urls.append(QJsonObject{{"url",fixture.url(path)},{"bytes",bytes.size()}});++partIndex;}auto created=fetch(client,port,"POST","/"+family.toUtf8()+"/create/"+key.toUtf8(),QJsonDocument(urls).toJson(QJsonDocument::Compact));check(created.status==200,row["name"].toString()+" create");auto target="/"+family.toUtf8()+"/stream?key="+key.toUtf8()+"&o="+QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(QJsonObject{{"fileIdx",row["index"]}}).toJson(QJsonDocument::Compact)));
  auto head=fetch(client,port,"HEAD",target);if(expected.contains("error")){check(head.status==500,"parser error HEAD");auto bad=fetch(client,port,"GET",target);check(bad.status==500,"parser error GET");check(bad.body=="There was an error with the "+family.toUtf8()+" parser.","parser error body");continue;}
  auto length=expected["length"].toInteger();check(head.status==204&&head.body.isEmpty(),"HEAD status/body");check(head.length==QByteArray::number(length)&&head.ranges=="bytes","HEAD headers");auto full=fetch(client,port,"GET",target);auto raw=QByteArray::fromBase64(expected["ranges"].toArray()[0].toObject()["body"].toString().toLatin1());check(full.status==200&&full.body==raw.left(length),row["name"].toString()+" full body");check(full.length==QByteArray::number(length),"full content length");
  auto suffix=fetch(client,port,"GET",target,{},"bytes=-3");check(suffix.status==206&&suffix.body==raw.left(4),"source suffix range means 0..3");
  auto openRange=fetch(client,port,"GET",target,{},"bytes=1-");check(openRange.status==206&&openRange.body==raw.mid(1,length-1),"open-ended range");
  if(row["name"].toString()=="rar4-large-payload")bodyCancelTarget=target;
  if(row["name"].toString()=="rar4-first"){
   auto other=target.left(target.indexOf("&o="))+"&o="+QUrl::toPercentEncoding("{\"fileIdx\":1}");auto second=fetch(client,port,"GET",other);check(second.status==200&&second.body=="0123456789abcde","selection cache keyed by URL not key");
  }
  for(auto rv:expected["ranges"].toArray()){auto r=rv.toObject();if(r.contains("error"))continue;auto start=r["start"].toInteger(),end=r["end"].toInteger();auto range="bytes="+QByteArray::number(start)+"-"+QByteArray::number(end);auto got=fetch(client,port,"GET",target,{},range);auto want=QByteArray::fromBase64(r["body"].toString().toLatin1()).left(end-start+1);check(got.status==206&&got.body==want,row["name"].toString()+" range body "+range);check(got.contentRange=="bytes "+QByteArray::number(start)+"-"+QByteArray::number(end)+"/"+QByteArray::number(length),"range header");}
  check(server1_http_server_max_queued_bytes(server)<=128*1024,"bounded HTTP queue");if(cancelTarget.isEmpty())cancelTarget=target;
 }
 // Distinct req.url prevents metadata cache reuse; abort while an upstream
 // metadata request is deliberately held, then verify it closes without reads.
 fixture.stallNext=true;auto before=fixture.requests;QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1").arg(port)+QString::fromUtf8(cancelTarget)+"&cancellation=1"));auto abandoned=client.get(req);until([&]{return fixture.requests>before;});abandoned->abort();delete abandoned;until([&]{return fixture.active==0;});auto requests=fixture.requests;QElapsedTimer timer;timer.start();while(timer.elapsed()<250)QCoreApplication::processEvents();check(fixture.requests==requests,"metadata cancellation prevents followup reads");
 // Cached metadata lets this cancellation land on a held payload read.
 fixture.stallNext=true;before=fixture.requests;QNetworkRequest bodyRequest(QUrl(QString("http://127.0.0.1:%1").arg(port)+QString::fromUtf8(bodyCancelTarget)));auto bodyAbandoned=client.get(bodyRequest);until([&]{return fixture.requests>before;});bodyAbandoned->abort();delete bodyAbandoned;until([&]{return fixture.active==0;});requests=fixture.requests;timer.restart();while(timer.elapsed()<250)QCoreApplication::processEvents();check(fixture.requests==requests,"payload cancellation prevents followup reads");
 server1_http_server_destroy(server);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);server1_http_router_destroy(router);std::cout<<"RAR/7zip real socket checks="<<checks<<" failures=0\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
