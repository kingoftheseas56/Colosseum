// MangaSeries — the manga detail page. Colosseum series-view design (mock:
// agents/colosseum-series-mock.html, approved 2026-06-27). Floats over the wallpaper; metadata is
// inline (no glass pills); gold stays a sparing accent. Data is LIVE from the native engine via the
// `Manga` bridge:
//   title → WeebCentral search → (chapters + detail)
//                              → volumes(wcId, title) → the Comick volume DB / live scrape, gated
//         → AniList art()      → banner / cover / synopsis / genres / year / score
// THE SURFACE IS DECIDED BY THE DATA (2026-07-29 ruling, no toggle): a series whose volume list
// passes the completeness gate gets the permanent tankoban volume library, with the glass chapter
// table below reduced to the loose tail ("Latest chapters"); a series that does not qualify gets
// the plain flat WeebCentral chapter list. An estimated volume boundary is never shown.
// Opened from a Top-10 manga tile.

import QtQuick
import QtQuick.Controls
import "MangaVolumes.js" as Vol

Item {
    id: page
    objectName: "mangaSeriesPage"
    property Item backdrop
    property string seriesTitle: ""
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    // the READER's own chrome, distinct from this page's topbar: minimize = the comic session
    // drops to the Colosseum taskbar; close = the session is closed (Windows-window vocabulary).
    signal readerMinimizeRequested()
    signal readerFullscreenRequested()
    signal readerCloseRequested()
    // Back used to be swallowed locally (clearing openChapterId just revealed this page
    // underneath); it now raises upward like the other three verbs, so Main.qml can route it
    // through the same teardown authority Close already uses and land on the Tankoban library.
    signal readerBackRequested()

    // --- resolved state ---
    property string seriesId: ""
    property string seriesUrl: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int    year: 0
    property string synopsis: ""
    property var genres: []
    property int score: 0
    property var chaptersModel: []
    property bool loading: true
    property string errorMsg: ""
    // What the USER is shown. A source failing must not read as the page failing: once the
    // volumes are in, the shelf below is complete and unaffected, so a raw transfer error
    // (WeebCentral rate-limits with a 429) is both alarming and untrue. Derived, not baked
    // at error time, because the failure can land BEFORE the volumes do.
    readonly property string errorText: !errorMsg.length ? ""
        : (volumes.length
           ? "Couldn't reach WeebCentral just now, so the newest chapters aren't listed. The volumes below are unaffected."
           : errorMsg)

    // --- Tankoban mode ---
    // Tankoban mode is PERMANENT for qualified series (2026-07-29 ruling): the gate in
    // ComickCatalogClient emits a complete volume list or nothing at all, so
    // volumes.length IS the verdict. No toggle, no per-series persistence.
    property bool tankobanMode: volumes.length > 0
    property bool _tankobanPrepared: false
    // Whether the seeding above already had the chapter list. Volumes and chapters arrive
    // from DIFFERENT sources at different times (and WeebCentral can fail outright), so the
    // shelf seeds as soon as volumes land and re-seeds once chapters show up.
    property bool _tankobanPreparedWithChapters: false
    // A REUSED page item (openSeries/openSeriesAt switching series) must re-prepare
    // for the new series, or it would keep the old series' volumes. Reset the prepare
    // latch + the reader's volume model whenever the id changes.
    onSeriesIdChanged: {
        page._tankobanPrepared = false
        page._tankobanPreparedWithChapters = false
        page.tankobanReaderEntries = []
    }

    // --- seamless reveal gate ---
    // The page fires AniList (art) alongside the WeebCentral search, and the volume lookup as
    // soon as that search resolves (it is keyed by the WC id). Three sources, each at a
    // different speed. We must NEVER reveal the page until ALL three are in — otherwise the
    // user sees the flat chapter list / low-q art first and watches it reflow. _maybeReveal()
    // drops `loading` only when everything is ready, so the page appears once, already finished.
    // Every path closes the gate: volumesResult always fires exactly once per volumes() call
    // (gate-fail included); a 0-result search sets loading=false itself; revealGuard caps 12s.
    property bool chaptersReady: false
    property bool artReady: false
    property bool volumesReady: false
    function _maybeReveal() {
        if (chaptersReady && artReady && volumesReady) {
            loading = false; revealGuard.stop()
            page._prepareTankoban()
        }
    }
    // Hand the snapshot to the native volume service. Called as soon as VOLUMES land —
    // deliberately NOT gated on the chapter list, because the two come from different
    // sources: the shelf is built from our volume DB, while chapters come from
    // WeebCentral, which rate-limits (429) and can fail for a whole session. Waiting on
    // chapters meant a 429 emptied a shelf we already held in full (Vagabond: 38 volumes
    // in hand, blank page — 2026-07-30). prepareSeries builds one record per VOLUME row
    // and only attaches chapters afterwards, so seeding with none is safe and complete.
    // Re-seeds once (and only once) if chapters arrive later, so downloads get their
    // chapter ids; prepareSeries overwrites the series wholesale, so re-running is safe.
    function _prepareTankoban() {
        if (!page.volumes.length) return          // unqualified series: never seed the volume service
        if (typeof TankobanVolumes === "undefined" || !page.seriesId.length) return
        var haveChapters = page.chaptersModel.length > 0
        if (page._tankobanPrepared && (page._tankobanPreparedWithChapters || !haveChapters)) return
        page._tankobanPrepared = true
        page._tankobanPreparedWithChapters = haveChapters
        TankobanVolumes.prepareSeries({
            seriesId: page.seriesId, title: page.seriesTitle,
            author: page.author, aliases: []
        }, page.volumes, page.chaptersModel)
        page._rebuildTankobanEntries()
    }

    // --- reader entry kind: "manga" (chapters) or "tankoban" (volumes). The one
    //     MangaReader below reads BOTH; this picks its store + model per open. ---
    property string openEntryKind: "manga"
    // The reader's DESCENDING volume model (highest volume first). The library shelf
    // stays ascending; this separate copy preserves MangaReader's newest-first
    // crossing law so curIndex-1 is the next HIGHER volume. Rebuilt whenever the
    // service's canonical volumes change (covers/synopsis/ready-state lands).
    property var tankobanReaderEntries: []
    function _rebuildTankobanEntries() {
        var s = (typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null
        if (!s || !page.seriesId.length) { page.tankobanReaderEntries = []; return }
        var vols = s.volumesForSeries(page.seriesId) || []
        var out = []
        for (var i = 0; i < vols.length; i++)
            out.push({ id: String(vols[i].id),
                       number: vols[i].number,
                       name: (vols[i].title && String(vols[i].title).length) ? String(vols[i].title) : "" })
        out.sort(function (a, b) {
            var an = Number(a.number), bn = Number(b.number)
            if (!isNaN(an) && !isNaN(bn)) return bn - an     // DESCENDING by volume number
            return String(b.number) < String(a.number) ? -1 : (String(b.number) > String(a.number) ? 1 : 0)
        })
        page.tankobanReaderEntries = out
    }
    // The ONE path that raises the source picker — a single tile and a batch both
    // come through here, so the series identity is merged in exactly one place.
    function _openSources(ctx) {
        ctx.seriesId = page.seriesId
        ctx.seriesTitle = page.seriesTitle
        ctx.volumeNumber = ctx.number
        ctx.volumeTitle = ctx.title
        sourcesPage.show(ctx)
    }

    // A batch button was pressed: turn volume NUMBERS into volume ids and raise the
    // picker over all of them. Ownership is re-checked HERE, at the moment of the
    // press — the shelf may have finished a volume since the button was drawn, and
    // an owned volume must never be re-downloaded (design 2026-07-30, constraint 2).
    // The picker searches ids[0]: the engine has no range search.
    function _requestBatch(numbers, label) {
        var want = {}
        for (var i = 0; i < numbers.length; i++) want[Number(numbers[i])] = true
        var ids = [], nums = [], rows = tankLib.volumeRows || []
        for (var r = 0; r < rows.length; r++)
            if (want[Number(rows[r].number)] && String(rows[r].state) !== "ready") {
                ids.push(String(rows[r].id))
                nums.push(Number(rows[r].number))
            }
        if (!ids.length) return
        // volumeNumbers rides along so the picker can offer only the releases that
        // actually contain every volume asked for, without parsing volume ids.
        page._openSources({ "volumeId": ids[0], "volumeIds": ids, "volumeNumbers": nums,
                            "number": "", "title": String(label), "cover": "" })
    }

    // Open a downloaded volume in the reader (the library's Downloaded->Open action).
    function _openVolume(volumeId) {
        var id = String(volumeId)
        if (!id.length) return
        page._rebuildTankobanEntries()
        var lbl = ""
        var ents = page.tankobanReaderEntries
        for (var i = 0; i < ents.length; i++)
            if (String(ents[i].id) === id) { lbl = ents[i].name; break }
        page.openEntryKind = "tankoban"                       // set BEFORE the id (store/model bind on it)
        page.openChapterLabel = lbl.length ? lbl : ("Vol. " + id)
        page.openChapterId = id
    }
    // The reader hit a NOT-ready volume (crossed off the end, or the download button):
    // leave the reader and open THAT volume's full-screen source picker via the library
    // (chooseSource emits sourcesRequested, which opens MangaTankobanSourcesPage).
    function _handleVolumeSource(entryId) {
        page.openChapterId = ""
        page.openChapterLabel = ""
        page.openEntryKind = "manga"
        tankLib.chooseSource(String(entryId))
    }
    // Continue/session resume of a saved tankoban record: open the saved volume through
    // the shared reader. Mode is DERIVED now — a resumable tankoban record implies a
    // qualified series, and if the series ever loses qualification the reader still
    // opens the downloaded volume by id.
    function resumeTankobanVolume(volumeId) {
        if (!volumeId || !String(volumeId).length) return
        page._openVolume(String(volumeId))
    }

    // Keep the reader's descending volume model current as the service learns more.
    Connections {
        target: (typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null
        ignoreUnknownSignals: true
        function onVolumesChanged(sid) { if (sid === page.seriesId) page._rebuildTankobanEntries() }
    }

    // --- volumes (Comick volume DB via MangaVolumes.js; complete ranges or none — gated) ---
    property var volumes: []                                  // [{number,cover,startNum,endNum,chapterStart,chapterEnd}]
    property var volGroups: Vol.group(chaptersModel, volumes) // { options:[{key,label}], byKey:{} }
    // qualified series: the chapter section shows ONLY the loose tail past the last
    // volume (group()'s X bucket) — an ongoing series' "Latest chapters".
    // unqualified series: the full flat WeebCentral list, exactly as before.
    property var visibleChapters: loading ? []
        : (page.tankobanMode ? ((volGroups.byKey && volGroups.byKey.X) || []) : chaptersModel)

    // ── the facts column beside the synopsis (Theatre's key/value stack) ─────
    // Only facts we actually hold. Nothing is padded to fill the column: a row
    // that has no answer simply is not there.
    readonly property var factRows: {
        var out = []
        if (page.author.length) out.push({ "k": "Author", "v": page.author })
        if (page.status.length) out.push({ "k": "Status", "v": page.status })
        if (page.tankobanMode && tankLib.volumeRows.length) {
            var rows = tankLib.volumeRows, owned = tankLib.ownedCount
            out.push({ "k": "Volumes", "v": String(rows.length) })
            out.push({ "k": "On this device", "v": owned
                       ? (owned + (owned === 1 ? " volume" : " volumes")) : "None yet" })
        }
        if (page.chaptersModel.length)
            out.push({ "k": "Chapters", "v": String(page.chaptersModel.length) })
        return out
    }

    // ── the hero's Read promise ──────────────────────────────────────────────
    // Theatre's button names the episode it will play. Ours names the volume: the
    // one you were part-way through, else the first you own, else the first book.
    // Nothing here invents a target — if there is no volume at all the button
    // falls back to the chapter list, which is what an unqualified series shows.
    readonly property string readCtaLabel: {
        if (!tankLib.continueVolumeId.length) return "Read"
        var rows = tankLib.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === tankLib.continueVolumeId)
                return "Continue Vol. " + rows[i].number
        return "Read"
    }
    function readPrimary() {
        // resume beats everything — it is the only target the user already chose
        if (tankLib.continueVolumeId.length) { page._openVolume(tankLib.continueVolumeId); return }
        var rows = tankLib.volumeRows || []
        for (var i = 0; i < rows.length; i++)                    // first book on disk
            if (String(rows[i].state) === "ready") { page._openVolume(String(rows[i].id)); return }
        if (rows.length) { tankLib.chooseSource(String(rows[0].id)); return }   // fetch volume 1
        var chs = page.visibleChapters                          // unqualified series: first chapter
        if (chs && chs.length) {
            page.openEntryKind = "manga"
            page.openChapterId = String(chs[0].id)
            page.openChapterLabel = (chs[0].name && String(chs[0].name).length)
                ? String(chs[0].name) : ("Chapter " + (chs[0].number || ""))
        }
    }

    function collectionEntry() {
        return { "id": page.seriesTitle, "type": "manga",
                 "title": page.seriesTitle, "cover": page.cover, "payload": ({}) }
    }

    Theme { id: theme }

    onSeriesTitleChanged: resolve()
    Component.onCompleted: if (seriesTitle.length) resolve()

    function resolve() {
        loading = true; errorMsg = ""
        seriesId = ""; banner = ""; cover = ""; author = ""; status = ""; year = 0
        synopsis = ""; genres = []; score = 0; chaptersModel = []
        volumes = []
        chaptersReady = false; artReady = false; volumesReady = false
        _tankobanPrepared = false
        if (seriesTitle.length) {
            revealGuard.restart()        // never hang on a dead source — reveal what we have after N s
            Manga.search(seriesTitle)    // → chapters + WeebCentral detail (then volumes, keyed by its id)
            Manga.art(seriesTitle)       // → AniList banner / cover / synopsis / genres / year
        }
    }

    // Safety net: if a source never answers, reveal after this timeout rather than spin forever.
    Timer { id: revealGuard; interval: 12000; repeat: false; onTriggered: page.loading = false }

    function fmtDate(ms) {
        var n = Number(ms)
        if (!n || n <= 0) return ""
        return new Date(n).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    Connections {
        target: Manga
        function onSearchResults(results) {
            if (results.length === 0) {
                page.errorMsg = "“" + page.seriesTitle + "” wasn’t found on WeebCentral."
                page.loading = false
                return
            }
            var r = results[0]
            page.seriesId = r.id; page.seriesUrl = r.url
            // NOTE: deliberately do NOT take WeebCentral's low-res cover for the banner — that was
            // the source of the "low-q art that changes after a while" swap. The banner comes only
            // from AniList (hi-res), set in onArtResult.
            page.author = r.author; page.status = r.status
            // volume structure needs the WC id (it is the volume DB's key) — fire it
            // as soon as the search resolves
            Manga.volumes(r.id, r.title)
            Manga.chapters(r.id)
            Manga.detail(r.id, r.url, r.title, r.cover)
        }
        // chapters re-seed the shelf (they carry the ids downloads need) but never gate it
        function onChaptersResults(chs) {
            page.chaptersModel = chs; page.chaptersReady = true
            page._prepareTankoban(); page._maybeReveal()
        }
        function onDetailResult(d) {
            // AniList is the source for synopsis + genres (onArtResult). WeebCentral detail only
            // contributes status + author — NOT its plainer description (AniList's reads better).
            if (d.status && d.status.length) page.status = d.status
            if (d.author && d.author.length) page.author = d.author
        }
        function onArtResult(a) {
            if (a.banner && a.banner.length) page.banner = a.banner
            if (a.cover && a.cover.length) page.cover = a.cover
            if (a.description && a.description.length) page.synopsis = a.description
            if (a.genres && a.genres.length) page.genres = a.genres
            if (a.score) page.score = a.score
            if (a.year) page.year = a.year
            page.artReady = true; page._maybeReveal()
        }
        // volumes seed the shelf IMMEDIATELY — they are the shelf, and they arrive from our
        // own volume DB, which does not depend on WeebCentral being reachable
        function onVolumesResult(d) {
            page.volumes = Vol.fromEngine(d.volumes || []); page.volumesReady = true
            page._prepareTankoban(); page._maybeReveal()
        }
        // A source failing must not cost the user a shelf we already hold. WeebCentral
        // rate-limits (429) and its raw transfer error is not something to put on screen;
        // when the volumes are in, say what it actually costs him (the chapter tail) in
        // plain words and keep the page. Only a page with nothing to show reports hard.
        function onEngineError(msg) {
            if (!page.loading) return
            page.errorMsg = msg
            page.loading = false; revealGuard.stop()
        }
    }

    // ===================== visual tree =====================
    MouseArea { anchors.fill: parent }                          // absorb clicks from the world page below

    // Match Theatre's pitch-black series surface while retaining a faint wallpaper relationship.
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
        opacity: 0.5
    }
    // adaptive scrim — keeps text + chrome legible over any wallpaper, darker toward the chapter list
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }
        }
    }

    // ---- top scrim so the back/window controls read against ANY background (bright banner or dark) ----
    ChromeScrim { z: 16 }

    // ---- ‹ Back (pinned, floats over the banner) ----
    BackAction {
        id: backBtn
        x: theme.margin; y: 28; z: 20
        onTriggered: page.backRequested()
    }

    // ---- window controls (minimize / power) — the SAME icons as the home/world top bar ----
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.fullscreenRequested()
            }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested() }
        }
    }

    // ---- the page: one vertical scroll; banner → synopsis → volume shelf → glass chapter table ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: flick }

        // The whole page stays invisible until fully assembled, then fades in as one finished piece.
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            // ── BANNER HERO (full-bleed art; content inset to the margin) ──
            Item {
                width: parent.width
                height: 360

                Image {
                    id: bannerImg
                    anchors.fill: parent
                    source: page.banner.length ? page.banner : page.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                    // soft fade in when the pixels arrive — never a hard pop, even on first load
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                // wash the banner down into the page so it reads as one surface (IP color stays up top)
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.035, 0.055, 0.15) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.03, 0.035, 0.05, 0.45) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.92) }
                    }
                }

                Column {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin; anchors.bottomMargin: 30
                    spacing: 12

                    Text {
                        text: "Manga · Tankoban"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                        font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                    }
                    Text {
                        width: parent.width
                        text: page.seriesTitle
                        color: theme.ink; font.family: theme.display; font.pixelSize: 64
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
                    }
                    // INLINE metadata — author (bright) · status · year · ★score · genres. No glass pills.
                    Row {
                        spacing: 11
                        Text { visible: page.author.length; text: page.author
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.author.length && (page.status.length || page.year)
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.status.length; text: page.status
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.status.length && page.year
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.year > 0; text: page.year
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.score > 0
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.score > 0; text: "★ " + page.score
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.genres.length > 0
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.genres.length > 0
                            text: page.genres.slice(0, 3).join(" · ")
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                    }
                    // Primary CTA — Read. Theatre names the exact episode its button will
                    // play ("Watch S1 · E3"); the same promise here names the volume, so the
                    // button never lies about where it lands.
                    Row {
                        spacing: 12
                        topPadding: 8
                        Rectangle {
                            width: readRow.implicitWidth + 40; height: 42; radius: 11; color: theme.gold
                            Row {
                                id: readRow; anchors.centerIn: parent; spacing: 9
                                PlayerIcon { kind: "play"; ink: "#1a1306"; width: 16; height: 16; iconSize: 14
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: page.readCtaLabel; color: "#1a1306"; font.family: theme.ui; font.pixelSize: 14
                                    font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onEntered: parent.opacity = 0.92; onExited: parent.opacity = 1.0
                                onClicked: page.readPrimary() }
                        }

                        LibraryButton {
                            world: "tankoban"
                            entry: page.collectionEntry()
                        }
                    }
                }
            }

            // ── synopsis + facts, Theatre's two-column band (56px gutter) ──
            Row {
                x: theme.margin
                spacing: 56
                Text {
                    visible: page.synopsis.length > 0
                    width: 580
                    text: page.synopsis
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15
                    lineHeight: 1.5; wrapMode: Text.WordWrap
                    topPadding: 22; bottomPadding: 6
                }
                Column {
                    spacing: 10
                    topPadding: 22
                    visible: page.factRows.length > 0 && page.width > 1040
                    Repeater {
                        model: page.factRows
                        Row {
                            id: factRow
                            required property var modelData
                            spacing: 18
                            Text { text: factRow.modelData.k; color: theme.inkDim; width: 104
                                   font.family: theme.ui; font.pixelSize: 13 }
                            Text { text: factRow.modelData.v; color: theme.ink
                                   font.family: theme.ui; font.pixelSize: 13 }
                        }
                    }
                }
            }

            // ── VOLUMES header — the shelf had none, so the page read as starting
            //    abruptly out of the synopsis. Theatre labels its episode run; so do we. ──
            Item {
                width: parent.width
                height: 60
                visible: page.tankobanMode
                Row {
                    x: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    Text {
                        text: "Volumes"
                        color: theme.ink; font.family: theme.display; font.pixelSize: 22
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: {
                            var n = tankLib.volumeRows.length
                            if (!n) return ""
                            var owned = tankLib.ownedCount
                            return owned > 0 ? (n + " books · " + owned + " on this device")
                                             : (n + " books")
                        }
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // NOTE: the batch controls used to live here, anchored to this
                // header's right edge. They were CUT OFF by the window (eyes-on
                // 2026-07-31) because `primaryBatch` was a Row with a MouseArea
                // child — a positioner lays a MouseArea out as an item, inflating
                // the Row past the viewport. The same trap TheatreSeries.qml:1186
                // documents. They now live on the shelf's own ledger header, in
                // Theatre's "Download season" position.
            }

            // ── THE VOLUME LIBRARY — the permanent surface for a gate-qualified series ──
            // service defaults to the native TankobanVolumes context property. A Downloaded->Open
            // action opens that volume through the SAME reader below (volume model + injected store).
            MangaTankobanLibrary {
                id: tankLib
                width: parent.width
                visible: page.tankobanMode
                seriesId: page.seriesId
                // the live chapter list is what a volume cover is derived FROM: the
                // shelf asks Downloads.fetchThumb for the first page of each volume's
                // first chapter, exactly as a chapter row gets its own thumbnail
                chapters: page.chaptersModel
                onOpenVolumeRequested: (volumeId) => page._openVolume(volumeId)
                // "Choose source" -> the full-screen picker. Merge the series identity
                // (the library only knows the volume) and open the overlay below.
                onSourcesRequested: (ctx) => page._openSources(ctx)
                // One press, N volumes — the same picker, opened over a batch.
                onBatchRequested: (numbers, label) => page._requestBatch(numbers, label)
            }

            // ── CHAPTER TABLE — the floating glass OS-widget.
            //    Qualified series: the loose tail past the last volume ("Latest chapters"),
            //      sitting BELOW the shelf as a footnote (his ruling 2026-07-30).
            //    Unqualified series: the whole flat run — and with no shelf above it, this
            //      card must stay exactly where it has always sat, directly under the
            //      synopsis. That is why the air above it is CONDITIONAL, not built in. ──
            Item {
                id: chapterSection
                width: parent.width
                // air between the volume shelf and this card; ZERO when there is no shelf,
                // so an unqualified series' geometry is unchanged by the reorder
                readonly property int topGap: page.tankobanMode ? 30 : 0
                height: chTable.height + chapterSection.topGap + 24
                visible: page.visibleChapters.length > 0

                Glass {
                    id: chTable
                    x: theme.margin
                    y: chapterSection.topGap
                    width: parent.width - 2 * theme.margin
                    height: tableInner.height
                    radius: 18
                    backdrop: page.backdrop
                    track: flick.contentY               // recompute blur as the page scrolls

                    Column {
                        id: tableInner
                        width: parent.width
                        // header
                        Item {
                            width: parent.width; height: 58
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 24
                                anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                Text {
                                    // honest header: on a qualified series this section is ONLY the
                                    // tail past the last volume; otherwise it is the whole run
                                    text: page.tankobanMode ? "Latest chapters" : "Chapters"
                                    color: theme.ink
                                    font.family: theme.display; font.pixelSize: 19; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: page.visibleChapters.length + " chapters"; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                // per-volume download — lives with the volume it acts on (DL wired in a later layer)
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: dlVolRow.implicitWidth + 26; height: 30; radius: 8
                                    color: dlVolMa.containsMouse ? theme.glassHi : theme.glassTint
                                    border.width: 1
                                    border.color: dlVolMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : theme.edge
                                    Row {
                                        id: dlVolRow; anchors.centerIn: parent; spacing: 7
                                        Text { text: "↓"; color: theme.ink; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                                        Text {
                                            // says what it downloads: exactly what this section lists —
                                            // the loose tail, or (flat state) the whole run
                                            text: page.tankobanMode ? "Download latest" : "Download all"
                                            color: theme.inkDim; font.family: theme.ui
                                            font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    MouseArea { id: dlVolMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof Downloads === "undefined") return
                                            var chs = page.visibleChapters
                                            for (var i = 0; i < chs.length; i++) {
                                                var id = String(chs[i].id || "")
                                                if (!id.length) continue
                                                var lbl = (chs[i].name && String(chs[i].name).length)
                                                          ? chs[i].name : ("Chapter " + (chs[i].number || ""))
                                                Downloads.downloadChapter(id, page.seriesId, page.seriesTitle, lbl)
                                            }
                                            Collection.add("tankoban", page.collectionEntry())
                                        } }
                                }
                            }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                        }
                        // chapter rows (selected volume → bounded count → a Repeater is fine)
                        Repeater {
                            model: page.visibleChapters
                            delegate: Item {
                                id: row
                                required property var modelData
                                width: tableInner.width; height: 156

                                // per-row download state, kept live via the Downloads signals
                                property string chId: String(row.modelData.id || "")
                                property string dlState: "none"   // none | queued | downloading | done | error
                                property int dlDone: 0
                                property int dlTotal: 0
                                readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                property string liveThumb: ""   // first-page url for an UNdownloaded chapter (scraped)
                                // chapter thumbnail = its FIRST page: downloaded -> local file (instant),
                                // else the scraped first-page url resolved via Downloads.fetchThumb.
                                readonly property string thumbUrl: dlState === "done" ? row.firstLocalUrl() : row.liveThumb
                                function firstLocalUrl() {
                                    if (typeof Downloads === "undefined") return ""
                                    var lp = Downloads.localPages(row.chId)
                                    return (lp && lp.length) ? lp[0].url : ""
                                }
                                function chLabel() {
                                    return (row.modelData.name && String(row.modelData.name).length)
                                        ? row.modelData.name : ("Chapter " + (row.modelData.number || ""))
                                }
                                function statusLine() {
                                    if (dlState === "done") return "● Downloaded"
                                    if (dlState === "queued") return "Queued…"
                                    if (dlState === "downloading")
                                        return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%") : "Downloading…"
                                    if (dlState === "error") return "⚠ Failed — tap to retry"
                                    return ""
                                }
                                function openReader() {
                                    page.openEntryKind = "manga"   // chapters always read as manga
                                    page.openChapterId = row.chId
                                    page.openChapterLabel = row.chLabel()
                                }
                                function startDownload() {
                                    if (typeof Downloads === "undefined" || !row.chId.length) return
                                    row.dlState = "queued"
                                    Downloads.downloadChapter(row.chId, page.seriesId, page.seriesTitle, row.chLabel())
                                    Collection.add("tankoban", page.collectionEntry())
                                }
                                // download-fed: tap reads a downloaded chapter, else downloads it (the reader only opens what's on disk)
                                function primary() {
                                    if (row.dlState === "done") row.openReader()
                                    else if (!row.inFlight) row.startDownload()
                                }
                                function refreshDl() {
                                    if (typeof Downloads === "undefined") return
                                    var st = Downloads.statusOf(row.chId)
                                    row.dlState = st.state; row.dlDone = st.done; row.dlTotal = st.total
                                }
                                function requestThumb() {
                                    if (typeof Downloads !== "undefined") Downloads.fetchThumb(page.seriesId, row.chId)
                                }
                                Component.onCompleted: { refreshDl(); requestThumb() }
                                Connections {
                                    target: typeof Downloads !== "undefined" ? Downloads : null
                                    function onProgress(cid, done, total) {
                                        if (cid !== row.chId) return
                                        row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total
                                    }
                                    function onFinished(cid) { if (cid === row.chId) row.dlState = "done" }
                                    function onFailed(cid, reason) { if (cid === row.chId) row.dlState = "error" }
                                    function onThumbReady(cid, url) { if (cid === row.chId && url.length) row.liveThumb = url }
                                    function onRemoved(cid) {
                                        if (cid !== row.chId) return
                                        row.dlState = "none"; row.liveThumb = ""; row.requestThumb()
                                    }
                                }

                                Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent" }

                                // thumbnail (portrait) — first page once downloaded, numbered placeholder otherwise
                                Item {
                                    id: thumb
                                    anchors.left: parent.left; anchors.leftMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 100; height: 140
                                    Rectangle {
                                        anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1
                                        border.color: row.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        Text { anchors.centerIn: parent; visible: thumbImg.status !== Image.Ready
                                            text: row.modelData.number || "?"; color: theme.inkDimmer
                                            font.family: theme.display; font.pixelSize: 30 }
                                    }
                                    Image { id: thumbImg; anchors.fill: parent; anchors.margins: 1
                                        source: row.thumbUrl; visible: status === Image.Ready
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                        sourceSize.width: 280 }
                                }

                                // title + status subtitle
                                Column {
                                    anchors.left: thumb.right; anchors.leftMargin: 16
                                    anchors.right: trailing.left; anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                    Text { width: parent.width; text: row.chLabel()
                                        color: rowMa.containsMouse ? theme.gold : theme.ink
                                        font.family: theme.ui; font.pixelSize: 17; elide: Text.ElideRight }
                                    Text { width: parent.width; text: row.statusLine(); visible: text.length > 0
                                        color: row.dlState === "done" ? theme.gold
                                             : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                                        font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                                }

                                // trailing control: ✓→✕ delete (done) · ✕ cancel (in-flight) · ↓/↻ download/retry
                                Item {
                                    id: trailing
                                    anchors.right: parent.right; anchors.rightMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 36; height: 36
                                    Rectangle { anchors.fill: parent; radius: 18
                                        color: trMa.containsMouse ? theme.glassHi : "transparent" }
                                    Text {
                                        anchors.centerIn: parent
                                        text: row.dlState === "done" ? (trMa.containsMouse ? "✕" : "✓")
                                            : row.inFlight ? "✕"
                                            : row.dlState === "error" ? "↻" : "↓"
                                        color: (row.dlState === "done" && trMa.containsMouse) ? "#e6a3a3"
                                             : row.dlState === "done" ? theme.gold
                                             : trMa.containsMouse ? theme.gold : theme.inkDim
                                        font.pixelSize: 16
                                        font.weight: (row.dlState === "done" && !trMa.containsMouse) ? Font.Bold : Font.Normal
                                    }
                                    MouseArea {
                                        id: trMa; anchors.fill: parent; hoverEnabled: true; z: 5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof Downloads === "undefined") return
                                            if (row.dlState === "done") Downloads.deleteChapter(row.chId)
                                            else if (row.inFlight) Downloads.cancelDownload(row.chId)
                                            else row.startDownload()
                                        }
                                    }
                                }

                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                    color: Qt.rgba(1,1,1,0.05); visible: row.y + row.height < tableInner.height }
                                MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: row.primary() }
                            }
                        }
                    }
                }
            }

            // post-reveal error (inset). Alarm red only when the page genuinely has nothing;
            // a shelf that loaded fine gets a quiet dimmed note instead.
            Text {
                visible: !page.loading && page.errorText.length > 0
                x: theme.margin
                width: Math.min(880, parent.width - 2 * theme.margin)
                wrapMode: Text.WordWrap
                text: page.errorText
                color: page.volumes.length ? theme.inkDimmer : "#e6a3a3"
                font.family: theme.ui; font.pixelSize: 13
                topPadding: 18
            }

            Item { width: 1; height: 70 }   // bottom breathing room
        }
    }

    ScrollGlide { flick: flick }

    // ---- clean loading state ----
    // Shown while the page assembles; it fades out as the finished page fades in (see Flickable opacity),
    // so the user sees one calm transition — never the flat list or low-q art being built in front of them.
    Column {
        id: loadingState
        visible: page.loading
        opacity: page.loading ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: 14
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.seriesTitle
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.errorText.length ? page.errorText : "Loading…"
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }
    }

    // ---- reader overlay: opened from a chapter row (the recreated Tankoban reader) ----
    // Direct child (NOT a Loader+inline-Component): inside a nested Component the outer
    // `page` id does not resolve, so every page.* binding was undefined and onBackRequested
    // silently threw — the reader could never close. As a direct child, `page` resolves
    // like everywhere else. Idle cost is nil: with no chapterId it fetches nothing and
    // visible:false removes it from input.
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: page.seriesId
        seriesCover: page.cover
        // chapter mode: default store (Downloads) + the flat chapter list.
        // tankoban mode: the native volume service + the DESCENDING volume model.
        entryKind: page.openEntryKind
        entryLabelPrefix: page.openEntryKind === "tankoban" ? "Vol. " : ""
        pageStore: page.openEntryKind === "tankoban"
                   ? ((typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null)
                   : null
        chapters: page.openEntryKind === "tankoban" ? page.tankobanReaderEntries : page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        // Do NOT clear openChapterId here — Main.qml's closeComicReader() reads it (still live)
        // to pick the right teardown lane. Clearing it first would make that routing fall
        // through to the wrong branch.
        onBackRequested: page.readerBackRequested()
        onSourceRequested: (entryId) => page._handleVolumeSource(entryId)
        onMinimizeRequested: page.readerMinimizeRequested()
        onFullscreenRequested: page.readerFullscreenRequested()
        onCloseRequested: page.readerCloseRequested()
    }

    // ---- full-screen "Choose source" picker: opened from a volume row (or the reader
    //      escape) via the library's sourcesRequested. A sibling of the reader (they're
    //      mutually-exclusive overlays); acquisition rides the native TankobanVolumes
    //      service under the original volumeId. ----
    MangaTankobanSourcesPage {
        id: sourcesPage
        anchors.fill: parent
        z: 70
        backdrop: page.backdrop
    }
}
