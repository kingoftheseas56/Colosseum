import QtQuick
import QtTest
TestCase {
    id:test
    name:"VinylLiveWorldPages"
    when:windowShown
    property var host
    property var world
    property var collection
    Component {id:storeFactory;QtObject {property var entries:[];function setVinylArtistSaved(artist,saved){entries=entries.filter(function(e){return e.id!==artist.id;});if(saved)entries=entries.concat([{id:artist.id,type:"artist",title:artist.name,payload:{artistId:artist.id}}]);return true;}function items(world){return entries}function has(world,id){return entries.some(function(e){return e.id===id})}function add(world,entry){entries=entries.concat([entry]);return true;}function replaceItems(world,removeIds,replacements){entries=entries.filter(function(e){return removeIds.indexOf(e.id)<0;}).concat(replacements);return true;}}}
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
    function item(name){return findChild(world,name)}
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
    function init(){host=windowFactory.createObject(test);source=sourceFactory.createObject(test);var c=Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");compare(c.status,Component.Ready,c.errorString());world=c.createObject(host.contentItem,{width:1280,height:720});world.session.source=source;preferences.setVinylTaste(false,[]);world.worldData.preferencesStore=preferences;collection=storeFactory.createObject(test);world.session.collectionStore=collection;wait(0);}
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
        seed(2);world.showArtist("Local Artist");
        var page=item("vinylArtistPage"),albums=item("vinylArtistTab_albums"),videos=item("vinylArtistTab_videos");
        verify(albums!==null && videos!==null);
        page.switchTab("albums",true);waitForRendering(world);
        compare(albums.focusPrimary,true);compare(albums.focusInset,-2);
        compare(findChild(albums,"vinylArtistTab_albums_selection").color,"#f0c44a");
        compare(findChild(videos,"vinylArtistTab_videos_selection").color,"#00000000");
        verify(albums.width>60 && albums.height===40,"Pill reserves space around the caption");
        page.switchTab("videos",true);waitForRendering(world);
        compare(videos.focusPrimary,true);compare(albums.focusPrimary,false);
        compare(findChild(videos,"vinylArtistTab_videos_selection").color,"#f0c44a");
        compare(videos.Accessible.checked,true);
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
    function test_asyncImportControlsProgressCancelAndAlbumAwareStatus() {
        var fileDialog=item("vinylOpenFilesDialog");
        verify(item("vinylOpenFiles")!==null);
        verify(item("vinylOpenFolder")!==null);
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
        seed(1);world.worldView.setTab("home");wait(0);
        var search=item("vinylPickerSearch"),tabs=item("vinylDockedTabs");
        var probe=clipProbeFactory.createObject(search,{x:4,y:4});
        var searchY=search.mapToItem(world.worldView,0,0).y;
        // Scroll the actual search field into the sticky strip, left of the pills.
        world.worldScroll.contentY=searchY-10;
        waitForRendering(world);verify(tabs.visible);
        var point=probe.mapToItem(world,4,4);
        var shot=grabImage(world);
        verify(!probeVisible(pixelAt(shot,point)),
               "Scrolling taste content must not paint inside the sticky tab viewport");
        // The same content must paint and remain interactive below that strip.
        world.worldScroll.contentY=searchY-world.worldView.dockedInset-10;
        waitForRendering(world);
        point=probe.mapToItem(world,4,4);shot=grabImage(world);
        verify(probeVisible(pixelAt(shot,point)),"Unclipped probe must remain visible");
        probe.destroy();
        world.worldView.syncPickerPlacement();wait(0);
        mouseClick(search,search.width/2,search.height/2);
        tryCompare(search,"activeFocus",true);
        keyClick(Qt.Key_A);compare(world.worldData.pickerQuery,"a");
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
        var cases=[{tab:"home",name:"vinylPickerQuestion"},
                   {tab:"discover",name:"vinylLensTitle"},
                   {tab:"library",name:"vinylListCard_0"}];
        for(var i=0;i<cases.length;i++) {
            // Exercise a fresh tab entry, rather than the saved startup carousel offset.
            world.worldView.tabOffsets={};
            world.worldView.setTab(cases[i].tab);wait(0);
            var target=item(cases[i].name);verify(target && target.visible);
            var top=target.mapToItem(world,0,0).y;
            var inset=item("vinylDockedTabs").visible ? world.worldView.dockedInset : 0;
            verify(top>=world.worldScroll.y+inset-1,cases[i].tab+" initial content clears sticky tabs");
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
        var albums=[];
        for(var i=0;i<13;i++){var a=record(i);a.year=1900+i*10;albums.push(a);}
        world.session.library=albums;
        world.session.playAlbum(albums[0],0);
        world.worldView.setTab("discover");wait(0);
        world.pushRoute();
        var trigger=item("vinylFilter_decade"),menu=item("vinylFilterMenu");
        // A newly resized native Window may not yet own OS keyboard focus.
        host.requestActivate();tryCompare(host,"active",true);
        waitForRendering(world);
        trigger.forceActiveFocus();tryCompare(trigger,"activeFocus",true);
        keyClick(Qt.Key_Return);tryCompare(menu,"opened",true);
        var scroll=item("vinylFilterMenuScroll");
        tryVerify(function(){return scroll.contentHeight>scroll.height;},5000,
                  "All decades require scrolling after popup layout settles");
        var popupTop=menu.contentItem.mapToItem(world,0,0).y-menu.topPadding;
        verify(popupTop>=world.worldScroll.y,"Popup stays below the page header");
        verify(popupTop+menu.height<=world.contentBottom+1,"Popup stays above the player");
        verify(scroll.height>=38,"At least one whole option remains reachable");
        if(data.close==="escape") {
            keyClick(Qt.Key_Escape);
        } else if(data.close==="selection") {
            scroll.contentY=scroll.contentHeight-scroll.height;waitForRendering(world);
            var last=findChild(menu.contentItem,"vinylFilterValue_decade_2020s");
            verify(last!==null,"Final decade option exists");
            mouseClick(last,last.width/2,last.height/2);
            compare(world.worldData.lens.decade,"2020s");
        } else if(data.close==="keyboardSelection") {
            keyClick(Qt.Key_Down);keyClick(Qt.Key_Return);
            compare(world.worldData.lens.decade,"1900s");
        } else {
            mouseClick(world,world.width-70,world.worldScroll.y+80);
        }
        tryCompare(menu,"opened",false);
        tryCompare(trigger,"activeFocus",true);
        keyClick(Qt.Key_Return);tryCompare(menu,"opened",true);
        var selected=findChild(menu.contentItem,"vinylFilterValue_decade_"+world.worldData.lens.decade);
        tryCompare(selected,"activeFocus",true);
        var optionTop=selected.mapToItem(scroll,0,0).y;
        verify(optionTop>=-1 && optionTop+selected.height<=scroll.height+1,"Reopening reveals selected option");
        if(data.width===1280 && data.close==="escape") {
            host.width=1024;world.width=1024;host.height=640;world.height=640;wait(0);
            tryVerify(function(){return menu.contentItem.mapToItem(world,0,0).y-menu.topPadding+menu.height<=world.contentBottom+1;});
            verify(scroll.height>=38,"Resize retains a complete scrollable option");
        }
        keyClick(Qt.Key_Escape);tryCompare(menu,"opened",false);
        compare(world.page,"discover","Escape closes only the popup, not the page");
        compare(world.routeStack.length,1,"Popup Escape must not invoke the page Back handler");
        tryCompare(trigger,"activeFocus",true);
        mouseClick(trigger);tryCompare(menu,"opened",true);
        keyClick(Qt.Key_Escape);tryCompare(menu,"opened",false);
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
    function test_recentSearchMenuKeyboardAndProfileClosure(){
        seed(2);history.recent=[];history.revision++;world.worldData.searchHistoryStore=history;
        var input=item("vinylSearchInput");input.forceActiveFocus();keyClick(Qt.Key_Q);keyClick(Qt.Key_A);keyClick(Qt.Key_Return);wait(0);
        compare(history.recent[0],"qa");var recent=item("vinylRecentSearches");verify(recent.visible);
        host.width=1024;world.width=1024;wait(0);var point=recent.mapToItem(world,recent.width,recent.height);verify(point.x<=world.width,"Recent recall fits 1024 width");
        mouseClick(recent);wait(0);var entry=item("vinylRecentSearch_0");verify(entry && entry.visible);compare(entry.text,"qa");
        keyClick(Qt.Key_Escape);tryCompare(input,"activeFocus",true);
        keyClick(Qt.Key_A,Qt.ControlModifier);keyClick(Qt.Key_Backspace);compare(world.worldData.query,"");
        mouseClick(recent);wait(0);mouseClick(entry);tryCompare(input,"text","qa");
        mouseClick(recent);wait(0);world.worldData.searchHistoryStore=null;wait(0);verify(!recent.visible);verify(!entry.visible);
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
        mouseClick(queueControl,queueControl.width/2,queueControl.height/2);wait(0);
        compare(world.session.queue.length,count+1);
        compare(world.session.playingEditionId,edition);
        compare(world.session.playingDisc,disc);compare(world.session.playingIndex,index);
    }
    function test_emptyDiscoverHasOneTabBarAndNoEmptyShelf(){
        seed(0);world.worldView.setTab("discover");wait(0);
        var flowing=item("vinylFlowTabs"),docked=item("vinylDockedTabs"),empty=item("vinylDiscoverEmpty"),discover=item("vinylDiscoverPage");
        verify(flowing!==null && docked!==null && empty!==null);
        compare(Number(flowing.visible)+Number(docked.visible),1,"Exactly one tab bar can be painted");
        compare(discover.shelfCount,0);verify(empty.visible);verify(!item("vinylWorldRowIndexSidebar").visible);
        verify(item("vinylShelf_local-albums")===null,"Empty data must not produce a header/See all over the empty message");
        var point=empty.mapToItem(world,0,0);verify(point.y>=world.worldScroll.y,"Empty message lies below world controls");
        world.worldScroll.contentY=0;wait(0);compare(Number(flowing.visible)+Number(docked.visible),1);verify(flowing.visible);
        world.worldScroll.contentY=world.worldView.pickerParkY+1;wait(0);compare(Number(flowing.visible)+Number(docked.visible),1);verify(docked.visible);
        seed(1);wait(0);compare(discover.shelfCount,1);verify(!empty.visible);verify(item("vinylShelf_local-albums")!==null);
    }
    function test_pointerSeeAllThenKeyboardBack(){
        seed(8);world.worldData.skipPicker();world.worldView.setTab("home");wait(0);
        var more=item("vinylHomeSeeAll_local-albums");verify(more!==null);verify(more.visible);
        mouseClick(more);wait(0);compare(world.worldView.listKey,"local-albums");
        var list=item("vinylListPage");var focus=host.activeFocusItem;
        verify(focus!==null && focus.visible,"Opening a list must move focus away from its hidden origin");
        var ancestor=focus,inList=false;while(ancestor){if(ancestor===list){inList=true;break;}ancestor=ancestor.parent;}
        verify(inList,"List receives focus after pointer See all");
        keyClick(Qt.Key_Escape);wait(0);compare(world.worldView.listKey,"");verify(more.activeFocus,"Back returns focus to original See all");
    }
    function test_emptySingleLongRefreshed(){
        seed(0);compare(world.worldData.listItems("library").length,0);
        seed(1);compare(world.worldData.listItems("library").length,1);
        seed(80);compare(world.worldData.listItems("library").length,80);
        var list=item("vinylLibraryPage");list.currentIndex=30;list.selectedId="album-30";
        var reversed=world.session.library.slice().reverse();world.session.library=reversed;wait(0);
        compare(list.row.items[list.currentIndex].id,"album-30");
        world.worldData.query="Album 030";compare(world.worldData.listItems("library").length,1);
        world.worldData.query="missing";compare(world.worldData.listItems("library").length,0);
    }
    function test_canonicalArtistsAndTaste(){
        world.session.library=[record(0,"artist-one"),record(1,"artist-two")];wait(0);
        var artists=world.worldData.artistOptions("");compare(artists.length,2);verify(artists[0].id!==artists[1].id);
        world.worldData.answerPicker(["artist-two"]);compare(world.worldData.homeRows[0].items.length,1);compare(world.worldData.homeRows[0].items[0].albumId,"album-1");
        world.activateItem(artists[1]);compare(world.page,"artist");compare(world.artistData.artistId,"artist-two");
    }
    function test_nestedBackRetainsLibraryAndArtist(){
        seed(40);world.worldData.skipPicker();world.worldView.setTab("discover");world.worldData.setLens("genre","Jazz");world.worldData.openList("local-albums",false);wait(0);
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
        seed(40);world.worldScroll.contentY=world.worldView.contentTop;wait(0);
        var list=item("vinylLibraryPage");list.focusCurrent();wait(0);keyClick(Qt.Key_Right);wait(0);compare(list.currentIndex,1);
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
        wait(0);compare(world.page,"library");
    }
    function test_browsedUntimedNotesHaveVisibleFallback(){
        seed(1);world.showBrowseSong(record(0),0);var song=item("vinylSongPage");
        verify(!song.pageData.isCurrent);compare(song.pageData.currentLineIndex,-1);
        item("vinylSongNotes").triggered();wait(0);
        compare(song.pageData.activeNoteLine,0,"Untimed or non-playing browse must use the supplied notes fallback");
        var notes=item("vinylNotesColumn");verify(notes.visible);compare(notes.noteCount,1);
        compare(notes.shownNotes[0].text,"Supplied note");
    }
    function test_canonicalTabTargetAlwaysVisible(){
        seed(0);world.worldView.setTab("discover");wait(0);
        var target=item("vinylTab_library");verify(target!==null);verify(target.visible,"Canonical action must resolve the visible bar");
        var bounds=target.mapToItem(world,0,0);verify(bounds.y>=world.worldScroll.y);
        mouseClick(target);compare(world.page,"library");
        world.worldScroll.contentY=0;wait(0);target=item("vinylTab_discover");verify(target.visible);mouseClick(target);compare(world.page,"discover");
    }
    function test_suppliedDetailsWideAndNarrow(){
        seed(1);world.showBrowseSong(record(0),0);var song=item("vinylSongPage");compare(song.pageData.lyrics.length,2);compare(song.pageData.playingCredits[0].names[0],"Local Artist");
        song.pageData.lyricsShown=true;wait(0);verify(item("vinylLyricLine_0").opacity>0);
        world.width=900;host.width=900;wait(0);verify(item("vinylCompactLyrics").visible);verify(item("vinylCompactLyrics").width>0);
        var notesOnly=record(1);delete notesOnly.tracks[0].lyrics;world.showBrowseSong(notesOnly,0);song.pageData.lyricsShown=true;wait(0);verify(song.pageData.hasNotes);verify(item("vinylCompactLyrics").visible);
    }
    function test_pagedSearchRetainsTrackTextLensAndSort() {
        seed(3);source.cataloguePageVersion=1;
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

}
