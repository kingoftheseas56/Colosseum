import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylLocalNavigationContracts"
    when: windowShown
    property var host
    property var world
    property var transport
    Component {
        id: preferencesFactory
        QtObject {
            property bool vinylTasteAnswered: true
            property var vinylTasteArtists: ["a","b","c","d","e"].map(letter => ({id:"ytm:artist:UC"+letter.repeat(22),title:letter}))
            signal vinylTasteChanged()
            function setVinylTasteSelections(answered, artists) {
                vinylTasteAnswered=answered;vinylTasteArtists=artists;vinylTasteChanged();return true;
            }
        }
    }
    Component { id: hostFactory; Window { width: 1280; height: 720; visible: true } }
    Component {
        id: sourceFactory
        QtObject {
            property int serial: 0
            property int importCalls: 0
            property var cancellations: []
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function request(lane, operation, value, options) { return ++serial; }
            function cancel(lane) { cancellations = cancellations.concat([lane]); }
            function setAlbums(entries) {}
            property int pendingImport: -1
            signal importCompleted(int id, var result)
            function startImport(paths) { pendingImport=++serial; return pendingImport; }
            function cancelImport() { pendingImport=-1; }
            function finishImport() {
                var id=pendingImport;
                if(id<0)return;
                pendingImport=-1;importCalls++;
                importCompleted(id,{albums:[],errors:[]});
            }
        }
    }
    function init() {
        host = hostFactory.createObject(test);
        transport = sourceFactory.createObject(test);
        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status, Component.Ready, component.errorString());
        world = component.createObject(host.contentItem, {width: 1280, height: 720});
        verify(world !== null);
        world.session.source = transport;
        world.worldData.preferencesStore = createTemporaryObject(preferencesFactory, test);
        tryCompare(world, "canEnterVinyl", true);
        world.showLibrary();
        wait(0);
    }
    function cleanup() {
        if (host) { host.close(); host.destroy(); }
        wait(0);
        if (transport) transport.destroy();
        wait(0);
    }
    function album(id) {
        return {id: id, browseId: id, origin: "local", title: id, artists: [], tracks: []};
    }
    // Native queue/checkpoint commands own playback identity and measured time.
    function selectPaused(record, duration) {
        var edition=world.session.normalize(record);
        var rows=[world.session.rowFor(edition,0,0)];
        rows[0].queueId="navigation-occurrence";
        verify(findChild(world.session,"vinylBackendSession").restoreLegacy(rows,0,false,[],"off",0,duration || 0));
    }
    function test_suppliedEditionLabelsAreActuallyDisplayed() {
        var record=album("same-album");record.title="Synthetic Disc Ordering";
        record.tracks=[{id:"track",title:"Track",seconds:1,artists:[]}];
        record.editions=[{id:"standard",title:"Standard",tracks:record.tracks},{id:"alternate",title:"Alternate",tracks:record.tracks}];
        world.session.shownEdition=world.session.normalize(record);world.page="album";wait(0);
        function containsText(item,text) {
            if(item.text!==undefined && item.text===text) return true;
            var children=item.children || [];
            for(var i=0;i<children.length;++i) if(containsText(children[i],text)) return true;
            return false;
        }
        var standard=findChild(world,"vinylEdition_standard"), alternate=findChild(world,"vinylEdition_alternate");
        verify(standard!==null);verify(alternate!==null);
        compare(standard.accessibleName,"Standard");compare(alternate.accessibleName,"Alternate");
        verify(containsText(standard,"Standard"));verify(containsText(alternate,"Alternate"));
        compare(world.session.shownEdition.name,"Synthetic Disc Ordering");
    }
    function test_libraryReturnCancelsPendingAlbum() {
        world.page = "album";
        world.session.openAlbum("late");
        var request = world.session.browseRequest;
        verify(world.session.browsing);
        world.showLibrary();
        compare(world.page, "library");
        verify(!world.session.browsing);
        verify(transport.cancellations.indexOf("browse") >= 0);
        transport.completed("browse", request, album("late"), "");
        compare(world.page, "library");
        compare(world.session.shownEditionId, "");
    }
    function test_backWhileOpeningCancelsInsteadOfLeavingWorld() {
        world.openAlbum("late");
        var request = world.session.browseRequest;
        verify(world.session.browsing);
        verify(world.pendingAlbumRoute !== null);
        verify(world.goBack());
        verify(!world.session.browsing);
        transport.completed("browse", request, album("late"), "");
        compare(world.page, "library");
        verify(!world.goBack(), "Library with no pending work returns Back to the shell");
    }
    function test_songNavigationCancelsPendingBrowseAndPreservesIdentity() {
        world.session.shownEdition = world.session.normalize(album("browsed"));
        var playing=album("playing");
        playing.tracks=[{id:"playing-track",title:"Playing",seconds:1}];
        selectPaused(playing,1);
        world.page = "album";
        world.session.openAlbum("late");
        var request = world.session.browseRequest;
        world.showSong();
        compare(world.page, "song");
        verify(world.session.paused, "Opening now playing must not resume audio");
        transport.completed("browse", request, album("late"), "");
        compare(world.page, "song");
        verify(world.goBack());
        compare(world.page, "album");
        compare(world.session.shownEditionId, "browsed");
        compare(world.session.playingEditionId, "playing");
    }
    function test_profileChangeRejectsDeferredImport() {
        world.importLocalFiles(["file:///test/local.wav"]);
        verify(world.importing);
        var request=transport.pendingImport;
        transport.profileLeaving();
        tryCompare(world, "importing", false);
        transport.finishImport();
        transport.importCompleted(request,{albums:[album("late-import")],errors:[]});
        compare(transport.importCalls, 0);
        compare(world.session.library.length,0);
        compare(world.importStatus, "");
    }
    function test_sameProfileRunsDeferredImport() {
        world.importLocalFiles(["file:///test/local.wav"]);
        verify(world.importing);
        transport.finishImport();
        tryCompare(world, "importing", false);
        compare(transport.importCalls, 1);
        compare(world.page, "library");
    }
    function test_shellGeometry_data() {
        return [{tag: "native", width: 1364, height: 1024},
                {tag: "desktop", width: 1280, height: 720},
                {tag: "narrow", width: 900, height: 720}];
    }
    function test_shellGeometry(data) {
        host.width = data.width; host.height = data.height;
        world.width = data.width; world.height = data.height;
        wait(0);
        var top = findChild(world, "vinylTopBar");
        var nav = findChild(world, "vinylFlowTabs");
        var library = findChild(world, "vinylPersonalLibraryPage");
        verify(top !== null);verify(nav !== null);verify(library !== null);
        verify(nav.mapToItem(world,0,0).y >= top.mapToItem(world,0,top.height).y + 20,
               "Shared navigation clears clock/date chrome");
        verify(library.mapToItem(world,0,0).y >= nav.mapToItem(world,0,nav.height).y,
               "Library starts below shared navigation");
        world.enterDiscoverSearch(false,false);
        tryVerify(function(){return world.searchPage !== null;});
        verify(findChild(world,"worldSearchField") !== null, "Search uses the shared search surface");
        world.closeSearch();
        world.page = "album";
        var back = findChild(world, "vinylAlbumBack");
        var albumPage = findChild(world, "vinylAlbumPage");
        top = findChild(world,"vinylTopBar");
        verify(back.y >= top.y + top.height + 16);
        verify(albumPage.y >= back.y + back.height);
        verify(!albumPage.scrollbarVisible);
        wait(0); // Let closeSearch's deferred focus parking finish first.
        albumPage.forceActiveFocus();
        keyClick(Qt.Key_PageDown);
        verify(albumPage.contentY > 0, "Keyboard scrolling remains available without a scrollbar");
        keyClick(Qt.Key_Home);
        tryCompare(albumPage, "contentY", 0);
        world.page = "song";
        verify(findChild(world, "vinylSongPage").y >= top.y + top.height + 16);
    }
    function test_unknownTrackDurationUsesMeasuredPlayerDurationOnlyWhenPlaying() {
        var record = album("untagged");
        record.tracks = [{id: "local-track", title: "Local file", seconds: 0, artists: []}];
        world.session.shownEdition = world.session.normalize(record);
        world.page = "album";
        wait(0);
        var row = findChild(world, "vinylTrackRow_0");
        verify(row !== null);
        var duration = findChild(row, "vinylTrackDuration_0");
        compare(duration.text, "—");
        selectPaused(record,90);
        compare(duration.text, "1:30");
        world.session.shownEdition = world.session.normalize({id: "different", origin: "local", title: "Other", tracks: record.tracks});
        compare(duration.text, "—", "Another playing album cannot lend duration to a browsed file");
    }
    function test_footerClearsActualCollapsedTaskbarGeometry() {
        var component = Qt.createComponent("../../qml/Taskbar.qml");
        compare(component.status, Component.Ready, component.errorString());
        var dock = component.createObject(host.contentItem, {
            pickerScene: world, vinylHudAvailable: true
        });
        verify(dock !== null);
        world.shellHudButton = dock.vinylHudButton;
        dock.vinylHudClicked.connect(world.toggleHud);
        dock.vinylHasMedia = Qt.binding(() => world.hudHasMedia);
        world.shellDockRight = Qt.binding(function() { return dock.visible ? dock.leftEdge + dock.closedWidth : 0; });
        world.shellDockBottomExtent = Qt.binding(function() { return dock.visible ? dock.bottomGap + dock.closedSize : 0; });
        wait(0);
        var status = findChild(world, "vinylStatusRow");
        var playerBar = findChild(world, "vinylPlayerBar");
        verify(status.x > dock.leftEdge + dock.closedWidth, "Library status clears the shell's permanent buttons");
        verify(world.contentBottom <= world.height - dock.bottomGap - dock.closedSize, "Library rows end above the dock");
        var record=album("footer");record.tracks=[{id:"footer-track",title:"Footer",seconds:1}];
        selectPaused(record,1);
        world.toggleHud();
        tryCompare(playerBar, "dockProgress", 0);
        verify(playerBar.visible);
        const dockButton = findChild(dock, "taskbarVinylHudAction");
        compare(dockButton, world.shellHudButton);
        const minimise = findChild(playerBar, "vinylBarMinimise");
        minimise.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(world.hudOpen, false);
        tryCompare(playerBar, "visible", false);
        compare(dockButton.activeFocus, true);
        compare(playerBar.dockTarget, world.mapFromItem(dockButton, dockButton.width / 2, dockButton.height / 2));
        keyClick(Qt.Key_Return);
        compare(world.hudOpen, true);
        tryCompare(playerBar, "dockProgress", 0);
        var capsule = findChild(dock, "taskbarDock");
        tryCompare(capsule, "width", 310);
        verify(playerBar.x > capsule.x + capsule.width, "Player cover and title clear the widened dock");
        verify(playerBar.x + playerBar.width <= world.width);
        var footerLeft = playerBar.x;
        dock.open = true;
        compare(playerBar.x, footerLeft, "Expanded taskbar retains its overlay behavior");
        dock.visible = false;
        verify(playerBar.x < footerLeft, "No dock means no artificial inset");
        verify(status.x < footerLeft);
    }
    function test_compactAlbumControls_data() {
        return [{tag: "desktop", width: 1280, height: 720},
                {tag: "narrow", width: 900, height: 720}];
    }
    function test_compactAlbumControls(data) {
        host.width = data.width; host.height = data.height;
        world.width = data.width; world.height = data.height;
        var record = album("local");
        record.title = "Local acceptance audio";
        record.tracks = [{id: "local-track", title: record.title, seconds: 0, artists: []}];
        world.session.shownEdition = world.session.normalize(record);
        selectPaused(record,0);
        world.page = "album";
        wait(0);
        var albumPage = findChild(world, "vinylAlbumPage");
        function visualItem(object, name) {
            if (object.objectName === name) return object;
            var children = object.children || [];
            for (var index = 0; index < children.length; ++index) {
                var found = visualItem(children[index], name);
                if (found) return found;
            }
            return null;
        }
        var names = ["vinylAlbumPlay", "vinylAlbumSave"];
        for (var i = 0; i < names.length; ++i) {
            var action = visualItem(albumPage, names[i]);
            verify(action !== null);
            // Large artwork is intentional (1f083080b): compact viewports scroll
            // to the actions rather than shrinking the album cover.
            var point = action.mapToItem(albumPage.contentItem, 0, 0);
            albumPage.contentY=Math.min(Math.max(0,point.y-24),albumPage.contentHeight-albumPage.height);
            var visiblePoint=action.mapToItem(albumPage,0,0);
            verify(visiblePoint.y >= 0 && visiblePoint.y + action.height <= albumPage.height,
                   names[i] + " must be fully reachable by scrolling");
        }
    }
}
