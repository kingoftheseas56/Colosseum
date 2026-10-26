import QtQuick
import QtTest

TestCase {
    name: "VinylArtistSharedPicker"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem, name) || findChild(host, name); }
    function init() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(component.status === Component.Ready, component.errorString());
        host = component.createObject(null, {width: 1280, height: 720});
        verify(host !== null);
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
        host.artistData.reducedMotion = true;
        host.artistScroll.contentY = host.artistScroll.heroItem.height - 110;
        waitForRendering(host.contentItem);
    }
    function cleanup() { if (host) { host.close(); host.destroy(); host = null; } }
    function test_scrolledPopupBoundsPointerAndOutsideClose() {
        var picker = item("vinylArtistReleasePicker");
        var trigger = item("vinylArtistReleasePickerAction");
        var pop = item("vinylArtistReleasePickerPopup");
        var scroll = host.artistScroll.contentY;
        var playing = host.pageData.playingEditionId;
        var position = host.pageData.position;
        for (var size of [[1280, 720], [1600, 900]]) {
            host.width = size[0]; host.height = size[1];
            waitForRendering(host.contentItem);
            mouseClick(trigger, trigger.width / 2, trigger.height / 2);
            tryCompare(picker, "open", true);
            verify(pop.contentItem.parent !== picker, "Popup content must escape the clipped page");
            var bounds = pop.contentItem.mapToItem(host.contentItem, 0, 0);
            verify(bounds.x >= 0 && bounds.y >= 0);
            verify(bounds.x + pop.contentItem.width <= host.width);
            verify(bounds.y + pop.contentItem.height <= host.height);
            mouseClick(host.contentItem, host.width - 10, 200);
            tryCompare(picker, "open", false);
            tryCompare(trigger, "activeFocus", true);
            compare(host.artistData.categoryFilter, "All");
            compare(host.artistScroll.contentY, scroll);
        }
        trigger.activate();
        var option = item("vinylArtistReleasePickerOption_1");
        waitForRendering(host.contentItem);
        mouseClick(option, option.width / 2, option.height / 2);
        compare(host.artistData.categoryFilter, "Studio");
        compare(picker.currentKey, "Studio");
        compare(picker.current.text, host.artistData.currentCategoryLabel);
        compare(host.artistScroll.contentY, scroll);
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.position, position);
    }
    function test_allReleaseCategoriesUseCanonicalOwner() {
        var picker = item("vinylArtistReleasePicker");
        compare(picker.clearable, false);
        var categories = host.artistData.releaseCategories;
        compare(picker.options.length, categories.length);
        for (var i = 0; i < categories.length; ++i) {
            compare(picker.options[i].key, categories[i].id);
            compare(picker.options[i].text, categories[i].label);
            compare(picker.options[i].sub, String(categories[i].count));
            picker.choose(i);
            compare(host.artistData.categoryFilter, categories[i].id);
            compare(picker.currentKey, categories[i].id);
            compare(host.artistData.discographyCards.length, categories[i].count);
        }
        picker.choose(0);
        compare(picker.currentKey, "All");
        compare(picker.current.text, "All releases");
        compare(host.artistData.discographyCards.length, 27);
    }
    function test_scrollAndResizeDismissAnchoredPopup() {
        var picker = item("vinylArtistReleasePicker");
        var trigger = item("vinylArtistReleasePickerAction");
        trigger.activate();
        tryCompare(picker, "open", true);
        host.artistScroll.contentY += 20;
        compare(picker.open, false);
        trigger.activate();
        tryCompare(picker, "open", true);
        host.width = 1600;
        tryCompare(picker, "open", false);
        tryCompare(trigger, "activeFocus", true);
    }
    function test_sameCountCategoryRefreshRebindsAdapter() {
        var picker = item("vinylArtistReleasePicker");
        host.artistData.catalogue = {albums: [
            {id: "refresh", title: "Release", artists: ["Adapter Artist"], tracks: []}
        ]};
        host.artistData.open("Adapter Artist");
        compare(picker.options.length, 2);
        compare(picker.options[1].key, "album");
        host.artistData.catalogue = {albums: [
            {id: "refresh", title: "Release", artists: ["Other Artist"],
                tracks: [{title: "Track", artists: ["Adapter Artist"], seconds: 120}]}
        ]};
        compare(picker.options.length, 2);
        compare(picker.options[1].key, "track");
        compare(picker.options[1].text, "Appears on");
        picker.choose(1);
        compare(host.artistData.categoryFilter, "track");
        compare(picker.currentKey, "track");
    }
    function test_hiddenTabClosesSharedPopup() {
        var picker = item("vinylArtistReleasePicker");
        item("vinylArtistReleasePickerAction").activate();
        tryCompare(picker, "open", true);
        host.artistScroll.switchTab("videos", true);
        compare(picker.open, false);
        tryCompare(item("vinylArtistTab_videos"), "activeFocus", true);
        compare(host.artistData.activeTab, "videos");
    }
}
