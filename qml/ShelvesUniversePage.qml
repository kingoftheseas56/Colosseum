// Shelves universe (Arc 58): THE universe page for every universe that is not a book saga.
// Born for the anime universes after Hemanth's ruling on the story grid ("the structure is
// fundamentally flawed in terms of navigation, division of mediums and overall flow"), then
// generalised on 2026-10-05 (Arc 58-E) to any universe that declares `shelves`.
// The page divides by MEDIUM: one glass panel per shelf (Films, Series, Books, Manga, Anime),
// each a single row of same-size tiles that scrolls sideways, and the tabs name the shelves.
// The franchise's own grouping (Bond's actors, Marvel's phases, a film era) becomes the
// labelled RUNS inside the row.
// A panel is only as wide as its row, and short panels share a line. A MAGAZINE's lineup
// (form "table") is a numbered table instead of covers. No grid, no side panel.
// Layout authority: Brotherhood/agents/colosseum-universe-shelves-all-mock.html, 1280x720.
// Data: SagaApi.loadShelves, from the universe's curated `shelves` (or derived for anime).
import QtQuick
import QtQuick.Controls
import "SagaApi.js" as Saga
import "Universes.js" as UDB

Item {
    id: root
    objectName: "shelvesUniversePage"
    anchors.fill: parent

    // shell contract — the same verbs the other template pages speak (Main's template
    // layer wires them; a page lacks only the signals it has no use for)
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var item)          // anime/film tile → A4's TheatreSeries
    signal seriesRequested(string title, var malId)   // manga / Read → Tankoban, by its pinned MAL id when curated
    signal bookRequested(var book)           // book tile / Read → the Biblio book page
    signal continueResumeRequested(var entry)   // Continue circle → resume in the owning world
    signal continueDetailRequested(var entry)   // Continue tile → its title page
    signal universeRequested(string name)       // a "More universes" card

    Theme { id: theme }
    FontLoader { id: shelfDisplay; source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { id: shelfUi; source: "../assets/fonts/Switzer-Regular.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Medium.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Semibold.otf" }
    readonly property string uiFace: shelfUi.name
    readonly property string displayFace: shelfDisplay.name
    property bool reducedMotion: true
    property int directorLoadedGenerationInternal: 0
    property int directorMemberRevisionInternal: 0
    readonly property var guide: UDB.configFor(root.universeName || root.uni.name)
    readonly property color worldColor: root.uni.color || root.guide.c1 || "#221c30"
    readonly property color paper: theme.ink
    readonly property color hair: Qt.rgba(1, 1, 1, 0.09)
    readonly property color goldEdge: Qt.rgba(0.94, 0.77, 0.29, 0.7)
    readonly property real gutter: width < 700 ? 24 : 64
    readonly property bool compact: width < 800
    property int loadGeneration: 0

    property var uni: ({ name: "", category: "", blurb: "", blurbSource: "", banner: "", wallpaper: "",
                         color: "", shelves: [] })

    // Arc 63-D1 diagnostic seam: the wallpaper is shot-defining image content. Expose the
    // existing Image status without turning it into the final surface-level readiness gate.
    readonly property string directorWallpaperSource: String(root.uni.wallpaper || root.uni.banner || "")
    readonly property string directorWallpaperStatus: directorWallpaperSource.length === 0 ? "unavailable"
            : wallpaperImage.status === Image.Ready ? "ready"
            : wallpaperImage.status === Image.Error ? "error" : "loading"

    // Arc 63-D2B production readiness. The declared shot is Arc 63-B's
    // title -> final shelf glide (align:start) and return. Readiness includes only image
    // delegates whose rendered pixels can intersect that vertical sweep, after every
    // intermediate clipping ancestor has been applied.
    function directorShelfWorkCountInternal(list) {
        let n = 0
        ;(list || []).forEach(function(shelf) {
            ;(shelf && shelf.runs ? shelf.runs : []).forEach(function(run) {
                n += root.runWorks(run).length
            })
        })
        return n
    }

    readonly property int directorExpectedDelegateCountInternal:
        continueRepeater.count
        + root.directorShelfWorkCountInternal(root.rowShelves)
        + root.directorShelfWorkCountInternal(root.tableShelves)
        + galleryRepeater.count
        + siblingRepeater.count

    readonly property Item directorShotEndSectionInternal:
        root.activeShelves.length > 0 ? root.sectionAt(root.activeShelves.length - 1) : null
    readonly property string directorShotEndObjectName:
        root.directorShotEndSectionInternal
        ? String(root.directorShotEndSectionInternal.objectName || "") : ""
    readonly property real directorShotEndContentYInternal:
        root.directorShotEndSectionInternal
        ? root.directorShotEndSectionInternal.mapToItem(page.contentItem, 0, 0).y : -1
    readonly property real directorShotSweepBottomInternal:
        root.directorShotEndContentYInternal >= 0 && page.height > 0 && page.contentHeight > 0
        ? Math.min(page.contentHeight, root.directorShotEndContentYInternal + page.height) : -1

    function directorIsEffectivelyVisibleInternal(item) {
        for (let node = item; node; node = node.parent) {
            if (node.visible === false)
                return false
        }
        return true
    }

    function directorRectInPageInternal(item) {
        const p0 = item.mapToItem(page.contentItem, 0, 0)
        const p1 = item.mapToItem(page.contentItem, item.width, 0)
        const p2 = item.mapToItem(page.contentItem, 0, item.height)
        const p3 = item.mapToItem(page.contentItem, item.width, item.height)
        return {
            left: Math.min(p0.x, p1.x, p2.x, p3.x),
            right: Math.max(p0.x, p1.x, p2.x, p3.x),
            top: Math.min(p0.y, p1.y, p2.y, p3.y),
            bottom: Math.max(p0.y, p1.y, p2.y, p3.y)
        }
    }

    function directorIntersectRectInternal(a, b) {
        return {
            left: Math.max(a.left, b.left),
            right: Math.min(a.right, b.right),
            top: Math.max(a.top, b.top),
            bottom: Math.min(a.bottom, b.bottom)
        }
    }

    function directorItemCanAppearInternal(item, sweepBottom) {
        if (!item || item.width <= 0 || item.height <= 0 || !isFinite(sweepBottom) || sweepBottom <= 0)
            return false
        if (!root.directorIsEffectivelyVisibleInternal(item))
            return false

        let rect = root.directorRectInPageInternal(item)
        for (let node = item.parent; node && node !== page.contentItem; node = node.parent) {
            if (node.visible === false)
                return false
            if (node.clip === true) {
                rect = root.directorIntersectRectInternal(rect, root.directorRectInPageInternal(node))
                if (rect.right <= rect.left || rect.bottom <= rect.top)
                    return false
            }
        }

        rect = root.directorIntersectRectInternal(rect, {
            left: 0, right: page.width, top: 0, bottom: sweepBottom
        })
        return rect.right > rect.left && rect.bottom > rect.top
    }

    function directorCollectMembersInternal(item, out) {
        if (!item)
            return
        if (item["directorReadinessMember"] === true)
            out.push(item)
        const children = item.children || []
        for (let i = 0; i < children.length; ++i)
            root.directorCollectMembersInternal(children[i], out)
    }

    function directorSnapshotInternal() {
        root.directorMemberRevisionInternal
        const diagnostics = []
        const requiredMembers = []
        const identityRows = []
        const observationRows = []
        const allMembers = []
        root.directorCollectMembersInternal(root, allMembers)

        const currentLoad = root.universeName.length > 0
                && root.directorLoadedGenerationInternal === root.loadGeneration
                && String(root.uni.name || "") === root.universeName
        let enumerable = currentLoad
                && root.directorShotEndSectionInternal !== null
                && root.directorShotEndObjectName.length > 0
                && root.directorShotSweepBottomInternal > 0
                && allMembers.length === root.directorExpectedDelegateCountInternal
        let required = 0
        let ready = 0
        let failed = 0

        function diagnose(kind, key, source, status, reason) {
            if (diagnostics.length < 8)
                diagnostics.push({ kind: kind, key: key, source: source,
                                   status: status, reason: reason })
        }

        function account(kind, key, expectedSource, actualSource, status) {
            required += 1
            requiredMembers.push({ kind: kind, key: key, source: expectedSource, status: status })
            identityRows.push([kind, key, expectedSource, actualSource])
            observationRows.push([kind, key, expectedSource, actualSource, status])
            if (!key) {
                failed += 1
                diagnose(kind, key, expectedSource, status, "missing-key")
                return
            }
            if (!expectedSource) {
                failed += 1
                diagnose(kind, key, expectedSource, "unavailable", "missing-source")
                return
            }
            if (actualSource !== expectedSource) {
                failed += 1
                diagnose(kind, key, expectedSource, status, "source-assignment-mismatch")
                return
            }
            if (status === "ready") {
                ready += 1
                return
            }
            if (status === "error" || status === "unavailable") {
                failed += 1
                diagnose(kind, key, expectedSource, status,
                         status === "error" ? "image-error" : "image-unavailable")
                return
            }
            diagnose(kind, key, expectedSource, status, "not-ready")
        }

        if (!currentLoad) {
            diagnose("surface", root.universeName, "", "unavailable", "generation-or-universe-mismatch")
        } else {
            account("wallpaper", "wallpaper:" + root.universeName,
                    String(root.directorWallpaperSource || ""),
                    String(wallpaperImage.source || ""),
                    String(root.directorWallpaperStatus || "loading"))
        }

        if (currentLoad && allMembers.length !== root.directorExpectedDelegateCountInternal) {
            diagnose("surface", root.universeName, "", "unavailable",
                     "delegate-cohort-not-enumerable:"
                     + allMembers.length + "/" + root.directorExpectedDelegateCountInternal)
        }
        if (currentLoad && root.directorShotEndSectionInternal === null)
            diagnose("surface", root.universeName, "", "unavailable", "final-shelf-not-enumerable")
        if (currentLoad && root.directorShotSweepBottomInternal <= 0)
            diagnose("surface", root.universeName, "", "unavailable", "shot-range-not-enumerable")

        if (enumerable) {
            for (let i = 0; i < allMembers.length; ++i) {
                const member = allMembers[i]
                if (!root.directorItemCanAppearInternal(member, root.directorShotSweepBottomInternal))
                    continue
                account(String(member["directorAssetKind"] || "asset"),
                        String(member["directorAssetKey"] || ""),
                        String(member["directorArtSource"] || ""),
                        String(member["directorActualArtSource"] || ""),
                        String(member["directorArtStatus"] || "loading"))
            }
        }

        const identityToken = JSON.stringify({
            generation: root.loadGeneration,
            loadedGeneration: root.directorLoadedGenerationInternal,
            requestedUniverse: root.universeName,
            loadedUniverse: String(root.uni.name || ""),
            endObject: root.directorShotEndObjectName,
            sweepBottom: Math.round(root.directorShotSweepBottomInternal * 100) / 100,
            expectedDelegates: root.directorExpectedDelegateCountInternal,
            observedDelegates: allMembers.length,
            enumerable: enumerable,
            members: identityRows
        })
        const observationToken = JSON.stringify({
            identity: identityToken,
            currentLoad: currentLoad,
            required: required,
            ready: ready,
            failed: failed,
            observations: observationRows
        })
        return {
            currentLoad: currentLoad,
            enumerable: enumerable,
            required: required,
            ready: ready,
            failed: failed,
            diagnostics: diagnostics,
            requiredMembers: requiredMembers,
            identityToken: identityToken,
            observationToken: observationToken
        }
    }

    readonly property var directorCohortSnapshotInternal: root.directorSnapshotInternal()
    readonly property string directorExpectedCohortTokenInternal:
        String(root.directorCohortSnapshotInternal.identityToken || "")
    readonly property string directorObservationTokenInternal:
        String(root.directorCohortSnapshotInternal.observationToken || "")
    property int directorReadinessEpoch: 0
    property int directorPresentedFramesInternal: 0
    readonly property int directorRequiredCount: root.directorCohortSnapshotInternal.required
    readonly property int directorReadyCount: root.directorCohortSnapshotInternal.ready
    readonly property int directorFailedCount: root.directorCohortSnapshotInternal.failed
    readonly property var directorReadinessDiagnostics: root.directorCohortSnapshotInternal.diagnostics
    readonly property var directorRequiredMembersInternal:
        root.directorCohortSnapshotInternal.requiredMembers
    readonly property int directorPresentedFrameCount: root.directorPresentedFramesInternal
    readonly property bool directorSurfaceActiveInternal: {
        const window = root.Window.window
        return root.directorIsEffectivelyVisibleInternal(root)
                && root.width > 0 && root.height > 0
                && window !== null && window.visible
    }
    readonly property bool directorAllRequiredReadyInternal:
        root.directorCohortSnapshotInternal.currentLoad
        && root.directorCohortSnapshotInternal.enumerable
        && root.directorRequiredCount > 0
        && root.directorFailedCount === 0
        && root.directorReadyCount === root.directorRequiredCount
    readonly property string directorReadinessState:
        !root.directorSurfaceActiveInternal
             || !root.directorCohortSnapshotInternal.currentLoad
             || !root.directorCohortSnapshotInternal.enumerable
             || root.directorRequiredCount === 0 ? "unavailable"
        : root.directorFailedCount > 0 ? "partial"
        : !root.directorAllRequiredReadyInternal ? "loading"
        : root.directorPresentedFramesInternal >= 2 ? "settled" : "loading"

    onDirectorExpectedCohortTokenInternalChanged: {
        root.directorReadinessEpoch += 1
        root.directorPresentedFramesInternal = 0
    }
    onDirectorObservationTokenInternalChanged: root.directorPresentedFramesInternal = 0
    onDirectorSurfaceActiveInternalChanged: root.directorPresentedFramesInternal = 0

    Connections {
        target: root.Window.window
        function onFrameSwapped() {
            if (!root.directorSurfaceActiveInternal || !root.directorAllRequiredReadyInternal) {
                root.directorPresentedFramesInternal = 0
                return
            }
            if (root.directorPresentedFramesInternal < 2)
                root.directorPresentedFramesInternal += 1
        }
    }

    // ---- the shelves are data: one panel per non-empty shelf, in entry order ----
    function runWorks(run) { return run ? (run.works || run.items || []) : [] }
    function shelfCount(shelf) {
        let n = 0
        ;(shelf && shelf.runs ? shelf.runs : []).forEach(function(r) { n += root.runWorks(r).length })
        return n
    }
    function shelfKey(shelf, index) {
        if (shelf && shelf.key) return shelf.key
        let k = String((shelf && shelf.title) || ("shelf-" + index)).toLowerCase().replace(/[^a-z0-9]+/g, "-")
        k = k.replace(/^-+|-+$/g, "")
        return k || ("shelf-" + index)
    }
    readonly property var shelves: root.uni.shelves || []
    readonly property var activeShelves: root.shelves.filter(function(s) { return root.shelfCount(s) > 0 })
    readonly property var rowShelves: root.activeShelves.filter(function(s) { return s.form !== "table" })
    readonly property var tableShelves: root.activeShelves.filter(function(s) { return s.form === "table" })
    // the section drawn for activeShelves[i], whichever kind it is
    function sectionAt(i) {
        const s = root.activeShelves[i]
        if (!s) return null
        const isTable = s.form === "table"
        let k = 0
        for (let j = 0; j < i; ++j)
            if ((root.activeShelves[j].form === "table") === isTable) k++
        return isTable ? tableRepeater.itemAt(k) : mediumRepeater.itemAt(k)
    }
    readonly property var works: {
        let out = []
        root.activeShelves.forEach(function(s) {
            ;(s.runs || []).forEach(function(r) { out = out.concat(root.runWorks(r)) })
        })
        return out
    }
    function isManga(item) { return !!item && item.medium === "manga" }
    function isBook(item) { return !!item && item.medium === "book" }
    function isScreen(item) { return !!item && (item.medium === "movie" || item.medium === "series"
                                                  || item.type === "movie" || item.type === "series") }
    function isReading(item) { return root.isManga(item) || root.isBook(item) }
    readonly property bool hasReadShelf: root.activeShelves.some(function(s) { return s.kind === "book" || s.kind === "manga" })
    readonly property bool hasScreenShelf: root.activeShelves.some(function(s) { return s.kind === "movie" || s.kind === "series" })
    // every standard page is a Universe, whatever it holds; only the four hand-made art
    // pages are Tributes (Hemanth 2026-10-05)
    readonly property string kicker: "UNIVERSE"

    // ---- identity: loader pushes rebuild every item, so selection is held by key ----
    function keyOf(item) {
        if (!item) return ""
        return item.key || (item.shelfKey ? item.shelfKey + ":" + (item.id || item.apple || item.al || item.title || "") :
                "x:" + (item.id || item.apple || item.al || item.title || ""))
    }
    function kindLabel(item) { return item && item.shelfTitle ? String(item.shelfTitle).toUpperCase() : "" }
    function canOpen(item) { return !!item && item.resolved !== false }
    function openWork(item) {
        if (!root.canOpen(item)) return
        if (root.isManga(item)) root.seriesRequested(item.query || item.title || "", item.mal || 0)
        // a book opens by its pinned Apple Books id: the full record is looked up BY ID
        else if (root.isBook(item)) Saga.resolveBook(item, function(book) { root.bookRequested(book) })
        else if (root.isScreen(item)) root.watchRequested(item)
    }

    // ---- progress: gold marks this and focus, nothing else. Tankoban keys a manga by
    //      its MAL id ("mal:<id>"), which the newer pins carry; a manga without the pin
    //      stays honestly unmatched rather than guessed by name ----
    property int progressRevision: typeof Progress !== "undefined" && Progress ? Progress.revision : 0
    // preview/test seam: [{ id, kind, title, sub, cover, progress, watched }]
    property var previewProgress: null
    readonly property var recentEntries: {
        root.progressRevision
        if (root.previewProgress) return root.previewProgress
        return typeof Progress !== "undefined" && Progress ? Progress.recent("", 200) : []
    }
    function entryMatches(entry, item) {
        if (!entry || !root.canOpen(item)) return false
        const kind = String(entry.kind || ""), id = String(entry.id || "")
        if (root.isManga(item)) return kind === "manga" && item.mal && id === "mal:" + item.mal
        if (root.isBook(item)) return kind === "book" && item.id !== undefined && id === String(item.id)
        return root.isScreen(item) && kind === "video" && item.id !== undefined && id.split(":")[0] === String(item.id)
    }
    function entryFor(item) {
        const list = root.recentEntries
        for (let i = 0; i < list.length; ++i)
            if (root.entryMatches(list[i], item)) return list[i]
        return null
    }
    function fraction(item) {
        const e = root.entryFor(item)
        if (!e) return 0
        return e.watched === true ? 1 : Math.max(0, Math.min(1, Number(e.progress) || 0))
    }
    // the Continue row: this universe's works only, newest first, one per work
    readonly property var continueItems: {
        const out = [], seen = {}
        const list = root.recentEntries
        for (let i = 0; i < list.length && out.length < 12; ++i) {
            for (let w = 0; w < root.works.length; ++w) {
                if (!root.entryMatches(list[i], root.works[w])) continue
                const key = root.keyOf(root.works[w])
                if (!seen[key] && list[i].watched !== true) { seen[key] = true; out.push(list[i]) }
                break
            }
        }
        return out
    }
    function resumeOf(wantRead) {
        const list = root.recentEntries
        for (let i = 0; i < list.length; ++i)
            for (let w = 0; w < root.works.length; ++w)
                if (root.isReading(root.works[w]) === wantRead && root.entryMatches(list[i], root.works[w])
                        && list[i].watched !== true)
                    return { item: root.works[w], entry: list[i] }
        return null
    }
    function firstOf(wantRead) {
        for (let w = 0; w < root.works.length; ++w)
            if (root.isReading(root.works[w]) === wantRead && root.canOpen(root.works[w])
                    && root.works[w].upcoming !== true)
                return root.works[w]
        return null
    }
    readonly property var resumeRead: root.resumeOf(true)
    readonly property var resumeWatch: root.resumeOf(false)
    readonly property var readTarget: (root.resumeRead ? root.resumeRead.item : null) || root.firstOf(true)
    readonly property var watchTarget: (root.resumeWatch ? root.resumeWatch.item : null) || root.firstOf(false)

    // ---- scrolling: the artwork steps back once the hero is gone, and content passing
    //      under Back and the docked tabs dissolves instead of colliding with them ----
    readonly property real heroHeight: root.compact ? 420 : 400
    readonly property real dimK: Math.max(0, Math.min(1, (page.contentY - 120) / 240))
    function revealSection(section) {
        const air = section.topPadding !== undefined ? section.topPadding : 44
        page.contentY = Math.min(Math.max(0, section.mapToItem(page.contentItem, 0, 0).y + air - 100),
                                 Math.max(0, page.contentHeight - page.height))
    }
    readonly property var tabs: {
        const out = []
        mediumRepeater.count; tableRepeater.count
        for (let i = 0; i < root.activeShelves.length; ++i) {
            const shelf = root.activeShelves[i]
            out.push({ title: shelf.tab || shelf.title, count: root.shelfCount(shelf), target: root.sectionAt(i) })
        }
        out.push({ title: "About", count: 0, target: aboutAnchor })
        return out
    }
    // THE PANEL LINES (2026-10-05): a panel is only as wide as its row needs (never
    // narrower than its heading, never wider than the content width); consecutive panels
    // that fit share one line, 24px apart, top-aligned, the tallest one's height; a row
    // wider than the content takes the full line and scrolls sideways, as always. Narrow
    // windows (compact) put every panel on its own line. Pure — computed from the
    // sections' own numbers, in medium order, nothing per-universe.
    readonly property real contentWidth: col.width - root.gutter * 2
    readonly property real panelSeam: 24
    readonly property real runBand: 26      // the height of a run's label above its tiles
    readonly property var panelLayout: {
        const sections = []
        mediumRepeater.count; tableRepeater.count
        for (let si = 0; si < root.activeShelves.length; ++si) {
            const candidate = root.sectionAt(si)
            if (candidate && candidate.visible && candidate.total > 0) sections.push(candidate)
        }
        const placed = {}
        const lines = []
        let line = null
        for (let i = 0; i < sections.length; ++i) {
            const s = sections[i]
            const w = s.naturalWidth
            const seam = line && line.items.length ? root.panelSeam : 0
            if (!line || root.compact || line.used + seam + w > root.contentWidth + 0.5) {
                line = { items: [], used: 0 }
                lines.push(line)
            }
            line.used += (line.items.length ? root.panelSeam : 0) + s.naturalWidth
            line.items.push(s)
        }
        let y = 0
        for (let k = 0; k < lines.length; ++k) {
            // a line with one labelled panel reserves the label band in all of them, so
            // every row of tiles on the line starts at the same height
            let tallest = 0, x = 0, band = false
            for (let j = 0; j < lines[k].items.length; ++j)
                if (lines[k].items[j].labelled === true) band = true
            for (let j = 0; j < lines[k].items.length; ++j) {
                const item = lines[k].items[j]
                tallest = Math.max(tallest, item.naturalHeight + (band && item.labelled === false ? root.runBand : 0))
            }
            for (let j = 0; j < lines[k].items.length; ++j) {
                const s = lines[k].items[j]
                placed[s.key] = { x: x, y: y, width: s.naturalWidth, height: tallest, band: band }
                x += s.naturalWidth + root.panelSeam
            }
            y += 44 + tallest      // the 44px gap above every line (each section's padding)
        }
        return { placed: placed, totalHeight: y }
    }
    readonly property int activeTab: {
        page.contentY; col.implicitHeight
        // the line under the chrome is the LAST line whose top has passed; when two
        // sections share a line the FIRST of them is the active tab (a shared line is
        // one y — later sections on it must not steal the tab)
        let active = 0, activeY = -Infinity
        for (let i = 0; i < root.tabs.length; ++i) {
            const t = root.tabs[i].target
            if (!t) continue
            const y = t.mapToItem(page.contentItem, 0, 0).y
            if (y - 140 <= page.contentY && y > activeY + 1) { activeY = y; active = i }
        }
        return active
    }

    KeyboardScrollController {
        id: pageKeyboardScroll
        flick: page
        arrowScrolling: false
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            root.backRequested()
            event.accepted = true
            return
        }
        if (!event.accepted)
            pageKeyboardScroll.handle(event)
    }
    Keys.onReleased: (event) => pageKeyboardScroll.handleRelease(event)

    function reload() {
        const generation = ++root.loadGeneration
        root.directorLoadedGenerationInternal = 0
        if (!root.universeName.length) return         // never load a default universe (the OP-flash lesson)
        root.selectedKey = ""
        page.contentY = 0
        Saga.loadShelves(root.universeName, function(u) {
            if (generation === root.loadGeneration && u) {
                root.uni = u
                root.directorLoadedGenerationInternal = generation
            }
        })
    }
    Component.onCompleted: { reload(); root.forceActiveFocus(Qt.TabFocusReason) }
    onUniverseNameChanged: reload()

    // selection: which work the hero actions and the shelves last pointed at (by key)
    property string selectedKey: ""

    // ---- persistent wallpaper the page floats over; Harbor's directional scrims ----
    Item {
        id: wall
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/cold-ripple.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Image {
            id: wallpaperImage
            anchors.fill: parent
            source: root.uni.wallpaper || root.uni.banner
            asynchronous: true; cache: true
            fillMode: Image.PreserveAspectCrop
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(0.067, 0.071, 0.075, 0.88) }
                GradientStop { position: 1.0; color: Qt.rgba(0.067, 0.071, 0.075, 0.36) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.45; color: Qt.rgba(0.067, 0.071, 0.075, 0.4) }
                GradientStop { position: 1.0; color: Qt.rgba(0.067, 0.071, 0.075, 0.92) }
            }
        }
        // (the glass form needs no dimming: the panels carry their own surface, so the
        //  wallpaper stays live behind the whole page — the ruled-in mock's behaviour)
    }

    Flickable {
        id: page
        objectName: "shelvesScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: col
            width: page.width
            spacing: 0

            // ===== hero: the same face as the saga page, minus the story count; the
            // facts count what the shelves hold =====
            Item {
                id: hero
                width: parent.width
                height: root.heroHeight
                Column {
                    x: root.gutter; y: 120
                    width: parent.width - root.gutter * 2
                    spacing: 18
                    Text {
                        text: root.kicker
                        color: theme.inkDim; font.family: root.uiFace
                        font.pixelSize: 11; font.letterSpacing: 3
                    }
                    Text {
                        width: parent.width
                        text: root.uni.name || root.universeName
                        color: root.paper; font.family: root.displayFace
                        font.pixelSize: root.compact ? 40 : 72
                        lineHeight: 1.05; wrapMode: Text.WordWrap
                    }
                    Flow {
                        width: parent.width; spacing: 8
                        visible: root.works.length > 0
                        Repeater {
                            model: root.facts
                            delegate: Rectangle {
                                id: fact
                                required property string modelData
                                width: factText.implicitWidth + 28; height: 30; radius: 15
                                color: theme.glassTint; border.width: 1; border.color: theme.edge
                                Text { id: factText; anchors.centerIn: parent; text: fact.modelData
                                       color: theme.inkDim; font.family: root.uiFace
                                       font.pixelSize: 12; font.weight: Font.Medium }
                            }
                        }
                    }
                    Flow {
                        width: parent.width; spacing: 12
                        ShelfAction {
                            objectName: "shelvesReadButton"
                            text: "Read"; primary: true
                            sub: root.resumeRead && root.resumeRead.entry ? String(root.resumeRead.entry.sub || "") : ""
                            visible: root.hasReadShelf
                            enabled: !!root.readTarget
                            onTriggered: root.openWork(root.readTarget)
                        }
                        ShelfAction {
                            objectName: "shelvesWatchButton"
                            text: "Watch"; primary: true
                            sub: root.resumeWatch && root.resumeWatch.entry ? String(root.resumeWatch.entry.sub || "") : ""
                            visible: root.hasScreenShelf
                            enabled: !!root.watchTarget
                            onTriggered: root.openWork(root.watchTarget)
                        }
                    }
                }
            }
            Item { width: 1; height: 46 }     // the section switcher floats here, then docks

            // ===== Continue: the house Continue tile, this universe's works only =====
            Section {
                id: continueSection
                objectName: "shelvesContinue"
                title: "Continue"
                visible: root.continueItems.length > 0
                Row {
                    spacing: 16
                    Repeater {
                        id: continueRepeater
                        model: root.continueItems.slice(0, Math.max(1, Math.floor((continueSection.width + 16) / 356)))
                        delegate: ContinueTile {
                            id: continueTile
                            required property var modelData
                            variant: "home"
                            onDirectorAssetKeyChanged: root.directorMemberRevisionInternal += 1
                            onDirectorArtSourceChanged: root.directorMemberRevisionInternal += 1
                            onDirectorActualArtSourceChanged: root.directorMemberRevisionInternal += 1
                            onDirectorArtStatusChanged: root.directorMemberRevisionInternal += 1
                            onXChanged: root.directorMemberRevisionInternal += 1
                            onYChanged: root.directorMemberRevisionInternal += 1
                            onWidthChanged: root.directorMemberRevisionInternal += 1
                            onHeightChanged: root.directorMemberRevisionInternal += 1
                            entry: continueTile.modelData
                            backdrop: wall
                            track: page.contentY
                            onResumeRequested: root.continueResumeRequested(continueTile.modelData)
                            onDetailRequested: root.continueDetailRequested(continueTile.modelData)
                        }
                    }
                }
            }

            // ===== THE SHELVES: one row per medium, divided by runs. The panels are
            // placed by root.panelLayout — a short row's panel hugs its row, and
            // consecutive short panels share a line =====
            Item {
                id: mediums
                width: col.width
                height: root.panelLayout.totalHeight
                Repeater {
                    id: mediumRepeater
                    model: root.rowShelves
                    delegate: MediumSection {
                        required property var modelData
                        required property int index
                        objectName: "shelvesMedium_" + root.shelfKey(modelData, index)
                        title: modelData.title || "Shelf"
                        kind: modelData.kind || "movie"
                        key: root.shelfKey(modelData, index)
                        runs: modelData.runs || []
                    }
                }
                Repeater {
                    id: tableRepeater
                    model: root.tableShelves
                    delegate: TableSection {
                        required property var modelData
                        required property int index
                        objectName: "shelvesTablePanel_" + root.shelfKey(modelData, index)
                        shelf: modelData
                        key: root.shelfKey(modelData, index)
                    }
                }
            }

            // ===== about: gallery, information, more universes =====
            Item { id: aboutAnchor; width: 1; height: 1 }
            Section {
                id: gallerySection
                objectName: "shelvesGallery"
                title: "Gallery"
                visible: root.stills.length > 0
                Row {
                    spacing: 24
                    Repeater {
                        id: galleryRepeater
                        model: root.stills
                        delegate: Rectangle {
                            id: still
                            required property var modelData
                            readonly property bool directorReadinessMember: true
                            readonly property string directorAssetKind: "gallery"
                            readonly property string directorAssetKey: "gallery:" + root.keyOf(still.modelData)
                            readonly property string directorArtSource: String(still.modelData.art || "")
                            readonly property string directorActualArtSource: String(stillArt.source || "")
                            readonly property string directorArtStatus: directorArtSource.length === 0 ? "unavailable"
                                    : stillArt.status === Image.Ready ? "ready"
                                    : stillArt.status === Image.Error ? "error" : "loading"
                            onDirectorAssetKeyChanged: root.directorMemberRevisionInternal += 1
                            onDirectorArtSourceChanged: root.directorMemberRevisionInternal += 1
                            onDirectorActualArtSourceChanged: root.directorMemberRevisionInternal += 1
                            onDirectorArtStatusChanged: root.directorMemberRevisionInternal += 1
                            onXChanged: root.directorMemberRevisionInternal += 1
                            onYChanged: root.directorMemberRevisionInternal += 1
                            onWidthChanged: root.directorMemberRevisionInternal += 1
                            onHeightChanged: root.directorMemberRevisionInternal += 1
                            width: (gallerySection.width - 72) / 4; height: Math.round(width * 9 / 16)
                            radius: 8; clip: true
                            color: root.worldColor
                            border.width: 1; border.color: Qt.rgba(0.97, 0.97, 0.96, 0.12)
                            Image {
                                id: stillArt
                                anchors.fill: parent; anchors.margins: 1
                                source: still.modelData.art
                                asynchronous: true; cache: true
                                sourceSize.width: 640
                                fillMode: Image.PreserveAspectCrop
                                opacity: status === Image.Ready ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
                            }
                        }
                    }
                }
            }

            Section {
                objectName: "shelvesInformation"
                title: "Information"
                Glass {
                    backdrop: wall
                    track: page.contentY
                    radius: 18
                    width: parent.width
                    height: infoCol.implicitHeight + 48
                    Column {
                        id: infoCol
                        x: 24; y: 24
                        width: parent.width - 48
                        spacing: 8
                        Text { visible: text.length > 0; width: Math.min(860, parent.width)
                               text: root.uni.blurb || ""
                               color: root.paper; font.family: root.uiFace; font.pixelSize: 15
                               lineHeight: 1.6; wrapMode: Text.WordWrap }
                        Text { visible: text.length > 0; text: root.uni.blurbSource || ""
                               color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11 }
                        Rectangle { visible: root.uni.blurb.length > 0; width: parent.width; height: 1; color: root.hair }
                        Grid {
                            topPadding: 22
                            width: parent.width
                            columns: root.compact ? 2 : 4
                            columnSpacing: 40; rowSpacing: 22
                            Repeater {
                                model: root.information
                                delegate: Column {
                                    id: infoCell
                                    required property var modelData
                                    width: (infoCol.width - (parent.columns - 1) * 40) / parent.columns
                                    spacing: 5
                                    Text { text: infoCell.modelData[0]
                                           color: theme.inkDimmer; font.family: root.uiFace
                                           font.pixelSize: 10; font.letterSpacing: 2 }
                                    Text { width: parent.width; text: infoCell.modelData[1]
                                           color: root.paper; font.family: root.uiFace; font.pixelSize: 15
                                           elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                }
            }

            Section {
                id: moreSection
                objectName: "shelvesMore"
                title: "More universes"
                visible: root.siblings.length > 0
                Row {
                    spacing: 24
                    Repeater {
                        id: siblingRepeater
                        model: root.siblings
                        delegate: Rectangle {
                            id: sibling
                            required property var modelData
                            readonly property bool directorReadinessMember: true
                            readonly property string directorAssetKind: "sibling"
                            readonly property string directorAssetKey: "sibling:" + String(sibling.modelData.name || "")
                            readonly property string directorArtSource:
                                String(sibling.modelData.wallpaper || sibling.modelData.banner || "")
                            readonly property string directorActualArtSource: String(siblingArt.source || "")
                            readonly property string directorArtStatus: directorArtSource.length === 0 ? "unavailable"
                                    : siblingArt.status === Image.Ready ? "ready"
                                    : siblingArt.status === Image.Error ? "error" : "loading"
                            onDirectorAssetKeyChanged: root.directorMemberRevisionInternal += 1
                            onDirectorArtSourceChanged: root.directorMemberRevisionInternal += 1
                            onDirectorActualArtSourceChanged: root.directorMemberRevisionInternal += 1
                            onDirectorArtStatusChanged: root.directorMemberRevisionInternal += 1
                            onXChanged: root.directorMemberRevisionInternal += 1
                            onYChanged: root.directorMemberRevisionInternal += 1
                            onWidthChanged: root.directorMemberRevisionInternal += 1
                            onHeightChanged: root.directorMemberRevisionInternal += 1
                            width: (moreSection.width - 72) / 4; height: Math.round(width * 9 / 16)
                            radius: 12; clip: true
                            color: sibling.modelData.c1 || root.worldColor
                            border.width: siblingKey.interactionActive ? 2 : 1
                            border.color: siblingKey.interactionActive ? root.goldEdge : Qt.rgba(0.97, 0.97, 0.96, 0.12)
                            Image {
                                id: siblingArt
                                anchors.fill: parent; anchors.margins: 1
                                source: sibling.modelData.wallpaper || sibling.modelData.banner || ""
                                asynchronous: true; cache: true
                                sourceSize.width: 640
                                fillMode: Image.PreserveAspectCrop
                            }
                            Rectangle {
                                anchors.fill: parent; anchors.margins: 1
                                gradient: Gradient {
                                    GradientStop { position: 0.35; color: "transparent" }
                                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.82) }
                                }
                            }
                            Column {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom; anchors.margins: 14
                                spacing: 3
                                Text { text: root.kicker; color: theme.inkDim; font.family: root.uiFace
                                       font.pixelSize: 9; font.letterSpacing: 2 }
                                Text { width: parent.width; text: sibling.modelData.name
                                       color: root.paper; font.family: root.displayFace; font.pixelSize: 19
                                       elide: Text.ElideRight }
                            }
                            KeyboardAction {
                                id: siblingKey
                                anchors.fill: parent
                                focusRadius: 12
                                accessibleName: "Open the " + sibling.modelData.name + " universe"
                                onActiveFocusChanged: if (activeFocus) root.revealSection(moreSection)
                                onTriggered: root.universeRequested(sibling.modelData.name)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 96 }
        }
    }

    // ---- hero facts and the Information grid: counts read from the shelves, never
    //      written down; the catalogue line names what actually dressed the works ----
    readonly property var facts: root.activeShelves.map(function(s) {
        return root.shelfCount(s) + " " + (s.pill || s.title)
    })
    readonly property var information: {
        const out = []
        const providers = []
        root.activeShelves.forEach(function(s) {
            const items = []
            ;(s.runs || []).forEach(function(r) { root.runWorks(r).forEach(function(w) { items.push(w) }) })
            const years = items.map(function(w) { return Number(w.year) || Number(w.from) || 0 })
                               .filter(function(y) { return y > 0 }).sort(function(a, b) { return a - b })
            out.push([String(s.pill || s.title || "").toUpperCase(),
                      String(items.length) + (years.length > 1 && years[0] !== years[years.length - 1]
                        ? " · " + years[0] + " to " + years[years.length - 1] : "")])
            const provider = (s.kind === "book") ? "Apple Books" : (s.kind === "manga") ? "AniList" : "Cinemeta"
            if (providers.indexOf(provider) < 0) providers.push(provider)
        })
        out.push(["CATALOGUE", "Curation · " + providers.join(" · ")])
        return out
    }
    readonly property var stills: root.works.filter(function(item) {
        return root.isScreen(item) && root.canOpen(item) && !!item.art
    }).slice(0, 4)
    // the other anime universes with a working page
    readonly property var siblings: {
        const all = UDB.universes.concat(UDB.archive)
        return all.filter(function(u) { return (!!u.shelves || !!u.manga) && u.name !== (root.universeName || root.uni.name) })
                  .slice(0, 4)
    }

    // content scrolling up dissolves under the chrome band instead of colliding with
    // Back and the docked tabs (the mock's chrome-band, drawn in as the app wears it)
    Rectangle {
        z: 16
        width: parent.width; height: 112
        visible: root.dimK > 0.01
        opacity: root.dimK
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.067, 0.071, 0.075, 1.0) }
            GradientStop { position: 0.62; color: Qt.rgba(0.067, 0.071, 0.075, 0.94) }
            GradientStop { position: 1.0; color: Qt.rgba(0.067, 0.071, 0.075, 0.0) }
        }
    }

    // ===== the section switcher names the MEDIUMS. Rides the page under the hero, then
    // docks beside Back; the active tab follows the scroll =====
    Glass {
        id: tabBar
        objectName: "shelvesTabs"
        z: 18
        backdrop: wall
        radius: 23
        // riding the page it sits under the hero; from 120px down to 70px (just clear of
        // Back's band) it slides right, so it is beside Back before it could pass under it
        readonly property real ride: root.heroHeight - page.contentY
        readonly property bool docked: ride <= 22
        x: root.gutter + 118 * Math.max(0, Math.min(1, (120 - ride) / 50))
        y: Math.max(22, ride)
        width: tabRow.width + 8; height: 46
        Row {
            id: tabRow
            x: 4; y: 5
            spacing: 2
            Repeater {
                model: root.tabs
                delegate: Rectangle {
                    id: tab
                    required property var modelData
                    required property int index
                    readonly property bool active: root.activeTab === tab.index
                    width: tabText.implicitWidth + 36; height: 36; radius: 18
                    color: tab.active ? root.paper : (tabKey.hovered ? theme.glassTint : "transparent")
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            id: tabText
                            text: tab.modelData.title
                            color: tab.active ? "#090c13" : (tabKey.hovered ? root.paper : theme.inkDim)
                            font.family: root.uiFace; font.pixelSize: 13
                            font.weight: tab.active ? Font.DemiBold : Font.Medium
                        }
                        Text {
                            visible: tab.modelData.count > 0
                            text: tab.modelData.count
                            color: tab.active ? Qt.rgba(9 / 255, 12 / 255, 19 / 255, 0.55) : theme.inkDimmer
                            font.family: root.uiFace; font.pixelSize: 11
                        }
                    }
                    KeyboardAction {
                        id: tabKey; anchors.fill: parent
                        focusRadius: 18
                        accessibleName: tab.modelData.title
                        onTriggered: if (tab.modelData.target) root.revealSection(tab.modelData.target)
                    }
                }
            }
        }
    }

    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: root.backRequested()
    }
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        UniverseChromeAction {
            accessibleName: "Minimize"
            source: "../assets/icons/minimize.svg"
            onTriggered: root.minimizeRequested()
        }
        UniverseChromeAction {
            accessibleName: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                            ? "Enter fullscreen" : "Exit fullscreen"
            source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                    ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
            onTriggered: root.fullscreenRequested()
        }
        UniverseChromeAction {
            accessibleName: "Close Colosseum"
            source: "../assets/icons/power.svg"
            onTriggered: root.closeRequested()
        }
    }

    // ---- a titled section: every heading on the page wears the same face ----
    component Section: Column {
        id: section
        property string title
        x: root.gutter
        width: col.width - root.gutter * 2
        topPadding: 44
        spacing: 14
        Text {
            text: section.title; color: root.paper
            font.family: root.displayFace; font.pixelSize: 22
        }
    }

    component ShelfAction: Rectangle {
        id: actionButton
        property string text
        property string sub: ""
        property bool primary: false
        signal triggered()
        width: Math.max(128, actionRow.implicitWidth + 48); height: 48; radius: height / 2
        scale: actionKey.pressed ? 0.98 : 1
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 180 } }
        opacity: enabled ? 1 : 0.4
        color: actionKey.pressed ? theme.glassHi : (primary ? root.paper : (actionKey.hovered ? theme.glassTint : "transparent"))
        border.width: primary ? 0 : 1; border.color: theme.edge
        Row {
            id: actionRow
            anchors.centerIn: parent
            spacing: 10
            Text {
                id: actionLabel
                text: actionButton.text
                color: actionButton.primary && !actionKey.pressed ? "#090c13" : root.paper
                font.family: root.uiFace; font.pixelSize: 13; font.weight: Font.DemiBold
            }
            Text {
                visible: actionButton.sub.length > 0
                anchors.baseline: actionLabel.baseline
                text: actionButton.sub
                color: actionButton.primary && !actionKey.pressed ? Qt.rgba(0.035, 0.047, 0.075, 0.62) : theme.inkDim
                font.family: root.uiFace; font.pixelSize: 12; font.weight: Font.Medium
            }
        }
        KeyboardAction {
            id: actionKey; anchors.fill: parent
            accessibleName: actionButton.sub.length ? actionButton.text + ", " + actionButton.sub : actionButton.text
            focusRadius: actionButton.radius
            focusPrimary: actionButton.primary
            onTriggered: actionButton.triggered()
        }
    }

    // ---- one medium: a titled GLASS PANEL holding THE ONE ROW — the same surface the
    //      book sagas' pages wrap their own sections in, ruled in 2026-10-04 evening on
    //      the glass mock. The panel is as wide as its row needs (never narrower than its
    //      heading); root.panelLayout places it — short panels share a line. Its labelled
    //      RUNS carry the split — the series vs the spin-offs, a film era. One keyboard
    //      stop: Left/Right walk the works across the runs, Enter opens, an arrow at
    //      either end leaves the row ----
    component MediumSection: Column {
        id: medium
        property string title
        property string kind
        property string key
        property var runs: []
        property int total: {
            let n = 0
            for (let i = 0; i < medium.runs.length; ++i) n += root.runWorks(medium.runs[i]).length
            return n
        }
        readonly property var liveRuns: medium.runs.filter(function(r) { return root.runWorks(r).length > 0 })
        readonly property bool labelled: medium.liveRuns.length > 1
        readonly property var flatWorks: {
            let out = []
            for (let i = 0; i < medium.liveRuns.length; ++i)
                out = out.concat(root.runWorks(medium.liveRuns[i]))
            return out
        }
        // the row's natural width from the data: tiles + tile gaps + the seams between runs
        readonly property real rowWidth: {
            let w = 0
            for (let i = 0; i < medium.liveRuns.length; ++i) {
                const n = root.runWorks(medium.liveRuns[i]).length
                if (i > 0) w += 28
                w += n * root.tileW + (n - 1) * root.tileGap
            }
            return w
        }
        // never narrower than its heading needs
        readonly property real headingWidth: headingLeft.implicitWidth
        // the panel this section asks for, before line packing clamps it to the page
        readonly property real naturalWidth: Math.min(root.contentWidth,
                Math.max(root.rowInset * 2 + medium.rowWidth, root.rowInset * 2 + medium.headingWidth))
        // 20 top + 30 heading + 14 gap + the row + 22 bottom (the Glass's own rhythm)
        readonly property real naturalHeight: 64 + runsRow.height + 6 + 22
        // where panelLayout put this section (its own slot until the layout has run)
        readonly property var slot: root.panelLayout.placed[medium.key]
                || { x: 0, y: 0, width: root.contentWidth, height: medium.naturalHeight }
        // an unlabelled panel beside a labelled one leaves the label band empty
        readonly property real bandOffset: medium.slot.band === true && !medium.labelled ? root.runBand : 0
        x: root.gutter + slot.x
        y: slot.y
        width: slot.width
        height: 44 + slot.height       // the 44px gap above the line rides on the section
        topPadding: 44
        visible: medium.total > 0

        Glass {
            id: mediumPanel
            backdrop: wall
            track: page.contentY
            radius: 18
            width: parent.width
            height: parent.height - 44   // fill the line's shared height below the gap
            clip: true

            // the heading carries the count — the tab, the fact pill and the shelf agree
            Item {
                x: 24; y: 20
                width: parent.width - 48; height: 30
                Row {
                    id: headingLeft
                    spacing: 12
                    Text { text: medium.title; color: root.paper
                           font.family: root.displayFace; font.pixelSize: 22
                           anchors.baseline: countText.baseline }
                    Text { id: countText; text: medium.total
                           color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 12 }
                }
            }

                Item {
                    id: shelfWrap
                    // the row keeps to its panel: it starts at the heading's left inset and
                    // ends at the panel's right edge (the glass mock's contained shelf)
                    x: 24; y: 64 + medium.bandOffset
                    width: parent.width - 24
                    height: runsRow.height + 6

                Flickable {
                    id: shelfFlick
                    objectName: "shelvesRow_" + medium.key
                    onContentXChanged: root.directorMemberRevisionInternal += 1
                    y: 3
                    width: parent.width
                    height: runsRow.height
                    contentWidth: runsRow.width + root.rowInset
                    clip: true
                    interactive: runsRow.width + root.rowInset > width
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    function pageBy(direction) {
                        slide.stop()
                        slide.to = Math.max(0, Math.min(contentWidth - width, contentX + direction * 4 * 180))
                        slide.start()
                    }
                    NumberAnimation {
                        id: slide; target: shelfFlick; property: "contentX"
                        duration: root.reducedMotion ? 0 : 300; easing.type: Easing.OutCubic
                    }

                    Row {
                        id: runsRow
                        spacing: 28
                        Repeater {
                            id: runsRepeater
                            model: medium.liveRuns
                            delegate: Column {
                                id: run
                                required property var modelData
                                required property int index
                                // where this run's works start in the row's flat order
                                readonly property int runBase: {
                                    let base = 0
                                    for (let i = 0; i < run.index; ++i) base += root.runWorks(medium.liveRuns[i]).length
                                    return base
                                }
                                // the keyboard walker's seams into this run's works
                                readonly property int workCount: root.runWorks(run.modelData).length
                                function workAt(i) { return workRepeater.itemAt(i) }
                                spacing: 0
                                // the run's label rides above its tiles, hairline after
                                Item {
                                    visible: medium.labelled
                                    width: runTiles.width; height: root.runBand
                                    Row {
                                        spacing: 12
                                        Text {
                                            id: runLabel
                                            text: String(run.modelData.label).toUpperCase()
                                            color: theme.inkDimmer; font.family: root.uiFace
                                            font.pixelSize: 10; font.letterSpacing: 2
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Math.max(12, runTiles.width - runLabel.implicitWidth - 12)
                                            height: 1; color: Qt.rgba(1, 1, 1, 0.12)
                                        }
                                    }
                                }
                                Row {
                                    id: runTiles
                                    spacing: root.tileGap
                                    Repeater {
                                        id: workRepeater
                                        model: root.runWorks(run.modelData)
                                        delegate: Column {
                                            id: work
                                            required property var modelData
                                            required property int index
                                            width: root.tileW
                                            spacing: 0
                                            ShelfTile {
                                                item: work.modelData
                                                current: root.selectedKey === root.keyOf(work.modelData)
                                                keyboardSelected: rowFocus.activeFocus
                                                                 && rowFocus.currentIndex === run.runBase + work.index
                                                onClicked: root.openWork(work.modelData)
                                                onHoveredIn: root.selectedKey = root.keyOf(work.modelData)
                                            }
                                            // the LIVE ACTION plate already says it; the caption drops the repeat
                                            Text { topPadding: 10; width: parent.width
                                                   text: work.modelData.live === true
                                                         ? String(work.modelData.title).replace(/\s+[—-]\s+Live Action$/i, "")
                                                         : work.modelData.title
                                                   color: root.paper; font.family: root.uiFace; font.pixelSize: 12
                                                   lineHeight: 1.35; wrapMode: Text.WordWrap
                                                   maximumLineCount: 2; elide: Text.ElideRight }
                                            Text { visible: text.length > 0; topPadding: 3; width: parent.width
                                                   text: work.modelData.year ? String(work.modelData.year) : ""
                                                   color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11 }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // one keyboard stop for the whole row; the works are walked in order,
                    // across the runs (the mock's contract: Left/Right over every work)
                    Item {
                        id: rowFocus
                        objectName: "shelvesRowFocus_" + medium.key
                        anchors.fill: parent
                        property int currentIndex: medium.flatWorks.length > 0 ? 0 : -1
                        focusPolicy: medium.total > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => rowNav.handle(event)
                        onActiveFocusChanged: if (activeFocus && medium.flatWorks.length > 0) {
                            shelfFlick.contentX = Math.max(0, Math.min(shelfFlick.contentWidth - shelfFlick.width,
                                                                       shelfFlick.contentX))
                        }
                        KeyboardCollectionController {
                            id: rowNav
                            view: rowFocus
                            orientation: "horizontal"
                            count: medium.flatWorks.length
                            positionIndexFn: function(index) {
                                // the works live across the run delegates; find by flat index
                                let flat = -1, target = null
                                for (let r = 0; r < runsRepeater.count && !target; ++r) {
                                    const runItem = runsRepeater.itemAt(r)
                                    if (!runItem) continue
                                    for (let w = 0; w < runItem.workCount; ++w) {
                                        flat++
                                        if (flat === index) { target = runItem.workAt(w); break }
                                    }
                                }
                                if (!target) return
                                // a work's own x is inside its run; the row scrolls in runsRow's frame
                                const left = target.mapToItem(runsRow, 0, 0).x, right = left + target.width
                                if (left < shelfFlick.contentX) shelfFlick.contentX = Math.max(0, left - 3)
                                else if (right > shelfFlick.contentX + shelfFlick.width)
                                    shelfFlick.contentX = Math.min(shelfFlick.contentWidth - shelfFlick.width,
                                                                   right - shelfFlick.width + root.rowInset)
                            }
                            onActivated: (index) => {
                                if (index >= 0 && index < medium.flatWorks.length)
                                    root.openWork(medium.flatWorks[index])
                            }
                        }
                    }
                }

                ShelfChevron {
                    objectName: "shelvesPrev_" + medium.key
                    label: medium.title
                    y: 3 + (medium.labelled ? root.runBand : 0)
                    height: root.tileH
                    visible: shelfFlick.interactive && shelfFlick.contentX > 8
                    onTriggered: shelfFlick.pageBy(-1)
                }
                ShelfChevron {
                    objectName: "shelvesNext_" + medium.key
                    label: medium.title
                    atRight: true
                    y: 3 + (medium.labelled ? root.runBand : 0)
                    height: root.tileH
                    visible: shelfFlick.interactive && shelfFlick.contentX + shelfFlick.width < shelfFlick.contentWidth - 8
                    onTriggered: shelfFlick.pageBy(1)
                }
                }
        }
    }

    // ---- a MAGAZINE's lineup (Hemanth 2026-10-05, Weekly Shonen Jump): not a shelf of covers
    //      but a numbered TABLE in the same glass panel, always full width. Two columns of
    //      rows, numbered DOWN the first column then the second; each row is one work: its
    //      place, cover, title, author and years. One keyboard stop: Up/Down walk the rows,
    //      Left/Right change column, Enter opens ----
    component TableSection: Item {
        id: table
        property var shelf: ({})
        property string key
        readonly property string title: shelf.title || ""
        readonly property var rows: {
            let out = []
            ;(table.shelf.runs || []).forEach(function(r) { out = out.concat(root.runWorks(r)) })
            return out
        }
        readonly property int total: table.rows.length
        readonly property bool twoColumns: !root.compact && table.total > 1
        readonly property int perColumn: table.twoColumns ? Math.ceil(table.total / 2) : table.total
        readonly property real rowH: 78
        readonly property real columnSeam: 40
        readonly property real naturalWidth: root.contentWidth
        // 20 top + 30 heading + 14 gap + the rows + 14 bottom
        readonly property real naturalHeight: 64 + table.perColumn * table.rowH + 14
        readonly property var slot: root.panelLayout.placed[table.key]
                || { x: 0, y: 0, width: root.contentWidth, height: table.naturalHeight }
        property int currentIndex: 0
        function years(w) { return w.to ? w.from + " to " + w.to : (w.from ? "Since " + w.from : "") }
        // keep the keyboard's row clear of the docked tabs above and the taskbar below
        function keepRowInView(index) {
            const line = table.twoColumns ? index % table.perColumn : index
            const top = table.mapToItem(page.contentItem, 0, 0).y + 44 + 64 + line * table.rowH
            if (top < page.contentY + 110) page.contentY = Math.max(0, top - 110)
            else if (top + table.rowH > page.contentY + page.height - 96)
                page.contentY = Math.min(Math.max(0, page.contentHeight - page.height), top + table.rowH - page.height + 96)
        }
        x: root.gutter + slot.x
        y: slot.y
        width: slot.width
        height: 44 + slot.height
        visible: table.total > 0

        Glass {
            y: 44
            backdrop: wall
            track: page.contentY
            radius: 18
            width: parent.width
            height: parent.height - 44

            Item {
                x: 24; y: 20
                width: parent.width - 48; height: 30
                Row {
                    spacing: 12
                    Text { text: table.title; color: root.paper
                           font.family: root.displayFace; font.pixelSize: 22
                           anchors.baseline: tableCount.baseline }
                    Text { id: tableCount; text: table.total
                           color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 12 }
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    // a list pinned from a moving source says when it was pinned
                    text: (String(table.shelf.order || "")
                           + (table.shelf.asOf ? "  ·  as of " + table.shelf.asOf : "")).toUpperCase()
                    color: theme.inkDimmer; font.family: root.uiFace
                    font.pixelSize: 11; font.letterSpacing: 2
                }
            }

            Item {
                id: tableBody
                x: 24; y: 64
                width: parent.width - 48
                height: table.perColumn * table.rowH
                readonly property real colW: table.twoColumns ? (width - table.columnSeam) / 2 : width

                Repeater {
                    id: tableRows
                    model: table.rows
                    delegate: Item {
                        id: line
                        required property var modelData
                        required property int index
                        readonly property int col: table.twoColumns ? Math.floor(line.index / table.perColumn) : 0
                        readonly property int place: table.twoColumns ? line.index % table.perColumn : line.index
                        readonly property bool keyed: tableFocus.activeFocus && table.currentIndex === line.index
                        readonly property bool hot: lineMouse.containsMouse || line.keyed
                        readonly property real done: root.fraction(line.modelData)
                        readonly property bool directorReadinessMember: true
                        readonly property string directorAssetKind: "table"
                        readonly property string directorAssetKey:
                            "table:" + table.key + ":" + root.keyOf(line.modelData)
                        readonly property string directorArtSource: String(line.modelData.cover || "")
                        readonly property string directorActualArtSource: String(tableCover.source || "")
                        readonly property string directorArtStatus: directorArtSource.length === 0 ? "unavailable"
                                : tableCover.status === Image.Ready ? "ready"
                                : tableCover.status === Image.Error ? "error" : "loading"
                        onDirectorAssetKeyChanged: root.directorMemberRevisionInternal += 1
                        onDirectorArtSourceChanged: root.directorMemberRevisionInternal += 1
                        onDirectorActualArtSourceChanged: root.directorMemberRevisionInternal += 1
                        onDirectorArtStatusChanged: root.directorMemberRevisionInternal += 1
                        onXChanged: root.directorMemberRevisionInternal += 1
                        onYChanged: root.directorMemberRevisionInternal += 1
                        onWidthChanged: root.directorMemberRevisionInternal += 1
                        onHeightChanged: root.directorMemberRevisionInternal += 1
                        objectName: "shelvesTableRow_" + table.key + "_" + line.index
                        x: line.col * (tableBody.colW + table.columnSeam)
                        y: line.place * table.rowH
                        width: tableBody.colW; height: table.rowH
                        Rectangle {      // hairline above every row but a column's first
                            visible: line.place > 0 && !line.hot
                            width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.07)
                        }
                        Rectangle {
                            anchors.fill: parent; anchors.leftMargin: -4; anchors.rightMargin: -4
                            radius: 10
                            color: line.hot ? theme.glassTint : "transparent"
                            border.width: line.keyed ? 1 : 0; border.color: root.goldEdge
                        }
                        Text {
                            width: 34; anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignRight
                            text: line.index + 1
                            color: line.index < 3 ? root.paper : theme.inkDimmer
                            font.family: root.displayFace; font.pixelSize: 24
                        }
                        Rectangle {
                            x: 50; anchors.verticalCenter: parent.verticalCenter
                            width: 44; height: 66; radius: 3; clip: true
                            color: root.worldColor
                            border.width: 1; border.color: Qt.rgba(0.97, 0.97, 0.96, 0.12)
                            Image {
                                id: tableCover
                                anchors.fill: parent; anchors.margins: 1
                                source: line.modelData.cover || ""
                                asynchronous: true; cache: true
                                sourceSize.width: 160
                                fillMode: Image.PreserveAspectCrop
                            }
                        }
                        Column {
                            x: 110; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 110 - when.width - 28
                            spacing: 3
                            Text { width: parent.width; text: line.modelData.title
                                   color: root.paper; font.family: root.uiFace
                                   font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { width: parent.width; visible: text.length > 0
                                   text: line.modelData.by || ""
                                   color: theme.inkDimmer; font.family: root.uiFace
                                   font.pixelSize: 12; elide: Text.ElideRight }
                            Rectangle {      // where you are in it (gold marks progress only)
                                visible: line.done > 0 && line.done < 1
                                width: Math.min(parent.width, 220); height: 3; radius: 2
                                color: Qt.rgba(1, 1, 1, 0.18)
                                Rectangle { width: parent.width * line.done; height: parent.height; radius: 2; color: theme.gold }
                            }
                        }
                        Column {
                            id: when
                            anchors.right: parent.right; anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text { anchors.right: parent.right; text: table.years(line.modelData)
                                   color: theme.inkDim; font.family: root.uiFace; font.pixelSize: 12 }
                            Text { anchors.right: parent.right; visible: line.modelData.volumes > 0
                                   text: line.modelData.volumes + " volumes"
                                   color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11 }
                        }
                        MouseArea {
                            id: lineMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openWork(line.modelData)
                        }
                    }
                }

                Item {
                    id: tableFocus
                    objectName: "shelvesTableFocus_" + table.key
                    anchors.fill: parent
                    focusPolicy: table.total > 0 ? Qt.TabFocus : Qt.NoFocus
                    onActiveFocusChanged: if (activeFocus) table.keepRowInView(table.currentIndex)
                    Keys.onPressed: (event) => {
                        const step = event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Up ? -1
                                   : (event.key === Qt.Key_Right && table.twoColumns) ? table.perColumn
                                   : (event.key === Qt.Key_Left && table.twoColumns) ? -table.perColumn : 0
                        if (step) {
                            const next = table.currentIndex + step
                            if (next < 0 || next >= table.total) return      // the arrow leaves the table
                            table.currentIndex = next
                            table.keepRowInView(next)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            root.openWork(table.rows[table.currentIndex])
                            event.accepted = true
                        }
                    }
                    Accessible.role: Accessible.List
                    Accessible.name: table.title
                    Accessible.focusable: focusPolicy !== Qt.NoFocus
                }
            }
        }
    }

    // The same soft edge chevron as the saga grid: a scrim gradient with a serif arrow,
    // fading into the glass panel's own tint rather than the page black. Pointer-only by
    // design — the keyboard walks the row itself and it scrolls to follow.
    component ShelfChevron: Item {
        id: chevron
        property bool atRight: false
        property string label: ""
        signal triggered()
        width: 54; z: 5
        x: chevron.atRight && parent ? parent.width - width : 0
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(11 / 255, 12 / 255, 13 / 255, chevron.atRight ? 0 : 0.72) }
                GradientStop { position: 1; color: Qt.rgba(11 / 255, 12 / 255, 13 / 255, chevron.atRight ? 0.72 : 0) }
            }
        }
        Text {
            anchors.centerIn: parent
            text: chevron.atRight ? "›" : "‹"
            color: edgeAction.hovered ? theme.gold : theme.ink
            font.family: root.displayFace; font.pixelSize: 42
        }
        KeyboardAction {
            id: edgeAction; anchors.fill: parent
            focusPolicy: Qt.NoFocus; focusEnabled: false
            accessibleName: chevron.atRight ? "More " + chevron.label : "Earlier " + chevron.label
            onTriggered: chevron.triggered()
        }
    }

    // ---- one work: a poster tile with its caption. Same size for every medium — the
    //      source and its adaptations carry equal weight ----
    component ShelfTile: Rectangle {
        id: tile
        property var item: null
        readonly property string rowLabel: item && item.shelfTitle ? String(item.shelfTitle) : ""
        property bool current: false            // the pointer is on it (gold edge)
        property bool keyboardSelected: false   // the row's keyboard selection (house ring)
        objectName: "shelvesTile_" + root.keyOf(tile.item).replace(/[^A-Za-z0-9]+/g, "_")
        readonly property bool directorReadinessMember: true
        readonly property string directorAssetKind: "shelf"
        readonly property string directorAssetKey: root.keyOf(tile.item)
        readonly property string directorArtSource: String(tile.item ? (tile.item.cover || "") : "")
        readonly property string directorActualArtSource: String(art.source || "")
        readonly property string directorArtStatus: directorArtSource.length === 0 ? "unavailable"
                : art.status === Image.Ready ? "ready"
                : art.status === Image.Error ? "error" : "loading"
        onDirectorAssetKeyChanged: root.directorMemberRevisionInternal += 1
        onDirectorArtSourceChanged: root.directorMemberRevisionInternal += 1
        onDirectorActualArtSourceChanged: root.directorMemberRevisionInternal += 1
        onDirectorArtStatusChanged: root.directorMemberRevisionInternal += 1
        onXChanged: root.directorMemberRevisionInternal += 1
        onYChanged: root.directorMemberRevisionInternal += 1
        onWidthChanged: root.directorMemberRevisionInternal += 1
        onHeightChanged: root.directorMemberRevisionInternal += 1
        readonly property bool directorArtReady: directorArtStatus === "ready"
        readonly property real done: root.fraction(tile.item)
        signal hoveredIn()
        signal clicked()
        width: root.tileW; height: root.tileH
        radius: 3; clip: true
        color: root.worldColor
        border.width: tile.current ? 2 : 1
        border.color: tile.current ? root.goldEdge : Qt.rgba(0.97, 0.97, 0.96, 0.12)
        Item {
            anchors.fill: parent; anchors.margins: 1
            clip: true
            Text {
                anchors.centerIn: parent; width: parent.width - 24
                text: tile.item ? tile.item.title : ""
                color: theme.inkDim; font.family: root.displayFace
                font.pixelSize: 16; wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                visible: art.status !== Image.Ready
            }
            Image {
                id: art
                anchors.fill: parent
                source: tile.item ? (tile.item.cover || "") : ""
                asynchronous: true; cache: true
                sourceSize.width: 400
                fillMode: Image.PreserveAspectCrop
                opacity: status === Image.Ready ? (tile.done >= 1 ? 0.72 : 1) : 0
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
            }
            Rectangle {   // UPCOMING / LIVE ACTION plate — the work stays, marked
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 7
                visible: !!tile.item && (tile.item.upcoming === true || tile.item.live === true)
                radius: 4
                color: Qt.rgba(0, 0, 0, 0.72)
                border.width: 1; border.color: theme.edge
                width: tagText.implicitWidth + 12; height: tagText.implicitHeight + 6
                Text { id: tagText; anchors.centerIn: parent
                       text: tile.item && tile.item.live === true ? "LIVE ACTION"
                             : "UPCOMING"
                       color: root.paper
                       font.family: root.uiFace; font.pixelSize: 9; font.letterSpacing: 2 }
            }
            Rectangle {   // read / watched
                visible: tile.done >= 1
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 7
                width: 22; height: 22; radius: 11
                color: Qt.rgba(0, 0, 0, 0.66); border.width: 1; border.color: theme.edge
                Text { anchors.centerIn: parent; text: "✓"; color: root.paper
                       font.family: root.uiFace; font.pixelSize: 12; font.weight: Font.Bold }
            }
            Rectangle {   // where you are in it
                visible: tile.done > 0 && tile.done < 1
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 4; color: Qt.rgba(1, 1, 1, 0.2)
                Rectangle { width: parent.width * tile.done; height: parent.height; color: theme.gold }
            }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.canOpen(tile.item) ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: tile.hoveredIn()
            onClicked: tile.clicked()
        }
        FocusRing { anchors.fill: parent; radius: tile.radius; shown: tile.keyboardSelected }
    }

    // tile geometry: the mock's fixed 164px poster, 2:3 (plain rows do not resize)
    readonly property real tileW: 164
    readonly property real tileH: 246
    readonly property real tileGap: 16
    readonly property real rowInset: 24      // the row's inset inside its panel, both ends
}
