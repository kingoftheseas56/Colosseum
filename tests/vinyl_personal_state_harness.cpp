#include "CollectionStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include <cstdlib>
#include <limits>
static int checks=0;
static void require(bool ok,const char *s){++checks;if(!ok){std::cerr<<"FAIL: "<<s<<'\n';std::exit(1);}}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv); QTemporaryDir dir; require(dir.isValid(),"temporary directory");
 const QString path=dir.filePath("a.ini"); QString playlist;
 const QVariantMap artist{{"id","vinyl:local-artist:a"},{"name","Synthetic artist"}};
 const QVariantMap first{{"albumId","release:a"},{"editionId","edition:a"},{"trackId","audio:a"},{"discIndex",0},{"trackIndex",0},{"index",0},{"title","First occurrence"}};
 QVariantMap second=first;second["trackIndex"]=1;second["index"]=1;second["title"]="Second occurrence";
 {CollectionStore store(path);
 require(store.setVinylArtistSaved(artist,true),"save canonical artist");
 require(store.has("Vinyl",artist["id"].toString()),"artist membership");
 playlist=store.createVinylPlaylist("Ordered list",artist["id"].toString(),{second,first});
 require(!playlist.isEmpty(),"create ordered playlist");
 require(store.createVinylPlaylist(" ",artist["id"].toString(),{first}).isEmpty(),"reject blank title");
 QVariantMap bad=first;bad["trackIndex"]=-1;
 require(store.createVinylPlaylist("bad",artist["id"].toString(),{bad}).isEmpty(),"reject malformed occurrence");
 for (const QVariant &invalid : QVariantList{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),QString("bad"),0.5}) {
  bad=first;bad["index"]=invalid;require(store.createVinylPlaylist("bad",artist["id"].toString(),{bad}).isEmpty(),"reject nonfinite or malformed index");
 }
 require(store.add("Vinyl",{{"id","album:collision"},{"type","album"}}),"seed album collision");
 require(!store.setVinylArtistSaved({{"id","album:collision"},{"name","bad"}},true),"artist save cannot overwrite album");
 require(!store.setVinylArtistSaved({{"id","album:collision"},{"name","bad"}},false),"artist unsave cannot remove album");
 require(store.has("Vinyl","album:collision"),"album survives invalid artist mutations");
 require(store.add("Biblio",{{"id","book:a"},{"type","book"}}),"other world save");}
 {CollectionStore store(path);auto lists=store.vinylPlaylists(artist["id"].toString());
 require(lists.size()==1,"playlist survives restart");auto refs=lists[0].toMap()["payload"].toMap()["occurrences"].toList();
 require(refs.size()==2 && refs[0].toMap()["trackIndex"].toInt()==1 && refs[1].toMap()["trackIndex"].toInt()==0,"same audio occurrence order retained");
 require(store.setVinylArtistSaved(artist,false),"unsave artist");require(store.has("Vinyl",playlist),"unsave preserves playlist");require(store.has("Biblio","book:a"),"other world preserved");}
 {CollectionStore other(dir.filePath("b.ini"));require(other.items("Vinyl").isEmpty(),"second profile isolated");}
 const QString liveDir=dir.filePath("live");require(QDir().mkdir(liveDir),"create retained-facts directory");
 {CollectionStore store(liveDir+"/state.ini");require(store.setVinylArtistSaved(artist,true),"seed retained artist");
  require(QDir().rename(liveDir,liveDir+"-backup"),"move temporary store directory");{QFile blocker(liveDir);require(blocker.open(QIODevice::WriteOnly),"block seeded store path");}
  require(!store.setVinylArtistSaved(artist,false),"failed unsave returned");require(store.has("Vinyl",artist["id"].toString()),"failed unsave retains prior fact");
  require(QFile::remove(liveDir)&&QDir().rename(liveDir+"-backup",liveDir),"restore seeded store path");
  QSettings recovered(liveDir+"/state.ini",QSettings::IniFormat);recovered.setValue("test/recovery",true);recovered.sync();}
 {CollectionStore store(liveDir+"/state.ini");require(store.has("Vinyl",artist["id"].toString()),"failed unsave never replays after recovery");}
 // Make a parent path a regular file: deterministic failure, even as root.
 const QString blocker=dir.filePath("blocked");{QFile f(blocker);require(f.open(QIODevice::WriteOnly),"create blocker");}
 {CollectionStore store(blocker+"/state.ini");require(!store.setVinylArtistSaved(artist,true),"write failure returned");require(!store.healthy()&&!store.persistenceError().isEmpty(),"write failure health visible");require(store.items("Vinyl").isEmpty(),"failed fact not published");require(QFile::remove(blocker)&&QDir().mkdir(blocker),"restore writable parent before destruction");QSettings recovered(blocker+"/state.ini",QSettings::IniFormat);recovered.setValue("test/recovery",true);recovered.sync();}
 {CollectionStore after(blocker+"/state.ini");require(after.items("Vinyl").isEmpty(),"failed mutation cannot replay at destruction");}
 std::cout<<"PASS "<<checks<<" checks\n";
}
