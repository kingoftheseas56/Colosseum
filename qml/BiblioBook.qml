// BiblioBook — the book "dust-jacket" detail page. Owner: A2. OUR OWN design (NOT the manga series
// view): the cover as a physical object · the tagline as the hero · a drop-capped synopsis · an
// "Editions" panel. Opens as a layer over the Biblio world (Main.qml bookLayer). `book` is a full
// Apple object from BiblioApi.fullBook.
//
// The Editions rows are a STUB until the libgen "delivery" layer is ported (TB2 had it; Colosseum
// doesn't yet). Metadata + layout are real; the download list is a preview.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi
import "AbbApi.js" as Abb

Item {
    id: detail
    property var book: ({})
    property Item backdrop
    property var editions: []
    property bool edLoading: false
    property string localPath: ""        // a downloaded edition of this book on disk ("" = none yet)
    property var torrents: []            // ranked rows from BookTorrents (native order — no re-sort)
    property bool torLoading: false
    property bool torExpanded: false     // "See more" toggle — collapsed to torCap by default
    property int torCap: 5               // rows shown before "See more"

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal readRequested(string path, var book)   // a downloaded edition is on disk, ready for the reader
    signal listenRequested(string bookKey, var book)   // a downloaded audiobook is ready for the player

    // ── audiobook pairing lane: the same title's audiobook, from AudioBookBay ──
    property var abRows: []                             // ABB search rows for this title
    property bool abLoading: false
    property string abInfoHash: ""                      // resolved infoHash of the picked row
    // the pairing identity — same for this title's ebook and audiobook entities.
    // audiobook-opened pages carry book.pairKey; ebook-opened pages compute it from title/author.
    property string pairKey: (detail.book && detail.book.pairKey) ? detail.book.pairKey
                             : BiblioApi.pairKey(detail.book ? detail.book.title : "",
                                                 detail.book ? detail.book.author : "")
    property bool audioLocal: false                     // recomputed in loadEditions + on Audiobooks.finished

    Theme { id: theme }
    MouseArea { anchors.fill: parent }                 // swallow clicks to the world beneath
    // SOLID page (doctrine: books = page solid, frame OS) — a calm dark reading ground so the busy
    // world page never bleeds through and the long-form text stays legible.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // raised illuminated initial: oversize the first letter inline (QML has no CSS float drop-cap)
    function dropCapHtml(s) {
        var t = String(s || "");
        if (t.length === 0) return "";
        var first = t.charAt(0);
        var rest = t.substring(1);
        return '<span style="font-family:' + theme.display + '; font-size:62px; color:#f7f7f5;">'
             + first + '</span>' + rest;
    }

    // ── editions: live LibGen search for this book (recreates TB2's scraper) ──
    onBookChanged: detail.loadEditions()
    function loadEditions() {
        if (!detail.book || !detail.book.title) return
        detail.edLoading = true
        detail.editions = []
        BiblioApi.searchLibgen(detail.book.title, detail.book.author, function(eds) {
            detail.editions = eds
            detail.edLoading = false
            detail.refreshLocal()
        })
        // audiobook lane: is one already downloaded? then find one to download.
        detail.audioLocal = (typeof Audiobooks !== 'undefined') && Audiobooks.isDownloaded(detail.pairKey)
        detail.abLoading = true; detail.abRows = []
        Abb.resolveAudiobook(detail.book.title, detail.book.author, function(res) {
            detail.abRows = (res && res.rows) ? res.rows : []
            detail.abLoading = false
            // Pre-warm the stream engine on the top match while the user reads: a COLD engine
            // can't fetch a torrent's peers fast (empty DHT), but by the time they click Download
            // it's warm. Best-effort — never blocks the UI. (root-caused 2026-07-12: cold /create
            // hangs 60s+; a warm engine resolves in seconds.)
            if (!detail.audioLocal && detail.abRows.length > 0 && typeof Stream !== 'undefined') {
                Abb.fetchInfoHash(detail.abRows[0].slug, function(d) {
                    if (d && d.infoHash) { detail.abInfoHash = d.infoHash; Stream.prefetch(d.infoHash, 0) }
                })
            }
        })
        detail.loadTorrents()
    }

    // ── torrents: live federated indexer search for this book (BookTorrents facade) ──
    function loadTorrents() {
        if (typeof BookTorrents === 'undefined' || !detail.book || !detail.book.title) return
        detail.torrents = []
        detail.torExpanded = false       // a new book collapses the shelf back to the top matches
        detail.torLoading = true
        BookTorrents.search(detail.book.title, detail.book.author || "")
    }
    Connections {
        target: (typeof BookTorrents !== 'undefined') ? BookTorrents : null
        function onResultsReady(rows) { detail.torrents = rows; detail.torLoading = false; detail.refreshLocal() }
        function onSearchFinished()   { detail.torLoading = false }
    }
    function edMeta(ed) {
        var p = []
        if (ed.size) p.push("<b>" + ed.size + "</b>")
        if (ed.source) p.push(String(ed.source).toUpperCase())   // LIBGEN / OCEANOFPDF / …
        if (ed.year) p.push(ed.year)
        if (ed.language) p.push(ed.language)
        return p.join("   ·   ")
    }
    // A clean format badge — source rows can carry messy format strings, so pull a real
    // extension out if there is one, else "WEB" for page-only sources and "FILE" as a last resort.
    function fmtLabel(ed) {
        var f = String((ed && ed.format) || "").toLowerCase()
        var m = f.match(/\b(epub|pdf|mobi|azw3|azw|cbz|cbr|djvu|fb2|txt)\b/)
        if (m) return m[1].toUpperCase()
        if (f.indexOf("/") >= 0 && f.length <= 12) return f.toUpperCase()   // e.g. "pdf/epub"
        return (ed && ed.md5) ? "FILE" : "WEB"
    }

    // ── download-fed reading: a click pulls the file IN-APP (never out to a browser) ──
    // The native `Books` engine resolves LibGen's fresh key + streams the file to
    // <appdata>/books, then the reader opens that local file. Mirrors Tankoban 2.
    function dlName(ed) {
        var base = (detail.book && detail.book.title) ? detail.book.title : "book"
        return base + "." + ((ed && ed.format) ? ed.format : "epub")
    }
    function bestEdition() {
        for (var i = 0; i < detail.editions.length; i++) if (detail.editions[i].best) return detail.editions[i]
        return detail.editions.length ? detail.editions[0] : null
    }
    // One copy per book: before any new download, drop the previous copy — LibGen edition
    // OR torrent — so a fresh pick REPLACES the old one instead of piling up on disk.
    function clearExistingCopies() {
        if (typeof Books !== 'undefined')
            for (var i = 0; i < detail.editions.length; i++)
                if (Books.isDownloaded(detail.editions[i].md5)) Books.deleteBook(detail.editions[i].md5)
        if (typeof BookTorrents !== 'undefined')
            for (var j = 0; j < detail.torrents.length; j++)
                if (BookTorrents.isDownloaded(detail.torrents[j].infoHash)) BookTorrents.deleteDownload(detail.torrents[j].infoHash)
    }
    function startDownload(ed) {
        if (!ed) return
        detail.clearExistingCopies()   // replace, don't accumulate
        // LibGen rows carry an md5 the native engine pulls in-app; page-URL sources (e.g.
        // OceanofPDF, when it lands) open the page to grab the file until native fetch arrives.
        if (ed.md5 && typeof Books !== 'undefined')
            Books.downloadBook(ed.md5, detail.dlName(ed),
                               (detail.book && detail.book.title) ? detail.book.title : "", 0)
        else if (ed.url)
            Qt.openUrlExternally(ed.url)
    }
    // "Do I have a readable copy?" — ONE book-level question across BOTH sources
    // (LibGen edition on disk, or a downloaded torrent). Either answers "Ready to read".
    function refreshLocal() {
        var p = ""
        if (typeof Books !== 'undefined')
            for (var i = 0; i < detail.editions.length; i++) {
                var lp = Books.localBook(detail.editions[i].md5)
                if (lp) { p = lp; break }
            }
        if (!p && typeof BookTorrents !== 'undefined')
            for (var j = 0; j < detail.torrents.length; j++) {
                var lf = BookTorrents.localFile(detail.torrents[j].infoHash)
                if (lf) { p = lf; break }
            }
        detail.localPath = p
    }
    Connections {
        target: (typeof Books !== 'undefined') ? Books : null
        function onFinished(md5, path) { detail.refreshLocal() }
        function onRemoved(md5) { detail.refreshLocal() }
    }
    Connections {
        target: (typeof BookTorrents !== 'undefined') ? BookTorrents : null
        function onFinished(h, path) { detail.refreshLocal() }
        function onRemoved(h) { detail.refreshLocal() }
    }

    // ── top bar ────────────────────────────────────────────────────────────
    Glass {
        id: bar
        backdrop: detail.backdrop
        x: theme.margin; y: 22
        width: detail.width - theme.margin * 2
        height: 64; radius: 16

        Row {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 22
            BackAction {
                // Biblio world rule: quieter size, white (ink) hover — never gold
                labelSize: 14
                idleColor: theme.inkDim
                hoverColor: theme.ink
                anchors.verticalCenter: parent.verticalCenter
                onTriggered: detail.backRequested()
            }
            Text {
                text: "Biblio"; color: theme.ink; font.family: theme.display; font.pixelSize: 20
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Repeater {
                model: [ { g: "—", a: "min" }, { g: "⏻", a: "pow" } ]   // fullscreen-only: no maximize
                delegate: Rectangle {
                    required property var modelData
                    width: 30; height: 30; radius: 8
                    color: sysMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.g; color: theme.inkDimmer; font.pixelSize: 14 }
                    MouseArea {
                        id: sysMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.a === "min") detail.minimizeRequested()
                            else if (modelData.a === "pow") detail.closeRequested()
                        }
                    }
                }
            }
        }
    }

    // ── scrollable content ─────────────────────────────────────────────────
    Flickable {
        id: page
        anchors.left: parent.left; anchors.right: parent.right
        y: 108; height: detail.height - 108
        contentWidth: width
        contentHeight: body.implicitHeight + 70
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Item {
            id: body
            x: theme.margin
            width: detail.width - theme.margin * 2
            implicitHeight: Math.max(coverCol.implicitHeight, textCol.implicitHeight) + 36

            // ── cover column ──
            Column {
                id: coverCol
                width: 268
                topPadding: 16
                spacing: 28

                // the book as a physical object: soft shadow + cover + spine + page edge
                Item {
                    width: 268; height: 402

                    Rectangle {                       // page edge (right)
                        anchors.right: parent.right; anchors.rightMargin: -5
                        y: 5; width: 7; height: parent.height - 10; radius: 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "#d3cdbe" }
                            GradientStop { position: 1; color: "#a8a294" }
                        }
                    }
                    Image {
                        id: coverImg
                        anchors.fill: parent
                        source: (detail.book && detail.book.cover) ? detail.book.cover : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true; cache: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Qt.rgba(0, 0, 0, 0.7)
                            shadowBlur: 1.0
                            shadowVerticalOffset: 26
                            shadowHorizontalOffset: 0
                            autoPaddingEnabled: true
                        }
                    }
                    Rectangle {                       // base tint while the cover loads
                        anchors.fill: coverImg; z: -1; radius: 3
                        color: (detail.book && detail.book.c1) ? detail.book.c1 : "#14131a"
                    }
                    Rectangle {                       // spine (left)
                        anchors.left: parent.left; width: 11; height: parent.height; radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.5) }
                            GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.05) }
                            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                        }
                    }
                }

                Column {                              // actions
                    width: 268; spacing: 12
                    Rectangle {
                        id: primaryCta            // one CTA, TB2-style: Get the book → Read (never a browser)
                        width: parent.width; height: 50; radius: 13; color: theme.gold
                        property bool ready: detail.localPath !== ""
                        Text {
                            anchors.centerIn: parent
                            text: primaryCta.ready ? "Read"
                                  : (detail.editions.length ? "Get the book" : "Find the book")
                            color: "#241a05"
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (primaryCta.ready) detail.readRequested(detail.localPath, detail.book)
                                else detail.startDownload(detail.bestEdition())
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 50; radius: 13
                        color: libMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent; text: "+ Library"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea { id: libMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
                    }
                    Rectangle {                       // Listen — appears once the paired audiobook is on disk (Option A: Read + Listen side by side)
                        visible: detail.audioLocal
                        width: parent.width; height: 50; radius: 13; color: theme.gold
                        Row { anchors.centerIn: parent; spacing: 8
                            Image { source: "../assets/icons/music.svg"; width: 15; height: 15; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "Listen"; color: "#241a05"; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: detail.listenRequested(detail.pairKey, detail.book) }
                    }
                }
            }

            // ── text column ──
            Column {
                id: textCol
                anchors.left: coverCol.right; anchors.leftMargin: 64
                anchors.right: parent.right
                topPadding: 18
                spacing: 0

                Text {                                // eyebrow
                    text: (detail.book && detail.book.genreLine ? detail.book.genreLine : "").toUpperCase()
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { width: 1; height: 14 }
                Text {                                // title
                    text: detail.book && detail.book.title ? detail.book.title : ""
                    color: theme.ink; font.family: theme.display; font.pixelSize: 54
                    width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.02
                }
                Item { width: 1; height: 20 }
                Text {                                // tagline — the hero
                    visible: text.length > 0
                    text: detail.book && detail.book.tagline ? "“" + detail.book.tagline + "”" : ""
                    color: theme.ink; opacity: 0.92
                    font.family: theme.display; font.italic: true; font.pixelSize: 28
                    width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.3
                }
                Item { width: 1; height: 30 }
                Item {                                // hairline rule with a gold tick
                    width: parent.width; height: 3
                    Rectangle {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 1
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: theme.edge }
                            GradientStop { position: 0.7; color: "transparent" }
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.top: parent.top; width: 34; height: 3; radius: 2; color: theme.gold }
                }
                Item { width: 1; height: 26 }
                Text {                                // synopsis with a raised initial
                    width: Math.min(parent.width, 640)
                    textFormat: Text.RichText
                    text: detail.dropCapHtml(detail.book ? detail.book.synopsis : "")
                    color: theme.inkDim; font.family: theme.display; font.pixelSize: 17
                    wrapMode: Text.WordWrap; lineHeight: 1.7
                }

                // ── Torrents — live federated indexer search; ranked best-match × seeders ──
                Item { width: 1; height: 40; visible: typeof BookTorrents !== 'undefined' }
                Text {
                    visible: typeof BookTorrents !== 'undefined'
                    text: "TORRENTS" + (detail.torLoading ? "  ·  SEARCHING…"
                          : (detail.torrents.length > 0 ? "  ·  " + detail.torrents.length : "  ·  NONE"))
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { width: 1; height: 12; visible: typeof BookTorrents !== 'undefined' }
                Glass {
                    visible: typeof BookTorrents !== 'undefined'
                    backdrop: detail.backdrop
                    width: Math.min(parent.width, 640); radius: 14
                    height: torCol.implicitHeight
                    Column {
                        id: torCol
                        width: parent.width
                        Item {                                   // loading / empty state
                            visible: detail.torLoading || detail.torrents.length === 0
                            width: parent.width; height: 52
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: detail.torLoading ? "Searching torrents…" : "No torrents found"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                        }
                        Repeater {
                            // collapsed to the top torCap matches; "See more" reveals the rest
                            model: detail.torExpanded ? detail.torrents : detail.torrents.slice(0, detail.torCap)
                            delegate: Item {
                                id: torRow
                                required property var modelData
                                required property int index
                                width: parent.width; height: 64
                                property string dlState:
                                    (typeof BookTorrents !== 'undefined' && BookTorrents.isDownloaded(modelData.infoHash)) ? "done" : "idle"
                                property real dlPct: 0
                                Connections {
                                    target: (typeof BookTorrents !== 'undefined') ? BookTorrents : null
                                    function onResolving(h) { if (h === torRow.modelData.infoHash) torRow.dlState = "resolving" }
                                    function onProgress(h, rcv, tot) { if (h === torRow.modelData.infoHash) { torRow.dlState = "downloading"; torRow.dlPct = tot > 0 ? rcv / tot : 0 } }
                                    function onFinished(h, path) { if (h === torRow.modelData.infoHash) { torRow.dlState = "done"; torRow.dlPct = 1 } }
                                    function onFailed(h, why) { if (h === torRow.modelData.infoHash) torRow.dlState = "failed" }
                                    function onRemoved(h) { if (h === torRow.modelData.infoHash) { torRow.dlState = "idle"; torRow.dlPct = 0 } }
                                }
                                // Top row = the recommended pick (best title-match × seeders) — subtly lit; the rest stay one tap away.
                                Rectangle { anchors.fill: parent; color: torMa.containsMouse ? Qt.rgba(1,1,1,0.06)
                                    : (index === 0 ? Qt.rgba(0.94,0.77,0.29,0.06) : "transparent") }
                                Rectangle { visible: index > 0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                                // No format pill on torrents: a torrent is an opaque bundle,
                                // so any format tag is a title-guess (often wrong) — and after
                                // the readable-book filter every row here is an ebook anyway.
                                // (Hemanth 2026-07-13.) The EDITIONS shelf keeps its tag — that
                                // one comes from LibGen's real per-file metadata.
                                Column {                         // title + metadata
                                    anchors.left: parent.left; anchors.leftMargin: 18
                                    anchors.right: torInd.left; anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3
                                    Text {                       // the torrent title (elided to one line)
                                        width: parent.width
                                        text: torRow.modelData.title
                                        elide: Text.ElideRight; maximumLineCount: 1
                                        color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                                    }
                                    Text {                       // seeders · size · pack
                                        text: "▲ " + torRow.modelData.seeders + "   " + torRow.modelData.size
                                              + (torRow.modelData.pack ? "   ·  PACK" : "")
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                    }
                                }
                                Text {                           // download-state indicator
                                    id: torInd
                                    anchors.right: parent.right; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: torRow.dlState === "done" ? "✓"
                                        : torRow.dlState === "downloading" ? (Math.round(torRow.dlPct * 100) + "%")
                                        : torRow.dlState === "resolving" ? "…"
                                        : torRow.dlState === "failed" ? "retry" : "↓"
                                    color: torRow.dlState === "done" ? theme.gold : (torMa.containsMouse ? theme.gold : theme.inkDimmer)
                                    font.family: theme.ui
                                    font.pixelSize: (torRow.dlState === "downloading" || torRow.dlState === "failed") ? 12 : 16
                                }
                                MouseArea { id: torMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (torRow.dlState === "done") {
                                            var lf = BookTorrents.localFile(torRow.modelData.infoHash)
                                            if (lf) { detail.readRequested(lf, detail.book); return }
                                            torRow.dlState = "idle"   // file vanished from disk since page opened — re-derive, fall through to re-download
                                        }
                                        if (torRow.dlState !== "downloading" && torRow.dlState !== "resolving") {
                                            detail.clearExistingCopies()   // replace any previous copy of this book
                                            BookTorrents.download(torRow.modelData.infoHash, detail.book.title, detail.book.author || "")
                                        }
                                    }
                                }
                            }
                        }
                        Item {                                   // See more / See less — only when capped
                            visible: detail.torrents.length > detail.torCap
                            width: parent.width; height: 46
                            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                            Text {
                                anchors.centerIn: parent
                                text: detail.torExpanded ? "See less"
                                      : ("See " + (detail.torrents.length - detail.torCap) + " more")
                                color: seeMoreMa.containsMouse ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 0.6
                            }
                            MouseArea { id: seeMoreMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: detail.torExpanded = !detail.torExpanded }
                        }
                    }
                }

                Item { width: 1; height: 40 }
                // ── Editions — live from LibGen (recreates TB2's scraper); click opens the download ──
                Text {
                    text: "EDITIONS" + (detail.edLoading ? "  ·  SEARCHING…"
                          : (detail.editions.length > 0 ? "  ·  " + detail.editions.length : "  ·  NONE"))
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { width: 1; height: 12 }
                Glass {
                    backdrop: detail.backdrop
                    width: Math.min(parent.width, 640); radius: 14
                    height: edCol.implicitHeight
                    Column {
                        id: edCol
                        width: parent.width

                        Item {                              // loading / empty state
                            visible: detail.edLoading || detail.editions.length === 0
                            width: parent.width; height: 52
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: detail.edLoading ? "Searching LibGen…" : "No editions found"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                        }

                        Repeater {
                            model: detail.editions
                            delegate: Item {
                                id: edRow
                                required property var modelData
                                required property int index
                                width: parent.width; height: 52
                                // download-fed state for THIS edition, reactive via the native Books signals
                                property string dlState: (typeof Books !== 'undefined' && Books.isDownloaded(modelData.md5)) ? "done" : "idle"
                                property real dlPct: 0
                                Connections {
                                    target: (typeof Books !== 'undefined') ? Books : null
                                    function onResolving(md5) { if (md5 === edRow.modelData.md5) edRow.dlState = "resolving" }
                                    function onProgress(md5, rcv, tot) { if (md5 === edRow.modelData.md5) { edRow.dlState = "downloading"; edRow.dlPct = tot > 0 ? rcv / tot : 0 } }
                                    function onFinished(md5, path) { if (md5 === edRow.modelData.md5) { edRow.dlState = "done"; edRow.dlPct = 1 } }
                                    function onFailed(md5, why) { if (md5 === edRow.modelData.md5) edRow.dlState = "failed" }
                                    function onRemoved(md5) { if (md5 === edRow.modelData.md5) { edRow.dlState = "idle"; edRow.dlPct = 0 } }
                                }
                                Rectangle { anchors.fill: parent; color: edMa.containsMouse ? Qt.rgba(1,1,1,0.06)
                                    : (modelData.best ? Qt.rgba(0.94,0.77,0.29,0.06) : "transparent") }
                                Rectangle { visible: index > 0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                                Row {
                                    anchors.left: parent.left; anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 16
                                    Rectangle {
                                        width: Math.max(54, fmtT.implicitWidth + 16); height: 24; radius: 7; color: "transparent"
                                        border.width: 1
                                        border.color: modelData.best ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { id: fmtT; anchors.centerIn: parent; text: detail.fmtLabel(modelData)
                                            color: modelData.best ? theme.gold : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.8 }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: detail.edMeta(modelData)
                                        textFormat: Text.RichText
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    }
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: edRow.dlState === "done" ? "✓"
                                        : edRow.dlState === "downloading" ? (Math.round(edRow.dlPct * 100) + "%")
                                        : edRow.dlState === "resolving" ? "…"
                                        : edRow.dlState === "failed" ? "retry"
                                        : (edRow.modelData.md5 ? "↓" : "↗")
                                    color: edRow.dlState === "done" ? theme.gold : (edMa.containsMouse ? theme.gold : theme.inkDimmer)
                                    font.family: theme.ui
                                    font.pixelSize: (edRow.dlState === "downloading" || edRow.dlState === "failed") ? 12 : 16
                                }
                                MouseArea { id: edMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (edRow.dlState === "done") {
                                            var lf = Books.localBook(edRow.modelData.md5)
                                            if (lf) { detail.readRequested(lf, detail.book); return }
                                            edRow.dlState = "idle"   // file vanished since page opened — re-derive, fall through to re-download
                                        }
                                        if (edRow.dlState !== "downloading" && edRow.dlState !== "resolving") detail.startDownload(edRow.modelData)
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Audiobook — paired from AudioBookBay; download → Listen (Option A) ──
                Item { width: 1; height: 30 }
                Text {
                    text: "AUDIOBOOK" + (detail.abLoading ? "  ·  SEARCHING…"
                          : (detail.abRows.length > 0 ? "  ·  " + detail.abRows.length : "  ·  NONE"))
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { width: 1; height: 12 }
                Glass {
                    backdrop: detail.backdrop
                    width: Math.min(parent.width, 640); radius: 14
                    height: abCol.implicitHeight
                    Column {
                        id: abCol
                        width: parent.width
                        // The download is per-TITLE (one paired audiobook / pairKey), but the panel
                        // lists several release candidates. Track WHICH row the user picked so only
                        // that row shows progress/✓ — the others stay "↓" (they're alternatives).
                        property string abState: "idle"
                        property string abActiveSlug: ""     // the row being downloaded
                        property real abPct: 0
                        Connections {
                            target: (typeof Audiobooks !== 'undefined') ? Audiobooks : null
                            function onResolving(key) { if (key === detail.pairKey) abCol.abState = "resolving" }
                            function onProgress(key, rcv, tot) { if (key === detail.pairKey) { abCol.abState = "downloading"; abCol.abPct = tot > 0 ? rcv / tot : 0 } }
                            function onFinished(key, path) { if (key === detail.pairKey) { abCol.abState = "done"; abCol.abPct = 1; detail.audioLocal = true } }
                            function onFailed(key, why) { if (key === detail.pairKey) abCol.abState = "failed" }
                        }
                        // REHYDRATE on page (re)entry: the download runs in C++ and survives leaving
                        // this page, but abState/abPct are page-local and reset — without this seed a
                        // running download read as "stopped" (the 2026-07-18 report). Which ROW was
                        // picked (abActiveSlug) is unrecoverable by design; the banner below carries
                        // slug-less background progress instead.
                        Component.onCompleted: {
                            if (typeof Audiobooks === 'undefined' || !detail.pairKey) return
                            var st = Audiobooks.statusOf(detail.pairKey)
                            if (st && (st.state === "downloading" || st.state === "resolving")) {
                                abCol.abState = st.state
                                abCol.abPct = (st.total > 0) ? st.received / st.total : 0
                            }
                        }
                        Item {                              // background download banner (rehydrated state)
                            visible: abCol.abActiveSlug === ""
                                     && (abCol.abState === "downloading" || abCol.abState === "resolving")
                            width: parent.width; height: visible ? 40 : 0
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter
                                text: abCol.abState === "resolving" ? "Downloading in the background — resolving…"
                                    : ("Downloading in the background — " + Math.round(abCol.abPct * 100) + "%")
                                color: theme.gold; font.family: theme.ui; font.pixelSize: 13
                            }
                        }
                        Item {                              // loading / empty
                            visible: detail.abLoading || detail.abRows.length === 0
                            width: parent.width; height: 52
                            Text { anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter
                                text: detail.abLoading ? "Searching AudioBookBay…" : "No audiobook found"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                        }
                        Repeater {
                            model: detail.abRows
                            delegate: Item {
                                id: abRow
                                required property var modelData
                                required property int index
                                width: parent.width; height: 52
                                // this row reflects the download state ONLY if it's the active pick
                                readonly property string rowState: (modelData.slug === abCol.abActiveSlug) ? abCol.abState : "idle"
                                Rectangle { anchors.fill: parent; color: abRowMa.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent" }
                                Rectangle { visible: index > 0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                                Row {
                                    anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter; spacing: 16
                                    Rectangle {
                                        width: 58; height: 24; radius: 7; color: "transparent"; border.width: 1; border.color: theme.edge
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { anchors.centerIn: parent
                                            text: (abRow.modelData.format || "AUDIO").toUpperCase().substring(0,6)
                                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.6 }
                                    }
                                    Text { anchors.verticalCenter: parent.verticalCenter
                                        text: [abRow.modelData.size, abRow.modelData.language, abRow.modelData.posted].filter(function(s){return s}).join("   ·   ")
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
                                    text: abRow.rowState === "done" ? "✓"
                                        : abRow.rowState === "downloading" ? (Math.round(abCol.abPct*100) + "%")
                                        : abRow.rowState === "resolving" ? "…"
                                        : abRow.rowState === "failed" ? "retry" : "↓"
                                    color: abRow.rowState === "done" ? theme.gold : (abRowMa.containsMouse ? theme.gold : theme.inkDimmer)
                                    font.family: theme.ui; font.pixelSize: (abRow.rowState === "downloading" || abRow.rowState === "failed") ? 12 : 16
                                }
                                MouseArea { id: abRowMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (abRow.rowState === "done") { detail.listenRequested(detail.pairKey, detail.book); return }
                                        // one download at a time: ignore clicks while another row is resolving/downloading
                                        if (abCol.abState === "downloading" || abCol.abState === "resolving") return
                                        if (typeof Audiobooks === 'undefined') return
                                        abCol.abActiveSlug = abRow.modelData.slug   // THIS row is the pick
                                        abCol.abState = "resolving"
                                        Abb.fetchInfoHash(abRow.modelData.slug, function(d) {
                                            if (!d || !d.infoHash) { abCol.abState = "failed"; return }
                                            detail.abInfoHash = d.infoHash
                                            // Auto-attach key: the reader's bookId = keyFor(<ebook path>),
                                            // the SAME id the reader's Audio tab reads the pairing by. Only
                                            // when the ebook is on disk (detail.localPath) can we produce it;
                                            // mirror ReaderShell's guard — empty path → "" (NEVER bookKey(""),
                                            // which the reader never queries). No path yet → no attach, no
                                            // mismatched key.
                                            var bookId = (detail.localPath && typeof Reader2Bridge !== 'undefined')
                                                ? Reader2Bridge.bookKey(detail.localPath) : ""
                                            Audiobooks.downloadAudiobook(detail.pairKey, d.infoHash,
                                                detail.book.title || "", detail.book.author || "", bookId)
                                        })
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ScrollGlide { flick: page }
}
