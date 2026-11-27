import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylLiveWorldPages"
    when:windowShown
    property var host
    property var world
    property var collection
    Component {id:storeFactory;QtObject {property var entries:[];signal changed();onEntriesChanged:changed();function setVinylArtistSaved(artist,saved){entries=entries.filter(function(e){return e.id!==artist.id;});if(saved)entries=entries.concat([{id:artist.id,type:"artist",title:artist.name,payload:{artistId:artist.id}}]);return true;}function items(world){return entries}function has(world,id){return entries.some(function(e){return e.id===id})}function add(world,entry){entries=entries.concat([entry]);return true;}function replaceItems(world,removeIds,replacements){entries=entries.filter(function(e){return removeIds.indexOf(e.id)<0;}).concat(replacements);return true;}}}
    QtObject { id:preferences
        property bool vinylTasteAnswered:false
        property var vinylTasteArtistIds:[]
        property string vinylPersistenceError:""
        signal vinylTasteChanged()
        function setVinylTaste(answered,ids){vinylTasteAnswered=answered;vinylTasteArtistIds=ids;vinylTasteChanged();return true;}
    }
    property var source
    Component {id:windowFactory;Window {width:1280;height:720;visible:true}}
    Component {id:sourceFactory;QtObject {
        property var artist: ({})
        property var artistAlbums: []
        property var artistSingles: []
        property var artistMedia: ({})
        property var artistIdentity: ({})
        property var artistArtwork: ({})
        property bool artistBusy:false
        property bool cached:false
        property string artistError:""
        property var lastArtistRef: ({})
        function openArtist(value){lastArtistRef=value;artist={name:value.name,id:value.id,relatedArtists:[{id:"UCedvOgsKFzcK3hA5taf3KoQ",name:"Shared name"}]};}
        function leaveArtist(){artist={};artistIdentity={};}
        property int serial:0
        property int importStarts:0
        property int importCancels:0
        property int identityDecisions:0
        property int lastImportId:0
        property var importPaths:[]
        property int cataloguePageVersion:0
        property var importOutcome: ({albums:[],errors:[]})
        function importFiles(paths) {return importOutcome;}
        function startImport(paths) {importStarts++;importPaths=paths.slice();lastImportId=++serial;return lastImportId;}
        function cancelImport() {importCancels++;}
        function decideImportIdentity(relationship,choice) {identityDecisions++;return true;}
        signal completed(string lane,int requestId,var data,string error)
        signal importCompleted(int id,var result)
        signal importProgress(int id,int processed,int total,string fileName)
        signal profileLeaving()
        signal profileEntered()
        signal checkpointRequested()
        signal shuttingDown()
        function request(lane,op,id){return ++serial}
        function cancel(lane: string){}
        function setAlbums(values){}
    }}
    function record(index,artistId) {return {id:"album-"+index,title:"Album "+("00"+index).slice(-3),origin:"local",artists:["Local Artist"],artistIds:[artistId || "artist-one"],year:2000+index%20,genre:index%2 ? "Folk" : "Jazz",tracks:[{id:"track-"+index,title:"Song "+index,artists:["Local Artist"],seconds:80,lyrics:[{text:"First supplied line"},{text:"Second supplied line"}],notes:{0:[{text:"Supplied note"}]},credits:[{role:"Performer",names:["Local Artist"]}]}]};}
    function seed(count) {var records=[];for(var i=0;i<count;i++)records.push({id:"album-"+i,payload:{album:record(i)}});collection.entries=records;world.session.refreshLibrary();wait(0);return records;}
    function item(name){
        var aliases={vinylTab_library:"vinylTab_Library",vinylTab_discover:"vinylTab_Discover"};
        if(name==="vinylHomeSeeAll_saved-albums")name="vinylShelfSeeAll_saved-albums";
        name=aliases[name] || name;
        // Runtime keeps separate hidden detail chrome; resolve the active shell chrome.
        var shell=world.browsingShell ? world.browseShell : world;
        var chrome=findChild(shell,name);
        return chrome || findChild(world,name);
    }
    function pixelAt(shot,point) {
        // grabImage returns device pixels on native Windows (150% here),
        // while mapToItem returns logical coordinates. Window-frame clipping
        // can shorten the image height, so derive both axes from its width.
        var scale=shot.width/world.width;
        return shot.pixel(Math.floor(point.x*scale),Math.floor(point.y*scale));
    }
    function probeVisible(pixel) {
        // Native fractional-DPI composition slightly dims this marker (248–252
        // rather than 255). Test presence, including in the negative control.
        return pixel.r>=0.96 && pixel.b>=0.96 && pixel.g<=0.02;
    }
    function init(){host=windowFactory.createObject(test);source=sourceFactory.createObject(test);var c=Qt.createComponent("../../qml/vinyl/VinylAppWorld.qml");compare(c.status,Component.Ready,c.errorString());world=c.createObject(host.contentItem,{width:1280,height:720});world.session.source=source;preferences.setVinylTaste(true,["artist:gate-a","artist:gate-b","artist:gate-c","artist:gate-d","artist:gate-e"]);world.worldData.preferencesStore=preferences;collection=storeFactory.createObject(test);world.session.collectionStore=collection;wait(0);}
    function cleanup(){
        // Detach/destroy the item tree while its Window and injected owners still
        // exist; do not overlap the next case with deferred Window teardown.
        world.destroy();
        wait(0);
        tryCompare(test, "world", null, 1000,
                   "World must finish destruction before Window teardown");
        world=null;
        host.close();host.destroy();
        tryCompare(test, "host", null, 1000,
                   "Window must finish destruction before the next case");
        source.destroy();collection.destroy();wait(0);source=null;collection=null;
    }
    QtObject { id:history
        property int revision:0
        property var recent:[]
        function list(scope){return recent;}
        function record(scope,query){recent=[query].concat(recent.filter(function(q){return q!==query;}));revision++;return recent;}
        function remove(scope,query){recent=recent.filter(function(q){return q!==query;});revision++;return recent;}
    }
    Component { id:clipProbeFactory; Rectangle { width:8; height:8; color:"#ff00ff"; z:1000 } }
    function test_onlineArtistSaveKeyStaysStable() {
        world.showArtist({id:"ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ",name:"Artist"});
        var key=world.artistData.canonicalArtistId;
        verify(world.artistData.toggleSaved());tryCompare(world.artistData,"saved",true);
        source.artistIdentity={mbid:"11111111-1111-4111-8111-111111111111"};
        compare(world.artistData.canonicalArtistId,key);compare(world.artistData.saved,true);
        verify(world.artistData.toggleSaved());verify(!collection.has("Vinyl",key));
    }
    function test_artistSearchPopup_data(){return [{tag:"desktop",width:1280},{tag:"narrow",width:640}];}
    function test_artistSearchPopup(data){
        host.width=data.width;world.width=data.width;seed(3);world.showArtist("Local Artist");
        host.requestActivate();tryCompare(host,"active",true);
        var page=item("vinylArtistPage"),origin=item("vinylArtistSearchButton");
        origin.forceActiveFocus(Qt.TabFocusReason);
        page.openSearchDialog(origin);
        var popup=item("vinylArtistSearchDialog"),input=item("vinylArtistSearchInput"),results=item("vinylArtistSearchResultsList");
        tryCompare(popup,"opened",true);tryCompare(results,"count",6);
        verify(popup.width<=popup.parent.width-40);verify(popup.height<=popup.parent.height-48);
        compare(popup.background.radius,24);compare(input.background.radius,14);
        verify(item("vinylArtistSearchClose")!==null);
        tryVerify(function(){return results.itemAtIndex(0)!==null;});
        verify(findChild(results.itemAtIndex(0),"vinylArtistSearchResult_0_art")!==null);
        input.forceActiveFocus();keyClick(Qt.Key_Down);tryCompare(results.itemAtIndex(0),"activeFocus",true);
        keyClick(Qt.Key_Up);tryCompare(input,"activeFocus",true);
        input.text="does-not-match";tryCompare(results,"count",0);verify(item("vinylArtistSearchEmpty").visible);
        keyClick(Qt.Key_Escape);tryCompare(popup,"opened",false);tryCompare(origin,"activeFocus",true);
        page.openSearchDialog(origin);tryCompare(popup,"opened",true);
        input.text="Album 000";tryCompare(results,"count",2);keyClick(Qt.Key_Down);
        tryCompare(results.itemAtIndex(0),"activeFocus",true);keyClick(Qt.Key_Return);tryCompare(popup,"opened",false);
        verify(world.session.browsing);source.completed("browse",world.session.browseRequest,record(0),"");
        tryCompare(world,"page","album");compare(world.session.queue.length,0);
    }
    function test_onlineArtistUsesArtworkFramesAndRotationStates(){
        compare(item("vinylArtistBackdropsButton"),null);
        compare(item("vinylArtistBackdropsDialog"),null);
        world.showArtist({id:"ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ",name:"Artist"});
        var hero=item("vinylArtistPage").heroItem;
        source.artistSingles=[{id:"single",title:"A release",kind:"single",origin:"youtube-music",browseId:"MPRE_single",artists:["Artist"],artistIds:["ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"],tracks:[]}];
        var hits=world.artistData.search("A release");compare(hits.length,1);compare(hits[0].type,"Single");
        compare(hero.eligiblePhotoCount,0);compare(hero.rotationMode,"fallback");
        var photos=[];for(var i=0;i<4;i++)photos.push({id:"photo-"+i,cover:"",name:"Photo "+i,era:"TheAudioDB",autoRotate:true});
        source.artistArtwork={photos:photos};tryCompare(hero,"eligiblePhotoCount",4);compare(hero.rotationMode,"multi-rotate");
        hero.setBackdrop("photo-2");compare(hero.rotationMode,"pinned");verify(!hero.slideshowEnabled);
        hero.setBackdrop("auto");verify(hero.slideshowEnabled);hero.advanceSlideshow();compare(hero.slideshowIndex,1);
        source.artistArtwork={photos:photos.slice(0,1)};tryCompare(hero,"rotationMode","static");verify(!hero.slideshowEnabled);
        source.artistArtwork={photos:[]};tryCompare(hero,"rotationMode","fallback");
        compare(String(hero.currentSynchronizedSource),"");
        source.artistArtwork={photos:[{id:"broken",name:"Broken photo",era:"Test",cover:"file:///missing-vinyl-artist-photo.jpg",autoRotate:true}]};
        tryCompare(hero,"eligiblePhotoCount",0);compare(hero.rotationMode,"fallback");
    }
    function test_artistSelectionMatchesWorldPills() {
        seed(2);world.showArtist("Local Artist");wait(0);
        var page=item("vinylArtistPage"),albums=item("vinylArtistTab_albums"),videos=item("vinylArtistTab_videos");
        verify(albums!==null && videos!==null);
        page.switchTab("albums",true);waitForRendering(world);
        var shared=item("vinylArtistSharedTabs");
        verify(shared.activeFocus);compare(shared.currentTab,"albums");
        compare(albums.activeState,true);compare(videos.activeState,false);
        tryCompare(albums,"color","#f0c44a");
        verify(albums.width>60 && albums.height>30,"Shared pill reserves space around the caption");
        page.switchTab("videos",true);waitForRendering(world);
        compare(shared.currentTab,"videos");compare(videos.activeState,true);compare(albums.activeState,false);
        tryCompare(videos,"color","#f0c44a");
    }
    function test_onlineArtistPlayLoadsAnAdmittedAlbumThroughExistingSession(){
        world.showArtist({id:"ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ",name:"Artist"});
        source.artistAlbums=[{id:"album-0",browseId:"MPRE_test",title:"Album",origin:"youtube-music",artists:["Artist"],artistIds:["ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ"],tracks:[]}];
        verify(world.artistData.playable);
        world.artistData.toggleDemoPlayback();verify(world.session.queueRequest>=0);
        source.completed("queue",world.session.queueRequest,record(0),"");
        tryCompare(world.session,"playingIndex",0);compare(world.session.playingTrack.id,"track-0");
    }
    function test_relatedArtistRetainsKnownProviderIdentity_data(){return [{tag:"shelf",full:false},{tag:"category",full:true}];}
    function test_relatedArtistRetainsKnownProviderIdentity(data){
        world.showArtist({id:"ytm:artist:UCfM3zsQsOnfWNUppiycmBuw",name:"Artist"});
        var page=item("vinylArtistPage"),row=world.artistData.media.relatedArtists[0];
        if(data.full){page.openCategory("related");wait(0);var card=item("vinylArtistFullCategory_0");verify(card!==null);card.cardRequested(row);}
        else item("vinylArtistRelatedSection").activated(row);
        compare(source.lastArtistRef.id,"ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ");
    }
    function test_liveWorldHasNoGlobalShortcutRow() {
        compare(item("vinylNavigation"),null);
        compare(item("vinylOpenFiles"),null);
        compare(item("vinylOpenFolder"),null);
        compare(item("vinylNowPlaying"),null);
        compare(item("vinylQueueButton"),null);
        compare(item("vinylCatalogueMode"),null);
        var top=item("vinylTopBar");
        verify(top!==null);
        compare(item("vinylLibraryHeader"),null);
        compare(world.worldScroll,world.browseShell.pageFlickable);
        verify(world.worldScroll.y>=top.y+top.height);
        verify(world.worldScroll.y<=top.y+top.height+18);
    }
    function verifyDetailTitlePage(name, backLabel, playerHeight) {
        const detail = item(name);
        verify(detail && detail.visible);
        compare(detail.mapToItem(host.contentItem, 0, 0).y, 0,
                name + " must start at the top of the window");
        compare(detail.width, host.width);
        compare(detail.height, host.height);
        const titleBar = item("titlePageBar");
        verify(titleBar && titleBar.visible);
        compare(titleBar.mapToItem(host.contentItem, 0, 0).y, 0);
        compare(titleBar.backItem.label, backLabel);
        verify(titleBar.backItem.visible);
        verify(!titleBar.solid);
        const top = item("vinylTopBar");
        verify(!top || !top.visible);
        const player = item("vinylPlayerBar");
        verify(player.visible && player.enabled);
        compare(player.height, playerHeight);
        return titleBar;
    }
    function test_fullWindowDetailTitlePages() {
        seed(2);
        world.showLibrary();
        world.session.playAlbum(record(0), 0);
        tryCompare(world.session, "playingIndex", 0);
        world.shellDockRight = 250;
        world.shellDockBottomExtent = 62;
        host.requestActivate();
        tryCompare(host, "active", true);

        world.showArtist({name:"Local Artist", id:"artist-one"});
        const artistBar = verifyDetailTitlePage("vinylArtistPage", "Vinyl", 76);
        const artistPage = item("vinylArtistPage");
        tryCompare(artistPage, "activeFocus", true);
        artistPage.contentY = 100;
        tryCompare(artistBar, "solid", true);
        verify(artistPage.contentEndPadding > item("vinylPlayerBar").height);
        verify(item("vinylPlayerBar").x > world.shellDockRight);
        mouseClick(item("vinylArtistMoreButton"));
        verify(item("vinylArtistTopOfPageAction").visible);
        mouseClick(item("vinylArtistTopOfPageAction"));
        tryCompare(artistBar, "solid", false);
        artistPage.contentY = 100;
        mouseClick(item("vinylBarOpenSong"));
        verifyDetailTitlePage("vinylSongPage", "Artist", 104);
        tryCompare(item("vinylSongPage"), "activeFocus", true);
        keyClick(Qt.Key_Escape);
        compare(world.page, "artist");
        tryCompare(artistBar, "solid", true);

        world.openAlbum("album-1");
        source.completed("browse", world.session.browseRequest, record(1), "");
        tryCompare(world, "page", "album");
        const albumBar = verifyDetailTitlePage("vinylAlbumPage", "Artist", 76);
        item("vinylAlbumPage").contentY = 100;
        tryCompare(albumBar, "solid", true);
        verify(item("vinylAlbumPage").contentEndPadding > item("vinylPlayerBar").height);

        world.showBrowseSong(record(1), 0);
        const songBar = verifyDetailTitlePage("vinylSongPage", "Album", 104);
        const song = item("vinylSongPage");
        verify(song.y + song.viewportHeight <= item("vinylPlayerBar").y);
        tryCompare(song, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(songBar.backItem, "automationActiveFocus", true);
        keyClick(Qt.Key_Return);
        compare(world.page, "album");
        tryCompare(albumBar, "solid", true);
        mouseClick(albumBar.backItem);
        compare(world.page, "artist");
        tryCompare(artistBar, "solid", true);
        mouseClick(findChild(artistBar, "titleBarSystemMenu"));
        tryCompare(artistBar, "menuOpen", true);
        keyClick(Qt.Key_Escape);
        tryCompare(artistBar, "menuOpen", false);
        compare(world.page, "artist");
        mouseClick(findChild(artistBar, "titleBarSearch"));
        tryCompare(world, "searchOpen", true);
        world.closeSearch();
        tryCompare(world, "searchOpen", false);
        mouseClick(artistBar.backItem);
        compare(world.page, "library");
        verify(item("vinylTopBar").visible);
        compare(world.session.playingTrack.id, "track-0");
    }
    function test_asyncImportControlsProgressCancelAndAlbumAwareStatus() {
        var fileDialog=item("vinylOpenFilesDialog");
        verify(item("vinylOpenFolderDialog")!==null);
        verify(fileDialog.nameFilters[0].indexOf("*.ape")>=0 && fileDialog.nameFilters[0].indexOf("*.m4b")>=0);
        world.importLocalFiles(["file:///music/Album"]);
        compare(source.importStarts,1);
        compare(source.importPaths,["file:///music/Album"]);
        tryCompare(world,"importing",true);
        compare(world.page,"library");
        source.importProgress(source.lastImportId,2,4,"track.flac");
        tryCompare(world.session,"importProcessed",2);
        tryVerify(function(){return item("vinylStatus").text.indexOf("2 of 4 files")>=0;});
        var firstId=source.lastImportId;
        source.importCompleted(firstId,{importedTracks:7,albums:[record(0),record(1)],errors:[]});
        tryCompare(world,"importing",false);
        tryVerify(function(){return world.importStatus.indexOf("7 tracks")>=0 && world.importStatus.indexOf("2 albums")>=0;});

        world.importLocalFiles(["file:///music/Album"]);
        tryCompare(source,"importStarts",2);
        var identityRequest=source.lastImportId;
        source.importCompleted(identityRequest,{importedTracks:0,albums:[],errors:[],identityCeremonies:[{relationship:"relationship-1"}]});
        tryCompare(world,"importing",false);
        verify(world.importStatus.indexOf("Choose an identity option")>=0);
        verify(world.session.decideImportIdentity("relationship-1","existing"));
        tryCompare(source,"importStarts",3);
        compare(source.importPaths,["file:///music/Album"]);
        tryCompare(world,"importing",true);
        source.importCompleted(source.lastImportId,{importedTracks:3,albums:[record(2)],errors:[]});
        tryCompare(world,"importing",false);
        tryVerify(function(){return world.importStatus.indexOf("3 tracks")>=0 && world.importStatus.indexOf("1 album")>=0;});

        world.importLocalFiles(["/cancel-me"]);
        tryCompare(world,"importing",true);
        var cancelButton=item("vinylCancelImport");
        verify(cancelButton.visible && cancelButton.enabled && cancelButton.width>0 && cancelButton.height>0);
        verify(cancelButton.activate(Qt.MouseFocusReason));
        tryCompare(world,"importing",false);
        compare(source.importCancels,1);
        compare(world.importStatus,"Import cancelled. Your library was kept.");
        tryCompare(item("vinylStatus"),"text","Import cancelled. Your library was kept.");

        world.importLocalFiles(["/source-replaced"]);
        tryCompare(world,"importing",true);
        var replacement=sourceFactory.createObject(test);
        world.session.source=replacement;
        tryCompare(world,"importing",false);
        compare(source.importCancels,2);
        world.session.source=source;
        replacement.destroy();
    }
    function test_dockedTabsClipScrollingTaste_data() {
        return [{tag:"narrow",width:1024},{tag:"wide",width:1280}];
    }
    function test_dockedTabsClipScrollingTaste(data) {
        host.width=data.width;world.width=data.width;
        seed(1);world.worldData.resetPicker();world.worldView.setTab("home");
        // (b) FIXED-TEST: 051a87f14 moved Vinyl onto WorldPage's glass dock.
        // Content deliberately scrolls under it (contentBackdrop/contentTrack);
        // the dock must remain visible and receive clicks, not clip that content.
        // HouseShell parks via callLater after picker/layout changes.
        wait(50);
        var heading=item("vinylPickerQuestion"),tabs=world.worldView.dockedTabBar;
        var probe=clipProbeFactory.createObject(heading,{x:4,y:4});
        var searchY=heading.mapToItem(world.worldScroll.contentItem,0,0).y;
        var point,shot;
        world.worldScroll.contentY=searchY-10;
        waitForRendering(world);verify(tabs.visible,"Glass dock is visible over scrolling picker content");
        var tab=findChild(tabs,"vinylTab_Discover");
        verify(tab!==null && tab.visible);
        var under=tab.mapToItem(heading,tab.width/2,tab.height/2);
        verify(under.x>=0 && under.x<heading.width && under.y>=0 && under.y<heading.height,
               "Dock tab centre overlaps the scrolling picker heading");
        var catcher=Qt.createQmlObject('import QtQuick; MouseArea { width:16; height:16; property int clicks:0; onClicked: clicks++ }',heading);
        catcher.x=under.x-8;catcher.y=under.y-8;
        // Negative control: the same picker-side hit target receives clicks below the dock.
        world.worldScroll.contentY=searchY-world.worldView.dockedInset-10;
        waitForRendering(world);
        mouseClick(catcher,catcher.width/2,catcher.height/2);
        compare(catcher.clicks,1,"Picker-side hit target works when unobscured");
        catcher.clicks=0;
        world.worldScroll.contentY=searchY-10;
        waitForRendering(world);verify(tabs.visible);
        mouseClick(tab,tab.width/2,tab.height/2);
        compare(world.worldView.currentTab,"discover","Dock tab receives the centre click and switches tabs");
        compare(catcher.clicks,0,"Dock click must not reach picker content underneath");
        catcher.destroy();
        world.worldView.setTab("home");wait(50);
        // The same content must paint and remain interactive below that strip.
        world.worldScroll.contentY=searchY-world.worldView.dockedInset-10;
        waitForRendering(world);
        point=probe.mapToItem(world,4,4);shot=grabImage(world);
        verify(probeVisible(pixelAt(shot,point)),"Unclipped probe must remain visible");
        probe.destroy();
        world.worldView.parkPage(Math.max(0,world.worldView.contentTop-world.worldView.dockedInset));wait(0);
        world.enterDiscoverSearch(true,false);wait(0);
        // 411919a5c restored dedicated inline artist search during taste editing.
        verify(!world.searchOpen);compare(world.searchPage,null);
        var search=item("vinylPickerArtistSearch");
        tryCompare(search,"activeFocus",true);
        mouseClick(search,search.width/2,search.height/2);
        keyClick(Qt.Key_A);tryCompare(world.worldData,"pickerQuery","a",1000);
        var done=item("vinylPickerDone");
        var bottom=done.mapToItem(world,0,done.height);
        verify(bottom.y<=world.worldScroll.y+world.worldScroll.height);
        mouseClick(done);verify(world.worldData.pickerAnswered);
    }
    function test_worldTabInitialContentBelowStickyStrip_data() {
        return [{tag:"narrow",width:1024},{tag:"wide",width:1280}];
    }
    function test_worldTabInitialContentBelowStickyStrip(data) {
        host.width=data.width;world.width=data.width;seed(8);
        world.worldData.resetPicker();
        var cases=[{tab:"home",name:"vinylPickerQuestion"},
                   {tab:"discover",name:"vinylDiscoverHeadingTitle"},
                   {tab:"explore",name:"vinylExplorePage"},
                   {tab:"library",name:"vinylLibraryRow_saved-albums"}];
        for(var i=0;i<cases.length;i++) {
            // Exercise a fresh tab entry, rather than the saved startup carousel offset.
            world.worldView.tabOffsets={};
            world.worldView.tabRoutes={};
            world.worldView.setTab(cases[i].tab);wait(0);
            var target=item(cases[i].name);verify(target && target.visible);
            var top=target.mapToItem(world,0,0).y;
            var dock=world.worldView.dockedTabBar;
            var inset=dock.visible ? dock.y+dock.height-world.worldScroll.y : 0;
            verify(top>=world.worldScroll.y+inset-1,cases[i].tab+" initial content clears sticky tabs: top="+top+" scrollY="+world.worldScroll.y+" inset="+inset+" contentY="+world.worldScroll.contentY+" contentTop="+world.worldView.contentTop);
            verify(top+8<world.worldScroll.y+world.worldScroll.height,cases[i].tab+" initial content is above the fold");
            var probe=clipProbeFactory.createObject(target,{x:4,y:4});
            waitForRendering(world);
            var point=probe.mapToItem(world,4,4);
            var shot=grabImage(world);
            verify(probeVisible(pixelAt(shot,point)),cases[i].tab+" content probe must remain visible");
            probe.destroy();
        }
    }
    function test_decadePopupKeyboardAndFocus_data() {
        var rows=[];
        [1280,1024].forEach(function(width){
            ["escape","selection","keyboardSelection","outside"].forEach(function(close){
                rows.push({tag:width+"-"+close,width:width,close:close});
            });
        });
        rows.push({tag:"1024-bottom",width:1024,height:640,close:"escape"});
        return rows;
    }
    function test_decadePopupKeyboardAndFocus(data) {
        host.width=data.width;world.width=data.width;
        host.height=data.height || 720;world.height=host.height;
        // bb346e35a replaced local decade lenses with the shared catalogue browser.
        // Exercise its current Genre picker, retaining keyboard/close/focus coverage.
        world.worldView.setTab("discover");wait(0);
        var browser=item("vinylDiscoverBrowser"),picker=item("discoverFilterPicker");
        var trigger=item("vinylDiscoverFilterPickerPill");
        compare(item("vinylFilter_decade"),null);
        host.requestActivate();tryCompare(host,"active",true);
        world.pushRoute();waitForRendering(world);
        trigger.forceActiveFocus();
        picker.openPopup(trigger);tryCompare(picker,"open",true);
        verify(picker.options.length>10);
        var popup=item("vinylDiscoverFilterPickerPopup"),options=item("vinylDiscoverFilterPickerList");
        tryCompare(options,"activeFocus",true);
        tryCompare(options,"count",picker.options.length);
        verify(popup.width>0 && popup.height>0);
        var popupTop=popup.contentItem.mapToItem(world,0,0).y-popup.topPadding;
        verify(popupTop>=0 && popupTop+popup.height<=world.height+1,"Filter popup fits the host");
        if(data.close==="keyboardSelection") {keyClick(Qt.Key_Down);keyClick(Qt.Key_Return);compare(picker.currentKey,picker.options[1].key);}
        else if(data.close==="selection") {
            var lastIndex=picker.options.length-1;
            options.positionViewAtIndex(lastIndex,ListView.Contain);waitForRendering(world);
            var last=findChild(options.contentItem,"vinylDiscoverFilterPickerOption_"+lastIndex);verify(last!==null);
            mouseClick(last);compare(picker.currentKey,picker.options[lastIndex].key);
        }
        else if(data.close==="outside") {mouseClick(world,world.width-10,world.worldScroll.y+10);}
        else keyClick(Qt.Key_Escape);
        tryCompare(picker,"open",false);
        compare(world.page,"discover");compare(world.routeStack.length,1);
        tryCompare(trigger,"activeFocus",true);
        picker.openPopup(trigger);tryCompare(picker,"open",true);
        tryCompare(options,"currentIndex",picker.selectedOptionIndex());
        if(data.width===1280 && data.close==="escape") {
            host.width=1024;world.width=1024;wait(0);
            tryCompare(picker,"open",false,1000,"Resize dismisses the anchored popup");
            picker.openPopup(trigger);tryCompare(picker,"open",true);
        }
        keyClick(Qt.Key_Escape);tryCompare(picker,"open",false);
        tryCompare(trigger,"activeFocus",true);
    }
    function test_deviceAccountRelay_data(){return [{tag:"wide",width:1280},{tag:"narrow",width:1024}];}
    function test_deviceAccountRelay(data){
        host.width=data.width;world.width=data.width;wait(0);
        item("vinylTopBar").accountController={mode:"localOnly",username:"",accountFeaturesAvailable:true};
        // The injected Device entry changes the Row's width and child positions.
        // Waiting on the unrelated contentItem does not polish this positioner.
        var chrome=item("topBarSystemChrome");
        verify(waitForPolish(chrome,1000));
        verify(typeof world.accountClicked === "function", "Device button needs a shell account relay");
        var count=0,right=0,bottom=0;
        world.accountClicked.connect(function(x,y){count++;right=x;bottom=y;});
        var button=item("colosseumTopbarAccountButton");
        var input=item("colosseumTopbarAccountInput");
        verify(button && input && input.visible);
        var anchor=button.mapToItem(null,button.width,button.height);
        var point=input.mapToItem(chrome,input.width/2,input.height/2);
        compare(chrome.childAt(point.x,point.y),button,"Pointer must hit Device, not adjacent chrome");
        mouseClick(input,input.width/2,input.height/2);wait(0);
        compare(count,1);compare(right,anchor.x);compare(bottom,anchor.y);
    }
    function test_unavailableAccountOwnerHidesDeviceEntry(){
        item("vinylTopBar").accountController=null;
        verify(!item("colosseumTopbarAccountInput").visible);
    }
    function test_sharedSearchRecentListCanRemoveAndTracksProfileClosure(){
        seed(2);verify(world.worldData.canEnterVinyl);world.worldView.setTab("library");wait(0);
        history.recent=[];history.revision++;world.worldData.searchHistoryStore=history;
        item("vinylTopBar").searchClicked();tryVerify(function(){return world.searchOpen && world.searchPage!==null;});
        var page=world.searchPage,input=item("searchSurfaceInput");
        input.forceActiveFocus();keyClick(Qt.Key_Q);keyClick(Qt.Key_A);keyClick(Qt.Key_Return);wait(0);
        tryVerify(function(){return history.recent.length===1 && history.recent[0]==="qa";});
        compare(page.historyScope(),"vinyl");compare(page.recentCount,1);compare(page.recent[0],"qa");
        page.removeRecent("qa");tryVerify(function(){return page.recentCount===0;});
        history.record("vinyl","profile entry");page.loadRecent();compare(page.recentCount,1);
        world.worldData.searchHistoryStore=null;tryCompare(page,"recentCount",0);
    }
    function test_albumActionsHaveGeometryAndEnqueue_data(){
        return [{tag:"wide",w:1280,h:720},{tag:"narrow",w:1024,h:640}];
    }
    function test_albumActionsHaveGeometryAndEnqueue(data){
        host.width=data.w;host.height=data.h;world.width=data.w;world.height=data.h;
        seed(1);
        var album=record(0);
        world.session.shownEdition=world.session.normalize(album);
        world.page="album";wait(0);
        var page=item("vinylAlbumPage");
        var names=["Play","Shuffle","Save","Queue"];
        for(var i=0;i<names.length;i++){
            var control=item("vinylAlbum"+names[i]);
            verify(control!==null && control.visible && control.enabled);
            verify(isFinite(control.width) && control.width>0,names[i]+" needs positive width");
            verify(isFinite(control.height) && control.height>0,names[i]+" needs positive height");
            // Large sleeve art can put actions below the fold. The album's
            // Flickable must let pointer users bring each action into view.
            var localCenter=control.mapToItem(page,control.width/2,control.height/2);
            if(localCenter.y>page.height-24) {page.contentY+=localCenter.y-page.height+24;wait(0);}
            var center=control.mapToItem(world,control.width/2,control.height/2);
            verify(center.x>=0 && center.x<world.width && center.y>=0 && center.y<world.height,
                   names[i]+" center must fit the viewport");
        }
        verify(isFinite(page.coverSize) && page.coverSize>=72,"Cover size must stay finite");
        var cover=item("vinylAlbumCover");verify(cover.width>0 && cover.height>0);
        // A real pointer activation must append, not just expose a callable API.
        world.session.playAlbum(album,0);wait(0);
        var edition=world.session.playingEditionId,disc=world.session.playingDisc;
        var index=world.session.playingIndex,count=world.session.queue.length;
        var queueControl=item("vinylAlbumQueue");
        var queueCenter=queueControl.mapToItem(page,queueControl.width/2,queueControl.height/2);
        if(queueCenter.y>page.height-24){page.contentY+=queueCenter.y-page.height+24;wait(0);}
        mouseClick(queueControl,queueControl.width/2,queueControl.height/2);wait(0);
        compare(world.session.queue.length,count+1);
        compare(world.session.playingEditionId,edition);
        compare(world.session.playingDisc,disc);compare(world.session.playingIndex,index);
    }
    function test_emptyDiscoverHasOneTabBarAndNoEmptyShelf(){
        seed(0);world.worldView.setTab("discover");world.worldData.setOnlineMode(false);wait(0);
        // bb346e35a: Discover is now the shared catalogue wall, not local shelves.
        var flowing=item("vinylFlowTabs"),docked=world.worldView.dockedTabBar;
        var browser=item("vinylDiscoverBrowser");verify(browser!==null);
        browser.loading=false;browser.items=[];wait(0);
        compare(Number(flowing.visible)+Number(docked.visible),1,"Exactly one tab bar can be painted");
        compare(browser.items.length,0);verify(browser.emptyMessage.length>0);
        verify(item("vinylShelf_local-albums")===null,"No empty legacy shelf/See all");
        verify(item("vinylDiscoverHeadingTitle").visible);
        world.worldScroll.contentY=0;wait(0);verify(flowing.visible);
        seed(1);wait(0);compare(browser.items.length,0,"Local imports do not populate catalogue charts");
    }
    function test_pointerSeeAllThenKeyboardBack(){
        seed(8);verify(world.worldData.canEnterVinyl);world.worldView.setTab("home");wait(0);
        world.worldView.setTab("library");wait(0);
        var more=item("vinylHomeSeeAll_saved-albums");verify(more!==null);verify(more.visible);
        more.forceActiveFocus(Qt.TabFocusReason);mouseClick(more);wait(0);compare(world.page,"library");
        var list=item("vinylListPage");var focus=host.activeFocusItem;
        verify(focus!==null && focus.visible,"Opening a list must move focus away from its hidden origin");
        compare(focus,world.worldView,"Pointer navigation starts at a neutral origin without selecting a card");
        keyClick(Qt.Key_Down);wait(0);focus=host.activeFocusItem;
        var ancestor=focus,inList=false;while(ancestor){if(ancestor===list){inList=true;break;}ancestor=ancestor.parent;}
        verify(inList,"First keyboard navigation enters the list");
        keyClick(Qt.Key_Escape);wait(0);compare(world.page,"library");verify(more.activeFocus,"Back returns focus to original See all");
    }
    function test_emptySingleLongRefreshed(){
        seed(0);world.worldView.setTab("library");world.worldData.openList("library",false);wait(0);
        compare(world.worldData.listItems("library").length,0);
        seed(1);compare(world.worldData.listItems("library").length,1);
        seed(80);compare(world.worldData.listItems("library").length,80);
        var list=item("vinylListPage");list.currentIndex=30;list.selectedId="album-30";
        collection.entries=collection.entries.slice().reverse();wait(0);
        compare(list.row.items[list.currentIndex].id,"album-30");
        world.worldData.query="Album 030";compare(world.worldData.listItems("library").length,1);
        world.worldData.query="missing";compare(world.worldData.listItems("library").length,0);
    }
    function test_canonicalArtistsAndTaste(){
        preferences.setVinylTaste(false,[]);
        var ids=["artist-one","artist-two","artist-three","artist-four","artist-five"];
        world.session.library=ids.map(function(id,index){return record(index,id);});wait(0);
        var artists=world.worldData.artistOptions("");compare(artists.length,5);
        compare(new Set(artists.map(function(artist){return artist.id;})).size,5);
        var selectedArtist=artists.find(function(artist){return artist.id==="artist-two";});
        world.worldData.togglePickerArtist(selectedArtist);
        compare(world.worldData.pickerDraft.length,1);
        verify(!world.worldData.answerPicker());
        compare(world.worldData.picks.length,0);verify(!world.worldData.canEnterVinyl);
        artists.filter(function(artist){return artist.id!=="artist-two";})
            .forEach(function(artist){world.worldData.togglePickerArtist(artist);});
        compare(world.worldData.pickerDraft.length,5);
        verify(world.worldData.answerPicker());compare(world.worldData.picks.length,5);
        verify(world.worldData.canEnterVinyl);
        world.activateItem(artists.find(function(artist){return artist.id==="artist-two";}));
        compare(world.page,"artist");compare(world.artistData.artistId,"artist-two");
    }
    function test_nestedBackRetainsLibraryAndArtist(){
        seed(40);verify(world.worldData.canEnterVinyl);world.worldView.setTab("discover");world.worldData.setOnlineMode(false);world.worldData.setLens("genre","Jazz");world.worldData.openList("local-albums",false);wait(0);
        world.worldScroll.contentY=world.worldView.contentTop+100;var y=world.worldScroll.contentY;
        world.showArtist({name:"Local Artist",id:"artist-one"});world.artistData.activeTab="singles";
        world.openAlbum("album-2");source.completed("browse",world.session.browseRequest,record(2),"");compare(world.page,"album");
        world.showBrowseSong(record(2),0);compare(world.page,"song");verify(world.session.playingIndex<0,"Browsing a song must not start playback");
        verify(world.goBack());compare(world.page,"album");verify(world.goBack());compare(world.page,"artist");compare(world.artistData.activeTab,"singles");
        verify(world.goBack());compare(world.page,"discover");compare(world.worldView.listKey,"local-albums");compare(world.worldData.lens.genre,"Jazz");wait(0);compare(world.worldScroll.contentY,y);
        verify(world.goBack());compare(world.worldView.listKey,"");
    }
    function test_browsedAlbumBackRestoresOriginal(){
        seed(3);world.session.shownEdition=world.session.normalize(record(0));world.page="album";
        world.openAlbum("album-1");source.completed("browse",world.session.browseRequest,record(1),"");compare(world.session.shownAlbumId,"album-1");
        world.goBack();compare(world.session.shownAlbumId,"album-0");
    }
    function test_playbackIdentityUnaffectedByBrowse(){
        seed(3);world.session.playAlbum(record(0),0);
        world.showBrowseSong(record(2),0);compare(item("vinylSongPage").songTitle,"Song 2");compare(world.session.playingTrack.title,"Song 0");
        world.showSong();compare(item("vinylSongPage").songTitle,"Song 0");world.goBack();compare(item("vinylSongPage").songTitle,"Song 2");
    }
    function test_cardKeyboardAndResize(){
        seed(40);world.worldView.setTab("library");world.worldData.openList("library",false);wait(0);
        world.worldScroll.contentY=world.worldView.contentTop;wait(0);
        var list=item("vinylListPage");list.focusCurrent();wait(0);keyClick(Qt.Key_Right);wait(0);compare(list.currentIndex,1);
        for(var i=0;i<8;i++)keyClick(Qt.Key_Down);wait(0);verify(list.currentIndex>1);verify(world.worldScroll.contentY>world.worldView.contentTop);
        world.width=900;host.width=900;wait(0);var card=findChild(list,"vinylListCard_0");verify(card.width>=100);verify(card.width<220);
    }
    function test_editionAndRepeatedTrackDetails(){
        var a=record(0);var t=a.tracks[0];
        a.editions=[{id:"standard",title:"Standard",tracks:[t]},{id:"expanded",title:"Expanded",tracks:[Object.assign({},t,{title:"Occurrence one",disc:1}),Object.assign({},t,{title:"Occurrence two",disc:2})]}];
        collection.entries=[{id:a.id,payload:{album:a}}];world.session.refreshLibrary();
        world.session.shownEdition=world.session.normalize(a,a.editions[1]);world.session.shownDisc=1;world.page="album";
        item("vinylAlbumPage").songRequested(0);var song=item("vinylSongPage");
        compare(song.songTitle,"Occurrence two");compare(song.pageData.playingEditionId,"expanded");compare(song.pageData.trackIndex,1);
        song.playRequested();compare(world.session.playingEditionId,"expanded");compare(world.session.playingDisc,1);compare(world.session.playingTrack.title,"Occurrence two");
    }
    function test_rapidDetailsTeardown(){
        seed(2);
        for(var i=0;i<12;i++){world.showBrowseSong(record(i%2),0);item("vinylSongPage").pageData.lyricsShown=true;world.goBack();}
        wait(0);compare(world.page,"home");
    }
    function test_browsedUntimedNotesHaveVisibleFallback(){
        seed(1);world.showBrowseSong(record(0),0);var song=item("vinylSongPage");
        verify(!song.pageData.isCurrent);compare(song.pageData.currentLineIndex,-1);
        item("vinylBarNotes").triggered();wait(0);
        compare(song.pageData.activeNoteLine,0,"Untimed or non-playing browse must use the supplied notes fallback");
        var notes=item("vinylNotesColumn");verify(notes.visible);compare(notes.noteCount,1);
        compare(notes.shownNotes[0].text,"Supplied note");
    }
    function test_canonicalTabTargetAlwaysVisible(){
        seed(0);world.worldView.setTab("discover");wait(0);
        var target=item("vinylTab_library");verify(target!==null);tryVerify(function(){return item("vinylTab_library").visible;},1000,"Canonical action must resolve the visible bar");target=item("vinylTab_library");
        var bounds=target.mapToItem(world,0,0);verify(bounds.y>=world.worldScroll.y);
        mouseClick(target);compare(world.page,"library");
        world.worldScroll.contentY=0;wait(0);target=item("vinylTab_discover");verify(target.visible);mouseClick(target);compare(world.page,"discover");
    }
    function test_suppliedDetailsWideAndNarrow(){
        seed(1);world.showBrowseSong(record(0),0);var song=item("vinylSongPage");compare(song.pageData.lyrics.length,2);compare(song.pageData.playingCredits[0].names[0],"Local Artist");
        song.pageData.lyricsShown=true;wait(0);verify(item("vinylLyricLine_0").opacity>0);
        world.width=640;host.width=640;wait(0);verify(song.stackedReaders);verify(item("vinylLyricsColumn").visible);verify(item("vinylLyricsColumn").width>0);
        var notesOnly=record(1);delete notesOnly.tracks[0].lyrics;world.showBrowseSong(notesOnly,0);song.pageData.lyricsShown=true;wait(0);verify(song.pageData.hasNotes);item("vinylBarNotes").triggered();wait(0);verify(item("vinylNotesColumn").visible);verify(item("vinylNotesColumn").width>0);
    }
    function test_pagedSearchRetainsTrackTextLensAndSort() {
        seed(3);source.cataloguePageVersion=1;world.worldView.setTab("library");
        world.worldData.query="Song";verify(world.session.browsing);
        // Existing local filtering remains usable while the complete snapshot loads.
        compare(world.worldData.albums.length,3);
        source.completed("browse",world.session.browseRequest,{items:[record(2),record(0)],revision:"r1",nextOffset:-1,complete:true},"");
        verify(world.session.searchComplete);compare(world.worldData.albums.length,2);
        compare(world.worldData.albums[0].id,"album-0");
        world.worldData.sortOrder="year";compare(world.worldData.albums[0].id,"album-2");
        world.worldData.setLens("genre","Folk");compare(world.worldData.albums.length,0);
        world.worldData.setLens("genre","");world.worldData.query="Song 1";
        var pending=world.session.browseRequest;world.worldData.query="";
        source.completed("browse",pending,{items:[record(1)],revision:"r1",nextOffset:-1,complete:true},"");
        verify(!world.session.searchComplete);compare(world.worldData.albums.length,3);
    }

    function test_homeDoesNotExposeAnInertLibraryFilter() {
        seed(2);verify(world.worldData.canEnterVinyl);world.worldView.setTab("home");wait(0);
        compare(item("vinylSearchInput"),null,"The prototype inline search input is removed.");
        var originalQuery=world.worldData.query;
        item("vinylTopBar").searchClicked();tryVerify(function(){return world.searchOpen && world.searchPage!==null;});
        compare(world.page,"home");compare(world.worldView.currentTab,"home");
        var input=item("searchSurfaceInput");verify(input!==null && input.visible && input.activeFocus);
        world.searchPage.backRequested();tryVerify(function(){return !world.searchOpen;});
        compare(world.page,"home");compare(world.worldView.currentTab,"home");compare(world.worldData.query,originalQuery);
    }
    function test_searchOverlayPreservesSavedDiscoverView() {
        seed(8);verify(world.worldData.canEnterVinyl);world.worldView.setTab("discover");world.worldData.setOnlineMode(false);
        world.worldData.openList("local-albums",false);wait(0);
        var discoverQuery=world.worldData.query;
        world.worldView.setTab("library");
        world.openAlbum("album-0");source.completed("browse",world.session.browseRequest,record(0),"");
        item("vinylTopBar").searchClicked();wait(0);
        tryVerify(function(){return world.searchOpen && world.searchPage!==null;});
        compare(world.page,"album");compare(world.worldView.currentTab,"library");
        world.searchPage.query="remote";compare(world.searchPage.query,"remote");
        compare(world.worldData.query,discoverQuery);verify(!world.worldData.onlineMode);
        verify(world.goBack());tryVerify(function(){return !world.searchOpen;});compare(world.page,"album");
        verify(world.goBack());compare(world.page,"library");
        world.worldView.setTab("discover");wait(0);
        compare(world.worldData.query,discoverQuery);compare(world.worldView.listKey,"local-albums");
        verify(!world.worldData.onlineMode);
    }
    function test_libraryCarouselOpensCanonicalLibrary() {
        seed(3);verify(world.worldData.canEnterVinyl);world.worldData.openList("library",false);wait(0);
        world.worldView.setTab("home");wait(0);
        // e9875c366/411919a5c retired the static Library carousel shortcut.
        compare(item("vinylCarousel"),null);
        world.worldView.setTab("discover");wait(0);
        var libraryTab=item("vinylTab_library");verify(libraryTab && libraryTab.visible);
        mouseClick(libraryTab);wait(0);
        compare(world.page,"library");compare(world.worldView.listKey,"");
    }
    function test_tabsRetainIndependentListDetours() {
        seed(8);verify(world.worldData.canEnterVinyl);
        world.worldView.setTab("home");world.worldData.openList("local-albums",true);wait(0);
        var homeY=world.worldScroll.contentY;
        world.worldView.setTab("discover");world.worldData.setOnlineMode(false);
        world.worldData.setKind("songs");world.worldData.openList("local-songs",false);wait(0);
        world.worldScroll.contentY=world.worldView.contentTop+70;wait(0);
        var discoverY=world.worldScroll.contentY;
        world.worldView.setTab("library");wait(0);compare(world.worldView.listKey,"");
        world.worldView.setTab("home");wait(0);
        compare(world.worldView.listKey,"local-albums");verify(world.worldView.listHome);compare(world.worldScroll.contentY,homeY);
        world.worldView.setTab("discover");wait(0);
        compare(world.worldView.listKey,"local-songs");verify(!world.worldView.listHome);compare(world.worldScroll.contentY,discoverY);
        verify(world.goBack());compare(world.worldView.listKey,"");compare(world.page,"discover");
    }
    function test_topBarSearchDetourRestoresLibraryListRoute() {
        seed(8);verify(world.worldData.canEnterVinyl);world.worldView.setTab("library");
        world.worldData.openList("library",false);wait(0);
        world.worldScroll.contentY=world.worldView.contentTop+90;wait(0);
        var list=item("vinylListPage");
        tryVerify(function(){var candidate=findChild(list,"vinylListCard_2");return candidate && candidate.visible;},5000,"Library list delegate must load");
        var card=findChild(list,"vinylListCard_2");card.forceActiveFocus();tryCompare(card,"activeFocus",true);
        var savedY=world.worldScroll.contentY;
        world.openAlbum("album-2");source.completed("browse",world.session.browseRequest,record(2),"");
        compare(world.page,"album");

        var originalOnline=world.worldData.onlineMode,originalQuery=world.worldData.query;
        item("vinylTopBar").searchClicked();tryVerify(function(){return world.searchOpen && world.searchPage!==null;});
        compare(world.page,"album");compare(world.worldView.currentTab,"library");
        compare(world.worldView.listKey,"library");verify(item("searchSurfaceInput").activeFocus);
        compare(world.worldData.onlineMode,originalOnline);
        world.searchPage.query="detour query";compare(world.searchPage.query,"detour query");
        compare(world.worldData.query,originalQuery);

        verify(world.goBack());tryVerify(function(){return !world.searchOpen;});compare(world.page,"album");
        compare(world.worldData.onlineMode,originalOnline);compare(world.worldData.query,originalQuery);
        verify(world.goBack());compare(world.page,"library");
        compare(world.worldView.currentTab,"library");compare(world.worldView.listKey,"library");
        tryCompare(world.worldScroll,"contentY",savedY);
        tryVerify(function(){var restored=findChild(item("vinylListPage"),"vinylListCard_2");return restored && restored.activeFocus;},5000,"Back restores list-card focus");
    }

    function test_searchModeOpensSharedPageAndRestoresLibraryList() {
        seed(8);verify(world.worldData.canEnterVinyl);world.worldView.setTab("library");
        world.worldData.openList("library",false);wait(0);
        world.worldScroll.contentY=world.worldView.contentTop+110;wait(0);
        var list=item("vinylListPage");
        tryVerify(function(){var candidate=findChild(list,"vinylListCard_1");return candidate && candidate.visible;},5000,"Library list delegate must load");
        var card=findChild(list,"vinylListCard_1");card.forceActiveFocus();tryCompare(card,"activeFocus",true);
        var savedY=world.worldScroll.contentY;
        world.openAlbum("album-1");source.completed("browse",world.session.browseRequest,record(1),"");
        compare(world.page,"album");

        var originalOnline=world.worldData.onlineMode,originalQuery=world.worldData.query;
        world.enterDiscoverSearch(false,true);tryVerify(function(){return world.searchOpen && world.searchPage!==null;});
        compare(world.page,"album");compare(world.worldView.currentTab,"library");
        compare(world.worldView.listKey,"library");verify(item("searchSurfaceInput").activeFocus);
        compare(world.searchPage.searchMode,"Vinyl");
        compare(world.worldData.onlineMode,originalOnline,"Opening shared search preserves the existing mode.");
        world.searchPage.query="local detour query";compare(world.searchPage.query,"local detour query");
        compare(world.worldData.query,originalQuery);

        verify(world.goBack());tryVerify(function(){return !world.searchOpen;});compare(world.page,"album");
        compare(world.worldData.onlineMode,originalOnline);compare(world.worldData.query,originalQuery);
        verify(world.goBack());compare(world.page,"library");
        compare(world.worldView.currentTab,"library");compare(world.worldView.listKey,"library");
        tryCompare(world.worldScroll,"contentY",savedY);
        tryVerify(function(){var restored=findChild(item("vinylListPage"),"vinylListCard_1");return restored && restored.activeFocus;},5000,"Back restores list-card focus");
    }

}
