import QtQuick
import QtTest
import "../../qml" as House
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylExplorePage"
    when: windowShown
    visible: true
    width: 1280
    height: 720
    property var view: null
    readonly property string lastfmArtLane: "explore-art-lastfm"
    readonly property string editorialArtLane: "explore-art"
    readonly property var fixtures: [
        ["chill", "Chill", "chill", "Sweater Weather"],
        ["feel-good", "Feel good", "happy", "I'm Yours"],
        ["sad", "Sad songs", "sad", "Forget Her"],
        ["love", "Love songs", "romantic", "Careless Whisper"],
        ["party", "Party", "party", "Tik Tok"],
        ["year-end-100-2025", "Biggest songs of 2025", "", "Die with a Smile"],
        ["2010s", "The 2010s", "2010s", "Someone You Loved"],
        ["2000s", "The 2000s", "00s", "Fixture 2000s song"],
        ["90s", "The '90s", "90s", "No Scrubs"],
        ["80s", "The '80s", "80s", "Purple Rain"],
        ["70s", "The '70s", "70s", "Landslide"],
        ["most-streamed", "Most streamed ever", "", "Blinding Lights"]
    ]
    function lane(fixture) { return fixture[2] ? "explore-tag-" + fixture[2] : "explore-" + fixture[0]; }
    QtObject {
        id: transport
        property int serial: 0
        property var calls: ({})
        property var canceled: []
        signal completed(string lane, int id, var data, string error)
        function request(lane, operation, value, payload) {
            var next = Object.assign({}, calls);
            next[lane] = {id: ++serial, operation: operation, value: value, payload: payload};
            calls = next;
            return serial;
        }
        function cancel(lane) { canceled.push(lane); }
        function pageReply(fixture, count) {
            var key = fixture[0];
            var list = fixture[2] || key;
            var source = fixture[2] ? "lastfm" : key === "most-streamed" ? "kworb" : "wikipedia";
            var items = Array.from({length: count}, (_, i) => ({catalogueCard: true,
                id: key + "-" + i, title: (i ? "Fixture song " + i : fixture[3]), kind: "songs",
                artist: "Fixture artist", source: source}));
            return {version: 1, items: items, kind: "songs", list: list, source: source,
                complete: !fixture[2], nextCursor: fixture[2] ? {page: 2} : {}};
        }
        function finish(fixture, data, error) {
            var lane = test.lane(fixture);
            completed(lane, calls[lane].id, data || pageReply(fixture, 1), error || "");
        }
    }
    QtObject {
        id: fixtureWorld
        property var selected: null
        property var opened: null
        property var session: QtObject {
            property var source: transport
            property int profileGeneration: 1
            property bool profileActive: true
            property bool shuttingDown: false
        }
        function activate(item) { selected = item; }
        function artwork(url) { return ""; }
    }
    Component {
        id: host
        House.WorldPage {
            id: shell
            width: test.width
            height: test.height
            medium: "Vinyl"
            Vinyl.VinylExplorePage {
                width: parent.width
                height: implicitHeight
                worldData: fixtureWorld
                pageFlick: shell.pageFlickable
                onRowRequested: target => shell.parkRow(target)
                onListRequested: row => fixtureWorld.opened = row
            }
        }
    }
    function init() {
        fixtureWorld.selected = null;
        fixtureWorld.opened = null;
        transport.calls = ({});
        transport.canceled = [];
        view = createTemporaryObject(host, test);
        verify(view !== null);
        tryVerify(() => Object.keys(transport.calls).length === fixtures.length);
    }
    function takeReplies(counts) {
        fixtures.forEach(fixture => transport.finish(fixture, transport.pageReply(fixture, counts[fixture[0]] || 1)));
    }
    function card(key, index) { return findChild(view, "vinylCard_explore-" + key + "_" + index); }
    function shelf(key) { return findChild(view, "vinylExploreRow_" + key); }

    function test_rowsInOrderAndRequests() {
        var page = findChild(view, "vinylExplorePage");
        var sidebar = findChild(view, "vinylExploreRowIndexSidebar");
        compare(page.rowCount, 12);
        compare(sidebar.rows.map(row => [row.key, row.title]), fixtures.map(fixture => fixture.slice(0, 2)));
        compare(Object.keys(transport.calls), fixtures.map(lane));
        fixtures.forEach(fixture => {
            var req = transport.calls[lane(fixture)];
            if (fixture[2]) {
                compare(req.operation, "browseCatalogue");
                compare(req.value, "songs");
                compare(req.payload, {catalogueSource: "lastfm", list: fixture[2], cursor: {}, pageSize: 50});
            } else {
                var wiki = fixture[0] === "year-end-100-2025";
                compare(req.operation, wiki ? "wikipediaList" : "mostStreamed");
                compare(req.value, wiki ? fixture[0] : "songs");
                compare(req.payload, {});
            }
        });
        ["week", "hot100-2025", "global-200-2025", "mainstream-rock-2020s", "hot100-1990s"].forEach(key => {
            verify(!shelf(key));
            verify(!transport.calls["explore-" + key]);
        });
        compare(findChild(page, "vinylExploreAttribution").text, "Lists from Last.fm, Kworb and Wikipedia");
        takeReplies({});
        fixtures.forEach(fixture => compare(card(fixture[0], 0).item.title, fixture[3]));
    }
    function test_moodPreviewActivatesAndOpensFullFirstPage() {
        var page = findChild(view, "vinylExplorePage");
        var status = findChild(page, "vinylShelfStatus_explore-chill");
        verify(status.visible);
        compare(status.text, "Loading this shelf…");
        var fixture = fixtures[0];
        var data = transport.pageReply(fixture, 50);
        data.stale = true;
        data.fetchedAt = "2026-11-27T00:00:00Z";
        transport.finish(fixture, data);
        compare(shelf("chill").row.status, "ready");
        compare(status.text, "As of 2026-11-27 (cached)");
        verify(card("chill", 7));
        verify(!card("chill", 8));
        page.forceLayout();
        var first = card("chill", 0);
        verify(waitForRendering(first));
        compare(first.presentationKind, "songs");
        mouseClick(first);
        compare(fixtureWorld.selected.id, "chill-0");
        var seeAll = findChild(page, "vinylShelfSeeAll_explore-chill");
        mouseClick(seeAll);
        verify(fixtureWorld.opened);
        compare(fixtureWorld.opened.key, "explore-chill");
        compare(fixtureWorld.opened.items.length, 50);
        compare(Object.keys(transport.calls).filter(lane => lane.startsWith("explore-tag-")).length, 10);
    }
    function test_invalidMoodReply_data() {
        return [{tag: "wrong list", field: "list", value: "happy"},
                {tag: "wrong kind", field: "kind", value: "albums"},
                {tag: "wrong source", field: "source", value: "kworb"},
                {tag: "invalid items", field: "items", value: [{catalogueCard: true, id: "wrong-source",
                    title: "Fixture song", kind: "songs", source: "wikipedia"}]}];
    }
    function test_invalidMoodReply(testData) {
        var fixture = fixtures[0];
        var data = transport.pageReply(fixture, 1);
        data[testData.field] = testData.value;
        transport.finish(fixture, data);
        compare(shelf("chill").row.status, "error");
        verify(!card("chill", 0));
        verify(findChild(view, "vinylShelfRetry_explore-chill").visible);
    }
    function waitForArt(lane, value, ids, after = 0) {
        tryVerify(() => transport.calls[lane] && transport.calls[lane].id > after,
            5000, "Expected a new " + value + " artwork request");
        var batch = transport.calls[lane];
        compare(batch.operation, "resolveCatalogueArt");
        compare(batch.value, value);
        compare(batch.payload.items.map(item => item.id), ids);
        return batch.id;
    }
    function finishArt(lane, id, result) {
        transport.completed(lane, id, Object.assign({version: 1, covers: {}}, result), "");
    }
    function test_previewArtworkBatchesBySourceAndUpdatesCardsInPlace() {
        takeReplies({chill: 10, "feel-good": 8});
        var moodCard = card("chill", 0);
        var wikiCard = card("year-end-100-2025", 0);
        var moodCover = "https://lastfm-img.freetls.fastly.net/chill.jpg";
        var wikiCover = "https://is1-ssl.mzstatic.com/song/600x600bb.jpg";
        var lastfm = waitForArt(lastfmArtLane, "lastfm", Array.from({length: 8}, (_, i) => "chill-" + i)
            .concat(Array.from({length: 4}, (_, i) => "feel-good-" + i)));
        var moodResult = {covers: {"chill-0": moodCover}};
        finishArt("catalogue-art", lastfm, moodResult);
        verify(!moodCard.item.cover);
        finishArt(lastfmArtLane, lastfm, moodResult);
        compare(card("chill", 0), moodCard);
        compare(moodCard.item.cover, moodCover);

        var chartIds = ["year-end-100-2025-0", "most-streamed-0"];
        var editorial = waitForArt(editorialArtLane, "editorial", chartIds);
        var chartCovers = {"year-end-100-2025-0": wikiCover};
        finishArt(editorialArtLane, editorial, {settled: [], covers: chartCovers});
        verify(!wikiCard.item.cover);
        lastfm = waitForArt(lastfmArtLane, "lastfm", ["feel-good-4", "feel-good-5", "feel-good-6", "feel-good-7",
            "sad-0", "love-0", "party-0", "2010s-0", "2000s-0", "90s-0", "80s-0", "70s-0"], lastfm);
        finishArt(lastfmArtLane, lastfm, {});
        editorial = waitForArt(editorialArtLane, "editorial", chartIds, editorial);
        finishArt(editorialArtLane, editorial, {settled: [chartIds[0]], covers: chartCovers});
        compare(card("year-end-100-2025", 0), wikiCard);
        compare(wikiCard.item.cover, wikiCover);
        editorial = waitForArt(editorialArtLane, "editorial", [chartIds[1]], editorial);
        finishArt(editorialArtLane, editorial, {settled: [chartIds[1]]});
        verify(!card("most-streamed", 0).item.cover);
        var serial = transport.serial;
        wait(1400);
        compare(transport.serial, serial);
    }
    function test_stalledEditorialDoesNotBlockVisibleDecadeArtwork() {
        var chart = fixtures.find(fixture => fixture[0] === "year-end-100-2025");
        var decade = fixtures.find(fixture => fixture[0] === "2010s");
        transport.finish(chart);
        var stalled = waitForArt(editorialArtLane, "editorial", ["year-end-100-2025-0"]);

        transport.finish(fixtures[0]);
        transport.finish(decade);
        findChild(view, "vinylExplorePage").forceLayout();
        view.pageFlickable.contentY = view.parkY(shelf("2010s"));
        var lastfm = waitForArt(lastfmArtLane, "lastfm", ["2010s-0", "chill-0"]);
        var cover = "https://lastfm-img.freetls.fastly.net/2010s.jpg";
        finishArt(lastfmArtLane, lastfm, {covers: {"2010s-0": cover}});
        compare(card("2010s", 0).item.cover, cover);
        compare(transport.calls[editorialArtLane].id, stalled);
        verify(!card("year-end-100-2025", 0).item.cover);
    }
    function test_cancelsBothArtworkLanes_data() {
        return [{tag: "leave", profileChange: false}, {tag: "profile change", profileChange: true}];
    }
    function test_cancelsBothArtworkLanes(data) {
        var page = findChild(view, "vinylExplorePage");
        transport.finish(fixtures[0]);
        transport.finish(fixtures[5]);
        var lastfm = waitForArt(lastfmArtLane, "lastfm", ["chill-0"]);
        var editorial = waitForArt(editorialArtLane, "editorial", ["year-end-100-2025-0"]);
        if (data.profileChange) fixtureWorld.session.profileGeneration++;
        else page.active = false;
        compare(transport.canceled.filter(lane => lane === lastfmArtLane || lane === editorialArtLane).sort(),
            [editorialArtLane, lastfmArtLane]);
        var covers = {"chill-0": "https://lastfm-img.freetls.fastly.net/chill.jpg",
            "year-end-100-2025-0": "https://is1-ssl.mzstatic.com/song/600x600bb.jpg"};
        finishArt(lastfmArtLane, lastfm, {covers: covers});
        finishArt(editorialArtLane, editorial, {settled: ["year-end-100-2025-0"], covers: covers});
        compare(page.artResults, {});
    }
    function test_failedRowRetriesWithoutReloadingOtherRows() {
        var fixture = fixtures[1];
        var page = findChild(view, "vinylExplorePage");
        fixtures.forEach(entry => transport.finish(entry, null, entry === fixture ? "Fixture failure" : ""));
        var existing = card("chill", 0);
        var retry = findChild(view, "vinylShelfRetry_explore-feel-good");
        verify(retry.visible && retry.enabled);
        var serial = transport.serial;
        page.forceLayout();
        verify(waitForRendering(retry));
        mouseClick(retry);
        compare(transport.serial, serial + 1);
        compare(transport.calls[lane(fixture)].id, serial + 1);
        verify(!retry.visible);
        transport.finish(fixture);
        compare(card("feel-good", 0).item.title, "I'm Yours");
        compare(card("chill", 0), existing);
    }
    function test_rowIndexJumps() {
        takeReplies({});
        var sidebar = findChild(view, "vinylExploreRowIndexSidebar");
        var action = findChild(sidebar, "vinylExploreRowIndexRow_year_end_100_2025Action");
        var row = shelf("year-end-100-2025");
        verify(action !== null && row !== null);
        compare(view.pageFlickable.contentY, 0);
        verify(waitForRendering(action));
        var y = view.parkY(row);
        verify(y > 0);
        mouseClick(action);
        tryVerify(() => Math.abs(view.pageFlickable.contentY - y) < 2);
        compare(sidebar.automationCurrentRow, "Biggest songs of 2025");
    }
}
