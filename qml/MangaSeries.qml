// MangaSeries — the manga detail page. Colosseum series-view design (mock:
// agents/colosseum-series-mock.html, approved 2026-06-27). Floats over the wallpaper; metadata is
// inline (no glass pills); gold stays a sparing accent.
//
// Catalogue-independence Slice 2 (amended 2026-08-20): identity + masthead are now SYNCHRONOUS
// and provider-free — malId (Discover) or a single exact matchByTitle() candidate resolves via
// MalCatalog, and mangaById() supplies title/score/synopsis/poster/authors/genres straight from
// the baked db. No WeebCentral/Comick/AniList call remains on this page's browse path (purity
// law, spec §2.1). seriesId is "mal:"+malId on every resolved page, "" when identity never
// resolves (ambiguous/unknown title) — see resolve()/_applyCatalogRow() below.
//
// Catalogue-independence Slice 3 (2026-08-20): the volume shelf is now seeded straight from
// TankobanCatalog.volumes(resolvedMalId) — a count-only shelf with baked covers, no Comick/
// WeebCentral volume-db ladder feeding it at all (purity law). See _prepareTankoban() below.
// Opened from a Top-10 manga tile.

import QtQuick

Item {
    id: page
    objectName: "mangaSeriesPage"
    property Item backdrop
    property string seriesTitle: ""
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    // R1 (2026-08-21): the sources picker's honest empty-state route, forwarded
    // straight through to the host (Main.qml -> win.openExtensionsPage()) — this
    // page never opens Extensions itself.
    signal openExtensionsRequested()
    // the READER's own chrome, distinct from this page's topbar: minimize = the comic session
    // drops to the Colosseum taskbar; close = the session is closed (Windows-window vocabulary).
    signal readerMinimizeRequested()
    signal readerFullscreenRequested()
    signal readerCloseRequested()
    // Back used to be swallowed locally (clearing openChapterId just revealed this page
    // underneath); it now raises upward like the other three verbs, so Main.qml can route it
    // through the same teardown authority Close already uses and land on the Tankoban library.
    signal readerBackRequested()

    // --- catalogue-independence identity seams (Slice 2, amended 2026-08-20) ---
    // Injectable properties defaulting to the context objects (the TankobanDiscoverPage
    // pattern) so this page constructs bare in a harness. Production never overrides these.
    property var malCatalogRef: (typeof MalCatalog !== "undefined") ? MalCatalog : null
    property var tankobanCatalogRef: (typeof TankobanCatalog !== "undefined") ? TankobanCatalog : null
    property var tankobanVolumesRef: (typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null

    // Arc 19: a Read that needs transport stays a foreground consumption intent until
    // this exact series/volume becomes readable, the user leaves, or a newer Read wins.
    property int _readIntentGeneration: 0
    property string pendingReadVolumeId: ""
    property string pendingReadSeriesId: ""
    property bool _pendingReadViaSources: false
    readonly property bool pendingReadActive: page.pendingReadVolumeId.length > 0

    // --- resolved state ---
    property string malId: ""    // Slice C: Discover card's MAL id, when the series was opened from one
    property string requestedVolumeNumber: "" // optional exact-volume landing from a universe catalogue
    onRequestedVolumeNumberChanged: Qt.callLater(page.openRequestedVolume)
    // The catalogue-resolved numeric identity (0 = unresolved). seriesId below is derived
    // from this ("mal:"+resolvedMalId) the moment a single row is found; a title that never
    // resolves to exactly one candidate leaves both at their unresolved value — the honest
    // shelf-less page, never a guess (identity-key vocabulary, plan Standing constraints).
    property int resolvedMalId: 0
    property var catalogRow: ({})   // the mangaById() row backing the masthead facts
    property string seriesId: ""
    property string seriesUrl: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int    year: 0
    property string synopsis: ""
    property var genres: []
    // real, not int: MAL scores are one-decimal (9.1) — an int property silently truncated
    // the masthead's ★ score (found live wiring this slice's harness). MangaReadingRoom's
    // own `score` property was already `real`; this was the mismatch.
    property real score: 0
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

    // --- the truthful primary button (Slice 2, amended) ---
    // hasShelf reads the baked, provider-free catalogue count (Slice 1's TankobanCatalog) —
    // the honest "does this series have a known shelf at all" fact, independent of whether
    // any volume has been downloaded yet. Slice 3 owns actually SEEDING the shelf from this
    // source; here it only backs the button truth-table + the masthead scalar.
    readonly property var _tcInfo: (page.resolvedMalId > 0 && page.tankobanCatalogRef
                                     && page.tankobanCatalogRef.ready())
        ? (page.tankobanCatalogRef.seriesInfo(page.resolvedMalId) || {}) : ({})
    readonly property bool hasShelf: (_tcInfo.volumeCount || 0) > 0
    // vol1Ready reads TankobanVolumes directly (no new wiring — the existing seriesId-keyed
    // read every other TankobanVolumes consumer in this file already performs).
    readonly property bool _vol1Ready: {
        var svc = page.tankobanVolumesRef
        if (!svc || !page.seriesId.length) return false
        var rows = svc.volumesForSeries(page.seriesId) || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].number) === "1" && String(rows[i].state) === "ready") return true
        return false
    }
    readonly property string primaryAction: page._vol1Ready ? "open" : (page.hasShelf ? "get" : "search")
    // A REUSED page item (openSeries/openSeriesAt switching series) must re-prepare
    // for the new series, or it would keep the old series' volumes. Reset the prepare
    // latch + the reader's volume model whenever the id changes.
    onSeriesIdChanged: {
        page._invalidateReadIntent()
        page._tankobanPrepared = false
        page.tankobanReaderEntries = []
    }

    // TB-002 legacy re-file guard. A page instance attempts the re-file for a given
    // seriesId at most once; a fresh page open re-checks and self-heals. Instance-local
    // is correct — Collection state can change between page opens, so a stale "already
    // done" across opens would miss a save that landed in between.
    property string _refiledFor: ""
    function _refileLegacyCollectionEntryIfNeeded() {
        var sid = page.seriesId
        if (!sid || !sid.length) return
        if (page._refiledFor === sid) return        // one-shot per seriesId on this page
        page._refiledFor = sid

        if (typeof Collection === "undefined") return
        var items = Collection.items("tankoban")
        var legacy = null
        for (var i = 0; i < items.length; i++) {
            var e = items[i]
            if (!e) continue
            if (e.type !== "manga") continue
            // A legacy entry for THIS series: its id is the title AND the title matches,
            // AND it is NOT already the canonical id (id !== seriesId).
            if (String(e.id) === page.seriesTitle && String(e.id) !== sid) { legacy = e; break }
        }
        if (!legacy) return

        // Re-file in add-verify-remove order. An interruption must NEVER leave the user's
        // save deleted: the canonical entry lands and is confirmed before the legacy one
        // is touched.
        var canonicalAddedAt = Number(legacy.addedAt) || 0
        // If a canonical entry already coexists (both present during the re-file window),
        // keep the OLDER valid addedAt on it so the save's "date added" position is kept.
        for (var j = 0; j < items.length; j++) {
            var ce = items[j]
            if (!ce) continue
            if (ce.type === "manga" && String(ce.id) === sid) {
                var existingAt = Number(ce.addedAt) || 0
                if (!canonicalAddedAt || (existingAt && existingAt < canonicalAddedAt))
                    canonicalAddedAt = existingAt
            }
        }
        var canonical = { "id": sid, "type": "manga",
                          "title": page.seriesTitle, "cover": page.cover,
                          "payload": ({}), "addedAt": canonicalAddedAt }
        Collection.add("tankoban", canonical)        // upsert; preserves our non-zero addedAt
        // Verify the canonical entry landed before removing the legacy one.
        if (Collection.has("tankoban", sid))
            Collection.remove("tankoban", String(legacy.id))
    }

    // Hand the catalogue's baked volume list to the native volume service (catalogue-
    // independence Slice 3, 2026-08-20). The Comick/WeebCentral volume-db ladder no
    // longer feeds the shelf (purity law, spec §2.1) — this is the SOLE seed path now.
    // TankobanCatalog.volumes(malId) already returns numeric-ordered rows with STRING
    // numbers ("1".."N", synthesized from the baked count, baked cover/name overlaid
    // where the harvest has landed) — exactly the VolumeRecord shape prepareSeries
    // wants, modulo the name->title rename. Chapter rows are always [] on this path:
    // MangaTankobanLogic already assembles a chapterless VolumeRecord per row (proven
    // by the "WHOLE SHELF with no chapters" case in manga_tankoban_logic_harness.cpp);
    // no C++ change was needed for this slice. One-shot per seriesId — onSeriesIdChanged
    // resets the latch for a reused page instance.
    function _prepareTankoban() {
        if (!page.hasShelf) return                 // no known catalogue count: never seed
        if (typeof TankobanVolumes === "undefined" || !page.seriesId.length) return
        if (page._tankobanPrepared) return
        page._tankobanPrepared = true
        var tc = page.tankobanCatalogRef
        var rows = (tc && tc.ready()) ? (tc.volumes(page.resolvedMalId) || []) : []
        var vols = []
        for (var i = 0; i < rows.length; i++)
            vols.push({ number: rows[i].number, cover: rows[i].cover || "",
                        title: rows[i].name || "" })
        TankobanVolumes.prepareSeries({
            seriesId: page.seriesId, title: page.seriesTitle,
            author: page.author, aliases: []
        }, vols, [])
        page._rebuildTankobanEntries()
        Qt.callLater(page.openRequestedVolume)
    }

    function openRequestedVolume() {
        var want = String(page.requestedVolumeNumber || "")
        var svc = page.tankobanVolumesRef
        if (!want.length || !svc || !page.seriesId.length) return
        var rows = svc.volumesForSeries(page.seriesId) || []
        for (var i = 0; i < rows.length; i++) {
            if (String(rows[i].number) === page.requestedVolumeNumber) {
                var volumeId = String(rows[i].id)
                page.requestedVolumeNumber = ""
                Qt.callLater(function() { page._readVolume(volumeId) })
                return
            }
        }
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
    // Shelf-less series' "Search nyaa" primary action (catalogue-independence
    // Slice 4, 2026-08-20): the SAME picker chrome as _openSources() below, but
    // in series mode — a volume-agnostic query from the series title, since a
    // shelf-less series has no volume 1 to target. One acquisition path only.
    function _openSeriesSearch() {
        sourcesPage.show({
            seriesMode: true,
            seriesId: page.seriesId,
            seriesTitle: page.seriesTitle,
            volumeId: "series:" + (page.seriesId.length ? page.seriesId : page.seriesTitle),
            volumeNumber: "", volumeTitle: "", cover: page.cover, synopsis: page.synopsis
        })
    }

    // The ONE path that raises the source picker — a single tile and a batch both
    // come through here, so the series identity is merged in exactly one place.
    function _openSources(ctx) {
        ctx.intent = String(ctx.intent || "acquire")
        if (ctx.intent !== "consume" && page.pendingReadActive
                && String(ctx.volumeId || "") !== page.pendingReadVolumeId)
            page._invalidateReadIntent()
        ctx.seriesId = page.seriesId
        ctx.seriesTitle = page.seriesTitle
        ctx.volumeNumber = ctx.number
        ctx.volumeTitle = ctx.title
        // Slice D: the sheet hero shows THIS volume's own synopsis when the enricher has
        // accepted one (see MangaTankobanService::volumeMap — mostly absent, so the mock's
        // italic empty state is the common case), else falls back to the series synopsis
        // so the hero is never blank when the series itself has one.
        ctx.synopsis = page._volumeSynopsis(ctx.volumeId) || page.synopsis
        sourcesPage.show(ctx)
    }

    function _volumeRow(volumeId) {
        var rows = readingRoom.library.volumeRows || []
        var id = String(volumeId)
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === id) return rows[i]
        return null
    }

    function _volumeState(volumeId) {
        var id = String(volumeId)
        var service = page.tankobanVolumesRef
        if (service && service.statusOf) {
            var status = service.statusOf(id) || ({})
            var state = String(status.state || "none")
            if (state !== "none") return state
        }
        var row = page._volumeRow(id)
        return row ? readingRoom.library.effectiveState(row) : "none"
    }

    function _inFlightVolumeState(state) {
        return state === "resolving" || state === "downloading"
            || state === "ingesting" || state === "packing"
    }

    function _clearReadIntent() {
        page.pendingReadVolumeId = ""
        page.pendingReadSeriesId = ""
        page._pendingReadViaSources = false
    }

    function _invalidateReadIntent() {
        page._readIntentGeneration += 1
        page._clearReadIntent()
    }

    function _beginReadIntent(volumeId, viaSources) {
        page._readIntentGeneration += 1
        page.pendingReadVolumeId = String(volumeId)
        page.pendingReadSeriesId = page.seriesId
        page._pendingReadViaSources = viaSources === true
        return page._readIntentGeneration
    }

    function _readIntentMatches(volumeId, generation) {
        return page.pendingReadActive
            && String(volumeId) === page.pendingReadVolumeId
            && Number(generation) === page._readIntentGeneration
            && page.pendingReadSeriesId === page.seriesId
    }

    function _completePendingRead(volumeId, generation) {
        var id = String(volumeId)
        if (!page._readIntentMatches(id, generation)) return false
        if (page._volumeState(id) !== "ready") return false
        var service = page.tankobanVolumesRef
        var pages = service && service.localPages ? (service.localPages(id) || []) : []
        if (!pages.length) return false
        page._clearReadIntent()
        page._openVolume(id)
        return true
    }

    function _readVolume(volumeId) {
        var id = String(volumeId || "")
        if (!id.length) return
        var state = page._volumeState(id)
        if (state === "ready") {
            page._invalidateReadIntent()
            page._openVolume(id)
            return
        }

        var generation = page._beginReadIntent(id, false)
        if (page._inFlightVolumeState(state)) return

        var row = page._volumeRow(id)
        if (!row) { page._invalidateReadIntent(); return }
        page._pendingReadViaSources = true
        var ctx = readingRoom.library.sourceContext(id, "consume")
        ctx.intentGeneration = generation
        page._openSources(ctx)
    }

    // Looks up a volume's accepted per-volume synopsis off the live canonical model
    // (the same rows the shelf renders from) — chooseSource()/_requestBatch() build ctx
    // from a lighter row projection that does not carry synopsis, so this reads the
    // library's own volumeRows directly rather than widening that shape.
    function _volumeSynopsis(volumeId) {
        var rows = readingRoom.library.volumeRows || []
        var id = String(volumeId)
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === id && rows[i].synopsis && String(rows[i].synopsis).length)
                return String(rows[i].synopsis)
        return ""
    }

    // A batch button was pressed: turn volume NUMBERS into volume ids and raise the
    // picker over all of them. Ownership is re-checked HERE, at the moment of the
    // press — the shelf may have finished a volume since the button was drawn, and
    // an owned volume must never be re-downloaded (design 2026-07-30, constraint 2).
    // The picker searches ids[0]: the engine has no range search.
    function _requestBatch(numbers, label) {
        var want = {}
        for (var i = 0; i < numbers.length; i++) want[String(numbers[i])] = true
        var ids = [], nums = [], rows = readingRoom.library.volumeRows || []
        for (var r = 0; r < rows.length; r++)
            if (want[String(rows[r].number)] && String(rows[r].state) !== "ready") {
                ids.push(String(rows[r].id))
                nums.push(String(rows[r].number))
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
        page._readVolume(String(entryId))
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
        target: page.tankobanVolumesRef
        ignoreUnknownSignals: true
        function onVolumesChanged(sid) {
            if (sid !== page.seriesId) return
            page._rebuildTankobanEntries()
            Qt.callLater(page.openRequestedVolume)
        }
        function onFinished(volumeId) {
            if (!page.pendingReadActive || page._pendingReadViaSources) return
            if (String(volumeId) !== page.pendingReadVolumeId) return
            page._completePendingRead(String(volumeId), page._readIntentGeneration)
        }
        function onFailed(volumeId, reason) {
            if (!page.pendingReadActive || page._pendingReadViaSources) return
            if (String(volumeId) === page.pendingReadVolumeId) page._invalidateReadIntent()
        }
        function onRemoved(volumeId) {
            if (page.pendingReadActive && String(volumeId) === page.pendingReadVolumeId)
                page._invalidateReadIntent()
        }
    }

    // --- volumes (Comick volume DB via MangaVolumes.js; complete ranges or none — gated) ---
    property var volumes: []                                  // [{number,cover,startNum,endNum,chapterStart,chapterEnd}]

    // ── the facts column beside the synopsis (Theatre's key/value stack) ─────
    // Only facts we actually hold. Nothing is padded to fill the column: a row
    // that has no answer simply is not there.
    readonly property var factRows: {
        var out = []
        if (page.author.length) out.push({ "k": "Author", "v": page.author })
        if (page.status.length) out.push({ "k": "Status", "v": page.status })
        if (page.tankobanMode && readingRoom.library.volumeRows.length) {
            var rows = readingRoom.library.volumeRows, owned = readingRoom.library.ownedCount
            out.push({ "k": "Volumes", "v": String(rows.length) })
            out.push({ "k": "On this device", "v": owned
                       ? (owned + (owned === 1 ? " volume" : " volumes")) : "None yet" })
        }
        return out
    }

    // ── the hero's Read promise ──────────────────────────────────────────────
    // Theatre's button names the episode it will play. Ours names the volume: the
    // one you were part-way through, else the first you own, else the first book.
    // Nothing here invents a target — if there is no volume at all the button
    // falls back to the chapter list, which is what an unqualified series shows.
    readonly property string readCtaLabel: {
        if (!readingRoom.library.continueVolumeId.length) return "Read"
        var rows = readingRoom.library.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === readingRoom.library.continueVolumeId)
                return "Continue Vol. " + rows[i].number
        return "Read"
    }
    function readPrimary() {
        // resume beats everything — it is the only target the user already chose
        if (readingRoom.library.continueVolumeId.length) { page._readVolume(readingRoom.library.continueVolumeId); return }
        var rows = readingRoom.library.volumeRows || []
        for (var i = 0; i < rows.length; i++)                    // first book on disk
            if (String(rows[i].state) === "ready") { page._readVolume(String(rows[i].id)); return }
        if (rows.length) { page._readVolume(String(rows[0].id)); return }
        // No known shelf at all (catalogue-independence Slice 4, 2026-08-20):
        // primaryAction === "search" — open the series-level nyaa picker. Chapters are
        // gone entirely (catalogue-independence Slice 5, 2026-08-20, purity law) so
        // there is no other fallback left — a shelf-less series always lands here.
        if (!page.hasShelf) { page._openSeriesSearch(); return }
    }

    function collectionEntry() {
        // Key by seriesId (TB-002), NOT seriesTitle. A saved manga's Collection id now
        // matches the id on its own Progress records, so the Library can tell it has been
        // read. seriesId is empty until the search resolves; LibraryButton already no-ops
        // on an empty entry.id, so that naturally gates saving until identity is ready.
        return { "id": page.seriesId, "type": "manga",
                 "title": page.seriesTitle, "cover": page.cover, "payload": ({}) }
    }

    Theme { id: theme }

    onSeriesTitleChanged: resolve()
    Component.onCompleted: if (seriesTitle.length) resolve()

    // Data-vault Slice 3 (2026-08-22): wake-on-ready. A fresh install can open this page
    // before MalCatalog's download lands (id>0/title given but the db not open yet), leaving
    // the honest shelf-less page (resolvedMalId stays 0). The moment malCatalogRef flips
    // ready, re-run resolve() so the page catches up without the user backing out and
    // reopening it. Targets the REF (not the bare MalCatalog context property) so a harness
    // driving a fake catalog through malCatalogRef exercises the same path production does.
    // Guarded on resolvedMalId === 0 so an already-resolved page NEVER re-resolves — a second
    // readyChanged pulse (or a page opened after the catalog was already ready) is a no-op.
    Connections {
        target: page.malCatalogRef
        function onReadyChanged() {
            if (page.resolvedMalId === 0 && page.seriesTitle.length) page.resolve()
        }
    }

    // Applies a mangaById()-shaped Jikan row to the masthead facts. The SOLE source of
    // masthead data now (Slice 2, amended) — both the malId-primary path and the
    // title-resolved-to-a-single-candidate path call this after finding the row, so the
    // masthead always renders the same catalogue shape regardless of how identity resolved.
    function _applyCatalogRow(row) {
        page.catalogRow = row
        var authors = row.authors || []
        page.author = authors.map(function(a) { return a.name }).join(", ")
        page.status = row.status || ""
        page.year = row.year || 0
        page.synopsis = row.synopsis || ""
        var genreList = row.genres || []
        page.genres = genreList.map(function(g) { return g.name })
        page.score = row.score || 0
        var poster = (row.images && row.images.jpg) ? (row.images.jpg.large_image_url || "") : ""
        page.cover = poster
        page.banner = poster    // the catalogue has no separate banner asset; reuse the poster
    }

    // The catalogue-fed identity + masthead resolve (Slice 2, amended 2026-08-20). Fully
    // SYNCHRONOUS — MalCatalog's accessors are bound C++ calls, not network — so the page
    // never needs a reveal timer. No WeebCentral/Comick/AniList call remains on this path
    // (purity law, spec §2.1): the browse-path identity is malId-first (offline) or, when
    // opened by title only, a single exact-normalized matchByTitle() candidate. Ambiguous
    // or unmatched titles get the honest shelf-less page — never a guess.
    function resolve() {
        loading = true; errorMsg = ""
        seriesId = ""; resolvedMalId = 0; catalogRow = ({})
        banner = ""; cover = ""; author = ""; status = ""; year = 0
        synopsis = ""; genres = []; score = 0
        volumes = []
        _tankobanPrepared = false

        var mc = page.malCatalogRef
        var id = 0
        if (malId) {
            id = Number(malId) || 0
        } else if (seriesTitle.length && mc) {
            var candidates = mc.matchByTitle(seriesTitle, 0, "manga") || []
            if (candidates.length === 1) id = Number(candidates[0].mal_id) || 0
            // 0 candidates (no match) or >1 (ambiguous) -> id stays 0, honest shelf-less page
        }

        if (id > 0 && mc) {
            var row = mc.mangaById(id) || {}
            if (Object.keys(row).length > 0) {
                page.resolvedMalId = id
                page._applyCatalogRow(row)
                page.seriesId = "mal:" + id
                // TB-002: silently re-file a legacy title-keyed Collection save under seriesId.
                page._refileLegacyCollectionEntryIfNeeded()
                // Catalogue-fed shelf seed (Slice 3) — the sole feed now (purity law).
                page._prepareTankoban()
            }
            // id>0 but no row found (db not ready / id unknown): resolvedMalId/seriesId stay
            // unresolved — same honest shelf-less page as an ambiguous/unmatched title.
        }
        // Bridge automation surface (closing-sweep fix, 2026-08-21): displayTitle is
        // committed here, in resolve()'s own synchronous call, rather than left as a
        // live `displayTitle: page.seriesTitle` binding. seriesTitle changing is what
        // TRIGGERS resolve() via onSeriesTitleChanged in the first place, so that binding
        // shared the SAME seriesTitleChanged signal's slot queue as this handler — Qt
        // dispatches connected slots in connection order, and onSeriesTitleChanged
        // (declared earlier in this file, so connected first during construction) always
        // runs to completion before a sibling binding declared later gets its turn. Every
        // OTHER masthead scalar (resolvedMalId, hasShelf, primaryAction) is bound to a
        // page property that changes INSIDE this function, so each fires its own
        // dedicated notify chain synchronously nested right here — but displayTitle's old
        // binding only caught up once resolve() returned and the outer signal moved on to
        // its next slot. That left a real window, on the GUI thread, where `ready` had
        // already flipped true (loading's own dedicated notify, below) while
        // `displayTitle` still held the PREVIOUS series' title — the exact stale-read
        // race the closing sweep (2026-08-21) caught on 3 of 4 replays, specifically on a
        // late re-navigation once enough prior work was queued for an external poll to
        // land inside that window. Assigning it explicitly here folds it into the same
        // synchronous batch as everything else, so by the time `ready` flips true below,
        // every masthead scalar an external reader can observe is already final.
        tankobanSeriesMasthead.displayTitle = seriesTitle
        loading = false
    }

    function fmtDate(ms) {
        var n = Number(ms)
        if (!n || n <= 0) return ""
        return new Date(n).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    // Bridge automation surface (world-namespaced per the naming law — never a bare shared
    // stem). Plain scalars only, per the Lanista ledger's qml-get vocabulary.
    Item {
        id: tankobanSeriesMasthead
        objectName: "tankobanSeriesMasthead"
        visible: false
        property bool ready: !page.loading
        property string resolvedMalId: page.resolvedMalId > 0 ? String(page.resolvedMalId) : ""
        // Assigned explicitly at the end of resolve() (see the comment there) instead of
        // a live `page.seriesTitle` binding — closes the ready/displayTitle stale-read
        // race the closing sweep (2026-08-21) ground-truthed on late re-navigation.
        property string displayTitle: ""
        property bool hasShelf: page.hasShelf
        property string primaryAction: page.primaryAction
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

    // The approved Reading Room is the page's whole reading surface. The old
    // uninstantiated "legacy corridor" Component (chapter table + duplicate volume
    // shelf, never wired to a Loader) was deleted outright in catalogue-independence
    // Slice 5, 2026-08-20 — it held the "Latest chapters" tail and per-chapter Get
    // rows Hemanth's lock removes completely, and it had no live callers to migrate.
    MangaReadingRoom {
        id: readingRoom
        anchors.fill: parent
        z: 40
        visible: !page.loading
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        backdrop: page.backdrop
        seriesId: page.seriesId
        seriesTitle: page.seriesTitle
        malId: page.resolvedMalId > 0 ? String(page.resolvedMalId) : ""
        primaryAction: page.primaryAction
        banner: page.banner
        cover: page.cover
        author: page.author
        status: page.status
        year: page.year
        synopsis: page.synopsis
        errorText: page.errorText
        genres: page.genres
        score: page.score
        service: page.tankobanVolumesRef
        pendingReadVolumeId: page.pendingReadVolumeId
        collectionEntry: page.collectionEntry()
        onBackRequested: { page._invalidateReadIntent(); page.backRequested() }
        onMinimizeRequested: { page._invalidateReadIntent(); page.minimizeRequested() }
        onFullscreenRequested: page.fullscreenRequested()
        onCloseRequested: { page._invalidateReadIntent(); page.closeRequested() }
        onPrimaryRequested: page.readPrimary()
        onReadVolumeRequested: (volumeId) => page._readVolume(volumeId)
        onSourcesRequested: (ctx) => page._openSources(ctx)
        onBatchRequested: (numbers, label) => page._requestBatch(numbers, label)
    }

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
        // chapters are gone entirely (catalogue-independence Slice 5, 2026-08-20) —
        // openEntryKind is never "manga" from a live route any more, so the reader's
        // chapters prop only ever needs the tankoban entries; [] covers the defensive
        // "manga" branch without an undefined chaptersModel reference.
        chapters: page.openEntryKind === "tankoban" ? page.tankobanReaderEntries : []
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
    // Harness reach (R1, 2026-08-21): a bare-page test (manga_series_catalogue_harness.qml)
    // has no Lanista bridge and no real Extensions context property; exposing the child by
    // alias lets it inject a fake via sourcesPage.extensionsRef and call sourcesPage.show()
    // directly, the same seam shape as malCatalogRef/tankobanCatalogRef/tankobanVolumesRef.
    readonly property alias sourcesPage: sourcesPage

    MangaTankobanSourcesPage {
        id: sourcesPage
        anchors.fill: parent
        z: 70
        backdrop: page.backdrop
        onOpenExtensionsRequested: page.openExtensionsRequested()
        onConsumeReady: (volumeId, generation) => page._completePendingRead(volumeId, generation)
        onConsumeAbandoned: (volumeId, generation) => {
            if (page._readIntentMatches(volumeId, generation)) page._invalidateReadIntent()
        }
    }
}
