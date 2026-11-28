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
    SignalSpy { id: windowActionSpy }
    Component {
        id: detailSourceFactory
        QtObject {
            property int serial: 0
            property var artist: ({})
            property var artistAlbums: []
            property var artistSingles: []
            property var artistMedia: ({})
            property var artistIdentity: ({})
            property var artistArtwork: ({})
            property bool artistBusy: false
            property string artistError: ""
            property bool cached: false
            property int artistRetryCount: 0
            signal completed(string lane, int requestId, var data, string error)
            function request(lane, operation, id) { return ++serial; }
            function cancel(lane: string) {}
            function setAlbums(albums) {}
            function openArtist(ref) { artist = ref; artistBusy = true; }
            function leaveArtist() { artist = {}; artistBusy = false; }
            function retryArtist() { ++artistRetryCount; artistBusy = true; artistError = ""; }
        }
    }
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
        windowActionSpy.target = null;
        if (runtime) { runtime.destroy(); wait(0); runtime = null; }
        if (host) { host.close(); host.destroy(); wait(0); host = null; }
    }
    function namedCount(object, name) {
        var count = object.objectName === name ? 1 : 0;
        var children = object.children || [];
        for (var i = 0; i < children.length; ++i) count += namedCount(children[i], name);
        return count;
    }
    function prepareDetailRuntime() {
        createRuntime("VinylAppWorld.qml");
        runtime.worldData.preferencesStore = createTemporaryObject(preferencesFactory, test);
        const source = createTemporaryObject(detailSourceFactory, test);
        runtime.session.source = source;
        runtime.hostReady = true;
        tryCompare(runtime, "canEnterVinyl", true);
        tryCompare(runtime, "ready", true);
        runtime.showLibrary();
        host.requestActivate();
        tryCompare(host, "active", true);
        return source;
    }
    function detailRecord() {
        return {id: "detail-album", title: "Fixture album", origin: "local", artists: ["Fixture artist"],
            tracks: [{id: "detail-track", title: "Fixture song", seconds: 20, available: true}]};
    }
    function countVisible(object, names) {
        if (!object.visible) return 0;
        return (names.includes(object.objectName) ? 1 : 0)
            + (object.children || []).reduce((sum, item) => sum + countVisible(item, names), 0);
    }
    function test_detailChrome_data() {
        return ["album", "song", "artist", "mix"].map(route => ({tag: route, route: route}));
    }
    function test_detailChrome(data) {
        const source = prepareDetailRuntime();
        const record = detailRecord();
        switch (data.route) {
        case "album":
            runtime.openAlbum(record.id);
            source.completed("browse", runtime.session.browseRequest, record, "");
            break;
        case "song": runtime.showBrowseSong(record, 0); break;
        case "artist": runtime.showArtist({name: "Fixture artist", id: "fixture-artist"}); break;
        case "mix": runtime.page = "mix"; break;
        }
        tryCompare(runtime, "page", data.route);
        const bar = runtime.detailTitleBar;
        verify(bar && bar.visible);
        verify(waitForRendering(bar));
        const overflow = findChild(bar, "titleBarSystemMenu");
        verify(!overflow || !overflow.visible, "An overflow button must not replace the window controls");
        verify(!bar.menuOpen);
        compare(countVisible(runtime, ["vinylArtistMoreButton"]), 0);
        compare(countVisible(runtime, ["titleBarSearch", "vinylArtistSearchButton"]), 1);
        const search = findChild(bar, "titleBarSearch");
        verify(search && search.visible);
        // Default TheatreSeries uses a 22 px SVG inside a 22 px item, with 20 px gaps.
        const controls = [
            ["Minimize", "minimize.svg", "minimizeClicked"],
            ["Fullscreen", "fullscreen-exit.svg", "fullscreenClicked"],
            ["Quit", "power.svg", "powerClicked"]
        ];
        let lastX = search.mapToItem(bar, search.width, 0).x;
        for (const [name, glyph, signal] of controls) {
            const control = findChild(bar, "titleBarWindow" + name);
            verify(control && control.visible && control.enabled, name);
            compare(control.width, 22);
            compare(control.height, 22);
            const action = control.children.find(child => typeof child.activate === "function");
            verify(action && action.accessibleName.length > 0, "Window controls must be keyboard actions");
            const icon = control.children.find(child => child.source !== undefined);
            verify(icon && String(icon.source).endsWith("/" + glyph));
            const x = control.mapToItem(bar, 0, 0).x;
            verify(x > lastX);
            if (name !== "Minimize") compare(x - lastX, 20);
            lastX = x + control.width;
            windowActionSpy.signalName = signal;
            windowActionSpy.target = runtime;
            windowActionSpy.clear();
            mouseClick(control);
            compare(windowActionSpy.count, 1, signal);
        }
        mouseClick(search);
        tryCompare(runtime, "searchOpen", true);
    }
    function test_albumLoadsSilentlyAndErrorsRemainVisible() {
        const source = prepareDetailRuntime();
        const record = detailRecord();
        runtime.openAlbum(record.id);
        verify(runtime.openingAlbum);
        const status = findChild(runtime, "vinylStatus");
        compare(status.text, "", "Opening an album must not produce a progress tag");
        verify(runtime.detailTitleBar.visible);
        verify(findChild(runtime, "vinylAlbumLoadingIndicator").visible);
        mouseClick(runtime.detailTitleBar.backItem);
        verify(!runtime.openingAlbum);
        compare(runtime.page, "library");

        runtime.openAlbum(record.id);
        source.completed("browse", runtime.session.browseRequest, {}, "Album request failed.");
        tryCompare(status, "text", "Album request failed.");
        verify(!runtime.openingAlbum);
        compare(runtime.page, "library");
        runtime.openAlbum(record.id);
        source.completed("browse", runtime.session.browseRequest, record, "");
        tryCompare(runtime, "page", "album");
        verify(!findChild(runtime, "vinylAlbumLoadingIndicator").visible);
        compare(status.text, "");
    }
    function test_catalogueLookupUsesAnIndicatorInsteadOfATag() {
        const source = prepareDetailRuntime();
        runtime.resolveDiscoverItem({id: "fixture", kind: "albums", title: "Fixture album"}, "open", true);
        verify(runtime.pendingDiscoverItem);
        const status = findChild(runtime, "vinylStatus");
        compare(status.text, "");
        const indicator = findChild(runtime, "vinylLookupIndicator");
        verify(indicator.visible);
        source.completed("catalogue-item", runtime.pendingDiscoverItem.id, {}, "Lookup failed.");
        verify(!indicator.visible);
        verify(status.text.includes("Select it again to retry"));
    }
    function test_artistLoadsSilentlyAndRetryWorks() {
        const source = prepareDetailRuntime();
        runtime.showArtist({name: "Fixture artist", id: "ytm:artist:fixture"});
        verify(runtime.artistData.loading);
        const status = findChild(runtime, "vinylStatus");
        compare(status.text, "");
        verify(findChild(runtime, "vinylArtistLoadingIndicator").visible);
        source.artistError = "Artist request failed.";
        source.artistBusy = false;
        tryCompare(status, "text", "Artist request failed.");
        const retry = findChild(runtime, "vinylCatalogueRetry");
        verify(retry.visible && retry.enabled);
        verify(waitForRendering(retry));
        mouseClick(retry);
        compare(source.artistRetryCount, 1);
        compare(status.text, "");
    }
    function test_artistFilterDismissesWithoutSwallowingBack() {
        prepareDetailRuntime();
        runtime.showArtist({name: "Fixture artist", id: "fixture-artist"});
        const artist = runtime.detailView;
        const picker = findChild(artist, "vinylArtistReleasePicker");
        const pill = findChild(picker, "vinylArtistReleasePickerPill");
        artist.contentY = pill.mapToItem(artist.contentItem, 0, 0).y - 240;
        mouseClick(pill);
        tryCompare(picker, "open", true);
        mouseWheel(artist, 700, 330, 0, -120);
        tryCompare(picker, "open", false);
        artist.contentY = pill.mapToItem(artist.contentItem, 0, 0).y - 240;
        mouseClick(pill);
        tryCompare(picker, "open", true);
        tryCompare(findChild(host, "vinylArtistReleasePickerList"), "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(picker, "open", false);
        compare(runtime.page, "artist");
        mouseClick(pill);
        tryCompare(picker, "open", true);
        mouseClick(runtime.detailTitleBar.backItem);
        tryCompare(runtime, "page", "library");
        compare(picker.open, false);
        runtime.showArtist({name: "Fixture artist", id: "fixture-artist"});
        artist.contentY = pill.mapToItem(artist.contentItem, 0, 0).y - 240;
        mouseClick(pill);
        tryCompare(picker, "open", true);
        runtime.showLibrary();
        tryCompare(picker, "open", false);
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
