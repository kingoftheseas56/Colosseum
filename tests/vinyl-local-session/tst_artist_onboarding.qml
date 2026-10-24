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
            signal vinylTasteChanged()

            function setVinylTasteSelections(answered, artists) {
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

        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
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
        world.showLibrary();
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

    function test_firstHomeOnly() {
        compare(service.onboardingState, "unanswered");
        verify(picker() === null || !picker().visible,
            "Creating or opening Library must not start onboarding.");

        world.worldView.setTab("discover");
        wait(0);
        compare(service.onboardingState, "unanswered");
        verify(picker() === null || !picker().visible,
            "Discover must remain available before the first Home visit.");

        world.worldView.setTab("home");
        wait(0);
        compare(service.onboardingState, "draft");
        verify(picker() !== null && picker().visible,
            "An unanswered profile opens the picker on its first Home visit.");
    }

    function test_leaveAndReturnKeepsDraft() {
        world.showHome();
        tryCompare(service, "onboardingState", "draft");
        var selected = artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist");
        service.toggleDraftArtist(selected);
        compare(artistKeys(service.draftArtists), [selected.key]);

        world.worldView.setTab("discover");
        wait(0);
        verify(picker() === null || !picker().visible);
        world.worldView.setTab("home");
        wait(0);
        verify(picker().visible);
        compare(artistKeys(service.draftArtists), [selected.key]);
    }

    function test_searchPagesKeepSelection() {
        world.showHome();
        tryCompare(service, "onboardingState", "draft");
        var selected = artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist");
        service.toggleDraftArtist(selected);

        var states = Object.assign({}, world.worldData.browseStates);
        states.discover = Object.assign({}, states.discover, {query:"ordinary Discover query"});
        world.worldData.browseStates = states;
        findChild(world, "vinylTopBar").searchClicked();
        wait(0);

        compare(world.page, "home");
        compare(world.worldView.currentTab, "home");
        verify(world.searchOpen, "The shared top search entry should open artist search in the picker.");
        verify(world.searchPage !== null && world.searchPage.artistPicker,
            "Onboarding artist search should use the shared search page in picker mode.");
        compare(findChild(world, "vinylPickerSearch"), null,
            "The picker must not add a second search field.");
        var sharedSearch = findChild(world, "searchSurfaceInput");
        verify(sharedSearch !== null && sharedSearch.visible,
            "Artist search should reuse the shared search page field.");

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

    function test_editCancelDoesNotEraseTaste() {
        world.showHome();
        tryCompare(service, "onboardingState", "draft");
        var saved = artist("UCaaaaaaaaaaaaaaaaaaaaaa", "Native Artist");
        service.toggleDraftArtist(saved);
        verify(service.commitTaste(false));
        compare(service.onboardingState, "answered");
        compare(artistKeys(service.selectedArtists), [saved.key]);

        world.showHome();
        wait(0);
        var edit = findChild(world, "vinylTasteEdit");
        verify(edit !== null && edit.visible,
            "The answered Home should expose Edit tastes.");
        edit.activate(Qt.MouseFocusReason);
        compare(service.onboardingState, "editing");
        service.toggleDraftArtist(artist("UCbbbbbbbbbbbbbbbbbbbbbb", "Second Artist"));
        var cancel = findChild(world, "vinylPickerCancel");
        verify(cancel !== null && cancel.visible);
        cancel.activate(Qt.MouseFocusReason);

        compare(service.onboardingState, "answered");
        compare(artistKeys(service.selectedArtists), [saved.key]);
        compare(artistKeys(preferences.vinylTasteArtists), [saved.key]);
    }

    function test_noNetworkStillAllowsSkip() {
        service.suggestions = [];
        world.showHome();
        tryCompare(service, "onboardingState", "draft");
        verify(picker().visible);

        var skip = findChild(world, "vinylPickerSkip");
        verify(skip !== null && skip.visible && skip.enabled);
        skip.activate(Qt.MouseFocusReason);
        compare(service.onboardingState, "answered");
        compare(service.selectedArtists.length, 0);
        compare(preferences.vinylTasteAnswered, true);
        compare(preferences.vinylTasteArtists.length, 0);
    }

    function test_lateSearchCannotChangeAnotherProfile() {
        world.showHome();
        tryCompare(service, "onboardingState", "draft");
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
