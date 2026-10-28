import QtQuick
import QtTest

// Presentation regressions run against test-only page data. The real source,
// decoder and library journey are independently qualified in the native app.
TestCase {
    name: "VinylLocalControls"
    when: windowShown
    property var host
    function item(name) {
        function walk(object) {
            if (object.objectName === name) return object;
            var children = object.children || [];
            for (var i = 0; i < children.length; ++i) {
                var found = walk(children[i]);
                if (found) return found;
            }
            return null;
        }
        return findChild(host, name) || walk(host.contentItem);
    }
    function init() {
        var component = Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml");
        compare(component.status, Component.Ready, component.errorString());
        host = component.createObject(null);
        verify(host !== null);
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
    }
    function cleanup() {
        if (host) { host.close(); host.destroy(); host = null; }
    }
    function test_futureActionsAreDisabledWithoutChangingSharedDefaults() {
        var album = item("vinylAlbumPage");
        var bar = item("vinylPlayerBar");
        verify(album.artistNavigationEnabled);
        verify(album.shuffleEnabled);
        verify(album.saveEnabled);
        verify(bar.queueEnabled);
        verify(bar.trackNavigationEnabled);
        album.artistNavigationEnabled = false;
        album.shuffleEnabled = false;
        bar.queueEnabled = false;
        bar.trackNavigationEnabled = false;
        verify(!item("vinylAlbumArtist").enabled);
        verify(!item("vinylAlbumShuffle").enabled);
        verify(item("vinylAlbumShuffle").opacity < 0.5);
        verify(item("vinylAlbumSave").enabled);
        verify(!item("vinylBarQueue").enabled);
        verify(!item("vinylBarPrev").enabled);
        verify(!item("vinylBarNext").enabled);
        var index = host.pageData.playingIndex;
        mouseClick(item("vinylBarNext"));
        compare(host.pageData.playingIndex, index);
    }
    function test_keyboardSeekUsesSessionAndClamps() {
        host.pageData.setSourceState("playing");
        host.pageData.position = 30;
        var seek = item("vinylBarSeek");
        verify(seek.enabled);
        seek.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(host.pageData.position, 35);
        keyClick(Qt.Key_Left);
        compare(host.pageData.position, 30);
        keyClick(Qt.Key_Home);
        compare(host.pageData.position, 0);
        keyClick(Qt.Key_Left);
        compare(host.pageData.position, 0);
        keyClick(Qt.Key_End);
        compare(host.pageData.position, host.pageData.duration);
        host.pageData.setSourceState("finding");
        verify(!seek.enabled);
    }
    function test_backLabelAndMissingArtwork() {
        host.page = "song";
        var song = item("vinylSongPage");
        song.backLabel = "Library";
        compare(item("vinylSongBack").label, "Library");
        var edition = JSON.parse(JSON.stringify(host.pageData.playingEdition));
        edition.cover = "";
        host.pageData.playingEdition = edition;
        verify(item("vinylSongPoster").width > 0);
        var playing = host.pageData.playingEditionId;
        var paused = host.pageData.paused;
        item("vinylSongBack").forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.page, "album");
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.paused, paused);
    }
}
