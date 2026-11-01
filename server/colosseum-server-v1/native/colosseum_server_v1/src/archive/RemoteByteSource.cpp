#include "server1/archive/ArchiveSources.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QMap>
#include <limits>
namespace server1::archive {
struct RemoteByteSource::State : std::enable_shared_from_this<State> {
 struct Job {QString key; qint64 offset=0,count=0;bool metadata=false,finished=false;QPointer<QNetworkReply> reply;QMap<Token,Completion> consumers;ReadResult result;};
 QUrl url;QPointer<QNetworkAccessManager> manager;bool alive=true;Token next=1;QHash<QString,std::shared_ptr<Job>> jobs;QHash<Token,std::shared_ptr<Job>> tokens;
 void finish(const std::shared_ptr<Job>&job,QString error={}) {
  if(job->finished)return;job->finished=true;jobs.remove(job->key);if(!error.isEmpty())job->result.error=error;
  if(job->reply){auto reply=job->reply;job->reply=nullptr;reply->disconnect();if(!reply->isFinished())reply->abort();reply->deleteLater();}
  // Keep sibling tokens cancellable throughout callback delivery. A callback may
  // cancel a sibling, create a replacement request, or destroy the source.
  auto consumers=std::move(job->consumers);for(auto it=consumers.begin();it!=consumers.end();++it){if(!alive)break;if(!tokens.remove(it.key()))continue;it.value()(job->result);}
 }
 void start(const std::shared_ptr<Job>&job){
  if(!alive||job->finished||job->consumers.isEmpty())return;
  if(!job->metadata&&(job->offset<0||job->count<=0||job->count>1024*1024||job->offset>std::numeric_limits<qint64>::max()-job->count)){finish(job,"Invalid bounded read interval");return;}
  QNetworkRequest request(url);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);request.setMaximumRedirectsAllowed(5);request.setTransferTimeout(30000);request.setRawHeader("Accept-Encoding","identity");if(!job->metadata)request.setRawHeader("Range","bytes="+QByteArray::number(job->offset)+"-"+QByteArray::number(job->offset+job->count-1));
  auto *reply=manager->get(request);reply->setReadBufferSize(64*1024);job->reply=reply;auto self=shared_from_this();
  auto headers=[self,job](){if(job->finished||!job->reply)return;auto *reply=job->reply.data();int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();if(status>=300&&status<400)return;job->result.status=status;if(!status)return;
   if(job->metadata){bool ok=false;auto n=reply->rawHeader("Content-Length").toLongLong(&ok);if(ok&&n>=0){job->result.totalLength=n;self->finish(job);}else self->finish(job,"Could not retrieve content-length from ranged request");return;}
   if(status!=206){self->finish(job,status==200?"Remote source ignored Range":"Unexpected remote range status");return;}
   auto match=QRegularExpression("^bytes (\\d+)-(\\d+)/(\\d+|\\*)$").match(QString::fromLatin1(reply->rawHeader("Content-Range")));bool ok1=false,ok2=false;auto first=match.captured(1).toLongLong(&ok1),last=match.captured(2).toLongLong(&ok2);if(!match.hasMatch()||!ok1||!ok2||first!=job->offset||last!=job->offset+job->count-1){self->finish(job,"Mismatched remote Content-Range");return;}bool totalOk=false;auto total=match.captured(3).toLongLong(&totalOk);if(totalOk){if(total<=last){self->finish(job,"Invalid remote total length");return;}job->result.totalLength=total;}
  };
  QObject::connect(reply,&QNetworkReply::metaDataChanged,manager,headers);
  QObject::connect(reply,&QIODevice::readyRead,manager,[self,job,headers](){headers();if(job->finished||!job->reply)return;auto remain=job->count-job->result.bytes.size();auto chunk=job->reply->read(remain+1);if(chunk.size()>remain){self->finish(job,"Remote range body exceeds requested length");return;}job->result.bytes+=chunk;});
  QObject::connect(reply,&QNetworkReply::finished,manager,[self,job,headers](){headers();if(job->finished||!job->reply)return;auto reply=job->reply;if(reply->error()!=QNetworkReply::NoError){self->finish(job,reply->errorString());return;}auto remain=job->count-job->result.bytes.size();auto rest=reply->read(remain+1);job->result.bytes+=rest;if(job->result.bytes.size()!=job->count){self->finish(job,"Short or oversized remote range body");return;}self->finish(job);});
 }
 Token add(qint64 offset,qint64 count,bool metadata,Completion cb){Token token=next++;QString key=metadata?"metadata":QString::number(offset)+":"+QString::number(count);auto job=jobs.value(key);bool fresh=!job;if(fresh){job=std::make_shared<Job>();job->key=key;job->offset=offset;job->count=count;job->metadata=metadata;jobs.insert(key,job);}job->consumers.insert(token,std::move(cb));tokens.insert(token,job);if(fresh){auto self=shared_from_this();QTimer::singleShot(0,manager,[self,job](){self->start(job);});}return token;}
};
RemoteByteSource::RemoteByteSource(QUrl url,QObject *parent):QObject(parent),state_(std::make_shared<State>()){state_->url=std::move(url);state_->manager=new QNetworkAccessManager(this);}
RemoteByteSource::~RemoteByteSource(){auto s=state_;s->alive=false;auto jobs=s->jobs;s->tokens.clear();for(auto job:jobs)s->finish(job,"Cancelled");}
RemoteByteSource::Token RemoteByteSource::read(qint64 offset,qint64 count,Completion cb){return state_->add(offset,count,false,std::move(cb));}
RemoteByteSource::Token RemoteByteSource::discoverLength(Completion cb){return state_->add(0,0,true,std::move(cb));}
void RemoteByteSource::cancel(Token token){auto s=state_;auto job=s->tokens.take(token);if(!job)return;job->consumers.remove(token);if(job->consumers.isEmpty())s->finish(job,"Cancelled");}
int RemoteByteSource::pendingRequests()const{return state_->jobs.size();}
}
