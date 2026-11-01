#include "server1/archive/ZipSource.h"
#include <QPointer>
#include <QTimer>
#include <zlib.h>
#include <limits>
namespace server1::archive {
struct ZipEntryReader::State : std::enable_shared_from_this<State> {
 ZipEntry entry;ZipSource::Read read;ZipSource::Cancel cancelRead;QPointer<QObject> owner;quint64 token=0,generation=0;bool alive=true,eof=false,initialized=false;QString error;Completion completion;qint64 maximum=0,consumed=0;QByteArray input;z_stream codec{};
 ~State(){if(initialized)inflateEnd(&codec);}
 void stop(){++generation;completion={};auto pending=token;token=0;input.clear();codec.avail_in=0;codec.next_in=nullptr;if(pending)cancelRead(pending);}
 void deliver(QByteArray bytes={},QString failure={}){if(!failure.isEmpty()){error=failure;eof=true;}auto cb=std::move(completion);if(cb&&alive)cb({std::move(bytes),error,eof});}
 void pump(){if(!alive||!completion||token)return;if(!error.isEmpty()||eof){deliver();return;}if(entry.flags&1){deliver({},"_password is not defined");return;}if(entry.dataOffset<0||entry.compressedSize<0||entry.dataOffset>std::numeric_limits<qint64>::max()-entry.compressedSize){deliver({},"Invalid ZIP entry interval");return;}
  if(entry.method){if(!initialized){if(inflateInit2(&codec,-MAX_WBITS)!=Z_OK){deliver({},"Cannot initialize ZIP inflater");return;}initialized=true;}
   if(codec.avail_in||consumed==entry.compressedSize){QByteArray out(maximum,Qt::Uninitialized);codec.next_out=reinterpret_cast<Bytef*>(out.data());codec.avail_out=uInt(out.size());int code=inflate(&codec,Z_NO_FLUSH);out.resize(out.size()-codec.avail_out);if(code==Z_STREAM_END)eof=true;else if(code!=Z_OK&&code!=Z_BUF_ERROR){deliver({},QString::fromLatin1(codec.msg?codec.msg:"Invalid deflate stream"));return;}if(!out.isEmpty()||eof){deliver(std::move(out));return;}if(consumed==entry.compressedSize){deliver({},"unexpected end of file");return;}}
  }else if(consumed==entry.compressedSize){eof=true;deliver();return;}
  qint64 count=qMin<qint64>(entry.compressedSize-consumed,entry.method?65536:qMin<qint64>(maximum,65536));if(count<=0){deliver({},"unexpected end of file");return;}
  auto self=shared_from_this();auto gen=generation;token=read(entry.dataOffset+consumed,count,[self,gen,count](ReadResult r){if(!self->alive||gen!=self->generation)return;self->token=0;if(!r.error.isEmpty()){self->deliver({},r.error);return;}if(r.bytes.size()!=count){self->deliver({},"FILE_ENDED");return;}self->consumed+=count;if(!self->entry.method){self->eof=self->consumed==self->entry.compressedSize;self->deliver(std::move(r.bytes));return;}self->input=std::move(r.bytes);self->codec.next_in=reinterpret_cast<Bytef*>(self->input.data());self->codec.avail_in=uInt(self->input.size());self->pump();});
 }
};
ZipEntryReader::ZipEntryReader(ZipEntry e,ZipSource::Read read,ZipSource::Cancel cancel,QObject*parent):QObject(parent),state_(std::make_shared<State>()){state_->entry=std::move(e);state_->read=std::move(read);state_->cancelRead=std::move(cancel);state_->owner=this;}
ZipEntryReader::~ZipEntryReader(){state_->alive=false;state_->stop();}
void ZipEntryReader::cancel(){auto s=state_;s->stop();s->eof=true;}
void ZipEntryReader::pull(qint64 count,Completion cb){auto s=state_;if(s->completion){QTimer::singleShot(0,this,[cb=std::move(cb)](){cb({{},"A ZIP pull is already pending",false});});return;}s->completion=std::move(cb);s->maximum=count;if(count<=0||count>1024*1024){QTimer::singleShot(0,this,[s](){s->deliver({},"Invalid bounded ZIP pull size");});return;}auto gen=s->generation;QTimer::singleShot(0,this,[s,gen](){if(s->generation==gen)s->pump();});}
}
