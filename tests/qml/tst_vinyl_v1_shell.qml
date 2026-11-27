import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id:test
    name:"VinylV1Shell"
    when:windowShown
    visible:true
    width:1280; height:700
    property var view:null
    property var gateData:null
    Rectangle { id:wallpaper; anchors.fill:parent; color:"#101015" }
    Component { id:shellFactory; Vinyl.VinylHouseShell { width:test.width; height:test.height; backdrop:wallpaper;contentBottomInset:80 } }
    Component {
        id:dataFactory
        QtObject {
            property var session:null
            property var personalLibrary:null
            property var recommendationService:null
            property bool usesRecommendationService:false
            property var heroSlides:[]
            property var crateItems:[]
            property var spinSongs:[]
            property var spinAlbums:[]
            property var spinArtists:[]
            property var v1HomeRows:[]
            property var libraryRows:[]
            property var picks:[{id:"one"},{id:"two"},{id:"three"},{id:"four"},{id:"five"}]
            property var homeRows:[]
            property var coldShelves:[]
            property var shelves:[]
            property var carouselSlides:[]
            property string activeTab:"home"
            property string lensTitle:"Everything"
            property var lens:({kind:"albums",genre:"",decade:"",country:"",language:""})
            property bool catalogueReachable:true
            property bool canEnterVinyl:true
            property int minimumArtistCount:5
            property bool tastePickerOpen:false
            property bool tasteGateLocked:false
            property bool pickerAnswered:true
            property string onboardingState:"answered"
            property string pickerQuery:""
            property var pickerDraft:[]
            property var savedPicks:[]
            property string artistSearchError:""
            property bool artistSearchLoading:false
            property bool artistSearchHasMore:false
            property bool discoveryLanding:false
            property bool onlineMode:false
            property bool discoveryLoading:false
            property string discoveryError:""
            property bool discoveryCached:false
            property string discoveryFetchedAt:""
            property bool cataloguePagingSupported:true
            property bool searchHasMore:false
            property bool searchLoading:false
            property string searchError:""
            property string homeStatus:"ready"
            property string homeError:""
            property var homeListSnapshot:null
            property var actions: []
            function recordAction(action, item) { actions = actions.concat([{action:action, item:item}]); }
            signal listRequested(string key,bool home)
            signal artistSearchFocusRequested()
            function selectTab(tab){activeTab=tab;}
            function artwork(value){return value;}
            function artistOptions(query){return [];}
            function filterOptions(key){return [];}
            function setKind(kind){lens=Object.assign({},lens,{kind:kind});}
            function row(key,home){return v1HomeRows.find(row=>home && row.key===key) || {key:key,title:key,items:[]};}
            function listItems(key){return [];}
            function activate(item){recordAction("activate",item);}
            function playHomeItem(item){recordAction("play",item);}
            function openHomeItem(item){recordAction("open",item);}
            function saveHomeItem(item){}
            function radioHomeItem(item){recordAction("radio",item);}
            function hideHomeItem(item){}
            function pinCrateItem(item,pinned){}
            function feedbackHomeItem(item,action){}
            function beginTasteEdit(){tastePickerOpen=true;}
            function togglePickerArtist(item){}
            function cancelTasteEdit(){tastePickerOpen=false;}
            function skipPicker(){}
            function answerPicker(){}
            function retryArtistSearch(){}
            function loadMoreArtistSearch(){}
            function openList(key,home){listRequested(key,home);}
        }
    }
    Component {
        id: floatingFooterFactory
        Item {
            id: footerHost
            anchors.fill: parent
            required property var shell
            property real hostBottomInset: 80
            property real dockBottomExtent: 80
            property alias playerBar: player
            Vinyl.VinylPageData { id: playback; frozen: true }
            Vinyl.VinylPlayerBar {
                id: player
                x: 260; y: footerHost.height - height - 18
                width: footerHost.width - x - 54
                page: "home"; pageData: playback; backdrop: wallpaper
                Component.onCompleted: playback.setFixture("thriller")
            }
            Vinyl.VinylScrollInsets {
                viewportHeight: footerHost.height
                hostBottomInset: footerHost.hostBottomInset
                dockBottomExtent: footerHost.dockBottomExtent
                playerBar: player
                browseShell: footerHost.shell
            }
        }
    }
    function card(kind,index){
        var id=kind+"-"+index;
        return {id:id,kind:kind,title:"Fixture "+index,artist:"Fixture artist",
            destination:kind==="artists" ? {kind:"artist", id:id}
                : kind==="albums" ? {kind:"album", id:id} : undefined,
            subtitle:"Owner metadata",reason:"Close to your city pop",
            cover:Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg")};
    }
    function cards(kind,count){var rows=[];for(var i=0;i<count;++i)rows.push(card(kind,i));return rows;}
    function initTestCase(){
        var kinds=["albums","songs","artists","albums"],slides=[];
        for(var i=0;i<4;++i)slides.push({kind:["topAlbum","topSong","topArtist","forYou"][i],
            item:card(kinds[i],"hero-"+i),sideItems:cards(kinds[i],4),kicker:i===3?"For you · picked today":"#1 this week"});
        var crate=[];for(i=0;i<16;++i)crate.push(card(["albums","artists","songs","mixes","radio"][i%5],i));
        var home=[];var keys=["mixes-radio","new-from-your-artists","albums-for-you"];for(i=0;i<keys.length;++i)home.push({key:"v1:"+keys[i],homeKey:keys[i],sectionId:"fixture-"+i,title:"Owner Home row "+i,items:cards("albums",8)});
        gateData=dataFactory.createObject(test,{heroSlides:slides,crateItems:crate,
            spinSongs:cards("songs",13),spinAlbums:cards("albums",9),spinArtists:cards("artists",9),v1HomeRows:home,
            libraryRows:[{key:"playlists",title:"Playlists",items:cards("playlists",8)},
                {key:"saved-albums",title:"Saved albums",items:cards("albums",8)},
                {key:"followed-artists",title:"Followed artists",items:cards("artists",8)}]});
        verify(gateData!==null);
        view=shellFactory.createObject(test,{worldData:gateData,runtimeReady:true,hostReady:true});
        verify(view!==null);verify(waitForRendering(view));
        tryVerify(function(){
            for(var n=0;n<8;++n){var tile=findChild(view,"vinylCrateTile_"+n);if(!tile||tile.height<=130)return false;}
            return true;
        });
        view.pageFlickable.contentY=0;
    }
    function init(){
        gateData.actions=[];
        view.resetRouteState("home");
        var spin=findChild(view,"vinylSpin");spin.view="songs";spin.page=0;
        view.pageFlickable.contentY=0;
        mouseMove(test,width-2,height-2);
        verify(waitForRendering(view));
        tryCompare(view.pageFlickable,"contentY",0);
    }
    function cleanupTestCase(){
        if(view){view.destroy();wait(0);view=null;}
        if(gateData){gateData.destroy();wait(0);gateData=null;}
    }
    function bounds(item,target){
        var at=item.mapToItem(target||test,0,0);
        return {x:at.x,y:at.y,width:item.width,height:item.height,right:at.x+item.width,bottom:at.y+item.height};
    }
    function clipBounds(){var box=bounds(view.pageFlickable);box.bottom=Math.min(box.bottom,height);box.right=Math.min(box.right,width);return box;}
    function fits(item){var box=bounds(item),clip=clipBounds();return item.visible && box.x>=clip.x-0.5 && box.y>=clip.y-0.5 && box.right<=clip.right+0.5 && box.bottom<=clip.bottom+0.5;}
    function test_01_firstScreenHasFullHeroAndFirstCrateRow(){
        var hero=findChild(view,"vinylHero"),crate=findChild(view,"vinylCrate"),spin=findChild(view,"vinylSpin");
        var header=findChild(spin,"vinylSpinSwitch").parent,tiles=[],kinds=[];
        for(var i=0;i<8;++i){var tile=findChild(crate,"vinylCrateTile_"+i);tiles.push(bounds(tile));kinds.push(tile.kind);}
        console.info("V1_SHELL_BOUNDS "+JSON.stringify({window:{width:view.Window.window.width,height:view.Window.window.height},
            viewport:clipBounds(),hero:bounds(hero),crate:bounds(crate),spinHeader:bounds(header),tiles:tiles}));
        compare(view.Window.window.width,1280);compare(view.Window.window.height,700);
        compare(hero.slideCount,4);compare(crate.count,16);compare(crate.columns,8);compare(crate.pageCount,2);
        for(var kind of ["albums","artists","songs","mixes","radio"])verify(kinds.indexOf(kind)>=0);
        verify(fits(hero),"Full Hero must fit the initial visible component viewport");
        verify(hero.height>=260,"Hero must be tall (>=260) at 1280x700, got "+hero.height);
        for(i=0;i<8;++i)verify(fits(findChild(crate,"vinylCrateTile_"+i)),"First-row Crate tile "+i+" including captions must fit");
        compare(tiles[0].y,tiles[7].y);
        compare(findChild(crate,"vinylCrateTile_8"),null,"The Crate is a single row (Hemanth, 2026-11-28)");
    }
    function test_homeScrollsUnderFloatingFooterAndEndsClearOfPlayer() {
        var footer = createTemporaryObject(floatingFooterFactory, test, {shell:view});
        verify(footer !== null);
        var bar = footer.playerBar, flick = view.pageFlickable;
        var lastRow = findChild(view, "vinylHomeRow_albums-for-you");
        verify(lastRow && lastRow.visible);
        verify(bar.visible && bar.hasPlayingSong);
        compare(bar.height, 76);
        compare(bar.y, 606);
        compare(bounds(flick).bottom, height, "The player and desktop dock must not clip Home");
        compare(view.contentEndPadding, height - bar.y + 14);
        verify(flick.contentHeight > flick.height);
        flick.contentY = flick.contentHeight - flick.height;
        tryCompare(flick, "atYEnd", true);
        verify(bounds(lastRow).bottom <= bar.y - 14, "The last Home row must scroll fully clear of the player");

        // Resize the docked footer without changing the Home viewport.
        footer.anchors.fill = null;
        footer.height = height;
        for (const width of [860, 760, 700, 699, 860]) {
            footer.width = width + bar.x + 54;
            compare(bar.width, width);
            compare(bar.y, width >= 700 ? 606 : 518);
            compare(bounds(flick).bottom, height);
            compare(view.contentEndPadding, height - bar.y + 14);
        }

        footer.dockBottomExtent = 0;
        footer.hostBottomInset = 56;
        compare(bounds(flick).bottom, height - 56, "Non-floating host navigation still reserves space");
        compare(view.contentEndPadding, height - 56 - bar.y + 14);
    }
    function test_02_heroThenTabsThenTabContent(){
        var hero=findChild(view,"vinylHero"),crate=findChild(view,"vinylCrate"),spin=findChild(view,"vinylSpin");
        var flow=view.flowTabs,target=view.pageFlickable.contentItem;
        for(var tab of ["home","discover","library"]){
            view.setTab(tab);tryCompare(view,"currentTab",tab);
            verify(waitForRendering(view));
            var destination=findChild(view,{home:"vinylHome",discover:"vinylDiscoverPage",library:"vinylLibrary"}[tab]);
            verify(destination && destination.visible);
            compare(findChild(view,"vinylHero"),hero);compare(findChild(view,"vinylCrate"),crate);compare(findChild(view,"vinylSpin"),spin);
            verify(hero.visible && flow.visible);
            var flowBounds=bounds(flow,target),destinationBounds=bounds(destination,target);
            verify(bounds(hero,target).bottom<=flowBounds.y);
            compare(crate.visible,tab==="home");compare(spin.visible,tab==="home");
            if(tab==="home"){
                var crateBounds=bounds(crate,target),spinBounds=bounds(spin,target);
                verify(crateBounds.y-flowBounds.bottom>=12,"The Crate header needs at least 12 px below the bar");
                verify(crateBounds.bottom<=spinBounds.y);
                verify(spinBounds.bottom<=bounds(destination.parent,target).y);
            }else{
                verify(flowBounds.bottom<=destinationBounds.y);
            }
        }
    }
    function test_03_spinPointerSwitchPreservesOuterScroll(){
        var spin=findChild(view,"vinylSpin"),flick=view.pageFlickable;
        var y=spin.mapToItem(flick.contentItem,0,0).y;
        flick.contentY=Math.round(Math.min(flick.contentHeight-flick.height,Math.max(0,y-80)));
        verify(waitForRendering(spin));var before=flick.contentY;verify(before>0);
        console.info("V1_SHELL_SCROLL "+JSON.stringify({phase:"before",view:spin.view,contentY:flick.contentY,
            contentHeight:flick.contentHeight,viewportHeight:flick.height,maxY:Math.max(0,flick.contentHeight-flick.height)}));
        for(var type of ["albums","artists","songs"]){
            var button=findChild(spin,"vinylSpinSwitch_"+type);verify(fits(button));mouseClick(button);
            tryCompare(spin,"view",type);compare(findChild(spin,"vinylSpinSwitch").view,type);
            verify(waitForRendering(spin));
            console.info("V1_SHELL_SCROLL "+JSON.stringify({phase:"after",view:type,contentY:flick.contentY,
                contentHeight:flick.contentHeight,viewportHeight:flick.height,maxY:Math.max(0,flick.contentHeight-flick.height)}));
            compare(flick.contentY,before);
        }
    }
    function test_04_unrelatedRowUpdatesPreserveCardsAndExposureFollowsHome(){
        var original=gateData.v1HomeRows;
        var shelf=findChild(view,"vinylHomeRow_mixes-radio");verify(shelf!==null);
        var tile=findChild(shelf,"vinylCard_v1:mixes-radio_0");verify(tile!==null);
        var updated=original.slice();
        updated[1]=Object.assign({},updated[1],{title:"Updated other row",items:cards("songs",8)});
        updated.push({key:"v1:deep-cuts",homeKey:"deep-cuts",title:"Deep cuts",items:cards("songs",8)});
        gateData.v1HomeRows=updated;
        tryVerify(function(){var added=findChild(view,"vinylHomeRow_deep-cuts");return added && added.visible && added.itemCount===8;});
        var song = findChild(view, "vinylCard_v1:deep-cuts_0");
        verify(song !== null);
        tryCompare(song, "height", song.width + 55);
        compare(song.captionTitle, song.item.title);
        tryCompare(song.exposureRail, "height", song.height + 6);
        compare(findChild(view,"vinylHomeRow_mixes-radio"),shelf,"Unchanged shelf must retain its object");
        compare(findChild(shelf,"vinylCard_v1:mixes-radio_0"),tile,"Unchanged card must retain its object");
        var sameRowItems=updated[0].items.slice();
        sameRowItems[0]=Object.assign({},sameRowItems[0],{title:"Same-row updated title"});
        updated=updated.slice();updated[0]=Object.assign({},updated[0],{items:sameRowItems});
        gateData.v1HomeRows=updated;
        compare(findChild(shelf,"vinylCard_v1:mixes-radio_0"),tile,"Same-row metadata must preserve the card object");
        tryCompare(tile,"captionTitle","Same-row updated title");
        compare(tile.item.id,original[0].items[0].id);
        compare(shelf.homePageVisible,true);
        view.setTab("library");tryCompare(shelf,"homePageVisible",false);
        view.setTab("home");tryCompare(shelf,"homePageVisible",true);
        view.contentVisible=false;tryCompare(shelf,"homePageVisible",false);
        view.contentVisible=true;tryCompare(shelf,"homePageVisible",true);
        gateData.tastePickerOpen=true;tryCompare(shelf,"homePageVisible",false);compare(shelf.visible,false);
        gateData.tastePickerOpen=false;tryCompare(shelf,"homePageVisible",true);
        gateData.v1HomeRows=original;
    }
    function test_05_wheelGlidesThroughHome() {
        var flick=view.pageFlickable;
        var tabsTop=view.flowTabs.mapToItem(flick.contentItem,0,0).y;
        for(var step of [{from:0,delta:168},{from:tabsTop-70,delta:168},
                         {from:tabsTop+70,delta:-168}]) {
            flick.contentY=step.from;
            verify(waitForRendering(view));
            mouseWheel(flick, flick.width/2, flick.height/2, 0,
                       step.delta>0 ? -120 : 120, Qt.NoButton, Qt.NoModifier, 0);
            tryCompare(flick,"contentY",step.from+step.delta);
        }
    }
    function test_06_wheelKeepsVisibleShelfSteadyDuringPublication_data() {
        return [{tag:"arrival-wheel-up", arrives:true, offset:240, delta:-168},
                {tag:"removal-wheel-down", arrives:false, offset:420, delta:168}];
    }
    function test_06_wheelKeepsVisibleShelfSteadyDuringPublication(data) {
        var flick=view.pageFlickable;
        var visibleShelf=findChild(view,"vinylHomeRow_albums-for-you");
        var earlyShelf=findChild(view,"vinylHomeRow_mixes-radio");
        var rows=gateData.v1HomeRows;
        var withoutMixes=rows.filter(row=>row.homeKey!=="mixes-radio");
        try {
            // Simulate a new provider result changing the rows on an already usable Home.
            gateData.v1HomeRows=data.arrives ? withoutMixes : rows;
            verify(waitForRendering(view));
            var shelfTop=visibleShelf.mapToItem(flick.contentItem,0,0).y;
            var start=shelfTop-data.offset;
            verify(start>0 && start<flick.contentHeight-flick.height);
            flick.contentY=start;
            verify(waitForRendering(view));
            compare(visibleShelf.mapToItem(flick,0,0).y,data.offset);
            mouseWheel(flick,flick.width/2,130,0,data.delta>0 ? -120 : 120,
                       Qt.NoButton,Qt.NoModifier,0);
            gateData.v1HomeRows=data.arrives ? rows : withoutMixes;
            tryCompare(earlyShelf,"visible",data.arrives);
            tryVerify(function() {
                return Math.abs(visibleShelf.mapToItem(flick.contentItem,0,0).y-shelfTop)>200;
            });
            var target=visibleShelf.mapToItem(flick.contentItem,0,0).y-data.offset+data.delta;
            // Flickable can round a half-pixel when it fixes up after an extent change.
            tryVerify(function() { return Math.abs(flick.contentY-target)<=1; },2000,
                      "Expected contentY near "+target+"; publication must not add a shelf to the wheel movement");
        } finally {
            gateData.v1HomeRows=rows;
        }
    }
    function scrollToCard(card) {
        verify(card && card.visible && card.enabled);
        var flick=view.pageFlickable;
        // Let pending layout and route parking settle before placing the card.
        verify(waitForRendering(view));
        tryVerify(function() {
            var top=card.mapToItem(flick.contentItem,0,0).y;
            return top+card.height<=flick.contentHeight+0.5;
        });
        view.parkPage(card.mapToItem(flick.contentItem,0,0).y-140);
        verify(waitForRendering(view));
        tryVerify(function() { return fits(card); }, 5000,
            "Card must fit the scroll viewport: "+card.objectName);
    }
    function test_homeArtistTileOpensArtistDestinationAndCentreStartsRadio_data() {
        return [
            {tag:"crate", cardId:"vinylCrateTile_1_card", primary:"open"},
            {tag:"spin", cardId:"vinylSpinTile_0", primary:"open"},
            {tag:"because-you-played", cardId:"vinylCard_v1:because-you-played_0", primary:"activate"},
            {tag:"rediscover", cardId:"vinylCard_v1:rediscover_0", primary:"activate"},
            {tag:"artists-like-yours", cardId:"vinylCard_v1:artists-like-yours_0", primary:"activate"},
            {tag:"see-all", cardId:"vinylListCard_0", primary:"activate"}
        ];
    }
    function test_homeArtistTileOpensArtistDestinationAndCentreStartsRadio(data) {
        var rows=gateData.v1HomeRows;
        var rowKey=data.tag==="see-all" ? "artists-like-yours" : data.tag;
        try {
            if(data.tag==="spin") findChild(view,"vinylSpin").view="artists";
            if(data.primary==="activate") {
                gateData.v1HomeRows=rows.concat([{key:"v1:"+rowKey, homeKey:rowKey,
                    title:"Artist fixture row", items:cards("artists",8)}]);
                if(data.tag==="see-all") view.showList("v1:"+rowKey,true);
            }
            var tile=findChild(view,data.cardId);
            scrollToCard(tile);
            var radio=findChild(tile,data.cardId+"_artistRadio");
            verify(radio && radio.enabled);
            compare(radio.accessibleName,"Artist radio");
            compare(radio.symbol,"play");
            tryCompare(radio,"opacity",0);

            mouseClick(tile,20,80);
            compare(gateData.actions.length,1,"Clicking the tile must not request radio");
            compare(gateData.actions[0].action,data.primary);
            compare(gateData.actions[0].item.id,tile.item.id);
            compare(gateData.actions[0].item.destination.kind,"artist");
            compare(gateData.actions[0].item.destination.id,tile.item.id);

            mouseMove(tile,20,80);
            tryCompare(radio,"opacity",1);
            mouseClick(radio);
            compare(gateData.actions.length,2,"Clicking the centre must not also open the tile");
            compare(gateData.actions[1].action,"radio");
            compare(gateData.actions[1].item.id,tile.item.id);

            mouseMove(test,width-2,height-2);
            tile.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(radio,"opacity",1);
            for(var key of [Qt.Key_Return,Qt.Key_Space]) keyClick(key);
            compare(gateData.actions.length,4);
            for(var i=2;i<4;++i) {
                compare(gateData.actions[i].action,data.primary);
                compare(gateData.actions[i].item.destination.kind,"artist");
            }
            keyClick(Qt.Key_Tab);
            tryCompare(radio,"activeFocus",true);
            keyClick(Qt.Key_Return);
            compare(gateData.actions.length,5);
            compare(gateData.actions[4].action,"radio");
        } finally {
            view.resetRouteState("home");
            gateData.v1HomeRows=rows;
        }
    }
    function test_homeAlbumShelfOpensDetailsAndCentrePlays_data() {
        return [{tag:"shelf", cardId:"vinylCard_v1:albums-for-you_0"},
                {tag:"see-all", cardId:"vinylListCard_0"}];
    }
    function test_homeAlbumShelfOpensDetailsAndCentrePlays(data) {
        try {
            if(data.tag==="see-all")view.showList("v1:albums-for-you",true);
            var tile=findChild(view,data.cardId);
            scrollToCard(tile);
            var play=findChild(tile,data.cardId+"_albumPlay");
            verify(play && play.enabled);
            compare(play.accessibleName,"Play album");
            tryCompare(play,"opacity",0);
            mouseClick(tile,20,80);
            mouseMove(test,width-2,height-2);
            tile.forceActiveFocus(Qt.TabFocusReason);
            for(var key of [Qt.Key_Return,Qt.Key_Space])keyClick(key);
            compare(gateData.actions.length,3);
            for(var action of gateData.actions){
                compare(action.action,"activate");
                compare(action.item.destination.kind,"album");
                compare(action.item.destination.id,tile.item.id);
            }
            tryCompare(play,"opacity",1);
            mouseClick(play);
            compare(gateData.actions.length,4);
            compare(gateData.actions[3].action,"play");
            compare(gateData.actions[3].item.id,tile.item.id);
            tile.forceActiveFocus(Qt.TabFocusReason);
            keyClick(Qt.Key_Tab);
            tryCompare(play,"activeFocus",true);
            keyClick(Qt.Key_Space);
            compare(gateData.actions.length,5);
            compare(gateData.actions[4].action,"play");
        } finally {
            view.resetRouteState("home");
        }
    }
    function test_homeArtistStationKeepsStationActivation() {
        var rows=gateData.v1HomeRows;
        var station={id:"station",kind:"radio",title:"Fixture station",cover:"",seedArtist:{id:"artist"},
            destination:{kind:"artist",id:"artist"}};
        try {
            gateData.v1HomeRows=rows.map(row=>row.homeKey==="mixes-radio"
                ? Object.assign({},row,{items:[station]}) : row);
            var tile=findChild(view,"vinylCard_v1:mixes-radio_0");
            scrollToCard(tile);
            compare(tile.captionDetail,"Keeps going");
            compare(findChild(tile,tile.objectName+"_artistRadio"),null);
            mouseClick(tile,20,80);
            compare(gateData.actions.length,1);
            compare(gateData.actions[0].action,"activate");
            compare(gateData.actions[0].item.kind,"radio");
            compare(gateData.actions[0].item.seedArtist.id,"artist");
        } finally {
            gateData.v1HomeRows=rows;
        }
    }
    function test_zz_lockedTasteGateShowsOnlyThePicker(){
        var hero=findChild(view,"vinylHero"),crate=findChild(view,"vinylCrate"),spin=findChild(view,"vinylSpin");
        var picker=findChild(view,"vinylTastePicker"),tabs=findChild(view,"vinylFlowTabs");
        verify(hero.visible&&crate.visible&&spin.visible&&tabs.visible);compare(picker.visible,false);
        gateData.canEnterVinyl=false;gateData.tasteGateLocked=true;gateData.tastePickerOpen=true;
        tryCompare(picker,"visible",true);
        compare(hero.visible,false);compare(crate.visible,false);compare(spin.visible,false);compare(tabs.visible,false);
        compare(findChild(view,"vinylCrateTasteCard"),null);
        var cancel=findChild(view,"vinylPickerCancel");verify(cancel===null||!cancel.visible);
        gateData.tasteGateLocked=false;gateData.canEnterVinyl=true;gateData.tastePickerOpen=false;
        tryCompare(picker,"visible",false);
        verify(hero.visible&&crate.visible&&spin.visible&&tabs.visible);
    }
}
