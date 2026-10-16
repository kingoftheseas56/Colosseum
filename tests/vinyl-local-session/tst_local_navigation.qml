import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylLocalNavigationContracts"
    when: windowShown
    property var host
    property var world
    property var transport
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
            function request(lane, operation, value) { return ++serial; }
            function cancel(lane) { cancellations = cancellations.concat([lane]); }
            function setAlbums(entries) {}
            function importFiles(paths) { importCalls++; return {albums: [], errors: []}; }
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
        world.session.openAlbum("late");
        var request = world.session.browseRequest;
        verify(world.goBack());
        verify(!world.session.browsing);
        transport.completed("browse", request, album("late"), "");
        compare(world.page, "library");
        verify(!world.goBack(), "Library with no pending work returns Back to the shell");
    }
    function test_songNavigationCancelsPendingBrowseAndPreservesIdentity() {
        world.session.shownEdition = world.session.normalize(album("browsed"));
        world.session.playingEdition = world.session.normalize(album("playing"));
        world.session.playingIndex = 0;
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
        transport.profileLeaving();
        tryCompare(world, "importing", false);
        compare(transport.importCalls, 0);
        verify(world.importStatus.indexOf("profile changed") >= 0);
    }
    function test_sameProfileRunsDeferredImport() {
        world.importLocalFiles(["file:///test/local.wav"]);
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
        var nav = findChild(world, "vinylNavigation");
        var header = findChild(world, "vinylLibraryHeader");
        verify(nav.y >= top.y + top.height + 20, "Local navigation clears shared clock/date chrome");
        verify(header.y >= nav.y + nav.height + 20, "Library starts below navigation");
        verify(findChild(world, "vinylSearchGlass") !== null, "Filter uses the shared glass surface");
        world.page = "album";
        var back = findChild(world, "vinylAlbumBack");
        var albumPage = findChild(world, "vinylAlbumPage");
        verify(back.y >= nav.y + nav.height + 16);
        verify(albumPage.y >= back.y + back.height);
        verify(!albumPage.scrollbarVisible);
        albumPage.forceActiveFocus();
        keyClick(Qt.Key_PageDown);
        verify(albumPage.contentY > 0, "Keyboard scrolling remains available without a scrollbar");
        keyClick(Qt.Key_Home);
        compare(albumPage.contentY, 0);
        world.page = "song";
        verify(findChild(world, "vinylSongPage").y >= nav.y + nav.height + 16);
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
        world.session.playingEdition = world.session.shownEdition;
        world.session.playingIndex = 0;
        world.session.measuredDuration = 90;
        compare(duration.text, "1:30");
        world.session.shownEdition = world.session.normalize({id: "different", origin: "local", title: "Other", tracks: record.tracks});
        compare(duration.text, "—", "Another playing album cannot lend duration to a browsed file");
    }
    function test_footerClearsActualCollapsedTaskbarGeometry() {
        var component = Qt.createComponent("../../qml/Taskbar.qml");
        compare(component.status, Component.Ready, component.errorString());
        var dock = component.createObject(host.contentItem);
        verify(dock !== null);
        world.shellDockRight = Qt.binding(function() { return dock.visible ? dock.leftEdge + dock.closedWidth : 0; });
        world.shellDockBottomExtent = Qt.binding(function() { return dock.visible ? dock.bottomGap + dock.closedSize : 0; });
        wait(0);
        var status = findChild(world, "vinylStatusRow");
        var playerBar = findChild(world, "vinylPlayerBar");
        verify(status.x > dock.leftEdge + dock.closedWidth, "Library status clears the shell's permanent buttons");
        verify(world.contentBottom <= world.height - dock.bottomGap - dock.closedSize, "Library rows end above the dock");
        world.session.playingIndex = 0;
        verify(playerBar.visible);
        verify(playerBar.x > dock.leftEdge + dock.closedWidth, "Player cover and title clear the collapsed dock");
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
        world.session.playingEdition = world.session.shownEdition;
        world.session.playingIndex = 0;
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
            var point = action.mapToItem(albumPage, 0, 0);
            verify(point.y >= 0 && point.y + action.height <= albumPage.height,
                   names[i] + " must be fully visible without scrolling: bottom=" + (point.y + action.height) + ", height=" + albumPage.height);
        }
    }
}
