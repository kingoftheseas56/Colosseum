#include "server1/media/KeyframeIndex.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonArray>
#include <iostream>
using namespace server1::media;
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv); if(argc<3)return 2;
 const QString mode=argv[1]; QVector<int> indexes;
 if(argc>3)for(auto index:QString(argv[3]).split(','))indexes.append(index.toInt());
 auto print=[](const QJsonObject& o){std::cout<<QJsonDocument(o).toJson(QJsonDocument::Compact).constData()<<"\n";};
 if(mode.startsWith("async-")||mode.startsWith("cancel-")) {
  const QUrl url=QString(argv[2]).contains("://")?QUrl(argv[2]):QUrl::fromLocalFile(argv[2]);
  auto callback=[&](SamplesResult r){QJsonArray offsets;for(auto offset:r.sourceOffsets)offsets.append(double(offset));print({{"tracks",r.tracks},{"error",r.error},{"available",r.available},{"bytesFetched",double(r.bytesFetched)},{"sourceOffsets",offsets}});app.exit(0);};
  QObject* job=mode.endsWith("mp4")?retrieveMp4Samples(url,indexes,callback):retrieveMkvSamples(url,indexes,callback);
  if(mode.startsWith("cancel-")){QTimer::singleShot(10,&app,[job]{delete job;});QTimer::singleShot(150,&app,[&]{print({{"cancelled",true}});app.exit(0);});}
  QTimer::singleShot(10000,&app,[&]{app.exit(4);});return app.exec();
 }
 QFile f(argv[2]);if(!f.open(QIODevice::ReadOnly))return 3;auto bytes=f.readAll();QJsonObject out;
 if(mode=="bundled")out=bundledCodecSamples();
 else if(mode=="group")out=groupKeyframeSegments(QJsonDocument::fromJson(bytes).object());
 else {auto r=mode=="mp4"?inspectMp4Samples(bytes,indexes):inspectMkvSamples(bytes,indexes);out={{"tracks",r.tracks},{"error",r.error},{"available",r.available}};}
 print(out);return 0;
}
