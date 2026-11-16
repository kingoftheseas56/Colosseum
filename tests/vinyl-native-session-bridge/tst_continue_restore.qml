import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylContinueRestore"
    width: 1000
    height: 720
    visible: true
    when: windowShown
    property var session
    property var source
    property var progress
    property var startupLoader: null
    property bool explicitPending: false
    Component { id: loaderFactory; Loader { active: false; asynchronous: true } }
    Component {
        id: preferencesFactory
        QtObject {
            property bool vinylTasteAnswered: true
            property var vinylTasteArtists: [
                {id:"ytm:artist:UCaaaaaaaaaaaaaaaaaaaaaa",title:"A"},
                {id:"ytm:artist:UCbbbbbbbbbbbbbbbbbbbbbb",title:"B"},
                {id:"ytm:artist:UCcccccccccccccccccccccc",title:"C"},
                {id:"ytm:artist:UCdddddddddddddddddddddd",title:"D"},
                {id:"ytm:artist:UCeeeeeeeeeeeeeeeeeeeeee",title:"E"}
            ]
            signal vinylTasteChanged()
            function setVinylTasteSelections(answered, artists) {
                vinylTasteAnswered=answered;
                vinylTasteArtists=JSON.parse(JSON.stringify(artists));
                vinylTasteChanged();
                return true;
            }
        }
    }
    Component {
        id: transport
        QtObject {
            property int serial: 0
            property int registeredAlbumCount: 0
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            function request(lane, operation, value, options) { return ++serial; }
            function cancel(lane: string): void {}
            function setAlbums(entries) { registeredAlbumCount=entries.length; }
        }
    }
    Component {
        id: store
        QtObject {
            property var entries: ({})
            property int revision: 0
            property int clock: 0
            property var removals: []
            function recordSilent(entry) {
                var copy=JSON.parse(JSON.stringify(entry));
                copy.updatedAt=++clock;
                entries[entry.kind+"/"+entry.id]=copy;
            }
            function record(entry) { recordSilent(entry);revision++; }
            function forget(kind,id) { delete entries[kind+"/"+id];removals.push(kind+"/"+id);revision++; }
            function get(kind,id) { return entries[kind+"/"+id] || {}; }
            function recent(kind,count) {
                var rows=Object.keys(entries).map(k=>entries[k]).filter(e=>e.kind===kind)
                    .sort((a,b)=>b.updatedAt-a.updatedAt);
                return count>0 ? rows.slice(0,count) : rows;
            }
        }
    }
    function createSession(defer) {
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source,progressStore:progress,collectionStore:null,deferInitialRestore:!!defer});
        verify(session!==null);
    }
    function init() { source=transport.createObject(test);progress=store.createObject(test);createSession(false); }
    function cleanup() { if(startupLoader){startupLoader.destroy();startupLoader=null;}else session.destroy();wait(0);source.destroy();progress.destroy();wait(0);explicitPending=false; }
    function select(id) {
        session.playAlbum({id:id,title:id,origin:"youtube-music",tracks:[{id:id+"-track",title:id,videoId:"abcdefghijk",seconds:120,available:true}]},0);
        session.suspendPlayback("test-selection");session.snapshot();wait(0);
        compare(session.playingTrack.id,id+"-track");verify(session.paused);
        return JSON.parse(JSON.stringify(progress.get("music-session","vinyl")));
    }
    Component {
        id: collectionFactory
        QtObject {
            property int revision: 0
            property int reads: 0
            signal changed()
            function items(world) { reads++;return []; }
        }
    }
    function test_navigationDoesNotReloadUnchangedCollection() {
        var collection=collectionFactory.createObject(test);
        session.collectionStore=collection;
        session.refreshLibrary();
        var component=Qt.createComponent("../../qml/vinyl/VinylPersonalLibraryData.qml");
        compare(component.status,Component.Ready,component.errorString());
        var personal=component.createObject(test,{session:session});
        var rebuilds=0;
        personal._albumRecordsChanged.connect(function(){rebuilds++;});
        var reads=collection.reads;
        session.shownEdition=session.normalize({id:"browse",title:"Browse",tracks:[]});
        session.refreshLibrary(); // album open
        session.refreshLibrary(); // Back/route restore
        compare(collection.reads,reads);
        collection.revision++;collection.changed();
        // The native personal-records projection also reads items on revision.
        verify(collection.reads>reads);
        wait(0); // Coalesced rebuild follows both Collection projections.
        compare(rebuilds,1,"Personal models refresh once for the Collection change");
        reads=collection.reads;
        session.refreshLibrary();
        compare(collection.reads,reads);
        personal.destroy();
        collection.destroy();
    }
    Component {
        id: personalCollectionFactory
        QtObject {
            property var entries: []
            property int revision: 0
            property bool snapshotFirst: false
            signal changed()
            function items(world) { return entries; }
            function has(world, id) { return entries.some(entry => entry.id === id); }
            function replaceItems(world, removeIds, replacements) {
                var next = entries.filter(entry => removeIds.indexOf(entry.id) < 0
                    && !replacements.some(write => write.id === entry.id)).concat(replacements);
                // Match native storage: items() itself must not expose a QML
                // entriesChanged dependency that masks the revision ordering.
                entries.splice.apply(entries, [0, entries.length].concat(next));
                // CollectionStore uses changed as revision's NOTIFY signal. Exercise
                // both observer orders rather than relying on QML connection order.
                if (snapshotFirst) { changed(); revision++; }
                else { revision++; changed(); }
                return true;
            }
        }
    }
    function test_personalMutationsRefreshModels_data() {
        return [{tag:"records-first", snapshotFirst:false},
                {tag:"snapshot-first", snapshotFirst:true}];
    }
    function test_personalMutationsRefreshModels(data) {
        var path = BridgeFixtures.createIdentityAudio();
        var album = {id:"personal-refresh", title:"Personal refresh", origin:"local",
            artists:["Test Artist"], tracks:[{id:"personal-track", title:"Test song",
                seconds:41, disc:1, number:1, available:true, localPath:path}]};
        var collection = personalCollectionFactory.createObject(test, {
            snapshotFirst:data.snapshotFirst,
            entries:[{id:album.id, type:"album", title:album.title,
                payload:{version:1, album:album, saved:true}}]
        });
        session.collectionStore = collection;
        var component = Qt.createComponent("../../qml/vinyl/VinylPersonalLibraryData.qml");
        compare(component.status, Component.Ready, component.errorString());
        var personal = component.createObject(test, {session:session});
        try {
            verify(session.personalEnabled);
            var row = session.rowFor(session.normalize(album), 0, 0);
            verify(session.personalRowAvailable(row));
            var rebuilds = 0;
            personal._albumRecordsChanged.connect(function() { rebuilds++; });

            verify(session.toggleSongSaved(row));
            // Drain only this mutation's queued work; never manually refresh or
            // issue another Collection change to make the models catch up.
            wait(0);
            compare(personal.savedSongs.length, 1, "Save appears without another collection change");
            compare(personal.savedSongs[0].ref.trackId, row.trackId);
            compare(rebuilds, 1);

            rebuilds = 0;
            verify(session.toggleSongSaved(row));
            wait(0);
            compare(personal.savedSongs.length, 0, "Unsave appears without another collection change");
            compare(rebuilds, 1);

            var result = session.personalCommand("createPlaylist", {name:"Refresh guard", rows:[]});
            verify(result.ok);
            wait(0);
            verify(personal.openPlaylist(result.id));
            compare(personal.selectedPlaylist.items.length, 0);
            rebuilds = 0;
            verify(session.personalCommand("addPlaylistRows", {id:result.id, rows:[row]}).ok);
            wait(0);
            compare(personal.playlists[0].count, 1);
            compare(personal.selectedPlaylist.items.length, 1, "Playlist addition appears immediately");
            compare(personal.selectedPlaylist.items[0].ref.trackId, row.trackId);
            compare(rebuilds, 1);
        } finally {
            personal.destroy();
            session.collectionStore = null;
            collection.destroy();
        }
    }
    function test_sourceRegistersOneSnapshotPerAlbumNotPerQueueRow() {
        var album={id:"many",title:"Many",origin:"youtube-music",tracks:[]};
        for(var i=0;i<50;i++)album.tracks.push({id:"song-"+i,title:"Song "+i,videoId:"abcdefghijk",seconds:120,available:true});
        session.playAlbum(album,0);
        session.refreshSource();
        compare(session.queue.length,50);
        compare(source.registeredAlbumCount,1);
    }
    function test_pausedSessionHasNoPeriodicCheckpoint() {
        select("paused");
        var timer=findChild(session,"vinylProgressTimer");
        verify(timer!==null);
        // Exercise the periodic callback independently of whether a core is parked.
        var before=progress.clock;
        timer.triggered();
        compare(progress.clock,before);
    }
    function test_coldTileRestoreNeverWritesBeforeSuccess_data() { return [{tag:"success",valid:true},{tag:"failure",valid:false}]; }
    function test_coldTileRestoreNeverWritesBeforeSuccess(data) {
        var saved=select("saved"), before=JSON.stringify(progress.entries);
        session.destroy();wait(0);createSession(true);
        compare(session.queue.length,0);
        // Reproduce the former cold Continue route at the real session boundary.
        // Even an incidental lifecycle snapshot before tile restore must be harmless.
        session.snapshot();
        compare(session.restore(data.valid ? saved : {resume:{version:3,backendState:"corrupt"}}),data.valid);
        compare(JSON.stringify(progress.entries),before,"Tile restore must not replace the durable singleton");
    }
    function test_emptyUnrestoredSnapshotPreservesSingleton() {
        select("saved");var before=JSON.stringify(progress.entries);
        session.destroy();wait(0);createSession(true);session.snapshot();
        compare(JSON.stringify(progress.entries),before);
        verify(!session.restore({resume:{version:3,backendState:"corrupt"}}));session.snapshot();
        compare(JSON.stringify(progress.entries),before);
    }
    function test_albumCheckpointsCappedAndOtherKindsUntouched() {
        ["video","book","manga"].forEach(kind=>progress.recordSilent({kind:kind,id:"sentinel",resume:{position:17}}));
        var sentinels=JSON.stringify(progress.recent("video",0).concat(progress.recent("book",0),progress.recent("manga",0)));
        for(var i=0;i<25;i++)select("album-"+i);
        compare(progress.recent("music",0).length,20);
        verify(!progress.get("music","album-0").resume);
        verify(progress.get("music","album-24").resume);
        compare(progress.removals.length,5);
        verify(progress.removals.every(key=>key.indexOf("music/")===0));
        compare(JSON.stringify(progress.recent("video",0).concat(progress.recent("book",0),progress.recent("manga",0))),sentinels);
        select("album-5");select("album-25");
        verify(progress.get("music","album-5").resume,"Replayed editions remain recent");
        verify(!progress.get("music","album-6").resume);
    }
    function test_continueRevisionChangesOnTrackButNotPeriodicSnapshots() {
        session.playAlbum({id:"two-tracks",title:"Two",tracks:[{id:"one",title:"One",seconds:60},{id:"two",title:"Two",seconds:60}]},0);
        wait(0);verify(progress.revision>0,"Continue appears on first selection");
        var revision=progress.revision;
        session.snapshot();session.snapshot();compare(progress.revision,revision);
        session.next(1);wait(0);
        verify(progress.revision>revision,"Continue refreshes on track change within an edition");
        compare(progress.get("music",session.playingEditionId).title,"Two");
        session.suspendPlayback("leaving");revision=progress.revision;
        session.snapshot();session.snapshot();compare(progress.revision,revision);
    }
    function test_finishedCheckpointRestartsLastTrack_data() {return [{tag:"singleton",tile:false},{tag:"tile",tile:true},{tag:"saved-queue",savedQueue:true}];}
    function test_finishedCheckpointRestartsLastTrack(data) {
        var entry=select("finished");
        // Reproduce an existing v3 EOF checkpoint: native naturalEnd saves duration as position.
        entry.resume.position=120;entry.resume.duration=120;
        progress.recordSilent(entry);
        session.destroy();wait(0);createSession(true);
        verify(data.savedQueue ? session.resumeSavedCheckpoint(entry) : session.restore(data.tile ? entry : undefined));
        compare(session.playingTrack.id,"finished-track");
        compare(session.position,0,"Finished media restarts from the top, like videoTargetFor");
    }
    function test_explicitClearStillPersistsEmptyQueueAndPolicy() {
        select("saved");session.cycleRepeat();session.setShuffle(true);session.clearQueue();
        var shuffle=session.shuffleEnabled;
        session.destroy();wait(0);createSession(false);
        compare(session.queue.length,0);compare(session.repeatMode,"all");compare(session.shuffleEnabled,shuffle);
    }
    function test_rejectedRestoreRetainsPausedOldSelection() {
        select("old");var queue=JSON.stringify(session.queue);
        compare(session.restore({resume:{version:3,backendState:"corrupt"}}),false);
        compare(JSON.stringify(session.queue),queue);verify(session.paused);verify(session.browseError.length>0);
        session.clearProfile();compare(session.restore({resume:{version:3}}),false);verify(session.paused);
    }
    function test_coldExplicitDefersDefaultAndRestoresRequestedSelection() {
        var requested=select("requested");select("default");
        session.destroy();wait(0);createSession(true);
        compare(session.queue.length,0);compare(session.player,null);
        compare(session.restore(requested),true);compare(session.playingTrack.id,"requested-track");verify(session.paused);
        session.deferInitialRestore=false;wait(0);compare(session.playingTrack.id,"requested-track");
    }
    function test_normalDefaultAndLegacyRestore() {
        select("default");session.destroy();wait(0);createSession(false);
        compare(session.playingTrack.id,"default-track");verify(session.paused);
        var edition=JSON.parse(JSON.stringify(session.shownEdition));
        compare(session.restore({resume:{version:1,edition:edition,queue:[0],queueIndex:0,trackId:"default-track",position:7}}),true);
        compare(session.playingTrack.id,"default-track");compare(session.position,7);verify(session.paused);
    }
    function test_loaderInitialBindingTracksPendingRequestBeforeCompletion() {
        var requested=select("requested");select("default");session.destroy();wait(0);
        startupLoader=loaderFactory.createObject(test);
        startupLoader.setSource(Qt.resolvedUrl("../../qml/vinyl/VinylLiveSession.qml"),{
            source:source,progressStore:progress,collectionStore:null,
            deferInitialRestore:Qt.binding(function(){return test.explicitPending;})
        });
        explicitPending=true;startupLoader.active=true;
        tryCompare(startupLoader,"status",Loader.Ready);session=startupLoader.item;
        verify(session.deferInitialRestore);compare(session.queue.length,0);
        verify(session.restore(requested));compare(session.playingTrack.id,"requested-track");verify(session.paused);
        explicitPending=false;verify(!session.deferInitialRestore);compare(session.playingTrack.id,"requested-track");
    }
    function test_readyWorldPresentationHoldsHomeUntilRouteCommit() {
        var prefs=preferencesFactory.createObject(test);
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status,Component.Ready,component.errorString());
        var world=component.createObject(test,{width:1000,height:720,homeLanding:true,deferInitialRestore:true,resumePresentationPending:true});
        verify(world!==null);
        try {
            verify(!world.visible,"The initial world cannot present its default route");
            world.session.source=source;world.worldData.preferencesStore=prefs;
            var recommendations=world.recommendationService;
            compare(recommendations.selectedArtists.length,5);
            recommendations.profileGeneration=world.session.profileGeneration+1;
            verify(!world.canEnterVinyl);verify(!world.resumeRequiresArtistSelection);
            wait(80);verify(!world.visible,"A ready world with five artists stays hidden during admission delay");
            recommendations.profileGeneration=world.session.profileGeneration;
            tryVerify(()=>world.canEnterVinyl);verify(!world.visible);
            world.page="song";verify(!world.visible);
            world.resumePresentationPending=false;verify(world.visible);compare(world.page,"song");
            world.resumePresentationPending=true;verify(!world.visible);
            world.session.browseError="Synthetic restore failure";
            world.resumePresentationPending=false;verify(world.visible);
            verify(findChild(world,"vinylStatus").text.indexOf("Synthetic restore failure")>=0);
            world.resumePresentationPending=true;world.resumePresentationPending=false;verify(world.visible);
            world.resumePresentationPending=true;
            prefs.vinylTasteAnswered=false;prefs.vinylTasteArtists=[];prefs.vinylTasteChanged();
            recommendations.beginHomeVisit();world.enforceTasteGate();
            tryVerify(()=>recommendations.canEnterVinyl);
            verify(!world.canEnterVinyl,"An active profile still needs five artists at the UI gate");
            verify(world.resumeRequiresArtistSelection);
            verify(world.worldData.tastePickerOpen,"An unanswered listener must see the mandatory picker");
            verify(world.visible,"The artist gate must be visible even while a restore route is pending");
            world.resumePresentationPending=false;verify(world.visible);
        } finally {world.destroy();wait(0);prefs.destroy();wait(0);}
    }
}
