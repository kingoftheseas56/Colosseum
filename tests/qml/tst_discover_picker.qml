import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml" as UI

// Shared default contract plus overlay behavior required by clipped artist hosts.
TestCase {
    id: testCase
    name: "DiscoverPicker"
    when: windowShown
    property var host
    property var picker
    property var chosen: []
    property int clears: 0
    property int underlayClicks: 0

    Component {
        id: fixture
        Window {
            width: 640; height: 480; visible: true
            property alias picker: control
            property alias flick: flick
            property alias other: other
            property int downEvents: 0
            MouseArea {
                anchors.fill: parent
                onClicked: testCase.underlayClicks++
            }
            Flickable {
                id: flick
                x: 20; y: 20; width: 600; height: 440
                contentWidth: width; contentHeight: 1000; clip: true
                Keys.onDownPressed: host.downEvents++
                UI.DiscoverPicker {
                    id: control
                    x: 420; y: 720; width: 160; height: 40
                    label: "Genre"
                    automationName: "testPicker"
                    options: [ {key:"", text:"All"}, {header:"Genres"},
                               {key:"action", text:"Action", sub:"Cinemeta"},
                               {key:"drama", text:"Drama"}, {header:"Formats"},
                               {key:"omnibus", text:"Omnibus"} ]
                    currentKey: ""
                    onPicked: key => { testCase.chosen.push(key); currentKey = key; }
                    onCleared: { testCase.clears++; currentKey = ""; }
                }
            }
            UI.KeyboardAction { id: other; x: 4; y: 4; width: 10; height: 10 }
        }
    }
    Component {
        id: discoverFixture
        Window {
            width: 1280; height: 720; visible: true
            property alias browser: browser
            UI.DiscoverBrowser {
                id: browser
                anchors.fill: parent
                automationPrefix: "contract"
                adapter: ({
                    types: function() { return [{key:"movie", label:"Movies"}]; },
                    catalogs: function() { return [
                        {key:"popular", title:"Popular", section:"Source"},
                        {key:"recent", title:"Recent", section:"Source"} ]; },
                    defaultCatalog: function() { return "popular"; },
                    filters: function() { return [{group:"Genre", options:[
                        {key:"action", label:"Action"}, {key:"drama", label:"Drama"}]}]; },
                    fetchPage: function(state, cursor, generation, done) {
                        done(generation, {items:[{id:"one", title:"One", type:"movie", cover:""}], exhausted:true});
                    }
                })
            }
        }
    }
    function walk(node, predicate) {
        if (predicate(node)) return node;
        var children = node.children || [];
        for (var i = 0; i < children.length; i++) {
            var found = walk(children[i], predicate);
            if (found) return found;
        }
        return null;
    }
    function named(name) { return findChild(host, name) || findChild(host.contentItem, name); }
    function action() {
        return named("testPickerAction") || walk(picker, n => n.accessibleDescription === "Open filter options");
    }
    function list() {
        return named("testPickerList") || walk(picker, n => typeof n.positionViewAtIndex === "function");
    }
    function openWith(key) {
        action().forceActiveFocus();
        keyClick(key === undefined ? Qt.Key_Return : key);
        tryCompare(picker, "open", true);
        tryCompare(list(), "activeFocus", true);
    }
    function init() {
        chosen = []; clears = 0; underlayClicks = 0;
        host = fixture.createObject(null);
        verify(host !== null);
        picker = host.picker;
        host.flick.contentY = 390;
        waitForRendering(host.contentItem);
    }
    function cleanup() {
        if (host) { host.close(); host.destroy(); host = null; }
    }
    function test_currentEmptyKeyAndClearContract() {
        compare(picker.current.text, "All");
        compare(picker.hasValue, false);
        picker.currentKey = "action";
        compare(picker.current.sub, "Cinemeta");
        compare(picker.hasValue, true);
        picker.clearSelection();
        compare(clears, 1);
        compare(picker.currentKey, "");
        compare(chosen.length, 0);
        tryCompare(action(), "activeFocus", true);
        picker.clearable = false;
        picker.currentKey = "drama";
        compare(picker.current.text, "Drama");
        // Non-clearable remains a selected value, not a placeholder.
        compare(picker.hasValue, true);
    }
    function test_headersNavigationAndDefaultEndpoints() {
        openWith();
        var l = list();
        compare(l.currentIndex, 0);
        keyClick(Qt.Key_Down); compare(l.currentIndex, 2);
        keyClick(Qt.Key_Down); compare(l.currentIndex, 3);
        keyClick(Qt.Key_Down); compare(l.currentIndex, 5);
        keyClick(Qt.Key_Down); compare(l.currentIndex, 5);
        compare(host.downEvents, 0, "Popup boundary navigation must not reach host");
        keyClick(Qt.Key_Home); compare(l.currentIndex, 0);
        keyClick(Qt.Key_Up); compare(l.currentIndex, 0);
        keyClick(Qt.Key_End); compare(l.currentIndex, 5);
        keyClick(Qt.Key_Up); compare(l.currentIndex, 3);
        keyClick(Qt.Key_Tab); verify(l.activeFocus);
        keyClick(Qt.Key_Backtab); verify(l.activeFocus);
        keyClick(Qt.Key_Space);
        compare(chosen.length, 1); compare(chosen[0], "drama");
        compare(picker.open, false);
        tryCompare(action(), "activeFocus", true);
    }
    function test_spaceOpensBackClosesWithoutSelection() {
        openWith(Qt.Key_Space);
        keyClick(Qt.Key_Back);
        compare(picker.open, false);
        compare(chosen.length, 0);
        tryCompare(action(), "activeFocus", true);
    }
    function test_backClosesWithoutSelection() {
        openWith();
        keyClick(Qt.Key_Back);
        compare(picker.open, false);
        compare(chosen.length, 0);
        tryCompare(action(), "activeFocus", true);
    }
    function test_escapeAndSelectedIndexReturn() {
        picker.currentKey = "drama";
        openWith();
        compare(list().currentIndex, 3);
        keyClick(Qt.Key_Home);
        keyClick(Qt.Key_Escape);
        compare(picker.open, false);
        compare(picker.currentKey, "drama");
        compare(chosen.length, 0);
        tryCompare(action(), "activeFocus", true);
        openWith();
        keyClick(Qt.Key_Home);
        keyClick(Qt.Key_Enter);
        compare(chosen.length, 1); compare(chosen[0], "");
        compare(picker.hasValue, false);
    }
    function test_popupBoundsAndPointerDeliveryInClippedScrolledHost() {
        openWith();
        var l = list();
        waitForRendering(host.contentItem);
        var p = l.mapToItem(host.contentItem, 0, 0);
        verify(p.x >= 0 && p.y >= 0, "Popup list remains in viewport");
        verify(p.x + l.width <= host.width && p.y + l.height <= host.height,
               "Popup list is bounded by window, not clipped/scrolled parent");
        var option = named("testPickerOption_2");
        verify(option !== null);
        mouseClick(option, option.width / 2, option.height / 2);
        compare(chosen.length, 1); compare(chosen[0], "action");
        compare(picker.open, false); compare(underlayClicks, 0);
        tryCompare(action(), "activeFocus", true);
    }
    function test_outsideClosesAndReturnsFocus() {
        openWith();
        mouseClick(host.contentItem, 10, host.height - 10);
        tryCompare(picker, "open", false);
        compare(underlayClicks, 0, "Dismissal must not activate the page underneath");
        compare(chosen.length, 0);
        tryCompare(action(), "activeFocus", true);
    }
    function test_externalCloseDoesNotStealNextHostFocus() {
        openWith();
        picker.open = false;
        host.other.forceActiveFocus();
        wait(30);
        verify(host.other.activeFocus, "Discover catalog host owns focus after external close");
        compare(chosen.length, 0);
    }
    function test_existingDiscoverHostSelectionAndMutualExclusion() {
        host.close(); host.destroy();
        host = discoverFixture.createObject(null);
        verify(host !== null);
        picker = findChild(host.browser, "discoverFilterPicker");
        verify(picker !== null);
        picker.openPopup();
        var l = named("contractDiscoverFilterPickerList");
        tryCompare(l, "activeFocus", true);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(host.browser.filterGroup, "Genre");
        compare(host.browser.filterKey, "action");
        compare(picker.currentKey, "Genre|action");
        picker.clearSelection();
        compare(host.browser.activeFilterCount, 0);
        picker.openPopup();
        tryCompare(l, "activeFocus", true);
        host.browser.openCatalogMenu(null);
        compare(picker.open, false);
        compare(host.browser.catalogMenuOpen, true);
        wait(30);
        verify(!l.activeFocus, "External catalog opening must retain its own focus");
        picker.openPopup();
        compare(host.browser.catalogMenuOpen, false);
        tryCompare(l, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        compare(picker.open, false);
    }

    function test_defaultDownRemainsHostOwnedAndArtistOptIns() {
        action().forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(picker.open, false);
        compare(host.downEvents, 1);
        picker.openOnDown = true;
        picker.wrapNavigation = true;
        keyClick(Qt.Key_Down);
        tryCompare(list(), "activeFocus", true);
        keyClick(Qt.Key_Up);
        compare(list().currentIndex, 5);
        keyClick(Qt.Key_Down);
        compare(list().currentIndex, 0);
        compare(host.downEvents, 1);
    }
    function test_pointerClearAndNonClearableExplicitAll() {
        picker.currentKey = "drama";
        var pill = named("testPickerPill");
        mouseClick(pill, pill.width - 14, pill.height / 2);
        compare(clears, 1);
        compare(picker.currentKey, "");
        picker.options = [{key:"All", text:"All releases", sub:"27"},
                          {key:"Studio", text:"Studio albums", sub:"12"}];
        picker.currentKey = "All";
        picker.clearable = false;
        compare(picker.hasValue, true);
        compare(picker.current.text, "All releases");
        mouseClick(pill, pill.width - 14, pill.height / 2);
        tryCompare(picker, "open", true);
        compare(clears, 1);
        var option = named("testPickerOption_1");
        mouseClick(option, option.width / 2, option.height / 2);
        compare(picker.currentKey, "Studio");
        compare(chosen.length, 1);
    }

    function test_scrolledHostMovementRepositionsOrSafelyCloses() {
        picker.y = 500;
        openWith();
        var l = list();
        var before = l.mapToItem(host.contentItem, 0, 0);
        host.flick.contentY += 30;
        wait(30);
        if (picker.open) {
            var after = l.mapToItem(host.contentItem, 0, 0);
            compare(Math.round(after.y), Math.round(before.y - 30),
                    "Open popup must follow its moving trigger or close safely");
        }
    }
    function test_windowResizeKeepsPopupBoundedOrSafelyCloses() {
        openWith();
        host.width = 420;
        host.height = 300;
        wait(30);
        if (picker.open) {
            var l = list();
            var point = l.mapToItem(host.contentItem, 0, 0);
            verify(point.x >= 0 && point.y >= 0 && point.x + l.width <= host.width
                   && point.y + l.height <= host.height,
                   "Open popup remains within resized window or closes safely");
        }
    }

    function test_bottomAnchorModelGrowthStaysAboveAndScrollable() {
        picker.options = [{key:"first", text:"First"}];
        picker.currentKey = "first";
        openWith();
        var rows = [{header:"Options"}];
        for (var i = 0; i < 30; ++i) rows.push({key:"row" + i, text:"Row " + i});
        picker.options = rows;
        var pop = named("testPickerPopup");
        var l = list();
        tryVerify(function() { return l.contentHeight > l.height; });
        waitForRendering(host.contentItem);
        var triggerTop = picker.mapToItem(host.contentItem, 0, 0).y;
        var bounds = l.mapToItem(host.contentItem, 0, 0);
        verify(bounds.y >= 8, "Long popup stays below window margin");
        verify(bounds.y + l.height <= triggerTop - 6,
               "Bottom-anchored popup must remain above its trigger after model growth");
        keyClick(Qt.Key_End);
        compare(l.currentIndex, rows.length - 1);
        tryVerify(function() { return l.contentY > 0; });
        waitForRendering(host.contentItem);
        var option = named("testPickerOption_" + (rows.length - 1));
        verify(option !== null);
        var point = option.mapToItem(l, 0, 0);
        verify(point.y >= -1 && point.y + option.height <= l.height + 1,
               "Last option is fully reachable");
        mouseClick(option, option.width / 2, option.height / 2);
        compare(chosen.length, 1); compare(chosen[0], "row29");
        tryCompare(action(), "activeFocus", true);
    }

    function test_longPopupInitialPlacementAndWheel_data() {
        return [{tag:"top", triggerY:15}, {tag:"middle", triggerY:210},
                {tag:"bottom", triggerY:390}];
    }
    function test_longPopupInitialPlacementAndWheel(data) {
        picker.y = host.flick.contentY + data.triggerY;
        var rows = [];
        for (var i = 0; i < 30; ++i) rows.push({key:"row" + i, text:"Row " + i});
        picker.options = rows;
        picker.currentKey = "row0";
        openWith();
        var l = list();
        waitForRendering(host.contentItem);
        var bounds = l.mapToItem(host.contentItem, 0, 0);
        var trigger = picker.mapToItem(host.contentItem, 0, 0);
        verify(bounds.x >= 8 && bounds.y >= 8);
        verify(bounds.x + l.width <= host.width - 8 && bounds.y + l.height <= host.height - 8);
        verify(bounds.y + l.height <= trigger.y - 6 || bounds.y >= trigger.y + picker.height + 6,
               "Popup must not cover its trigger when the list is taller than either available side");
        var before = l.contentY;
        mouseWheel(l, l.width / 2, l.height / 2, 0, -240);
        tryVerify(function() { return l.contentY > before; });
        keyClick(Qt.Key_End);
        compare(l.currentIndex, rows.length - 1);
        keyClick(Qt.Key_Return);
        compare(chosen.length, 1); compare(chosen[0], "row29");
        tryCompare(action(), "activeFocus", true);
    }

}
