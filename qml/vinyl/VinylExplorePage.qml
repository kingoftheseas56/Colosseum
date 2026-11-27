import QtQuick
import ".."

pragma ComponentBehavior: Bound

Item {
    id: page
    objectName: "vinylExplorePage"
    required property var worldData
    required property Flickable pageFlick
    property Item backdrop: null
    property bool active: true
    readonly property var session: worldData.session || null
    readonly property var source: session ? session.source : null
    readonly property int profileGeneration: session ? session.profileGeneration || 0 : 0
    readonly property bool profileReady: !!source && session.profileActive !== false && !session.shuttingDown
    readonly property int rowCount: shelves.count
    property int shelfRevision: 0
    property var artResults: ({})
    property var artBatch: []
    property double artDeadline: 0
    property var pendingArt: null
    readonly property real contentLeft: rowIndex.contentLeft
    implicitHeight: shelfColumn.implicitHeight
    signal rowRequested(var target)
    signal listRequested(var row)

    function forceLayout() { shelfColumn.forceLayout(); }
    function loadRows() {
        if (!active || !profileReady) return;
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (shelf) shelf.load(false);
        }
    }
    function resetRows() {
        stopArt();
        artResults = ({});
        if (!shelves) return;
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (shelf) shelf.reset();
        }
        Qt.callLater(page.loadRows);
    }
    onActiveChanged: {
        if (active) {
            Qt.callLater(page.loadRows);
            scheduleArt();
        } else stopArt();
    }
    onSourceChanged: resetRows()
    onProfileGenerationChanged: resetRows()
    onProfileReadyChanged: resetRows()
    Component.onCompleted: Qt.callLater(page.loadRows)
    Component.onDestruction: stopArt()

    function stopArt() {
        if (artPoll) artPoll.stop();
        var request = pendingArt;
        pendingArt = null;
        artBatch = [];
        if (request && request.source) request.source.cancel("explore-art");
    }
    function scheduleArt() {
        if (active && profileReady && !pendingArt && !artPoll.running) artPoll.start();
    }
    function requestArt() {
        if (!active || !profileReady || pendingArt) return;
        var batch = artBatch.filter(card => !artResults[card.id]);
        if (batch.length && Date.now() >= artDeadline) {
            var timedOut = Object.assign({}, artResults);
            for (var card of batch) timedOut[card.id] = {};
            artResults = timedOut;
            batch = [];
        }
        if (!batch.length) {
            // Enrich the shelf previews, not hundreds of offscreen chart entries.
            var seen = new Set();
            for (var i = 0; i < shelves.count && batch.length < 12; ++i) {
                var shelf = shelves.itemAt(i);
                if (!shelf) continue;
                for (var item of shelf.items.slice(0, shelf.previewCount)) {
                    if (item.cover || artResults[item.id] || seen.has(item.id)
                            || !["kworb", "wikipedia"].includes(item.source)) continue;
                    batch.push(item);
                    seen.add(item.id);
                    if (batch.length === 12) break;
                }
            }
            artDeadline = Date.now() + 180000;
        }
        artBatch = batch;
        if (!batch.length) return;
        var id = source.request("explore-art", "resolveCatalogueArt", "editorial", {items: batch});
        if (id >= 0) pendingArt = {id: id, source: source, generation: profileGeneration};
        else scheduleArt();
    }
    function acceptArt(id, data, error) {
        var request = pendingArt;
        if (!request || request.id !== id) return;
        pendingArt = null;
        if (!active || !profileReady || request.source !== source || request.generation !== profileGeneration) return;
        try { data = JSON.parse(JSON.stringify(data)); } catch (conversionError) { data = null; }
        if (!error && data && data.version === 1 && data.covers && Array.isArray(data.settled) && data.settled.length) {
            var settled = new Set(data.settled);
            var next = Object.assign({}, artResults);
            for (var item of artBatch) {
                if (!settled.has(item.id)) continue;
                var cover = data.covers[item.id];
                var album = data.albums && data.albums[item.id];
                next[item.id] = {cover: typeof cover === "string" ? cover : "",
                    album: typeof album === "string" ? album : ""};
            }
            artResults = next;
        }
        scheduleArt();
    }
    function withArt(item) {
        var art = artResults[item.id];
        if (!art || (!art.cover && !art.album)) return item;
        var card = Object.assign({}, item, {cover: item.cover || art.cover});
        if (art.album) card.hints = Object.assign({}, item.hints, {album: art.album});
        return card;
    }
    Timer { id: artPoll; interval: 1200; onTriggered: page.requestArt() }
    Connections {
        target: page.source
        ignoreUnknownSignals: true
        function onCompleted(lane, id, data, error) {
            if (lane === "explore-art") page.acceptArt(id, data, error);
        }
    }

    Theme { id: theme }
    RowIndexSidebar {
        id: rowIndex
        z: 2
        pageFlick: page.pageFlick
        flowHost: page
        backdrop: page.backdrop
        worldName: "Vinyl"
        automationPrefix: "vinylExploreRowIndex"
        rows: {
            page.shelfRevision;
            return Array.from({length: shelves.count}, function(_, i) {
                var shelf = shelves.itemAt(i);
                return shelf ? {key: shelf.modelData.key, title: shelf.modelData.title, target: shelf} : null;
            });
        }
        onRowRequested: target => page.rowRequested(target)
    }
    Column {
        id: shelfColumn
        x: page.contentLeft
        width: Math.max(0, page.width - x)
        spacing: 30
        Repeater {
            id: shelves
            onItemAdded: page.shelfRevision++
            onItemRemoved: page.shelfRevision++
            model: [
                {key:"week", title:qsTr("This week worldwide"), operation:"chartWeek", values:["songs","albums","artists"]},
                {key:"hot100-2025", title:qsTr("Every #1 of 2025"), operation:"wikipediaList", values:["hot100-2025"]},
                {key:"global-200-2025", title:qsTr("Global #1s"), operation:"wikipediaList", values:["global-200-2025"]},
                {key:"year-end-100-2025", title:qsTr("The year-end 100"), operation:"wikipediaList", values:["year-end-100-2025"]},
                {key:"mainstream-rock-2020s", title:qsTr("Rock's #1s this decade"), operation:"wikipediaList", values:["mainstream-rock-2020s"]},
                {key:"hot100-1990s", title:qsTr("#1 hits of the '90s"), operation:"wikipediaList", values:["hot100-1990s"]},
                {key:"most-streamed", title:qsTr("Most streamed ever"), operation:"mostStreamed", values:["songs"]}
            ]
            delegate: VinylShelf {
                id: shelf
                required property var modelData
                objectName: "vinylExploreRow_" + modelData.key
                width: shelfColumn.width
                height: implicitHeight
                worldData: page.worldData
                pageFlick: page.pageFlick
                backdrop: page.backdrop
                cardSize: 132
                openRowHandler: function(row) { page.listRequested(row); }
                retryRowHandler: function() { shelf.load(true); }
                property var requests: ({})
                property var replies: ({})
                readonly property string operation: modelData.operation
                readonly property var values: modelData.values
                readonly property bool loading: values.some(value => !replies[value] || !!requests[value])
                readonly property bool failed: values.some(value => replies[value] && replies[value].failed)
                readonly property string staleDate: values.map(value => (replies[value] || {}).staleDate || "")
                    .filter(date => date).sort()[0] || ""
                readonly property var items: {
                    var groups = values.map(value => (replies[value] || {}).items || []);
                    // Keep each kind's order, but put all kinds in the weekly shelf's preview.
                    var result = [];
                    for (var i = 0, last = Math.max(...groups.map(group => group.length)); i < last; ++i)
                        for (var group of groups) if (i < group.length) result.push(page.withArt(group[i]));
                    return result;
                }
                row: ({key: "explore-" + modelData.key, title: modelData.title, items: items,
                    personal: false, status: loading ? "loading" : failed ? "error" : "ready",
                    canRetry: failed && !loading})
                statusNote: !hasCards ? "" : loading ? qsTr("Loading this shelf…")
                    : failed ? qsTr("Some charts could not load.")
                    : staleDate ? qsTr("As of %1 (cached)").arg(staleDate) : ""

                function lane(value) {
                    return row.key + (operation === "chartWeek" ? "-" + value : "");
                }
                function load(retry) {
                    if (!page.active || !page.profileReady) return;
                    var next = Object.assign({}, requests);
                    for (var value of values) {
                        if (next[value] || (replies[value] && !(retry && replies[value].failed))) continue;
                        var id = page.source.request(lane(value), operation, value, {});
                        if (id < 0) setReply(value, {failed: true});
                        else next[value] = {id: id, lane: lane(value), source: page.source, generation: page.profileGeneration};
                    }
                    requests = next;
                }
                function reset() {
                    var pending = requests;
                    requests = ({});
                    replies = ({});
                    for (var value in pending) {
                        var request = pending[value];
                        if (request.source) request.source.cancel(request.lane);
                    }
                }
                function setReply(value, reply) {
                    var next = Object.assign({}, replies);
                    next[value] = reply;
                    replies = next;
                }
                function accept(responseLane, id, data, error) {
                    var value = values.find(value => lane(value) === responseLane);
                    var pending = requests[value];
                    if (!pending || pending.id !== id || pending.source !== page.source
                            || pending.generation !== page.profileGeneration) return;
                    var next = Object.assign({}, requests);
                    delete next[value];
                    requests = next;
                    var kind = operation === "chartWeek" ? value : "songs";
                    var expected = operation === "chartWeek" ? "chart-week"
                        : operation === "wikipediaList" ? "wikipedia" : "kworb";
                    if (error || !data || data.version !== 1 || data.kind !== kind || data.source !== expected
                            || (expected === "wikipedia" && data.list !== value)
                            || (expected === "chart-week" && data.scope !== "global")) {
                        setReply(value, {failed: true});
                        return;
                    }
                    var cards = Array.from(data.items || []).filter(item => item && item.catalogueCard === true
                        && item.kind === kind && item.id && item.title && (expected === "chart-week"
                            ? ["kworb", "youtube-music"].includes(item.source) : item.source === expected));
                    setReply(value, {items: cards, failed: !cards.length,
                        staleDate: data.stale ? String(data.fetchedAt || "").slice(0, 10) : ""});
                    if (cards.length) page.scheduleArt();
                }
                Component.onDestruction: reset()
                Connections {
                    target: page.source
                    ignoreUnknownSignals: true
                    function onCompleted(lane, id, data, error) { shelf.accept(lane, id, data, error); }
                }
            }
        }
        Text {
            objectName: "vinylExploreAttribution"
            width: parent.width
            text: qsTr("Lists from Wikipedia")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
        }
    }
}
