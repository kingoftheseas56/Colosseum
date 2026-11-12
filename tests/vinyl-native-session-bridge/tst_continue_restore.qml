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
        }
    }
    Component {
        id: transport
        QtObject {
            property int serial: 0
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            function request(lane: string, operation: string, value: string): int { return ++serial; }
            function cancel(lane: string): void {}
            function setAlbums(entries) {}
        }
    }
    Component {
        id: store
        QtObject {
            property var entries: ({})
            function recordSilent(entry) { entries[entry.kind+"/"+entry.id]=JSON.parse(JSON.stringify(entry)); }
            function get(kind,id) { return entries[kind+"/"+id] || {}; }
            function recent(kind,count) { return []; }
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
        session.suspendPlayback("test-selection");session.snapshot();
        compare(session.playingTrack.id,id+"-track");verify(session.paused);
        return JSON.parse(JSON.stringify(progress.get("music-session","vinyl")));
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
            tryVerify(()=>world.resumeRequiresArtistSelection);verify(world.visible,"The real artist picker must remain visible");
            verify(world.worldData.tastePickerOpen);
        } finally {world.destroy();wait(0);prefs.destroy();wait(0);}
    }
}
