// RED/GREEN contract harness for MangaSeries.qml's catalogue-fed identity + masthead
// (catalogue-independence Slice 2, amended 2026-08-20). Instantiates the real page bare
// (Qt.createComponent + createObject) with fake MalCatalog/TankobanCatalog/TankobanVolumes
// seams injected via the page's own injectable properties (malCatalogRef/tankobanCatalogRef/
// tankobanVolumesRef) — no network, no live app state, mirrors manga_reading_room_harness's
// house convention (sentinel + Qt.exit).
import QtQuick

Item {
    id: harness
    width: 1440
    height: 900
    visible: true

    component FakeMalCatalog: QtObject {
        property bool readyVal: true
        property var rows: ({})       // malId(string) -> Jikan-shaped manga row
        property var titleMap: ({})   // title -> [malId, ...] (matchByTitle candidates)
        // Data-vault Slice 3 (2026-08-22) wake-on-ready seam: readyChanged mirrors the real
        // MalCatalog's Q_PROPERTY NOTIFY signal of the same name — the page's Connections
        // block listens for exactly this name. setReady() is the harness's own emit path
        // (a plain property write on readyVal does NOT auto-fire a differently-named signal).
        signal readyChanged()
        function setReady(v) { readyVal = v; readyChanged() }
        // Call instrumentation for case 7/8 (wake-on-ready): mangaByIdCalls proves resolve()
        // actually re-ran (case 7b) or did NOT re-run (case 8), never just inferred from the
        // page's own state.
        property int mangaByIdCalls: 0
        function ready() { return readyVal }
        function mangaById(id) {
            mangaByIdCalls++
            if (!readyVal) return ({})
            var r = rows[String(id)]
            return r !== undefined ? r : ({})
        }
        function matchByTitle(title, year, medium) {
            if (!readyVal) return []
            var ids = titleMap[title] || []
            var out = []
            for (var i = 0; i < ids.length; i++) out.push({ "mal_id": ids[i] })
            return out
        }
    }

    component FakeTankobanCatalog: QtObject {
        property bool remoteEnabled: false
        property int requestedId: 0
        property var volumeMap: ({})
        signal seriesChanged(int malId)
        function requestSeries(id) { requestedId = id }
        function hasSeries(id) { return infoMap[String(id)] !== undefined }
        function volumes(id) { return volumeMap[String(id)] || [] }
        property bool readyVal: true
        property var infoMap: ({})    // malId(string) -> {volumeCount, countBasis}
        function ready() { return readyVal }
        function seriesInfo(id) {
            var v = infoMap[String(id)]
            return v !== undefined ? v : ({ "volumeCount": 0, "countBasis": "" })
        }
    }

    // Same shape as manga_reading_room_harness.qml's FakeService — the nested
    // MangaReadingRoom -> MangaTankobanLibrary chain needs the full surface.
    component FakeVolumesService: QtObject {
        property var volMap: ({})     // seriesId -> [{number,state,...}]
        signal volumesChanged(string seriesId)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return [] }
        function cancel(vid) {}
        function prepareSeries(info, volumes, chapters) {
            volMap[info.seriesId] = volumes
        }
    }

    // R1 (release gate for 1.1.1, 2026-08-21, "nyaa ships dark"): the same shape
    // ExtensionsStore::installed() returns -- id/enabled pairs only, everything else
    // the picker's _nyaaEnabled() reads is unused by it.
    component FakeExtensions: QtObject {
        property bool nyaaEnabled: false
        property bool tankoyomiEnabled: false
        property int revision: 0
        signal changed()
        function installed() {
            return [
                { "id": "colosseum.well.nyaa", "enabled": nyaaEnabled },
                { "id": "colosseum.well.tankoyomi", "enabled": tankoyomiEnabled }
            ]
        }
        function setTankoyomiEnabled(v) { tankoyomiEnabled = v; revision += 1; changed() }
    }

    component FakeMangaEngine: QtObject {
        property int catalogueCalls: 0
        property string lastLanguage: ""
        property var lastProfile: null
        // Match MangaEngine's Q_PROPERTY shape. The production bridge exposes
        // chapterDefaultLanguage as a QString property (and invokable getter).
        property string chapterDefaultLanguage: "en"
        signal chapterConfigurationChanged()
        signal chapterCatalogueResults(string requestId, string sourceSeriesId, var rows)
        signal chapterCatalogueFailed(string requestId, string message)
        signal pagesResult(var rows)
        signal engineError(string message)
        function chapterLanguages() {
            return [
                { "code": "en", "label": "English", "providerCount": 1 },
                { "code": "es", "label": "Espa?ol", "providerCount": 2 },
                { "code": "pt", "label": "Portugu?s (Brasil)", "providerCount": 3 },
                { "code": "fr", "label": "Fran?ais", "providerCount": 4 }
            ]
        }
        function setChapterDefaultLanguage(language) {
            chapterDefaultLanguage = String(language || "")
            chapterConfigurationChanged()
        }
        function chapterCatalogueForProfile(requestId, profile, language) {
            lastProfile = profile
            chapterCatalogueForLanguage(requestId, profile.discoveryTitle || profile.title, language)
        }
        function chapterCatalogueForLanguage(requestId, title, language) {
            catalogueCalls += 1
            lastLanguage = String(language)
        }
    }

    FakeMalCatalog { id: malCatalog }
    FakeTankobanCatalog { id: tankCatalog }
    FakeVolumesService { id: volService }
    FakeExtensions { id: fakeExtensions }
    FakeMangaEngine { id: fakeMangaEngine }
    component FakeMangaPreferences: QtObject {
        property var overrides: ({})
        property int mangaLanguageRevision: 0
        property bool rejectWrites: false
        signal mangaLanguagesChanged()
        function mangaLanguageForSeries(id) { return String(overrides[String(id)] || "") }
        function setMangaLanguageForSeries(id, code) {
            if (rejectWrites) return false
            var next = ({})
            for (var k in overrides) next[k] = overrides[k]
            next[String(id)] = String(code)
            overrides = next
            mangaLanguageRevision++
            mangaLanguagesChanged()
            return true
        }
        function clearMangaLanguageForSeries(id) {
            if (rejectWrites) return false
            var next = ({})
            for (var k in overrides) if (k !== String(id)) next[k] = overrides[k]
            overrides = next
            mangaLanguageRevision++
            mangaLanguagesChanged()
            return true
        }
    }
    FakeMangaPreferences { id: fakePrefs }
    FakeMangaPreferences { id: fakeOtherProfile }
    component FakeContinueProgress: QtObject {
        property int revision: 0
        property var records: ({})
        signal changed()
        function get(kind, seriesId) { return records[String(kind) + "|" + String(seriesId)] || ({}) }
        function setRecords(next) { records = next; revision++; changed() }
    }
    component FakeContinueDownloads: QtObject {
        property var states: ({})
        signal finished(string chapterId)
        signal failed(string chapterId, string reason)
        signal removed(string chapterId)
        function statusOf(chapterId) { return states[String(chapterId)] || ({ "state": "none" }) }
        function downloadChapter(chapterId, seriesId, title, label) {}
    }
    FakeContinueProgress { id: fakeContinueProgress }
    FakeContinueDownloads { id: fakeContinueDownloads }

    function ck(condition, message) {
        if (!condition) throw new Error(message)
    }
    function findNamed(item, name, enabledOnly) {
        if (!item) return null
        if (item.objectName === name && (!enabledOnly || item.enabled)) return item
        var children = item.children || []
        for (var i = 0; i < children.length; i++) {
            var found = findNamed(children[i], name, enabledOnly)
            if (found) return found
        }
        return null
    }

    property var pageComp: null
    function makePage(malId, seriesTitle, profile) {
        if (!pageComp) {
            pageComp = Qt.createComponent("../qml/MangaSeries.qml")
            if (pageComp.status === Component.Error)
                throw new Error("MangaSeries component: " + pageComp.errorString())
        }
        var p = pageComp.createObject(harness, {
            "width": 1320, "height": 860,
            "malCatalogRef": malCatalog, "tankobanCatalogRef": tankCatalog,
            "tankobanVolumesRef": volService
        })
        if (!p) throw new Error("MangaSeries createObject returned null")
        // Mirror production sequencing: the edition profile and malId must be
        // present BEFORE seriesTitle triggers resolve().
        var pr = profile || ({})
        p.preferencesRef = pr.preferencesRef || null
        if (pr.progressRef) p.progressRef = pr.progressRef
        if (pr.downloadsRef) p.downloadsRef = pr.downloadsRef
        p.seriesIdOverride = pr.seriesIdOverride || ""
        p.sourceSearchTitle = pr.sourceSearchTitle || ""
        p.sourceSearchAliases = pr.sourceSearchAliases || []
        p.sourceRequiredMarkers = pr.sourceRequiredMarkers || []
        p.malId = malId || ""
        p.seriesTitle = seriesTitle || ""
        return p
    }

    // Case 4's sub-cases live behind one flag so the negative control (flip the "get"
    // expectation) touches exactly one line and is trivially revertible.
    property bool _negControlFlipGet: false

    // Case 7's negative control (data-vault Slice 3, wake-on-ready): flip to true, rerun,
    // confirm exactly case 7b reds (it expects the page to stay unresolved after ready
    // flips true, which is false — the page DOES re-resolve), then flip back and restore
    // green.
    property bool _negControlFlipWake: false

    function runChecks() {
        try {
            var monsterRow = {
                "mal_id": 1, "title": "Monster", "title_english": "Monster",
                "score": 9.1, "members": 500000, "status": "Finished", "volumes": 18,
                "year": 1994, "published": { "prop": { "from": { "year": 1994 } } },
                "images": { "jpg": { "large_image_url": "http://cover/1" } },
                "synopsis": "A synopsis about a doctor and a monster.",
                "authors": [ { "name": "Naoki Urasawa" } ],
                "genres": [ { "name": "Mystery" }, { "name": "Seinen" } ]
            }

            // ── Case 1: malId-open renders fake-row facts, ready==true, seriesId=="mal:1" ──
            malCatalog.rows = ({ "1": monsterRow })
            malCatalog.titleMap = ({})
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })   // nothing downloaded yet

            var p1 = makePage("1", "Monster")
            ck(p1.loading === false, "case1: malId-open resolves synchronously (loading false)")
            ck(p1.resolvedMalId === 1, "case1: resolvedMalId must be 1, got " + p1.resolvedMalId)
            ck(p1.seriesId === "mal:1", "case1: seriesId must be mal:1, got " + p1.seriesId)
            ck(p1.seriesTitle === "Monster", "case1: displayTitle (seriesTitle) must be Monster")
            ck(p1.author === "Naoki Urasawa", "case1: author must come from the catalogue authors list")
            ck(p1.status === "Finished", "case1: status must come from the catalogue row")
            ck(p1.year === 1994, "case1: year must come from the catalogue row")
            ck(Math.abs(p1.score - 9.1) < 0.001, "case1: score must come from the catalogue row")
            ck(p1.synopsis.indexOf("doctor") >= 0, "case1: synopsis must come from the catalogue row")
            ck(p1.cover === "http://cover/1",
               "case1: poster must come from images.jpg.large_image_url, got " + p1.cover)
            ck(p1.hasShelf === true, "case1: hasShelf must be true when catalogue volumeCount>0")
            ck(p1.primaryAction === "get",
               "case1: primaryAction must be get (shelf present, nothing downloaded), got " + p1.primaryAction)
            ck(p1.errorMsg === "", "case1: errorMsg must stay empty (no WeebCentral search fired)")

            // ── Case 2: exact-title resolve — a single matchByTitle candidate resolves
            //    the SAME way malId-open does (masthead facts still come from mangaById) ──
            malCatalog.titleMap = ({ "Monster": [1] })
            var p2 = makePage("", "Monster")
            ck(p2.resolvedMalId === 1, "case2: single exact-title candidate must resolve to malId 1")
            ck(p2.seriesId === "mal:1", "case2: exact-title resolve must set seriesId mal:1")
            ck(p2.author === "Naoki Urasawa", "case2: masthead facts must come from mangaById after title resolve")
            ck(p2.hasShelf === true, "case2: exact-title resolve must still see the catalogue shelf")

            // ── Case 2c: One Piece Color reuses MAL 13 but owns a distinct durable id ──
            var colorRow = {
                "mal_id": 13, "title": "One Piece", "title_english": "One Piece",
                "score": 9.2, "status": "Publishing", "year": 1997,
                "images": { "jpg": { "large_image_url": "http://cover/13" } },
                "synopsis": "Pirates.", "authors": [ { "name": "Eiichiro Oda" } ],
                "genres": [ { "name": "Adventure" } ]
            }
            malCatalog.rows = ({ "1": monsterRow, "13": colorRow })
            tankCatalog.infoMap = ({ "13": { "volumeCount": 113, "countBasis": "mal" } })
            volService.volMap = ({ "mal:13:color": [] })
            var pc = makePage("13", "One Piece (Color)", {
                "seriesIdOverride": "mal:13:color",
                "sourceSearchTitle": "One Piece Colored",
                "sourceSearchAliases": ["One Piece Digital Colored Comics"],
                "sourceRequiredMarkers": ["colored", "full color", "full colour"]
            })
            ck(pc.resolvedMalId === 13, "case2c: Color must resolve through MAL 13")
            ck(pc.seriesId === "mal:13:color", "case2c: Color durable id must stay isolated")
            ck(pc.hasShelf === true, "case2c: Color must reuse MAL 13's 113-volume shelf")
            ck(pc.sourceSearchTitle === "One Piece Colored", "case2c: colored discovery title retained")
            ck(pc.sourceSearchAliases.length === 1
               && pc.sourceSearchAliases[0] === "One Piece Digital Colored Comics",
               "case2c: colored discovery alias retained")
            ck(pc.sourceRequiredMarkers.length === 3, "case2c: colored marker gate retained")
            pc.extensionsRef = fakeExtensions
            pc.mangaEngineRef = fakeMangaEngine
            fakeExtensions.setTankoyomiEnabled(true)
            pc._enterChapterMode()
            ck(fakeMangaEngine.lastProfile && fakeMangaEngine.lastProfile.title === "One Piece (Color)",
               "case2c: Chapter Mode must forward the canonical title in the complete profile")
            ck(fakeMangaEngine.lastProfile.discoveryTitle === "One Piece Colored"
               && fakeMangaEngine.lastProfile.aliases[0] === "One Piece Digital Colored Comics"
               && fakeMangaEngine.lastProfile.requiredTitleMarkers.length === 3,
               "case2c: Chapter Mode must forward discovery aliases and edition markers")
            pc._enterTankobanMode()
            fakeExtensions.setTankoyomiEnabled(false)


            // ── Case 3: ambiguous / unmatched title -> the honest shelf-less page ──
            malCatalog.titleMap = ({ "Ambiguous Title": [1, 2] })
            var p3a = makePage("", "Ambiguous Title")
            ck(p3a.resolvedMalId === 0, "case3a: an ambiguous title must leave resolvedMalId 0")
            ck(p3a.seriesId === "", "case3a: an ambiguous title must leave seriesId empty")
            ck(p3a.hasShelf === false, "case3a: an ambiguous title must show no shelf")
            ck(p3a.primaryAction === "search",
               "case3a: an ambiguous title's primaryAction must be search, got " + p3a.primaryAction)
            ck(p3a.loading === false, "case3a: the page must still reveal (never hang) when ambiguous")

            malCatalog.titleMap = ({})   // "Unknown Series" -> zero candidates
            var p3b = makePage("", "Unknown Series")
            ck(p3b.resolvedMalId === 0, "case3b: an unmatched title must leave resolvedMalId 0")
            ck(p3b.seriesId === "", "case3b: an unmatched title must leave seriesId empty")
            ck(p3b.primaryAction === "search",
               "case3b: an unmatched title's primaryAction must be search, got " + p3b.primaryAction)

            // ── Case 4: the primary-button truth table ──
            // 4a: volume 1 downloaded -> "open" wins over a present shelf
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [ { "number": "1", "state": "ready" } ] })
            var p4a = makePage("1", "Monster")
            ck(p4a.primaryAction === "open",
               "case4a: volume 1 ready must give primaryAction open, got " + p4a.primaryAction)

            // 4b: shelf known, nothing downloaded -> "get"
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p4b = makePage("1", "Monster")
            // NEGATIVE CONTROL toggle (flip the "get" expectation) — flip _negControlFlipGet
            // to true, rerun, confirm exactly this line reds, then flip back and restore green.
            var expectGet = harness._negControlFlipGet ? "open" : "get"
            ck(p4b.primaryAction === expectGet,
               "case4b: a known shelf with nothing downloaded must give primaryAction get, got " + p4b.primaryAction)

            // 4c: no shelf at all -> "search"
            tankCatalog.infoMap = ({ "1": { "volumeCount": 0, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p4c = makePage("1", "Monster")
            ck(p4c.primaryAction === "search",
               "case4c: no shelf must give primaryAction search, got " + p4c.primaryAction)

            // ── Case 5: the WC error-copy path is gone ──
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p5 = makePage("1", "Monster")
            ck(p5.errorMsg === "", "case5: errorMsg must never be set (no WeebCentral search/error path)")
            ck(p5.errorText === "", "case5: errorText must never surface WeebCentral copy")
            ck(p5.errorText.indexOf("WeebCentral") < 0, "case5: no WeebCentral string reaches errorText")

            // ── Case 6 (R1, 2026-08-21): the picker's extension gate -- nyaa ships dark ──
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p6 = makePage("1", "Monster")
            p6.sourcesPage.extensionsRef = fakeExtensions

            fakeExtensions.nyaaEnabled = false
            p6.sourcesPage.show({ "volumeId": "vol-1", "seriesTitle": "Monster" })
            ck(p6.sourcesPage.sourcesEnabled === false,
               "case6a: sourcesEnabled must be false when the nyaa well is disabled")
            ck(p6.sourcesPage.loading === false,
               "case6a: a dark well must resolve immediately, never hang loading")
            ck(p6.sourcesPage.complete === true,
               "case6a: a dark well must still mark the sheet complete (honest empty state)")
            ck(p6.sourcesPage.rows.length === 0,
               "case6a: a dark well must never return rows")

            fakeExtensions.nyaaEnabled = true
            p6.sourcesPage.show({ "volumeId": "vol-1", "seriesTitle": "Monster" })
            ck(p6.sourcesPage.sourcesEnabled === true,
               "case6b: sourcesEnabled must be true once the nyaa well is enabled")

            // ── Case 7 (data-vault Slice 3, 2026-08-22): wake-on-ready ──
            // Case 6c: Tankoyomi is a real extension gate, not a decorative row.
            var p6c = makePage("1", "Monster")
            p6c.extensionsRef = fakeExtensions
            p6c.mangaEngineRef = fakeMangaEngine
            fakeMangaEngine.catalogueCalls = 0
            fakeExtensions.setTankoyomiEnabled(false)
            p6c._enterChapterMode()
            ck(p6c.chapterMode === true, "case6c: Chapter Mode still opens while Tankoyomi is off")
            ck(p6c.tankoyomiEnabled === false, "case6c: page must observe Tankoyomi disabled")
            ck(fakeMangaEngine.catalogueCalls === 0,
               "case6c: disabled Tankoyomi must make zero provider calls")
            ck(p6c.chaptersLoading === false, "case6c: disabled Tankoyomi must not hang loading")
            ck(p6c.chaptersError.indexOf("Tankoyomi") >= 0,
               "case6c: disabled state must name Tankoyomi honestly")

            fakeExtensions.setTankoyomiEnabled(true)
            ck(p6c.tankoyomiEnabled === true, "case6d: page must observe Tankoyomi enabled")
            ck(fakeMangaEngine.catalogueCalls === 1,
               "case6d: enabling Tankoyomi must reload the Chapter Mode provider call")
            ck(fakeMangaEngine.lastLanguage === "en",
               "case6d: enabled Tankoyomi preserves the selected language")

            // ── Case 6d.1: live master disable/re-enable invalidates a stale request ──
            var requestBeforeDisable = p6c._chapterRequestId
            var callsBeforeDisable = fakeMangaEngine.catalogueCalls
            fakeExtensions.setTankoyomiEnabled(false)
            ck(p6c._chapterRequestId === "",
               "case6d.1: disabling Tankoyomi must invalidate the active request id")
            ck(p6c.chaptersModel.length === 0 && p6c.chapterSourceSeriesId === "",
               "case6d.1: disabling Tankoyomi must clear stale Chapter Mode results")
            ck(fakeMangaEngine.catalogueCalls === callsBeforeDisable,
               "case6d.1: disabling Tankoyomi must make no provider calls")
            fakeMangaEngine.chapterCatalogueResults(requestBeforeDisable, "stale-series",
                                                     [{ "id": "stale-chapter" }])
            ck(p6c.chaptersModel.length === 0 && p6c.chapterSourceSeriesId === "",
               "case6d.1: a late pre-disable result must be ignored")

            fakeExtensions.setTankoyomiEnabled(true)
            ck(fakeMangaEngine.catalogueCalls === callsBeforeDisable + 1,
               "case6d.1: re-enabling Tankoyomi must reload Chapter Mode")
            ck(p6c._chapterRequestId !== "" && p6c._chapterRequestId !== requestBeforeDisable,
               "case6d.1: re-enable must issue a fresh request id")

            // ── Case 6e (Task 2): Chapter Mode adopts the configured default language ──
            // A page with no explicit in-page choice must request the persisted default,
            // not the historical English literal.
            p6c._enterTankobanMode()
            fakeMangaEngine.chapterDefaultLanguage = "es"
            fakeMangaEngine.catalogueCalls = 0
            var p6e = makePage("1", "Monster")
            p6e.extensionsRef = fakeExtensions
            p6e.mangaEngineRef = fakeMangaEngine
            p6e._enterChapterMode()
            ck(p6e.selectedChapterLanguage === "es",
               "case6e: Chapter Mode must initialize from Manga.chapterDefaultLanguage()")
            ck(fakeMangaEngine.catalogueCalls === 1 && fakeMangaEngine.lastLanguage === "es",
               "case6e: initial Chapter Mode request must use the configured default language")

            // ── Case 6f: a live configuration change invalidates and reloads the active route ──
            var callsBeforeDefaultChange = fakeMangaEngine.catalogueCalls
            fakeMangaEngine.setChapterDefaultLanguage("pt")
            ck(p6e.selectedChapterLanguage === "pt",
               "case6f: an unqualified page must adopt a live default-language change")
            ck(fakeMangaEngine.catalogueCalls === callsBeforeDefaultChange + 1
               && fakeMangaEngine.lastLanguage === "pt",
               "case6f: default-language changes must reload the active Chapter Mode route")

            // ── Case 6g: explicit in-page language survives a later default change ──
            p6e._selectChapterLanguage("fr")
            var callsBeforeExplicitDefaultChange = fakeMangaEngine.catalogueCalls
            fakeMangaEngine.setChapterDefaultLanguage("en")
            ck(p6e.selectedChapterLanguage === "fr",
               "case6g: an explicit in-page language must not be overwritten by a default change")
            ck(fakeMangaEngine.catalogueCalls === callsBeforeExplicitDefaultChange + 1
               && fakeMangaEngine.lastLanguage === "fr",
               "case6g: configuration changes still reload the explicitly selected route")

            // ── Case 6h: a later Chapter Mode entry uses the new default when no choice exists ──
            fakeMangaEngine.chapterDefaultLanguage = "es"
            var p6h = makePage("1", "Monster")
            p6h.extensionsRef = fakeExtensions
            p6h.mangaEngineRef = fakeMangaEngine
            p6h._enterChapterMode()
            ck(p6h.selectedChapterLanguage === "es"
               && fakeMangaEngine.lastLanguage === "es",
               "case6h: a new Chapter Mode page must use the current configured default")
            p6h._enterTankobanMode()
            fakeMangaEngine.setChapterDefaultLanguage("pt")
            p6h._enterChapterMode()
            ck(p6h.selectedChapterLanguage === "pt"
               && fakeMangaEngine.lastLanguage === "pt",
               "case6h: the next Chapter Mode entry must see a live default change")

            // ── Case 6i: unsupported configured languages never silently become English ──
            fakeMangaEngine.chapterDefaultLanguage = "xx"
            fakeMangaEngine.catalogueCalls = 0
            var p6i = makePage("1", "Monster")
            p6i.extensionsRef = fakeExtensions
            p6i.mangaEngineRef = fakeMangaEngine
            p6i._enterChapterMode()
            ck(p6i.selectedChapterLanguage !== "en",
               "case6i: an unsupported configured language must not fall back to English")
            ck(fakeMangaEngine.catalogueCalls === 0,
               "case6i: an unsupported configured language must make zero provider calls")

            // 7a: the catalog starts not-ready — malId-open must stay honestly unresolved,
            // never a guess (mirrors resolve()'s existing "id>0 but no row found" path).
            malCatalog.setReady(false)
            malCatalog.rows = ({ "1": monsterRow })
            malCatalog.titleMap = ({})
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p7 = makePage("1", "Monster")
            ck(p7.resolvedMalId === 0, "case7a: a not-ready catalog must leave resolvedMalId 0")
            ck(p7.seriesId === "", "case7a: a not-ready catalog must leave seriesId empty")
            ck(p7.hasShelf === false, "case7a: a not-ready catalog must show no shelf")
            ck(p7.loading === false, "case7a: the page must still reveal (never hang) while not-ready")

            // 7b: flip ready true and emit readyChanged (the exact signal MangaSeries.qml's
            // Connections listens for) -> the page re-resolves EXACTLY once and renders the row.
            var callsBeforeWake = malCatalog.mangaByIdCalls
            malCatalog.setReady(true)
            var expectResolvedAfterWake = harness._negControlFlipWake ? 0 : 1
            ck(p7.resolvedMalId === expectResolvedAfterWake,
               "case7b: flipping ready must re-resolve to malId " + expectResolvedAfterWake +
               ", got " + p7.resolvedMalId)
            ck(p7.seriesId === (expectResolvedAfterWake ? "mal:1" : ""),
               "case7b: flipping ready must set seriesId accordingly, got " + p7.seriesId)
            ck(malCatalog.mangaByIdCalls > callsBeforeWake,
               "case7b: resolve() must have actually re-run mangaById after the ready flip")

            // ── Case 8: an already-resolved page must NOT re-resolve on a further pulse ──
            var callsAfterWake = malCatalog.mangaByIdCalls
            malCatalog.readyChanged()   // pulse again — p7 is already resolved
            ck(malCatalog.mangaByIdCalls === callsAfterWake,
               "case8: an already-resolved page must not re-run resolve() on a further readyChanged pulse")

            // Remote enrichment only accepts explicit MAL IDs when the local row is absent.
            malCatalog.setReady(false)
            tankCatalog.remoteEnabled = true
            tankCatalog.readyVal = false
            tankCatalog.infoMap = ({})
            var remotePage = makePage("13", "Explicit MAL Series")
            ck(remotePage.resolvedMalId === 13, "remote: explicit MAL ID survives absent local metadata")
            ck(remotePage.seriesId === "mal:13", "remote: canonical MAL namespace unchanged")
            ck(tankCatalog.requestedId === 13, "remote: explicit MAL ID requests facts")
            tankCatalog.infoMap["13"] = { volumeCount: 2, countBasis: "mal" }
            tankCatalog.volumeMap = ({ "13": [{ number: "1", cover: "", name: "One" },
                                              { number: "2", cover: "", name: "Two" }] })
            tankCatalog.seriesChanged(99)
            ck(remotePage.hasShelf === false, "remote: stale other-series reply cannot refresh shelf")
            tankCatalog.seriesChanged(13)
            ck(remotePage.hasShelf === true, "remote: matching response refreshes shelf")
            ck(volService.volMap["mal:13"].length === 2, "remote: same existing volume owner receives facts")
            var unresolved = makePage("", "Unmatched remote title")
            ck(unresolved.resolvedMalId === 0 && unresolved.seriesId === "",
               "remote: title-only missing metadata remains unresolved")
            // Arc 74-B: test production QML with injected profile-local persistent owner.
            malCatalog.setReady(true)
            malCatalog.rows = ({ "1": monsterRow, "13": colorRow })
            tankCatalog.readyVal = true
            tankCatalog.remoteEnabled = false
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18 }, "13": { "volumeCount": 113 } })
            fakeMangaEngine.chapterDefaultLanguage = "en"
            fakeExtensions.setTankoyomiEnabled(true)
            var first = makePage("1", "Monster", {preferencesRef: fakePrefs})
            first.mangaEngineRef = fakeMangaEngine
            first.extensionsRef = fakeExtensions
            first._enterChapterMode()
            ck(first.selectedChapterLanguage === "en", "arc74 initial global default")
            first._selectChapterLanguage("pt")
            ck(fakePrefs.mangaLanguageForSeries("mal:1") === "pt", "arc74 must save manga override")
            var reopened = makePage("1", "Monster", {preferencesRef: fakePrefs})
            reopened.mangaEngineRef = fakeMangaEngine
            reopened.extensionsRef = fakeExtensions
            reopened._enterChapterMode()
            ck(reopened.selectedChapterLanguage === "pt", "arc74 same manga must retain Portuguese")
            fakeMangaEngine.setChapterDefaultLanguage("es")
            ck(reopened.selectedChapterLanguage === "pt", "arc74 series override must beat changed global default")
            var separate = makePage("13", "One Piece", {preferencesRef: fakePrefs})
            separate.mangaEngineRef = fakeMangaEngine
            separate.extensionsRef = fakeExtensions
            separate._enterChapterMode()
            ck(separate.selectedChapterLanguage === "es", "arc74 another manga must use global default")
            first.malId = "13"
            first.seriesTitle = "One Piece"
            first._enterChapterMode()
            ck(first.selectedChapterLanguage === "es", "arc74 reused page must not leak Portuguese")
            reopened.preferencesRef = fakeOtherProfile
            ck(reopened.selectedChapterLanguage === "es", "arc74 profile switch must use new profile")
            reopened.preferencesRef = fakePrefs
            ck(reopened.selectedChapterLanguage === "pt", "arc74 switching back restores own profile")
            var languageCombo = findNamed(reopened, "mangaLanguageCombo", true)
            ck(languageCombo !== null && languageCombo.count === 5,
               "arc74 selector offers preferred/default plus four real languages")
            ck(languageCombo.currentIndex === 3, "arc74 selector reflects the saved Portuguese override")
            var languageSettingsCount = 0
            reopened.languageSettingsRequested.connect(function() { languageSettingsCount++ })
            var languageSettingsShortcut = findNamed(reopened, "mangaLanguageSettingsShortcut", true)
            ck(languageSettingsShortcut !== null && languageSettingsShortcut.visible,
               "arc74 existing language settings shortcut is discoverable")
            languageSettingsShortcut.clicked()
            ck(languageSettingsCount === 1, "arc74 shared-header shortcut reaches series navigation")
            languageCombo.activated(0) // simulate the public ComboBox default-choice signal
            ck(fakePrefs.mangaLanguageForSeries("mal:1") === "", "arc74 reset removes only series override")
            ck(reopened.selectedChapterLanguage === "es", "arc74 reset adopts global default")
            fakePrefs.rejectWrites = true
            reopened._selectChapterLanguage("fr")
            ck(reopened.selectedChapterLanguage === "es", "arc74 rejected disk write must not change selection")
            fakePrefs.rejectWrites = false
            fakePrefs.overrides = ({"mal:1": "zz"})
            fakePrefs.mangaLanguagesChanged()
            ck(reopened.selectedChapterLanguage === "zz", "arc74 unavailable override remains explicit")
            ck(reopened.chaptersError.indexOf("unavailable") >= 0,
               "arc74 unavailable language must not silently fall back")
            // Both title-page implementations must preserve this contract.
            fakePrefs.overrides = ({})
            fakePrefs.mangaLanguagesChanged()
            pageComp = Qt.createComponent("../qml/TankobanSeriesView.qml")
            ck(pageComp.status !== Component.Error, "arc74 rebuilt series component must load")
            fakeMangaEngine.chapterDefaultLanguage = "es"
            var rebuilt = makePage("1", "Monster", {preferencesRef: fakePrefs})
            rebuilt.mangaEngineRef = fakeMangaEngine
            rebuilt.extensionsRef = fakeExtensions
            rebuilt._enterChapterMode()
            ck(rebuilt.selectedChapterLanguage === "es", "arc74 rebuilt title starts global Spanish")
            rebuilt._selectChapterLanguage("pt")
            ck(fakePrefs.mangaLanguageForSeries("mal:1") === "pt", "arc74 rebuilt title writes override")
            var rebuiltAgain = makePage("1", "Monster", {preferencesRef: fakePrefs})
            rebuiltAgain.mangaEngineRef = fakeMangaEngine
            rebuiltAgain.extensionsRef = fakeExtensions
            rebuiltAgain._enterChapterMode()
            ck(rebuiltAgain.selectedChapterLanguage === "pt", "arc74 rebuilt title reopens Portuguese")
            var rebuiltCombo = findNamed(rebuiltAgain, "mangaLanguageCombo", true)
            ck(rebuiltCombo !== null && rebuiltCombo.count === 5 && rebuiltCombo.currentIndex === 3,
               "arc74 rebuilt selector reflects saved Portuguese override")
            rebuiltAgain.languageSettingsRequested.connect(function() { languageSettingsCount++ })
            var rebuiltSettingsShortcut = findNamed(rebuiltAgain, "mangaLanguageSettingsShortcut", true)
            ck(rebuiltSettingsShortcut !== null && rebuiltSettingsShortcut.visible,
               "arc74 rebuilt language settings shortcut is discoverable")
            rebuiltSettingsShortcut.clicked()
            ck(languageSettingsCount === 2, "arc74 rebuilt shortcut reaches series navigation")
            rebuilt.malId = "13"
            rebuilt.seriesTitle = "One Piece"
            rebuilt._enterChapterMode()
            ck(rebuilt.selectedChapterLanguage === "es", "arc74 rebuilt title rejects cross-series bleed")
            console.log("ARC74_B_QML_LANGUAGE_OK")
            // Arc 74-C: a saved chapter/volume is a direct CTA only when the
            // actual local download is ready; never create an invented position.
            var savedEntry = ({ id: "mal:1", kind: "manga", title: "Monster",
                sub: "Chapter 47", resume: { chapterId: "qualified:chapter-47",
                                            page: 12, pageFraction: 0.4 } })
            fakeContinueProgress.setRecords(({
                "manga|mal:1": savedEntry,
                "tankoban|mal:1": { id: "mal:1", kind: "tankoban",
                                    resume: {chapterId: "volume-one", page: 4} }
            }))
            fakeContinueDownloads.states = ({ "qualified:chapter-47": { "state": "done" } })
            volService.volMap = ({ "mal:1": [
                { id: "volume-one", number: "1", state: "ready" }
            ] })
            tankCatalog.infoMap = ({ "1": { volumeCount: 1, countBasis: "catalogue" } })
            for (var variant = 0; variant < 2; variant++) {
                pageComp = Qt.createComponent(variant === 0
                    ? "../qml/MangaSeries.qml" : "../qml/TankobanSeriesView.qml")
                ck(pageComp.status !== Component.Error, "arc74-c variant component loads")
                var view = makePage("1", "Monster", {progressRef: fakeContinueProgress,
                    downloadsRef: fakeContinueDownloads, preferencesRef: fakePrefs})
                view.mangaEngineRef = fakeMangaEngine
                view.extensionsRef = fakeExtensions
                view._enterChapterMode()
                ck(view.savedChapterReady, "arc74-c saved chapter must be ready")
                ck(view.savedChapterCtaLabel === "Continue Chapter 47 · p. 12",
                   "arc74-c truthful chapter/page label")
                var buttonName = variant === 0
                    ? "mangaChapterContinueAction" : "tankobanChapterContinueAction"
                var chapterButton = findNamed(view, buttonName)
                ck(chapterButton && chapterButton.visible,
                   "arc74-c saved downloaded chapter CTA visible " + buttonName)
                var resumed = null
                view.resumeSavedRequested.connect(function(record) { resumed = record })
                ck(view.resumeSavedChapter(), "arc74-c resume dispatch succeeds")
                ck(resumed && resumed.resume.chapterId === "qualified:chapter-47"
                   && resumed.resume.page === 12 && resumed.resume.pageFraction === 0.4,
                   "arc74-c original qualified chapter and page passed unchanged")
                ck(fakeContinueProgress.get("manga", "mal:1").resume.page === 12,
                   "arc74-c CTA must not write or advance saved progress")
                fakeContinueDownloads.states = ({})
                fakeContinueDownloads.removed("qualified:chapter-47")
                ck(!view.savedChapterReady, "arc74-c missing download disables direct Continue")
                ck(!chapterButton.visible,
                   "arc74-c missing download must hide direct action")
                var unavailableName = variant === 0
                    ? "mangaChapterSavedUnavailable" : "tankobanSavedChapterUnavailable"
                var unavailable = findNamed(view, unavailableName)
                ck(unavailable && unavailable.visible,
                   "arc74-c missing download shows truthful recovery copy")
                ck(!view.resumeSavedChapter(),
                   "arc74-c missing target cannot emit resume")
                fakeContinueDownloads.states = ({ "qualified:chapter-47": { "state": "done" } })
                fakeContinueDownloads.finished("qualified:chapter-47")
                ck(view.savedChapterReady, "arc74-c completed download re-enables Continue")
                // The fake volume service does not synthesize canonical download IDs
                // as the real native owner does; publish the ready row explicitly.
                volService.volMap = ({ "mal:1": [
                    {id: "volume-one", number: "1", state: "ready"}
                ] })
                volService.volumesChanged("mal:1")
                ck(view.readCtaLabel === "Continue Vol. 1",
                   "arc74-c existing volume Resume CTA reads nested saved Progress")
                view.destroy()
            }
            console.log("ARC74_C_QML_CONTINUE_OK")
            watchdog.stop()
            console.log("MANGA_SERIES_CATALOGUE_OK")
            Qt.exit(0)
        } catch (e) {
            watchdog.stop()
            console.log("MANGA_SERIES_CATALOGUE_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Timer { interval: 100; running: true; repeat: false; onTriggered: harness.runChecks() }
    Timer { id: watchdog; interval: 8000; running: true
        onTriggered: { console.log("MANGA_SERIES_CATALOGUE_FAIL timeout"); Qt.exit(1) } }
}
