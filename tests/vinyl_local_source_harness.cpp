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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <memory>

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
    // A changed file must expose the shared owner's decision, never silently import.
    check(f.open(QIODevice::WriteOnly),"replace same-path audio"); f.write("RIFF changed fixture with different size"); f.close();
    const auto changedImport=source.importFiles({path});
    check(changedImport.value("albums").toList().isEmpty(),"changed audio waits for decision");
    check(changedImport.value("identityCeremonies").toList().size()==1,"changed audio exposes actionable shared identity ceremony");
    auto ceremony=changedImport.value("identityCeremonies").toList().first().toMap();
    check(ceremony.value("oldId")==id && ceremony.value("newId")!=id,"shared ceremony preserves old and candidate IDs");
    check(source.importFiles({path}).value("identityCeremonies").toList().size()==1,"deferred ceremony is offered again without bypass");
    check(!source.decideImportIdentity(ceremony.value("relationship").toString(),"cancel"),"cancel is not an identity decision");
    check(source.importFiles({path}).value("albums").toList().isEmpty(),"cancel leaves changed input blocked");
    check(source.decideImportIdentity(ceremony.value("relationship").toString(),"same-media"),"same media accepted by shared owner");
    auto sameMedia=source.importFiles({path});
    check(sameMedia.value("errors").toStringList().isEmpty() && sameMedia.value("albums").toList().first().toMap().value("id")==album.value("id"),"same media reimport retains album and progress identity");
    source.request("play","resolve",id); QCoreApplication::processEvents();check(error.isEmpty(),"same media repairs old queued track resolution");
    { VaultIdentity restored(registry); VinylLocalSource second(&restored);
      check(second.importFiles({path}).value("albums").toList().first().toMap().value("id")==album.value("id"),"same-media choice survives restart"); }
    const QString newMediaPath=temp.path()+"/different.wav"; QFile different(newMediaPath);
    check(different.open(QIODevice::WriteOnly),"write new-media fixture");different.write("RIFF initial different");different.close();
    auto priorNew=source.importFiles({newMediaPath}).value("albums").toList().first().toMap();
    const auto priorId=priorNew.value("tracks").toList().first().toMap().value("id").toString();
    source.setAlbums({album,priorNew});
    check(different.open(QIODevice::WriteOnly),"replace new-media fixture");different.write("RIFF replacement with distinct bytes and size");different.close();
    auto separate=source.importFiles({newMediaPath}); auto separateChoice=separate.value("identityCeremonies").toList().first().toMap();
    check(source.decideImportIdentity(separateChoice.value("relationship").toString(),"new-media"),"new media accepted by shared owner");
    auto separateAlbum=source.importFiles({newMediaPath}).value("albums").toList().first().toMap();
    const auto separateId=separateAlbum.value("tracks").toList().first().toMap().value("id").toString();
    check(separateId!=priorId && identity.resolve(priorId)!=identity.resolve(separateId),"new media never aliases old progress identity");
    source.setAlbums({album,priorNew,separateAlbum});
    source.request("play","resolve",priorId);QCoreApplication::processEvents();check(error.contains("changed"),"new-media choice cannot retarget old queue");
    source.request("play","resolve",separateId);QCoreApplication::processEvents();check(error.isEmpty(),"new-media identity resolves replacement");
    const QFileInfo original(path); const auto size=original.size(); const auto mtime=original.lastModified().toMSecsSinceEpoch();
    const QString moved=temp.path()+"/renamed.wav"; check(QFile::rename(path,moved),"rename input");
    identity.reconcile({{moved,size,mtime}});
    source.request("play","resolve",id); QCoreApplication::processEvents();
    check(error.isEmpty() && QUrl(data.toMap().value("url").toString()).toLocalFile()==moved,"shared owner rename reconciliation followed");
    check(QFile::remove(moved),"remove input"); source.request("play","resolve",id); QCoreApplication::processEvents();
    check(error.contains("no longer exists"),"missing after import errors truthfully");
    requested=source.request("browse","search",""); emit source.profileLeaving(); QCoreApplication::processEvents(); check(lastId!=requested,"profile cancels pending callbacks");
    source.request("browse","search",""); QCoreApplication::processEvents(); check(data.toList().isEmpty(),"profile clears projection");
    // Explicit supplied metadata exercises the production parser, not production fixtures.
    auto write = [&](const QString &name, const QByteArray &bytes) {
        QFile output(temp.path()+"/"+name); check(output.open(QIODevice::WriteOnly),"write catalogue fixture"); output.write(bytes); output.close();
    };
    write("one.wav","RIFF one"); write("two.wav","RIFF second"); write("bonus.wav","RIFF bonus"); write("cover.png",QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jG9kAAAAASUVORK5CYII="));
    const QString manifest=temp.path()+"/collection.vinyl.json";
    QByteArray supplied=R"({"version":1,"artist":{"key":"performer","name":"Local Ensemble","biography":"Supplied biography","backdrops":["cover.png"]},"releases":[{"key":"main","title":"Two Discs","kind":"album","year":"2026","genre":"Instrumental","cover":"cover.png","credits":[{"role":"Producer","name":"A Person"}],"editions":[{"key":"standard","title":"Original","tracks":[{"file":"two.wav","title":"Second disc","disc":10,"number":1},{"file":"one.wav","title":"First disc","disc":2,"number":1,"seconds":12,"lyrics":[{"text":"Supplied line","at":0}],"notes":{"0":"Supplied note"}}]},{"key":"extended","title":"Extended","tracks":[{"file":"bonus.wav","title":"Bonus","disc":1,"number":1}]}]},{"key":"single","title":"Separate single","kind":"single","editions":[{"key":"standard","tracks":[{"file":"one.wav","title":"First disc"}]}]}]})";
    auto occurrenceDocument=QJsonDocument::fromJson(supplied).object();
    auto occurrenceReleases=occurrenceDocument.value("releases").toArray();
    auto occurrenceRelease=occurrenceReleases[0].toObject();
    auto occurrenceEditions=occurrenceRelease.value("editions").toArray();
    auto occurrenceEdition=occurrenceEditions[0].toObject();
    auto occurrenceTracks=occurrenceEdition.value("tracks").toArray();
    occurrenceTracks.append(QJsonObject{{"file","one.wav"},{"title","Reprise of first recording"},{"disc",2},{"number",2},{"notes","Distinct reprise liner note"}});
    occurrenceEdition.insert("tracks",occurrenceTracks); occurrenceEditions[0]=occurrenceEdition;
    occurrenceRelease.insert("editions",occurrenceEditions); occurrenceReleases[0]=occurrenceRelease;
    occurrenceDocument.insert("releases",occurrenceReleases); supplied=QJsonDocument(occurrenceDocument).toJson(QJsonDocument::Compact);
    write("collection.vinyl.json",supplied);
    auto catalogue=source.importFiles({manifest});
    if (!catalogue.value("errors").toStringList().isEmpty()) qWarning()<<catalogue.value("errors");
    check(catalogue.value("errors").toStringList().isEmpty() && catalogue.value("albums").toList().size()==2,"supplied catalogue imports multiple releases");
    auto records=catalogue.value("albums").toList(); auto main=records.first().toMap();
    check(main.value("artistIds")==records.last().toMap().value("artistIds"),"one stable artist owns multiple releases");
    check(main.value("editions").toList().size()==2,"editions preserved");
    auto ordered=main.value("tracks").toList();
    check(ordered.first().toMap().value("disc").toInt()==2 && ordered.last().toMap().value("disc").toInt()==10,"numeric disc ordering");
    check(ordered.first().toMap().value("lyrics").toList().first().toMap().value("text")=="Supplied line","supplied lyrics retained");
    check(ordered.first().toMap().value("notes").toMap().value("0")=="Supplied note","supplied notes retained");
    check(!ordered.last().toMap().contains("lyrics"),"missing lyrics remain absent");
    check(ordered[0].toMap().value("id")==ordered[1].toMap().value("id")
        && ordered[0].toMap().value("title")=="First disc"
        && ordered[1].toMap().value("title")=="Reprise of first recording"
        && ordered[1].toMap().value("notes")=="Distinct reprise liner note",
        "same canonical recording retains distinct occurrence titles and notes");
    source.setAlbums(records);
    source.request("browse","album",main.value("id").toString()); QCoreApplication::processEvents();
    const auto occurrenceRead=data.toMap().value("tracks").toList();
    check(error.isEmpty() && occurrenceRead[0].toMap().value("notes").toMap().value("0")=="Supplied note"
        && occurrenceRead[1].toMap().value("notes")=="Distinct reprise liner note",
        "album read preserves exact contextual metadata for duplicate recording occurrences");
    for (const QString operation : {QStringLiteral("song"),QStringLiteral("lyrics"),QStringLiteral("notes"),QStringLiteral("credits")})
        check(source.request("browse",operation,ordered[0].toMap().value("id").toString())==-1,"ambiguous file-ID metadata operation explicitly rejected");
    source.request("browse","artist",main.value("artistIds").toStringList().first()); QCoreApplication::processEvents();
    check(error.isEmpty() && data.toMap().value("albums").toList().size()==1 && data.toMap().value("singles").toList().size()==1,"artist projection groups release kinds");
    auto bonus=main.value("editions").toList().last().toMap().value("tracks").toList().first().toMap();
    source.request("play","resolve",bonus.value("id").toString()); QCoreApplication::processEvents();
    check(error.isEmpty() && data.toMap().value("url").toString().endsWith("bonus.wav"),"nondefault edition track resolves");
    const int browse=source.request("browse","album",main.value("id").toString());
    source.request("queue","album",main.value("id").toString()); QCoreApplication::processEvents();
    check(lastId!=browse && error.isEmpty(),"queue request accepted independently");
    write("one.wav","RIFF replacement first recording"); write("two.wav","RIFF replacement second recording with another size");
    auto pendingManifest=source.importFiles({manifest});
    check(pendingManifest.value("albums").toList().isEmpty() && pendingManifest.value("replacedManifestPaths").toStringList().isEmpty(),"manifest identity decisions preserve existing Collection atomically");
    check(!pendingManifest.value("identityCeremonies").toList().isEmpty(),"manifest returns shared identity ceremony");
    int manifestDecisions=0;
    while(!pendingManifest.value("identityCeremonies").toList().isEmpty() && manifestDecisions<3) {
        const auto prompt=pendingManifest.value("identityCeremonies").toList().first().toMap();
        check(source.decideImportIdentity(prompt.value("relationship").toString(),"same-media"),"manifest shared decision accepted");
        ++manifestDecisions;pendingManifest=source.importFiles({manifest});
    }
    check(manifestDecisions==2 && pendingManifest.value("errors").toStringList().isEmpty() && pendingManifest.value("albums").toList().size()==2,"manifest retries finish after each bounded file decision");
    supplied.replace("Two Discs","Retitled Discs"); write("collection.vinyl.json",supplied);
    auto refreshed=source.importFiles({manifest});
    check(refreshed.value("errors").toStringList().isEmpty() && refreshed.value("albums").toList().first().toMap().value("id")==main.value("id"),"metadata refresh preserves canonical release identity");
    check(refreshed.value("replacedManifestPaths").toStringList()==QStringList{manifest},"refresh declares replaced source");
    auto reject = [&](QByteArray malformed, const char *name) {
        write("invalid.vinyl.json",malformed); auto rejected=source.importFiles({temp.path()+"/invalid.vinyl.json"});
        check(!rejected.value("errors").toStringList().isEmpty() && rejected.value("albums").toList().isEmpty() && rejected.value("replacedManifestPaths").toStringList().isEmpty(),name);
    };
    reject("{", "malformed JSON rejected");
    source.request("browse","album",main.value("id").toString()); QCoreApplication::processEvents();
    check(error.isEmpty() && data.toMap().value("title")==main.value("title"),"rejected manifest leaves existing projection unchanged");
    reject(QByteArray(supplied).replace("two.wav","../two.wav"),"traversal rejected atomically");
    reject(QByteArray(supplied).replace("two.wav","https://example.com/two.wav"),"remote audio rejected");
    reject(QByteArray(supplied).replace("\"disc\":10","\"disc\":2"),"duplicate disc position rejected");
    reject(QByteArray(supplied).replace("\"disc\":10","\"disc\":1.5"),"fractional disc rejected");
    reject(QByteArray(supplied).replace("\"key\":\"single\"","\"key\":\"main\""),"duplicate release key rejected");
    auto overLimit=QJsonDocument::fromJson(supplied).object();
    auto manyReleases=overLimit.value("releases").toArray(); auto manyRelease=manyReleases.first().toObject();
    auto manyEditions=manyRelease.value("editions").toArray(); auto manyEdition=manyEditions.first().toObject();
    QJsonArray manyTracks;
    for (int n=1;n<=1001;++n) manyTracks.append(QJsonObject{{"file","one.wav"},{"title","Repeated recording"},{"number",n}});
    manyEdition.insert("tracks",manyTracks); manyEditions[0]=manyEdition; manyRelease.insert("editions",manyEditions);
    manyReleases[0]=manyRelease; overLimit.insert("releases",manyReleases);
    reject(QJsonDocument(overLimit).toJson(QJsonDocument::Compact),"track count bound rejected atomically");
    reject(QByteArray(2*1024*1024+1,' '),"oversized manifest rejected");
    QTemporaryDir outside; QFile external(outside.path()+"/outside.wav"); check(external.open(QIODevice::WriteOnly),"write outside fixture"); external.write("RIFF outside"); external.close();
    if (QFile::link(external.fileName(),temp.path()+"/escape.wav")) reject(QByteArray(supplied).replace("two.wav","escape.wav"),"symlink escape rejected");
    write("collection.vinyl.json",R"({"version":1,"artist":{"name":"Local Ensemble"},"releases":[]})");
    auto empty=source.importFiles({manifest}); check(!empty.value("errors").toStringList().isEmpty() && empty.value("albums").toList().isEmpty() && empty.value("replacedManifestPaths").toStringList().isEmpty(),"empty manifest rejected without losing source identity");
    QTemporaryDir batchStore, individualStore;
    VaultIdentity batchIdentity(batchStore.path()), individualIdentity(individualStore.path());
    const QList<VaultIdentity::FileFacts> batchFacts{{temp.path()+"/one.wav",111,10000},
        {temp.path()+"/two.wav",222,10000},{temp.path()+"/bonus.wav",111,10000}};
    QList<QVariantMap> individualResults;
    for (const auto &fact : batchFacts) individualResults.append(individualIdentity.observeFile(fact.path,fact.size,fact.mtimeMs));
    int batchChanges=0;
    QObject::connect(&batchIdentity,&VaultIdentity::changed,&app,[&]{++batchChanges;});
    const auto batchResults=batchIdentity.observeFiles(batchFacts);
    check(batchResults==individualResults,"batch observes exactly the single-file identity outcomes including copy ambiguity");
    check(batchChanges==1,"batch emits one persisted change notification");
    check(batchResults.last().value("prompt").toBool(),"batch never approves identity ceremony");
    { VaultIdentity reloaded(batchStore.path());
      check(reloaded.knows(batchResults.first().value("id").toString()) && reloaded.knows(batchResults.last().value("id").toString()),"batched registry persistence reloads all identities"); }
    batchIdentity.observeFiles({}); check(batchChanges==1,"empty batch does not persist or notify");
    const QList<VaultIdentity::FileFacts> changedFacts{{temp.path()+"/one.wav",333,90000}};
    check(batchIdentity.observeFiles(changedFacts).first()==individualIdentity.observeFile(changedFacts.first().path,333,90000),"batch preserves changed-content ceremony semantics");
    const int importArgument=app.arguments().indexOf("--import");
    if (importArgument>=0 && importArgument+1<app.arguments().size()) {
        QTemporaryDir smoke; VaultIdentity smokeIdentity(smoke.path()); VinylLocalSource smokeSource(&smokeIdentity);
        const auto smokeResult=smokeSource.importFiles({app.arguments().at(importArgument+1)});
        qInfo()<<"SMOKE imported releases"<<smokeResult.value("albums").toList().size()<<"errors"<<smokeResult.value("errors");
        check(smokeResult.value("errors").toStringList().isEmpty(),"supplied runtime catalogue imports");
    }
    // Paged local read contract over the same request/completed consumer API.
    QVariantList pageRecords;
    for(int i=0;i<5;++i) {
        auto record=album;record.insert("id",QStringLiteral("page-%1").arg(i));
        record.insert("title",QStringLiteral("Page album %1").arg(i));
        record.insert("tracks",QVariantList{QVariantMap{{"id",QStringLiteral("recording-%1").arg(i)},
            {"title",QStringLiteral("Needle song")},{"localPath",path}}});
        pageRecords.append(record);
    }
    source.setAlbums(pageRecords);
    check(source.cataloguePageVersion()==1,"explicit paged read capability");
    auto pageRead=[&](const QVariantMap &options, const QString &query=QString()) {
        source.request("browse","search",query,options);QCoreApplication::processEvents();return data.toMap();
    };
    auto first=pageRead({{"pageSize",2},{"offset",0}},"needle song");
    check(error.isEmpty() && first.value("items").toList().size()==2 && first.value("nextOffset").toInt()==2,"first bounded page includes track title search");
    const auto revision=first.value("revision").toString();
    source.setAlbums(pageRecords); // Identical reprojection must not invalidate continuation.
    auto second=pageRead({{"pageSize",2},{"offset",2},{"revision",revision}},"needle song");
    check(error.isEmpty() && second.value("revision")==revision && second.value("nextOffset").toInt()==4,"stable projection revision");
    auto last=pageRead({{"pageSize",2},{"offset",4},{"revision",revision}},"needle song");
    check(error.isEmpty() && last.value("items").toList().size()==1 && last.value("complete").toBool()
        && last.value("nextOffset").toInt()==-1,"last page terminal cursor");
    auto emptyPage=pageRead({{"pageSize",2},{"offset",0}},"no such album");
    check(error.isEmpty() && emptyPage.value("items").toList().isEmpty() && emptyPage.value("complete").toBool(),"empty search terminal page");
    pageRead({{"pageSize",201},{"offset",0}});check(!error.isEmpty(),"oversized page rejected");
    pageRead({{"pageSize",2},{"offset",1.5}});check(!error.isEmpty(),"fractional cursor rejected");
    pageRead({{"pageSize",2},{"offset",2}});check(!error.isEmpty(),"continuation needs revision");
    pageRead({{"pageSize",2},{"offset",99},{"revision",revision}});check(!error.isEmpty(),"cursor beyond result rejected");
    auto changed=pageRecords[0].toMap();changed.insert("title","Updated");pageRecords[0]=changed;source.setAlbums(pageRecords);
    pageRead({{"pageSize",2},{"offset",2},{"revision",revision}});check(error.contains("changed"),"changed snapshot rejects continuation");
    auto beforeCancelled=completions;
    source.request("browse","search","",{{"pageSize",2},{"offset",0}});source.cancel("browse");QCoreApplication::processEvents();
    check(completions==beforeCancelled,"paged cancellation suppresses completion");
    source.request("browse","search","",{{"pageSize",2},{"offset",0}});emit source.profileLeaving();QCoreApplication::processEvents();
    check(completions==beforeCancelled,"profile departure cancels pending page");
    source.setAlbums(pageRecords);pageRead({{"pageSize",2},{"offset",2},{"revision",revision}});
    check(error.contains("changed"),"profile round trip cannot revive old cursor");

    // Real Qt timer seam; production construction remains zero-delay.
    auto pump=[](int ms) {QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    int delayedCompletions=0;
    VinylLocalSource delayed(&identity,nullptr,25);
    QObject::connect(&delayed,&VinylLocalSource::completed,&app,[&](const QString &,int,const QVariant &,const QString &) {++delayedCompletions;});
    delayed.request("play","resolve","missing");QCoreApplication::processEvents();
    check(delayedCompletions==0,"controlled resolver remains pending before timer");
    delayed.cancel("play");pump(60);check(delayedCompletions==0,"delayed resolver cancellation suppresses callback");
    delayed.request("play","resolve","missing");emit delayed.profileLeaving();pump(60);
    check(delayedCompletions==0,"profile departure fences actual delayed timer");
    delayed.request("browse","search","");QCoreApplication::processEvents();
    check(delayedCompletions==1,"resolution delay does not delay catalogue reads");
    delayed.request("play","resolve","missing");pump(60);check(delayedCompletions==2,"controlled delayed resolver eventually completes");
    int destroyedCompletions=0;
    auto destroyed=std::make_unique<VinylLocalSource>(&identity,nullptr,25);
    QObject::connect(destroyed.get(),&VinylLocalSource::completed,&app,[&](const QString &,int,const QVariant &,const QString &) {++destroyedCompletions;});
    destroyed->request("play","resolve","missing");destroyed.reset();pump(60);
    check(destroyedCompletions==0,"source destruction cancels timer context");
    int negativeCompletions=0;
    VinylLocalSource negative(&identity,nullptr,-1);
    QObject::connect(&negative,&VinylLocalSource::completed,&app,[&](const QString &,int,const QVariant &,const QString &) {++negativeCompletions;});
    negative.request("play","resolve","missing");QCoreApplication::processEvents();
    check(negativeCompletions==1,"negative resolution delay clamps to zero");
    int cappedCompletions=0;
    VinylLocalSource capped(&identity,nullptr,10000);
    QObject::connect(&capped,&VinylLocalSource::completed,&app,[&](const QString &,int,const QVariant &,const QString &) {++cappedCompletions;});
    capped.request("play","resolve","missing");pump(60);check(cappedCompletions==0,"positive capped delay remains pending");
    pump(2200);check(cappedCompletions==1,"resolution delay above maximum clamps to two seconds");

    if (app.arguments().contains("--benchmark")) {
        QTemporaryDir large; QDir().mkpath(large.path()+"/registry");
        VaultIdentity largeIdentity(large.path()+"/registry"); VinylLocalSource largeSource(&largeIdentity);
        QJsonArray largeTracks;
        const int benchmarkCount = qEnvironmentVariableIntValue("VINYL_BENCHMARK_TRACKS") > 0 ? qEnvironmentVariableIntValue("VINYL_BENCHMARK_TRACKS") : 1000;
        for (int i=0;i<benchmarkCount;++i) {
            const QString name=QString::number(i)+".wav"; QFile audio(large.path()+"/"+name);
            check(audio.open(QIODevice::WriteOnly),"benchmark fixture"); audio.write(QByteArray(64+i,'a')); audio.close();
            largeTracks.append(QJsonObject{{"file",name},{"title",name},{"number",i+1}});
        }
        QJsonObject largeManifest{{"version",1},{"artist",QJsonObject{{"name","Benchmark"}}},{"title","Large catalogue"},
            {"editions",QJsonArray{QJsonObject{{"tracks",largeTracks}}}}};
        QFile largeFile(large.path()+"/large.vinyl.json"); check(largeFile.open(QIODevice::WriteOnly),"benchmark manifest");
        largeFile.write(QJsonDocument(largeManifest).toJson(QJsonDocument::Compact)); largeFile.close();
        QElapsedTimer timer; timer.start(); const auto largeResult=largeSource.importFiles({largeFile.fileName()});
        qInfo()<<"BENCHMARK"<<benchmarkCount<<"fresh tracks import milliseconds"<<timer.elapsed()<<"errors"<<largeResult.value("errors");
        check(largeResult.value("errors").toStringList().isEmpty(),"large catalogue import");
    }
    qInfo()<<"PASS vinyl_local_source_harness"<<checks<<"checks"; return 0;
}
