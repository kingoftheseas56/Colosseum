import QtQuick
import QtTest
import Colosseum.Vinyl 1.0
import "../../qml/vinyl" as Vinyl

TestCase {
    id:test
    name:"VinylNativeYoutubeBrowse"
    width:1280;height:850;visible:true
    property var calls:[]
    QtObject {
        id:temporaryPreferences
        property bool vinylTasteAnswered:true
        property var vinylTasteArtists:[]
        property bool keepActivityHistory:false
        signal vinylTasteChanged()
        function setVinylTasteSelections(answered,artists) {vinylTasteAnswered=answered;vinylTasteArtists=artists;vinylTasteChanged();return true;}
    }
    QtObject {
        id:temporaryCollection
        property int revision:0
        property bool healthy:true
        property string persistenceError:""
        function items(world){return [];}
        function has(world,id){return false;}
    }
    QtObject {
        id:temporaryProgress
        property bool healthy:true
        property string persistenceError:""
        function recent(kind,count){return [];}
        function get(kind,id){return null;}
    }
    Component {
        id:liveWorldFactory
        Vinyl.VinylLiveWorld {
            width:1280;height:850;deferInitialRestore:true
            session.source:nativeSource
            session.collectionStore:temporaryCollection
            session.progressStore:temporaryProgress
            session.activityPreferences:temporaryPreferences
            worldData.preferencesStore:temporaryPreferences
        }
    }
    VideoTestSource {id:nativeSource}
    Connections {
        target:nativeSource
        function onRequested(lane,id,operation,value,options) {
            test.calls=test.calls.concat([{lane:lane,id:id,operation:operation,value:value,options:options}]);
        }
    }
    Vinyl.VinylCatalogueBrowseData {id:browse;source:nativeSource}
    QtObject {
        id:rejectingSource
        property bool reject:true
        function request(lane,operation,value,options) {return reject ? -1 : nativeSource.request(lane,operation,value,options);}
        function cancel(lane) {nativeSource.cancel(lane);}
        signal completed(string lane,int id,var data,string error)
    }
    function init() {
        browse.enabled=false;browse.active=false;browse.activeKind="home";
        browse.source=nativeSource;browse.reset();calls=[];rejectingSource.reject=true;
        browse.enabled=true;browse.active=true;
    }
    function last(kind) {return calls.filter(function(call){return call.value===kind;}).slice(-1)[0];}
    function response(call,items,extra) {
        return Object.assign({version:1,kind:call.value,category:call.options.category,region:call.options.region,
            items:items,records:[],nextCursor:{},complete:true},extra || {});
    }
    function test_assembled_world_gate_tabs_and_song_back_route() {
        // Canonical provider identities satisfy the real five-artist onboarding gate.
        var picks=[];
        ["UCaaaaaaaaaaaaaaaaaaaaaa","UCbbbbbbbbbbbbbbbbbbbbbb","UCcccccccccccccccccccccc","UCdddddddddddddddddddddd","UCeeeeeeeeeeeeeeeeeeeeee"].forEach(function(id,index){
            picks.push({key:JSON.stringify(["youtube-music","artist",id]),kind:"artist",provider:"youtube-music",providerId:id,name:"Fixture artist "+index,image:"",mappingStatus:"unmapped"});
        });
        temporaryPreferences.setVinylTasteSelections(true,picks);
        // Keep the standalone adapter inactive: only the assembled world's source requests count.
        browse.enabled=false;
        var live=createTemporaryObject(liveWorldFactory,test);verify(live!==null);
        tryCompare(live,"canEnterVinyl",true);
        compare(calls.filter(function(call){return call.operation==="browseCatalogue";}).length,0);
        var songItem={id:"ytm:track:assembled",kind:"songs",title:"Assembled song",artist:"Fixture artist",albumId:"assembled-container",trackId:"assembled-track",trackIndex:0};
        var record={id:"assembled-container",title:"Assembled release",artists:["Fixture artist"],artistIds:[picks[0].providerId],origin:"youtube-music",
            tracks:[{id:"assembled-track",title:"Assembled song",artists:["Fixture artist"],artistIds:[picks[0].providerId],seconds:120,disc:1,number:1}]};
        ["songs","albums","artists"].forEach(function(kind){
            live.worldView.setTab(kind);
            compare(live.page,kind);compare(live.worldData.activeTab,kind);
            var call=last(kind);verify(call!==undefined);compare(call.operation,"browseCatalogue");compare(call.value,kind);compare(call.options.category,"");
            if(kind==="artists")compare(call.options.region,"ZZ");
            var items=kind==="songs" ? [songItem] : kind==="albums" ? [{id:record.id,albumId:record.id,title:record.title,kind:"albums"}] : [{id:picks[0].providerId,artistId:picks[0].providerId,artist:"Fixture artist",title:"Fixture artist",kind:"artists"}];
            nativeSource.reply(call.lane,call.id,response(call,items,{records:kind==="artists" ? [] : [record]}),"");
        });
        var categoryCall=calls.filter(function(call){return call.operation==="browseCategories";}).slice(-1)[0];
        nativeSource.reply(categoryCall.lane,categoryCall.id,{version:1,categories:[{id:"rock",title:"Rock",group:"Genres",params:"assembled-rock"}]},"");
        live.worldView.setTab("songs");
        var categoryPicker=findChild(live,"vinylCatalogueCategory");verify(categoryPicker!==null);categoryPicker.picked("assembled-rock");
        var selected=last("songs");compare(selected.options.category,"assembled-rock");
        nativeSource.reply(selected.lane,selected.id,response(selected,[songItem],{records:[record]}),"");
        compare(live.worldData.findAlbum(record.id).tracks[0].id,"assembled-track");
        compare(live.session.queue.length,0);
        live.activateItem(songItem);compare(live.page,"song");verify(live.browsingSong);compare(live.session.queue.length,0);
        verify(live.goBack());compare(live.page,"songs");compare(live.worldData.activeTab,"songs");compare(live.worldView.currentTab,"songs");
        compare(categoryPicker.currentKey,"assembled-rock");compare(live.worldData.findAlbum(record.id).id,record.id);
        live.destroy();wait(0);
    }
    function test_native_map_lists_categories_song_records_markets() {
        compare(calls.length,0);browse.activeKind="songs";
        var categoryCall=calls.filter(function(call){return call.operation==="browseCategories";})[0];
        nativeSource.reply(categoryCall.lane,categoryCall.id,{version:1,categories:[
            {id:"rock",title:"Rock",group:"Genres",params:"native-opaque-rock"}]},"");
        compare(browse.categories.length,1);compare(browse.categories[0].params,"native-opaque-rock");
        verify(Array.isArray(browse.categories));
        var songCall=last("songs");nativeSource.reply(songCall.lane,songCall.id,response(songCall,
            [{id:"ytm:track:native-one",kind:"songs",title:"Native song",albumId:"native-container",trackId:"native-one",trackIndex:0}],
            {records:[{id:"native-container",tracks:[{id:"native-one",title:"Native song",artistIds:["native-artist"]}]}]}),"");
        compare(browse.page("songs").error,"");compare(browse.page("songs").items.length,1);
        compare(browse.page("songs").items[0].albumId,"native-container");
        verify(Array.isArray(browse.page("songs").items));verify(Array.isArray(browse.records));
        verify(Array.isArray(browse.records[0].tracks));compare(browse.records[0].tracks[0].artistIds[0],"native-artist");
        browse.activeKind="artists";var artistCall=last("artists");
        nativeSource.reply(artistCall.lane,artistCall.id,response(artistCall,[{id:"native-artist",artistId:"native-artist",kind:"artists",artist:"Native artist"}],
            {markets:[{code:"ZZ",title:"Global"},{code:"IN",title:"India"}]}),"");
        compare(browse.page("artists").error,"");verify(Array.isArray(browse.markets));compare(browse.markets[1].code,"IN");
    }
    function test_native_stale_selection_and_profile_replies_are_ignored() {
        browse.activeKind="songs";var old=last("songs");browse.select("songs","new-collection","");var current=last("songs");
        nativeSource.reply(old.lane,old.id,response(old,[{id:"old-song"}]),"");compare(browse.page("songs").items.length,0);
        nativeSource.reply(current.lane,current.id,response(current,[{id:"current-song"}]),"");compare(browse.page("songs").items[0].id,"current-song");
        browse.fetch("songs",false,true);old=last("songs");browse.profileGeneration++;
        nativeSource.reply(old.lane,old.id,response(old,[{id:"old-profile"}]),"");compare(browse.page("songs").items.length,0);
    }
    function test_negative_request_is_retryable() {
        // VideoTestSource always returns positive IDs; force rejection at the request port.
        browse.source=rejectingSource;browse.activeKind="songs";
        compare(browse.categoriesLoading,false);verify(browse.categoriesError.length>0);
        compare(browse.page("songs").loading,false);verify(browse.page("songs").error.length>0);
        rejectingSource.reject=false;browse.retry("songs");verify(browse.page("songs").loading);verify(last("songs").id>0);
        var call=last("songs");rejectingSource.completed(call.lane,call.id,response(call,[{id:"retried"}]),"");
        compare(browse.page("songs").error,"");compare(browse.page("songs").items[0].id,"retried");
    }
}
