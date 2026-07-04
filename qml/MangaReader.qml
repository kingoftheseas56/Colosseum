// MangaReader — native QML recreation of Tankoban Electron's MangaReader.jsx (recreate, not
// redesign). Driven by ReaderEngine.js (the verbatim layout gem) + the download-fed page source.
// PASS 2: the real reader chrome — persisted prefs (QtCore Settings, app-wide), the three modals
// (chapter grid / page-jump grid / preferences), chapter-crossing prev·next, MangaPlus
// double_page_v2, page-width control, and auto-hide chrome. Reading is DOWNLOAD-FED: pages are
// the local files from Downloads.localPages(); an undownloaded chapter shows "go download it".
// PASS 3 (TB2 gap pass, 2026-07-04): live strip page tracking + bottom scrub bar with hover
// bubble, windowed strip loading (the memory diet) + paged neighbor prefetch, Ctrl+wheel zoom
// (100–260%, TB2 range) with drag/arrow pan, resume completeness (strip scroll fraction,
// per-series settings, max-seen/finished), cursor auto-hide + H/center-click chrome toggle.
// PASS 4 (2026-07-04): pairing memory (persisted spread knowledge + per-page override) so
// double-page pairs never reshuffle across reopens; session tools (bookmarks B, replay Z,
// checkpoint S, restart R, thumbnails T, shortcuts K, right-click menu, Ctrl+G); polish tail
// (split wide pages in strip, side padding, quality toggle, dim overlay, gutter shadow).
import QtQuick
import QtCore
import "ReaderEngine.js" as Engine

