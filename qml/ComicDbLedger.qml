// ComicDbLedger — the DB-centered series view (step 3 of the comics-brain wiring).
//
// Renders a series straight from the weekly comics_db.json (via ComicsDb.js): a hero with
// creators + synopsis, then collected editions GROUPED BY FORMAT (Compendium / Omnibus /
// Complete Library / …), each edition a catalogue record — collected-issues, page count, year —
// with a download-state symbol (download → % → read). No live LOCG/GetComics resolution; the app
// just reads the file. Shown by ComicSeriesPage when ComicsDb has the series; otherwise the old
// live flow runs. Design ratified with Hemanth via HTML mock 2026-07-13.
//
// Download reuses the SAME engine as the live rows: the global `Comics` bridge (downloadIssue /
// statusOf / progress signals) + Resolve.failureIsTerminal. Reading is emitted up via readRequested.
import QtQuick
import QtQuick.Controls
import "ComicResolve.js" as Resolve

Item {
    id: ledger

    property var    theme                       // injected from ComicSeriesPage
    property var    dbSeries: null              // ComicsDb series record { title, publisher, cover, editions[] }
    property string seriesTitle: ""
    property string gcTag: ""                    // download namespace: "gc:<tag>"
    property real   contentWidth: width
    property var libraryEntry: null   // Collection snapshot passed by the host page (+ Library / download-collect)

    signal readRequested(string chId, string label)
    // A user opened the alternate-sources picker for an idle, undownloaded edition.
    signal alternateSourcesRequested(var edition, string chId)

    implicitHeight: col.implicitHeight
    height: col.implicitHeight        // a Column child sizes by height, not implicitHeight

    // ---- group editions by format, in a sensible collection-type order ----
    readonly property var formatGroups: computeGroups(dbSeries && dbSeries.editions ? dbSeries.editions : [])
    function computeGroups(eds) {
        var order = ["Compendium", "Omnibus", "Complete Library", "Ultimate Collection",
                     "Epic Collection", "Absolute", "Deluxe", "Hardcover", "Trade Paperback",
                     "Graphic Novel", "Other"]
        var byFmt = {}
        for (var i = 0; i < eds.length; i++) {
            var f = eds[i].format || "Other"
            if (!byFmt[f]) byFmt[f] = []
            byFmt[f].push(eds[i])
        }
        var out = []
        for (var j = 0; j < order.length; j++)
            if (byFmt[order[j]]) { out.push({ format: order[j], editions: byFmt[order[j]] }); delete byFmt[order[j]] }
        // any format not in the known order (e.g. "Collected Edition") — append it, never drop editions
        for (var k in byFmt)
            out.push({ format: k, editions: byFmt[k] })
        return out
    }
    // series-level hints (the DB is per-edition): synopsis from the first enriched edition,
    // byline = every distinct creator across editions (a run spans authors — Batman is King + Tynion)
    readonly property string heroCreators: distinctCreators()
    readonly property var seriesCoverage: dbSeries && dbSeries.coverage ? dbSeries.coverage : ({})
    function coverageLabel() {
        var state = String(seriesCoverage.availabilityState || "")
        if (state === "complete") return "Complete run"
        if (state === "near_complete") return "Near complete"
        if (state === "partial") return "Partial run"
        if (state === "collected_only") return "Collected editions available"
        if (state === "bibliography_only") return "Bibliography only"
        if (state === "unknown") return "Coverage unknown"
        return ""
    }
    // Series-level synopsis (Wikipedia lead via the Wikidata P3589 join) wins;
    // the first edition's PRH back-cover copy remains the fallback.
    readonly property string heroSynopsis: (dbSeries && dbSeries.synopsis)
                                           ? String(dbSeries.synopsis)
                                           : firstField("description")
    function firstField(k) {
        var eds = dbSeries && dbSeries.editions ? dbSeries.editions : []
        for (var i = 0; i < eds.length; i++) if (eds[i][k]) return eds[i][k]
        return ""
    }
    function distinctCreators() {
        var eds = dbSeries && dbSeries.editions ? dbSeries.editions : []
        var seen = ({}), out = []
        for (var i = 0; i < eds.length; i++) {
            var c = eds[i].creators
            if (c && !seen[c]) { seen[c] = true; out.push(c) }
        }
        return out.slice(0, 3).join(", ")
    }
    // the edition title is the full, honest record ("Batman Vol. 1: I Am Gotham"); on a series
    // page the leading series name is redundant, so trim it for the row (keeping any divergent
    // part, e.g. "Batman: Detective Comics …" → "Detective Comics …"). Hemanth's call 2026-07-13.
    function displayTitle(t) {
        var s = String(t || "")
        var pre = String(ledger.seriesTitle || "")
        if (pre.length && s.toLowerCase().indexOf(pre.toLowerCase()) === 0) {
            var rest = s.slice(pre.length).replace(/^[\s:,./–—\-]+/, "")   // strip dangling "/" (Batman / The Flash) and "," (TWD, Book One)
            if (rest.length >= 2) return rest
        }
        return s
    }
    function specLine(ed) {
        var bits = []
        if (ed.creators)  bits.push(ed.creators)
        if (ed.pages)     bits.push(ed.pages + " Pages")
        if (ed.published) {                              // GCD dates look like "[October] 2017"
            var y = String(ed.published).match(/\d{4}/)  // → pull the 4-digit year, not slice(0,4)
            if (y) bits.push(y[0])
        }
        return bits
    }
    Column {
        id: col
        width: ledger.contentWidth
        spacing: 0

        // ================= HERO =================
        Row {
            width: parent.width
            spacing: 26
            bottomPadding: 26

            Rectangle {                                   // series cover
                width: 158; height: 236; radius: 9
                color: "#15171f"; border.width: 1; border.color: theme.edge; clip: true
                Image {
                    anchors.fill: parent; anchors.margins: 1
                    source: dbSeries && dbSeries.cover ? dbSeries.cover : ""
                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                    sourceSize.width: 320
                }
            }
            Column {
                width: parent.width - 158 - 26
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Text { text: "Comics"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                    font.letterSpacing: 3.4; font.capitalization: Font.AllUppercase }
                Text { width: parent.width; text: seriesTitle
                    color: theme.ink; font.family: theme.display; font.pixelSize: 50; font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Text { visible: heroCreators.length > 0; width: parent.width; text: "by " + heroCreators
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                    elide: Text.ElideRight }
                Text {
                    text: {
                        var b = []
                        if (dbSeries && dbSeries.rank) b.push("#" + dbSeries.rank + " Most-Read")
                        if (dbSeries && dbSeries.publisher) b.push(dbSeries.publisher)
                        var n = dbSeries && dbSeries.editions ? dbSeries.editions.length : 0
                        b.push(n + " collected edition" + (n === 1 ? "" : "s"))
                        if (coverageLabel().length) b.push(coverageLabel())
                        return b.join("   ·   ")
                    }
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14
                }
                Text { visible: heroSynopsis.length > 0; width: Math.min(parent.width, 620)
                    text: heroSynopsis; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                    wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                    lineHeight: 1.35; topPadding: 5 }
                LibraryButton {
                    world: "tankoban"
                    visible: !!ledger.libraryEntry
                    entry: ledger.libraryEntry
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: theme.edge }

        // ================= FORMAT GROUPS =================
        Repeater {
            model: ledger.formatGroups
            delegate: Column {
                required property var modelData
                width: col.width
                topPadding: 26
                spacing: 4

                Row {                                     // section header
                    width: parent.width; spacing: 14
                    Text { text: modelData.format; color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                        font.letterSpacing: 2.8; font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: modelData.editions.length; color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle { width: parent.width - x; height: 1; color: Qt.rgba(0.94,0.77,0.29,0.34)
                        anchors.verticalCenter: parent.verticalCenter }
                }

                // ---- edition rows ----
                Repeater {
                    model: modelData.editions
                    delegate: Item {
                        id: ed
                        required property var modelData
                        width: col.width
                        height: 192 + sourceRail.implicitHeight

                        property string chId: String(ed.modelData.locg_comic_id || ed.modelData.slug || "")
                        property var sourceRows: ed.modelData.sources || []
                        readonly property var officialSources: sourceRows.filter(function(s) {
                            return !s.fanMade && s.available !== false
                                   && (s.confidenceClass === "exact" || s.confidenceClass === "strong")
                        })
                        readonly property var communitySources: sourceRows.filter(function(s) { return !!s.fanMade })
                        readonly property var primarySource: officialSources.length ? officialSources[0] : null
                        property string postUrl: primarySource ? (primarySource.postUrl || "") : (ed.modelData.getcomics_post || "")
                        property bool   hasSource: officialSources.length > 0 || (!!ed.modelData.available && postUrl.length > 0)
                        readonly property bool canAcquire: chId.length > 0 && (hasSource || dlState === "done")
                        property string dlState: "none"     // none|resolving|queued|downloading|extracting|done|error|dead
                        property real   dlDone: 0
                        property real   dlTotal: 0
                        readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                                      || dlState === "resolving"   || dlState === "extracting"
                                                      || dlState === "choosing"
                        // Every idle, undownloaded edition offers a smaller "Find alternate
                        // sources" action — whether or not a verified GetComics post exists.
                        readonly property bool canAlternate: chId.length > 0 && !inFlight && dlState !== "done"

                        function refreshDl() {
                            if (typeof Comics === "undefined" || !chId.length) return
                            var st = Comics.statusOf(chId)
                            ed.dlState = st.state; ed.dlDone = st.done; ed.dlTotal = st.total
                        }
                        function primary() {
                            if (typeof Comics === "undefined" || !chId.length || !canAcquire) return
                            if (dlState === "dead") return
                            if (dlState === "done") { ledger.readRequested(chId, String(ed.modelData.display_title || ed.modelData.title || "")); return }
                            if (inFlight) return
                            ed.dlState = "queued"
                            var editionTitle = String(ed.modelData.display_title || ed.modelData.title || "")
                            Comics.downloadIssue(chId, postUrl, ledger.gcTag, ledger.seriesTitle,
                                                 editionTitle, 0)
                            if (ledger.libraryEntry) Collection.add("tankoban", ledger.libraryEntry)
                        }
                        Component.onCompleted: refreshDl()
                        Connections {
                            target: typeof Comics !== "undefined" ? Comics : null
                            function onProgress(cid, done, total) {
                                if (cid !== ed.chId) return
                                ed.dlState = "downloading"; ed.dlDone = done; ed.dlTotal = total
                            }
                            function onFinished(cid) { if (cid === ed.chId) ed.dlState = "done" }
                            function onFailed(cid, reason) {
                                if (cid === ed.chId) ed.dlState = Resolve.failureIsTerminal(reason) ? "dead" : "error"
                            }
                            function onRemoved(cid) { if (cid === ed.chId) ed.dlState = "none" }
                        }

                        Rectangle { anchors.fill: parent; color: ed.canAcquire && edMa.containsMouse ? Qt.rgba(1,1,1,0.045) : "transparent"; radius: 8 }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.055) }

                        Rectangle {                          // LARGE edition cover
                            id: edCover
                            anchors.left: parent.left; anchors.leftMargin: 4
                            anchors.top: parent.top; anchors.topMargin: 18
                            width: 104; height: 156; radius: 7
                            color: "#15171f"
                            border.width: 1
                            border.color: ed.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                            clip: true
                            Image { anchors.fill: parent; anchors.margins: 1
                                // cover ladder (Hemanth 2026-07-17): baked cover first, else PRH's
                                // keyless ISBN cover CDN (images.penguinrandomhouse.com/cover/<isbn>,
                                // IPv4-only — no dead-IPv6 stall) carries art for most ISBN'd
                                // editions the pipeline missed. Error -> next rung -> quiet card.
                                property int rung: 0
                                readonly property var ladder: {
                                    var l = []
                                    if (ed.modelData.cover) l.push(ed.modelData.cover)
                                    var isbn = String(ed.modelData.isbn || "").replace(/-/g, "")
                                    if (isbn.length) l.push("https://images.penguinrandomhouse.com/cover/" + isbn)
                                    return l
                                }
                                source: rung < ladder.length ? ladder[rung] : ""
                                onStatusChanged: if (status === Image.Error && rung < ladder.length) rung += 1
                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                sourceSize.width: 220 }
                        }

                        Column {                             // title · spec · blurb
                            anchors.left: edCover.right; anchors.leftMargin: 20
                            anchors.right: ed.canAlternate ? altBtn.left : edState.left; anchors.rightMargin: 16
                            anchors.verticalCenter: edCover.verticalCenter
                            spacing: 7
                            Text { width: parent.width; text: ledger.displayTitle(ed.modelData.display_title || ed.modelData.title)
                                color: edMa.containsMouse && ed.canAcquire ? theme.gold : theme.ink
                                opacity: 1
                                font.family: theme.display; font.pixelSize: 21; font.weight: Font.Medium
                                elide: Text.ElideRight }
                            // "Collects #1-6" lines REMOVED (Hemanth 2026-07-17, reversing his
                            // 2026-07-13 headline-datum call): GCD titles collected editions like
                            // single issues, so "…#1 · Collects #1-6" read as confusion, not data.
                            Row {
                                spacing: 13; visible: specRep.count > 0
                                Repeater { id: specRep; model: ledger.specLine(ed.modelData)
                                    delegate: Row { spacing: 13
                                        required property var modelData
                                        required property int index
                                        Text { visible: index > 0; text: "·"; color: theme.inkDimmer; opacity: 0.5
                                            font.family: theme.ui; font.pixelSize: 14 }
                                        Text { text: modelData; color: index === 0 ? theme.inkDim : theme.inkDimmer
                                            font.family: theme.ui; font.pixelSize: 14 }
                                    }
                                }
                            }
                            Text { width: parent.width; visible: !!ed.modelData.description
                                text: ed.modelData.description || ""
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; lineHeight: 1.3 }
                        }

                        Column {
                            id: sourceRail
                            anchors.left: edCover.right; anchors.leftMargin: 20
                            anchors.right: parent.right; anchors.rightMargin: 8
                            anchors.top: edCover.bottom; anchors.topMargin: 10
                            spacing: 4
                            visible: ed.officialSources.length > 0 || ed.communitySources.length > 0

                            Text {
                                visible: ed.officialSources.length > 0
                                text: ed.officialSources.length + " verified source" + (ed.officialSources.length === 1 ? "" : "s")
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                            }
                            Repeater {
                                model: ed.officialSources
                                delegate: Row {
                                    required property var modelData
                                    width: sourceRail.width; height: 22; spacing: 10
                                    Text { text: modelData.format || modelData.kind || "GetComics"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11 }
                                    Text { text: modelData.date || ""; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11 }
                                    Text { width: parent.width - x; text: modelData.title || ""; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideRight }
                                }
                            }
                            Text {
                                visible: ed.communitySources.length > 0
                                text: "Community Collections · " + ed.communitySources.length
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; topPadding: 3
                            }
                            Repeater {
                                model: ed.communitySources
                                delegate: Text {
                                    required property var modelData
                                    width: sourceRail.width
                                    text: (modelData.format || modelData.kind || "Community") + " · " + (modelData.title || "")
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideRight
                                }
                            }
                        }

                        // download-state symbol: read (done) · % (in flight) · GetComics download.
                        // Unavailable editions stay visible as bibliography with no action glyph.
                        Item {
                            id: edState
                            anchors.right: parent.right; anchors.rightMargin: 8
                            anchors.verticalCenter: edCover.verticalCenter
                            width: 40; height: 40
                            Image {                          // read (downloaded)
                                anchors.centerIn: parent; visible: ed.dlState === "done"
                                source: "../assets/icons/books.svg"; width: 24; height: 24 }
                            Text {                           // download progress %
                                anchors.centerIn: parent; visible: ed.inFlight
                                text: ed.dlTotal > 0 ? Math.round(ed.dlDone / ed.dlTotal * 100) + "%" : "…"
                                color: theme.gold; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Image {                          // verified GetComics download
                                anchors.centerIn: parent
                                visible: ed.hasSource && !ed.inFlight && ed.dlState !== "done"
                                source: "../assets/icons/download.svg"; width: 23; height: 23
                                opacity: 0.92 }
                        }
                        MouseArea {
                            id: edMa; anchors.fill: parent; enabled: ed.canAcquire; hoverEnabled: ed.canAcquire
                            cursorShape: (ed.canAcquire || ed.dlState === "done") ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: ed.primary()
                        }

                        // ── Find alternate sources: a smaller circular action with its OWN
                        //    MouseArea, declared AFTER edMa so it stacks above it — clicking
                        //    here opens the torrent picker and NEVER the GetComics download. ──
                        Rectangle {
                            id: altBtn
                            visible: ed.canAlternate
                            anchors.right: edState.left; anchors.rightMargin: 12
                            anchors.verticalCenter: edCover.verticalCenter
                            width: 38; height: 38; radius: 19
                            color: altMa.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05)
                            border.width: 1
                            border.color: altMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.45) : theme.edge
                            Behavior on border.color { ColorAnimation { duration: 140 } }
                            Image {
                                anchors.centerIn: parent
                                source: "../assets/icons/search.svg"; width: 17; height: 17
                                opacity: altMa.containsMouse ? 1.0 : 0.82
                            }
                            MouseArea {
                                id: altMa; anchors.fill: parent; enabled: ed.canAlternate
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: ledger.alternateSourcesRequested(ed.modelData, ed.chId)
                                ToolTip.visible: containsMouse
                                ToolTip.text: "Find alternate sources"
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: "Find alternate sources"
                        }
                    }
                }
            }
        }

    }
}
