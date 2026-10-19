#include "server1/archive/ZipSource.h"
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <iostream>
using namespace server1::archive;
int main(int argc,char**argv){QCoreApplication app(argc,argv);int fail=0;auto check=[&](bool b,const char*m){if(!b){std::cerr<<m<<'\n';++fail;}};auto until=[&](auto p){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<2000)app.processEvents(QEventLoop::AllEvents,10);check(p(),"timeout");};for(QString name:{"stored.zip","deflated.zip"}){QFile f(QString(argv[1])+"/"+name);if(!f.open(QIODevice::ReadOnly))return 2;auto bytes=f.readAll();int reads=0;quint64 seq=0;QSet<quint64> cancelled;ZipSource::Read read=[&](qint64 p,qint64 n,auto cb){auto id=++seq;++reads;check(n<=65536,"bounded source read");QTimer::singleShot(0,&app,[&,p,n,id,cb](){if(!cancelled.contains(id))cb({bytes.mid(p,n),{}});});return id;};auto cancel=[&](quint64 t){cancelled.insert(t);};ZipEntry e;e.size=200000;e.compressedSize=name=="stored.zip"?200000:211;e.dataOffset=39;e.method=name=="stored.zip"?0:8;ZipEntryReader stream(e,read,cancel);check(reads==0,"lazy reader");QByteArray output;bool eof=false;for(int i=0;i<400&&!eof;++i){bool got=false;stream.pull(777,[&](ZipChunk c){check(c.error.isEmpty(),"stream error");check(c.bytes.size()<=777,"bounded output");output+=c.bytes;eof=c.eof;got=true;});until([&]{return got;});}check(eof&&output==QByteArray(200000,'x'),"exact native inflate/stored output");}
 std::cout<<"failures="<<fail<<'\n';return fail?1:0;}
