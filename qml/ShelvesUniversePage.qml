// Shelves universe (Arc 58): the SHELVES centre of the universe page — the anime
// universes' second form, after Hemanth's ruling on the story grid in the app: "the
// structure is fundamentally flawed in terms of navigation, division of mediums and
// overall flow". The page divides by MEDIUM: one shelf per medium (Manga, Anime, Films),
// each a single row of same-size tiles that scrolls sideways, and the tabs name the
// mediums. Labelled runs inside a row carry the split (the series vs the spin-offs, a
// film era). Since 2026-10-04 (evening) the rows wear the GLASS the book sagas' pages
// already have: every medium sits in a house glass panel over the universe's own
// wallpaper, the row keeps to its panel, and the wallpaper no longer dims behind scroll.
// Since 2026-10-05 a panel is only as wide as its row needs, and consecutive short panels
// share a line (root.panelLayout) — a three-tile shelf no longer drags a full-width slab.
// No grid, no side panel, no boxes.
// Layout authority: Brotherhood/agents/colosseum-universe-shelves-mock.html (plain) and
// colosseum-universe-shelves-glass-mock.html (the ruled-in glass form), 1280x720.
import QtQuick
import QtQuick.Controls
import "SagaApi.js" as Saga
import "Universes.js" as UDB

