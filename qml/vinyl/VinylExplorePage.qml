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
    property bool warmUp: false
    property real warmTopInset: 0
    readonly property var session: worldData.session || null
    readonly property var source: session ? session.source : null
    readonly property int profileGeneration: session ? session.profileGeneration || 0 : 0
    readonly property bool profileReady: !!source && session.profileActive !== false && !session.shuttingDown
    readonly property int rowCount: shelves.count
    property int shelfRevision: 0
    property var artResults: ({})
    property bool entered: false
    property bool visitStarted: false
    property bool warmAttempted: false
    property bool warming: false
    readonly property bool working: profileReady && (active || (warming && warmUp))
    property var firstRows: []
    property bool firstScreenRevealed: false
    readonly property bool firstListsReady: firstRows.length > 0 && firstRows.every(shelf => shelf.reply && !shelf.pending)
    readonly property bool firstArtReady: !firstScreenRevealed && firstListsReady
        && firstRows.every(shelf => shelf.previewArtSettled && shelf.cardArtworkSettled)
    readonly property real contentLeft: rowIndex.contentLeft
    implicitHeight: shelfColumn.implicitHeight
    signal rowRequested(var target)
    signal listRequested(var row)

    function forceLayout() { shelfColumn.forceLayout(); }
    function viewportRowTop() {
        return warming ? warmTopInset : page.mapToItem(pageFlick, 0, 0).y;
    }
    function loadRows(initial) {
        if (!working || pageFlick.height <= 0 || !shelves.count) return false;
        forceLayout();
        var height = pageFlick.height;
        var origin = viewportRowTop();
        var rows = [];
        var below = null;
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (!shelf) continue;
            var top = origin + shelf.y;
            if (initial) {
                if (top < height && top + shelf.height > 0) rows.push(shelf);
                else if (top >= height && !below) below = shelf;
            } else if (top < 2 * height && top + shelf.height > -height) rows.push(shelf);
        }
        if (initial && (!entered || !firstScreenRevealed)) {
            if (rows.length !== firstRows.length || rows.some((row, i) => row !== firstRows[i]))
                firstScreenRevealed = false;
            firstRows = rows;
        }
        for (var row of rows) row.load(false);
        if (initial && below) below.load(false);
        return true;
    }
    function syncLoading() {
        if (!profileReady) return;
        // Opening Explore adopts any warm-up in flight; the same page owns both modes.
        if (active && !visitStarted) {
            warming = false;
            visitStarted = loadRows(true);
            entered = entered || visitStarted;
            scheduleArt(true);
        } else if (!active && !warmUp && warming) {
            warming = false;
            stopRequests();
        } else if (!active && warmUp && !entered && !warmAttempted && pageFlick.height > 0) {
            warming = true;
            warmAttempted = loadRows(true);
        }
        maybeReveal();
    }
    function stopRequests() {
        streamTimer.stop();
        stopArt();
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (shelf) shelf.cancelList();
        }
    }
    function resetRows(newProfile) {
        if (!shelves) return;
        warming = false;
        visitStarted = false;
        if (newProfile) {
            entered = false;
            warmAttempted = false;
        }
        firstRows = [];
        firstScreenRevealed = false;
        stopRequests();
        artResults = ({});
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (shelf) shelf.reply = null;
        }
        Qt.callLater(page.syncLoading);
    }
    onActiveChanged: {
        if (!active && visitStarted) {
            visitStarted = false;
            stopRequests();
        }
        Qt.callLater(page.syncLoading);
    }
    onWarmUpChanged: Qt.callLater(page.syncLoading)
    onSourceChanged: resetRows(true)
    onProfileGenerationChanged: resetRows(true)
    onProfileReadyChanged: {
        if (!profileReady) resetRows(false);
        else Qt.callLater(page.syncLoading);
    }
    Component.onCompleted: Qt.callLater(page.syncLoading)
    Component.onDestruction: stopArt()

    function maybeReveal() {
        if (working && firstArtReady) firstScreenRevealed = true;
    }
    onFirstArtReadyChanged: Qt.callLater(page.maybeReveal)
    Timer {
        id: revealTimer
        interval: 1500
        running: page.working && page.firstListsReady && !page.firstScreenRevealed
        onTriggered: page.firstScreenRevealed = true
    }
    Timer {
        id: streamTimer
        interval: 80
        onTriggered: if (page.active) page.loadRows(false)
    }
    Connections {
        target: page.active ? page.pageFlick : null
        function onContentYChanged() {
            if (page.visitStarted && !streamTimer.running) streamTimer.start();
        }
        function onHeightChanged() {
            if (page.visitStarted && !streamTimer.running) streamTimer.start();
            else if (!page.visitStarted) Qt.callLater(page.syncLoading);
        }
    }

    function stopArt() {
        if (lastfmArt) lastfmArt.clear();
        if (editorialArt) editorialArt.clear();
    }
    function scheduleArt(immediate) {
        lastfmArt.schedule(immediate);
        editorialArt.schedule(immediate);
    }
    function artSource(item) {
        if (item.source === "lastfm") return "lastfm";
        if (item.source === "kworb" || item.source === "wikipedia") return "editorial";
        return "";
    }
    function nextArtBatch(kind) {
        // Re-rank each new batch by distance from the viewport. Keep row order for ties.
        var ranked = [];
        var origin = viewportRowTop();
        for (var i = 0; i < shelves.count; ++i) {
            var shelf = shelves.itemAt(i);
            if (!shelf || !shelf.items.length) continue;
            var top = origin + shelf.y;
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
        if (!working || walk.pending) return;
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
        if (!working || request.source !== source || request.generation !== profileGeneration) return;
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
            if (walk.kind === "lastfm") {
                walk.schedule(true);
                return;
            }
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
        onTriggered: page.requestArt(walk)
        function schedule(immediate) {
            if (!page.working || pending || running) return;
            interval = immediate ? 1 : 1200;
            start();
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
            else if (page.working && lane.startsWith("explore-")) {
                for (var i = 0; i < shelves.count; ++i) {
                    var shelf = shelves.itemAt(i);
                    if (shelf && shelf.lane === lane) {
                        shelf.accept(id, data, error);
                        break;
                    }
                }
            }
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
                if (!shelf) return null;
                const row = shelf.modelData;
                return {key: row.key, title: row.title, target: shelf,
                    iconPaths: row.iconPaths, iconText: row.iconText};
            });
        }
        onRowRequested: target => {
            if (page.active) target.load(false);
            page.rowRequested(target);
        }
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
                {key:"chill", title:qsTr("Chill"), tag:"chill",
                    iconPaths:'<path d="M20.8 14.4A9 9 0 0 1 9.6 3.2 9 9 0 1 0 20.8 14.4Z"/>'},
                {key:"feel-good", title:qsTr("Feel good"), tag:"happy",
                    iconPaths:'<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.4 1.4m11.2 11.2L19 19M5 19l1.4-1.4M17.6 6.4 19 5"/>'},
                {key:"sad", title:qsTr("Sad songs"), tag:"sad",
                    iconPaths:'<path d="M6 14a4 4 0 1 1 .8-7.9 5.5 5.5 0 0 1 10.4 1.4A3.3 3.3 0 1 1 18 14H6Zm1 3-2 4m7-4-2 4m7-4-2 4"/>'},
                {key:"love", title:qsTr("Love songs"), tag:"romantic",
                    iconPaths:'<path d="M12 20 4.4 12.5a4.6 4.6 0 0 1 6.5-6.5L12 7.1l1.1-1.1a4.6 4.6 0 0 1 6.5 6.5Z"/>'},
                {key:"party", title:qsTr("Party"), tag:"party",
                    iconPaths:'<path d="m9 3 1.6 4.4L15 9l-4.4 1.6L9 15l-1.6-4.4L3 9l4.4-1.6L9 3Zm8 10 1.1 2.9L21 17l-2.9 1.1L17 21l-1.1-2.9L13 17l2.9-1.1L17 13ZM19 3v5m-2.5-2.5h5"/>'},
                {key:"year-end-100-2025", title:qsTr("Biggest songs of 2025"), operation:"wikipediaList", value:"year-end-100-2025",
                    iconPaths:'<path d="M7 3h10v7a5 5 0 0 1-10 0V3Zm0 2H3v2a4 4 0 0 0 4 4m10-6h4v2a4 4 0 0 1-4 4m-5 4v6m-4 0h8"/>'},
                {key:"2010s", title:qsTr("The 2010s"), tag:"2010s", iconText:"10s"},
                {key:"2000s", title:qsTr("The 2000s"), tag:"00s", iconText:"00s"},
                {key:"90s", title:qsTr("The '90s"), tag:"90s", iconText:"90s"},
                {key:"80s", title:qsTr("The '80s"), tag:"80s", iconText:"80s"},
                {key:"70s", title:qsTr("The '70s"), tag:"70s", iconText:"70s"},
                {key:"most-streamed", title:qsTr("Most streamed ever"), operation:"mostStreamed", value:"songs",
                    iconPaths:'<path d="M4 20v-5m5 5v-8m5 8V9m5 11V5M3 11l6-5 5-1 5-3"/>'}
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
                reservedCardHeight: cardSize + 55
                cardsRevealed: !firstScreen || page.firstScreenRevealed
                trackCardArtwork: firstScreen && !page.firstScreenRevealed
                showCardPlaceholder: page.active && (!!pending || !!reply) && (loading || !cardsRevealed) && !failed
                openRowHandler: function(row) { page.listRequested(row); }
                retryRowHandler: function() { shelf.load(true); }
                property var pending: null
                property var reply: null
                readonly property string tag: modelData.tag || ""
                readonly property string lane: tag ? "explore-tag-" + tag : "explore-" + modelData.key
                readonly property string operation: tag ? "browseCatalogue" : modelData.operation
                readonly property string value: tag ? "songs" : modelData.value
                readonly property bool loading: !reply || !!pending
                readonly property bool firstScreen: page.firstRows.includes(shelf)
                readonly property bool previewArtSettled: items.slice(0, previewCount).every(item =>
                    item.cover || !page.artSource(item) || page.artResults[item.id])
                readonly property bool failed: !!reply && reply.failed
                readonly property string staleDate: (reply && reply.staleDate) || ""
                readonly property var items: ((reply && reply.items) || []).map(item => page.withArt(item))
                row: ({key: "explore-" + modelData.key, title: modelData.title, items: items,
                    personal: false, status: failed ? "error" : loading || !cardsRevealed ? "loading" : "ready",
                    canRetry: failed && !loading})
                statusNote: !hasCards ? "" : loading || !cardsRevealed ? qsTr("Loading this shelf…")
                    : failed ? qsTr("Some charts could not load.")
                    : staleDate ? qsTr("As of %1 (cached)").arg(staleDate) : ""

                function load(retry) {
                    if (!page.working || pending || (reply && !(retry && failed))) return;
                    if (retry) reply = null;
                    var payload = tag ? {catalogueSource: "lastfm", list: tag, cursor: {}, pageSize: 50} : {};
                    var id = page.source.request(lane, operation, value, payload);
                    if (id < 0) reply = {failed: true};
                    else pending = {id: id, source: page.source, generation: page.profileGeneration};
                }
                function cancelList() {
                    var request = pending;
                    pending = null;
                    if (request && request.source) request.source.cancel(lane);
                }
                function accept(id, data, error) {
                    if (!pending || pending.id !== id || pending.source !== page.source
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
                    if (cards.length) page.scheduleArt(true);
                }
                Component.onDestruction: cancelList()
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
