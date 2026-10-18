#include "vinyl/VinylLocalSource.h"
#include "engine/VaultIdentity.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDataStream>
#include <QProcess>
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
#include <filesystem>

int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()>=5 && (args.at(1)=="--cold-pending" || args.at(1)=="--cold-accepted")) {
        VaultIdentity owner(args.at(2));VinylLocalSource local(&owner);const auto result=local.importFiles({args.at(3)});
        if(args.at(1)=="--cold-pending") {
            const auto prompts=result.value("identityCeremonies").toList();
            if(!result.value("albums").toList().isEmpty() || prompts.size()!=1 || prompts.first().toMap().value("oldId")!=args.at(4)) return 11;
        } else {
            const auto albums=result.value("albums").toList();
            if(!result.value("errors").toStringList().isEmpty() || albums.size()!=1
                || albums.first().toMap().value("tracks").toList().first().toMap().value("id")!=args.at(4)) return 12;
        }
        return 0;
    }
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
    // QFile::link creates a Windows shortcut, not a native filesystem symlink.
    // A shortcut named .wav only tests an ordinary in-root file. Require a real
    // symlink on every platform, then keep the escape rejection unconditional.
    const QString escape = temp.path()+"/escape.wav";
    std::error_code linkError;
    std::filesystem::create_symlink(external.filesystemFileName(), QFile(escape).filesystemFileName(), linkError);
    check(!linkError && QFileInfo(escape).isSymbolicLink(), "native symbolic link fixture");
    reject(QByteArray(supplied).replace("two.wav","escape.wav"),"symlink escape rejected");
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
    // Persisted/legacy/malformed unresolved candidates must never become consent.
    auto writePcm = [&](const QString &name, int frames) {
        QFile output(name); check(output.open(QIODevice::WriteOnly),"write valid cold-identity PCM");
        QDataStream pcm(&output);pcm.setByteOrder(QDataStream::LittleEndian);
        pcm.writeRawData("RIFF",4);pcm << quint32(36+frames*2);pcm.writeRawData("WAVEfmt ",8);
        pcm << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        pcm.writeRawData("data",4);pcm << quint32(frames*2);
        for(int n=0;n<frames;++n) pcm << qint16(n%2 ? 1000 : -1000);
    };
    for (const bool copy : {false,true}) for (const bool share : {false,true})
    for (const QString mode : {QStringLiteral("persisted"),QStringLiteral("legacy"),QStringLiteral("malformed")}) {
        QTemporaryDir cold; const QString registry=cold.path()+"/vault";
        const QString original=cold.path()+"/original.wav", candidate=copy ? cold.path()+"/copy.wav" : original;
        writePcm(original,160);QString oldId,newId,relationship;
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            const auto first=local.importFiles({original}).value("albums").toList();check(first.size()==1,"cold identity original imports");
            oldId=first.first().toMap().value("tracks").toList().first().toMap().value("id").toString();
            if(copy) {
                check(QFile::copy(original,candidate),"copy valid PCM");QFile target(candidate);check(target.open(QIODevice::ReadWrite),"open copied PCM");
                check(target.setFileTime(QFileInfo(original).lastModified(),QFileDevice::FileModificationTime),"preserve copy signature");target.close();
            } else writePcm(candidate,320);
            const auto pending=local.importFiles({candidate});check(pending.value("albums").toList().isEmpty(),"cold identity initially blocks candidate");
            const auto prompts=pending.value("identityCeremonies").toList();check(prompts.size()==1,"cold identity initially offers decision");
            const auto prompt=prompts.first().toMap();newId=prompt.value("newId").toString();relationship=prompt.value("relationship").toString();
        } // Cancel/defer: no decideCeremony call before destruction.
        if(mode!="persisted") {
            QFile store(registry+"/identity.json");check(store.open(QIODevice::ReadOnly),"read cold identity storage");auto json=QJsonDocument::fromJson(store.readAll()).object();store.close();
            if(mode=="legacy") json.remove("pendingCeremonies");
            else json.insert("pendingCeremonies",QJsonArray{QJsonObject{{"type","invalid"},{"newId",newId}}});
            check(store.open(QIODevice::WriteOnly|QIODevice::Truncate),"write legacy or malformed identity storage");store.write(QJsonDocument(json).toJson());store.close();
        }
        if(copy && mode!="persisted") {
            for(const auto &observed : {original,candidate}) {
                VaultIdentity owner(registry);VinylLocalSource local(&owner);
                const auto blocked=local.importFiles({observed});
                check(blocked.value("albums").toList().isEmpty() && blocked.value("identityCeremonies").toList().isEmpty(),"ambiguous legacy copy neither admits nor fabricates decision direction");
                check(blocked.value("errors").toStringList().join(" ").contains("ambiguous copy identities"),"legacy copy block provides actionable explanation");
                check(owner.resolve(oldId)==oldId && owner.resolve(newId)==newId,"legacy copy block preserves both canonical identities");
                check(!owner.decideCeremony(relationship,"use-existing-state"),"legacy copy cannot redirect canonical progress without provenance");
            }
            continue;
        }
        check(QProcess::execute(QCoreApplication::applicationFilePath(),{"--cold-pending",registry,candidate,oldId})==0,"fresh process cancelled candidate reprompts without adoption");
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            const auto pending=local.importFiles({candidate});check(pending.value("albums").toList().isEmpty(),"cold cancelled candidate never silently admitted");
            const auto prompts=pending.value("identityCeremonies").toList();check(prompts.size()==1,"cold cancelled candidate reprompts");
            const auto prompt=prompts.first().toMap();check(prompt.value("oldId")==oldId && prompt.value("newId")==newId,"cold prompt retains exact original and candidate identities");
            relationship=prompt.value("relationship").toString();
        }
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            check(local.importFiles({candidate}).value("albums").toList().isEmpty(),"second cold defer remains blocked");
            const QString choice=copy ? (share ? "use-existing-state" : "separate-copy") : (share ? "same-media" : "new-media");
            check(owner.decideCeremony(relationship,choice),"cold decision accepted by existing identity owner");
        }
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            const auto accepted=local.importFiles({candidate});check(accepted.value("errors").toStringList().isEmpty(),"accepted choice survives cold restart");
            const auto records=accepted.value("albums").toList();check(records.size()==1,"accepted candidate imports after restart");
            const auto acceptedId=records.first().toMap().value("tracks").toList().first().toMap().value("id").toString();
            check(acceptedId==(share ? oldId : newId),"cold shared versus separate canonical identity preserved");
            check(owner.pendingCeremonies().isEmpty(),"accepted cold decision clears pending state");
        }
        check(QProcess::execute(QCoreApplication::applicationFilePath(),{"--cold-accepted",registry,candidate,share ? oldId : newId})==0,"fresh process preserves accepted canonical choice");
    }
    {
        QTemporaryDir changedAgain;const QString registry=changedAgain.path()+"/vault",path=changedAgain.path()+"/again.wav";
        writePcm(path,160);VaultIdentity owner(registry);VinylLocalSource local(&owner);
        const auto original=local.importFiles({path}).value("albums").toList().first().toMap();
        const auto oldId=original.value("tracks").toList().first().toMap().value("id").toString();
        writePcm(path,320);const auto first=local.importFiles({path}).value("identityCeremonies").toList().first().toMap();
        writePcm(path,480);
        check(!local.decideImportIdentity(first.value("relationship").toString(),"same-media"),"stale candidate choice rejected after further replacement");
        const auto retried=local.importFiles({path});check(retried.value("albums").toList().isEmpty(),"further replacement requires fresh decision");
        const auto prompt=retried.value("identityCeremonies").toList().first().toMap();
        check(prompt.value("oldId")==oldId && prompt.value("newId")!=first.value("newId"),"fresh prompt preserves original and updates candidate identity");
        check(local.decideImportIdentity(prompt.value("relationship").toString(),"same-media"),"fresh replacement choice accepted");
        check(local.importFiles({path}).value("albums").toList().first().toMap().value("id")==original.value("id"),"fresh accepted replacement retains old canonical media");
    }
    {
        QTemporaryDir legacy;const QString registry=legacy.path()+"/vault";QStringList paths,oldIds;
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            for(int n=0;n<2;++n) {
                const QString path=legacy.path()+QString("/legacy-%1.wav").arg(n);paths.append(path);writePcm(path,160+n*20);
                oldIds.append(local.importFiles({path}).value("albums").toList().first().toMap().value("tracks").toList().first().toMap().value("id").toString());
                writePcm(path,320+n*20);check(local.importFiles({path}).value("albums").toList().isEmpty(),"multiple legacy candidates initially blocked");
            }
        }
        QFile store(registry+"/identity.json");check(store.open(QIODevice::ReadOnly),"read multiple legacy candidates");auto json=QJsonDocument::fromJson(store.readAll()).object();store.close();json.remove("pendingCeremonies");
        check(store.open(QIODevice::WriteOnly|QIODevice::Truncate),"write multiple legacy candidates");store.write(QJsonDocument(json).toJson());store.close();
        check(QProcess::execute(QCoreApplication::applicationFilePath(),{"--cold-pending",registry,paths[0],oldIds[0]})==0,"first legacy recovery persists its prompt");
        check(QProcess::execute(QCoreApplication::applicationFilePath(),{"--cold-pending",registry,paths[1],oldIds[1]})==0,"second legacy candidate remains blocked across recovery write and restart");
    }
    for(const bool share : {false,true}) {
        QTemporaryDir movedPending;const QString registry=movedPending.path()+"/vault";
        const QString original=movedPending.path()+"/pending.wav",moved=movedPending.path()+"/moved.wav";
        QString oldId,candidateId;
        writePcm(original,160);
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            oldId=local.importFiles({original}).value("albums").toList().first().toMap().value("tracks").toList().first().toMap().value("id").toString();
            writePcm(original,320);const auto prompt=local.importFiles({original}).value("identityCeremonies").toList().first().toMap();candidateId=prompt.value("newId").toString();
            check(QFile::rename(original,moved),"rename deferred candidate");const QFileInfo facts(moved);
            owner.reconcile({{moved,facts.size(),facts.lastModified().toMSecsSinceEpoch()}});
            check(local.importFiles({moved}).value("albums").toList().isEmpty(),"rename alias cannot admit pending candidate");
        }
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            const auto pending=local.importFiles({moved});check(pending.value("albums").toList().isEmpty(),"cold renamed candidate remains blocked");
            const auto prompt=pending.value("identityCeremonies").toList().first().toMap();
            check(prompt.value("oldId")==oldId && prompt.value("newId")==candidateId && prompt.value("newPath")==moved,"renamed pending retains original identities at current path");
            check(local.decideImportIdentity(prompt.value("relationship").toString(),share ? "same-media" : "new-media"),"renamed pending accepts current-file decision");
            const auto accepted=local.importFiles({moved}).value("albums").toList();check(accepted.size()==1,"renamed decided candidate imports");
            check(accepted.first().toMap().value("tracks").toList().first().toMap().value("id")== (share ? oldId : candidateId),"renamed decision keeps one-hop canonical alias semantics");
        }
        check(QProcess::execute(QCoreApplication::applicationFilePath(),{"--cold-accepted",registry,moved,share ? oldId : candidateId})==0,"renamed accepted identity survives fresh process");
    }
    for(const bool ambiguous : {false,true}) for(const bool twoMoves : {false,true}) for(const bool cycle : {false,true}) {
        QTemporaryDir legacyMove;const QString registry=legacyMove.path()+"/vault";
        const QString path=legacyMove.path()+"/prior.wav",intermediate=legacyMove.path()+"/intermediate.wav",moved=legacyMove.path()+"/current.wav";QString oldId,candidateId;
        writePcm(path,160);
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);
            oldId=local.importFiles({path}).value("albums").toList().first().toMap().value("tracks").toList().first().toMap().value("id").toString();
            if(ambiguous) {writePcm(path,320);candidateId=local.importFiles({path}).value("identityCeremonies").toList().first().toMap().value("newId").toString();}
            if(twoMoves) {
                check(QFile::rename(path,intermediate),"first legacy admission rename");
                const QFileInfo facts(intermediate);owner.reconcile({{intermediate,facts.size(),facts.lastModified().toMSecsSinceEpoch()}});
            }
            check(QFile::rename(twoMoves ? intermediate : path,moved),"rename legacy admission fixture");const QFileInfo facts(moved);owner.reconcile({{moved,facts.size(),facts.lastModified().toMSecsSinceEpoch()}});
        }
        QFile file(registry+"/identity.json");check(file.open(QIODevice::ReadOnly),"read legacy rename snapshot");auto json=QJsonDocument::fromJson(file.readAll()).object();file.close();json.remove("pendingCeremonies");
        if(cycle) {
            auto aliases=json.value("pathAliases").toArray();
            aliases.append(QJsonObject{{"oldPath",moved},{"newPath",path}});
            json.insert("pathAliases",aliases);
        }
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"write legacy rename snapshot");file.write(QJsonDocument(json).toJson());file.close();
        VaultIdentity owner(registry);VinylLocalSource local(&owner);const auto result=local.importFiles({moved});
        if(ambiguous) {
            check(result.value("albums").toList().isEmpty() && result.value("errors").toStringList().join(" ").contains("ambiguous moved-file identities"),"legacy alias cannot silently admit unresolved renamed candidate");
            check(owner.resolve(oldId)==oldId && owner.resolve(candidateId)==candidateId,"blocked legacy alias preserves canonical entries");
        } else {
            check(result.value("errors").toStringList().isEmpty() && result.value("albums").toList().size()==1,"ordinary legacy unique rename remains playable");
            check(result.value("albums").toList().first().toMap().value("tracks").toList().first().toMap().value("id")==oldId,"ordinary legacy rename retains canonical identity");
        }
    }
    {
        QTemporaryDir approvedMove;const QString registry=approvedMove.path()+"/vault";
        const QString original=approvedMove.path()+"/original.wav", path=approvedMove.path()+"/copy.wav", middle=approvedMove.path()+"/middle.wav", moved=approvedMove.path()+"/moved.wav";
        QString copyId;
        writePcm(original,160);check(QFile::copy(original,path),"copy approved legacy fixture");
        QFile copied(path);check(copied.open(QIODevice::ReadWrite),"open copy to preserve signature");
        check(copied.setFileTime(QFileInfo(original).lastModified(),QFileDevice::FileModificationTime),"preserve copy signature");copied.close();
        {
            VaultIdentity owner(registry);VinylLocalSource local(&owner);local.importFiles({original});
            const auto prompt=local.importFiles({path}).value("identityCeremonies").toList().first().toMap();copyId=prompt.value("newId").toString();
            check(local.decideImportIdentity(prompt.value("relationship").toString(),"separate-copy"),"approve separate copy before renames");
            check(QFile::rename(path,middle),"first approved copy rename");
            check(QFile::rename(middle,moved),"second approved copy rename");
        }
        QFile file(registry+"/identity.json");check(file.open(QIODevice::ReadOnly),"read approved legacy snapshot");auto json=QJsonDocument::fromJson(file.readAll()).object();file.close();json.remove("pendingCeremonies");
        // Persisted legacy provenance fixture: two rename aliases for the
        // already approved distinct copy, with its original still present.
        auto entries=json.value("entries").toArray();
        for(qsizetype i=0;i<entries.size();++i) {auto entry=entries.at(i).toObject();if(entry.value("id").toString()==copyId) {entry.insert("path",moved);entries[i]=entry;}}
        json.insert("entries",entries);
        const QFileInfo movedFacts(moved);
        json.insert("aliases",QJsonArray{QJsonObject{{"computed",VaultIdentity::computeId(moved,movedFacts.size(),movedFacts.lastModified().toMSecsSinceEpoch())},{"canonical",copyId}}});
        json.insert("pathAliases",QJsonArray{QJsonObject{{"oldPath",path},{"newPath",middle}},QJsonObject{{"oldPath",middle},{"newPath",moved}}});
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"write approved legacy snapshot");file.write(QJsonDocument(json).toJson());file.close();
        VaultIdentity owner(registry);VinylLocalSource local(&owner);const auto result=local.importFiles({moved});
        check(result.value("errors").toStringList().isEmpty() && result.value("albums").toList().size()==1,"approved legacy copy remains admitted after two renames");
        check(result.value("albums").toList().first().toMap().value("tracks").toList().first().toMap().value("id")==copyId,"approved legacy copy preserves distinct canonical identity");
    }
    qInfo()<<"PASS vinyl_local_source_harness"<<checks<<"checks"; return 0;
}