Item {
    id: reader
    property Item backdrop
    property string seriesTitle: ""
    property string seriesId: ""
    property string seriesCover: ""           // series cover (from the series view) — for the Continue card
    property var    chapters: []              // ALL chapters (newest-first) — for the modal + crossing
    property string chapterId: ""             // incoming open target (from the series view)
    property string chapterLabel: ""          // incoming fallback label
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    // --- western switch: the SAME reader, fed by the Comics store (GetComics
    // issues, one extracted archive per release) instead of Downloads (manga
    // chapters). Pages/status/download route through `store`; Continue records
    // under kind "comic". Everything else — pairing, resume, zoom — is shared. ---
    property bool western: false
    readonly property var store: western
        ? (typeof Comics !== "undefined" ? Comics : null)
        : (typeof Downloads !== "undefined" ? Downloads : null)
    readonly property string progressKind: western ? "comic" : "manga"

    // --- preferences (app-wide, persisted; mirrors Electron mangaPrefs) ---
    Settings {
        id: prefs
        category: "mangaReader"
        property string reading_style: "long_strip"      // long_strip|single_page|double_page|double_page_v2
        property string reading_direction: "right_left"  // left_right|right_left (manga default RTL)
        property string image_fit: "width"               // width|height
        property bool   gap: true
        property bool   dark_background: true
        property bool   back_to_top: true
        property bool   sticky_top_nav: false
        property int    portrait_width_pct: 100
        // PASS 4 taste toggles (global — per-series felt like over-machinery for these)
        property bool   split_wide: false          // strip: wide spreads render as two stacked halves
        property int    side_padding: 0            // strip: px each side (0/40/80/120/160)
        property string image_quality: "smooth"    // smooth | fast
        property int    dim: 0                     // 0 off · 1 soft · 2 strong (night reading)
    }
    readonly property bool smoothQ: prefs.image_quality !== "fast"
    // --- per-series overrides (TB2 behavior: settings are remembered per series; the globals
    // above stay the defaults for a series you haven't touched). One JSON map keyed by seriesId,
    // in its own Settings category — no C++ needed, survives a Continue forget(). ---
    Settings { id: seriesStore; category: "mangaReaderSeries"; property string all: "{}" }
    property string styleOv: ""
    property string dirOv: ""
    property string fitOv: ""
    property int    widthOv: 0
    function loadSeriesPrefs() {
        var m = {}
        try { m = JSON.parse(seriesStore.all) } catch (e) { m = {} }
        var p = (seriesId.length && m[seriesId]) ? m[seriesId] : null
        styleOv = (p && p.style) ? p.style : ""
        dirOv   = (p && p.dir)   ? p.dir   : ""
        fitOv   = (p && p.fit)   ? p.fit   : ""
        widthOv = (p && p.width) ? p.width : 0
        zoomPct = (p && p.zoom)  ? p.zoom  : 100
        panX = 0; panY = 0
    }
    function saveSeriesPrefs() {
        if (!seriesId.length) return
        var m = {}
        try { m = JSON.parse(seriesStore.all) } catch (e) { m = {} }
        m[seriesId] = { style: styleOv, dir: dirOv, fit: fitOv, width: widthOv,
                        zoom: zoomPct !== 100 ? zoomPct : 0 }
        seriesStore.all = JSON.stringify(m)
    }
    onSeriesIdChanged: loadSeriesPrefs()
    function setDirection(d) { dirOv = d; prefs.reading_direction = d; saveSeriesPrefs() }
    function setWidthPct(w)  { widthOv = w; prefs.portrait_width_pct = w; saveSeriesPrefs() }
    function setFit(f)       { fitOv = f; prefs.image_fit = f; saveSeriesPrefs() }

    readonly property string style: styleOv.length ? styleOv : prefs.reading_style
    readonly property string fit: fitOv.length ? fitOv : prefs.image_fit
    readonly property int    portraitWidthPct: widthOv > 0 ? widthOv : prefs.portrait_width_pct
    // MangaPlus (double_page_v2) reads LEFT-TO-RIGHT regardless of the saved direction.
    readonly property bool rtl: style === "double_page_v2" ? false
                                : (dirOv.length ? dirOv : prefs.reading_direction) === "right_left"
    readonly property bool isDouble: style === "double_page" || style === "double_page_v2"
    readonly property bool paged: style === "single_page" || isDouble

    // --- which chapter we're actually reading (the modal / crossing can change it) ---
    property string curChapterId: ""
    property bool   pendingAtLast: false        // open an older chapter at its last page
    onChapterIdChanged: { flushChapterRec(); _resumeArmed = true; curChapterId = chapterId }
    Component.onCompleted: { _resumeArmed = true; curChapterId = chapterId; if (curChapterId.length) load() }
    onCurChapterIdChanged: { load(); recordProgress() }
    // grab keyboard focus whenever the reader is shown, so arrows/Esc work (it's a
    // direct child of the series page and won't get active focus on its own).
    onVisibleChanged: {
        if (visible) reader.forceActiveFocus()
        else { recordProgress(); saveChapterRec() }   // leaving — flush the debounced saves
    }

    readonly property int curIndex: {
        for (var i = 0; i < chapters.length; i++)
            if (String(chapters[i].id) === curChapterId) return i
        return -1
    }
    readonly property string curLabel: {
        var c = curIndex >= 0 ? chapters[curIndex] : null
        if (c) return (c.name && String(c.name).length) ? c.name : ("Chapter " + (c.number || ""))
        return chapterLabel
    }
    // newest-first: index-1 = newer (forward read), index+1 = older (previous)
    readonly property bool hasNewer: curIndex > 0
    readonly property bool hasOlder: curIndex >= 0 && curIndex < chapters.length - 1

    // --- data ---
    property var  pagesModel: []               // [{index, url, group}] — LOCAL file:/// urls
    property int  page: 1                       // 1-based current page (anchor in double mode)
    property bool loading: true
    property string errorMsg: ""
    property bool downloading: false
    property real dlDone: 0       // manga: pages · western: bytes (real — TPBs pass 2^31)
    property real dlTotal: 0
    property var  dims: ({})                    // { index: {w,h} } natural px
    property int  couplingNudge: 0
    property bool atEnd: false                  // "all caught up" end card

    readonly property int  max: pagesModel.length

    // --- resume + zoom state (PASS 3) ---
    property int  maxSeen: 0                    // highest page reached this chapter (TB2 maxPageIndexSeen)
    property bool _resumeArmed: false           // an external open may restore the saved spot
    property real _pendingFrac: 0               // strip scroll fraction awaiting layout settle
    property int  zoomPct: 100                  // paged-mode zoom, TB2 range 100–260
    property real panX: 0
    property real panY: 0
    property real zoneY: 0                      // throttled scroll anchor for the strip loading window
    // paged-mode decode width: bump the source cap when zoomed so magnification isn't blur
    readonly property int pagedSrcW: zoomPct >= 180 ? 2800 : (zoomPct > 100 ? 2048 : 1400)

    // --- continue tracking: note how far into this series we've read, for the Continue row ---
    function stripFrac() {
        var hmax = flick.contentHeight - flick.height
        return hmax > 1 ? Math.max(0, Math.min(1, flick.contentY / hmax)) : 0
    }
    function recordProgress() {
        if (typeof Progress === "undefined" || !reader.seriesId.length || reader.max <= 0)
            return
        Progress.record({
            "id": reader.seriesId,
            "kind": reader.progressKind,
            "caption": reader.seriesTitle,
            "title": reader.seriesTitle,
            "sub": reader.curLabel,
            "cover": reader.seriesCover,
            "c1": "#3a2f55", "c2": "#15111f",
            "progress": Math.min(1, Math.max(0, reader.page / reader.max)),
            "resume": { "chapterId": reader.curChapterId, "page": reader.page,
                        "scrollFrac": reader.style === "long_strip" ? reader.stripFrac() : 0,
                        "maxSeen": reader.maxSeen,
                        "finished": reader.maxSeen >= reader.max }
        })
    }
    // debounced save — QSettings syncs to disk on every record(); don't do that per page-turn
    Timer { id: saveSoon; interval: 600; onTriggered: reader.recordProgress() }
    function recordProgressSoon() { saveSoon.restart() }
    // maxSeen counts the pair PARTNER too — in double mode `page` is always the pair anchor,
    // which never reaches max on chapters that end in a pair (finished would stay false forever).
    // Computed straight from the engine: the `pair` binding can still be stale inside this
    // handler (deferred re-evaluation), so don't read it here.
    function bumpSeen() {
        var s = page
        if (isDouble && max) {
            var pr = Engine.getTwoPagePair(page - 1, ctx())
            if (pr && pr.partnerIndex !== null) s = Math.max(s, pr.partnerIndex + 1)
        }
        if (s > maxSeen) maxSeen = s
    }
    onPageChanged: { bumpSeen(); panX = 0; panY = 0; recordProgressSoon() }
    Component.onDestruction: { recordProgress(); saveChapterRec() }   // flush BOTH debounced stores

    // --- modals + HUD popups ---
    property bool showPrefs: false
    property bool showJump: false
    property bool showChapters: false
    property bool showThumbs: false             // T — page thumbnail grid
    property bool showKeys: false               // K — shortcuts card
    property bool showCtx: false                // right-click menu
    property real ctxX: 0
    property real ctxY: 0
    property var  ctxItems: []
    property string hudMenu: ""                 // "mode" | "width" | ""
    readonly property bool anyModal: showPrefs || showJump || showChapters || showThumbs || showKeys || showCtx || hudMenu !== ""

    Theme { id: theme }

    function load() {
        errorMsg = ""; dims = ({}); loading = false; atEnd = false
        pagesModel = (curChapterId.length && store)
                     ? store.localPages(curChapterId) : []
        if (pagesModel.length > 0) {
            downloading = false
            maxSeen = 0; _pendingFrac = 0; zoneY = 0; panX = 0; panY = 0
            loadChapterRec()   // seed persisted spread knowledge + bookmarks BEFORE any snap
            var start = pendingAtLast ? pagesModel.length : 1
            pendingAtLast = false
            // resume — when this open matches the saved Continue entry, drop back to the spot
            if (_resumeArmed) {
                _resumeArmed = false
                var saved = (typeof Progress !== "undefined" && seriesId.length)
                            ? Progress.get(progressKind, seriesId) : null
                var r = (saved && saved.resume) ? saved.resume : null
                if (r && String(r.chapterId) === curChapterId) {
                    start = Math.max(1, Math.min(pagesModel.length, Number(r.page) || 1))
                    maxSeen = Math.max(start, Number(r.maxSeen) || 0)
                    if (style === "long_strip") _pendingFrac = Number(r.scrollFrac) || 0
                }
            }
            page = isDouble ? (Engine.snapTwoPageIndex(start - 1, ctx()) + 1) : start
            maxSeen = Math.max(maxSeen, page)
            // strip: settle from a clean top, then restore fraction / jump to the resume page
            // (300ms — TB2's proven layout-settle delay before a fractional restore)
            if (style === "long_strip") {
                scrollAnim.stop(); flick.contentY = 0; _scrollTarget = 0
                stripRestore.restart()
            }
            return
        }
        page = 1; pendingAtLast = false
        var st = (curChapterId.length && store)
                 ? store.statusOf(curChapterId) : { state: "none", done: 0, total: 0 }
        downloading = (st.state === "downloading" || st.state === "queued"
                       || st.state === "resolving" || st.state === "extracting")
        dlDone = st.done; dlTotal = st.total
    }

    function startDownload() {
        if (!curChapterId.length || !store) return
        downloading = true; errorMsg = ""
        if (western) {
            // the release post's permalink rides in the chapters model (ComicSeries)
            var c = curIndex >= 0 ? chapters[curIndex] : null
            store.downloadIssue(curChapterId, (c && c.url) ? c.url : "", seriesId, seriesTitle,
                                curLabel, ((c && c.sizeMB) || 0) * 1024 * 1024)
        } else {
            store.downloadChapter(curChapterId, seriesId, seriesTitle, curLabel)
        }
    }

    // ── pairing memory (PASS 4): persisted spread knowledge + per-page override, per chapter.
    // Spread detection used to be dims-only, so pairs could reshuffle mid-read as images
    // loaded and re-detect from scratch every open (TB2 persists knownSpreadIndices — this
    // is our equivalent). Override beats knowledge beats live dims beats "not a spread". ──
    Settings { id: chapterStore; category: "mangaReaderChapters"; property string all: "{}" }
    property var spreadKnown: ({})              // idx → true (accumulated from decoded dims)
    property var forceS: ({})                   // idx → true (user: this IS a spread)
    property var forceN: ({})                   // idx → true (user: this is NOT a spread)
    property var marks: []                      // bookmarked pages (1-based), sorted
    Timer { id: chapterSave; interval: 800; onTriggered: reader.saveChapterRec() }
    function loadChapterRec() {
        var m = {}
        try { m = JSON.parse(chapterStore.all) } catch (e) { m = {} }
        var r = m[curChapterId] || null
        var k = ({}), fs = ({}), fn = ({})
        if (r) {
            var i
            for (i = 0; i < (r.k  || []).length; i++) k[r.k[i]]   = true
            for (i = 0; i < (r.fs || []).length; i++) fs[r.fs[i]] = true
            for (i = 0; i < (r.fn || []).length; i++) fn[r.fn[i]] = true
        }
        spreadKnown = k; forceS = fs; forceN = fn
        marks = (r && r.m) ? r.m.slice() : []
    }
    function saveChapterRec() {
        if (!curChapterId.length) return
        var m = {}
        try { m = JSON.parse(chapterStore.all) } catch (e) { m = {} }
        function keysOf(o) { var a = []; for (var kk in o) a.push(Number(kk)); return a }
        var rec = { k: keysOf(spreadKnown), fs: keysOf(forceS), fn: keysOf(forceN), m: marks }
        if (!rec.k.length && !rec.fs.length && !rec.fn.length && !rec.m.length) delete m[curChapterId]
        else m[curChapterId] = rec
        chapterStore.all = JSON.stringify(m)
    }
    function isSpreadIdx(i) {
        if (forceN[i]) return false
        if (forceS[i]) return true
        var d = dims[i]
        if (d) return Engine.isSpread(d.w, d.h)
        return !!spreadKnown[i]
    }
    function spreadStateOf(i) { return forceS[i] ? "spread" : (forceN[i] ? "single" : "auto") }
    // auto → spread → single → auto, then re-snap the pairing around the change
    function cycleSpreadOverride(i) {
        var fs = forceS, fn = forceN
        if (fs[i]) { delete fs[i]; fn[i] = true }
        else if (fn[i]) { delete fn[i] }
        else { fs[i] = true }
        forceS = fs; forceN = fn
        saveChapterRec()
        if (isDouble && max) page = Engine.snapTwoPageIndex(page - 1, ctx()) + 1
        showToast("Page " + (i + 1) + " pairing: " + spreadStateOf(i))
    }
    // ── bookmarks ──
    function isBookmarked(p) { return marks.indexOf(p) >= 0 }
    function toggleBookmark() {
        if (max <= 0) return
        var m = marks.slice()
        var i = m.indexOf(page)
        if (i >= 0) { m.splice(i, 1); showToast("Bookmark removed") }
        else { m.push(page); m.sort(function (a, b) { return a - b }); showToast("Bookmarked p." + page) }
        marks = m; saveChapterRec()
    }

    function reportDims(i, w, h) {
        if (!w || !h || dims[i]) return
        var d = dims; d[i] = { w: w, h: h }; dims = d
        // remember discovered spreads so the pairing is stable on every future open
        if (Engine.isSpread(w, h) && !spreadKnown[i]) {
            var k = spreadKnown; k[i] = true; spreadKnown = k
            chapterSave.restart()
        }
    }
    function ctx() {
        return { n: reader.max, isSpreadAt: reader.isSpreadIdx, couplingNudge: reader.couplingNudge }
    }
    readonly property int anchor: (isDouble && max) ? Engine.snapTwoPageIndex(page - 1, ctx()) : page - 1
    readonly property var pair: (isDouble && max) ? Engine.getTwoPagePair(anchor, ctx()) : null
    readonly property string curUrl: (page >= 1 && page <= max) ? pagesModel[page - 1].url : ""

    Connections {
        target: reader.store
        function onProgress(cid, done, total) {
            if (cid !== reader.curChapterId) return
            reader.downloading = true; reader.dlDone = done; reader.dlTotal = total
        }
        function onFinished(cid) {
            if (cid !== reader.curChapterId) return
            reader.downloading = false; reader.errorMsg = ""; reader.load()
        }
        function onFailed(cid, reason) {
            if (cid !== reader.curChapterId) return
            reader.downloading = false; reader.errorMsg = reason
        }
    }

    // --- chapter crossing (newest-first order) ---
    // flush a pending spread/bookmark save for the OUTGOING chapter before curChapterId moves,
    // or the debounced timer would write the old data under the new chapter's key
    function flushChapterRec() {
        if (chapterSave.running) { chapterSave.stop(); saveChapterRec() }
    }
    function openChapterById(id, atLast) {
        if (!id || !id.length) return
        flushChapterRec()
        pendingAtLast = !!atLast
        curChapterId = String(id)
    }
    function goNextChapter() {
        if (hasNewer) openChapterById(chapters[curIndex - 1].id, false)
        else if (chapters.length && curIndex === 0) atEnd = true
    }
    function goPrevChapter(atLast) {
        if (hasOlder) openChapterById(chapters[curIndex + 1].id, atLast)
    }

    // --- paged turning (direction-aware; crosses chapter at the ends) ---
    function turnNext() {
        if (!max) return
        if (isDouble) { var nx = Engine.stepNext(page - 1, ctx()); if (nx === null) goNextChapter(); else page = nx + 1 }
        else if (page < max) page = page + 1
        else goNextChapter()
    }
    function turnPrev() {
        if (!max) { goPrevChapter(true); return }
        if (isDouble) { var pv = Engine.stepPrev(page - 1, ctx()); if (pv === null) goPrevChapter(true); else page = pv + 1 }
        else if (page > 1) page = page - 1
        else goPrevChapter(true)
    }
    // HUD prev/next: paged turns a page; long_strip jumps chapters
    function prevAction() { paged ? turnPrev() : goPrevChapter(false) }
    function nextAction() { paged ? turnNext() : goNextChapter() }

    function setStyle(s) {
        if (s === style) return
        var keep = page
        styleOv = s; prefs.reading_style = s; saveSeriesPrefs()
        panX = 0; panY = 0
        if ((s === "double_page" || s === "double_page_v2") && max)
            page = Engine.snapTwoPageIndex(keep - 1, ctx()) + 1
        // switching INTO the strip keeps your page. NOT Qt.callLater: the Column positions
        // its children in updatePolish (a vsync later), so an immediate jump reads y=0 for
        // every delegate and lands at the top. stripRestore's 300ms settle handles it.
        if (s === "long_strip" && max && keep > 1) {
            page = keep
            stripRestore.restart()
        }
    }

    // Smooth long-strip scrolling (wheel + click + edge bars): animate contentY toward
    // an accumulating target so rapid wheel notches glide instead of stepping harshly.
    property real _scrollTarget: 0
    NumberAnimation { id: scrollAnim; target: flick; property: "contentY"
        duration: 240; easing.type: Easing.OutCubic }
    function smoothScrollBy(dy) {
        var hmax = Math.max(0, flick.contentHeight - flick.height)
        var base = scrollAnim.running ? reader._scrollTarget : flick.contentY
        var t = Math.max(0, Math.min(hmax, base + dy))
        reader._scrollTarget = t
        reader.zoneY = t   // start loading the destination pages during the glide
        scrollAnim.stop(); scrollAnim.from = flick.contentY; scrollAnim.to = t; scrollAnim.start()
    }
    function smoothScrollTo(y) { smoothScrollBy(y - flick.contentY) }

    // ── strip page geometry (PASS 3): which page sits at a content Y, and jumping to one ──
    function pageAtY(cy) {
        var n = stripRep.count
        if (n <= 0) return 1
        var lo = 0, hi = n - 1
        while (lo < hi) {
            var mid = (lo + hi) >> 1
            var it = stripRep.itemAt(mid)
            if (!it) return Math.max(1, Math.min(n, reader.page))  // model churn — keep current
            if (it.y + it.height < cy) lo = mid + 1; else hi = mid
        }
        return lo + 1
    }
    function jumpToPage(p, instant) {
        p = Math.max(1, Math.min(max, Math.round(p)))
        if (style === "long_strip") {
            var hmax = Math.max(0, flick.contentHeight - flick.height)
            var it = stripRep.itemAt(p - 1)
            var y = it ? it.y : (p - 1) / Math.max(1, max) * hmax
            y = Math.max(0, Math.min(y, hmax))
            zoneY = y
            if (instant) { scrollAnim.stop(); flick.contentY = y; _scrollTarget = y }
            else smoothScrollTo(y)
            page = p
        } else {
            page = isDouble ? Engine.snapTwoPageIndex(p - 1, ctx()) + 1 : p
        }
    }
    // live "current page" while the strip scrolls — the page at the viewport center (TB2
    // behavior; ticks at most every 80ms while in motion, so the HUD counter reads live).
    Timer {
        id: pageTrack; interval: 80
        onTriggered: {
            if (reader.style !== "long_strip" || reader.max <= 0) return
            if (stripRestore.running) return   // a reflow-restore is pending — don't relabel mid-flight
            var p = reader.pageAtY(flick.contentY + flick.height / 2)
            if (p !== reader.page) reader.page = p
            reader.recordProgressSoon()
        }
    }
    // throttled anchor for the strip loading window — don't rebind every delegate per frame
    Timer {
        interval: 150; repeat: true
        running: reader.style === "long_strip" && reader.max > 0 && reader.visible
        onTriggered: if (Math.abs(flick.contentY - reader.zoneY) > 32) reader.zoneY = flick.contentY
    }
    // fractional strip restore, after the estimated layout settles (TB2's 300ms)
    Timer {
        id: stripRestore; interval: 300
        onTriggered: {
            if (reader.style !== "long_strip" || reader.max <= 0) return
            if (reader._pendingFrac > 0) {
                var hmax = Math.max(0, flick.contentHeight - flick.height)
                scrollAnim.stop()
                flick.contentY = reader._pendingFrac * hmax
                reader._scrollTarget = flick.contentY
                reader.zoneY = flick.contentY
                reader._pendingFrac = 0
            } else if (reader.page > 1) {
                reader.jumpToPage(reader.page, true)
            }
        }
    }

    // ── zoom + pan (paged modes; TB2: Ctrl+wheel ±20%, clamp 100–260, pan when magnified) ──
    // Bounds come from the RENDERED CONTENT extent, not just the viewport — a fit-width page
    // taller than the screen must be pannable down to its last panel, and short content must
    // not pan off into empty space.
    function clampPan() {
        var s = zoomPct / 100
        var contentW = flick.width, contentH = flick.height
        if (style === "single_page" && spImg.height > 0) {
            contentW = spImg.width; contentH = spImg.height
        } else if (isDouble && dblRow.height > 0) {
            contentW = dblRow.width; contentH = dblRow.height
        }
        var mx = Math.max(0, (contentW * s - flick.width)  / 2, flick.width  * (s - 1) / 2)
        var my = Math.max(0, (contentH * s - flick.height) / 2, flick.height * (s - 1) / 2)
        panX = Math.max(-mx, Math.min(mx, panX))
        panY = Math.max(-my, Math.min(my, panY))
    }
    Timer { id: zoomSave; interval: 500; onTriggered: reader.saveSeriesPrefs() }
    function zoomBy(d) {
        if (!paged) return
        var z = Math.max(100, Math.min(260, zoomPct + d))
        if (z === zoomPct) return
        zoomPct = z; clampPan(); showToast("Zoom " + z + "%"); zoomSave.restart()
    }

    // transient toast (zoom feedback etc.)
    property string toast: ""
    Timer { id: toastTimer; interval: 900; onTriggered: reader.toast = "" }
    function showToast(t) { toast = t; toastTimer.restart() }

    // ===================== auto-hide chrome (Tankoban Max behavior) =====================
    // HUD + side bars recede while reading (after 3s idle) and STAY hidden while you read.
    // They return when you reach for them — the cursor enters the top/bottom 60px edge — or
    // on wheel / click / hovering the HUD. Keyboard scrolling does NOT wake them (immersive).
    // A modal/dropdown open or hovering the HUD freezes them shown; "Pin toolbar" pins them on.
    property bool hudShown: true
    property bool hudHover: false
    property bool hudExplicitlyHidden: false          // H key / center-click — sticks until you reach for the chrome
    property bool edgeCooldown: false                 // brief lock after an edge-reveal (anti-flicker)
    readonly property int hudEdgePx: 60               // reveal band at top/bottom
    readonly property bool pinned: prefs.sticky_top_nav
    readonly property bool frozen: anyModal || hudHover
                                   || (scrub.visible && (scrubMa.containsMouse || scrubMa.pressed))
    // explicit hide beats everything, pin included, until the next poke (TB2's toggleToolbar:
    // pin only prevents IDLE hiding; a deliberate H/center-click always hides, edge-reach revives)
    readonly property bool chromeShown: hudExplicitlyHidden ? false : (frozen || hudShown || pinned)
    // cursor auto-hide (TB2: blank after 3s idle while the chrome is away)
    property bool cursorIdle: false
    readonly property bool cursorHidden: cursorIdle && !chromeShown && max > 0
    Timer { id: cursorTimer; interval: 3000; onTriggered: reader.cursorIdle = true }
    Timer { id: idleHide; interval: 3000; running: reader.max > 0
        onTriggered: if (!reader.frozen && !reader.pinned) reader.hudShown = false }
    Timer { id: edgeCool; interval: 600; onTriggered: reader.edgeCooldown = false }
    function pokeChrome() {
        hudExplicitlyHidden = false
        cursorIdle = false; cursorTimer.restart()
        hudShown = true; if (!pinned) idleHide.restart()
    }
    // explicit hide/show (H key or a center click) — TB2's toggleToolbar
    function toggleChrome() {
        if (anyModal) return
        if (chromeShown) { hudExplicitlyHidden = true; hudShown = false; idleHide.stop() }
        else pokeChrome()
    }
    onFrozenChanged: {
        if (frozen) { idleHide.stop(); hudShown = true }
        else if (!hudExplicitlyHidden) pokeChrome()   // an explicit hide must survive un-hovering
        else { cursorIdle = false; cursorTimer.restart() }
    }

    // ===================== visual tree =====================
    focus: true
    // Keyboard scroll/nav — Tankoban Max key map. Scroll/turn keys deliberately do NOT pokeChrome():
    // keyboard reading keeps the HUD hidden (immersive). Only Esc + modal handling are exempt.
    Keys.onPressed: (e) => {
        // Esc — close an open modal/menu, else leave the reader
        if (e.key === Qt.Key_Escape) {
            if (showCtx) showCtx = false
            else if (showKeys) showKeys = false
            else if (showThumbs) showThumbs = false
            else if (showPrefs || showJump || showChapters || hudMenu !== "") {
                showPrefs = false; showJump = false; showChapters = false; hudMenu = ""
            } else reader.backRequested()
            e.accepted = true
            return
        }
        // K toggles the shortcuts card even from inside it
        if (e.key === Qt.Key_K) { showKeys = !showKeys; e.accepted = true; return }
        // never scroll / turn pages behind an open modal or dropdown
        if (anyModal) return
        if (reader.max <= 0) return

        // session keys (TB2 Batch-D family)
        switch (e.key) {
        case Qt.Key_B: toggleBookmark(); e.accepted = true; return
        case Qt.Key_T: showThumbs = true; e.accepted = true; return
        case Qt.Key_Z:   // instant replay — a beat back
            style === "long_strip" ? smoothScrollBy(-flick.height * 0.3) : turnPrev()
            e.accepted = true; return
        case Qt.Key_S:
            if (!(e.modifiers & Qt.ControlModifier)) { recordProgress(); showToast("Checkpoint saved"); e.accepted = true; return }
            break
        // R restarts the chapter but leaves maxSeen alone — it's a high-water mark; re-reading
        // a finished chapter must not un-finish it in the Continue store
        case Qt.Key_R: jumpToPage(1, true); recordProgressSoon(); showToast("Back to the start"); e.accepted = true; return
        case Qt.Key_G:
            if (e.modifiers & Qt.ControlModifier) { showJump = true; e.accepted = true; return }
            break
        }

        // H — hide/show the chrome on demand (sticks hidden until you reach for it)
        if (e.key === Qt.Key_H) { toggleChrome(); e.accepted = true; return }
        // Ctrl+0 — reset zoom (paged)
        if (paged && e.key === Qt.Key_0 && (e.modifiers & Qt.ControlModifier)) {
            zoomPct = 100; panX = 0; panY = 0; zoomSave.restart(); showToast("Zoom 100%")
            e.accepted = true; return
        }
        // zoomed pan (paged): arrows pan the magnified page (TB2 behavior — ±80px / ⅙ width)
        if (paged && zoomPct > 100) {
            var px = flick.width / 6
            switch (e.key) {
            case Qt.Key_Left:  panX += px; clampPan(); e.accepted = true; return
            case Qt.Key_Right: panX -= px; clampPan(); e.accepted = true; return
            case Qt.Key_Up:    panY += 80; clampPan(); e.accepted = true; return
            case Qt.Key_Down:  panY -= 80; clampPan(); e.accepted = true; return
            }
        }

        if (reader.style === "long_strip") {
            var shift  = (e.modifiers & Qt.ShiftModifier) !== 0
            var step   = Math.max(64, flick.height * 0.12)     // arrow
            var big    = Math.max(64, flick.height * 0.25)     // shift+arrow
            var screen = flick.height * 0.90                   // space / page
            var hmax   = Math.max(0, flick.contentHeight - flick.height)
            switch (e.key) {
            case Qt.Key_Down:     reader.smoothScrollBy(shift ? big : step);    e.accepted = true; break
            case Qt.Key_Up:       reader.smoothScrollBy(shift ? -big : -step);   e.accepted = true; break
            case Qt.Key_Space:    reader.smoothScrollBy(shift ? -screen : screen); e.accepted = true; break
            case Qt.Key_PageDown: reader.smoothScrollBy(screen);   e.accepted = true; break
            case Qt.Key_PageUp:   reader.smoothScrollBy(-screen);  e.accepted = true; break
            case Qt.Key_Home:     reader.smoothScrollTo(0);        e.accepted = true; break
            case Qt.Key_End:      reader.smoothScrollTo(hmax);     e.accepted = true; break
            }
            return
        }

        // paged (single_page / double_page) — Left/Right RTL-aware
        switch (e.key) {
        case Qt.Key_Left:     (rtl ? turnNext : turnPrev)(); e.accepted = true; break
        case Qt.Key_Right:    (rtl ? turnPrev : turnNext)(); e.accepted = true; break
        case Qt.Key_Space:    ((e.modifiers & Qt.ShiftModifier) ? turnPrev : turnNext)(); e.accepted = true; break
        case Qt.Key_PageDown: turnNext(); e.accepted = true; break
        case Qt.Key_PageUp:   turnPrev(); e.accepted = true; break
        case Qt.Key_Home:     reader.page = 1; e.accepted = true; break
        case Qt.Key_End:      reader.page = reader.max; e.accepted = true; break
        }
    }

    Rectangle { anchors.fill: parent; color: prefs.dark_background ? "#000000" : "#0a0b10" }

    // ── the page surface (scrolls) ──
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol ? pageCol.height : height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        pixelAligned: true
        interactive: reader.max > 0 && reader.style === "long_strip"
        opacity: reader.max > 0 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        // live page tracking while the strip scrolls (feeds the HUD counter + scrub bar)
        onContentYChanged: if (!pageTrack.running) pageTrack.start()

        // Smooth wheel scrolling for long-strip (default Flickable wheel steps harshly).
        WheelHandler {
            enabled: reader.style === "long_strip" && reader.max > 0
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (e) => { reader.pokeChrome(); reader.smoothScrollBy(-e.angleDelta.y * 1.4) }
        }
        // paged: Ctrl+wheel zooms (TB2: ±20% per notch, 100–260%)
        WheelHandler {
            enabled: reader.paged && reader.max > 0
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            onWheel: (e) => reader.zoomBy(e.angleDelta.y > 0 ? 20 : -20)
        }
        // paged: plain wheel pans when magnified, else turns the page (accumulated so
        // trackpad micro-deltas don't spam page turns)
        property real wheelAcc: 0
        WheelHandler {
            enabled: reader.paged && reader.max > 0
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: (e) => {
                if (reader.zoomPct > 100) { reader.panY += e.angleDelta.y * 0.8; reader.clampPan(); return }
                flick.wheelAcc += e.angleDelta.y
                if (flick.wheelAcc <= -100)     { flick.wheelAcc = 0; reader.pokeChrome(); reader.turnNext() }
                else if (flick.wheelAcc >= 100) { flick.wheelAcc = 0; reader.pokeChrome(); reader.turnPrev() }
            }
        }

        // LONG STRIP
        Column {
            id: pageCol
            visible: reader.style === "long_strip"
            width: Math.max(200, Math.min(flick.width - 2 * prefs.side_padding,
                                          flick.width * reader.portraitWidthPct / 100))
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: prefs.gap ? 8 : 0
            Repeater {
                id: stripRep
                model: reader.style === "long_strip" ? reader.pagesModel : []
                delegate: Item {
                    id: cell
                    required property var modelData
                    required property int index
                    // the memory diet (TB2's prefetch zone): only pages within ±1.5 screens of
                    // the throttled scroll anchor hold a decoded image; far pages release theirs.
                    readonly property bool inZone: {
                        var m = flick.height * 1.5
                        return (y + height) >= (reader.zoneY - m) && y <= (reader.zoneY + flick.height + m)
                    }
                    // split-wide (P3-3): a known spread renders as two stacked portrait halves
                    readonly property bool split: prefs.split_wide && reader.isSpreadIdx(index)
                    width: pageCol.width
                    // known dims pin the height, so the layout stays stable when a far page unloads
                    height: {
                        var d = reader.dims[index]
                        if (split) return d ? 2 * (width * 2 * d.h / d.w) + 8
                                            : width * 2.86 + 8   // ~1.4-aspect spread estimate, no ¼-height pop
                        if (d) return width * d.h / d.w
                        return width * 1.45
                    }
                    Image {
                        visible: !cell.split
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        source: (cell.inZone && !cell.split) ? cell.modelData.url : ""
                        asynchronous: true; cache: true
                        smooth: reader.smoothQ; mipmap: reader.smoothQ
                        sourceSize.width: 1100
                        onStatusChanged: if (status === Image.Ready) reader.reportDims(cell.index, implicitWidth, implicitHeight)
                    }
                    // the two halves: read-first half on top (RTL manga = right half first)
                    Column {
                        visible: cell.split
                        anchors.fill: parent
                        spacing: 8
                        Repeater {
                            model: cell.split ? 2 : 0
                            delegate: Item {
                                required property int index
                                readonly property bool rightHalf: (index === 0) === reader.rtl
                                width: cell.width
                                height: (cell.height - 8) / 2
                                clip: true
                                Image {
                                    width: parent.width * 2
                                    height: parent.height   // box = 2·cellW × halfH, matches the aspect — halves tile flush
                                    fillMode: Image.PreserveAspectFit
                                    x: parent.rightHalf ? -parent.width : 0
                                    source: cell.inZone ? cell.modelData.url : ""
                                    asynchronous: true; cache: true
                                    smooth: reader.smoothQ; mipmap: reader.smoothQ
                                    sourceSize.width: 2048
                                    // a persisted-known spread may never load the whole-page
                                    // Image, so the half reports dims too (guard dedupes)
                                    onStatusChanged: if (status === Image.Ready) reader.reportDims(cell.index, implicitWidth, implicitHeight)
                                }
                            }
                        }
                    }
                }
            }
        }

        // SINGLE PAGE
        Item {
            visible: reader.style === "single_page"
            width: flick.width; height: flick.height
            scale: reader.paged ? reader.zoomPct / 100 : 1
            transformOrigin: Item.Center
            x: reader.panX; y: reader.panY
            Image {
                id: spImg
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width * reader.portraitWidthPct / 100
                height: reader.fit === "height" ? parent.height : ((implicitWidth > 0) ? width * (implicitHeight / implicitWidth) : width * 1.45)
                fillMode: Image.PreserveAspectFit
                source: reader.curUrl
                asynchronous: true; cache: true
                smooth: reader.smoothQ; mipmap: reader.smoothQ
                retainWhileLoading: true   // zoom-tier source swap keeps the old frame up
                sourceSize.width: reader.pagedSrcW
                onStatusChanged: if (status === Image.Ready) reader.reportDims(reader.page - 1, implicitWidth, implicitHeight)
            }
            // neighbor prefetch — warm the image cache so page turns don't flash blank
            Image { visible: false; asynchronous: true; cache: true; sourceSize.width: reader.pagedSrcW
                source: (reader.style === "single_page" && reader.page < reader.max)
                        ? reader.pagesModel[reader.page].url : "" }
            Image { visible: false; asynchronous: true; cache: true; sourceSize.width: reader.pagedSrcW
                source: (reader.style === "single_page" && reader.page > 1)
                        ? reader.pagesModel[reader.page - 2].url : "" }
        }

        // DOUBLE PAGE (double_page + double_page_v2)
        Item {
            id: dbl
            visible: reader.isDouble && reader.pair !== null
            width: flick.width; height: flick.height
            scale: reader.paged ? reader.zoomPct / 100 : 1
            transformOrigin: Item.Center
            x: reader.panX; y: reader.panY
            property var layout: (reader.isDouble && reader.pair) ? Engine.computeSpreadLayout({
                kind: reader.pair.kind,
                anchorDims: reader.dims[reader.pair.anchorIndex] || { w: 800, h: 1200 },
                partnerDims: reader.pair.partnerIndex !== null ? (reader.dims[reader.pair.partnerIndex] || { w: 800, h: 1200 }) : null,
                containerW: flick.width, containerH: flick.height, gutter: 0, fitWidth: true, rtl: reader.rtl
            }) : null
            Image { visible: false; asynchronous: true; cache: true; sourceSize.width: reader.pagedSrcW
                source: (reader.pair && reader.pair.anchorIndex < reader.max) ? reader.pagesModel[reader.pair.anchorIndex].url : ""
                onStatusChanged: if (status === Image.Ready && reader.pair) reader.reportDims(reader.pair.anchorIndex, implicitWidth, implicitHeight) }
            Image { visible: false; asynchronous: true; cache: true; sourceSize.width: reader.pagedSrcW
                source: (reader.pair && reader.pair.partnerIndex !== null && reader.pair.partnerIndex < reader.max) ? reader.pagesModel[reader.pair.partnerIndex].url : ""
                onStatusChanged: if (status === Image.Ready && reader.pair && reader.pair.partnerIndex !== null) reader.reportDims(reader.pair.partnerIndex, implicitWidth, implicitHeight) }
            Row {
                id: dblRow
                anchors.centerIn: parent
                spacing: 0
                Repeater {
                    // honor the engine's physical `side`: in RTL the read-first page belongs on
                    // the RIGHT, but a Row lays the model out left→right — so flip pair order
                    // when the anchor's side is right. (Pre-PASS-4 bug: side was ignored.)
                    model: {
                        if (!dbl.layout) return []
                        var pg = dbl.layout.pages
                        if (pg.length === 2 && pg[0].side === "right") return [pg[1], pg[0]]
                        return pg
                    }
                    delegate: Image {
                        required property var modelData
                        property int pgIdx: modelData.role === "anchor" ? reader.pair.anchorIndex : reader.pair.partnerIndex
                        width: modelData.w; height: modelData.h
                        fillMode: Image.PreserveAspectFit
                        source: (pgIdx >= 0 && pgIdx < reader.max) ? reader.pagesModel[pgIdx].url : ""
                        asynchronous: true; cache: true
                        smooth: reader.smoothQ; mipmap: reader.smoothQ
                        retainWhileLoading: true   // zoom-tier source swap keeps the old frame up
                        sourceSize.width: reader.pagedSrcW
                    }
                }
            }
            // gutter shadow — a soft spine crease where two pages meet (TB2's H3, fixed-subtle)
            Rectangle {
                visible: reader.pair !== null && reader.pair.partnerIndex !== null
                anchors.horizontalCenter: dblRow.horizontalCenter
                anchors.verticalCenter: dblRow.verticalCenter
                width: 22; height: dblRow.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0;  color: "transparent" }
                    GradientStop { position: 0.5;  color: Qt.rgba(0, 0, 0, 0.26) }
                    GradientStop { position: 1.0;  color: "transparent" }
                }
            }
        }
    }

    // dim overlay — night-reading veil over the page surface (never over the HUD, z20+)
    Rectangle {
        anchors.fill: parent; z: 10
        color: "#000000"
        visible: prefs.dim > 0 && reader.max > 0
        opacity: prefs.dim === 2 ? 0.26 : 0.12
    }

    // ── click zones: left/right thirds turn (paged) or scroll (strip); the center third
    // toggles the chrome (TB2's center-click). When magnified, press-drag pans instead —
    // a >4px drift cancels the click so panning never turns a page (TB2's drift guard). ──
    MouseArea {
        anchors.fill: parent
        enabled: reader.max > 0 && !reader.atEnd
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property real pressX: 0
        property real pressY: 0
        property real panX0: 0
        property real panY0: 0
        property bool dragged: false
        onPressed: (m) => { pressX = m.x; pressY = m.y; panX0 = reader.panX; panY0 = reader.panY; dragged = false }
        onPositionChanged: (m) => {
            if (!(pressedButtons & Qt.LeftButton) || !reader.paged || reader.zoomPct <= 100) return
            var dx = m.x - pressX, dy = m.y - pressY
            if (!dragged && Math.abs(dx) < 4 && Math.abs(dy) < 4) return
            dragged = true
            reader.panX = panX0 + dx; reader.panY = panY0 + dy; reader.clampPan()
        }
        onClicked: (m) => {
            if (m.button === Qt.RightButton) { reader.openCtxMenu(m.x, m.y); return }
            if (dragged) return
            var third = width / 3
            if (m.x >= third && m.x <= width - third) { reader.toggleChrome(); return }
            reader.pokeChrome()
            if (reader.style === "long_strip") {
                if (m.x < third) reader.smoothScrollBy(-flick.height * 0.82)
                else reader.smoothScrollBy(flick.height * 0.82)
            } else {
                if (m.x < third) (reader.rtl ? reader.turnNext : reader.turnPrev)()
                else (reader.rtl ? reader.turnPrev : reader.turnNext)()
            }
        }
    }

    // ── right-click menu: the reader's control surface (TB2's context menu) ──
    function openCtxMenu(mx, my) {
        if (anyModal) return   // scrims pass right-clicks through — never open under one
        var items = []
        items.push({ t: "Go to page…", a: "jump" })
        items.push({ t: "Page thumbnails", a: "thumbs" })
        items.push({ t: "Shortcuts", a: "keys" })
        items.push({ t: isBookmarked(page) ? ("Remove bookmark (p." + page + ")") : ("Bookmark page " + page), a: "bmark" })
        for (var i = Math.max(0, marks.length - 5); i < marks.length; i++)
            items.push({ t: "  ↳ bookmark p." + marks[i], a: "goto", p: marks[i] })
        if (isDouble)
            items.push({ t: "Page " + (anchor + 1) + " pairing: " + spreadStateOf(anchor), a: "spread" })
        if (style === "long_strip") {
            items.push({ t: (prefs.split_wide ? "✓ " : "") + "Split wide pages", a: "split" })
            items.push({ t: "Side padding: " + prefs.side_padding + "px", a: "pad" })
        }
        items.push({ t: "Quality: " + (reader.smoothQ ? "Smooth" : "Fast"), a: "quality" })
        items.push({ t: "Dim: " + ["Off", "Soft", "Strong"][prefs.dim], a: "dim" })
        items.push({ t: "Save checkpoint", a: "save" })
        ctxItems = items
        ctxX = Math.max(8, Math.min(mx, width - 258))
        ctxY = Math.max(8, Math.min(my, height - (items.length * 34 + 20)))
        showCtx = true
    }
    function ctxAction(item) {
        showCtx = false
        switch (item.a) {
        case "jump":    showJump = true; break
        case "thumbs":  showThumbs = true; break
        case "keys":    showKeys = true; break
        case "bmark":   toggleBookmark(); break
        case "goto":    jumpToPage(item.p); break
        case "spread":  cycleSpreadOverride(anchor); break
        case "split":
            prefs.split_wide = !prefs.split_wide
            if (style === "long_strip") stripRestore.restart()   // reflow — return to this page
            break
        case "pad": {
            var pads = [0, 40, 80, 120, 160]
            prefs.side_padding = pads[(pads.indexOf(prefs.side_padding) + 1) % pads.length]
            showToast("Side padding " + prefs.side_padding + "px")
            if (style === "long_strip") stripRestore.restart()   // reflow — return to this page
            break
        }
        case "quality":
            prefs.image_quality = reader.smoothQ ? "fast" : "smooth"
            showToast("Quality: " + (reader.smoothQ ? "Smooth" : "Fast"))
            break
        case "dim":
            prefs.dim = (prefs.dim + 1) % 3
            showToast("Dim: " + ["Off", "Soft", "Strong"][prefs.dim])
            break
        case "save":    recordProgress(); showToast("Checkpoint saved"); break
        }
    }

    // reveal-on-EDGE overlay (Max behavior): NoButton so it never eats page-turn clicks.
    // Only wakes the HUD when it's currently hidden AND the cursor reaches the top/bottom
    // 60px band — so mid-screen movement while reading leaves the chrome hidden. A 600ms
    // cooldown after a reveal prevents flicker when the cursor lingers at the edge.
    MouseArea {
        anchors.fill: parent; z: 18
        enabled: reader.max > 0
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        // cursor WAKE rides this hover overlay; the blanking itself lives on a separate
        // topmost item below — an always-set cursorShape here would shadow every
        // pointing-hand cursor on lower-z controls (nav bars, back-to-top, end card).
        onPositionChanged: (m) => {
            reader.cursorIdle = false; cursorTimer.restart()
            if (reader.chromeShown || reader.edgeCooldown) return
            if (m.y <= reader.hudEdgePx || m.y >= height - reader.hudEdgePx) {
                reader.pokeChrome(); reader.edgeCooldown = true; edgeCool.restart()
            }
        }
    }
    // cursor blanking overlay — only exists while hidden, so it never steals cursor
    // resolution from interactive controls when the cursor is up (TB2: blank after 3s idle)
    MouseArea {
        anchors.fill: parent; z: 100
        visible: reader.cursorHidden
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.BlankCursor
    }

    // ── download panel (no local pages; download-fed, never streams) ──
    Column {
        visible: reader.max === 0
        anchors.centerIn: parent; spacing: 16; width: parent.width * 0.7
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: reader.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 26
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: reader.curLabel; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: reader.errorMsg.length > 0
            text: reader.errorMsg; color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: !reader.downloading && reader.errorMsg.length === 0
            text: "Not downloaded yet — download this chapter to read it offline."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: reader.downloading
            text: reader.dlTotal > 0
                  ? (reader.western
                     ? ("Downloading… " + Math.round(reader.dlDone / 1048576) + " / " + Math.round(reader.dlTotal / 1048576) + " MB")
                     : ("Downloading… " + reader.dlDone + " / " + reader.dlTotal + " pages"))
                  : "Starting download…"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 14 }
        Rectangle {
            visible: reader.downloading && reader.dlTotal > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.6; height: 6; radius: 3; color: theme.glassTint
            Rectangle { height: parent.height; radius: 3; color: theme.gold
                width: parent.width * (reader.dlTotal > 0 ? reader.dlDone / reader.dlTotal : 0)
                Behavior on width { NumberAnimation { duration: 200 } } }
        }
        Rectangle {
            visible: !reader.downloading
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 10; height: 40; width: dlt.implicitWidth + 40
            color: dlMa.containsMouse ? Qt.lighter(theme.gold, 1.1) : theme.gold
            Text { id: dlt; anchors.centerIn: parent
                text: reader.errorMsg.length ? "Retry download" : "⬇  Download chapter"
                color: "#1a1306"; font.family: theme.ui; font.weight: Font.DemiBold; font.pixelSize: 14 }
            MouseArea { id: dlMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: reader.startDownload() }
        }
    }

    // back affordance for the download panel (a real sized button, top-left)
    Rectangle {
        visible: reader.max === 0
        z: 100
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 18
        width: bkRow.implicitWidth + 28; height: 40; radius: 10
        color: bkMa.containsMouse ? theme.glassHi : theme.glassTint
        border.width: 1; border.color: theme.edge
        Row {
            id: bkRow; anchors.centerIn: parent; spacing: 7
            Text { text: "‹"; color: bkMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: bkMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: bkMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() }
    }

    // ── "all caught up" end card ──
    Column {
        visible: reader.atEnd
        anchors.centerIn: parent; spacing: 16; width: parent.width * 0.6
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "You're all caught up"
            color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: "You've reached the latest chapter. Check back later for more."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; spacing: 12
            Rectangle { radius: 9; height: 38; width: stayT.implicitWidth + 30; color: staMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text { id: stayT; anchors.centerIn: parent; text: "‹ Stay here"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                MouseArea { id: staMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: reader.atEnd = false } }
            Rectangle { radius: 9; height: 38; width: bsT.implicitWidth + 30; color: theme.gold
                Text { id: bsT; anchors.centerIn: parent; text: "Back to series"; color: "#1a1306"; font.family: theme.ui; font.weight: Font.DemiBold; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() } }
        }
    }

    // ── edge side-bars (prev/next or scroll) — auto-hide ──
    component NavBar: Rectangle {
        property bool isLeft: true
        property bool shown: true
        enabled: shown
        width: 52; height: parent.height
        color: navMa.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent"
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Text { anchors.centerIn: parent; text: parent.isLeft ? "‹" : "›"
            color: navMa.containsMouse ? theme.gold : Qt.rgba(1,1,1,0.45)
            font.family: theme.display; font.pixelSize: 34 }
        MouseArea {
            id: navMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (reader.style === "long_strip") reader.smoothScrollBy(parent.isLeft ? -flick.height * 0.82 : flick.height * 0.82)
                else (parent.isLeft ? (reader.rtl ? reader.turnNext : reader.turnPrev)
                                    : (reader.rtl ? reader.turnPrev : reader.turnNext))()
            }
        }
    }
    NavBar { isLeft: true;  anchors.left: parent.left;   visible: reader.max > 0 && !reader.atEnd; shown: reader.chromeShown; z: 15 }
    NavBar { isLeft: false; anchors.right: parent.right; visible: reader.max > 0 && !reader.atEnd; shown: reader.chromeShown; z: 15 }

    // floating back-to-top (long strip) — sits above the scrub lane
    Rectangle {
        visible: reader.max > 0 && reader.style === "long_strip" && prefs.back_to_top && !reader.atEnd
        z: 16; width: 44; height: 44; radius: 22
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.rightMargin: 24; anchors.bottomMargin: 44
        color: ttMa.containsMouse ? theme.gold : theme.glassTint; border.width: 1; border.color: theme.edge
        Text { anchors.centerIn: parent; text: "↑"; color: ttMa.containsMouse ? "#1a1306" : theme.ink
            font.family: theme.display; font.pixelSize: 20 }
        MouseArea { id: ttMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: reader.smoothScrollTo(0) }
    }

    // ── bottom scrub bar (TB2's ScrubBar): track + live fill + hover/drag page bubble.
    // Paged: fill = page position, drag seeks pages live. Strip: fill = scroll position,
    // drag seeks the scroll, bubble names the page under the cursor. ──
    Item {
        id: scrub
        visible: reader.max > 1 && !reader.atEnd
        z: 20
        height: 26
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: 64; anchors.rightMargin: 64; anchors.bottomMargin: 8
        opacity: reader.chromeShown ? 1 : 0
        enabled: reader.chromeShown
        Behavior on opacity { NumberAnimation { duration: 180 } }
        readonly property real frac: {
            if (reader.style === "long_strip") {
                var hmax = flick.contentHeight - flick.height
                return hmax > 1 ? Math.max(0, Math.min(1, flick.contentY / hmax)) : 0
            }
            return reader.max > 1 ? (reader.page - 1) / (reader.max - 1) : 0
        }
        function fracAt(mx) { return Math.max(0, Math.min(1, mx / width)) }
        function pageAtFrac(f) {
            if (reader.style === "long_strip") {
                var hmax = Math.max(0, flick.contentHeight - flick.height)
                return reader.pageAtY(f * hmax + flick.height / 2)
            }
            return Math.round(f * (reader.max - 1)) + 1
        }
        function seek(f) {
            if (reader.style === "long_strip") {
                var hmax = Math.max(0, flick.contentHeight - flick.height)
                scrollAnim.stop()
                flick.contentY = f * hmax
                reader._scrollTarget = flick.contentY
                // zoneY deliberately NOT written here — seek() runs per mouse-move while
                // dragging, and stamping zoneY each move would re-evaluate every delegate's
                // loading window at pointer rate. The 150ms throttle timer follows contentY.
            } else {
                reader.jumpToPage(pageAtFrac(f))
            }
        }
        Rectangle {   // track
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width; height: 4; radius: 2
            color: Qt.rgba(1, 1, 1, 0.22)
        }
        Rectangle {   // fill
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * scrub.frac; height: 4; radius: 2
            color: theme.gold
        }
        Rectangle {   // thumb — grows on hover/drag like TB2's
            anchors.verticalCenter: parent.verticalCenter
            x: parent.width * scrub.frac - width / 2
            width: (scrubMa.containsMouse || scrubMa.pressed) ? 12 : 9
            height: width; radius: width / 2
            color: theme.gold; border.width: 1; border.color: "#1a1306"
        }
        Rectangle {   // hover/drag bubble — the page number under the cursor
            visible: scrubMa.containsMouse || scrubMa.pressed
            x: Math.max(0, Math.min(parent.width - width, scrubMa.mouseX - width / 2))
            anchors.bottom: parent.top; anchors.bottomMargin: 6
            width: bub.implicitWidth + 16; height: 24; radius: 7
            color: "#e0101218"; border.width: 1; border.color: theme.edge
            Text { id: bub; anchors.centerIn: parent
                text: scrub.pageAtFrac(scrub.fracAt(scrubMa.mouseX)) + " / " + reader.max
                color: theme.ink; font.family: theme.ui; font.pixelSize: 11 }
        }
        MouseArea {
            id: scrubMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: (m) => scrub.seek(scrub.fracAt(m.x))
            onPositionChanged: (m) => { if (pressed) scrub.seek(scrub.fracAt(m.x)) }
        }
    }

    // transient toast (zoom feedback)
    Rectangle {
        visible: reader.toast.length > 0
        z: 26
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top; anchors.topMargin: 76
        width: toastT.implicitWidth + 24; height: 32; radius: 9
        color: "#d5101218"; border.width: 1; border.color: theme.edge
        Text { id: toastT; anchors.centerIn: parent; text: reader.toast
            color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
    }

    // ===================== HUD (frosted glass, top) =====================
    Glass {
        id: hud
        backdrop: reader.backdrop
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 10
        height: 52; radius: 14
        visible: reader.max > 0 && !reader.atEnd
        opacity: reader.chromeShown ? 1 : 0
        enabled: reader.chromeShown
        Behavior on opacity { NumberAnimation { duration: 180 } }
        z: 20
        HoverHandler { onHoveredChanged: reader.hudHover = hovered }

        // left cluster: back · series · chapter chip
        Row {
            anchors.left: parent.left; anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 14
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink; font.family: theme.display
                font.pixelSize: 24; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() } }
            Text { text: reader.seriesTitle; color: theme.ink; font.family: theme.display; font.weight: Font.DemiBold
                font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
            // chapter chip → chapter modal
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: chRow.implicitWidth + 22; color: chMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: chMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : theme.edge
                Row { id: chRow; anchors.centerIn: parent; spacing: 6
                    Text { text: "≣"; color: theme.gold; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: reader.curLabel; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: chMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.showChapters = true } }
        }

        // right cluster
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 10
            // prev
            Text { text: "Prev"; color: pvMa.containsMouse ? theme.gold : theme.inkDim; font.family: theme.ui
                font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: pvMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.prevAction() } }
            // page chip → page-jump modal
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: pc.implicitWidth + 20; color: pgMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text { id: pc; anchors.centerIn: parent
                    // pair-aware label (TB2: "12-13 / 200" when two pages are up)
                    text: (reader.isDouble && reader.pair && reader.pair.partnerIndex !== null)
                          ? ((reader.pair.anchorIndex + 1) + "-" + (reader.pair.partnerIndex + 1) + " / " + reader.max)
                          : (reader.page + " / " + reader.max)
                    color: theme.gold; font.family: theme.display; font.pixelSize: 13; font.weight: Font.DemiBold }
                MouseArea { id: pgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.showJump = true } }
            // next
            Text { text: "Next"; color: nxMa.containsMouse ? theme.gold : theme.inkDim; font.family: theme.ui
                font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: nxMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.nextAction() } }
            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }

            // reading-mode dropdown trigger
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: mdRow.implicitWidth + 18; color: mdMa.containsMouse || reader.hudMenu === "mode" ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row { id: mdRow; anchors.centerIn: parent; spacing: 6
                    Text { text: reader.modeShort(reader.style); color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "▾"; color: theme.inkDim; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: mdMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.hudMenu = (reader.hudMenu === "mode" ? "" : "mode") } }

            // direction toggle (locked LTR in MangaPlus)
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28; width: dt.implicitWidth + 18
                color: dMa.containsMouse ? theme.glassHi : theme.glassTint; border.width: 1; border.color: theme.edge
                opacity: reader.style === "double_page_v2" ? 0.5 : 1
                Text { id: dt; anchors.centerIn: parent; text: reader.rtl ? "RTL" : "LTR"
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                MouseArea { id: dMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    enabled: reader.style !== "double_page_v2"
                    onClicked: reader.setDirection(reader.rtl ? "left_right" : "right_left") } }

            // change-pairing (double modes)
            Text { visible: reader.isDouble; text: "⇄"; font.pixelSize: 18
                color: reader.couplingNudge ? theme.gold : (swMa.containsMouse ? theme.gold : theme.inkDim)
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: swMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { reader.couplingNudge = reader.couplingNudge ? 0 : 1
                        reader.page = Engine.snapTwoPageIndex(reader.page - 1, reader.ctx()) + 1 } } }

            // width dropdown trigger (single/strip)
            Rectangle { visible: reader.style === "single_page" || reader.style === "long_strip"
                anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: wdRow.implicitWidth + 16; color: wdMa.containsMouse || reader.hudMenu === "width" ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row { id: wdRow; anchors.centerIn: parent; spacing: 4
                    Text { text: reader.portraitWidthPct + "%"; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "▾"; color: theme.inkDim; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: wdMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.hudMenu = (reader.hudMenu === "width" ? "" : "width") } }

            // settings (prefs modal)
            Text { text: "⚙"; color: grMa.containsMouse ? theme.gold : theme.inkDim; font.pixelSize: 18
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: grMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.showPrefs = true } }
            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }
            // window controls
            Image { source: "../assets/icons/minimize.svg"; sourceSize.width: 18; sourceSize.height: 18
                width: 18; height: 18; fillMode: Image.PreserveAspectFit; anchors.verticalCenter: parent.verticalCenter
                opacity: miMa.containsMouse ? 1 : 0.7
                MouseArea { id: miMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; sourceSize.width: 18; sourceSize.height: 18
                width: 18; height: 18; fillMode: Image.PreserveAspectFit; anchors.verticalCenter: parent.verticalCenter
                opacity: poMa.containsMouse ? 1 : 0.7
                MouseArea { id: poMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.closeRequested() } }
        }
    }

    // mode short labels
    function modeShort(s) {
        if (s === "long_strip") return "Strip"
        if (s === "single_page") return "Single"
        if (s === "double_page") return "Double"
        if (s === "double_page_v2") return "MangaPlus"
        return "Mode"
    }

    // ── HUD dropdown menus (mode / width) ──
    Rectangle {
        visible: reader.hudMenu === "mode"
        z: 30; radius: 10; color: "#15171f"; border.width: 1; border.color: theme.edge
        anchors.top: hud.bottom; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 140
        width: 190; height: modeCol.height + 12
        Column { id: modeCol; width: parent.width; y: 6
            Repeater {
                model: [{v:"long_strip",t:"Long Strip"},{v:"single_page",t:"Single Page"},
                        {v:"double_page",t:"Double Page"},{v:"double_page_v2",t:"Double Page (MangaPlus)"}]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 36; color: mi.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t; color: reader.style === modelData.v ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13 }
                    Text { visible: reader.style === modelData.v; anchors.right: parent.right; anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter; text: "✓"; color: theme.gold; font.pixelSize: 13 }
                    MouseArea { id: mi; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.setStyle(modelData.v); reader.hudMenu = "" } }
                }
            }
        }
    }
    Rectangle {
        visible: reader.hudMenu === "width"
        z: 30; radius: 10; color: "#15171f"; border.width: 1; border.color: theme.edge
        anchors.top: hud.bottom; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 60
        width: 92; height: wCol.height + 12
        Column { id: wCol; width: parent.width; y: 6
            Repeater {
                model: [50, 60, 70, 74, 78, 90, 100]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 32; color: wi.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData + "%"; color: reader.portraitWidthPct === modelData ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { id: wi; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.setWidthPct(modelData); reader.hudMenu = "" } }
                }
            }
        }
    }
    // dismiss a HUD dropdown on outside click
    MouseArea { anchors.fill: parent; z: 29; visible: reader.hudMenu !== ""
        onClicked: reader.hudMenu = "" }

    // ── right-click context menu ──
    MouseArea { anchors.fill: parent; z: 34; visible: reader.showCtx
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: reader.showCtx = false }
    Rectangle {
        visible: reader.showCtx
        z: 35; x: reader.ctxX; y: reader.ctxY
        width: 250; height: ctxCol.height + 12; radius: 10
        color: "#15171f"; border.width: 1; border.color: theme.edge
        Column {
            id: ctxCol; width: parent.width; y: 6
            Repeater {
                model: reader.ctxItems
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 34
                    color: cxMa.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t; color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight; width: parent.width - 24 }
                    MouseArea { id: cxMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: reader.ctxAction(parent.modelData) }
                }
            }
        }
    }

    // ===================== MODALS =====================
    component ModalScrim: Rectangle {
        anchors.fill: parent; color: Qt.rgba(0,0,0,0.62); z: 40
    }
    component ModalCard: Rectangle {
        anchors.centerIn: parent; z: 41; radius: 16; color: "#15171f"
        border.width: 1; border.color: theme.edge
    }

    // ── PREFERENCES ──
    ModalScrim { visible: reader.showPrefs; MouseArea { anchors.fill: parent; onClicked: reader.showPrefs = false } }
    ModalCard {
        visible: reader.showPrefs
        width: 420; height: prefCol.height + 40
        Column {
            id: prefCol; width: parent.width - 48; x: 24; y: 24; spacing: 6
            Text { text: "⚙  Preferences"; color: theme.ink; font.family: theme.display; font.pixelSize: 18
                bottomPadding: 8 }
            PrefToggle { label: "Pin toolbar"; checked: prefs.sticky_top_nav; onToggled: (v) => prefs.sticky_top_nav = v }
            PrefToggle { label: "Gap between pages"; checked: prefs.gap; onToggled: (v) => prefs.gap = v }
            PrefToggle { label: "Back-to-top button"; checked: prefs.back_to_top; onToggled: (v) => prefs.back_to_top = v }
            PrefToggle { label: "Dark background"; checked: prefs.dark_background; onToggled: (v) => prefs.dark_background = v }
            Item { width: 1; height: 8 }
            Text { text: "IMAGE FIT"; color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 11; font.letterSpacing: 2 }
            PrefRadio { label: "Fit width"; checked: reader.fit === "width"; onPicked: reader.setFit("width") }
            PrefRadio { label: "Fit height"; checked: reader.fit === "height"; onPicked: reader.setFit("height") }
        }
    }

    // ── CHAPTER SELECT ──
    ModalScrim { visible: reader.showChapters; MouseArea { anchors.fill: parent; onClicked: reader.showChapters = false } }
    ModalCard {
        visible: reader.showChapters
        width: 520; height: Math.min(parent.height * 0.72, 560)
        Text { id: chTitle; text: "≣  Select Chapter"; color: theme.ink; font.family: theme.display; font.pixelSize: 18
            x: 24; y: 22 }
        GridView {
            anchors.top: chTitle.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 96; cellHeight: 44
            model: reader.chapters
            delegate: Item {
                required property var modelData
                width: 96; height: 44
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 8
                    property bool active: String(modelData.id) === reader.curChapterId
                    color: active ? theme.gold : (cgMa.containsMouse ? theme.glassHi : theme.glassTint)
                    border.width: 1; border.color: active ? theme.gold : theme.edge
                    Text { anchors.centerIn: parent
                        text: (modelData.number !== undefined && modelData.number !== "") ? modelData.number
                              : (modelData.name || "?")
                        color: parent.active ? "#1a1306" : theme.ink; font.family: theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter }
                    MouseArea { id: cgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.showChapters = false
                            if (String(modelData.id) !== reader.curChapterId) reader.openChapterById(modelData.id, false) } }
                }
            }
        }
    }

    // ── PAGE JUMP ──
    ModalScrim { visible: reader.showJump; MouseArea { anchors.fill: parent; onClicked: reader.showJump = false } }
    ModalCard {
        visible: reader.showJump
        width: 520; height: Math.min(parent.height * 0.72, 560)
        Text { id: pgTitle; text: "Select Page"; color: theme.ink; font.family: theme.display; font.pixelSize: 18; x: 24; y: 22 }
        GridView {
            anchors.top: pgTitle.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 60; cellHeight: 44
            model: reader.max
            delegate: Item {
                required property int index
                width: 60; height: 44
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 8
                    property bool active: index + 1 === reader.page
                    color: active ? theme.gold : (pgGMa.containsMouse ? theme.glassHi : theme.glassTint)
                    border.width: 1; border.color: active ? theme.gold : theme.edge
                    Text { anchors.centerIn: parent; text: index + 1
                        color: parent.active ? "#1a1306" : theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { id: pgGMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        // jumpToPage lands the strip ON the page (the old code reset to the top)
                        onClicked: { reader.jumpToPage(index + 1); reader.showJump = false } }
                }
            }
        }
    }

    // ── PAGE THUMBNAILS (T) ──
    ModalScrim { visible: reader.showThumbs; MouseArea { anchors.fill: parent; onClicked: reader.showThumbs = false } }
    ModalCard {
        visible: reader.showThumbs
        width: 640; height: Math.min(parent.height * 0.78, 620)
        Text { id: thTitle; text: "Pages"; color: theme.ink; font.family: theme.display; font.pixelSize: 18; x: 24; y: 22 }
        GridView {
            anchors.top: thTitle.bottom; anchors.topMargin: 14
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 120; cellHeight: 196
            model: reader.showThumbs ? reader.max : 0   // built on open, freed on close
            delegate: Item {
                required property int index
                width: 120; height: 196
                Rectangle {
                    anchors.fill: parent; anchors.margins: 5; radius: 8
                    color: theme.glassTint
                    border.width: index + 1 === reader.page ? 2 : 1
                    border.color: index + 1 === reader.page ? theme.gold : theme.edge
                    Image {
                        anchors.fill: parent; anchors.margins: 4; anchors.bottomMargin: 22
                        fillMode: Image.PreserveAspectFit
                        source: (index < reader.max) ? reader.pagesModel[index].url : ""
                        asynchronous: true; cache: true
                        sourceSize.width: 110
                    }
                    Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (index + 1) + (reader.isBookmarked(index + 1) ? " ◆" : "")
                        color: index + 1 === reader.page ? theme.gold : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 11 }
                    MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.jumpToPage(index + 1); reader.showThumbs = false } }
                }
            }
        }
    }

    // ── SHORTCUTS (K) ──
    ModalScrim { visible: reader.showKeys; MouseArea { anchors.fill: parent; onClicked: reader.showKeys = false } }
    ModalCard {
        visible: reader.showKeys
        width: 430; height: keysCol.height + 48
        Column {
            id: keysCol; width: parent.width - 48; x: 24; y: 24; spacing: 4
            Text { text: "Shortcuts"; color: theme.ink; font.family: theme.display; font.pixelSize: 18; bottomPadding: 8 }
            Repeater {
                model: [
                    ["← → · Space · PgUp/PgDn", "turn / scroll (Shift = back)"],
                    ["Home · End", "first / last page"],
                    ["H · center-click", "hide or show the bars"],
                    ["Ctrl+wheel · Ctrl+0", "zoom / reset zoom"],
                    ["drag · arrows", "pan while zoomed"],
                    ["B", "bookmark this page"],
                    ["Z", "instant replay (a beat back)"],
                    ["S", "save checkpoint"],
                    ["R", "back to the start"],
                    ["T", "page thumbnails"],
                    ["Ctrl+G", "go to page"],
                    ["right-click", "everything else"],
                    ["Esc", "close / leave"]
                ]
                delegate: Row {
                    required property var modelData
                    spacing: 12; height: 26
                    Text { text: modelData[0]; color: theme.gold; font.family: theme.ui; font.pixelSize: 13
                        width: 190; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: modelData[1]; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }
    }

    // ── reusable pref controls ──
    component PrefToggle: Item {
        id: ptRoot
        property string label: ""
        property bool checked: false
        signal toggled(bool v)
        width: parent ? parent.width : 360; height: 40
        Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            text: ptRoot.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 14 }
        Rectangle {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 42; height: 24; radius: 12
            color: ptRoot.checked ? theme.gold : theme.glassTint; border.width: 1; border.color: theme.edge
            Rectangle { width: 18; height: 18; radius: 9; color: "#ffffff"; y: 3
                x: ptRoot.checked ? parent.width - width - 3 : 3
                Behavior on x { NumberAnimation { duration: 120 } } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            onClicked: ptRoot.toggled(!ptRoot.checked) }
    }
    component PrefRadio: Item {
        id: prRoot
        property string label: ""
        property bool checked: false
        signal picked()
        width: parent ? parent.width : 360; height: 34
        Row { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: 10
            Rectangle { width: 18; height: 18; radius: 9; color: "transparent"; border.width: 2
                border.color: prRoot.checked ? theme.gold : theme.edge; anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 5; color: theme.gold; visible: prRoot.checked } }
            Text { text: prRoot.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: prRoot.picked() }
    }
}
