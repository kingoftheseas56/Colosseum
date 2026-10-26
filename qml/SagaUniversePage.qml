// Saga universe (Arc 58): one grid for the saga — a column per story, a row per form — so a
// book and its adaptation carry the same weight. Layout authority is the approved mock,
// Brotherhood/agents/colosseum-universe-saga-organised-mock.html, measured at 1280x720.
// Spec: docs/superpowers/specs/2026-10-04-colosseum-saga-universe-template-design.md.
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import QtQuick.Controls
import "SagaApi.js" as Saga
import "ComicsApi.js" as ComicsApi
import "Universes.js" as UDB

Item {
    id: root
    onUniverseContentGenerationChanged: reload()
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0
    anchors.fill: parent

    // shell contract (bookRequested is this template's own verb)
    property Item backdrop: null
    property var contentStore: typeof Content !== "undefined" ? Content : null
    readonly property string fallbackWallpaperSource: root.contentStore
        ? (root.contentStore.generation, root.contentStore.assetUrl("assets/wallpaper/cold-ripple.jpg")) : ""
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)     // film/show tile → A4's TheatreSeries
    signal bookRequested(var book)      // novel tile / Read → the Biblio book detail
    signal seriesRequested(string title)   // manga tile / Read → Tankoban's series page (by its curated query)
    signal comicsArchiveRequested(var box)   // the comics door → the GC archive index
    signal continueResumeRequested(var entry)   // Continue circle → resume in the owning world
    signal continueDetailRequested(var entry)   // Continue tile → its title page
    signal universeRequested(string name)       // a "More universes" card

    Theme { id: theme }
    // Typography and control geometry adapted from the supplied Harbor QML prototype.
    FontLoader { id: sagaDisplay; source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { id: sagaUi; source: "../assets/fonts/Switzer-Regular.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Medium.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Semibold.otf" }
    readonly property string uiFace: sagaUi.name
    readonly property string displayFace: sagaDisplay.name
    property bool reducedMotion: true
    readonly property var guide: (root.universeContentGeneration, UDB.configFor)(root.universeName || root.uni.name)
    // an anime universe (curated manga list) is the same page with the read form in manga:
    // the hero kicker reads ANIME, the read row and blocks say MANGA, and the read form
    // opens Tankoban. One variation per TYPE, never per franchise — all data-driven.
    readonly property string readWord: "Book"
    readonly property color worldColor: guide.c1 || "#221c30"
    readonly property color paper: theme.ink
    readonly property color hair: Qt.rgba(1, 1, 1, 0.09)
    readonly property color goldEdge: Qt.rgba(0.94, 0.77, 0.29, 0.7)
    readonly property real gutter: width < 700 ? 24 : 64
    readonly property bool compact: width < 800
    property int loadGeneration: 0

    property var uni: ({ name: "", blurb: "", banner: "", metaline: "", books: [], films: [], shows: [],
                         comics: null })
    // the saga drawn as a grid: stories, rows, blocks (pure — SagaApi.sagaGrid)
    readonly property var grid: Saga.sagaGrid(root.uni, root.guide)
    readonly property var collections: Saga.sagaCollections(root.uni, root.guide)
    readonly property var works: (root.uni.books || []).concat(root.uni.films || [], root.uni.shows || [])

    // ---- grid geometry: tiles grow for a short saga, shrink no further than 124 ----
    readonly property int tileGap: 16
    readonly property real panelInner: Math.max(200, width - gutter * 2 - 48)
    readonly property real tileMax: grid.rows.length >= 3 ? 132 : 164
    readonly property real tileW: {
        const n = Math.max(1, root.grid.cols)
        return Math.max(124, Math.min(root.tileMax, Math.floor((root.panelInner - (n - 1) * root.tileGap) / n)))
    }
    readonly property real tileH: Math.round(tileW * 1.5)
    readonly property real gridW: Math.max(0, grid.cols * tileW + (grid.cols - 1) * tileGap)
    readonly property int headH: 54      // story headers: up to three lines
    readonly property real gridH: headH + grid.rows.length * (28 + tileH + 14)
    // a short saga leaves room beside the grid; the detail moves there instead of leaving it empty
    readonly property bool sideDetail: grid.cols > 0 && grid.cols <= 4 && !compact

    // ---- identity: loader pushes rebuild every item, so selection is held by key ----
    function keyOf(item) {
        if (!item) return ""
        return item.medium !== undefined && item.canonIndex !== undefined
                ? item.medium + ":" + item.canonIndex : "x:" + (item.id || item.title || "")
    }
    function isBook(item) {
        if (!item) return false
        if (item.medium !== undefined) return item.medium === "book" || item.medium === "manga"
        return (root.uni.books || []).indexOf(item) >= 0
    }
    function isSeries(item) { return !!item && (item.medium ? item.medium === "series" : item.type === "series") }
    function kindLabel(item) {
        if (!!item && item.medium === "manga") return "MANGA"
        return root.isBook(item) ? "BOOK" : (root.isSeries(item) ? "SERIES" : "FILM")
    }
    function canOpen(item) { return !!item && item.resolved !== false }
    function openWork(item) {
        if (!root.canOpen(item)) return
        if (item.medium === "manga") root.seriesRequested(item.query || item.title)
        else if (root.isBook(item)) root.bookRequested(item)
        else root.watchRequested(item)
    }
    function locate(item) {
        const key = root.keyOf(item)
        for (let r = 0; r < root.grid.rows.length; ++r)
            for (let c = 0; c < root.grid.rows[r].cells.length; ++c)
                if (root.keyOf(root.grid.rows[r].cells[c].item) === key) return { row: r, cell: c }
        return null
    }

    // ---- progress: where you are, in either form. Gold marks this and focus, nothing else ----
    property int progressRevision: typeof Progress !== "undefined" ? Progress.revision : 0
    // preview/test seam: [{ id, kind: "book"|"video", title, sub, cover, progress, watched }]
    property var previewProgress: null
    readonly property var recentEntries: {
        root.progressRevision
        if (root.previewProgress) return root.previewProgress
        return typeof Progress !== "undefined" ? Progress.recent("", 200) : []
    }
    function entryMatches(entry, item) {
        if (!entry || !root.canOpen(item) || item.id === undefined) return false
        const kind = String(entry.kind || ""), id = String(entry.id || "")
        if (root.isBook(item)) return kind === "book" && id === String(item.id)
        return kind === "video" && id.split(":")[0] === String(item.id)
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
    function resumeOf(wantBook) {
        const list = root.recentEntries
        for (let i = 0; i < list.length; ++i)
            for (let w = 0; w < root.works.length; ++w)
                if (root.isBook(root.works[w]) === wantBook && root.entryMatches(list[i], root.works[w])
                        && list[i].watched !== true)
                    return root.works[w]
        return null
    }
    function firstOf(wantBook) {
        for (let r = 0; r < root.grid.rows.length; ++r)
            for (let c = 0; c < root.grid.rows[r].cells.length; ++c) {
                const item = root.grid.rows[r].cells[c].item
                if (root.isBook(item) === wantBook && root.canOpen(item) && item.upcoming !== true) return item
            }
        for (let w = 0; w < root.works.length; ++w)
            if (root.isBook(root.works[w]) === wantBook && root.canOpen(root.works[w]) && root.works[w].upcoming !== true)
                return root.works[w]
        return null
    }
    readonly property var resumeBook: root.resumeOf(true)
    readonly property var resumeWatch: root.resumeOf(false)
    readonly property var readTarget: root.resumeBook || root.firstOf(true)
    readonly property var watchTarget: root.resumeWatch || root.firstOf(false)
    // "Book 3" / "Film 2": only while resuming, and only for a work that sits in the grid
    function resumeLabel(item) {
        const at = item ? root.locate(item) : null
        if (!at) return ""
        const cell = root.grid.rows[at.row].cells[at.cell]
        if (root.isBook(item)) return root.readWord + " " + root.storyAt(cell.col).n
        return root.isSeries(item) ? "" : "Film " + (at.cell + 1)
    }
    function storyAt(col) {
        for (let i = 0; i < root.grid.stories.length; ++i) {
            const s = root.grid.stories[i]
            if (col >= s.col && col < s.col + s.span) return s
        }
        return root.grid.stories[0]
    }

    // ---- the one work the detail line describes: hover and keyboard both move it ----
    property string selectedKey: ""
    readonly property var shown: {
        if (root.selectedKey.length)
            for (let w = 0; w < root.works.length; ++w)
                if (root.keyOf(root.works[w]) === root.selectedKey) return root.works[w]
        return root.readTarget || root.watchTarget
    }
    function select(item) { root.selectedKey = root.keyOf(item) }
    function detailOf(item) {
        root.uni                       // re-read when the loader pushes (a detail may have landed)
        return item && !root.isBook(item) && item.id ? Saga.detailFor(item.id) : null
    }
    readonly property string shownWhere: {
        const item = root.shown
        if (!item) return ""
        let where = ""
        const at = root.locate(item)
        if (at) {
            const cell = root.grid.rows[at.row].cells[at.cell]
            const a = root.storyAt(cell.col), b = root.storyAt(cell.col + cell.span - 1)
            where = a === b ? "STORY " + a.n + " OF " + root.grid.stories.length : "STORIES " + a.n + " TO " + b.n
            where += " · " + root.kindLabel(item)
            if (!root.isBook(item) && a === b && a.span > 1 && cell.span === 1)
                where += " · PART " + (cell.col - a.col + 1)
        } else {
            for (let i = 0; i < root.grid.blocks.length && !where.length; ++i)
                for (let s = 0; s < root.grid.blocks[i].shelves.length; ++s)
                    for (let k = 0; k < root.grid.blocks[i].shelves[s].items.length; ++k)
                        if (root.keyOf(root.grid.blocks[i].shelves[s].items[k]) === root.keyOf(item))
                            where = root.grid.blocks[i].title.toUpperCase() + " · " + root.kindLabel(item)
            if (!where.length) where = root.kindLabel(item)
        }
        const f = root.fraction(item), e = root.entryFor(item)
        if (item.upcoming === true) where += " · UPCOMING"
        else if (f >= 1) where += root.isBook(item) ? " · READ" : " · WATCHED"
        else if (e && root.isSeries(item) && e.sub) where += " · " + String(e.sub).toUpperCase()
        else if (f > 0) where += " · " + Math.round(f * 100) + "% " + (root.isBook(item) ? "READ" : "WATCHED")
        return where
    }
    readonly property string shownMeta: {
        const item = root.shown
        if (!item) return ""
        if (root.isBook(item)) return item.author || ""
        const d = root.detailOf(item), parts = []
        if ((d && d.span) || item.year) parts.push((d && d.span) || item.year)
        if (root.isSeries(item)) { if (d && d.seasons) parts.push(d.seasons + (d.seasons === 1 ? " season" : " seasons")) }
        else if (d && d.runtime) parts.push(d.runtime)
        if (d && !root.isSeries(item) && d.people.length) parts.push(d.people.map(function(p) { return p.name }).join(", "))
        if (d && d.rating) parts.push("IMDb " + d.rating)
        return parts.join(" · ")
    }
    readonly property string shownSummary: {
        const item = root.shown
        if (!item) return ""
        if (root.isBook(item)) return item.synopsis || ""
        const d = root.detailOf(item)
        return d ? d.summary : ""
    }
    readonly property string shownAction: {
        const item = root.shown, book = root.isBook(item), f = root.fraction(item)
        if (f >= 1) return book ? "Read again" : "Watch again"
        if (f > 0) return book ? "Continue reading" : "Continue watching"
        return book ? "Read" : "Watch"
    }

    // ---- hero facts, creators, information: derived from the records, never written down ----
    readonly property var facts: {
        const out = []
        if (root.grid.stories.length) out.push(root.grid.stories.length + (root.grid.stories.length === 1 ? " story" : " stories"))
        const chips = root.guide.chips || []
        for (let i = 0; i < chips.length; ++i) out.push(chips[i].t)
        return out
    }
    readonly property var creators: {
        root.uni
        const map = {}, order = []
        function add(name, role, item) {
            name = String(name || "").trim()
            if (!name.length) return
            if (!map[name]) { map[name] = { name: name, role: role, count: 0, title: item.title }; order.push(name) }
            if (map[name].role === role) map[name].count++
        }
        root.works.forEach(function(item) {
            if (!root.canOpen(item)) return
            if (root.isBook(item)) {
                String(item.author || "").split(/\s*(?:&|,)\s*/).forEach(function(n) { if (n.indexOf("Jr") !== 0) add(n, "Author", item) })
            } else {
                const d = root.detailOf(item)
                if (d) d.people.forEach(function(p) { add(p.name, p.role, item) })
            }
        })
        const rank = { Author: 0, Director: 1, Writer: 2 }
        // the lead author always; a co-author only once they have signed more than one book
        // (a store's byline also carries illustrators and in-world pen names)
        let lead = null
        order.forEach(function(n) { if (map[n].role === "Author" && (!lead || map[n].count > lead.count)) lead = map[n] })
        return order.map(function(n) { return map[n] })
            .filter(function(p) { return p.role !== "Author" || p === lead || p.count > 1 })
            .sort(function(a, b) { return rank[a.role] - rank[b.role] || b.count - a.count })
            .slice(0, 6).map(function(p) {
                const what = p.role === "Author" ? p.count + (p.count === 1 ? " book" : " books")
                           : p.role === "Director" ? p.count + (p.count === 1 ? " film" : " films") : p.title
                return { name: p.name, role: p.role + " · " + what }
            })
    }
    readonly property var information: {
        const out = [], books = [], films = [], shows = []
        root.works.forEach(function(item) {
            if (!root.canOpen(item)) return
            (root.isBook(item) ? books : (root.isSeries(item) ? shows : films)).push(item)
        })
        if (root.creators.length && root.creators[0].role.indexOf("Author") === 0) out.push(["CREATOR", root.creators[0].name])
        if (books.length) out.push(["BOOKS", String(books.length)])
        if (films.length) {
            const years = films.map(function(f) { return f.year }).filter(function(y) { return y > 0 }).sort()
            out.push(["FILMS", films.length + (years.length > 1 && years[0] !== years[years.length - 1]
                                               ? " · " + years[0] + " to " + years[years.length - 1] : "")])
        }
        if (shows.length) out.push(["TELEVISION", shows.length + " series"])
        out.push(["CATALOGUE", "Apple Books · Cinemeta"])
        return out
    }
    readonly property var stills: root.works.filter(function(item) {
        return !root.isBook(item) && root.canOpen(item) && !!item.art
    }).slice(0, 4)
    readonly property var siblings: (root.universeContentGeneration, UDB.universes).concat((root.universeContentGeneration, UDB.archive)).filter(function(u) {
        return u.category === "saga" && !!u.wallpaper && u.name !== (root.universeName || root.uni.name)
    }).slice(0, 4)

    // ---- scrolling ----
    function revealSection(section) {
        page.contentY = Math.min(Math.max(0, section.mapToItem(page.contentItem, 0, 0).y - 96),
                                 Math.max(0, page.contentHeight - page.height))
    }
    function ensureVisible(item) {
        const top = item.mapToItem(page.contentItem, 0, 0).y, bottom = top + item.height
        const limit = Math.max(0, page.contentHeight - page.height)
        // 72 clears the docked switcher. Something taller than the room left shows its top, never its tail.
        if (top - 72 < page.contentY || item.height + 72 > page.height) page.contentY = Math.min(limit, Math.max(0, top - 72))
        else if (bottom > page.contentY + page.height) page.contentY = Math.min(limit, bottom - page.height)
    }
    readonly property var tabs: {
        const out = [{ title: "The saga", target: sagaSection }]
        if (beyondSection.visible) out.push({ title: "Beyond the saga", target: beyondSection })
        if (upcomingSection.visible) out.push({ title: "Upcoming", target: upcomingSection })
        out.push({ title: "About", target: aboutAnchor })
        return out
    }
    readonly property int activeTab: {
        page.contentY; col.implicitHeight
        let active = 0
        for (let i = 0; i < root.tabs.length; ++i)
            if (root.tabs[i].target.mapToItem(page.contentItem, 0, 0).y - 160 <= page.contentY) active = i
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

    // the pinned archive resolved live (real GC name + release count); curated pin = fallback
    property var comicsBox: null
    property int comicsGeneration: -1
    onUniChanged: {
        if (root.uni.comics && root.uni.comics.tagId && root.comicsGeneration !== root.loadGeneration) {
            const generation = root.loadGeneration
            root.comicsGeneration = generation
            ComicsApi.tagBox(root.uni.comics.tagId, function(b) {
                if (b && generation === root.loadGeneration) root.comicsBox = b
            })
        }
    }
    function comicsDoor() {
        return root.comicsBox || { name: root.uni.name, tag: root.uni.comics.tag,
                                   tagId: root.uni.comics.tagId, count: 0 }
    }

    function reload() {
        if (!root.universeName.length) return         // never load a default universe (the OP-flash lesson)
        root.comicsBox = null
        root.selectedKey = ""
        page.contentY = 0
        const generation = ++root.loadGeneration
        Saga.loadSaga(root.universeName, function(u) {
            if (generation === root.loadGeneration && u) root.uni = u
        })
    }
    Component.onCompleted: { reload(); root.forceActiveFocus(Qt.TabFocusReason) }
    onUniverseNameChanged: reload()

    // ---- persistent wallpaper the page floats over; also what every Glass surface frosts ----
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
                objectName: "fallbackWallpaperImage"
                source: root.fallbackWallpaperSource
                fillMode: Image.PreserveAspectCrop; cache: true }
        Image {
            id: worldWallpaper
            anchors.fill: parent
            source: root.guide.wallpaper || root.uni.banner
            asynchronous: true; cache: true
            fillMode: Image.PreserveAspectCrop
        }
        // Harbor's directional scrims keep the world visible behind the content.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0.067, 0.071, 0.075, 0.88) }
                GradientStop { position: 1; color: Qt.rgba(0.067, 0.071, 0.075, 0.36) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.45; color: Qt.rgba(0.067, 0.071, 0.075, 0.4) }
                GradientStop { position: 1; color: Qt.rgba(0.067, 0.071, 0.075, 0.92) }
            }
        }
    }

    Flickable {
        id: page
        objectName: "sagaScroll"
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

            // ===== hero: the world leads; both forms have the same action weight =====
            Item {
                id: hero
                width: parent.width
                height: root.compact ? 420 : 400
                Column {
                    x: root.gutter; y: 120
                    width: parent.width - root.gutter * 2
                    spacing: 18
                    Text {
                        text: "SAGA"
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
                        SagaAction {
                            objectName: "sagaReadButton"
                            text: "Read"; primary: true
                            sub: root.resumeBook ? root.resumeLabel(root.resumeBook) : ""
                            enabled: !!root.readTarget
                            onTriggered: root.openWork(root.readTarget)
                        }
                        SagaAction {
                            objectName: "sagaAdaptationsButton"
                            text: "Watch"; primary: true
                            sub: root.resumeWatch ? root.resumeLabel(root.resumeWatch) : ""
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
                objectName: "sagaContinue"
                title: "Continue"
                visible: root.continueItems.length > 0
                Row {
                    spacing: 16
                    Repeater {
                        model: root.continueItems.slice(0, Math.max(1, Math.floor((continueSection.width + 16) / 356)))
                        delegate: ContinueTile {
                            id: continueTile
                            required property var modelData
                            variant: "home"
                            entry: continueTile.modelData
                            backdrop: wall
                            track: page.contentY
                            onResumeRequested: root.continueResumeRequested(continueTile.modelData)
                            onDetailRequested: root.continueDetailRequested(continueTile.modelData)
                        }
                    }
                }
            }

            // ===== THE SAGA: a column per story, a row per form =====
            Section {
                id: sagaSection
                objectName: "sagaGrid"
                title: "The saga"
                visible: root.grid.cols > 0
                Glass {
                    id: sagaPanel
                    backdrop: wall
                    track: page.contentY
                    radius: 18
                    width: parent.width
                    height: 48 + (root.sideDetail ? Math.max(root.gridH, root.headH + detail.implicitHeight)
                                                  : root.gridH + detail.implicitHeight)

                    Item {
                        id: gridBox
                        x: 24; y: 24
                        width: Math.min(root.gridW, root.panelInner)
                        height: root.gridH

                        Flickable {
                            id: gridFlick
                            objectName: "sagaGridScroll"
                            // 6px breathing room so the focus ring is never clipped
                            x: -6; y: -6
                            width: parent.width + 12; height: parent.height + 12
                            contentWidth: root.gridW + 12; contentHeight: height
                            clip: true
                            interactive: root.gridW > gridBox.width
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            function pageBy(direction) {
                                slide.stop()
                                slide.to = Math.max(0, Math.min(contentWidth - width, contentX + direction * width * 0.8))
                                slide.start()
                            }
                            function revealCell(cell) {
                                const left = cell.col * (root.tileW + root.tileGap)
                                const right = left + cell.span * root.tileW + (cell.span - 1) * root.tileGap + 12
                                if (left < contentX) contentX = left
                                else if (right > contentX + width) contentX = Math.min(contentWidth - width, right - width)
                            }
                            NumberAnimation {
                                id: slide; target: gridFlick; property: "contentX"
                                duration: root.reducedMotion ? 0 : 300; easing.type: Easing.OutCubic
                            }

                            Item {
                                x: 6; y: 6
                                width: root.gridW; height: root.gridH

                                // story headers
                                Repeater {
                                    model: root.grid.stories
                                    delegate: Item {
                                        id: head
                                        required property var modelData
                                        x: head.modelData.col * (root.tileW + root.tileGap)
                                        width: head.modelData.span * root.tileW + (head.modelData.span - 1) * root.tileGap
                                        height: root.headH
                                        Row {
                                            spacing: 8
                                            anchors.horizontalCenter: head.modelData.span > 1 ? parent.horizontalCenter : undefined
                                            Rectangle {
                                                width: 22; height: 22; radius: 6
                                                color: Qt.rgba(0, 0, 0, 0.62)
                                                border.width: 1; border.color: theme.edge
                                                Text { anchors.centerIn: parent; text: head.modelData.n
                                                       color: root.paper; font.family: root.uiFace
                                                       font.pixelSize: 12; font.weight: Font.DemiBold }
                                            }
                                            Text {
                                                y: 2
                                                width: Math.min(implicitWidth, head.width - 34)
                                                text: head.modelData.short
                                                color: root.paper; font.family: root.uiFace
                                                font.pixelSize: 12; font.weight: Font.Medium
                                                lineHeight: 1.1; wrapMode: Text.WordWrap
                                                maximumLineCount: 3; elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                // one row per form
                                Repeater {
                                    id: rowRepeater
                                    model: root.grid.rows
                                    delegate: Item {
                                        id: gridRow
                                        required property var modelData
                                        required property int index
                                        y: root.headH + gridRow.index * (28 + root.tileH + 14)
                                        width: root.gridW; height: 28 + root.tileH
                                        Text {
                                            id: rowLabel
                                            anchors.verticalCenter: rowRule.verticalCenter
                                            text: gridRow.modelData.label
                                            color: theme.inkDimmer; font.family: root.uiFace
                                            font.pixelSize: 10; font.letterSpacing: 2
                                        }
                                        Rectangle {
                                            id: rowRule
                                            x: rowLabel.width + 12; y: 14
                                            width: parent.width - x; height: 1; color: root.hair
                                        }
                                        Repeater {
                                            model: gridRow.modelData.cells
                                            delegate: SagaTile {
                                                id: cellTile
                                                required property var modelData
                                                required property int index
                                                readonly property real spanW: cellTile.modelData.span * root.tileW
                                                                              + (cellTile.modelData.span - 1) * root.tileGap
                                                item: cellTile.modelData.item
                                                form: cellTile.modelData.form
                                                y: 28
                                                x: cellTile.modelData.col * (root.tileW + root.tileGap)
                                                   + (cellTile.modelData.form === "center" ? (cellTile.spanW - root.tileW) / 2 : 0)
                                                width: cellTile.modelData.form === "banner" ? cellTile.spanW : root.tileW
                                                height: root.tileH
                                                ring: gridFocus.activeFocus && gridFocus.row === gridRow.index
                                                      && gridFocus.cell === cellTile.index
                                                onHoveredIn: { gridFocus.row = gridRow.index; gridFocus.cell = cellTile.index }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        SagaChevron {
                            objectName: "sagaGridPrevious"
                            x: -6; y: root.headH; height: root.gridH - root.headH
                            visible: gridFlick.interactive && gridFlick.contentX > 1
                            onTriggered: gridFlick.pageBy(-1)
                        }
                        SagaChevron {
                            objectName: "sagaGridNext"
                            x: parent.width + 6 - width; y: root.headH; height: root.gridH - root.headH; atRight: true
                            visible: gridFlick.interactive && gridFlick.contentX < gridFlick.contentWidth - gridFlick.width - 1
                            onTriggered: gridFlick.pageBy(1)
                        }

                        // One keyboard stop for the whole grid. Left/Right walk the story; Up/Down
                        // move between the forms of the same story; Enter opens the work.
                        Item {
                            id: gridFocus
                            objectName: "sagaGridFocus"
                            anchors.fill: parent
                            property int row: 0
                            property int cell: 0
                            readonly property var current: {
                                const r = root.grid.rows[gridFocus.row]
                                return r && r.cells[gridFocus.cell] ? r.cells[gridFocus.cell] : null
                            }
                            focusPolicy: root.grid.cols > 0 ? Qt.TabFocus : Qt.NoFocus
                            function land() {
                                if (!gridFocus.current) { gridFocus.row = 0; gridFocus.cell = 0 }
                                if (!gridFocus.current) return
                                gridFlick.revealCell(gridFocus.current)
                                root.select(gridFocus.current.item)
                            }
                            onActiveFocusChanged: if (activeFocus) {
                                const at = root.shown ? root.locate(root.shown) : null
                                if (at) { gridFocus.row = at.row; gridFocus.cell = at.cell }
                                gridFocus.land()
                                root.ensureVisible(sagaPanel)
                            }
                            Keys.onPressed: (event) => {
                                const dx = event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Left ? -1 : 0
                                const dy = event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Up ? -1 : 0
                                if (dx || dy) {
                                    const next = Saga.gridMove(root.grid, gridFocus.row, gridFocus.cell, dx, dy)
                                    if (!next) return               // boundary: the arrow leaves the grid
                                    gridFocus.row = next.row; gridFocus.cell = next.cell
                                    gridFocus.land()
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    if (gridFocus.current) root.openWork(gridFocus.current.item)
                                    event.accepted = true
                                }
                            }
                            Accessible.role: Accessible.List
                            Accessible.name: "The saga: " + (root.shown ? root.shown.title : "")
                            Accessible.focusable: focusPolicy !== Qt.NoFocus
                        }
                    }

                    // the detail line: what the tile under the pointer or the keyboard is
                    Rectangle {
                        visible: !root.sideDetail
                        x: 24; y: 24 + root.gridH
                        width: parent.width - 48; height: 1; color: root.hair
                    }
                    Rectangle {
                        visible: root.sideDetail
                        x: 24 + gridBox.width + 40; y: 24 + root.headH
                        width: 1; height: parent.height - 48 - root.headH; color: root.hair
                    }
                    Item {
                        id: detail
                        objectName: "sagaDetail"
                        x: root.sideDetail ? 24 + gridBox.width + 80 : 24
                        y: root.sideDetail ? 24 + root.headH : 24 + root.gridH + 20
                        width: parent.width - x - 24
                        implicitHeight: root.sideDetail ? detailText.implicitHeight + 24 + detailGo.height
                                                        : Math.max(detailText.implicitHeight, detailGo.height) + 20
                        Column {
                            id: detailText
                            width: root.sideDetail ? parent.width : Math.min(760, parent.width - detailGo.width - 32)
                            spacing: 6
                            Text { text: root.shownWhere
                                   color: theme.inkDimmer; font.family: root.uiFace
                                   font.pixelSize: 10; font.letterSpacing: 2 }
                            Text { width: parent.width; text: root.shown ? root.shown.title : ""
                                   color: root.paper; font.family: root.displayFace; font.pixelSize: 24
                                   wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                            Text { width: parent.width; visible: text.length > 0; text: root.shownMeta
                                   color: theme.inkDim; font.family: root.uiFace; font.pixelSize: 12
                                   elide: Text.ElideRight }
                            Text { width: parent.width; visible: text.length > 0; text: root.shownSummary
                                   color: theme.inkDim; font.family: root.uiFace; font.pixelSize: 13
                                   lineHeight: 1.25; wrapMode: Text.WordWrap
                                   maximumLineCount: root.sideDetail ? 6 : 2; elide: Text.ElideRight }
                        }
                        Column {
                            id: detailGo
                            spacing: 8
                            x: root.sideDetail ? 0 : parent.width - width
                            y: root.sideDetail ? detailText.implicitHeight + 24 : Math.max(0, (detailText.implicitHeight - height) / 2)
                            SagaAction {
                                objectName: "sagaDetailAction"
                                anchors.right: root.sideDetail ? undefined : parent.right
                                text: root.shownAction; primary: true
                                enabled: root.canOpen(root.shown)
                                onTriggered: root.openWork(root.shown)
                            }
                            Text {
                                anchors.right: root.sideDetail ? undefined : parent.right
                                text: !root.canOpen(root.shown) ? "Details pending"
                                      : (root.shown && root.shown.medium === "manga" ? "Opens in Tankoban"
                                         : (root.isBook(root.shown) ? "Opens in Biblio" : "Opens in Theatre"))
                                color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // ===== beyond the saga: branches, and adaptations that go their own way =====
            Section {
                id: beyondSection
                objectName: "sagaBranches"
                title: "Beyond the saga"
                visible: root.grid.blocks.length > 0 || !!root.uni.comics
                Grid {
                    id: blockGrid
                    width: parent.width
                    visible: root.grid.blocks.length > 0
                    columns: root.grid.blocks.length >= 2 && !root.compact ? 2 : 1
                    columnSpacing: 24; rowSpacing: 24
                    property real tallest: 0
                    Repeater {
                        model: root.grid.blocks
                        onModelChanged: blockGrid.tallest = 0
                        delegate: Glass {
                            id: block
                            required property var modelData
                            backdrop: wall
                            track: page.contentY
                            radius: 18
                            width: (blockGrid.width - (blockGrid.columns - 1) * 24) / blockGrid.columns
                            height: Math.max(blockGrid.tallest, blockCol.implicitHeight) + 48
                            Column {
                                id: blockCol
                                x: 24; y: 24
                                width: parent.width - 48
                                spacing: 10
                                onImplicitHeightChanged: blockGrid.tallest = Math.max(blockGrid.tallest, implicitHeight)
                                Text { text: block.modelData.title; color: root.paper
                                       font.family: root.displayFace; font.pixelSize: 20 }
                                Flow {
                                    width: parent.width
                                    spacing: 40
                                    Repeater {
                                        model: block.modelData.shelves
                                        delegate: Shelf {
                                            required property var modelData
                                            label: modelData.label
                                            items: modelData.items
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // the comics door — the canon in print (curated GC pin)
                Rectangle {
                    width: parent.width; height: 108
                    radius: 12; clip: true
                    visible: !!root.uni.comics
                    color: "#241813"
                    border.width: 1
                    border.color: sagaComicsKey.interactionActive ? root.goldEdge : Qt.rgba(0.97, 0.97, 0.96, 0.10)
                    Image {
                        anchors.fill: parent
                        source: root.uni.comics ? root.uni.banner : ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? (sagaComicsKey.interactionActive ? 0.5 : 0.28) : 0
                        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.76) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.30) }
                        }
                    }
                    Column {
                        anchors.left: parent.left; anchors.leftMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - (root.compact ? 52 : 270)
                        spacing: 7
                        Text { text: "COMICS"; color: theme.inkDimmer
                               font.family: root.uiFace; font.pixelSize: 10; font.letterSpacing: 3 }
                        Text {
                            text: "Graphic adaptations"
                            width: parent.width; elide: Text.ElideRight
                            color: theme.ink; font.family: root.displayFace; font.pixelSize: 19
                        }
                    }
                    Text {
                        visible: !root.compact
                        anchors.right: parent.right; anchors.rightMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.comicsBox && root.comicsBox.count
                              ? "Browse " + root.comicsBox.count + " releases" : "Browse the archive"
                        color: theme.ink; font.family: root.uiFace
                        font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                    KeyboardAction {
                        id: sagaComicsKey
                        anchors.fill: parent
                        focusEnabled: parent.visible
                        focusRadius: 12
                        accessibleName: "Browse the comics archive"
                        onTriggered: root.comicsArchiveRequested(root.comicsDoor())
                    }
                }
            }

            // ===== upcoming: what the providers already carry an identity for =====
            Section {
                id: upcomingSection
                objectName: "sagaUpcoming"
                title: "Upcoming"
                visible: root.grid.upcoming.length > 0
                Glass {
                    backdrop: wall
                    track: page.contentY
                    radius: 18
                    width: parent.width
                    height: upcomingShelf.implicitHeight + 48
                    Shelf {
                        id: upcomingShelf
                        x: 24; y: 24
                        width: parent.width - 48
                        label: ""
                        items: root.grid.upcoming
                    }
                }
            }

            // ===== about: gallery, creators, information, more universes =====
            Item { id: aboutAnchor; width: 1; height: 1 }
            Section {
                id: aboutSection
                objectName: "sagaGallery"
                title: "Gallery"
                visible: root.stills.length > 0
                Row {
                    spacing: 24
                    Repeater {
                        model: root.stills
                        delegate: Rectangle {
                            id: still
                            required property var modelData
                            width: (aboutSection.width - 72) / 4; height: Math.round(width * 9 / 16)
                            radius: 3; clip: true
                            color: root.worldColor
                            border.width: 1; border.color: Qt.rgba(0.97, 0.97, 0.96, 0.12)
                            Image {
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
                objectName: "sagaCreators"
                title: "Creators"
                visible: root.creators.length > 0
                Row {
                    spacing: 20
                    Repeater {
                        model: root.creators
                        delegate: Column {
                            id: person
                            required property var modelData
                            width: 132
                            spacing: 2
                            Glass {
                                backdrop: wall
                                track: page.contentY
                                radius: 8
                                width: 132; height: 132
                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        const words = person.modelData.name.split(" ")
                                        return words[0].charAt(0) + words[words.length - 1].charAt(0)
                                    }
                                    color: theme.inkDimmer; font.family: root.displayFace; font.pixelSize: 34
                                }
                            }
                            Text { topPadding: 8; width: parent.width; text: person.modelData.name
                                   color: root.paper; font.family: root.uiFace
                                   font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { width: parent.width; text: person.modelData.role
                                   color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 12
                                   wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        }
                    }
                }
            }

            Section {
                objectName: "sagaInformation"
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
                               text: root.guide.blurb || ""
                               color: root.paper; font.family: root.uiFace; font.pixelSize: 15
                               lineHeight: 1.3; wrapMode: Text.WordWrap }
                        Text { visible: text.length > 0; text: root.guide.blurbSource || ""
                               color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11 }
                        Rectangle { visible: !!root.guide.blurb; width: parent.width; height: 1; color: root.hair }
                        Grid {
                            topPadding: 14
                            width: parent.width
                            columns: root.compact ? 1 : 3
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
                objectName: "sagaMore"
                title: "More universes"
                visible: root.siblings.length > 0
                Row {
                    spacing: 24
                    Repeater {
                        model: root.siblings
                        delegate: Rectangle {
                            id: sibling
                            required property var modelData
                            width: (aboutSection.width - 72) / 4; height: Math.round(width * 9 / 16)
                            radius: 3; clip: true
                            color: sibling.modelData.c1 || root.worldColor
                            border.width: siblingKey.interactionActive ? 2 : 1
                            border.color: siblingKey.interactionActive ? root.goldEdge : Qt.rgba(0.97, 0.97, 0.96, 0.12)
                            Image {
                                anchors.fill: parent; anchors.margins: 1
                                source: Qt.resolvedUrl(sibling.modelData.wallpaper)
                                asynchronous: true; cache: true
                                sourceSize.width: 640
                                fillMode: Image.PreserveAspectCrop
                            }
                            Rectangle {
                                anchors.fill: parent; anchors.margins: 1
                                gradient: Gradient {
                                    GradientStop { position: 0.35; color: "transparent" }
                                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.82) }
                                }
                            }
                            Column {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: 14
                                spacing: 3
                                Text { text: "SAGA"; color: theme.inkDim; font.family: root.uiFace
                                       font.pixelSize: 9; font.letterSpacing: 2 }
                                Text { width: parent.width; text: sibling.modelData.name
                                       color: root.paper; font.family: root.displayFace; font.pixelSize: 19
                                       elide: Text.ElideRight }
                            }
                            KeyboardAction {
                                id: siblingKey
                                anchors.fill: parent
                                focusRadius: 3
                                accessibleName: "Open the " + sibling.modelData.name + " universe"
                                onActiveFocusChanged: if (activeFocus) root.ensureVisible(sibling)
                                onTriggered: root.universeRequested(sibling.modelData.name)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 96 }
        }
    }

    ChromeScrim { z: 16 }

    // ===== the section switcher: rides the page under the hero, then docks beside Back =====
    Glass {
        id: tabBar
        objectName: "sagaTabs"
        z: 18
        backdrop: wall
        radius: 23
        readonly property bool docked: hero.height - page.contentY <= 22
        x: root.gutter + (docked ? 118 : 0)
        y: Math.max(22, hero.height - page.contentY)
        width: tabRow.width + 8; height: 46
        Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 220; easing.type: Easing.OutCubic } }
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
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: tab.modelData.title
                        color: tab.active ? "#090c13" : (tabKey.hovered ? root.paper : theme.inkDim)
                        font.family: root.uiFace; font.pixelSize: 13
                        font.weight: tab.active ? Font.DemiBold : Font.Medium
                    }
                    KeyboardAction {
                        id: tabKey
                        anchors.fill: parent
                        focusRadius: 18
                        accessibleName: tab.modelData.title
                        onTriggered: root.revealSection(tab.modelData.target)
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
        topPadding: 40
        spacing: 16
        Text { text: section.title; color: root.paper
               font.family: root.displayFace; font.pixelSize: 22 }
    }

    component SagaAction: Rectangle {
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

    // The same soft edge scrim and chevron treatment as TrendingTop10.
    component SagaChevron: Item {
        id: chevron
        property bool atRight: false
        signal triggered()
        width: 46; z: 5
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, chevron.atRight ? 0 : 0.65) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, chevron.atRight ? 0.65 : 0) }
            }
        }
        Text {
            anchors.centerIn: parent
            text: chevron.atRight ? "›" : "‹"
            color: edgeAction.hovered ? theme.gold : theme.ink
            font.family: root.displayFace; font.pixelSize: 42
        }
        // Pointer-only by design: the keyboard walks the grid itself and the grid scrolls to follow.
        KeyboardAction {
            id: edgeAction; anchors.fill: parent
            focusPolicy: Qt.NoFocus; focusEnabled: false
            accessibleName: chevron.atRight ? "Show later stories" : "Show earlier stories"
            onTriggered: chevron.triggered()
        }
    }

    // ---- one work. A book and a film are the same tile; a spanning show wears its backdrop ----
    component SagaTile: Rectangle {
        id: tile
        property var item: null
        property string form: "tile"       // tile | center | banner
        property bool ring: false          // the keyboard is here
        readonly property bool banner: tile.form === "banner"
        readonly property bool current: root.keyOf(root.shown) === root.keyOf(tile.item)
        readonly property real done: root.fraction(tile.item)
        signal hoveredIn()
        radius: 3
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
                source: tile.item ? ((tile.banner ? tile.item.art : tile.item.cover) || "") : ""
                asynchronous: true; cache: true
                sourceSize.width: tile.banner ? 1280 : 400
                fillMode: tile.banner ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                verticalAlignment: tile.banner ? Image.AlignTop : Image.AlignVCenter
                opacity: status === Image.Ready ? (tile.done >= 1 ? 0.72 : 1) : 0
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
            }
            // a spanning work is named by its own logo, over a scrim
            Rectangle {
                visible: tile.banner
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.4; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.78) }
                }
            }
            Image {
                id: logo
                visible: tile.banner && status === Image.Ready
                x: 18; height: 46; width: Math.min(parent.width * 0.46, 260)
                anchors.bottom: parent.bottom; anchors.bottomMargin: 18
                source: tile.banner && tile.item && tile.item.id
                        ? "https://live.metahub.space/logo/medium/" + tile.item.id + "/img" : ""
                asynchronous: true; cache: true
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft; verticalAlignment: Image.AlignBottom
            }
            Text {
                visible: tile.banner && logo.status !== Image.Ready && art.status === Image.Ready
                x: 18; anchors.bottom: parent.bottom; anchors.bottomMargin: 18
                text: tile.item ? tile.item.title : ""
                color: root.paper; font.family: root.displayFace; font.pixelSize: 26
            }
            Rectangle {   // read / watched
                visible: tile.done >= 1
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 6
                width: 22; height: 22; radius: 11
                color: Qt.rgba(0, 0, 0, 0.66); border.width: 1; border.color: theme.edge
                Text { anchors.centerIn: parent; text: "✓"; color: root.paper
                       font.family: root.uiFace; font.pixelSize: 12; font.weight: Font.Bold }
            }
            Rectangle {   // UPCOMING plate — future work stays, marked (ratified 2026-07-13)
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 6
                visible: !!tile.item && tile.item.upcoming === true
                radius: 4
                color: Qt.rgba(0, 0, 0, 0.72)
                border.width: 1; border.color: theme.edge
                width: upTag.implicitWidth + 12; height: upTag.implicitHeight + 6
                Text { id: upTag; anchors.centerIn: parent
                       text: "UPCOMING"; color: root.paper
                       font.family: root.uiFace; font.pixelSize: 9; font.letterSpacing: 2 }
            }
            Rectangle {   // where you are in it
                visible: tile.done > 0 && tile.done < 1
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 4; color: Qt.rgba(1, 1, 1, 0.2)
                Rectangle { width: parent.width * tile.done; height: parent.height; color: theme.gold }
            }
        }
        FocusRing { anchors.fill: parent; radius: tile.radius; shown: tile.ring }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.canOpen(tile.item) ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: { root.select(tile.item); tile.hoveredIn() }
            onClicked: root.openWork(tile.item)
        }
    }

    // ---- a labelled run of works with captions: branches and Upcoming. The tiles WRAP
    // inside the block's width (a twelve-film group is two lines, not an overflow) and the
    // shelf stays ONE keyboard stop: Left/Right walk the tiles in order, line breaks and all ----
    component Shelf: Column {
        id: shelf
        property string label: ""
        property var items: []
        readonly property real natural: Math.max(1, shelf.items.length) * root.tileW
                                       + Math.max(0, shelf.items.length - 1) * root.tileGap
        width: Math.min(natural, parent ? parent.width : natural)
        Item {
            width: parent.width; height: 28
            visible: shelf.label.length > 0
            Text { id: shelfLabel; anchors.verticalCenter: parent.verticalCenter; text: shelf.label
                   color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 10; font.letterSpacing: 2 }
            Rectangle { x: shelfLabel.width + 12; anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - x); height: 1; color: root.hair }
        }
        Item {
            width: parent.width
            height: shelfFlow.implicitHeight
            Flow {
                id: shelfFlow
                width: parent.width
                spacing: root.tileGap
                Repeater {
                    id: shelfRepeater
                    model: shelf.items
                    delegate: Column {
                        id: entry
                        required property var modelData
                        required property int index
                        width: root.tileW
                        SagaTile {
                            item: entry.modelData
                            width: root.tileW; height: root.tileH
                            ring: shelfFocus.activeFocus && shelfFocus.index === entry.index
                            onHoveredIn: shelfFocus.index = entry.index
                        }
                        Text { topPadding: 10; width: parent.width; text: entry.modelData.title
                               color: root.paper; font.family: root.uiFace; font.pixelSize: 12
                               lineHeight: 1.1; wrapMode: Text.WordWrap
                               maximumLineCount: 2; elide: Text.ElideRight }
                        Text { topPadding: 3; width: parent.width
                               text: entry.modelData.resolved === false ? "Details pending"
                                     : (entry.modelData.medium === "manga" ? ""
                                        : (root.isBook(entry.modelData) ? (entry.modelData.author || "")
                                           : (entry.modelData.year ? String(entry.modelData.year) : "")))
                               color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11
                               wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                    }
                }
            }
            // one keyboard stop per shelf: Left/Right move, Enter opens
            Item {
                id: shelfFocus
                objectName: "sagaShelfFocus"
                anchors.fill: parent
                property int index: 0
                focusPolicy: shelf.items.length > 0 && shelf.visible ? Qt.TabFocus : Qt.NoFocus
                onActiveFocusChanged: if (activeFocus) {
                    shelfFocus.index = Math.max(0, Math.min(shelf.items.length - 1, shelfFocus.index))
                    root.select(shelf.items[shelfFocus.index])
                    root.ensureVisible(shelf)
                }
                Keys.onPressed: (event) => {
                    const step = event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Left ? -1 : 0
                    if (step) {
                        const next = shelfFocus.index + step
                        if (next < 0 || next >= shelf.items.length) return
                        shelfFocus.index = next
                        root.select(shelf.items[next])
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.openWork(shelf.items[shelfFocus.index])
                        event.accepted = true
                    }
                }
                Accessible.role: Accessible.List
                Accessible.name: shelf.label
                Accessible.focusable: focusPolicy !== Qt.NoFocus
            }
        }
    }
}
