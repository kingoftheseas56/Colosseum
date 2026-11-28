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
    readonly property url fixtureCover: Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg")
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
        property var requests: []
        signal completed(string lane, int id, var data, string error)
        function request(lane, operation, value, payload) {
            var next = Object.assign({}, calls);
            next[lane] = {id: ++serial, operation: operation, value: value, payload: payload};
            calls = next;
            requests.push(lane);
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
        property string homeStatus: "loading"
        property string homeError: ""
        property bool pickerAnswered: false
        property bool tastePickerOpen: false
        property bool tasteGateLocked: false
        property string pickerQuery: ""
        property bool artistSearchLoading: false
        property string artistSearchError: ""
        property bool artistSearchHasMore: false
        property var pickerDraft: []
        property var session: QtObject {
            property var source: transport
            property int profileGeneration: 1
            property bool profileActive: true
            property bool shuttingDown: false
        }
        function activate(item) { selected = item; }
        function selectTab(tab) {}
        function artistOptions(query) { return []; }
        function row(key, home) { return {key: key, title: "", items: []}; }
        function artwork(url) { return String(url) === String(test.fixtureCover) ? url : ""; }
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
    Component {
        id: warmHost
        Vinyl.VinylHouseShell {
            width: test.width
            height: test.height
            contentBottomInset: 80
            worldData: fixtureWorld
            runtimeReady: true
            hostReady: true
        }
    }
    function init() {
        fixtureWorld.homeStatus = "loading";
        fixtureWorld.selected = null;
        fixtureWorld.opened = null;
        fixtureWorld.session.shuttingDown = false;
        fixtureWorld.session.profileActive = true;
        transport.calls = ({});
        transport.canceled = [];
        transport.requests = [];
    }
    function open() {
        view = createTemporaryObject(host, test);
        verify(view !== null);
        tryVerify(() => !!transport.calls[lane(fixtures[0])]);
    }
    function visibleFixtures() {
        return fixtures.filter(fixture => {
            var row = shelf(fixture[0]);
            var y = row.mapToItem(view.pageFlickable, 0, 0).y;
            return y < view.pageFlickable.height && y + row.height > 0;
        });
    }
    function withinPrefetchHorizon() {
        var height = view.pageFlickable.height;
        return fixtures.filter(fixture => {
            var row = shelf(fixture[0]);
            var y = row.mapToItem(view.pageFlickable, 0, 0).y;
            return y < height + 2.5 * height && y + row.height > -2.5 * height;
        });
    }
    function scrollTo(key) {
        view.pageFlickable.contentY = Math.max(0, Math.min(view.parkY(shelf(key)),
            view.pageFlickable.contentHeight - view.pageFlickable.height));
        tryVerify(() => !!transport.calls[lane(fixtures.find(f => f[0] === key))]);
    }
    function requestAllByScrolling() {
        fixtures.forEach(fixture => {
            if (!transport.calls[lane(fixture)]) scrollTo(fixture[0]);
        });
        compare(transport.requests, fixtures.map(lane));
        view.pageFlickable.contentY = 0;
    }
    function takeReplies(counts) {
        fixtures.forEach(fixture => transport.finish(fixture, transport.pageReply(fixture, counts[fixture[0]] || 1)));
    }
    function finishCovered(fixture, count = 1) {
        var data = transport.pageReply(fixture, count);
        data.items.forEach(item => item.cover = String(fixtureCover));
        transport.finish(fixture, data);
    }
    function card(key, index) { return findChild(view, "vinylCard_explore-" + key + "_" + index); }
    function shelf(key) { return findChild(view, "vinylExploreRow_" + key); }

    function test_each_row_has_a_distinct_icon() {
        open();
        const sidebar = findChild(view, "vinylExploreRowIndexSidebar");
        const decades = {"2010s": "10s", "2000s": "00s", "90s": "90s", "80s": "80s", "70s": "70s"};
        const seen = new Set();
        compare(sidebar.visibleRows.length, 12);
        for (const row of sidebar.visibleRows) {
            const prefix = "vinylExploreRowIndexRow_" + row.key.replace(/[^A-Za-z0-9]/g, "_");
            const icon = findChild(sidebar, prefix + "Icon");
            verify(icon !== null, row.key);
            let glyph;
            if (decades[row.key]) {
                const label = findChild(sidebar, prefix + "IconText");
                verify(label && label.visible, row.key);
                compare(label.text, decades[row.key]);
                verify(!icon.visible, row.key);
                glyph = label.text;
            } else {
                verify(icon.visible, row.key);
                tryCompare(icon, "status", Image.Ready);
                // Changing the current row's stroke colour must not count as a different glyph.
                glyph = decodeURIComponent(icon.source.toString()).replace(/stroke="[^"]*"/, "");
            }
            verify(!seen.has(glyph), "Duplicate Explore icon on " + row.key);
            seen.add(glyph);
        }
    }

    function test_rowsInOrderAndRequests() {
        open();
        var page = findChild(view, "vinylExplorePage");
        var sidebar = findChild(view, "vinylExploreRowIndexSidebar");
        compare(page.rowCount, 12);
        compare(sidebar.rows.map(row => [row.key, row.title]), fixtures.map(fixture => fixture.slice(0, 2)));
        var initial = withinPrefetchHorizon();
        verify(initial.length > visibleFixtures().length + 1 && initial.length < fixtures.length);
        compare(transport.requests, initial.map(lane), "Open must queue the full look-ahead, not just the next shelf");
        var extent = view.pageFlickable.contentHeight;
        var deferred = fixtures[initial.length];
        var deferredHeight = shelf(deferred[0]).height;
        verify(deferredHeight > 132);
        verify(!card(deferred[0], 0));
        verify(!transport.calls[lane(deferred)]);
        view.pageFlickable.contentY += deferredHeight;
        var extended = withinPrefetchHorizon();
        verify(extended.includes(deferred));
        verify(!visibleFixtures().includes(deferred), "The newly requested row still must be off-screen");
        tryCompare(transport, "requests", extended.map(lane));
        verify(!transport.calls[lane(fixtures[fixtures.length - 1])]);
        finishCovered(deferred);
        tryVerify(() => card(deferred[0], 0) && card(deferred[0], 0).visible);
        compare(shelf(deferred[0]).height, deferredHeight);
        compare(view.pageFlickable.contentHeight, extent);
        requestAllByScrolling();
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
    }
    function test_firstScreenRevealsTogetherAfterArtwork() {
        open();
        var first = visibleFixtures();
        verify(first.length >= 2);
        var keys = first.map(f => f[0]);
        var extent = view.pageFlickable.contentHeight;
        first.slice(0, -1).forEach(f => transport.finish(f));
        var art = waitForArt(lastfmArtLane, "lastfm", keys.slice(0, -1).map(key => key + "-0"));
        var covers = {};
        keys.slice(0, -1).forEach(key => covers[key + "-0"] = String(fixtureCover));
        finishArt(lastfmArtLane, art, {covers: covers});
        tryCompare(findChild(card(keys[0], 0), "vinylArtworkImage"), "status", Image.Ready);
        verify(!card(keys[0], 0).visible, "A ready row must still wait for the other visible lists");
        transport.finish(first[first.length - 1]);
        art = waitForArt(lastfmArtLane, "lastfm", [keys[keys.length - 1] + "-0"], art);
        keys.forEach(key => {
            verify(!card(key, 0).visible, "Lists alone must not reveal any first-screen row");
            verify(shelf(key).showCardPlaceholder);
        });
        var shown = [];
        keys.forEach(key => {
            var tile = card(key, 0);
            tile.visibleChanged.connect(() => { if (tile.visible) shown.push(key); });
        });
        covers[keys[keys.length - 1] + "-0"] = String(fixtureCover);
        finishArt(lastfmArtLane, art, {covers: covers});
        tryVerify(() => card(keys[0], 0).visible, 1000);
        compare(shown.slice().sort(), keys.slice().sort());
        keys.forEach(key => compare(findChild(card(key, 0), "vinylArtworkImage").status, Image.Ready));
        compare(view.pageFlickable.contentHeight, extent);
    }
    function test_firstScreenRevealsTogetherOnArtworkTimeout() {
        open();
        var first = visibleFixtures();
        first.forEach(f => transport.finish(f));
        wait(250);
        first.forEach(f => verify(!card(f[0], 0).visible));
        tryVerify(() => card(first[0][0], 0).visible, 2500);
        first.forEach(f => verify(card(f[0], 0).visible));
    }
    function test_moodPreviewActivatesAndOpensFullFirstPage() {
        open();
        var page = findChild(view, "vinylExplorePage");
        var status = findChild(page, "vinylShelfStatus_explore-chill");
        verify(!status.visible);
        compare(status.text, "");
        var skeleton = findChild(page, "vinylShelfSkeleton_explore-chill");
        verify(skeleton && skeleton.visible && skeleton.skeleton && skeleton.squareArtwork);
        var fixture = fixtures[0];
        var data = transport.pageReply(fixture, 50);
        data.stale = true;
        data.fetchedAt = "2026-11-27T00:00:00Z";
        data.items.forEach(item => item.cover = String(fixtureCover));
        transport.finish(fixture, data);
        visibleFixtures().slice(1).forEach(f => finishCovered(f));
        tryVerify(() => shelf("chill").row.status === "ready");
        compare(status.text, "As of 2026-11-27 (cached)");
        verify(card("chill", 7));
        verify(!card("chill", 8));
        page.forceLayout();
        var first = card("chill", 0);
        verify(waitForRendering(first));
        compare(first.presentationKind, "songs");
        var tile = findChild(first, first.objectName + "_tile");
        mousePress(first);
        verify(first.pressed && tile.opacity < 1);
        mouseRelease(first);
        compare(tile.opacity, 1);
        compare(fixtureWorld.selected.id, "chill-0");
        var seeAll = findChild(page, "vinylShelfSeeAll_explore-chill");
        mouseClick(seeAll);
        verify(fixtureWorld.opened);
        compare(fixtureWorld.opened.key, "explore-chill");
        compare(fixtureWorld.opened.items.length, 50);
        compare(transport.requests, withinPrefetchHorizon().map(lane));
    }
    function test_invalidMoodReply_data() {
        return [{tag: "wrong list", field: "list", value: "happy"},
                {tag: "wrong kind", field: "kind", value: "albums"},
                {tag: "wrong source", field: "source", value: "kworb"},
                {tag: "invalid items", field: "items", value: [{catalogueCard: true, id: "wrong-source",
                    title: "Fixture song", kind: "songs", source: "wikipedia"}]}];
    }
    function test_invalidMoodReply(testData) {
        open();
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
        open();
        requestAllByScrolling();
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
    function test_artContinuesThroughOffscreenLoadedRowsWithoutScrolling() {
        open();
        var y = view.pageFlickable.contentY;
        var offscreen = withinPrefetchHorizon().filter(f => f[2] && !visibleFixtures().includes(f));
        verify(offscreen.length >= 2);
        var near = offscreen[0], far = offscreen[offscreen.length - 1];
        transport.finish(fixtures[0]);
        var first = waitForArt(lastfmArtLane, "lastfm", ["chill-0"]);
        // Arrive in reverse distance order while the visible batch is still running.
        transport.finish(far, transport.pageReply(far, 8));
        transport.finish(near, transport.pageReply(near, 8));
        compare(transport.calls[lastfmArtLane].id, first);
        finishArt(lastfmArtLane, first, {});
        var ids = f => Array.from({length: 8}, (_, i) => f[0] + "-" + i);
        var second = waitForArt(lastfmArtLane, "lastfm", ids(near).concat(ids(far).slice(0, 4)), first);
        finishArt(lastfmArtLane, second, {});
        var third = waitForArt(lastfmArtLane, "lastfm", ids(far).slice(4), second);
        var covers = {};
        covers[far[0] + "-7"] = String(fixtureCover);
        finishArt(lastfmArtLane, third, {covers: covers});
        compare(card(far[0], 7).item.cover, String(fixtureCover));
        compare(view.pageFlickable.contentY, y);
    }
    function test_stalledEditorialDoesNotBlockVisibleDecadeArtwork() {
        open();
        scrollTo("2010s");
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
        open();
        scrollTo("year-end-100-2025");
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
        verify(transport.canceled.includes(lane(fixtures[1])));
    }
    function test_failedRowRetriesWithoutReloadingOtherRows() {
        open();
        var fixture = fixtures[1];
        var page = findChild(view, "vinylExplorePage");
        withinPrefetchHorizon().forEach(entry => {
            if (entry === fixture) transport.finish(entry, null, "Fixture failure");
            else finishCovered(entry);
        });
        tryVerify(() => card("chill", 0) && card("chill", 0).visible);
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
        finishCovered(fixture);
        tryVerify(() => card("feel-good", 0) && card("feel-good", 0).visible);
        compare(card("feel-good", 0).item.title, "I'm Yours");
        compare(card("chill", 0), existing);
    }
    function beginWarmUp() {
        view = createTemporaryObject(warmHost, test);
        verify(view);
        wait(700);
        compare(transport.requests, []);
        fixtureWorld.homeStatus = "ready";
        tryVerify(() => !!transport.calls[lane(fixtures[0])]);
        var page = findChild(view, "vinylExplorePage");
        verify(page && !page.active && !page.visible);
        compare(view.activeTab, "Home");
        return page;
    }
    function test_warmUpUsesFirstScreenOnceAndExploreReusesIt_data() {
        return [{tag: "already warm", openDuringArt: false}, {tag: "in-flight handoff", openDuringArt: true}];
    }
    function test_warmUpUsesFirstScreenOnceAndExploreReusesIt(data) {
        var page = beginWarmUp();
        var lists = transport.requests.slice();
        verify(lists.length > 1 && lists.length < fixtures.length);
        compare(lists, fixtures.slice(0, lists.length).map(lane));
        fixtures.slice(0, lists.length).forEach(f => transport.finish(f));
        var ids = fixtures.slice(0, lists.length).map(f => f[0] + "-0");
        var art = waitForArt(lastfmArtLane, "lastfm", ids);
        var covers = {};
        ids.forEach(id => covers[id] = String(fixtureCover));
        if (data.openDuringArt) {
            view.setTab("explore");
            tryVerify(() => page.active);
            verify(!transport.canceled.includes(lastfmArtLane));
            verify(!card("chill", 0).visible);
        }
        finishArt(lastfmArtLane, art, {covers: covers});
        tryVerify(() => page.firstScreenRevealed);
        var first = card("chill", 0);
        compare(findChild(first, "vinylArtworkImage").status, Image.Ready);
        view.setTab("explore");
        tryVerify(() => first.visible);
        var extra = withinPrefetchHorizon().slice(lists.length);
        tryCompare(transport, "requests", lists.concat(lastfmArtLane, extra.map(lane)));
        extra.forEach(f => finishCovered(f));
        var all = transport.requests.slice();
        view.setTab("home");
        wait(750);
        compare(transport.requests, all);
        view.setTab("explore");
        tryVerify(() => first.visible);
        compare(card("chill", 0), first);
        compare(transport.requests, all);
    }
    function test_profileChangeAndShutdownCancelWarmUp() {
        var page = beginWarmUp();
        var first = fixtures[0];
        var second = fixtures[1];
        var oldList = transport.calls[lane(second)].id;
        transport.finish(first);
        var oldArt = waitForArt(lastfmArtLane, "lastfm", ["chill-0"]);
        fixtureWorld.session.profileActive = false;
        fixtureWorld.session.profileGeneration++;
        verify(transport.canceled.includes(lastfmArtLane));
        verify(transport.canceled.includes(lane(second)));
        transport.completed(lane(second), oldList, transport.pageReply(second, 1), "");
        finishArt(lastfmArtLane, oldArt, {covers: {"chill-0": String(fixtureCover)}});
        verify(!card("feel-good", 0));
        compare(page.artResults, {});
        fixtureWorld.session.profileActive = true;
        tryVerify(() => transport.calls[lane(second)].id > oldList);
        transport.finish(first);
        waitForArt(lastfmArtLane, "lastfm", ["chill-0"], oldArt);
        transport.canceled = [];
        fixtureWorld.session.shuttingDown = true;
        verify(transport.canceled.includes(lastfmArtLane));
        verify(transport.canceled.includes(lane(second)));
    }
    function test_rowIndexJumps() {
        open();
        var sidebar = findChild(view, "vinylExploreRowIndexSidebar");
        var target = fixtures[withinPrefetchHorizon().length];
        var action = findChild(sidebar, "vinylExploreRowIndexRow_" + target[0].replace(/-/g, "_") + "Action");
        var row = shelf(target[0]);
        verify(action !== null && row !== null);
        verify(!transport.calls[lane(target)]);
        verify(!card(target[0], 0));
        compare(view.pageFlickable.contentY, 0);
        verify(waitForRendering(action));
        var y = view.parkY(row);
        verify(y > 0);
        mouseClick(action);
        tryVerify(() => !!transport.calls[lane(target)]);
        tryVerify(() => Math.abs(view.pageFlickable.contentY - y) < 2);
        finishCovered(target);
        tryVerify(() => card(target[0], 0) && card(target[0], 0).visible);
        compare(sidebar.automationCurrentRow, target[1]);
    }
}