Item {
    id: root
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
    signal seriesRequested(string title)     // manga tile / Read → Tankoban, by its curated query
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
    readonly property var guide: UDB.configFor(root.universeName || root.uni.name)
    readonly property color worldColor: root.uni.color || root.guide.c1 || "#221c30"
    readonly property color paper: theme.ink
    readonly property color hair: Qt.rgba(1, 1, 1, 0.09)
    readonly property color goldEdge: Qt.rgba(0.94, 0.77, 0.29, 0.7)
    readonly property real gutter: width < 700 ? 24 : 64
    readonly property bool compact: width < 800
    property int loadGeneration: 0

    property var uni: ({ name: "", blurb: "", blurbSource: "", banner: "", wallpaper: "",
                         color: "", manga: [], shows: [], filmEras: [] })

    // ---- the mediums, each with its runs (labelled stretches inside the one row) ----
    readonly property var filmsFlat: {
        const out = []
        ;(root.uni.filmEras || []).forEach(function(g) { (g.films || []).forEach(function(f) { out.push(f) }) })
        return out
    }
    readonly property var works: (root.uni.manga || []).concat(root.uni.shows || [], root.filmsFlat)
    readonly property int nManga: (root.uni.manga || []).length
    readonly property int nAnime: (root.uni.shows || []).length
    readonly property int nFilms: root.filmsFlat.length
    readonly property var mangaRuns: [
        { label: "The series", items: (root.uni.manga || []).filter(function(m) { return m.main === true }) },
        { label: "Spin-offs",  items: (root.uni.manga || []).filter(function(m) { return m.main !== true }) } ]
    readonly property var animeRuns: [ { label: "", items: root.uni.shows || [] } ]
    readonly property var filmRuns: (root.uni.filmEras || []).map(function(g) {
        return { label: g.era, items: g.films || [] }
    })

    // ---- identity: loader pushes rebuild every item, so selection is held by key ----
    function keyOf(item) {
        if (!item) return ""
        return item.medium !== undefined && item.canonIndex !== undefined
                ? item.medium + ":" + item.canonIndex : "x:" + (item.id || item.title || "")
    }
    function isManga(item) { return !!item && item.medium === "manga" }
    function isSeries(item) { return !!item && (item.medium ? item.medium === "series" : item.type === "series") }
    function kindLabel(item) { return root.isManga(item) ? "MANGA" : (root.isSeries(item) ? "ANIME" : "FILM") }
    function canOpen(item) { return !!item && item.resolved !== false }
    function openWork(item) {
        if (!root.canOpen(item)) return
        if (root.isManga(item)) root.seriesRequested(item.query || item.title)
        else root.watchRequested(item)
    }

    // ---- progress: gold marks this and focus, nothing else. Tankoban keys a manga by
    //      its MAL id ("mal:<id>"), which the newer pins carry; a manga without the pin
    //      stays honestly unmatched rather than guessed by name ----
    property int progressRevision: typeof Progress !== "undefined" ? Progress.revision : 0
    // preview/test seam: [{ id, kind, title, sub, cover, progress, watched }]
    property var previewProgress: null
    readonly property var recentEntries: {
        root.progressRevision
        if (root.previewProgress) return root.previewProgress
        return typeof Progress !== "undefined" ? Progress.recent("", 200) : []
    }
    function entryMatches(entry, item) {
        if (!entry || !root.canOpen(item)) return false
        const kind = String(entry.kind || ""), id = String(entry.id || "")
        if (root.isManga(item)) return kind === "manga" && item.mal && id === "mal:" + item.mal
        return kind === "video" && item.id !== undefined && id.split(":")[0] === String(item.id)
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
    function resumeOf(wantManga) {
        const list = root.recentEntries
        for (let i = 0; i < list.length; ++i)
            for (let w = 0; w < root.works.length; ++w)
                if (root.isManga(root.works[w]) === wantManga && root.entryMatches(list[i], root.works[w])
                        && list[i].watched !== true)
                    return { item: root.works[w], entry: list[i] }
        return null
    }
    function firstOf(wantManga) {
        for (let w = 0; w < root.works.length; ++w)
            if (root.isManga(root.works[w]) === wantManga && root.canOpen(root.works[w])
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
        if (root.nManga > 0) out.push({ title: "Manga", count: root.nManga, target: mangaSection })
        if (root.nAnime > 0) out.push({ title: "Anime", count: root.nAnime, target: animeSection })
        if (root.nFilms > 0) out.push({ title: "Films", count: root.nFilms, target: filmsSection })
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
    readonly property var panelLayout: {
        const sections = [mangaSection, animeSection, filmsSection]
            .filter(function(s) { return s.visible && s.total > 0 })
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
            let tallest = 0, x = 0
            for (let j = 0; j < lines[k].items.length; ++j)
                tallest = Math.max(tallest, lines[k].items[j].naturalHeight)
            for (let j = 0; j < lines[k].items.length; ++j) {
                const s = lines[k].items[j]
                placed[s.key] = { x: x, y: y, width: s.naturalWidth, height: tallest }
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
        if (!root.universeName.length) return         // never load a default universe (the OP-flash lesson)
        root.selectedKey = ""
        page.contentY = 0
        const generation = ++root.loadGeneration
        Saga.loadShelves(root.universeName, function(u) {
            if (generation === root.loadGeneration && u) root.uni = u
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
                        text: "ANIME"
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
                        visible: root.nManga + root.nAnime + root.nFilms > 0
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
                            enabled: !!root.readTarget
                            onTriggered: root.openWork(root.readTarget)
                        }
                        ShelfAction {
                            objectName: "shelvesWatchButton"
                            text: "Watch"; primary: true
                            sub: root.resumeWatch && root.resumeWatch.entry ? String(root.resumeWatch.entry.sub || "") : ""
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

            // ===== THE SHELVES: one row per medium, divided by runs. The panels are
            // placed by root.panelLayout — a short row's panel hugs its row, and
            // consecutive short panels share a line =====
            Item {
                id: mediums
                width: col.width
                height: root.panelLayout.totalHeight
                MediumSection { id: mangaSection; objectName: "shelvesManga"; title: "Manga"; key: "manga"; runs: root.mangaRuns }
                MediumSection { id: animeSection; objectName: "shelvesAnime"; title: "Anime"; key: "anime"; runs: root.animeRuns }
                MediumSection { id: filmsSection; objectName: "shelvesFilms"; title: "Films"; key: "films"; runs: root.filmRuns }
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
                        model: root.stills
                        delegate: Rectangle {
                            id: still
                            required property var modelData
                            width: (gallerySection.width - 72) / 4; height: Math.round(width * 9 / 16)
                            radius: 8; clip: true
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
                        model: root.siblings
                        delegate: Rectangle {
                            id: sibling
                            required property var modelData
                            width: (moreSection.width - 72) / 4; height: Math.round(width * 9 / 16)
                            radius: 12; clip: true
                            color: sibling.modelData.c1 || root.worldColor
                            border.width: siblingKey.interactionActive ? 2 : 1
                            border.color: siblingKey.interactionActive ? root.goldEdge : Qt.rgba(0.97, 0.97, 0.96, 0.12)
                            Image {
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
                                Text { text: "ANIME"; color: theme.inkDim; font.family: root.uiFace
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
    readonly property var facts: {
        const out = []
        if (root.nManga > 0) out.push(root.nManga + " Manga")
        if (root.nAnime > 0) out.push(root.nAnime + " Anime")
        if (root.nFilms > 0) out.push(root.nFilms + " Films")
        return out
    }
    readonly property var information: {
        const out = []
        if (root.nManga > 0) out.push(["MANGA", String(root.nManga)])
        if (root.nAnime > 0) out.push(["ANIME", root.nAnime + " series"])
        if (root.nFilms > 0) {
            const years = root.filmsFlat.map(function(f) { return f.year })
                             .filter(function(y) { return y > 0 }).sort(function(a, b) { return a - b })
            out.push(["FILMS", root.nFilms + (years.length > 1 && years[0] !== years[years.length - 1]
                                               ? " · " + years[0] + " to " + years[years.length - 1] : "")])
        }
        out.push(["CATALOGUE", "Curation · Cinemeta"])
        return out
    }
    readonly property var stills: root.works.filter(function(item) {
        return !root.isManga(item) && root.canOpen(item) && !!item.art
    }).slice(0, 4)
    // the other anime universes with a working page
    readonly property var siblings: {
        const all = UDB.universes.concat(UDB.archive)
        return all.filter(function(u) { return !!u.manga && u.name !== (root.universeName || root.uni.name) })
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
        property string key
        property var runs: []
        property int total: {
            let n = 0
            for (let i = 0; i < medium.runs.length; ++i) n += (medium.runs[i].items || []).length
            return n
        }
        readonly property var liveRuns: medium.runs.filter(function(r) { return (r.items || []).length > 0 })
        readonly property bool labelled: medium.liveRuns.length > 1
        readonly property var flatWorks: {
            let out = []
            for (let i = 0; i < medium.liveRuns.length; ++i)
                out = out.concat(medium.liveRuns[i].items)
            return out
        }
        // the row's natural width from the data: tiles + tile gaps + the seams between runs
        readonly property real rowWidth: {
            let w = 0
            for (let i = 0; i < medium.liveRuns.length; ++i) {
                const n = (medium.liveRuns[i].items || []).length
                if (i > 0) w += 28
                w += n * root.tileW + (n - 1) * root.tileGap
            }
            return w
        }
        // never narrower than its heading needs
        readonly property real headingWidth: headingRow.implicitWidth
        // the panel this section asks for, before line packing clamps it to the page
        readonly property real naturalWidth: Math.min(root.contentWidth,
                Math.max(root.rowInset * 2 + medium.rowWidth, root.rowInset * 2 + medium.headingWidth))
        // 20 top + 30 heading + 14 gap + the row + 22 bottom (the Glass's own rhythm)
        readonly property real naturalHeight: 64 + runsRow.height + 6 + 22
        // where panelLayout put this section (its own slot until the layout has run)
        readonly property var slot: root.panelLayout.placed[medium.key]
                || { x: 0, y: 0, width: root.contentWidth, height: medium.naturalHeight }
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
                width: parent.width - 24; height: 30
                Row {
                    id: headingRow
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
                    x: 24; y: 64
                    width: parent.width - 24
                    height: runsRow.height + 6

                Flickable {
                    id: shelfFlick
                    objectName: "shelvesRow_" + medium.key
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
                                    for (let i = 0; i < run.index; ++i) base += medium.liveRuns[i].items.length
                                    return base
                                }
                                // the keyboard walker's seams into this run's works
                                readonly property int workCount: (run.modelData.items || []).length
                                function workAt(i) { return workRepeater.itemAt(i) }
                                spacing: 0
                                // the run's label rides above its tiles, hairline after
                                Item {
                                    visible: medium.labelled
                                    width: runTiles.width; height: 26
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
                                        model: run.modelData.items
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
                    y: 3 + (medium.labelled ? 26 : 0)
                    height: root.tileH
                    visible: shelfFlick.interactive && shelfFlick.contentX > 8
                    onTriggered: shelfFlick.pageBy(-1)
                }
                ShelfChevron {
                    objectName: "shelvesNext_" + medium.key
                    label: medium.title
                    atRight: true
                    y: 3 + (medium.labelled ? 26 : 0)
                    height: root.tileH
                    visible: shelfFlick.interactive && shelfFlick.contentX + shelfFlick.width < shelfFlick.contentWidth - 8
                    onTriggered: shelfFlick.pageBy(1)
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
        property bool current: false            // the pointer is on it (gold edge)
        property bool keyboardSelected: false   // the row's keyboard selection (house ring)
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
