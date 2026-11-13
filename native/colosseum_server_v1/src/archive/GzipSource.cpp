#include "server1/archive/GzipSource.h"
#include <QTimer>
#include <QPointer>
#include <zlib.h>
namespace server1::archive {
struct GzipReader::State:std::enable_shared_from_this<State>{
 qint64 length=0,compressed=0,position=0,maximum=0,target=0,wanted=0;
 ZipSource::Read read;ZipSource::Cancel cancelRead;QPointer<QObject> owner;
 quint64 sourceToken=0,generation=0,nextToken=0,forwardToken=0;
 bool alive=true,initialized=false,memberEnd=false,eof=false,stopped=false;
 QString error;QByteArray input,output;z_stream codec{};Completion completion;RemoteByteSource::Completion forward;
 ~State(){if(initialized)inflateEnd(&codec);}
 void stop(){++generation;stopped=true;completion={};forward={};forwardToken=0;input.clear();output.clear();codec.avail_in=0;codec.next_in=nullptr;auto t=sourceToken;sourceToken=0;if(t)cancelRead(t);}
 void deliver(QByteArray bytes={},QString failure={}){if(!failure.isEmpty()){error=failure;eof=true;}if(forward){if(!error.isEmpty()){auto cb=std::move(forward);forwardToken=0;output.clear();cb({{},error});return;}if(position>target)output.append(bytes);if(output.size()>=wanted||eof){auto cb=std::move(forward);forwardToken=0;cb({std::move(output),{},0,eof?position:-1});output.clear();return;}later();return;}auto cb=std::move(completion);if(cb&&alive)cb({std::move(bytes),error,eof||stopped});}
 void later(){auto self=shared_from_this();auto gen=generation;QTimer::singleShot(0,owner,[self,gen](){if(self->alive&&gen==self->generation)self->pump();});}
 void pump(){if(!alive||(!completion&&!forward)||sourceToken)return;if(eof||stopped||!error.isEmpty()){deliver();return;}if(length<0){deliver({},"Unknown gzip source length");return;}if(!initialized){if(inflateInit2(&codec,MAX_WBITS+16)!=Z_OK){deliver({},"Cannot initialize gzip inflater");return;}initialized=true;}
  if(memberEnd){if(codec.avail_in&&*codec.next_in==0){eof=true;deliver();return;}if(!codec.avail_in&&compressed==length){eof=true;deliver();return;}if(codec.avail_in){auto ptr=codec.next_in;auto available=codec.avail_in;if(inflateReset2(&codec,MAX_WBITS+16)!=Z_OK){deliver({},"Cannot reset gzip inflater");return;}codec.next_in=ptr;codec.avail_in=available;memberEnd=false;}}
  if(!memberEnd&&(codec.avail_in||compressed==length)){qint64 count=forward?(position<target?qMin<qint64>(65536,target-position):qMin<qint64>(65536,wanted-output.size())):qMin<qint64>(maximum,65536);QByteArray out(count,Qt::Uninitialized);codec.next_out=reinterpret_cast<Bytef*>(out.data());codec.avail_out=uInt(count);int code=inflate(&codec,Z_NO_FLUSH);out.resize(count-codec.avail_out);if(code==Z_STREAM_END){memberEnd=true;if(!codec.avail_in&&compressed==length)eof=true;}else if(code!=Z_OK&&code!=Z_BUF_ERROR){deliver({},QString::fromLatin1(codec.msg?codec.msg:"Invalid gzip stream"));return;}position+=out.size();if(!out.isEmpty()||eof){deliver(std::move(out));return;}if(memberEnd){later();return;}if(compressed==length){deliver({},"unexpected end of file");return;}}
  qint64 count=qMin<qint64>(65536,length-compressed);if(count<=0){deliver({},"unexpected end of file");return;}auto self=shared_from_this();auto gen=generation;sourceToken=read(compressed,count,[self,gen,count](ReadResult result){if(!self->alive||gen!=self->generation)return;self->sourceToken=0;if(!result.error.isEmpty()){self->deliver({},result.error);return;}if(result.bytes.size()!=count){self->deliver({},"unexpected end of file");return;}self->compressed+=count;self->input=std::move(result.bytes);self->codec.next_in=reinterpret_cast<Bytef*>(self->input.data());self->codec.avail_in=uInt(count);self->later();});
 }
};
GzipReader::GzipReader(qint64 length,ZipSource::Read read,ZipSource::Cancel cancel,QObject*parent):QObject(parent),state_(std::make_shared<State>()){state_->length=length;state_->read=std::move(read);state_->cancelRead=std::move(cancel);state_->owner=this;}
GzipReader::~GzipReader(){state_->alive=false;state_->stop();}
void GzipReader::cancel(){state_->stop();}
void GzipReader::cancel(quint64 token){if(token&&token==state_->forwardToken)state_->stop();}
void GzipReader::pull(qint64 maximum,Completion completion){auto s=state_;if(s->completion||s->forward){QTimer::singleShot(0,this,[completion=std::move(completion)](){completion({{},"A gzip pull is already pending",false});});return;}if(maximum<=0||maximum>1024*1024){QTimer::singleShot(0,this,[completion=std::move(completion)](){completion({{},"Invalid bounded gzip pull size",false});});return;}s->completion=std::move(completion);s->maximum=maximum;s->later();}
quint64 GzipReader::readForward(qint64 offset,qint64 count,RemoteByteSource::Completion completion){auto s=state_;auto token=++s->nextToken;if(s->completion||s->forward||offset<s->position||count<=0||count>1024*1024){QTimer::singleShot(0,this,[completion=std::move(completion)](){completion({{},"Invalid forward gzip read"});});return token;}s->forwardToken=token;s->target=offset;s->wanted=count;s->output.clear();s->forward=std::move(completion);s->later();return token;}
}
