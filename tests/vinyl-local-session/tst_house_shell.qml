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
        compare(runtime.worldView.tabModel.length, 3);
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
