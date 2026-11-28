import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylArtistOnboarding"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var collection: null
    property var progress: null
    property var preferences: null
    property var service: null

    Component {
        id: windowFactory
        Window { width: 960; height: 720; visible: true }
    }

    Component {
        id: collectionFactory
        QtObject {
            property var entries: []
            property int revision: 0
            function items(worldName) { return entries; }
            function has(worldName, id) { return entries.some(function(entry) { return entry.id === id; }); }
            function add(worldName, entry) { entries = entries.concat([entry]); revision++; return true; }
            function replaceItems(worldName, removeIds, replacements) {
                entries = entries.filter(function(entry) { return (removeIds || []).indexOf(entry.id) < 0; })
                    .concat(replacements || []);
                revision++;
                return true;
            }
        }
    }

    Component {
        id: progressFactory
        QtObject {
            property var entries: []
            property int revision: 0
            function recent(kind, limit) { return entries.filter(function(entry) { return entry.kind === kind; }).slice(-limit); }
            function get(kind, id) { return entries.find(function(entry) { return entry.kind === kind && entry.id === id; }) || null; }
        }
    }

    Component {
        id: preferencesFactory
        QtObject {
            property bool vinylTasteAnswered: false
            property var vinylTasteArtistIds: []
            property var vinylTasteArtists: []
            property string vinylPersistenceError: ""
            property bool failWrites: false
            property int writeCalls: 0
            signal vinylTasteChanged()

            function setVinylTasteSelections(answered, artists) {
                writeCalls++;
                if (failWrites) {
                    vinylPersistenceError = "Synthetic preference write failure.";
                    return false;
                }
                vinylTasteAnswered = answered;
                vinylTasteArtists = artists.slice();
                vinylTasteArtistIds = artists.map(function(artist) { return artist.providerId; });
                vinylPersistenceError = "";
                vinylTasteChanged();
                return true;
            }
        }
    }

    Component {
        id: soundtrackSourceFactory
        QtObject {
            property var record: ({id:"fixture:aot:ost", origin:"online", title:"Attack on Titan Original Soundtrack",
                artists:["Hiroyuki Sawano"], tracks:[
                    {id:"fixture:aot:vogel-im-kafig", title:"Vogel im Käfig", seconds:380},
                    {id:"fixture:aot:ətˈæk", title:"ətˈæk 0N tάɪtn", seconds:257}
                ]})
            property var requests: []
            signal completed(string lane, int requestId, var data, string error)
            function request(lane, operation, value, options) {
                requests = requests.concat([{lane:lane, operation:operation, value:value}]);
                return requests.length;
            }
            function cancel(lane: string): void {}
            function setAlbums(albums) {}
        }
    }

    function artist(providerId, name) {
        return {
            key: JSON.stringify(["youtube-music", "artist", providerId]),
            kind: "artist",
            provider: "youtube-music",
            providerId: providerId,
            name: name,
            image: "",
            mappingStatus: "unresolved",
            id: "ytm:artist:" + providerId,
            title: name,
            cover: ""
        };
    }

    function picker() { return findChild(world, "vinylTastePicker"); }
    function pickArtists(count) {
        var rows = [];
        for (var i = 0; i < count; ++i) {
            var ref = artist("UC" + String.fromCharCode(97 + i).repeat(22), "Native Artist " + i);
            rows.push(ref);
            service.toggleDraftArtist(ref);
        }
        return rows;
    }
    function artistKeys(values) { return values.map(function(value) { return value.key; }).sort(); }
    function typeText(value) {
        for (var i = 0; i < value.length; ++i) {
            var character = value.charAt(i);
            keyClick(character === " " ? Qt.Key_Space : Qt["Key_" + character.toUpperCase()]);
        }
    }

    function init() {
        if (typeof NativeSource === "undefined") {
            skip("Requires the vinyl_native_session_bridge runner.");
            return;
        }

        host = windowFactory.createObject(test);
        collection = collectionFactory.createObject(test);
        progress = progressFactory.createObject(test);
        preferences = preferencesFactory.createObject(test);

        var component = Qt.createComponent("../../qml/vinyl/VinylAppWorld.qml");
        compare(component.status, Component.Ready, component.errorString());
        world = component.createObject(host.contentItem, {width:960, height:720});
        verify(world !== null);
        world.session.source = NativeSource;
        world.session.collectionStore = collection;
        world.session.progressStore = progress;
        world.worldData.preferencesStore = preferences;
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();

        service = world.recommendationService;
        verify(service && typeof service.beginHomeVisit === "function",
            "LiveWorld must own the native recommendation service.");
        service.suggestions = [artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist"), artist("UCbbbbbbbbbbbbbbbbbbbbbb", "Second Artist")];
        wait(0);
    }

    function cleanup() {
        if (world) {
            world.destroy();
            wait(0);
            world = null;
        }
        if (host) {
            host.close();
            host.destroy();
            wait(0);
            host = null;
        }
        if (collection) { collection.destroy(); wait(0); collection = null; }
        if (progress) { progress.destroy(); wait(0); progress = null; }
        if (preferences) { preferences.destroy(); wait(0); preferences = null; }
        service = null;
    }

    function test_freshProfileCanOpenHomeBeforeMakingAnyPicks() {
        world.showHome();
        verify(world.canEnterVinyl);
        compare(world.page, "home");
        compare(service.selectedArtists.length, 0);
        compare(service.onboardingState, "unanswered");
        verify(!picker().visible);
        verify(findChild(world, "vinylCrateTasteSetup").visible);
        verify(findChild(world, "vinylSpin").visible);
        verify(findChild(world, "vinylFlowTabs").visible);
        world.enterDiscoverSearch(true, false);
        tryCompare(world, "searchOpen", true);
        world.closeSearch();
        for (var tab of ["discover", "explore", "library", "home"]) {
            world.worldView.setTab(tab);
            compare(world.page, tab);
        }
        compare(preferences.writeCalls, 0);
    }

    function test_externalDetailOpensWithoutPlayback_data() {
        return [
            {tag:"cold-song", kind:"song", editing:false},
            {tag:"cold-album", kind:"album", editing:false},
            {tag:"warm-song-while-editing", kind:"song", editing:true},
            {tag:"warm-album-while-editing", kind:"album", editing:true},
            {tag:"warm-artist-while-editing", kind:"artist", editing:true}
        ];
    }

    function test_externalDetailOpensWithoutPlayback(data) {
        var source = createTemporaryObject(soundtrackSourceFactory, test);
        world.session.source = source;
        var record = source.record;
        var track = record.tracks[1];
        var artistId = "ytm:artist:UCaaaaaaaaaaaaaaaaaaaaaa";
        if (data.editing) {
            world.showHome();
            findChild(world, "vinylCrateTasteSetup").activate(Qt.MouseFocusReason);
            tryVerify(function() { return picker().visible; });
        }
        compare(service.selectedArtists.length, 0);
        world.openNamedCollectionReference({route:"vinyl-" + data.kind + "-detail",
            source:"youtube-music", title:data.kind === "artist" ? record.artists[0] : "",
            sourceId:data.kind === "song" ? track.id : data.kind === "artist" ? artistId : record.id,
            editionId:record.id, payload:{albumId:record.id, browseId:"fixture:aot:browse"}});
        if (data.kind !== "artist") {
            var request = source.requests.find(req => req.lane === "named-collections");
            verify(!!request, "The external album lookup must run without any picks.");
            compare(request.operation, "album");
            compare(request.value, "fixture:aot:browse");
            source.completed(request.lane, world.namedCollectionRequestId, record, "");
        }
        tryCompare(world, "page", data.kind);
        verify(world.detailView.visible);
        if (data.kind === "song") {
            compare(world.detailView.pageData.rawTrack.id, track.id);
            compare(world.detailView.pageData.rawTrack.title, track.title);
        } else if (data.kind === "album") {
            compare(world.session.shownEditionId, record.id);
            compare(world.session.shownEdition.name, record.title);
        } else {
            compare(world.artistData.artistId, artistId);
        }
        verify(!picker().visible);
        compare(world.session.queue.length, 0);
        compare(world.session.playingIndex, -1);
        compare(preferences.writeCalls, 0);
        world.session.source = NativeSource;
    }

    function test_optionalPickerSavesOneArtist() {
        world.showHome();
        findChild(world, "vinylCrateTasteSetup").activate(Qt.MouseFocusReason);
        tryVerify(function() { return picker().visible; });
        var done = findChild(world, "vinylPickerDone");
        verify(!done.enabled);
        var chosen = pickArtists(1);
        tryCompare(done, "enabled", true);
        done.activate(Qt.MouseFocusReason);
        compare(preferences.writeCalls, 1);
        compare(artistKeys(service.selectedArtists), artistKeys(chosen));
        verify(world.canEnterVinyl);
        verify(!picker().visible);
    }

    function test_profilesWithOneToFourSavedArtistsStayPrefilled() {
        var legacyArtists = [];
        for (var i = 0; i < 4; ++i)
            legacyArtists.push(artist("UC" + String.fromCharCode(97 + i).repeat(22), "Legacy Artist " + i));

        preferences.vinylTasteAnswered = true;
        for (var count = 1; count <= 4; ++count) {
            var saved = legacyArtists.slice(0, count);
            preferences.vinylTasteArtists = saved;
            preferences.vinylTasteArtistIds = saved.map(function(value) { return value.providerId; });
            preferences.vinylTasteChanged();
            world.showHome();

            verify(world.canEnterVinyl);
            verify(!picker().visible);
            world.worldData.beginTasteEdit();
            tryCompare(service, "onboardingState", "editing");
            tryCompare(service, "draftArtistCount", count);
            tryVerify(function() { return picker().visible; });
            compare(artistKeys(service.draftArtists), artistKeys(saved),
                "Existing selections remain prefilled when editing.");
            compare(picker().selectedArtistCount, count);
            verify(findChild(world, "vinylPickerDone").enabled);
            world.worldData.cancelTasteEdit();
            verify(world.canEnterVinyl);
        }
    }

    function test_profileWithFiveSavedArtistsKeepsItsChoices() {
        world.worldData.beginTasteEdit();
        tryCompare(service, "onboardingState", "editing");
        pickArtists(5);
        verify(service.commitTaste(false));
        world.showHome();
        verify(world.canEnterVinyl);
        compare(service.selectedArtists.length, 5);
        verify(!picker().visible);
        tryVerify(function() { return findChild(world, "vinylCrateTasteSetup") === null; });
        world.worldView.setTab("discover");
        tryCompare(world.worldView, "currentTab", "discover");
    }

    function test_optionalDraftSurvivesTabRoundTripAndCancelWritesNothing() {
        world.showHome();
        world.worldData.beginTasteEdit();
        tryCompare(service, "onboardingState", "editing");
        var selected = artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist");
        service.toggleDraftArtist(selected);
        compare(artistKeys(service.draftArtists), [selected.key]);
        world.worldView.setTab("discover");
        compare(world.worldView.currentTab, "discover");
        verify(!picker().visible);
        world.worldView.setTab("home");
        verify(picker().visible);
        compare(artistKeys(service.draftArtists), [selected.key]);
        world.worldData.cancelTasteEdit();
        compare(service.onboardingState, "unanswered");
        compare(preferences.writeCalls, 0);
        compare(preferences.vinylTasteAnswered, false);
        verify(world.canEnterVinyl);
    }

    function test_searchPagesKeepSelection() {
        world.showHome();
        world.worldData.beginTasteEdit();
        tryCompare(service, "onboardingState", "editing");
        var selected = artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist");
        service.toggleDraftArtist(selected);

        var states = Object.assign({}, world.worldData.browseStates);
        states.discover = Object.assign({}, states.discover, {query:"ordinary Discover query"});
        world.worldData.browseStates = states;
        findChild(world, "vinylTopBar").searchClicked();
        wait(0);

        compare(world.page, "home");
        compare(world.worldView.currentTab, "home");
        verify(!world.searchOpen, "The top search entry must keep the dedicated artist picker visible.");
        compare(world.searchPage, null, "Artist setup must not create a general catalogue search page.");
        var sharedSearch = findChild(world, "vinylPickerArtistSearch");
        verify(sharedSearch !== null && sharedSearch.visible,
            "Artist setup and editing retain their dedicated artist search field.");
        tryCompare(sharedSearch, "activeFocus", true);

        sharedSearch.forceActiveFocus();
        typeText("native");
        tryVerify(function() { return service.artistResults.query === "native"; });
        keyClick(Qt.Key_Return);
        sharedSearch.selectAll();
        keyClick(Qt.Key_Backspace);
        typeText("native artist");
        tryVerify(function() { return service.artistResults.query === "native artist"; });
        compare(artistKeys(service.draftArtists), [selected.key],
            "Changing search pages must preserve the selected artist draft.");

        sharedSearch.selectAll();
        keyClick(Qt.Key_Backspace);
        tryVerify(function() { return service.artistResults.query === ""; });
        verify(world.worldData.artistResultItems.some(function(artist) { return artist.key === selected.key; }),
            "Clearing artist search should restore the merged no-query suggestions.");
        compare(world.worldData.browseStates.discover.query, "ordinary Discover query",
            "Artist search must not replace the saved Discover query.");
    }

    function test_closingCatalogueBeforeArtistEditKeepsSearchFocus() {
        world.worldData.beginTasteEdit();
        pickArtists(5);
        verify(service.commitTaste(false));
        world.showHome();
        world.enterDiscoverSearch(true, false);
        tryCompare(world, "searchOpen", true);
        tryVerify(function(){return world.searchPage !== null;});

        world.closeSearch();
        world.worldData.beginTasteEdit();

        var field=findChild(world, "vinylPickerArtistSearch");
        verify(field !== null);
        tryCompare(field, "activeFocus", true);
        verify(!world.searchOpen, "A queued catalogue-close focus must not steal artist-search focus.");
        compare(service.draftArtistCount, 5);
        compare(preferences.writeCalls, 1);
    }

    function test_editCancelDoesNotEraseTaste() {
        world.showHome();
        world.worldData.beginTasteEdit();
        tryCompare(service, "onboardingState", "editing");
        var saved = pickArtists(5);
        verify(service.commitTaste(false));
        compare(service.onboardingState, "answered");
        compare(artistKeys(service.selectedArtists), artistKeys(saved));

        world.showHome();
        wait(0);
        // The house shell replaced the legacy Home toolbar (051a87f14).
        // Exercise its shared taste-edit command, not the retired page's button.
        world.worldData.beginTasteEdit();
        tryVerify(function() { return picker().visible; });
        compare(service.onboardingState, "editing");
        service.toggleDraftArtist(saved[1]);
        compare(service.draftArtistCount, 4);
        verify(findChild(world, "vinylPickerDone").enabled);
        var writesBeforeCancel = preferences.writeCalls;
        var cancel = findChild(world, "vinylPickerCancel");
        verify(cancel !== null && cancel.visible);
        cancel.activate(Qt.MouseFocusReason);

        compare(service.onboardingState, "answered");
        compare(artistKeys(service.selectedArtists), artistKeys(saved));
        compare(artistKeys(preferences.vinylTasteArtists), artistKeys(saved));
        compare(preferences.writeCalls, writesBeforeCancel);
    }

    function test_emptySuggestionsCanBeDismissedWithoutAnswerOrWrites() {
        service.suggestions = [];
        world.showHome();
        world.worldData.beginTasteEdit();
        tryCompare(service, "onboardingState", "editing");
        verify(picker().visible);
        verify(!findChild(world, "vinylPickerDone").enabled);
        verify(!service.commitTaste(false), "An empty draft cannot be saved as artist choices.");
        compare(preferences.writeCalls, 0);
        world.worldData.skipPicker();
        compare(service.onboardingState, "unanswered");
        verify(!picker().visible);
        verify(world.canEnterVinyl);
        compare(service.selectedArtists.length, 0);
        compare(preferences.vinylTasteAnswered, false);
        compare(preferences.vinylTasteArtists.length, 0);
        compare(preferences.writeCalls, 0);
    }

    function test_lateSearchCannotChangeAnotherProfile() {
        world.showHome();
        world.worldData.beginTasteEdit();
        tryCompare(service, "onboardingState", "editing");
        var selected = artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist");
        service.toggleDraftArtist(selected);
        service.searchArtists("old profile", {});
        var oldGeneration = world.session.profileGeneration;

        // A profile lifecycle generation change invalidates every in-flight result.
        var backend = findChild(world, "vinylBackendSession");
        verify(backend !== null);
        backend.profileScope = "another-profile";
        world.session.profileGeneration = oldGeneration + 1;
        compare(world.session.profileScope, "another-profile");
        tryCompare(service, "profileScope", "another-profile");
        tryCompare(service, "profileGeneration", oldGeneration + 1);
        wait(0);
        compare(service.profileGeneration, oldGeneration + 1);
        verify(service.artistResults.query !== "old profile",
            "A late result from the previous profile must not replace current results.");
        compare(service.draftArtists.length, 0,
            "A profile switch must clear the previous profile's onboarding draft.");
    }
}
