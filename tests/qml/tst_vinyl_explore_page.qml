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
    readonly property var fixtures: ({
        "explore-week-songs": ["BbY WOW", "songs"],
        "explore-week-albums": ["Grand Theft Auto VI: The Album", "albums"],
        "explore-week-artists": ["Bad Bunny", "artists"],
        "explore-hot100-2025": ["All I Want for Christmas Is You", "songs"],
        "explore-global-200-2025": ["APT.", "songs"],
        "explore-year-end-100-2025": ["Die with a Smile", "songs"],
        "explore-mainstream-rock-2020s": ["Under the Graveyard", "songs"],
        "explore-hot100-1990s": ["How Am I Supposed to Live Without You", "songs"],
        "explore-most-streamed": ["Blinding Lights", "songs"]
    })
    QtObject {
        id: transport
        property int serial: 0
        property var calls: ({})
        signal completed(string lane, int id, var data, string error)
        function request(lane, operation, value, payload) {
            var next = Object.assign({}, calls);
            next[lane] = {id: ++serial, operation: operation, value: value, payload: payload};
            calls = next;
            return serial;
        }
        function cancel(lane) {}
        function finish(lane, error) {
            var req = calls[lane];
            var fixture = test.fixtures[lane];
            var card = {catalogueCard: true, id: lane, title: fixture[0], kind: fixture[1],
                artist: fixture[1] === "artists" ? fixture[0] : "Fixture artist",
                source: req.operation === "wikipediaList" ? "wikipedia" : "kworb"};
            completed(lane, req.id, {version: 1, items: [card], kind: card.kind, list: req.value,
                scope: "global", source: req.operation === "chartWeek" ? "chart-week" : card.source}, error || "");
        }
    }
    QtObject {
        id: fixtureWorld
        property var selected: null
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
            }
        }
    }
    function init() {
        fixtureWorld.selected = null;
        transport.calls = ({});
        view = createTemporaryObject(host, test);
        verify(view !== null);
        tryVerify(() => Object.keys(transport.calls).length === 9);
    }
    function takeReplies(failed) {
        Object.keys(fixtures).forEach(lane => transport.finish(lane, lane === failed ? "Fixture failure" : ""));
    }
    function card(key, index) { return findChild(view, "vinylCard_explore-" + key + "_" + index); }

    function test_fixtureRowsAndMixedCardActivation() {
        var page = findChild(view, "vinylExplorePage");
        compare(page.rowCount, 7);
        var loading = findChild(page, "vinylShelfStatus_explore-hot100-2025");
        verify(loading.visible);
        compare(loading.text, "Loading this shelf…");
        takeReplies("");
        compare(findChild(page, "vinylExploreAttribution").text, "Lists from Wikipedia");
        Object.keys(fixtures).filter(lane => !lane.startsWith("explore-week-")).forEach(lane => {
            compare(card(lane.slice("explore-".length), 0).item.title, fixtures[lane][0]);
        });
        page.forceLayout();
        verify(waitForRendering(findChild(page, "vinylExploreRowIndexRow_weekAction")));
        ["songs", "albums", "artists"].forEach((kind, i) => {
            var item = card("week", i);
            compare(item.presentationKind, kind);
            compare(item.item.title, fixtures["explore-week-" + kind][0]);
            verify(item.visible && item.width > 0 && item.height > 0);
            mouseClick(item);
            compare(fixtureWorld.selected.id, "explore-week-" + kind);
        });
    }
    function test_previewArtworkUpdatesCardsInPlace() {
        takeReplies("");
        var weekCard = card("week", 2);
        var wikiCard = card("hot100-2025", 0);
        var streamedCard = card("most-streamed", 0);
        verify(weekCard && wikiCard && streamedCard);
        tryVerify(() => !!transport.calls["explore-art"]);
        var request = transport.calls["explore-art"];
        compare(request.operation, "resolveCatalogueArt");
        compare(request.value, "editorial");
        verify(request.payload.items.some(item => item.id === weekCard.item.id));
        verify(request.payload.items.some(item => item.id === wikiCard.item.id));
        verify(request.payload.items.some(item => item.id === streamedCard.item.id));

        var covers = {};
        covers[weekCard.item.id] = "https://cdn-images.dzcdn.net/artist/1000x1000.jpg";
        covers[wikiCard.item.id] = "https://is1-ssl.mzstatic.com/song/600x600bb.jpg";
        var settled = request.payload.items.map(item => item.id);
        transport.completed("catalogue-art", request.id, {version: 1, covers: covers, settled: settled}, "");
        verify(!weekCard.item.cover);
        transport.completed("explore-art", request.id, {version: 1, covers: covers, settled: settled}, "");
        compare(card("week", 2), weekCard);
        compare(card("hot100-2025", 0), wikiCard);
        compare(weekCard.item.cover, covers[weekCard.item.id]);
        compare(wikiCard.item.cover, covers[wikiCard.item.id]);
        verify(!streamedCard.item.cover);
    }
    function test_failedRowRetriesWithoutReloadingOtherRows() {
        takeReplies("explore-global-200-2025");
        var existing = card("hot100-2025", 0);
        verify(existing !== null);
        compare(card("most-streamed", 0).item.title, "Blinding Lights");
        var retry = findChild(view, "vinylShelfRetry_explore-global-200-2025");
        verify(retry.visible && retry.enabled);
        var serial = transport.serial;
        view.parkRow(findChild(view, "vinylExploreRow_global-200-2025"));
        tryVerify(() => {
            var y = retry.mapToItem(view.pageFlickable, 0, 0).y;
            return y >= 0 && y + retry.height <= view.pageFlickable.height;
        });
        mouseClick(retry);
        compare(transport.serial, serial + 1);
        compare(transport.calls["explore-global-200-2025"].id, serial + 1);
        verify(!retry.visible);
        transport.finish("explore-global-200-2025");
        compare(card("global-200-2025", 0).item.title, "APT.");
        compare(card("hot100-2025", 0), existing);
    }
    function test_rowIndexJumps() {
        takeReplies("");
        var sidebar = findChild(view, "vinylExploreRowIndexSidebar");
        var action = findChild(sidebar, "vinylExploreRowIndexRow_year_end_100_2025Action");
        var row = findChild(view, "vinylExploreRow_year-end-100-2025");
        verify(action !== null && row !== null);
        compare(view.pageFlickable.contentY, 0);
        verify(waitForRendering(action));
        var y = view.parkY(row);
        verify(y > 0);
        mouseClick(action);
        tryVerify(() => Math.abs(view.pageFlickable.contentY - y) < 2);
        compare(sidebar.automationCurrentRow, "The year-end 100");
    }
}
