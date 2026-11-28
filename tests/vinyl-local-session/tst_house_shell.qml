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
            property bool artistCatalogueBusy: false
            property string artistError: ""
            property bool cached: false
            property int artistRetryCount: 0
            property var calls: ({})
            property var canceled: []
            signal completed(string lane, int requestId, var data, string error)
            function request(lane: string, operation: string, id: string): int {
                calls[lane] = {operation: operation, value: id, id: ++serial};
                return serial;
            }
            function cancel(lane: string) { canceled.push(lane); }
            function setAlbums(albums) {}
            function openArtist(ref) { artist = ref; artistCatalogueBusy = true; artistBusy = true; }
            function leaveArtist() { artist = {}; artistCatalogueBusy = false; artistBusy = false; }
            function retryArtist() { ++artistRetryCount; artistCatalogueBusy = true; artistBusy = true; artistError = ""; }
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
        source.artistCatalogueBusy = false;
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
    function artistRelease(kind, index) {
        return {id: kind + index, browseId: "MPREb_" + kind + index, title: kind + " " + index,
            year: 2025 - index, kind: kind, origin: "youtube-music", artists: ["Fixture artist"],
            artistIds: [runtime.artistData.artistId], tracks: []};
    }
    function openOnlineArtist() {
        const source = prepareDetailRuntime();
        runtime.showArtist({name: "Fixture artist", id: "ytm:artist:UC" + "a".repeat(22)});
        tryCompare(runtime, "page", "artist");
        return source;
    }
    function test_artistReleaseCountsAndAlbumRoundTrip() {
        const source = openOnlineArtist();
        const artist = runtime.detailView;
        const picker = findChild(artist, "vinylArtistReleasePicker");
        const total = findChild(artist, "vinylArtistTotalReleaseCount");
        const count = findChild(artist, "vinylArtistDiscographyCount");
        const albums = Array.from({length: 32}, (_, i) => artistRelease("album", i));
        const singles = Array.from({length: 92}, (_, i) => artistRelease("single", i));
        function counts(all, selected) {
            compare(picker.options[0].sub, all);
            compare(total.text, all === "" ? "" : all + " releases");
            compare(picker.current.sub, selected);
            compare(count.text, selected === "" ? "" : selected + " releases");
        }
        compare(picker.currentKey, "Album");
        counts("", "");
        source.artistAlbums = albums.slice(0, 12);
        source.artistSingles = singles.slice(0, 8);
        source.artistCatalogueBusy = false; // overview done; continuations still pending
        counts("", "");
        source.artistAlbums = albums;
        source.artistSingles = singles;
        compare(runtime.artistData.discographyCards.length, 32);
        counts("", "");

        // The shared picker must close synchronously, before committing a new grid.
        const pill = findChild(picker, "vinylArtistReleasePickerPill");
        artist.contentY = pill.mapToItem(artist.contentItem, 0, 0).y - 240;
        picker.openPopup();
        tryCompare(picker, "open", true);
        picker.choose(0);
        verify(!picker.open);
        compare(picker.currentKey, "Album");
        tryCompare(picker, "currentKey", "All");
        picker.choose(1);
        tryCompare(picker, "currentKey", "Album");

        const tile = findChild(artist, "vinylArtistDiscographyCard_0");
        tile.cardRequested(tile.record);
        verify(runtime.openingAlbum);
        source.completed("browse", runtime.session.browseRequest, Object.assign(detailRecord(), {id: tile.record.album.id}), "");
        tryCompare(runtime, "page", "album");

        // Native identity enrichment retains the editions but publishes one card
        // per group. It can finish while the album detail is on screen.
        source.artistIdentity = {releases: albums.map(a => ({browseId: a.browseId, ytShelf: "albums"}))};
        const editions = albums.slice(0, 21).map(a => ({browseId: a.browseId, title: a.title, year: a.year}));
        source.artistAlbums = [Object.assign({}, albums[0], {category: "Album", providerEditions: editions})]
            .concat(albums.slice(21));
        compare(source.artistAlbums.length, 12);
        // Artwork can still be in flight after release/identity work is settled.
        mouseClick(runtime.detailTitleBar.backItem);
        tryCompare(runtime, "page", "artist");
        compare(picker.currentKey, "Album");
        counts("124", "32");
        compare(runtime.artistData.discographyCards.length, 32);
        compare(runtime.artistData.discographyCards.map(c => c.album.browseId).sort(), albums.map(a => a.browseId).sort());

        // One of the restored edition cards opens its own provider release.
        const editionTile = findChild(artist, "vinylArtistDiscographyCard_1");
        editionTile.cardRequested(editionTile.record);
        compare(runtime.session.browseValue, albums[1].browseId);
        source.completed("browse", runtime.session.browseRequest, Object.assign(detailRecord(), {browseId: albums[1].browseId}), "");
        tryCompare(runtime, "page", "album");
        mouseClick(runtime.detailTitleBar.backItem);
        tryCompare(runtime, "page", "artist");
        counts("124", "32");
    }
    function test_artistPopularArrivalKeepsDiscographyStill() {
        const source = openOnlineArtist();
        const artist = runtime.detailView;
        const popular = findChild(artist, "vinylArtistPopularSongs");
        const grid = findChild(artist, "vinylArtistDiscographyGrid");
        const top = findChild(artist, "vinylArtistScrollToTop");
        const create = findChild(artist, "vinylArtistCreatePlaylistAction");
        source.artistAlbums = Array.from({length: 24}, (_, i) => artistRelease("album", i));
        verify(waitForRendering(artist));
        verify(!top.visible);
        compare(popular.shownSongs.length, 0);
        artist.contentY = grid.mapToItem(artist.contentItem, 0, 0).y - 260;
        verify(top.visible);
        const y = artist.contentY;
        const gridY = grid.mapToItem(artist, 0, 0).y;
        const topX = top.mapToItem(runtime.detailTitleBar, 0, 0).x;
        const artistId = runtime.artistData.artistId;
        const track = {id: "exact-track", videoId: "abcdefghijk", title: "Fixture song", seconds: 243};
        const song = {id: "exact-song", origin: "youtube-music", type: "Song", title: track.title, tracks: [track]};
        source.artist = {id: artistId, songs: [{videoId: track.videoId, record: song}]};
        source.artistCatalogueBusy = false;
        tryCompare(popular, "shownSongs", runtime.artistData.popularSongs);
        verify(waitForRendering(artist));
        compare(artist.contentY, y);
        compare(grid.mapToItem(artist, 0, 0).y, gridY);
        compare(top.mapToItem(runtime.detailTitleBar, 0, 0).x, topX);
        verify(create.visible);
        mouseClick(top);
        tryCompare(artist, "contentY", 0);
        verify(!top.visible);

        const row = findChild(popular, "vinylArtistSong_0");
        const wash = findChild(popular, "vinylArtistPopularHover_0");
        const save = findChild(popular, "vinylArtistPopularSave_0");
        const queueAction = findChild(popular, "vinylArtistPopularQueue_0");
        compare(findChild(popular, "vinylArtistPopularDuration_0").text, "4:03");
        verify(save.visible && save.accessibleName === "Save song" && save.parent.personalRow);
        verify(queueAction.visible && queueAction.enabled);
        mouseClick(queueAction);
        compare(runtime.session.queue[0].track.id, track.id);
        artist.contentY = Math.max(0, row.mapToItem(artist.contentItem, 0, 0).y - 230);
        row.forceActiveFocus(Qt.TabFocusReason);
        verify(waitForRendering(artist));
        mouseMove(artist, 2, 300);
        mouseMove(row, 180, row.height / 2);
        tryVerify(() => wash.color.a > 0, 1500, "Pointer should light the row");
        mouseMove(artist, 2, 300);
        tryVerify(() => wash.color.a === 0);
        verify(row.activeFocus, "Focus must not look like a stuck hover");
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
        runtime.session.source = createTemporaryObject(detailSourceFactory, test);
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
        tryCompare(bar, "visible", true);
        verify(runtime.contentBottom < bar.y);
        verify(runtime.browseShell.contentEndPadding > runtime.shellDockBottomExtent + 14);
        runtime.toggleHud();
        tryCompare(bar, "visible", false);
        compare(runtime.browseShell.contentEndPadding, runtime.shellDockBottomExtent + 14);

        runtime.showSong();
        compare(runtime.page, "song");
        compare(runtime.hudOpen, false);
        compare(bar.visible, true);
        runtime.goBack();
        tryCompare(bar, "visible", false);

        runtime.toggleHud();
        runtime.lifecycleActive = false;
        runtime.lifecycleActive = true;
        compare(runtime.hudOpen, false);
        tryCompare(bar, "visible", false);
        compare(runtime.session.playingIndex, 0, "Re-entry must not clear the queue");
    }
    function test_trackRowStartsSlimHudAndMinimisePreservesPlayback() {
        const source = prepareDetailRuntime();
        const record = detailRecord();
        record.tracks.push({id: "second-track", title: "Second song", seconds: 24, available: true});
        runtime.shellDockBottomExtent = 80;
        runtime.openAlbum(record.id);
        source.completed("browse", runtime.session.browseRequest, record, "");
        tryCompare(runtime, "page", "album");
        const album = runtime.detailView;
        const bar = findChild(runtime, "vinylPlayerBar");
        verify(!runtime.hudOpen);
        const first = findChild(album, "vinylTrackRow_0");
        verify(first);
        album.contentY = first.mapToItem(album.contentItem, 0, 0).y - 300;
        verify(waitForRendering(album));
        mouseClick(first);
        compare(runtime.hudOpen, true, "Playing a row outside the song page opens the HUD");
        tryCompare(bar, "dockProgress", 0);
        verify(bar.visible && bar.height <= 80);
        compare(runtime.session.sourceState, "finding", runtime.session.error);
        compare(findChild(bar, "vinylBarTitle").text, "Fixture song");
        verify(findChild(bar, "vinylBarLoading").running);
        verify(album.contentEndPadding >= bar.height + 14);
        album.contentY = album.contentHeight - album.height;
        tryCompare(album, "atYEnd", true);
        const second = findChild(album, "vinylTrackRow_1");
        verify(second);
        verify(second.mapToItem(runtime, 0, second.height).y <= bar.y - 14);
        album.contentY = second.mapToItem(album.contentItem, 0, 0).y - 300;
        verify(second.mapToItem(runtime, 0, 0).y >= runtime.detailTitleBar.height);
        verify(second.mapToItem(runtime, 0, second.height).y <= bar.y - 14);

        const minimise = findChild(bar, "vinylBarMinimise");
        verify(minimise && minimise.visible);
        mouseClick(minimise);
        compare(runtime.hudOpen, false);
        tryCompare(bar, "visible", false);
        compare(runtime.session.playingIndex, 0);
        compare(runtime.session.sourceState, "finding");
        runtime.session.playQueue(0); // Restarts resolution for the same occurrence.
        compare(runtime.hudOpen, false);

        mouseClick(second);
        compare(runtime.hudOpen, true, "A new play opens the bar again");
        tryCompare(bar, "dockProgress", 0);
        compare(findChild(bar, "vinylBarTitle").text, "Second song");
        compare(runtime.page, "album");
    }
    function test_exploreSongStartsSlimHudAndKeepsLatestSelection() {
        const source = prepareDetailRuntime();
        runtime.worldView.setTab("explore");
        const page = findChild(runtime, "vinylExplorePage");
        verify(page);
        tryVerify(() => source.calls["explore-tag-chill"]);
        const songs = ["Sweater Weather", "Amber"].map((title, i) => ({id: "explore-song-" + i,
            title: title, artist: "Fixture artist", source: "lastfm", kind: "songs", catalogueCard: true,
            cover: String(Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg"))}));
        for (const row of page.firstRows) {
            if (row.tag === "chill") source.completed(row.lane, source.calls[row.lane].id,
                {version: 1, kind: "songs", source: "lastfm", list: "chill", items: songs}, "");
            else source.completed(row.lane, source.calls[row.lane].id, {}, "Fixture unavailable");
        }
        const first = findChild(page, "vinylCard_explore-chill_0");
        const second = findChild(page, "vinylCard_explore-chill_1");
        const bar = findChild(runtime, "vinylPlayerBar");
        const title = findChild(bar, "vinylBarTitle");
        const spinner = findChild(bar, "vinylBarLoading");
        const status = findChild(runtime, "vinylStatus");
        tryCompare(first, "visible", true);
        const route = runtime.page;
        verify(route !== "song" && !runtime.hudOpen);
        verify(waitForRendering(first));
        mouseClick(first);
        compare(runtime.hudOpen, true, "Explore must open the HUD before the catalogue request replies");
        compare(runtime.page, route);
        tryCompare(bar, "dockProgress", 0);
        verify(bar.visible && bar.slim && spinner.running);
        compare(title.text, songs[0].title);
        verify(!findChild(runtime, "vinylLookupIndicator").visible);
        compare(status.text, "");
        const stale = source.calls["catalogue-item"];
        mouseClick(second);
        const current = source.calls["catalogue-item"];
        verify(current.id > stale.id && source.canceled.includes("catalogue-item"));
        compare(current.operation, "resolveCatalogueItem");
        compare(current.value, songs[1].id);
        compare(title.text, songs[1].title);
        function matched(song) {
            const record = detailRecord();
            record.tracks[0].title = song.title;
            return {version: 1, id: song.id, kind: "songs", matched: true, record: record};
        }
        source.completed("catalogue-item", stale.id, matched(songs[0]), "");
        compare(runtime.session.playingIndex, -1);
        wait(350);
        verify(bar.visible && spinner.running);
        compare(status.text, "");
        source.completed("catalogue-item", current.id, matched(songs[1]), "");
        compare(runtime.session.playingIndex, 0);
        compare(runtime.session.sourceState, "finding", runtime.session.error);
        compare(runtime.hudOpen, true);
        compare(runtime.page, route, "A successful Explore lookup must stay on Explore");
        compare(runtime.worldView.currentTab, "explore");
        compare(bar.pendingSong, null);
        compare(title.text, songs[1].title);
        verify(bar.slim && spinner.running);
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
        bar.enabled = true;
        bar.dockProgress = 0;
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
