import QtQuick
import QtTest
import Colosseum.Vinyl 1.0
import "../../qml/vinyl" as Vinyl

TestCase {
    id:test
    name:"VinylNativeMusicDiscover"
    width:1280;height:850;visible:true
    property var calls:[]
    property var cancelled:[]
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
        function recordSilent(entry) {}
    }
    Item {id:shellWallpaper}
    Component {
        id:liveWorldFactory
        Vinyl.VinylLiveWorld {
            width:1280;height:850;deferInitialRestore:true
            backdrop:shellWallpaper
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
        function onCancelled(lane) {test.cancelled=test.cancelled.concat([lane]);}
        function onRequested(lane,id,operation,value,options) {
            test.calls=test.calls.concat([{lane:lane,id:id,operation:operation,value:value,options:options}]);
        }
    }
    function init() {
        calls=[];cancelled=[];
        var picks=[];
        ["UCaaaaaaaaaaaaaaaaaaaaaa","UCbbbbbbbbbbbbbbbbbbbbbb","UCcccccccccccccccccccccc","UCdddddddddddddddddddddd","UCeeeeeeeeeeeeeeeeeeeeee"].forEach(function(id,index){
            picks.push({key:JSON.stringify(["youtube-music","artist",id]),kind:"artist",provider:"youtube-music",providerId:id,name:"Fixture artist "+index,image:"",mappingStatus:"unmapped"});
        });
        temporaryPreferences.setVinylTasteSelections(true,picks);
    }
    function chartCalls() {return calls.filter(call=>call.operation==="browseCatalogue");}
    function last() {return chartCalls().slice(-1)[0];}
    function taps() {return calls.filter(call=>call.operation==="resolveCatalogueItem");}
    function respond(call, result, error) {
        nativeSource.reply(call.lane,call.id,result,error || "");
    }
    function cards(browser) {
        browser.selectSource("Indian");
        var call=last();
        compare(call.options.list,"tamil:default");
        var song={id:"jiosaavn:songs:fixture",kind:"songs",catalogueCard:true,source:"jiosaavn",
            title:"Chart song",artist:"Fixture artist",cover:""};
        var unmatched={id:"jiosaavn:songs:unmatched",kind:"songs",catalogueCard:true,source:"jiosaavn",
            title:"Unmatched",artist:"Fixture artist",cover:""};
        respond(call,{version:1,kind:"songs",source:"jiosaavn",list:"tamil:default",items:[song,unmatched],
            nextCursor:{},complete:true,fetchedAt:"2026-11-04",stale:true});
        compare(browser.items.length,2);
        verify(browser.bannerText.indexOf("As of 2026-11-04")>=0);
        compare(taps().length,0);
        return song;
    }
    function match(song) {
        return {version:1,id:song.id,kind:song.kind,matched:true,
            record:{id:"catalogue-song-record",browseId:"ytm:track:abcdefghijk",type:"Song",origin:"youtube-music",
                title:song.title,artists:[song.artist],tracks:[
                    {id:"catalogue-song-track",videoId:"abcdefghijk",title:song.title,artists:[song.artist],seconds:120,available:true}
                ]}};
    }
    function test_hosted_shell_geometry_and_wallpaper() {
        var picks=temporaryPreferences.vinylTasteArtists;
        temporaryPreferences.setVinylTasteSelections(false,[]);
        var live=createTemporaryObject(liveWorldFactory,test);verify(live!==null);
        compare(live.canEnterVinyl,false);
        compare(live.chromeBackdrop,shellWallpaper);
        var shell=findChild(live,"vinylBrowseShell");verify(shell && shell.visible);
        temporaryPreferences.setVinylTasteSelections(true,picks);
        tryCompare(live,"canEnterVinyl",true);
        var tabs=findChild(live,"vinylFlowTabs");
        tabs.requestIndex(1);compare(live.page,"discover");
        verify(findChild(tabs,"vinylTab_discover")!==null);
        var discover=findChild(live,"vinylDiscoverPage");
        var browser=findChild(live,"vinylDiscoverBrowser");
        compare(live.worldScroll,shell.pageFlickable);
        compare(shell.contentBottomInset,0);
        compare(live.worldScroll.y+live.worldScroll.height,live.height);
        compare(live.worldScroll.contentY,0);
        tryVerify(function(){return tabs.mapToItem(live.worldScroll.contentItem,0,0).y===shell.boardTopPadding;});
        compare(discover.mapToItem(live.worldScroll.contentItem,0,0).y,
                shell.boardTopPadding+tabs.height+shell.boardSectionSpacing);
        compare(browser.activeFilterCount,0);
        compare(last().options.list,"chart");
        compare(shell.dockContentLeft,browser.contentLeft);
        compare(live.chromeBackdrop,shellWallpaper);
        live.destroy();wait(0);
    }
    function test_blank_tabs_resolve_song_and_retain_discover_on_back() {
        var live=createTemporaryObject(liveWorldFactory,test);verify(live!==null);
        tryCompare(live,"canEnterVinyl",true);
        ["songs","albums","artists"].forEach(function(kind){
            live.worldView.setTab(kind);compare(live.page,kind);
            compare(chartCalls().length,0);
            compare(findChild(live,"vinylDiscoverPage").visible,false);
        });
        live.worldView.setTab("home");compare(live.page,"home");
        verify(findChild(live,"vinylHomePage")!==null);
        live.worldView.setTab("discover");
        var browser=findChild(live,"vinylDiscoverBrowser");verify(browser!==null);
        compare(browser.posterVisualProfile,"gallery");compare(last().options.list,"chart");
        var song=cards(browser);
        compare(live.session.queue.length,0);
        browser.activateIndex(0);
        compare(taps().length,1);compare(taps()[0].value,song.id);compare(taps()[0].options.item.id,song.id);
        verify(findChild(live,"vinylStatus").text.indexOf(song.title)>=0);
        browser.activateIndex(1);
        compare(taps().length,1);compare(live.page,"discover");compare(live.session.queue.length,0);
        respond(taps()[0],match(song));
        compare(live.page,"song");compare(live.browsingSong,false);
        compare(live.session.queue.length,1);compare(live.session.playingIndex,0);
        compare(live.session.queue[0].track.id,"catalogue-song-track");
        tryVerify(function(){return calls.some(call=>call.lane==="play" && call.value==="catalogue-song-track");});
        var playCall=calls.filter(call=>call.lane==="play" && call.value==="catalogue-song-track").slice(-1)[0];
        respond(playCall,{},"YouTube unavailable");
        tryCompare(live.session,"error","YouTube unavailable");
        verify(live.goBack());compare(live.page,"discover");compare(live.worldView.currentTab,"discover");
        compare(browser.currentType,"songs");compare(browser.currentCatalogKey,"jiosaavn");compare(browser.items.length,2);
        var queueBefore=JSON.stringify(live.session.queue);
        browser.activateIndex(1);
        var miss=taps().slice(-1)[0];
        respond(miss,{version:1,id:miss.value,kind:"songs",matched:false,reason:"No reliable match"});
        compare(JSON.stringify(live.session.queue),queueBefore);
        compare(live.session.error,"YouTube unavailable");
        verify(findChild(live,"vinylStatus").text.indexOf("Couldn't find")>=0);
        verify(findChild(live,"vinylStatus").text.indexOf("Unmatched")>=0);
        var count=chartCalls().length;wait(0);compare(chartCalls().length,count);
        browser.activateIndex(0);
        var pending=taps().slice(-1)[0];
        var close=findChild(live,"vinylBarClose");verify(close && close.visible);
        close.triggered();
        verify(cancelled.includes("catalogue-item"));
        compare(live.session.queue.length,0);
        respond(pending,match(song));
        compare(live.session.queue.length,0);compare(live.page,"discover");
        live.destroy();wait(0);
    }
    function test_cancel_tap_on_leave_and_retry_transport_error() {
        var live=createTemporaryObject(liveWorldFactory,test);verify(live!==null);
        tryCompare(live,"canEnterVinyl",true);
        live.worldView.setTab("discover");
        var browser=findChild(live,"vinylDiscoverBrowser");verify(browser!==null);
        var song=cards(browser);
        browser.activateIndex(0);
        var stale=taps()[0];
        live.worldView.setTab("library");
        verify(cancelled.includes("catalogue-item"));
        respond(stale,match(song));
        compare(live.session.queue.length,0);compare(live.page,"library");
        live.worldView.setTab("discover");
        browser.activateIndex(0);
        compare(taps().length,2);
        respond(taps()[1],{},"Network unavailable");
        verify(findChild(live,"vinylStatus").text.indexOf("retry")>=0);
        browser.activateIndex(0);
        compare(taps().length,3);
        live.searchOpen=true;
        respond(taps()[2],match(song));
        compare(live.session.queue.length,0);
        live.destroy();wait(0);
    }
}