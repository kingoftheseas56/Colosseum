// BiblioBook — the book "dust-jacket" detail page. Owner: A2. OUR OWN design (NOT the manga series
// view): the cover as a physical object · the tagline as the hero · a drop-capped synopsis · an
// "Editions" panel. Opens as a layer over the Biblio world (Main.qml bookLayer). `book` is a full
// Apple object from BiblioApi.fullBook.
//
// Editions are live acquisition surfaces: tracked LibGen files download in-app, while page-only
// sources remain explicit external links. Arc 19 keeps those power-user controls acquire-only.

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

    // Arc 19: injectable service seams preserve production defaults while letting the
    // foreground Read contract run deterministically in an offscreen harness.
    property var booksRef: (typeof Books !== "undefined") ? Books : null
    property var bookTorrentsRef: (typeof BookTorrents !== "undefined") ? BookTorrents : null
    property var collectionRef: (typeof Collection !== "undefined") ? Collection : null
    property var progressRef: (typeof Progress !== "undefined") ? Progress : null

    // Foreground consumption intent is page-local and ephemeral. The download engines remain
    // the durable acquisition authorities and keep running when this intent is abandoned.
    property int pendingReadGeneration: 0
    property string pendingReadTransport: ""     // "lookup" | "choice" | "books" | "torrent"
    property string pendingReadAcquisitionId: "" // md5 or infoHash
    property string pendingReadBookIdentity: ""  // pairKey at the instant Read was asserted
    property string pendingReadState: ""
    property real pendingReadReceived: 0
    property real pendingReadTotal: 0
    property string readError: ""
    property bool readChoiceOpen: false
    property var readChoiceRows: []
    property int sourceGeneration: 0
    readonly property bool pendingReadActive: detail.pendingReadTransport.length > 0

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal readRequested(string path, var book)   // a downloaded edition is on disk, ready for the reader
    // (listenRequested retired 2026-07-18 — the reader is the one audiobook surface)

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
    onBookChanged: {
        detail._invalidateReadIntent()
        detail.readChoiceOpen = false
        detail.readChoiceRows = []
        detail.readError = ""
        detail.localPath = ""                 // never expose the previous book's ready path
        detail.loadEditions()
    }
    function loadEditions() {
        if (!detail.book || !detail.book.title) return
        var gen = ++detail.sourceGeneration
        detail.edLoading = true
        detail.editions = []
        BiblioApi.searchLibgen(detail.book.title, detail.book.author, function(eds) {
            if (gen !== detail.sourceGeneration) return
            detail.editions = eds
            detail.edLoading = false
            detail.refreshLocal()
            detail._resolvePendingLookup(detail.pendingReadGeneration)
        })
        // audiobook lane: is one already downloaded? then find one to download.
        detail.audioLocal = (typeof Audiobooks !== 'undefined') && Audiobooks.isDownloaded(detail.pairKey)
        detail.abLoading = true; detail.abRows = []
        Abb.resolveAudiobook(detail.book.title, detail.book.author, function(res) {
            if (gen !== detail.sourceGeneration) return
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
        var service = detail.bookTorrentsRef
        detail.torrents = []
        detail.torExpanded = false       // a new book collapses the shelf back to the top matches
        if (!service || !detail.book || !detail.book.title) {
            detail.torLoading = false
            detail._resolvePendingLookup(detail.pendingReadGeneration)
            return
        }
        detail.torLoading = true
        service.search(detail.book.title, detail.book.author || "")
    }
    Connections {
        target: detail.bookTorrentsRef
        ignoreUnknownSignals: true
        function onResultsReady(rows) {
            detail.torrents = rows || []
            detail.torLoading = false
            detail.refreshLocal()
            detail._resolvePendingLookup(detail.pendingReadGeneration)
        }
        function onSearchFinished() {
            detail.torLoading = false
            detail._resolvePendingLookup(detail.pendingReadGeneration)
        }
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
    // One copy per book: before any NEW acquisition, drop the previous copy so a fresh
    // pick replaces it instead of piling up on disk. Adopting an existing job never calls this.
    function clearExistingCopies() {
        var books = detail.booksRef
        if (books)
            for (var i = 0; i < detail.editions.length; i++) {
                var md5 = String(detail.editions[i].md5 || "")
                if (md5.length && books.isDownloaded(md5)) books.deleteBook(md5)
            }
        var torrents = detail.bookTorrentsRef
        if (torrents)
            for (var j = 0; j < detail.torrents.length; j++) {
                var hash = String(detail.torrents[j].infoHash || "")
                if (hash.length && torrents.isDownloaded(hash)) torrents.deleteDownload(hash)
            }
    }
    function collectionEntry() {
        return { "id": detail.pairKey, "type": "book",
                 "title": detail.book.title || "", "cover": detail.book.cover || "",
                 "payload": ({ "book": detail.book }) }
    }
    function collectBook() {
        if (detail.collectionRef && detail.collectionRef.add)
            detail.collectionRef.add("biblio", detail.collectionEntry())
    }
    function _bookIdentity() { return String(detail.pairKey || "") }
    function _progressIdentity() {
        if (detail.book && detail.book.id !== undefined && String(detail.book.id).length)
            return String(detail.book.id)
        return detail.localPath || ""
    }
    function readingProgress() {
        var id = detail._progressIdentity()
        if (!id.length || !detail.progressRef || !detail.progressRef.get) return -1
        var rec = detail.progressRef.get("book", id) || ({})
        var value = Number(rec.progress)
        if (!isFinite(value)) return -1
        return Math.max(0, Math.min(1, value))
    }
    function primaryReadLabel() {
        if (detail.pendingReadTransport === "books" || detail.pendingReadTransport === "torrent")
            return "Read when ready"
        if (detail.localPath.length) {
            var p = detail.readingProgress()
            if (p > 0 && p < 1) return "Continue"
        }
        return "Read"
    }
    function primaryReadStatus() {
        if (detail.readError.length) return detail.readError
        if (detail.localPath.length) {
            var p = detail.readingProgress()
            return (p > 0 && p < 1) ? (Math.round(p * 100) + "% read · Ready on this device")
                                    : "Ready on this device"
        }
        if (detail.pendingReadTransport === "lookup") return "Finding a readable edition…"
        if (detail.pendingReadTransport === "choice") return "Choose an edition to continue"
        if (detail.pendingReadTransport === "books" || detail.pendingReadTransport === "torrent") {
            if (detail.pendingReadState === "resolving") return "Preparing your edition…"
            if (detail.pendingReadState === "queued") return "Queued · your Read will open when ready"
            if (detail.pendingReadTotal > 0)
                return "Your Read is attached · " + Math.round(detail.pendingReadReceived / detail.pendingReadTotal * 100) + "%"
            return "Your Read is attached · downloading"
        }
        var best = detail.bestEdition()
        if (best && best.md5 && detail.booksRef)
            return "Preferred " + detail.fmtLabel(best) + " will be prepared if needed"
        if (detail.edLoading || detail.torLoading) return "Sources are loading"
        if (detail.editions.length || detail.torrents.length) return "Read will choose a tracked edition"
        return "No tracked readable source yet"
    }
    function _clearReadIntent() {
        detail.pendingReadTransport = ""
        detail.pendingReadAcquisitionId = ""
        detail.pendingReadBookIdentity = ""
        detail.pendingReadState = ""
        detail.pendingReadReceived = 0
        detail.pendingReadTotal = 0
        detail.readChoiceOpen = false
        detail.readChoiceRows = []
    }
    function _invalidateReadIntent() {
        detail.pendingReadGeneration += 1
        detail._clearReadIntent()
    }
    function _beginReadIntent() {
        detail.pendingReadGeneration += 1
        detail.pendingReadBookIdentity = detail._bookIdentity()
        detail.pendingReadTransport = "lookup"
        detail.pendingReadAcquisitionId = ""
        detail.pendingReadState = "finding"
        detail.pendingReadReceived = 0
        detail.pendingReadTotal = 0
        detail.readError = ""
        return detail.pendingReadGeneration
    }
    function _readIntentCurrent(gen) {
        return detail.pendingReadActive
            && Number(gen) === detail.pendingReadGeneration
            && detail.pendingReadBookIdentity === detail._bookIdentity()
    }
    function _inFlight(state) {
        return state === "resolving" || state === "queued" || state === "downloading"
    }
    function _status(service, id) {
        if (!service || !service.statusOf || !id) return ({ state: "none", received: 0, total: 0 })
        return service.statusOf(id) || ({ state: "none", received: 0, total: 0 })
    }
    function _candidate(transport, id, label, meta, payload) {
        return { transport: transport, id: String(id || ""), label: String(label || ""),
                 meta: String(meta || ""), payload: payload || ({}) }
    }
    function _activeReadCandidates() {
        var out = []
        var books = detail.booksRef
        if (books) {
            for (var i = 0; i < detail.editions.length; i++) {
                var ed = detail.editions[i]
                var md5 = String(ed.md5 || "")
                if (!md5.length) continue
                var bs = detail._status(books, md5)
                if (detail._inFlight(String(bs.state || "")))
                    out.push(detail._candidate("books", md5, detail.fmtLabel(ed), detail.edMeta(ed), ed))
            }
        }
        var torrents = detail.bookTorrentsRef
        if (torrents) {
            for (var j = 0; j < detail.torrents.length; j++) {
                var tor = detail.torrents[j]
                var hash = String(tor.infoHash || "").toLowerCase()
                if (!hash.length) continue
                var ts = detail._status(torrents, hash)
                if (detail._inFlight(String(ts.state || "")))
                    out.push(detail._candidate("torrent", hash, tor.format || "EBOOK",
                                               tor.title || "Torrent", tor))
            }
        }
        return out
    }
    function _trackedReadChoices() {
        var out = []
        if (detail.booksRef) {
            for (var i = 0; i < detail.editions.length; i++) {
                var ed = detail.editions[i]
                if (!ed.md5) continue
                out.push(detail._candidate("books", ed.md5, detail.fmtLabel(ed), detail.edMeta(ed), ed))
            }
        }
        if (detail.bookTorrentsRef) {
            var cap = Math.min(detail.torrents.length, detail.torCap)
            for (var j = 0; j < cap; j++) {
                var tor = detail.torrents[j]
                if (!tor.infoHash) continue
                out.push(detail._candidate("torrent", String(tor.infoHash).toLowerCase(),
                                           tor.format || "EBOOK", tor.title || "Torrent", tor))
            }
        }
        return out
    }
    function _openReadChoice(rows, gen) {
        if (!detail._readIntentCurrent(gen)) return false
        detail.readChoiceRows = rows || []
        if (!detail.readChoiceRows.length) return false
        detail.pendingReadTransport = "choice"
        detail.pendingReadAcquisitionId = ""
        detail.pendingReadState = "choice"
        detail.readChoiceOpen = true
        return true
    }
    function _targetReadCandidate(candidate, gen) {
        if (!candidate || !candidate.id || !detail._readIntentCurrent(gen)) return false
        var transport = String(candidate.transport || "")
        var service = transport === "books" ? detail.booksRef : detail.bookTorrentsRef
        if (!service) return false
        var id = String(candidate.id || "")
        if (transport === "torrent") id = id.toLowerCase()
        var state = detail._status(service, id)
        var stateName = String(state.state || "none")
        detail.pendingReadTransport = transport
        detail.pendingReadAcquisitionId = id
        detail.pendingReadState = stateName
        detail.pendingReadReceived = Number(state.received || 0)
        detail.pendingReadTotal = Number(state.total || 0)
        detail.readChoiceOpen = false
        detail.readChoiceRows = []
        if (stateName === "done") return detail._finishRead(transport, id, gen)
        if (detail._inFlight(stateName)) return true

        detail.clearExistingCopies()
        detail.collectBook()
        if (transport === "books") {
            var ed = candidate.payload || ({})
            service.downloadBook(id, detail.dlName(ed),
                                 (detail.book && detail.book.title) ? detail.book.title : "", 0,
                                 (detail.book && detail.book.author) ? detail.book.author : "")
        } else {
            service.download(id, detail.book.title || "", detail.book.author || "")
        }
        var fresh = detail._status(service, id)
        detail.pendingReadState = String(fresh.state || "resolving")
        detail.pendingReadReceived = Number(fresh.received || 0)
        detail.pendingReadTotal = Number(fresh.total || 0)
        return true
    }
    function chooseReadSource(candidate) {
        var gen = detail.pendingReadGeneration
        if (detail.pendingReadTransport !== "choice" || !detail._readIntentCurrent(gen)) return
        detail._targetReadCandidate(candidate, gen)
    }
    function cancelReadChoice() {
        detail._invalidateReadIntent()
        detail.readError = ""
    }
    function _finishRead(transport, id, gen) {
        if (!detail._readIntentCurrent(gen)
                || detail.pendingReadTransport !== transport
                || detail.pendingReadAcquisitionId !== String(id)) return false
        var service = transport === "books" ? detail.booksRef : detail.bookTorrentsRef
        var state = detail._status(service, id)
        if (String(state.state || "") !== "done") return false
        var path = transport === "books" ? service.localBook(id) : service.localFile(id)
        if (!path) return false
        detail.localPath = path
        detail._clearReadIntent()
        detail.readError = ""
        detail.readRequested(path, detail.book)
        return true
    }
    function _failRead(transport, id, reason) {
        if (detail.pendingReadTransport !== transport
                || detail.pendingReadAcquisitionId !== String(id)
                || detail.pendingReadBookIdentity !== detail._bookIdentity()) return
        detail._invalidateReadIntent()
        detail.readError = reason && String(reason).length ? ("Read failed · " + reason) : "Read failed · try again"
    }
    function _resolvePendingLookup(gen) {
        if (!detail._readIntentCurrent(gen) || detail.pendingReadTransport !== "lookup") return false
        detail.refreshLocal()
        if (detail.localPath.length) {
            var path = detail.localPath
            detail._clearReadIntent()
            detail.readRequested(path, detail.book)
            return true
        }

        var active = detail._activeReadCandidates()
        if (active.length === 1) return detail._targetReadCandidate(active[0], gen)
        if (active.length > 1) return detail._openReadChoice(active, gen)

        // Source inventories must settle before choosing a NEW acquisition. An already-known
        // active job above may be adopted immediately, but otherwise waiting preserves the
        // "adopt what is already arriving" precedence when the other transport returns later.
        if (detail.edLoading || detail.torLoading) {
            detail.pendingReadState = "finding"
            return true
        }
        var best = detail.bestEdition()
        if (best && best.md5 && detail.booksRef)
            return detail._targetReadCandidate(
                detail._candidate("books", best.md5, detail.fmtLabel(best), detail.edMeta(best), best), gen)

        // A page-only "best" edition cannot carry a tracked Read through completion.
        // Ask the user to choose among tracked alternatives instead of silently substituting
        // a different edition behind the source they were shown as preferred.
        if (best) {
            var choices = detail._trackedReadChoices()
            if (detail._openReadChoice(choices, gen)) return true
            detail._invalidateReadIntent()
            detail.readError = "This book only has external editions right now · use Editions"
            return false
        }

        // No LibGen edition exists. The torrent list is already ranked by BookTorrentRanker,
        // so its first row is the current product policy rather than a new QML heuristic.
        if (detail.bookTorrentsRef && detail.torrents.length) {
            var tor = detail.torrents[0]
            return detail._targetReadCandidate(
                detail._candidate("torrent", String(tor.infoHash || "").toLowerCase(),
                                  tor.format || "EBOOK", tor.title || "Torrent", tor), gen)
        }
        var fallbackChoices = detail._trackedReadChoices()
        if (detail._openReadChoice(fallbackChoices, gen)) return true
        detail._invalidateReadIntent()
        detail.readError = "No tracked readable source yet"
        return false
    }
    function readBook() {
        detail.refreshLocal()
        if (detail.localPath.length) {
            detail._invalidateReadIntent()
            detail.readError = ""
            detail.readRequested(detail.localPath, detail.book)
            return
        }
        var gen = detail._beginReadIntent()
        detail._resolvePendingLookup(gen)
    }
    // Explicit Editions/Torrents actions are acquire-only. They deliberately invalidate any
    // foreground Read before starting, so their completion can only stop at Ready.
    function downloadEdition(ed) {
        if (!ed) return
        detail._invalidateReadIntent()
        detail.readError = ""
        if (ed.md5 && detail.booksRef) {
            var st = detail._status(detail.booksRef, String(ed.md5))
            if (String(st.state || "") === "done" || detail._inFlight(String(st.state || ""))) return
            detail.clearExistingCopies()
            detail.collectBook()
            detail.booksRef.downloadBook(ed.md5, detail.dlName(ed),
                                         detail.book.title || "", 0, detail.book.author || "")
        } else if (ed.url) {
            // External pages cannot participate in auto-open because Colosseum has no exact
            // completion signal for them. Preserve the existing explicit external-source lane.
            Qt.openUrlExternally(ed.url)
        }
    }
    function downloadTorrent(row) {
        if (!row || !row.infoHash || !detail.bookTorrentsRef) return
        detail._invalidateReadIntent()
        detail.readError = ""
        var hash = String(row.infoHash).toLowerCase()
        var st = detail._status(detail.bookTorrentsRef, hash)
        if (String(st.state || "") === "done" || detail._inFlight(String(st.state || ""))) return
        detail.clearExistingCopies()
        detail.collectBook()
        detail.bookTorrentsRef.download(hash, detail.book.title || "", detail.book.author || "")
    }

    // "Do I have a readable copy?" remains one book-level question across BOTH transports.
    function refreshLocal() {
        var p = ""
        var books = detail.booksRef
        if (books)
            for (var i = 0; i < detail.editions.length; i++) {
                var md5 = String(detail.editions[i].md5 || "")
                if (!md5.length) continue
                var lp = books.localBook(md5)
                if (lp) { p = lp; break }
            }
        var torrents = detail.bookTorrentsRef
        if (!p && torrents)
            for (var j = 0; j < detail.torrents.length; j++) {
                var hash = String(detail.torrents[j].infoHash || "").toLowerCase()
                if (!hash.length) continue
                var lf = torrents.localFile(hash)
                if (lf) { p = lf; break }
            }
        detail.localPath = p
    }
    Connections {
        target: detail.booksRef
        ignoreUnknownSignals: true
        function onResolving(md5) {
            if (detail.pendingReadTransport === "books" && String(md5) === detail.pendingReadAcquisitionId) {
                detail.pendingReadState = "resolving"; detail.pendingReadReceived = 0; detail.pendingReadTotal = 0
            }
        }
        function onProgress(md5, received, total) {
            if (detail.pendingReadTransport === "books" && String(md5) === detail.pendingReadAcquisitionId) {
                detail.pendingReadState = "downloading"
                detail.pendingReadReceived = Number(received || 0); detail.pendingReadTotal = Number(total || 0)
            }
        }
        function onFinished(md5, path) {
            detail.refreshLocal()
            detail._finishRead("books", String(md5), detail.pendingReadGeneration)
        }
        function onFailed(md5, reason) { detail._failRead("books", String(md5), reason) }
        function onRemoved(md5) {
            detail.refreshLocal()
            if (detail.pendingReadTransport === "books" && String(md5) === detail.pendingReadAcquisitionId)
                detail._failRead("books", String(md5), "edition removed")
        }
    }
    Connections {
        target: detail.bookTorrentsRef
        ignoreUnknownSignals: true
        function onResolving(hash) {
            var id = String(hash).toLowerCase()
            if (detail.pendingReadTransport === "torrent" && id === detail.pendingReadAcquisitionId) {
                detail.pendingReadState = "resolving"; detail.pendingReadReceived = 0; detail.pendingReadTotal = 0
            }
        }
        function onProgress(hash, received, total) {
            var id = String(hash).toLowerCase()
            if (detail.pendingReadTransport === "torrent" && id === detail.pendingReadAcquisitionId) {
                detail.pendingReadState = "downloading"
                detail.pendingReadReceived = Number(received || 0); detail.pendingReadTotal = Number(total || 0)
            }
        }
        function onFinished(hash, path) {
            detail.refreshLocal()
            detail._finishRead("torrent", String(hash).toLowerCase(), detail.pendingReadGeneration)
        }
        function onFailed(hash, reason) { detail._failRead("torrent", String(hash).toLowerCase(), reason) }
        function onRemoved(hash) {
            detail.refreshLocal()
            var id = String(hash).toLowerCase()
            if (detail.pendingReadTransport === "torrent" && id === detail.pendingReadAcquisitionId)
                detail._failRead("torrent", id, "edition removed")
        }
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
                onTriggered: { detail._invalidateReadIntent(); detail.backRequested() }
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
                model: [ { g: "—", a: "min" }, { g: "⛶", a: "fs" }, { g: "⏻", a: "pow" } ]   // min · fullscreen · power (fullscreen rule removed 2026-07-20)
                delegate: Rectangle {
                    required property var modelData
                    width: 30; height: 30; radius: 8
                    color: sysMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.g; color: theme.inkDimmer; font.pixelSize: 14 }
                    MouseArea {
                        id: sysMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.a === "min") detail.minimizeRequested()
                            else if (modelData.a === "fs") detail.fullscreenRequested()
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
                        id: primaryCta
                        objectName: "biblioPrimaryRead"
                        width: parent.width; height: 50; radius: 13; color: theme.gold
                        opacity: enabled ? 1.0 : 0.5
                        enabled: detail.localPath.length > 0 || detail.pendingReadActive
                                 || detail.edLoading || detail.torLoading
                                 || detail.editions.length > 0 || detail.torrents.length > 0
                        Text {
                            anchors.centerIn: parent
                            text: detail.primaryReadLabel()
                            color: "#241a05"
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent; enabled: primaryCta.enabled
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: detail.readBook()
                        }
                    }
                    Text {
                        objectName: "biblioPrimaryReadStatus"
                        width: parent.width
                        text: detail.primaryReadStatus()
                        color: detail.readError.length ? "#e6a3a3" : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    LibraryButton {
                        width: parent.width
                        height: 50
                        radius: 13
                        world: "biblio"
                        entry: detail.collectionEntry()
                    }
                    // (The standalone Listen button is retired — Hemanth 2026-07-18: the reader IS
                    // the audiobook player. A downloaded audiobook auto-attaches; Read opens the
                    // book and the reader's HUD transport + Audio tab carry playback.)
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
                                readonly property var initialStatus: detail._status(
                                    detail.bookTorrentsRef, String(modelData.infoHash || "").toLowerCase())
                                property string dlState: String(initialStatus.state || "none") === "none"
                                                         ? "idle" : String(initialStatus.state || "idle")
                                property real dlPct: Number(initialStatus.total || 0) > 0
                                                     ? Number(initialStatus.received || 0) / Number(initialStatus.total) : 0
                                Connections {
                                    target: detail.bookTorrentsRef
                                    ignoreUnknownSignals: true
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
                                MouseArea { id: torMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: torRow.dlState === "done" ? Qt.ArrowCursor : Qt.PointingHandCursor
                                    onClicked: {
                                        if (torRow.dlState === "done" || torRow.dlState === "downloading"
                                                || torRow.dlState === "resolving") return
                                        detail.downloadTorrent(torRow.modelData)
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
                                // Explicit edition rows are acquisition controls; Read lives at the hero.
                                readonly property var initialStatus: detail._status(detail.booksRef, String(modelData.md5 || ""))
                                property string dlState: String(initialStatus.state || "none") === "none"
                                                         ? "idle" : String(initialStatus.state || "idle")
                                property real dlPct: Number(initialStatus.total || 0) > 0
                                                     ? Number(initialStatus.received || 0) / Number(initialStatus.total) : 0
                                Connections {
                                    target: detail.booksRef
                                    ignoreUnknownSignals: true
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
                                MouseArea { id: edMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: edRow.dlState === "done" ? Qt.ArrowCursor : Qt.PointingHandCursor
                                    onClicked: {
                                        if (edRow.dlState === "done" || edRow.dlState === "downloading"
                                                || edRow.dlState === "resolving") return
                                        detail.downloadEdition(edRow.modelData)
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Audiobook — paired from AudioBookBay; acquisition remains orthogonal to ebook Read ──
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
                                        // ✓ row → into the READER (the one audiobook surface): the shell
                                        // self-heals the pairing and its HUD/Audio tab carry playback.
                                        if (abRow.rowState === "done") {
                                            if (detail.localPath) detail.readRequested(detail.localPath, detail.book)
                                            return
                                        }
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
                                                detail.book.title || "", detail.book.author || "", bookId,
                                                detail.localPath || "")
                                            Collection.add("biblio", detail.collectionEntry())
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

    // Read-originated source choice. This is dependency UI inside the SAME foreground
    // consumption intent, not a separate acquisition command. Only tracked sources appear here.
    Rectangle {
        id: readChoiceShade
        anchors.fill: parent; z: 80
        visible: detail.readChoiceOpen
        color: Qt.rgba(0, 0, 0, 0.72)
        MouseArea { anchors.fill: parent }   // swallow the book page while the choice is active

        Glass {
            id: readChoiceCard
            anchors.centerIn: parent
            width: Math.min(620, detail.width - theme.margin * 2)
            height: Math.min(detail.height - 120, readChoiceCol.implicitHeight + 48)
            radius: 18
            backdrop: detail.backdrop

            Column {
                id: readChoiceCol
                x: 24; y: 24
                width: parent.width - 48
                spacing: 14
                Text {
                    text: "Choose an edition"
                    color: theme.ink; font.family: theme.display; font.pixelSize: 28
                }
                Text {
                    width: parent.width
                    text: "Read will continue into Reader2 when this exact edition is ready."
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: detail.readChoiceRows
                    delegate: Rectangle {
                        id: readChoiceRow
                        required property var modelData
                        width: readChoiceCol.width; height: 58; radius: 10
                        color: readChoiceMa.containsMouse ? Qt.rgba(1,1,1,0.08) : Qt.rgba(1,1,1,0.035)
                        border.width: 1; border.color: theme.edge
                        Column {
                            anchors.left: parent.left; anchors.leftMargin: 16
                            anchors.right: readChoiceVerb.left; anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter; spacing: 3
                            Text { width: parent.width; text: modelData.label || "Edition"
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                elide: Text.ElideRight }
                            Text { width: parent.width; text: modelData.meta || ""
                                textFormat: Text.AutoText; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                elide: Text.ElideRight }
                        }
                        Text { id: readChoiceVerb; anchors.right: parent.right; anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter; text: "Read"
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                        MouseArea { id: readChoiceMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: detail.chooseReadSource(modelData) }
                    }
                }
                Rectangle {
                    width: readChoiceCol.width; height: 42; radius: 10; color: "transparent"
                    border.width: 1; border.color: theme.edge
                    Text { anchors.centerIn: parent; text: "Cancel"; color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: detail.cancelReadChoice() }
                }
            }
        }
    }

    ScrollGlide { flick: page }
}
