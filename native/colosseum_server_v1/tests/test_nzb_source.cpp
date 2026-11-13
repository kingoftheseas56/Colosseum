#include "server1/nzb/NzbSource.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <iostream>
using namespace server1::nzb;
void require(bool ok,const char*why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
QByteArray yenc(QByteArray bytes){QByteArray out="=ybegin line=128 size="+QByteArray::number(bytes.size())+" name=movie.mkv\r\n",line;for(unsigned char c:bytes){unsigned char e=c+42;if(e==0||e==10||e==13||e==61){line+='=';line+=char(e+64);}else line+=char(e);}if(line.startsWith('.'))line.prepend('.');return out+line+"\r\n=yend size="+QByteArray::number(bytes.size())+"\r\n";}
struct Fixture:QTcpServer{QHash<QByteArray,QByteArray> articles;QStringList commands;bool missing=false;int connections=0,activeConnections=0;Fixture(){connect(this,&QTcpServer::newConnection,this,[this]{while(hasPendingConnections()){auto s=nextPendingConnection();++connections;++activeConnections;connect(s,&QTcpSocket::disconnected,this,[this]{--activeConnections;});s->write("200 fixture\r\n");connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);connect(s,&QTcpSocket::readyRead,s,[this,s]{while(s->canReadLine()){auto l=s->readLine().trimmed();commands<<QString::fromUtf8(l);if(l.startsWith("AUTHINFO USER"))s->write("381 password\r\n");else if(l.startsWith("AUTHINFO PASS"))s->write("281 ok\r\n");else if(l.startsWith("GROUP"))s->write("211 group\r\n");else if(l.startsWith("ARTICLE")){auto id=l.mid(8);if(id=="<stall>")continue;if(missing||!articles.contains(id)){s->write("430 missing\r\n");continue;}auto response="220 article\r\nSubject: fixture\r\n\r\n"+articles[id]+".\r\n";s->write(response.left(9));QTimer::singleShot(1,s,[s,response]{s->write(response.mid(9,response.size()-11));});QTimer::singleShot(3,s,[s,response]{s->write(response.right(2));});}}});}});require(listen(QHostAddress::LocalHost,0),"fixture listen");}Server config(){Server s;s.host="127.0.0.1";s.port=serverPort();s.user="synthetic";s.password="fixture";return s;}};
void spin(std::function<void(std::function<void()>)> fn){QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&loop,[]{require(false,"test timeout");});timeout.start(3000);fn([&]{loop.quit();});loop.exec();}
int main(int argc,char**argv){QCoreApplication app(argc,argv);
 if(argc>1){
  QFile f(QString::fromLocal8Bit(argv[1]));require(f.open(QIODevice::ReadOnly),"oracle fixture");auto root=QJsonDocument::fromJson(f.readAll()).object();
  for(const auto value:root["rows"].toArray()){auto row=value.toObject();auto result=decodeYenc(QByteArray::fromBase64(row["input"].toString().toLatin1()));require(result.error.isEmpty()&&result.filename==row["filename"].toString()&&result.bytes.toHex()==row["hex"].toString().toLatin1(),"M1115 differential");}
  for(const auto value:root["xml"].toArray()){auto row=value.toObject();QString err;auto files=parse(row["input"].toString().toUtf8(),&err);auto expected=row["files"].toArray();require(err.isEmpty()&&files.size()==expected.size(),"M504 file count");for(int i=0;i<files.size();++i){auto segments=expected[i].toArray();require(files[i].size()==segments.size(),"M504 segment count");for(int j=0;j<segments.size();++j){if(segments[j].isNull()){require(files[i][j].article.isEmpty(),"M504 sparse segment");continue;}auto e=segments[j].toObject();auto s=files[i][j];require(s.article==e["article"].toString()&&s.group==e["group"].toString()&&s.subject==e["subject"].toString()&&s.bytes==e["bytes"].toInteger(),"M504 segment equality");}}}
 }

 auto encoded=yenc(QByteArray::fromHex("00010a0d133dffd6e0e304"));auto decoded=decodeYenc(encoded);require(decoded.error.isEmpty()&&decoded.bytes.toHex()=="00010a0d133dffd6e0e304","yEnc escaped bytes");require(decoded.filename=="movie.mkv","filename");require(!decodeYenc("=ybegin name=x\r\nabc").error.isEmpty(),"truncated yEnc");
 auto badCrc=encoded;badCrc.replace("=yend size=11","=yend size=11 pcrc32=deadbeef");require(decodeYenc(badCrc).crcMismatch,"diagnostic CRC");
 QString error;auto files=parse("<nzb><file subject='&quot;movie.mkv&quot;'><groups><group>test.group</group><group>ignored</group></groups><segments><segment bytes='12' number='2'>b</segment><segment bytes='12' number='1'>a</segment><segment bytes='3' number='3'>c</segment></segments></file></nzb>",&error);require(error.isEmpty()&&files.size()==1&&files[0][0].article=="a"&&files[0][2].group=="test.group","NZB ordering and first group");require(parse("<bad/>",&error).isEmpty()&&!error.isEmpty(),"invalid NZB");auto config=parseServer("nntp://a%40b:p%3Ass@localhost:119/3");require(config&&config->user=="a@b"&&config->password=="p:ss"&&config->connections==3,"NNTP URL");
 Fixture fixture,backbone;fixture.missing=true;backbone.articles={{"<a>",yenc("abcd")},{"<b>",yenc("efgh")},{"<c>",yenc("ij")}};
 NzbSource source;spin([&](auto done){source.open({fixture.config(),backbone.config()},files[0],[&,done](QString err){require(err.isEmpty(),"open with fallback");done();});});require(source.size()==10&&source.name()=="movie.mkv","first last geometry");
 spin([&](auto done){source.read(2,7,[done](auto r){require(r.error.isEmpty()&&r.bytes=="cdefghi","cross segment range");done();});});
 spin([&](auto done){source.read(10,0,[done](auto r){require(r.error.isEmpty()&&r.bytes.isEmpty(),"EOF zero read");done();});});
 spin([&](auto done){source.read(9,2,[done](auto r){require(r.status==416,"invalid range");done();});});
 const int commandsBefore=backbone.commands.size();spin([&](auto done){source.read(2,7,[done](auto r){require(r.bytes=="cdefghi","cached read bytes");done();});});require(backbone.commands.size()==commandsBefore,"decoded segment cache hit");
 File sparse;for(int i=0;i<50;++i)sparse.append({"test.group",i==1?QString():i==49?QString("c"):QString("a"),"movie.mkv",4});
 NzbSource concealed;spin([&](auto done){concealed.open({backbone.config()},sparse,[done](QString err){require(err.isEmpty(),"2 percent missing metadata allowed");done();});});
 spin([&](auto done){concealed.read(2,8,[done](auto r){require(r.error.isEmpty()&&r.bytes==QByteArray("cd\0\0\0\0ab",8),"missing middle piece zero fill");done();});});
 sparse[2].article.clear();NzbSource unavailable;spin([&](auto done){unavailable.open({backbone.config()},sparse,[done](QString err){require(err.contains("2%"),"over 2 percent metadata rejected");done();});});
 bool called=false;spin([&](auto done){auto token=source.read(0,4,[&](auto){called=true;});source.cancel(token);QTimer::singleShot(20,done);});require(!called,"cancel before request");
 ArticleClient client;client.setTimeout(30);spin([&](auto done){client.get(backbone.config(),{"test.group","stall","",0},[done](auto r){require(r.error.contains("timed out"),"timeout");done();});});
 spin([&](auto done){client.get(backbone.config(),{"test.group","gone","",0},[done](auto r){require(r.error.contains("430"),"missing article");done();});});
 spin([&](auto done){auto s=backbone.config();s.tls=true;client.get(s,files[0][0],[done](auto r){require(r.error.contains("TLS"),"TLS unsupported explicit");done();});});
 spin([&](auto done){auto token=client.get(backbone.config(),{"test.group","stall","",0},[&](auto){called=true;});QTimer::singleShot(5,[&,token]{client.cancel(token);});QTimer::singleShot(50,done);});require(!called,"cancel active article");
 spin([&](auto done){auto temporary=new NzbSource;temporary->open({backbone.config()},File{{"test.group","stall","",0}},[&](QString){called=true;});QTimer::singleShot(5,[temporary]{delete temporary;});QTimer::singleShot(50,done);});require(!called,"destroy source while opening");
 Fixture shared;shared.articles=backbone.articles;ArticleClient pooled;int delivered=0;bool canceled=false;auto firstCount=shared.commands.count("ARTICLE <b>");
 spin([&](auto done){auto token=pooled.get(shared.config(),{"test.group","b","",4},[&](auto){canceled=true;});pooled.get(shared.config(),{"test.group","b","",4},[&,done](auto r){require(r.bytes=="efgh","shared subscriber bytes");++delivered;done();});QTimer::singleShot(1,[&,token]{pooled.cancel(token);});});
 require(!canceled&&delivered==1&&shared.commands.count("ARTICLE <b>")==firstCount+1,"cancel subscriber preserves sibling and shared work");
 spin([&](auto done){pooled.get(shared.config(),{"test.group","a","",4},[done](auto r){require(r.bytes=="abcd","pooled second article");done();});});require(shared.connections==1&&shared.commands.count("AUTHINFO USER synthetic")==1,"authenticated connection reused");
 spin([&](auto done){auto token=pooled.get(shared.config(),{"test.group","stall","",0},[&](auto){canceled=true;});QTimer::singleShot(5,[&,token]{pooled.cancel(token);});QTimer::singleShot(30,done);});require(shared.activeConnections==0&&!canceled,"last subscriber aborts socket");
 spin([&](auto done){pooled.get(shared.config(),{"test.group","a","",4},[done](auto r){require(r.bytes=="abcd","new request after cancel");done();});});require(shared.connections==2,"pool reconnect after final cancellation");
 spin([&](auto done){pooled.get(shared.config(),{"test.group","retry","",4},[done](auto r){require(!r.error.isEmpty(),"shared error");done();});});shared.articles["<retry>"]=yenc("pass");spin([&](auto done){pooled.get(shared.config(),{"test.group","retry","",4},[done](auto r){require(r.error.isEmpty()&&r.bytes=="pass","retry after failure");done();});});
 Fixture queued;queued.articles=backbone.articles;ArticleClient limited;spin([&](auto done){auto count=std::make_shared<int>(0);for(const auto &article:{QString("a"),QString("b")})limited.get(queued.config(),{"test.group",article,"",4},[count,done](auto result){require(result.error.isEmpty(),"queued article");if(++*count==2)done();});});require(queued.connections==1,"configured one-connection pool bound");
 require(backbone.commands.contains("AUTHINFO USER synthetic")&&backbone.commands.contains("AUTHINFO PASS fixture")&&backbone.commands.contains("ARTICLE <a>"),"NNTP command transcript");std::cout<<"NZB parser/yEnc/NNTP/range/fallback/cancel/timeout PASS\n";
}
