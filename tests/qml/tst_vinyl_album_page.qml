import QtQuick
import QtTest

TestCase {
    name: "VinylAlbumPage"
    when: windowShown
    property var host
    function item(name) {
        function walk(o) {
            if (o.objectName === name)
                return o;
            var children = o.children || [];
            for (var i = 0; i < children.length; i++) {
                var found = walk(children[i]);
                if (found)
                    return found;
            }
            return null;
        }
        return findChild(host, name) || walk(host.contentItem);
    }
    function init() {
        var c = Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml");
        verify(c.status === Component.Ready, c.errorString());
        host = c.createObject(null);
        verify(host !== null);
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
    }
    function cleanup() {
        if (host) {
            host.close();
            host.destroy();
        }
    }
    function test_sectionIconsAreDistinctAndLoaded() {
        var sidebar = item("vinylRowIndexSidebar");
        var sources = {};
        for (var i = 0; i < sidebar.visibleRows.length; ++i) {
            var row = sidebar.visibleRows[i];
            verify(!!row.iconSource, row.key + " needs an explicit section icon");
            verify(!sources[row.iconSource], "Section icons must be distinct");
            sources[row.iconSource] = true;
            var icon = item("vinylRowIndexRow_" + row.key + "Icon");
            verify(icon !== null);
            tryCompare(icon, "status", Image.Ready);
        }
    }
    function test_hudOpensSongWithoutHijackingControls() {
        var bar = item("vinylPlayerBar");
        mouseClick(bar, 35, 35);
        compare(host.page, "song", "Artwork must open the playing song");
        host.page = "album";
        mouseClick(item("vinylBarNowPlaying"));
        compare(host.page, "song");
        host.page = "album";
        var paused = host.pageData.paused;
        mouseClick(item("vinylBarPlay"));
        compare(host.page, "album", "Transport must not navigate");
        compare(host.pageData.paused, !paused);
        var index = host.pageData.playingIndex;
        mouseClick(item("vinylBarNext"));
        compare(host.page, "album");
        compare(host.pageData.playingIndex, index + 1);
    }
    function test_coverKeepsScaleInShortViewport() {
        var page=item("vinylAlbumPage");
        page.height=390;
        wait(0);
        verify(page.coverSize>=320, "Album artwork must retain its large desktop scale.");
        verify(page.contentHeight>=page.coverSize+page.albumMetadataHeight+48,
            "Scrolling must keep artwork and metadata reachable in a short viewport.");
    }
    function test_barStates() {
        var bar = item("vinylPlayerBar");
        verify(item("vinylBarQueue").visible);
        verify(!item("vinylBarLyrics").visible);
        verify(!item("vinylBarNotes").visible);
        host.page = "song";
        verify(item("vinylBarQueue").visible);
        verify(item("vinylBarLyrics").visible);
        verify(item("vinylBarNotes").visible);
        verify(item("vinylBarVideo").visible);
        host.pageData.setFixture("mounamela");
        verify(!item("vinylBarVideo").enabled);
        host.pageData.setSourceState("finding");
        compare(item("vinylBarSourceIndicator").description, "Finding a source");
        verify(bar.busy);
        host.pageData.setSourceState("playing");
        host.pageData.position = host.pageData.duration / 2;
        compare(bar.seekFraction, 0.5);
        var old = host.pageData.playingIndex;
        mouseClick(item("vinylBarNext"));
        compare(host.pageData.playingIndex, old + 1);
    }

    function test_trackRows() {
        var page = item("vinylAlbumPage");
        compare(page.trackCount, 9);
        var count = 0;
        for (var i = 0; i < 9; i++)
            if (item("vinylTrackRow_" + i).artistLine)
                count++;
        compare(count, 1);
        var row = item("vinylTrackRow_2");
        mouseClick(row);
        compare(host.pageData.playingIndex, 2);
        verify(row.playing);
        var first = item("vinylTrackRow_0");
        var at = first.mapToItem(host.contentItem, 0, 0);
        verify(at.y >= 0 && at.y + first.height < host.height);
        verify(!page.discSwitchVisible);
        host.pageData.selectEdition("40");
        verify(page.discSwitchVisible);
        verify(waitForRendering(item("vinylDisc_1")));
        mouseClick(item("vinylDisc_1"));
        compare(page.trackCount, 10);
    }
    function test_coverFitsViewport_data() {
        return [
            { tag: "short-desktop", width: 1280, height: 672 },
            { tag: "desktop", width: 1280, height: 720 },
            { tag: "large-desktop", width: 1600, height: 900 }
        ];
    }
    function test_coverFitsViewport(data) {
        host.width = data.width;
        host.height = data.height;
        var page = item("vinylAlbumPage");
        var cover = item("vinylAlbumCover");
        page.contentY = 0;
        verify(waitForRendering(cover));
        compare(cover.width, cover.height, "Album artwork must stay square");
        var at = cover.mapToItem(page, 0, 0);
        verify(at.y >= 0 && at.y + cover.height <= page.height,
               "The entire square cover must fit before scrolling");
        var action = item("vinylAlbumPlay");
        var actionAt = action.mapToItem(page, 0, 0);
        verify(actionAt.y >= 0 && actionAt.y + action.height <= page.height,
               "Artwork sizing must leave room for the album details and actions");
        page.contentY = Math.min(page.contentHeight - page.height,
                                actionAt.y);
        verify(waitForRendering(cover));
        actionAt = action.mapToItem(page, 0, 0);
        verify(actionAt.y >= 0 && actionAt.y + action.height <= page.height,
               "Album actions must remain reachable by scrolling");
    }
    function test_sectionsAndEdition() {
        var page = item("vinylAlbumPage");
        var names = ["about", "credits", "videos", "editions", "more", "information"];
        var y = -1;
        for (var i = 0; i < names.length; i++) {
            var section = item("vinylSection_" + names[i]);
            verify(section !== null);
            verify(section.y > y);
            y = section.y;
        }
        var grid = item("vinylCreditsGrid");
        var before = grid.groupCount;
        item("vinylAllCredits").activate();
        verify(grid.groupCount > before);
        compare(item("vinylAllCredits").accessibleName, "Fewer credits");
        var sections = item("vinylSection_editions");
        for (i = 1; i < sections.editionYears.length; i++)
            verify(sections.editionYears[i] >= sections.editionYears[i - 1]);
        page.contentY = 300;
        var index = host.pageData.playingIndex;
        item("vinylEdition_40").activate();
        compare(page.shownEditionId, "40");
        compare(page.contentY, 0);
        compare(host.pageData.playingIndex, index);
        compare(host.pageData.playingEditionId, "original");
        host.pageData.setFixture("without-me");
        verify(item("vinylInfo_barcode") === null || !item("vinylInfo_barcode").visible);
    }
    function test_barcode() {
        var c = Qt.createComponent("../../qml/vinyl/VinylBarcode.qml");
        verify(c.status === Component.Ready, c.errorString());
        var b = createTemporaryObject(c, host.contentItem, {
            digits: "074643811217"
        });
        verify(b.valid);
        verify(b.bars.length > 0);
        compare(b.bars.length, 95);
        // The width of the encoded symbol must preserve zero-bit spaces.
        verify(b.symbolWidth >= 159);
        b.digits = "074643811218";
        verify(!b.valid);
        compare(b.bars.length, 0);
        compare(b.displayDigits, "074643811218");
    }
    function test_sidebar() {
        var page = item("vinylAlbumPage");
        var sidebar = item("vinylRowIndexSidebar");
        compare(sidebar.visibleRows.length, 6);
        var section = item("vinylSection_more");
        section.visible = false;
        compare(sidebar.visibleRows.length, 5);
        section.visible = true;
        sidebar.rowRequested(item("vinylSection_editions"));
        compare(page.currentSectionKey, "editions");
        sidebar.collapsed = false;
        tryCompare(sidebar, "width", sidebar.openWidth);
        verify(item("vinylSection_editions").mapToItem(page, 0, 0).x >= sidebar.x + sidebar.width, "Open sidebar must not cover section text");
        var railY = sidebar.mapToItem(page, 0, 0).y;
        verify(railY >= 0 && railY < page.height, "Sidebar must be inside the visible page after a jump");
        sidebar.rowRequested(item("vinylSection_editions"));
        var y = item("vinylSection_editions").mapToItem(page, 0, 0).y;
        verify(Math.abs(y - sidebar.pinLine) < 2);
    }

    function test_allEditions() {
        var sections = item("vinylSection_editions").parent;
        sections.editionThreshold = 2;
        verify(item("vinylAllEditions").visible);
        item("vinylAllEditions").activate();
        verify(item("vinylEdition_40") !== null);
        compare(item("vinylAllEditions").accessibleName, "Fewer editions");
    }
    function test_captureStates() {
        if (typeof VinylCaptureDirectory === "undefined" || !VinylCaptureDirectory)
            skip("Set VINYL_CAPTURE_DIR for visual captures");
        host.developerStrip = false;
        var page = item("vinylAlbumPage");
        for (var size = 0; size < 2; size++) {
            host.width = size ? 1600 : 1280;
            host.height = size ? 900 : 720;
            page.contentY = 0;
            host.requestUpdate();
            waitForRendering(host.contentItem);
            var capture = grabImage(host.contentItem);
            compare(capture.width, host.width);
            capture.save(VinylCaptureDirectory + '/album-' + host.width + '-first.png');
            var sections = ['about', 'credits', 'videos', 'editions', 'more', 'information'];
            for (var i = 0; i < sections.length; i++) {
                item('vinylRowIndexSidebar').rowRequested(item('vinylSection_' + sections[i]));
                host.requestUpdate();
                waitForRendering(host.contentItem);
                var capture = grabImage(host.contentItem);
                compare(capture.width, host.width);
                capture.save(VinylCaptureDirectory + '/album-' + host.width + '-' + sections[i] + '.png');
            }
            item('vinylRowIndexSidebar').collapsed = false;
            tryCompare(item('vinylRowIndexSidebar'), 'width', 240);
            item('vinylRowIndexSidebar').rowRequested(item('vinylSection_information'));
            host.requestUpdate();
            waitForRendering(host.contentItem);
            var capture = grabImage(host.contentItem);
            compare(capture.width, host.width);
            capture.save(VinylCaptureDirectory + '/album-' + host.width + '-sidebar-open.png');
            item('vinylRowIndexSidebar').collapsed = true;
            tryCompare(item('vinylRowIndexSidebar'), 'width', 52);
        }
        page.contentY = 0;
        for (var state of ['playing', 'finding', 'retrying']) {
            host.pageData.setSourceState(state);
            host.requestUpdate();
            waitForRendering(host.contentItem);
            var capture = grabImage(host.contentItem);
            compare(capture.width, host.width);
            capture.save(VinylCaptureDirectory + '/bar-' + state + '.png');
        }
    }

    function test_albumVideoCanReturnToPoster() {
        item("vinylVideo_0").activate();
        compare(host.page, "song");
        verify(host.pageData.videoShown);
        verify(item("vinylBarVideo").enabled);
        waitForRendering(host.contentItem);
        mouseClick(item("vinylBarVideo"));
        verify(!host.pageData.videoShown);
    }
}
