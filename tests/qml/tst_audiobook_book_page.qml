import QtQuick
import QtTest
import "../../qml/audiobook" as Audiobook
import "fixtures/AudiobookPageFixtures.js" as Fixtures

TestCase {
    id: testCase
    name: "AudiobookBookPage"
    when: windowShown
    visible: true
    width: 1280
    height: 720
    property var page
    FontLoader { source: "../../assets/fonts/Fraunces-Regular.ttf" }
    Component {
        id: pageComponent
        Audiobook.AudiobookBookPage { width: 1280; height: 720; backdrop: Item {} }
    }
    SignalSpy { id: downloadSpy; signalName: "downloadRequested" }
    SignalSpy { id: listenSpy; signalName: "listenRequested" }
    SignalSpy { id: seriesSpy; signalName: "seriesBookRequested" }
    SignalSpy { id: backSpy; signalName: "backRequested" }

    function make(work, seriesWorks) {
        page = createTemporaryObject(pageComponent, testCase, { work: work });
        verify(page !== null);
        if (seriesWorks) page.seriesWorks = seriesWorks;
        downloadSpy.target = page; downloadSpy.clear();
        listenSpy.target = page; listenSpy.clear();
        seriesSpy.target = page; seriesSpy.clear();
        backSpy.target = page; backSpy.clear();
        verify(waitForRendering(page));
    }
    function item(name) { return findChild(page, name); }
    function visibleTexts(o, out) {
        o = o || page; out = out || [];
        if (o.visible && typeof o.text === "string") out.push(o.text);
        var children = o.children || [];
        for (var i = 0; i < children.length; ++i) visibleTexts(children[i], out);
        return out;
    }
    function test_singleBook() {
        make(Fixtures.single());
        compare(item("audiobookTitle").text, "Project Hail Mary");
        compare(item("audiobookAuthor").text, "By Andy Weir");
        compare(item("audiobookNarrator").text, "Ray Porter");
        compare(item("audiobookFact_runtime").text, "16 h 10 min");
        compare(item("audiobookCover").width, item("audiobookCover").height);
        compare(item("audiobookPrimaryAction").accessibleName, "Download");
        compare(item("audiobookLocalStatus").text, "Not downloaded");
    }
    function test_narratorSeparateFromAuthor() {
        make(Fixtures.series(), Fixtures.seriesWorks());
        compare(item("audiobookAuthor").text, "By Brandon Sanderson");
        compare(item("audiobookNarratorLabel").text, "NARRATED BY");
        compare(item("audiobookNarrator").text, "Kate Reading & Michael Kramer");
        compare(item("audiobookFact_runtime").text, "45 h 30 min");
    }
    function test_seriesOrderAndCurrentBook() {
        make(Fixtures.series(), Fixtures.seriesWorks());
        var ids = ["way-of-kings", "words-radiance", "edgedancer", "oathbringer", "dawnshard", "rhythm-war", "wind-truth"];
        compare(page.built.seriesBooks.map(function(b) { return b.workId; }).join(","), ids.join(","));
        compare(item("audiobookNumber_edgedancer").text, "2.5");
        compare(item("audiobookNumber_dawnshard").text, "3.5");
        for (var i = 0; i < ids.length; ++i)
            compare(item("audiobookCurrent_" + ids[i]).visible, i === 0);
        var work = Fixtures.series(); work.id = "dawnshard";
        page.work = work;
        verify(item("audiobookCurrent_dawnshard").visible);
        verify(!item("audiobookCurrent_way-of-kings").visible);
    }
    function test_downloadProgressListen() {
        make(Fixtures.single());
        var action = item("audiobookPrimaryAction");
        mouseClick(action);
        compare(downloadSpy.count, 1);
        compare(downloadSpy.signalArguments[0][0], "hail-mary");
        page.downloadState = { "rec-phm": { state: "active", progress: 0.42 } };
        compare(action.accessibleName, "Downloading 42%");
        verify(!action.enabled);
        compare(item("audiobookDownloadProgress").value, 0.42);
        action.activate();
        compare(downloadSpy.count, 1);
        page.downloadState = { "rec-phm": "done" };
        compare(action.accessibleName, "Listen");
        verify(action.enabled);
        action.forceActiveFocus(); keyClick(Qt.Key_Return);
        compare(listenSpy.count, 1);
        compare(listenSpy.signalArguments[0][0], "rec-phm");
        compare(downloadSpy.count, 1);
        compare(item("audiobookLocalStatus").text, "Downloaded · Ready to listen");
    }
    function test_catalogueCamelCaseSeries() {
        var books = Fixtures.seriesWorks().map(function(book) {
            return { id: book.id, title: book.title, numberText: book.number_text, positionSort: book.position_sort };
        });
        make(Fixtures.series(), books);
        compare(page.built.seriesBooks[2].workId, "edgedancer");
        compare(item("audiobookNumber_edgedancer").text, "2.5");
        compare(page.built.seriesBooks[4].workId, "dawnshard");
    }
    function test_catalogueWithoutCanonicalRecordingKey() {
        make({ id: "catalogue-work", title: "Catalogue title", recordings: [
            { id: "catalogue-recording", narrators: [{ name: "Narrator" }], runtime_min: 970 }
        ] });
        compare(item("audiobookFact_runtime").text, "16 h 10 min");
        compare(item("audiobookNarrator").text, "Narrator");
        page.downloadState = { "catalogue-work": { state: "active", progress: 0.25 } };
        compare(item("audiobookPrimaryAction").accessibleName, "Downloading 25%");
        // A catalogue ID cannot substitute for the download owner's canonical recording key.
        page.downloadState = { "catalogue-work": "done" };
        compare(item("audiobookPrimaryAction").accessibleName, "Download");
        page.downloadState = {};
        item("audiobookPrimaryAction").activate();
        compare(downloadSpy.signalArguments[0][0], "catalogue-work");
    }
    function test_localRecordingWinsOverUndownloadedFirst() {
        var work = Fixtures.single();
        work.recordings.push({ id: "other", recordingKey: "rec-local", narrators: ["Another Narrator"] });
        make(work);
        page.local = { "rec-local": { chapters: [] } };
        compare(item("audiobookPrimaryAction").accessibleName, "Listen");
        compare(item("audiobookNarrator").text, "Another Narrator");
        item("audiobookPrimaryAction").activate();
        compare(listenSpy.signalArguments[0][0], "rec-local");
    }
    function test_downloadWithoutSourceOrRecording() {
        make({ id: "bare", title: "Bare book" });
        verify(item("audiobookPrimaryAction").enabled);
        item("audiobookPrimaryAction").activate();
        compare(downloadSpy.signalArguments[0][0], "bare");
        compare(listenSpy.count, 0);
    }
    function test_failedDownloadCanRetry() {
        make(Fixtures.single());
        page.downloadState = { "rec-phm": "failed" };
        verify(item("audiobookPrimaryAction").enabled);
        item("audiobookPrimaryAction").activate();
        compare(downloadSpy.signalArguments[0][0], "hail-mary");
    }
    function test_seriesCoverSignalAndKeyboard() {
        make(Fixtures.series(), Fixtures.seriesWorks());
        mouseClick(item("audiobookSeries_edgedancer"));
        compare(seriesSpy.count, 1);
        compare(seriesSpy.signalArguments[0][0], "edgedancer");
        item("audiobookSeries_dawnshard").forceActiveFocus(); keyClick(Qt.Key_Return);
        compare(seriesSpy.count, 2);
        compare(seriesSpy.signalArguments[1][0], "dawnshard");
        compare(page.work.id, "way-of-kings");
    }
    function test_unknownFieldsOmitted() {
        make({ id: "bare", title: "Bare book", recordings: [{ id: "rec" }] });
        verify(!item("audiobookAuthor").visible);
        verify(!item("audiobookNarratorRow").visible);
        verify(!item("audiobookStory").visible);
        verify(!item("audiobookFacts").visible);
        compare(item("audiobookFact_runtime"), null);
        verify(!visibleTexts().some(function(t) { return t === "—" || t === "-" || t === "Unknown"; }));
    }
    function test_noSeriesNoRow() {
        make(Fixtures.single());
        verify(!item("audiobookSeriesShelf").visible);
        compare(item("audiobookSeries_way-of-kings"), null);
    }
    function test_backAndEscape() {
        make(Fixtures.single());
        mouseClick(item("audiobookBack"));
        compare(backSpy.count, 1);
        page.forceActiveFocus(); keyClick(Qt.Key_Escape);
        compare(backSpy.count, 2);
    }
    function test_desktopFit_data() {
        return [{ tag: "single", work: Fixtures.single(), books: [] },
                { tag: "series", work: Fixtures.series(), books: Fixtures.seriesWorks() }];
    }
    function test_desktopFit(data) {
        var books = data.books.slice();
        if (books.length) {
            for (var extra = 0; extra < 4; ++extra)
                books.push({ id: "extra-" + extra, title: "Later Book", positionSort: 20 + extra });
        }
        make(data.work, books);
        var names = ["audiobookCover", "audiobookPrimaryAction", "audiobookFacts"];
        if (data.books.length) names.push("audiobookSeriesShelf");
        for (var i = 0; i < names.length; ++i) {
            var node = item(names[i]), at = node.mapToItem(page, 0, 0);
            verify(at.y >= 88, names[i] + " clears topbar");
            verify(at.x >= 0 && at.x + node.width <= 1280, names[i] + " fits horizontally");
            verify(at.y + node.height <= 690, names[i] + " fits vertically");
            verify(at.x >= 230 || at.y + node.height < 630, names[i] + " clears dock");
        }
        var shelf = item("audiobookSeriesShelf"), facts = item("audiobookFacts");
        if (shelf.visible) {
            verify(shelf.y >= facts.mapToItem(shelf.parent, 0, facts.height).y + 12);
            var seriesScroll = item("audiobookSeriesScroll");
            verify(seriesScroll !== null && seriesScroll.contentWidth > seriesScroll.width);
            var firstY = item("audiobookSeries_" + page.built.seriesBooks[0].workId).mapToItem(shelf, 0, 0).y;
            for (var b = 0; b < page.built.seriesBooks.length; ++b) {
                var tile = item("audiobookSeries_" + page.built.seriesBooks[b].workId);
                var pos = tile.mapToItem(shelf, 0, 0);
                compare(pos.y, firstY);
                verify(pos.y >= 0 && pos.y + tile.height <= shelf.height);
            }
            var lastTile = item("audiobookSeries_extra-3");
            seriesScroll.contentX = seriesScroll.contentWidth - seriesScroll.width;
            verify(lastTile.mapToItem(shelf, lastTile.width, 0).x <= shelf.width);
        }
    }
    function test_smallWindowScrolls() {
        make(Fixtures.series(), Fixtures.seriesWorks());
        page.width = 640; page.height = 480;
        var scroll = item("audiobookScroll");
        verify(scroll.contentHeight > scroll.height);
        scroll.contentY = scroll.contentHeight - scroll.height;
        var tile = item("audiobookSeries_dawnshard");
        var at = tile.mapToItem(page, 0, 0);
        verify(at.y >= 88 && at.y + tile.height <= page.height);
        mouseClick(tile);
        compare(seriesSpy.signalArguments[0][0], "dawnshard");
    }
}
