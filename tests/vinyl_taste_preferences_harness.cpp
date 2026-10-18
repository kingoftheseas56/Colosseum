#include "account/ProfilePreferencesStore.h"
#include "SearchHistoryStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <iostream>
#include <cstdlib>
static int checks=0;static void require(bool ok,const char *s){++checks;if(!ok){std::cerr<<"FAIL: "<<s<<'\n';std::exit(1);}}
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir dir;const auto path=dir.filePath("a.ini");
 {ProfilePreferencesStore prefs(path);require(!prefs.vinylTasteAnswered(),"new profile unanswered");require(prefs.setVinylTaste(true,{"artist:a","artist:b"}),"taste committed");require(!prefs.setVinylTaste(true,{"artist:a","artist:a"}),"duplicate rejected");}
 {ProfilePreferencesStore prefs(path);require(prefs.vinylTasteAnswered() && prefs.vinylTasteArtistIds()==QStringList({"artist:a","artist:b"}),"taste cold reload");}
 {ProfilePreferencesStore other(dir.filePath("b.ini"));require(!other.vinylTasteAnswered()&&other.vinylTasteArtistIds().isEmpty(),"second profile isolated");require(other.setVinylTaste(true,{}),"explicit skip persisted");}
 const QString liveDir=dir.filePath("live");require(QDir().mkdir(liveDir),"create retained taste directory");
 {ProfilePreferencesStore prefs(liveDir+"/state.ini");require(prefs.setVinylTaste(true,{"artist:retained"}),"seed retained taste");require(QDir().rename(liveDir,liveDir+"-backup"),"move temporary taste directory");{QFile blocker(liveDir);require(blocker.open(QIODevice::WriteOnly),"block seeded preference path");}
  require(!prefs.setVinylTaste(false,{}),"failed taste reset returned");require(prefs.vinylTasteAnswered()&&prefs.vinylTasteArtistIds()==QStringList{"artist:retained"},"failed reset retains prior facts");require(QFile::remove(liveDir)&&QDir().rename(liveDir+"-backup",liveDir),"restore seeded preference path");QSettings recovered(liveDir+"/state.ini",QSettings::IniFormat);recovered.setValue("test/recovery",true);recovered.sync();}
 {ProfilePreferencesStore prefs(liveDir+"/state.ini");require(prefs.vinylTasteAnswered()&&prefs.vinylTasteArtistIds()==QStringList{"artist:retained"},"failed reset never replays after recovery");}
 const auto blocker=dir.filePath("blocked");{QFile f(blocker);require(f.open(QIODevice::WriteOnly),"blocker created");}
 {ProfilePreferencesStore prefs(blocker+"/state.ini");require(!prefs.setVinylTaste(true,{"artist:failed"}),"failed write returned");require(!prefs.vinylTasteAnswered()&&prefs.vinylTasteArtistIds().isEmpty(),"failed facts not published");require(!prefs.vinylPersistenceError().isEmpty(),"failed write visible");require(QFile::remove(blocker)&&QDir().mkdir(blocker),"parent made writable");QSettings recovered(blocker+"/state.ini",QSettings::IniFormat);recovered.setValue("test/recovery",true);recovered.sync();}
 {ProfilePreferencesStore prefs(blocker+"/state.ini");require(!prefs.vinylTasteAnswered()&&prefs.vinylTasteArtistIds().isEmpty(),"failed write cannot replay on destruction");}
 const auto corruptPath=dir.filePath("corrupt.ini");
 for (const QVariant &invalid : QVariantList{QString("bad"),QVariantMap{{"answered",QString("true")},{"artistIds",QStringList{"artist:a"}}},QVariantMap{{"answered",true},{"artistIds",QStringList{"artist:a","artist:a"}}},QVariantMap{{"answered",false},{"artistIds",QStringList{"artist:a"}}}}) {
  {QSettings settings(corruptPath,QSettings::IniFormat);settings.setValue("vinyl/taste",invalid);settings.sync();}
  ProfilePreferencesStore prefs(corruptPath);require(!prefs.vinylTasteAnswered()&&prefs.vinylTasteArtistIds().isEmpty(),"malformed taste never published");require(!prefs.vinylPersistenceError().isEmpty(),"malformed taste visible");
 }
 {
  ProfilePreferencesStore prefs(path);SearchHistoryStore history(dir.filePath("search-a.ini"));
  history.setRetentionEnabled(prefs.rememberSearchHistory());
  QObject::connect(&prefs,&ProfilePreferencesStore::rememberSearchHistoryChanged,&history,[&]{history.setRetentionEnabled(prefs.rememberSearchHistory());});
  history.record("Vinyl","Synthetic query");require(history.list("Vinyl").size()==1,"Vinyl uses existing search owner");
  prefs.setRememberSearchHistory(false);history.record("Vinyl","Do not retain");require(history.list("Vinyl").size()==1,"profile search optout respected");
 }
 {ProfilePreferencesStore prefs(path);require(!prefs.rememberSearchHistory(),"search preference cold reload");SearchHistoryStore history(dir.filePath("search-a.ini"));require(history.list("Vinyl")==QStringList{"Synthetic query"},"search history cold reload");SearchHistoryStore other(dir.filePath("search-b.ini"));require(other.list("Vinyl").isEmpty(),"search history profile isolation");}
 std::cout<<"PASS "<<checks<<" checks\n";
}
