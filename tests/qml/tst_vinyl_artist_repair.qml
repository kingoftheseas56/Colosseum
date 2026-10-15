import QtQuick
import QtTest

// Native regression coverage for the approved artist destination. The original
// returned suite is retained unchanged, including legacy UI assertions.
TestCase {
    name: "VinylArtistRepair"
    when: windowShown
    property var host
    // Popup and Timer objects belong to the Window QObject tree, not just its
    // visual contentItem descendants.
    function item(name) { return findChild(host.contentItem, name) || findChild(host, name); }
    function init() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(component.status === Component.Ready, component.errorString());
        host = component.createObject(null);
        verify(host !== null);
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
        host.artistData.open("Eminem");
        host.artistScroll.heroItem.rotationIntervalMs = 600000;
    }
    function cleanup() { if (host) { host.close(); host.destroy(); } }
    function test_catalogueAndActiveOnlyHud() {
        compare(host.artistData.allDiscographyCards.length, 27);
        compare(host.artistData.singlesRows.length, 43);
        compare(item("vinylWorldPillsBar").visible, false);
        compare(item("vinylArtistTitleBar").height, 76);
        compare(item("vinylArtistTabBar").height, 58);
        var hud = item("vinylArtistHudPlayer");
        compare(hud.visible, false);
        compare(item("vinylPlayerBar").visible, false, "Artist destination has no always-on legacy player");
        item("vinylArtistTab_singles").activate();
        var first = item("vinylArtistSinglesRow_0");
        first.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.artistData.queuedSongIndex, 0);
        compare(hud.visible, true);
        compare(hud.height, 72);
        compare(item("vinylPlayerBar").visible, false, "Active artist HUD is the only player");
        compare(item("vinylArtistHudTitle").text, "Houdini");
        host.artistData.open("Ilaiyaraaja");
        compare(host.artistData.queuedSongIndex, -1);
        compare(hud.visible, false);
    }
    function test_releaseMenuEndpointsBackAndFocusEscape() {
        var trigger = item("vinylArtistReleasePickerAction");
        var focus = item("vinylArtistReleasePickerList");
        trigger.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryCompare(focus, "activeFocus", true);
        var last = host.artistData.releaseCategories.length - 1;
        keyClick(Qt.Key_End);
        compare(item("vinylArtistReleasePickerList").currentIndex, last);
        keyClick(Qt.Key_Down);
        compare(item("vinylArtistReleasePickerList").currentIndex, 0);
        keyClick(Qt.Key_Up);
        compare(item("vinylArtistReleasePickerList").currentIndex, last);
        keyClick(Qt.Key_Home);
        compare(item("vinylArtistReleasePickerList").currentIndex, 0);
        keyClick(Qt.Key_Tab);
        verify(focus.activeFocus);
        keyClick(Qt.Key_Backtab);
        verify(focus.activeFocus);
        keyClick(Qt.Key_Back);
        compare(item("vinylArtistReleasePicker").open, false);
        compare(host.artistData.categoryFilter, "All");
        tryCompare(trigger, "activeFocus", true);
        keyClick(Qt.Key_Down);
        tryCompare(focus, "activeFocus", true);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Space);
        compare(host.artistData.categoryFilter, "Studio");
        compare(host.artistData.discographyCards.length, 12);
        tryCompare(trigger, "activeFocus", true);
        keyClick(Qt.Key_Return);
        tryCompare(focus, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(trigger, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return !trigger.activeFocus && !focus.activeFocus; });
    }
    function test_dropdownPointerHitsOptionAboveGrid() {
        host.artistScroll.contentY = host.artistScroll.heroItem.height - 110;
        var trigger = item("vinylArtistReleasePickerAction");
        waitForRendering(host.contentItem);
        mouseClick(trigger, trigger.width / 2, trigger.height / 2);
        tryCompare(item("vinylArtistReleasePicker"), "open", true);
        var option = item("vinylArtistReleasePickerOption_1");
        verify(option !== null);
        waitForRendering(host.contentItem);
        mouseClick(option, option.width / 2, option.height / 2);
        compare(host.artistData.categoryFilter, "Studio");
        compare(host.artistData.discographyCards.length, 12);
        compare(item("vinylArtistReleasePicker").open, false);
        compare(host.page, "artist");
        tryCompare(trigger, "activeFocus", true);
    }
    function test_focusedRowsClearStickyHeaderAndActiveHud() {
        item("vinylArtistTab_singles").activate();
        wait(0);
        item("vinylArtistSinglesRow_0").activate();
        var hud = item("vinylArtistHudPlayer");
        var row = item("vinylArtistSinglesRow_42");
        row.forceActiveFocus();
        waitForRendering(host.contentItem);
        var point = row.mapToItem(host.contentItem, 0, 0);
        var hudPoint = hud.mapToItem(host.contentItem, 0, 0);
        verify(point.y >= 134, "Last row clears contextual header and tabs");
        verify(point.y + row.height <= hudPoint.y, "Last row clears active HUD");
        row = item("vinylArtistSinglesRow_0");
        row.forceActiveFocus();
        waitForRendering(host.contentItem);
        point = row.mapToItem(host.contentItem, 0, 0);
        verify(point.y >= 134, "First row clears contextual header and tabs after returning upward");
    }
    function test_tabsHomeEndAndArrows() {
        var keys = ["albums", "singles", "videos", "live"];
        item("vinylArtistTab_albums").forceActiveFocus();
        keyClick(Qt.Key_End);
        compare(host.artistData.activeTab, "live");
        verify(item("vinylArtistTab_live").activeFocus);
        keyClick(Qt.Key_Left);
        compare(host.artistData.activeTab, "videos");
        keyClick(Qt.Key_Home);
        compare(host.artistData.activeTab, "albums");
        keyClick(Qt.Key_Right);
        compare(host.artistData.activeTab, "singles");
        for (var i = 0; i < keys.length; ++i)
            compare(item("vinylArtistPanel_" + keys[i]).visible, keys[i] === "singles");
    }
    function test_gridArrowNavigationAndScroll() {
        var first = item("vinylArtistDiscographyCard_0");
        first.forceActiveFocus();
        keyClick(Qt.Key_Right);
        tryCompare(item("vinylArtistDiscographyCard_1"), "activeFocus", true);
        keyClick(Qt.Key_Down);
        var next = 1 + host.artistScroll.albumGridColumns;
        tryCompare(item("vinylArtistDiscographyCard_" + next), "activeFocus", true);
        keyClick(Qt.Key_Up);
        tryCompare(item("vinylArtistDiscographyCard_1"), "activeFocus", true);
        keyClick(Qt.Key_Left);
        tryCompare(first, "activeFocus", true);
        verify(host.artistScroll.contentY > 0);
    }
    function test_nativeAlbumRoundTripPreservesPlaybackAndScroll() {
        host.artistData.open("Ilaiyaraaja");
        var expected = host.artistData.discographyCards[0].album;
        var playing = host.pageData.playingEditionId;
        var position = host.pageData.position;
        host.artistScroll.contentY = 100;
        item("vinylArtistDiscographyCard_0").activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, expected.title);
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.position, position);
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistScroll.contentY, 100);
    }
    function test_headerBackRestoresArtistTabAndFilter() {
        host.artistData.selectReleaseCategory("Studio");
        item("vinylArtistTab_videos").activate();
        host.artistScroll.contentY = 120;
        host.openArtist("Ilaiyaraaja");
        var back = item("vinylArtistHeaderBack");
        // Focus the shared component's actual keyboard input, then exercise Return.
        var input = null;
        for (var child of back.children) {
            if (child.accessibleName === "Back to Vinyl") { input = child; break; }
        }
        verify(input !== null);
        input.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.page, "artist");
        compare(host.artistData.name, "Eminem");
        compare(host.artistData.categoryFilter, "Studio");
        compare(host.artistData.activeTab, "videos");
        tryCompare(host.artistScroll, "contentY", 120);
    }
    function test_pageScrollHomeEndAndDown() {
        var page = host.artistScroll;
        page.contentY = 0;
        page.forceActiveFocus();
        keyClick(Qt.Key_Down);
        verify(page.contentY > 0);
        keyClick(Qt.Key_End);
        compare(page.contentY, Math.max(0, page.contentHeight - page.height));
        keyClick(Qt.Key_Home);
        compare(page.contentY, 0);
        compare(item("vinylArtistTitleBar").solid, false);
    }
    function test_singlesArrowsAndEndDoesNotQueue() {
        item("vinylArtistTab_singles").activate();
        item("vinylArtistSinglesRow_0").forceActiveFocus();
        keyClick(Qt.Key_Down);
        tryCompare(item("vinylArtistSinglesRow_1"), "activeFocus", true);
        keyClick(Qt.Key_Up);
        tryCompare(item("vinylArtistSinglesRow_0"), "activeFocus", true);
        keyClick(Qt.Key_End);
        compare(host.artistScroll.contentY, Math.max(0, host.artistScroll.contentHeight - host.artistScroll.height));
        compare(host.artistData.queuedSongIndex, -1);
        compare(item("vinylArtistHudPlayer").visible, false);
    }
    function test_sameWindowResizeAndArtistReopen() {
        var sizes = [[1280,720], [1920,1080], [1600,900]];
        for (var i = 0; i < sizes.length; ++i) {
            host.width = sizes[i][0];
            host.height = sizes[i][1];
            host.artistData.open("Ilaiyaraaja");
            host.artistData.open("Eminem");
            tryCompare(host.artistScroll, "width", sizes[i][0]);
            tryCompare(host.artistScroll, "height", sizes[i][1]);
            compare(host.artistData.allDiscographyCards.length, 27);
            compare(host.artistData.singlesRows.length, 43);
            compare(host.artistData.activeTab, "albums");
            compare(host.artistData.queuedSongIndex, -1);
            compare(item("vinylArtistHudPlayer").visible, false);
            host.artistScroll.contentY = host.artistScroll.heroItem.height;
            waitForRendering(host.contentItem);
            var header = item("vinylArtistTitleBar");
            var tabs = item("vinylArtistTabBar");
            compare(header.height, 76);
            compare(tabs.height, 58);
            compare(header.mapToItem(host.contentItem, 0, 0).y, 0);
            compare(tabs.mapToItem(host.contentItem, 0, 0).y, 76);
            var trigger = item("vinylArtistReleasePickerAction");
            trigger.forceActiveFocus();
            keyClick(Qt.Key_Return);
            tryCompare(item("vinylArtistReleasePickerList"), "activeFocus", true);
            keyClick(Qt.Key_Escape);
            tryCompare(trigger, "activeFocus", true);
        }
    }
    function test_pageEscapeAndBackReturnToOrigin() {
        var keys = [Qt.Key_Escape, Qt.Key_Back];
        for (var i = 0; i < keys.length; ++i) {
            host.page = "world";
            host.openArtist("Eminem");
            host.artistScroll.forceActiveFocus();
            keyClick(keys[i]);
            compare(host.page, "world");
        }
    }
    function test_searchEscapeRestoresFocus() {
        var button = item("vinylArtistSearchButton");
        button.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(item("vinylArtistSearchDialog"), "visible", true);
        keyClick(Qt.Key_Escape);
        tryCompare(item("vinylArtistSearchDialog"), "visible", false);
        tryCompare(button, "activeFocus", true);
    }
    function assertPhotoSync() {
        var hero = item("vinylArtistHero");
        var skin = item("vinylArtistWallpaper");
        compare(skin.artistActiveLayer, hero.activeVisualLayer);
        compare(skin.artistLayerASource, hero.layerASource);
        compare(skin.artistLayerBSource, hero.layerBSource);
        verify(skin.artistSkinActive);
    }
    function test_photoRotationDistinctAndSynchronized() {
        var hero = item("vinylArtistHero");
        var timer = item("vinylArtistHeroTimer");
        compare(hero.eligiblePhotoCount, 4);
        compare(hero.rotationMode, "multi-rotate");
        verify(timer.running);
        var seen = {};
        for (var i = 0; i < 4; ++i) {
            assertPhotoSync();
            var source = String(hero.currentSynchronizedSource);
            verify(source.length > 0);
            verify(!seen[source], "Each of four rotations uses a distinct photographic source");
            seen[source] = true;
            hero.advanceSlideshow();
        }
        compare(hero.slideshowIndex, 0);
        host.artistData.overrideHeroPhotos = hero.eligiblePhotos.slice(0, 2);
        compare(hero.rotationMode, "dual-crossfade");
        var initial = String(hero.currentSynchronizedSource);
        hero.advanceSlideshow();
        verify(String(hero.currentSynchronizedSource) !== initial);
        assertPhotoSync();
        hero.advanceSlideshow();
        compare(String(hero.currentSynchronizedSource), initial);
        assertPhotoSync();
        host.artistData.reducedMotion = true;
        compare(timer.running, false);
        host.artistData.reducedMotion = false;
        compare(timer.running, true);
        host.artistData.overrideHeroPhotos = null;
        host.artistData.open("Ilaiyaraaja");
        compare(hero.eligiblePhotoCount, 1);
        compare(hero.rotationMode, "static");
        compare(timer.running, false);
        initial = String(hero.currentSynchronizedSource);
        hero.advanceSlideshow();
        compare(String(hero.currentSynchronizedSource), initial);
        assertPhotoSync();
        host.artistData.open("Uncatalogued artist");
        compare(hero.eligiblePhotoCount, 0);
        compare(hero.rotationMode, "fallback");
        compare(timer.running, false);
        compare(String(hero.currentSynchronizedSource), "");
        assertPhotoSync();
    }
}
