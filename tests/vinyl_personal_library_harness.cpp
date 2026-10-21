#include <QtTest>
#include <QTemporaryDir>
#include "CollectionStore.h"
#include "vinyl/VinylBackendSession.h"

class VinylPersonalLibraryContracts : public QObject {
    Q_OBJECT
    QVariantMap command(VinylBackendSession &backend,const QString &op,const QVariantMap &args={}) {
        QVariantMap result;
        const bool invoked=QMetaObject::invokeMethod(&backend,"mutatePersonal",Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap,result),Q_ARG(QString,op),Q_ARG(QVariantMap,args));
        if(!invoked) return {{"ok",false},{"error","Native personal command is not available."}};
        return result;
    }
    QVariantMap row(QString title="First",QString track="track-one") {
        const QVariantMap song{{"id",track},{"title",title},{"artists",QStringList{"Artist"}},{"seconds",30}};
        const QVariantMap album{{"id","album-one"},{"title","Album"},{"origin","local"},{"artists",QStringList{"Artist"}},{"tracks",QVariantList{song}}};
        const QVariantMap edition{{"id","album-one"},{"albumId","album-one"},{"name","Album"},{"artist","Artist"},{"sourceAlbum",album},{"discs",QVariantList{QVariant::fromValue(QVariantList{song})}}};
        return {{"albumId","album-one"},{"editionId","album-one"},{"trackId",track},{"discIndex",0},{"trackIndex",0},{"track",song},{"edition",edition},{"title",title}};
    }
    QVariantMap find(CollectionStore &store,const QString &id) {
        for(const auto &v:store.items("Vinyl"))if(v.toMap().value("id")==id)return v.toMap();
        return {};
    }
    void bind(VinylBackendSession &backend,CollectionStore &store) {
        backend.setProperty("collectionStore",QVariant::fromValue<QObject*>(&store));backend.setProfileScope("local-test");
    }
