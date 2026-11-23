.pragma library

function scrollBy(page, dy) {
    page.contentY = Math.max(0, Math.min(page.contentHeight - page.height, page.contentY + dy));
}

function scrollPastTabSlot(test, host) {
    var page = host.artistScroll;
    var bar = test.findChild(page, "vinylArtistTabBar");
    test.verify(test.waitForRendering(page));
    // Scroll past the tab bar's flow position, not merely past the hero.
    scrollBy(page, bar.mapToItem(page, 0, 0).y);
    test.verify(test.waitForRendering(page));
}

function clickArtistTab(test, host, key) {
    var page = host.artistScroll;
    var tab = test.findChild(page, "vinylArtistTab_" + key);
    test.verify(tab !== null && tab.visible, "Missing artist tab: " + key);
    test.verify(test.waitForRendering(page));
    var top = tab.mapToItem(page, 0, 0).y;
    if (top < 0 || top + tab.height > page.height) {
        scrollBy(page, top + (tab.height - page.height) / 2);
        test.verify(test.waitForRendering(page));
    }
    top = tab.mapToItem(page, 0, 0).y;
    test.verify(top >= 0 && top + tab.height <= page.height,
                "Artist tab is outside the scroll viewport: " + key);
    test.mouseClick(tab);
    test.tryCompare(host.artistData, "activeTab", key);
}
