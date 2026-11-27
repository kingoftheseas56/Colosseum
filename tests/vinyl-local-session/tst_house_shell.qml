import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylHouseRuntime"
    when: windowShown
    property var host: null
    property var runtime: null
    Component { id: windowFactory; Window { width: 1280; height: 700; visible: true } }
    SignalSpy { id: songSpy; signalName: "songRequested" }
    Component {
        id: preferencesFactory
        QtObject {
            property bool vinylTasteAnswered: true
            property var vinylTasteArtists: ["a", "b", "c", "d", "e"].map(letter => ({
                id: "ytm:artist:UC" + letter.repeat(22), title: letter
            }))
            signal vinylTasteChanged()
            function setVinylTasteSelections(answered, artists) {
                vinylTasteAnswered = answered;
                vinylTasteArtists = artists;
                vinylTasteChanged();
                return true;
            }
        }
    }

    function createRuntime(file) {
        host = windowFactory.createObject(test);
        var component = Qt.createComponent("../../qml/vinyl/" + file);
        compare(component.status, Component.Ready, component.errorString());
        runtime = component.createObject(host.contentItem, {
            width: 1280, height: 700, hostReady: false, deferInitialRestore: true
        });
        verify(runtime !== null, component.errorString());
        tryCompare(runtime, "componentReady", true);
    }
    function cleanup() {
        songSpy.target = null;
        if (runtime) { runtime.destroy(); wait(0); runtime = null; }
        if (host) { host.close(); host.destroy(); wait(0); host = null; }
    }
    function namedCount(object, name) {
        var count = object.objectName === name ? 1 : 0;
        var children = object.children || [];
        for (var i = 0; i < children.length; ++i) count += namedCount(children[i], name);
        return count;
    }
    function test_houseUsesOneExistingSessionAndNoLegacyBoard() {
        createRuntime("VinylAppWorld.qml");
        verify(runtime.session !== null);
        compare(findChild(runtime, "vinylLiveSession"), runtime.session);
        compare(namedCount(runtime, "vinylLiveSession"), 1);
        compare(runtime.worldView, runtime.browseShell);
        compare(runtime.worldView.objectName, "vinylWorld");
        compare(findChild(runtime, "vinylBrowseShell"), null);
        compare(findChild(runtime, "vinylLiveWorld"), null);
        compare(runtime.page, "home");
        compare(runtime.activeTab, "Home");
        compare(runtime.worldView.tabModel.length, 4);
    }
    function test_houseWaitsForItsHost() {
        createRuntime("VinylAppWorld.qml");
        compare(runtime.ready, false);
        compare(runtime.worldView.ready, false);
        compare(runtime.enabled, false);
        runtime.hostReady = true;
        tryCompare(runtime, "ready", true);
        compare(runtime.worldView.ready, true);
        compare(runtime.enabled, true);
    }
    function test_playerHudStartsClosedAndSongKeepsItsControls() {
        createRuntime("VinylAppWorld.qml");
        runtime.worldData.preferencesStore = createTemporaryObject(preferencesFactory, test);
        runtime.hostReady = true;
        tryCompare(runtime, "canEnterVinyl", true);
        tryCompare(runtime, "ready", true);
        runtime.showHome();
        runtime.shellDockBottomExtent = 80;
        var bar = findChild(runtime, "vinylPlayerBar");
        verify(bar !== null);
        compare(runtime.hudOpen, false);
        compare(runtime.hudHasMedia, false);
        runtime.toggleHud();
        compare(runtime.hudOpen, false);

        var edition = runtime.session.normalize({ id: "hud-album", title: "HUD album", origin: "local",
            tracks: [{ id: "hud-track", title: "HUD track", available: true, seconds: 20 }] });
        verify(runtime.session.appendRows(runtime.session.editionRows(edition), false));
        compare(runtime.session.playingIndex, 0);
        compare(runtime.session.paused, true);
        compare(runtime.hudOpen, false);
        compare(bar.visible, false, "Queued media must not open the browsing HUD");
        compare(runtime.contentBottom, runtime.height - runtime.shellDockBottomExtent);
        compare(runtime.browseShell.contentEndPadding, runtime.shellDockBottomExtent + 14);

        runtime.toggleHud();
        compare(runtime.hudOpen, true);
        compare(bar.visible, true);
        verify(runtime.contentBottom < bar.y);
        verify(runtime.browseShell.contentEndPadding > runtime.shellDockBottomExtent + 14);
        runtime.toggleHud();
        compare(bar.visible, false);
        compare(runtime.browseShell.contentEndPadding, runtime.shellDockBottomExtent + 14);

        runtime.showSong();
        compare(runtime.page, "song");
        compare(runtime.hudOpen, false);
        compare(bar.visible, true);
        runtime.goBack();
        compare(bar.visible, false);

        runtime.toggleHud();
        runtime.lifecycleActive = false;
        runtime.lifecycleActive = true;
        compare(runtime.hudOpen, false);
        compare(bar.visible, false);
        compare(runtime.session.playingIndex, 0, "Re-entry must not clear the queue");
    }
    function test_houseDoesNotInterceptPlayerControls() {
        createRuntime("VinylAppWorld.qml");
        runtime.hostReady = true;
        tryCompare(runtime, "ready", true);
        var bar = findChild(runtime, "vinylPlayerBar");
        var button = findChild(bar, "vinylBarOpenSong");
        verify(bar !== null && button !== null);
        // Isolate pointer delivery from queue/taste setup. No canonical playback
        // state is changed; the real control must remain above the full shell.
        bar.visible = true;
        button.enabled = true;
        songSpy.target = bar;
        songSpy.clear();
        wait(0);
        mouseClick(button, button.width / 2, button.height / 2);
        compare(songSpy.count, 1);
    }
    function test_legacyComponentStillUsesTheSameRuntimeOwner() {
        createRuntime("VinylLiveWorld.qml");
        compare(runtime.objectName, "vinylLiveWorld");
        compare(runtime.browseShell.objectName, "vinylBrowseShell");
        verify(runtime.worldView !== runtime.browseShell);
        compare(findChild(runtime, "vinylLiveSession"), runtime.session);
        compare(namedCount(runtime, "vinylLiveSession"), 1);
    }
}
