#include "server1/archive/FtpSource.h"
#include <QTcpSocket>
#include <QTimer>
#include <QRegularExpression>
#include <QFileInfo>
#include <QPointer>
#include <limits>
namespace server1::archive {
namespace {
class Operation final : public QObject {
public:
 QTcpSocket control,data;QTimer timer;QUrl url;QByteArray lines,multiline,bytes;int multiCode=0;
 qint64 offset=0,count=0,skip=0;bool metadata=false,done=false,useRest=true,dataEnded=false,transferComplete=false,listing=false,listEnded=false,listComplete=false;
 QString stage;FtpMetadata info;QHash<QString,QString> features;QStringList listCommands;int listIndex=0;QByteArray listingBytes;
 std::function<void(FtpMetadata,ReadResult)> callback;
 Operation(QUrl u):url(std::move(u)) {
  timer.setSingleShot(true);timer.setInterval(30000);
  connect(&timer,&QTimer::timeout,this,[this]{finish("FTP operation timed out");});
  connect(&control,&QTcpSocket::readyRead,this,[this]{receive();});
  connect(&control,&QTcpSocket::errorOccurred,this,[this](auto error){if(!done&&!(error==QAbstractSocket::RemoteHostClosedError&&transferComplete))finish(control.errorString());});
  connect(&data,&QTcpSocket::errorOccurred,this,[this](auto error){if(!done&&error!=QAbstractSocket::RemoteHostClosedError)finish(data.errorString());});
  connect(&data,&QTcpSocket::readyRead,this,[this]{consume();});
  connect(&data,&QTcpSocket::disconnected,this,[this]{dataEnded=true;if(listing){listEnded=true;consume();finishList();}else {consume();if(!done&&callback&&bytes.size()<count)finish("Short FTP body");}});
  connect(&data,&QTcpSocket::connected,this,[this]{
   if(listing){auto directory=QFileInfo(info.path).path();command("list",listCommands.value(listIndex).toUtf8()+" "+directory.toUtf8());}
   else if(offset&&useRest)command("rest","REST "+QByteArray::number(offset));else command("retr","RETR "+info.path.toUtf8());
  });
  data.setReadBufferSize(65536);
 }
 ~Operation() override {control.abort();data.abort();}
 void finish(QString error={}) {
  if(done)return;done=true;timer.stop();control.disconnect(this);data.disconnect(this);control.abort();data.abort();
  info.error=error;auto cb=std::move(callback);if(cb)cb(info,{bytes,error,error.isEmpty()?200:500,info.size});deleteLater();
 }
 void cancel(){callback={};finish("Cancelled");}
 void command(const QString&s,const QByteArray&line){stage=s;control.write(line+"\r\n");}
 void start(){
  if(done)return;
  info.path=url.path(QUrl::FullyDecoded);
  if(url.scheme()!="ftp"){finish("Only plain FTP is supported; FTPS certificate verification is not bypassed");return;}
  if(!url.isValid()||url.host().isEmpty()||info.path.isEmpty()||info.path.contains('\r')||info.path.contains('\n')||url.userName(QUrl::FullyDecoded).contains('\r')||url.userName(QUrl::FullyDecoded).contains('\n')||url.password(QUrl::FullyDecoded).contains('\r')||url.password(QUrl::FullyDecoded).contains('\n')){finish("Invalid FTP URL");return;}
  if(!metadata&&(offset<0||count<=0||count>1024*1024||offset>std::numeric_limits<qint64>::max()-count)){finish("Invalid bounded read interval");return;}
  stage="greeting";timer.start();control.connectToHost(url.host(),quint16(url.port(21)));
 }
 void consume(){
  if(done)return;
  if(listing){listingBytes+=data.readAll();if(listingBytes.size()>1024*1024)finish("FTP listing exceeds 1 MiB metadata limit");return;}
  if(!callback||!count)return;
  while(skip&&data.bytesAvailable()){auto n=data.read(qMin<qint64>(skip,65536)).size();skip-=n;}if(skip)return;
  bytes+=data.read(count-bytes.size());
  if(bytes.size()==count){auto cb=std::move(callback);auto result=std::move(bytes);bytes.clear();offset+=count;count=0;timer.stop();cb(info,{result,{},200,info.size});}
 }
 void resume(qint64 n,std::function<void(FtpMetadata,ReadResult)> cb){count=n;callback=std::move(cb);timer.start();QTimer::singleShot(0,this,[this]{if(done)return;consume();if(!done&&callback&&dataEnded)finish("Short FTP body");});}
 void parseFeatures(const QByteArray&line,int code){features.clear();if(code>=400||line.size()<4||line[3]!='-')return;auto rows=line.split('\n');for(int i=1;i<rows.size()-1;++i){auto parts=rows[i].trimmed().split(' ');if(!parts.isEmpty())features[QString::fromUtf8(parts[0])]=QString::fromUtf8(parts.value(1));}}
 void startList(){listing=true;listEnded=false;listComplete=false;listingBytes.clear();dataEnded=false;passive(true);}
 void finishList(){
  if(done||!listing||!listEnded||!listComplete)return;
  const auto name=QFileInfo(info.path).fileName();
  // M1309's Unix and DOS parsers expose rawModifiedAt only. M1308 accepts
  // modifiedAt, so those successful listings still produce 'not found'.
  for(auto raw:listingBytes.split('\n')){if(raw.endsWith('\r'))raw.chop(1);int space=raw.indexOf(' ');if(space<0||QString::fromUtf8(raw.mid(space+1))!=name)continue;
   QHash<QByteArray,QByteArray> facts;for(auto fact:raw.left(space).split(';')){int equals=fact.indexOf('=');if(equals>0)facts[fact.left(equals).toLower()]=fact.mid(equals+1);}
   auto modified=facts.value("modify");auto type=facts.value("type");if(!modified.isEmpty()&&type!="cdir"&&type!="pdir")info.lastModified=QString::fromUtf8(modified);
  }
  listing=false;if(info.lastModified.isEmpty()){finish("not found");return;}command("size","SIZE "+info.path.toUtf8());
 }
 void receive(){
  lines+=control.readAll();if(lines.size()+multiline.size()>65536){finish("FTP reply too large");return;}
  while(!done){auto end=lines.indexOf('\n');if(end<0)return;auto line=lines.left(end+1).trimmed();lines.remove(0,end+1);if(line.size()<3)continue;bool ok=false;int code=line.left(3).toInt(&ok);
   if(multiCode){multiline+=line+'\n';if(ok&&code==multiCode&&line.size()>3&&line[3]==' '){auto result=multiline;multiline.clear();multiCode=0;reply(code,result);}continue;}
   if(!ok)continue;if(line.size()>3&&line[3]=='-'){multiCode=code;multiline=line+'\n';continue;}reply(code,line);
  }
 }
 void passive(bool extended){command(extended?"epsv":"pasv",extended?"EPSV":"PASV");}
 void reply(int code,const QByteArray&line){
  if(stage=="greeting"){if(code!=220){finish(QString::fromUtf8(line));return;}command("welcomeopts","OPTS UTF8 ON");}
  else if(stage=="welcomeopts"){command("user","USER "+(url.userName().isEmpty()?QByteArray("anonymous"):url.userName(QUrl::FullyDecoded).toUtf8()));}
  else if(stage=="user"){if(code==331)command("pass","PASS "+(url.password().isEmpty()?QByteArray("guest"):url.password(QUrl::FullyDecoded).toUtf8()));else if(code==230)command("initfeat","FEAT");else finish(QString::fromUtf8(line));}
  else if(stage=="pass"){if(code==230)command("initfeat","FEAT");else finish(QString::fromUtf8(line));}
  else if(stage=="initfeat"){parseFeatures(line,code);listCommands=features.contains("MLST")?QStringList{"MLSD","LIST -a","LIST"}:QStringList{"LIST -a","LIST"};command("type","TYPE I");}
  else if(stage=="type"){if(code>=400){finish(QString::fromUtf8(line));return;}command("stru","STRU F");}
  else if(stage=="stru"){command("utf8","OPTS UTF8 ON");}
  else if(stage=="utf8"&&features.contains("MLST")){command("mlstopts","OPTS MLST type;size;modify;unique;unix.mode;unix.owner;unix.group;unix.ownername;unix.groupname;");}
  else if(stage=="utf8"||stage=="mlstopts"){if(metadata)command("mdtm","MDTM "+info.path.toUtf8());else passive(true);}
  else if(stage=="mdtm"){if(code>=400){if(code==550)finish("not found");else startList();return;}info.lastModified=QString::fromUtf8(line.mid(4));command("size","SIZE "+info.path.toUtf8());}
  else if(stage=="size"){bool ok=false;info.size=line.mid(4).trimmed().toLongLong(&ok);if(code!=213||!ok||info.size<0){finish("Invalid FTP size");return;}command("feat","FEAT");}
  else if(stage=="feat"){parseFeatures(line,code);info.supportsSeek=!features.value("REST").isEmpty();finish();}
  else if(stage=="epsv"||stage=="pasv"){
   bool extended=stage=="epsv";if(extended&&code!=229){passive(false);return;}int port=0;
   if(extended){auto match=QRegularExpression("\\((.)(?:\\1){2}(\\d+)\\1\\)").match(QString::fromUtf8(line));port=match.captured(2).toInt();}
   else if(code==227){auto match=QRegularExpression("\\((?:\\d+,){4}(\\d+),(\\d+)\\)").match(QString::fromUtf8(line));port=match.captured(1).toInt()*256+match.captured(2).toInt();}
   if(port<=0||port>65535){finish("Invalid FTP passive endpoint");return;}
   // Use the control peer, never an untrusted PASV-advertised third-party host.
   dataEnded=false;data.connectToHost(control.peerAddress(),quint16(port));
  }else if(stage=="rest"){if(code!=350){finish(QString::fromUtf8(line));return;}command("retr","RETR "+info.path.toUtf8());}
  else if(stage=="list"){
   if(code>=400){listing=false;data.blockSignals(true);data.abort();data.blockSignals(false);if(++listIndex<listCommands.size())startList();else finish(QString::fromUtf8(line));}
   else if(code==226||code==250){listComplete=true;consume();finishList();}
  }
  else if(stage=="retr"){if(code>=400)finish(QString::fromUtf8(line));else if(code==226||code==250){transferComplete=true;consume();}}
 }
};
}
struct FtpSource::State {QUrl url;Token next=1;QHash<Token,Operation*> jobs;QPointer<Operation> sequential;};
FtpSource::FtpSource(QUrl url,QObject*p):QObject(p),state_(std::make_shared<State>()){state_->url=std::move(url);}
FtpSource::~FtpSource(){auto jobs=state_->jobs;state_->jobs.clear();if(state_->sequential)state_->sequential->cancel();for(auto job:jobs)job->cancel();}
FtpSource::Token FtpSource::inspect(std::function<void(FtpMetadata)> cb){auto s=state_;auto token=s->next++;auto job=new Operation(s->url);job->metadata=true;s->jobs[token]=job;job->callback=[s,token,cb=std::move(cb)](auto meta,auto){s->jobs.remove(token);cb(meta);};QTimer::singleShot(0,job,[job]{job->start();});return token;}
FtpSource::Token FtpSource::read(qint64 offset,qint64 count,RemoteByteSource::Completion cb,bool useRest){
 auto s=state_;auto token=s->next++;
 auto completion=[s,token,cb=std::move(cb)](auto,auto result){auto completed=s->jobs.take(token);if(completed&&s->sequential!=completed&&!completed->done)completed->cancel();cb(result);};
 auto job=s->sequential.data();
 if(job&&!job->done&&!job->callback&&job->offset==offset&&job->useRest==useRest&&count>0&&count<=1024*1024){s->jobs[token]=job;job->resume(count,std::move(completion));return token;}
 // Non-contiguous reads do not steal a pending operation. The next sequential
 // read may reuse only the most recently started transfer.
 if(job&&!job->callback)job->cancel();
 job=new Operation(s->url);job->offset=offset;job->count=count;job->useRest=useRest;job->skip=useRest?0:offset;s->jobs[token]=job;s->sequential=job;job->callback=std::move(completion);QTimer::singleShot(0,job,[job]{job->start();});return token;
}
void FtpSource::cancel(Token token){if(auto job=state_->jobs.take(token))job->cancel();}
int FtpSource::pendingRequests()const{return state_->jobs.size();}
FtpRangePlan ftpRangePlan(qint64 size,bool seek,const QString&range){
 FtpRangePlan p;p.length=size;p.acceptRanges=seek;if(range.isEmpty())return p;
 if(!seek&&range!="bytes=0-"&&range!="bytes=0-"+QString::number(size-1)){p.status=405;p.length=0;return p;}
 // Preserve M1308's end=0 truthiness and suffix start=null wire behavior.
 auto text=range;auto prefix=text.indexOf("bytes=");if(prefix>=0)text.remove(prefix,6);auto parts=text.split('-');
 auto parse=[](QString s,bool*ok){auto m=QRegularExpression("^\\s*([+-]?\\d+)").match(s);*ok=m.hasMatch();return m.captured(1).toLongLong();};
 bool startOk=false,endOk=false;auto start=parse(parts.value(0),&startOk),end=parse(parts.value(1),&endOk);if(!endOk||end==0)end=size-1;
 p.start=startOk?start:0;p.status=206;p.acceptRanges=true;
 if(end<0||p.start<0||end<p.start||end==std::numeric_limits<qint64>::max()){p.status=500;p.length=0;return p;}
 p.length=end-p.start+1;p.contentRange="bytes "+(startOk?QString::number(start):QString("null"))+"-"+(end?QString::number(end):QString())+"/"+QString::number(size);return p;
}
}
