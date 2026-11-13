#include "server1/archive/ArchiveByteSource.h"
#include <QPointer>
#include <QTimer>
#include <limits>
#include <map>
namespace server1::archive {
struct ArchiveByteSource::State : std::enable_shared_from_this<State> {
    struct Job { Token token=0, pending=0; int part=-1; qint64 offset=0, left=0; QByteArray bytes; Completion completion; };
    bool alive=true; quint64 generation=0; Token next=1; qint64 length=0;
    QPointer<QObject> owner; SourceFamily family; QJsonArray urls; QList<qint64> lengths;
    std::vector<std::unique_ptr<RemoteByteSource>> sources;
    std::map<Token,std::shared_ptr<Job>> jobs;
    std::function<void(QString)> opened;
    void stop() {
        ++generation; opened={};
        for(auto &item:jobs) if(item.second->pending) sources[item.second->part]->cancel(item.second->pending);
        jobs.clear(); sources.clear(); lengths.clear(); length=0;
    }
    void finishOpen(QString error={}) { auto callback=std::move(opened); if(alive&&callback)callback(error); }
    void nextPart() {
        if(!alive||!opened)return;
        if(sources.size()==size_t(urls.size())) { finishOpen(); return; }
        auto descriptor=describeSource(family,urls[int(sources.size())],lengths,sources.size()+1==size_t(urls.size()));
        if(!descriptor.error.isEmpty()) {finishOpen(descriptor.error);return;}
        QUrl url(descriptor.url);
        if((url.scheme()!="http"&&url.scheme()!="https")||url.host().isEmpty()) {finishOpen("Unsupported archive source URL");return;}
        sources.push_back(std::make_unique<RemoteByteSource>(url));
        auto self=shared_from_this(); const auto gen=generation;
        auto accept=[self,gen](qint64 length,QString error) {
            if(!self->alive||gen!=self->generation)return;
            if(!error.isEmpty()){self->finishOpen(error);return;}
            if(length<0||length>std::numeric_limits<qint64>::max()-self->length){self->finishOpen("Invalid archive part length");return;}
            self->lengths.append(length);self->length+=length;
            QTimer::singleShot(0,self->owner,[self,gen](){if(gen==self->generation)self->nextPart();});
        };
        if(descriptor.length)accept(*descriptor.length,{});
        else sources.back()->discoverLength([accept](ReadResult result){accept(result.totalLength,result.error);});
    }
    void finish(const std::shared_ptr<Job>&job,QString error={}) {
        if(!jobs.erase(job->token))return;
        auto callback=std::move(job->completion);
        if(alive&&callback)callback({std::move(job->bytes),error,206,length});
    }
    void pump(const std::shared_ptr<Job>&job) {
        if(!alive||!jobs.count(job->token))return;
        if(job->offset<0||job->left<0||job->left>1024*1024||job->offset>length||job->left>length-job->offset){finish(job,"Invalid bounded archive interval");return;}
        if(!job->left){finish(job);return;}
        qint64 base=0; int part=0;
        while(part<lengths.size()&&job->offset-base>=lengths[part]){base+=lengths[part];++part;}
        if(part>=lengths.size()){finish(job,"FILE_ENDED");return;}
        job->part=part;const qint64 count=qMin(job->left,lengths[part]-(job->offset-base));
        auto self=shared_from_this();const auto gen=generation;
        job->pending=sources[part]->read(job->offset-base,count,[self,job,gen,count](ReadResult result){
            if(!self->alive||gen!=self->generation||!self->jobs.count(job->token))return;
            job->pending=0;
            if(!result.error.isEmpty()){self->finish(job,result.error);return;}
            if(result.bytes.size()!=count){self->finish(job,"FILE_ENDED");return;}
            job->bytes+=result.bytes;job->offset+=count;job->left-=count;self->pump(job);
        });
    }
};
ArchiveByteSource::ArchiveByteSource(QObject *parent):QObject(parent),state_(std::make_shared<State>()){state_->owner=this;}
ArchiveByteSource::~ArchiveByteSource(){state_->alive=false;state_->stop();}
void ArchiveByteSource::open(SourceFamily family,QJsonArray urls,std::function<void(QString)> completion){
    auto s=state_;s->stop();s->family=family;s->urls=std::move(urls);s->opened=std::move(completion);
    const auto gen=s->generation;
    QTimer::singleShot(0,this,[s,gen](){if(gen!=s->generation)return;if(s->urls.isEmpty())s->finishOpen("Missing archive sources");else s->nextPart();});
}
qint64 ArchiveByteSource::size()const{return state_->length;}
QList<qint64> ArchiveByteSource::partLengths()const{return state_->lengths;}
ArchiveByteSource::Token ArchiveByteSource::read(qint64 offset,qint64 count,Completion completion){
    auto s=state_;auto job=std::make_shared<State::Job>();job->token=s->next++;job->offset=offset;job->left=count;job->completion=std::move(completion);s->jobs.emplace(job->token,job);
    QTimer::singleShot(0,this,[s,job](){s->pump(job);});return job->token;
}
void ArchiveByteSource::cancel(Token token){auto s=state_;auto it=s->jobs.find(token);if(it==s->jobs.end())return;auto job=it->second;s->jobs.erase(it);if(job->pending)s->sources[job->part]->cancel(job->pending);}
}
