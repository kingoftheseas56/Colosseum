#include "server1/archive/TarSource.h"
#include <QTimer>
#include <QPointer>
#include <limits>
namespace server1::archive {
namespace {
constexpr qint64 SafeInteger=9007199254740991LL;
QString str(const QByteArray &b,int start,int count){auto s=b.mid(start,count);auto n=s.indexOf('\0');if(n>=0)s.truncate(n);return QString::fromUtf8(s);}
qint64 oct(const QByteArray &b,int start,int count){auto s=b.mid(start,count);if(s.isEmpty())return 0;auto first=quint8(s[0]);if(first&128){if(first!=128&&first!=255)return 0;quint64 n=0;for(int i=1;i<s.size();++i){auto byte=first==128?quint8(s[i]):255-quint8(s[i]);if(n>quint64(SafeInteger)/256)return first==128?SafeInteger:-SafeInteger;n=n*256+byte;}return first==128?qint64(n):-qint64(n);}int i=0;while(i<s.size()&&(s[i]==' '||s[i]=='\0'))++i;qint64 n=0;bool neg=i<s.size()&&s[i]=='-';if(neg|| (i<s.size()&&s[i]=='+'))++i;while(i<s.size()&&s[i]>='0'&&s[i]<='7'){if(n>SafeInteger/8)return SafeInteger;n=n*8+s[i++]-'0';}return neg?-n:n;}
QString type(char c){switch(c){case 0:case '0':return "file";case '1':return "link";case '2':return "symlink";case '3':return "character-device";case '4':return "block-device";case '5':return "directory";case '6':return "fifo";case '7':return "contiguous-file";case 'x':return "pax-header";case 'g':return "pax-global-header";case 'K':return "gnu-long-link-path";case 'L':case 'N':return "gnu-long-path";default:return {};}}
QJsonObject pax(const QByteArray &b){QJsonObject result;qsizetype i=0;while(i<b.size()){auto start=i,space=b.indexOf(' ',i);if(space<0)break;bool ok=false;auto len=b.mid(i,space-i).toLongLong(&ok);if(!ok||len<=space-start+1||len>b.size()-start)break;auto pair=b.mid(space+1,start+len-space-2);auto eq=pair.indexOf('=');if(eq<0)break;result[QString::fromUtf8(pair.left(eq))]=QString::fromUtf8(pair.mid(eq+1));i=start+len;}return result;}
}
struct TarSource::State:std::enable_shared_from_this<State>{
 qint64 length=0,cursor=0,streamStart=0;Read read;Cancel cancelRead;QPointer<QObject> owner;
 quint64 token=0,generation=0;bool alive=true,streaming=false,used=false;int visited=0;
 Predicate predicate;Completion completion;QJsonObject global,pending;QString longName,longLink;
 void stop(){++generation;completion={};auto t=token;token=0;if(t)cancelRead(t);}
 void finish(TarEntry e={},QString error={}){auto cb=std::move(completion);if(alive&&cb)cb(std::move(e),std::move(error));}
 void request(qint64 at,qint64 count,std::function<void(QByteArray)> cb){auto self=shared_from_this();auto gen=generation;token=read(at,count,[self,gen,count,cb=std::move(cb)](ReadResult r){if(!self->alive||gen!=self->generation)return;self->token=0;if(!r.error.isEmpty()){self->finish({},r.error);return;}if(r.bytes.size()!=count){self->finish({},r.bytes.isEmpty()?"No matching TAR entry":"Premature end of tar stream");return;}cb(std::move(r.bytes));});}
 void later(){auto self=shared_from_this();auto gen=generation;QTimer::singleShot(0,owner,[self,gen](){if(self->alive&&gen==self->generation)self->next();});}
 void next(){if(!completion||!alive)return;if(++visited>10000){finish({},"TAR traversal limit exceeded");return;}if(cursor<0||cursor>SafeInteger-512){finish({},"Integer overflow in tar stream");return;}if(length>=0&&cursor+512>length){finish({},cursor==length?"No matching TAR entry":"Premature end of tar stream");return;}auto self=shared_from_this();request(cursor,512,[self](QByteArray b){self->header(b);});}
 void header(const QByteArray&b){bool empty=true;for(char c:b)if(c){empty=false;break;}if(empty){cursor+=512;later();return;}qint64 sum=256;for(int i=0;i<512;++i)if(i<148||i>=156)sum+=quint8(b[i]);if(sum!=oct(b,148,8)){finish({},"Invalid tar header. Maybe the tar is corrupted or it needs to be gunzipped?");return;}if(b.mid(257,6)!=QByteArray("ustar\0",6)&&!(b.mid(257,6)=="ustar "&&b.mid(263,2)==QByteArray(" \0",2))){finish({},"Invalid tar header: unknown format.");return;}
  TarEntry e;e.name=str(b,0,100);if(b.mid(257,6)==QByteArray("ustar\0",6)&&b[345])e.name=str(b,345,155)+"/"+e.name;e.type=type(b[156]);e.linkname=str(b,157,100);e.size=oct(b,124,12);e.offset=cursor-streamStart;e.dataOffset=streamStart+2*e.offset+512;e.streamOffset=cursor+512;e.streaming=streaming;
  if(e.size<0||e.size>SafeInteger-cursor-1024){finish({},"Integer overflow in tar stream");return;}if(e.type=="gnu-long-path"||e.type=="gnu-long-link-path"||e.type=="pax-header"||e.type=="pax-global-header"){
   // Bound extended metadata; unlike regular entries it must be retained.
   if(e.size>1024*1024){finish({},"TAR extended header exceeds metadata limit");return;}auto self=shared_from_this();auto apply=[self,e](QByteArray bytes){if(e.type=="gnu-long-path")self->longName=str(bytes,0,bytes.size());else if(e.type=="gnu-long-link-path")self->longLink=str(bytes,0,bytes.size());else if(e.type=="pax-global-header")self->global=pax(bytes);else{self->pending=self->global;auto values=pax(bytes);for(auto i=values.begin();i!=values.end();++i)self->pending[i.key()]=i.value();}self->cursor+=512+((e.size+511)/512)*512;self->later();};if(e.size)request(cursor+512,e.size,apply);else apply({});return;
  }
  if(!longName.isEmpty()){e.name=longName;longName.clear();}if(!longLink.isEmpty()){e.linkname=longLink;longLink.clear();}if(!pending.isEmpty()){if(!pending.value("path").toString().isEmpty())e.name=pending.value("path").toString();if(!pending.value("linkpath").toString().isEmpty())e.linkname=pending.value("linkpath").toString();if(!pending.value("size").toString().isEmpty()){bool ok=false;auto n=pending.value("size").toString().toLongLong(&ok);if(!ok||n<0||n>SafeInteger-cursor-1024){finish({},"Invalid TAR pax size");return;}e.size=n;}e.pax=pending;pending={};}
  if(predicate(e)){finish(e);return;}
  // M1297 _unlock uses the relative header.offset as an absolute seek target.
  // This is deliberately not corrected: skipping the second nonempty file can
  // revisit it. M1304 consumes forward and does not have this behavior.
  if(!streaming&&e.size>0){cursor=e.offset+512+((e.size+511)/512)*512;streamStart=cursor;}else cursor+=512+((e.size+511)/512)*512;
  later();
 }
};
TarSource::TarSource(qint64 length,Read read,Cancel cancel,QObject*parent,bool streaming):QObject(parent),state_(std::make_shared<State>()){state_->length=length;state_->read=std::move(read);state_->cancelRead=std::move(cancel);state_->owner=this;state_->streaming=streaming;}
TarSource::~TarSource(){state_->alive=false;state_->stop();}
void TarSource::cancel(){state_->stop();}
void TarSource::find(Predicate predicate,Completion completion){auto s=state_;if(s->used){QTimer::singleShot(0,this,[cb=std::move(completion)](){cb({},"TAR selection already started");});return;}s->used=true;s->predicate=std::move(predicate);s->completion=std::move(completion);s->later();}
struct TarEntryReader::State:std::enable_shared_from_this<State>{TarEntry entry;ZipSource::Read read;ZipSource::Cancel cancelRead;QPointer<QObject> owner;qint64 position=0,end=0;quint64 token=0,generation=0;bool alive=true,stopped=false;Completion completion;
 void stop(){++generation;stopped=true;completion={};auto t=token;token=0;if(t)cancelRead(t);}
 void deliver(ZipChunk chunk){auto cb=std::move(completion);if(alive&&cb)cb(std::move(chunk));}
 void pump(qint64 maximum){if(!completion||!alive)return;if(stopped||position>=end){deliver({{}, {},true});return;}auto self=shared_from_this();auto gen=generation;auto count=qMin<qint64>(maximum,qMin<qint64>(end-position,65536));token=read((entry.streaming?entry.streamOffset:entry.dataOffset)+position,count,[self,gen,count](ReadResult r){if(!self->alive||gen!=self->generation)return;self->token=0;if(!r.error.isEmpty()){self->stopped=true;self->deliver({{},r.error,true});return;}if(r.bytes.size()!=count){self->stopped=true;self->deliver({{},"Premature end of stream",true});return;}self->position+=r.bytes.size();self->deliver({std::move(r.bytes),{},self->position>=self->end});});}
};
TarEntryReader::TarEntryReader(TarEntry entry,ZipSource::Read read,ZipSource::Cancel cancel,QObject*parent,qint64 start,qint64 end):QObject(parent),state_(std::make_shared<State>()){auto s=state_;s->entry=std::move(entry);s->read=std::move(read);s->cancelRead=std::move(cancel);s->owner=this;s->position=qMax<qint64>(0,start);auto relativeEnd=end<0?s->entry.size:qMin(end,s->entry.size);s->end=relativeEnd;if(!s->entry.streaming&&s->position<relativeEnd)++s->end;}
TarEntryReader::~TarEntryReader(){state_->alive=false;state_->stop();}
void TarEntryReader::cancel(){state_->stop();}
void TarEntryReader::pull(qint64 maximum,Completion completion){auto s=state_;if(s->completion){QTimer::singleShot(0,this,[completion=std::move(completion)](){completion({{},"A TAR pull is already pending",false});});return;}s->completion=std::move(completion);auto gen=s->generation;QTimer::singleShot(0,this,[s,gen,maximum](){if(gen!=s->generation)return;if(maximum<=0||maximum>1024*1024){s->deliver({{},"Invalid bounded TAR pull size",false});return;}s->pump(maximum);});}
}
