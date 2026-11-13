#include "server1/archive/RarSevenZipSource.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <iostream>
using namespace server1::archive;
struct Memory {
 QByteArray bytes;QObject *owner;quint64 next=0;QSet<quint64> pending;int reads=0,cancels=0;qint64 largest=0;
 ZipSource::Read reader(){return [this](qint64 at,qint64 n,RemoteByteSource::Completion cb){auto token=++next;pending.insert(token);++reads;largest=qMax(largest,n);QTimer::singleShot(0,owner,[this,at,n,token,cb=std::move(cb)]{if(pending.remove(token))cb({bytes.mid(at,n),{},206,bytes.size()});});return token;};}
 ZipSource::Cancel cancel(){return [this](quint64 token){if(pending.remove(token))++cancels;};}
};
bool until(std::function<bool()> done){QEventLoop loop;QTimer tick;tick.setInterval(1);QObject::connect(&tick,&QTimer::timeout,&loop,[&]{if(done())loop.quit();});tick.start();QTimer::singleShot(3000,&loop,&QEventLoop::quit);if(!done())loop.exec();return done();}
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile file(argv[1]);if(!file.open(QIODevice::ReadOnly))return 2;auto root=QJsonDocument::fromJson(file.readAll()).object();int checks=0,failures=0;auto check=[&](bool ok,QString message){++checks;if(!ok){++failures;std::cerr<<message.toStdString()<<'\n';}};
 for(const auto value:root["rows"].toArray()){auto row=value.toObject(),expected=row["expected"].toObject();auto label=row["name"].toString();QByteArray all;QList<qint64> lengths;for(auto p:row["parts"].toArray()){auto part=QByteArray::fromBase64(p.toString().toLatin1());lengths.append(part.size());all+=part;}Memory memory{all,&app};RarSevenZipSource source(row["family"].toString()=="rar"?SourceFamily::Rar:SourceFamily::SevenZip,all.size(),memory.reader(),memory.cancel());source.setVolumeLengths(lengths);bool done=false;ArchiveStoredEntry entry;QString error;source.find([&](const QString&,int i){return i==row["index"].toInt();},[&](ArchiveStoredEntry e,QString err){entry=std::move(e);error=err;done=true;});check(until([&]{return done;}),label+" metadata timeout");if(expected.contains("error")){check(error==expected["error"].toString(),label+" error "+error);continue;}check(error.isEmpty(),label+" metadata "+error);check(entry.name==expected["name"].toString(),label+" name "+entry.name);check(entry.length==expected["length"].toInteger(),label+" length "+QString::number(entry.length));
 for(auto rv:expected["ranges"].toArray()){auto range=rv.toObject();int before=memory.reads;ArchiveStoredReader reader(entry,memory.reader(),memory.cancel(),nullptr,range["start"].toInteger(),range["end"].toInteger());check(memory.reads==before,label+" eager body read");QByteArray output;QString err;bool eof=false;for(int i=0;i<1000&&!eof;++i){bool pulled=false;reader.pull(entry.length>3000?4093:3,[&](ZipChunk c){output+=c.bytes;err=c.error;eof=c.eof;pulled=true;});check(until([&]{return pulled;}),label+" body timeout");}check(eof,label+" eof");if(range.contains("error"))check(err==range["error"].toString(),label+" range error "+err);else check(err.isEmpty()&&output==QByteArray::fromBase64(range["body"].toString().toLatin1()),label+" body mismatch "+err);}
 check(memory.largest<=65536,label+" bounded read");
 // Cancellation must detach before abort, with no callback after cancellation.
 {RarSevenZipSource pending(SourceFamily::Rar,all.size(),memory.reader(),memory.cancel());bool called=false;pending.find([](const QString&,int){return true;},[&](ArchiveStoredEntry,QString){called=true;});pending.cancel();QCoreApplication::processEvents();check(!called&&memory.pending.isEmpty(),label+" cancellation before metadata");}
 {RarSevenZipSource pending(SourceFamily::Rar,all.size(),memory.reader(),memory.cancel());bool called=false;pending.find([](const QString&,int){return true;},[&](ArchiveStoredEntry,QString){called=true;});QCoreApplication::processEvents();pending.cancel();QCoreApplication::processEvents();check(!called&&memory.pending.isEmpty(),label+" cancellation during metadata");}
 {ArchiveStoredReader pending(entry,memory.reader(),memory.cancel());bool called=false;pending.pull(3,[&](ZipChunk){called=true;});QCoreApplication::processEvents();pending.cancel();QCoreApplication::processEvents();check(!called&&memory.pending.isEmpty(),label+" cancellation during body");}
 }
 check(orderArchiveStoredUrls(SourceFamily::Rar,QJsonArray{"http://fixture/c.r02","http://fixture/a.rar","http://fixture/c.r00","http://fixture/no.txt"})==QJsonArray{"http://fixture/a.rar","http://fixture/c.r00","http://fixture/c.r02"},"RAR bundle order/filter");
 check(orderArchiveStoredUrls(SourceFamily::Rar,QJsonArray{"http://fixture/a.rar","http://fixture/a.part12.rar","http://fixture/a.part2.rar"})==QJsonArray{"http://fixture/a.part2.rar","http://fixture/a.part12.rar"},"RAR part bundle priority");
 check(orderArchiveStoredUrls(SourceFamily::SevenZip,QJsonArray{"http://fixture/a.7z.002","http://fixture/a.7z","http://fixture/a.7z.001","http://fixture/no.txt"})==QJsonArray{"http://fixture/a.7z","http://fixture/a.7z.001","http://fixture/a.7z.002"},"7zip bundle order/filter");
 check(orderArchiveStoredUrls(SourceFamily::SevenZip,QJsonArray{"http://fixture/a.7z","http://fixture/a.part12.7z","http://fixture/a.part2.7z"})==QJsonArray{"http://fixture/a.part2.7z","http://fixture/a.part12.7z"},"7zip part bundle priority");
 std::cout<<"RAR/7zip checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
