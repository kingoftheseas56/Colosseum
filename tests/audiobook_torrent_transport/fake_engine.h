#pragma once
#include "../../native/engine/AudiobookTorrentEngineAdapter.h"
#include <QJsonObject>
#include <QUrlQuery>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <functional>
class FakeEngine final:public IAudiobookTorrentEngine {
public:
 QHash<QString,QString> paths;
 QHash<QString,QJsonArray> manifests,counts;
 QList<QString> adds;
 QList<QVector<int>> priorities;
 QList<bool> deletions;
 struct Removal { QString hash,path;bool erase=false; };
 QHash<QString,Removal> removals;QStringList tokens;bool holdRemovals=false;
 std::function<void()> duringStart,onPriorities,onPayload;
 int starts=0,payloadStarts=0;bool running=true,autoComplete=false;
 QString duplicatePath;
 std::function<void(const QString&,const QString&)> afterAdd;
 bool isRunning() const override {return running;}
 void start() override {++starts;running=true;if(duringStart)duringStart();}
 bool hasTorrent(const QString& h) const override {return paths.contains(h);}
 QString addMagnet(const QString& magnet,const QString& path,bool metadataOnly) override {
  Q_ASSERT(metadataOnly);
  const auto hash=QUrlQuery(QUrl(magnet)).queryItemValue("xt").mid(9);
  adds.append(hash);paths.insert(hash,duplicatePath.isEmpty()?path:duplicatePath);
  if(afterAdd)afterAdd(hash,path);
  return hash;
 }
 QString actualSavePath(const QString& h) const override {return paths.value(h);}
 QJsonArray files(const QString& h) const override {return manifests.value(h);}
 QJsonArray verifiedBytes(const QString& h) const override {return counts.value(h);}
 void setPriorities(const QString&,const QVector<int>& p) override {priorities.append(p);if(onPriorities)onPriorities();}
 void startPayload(const QString& h) override {++payloadStarts;if(onPayload)onPayload();if(autoComplete && paths.contains(h))complete(h);}
 bool requestRemoval(const QString& token,const QString& h,const QString& expected,bool erase) override {
  if(!paths.contains(h) || QFileInfo(paths[h]).canonicalFilePath()!=expected || removals.contains(token))return false;
  removals.insert(token,{h,paths[h],erase});tokens.append(token);deletions.append(erase);paths.remove(h);
  if(!holdRemovals)QTimer::singleShot(0,this,[this,token]{ack(token);});return true;
 }
 void ack(const QString& token,bool success=true,int error=0) {
  if(!removals.contains(token)){emit removalSettled(token,success,error);return;}
  const auto removal=removals.take(token);
  if(success && removal.erase)QDir(removal.path).removeRecursively();
  emit removalSettled(token,success,error);
 }
 void complete(const QString& h) {
  QJsonArray bytes;
  for(const auto& v:manifests.value(h)) {
   const auto o=v.toObject();const int size=o["size"].toInt();
   QString path=QDir(paths.value(h)).filePath(o["name"].toString());QDir().mkpath(QFileInfo(path).absolutePath());
   QFile f(path);if(!f.open(QIODevice::WriteOnly))qFatal("fixture write failed");f.write(QByteArray(size,'X'));f.close();bytes.append(size);
  }
  counts[h]=bytes;emit changed(h);
 }
 static QJsonArray mixed() {return {QJsonObject{{"index",0},{"name","Book/cover.jpg"},{"size",1}},QJsonObject{{"index",1},{"name","Book/Chapter 10.mp3"},{"size",3}},QJsonObject{{"index",2},{"name","Book/Chapter 2.mp3"},{"size",2}}};}
};
