import QtQuick
import QtTest
import "../../qml" as House
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylShellTabs"
    when: windowShown
    visible: true
    width: 1280
    height: 720
    property var view: null
    property var gateData: null
    Component {
        id: gateDataFactory
        QtObject {
            property var session: null
            property var heroSlides: [{kind: "topAlbum", item: {id: "fixture-album",
                kind: "albums", title: "Fixture album", artist: "Fixture artist", cover: ""}, sideItems: []}]
            property var homeRows: []
            property var coldShelves: []
            property var shelves: []
            property var personalLibrary: null
            property string activeTab: "home"
            property string lensTitle: "Everything"
            property var lens: ({kind:"albums",genre:"",decade:"",country:"",language:""})
            property bool catalogueReachable: true
            property int minimumArtistCount: 5
            property bool usesRecommendationService: false
            property var recommendationService: null
            property bool canEnterVinyl: !tasteGateLocked
            property bool tasteGateLocked: false
            property bool tastePickerOpen: false
            property bool pickerAnswered: true
            property string onboardingState: "answered"
            property string pickerQuery: ""
            property var pickerDraft: []
            property var savedPicks: []
            property int beginTasteEditCalls: 0
            property string artistSearchError: ""
            property bool artistSearchLoading: false
            property bool artistSearchHasMore: false
            property bool discoveryLanding: false
            property bool onlineMode: false
            property bool discoveryLoading: false
            property string discoveryError: ""
            property bool discoveryCached: false
            property string discoveryFetchedAt: ""
            property bool cataloguePagingSupported: true
            property bool searchHasMore: false
            property bool searchLoading: false
            property string searchError: ""
            property string homeStatus: "ready"
            property string homeError: ""
            property var offeredArtists: []
            signal listRequested(string key, bool home)
            signal artistSearchFocusRequested()
            function artistOptions(query) { return offeredArtists; }
            function selectTab(tab) { activeTab=tab; }
            function beginTasteEdit() {
                beginTasteEditCalls++;
                pickerDraft=savedPicks.slice();
                onboardingState="editing";
                tastePickerOpen=true;
            }
            function openList(key,home) {
                if(home && key==="taste-artists") {
                    beginTasteEdit();
                    artistSearchFocusRequested();
                    return true;
                }
                listRequested(key,home);
                return false;
            }
            function row(key,home) { return {key:key,title:key,items:[]}; }
            function listItems(key) { return []; }
            function artwork(cover) { return ""; }
            function activate(item) {}
            function resume() {}
            function openHomeDestination(key) {}
            function togglePickerArtist(artist) {
                if (pickerDraft.includes(artist.key))
                    pickerDraft = pickerDraft.filter(key => key !== artist.key);
                else
                    pickerDraft = pickerDraft.concat([artist.key]);
            }
            function cancelTasteEdit() {
                pickerDraft=savedPicks.slice();
                onboardingState="answered";
                tastePickerOpen=false;
            }
            function skipPicker() {}
            function answerPicker() {
                if (pickerDraft.length < minimumArtistCount) return;
                savedPicks = pickerDraft.slice();
                onboardingState = "answered";
                pickerAnswered = true;
                tasteGateLocked = false;
                tastePickerOpen = false;
            }
            function retryArtistSearch() {}
            function loadMoreArtistSearch() {}
            function setKind(kind) { lens=Object.assign({},lens,{kind:kind}); }
            function filterOptions(key) { return []; }
        }
    }

    Rectangle { id: wallpaper; anchors.fill: parent; color: "#101015" }
    Component {
        id: shellFactory
        Vinyl.VinylHouseShell {
            width: test.width
            height: test.height
            backdrop: wallpaper
        }
    }
    Component {
        id: compatibilityTabs
        House.WorldTabBar {
            width: 600
            backdrop: wallpaper
            tabPrefix: "compatTab"
            tabModel: [{key:"home",label:"Home"},
                       {key:"library",label:"Library",automationKey:"Library"}]
        }
    }
    function init() {
        gateData = gateDataFactory.createObject(test);
        verify(gateData !== null);
        view = shellFactory.createObject(test, {worldData:gateData});
        verify(view !== null);
        tryCompare(view.flowTabs, "visible", true);
        verify(waitForRendering(view));
    }
    function cleanup() {
        if (view) { view.destroy(); wait(0); view = null; }
        if (gateData) { gateData.destroy(); wait(0); gateData = null; }
    }
    function clickTab(name, expected) {
        var tab = findChild(view, "vinylTab_" + name);
        verify(tab !== null);
        verify(tab.visible);
        verify(waitForRendering(tab));
        var at = tab.mapToItem(view, 0, 0);
        verify(at.y >= 0 && at.y + tab.height <= view.height);
        mouseClick(tab);
        tryCompare(view, "activeTab", expected);
    }
    function namedCount(node, name) {
        var count = node.objectName === name ? 1 : 0;
        var children = node.children || [];
        for (var i = 0; i < children.length; ++i) count += namedCount(children[i], name);
        return count;
    }
    function test_threeTabIdentityOrderAndNoLegacyShell() {
        compare(view.objectName, "vinylWorld");
        compare(view.medium, "Vinyl");
        compare(view.board, view);
        var actual = view.flowTabs.tabModel;
        compare(actual.length, 3);
        compare(JSON.stringify(actual.map(function(tab) { return tab.key; })),
                JSON.stringify(["home", "discover", "library"]));
        compare(JSON.stringify(actual.map(function(tab) { return tab.label; })),
                JSON.stringify(["Home", "Discover", "Library"]));
        compare(view.currentTab, "home");
        compare(view.activeTab, "Home");
        compare(findChild(view, "vinylWorldScroll"), view.pageFlickable);
        ["vinylLiveWorld", "vinylPrototypeWindow", "vinylBrowseShell", "vinylCarousel",
         "vinylHomePage", "vinylLibraryPage"].forEach(function(name) {
            compare(findChild(view, name), null, name + " must not be instantiated");
        });
        ["Home", "Discover", "Library"].forEach(function(name) {
            compare(namedCount(view, "vinylTab_" + name), 1);
        });
    }
    function test_pointerSwitchesLibraryDiscoverHome() {
        clickTab("Library", "Library");
        compare(gateData.activeTab, "library");
        clickTab("Discover", "Discover");
        compare(gateData.activeTab, "discover");
        verify(findChild(view, "vinylDiscoverPage").visible);
        clickTab("Home", "Home");
        compare(gateData.activeTab, "home");
    }
    function test_readinessRequiresBothOwners() {
        verify(view.componentReady);
        verify(!view.ready);
        view.runtimeReady = true;
        verify(!view.ready);
        view.hostReady = true;
        verify(view.ready);
        view.runtimeReady = false;
        verify(!view.ready);
        view.runtimeReady = true;
        view.hostReady = false;
        verify(!view.ready);
    }
    function lockTasteGate() {
        gateData.tasteGateLocked = true;
        gateData.pickerAnswered = false;
        gateData.onboardingState = "draft";
        gateData.tastePickerOpen = true;
        tryCompare(view, "navigationEnabled", false);
    }
    function test_gateAndStricterRuntimeFenceKeepHomeReachable() {
        clickTab("Library", "Library");
        lockTasteGate();
        tryCompare(view, "activeTab", "Home");
        compare(view.flowTabs.visible, false);
        compare(view.dockedTabBar.visible, false);
        compare(view.tabBarSource, null);
        verify(findChild(view, "vinylTastePicker").visible);
        view.setTab("discover");
        compare(view.activeTab, "Home");
        gateData.tasteGateLocked = false;
        gateData.pickerAnswered = true;
        gateData.onboardingState = "answered";
        gateData.tastePickerOpen = false;
        clickTab("Library", "Library");
        view.navigationEnabled = false;
        tryCompare(view, "activeTab", "Home");
        clickTab("Library", "Home");
        view.setTab("discover");
        compare(view.activeTab, "Home");
    }
    function test_gatedPickerCanSaveAfterScrollingArtists() {
        gateData.offeredArtists = Array.from({length: 70}, (_, i) => ({key: "artist-" + i, title: "Artist " + i}));
        lockTasteGate();
        var flick = view.pageFlickable;
        var picker = findChild(view, "vinylTastePicker");
        var done = findChild(picker, "vinylPickerDone");
        function fitsDone() {
            var y = done.mapToItem(flick, 0, 0).y;
            return done.visible && y >= 0 && y + done.height <= flick.height;
        }
        verify(waitForRendering(picker));
        compare(flick.contentY, 0, "The gated picker must start at the top of its page");
        verify(fitsDone(), "Save must fit in the initial picker viewport");
        verify(!done.enabled);
        for (var i = 0; i < 5; ++i) {
            var card = findChild(picker, "vinylPickerArtist_" + i);
            verify(card !== null);
            var y = card.mapToItem(flick, 0, 0).y;
            verify(y >= 0 && y + card.height <= flick.height,
                   "Artist card must be fully in the viewport: y=" + y + " height=" + card.height);
            mouseClick(card);
            compare(gateData.pickerDraft.length, i + 1);
        }
        verify(done.enabled);
        mouseWheel(flick, flick.width / 2, flick.height / 2, 0, -480);
        tryVerify(function() { return flick.contentY > 200 && !fitsDone(); });
        mouseWheel(flick, flick.width / 2, flick.height / 2, 0, 480);
        tryCompare(flick, "contentY", 0);
        verify(fitsDone());
        mouseClick(done);
        compare(gateData.savedPicks, ["artist-0", "artist-1", "artist-2", "artist-3", "artist-4"]);
        tryCompare(view, "navigationEnabled", true);
        compare(picker.visible, false);
    }
    function test_discoverListDetourAndBack() {
        clickTab("Discover", "Discover");
        var origin = view.pageFlickable.contentY;
        gateData.listRequested("fixture-list", false);
        tryCompare(view, "listKey", "fixture-list");
        var page = findChild(view, "vinylListPage");
        verify(page.visible);
        compare(page.title, "fixture-list");
        var back = findChild(view, "vinylListBack");
        verify(waitForRendering(back));
        mouseClick(back);
        tryCompare(view, "listKey", "");
        compare(view.activeTab, "Discover");
        tryVerify(function() { return Math.abs(view.pageFlickable.contentY - origin) < 1; });
    }
    function test_contentVisibilityHidesBodyAndDockSource() {
        compare(view.contentVisible, true);
        compare(view.tabBarSource, view.flowTabs);
        view.contentVisible = false;
        verify(view.visible);
        tryCompare(view.flowTabs, "visible", false);
        var homeContent = findChild(view, "vinylHome");
        verify(homeContent !== null);
        verify(!homeContent.visible);
        compare(view.tabBarSource, null);
        verify(!view.dockedTabBar.visible);
        view.contentVisible = true;
        tryCompare(view.flowTabs, "visible", true);
        compare(view.tabBarSource, view.flowTabs);
        clickTab("Discover", "Discover");
    }
    function test_optionalAutomationKeyPreservesExistingCallers() {
        var bar = createTemporaryObject(compatibilityTabs, test);
        verify(bar !== null);
        verify(findChild(bar, "compatTab_home") !== null);
        verify(findChild(bar, "compatTab_Library") !== null);
        compare(findChild(bar, "compatTab_library"), null);
    }
}
