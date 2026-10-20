#include "server1/archive/ZipSource.h"
#include <QtEndian>
#include <QRegularExpression>
#include <QSet>
#include <limits>
namespace server1::archive {
namespace {
quint16 u16(const QByteArray&b,int p){return qFromLittleEndian<quint16>(b.constData()+p);}
quint32 u32(const QByteArray&b,int p){return qFromLittleEndian<quint32>(b.constData()+p);}
quint64 u64(const QByteArray&b,int p){return qFromLittleEndian<quint64>(b.constData()+p);}
std::optional<qint64> integer(QString text){auto m=QRegularExpression("^[\\s]*([+-]?\\d+)").match(text);if(!m.hasMatch())return {};bool ok=false;auto n=m.captured(1).toLongLong(&ok);return ok?std::optional<qint64>(n):std::nullopt;}
}
ZipRangePlan zipRangePlan(const ZipEntry&e,const QString&method,const QString&range){
 ZipRangePlan p;p.head=method=="HEAD";p.contentLength=e.size;p.end=e.size-1;p.sourceStart=e.dataOffset;p.sourceEnd=e.dataOffset+e.size;
 if(e.method){if(!range.isEmpty()&&range!="bytes=0-"&&range!="bytes=0-"+QString::number(e.size-1)){p.status=405;p.contentLength=0;return p;}p.status=p.head?204:200;return p;}
 p.acceptRanges=true;if(range.isEmpty())return p;QString clean=range;int prefix=clean.indexOf("bytes=");if(prefix>=0)clean.remove(prefix,6);auto pieces=clean.split('-');auto start=integer(pieces.value(0));auto end=pieces.value(1).isEmpty()?std::optional<qint64>(e.size-1):integer(pieces.value(1));if(start&&!end)end=e.size-1;if(!start&&end){start=e.size-*end;end=e.size-1;}
 if(!start||!end){p.status=500;p.error="Invalid ZIP range number";return p;}
 p.start=*start;p.end=*end;if(p.start>=e.size||p.end>=e.size||p.start<0||p.end<0){p.status=416;p.contentRange="bytes */"+QString::number(e.size);p.contentLength=0;return p;}
 p.status=206;p.contentRange="bytes "+QString::number(p.start)+"-"+QString::number(p.end)+"/"+QString::number(e.size);p.contentLength=p.end-p.start+1;
 if(p.start||p.end){p.sourceStart=e.dataOffset+p.start;p.sourceEnd=e.dataOffset+p.end;}return p;
}
struct ZipSource::State : std::enable_shared_from_this<State> {
 qint64 length;Read read;Cancel cancelRead;QSet<quint64> tokens;quint64 generation=0;bool alive=true;Completion completion;ZipDirectory directory;qint64 cursor=0;quint64 remaining=0;
 void stop(){++generation;auto pending=tokens;tokens.clear();completion={};for(auto t:pending)cancelRead(t);}
 void finish(QString error={}){if(!completion)return;directory.error=error;auto cb=std::move(completion);cb(std::move(directory));}
 void fetch(qint64 offset,qint64 count,std::function<void(QByteArray,QString)> cb){if(offset<0||count<0||count>1024*1024||offset>length||count>length-offset){cb({},"ZIP source interval out of bounds");return;}if(!count){cb({},{});return;}auto self=shared_from_this();auto gen=generation;auto token=std::make_shared<quint64>(0);*token=read(offset,count,[self,gen,token,count,cb=std::move(cb)](ReadResult r){self->tokens.remove(*token);if(!self->alive||self->generation!=gen)return;if(r.error.isEmpty()&&r.bytes.size()!=count)r.error="FILE_ENDED";cb(std::move(r.bytes),r.error);});tokens.insert(*token);}
 void central(qint64 offset,quint64 records){if(records>1000000){finish("ZIP directory record limit exceeded");return;}cursor=offset;remaining=records;next();}
 void next(){if(!remaining){finish();return;}auto self=shared_from_this();fetch(cursor,46,[self](QByteArray b,QString error){if(!error.isEmpty()){self->finish(error);return;}if(u32(b,0)!=0x02014b50){self->finish("Invalid ZIP central directory signature");return;}ZipEntry e;e.flags=u16(b,8);e.method=u16(b,10);e.crc=u32(b,16);e.compressedSize=u32(b,20);e.size=u32(b,24);e.localOffset=u32(b,42);int name=u16(b,28),extra=u16(b,30),comment=u16(b,32);self->cursor+=46;self->fetch(self->cursor,name+extra+comment,[self,e,name,extra,comment](QByteArray b,QString error)mutable{if(!error.isEmpty()){self->finish(error);return;}e.name=QString::fromUtf8(b.left(name));auto fields=b.mid(name,extra);for(int p=0;p+4<=fields.size();){int tag=u16(fields,p),n=u16(fields,p+2);p+=4;if(n>fields.size()-p){self->finish("Truncated ZIP extra field");return;}if(tag==1){int x=p;auto wide=[&](qint64 &value){if(value!=0xffffffffLL)return true;if(x+8>p+n)return false;auto number=u64(fields,x);x+=8;if(number>quint64(std::numeric_limits<qint64>::max()))return false;value=qint64(number);return true;};if(!wide(e.size)||!wide(e.compressedSize)||!wide(e.localOffset)){self->finish("Invalid ZIP64 extra field");return;}}p+=n;}self->directory.entries.append(e);self->cursor+=name+extra+comment;--self->remaining;self->next();});});}
 void start(){auto self=shared_from_this();qint64 offset=qMax<qint64>(0,length-80);fetch(offset,length-offset,[self,offset](QByteArray tail,QString error){if(!error.isEmpty()){self->finish(error);return;}int p=tail.indexOf(QByteArray::fromHex("504b0506"));if(p<0||tail.size()-p<22){self->finish("FILE_ENDED");return;}auto record=tail.mid(p);quint64 count=u16(record,10);qint64 central=u32(record,16);if(u16(record,4)==65535||count==65535||central==0xffffffffLL){self->fetch(offset+p-20,20,[self](QByteArray b,QString error){if(!error.isEmpty()){self->finish(error);return;}if(u32(b,0)!=0x07064b50){self->finish("Invalid ZIP64 locator signature");return;}auto offset=u64(b,8);if(offset>quint64(std::numeric_limits<qint64>::max())){self->finish("ZIP64 offset overflow");return;}self->fetch(qint64(offset),56,[self](QByteArray b,QString error){if(!error.isEmpty()){self->finish(error);return;}if(u32(b,0)!=0x06064b50){self->finish("Invalid ZIP64 end signature");return;}auto count=u64(b,32),offset=u64(b,48);if(offset>quint64(std::numeric_limits<qint64>::max())){self->finish("ZIP64 directory overflow");return;}self->central(qint64(offset),count);});});}else self->central(central,count);});}
};
ZipSource::ZipSource(qint64 length,Read read,Cancel cancel,QObject*parent):QObject(parent),state_(std::make_shared<State>()){state_->length=length;state_->read=std::move(read);state_->cancelRead=std::move(cancel);}
ZipSource::~ZipSource(){state_->alive=false;state_->stop();}
void ZipSource::open(Completion cb){auto s=state_;s->stop();s->directory={};s->completion=std::move(cb);s->start();}
void ZipSource::cancel(){state_->stop();}
void ZipSource::localEntry(ZipEntry entry,std::function<void(ZipEntry,QString)>cb){auto s=state_;s->fetch(entry.localOffset,30,[s,entry,cb=std::move(cb)](QByteArray b,QString error)mutable{if(!error.isEmpty()){cb(entry,error);return;}if(u32(b,0)!=0x04034b50){cb(entry,"Invalid ZIP local header signature");return;}int name=u16(b,26),extra=u16(b,28);qint64 overhead=30+name+extra+((entry.flags&1)?12:0);if(entry.localOffset>std::numeric_limits<qint64>::max()-overhead){cb(entry,"ZIP data offset overflow");return;}entry.dataOffset=entry.localOffset+overhead;if(entry.size==0)entry.size=u32(b,22);s->fetch(entry.localOffset+30,name,[entry,cb=std::move(cb)](QByteArray name,QString error)mutable{entry.name=QString::fromUtf8(name);cb(entry,error);});});}
}
