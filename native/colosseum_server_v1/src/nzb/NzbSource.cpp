#include "server1/nzb/NzbSource.h"
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>
#include <QPointer>
#include <QJsonDocument>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <zlib.h>
#include <limits>
namespace server1::nzb {
QList<File> parse(const QByteArray &xml,QString *error) {
    if(error)error->clear();
    QXmlStreamReader reader(xml);QList<File> files;File file;QString group,subject;bool root=false,inFile=false;int allocatedSegments=0;
    while(!reader.atEnd()) {
        reader.readNext();
        if(reader.isStartElement()) {
            const auto tag=reader.name();
            if(tag==u"nzb")root=true;
            else if(tag==u"file"){inFile=true;file.clear();group.clear();subject=reader.attributes().value(u"subject").toString();}
            else if(inFile&&tag==u"group") {auto value=reader.readElementText();if(group.isEmpty())group=value;}
            else if(inFile&&tag==u"segment") {
                bool ok=false;int number=reader.attributes().value(u"number").toInt(&ok);
                qint64 bytes=reader.attributes().value(u"bytes").toLongLong();
                if(!ok||number<1||number>1000000){reader.raiseError("Invalid NZB segment number");break;}
                if(number>file.size()){const int added=number-int(file.size());if(allocatedSegments>1000000-added){reader.raiseError("NZB segment limit exceeded");break;}allocatedSegments+=added;}while(file.size()<number)file.append(Segment{});
                file[number-1]={group,reader.readElementText(),subject,bytes};
            }
        } else if(reader.isEndElement()&&reader.name()==u"file") {files.append(file);inFile=false;}
    }
    if(reader.hasError()||!root||files.isEmpty()) {if(error)*error=reader.hasError()?reader.errorString():QStringLiteral("Could not parse nzb file");return {};}
    return files;
}
std::optional<Server> parseServer(const QString &url) {
    static const QRegularExpression re(QStringLiteral(R"((nntps?)\:\/\/(.*)\:(.*)\@(.*)\:([0-9]+)\/([0-9]+))"),QRegularExpression::CaseInsensitiveOption);
    auto match=re.match(url);if(!match.hasMatch())return {};
    bool ok=false;int port=match.captured(5).toInt(&ok);if(!ok||port<1||port>65535)return {};
    Server server;server.tls=match.captured(1).compare("nntps",Qt::CaseInsensitive)==0;
    server.user=QUrl::fromPercentEncoding(match.captured(2).toUtf8());server.password=QUrl::fromPercentEncoding(match.captured(3).toUtf8());server.host=match.captured(4);server.port=quint16(port);server.connections=match.captured(6).toInt();
    if(server.connections<1)return {};return server;
}
Decoded decodeYenc(const QByteArray &body) {
    Decoded result;bool began=false,ended=false;
    for(auto line:body.split('\n')) {
        if(line.endsWith('\r'))line.chop(1);
        if(line.startsWith(".."))line.remove(0,1);
        if(line.startsWith("=ybegin")){began=true;auto at=line.indexOf("name=");if(at>=0)result.filename=QString::fromUtf8(line.mid(at+5));continue;}
        if(line.startsWith("=ypart"))continue;
        if(line.startsWith("=yend")) {ended=true;auto at=line.indexOf("pcrc32=");if(at>=0){bool ok=false;auto checksum=line.mid(at+7).split(' ').first().toUInt(&ok,16);result.crcMismatch=!ok||checksum!=::crc32(0,reinterpret_cast<const Bytef*>(result.bytes.constData()),uInt(result.bytes.size()));}break;}
        bool escape=false;for(unsigned char ch:line){if(ch=='='&&!escape){escape=true;continue;}result.bytes.append(char((int(ch)-(escape?64:0)-42)&255));escape=false;}
        if(escape){result.error="Truncated yEnc escape";return result;}
    }
    if(!began||!ended||result.filename.isEmpty())result.error="Invalid yEnc article";
    return result;
}
namespace {
bool safe(const QString &s){return !s.contains('\r')&&!s.contains('\n');}
struct ArticleJob : QObject {
    QTcpSocket socket;QTimer timer;QByteArray buffer,body;int phase=0;Server server;Segment segment;std::function<void(Decoded)> done;bool finished=true;QString selectedGroup;
    explicit ArticleJob(QObject *p):QObject(p){timer.setSingleShot(true);socket.setReadBufferSize(16*1024*1024);}
    ~ArticleJob() override {QObject::disconnect(&socket,nullptr,nullptr,nullptr);timer.stop();socket.abort();}
    void finish(Decoded result){if(finished)return;finished=true;timer.stop();if(!result.error.isEmpty()){selectedGroup.clear();socket.abort();}phase=6;timer.start(30000);auto cb=std::move(done);if(cb)cb(std::move(result));}
    void fail(QString error){Decoded result;result.error=std::move(error);finish(std::move(result));}
    void command(QByteArray text,int next){phase=next;socket.write(text+"\r\n");}
    void article(){auto id=segment.article;if(!id.startsWith('<'))id='<'+id+'>';command("ARTICLE "+id.toUtf8(),4);}
    void group(){if(selectedGroup==segment.group)article();else command("GROUP "+segment.group.toUtf8(),3);}
    void start(int timeout){finished=false;buffer.clear();body.clear();timer.start(timeout);if(server.tls){fail("NNTP TLS is not implemented");return;}if(server.host.isEmpty()||!safe(server.user)||!safe(server.password)||!safe(segment.group)||!safe(segment.article)||segment.article.isEmpty()){fail("Invalid NNTP request");return;}if(socket.state()==QAbstractSocket::ConnectedState)group();else{selectedGroup.clear();phase=0;socket.connectToHost(server.host,server.port);}}
    void consume(){
        buffer+=socket.readAll();if(buffer.size()+body.size()>16*1024*1024){fail("NNTP article exceeds 16 MiB limit");return;}
        for(;;){auto end=buffer.indexOf("\r\n");if(end<0)return;auto line=buffer.left(end);buffer.remove(0,end+2);
            if(phase==5){if(line=="."){auto split=body.indexOf("\r\n\r\n");if(split<0){fail("Invalid NNTP article headers");return;}finish(decodeYenc(body.mid(split+4)));return;}body+=line+"\r\n";continue;}
            const int code=line.left(3).toInt();
            if(phase==0){if(code!=200&&code!=201){fail("NNTP greeting rejected");return;}if(!server.user.isEmpty()||!server.password.isEmpty())command("AUTHINFO USER "+server.user.toUtf8(),1);else group();}
            else if(phase==1){if(code!=381){fail("NNTP username rejected");return;}command("AUTHINFO PASS "+server.password.toUtf8(),2);}
            else if(phase==2){if(code/100!=2){fail("NNTP authentication rejected");return;}group();}
            else if(phase==3){if(code/100!=2){fail("NNTP group rejected");return;}selectedGroup=segment.group;article();}
            else if(phase==4){if(code/100!=2){fail(QStringLiteral("NNTP article unavailable (%1)").arg(code));return;}phase=5;}
        }
    }
};
}
struct ArticleClient::State : std::enable_shared_from_this<State> {
    struct Task {QString key,pool;Server server;Segment segment;QHash<Token,std::function<void(Decoded)>> subscribers;ArticleJob *job=nullptr;};
    QPointer<ArticleClient> owner;QHash<QString,std::shared_ptr<Task>> tasks;QHash<Token,QString> tokens;QHash<QString,QList<ArticleJob*>> pools;QList<QString> queue;Token next=0;int timeout=20000;bool alive=true;
    void schedule(){if(!owner||!alive)return;auto weak=weak_from_this();QTimer::singleShot(0,owner,[weak]{if(auto self=weak.lock())self->pump();});}
    void pump(){
        if(!owner||!alive)return;
        const auto queued=queue;
        for(const auto &key:queued){if(!owner||!alive)return;auto task=tasks.value(key);if(!task||task->job)continue;auto &pool=pools[task->pool];ArticleJob *worker=nullptr;for(auto candidate:pool)if(candidate->finished){worker=candidate;break;}
            if(!worker&&pool.size()<qBound(1,task->server.connections,64)){worker=new ArticleJob(owner);pool.append(worker);auto job=worker;
                QObject::connect(&job->socket,&QTcpSocket::readyRead,job,[job]{job->consume();});
                QObject::connect(&job->socket,&QTcpSocket::errorOccurred,job,[job](auto){job->fail("NNTP connection failed");});
                QObject::connect(&job->socket,&QTcpSocket::disconnected,job,[job]{job->selectedGroup.clear();job->fail("NNTP connection closed before article completed");});
                QObject::connect(&job->timer,&QTimer::timeout,job,[job]{if(job->finished){job->selectedGroup.clear();job->socket.abort();}else job->fail("NNTP request timed out");});
            }
            if(!worker)continue;queue.removeAll(key);task->job=worker;worker->server=task->server;worker->segment=task->segment;auto weak=weak_from_this();
            worker->done=[weak,key](Decoded result){auto self=weak.lock();if(!self||!self->alive)return;auto task=self->tasks.take(key);if(!task)return;task->job=nullptr;auto callbacks=std::move(task->subscribers);self->schedule();for(auto i=callbacks.begin();i!=callbacks.end();++i){if(!self->alive)return;if(!self->tokens.remove(i.key()))continue;i.value()(result);}};
            worker->start(timeout);
        }
    }
};
ArticleClient::ArticleClient(QObject *p):QObject(p),state_(std::make_shared<State>()){state_->owner=this;}
ArticleClient::~ArticleClient(){state_->alive=false;state_->owner=nullptr;for(const auto &pool:state_->pools)for(auto job:pool){job->done={};delete job;}state_->pools.clear();state_->tasks.clear();state_->tokens.clear();}
void ArticleClient::setTimeout(int milliseconds){state_->timeout=qMax(1,milliseconds);}
ArticleClient::Token ArticleClient::get(const Server &server,const Segment &segment,std::function<void(Decoded)> done){
    auto s=state_;auto token=++s->next;
    auto pool=QString::fromUtf8(QJsonDocument(QJsonArray{server.host,int(server.port),server.user,server.password,server.tls,qBound(1,server.connections,64)}).toJson(QJsonDocument::Compact));
    auto key=pool+QString::fromUtf8(QJsonDocument(QJsonArray{segment.group,segment.article}).toJson(QJsonDocument::Compact));
    auto task=s->tasks.value(key);if(!task){task=std::make_shared<State::Task>();task->key=key;task->pool=pool;task->server=server;task->segment=segment;s->tasks[key]=task;s->queue.append(key);}task->subscribers[token]=std::move(done);s->tokens[token]=key;s->schedule();return token;
}
void ArticleClient::cancelAll(){const auto tokens=state_->tokens.keys();for(auto token:tokens)cancel(token);}
void ArticleClient::cancel(Token token){auto s=state_;auto key=s->tokens.take(token);auto task=s->tasks.value(key);if(!task)return;task->subscribers.remove(token);if(!task->subscribers.isEmpty())return;s->tasks.remove(key);s->queue.removeAll(key);if(auto job=task->job){job->done={};job->finished=true;job->timer.stop();job->selectedGroup.clear();job->socket.abort();}s->schedule();}
struct NzbSource::State : std::enable_shared_from_this<State> {
    std::shared_ptr<ArticleClient> clientOwner;ArticleClient &client;ArticleClient::Token opening=0;
    explicit State(std::shared_ptr<ArticleClient> shared):clientOwner(shared?std::move(shared):std::make_shared<ArticleClient>()),client(*clientOwner){}
    QList<Server> servers;File file;QString name;qint64 length=-1,chunk=0;Token next=0;
    struct Read {qint64 offset,count;QByteArray bytes;archive::RemoteByteSource::Completion done;ArticleClient::Token active=0;};
    QHash<Token,std::shared_ptr<Read>> reads;bool alive=true;
    struct Cached{Decoded value;qint64 expires=0;};QHash<int,Cached> cache;QList<int> order;qint64 cacheBytes=0;
    void remember(int index,const Decoded &value){if(value.bytes.size()>16*1024*1024)return;if(cache.contains(index))cacheBytes-=cache[index].value.bytes.size();order.removeAll(index);while(cacheBytes+value.bytes.size()>32*1024*1024&&!order.isEmpty()){auto old=order.takeFirst();cacheBytes-=cache.take(old).value.bytes.size();}cache[index]={value,QDateTime::currentMSecsSinceEpoch()+7000};cacheBytes+=value.bytes.size();order.append(index);auto weak=weak_from_this();auto expires=cache[index].expires;QTimer::singleShot(7000,&client,[weak,index,expires]{if(auto self=weak.lock()){if(self->cache.contains(index)&&self->cache[index].expires==expires){self->cacheBytes-=self->cache.take(index).value.bytes.size();self->order.removeAll(index);}}});}
    void fetch(int index,int backbone,std::function<void(Decoded)> cb,std::shared_ptr<Read> read={}){
        if(backbone==0&&cache.contains(index)){auto entry=cache[index];if(entry.expires>QDateTime::currentMSecsSinceEpoch()){auto self=shared_from_this();QTimer::singleShot(0,&client,[self,read,cb,entry]{if(self->alive&&(!read||read->done))cb(entry.value);});return;}cacheBytes-=entry.value.bytes.size();cache.remove(index);order.removeAll(index);}
        if(backbone>=servers.size()){Decoded r;r.error="NNTP article unavailable from all servers";cb(r);return;}
        auto self=shared_from_this();auto token=client.get(servers[backbone],file[index],[self,index,backbone,cb=std::move(cb),read](Decoded result) mutable {if(!read)self->opening=0;if(!self->alive)return;if(read&& !read->done)return;if(!result.error.isEmpty())self->fetch(index,backbone+1,std::move(cb),read);else {self->remember(index,result);cb(std::move(result));}});if(read)read->active=token;else opening=token;
    }
    void nextRead(Token token){auto read=reads.value(token);if(!read)return;if(read->bytes.size()==read->count){reads.remove(token);auto cb=std::move(read->done);cb({read->bytes,{},200,length});return;}qint64 pos=read->offset+read->bytes.size();int index=int(pos/chunk);auto self=shared_from_this();fetch(index,0,[self,read,token,pos,index](Decoded result){if(!self->reads.contains(token))return;if(!result.error.isEmpty()&&index>0&&index<self->file.size()-1){result.error.clear();result.bytes=QByteArray(self->chunk,'\0');}
        if(!result.error.isEmpty()){self->reads.remove(token);auto cb=std::move(read->done);cb({{},result.error,502,self->length});return;}auto start=pos%self->chunk;auto count=qMin<qint64>(read->count-read->bytes.size(),result.bytes.size()-start);if(count<=0){self->reads.remove(token);auto cb=std::move(read->done);cb({{},"Invalid NZB segment geometry",502,self->length});return;}read->bytes+=result.bytes.mid(start,count);self->nextRead(token);},read);}
};
NzbSource::NzbSource(QObject *p,std::shared_ptr<ArticleClient> client):QObject(p),state_(std::make_shared<State>(std::move(client))){}
NzbSource::~NzbSource(){state_->alive=false;for(auto read:state_->reads){state_->client.cancel(read->active);read->done={};}state_->reads.clear();if(state_->opening)state_->client.cancel(state_->opening);}
void NzbSource::open(QList<Server> servers,File file,std::function<void(QString)> done){
    auto s=state_;if(s->length>=0||!s->file.isEmpty()){QTimer::singleShot(0,this,[done]{done("NZB source already opened");});return;}
    s->servers=std::move(servers);s->file=std::move(file);if(s->servers.isEmpty()||s->file.isEmpty()||s->file.first().article.isEmpty()||s->file.last().article.isEmpty()){QTimer::singleShot(0,this,[done]{done("Missing initial pieces from a file in nzb metadata");});return;}
    int missing=0;for(const auto &segment:s->file)if(segment.article.isEmpty())++missing;
    if(double(missing)/s->file.size()>.02){QTimer::singleShot(0,this,[done]{done("more than 2% of pieces are missing, breaking connection");});return;}
    s->fetch(0,0,[s,done](Decoded first){if(!first.error.isEmpty()||first.bytes.isEmpty()){done(first.error.isEmpty()?"Empty first segment":first.error);return;}s->name=first.filename;s->chunk=first.bytes.size();s->fetch(int(s->file.size()-1),0,[s,done](Decoded last){if(!last.error.isEmpty()){done(last.error);return;}if(s->chunk>std::numeric_limits<qint64>::max()/s->file.size()){done("NZB file size overflow");return;}s->length=(s->file.size()-1)*s->chunk+last.bytes.size();done({});});});
}
qint64 NzbSource::size()const{return state_->length;}
QString NzbSource::name()const{return state_->name;}
NzbSource::Token NzbSource::read(qint64 offset,qint64 count,archive::RemoteByteSource::Completion done){auto s=state_;auto token=++s->next;auto read=std::make_shared<State::Read>();read->offset=offset;read->count=count;read->done=std::move(done);s->reads[token]=read;QTimer::singleShot(0,this,[s,token]{auto r=s->reads.value(token);if(!r)return;if(s->length<0||r->offset<0||r->count<0||r->count>1024*1024||r->offset>s->length||r->count>s->length-r->offset){s->reads.remove(token);auto cb=std::move(r->done);cb({{},"Invalid NZB read range",416,s->length});return;}s->nextRead(token);});return token;}
void NzbSource::cancel(Token token){auto read=state_->reads.take(token);if(read){read->done={};state_->client.cancel(read->active);}}
}
