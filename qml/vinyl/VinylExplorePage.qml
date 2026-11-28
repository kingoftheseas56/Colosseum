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
        if (lastfmArt) lastfmArt.clear();
        if (editorialArt) editorialArt.clear();
    }
    function scheduleArt() {
        lastfmArt.schedule();
        editorialArt.schedule();
    }
    function artSource(item) {
        if (item.source === "lastfm") return "lastfm";
        if (item.source === "kworb" || item.source === "wikipedia") return "editorial";
        return "";
    }
    function nextArtBatch(kind) {
        // Re-rank each new batch by distance from the viewport. Keep row order for ties.
        var ranked = [];
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (!shelf || !shelf.items.length) continue;
            var top = shelf.mapToItem(pageFlick, 0, 0).y;
            var distance = Math.max(0, -top - shelf.height, top - pageFlick.height);
            ranked.push({shelf: shelf, distance: distance, index: i});
        }
        ranked.sort((a, b) => a.distance - b.distance || a.index - b.index);
        var batch = [];
        var seen = new Set();
        for (var row of ranked) {
            for (var item of row.shelf.items.slice(0, row.shelf.previewCount)) {
                if (artSource(item) !== kind || item.cover || artResults[item.id] || seen.has(item.id)) continue;
                batch.push(item);
                seen.add(item.id);
                if (batch.length === 12) return batch;
            }
        }
        return batch;
    }
    function requestArt(walk) {
        if (!active || !profileReady || walk.pending) return;
        var batch = walk.batch.filter(card => !artResults[card.id]);
        if (batch.length && Date.now() >= walk.deadline) {
            var timedOut = Object.assign({}, artResults);
            for (var card of batch) timedOut[card.id] = {};
            artResults = timedOut;
            batch = [];
        }
        if (!batch.length) {
            batch = nextArtBatch(walk.kind);
            walk.deadline = Date.now() + 180000;
        }
        walk.batch = batch;
        if (!batch.length) return;
        var id = source.request(walk.lane, "resolveCatalogueArt", walk.kind, {items: batch});
        if (id >= 0) walk.pending = {id: id, source: source, generation: profileGeneration};
        else walk.schedule();
    }
    function acceptArt(walk, id, data, error) {
        var request = walk.pending;
        if (!request || request.id !== id) return;
        walk.pending = null;
        if (!active || !profileReady || request.source !== source || request.generation !== profileGeneration) return;
        try { data = JSON.parse(JSON.stringify(data)); } catch (conversionError) { data = null; }
        // Last.fm resolves the batch inline; only editorial polls return settled IDs.
        if (!error && data && data.version === 1 && data.covers) {
            var ids = walk.kind === "lastfm" ? walk.batch.map(item => item.id) : data.settled;
            if (!Array.isArray(ids) || !ids.length) {
                walk.schedule();
                return;
            }
            var settled = new Set(ids);
            var next = Object.assign({}, artResults);
            for (var item of walk.batch) {
                if (!settled.has(item.id)) continue;
                var cover = data.covers[item.id];
                var album = data.albums && data.albums[item.id];
                next[item.id] = {cover: typeof cover === "string" ? cover : "",
                    album: typeof album === "string" ? album : ""};
            }
            artResults = next;
        }
        walk.schedule();
    }
    function withArt(item) {
        var art = artResults[item.id];
        if (!art || (!art.cover && !art.album)) return item;
        var card = Object.assign({}, item, {cover: item.cover || art.cover});
        if (art.album) card.hints = Object.assign({}, item.hints, {album: art.album});
        return card;
    }
    component ArtWalk: Timer {
        id: walk
        required property string kind
        required property string lane
        property var batch: []
        property double deadline: 0
        property var pending: null
        interval: 1200
        onTriggered: page.requestArt(walk)
        function schedule() {
            if (page.active && page.profileReady && !pending && !running) start();
        }
        function clear() {
            stop();
            var request = pending;
            pending = null;
            batch = [];
            if (request && request.source) request.source.cancel(lane);
        }
    }
    ArtWalk { id: lastfmArt; kind: "lastfm"; lane: "explore-art-lastfm" }
    ArtWalk { id: editorialArt; kind: "editorial"; lane: "explore-art" }
    Connections {
        target: page.source
        ignoreUnknownSignals: true
        function onCompleted(lane, id, data, error) {
            if (lane === lastfmArt.lane) page.acceptArt(lastfmArt, id, data, error);
            else if (lane === editorialArt.lane) page.acceptArt(editorialArt, id, data, error);
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
                {key:"chill", title:qsTr("Chill"), tag:"chill"},
                {key:"feel-good", title:qsTr("Feel good"), tag:"happy"},
                {key:"sad", title:qsTr("Sad songs"), tag:"sad"},
                {key:"love", title:qsTr("Love songs"), tag:"romantic"},
                {key:"party", title:qsTr("Party"), tag:"party"},
                {key:"year-end-100-2025", title:qsTr("Biggest songs of 2025"), operation:"wikipediaList", value:"year-end-100-2025"},
                {key:"2010s", title:qsTr("The 2010s"), tag:"2010s"},
                {key:"2000s", title:qsTr("The 2000s"), tag:"00s"},
                {key:"90s", title:qsTr("The '90s"), tag:"90s"},
                {key:"80s", title:qsTr("The '80s"), tag:"80s"},
                {key:"70s", title:qsTr("The '70s"), tag:"70s"},
                {key:"most-streamed", title:qsTr("Most streamed ever"), operation:"mostStreamed", value:"songs"}
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
                property var pending: null
                property var reply: null
                readonly property string tag: modelData.tag || ""
                readonly property string lane: tag ? "explore-tag-" + tag : "explore-" + modelData.key
                readonly property string operation: tag ? "browseCatalogue" : modelData.operation
                readonly property string value: tag ? "songs" : modelData.value
                readonly property bool loading: !reply || !!pending
                readonly property bool failed: !!reply && reply.failed
                readonly property string staleDate: (reply && reply.staleDate) || ""
                readonly property var items: ((reply && reply.items) || []).map(item => page.withArt(item))
                row: ({key: "explore-" + modelData.key, title: modelData.title, items: items,
                    personal: false, status: loading ? "loading" : failed ? "error" : "ready",
                    canRetry: failed && !loading})
                statusNote: !hasCards ? "" : loading ? qsTr("Loading this shelf…")
                    : failed ? qsTr("Some charts could not load.")
                    : staleDate ? qsTr("As of %1 (cached)").arg(staleDate) : ""

                function load(retry) {
                    if (!page.active || !page.profileReady || pending || (reply && !(retry && failed))) return;
                    var payload = tag ? {catalogueSource: "lastfm", list: tag, cursor: {}, pageSize: 50} : {};
                    var id = page.source.request(lane, operation, value, payload);
                    if (id < 0) reply = {failed: true};
                    else pending = {id: id, source: page.source, generation: page.profileGeneration};
                }
                function reset() {
                    var request = pending;
                    pending = null;
                    reply = null;
                    if (request && request.source) request.source.cancel(lane);
                }
                function accept(responseLane, id, data, error) {
                    if (responseLane !== lane || !pending || pending.id !== id || pending.source !== page.source
                            || pending.generation !== page.profileGeneration) return;
                    pending = null;
                    var expected = tag ? "lastfm" : operation === "wikipediaList" ? "wikipedia" : "kworb";
                    if (error || !data || data.version !== 1 || data.kind !== "songs" || data.source !== expected
                            || (tag && data.list !== tag) || (expected === "wikipedia" && data.list !== value)) {
                        reply = {failed: true};
                        return;
                    }
                    var cards = Array.from(data.items || []).filter(item => item && item.catalogueCard === true
                        && item.kind === "songs" && item.id && item.title && item.source === expected);
                    reply = {items: cards, failed: !cards.length,
                        staleDate: data.stale ? String(data.fetchedAt || "").slice(0, 10) : ""};
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
            text: qsTr("Lists from Last.fm, Kworb and Wikipedia")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
        }
    }
}
