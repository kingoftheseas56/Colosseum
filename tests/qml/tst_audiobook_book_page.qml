import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/audiobook" as Audiobook
import "../../qml/vinyl" as Vinyl

TestCase {
    id: testCase
    name: "AudiobookBookPage"
    when: windowShown
    width: 1280
    height: 700

    Component {
        id: hostComponent
        Window {
            property alias page: bookPage
            width: 1280
            height: 700
            visible: true
            color: "#0c0f18"
            Vinyl.VinylBackdrop { id: bookBackdrop; anchors.fill: parent; contentStore: null }
            Audiobook.AudiobookBookPage { id: bookPage; anchors.fill: parent; backdrop: bookBackdrop }
        }
    }
    SignalSpy { id: listenSpy; signalName: "listenRequested" }
    SignalSpy { id: downloadSpy; signalName: "downloadRequested" }
    SignalSpy { id: authorSpy; signalName: "authorRequested" }
    SignalSpy { id: seriesSpy; signalName: "seriesRequested" }
    SignalSpy { id: backSpy; signalName: "backRequested" }

    readonly property var rhythmWork: ({
        id: "work-rhythm",
        title: "Rhythm of War",
        authors: ["Brandon Sanderson"],
        series: [{ id: "series-stormlight", name: "The Stormlight Archive", number_text: "4" }],
        description: "The fourth Stormlight Archive novel."
    })
    readonly property var duneWork: ({
        id: "work-dune-messiah",
        title: "Dune Messiah",
        authors: ["Frank Herbert"],
        series: [
            { name: "Dune", number_text: "2" },
            { name: "Dune Chronicles", number_text: "2" }
        ]
    })
    // The second recording leaves runtime out on purpose: unknown stays absent.
    readonly property var rhythmRecordings: ([
        {
            recordingKey: "rec-kr",
            narrators: ["Kate Reading", "Michael Kramer"],
            runtime_min: 3015,
            language: "English",
            abridgement: "unabridged",
            release_year: 2020,
            format: "M4B",
            source: "AudioBookBay",
            sourceMatch: "exact"
        },
        {
            recordingKey: "rec-ga",
            narrators: ["Full cast"],
            language: "English",
            abridgement: "dramatized",
            source: "AudioBookBay",
            sourceMatch: "probable"
        },
        {
            recordingKey: "rec-bundle",
            narrators: [],
            source: "AudioBookBay",
            sourceMatch: "exact",
            bundle: true
        }
    ])

    function walk(o, visit) {
        if (!o) return null;
        var found = visit(o);
        if (found) return found;
        var lists = [o.children || []];
        if (o.contentItem) lists.push(o.contentItem.children || []);
        for (var l = 0; l < lists.length; ++l)
            for (var i = 0; i < lists[l].length; ++i) {
                found = walk(lists[l][i], visit);
                if (found) return found;
            }
        return null;
    }
    function item(name) {
        return walk(page, function (o) { return o.objectName === name ? o : null; });
    }
    function visibleTexts() {
        var out = [];
        walk(page, function (o) {
            if (typeof o.text === "string" && o.contentWidth !== undefined && o.visible)
                out.push(o.text);
            return null;
        });
        return out;
    }
    function anyText(fragment) {
        return visibleTexts().some(function (t) { return t.indexOf(fragment) >= 0; });
    }
    property var page: null
    property var host: null
    function make(work, recordings, extra) {
        if (host) { host.close(); host.destroy(); host = null; }
        host = createTemporaryObject(hostComponent, null);
        verify(host !== null);
        page = host.page;
        page.work = work;
        page.recordings = recordings;
        for (var k in (extra || {})) page[k] = extra[k];
        listenSpy.target = page; listenSpy.clear();
        downloadSpy.target = page; downloadSpy.clear();
        authorSpy.target = page; authorSpy.clear();
        seriesSpy.target = page; seriesSpy.clear();
        verify(waitForRendering(host.contentItem));
        return page;
    }
    function cleanup() {
        page = null;
        if (host) { host.close(); host.destroy(); host = null; }
    }

    function test_recordingsAreListedAndLabelled() {
        make(rhythmWork, rhythmRecordings);
        var first = item("vinylEdition_rec-kr");
        var second = item("vinylEdition_rec-ga");
        verify(first !== null && second !== null, "both recordings are listed");
        compare(first.accessibleName, "Kate Reading & Michael Kramer · Unabridged · English");
        compare(second.accessibleName, "Full cast · Dramatized · English");
        compare(page.built.editions.length, 2);
        verify(anyText("Recordings"), "the section is called Recordings");
        verify(!anyText("Editions"));
    }

    function test_probableMatchShowsTheWord() {
        make(rhythmWork, rhythmRecordings);
        verify(anyText("probable match"), "probable recording row says probable");
        compare(page.built.editions[0].detail.indexOf("probable"), -1);
        verify(page.built.editions[1].detail.indexOf("probable match") >= 0);
        item("vinylEdition_rec-ga").activate();
        compare(page.shownEdition.id, "rec-ga");
        verify(item("audiobookSourceMatch").visible);
        item("vinylEdition_rec-kr").activate();
        verify(!item("audiobookSourceMatch").visible);
    }

    function test_bundleLandsUnderOtherDownloads() {
        make(rhythmWork, rhythmRecordings);
        verify(item("vinylEdition_rec-bundle") === null, "a bundle is not a recording");
        var row = item("audiobookOther_rec-bundle");
        verify(row !== null, "a bundle is listed under Other downloads");
        verify(item("vinylSection_extra").visible);
        verify(anyText("Other downloads"));
        verify(anyText("A bundle: this download holds more than one book"));
        row.activate();
        compare(downloadSpy.count, 1);
        compare(downloadSpy.signalArguments[0][0], "rec-bundle");
    }

    function test_unmatchedSourceIsNotARecording() {
        make(rhythmWork, [{ recordingKey: "loose", source: "Tankorent", sourceMatch: "none" }]);
        verify(item("vinylEdition_loose") === null);
        verify(item("audiobookOther_loose") !== null);
        verify(!item("vinylSection_editions").visible);
        verify(anyText("Not matched to a recording of this book"));
    }

    function test_primaryActionFollowsDownloadState() {
        make(rhythmWork, rhythmRecordings);
        var action = item("vinylAlbumPrimaryAction");
        verify(action !== null);
        compare(action.accessibleName, "Download");
        verify(action.enabled);
        action.activate();
        compare(downloadSpy.count, 1);
        compare(downloadSpy.signalArguments[0][0], "rec-kr");
        compare(listenSpy.count, 0);

        page.downloadState = { "rec-kr": { state: "active", progress: 0.42 } };
        compare(item("vinylAlbumPrimaryAction").accessibleName, "Downloading 42%");
        verify(!item("vinylAlbumPrimaryAction").enabled);

        page.downloadState = { "rec-kr": "failed" };
        verify(item("vinylAlbumPrimaryAction").enabled);

        page.downloadState = { "rec-kr": "done" };
        action = item("vinylAlbumPrimaryAction");
        compare(action.accessibleName, "Listen");
        action.activate();
        compare(listenSpy.count, 1);
        compare(listenSpy.signalArguments[0][0], "rec-kr");
        compare(downloadSpy.count, 1);
        // The other recording is still not downloaded.
        item("vinylEdition_rec-ga").activate();
        compare(item("vinylAlbumPrimaryAction").accessibleName, "Download");
    }

    function test_chaptersShowOnlyWhenLocal() {
        make(rhythmWork, rhythmRecordings);
        verify(item("vinylTrackRow_0") === null, "no chapters before download");
        verify(item("audiobookRecordingDetails") !== null);
        verify(item("audiobookFact_runtime") !== null, "recording facts stand where chapters will be");
        verify(!anyText("Chapters"));
        page.local = { "rec-kr": { chapters: [
            { title: "Prelude", seconds: 600 },
            { title: "Chapter 1", seconds: 1800 }
        ] } };
        verify(item("vinylTrackRow_0") !== null);
        verify(item("vinylTrackRow_1") !== null);
        verify(item("vinylTrackRow_2") === null);
        verify(anyText("Chapters"));
        verify(anyText("Prelude"));
        verify(item("audiobookRecordingDetails") === null || !item("audiobookRecordingDetails").visible);
        compare(item("vinylAlbumPrimaryAction").accessibleName, "Listen");
        // Another, undownloaded recording has no chapters.
        item("vinylEdition_rec-ga").activate();
        verify(item("vinylTrackRow_0") === null);
    }

    function test_unknownFieldsAreAbsent() {
        make(rhythmWork, rhythmRecordings);
        item("vinylEdition_rec-ga").activate();
        verify(item("audiobookFact_runtime") === null, "unknown runtime is absent");
        verify(item("audiobookFact_format") === null);
        verify(item("audiobookFact_year") === null);
        verify(item("audiobookFact_language") !== null);
        verify(!visibleTexts().some(function (t) { return t === "—" || t === "-" || t === "Unknown"; }),
               "no placeholder dashes");
        item("vinylEdition_rec-kr").activate();
        verify(item("audiobookFact_runtime") !== null);
        compare(item("audiobookFact_runtime").children[1].text, "50 h 15 min");
        // A work without a description or series shows neither.
        make({ id: "w", title: "Bare Book", authors: ["Someone"] }, []);
        verify(!item("vinylSection_about").visible);
        verify(item("vinylAlbumMeta_series") === null);
        verify(item("vinylAlbumMeta_narrator") === null);
        verify(item("audiobookNoRecordings") !== null);
    }

    function test_seriesLineAndSeveralSeries() {
        make(duneWork, [{ recordingKey: "dune-1", narrators: ["Scott Brick"], source: "AudioBookBay", sourceMatch: "exact" }]);
        compare(item("vinylAlbumMeta_series").accessibleName, "Dune, Book 2");
        var other = item("vinylInfo_series1");
        verify(other !== null, "the second series is not dropped silently");
        verify(anyText("Dune Chronicles, Book 2"));
        verify(!anyText("Also in the series Dune, Book 2"));
        make(rhythmWork, rhythmRecordings);
        compare(item("vinylAlbumMeta_series").accessibleName, "The Stormlight Archive, Book 4");
        compare(item("vinylAlbumMeta_narrator").accessibleName, "Narrated by Kate Reading & Michael Kramer");
    }

    function test_authorAndSeriesSignals() {
        make(rhythmWork, rhythmRecordings);
        var author = walk(item("vinylAlbumArtist"), function (o) {
            return o.accessibleName === "Brandon Sanderson" ? o : null;
        });
        verify(author !== null);
        author.activate();
        compare(authorSpy.count, 1);
        compare(authorSpy.signalArguments[0][0], "Brandon Sanderson");
        item("vinylAlbumMeta_series").activate();
        compare(seriesSpy.count, 1);
        compare(seriesSpy.signalArguments[0][0], "series-stormlight");
    }

    function test_noVinylOnlyControls() {
        make(rhythmWork, rhythmRecordings);
        compare(item("vinylAlbumPlay"), null);
        compare(item("vinylAlbumShuffle"), null);
        compare(item("vinylAlbumSave"), null);
        compare(item("vinylAlbumNext"), null);
        compare(item("vinylAlbumQueue"), null);
        compare(item("vinylAlbumPlaylist"), null);
        var collections = item("vinylAlbumAddToCollections");
        verify(collections === null || !collections.visible);
        verify(!item("vinylSection_videos").visible);
        verify(!item("vinylSection_credits").visible);
        verify(!item("vinylSection_more").visible);
        verify(!page.albumPage.discSwitchVisible);
        verify(!item("vinylAlbumFacts").visible);
        verify(!anyText("Tracks") && !anyText("Shuffle"));
    }

    function test_backSignal() {
        make(rhythmWork, rhythmRecordings);
        backSpy.target = page;
        backSpy.clear();
        item("audiobookBack").activate();
        compare(backSpy.count, 1);
    }

    function test_fitsDesktopViewport() {
        make(rhythmWork, rhythmRecordings);
        var cover = item("vinylAlbumCover");
        verify(cover.mapToItem(page, 0, 0).y >= 0);
        var action = item("vinylAlbumPrimaryAction");
        var at = action.mapToItem(page, 0, 0);
        verify(at.y >= 0 && at.y + action.height <= page.height, "the primary action is on screen at 1280x700");
    }
}
