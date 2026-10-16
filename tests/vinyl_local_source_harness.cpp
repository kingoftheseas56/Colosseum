#include "vinyl/VinylLocalSource.h"
#include "engine/VaultIdentity.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QDebug>

int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    int checks=0;
    auto check=[&](bool ok,const char *name) { ++checks; if (!ok) { qCritical()<<"FAIL"<<name; ::exit(1); } };
    check(temp.isValid(),"temporary storage");
    const QString registry=temp.path()+"/identity"; QDir().mkpath(registry);
    const QString path=temp.path()+QString::fromUtf8("/Local song ü.wav");
    QFile f(path); check(f.open(QIODevice::WriteOnly),"create local input"); f.write("RIFFtestfixture"); f.close();
    VaultIdentity identity(registry);
    VinylLocalSource source(&identity);
    const auto imported=source.importFiles({QUrl::fromLocalFile(path).toString(),path});
    const auto albums=imported.value("albums").toList();
    check(albums.size()==1 && imported.value("errors").toStringList().isEmpty(),"deduplicate path and encoded URL");
    const auto album=albums.first().toMap(); const auto track=album.value("tracks").toList().first().toMap();
    const auto id=track.value("id").toString();
    check(id.startsWith("vault:"),"existing canonical file identity");
    check(track.value("externalIds").toMap().isEmpty(),"no fabricated external identifiers");
    check(source.importFiles({path}).value("albums").toList().first().toMap().value("id")==album.value("id"),"repeat import stable album identity");
    source.setAlbums({QVariantMap{{"payload",QVariantMap{{"album",album}}}}});
    int completions=0,lastId=-1; QVariant data; QString error;
    QObject::connect(&source,&VinylLocalSource::completed,&app,[&](const QString &,int requestId,const QVariant &d,const QString &e) {++completions;lastId=requestId;data=d;error=e;});
    int requested=source.request("play","resolve",id);
    check(completions==0,"completion always asynchronous"); QCoreApplication::processEvents();
    check(completions==1 && lastId==requested && error.isEmpty(),"resolve completion");
    check(QUrl(data.toMap().value("url").toString()).toLocalFile()==path,"encoded source round trip");
    source.request("browse","search","no match"); requested=source.request("browse","search","Local");
    QCoreApplication::processEvents(); check(completions==2 && lastId==requested && data.toList().size()==1,"newest browse request wins");
    source.request("play","resolve",id); source.cancel("play"); QCoreApplication::processEvents(); check(completions==2,"cancellation suppresses completion");
    source.request("play","resolve","missing-id"); QCoreApplication::processEvents(); check(!error.isEmpty(),"unknown track errors");
    check(source.importFiles({"https://example.com/audio.wav",temp.path(),temp.path()+"/absent.wav"}).value("errors").toStringList().size()==3,"reject remote directory and absent input");
    { VaultIdentity restored(registry); VinylLocalSource second(&restored);
      check(second.importFiles({path}).value("albums").toList().first().toMap().value("id")==album.value("id"),"identity survives registry restart"); }
    const QFileInfo original(path); const auto size=original.size(); const auto mtime=original.lastModified().toMSecsSinceEpoch();
    const QString moved=temp.path()+"/renamed.wav"; check(QFile::rename(path,moved),"rename input");
    identity.reconcile({{moved,size,mtime}});
    source.request("play","resolve",id); QCoreApplication::processEvents();
    check(error.isEmpty() && QUrl(data.toMap().value("url").toString()).toLocalFile()==moved,"shared owner rename reconciliation followed");
    check(QFile::remove(moved),"remove input"); source.request("play","resolve",id); QCoreApplication::processEvents();
    check(error.contains("no longer exists"),"missing after import errors truthfully");
    requested=source.request("browse","search",""); emit source.profileLeaving(); QCoreApplication::processEvents(); check(lastId!=requested,"profile cancels pending callbacks");
    source.request("browse","search",""); QCoreApplication::processEvents(); check(data.toList().isEmpty(),"profile clears projection");
    qInfo()<<"PASS vinyl_local_source_harness"<<checks<<"checks"; return 0;
}
