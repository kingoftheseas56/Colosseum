import QtQuick
import QtTest
import Colosseum.Vinyl 1.0
import "../../qml/vinyl" as Vinyl
import "../../qml/vinyl/VinylLibraryProjection.js" as LibraryProjection

TestCase {
    id:test
    name:"VinylV1HomeActions"
    when:windowShown
    property var host:null
    property var world:null
    property var calls:[]
    property var record:null
    Component {
        id:windowFactory
        Window {
            width:1280; height:700; visible:true
            property alias wallpaper:wallpaper
            Item { id:wallpaper; anchors.fill:parent }
        }
    }
    // Same read-only owner ports used by tst_music_discover.qml. No personal
    // writes, replacement store, resolver, media URL or decoder fixture here.
    QtObject {
        id:temporaryPreferences
        property bool vinylTasteAnswered:true
        property var vinylTasteArtists:[]
        property var vinylCratePins:[]
        property bool keepActivityHistory:false
        signal vinylTasteChanged()
        function setVinylTasteSelections(answered,artists){vinylTasteAnswered=answered;vinylTasteArtists=artists;vinylTasteChanged();return true;}
        function setVinylCratePins(pins: list<var>): bool {vinylCratePins=pins;return true;}
    }
    QtObject {
        id:temporaryCollection
        property var entries:[]
        property int revision:0
        property bool healthy:true
        property string persistenceError:""
        function items(world){return entries;}
        function has(world,id){return entries.some(function(entry){return entry.id===id;});}
    }
    QtObject {
        id:projectionRecommendations
        property bool canEnterVinyl:true
        property int minimumArtistCount:1
        property int homeRevision:0
        property string homeStatus:"ready"
        property string homeError:""
        property string onboardingState:"answered"
        property string error:""
        property var selectedArtists:[]
        property var draftArtists:[]
        property var artistResults:({status:"ready",items:[]})
        property var homeSnapshot:({})
        property var homeSourcePool:({})
        property var homeSections:[]
        property var homeCarouselItems:[]
        property var homeListeningCards:[]
        property var homeQuickPicks:[]
        property var historySnapshot:({})
        property var visibleCalls:[]
        function beginHomeVisit(){}
        function recordVisibleItems(section,snapshot,refs,generation){
            visibleCalls=visibleCalls.concat([{section:section,snapshot:snapshot,refs:refs,generation:generation}]);
            return true;
        }
    }
    VideoTestSource { id:nativeSource }
    Connections {
        target:nativeSource
        function onRequested(lane,id,operation,value,options){
            test.calls=test.calls.concat([{lane:lane,id:id,operation:operation,value:value,options:options}]);
        }
    }
    Component {
        id:worldFactory
        Vinyl.VinylAppWorld {
            width:1280; height:700; hostReady:true; deferInitialRestore:true
            backdrop:test.host ? test.host.wallpaper : null
            session.source:nativeSource
            session.collectionStore:temporaryCollection
            session.activityPreferences:temporaryPreferences
            worldData.preferencesStore:temporaryPreferences
        }
    }
    Component {
        id:crateFactory
        Vinyl.VinylCrate {
            width:640; z:100; compact:true
            onOpenRequested: item => worldData.openHomeItem(item)
            onActivateRequested: item => worldData.playHomeItem(item)
        }
    }
    function album(){
        var artist="ytm:artist:UC"+"a".repeat(22);
        return {id:"home-route-album",browseId:"MPRE_home_fixture",origin:"youtube-music",type:"Album",
            title:"Known Home Album",artists:["Fixture artist"],artistIds:[artist],cover:"",
            providerIds:{"youtube-music":"MPRE_home_fixture"},tracks:[
                {id:"home-one",videoId:"HOME0000001",title:"Known One",artists:["Fixture artist"],artistIds:[artist],seconds:120,available:true},
                {id:"home-two",videoId:"HOME0000002",title:"Known Two",artists:["Fixture artist"],artistIds:[artist],seconds:121,available:true}]};
    }
    function init(){
        verify(typeof BridgeFixtures!=="undefined","Requires the rebuilt vinyl_native_session_bridge runner");
        temporaryPreferences.vinylCratePins=[];
        var picks=[];
        for(var letter of ["a","b","c","d","e"]){
            var id="UC"+letter.repeat(22);
            picks.push({key:JSON.stringify(["youtube-music","artist",id]),kind:"artist",provider:"youtube-music",
                providerId:id,name:"Fixture "+letter,image:"",mappingStatus:"unresolved"});
        }
        temporaryPreferences.setVinylTasteSelections(true,picks);
        record=album();
        temporaryCollection.entries=[{id:record.id,type:"album",title:record.title,
            payload:{version:1,album:record,saved:true}}];
        temporaryCollection.revision++;
        calls=[];
        host=windowFactory.createObject(test);
        verify(host!==null);
        world=worldFactory.createObject(host.contentItem);
        verify(world!==null);
        tryCompare(world,"componentReady",true);
        tryCompare(world,"canEnterVinyl",true);
        world.session.refreshLibrary();world.personalLibrary.refreshModels();world.showHome();
        tryCompare(world,"page","home");
        verify(world.homeActivationAllowed);
        compare(world.session.queue.length,0);
        // The selection is immediate; the initial Home visit settles next turn.
        wait(0);
        calls=[];
    }
    function cleanup(){
        if(world){world.destroy();wait(0);world=null;}
        if(host){host.close();host.destroy();wait(0);host=null;}
        temporaryCollection.entries=[];record=null;calls=[];
    }
    function requests(operation,lane){return calls.filter(function(call){return call.operation===operation && (!lane || call.lane===lane);});}
    function reply(call,data){nativeSource.reply(call.lane,call.id,data,"");}
    function assertSelectedQueue(index){
        tryCompare(world.session,"queueIndex",index);
        compare(world.session.queue.length,2);
        compare(world.session.queue[index].trackId,record.tracks[index].id);
        tryVerify(function(){return requests("resolve","play").length>0;},5000,"The existing native session must request the selected queue track");
        var resolves=requests("resolve","play");
        compare(resolves[resolves.length-1].value,record.tracks[index].id);
    }
    function test_homeVisibilityStopsDeferredRebuildsOnEveryDetour(){
        var owner=world.recommendationService;
        var timer=findChild(owner,"homeInputsTimer");
        verify(timer!==null);
        var serial=0;
        function dirty(){
            verify(world.worldData.homePageVisible);
            wait(0); // Run the deferred Home visit before dirtying its inputs.
            owner.homeCollections={visibilityTest:++serial};
            verify(timer.active);
        }
        dirty();
        world.worldView.setTab("library");
        verify(!world.worldData.homePageVisible);verify(!timer.active);
        world.showHome();
        dirty();
        world.worldView.showList("albums",true);
        verify(!world.worldData.homePageVisible);verify(!timer.active);
        world.worldView.backFromList();
        dirty();
        world.page="song";
        verify(!world.worldData.homePageVisible);verify(!timer.active);
        world.showHome();
        dirty();
        world.visible=false;
        verify(!world.worldData.homePageVisible);verify(!timer.active);
        world.visible=true;
        dirty();
        world.lifecycleActive=false;
        verify(!world.worldData.homePageVisible);verify(!timer.active);
    }
    function test_homeMetadataWrapperSendsOnlyOriginalCatalogueItem(){
        var original={id:"apple-music:albums:home-fixture",kind:"albums",catalogueCard:true,source:"apple-music",
            title:"External album",artist:"Fixture artist",cover:"",sourceIds:{id:"12345"},hints:{artists:["Fixture artist"]}};
        var wrapper={id:"wrapped-home-pick",kind:"albums",title:"Presentation wrapper",requiresResolution:true,
            catalogueItem:original,reason:"Owner reason",rank:1};
        world.worldData.openHomeItem(wrapper);
        var taps=requests("resolveCatalogueItem","catalogue-item");compare(taps.length,1);
        compare(taps[0].value,original.id);compare(Object.keys(taps[0].options).join(","),"item");
        var actual=taps[0].options.item;
        compare(Object.keys(actual).sort().join(","),Object.keys(original).sort().join(","));
        for(var key in original)compare(JSON.stringify(actual[key]),JSON.stringify(original[key]));
        compare(world.page,"home");compare(world.session.queue.length,0);compare(requests("resolve","play").length,0);
        // A conservative miss is still a miss: no fallback guessing or playback.
        reply(taps[0],{version:1,id:original.id,kind:original.kind,matched:false,reason:"Fixture miss"});
        compare(world.page,"home");compare(world.session.queue.length,0);verify(world.discoverStatus.length>0);
    }
    function test_knownSongOpenDoesNotPlayAndPlayUsesSelectedQueueTrack(){
        var item={id:"ytm:track:HOME0000002",kind:"songs",title:"Known Two",record:record,trackIndex:1,
            destination:{kind:"song",provider:"youtube-music",providerId:"HOME0000002",id:"ytm:track:HOME0000002",playable:true}};
        world.worldData.openHomeItem(item);
        compare(world.page,"song");verify(world.browsingSong);compare(world.session.queue.length,0);
        compare(requests("resolve","play").length,0);verify(world.session.paused);
        world.showHome();calls=[];
        world.worldData.playHomeItem(item);
        assertSelectedQueue(1);
    }
    function test_newerPlaybackAndNavigationCancelPendingMixPlay(){
        var mix={id:"home-mix:personal",kind:"mixes",mixId:"personal",title:"Personal mix"};
        world.activateHomeItem(mix,"play");
        verify(world.pendingMixPlay!==null,"The requested mix is still waiting for catalogue tracks");
        world.session.playAlbum(record,1);
        assertSelectedQueue(1);
        compare(world.pendingMixPlay,null,"A newer manual song choice must cancel the waiting mix");

        world.activateHomeItem(mix,"play");
        verify(world.pendingMixPlay!==null);
        world.showSong();
        tryCompare(world,"page","song");
        compare(world.pendingMixPlay,null,"Leaving the requesting route must cancel its waiting mix");
        compare(world.session.queue[world.session.queueIndex].trackId,record.tracks[1].id);
    }
    function test_knownAlbumOpenOnlyBrowsesAndPlayUsesResumeQueueIndex(){
        var item={id:record.id,albumId:record.id,kind:"albums",title:record.title,record:record,resumeTrack:2,totalTracks:2};
        var crate=createTemporaryObject(crateFactory,host.contentItem,{items:[item],worldData:world.worldData});
        verify(crate);
        var card=findChild(crate,"vinylCrateTile_0_card");
        verify(card && waitForRendering(card));
        mouseClick(card,20,70);
        var browse=requests("album","browse");compare(browse.length,1);compare(browse[0].value,record.id);
        compare(world.session.queue.length,0);compare(requests("resolve","play").length,0);
        reply(browse[0],record);
        tryCompare(world,"page","album");compare(world.session.shownAlbumId,record.id);compare(world.session.queue.length,0);
        world.showHome();calls=[];
        var play=findChild(card,card.objectName+"_albumPlay");
        mouseMove(card,20,70);tryCompare(play,"revealed",true);
        compare(play.accessibleName,"Resume album");mouseClick(play);
        assertSelectedQueue(1);
        compare(requests("album","browse").length,0);
    }
    function test_libraryProjectionDispatchesOriginalPersonalOwnerItem(){
        var rows=LibraryProjection.project(world.personalLibrary,{ok:false},{});
        var saved=rows.find(function(row){return row.key==="saved-albums";});
        verify(saved!==undefined);compare(saved.items.length,1);
        var projected=saved.items[0];compare(projected.libraryAction,"activateAlbum");
        compare(projected.libraryItem.albumId,record.id);
        var wrapper=Object.assign({},projected,{id:"different-wrapper-id",albumId:"different-wrapper-album"});
        world.worldData.openHomeItem(wrapper);
        var browse=requests("album","browse");compare(browse.length,1);
        compare(browse[0].value,record.id,"The personal owner's original item must select the album, not wrapper fields");
        compare(world.session.queue.length,0);compare(requests("resolve","play").length,0);
        reply(browse[0],record);tryCompare(world,"page","album");compare(world.session.shownAlbumId,record.id);
    }
    function test_downloadPinReadAndUnpinUseTheExistingOwnedSongIdentity(){
        var entries=[{id:"fixture-download",kind:"music",available:true,addedAt:1,
            path:Qt.resolvedUrl("../fixtures/vault/media/tiny-audio.m4a").toString(),
            music:{album:record,trackIndex:1,videoId:record.tracks[1].videoId}}];
        var rows=LibraryProjection.project(world.personalLibrary,{ok:false},{},entries);
        var downloads=rows.find(function(row){return row.key==="downloads";});
        verify(downloads!==undefined);compare(downloads.items.length,1);
        var item=downloads.items[0];compare(item.libraryAction,"openDownloadedSong");
        tryVerify(function(){return world.worldData.crateItem(item)!==null;});
        verify(!world.worldData.isCratePinned(item));
        verify(world.worldData.pinCrateItem(item,true));
        compare(temporaryPreferences.vinylCratePins.length,1);
        verify(world.worldData.isCratePinned(item),"The Download row must read the same pin that it wrote");
        verify(world.worldData.pinCrateItem(item,false));
        compare(temporaryPreferences.vinylCratePins.length,0);
        verify(!world.worldData.isCratePinned(item));
        compare(world.session.queue.length,0);compare(requests("resolve","play").length,0);
    }
    function test_backgroundUpdatesKeepUnchangedDelegateModels(){
        var data=world.worldData;
        world.recommendationService.refreshHome(true);
        tryVerify(function(){return requests("artistCandidates").length>0;});
        var names=["heroSlides","spinSongs","spinAlbums","spinArtists","v1HomeRows","crateItems","libraryRows"];
        var models=names.map(function(name){return data[name];});
        var revision=data.homeRevision;
        reply(requests("artistCandidates")[0],{});
        tryVerify(function(){return data.homeRevision>revision;});
        verify(data.homeError.length>0);
        for(var i=0;i<names.length;++i)
            verify(data[names[i]]===models[i],names[i]+" must retain its delegates when the visual data is unchanged");
    }
    function test_exposureRefreshKeepsMusicModelsAndUsesTheCurrentVisibilityToken(){
        var data=world.worldData,ref={identity:"provider",kind:"artist",providerId:"youtube-music",itemId:"UC"+"a".repeat(22)};
        var item={id:"metadata-artist",kind:"artists",title:"Same artist",ref:ref};
        projectionRecommendations.homeSourcePool={rows:[{key:"artists-like-yours",title:"Artists like yours",items:[item]}]};
        projectionRecommendations.homeSnapshot={exposureSnapshotId:"before-metadata"};
        projectionRecommendations.visibleCalls=[];
        data.recommendationService=projectionRecommendations;
        var rows=data.v1HomeRows,generation=data.recommendationProfileGeneration();
        compare(rows.length,1);
        projectionRecommendations.homeSnapshot={exposureSnapshotId:"after-metadata"};
        projectionRecommendations.homeRevision++;
        verify(data.v1HomeRows===rows,"Metadata-only snapshot changes must preserve the Home music model");
        verify(data.recordVisibleRecommendation(rows[0],rows[0].items[0],generation,true));
        compare(projectionRecommendations.visibleCalls.length,1);
        compare(projectionRecommendations.visibleCalls[0].snapshot,"after-metadata");
        verify(!data.recordVisibleRecommendation(rows[0],rows[0].items[0],generation+1,true));
        verify(!data.recordVisibleRecommendation(rows[0],rows[0].items[0],generation,false));
        compare(projectionRecommendations.visibleCalls.length,1);
    }
}
