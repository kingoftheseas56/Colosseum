// UniverseExtensionPage — the ONE renderer for every universe extension, forever.
//
// Layout: agents/colosseum-universe-onepiece-rows-mock.html (rev 2). Values inherited from
// SagaUniversePage via the One Piece plan §3.0 — band 360, margin 54, name Fraunces 62,
// kicker letterSpacing 4, section title Fraunces 25, tile 150x236, gap 22, spacer 40.
//
// SECTIONS ARE DATA. Order, titles and contents come from the payload; the page renders
// whatever arrives and never re-sorts (spec §5.2). No hero, no canon tags, no chronology —
// One Piece has no inherent timeline, and a timeline belongs only to IPs that have one.
// Continue is the mock's one personal row and is NOT built here: it needs per-medium
// progress matching and is its own task. An empty faked Continue would be a lie.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "UniverseExtApi.js" as UniverseApi

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null          // shell contract (mirrors SagaUniversePage); the mock's
                                          // page sits on flat #0c0e11, so nothing samples it here
    property string extensionId: ""
    property string universeName: ""

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)     // video → Theatre
    signal seriesRequested(string name)    // manga → Tankoban
    signal bookRequested(var payload)      // book → Biblio
    signal comicsArchiveRequested(var payload)

    Theme { id: theme }

    property var payload: null
    onExtensionIdChanged: root.reload()
    Component.onCompleted: root.reload()
    function reload() {
        if (!root.extensionId) { root.payload = null; return }
        UniverseApi.load(root.extensionId, function (p) { root.payload = p })
    }

    readonly property string banner: (root.payload && root.payload.background) || ""

    // "54 works · tv shows, movies, specials, manga, novels" — derived from what actually
    // arrived, never written down. A payload that gains a section gains a metaline word.
    readonly property string metaline: {
        if (!root.payload || !root.payload.sections.length) return ""
        var n = 0, names = []
        for (var i = 0; i < root.payload.sections.length; i++) {
            n += root.payload.sections[i].entries.length
            names.push(root.payload.sections[i].title.toLowerCase())
        }
        return n + " works · " + names.join(", ")
    }

    // A tile's destination is decided by its SECTION kind, never guessed from the entry.
    function openEntry(kind, entry) {
        if (kind === "video")
            root.watchRequested({ id: entry.id, type: entry.type,
                                  title: entry.title, cover: "" })
        else if (kind === "manga")
            root.seriesRequested(entry.title)
        else if (kind === "book")
            root.bookRequested({ id: entry.id, title: entry.title })
        else if (kind === "comic")
            root.comicsArchiveRequested({ title: entry.title, posts: entry.posts,
                                          year: entry.year })
    }

    Rectangle { anchors.fill: parent; color: "#0c0e11" }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: col
            width: page.width
            spacing: 0

            // ---- header band: 360, banner at low opacity behind a left-heavy wash ----
            Item {
                width: parent.width
                height: 360
                clip: true
                Image {
                    anchors.fill: parent
                    source: root.banner
                    visible: root.banner.length > 0
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    opacity: 0.30
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.04; color: "#0c0e11" }
                        GradientStop { position: 0.55; color: Qt.rgba(0.047, 0.055, 0.067, 0.55) }
                        GradientStop { position: 1.0;  color: Qt.rgba(0.047, 0.055, 0.067, 0.90) }
                    }
                }
                Column {
                    x: theme.margin
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 46
                    spacing: 9
                    Text {
                        text: "UNIVERSE"
                        color: theme.gold
                        font.family: theme.ui; font.pixelSize: 12
                        font.letterSpacing: 4; font.bold: true
                    }
                    Text {
                        text: (root.payload && root.payload.title) || root.universeName
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 62
                    }
                    Text {
                        text: root.metaline
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // the mock's `main { padding-top: 44 }` — ONCE, above the first section. Each
            // section carries its own 40 tail below, so consecutive sections sit 40 apart
            // (`.sec { margin-bottom: 40 }`), not 84.
            Item { width: 1; height: 44 }

            // ---- the served sections ----
            Repeater {
                model: root.payload ? root.payload.sections : []
                delegate: Column {
                    id: section
                    required property var modelData
                    x: theme.margin
                    width: page.width - theme.margin * 2
                    spacing: 0

                    Text {
                        text: section.modelData.title
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 25
                        bottomPadding: 12
                    }
                    Text {
                        text: section.modelData.entries.length
                              + (section.modelData.entries.length === 1 ? " work" : " works")
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13
                        bottomPadding: 16
                    }
                    Rectangle {
                        width: section.width; height: 3; radius: 1.5
                        opacity: 0.5
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0;  color: theme.gold }
                            GradientStop { position: 0.42; color: "transparent" }
                        }
                    }
                    Item { width: 1; height: 18 }

                    ListView {
                        id: rail
                        width: section.width
                        // the tile's own 236 art + 56 caption, plus the mock's 10px gutter
                        // under the row (`.row { padding-bottom: 10 }`). The mock's 246 is a
                        // min-height FLOOR, not the tile height — the tile is 292.
                        height: 236 + 56 + 10
                        orientation: ListView.Horizontal
                        spacing: 22
                        clip: true
                        model: section.modelData.entries
                        boundsBehavior: Flickable.StopAtBounds
                        reuseItems: true
                        // NO wheel hijack. The mock's row is `overflow-x: auto`, where the
                        // vertical wheel scrolls the PAGE — and both house rails
                        // (SagaUniversePage's AdaptationRow, OnePieceUniversePage) do the
                        // same. Rows fill nearly the whole viewport, so a row that ate the
                        // vertical wheel would leave the page unscrollable by wheel.
                        // Measured: Qt lets the vertical wheel through to the page, and
                        // scrolls the rail on horizontal delta, with no handler at all.

                        // `order: index`, NOT `index: index` — a delegate that re-declares
                        // `index` shadows the tile's own property and every tile silently
                        // renders "1", with no error and no warning. Measured.
                        delegate: UniverseTile {
                            required property var modelData
                            required property int index
                            entry: modelData
                            kind: section.modelData.kind
                            order: index
                            onActivated: root.openEntry(section.modelData.kind, modelData)
                        }
                    }
                    Item { width: 1; height: 40 }
                }
            }
            Item { width: 1; height: 60 }
        }
    }

    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: root.backRequested()
    }
}