private slots:
    void savesSongsWithoutSavingAlbumMembership() {
        QTemporaryDir dir;CollectionStore store(dir.filePath("collection.ini"));VinylBackendSession backend;bind(backend,store);
        auto result=command(backend,"saveSong",{{"row",row()},{"saved",true}});
        QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
        const auto id=result.value("id").toString();QVERIFY(id.startsWith("vinyl:song:"));
        QCOMPARE(find(store,id).value("type").toString(),QString("song"));
        QVERIFY(!find(store,"album-one").value("payload").toMap().value("saved").toBool());
        QVERIFY(command(backend,"saveSong",{{"row",row()},{"saved",true}}).value("ok").toBool());
        QCOMPARE(store.items("Vinyl").size(),2);
        QVERIFY(command(backend,"saveSong",{{"row",row()},{"saved",false}}).value("ok").toBool());
        QVERIFY(find(store,id).isEmpty());QVERIFY(!find(store,"album-one").isEmpty());
    }
    void playlistsRetainDuplicateItemIdentityAndPersist() {
        QTemporaryDir dir;const auto path=dir.filePath("collection.ini");QString id;
        {CollectionStore store(path);VinylBackendSession backend;bind(backend,store);
            auto result=command(backend,"createPlaylist",{{"name","Mix"},{"rows",QVariantList{row(),row()}}});
            QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));id=result.value("id").toString();
            auto refs=find(store,id).value("payload").toMap().value("occurrences").toList();QCOMPARE(refs.size(),2);
            const auto first=refs[0].toMap().value("itemId").toString(),second=refs[1].toMap().value("itemId").toString();QVERIFY(first!=second);
            QVERIFY(command(backend,"movePlaylistItem",{{"id",id},{"itemId",second},{"index",0}}).value("ok").toBool());
            QVERIFY(command(backend,"removePlaylistItem",{{"id",id},{"itemId",first}}).value("ok").toBool());
            QVERIFY(command(backend,"renamePlaylist",{{"id",id},{"name","Renamed"}}).value("ok").toBool());
            refs=find(store,id).value("payload").toMap().value("occurrences").toList();QCOMPARE(refs.size(),1);QCOMPARE(refs[0].toMap().value("itemId").toString(),second);
            store.flush();
        }
        CollectionStore reopened(path);QCOMPARE(find(reopened,id).value("title").toString(),QString("Renamed"));
        VinylBackendSession backend;bind(backend,reopened);QVERIFY(command(backend,"deletePlaylist",{{"id",id}}).value("ok").toBool());
        QVERIFY(find(reopened,id).isEmpty());QVERIFY(!find(reopened,"album-one").isEmpty());
    }
    void emptyPlaylistAndAtomicValidation() {
        QTemporaryDir dir;CollectionStore store(dir.filePath("collection.ini"));VinylBackendSession backend;bind(backend,store);
        auto result=command(backend,"createPlaylist",{{"name","Empty"},{"rows",QVariantList{}}});QVERIFY(result.value("ok").toBool());
        const auto id=result.value("id").toString();const auto before=store.items("Vinyl");
        auto invalid=row();invalid["discIndex"]=0.5;
        QVERIFY(!command(backend,"addPlaylistRows",{{"id",id},{"rows",QVariantList{row(),invalid}}}).value("ok").toBool());QCOMPARE(store.items("Vinyl"),before);
        QVERIFY(!command(backend,"renamePlaylist",{{"id",id},{"name","bad\nname"}}).value("ok").toBool());QCOMPARE(store.items("Vinyl"),before);
        QVERIFY(command(backend,"addPlaylistRows",{{"id",id},{"rows",QVariantList{row()}}}).value("ok").toBool());
        backend.clearProfile();QVERIFY(!command(backend,"deletePlaylist",{{"id",id}}).value("ok").toBool());QVERIFY(!find(store,id).isEmpty());
        QVERIFY(backend.personalRecords().value("playlists").toList().isEmpty());
    }
    void rejectsWrongSourceAndRemovesUnavailableSavedRecord() {
        QTemporaryDir dir;CollectionStore store(dir.filePath("collection.ini"));VinylBackendSession backend;bind(backend,store);
        auto forged=row();auto edition=forged["edition"].toMap(),album=edition["sourceAlbum"].toMap();
        album["tracks"]=QVariantList{};edition["sourceAlbum"]=album;forged["edition"]=edition;
        QVERIFY(!command(backend,"saveSong",{{"row",forged},{"saved",true}}).value("ok").toBool());QVERIFY(store.items("Vinyl").isEmpty());
        auto result=command(backend,"saveSong",{{"row",row()},{"saved",true}});QVERIFY(result.value("ok").toBool());
        QVERIFY(!backend.personalRowAvailable(row()));
        QVERIFY(!command(backend,"removeSavedSong",{{"id","album-one"}}).value("ok").toBool());
        QVERIFY(command(backend,"removeSavedSong",{{"id",result.value("id")}}).value("ok").toBool());
        QCOMPARE(store.items("Vinyl").size(),1);
    }
    void legacyDuplicatesRemainIndependentlyEditable() {
        QTemporaryDir dir;CollectionStore store(dir.filePath("collection.ini"));VinylBackendSession backend;bind(backend,store);
        const QString id="vinyl:playlist:legacy";
        const QVariantMap ref{{"albumId","album-one"},{"editionId","album-one"},{"trackId","track-one"},{"discIndex",0},{"trackIndex",0},{"index",0}};
        QVERIFY(store.add("Vinyl",{{"id",id},{"type","playlist"},{"title","Legacy"},{"payload",QVariantMap{{"version",1},{"occurrences",QVariantList{ref,ref}}}}}));
        const auto refs=backend.personalRecords()["playlists"].toList()[0].toMap()["payload"].toMap()["occurrences"].toList();
        const auto first=refs[0].toMap()["itemId"],second=refs[1].toMap()["itemId"];QVERIFY(first!=second);
        QVERIFY(command(backend,"removePlaylistItem",{{"id",id},{"itemId",first}}).value("ok").toBool());
        QCOMPARE(find(store,id)["payload"].toMap()["occurrences"].toList()[0].toMap()["itemId"],second);
    }
    void oldQueueSnapshotCannotOverwriteFreshCollectionMetadata() {
        QTemporaryDir dir;CollectionStore store(dir.filePath("collection.ini"));VinylBackendSession backend;bind(backend,store);
        auto current=row()["edition"].toMap()["sourceAlbum"].toMap();current["title"]="Fresh import";
        QVERIFY(store.add("Vinyl",{{"id","album-one"},{"type","album"},{"title","Fresh import"},{"payload",QVariantMap{{"album",current},{"saved",true}}}}));
        QVERIFY(command(backend,"saveSong",{{"row",row()},{"saved",true}}).value("ok").toBool());
        QCOMPARE(find(store,"album-one")["payload"].toMap()["album"].toMap()["title"].toString(),QString("Fresh import"));
        const auto before=store.items("Vinyl");
        QVERIFY(!command(backend,"createPlaylist",{{"name","Stale"},{"rows",QVariantList{row("Other","wrong-track")}}}).value("ok").toBool());
        QCOMPARE(store.items("Vinyl"),before);
    }
    void unsaveCannotDeleteAnotherRecordType() {
        QTemporaryDir dir;CollectionStore store(dir.filePath("collection.ini"));VinylBackendSession backend;bind(backend,store);
        const auto id=backend.personalSongId(row());
        QVERIFY(store.add("Vinyl",{{"id",id},{"type","artist"},{"title","Keep"}}));
        const auto before=store.items("Vinyl");
        QVERIFY(!command(backend,"saveSong",{{"row",row()},{"saved",false}}).value("ok").toBool());
        QCOMPARE(store.items("Vinyl"),before);
    }
};
QTEST_GUILESS_MAIN(VinylPersonalLibraryContracts)
#include "vinyl_personal_library_harness.moc"
