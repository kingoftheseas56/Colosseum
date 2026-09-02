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

    // NOTE for the routing task: this page has NO `backdrop` property, unlike the seven
    // bespoke universe pages. Those float over the shared wallpaper via
    // `ShaderEffectSource { sourceItem: root.backdrop }`; this one paints the mock's flat
    // #0c0e11 and never samples a wallpaper, so a `backdrop` property here would be a
    // contract the shell satisfies for nothing. Do NOT add `item.backdrop = wall` when
    // wiring the route — it would throw on a non-existent property.
    property string extensionId: ""
    property string universeName: ""

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)     // video → Theatre
    signal seriesRequested(var entry)      // manga → Tankoban (entry carries provider/id for a sourced series)
    signal bookRequested(var payload)      // book → Biblio
    signal comicsArchiveRequested(var payload)

    Theme { id: theme }

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

    property var payload: null
    // No `Component.onCompleted: reload()`. Setting `extensionId` at construction — the only
    // way this page is ever opened — already fires onExtensionIdChanged, so the pair issued
    // TWO loads per open. Harmless while the payload is a cached local file; two in-flight
    // requests per open once §5.5 serves it over HTTPS.
    onExtensionIdChanged: root.reload()
    Component.onCompleted: root.forceActiveFocus(Qt.TabFocusReason)
    function reload() {
        // A shorter universe must not open mid-page: contentY survives the swap otherwise.
        page.contentY = 0
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
            root.seriesRequested(entry)          // entry: a weebcentral source routes to its own series
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

            // ---- nothing arrived ----
            // This feature has shipped blank three times. A universe that cannot load says so,
            // rather than looking identical to one that is merely still loading. One condition
            // covers all three silent cases, because each ends at payload === null: no reader
            // installed yet, an extensionId with no bundled file, and a payload that validates
            // down to zero sections. A Column skips invisible children, so this costs no space
            // once a payload does arrive.
            Text {
                visible: root.payload === null && root.extensionId !== ""
                x: theme.margin
                text: "This universe isn't installed yet."
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 14
            }

            // ---- the served sections ----
            Repeater {
                model: root.payload ? root.payload.sections : []
                delegate: Column {
                    id: section
                    required property var modelData
                    x: theme.margin
                    width: page.width - theme.margin * 2
                    spacing: 0

                    // ---- the two bounds on how many tiles this page can hold at once ----
                    // The rails grow on TWO axes and only one of them is a rail's own model.
                    //
                    // itemLimit is the house cap (qml/PosterRail.qml:13-20). 60, not
                    // PosterRail's 20: the largest curated section today is 17 (One Piece
                    // Specials) and this page has no "See all" door, so a cap that could bite
                    // a real universe would hide works — the same lie validate() refuses when
                    // it drops empty rows. 60 is a safety valve against a malformed payload
                    // (contentWidth, the model copy), not a display policy.
                    readonly property int itemLimit: 60
                    //
                    // nearFold is the bound that actually holds resident textures down, and
                    // it is the measured one. A 20-section payload built 220 live tiles
                    // (~122 MB of decoded poster) on this page: the outer Column + Repeater
                    // has no vertical virtualization, so EVERY rail builds its ~11 visible
                    // delegates even a full screen below the fold. itemLimit alone did not
                    // move that number at all — rail model 17, live 11 either way. Gating the
                    // model on proximity to the fold is what does. Rail height is a constant
                    // 302 whatever the model holds, so section.y can never depend on this
                    // gate: no binding loop.
                    readonly property bool nearFold:
                        section.y < page.contentY + page.height * 2
                        && section.y + section.height > page.contentY - page.height * 0.5
                    readonly property var railModel: {
                        if (!section.nearFold) return []
                        var src = section.modelData.entries, out = []
                        var n = Math.min(src.length, section.itemLimit)
                        for (var i = 0; i < n; i++) out.push(src[i])
                        return out
                    }

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
                        id: sectionRail
                        width: section.width
                        focusPolicy: count > 0 ? Qt.TabFocus : Qt.NoFocus
                        currentIndex: count > 0 ? 0 : -1
                        Keys.onPressed: (event) => sectionRailNav.handle(event)
                        // the tile's own 236 art + 56 caption, plus the mock's 10px gutter
                        // under the row (`.row { padding-bottom: 10 }`). The mock's 246 is a
                        // min-height FLOOR, not the tile height — the tile is 292.
                        height: 236 + 56 + 10
                        orientation: ListView.Horizontal
                        spacing: 22
                        clip: true
                        model: section.railModel
                        boundsBehavior: Flickable.StopAtBounds
                        reuseItems: true
                        // NO wheel hijack. The mock's row is `overflow-x: auto`, where the
                        // vertical wheel scrolls the PAGE — and the house rail
                        // (SagaUniversePage's AdaptationRow) does the same. Rows fill
                        // nearly the whole viewport, so a row that ate the
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
                            focusManagedByCollection: true
                            keyboardSelected: sectionRail.activeFocus && sectionRail.currentIndex === index
                            onActivated: {
                                sectionRail.currentIndex = index
                                root.openEntry(section.modelData.kind, modelData)
                            }
                        }
                        KeyboardCollectionController {
                            id: sectionRailNav
                            view: sectionRail
                            count: sectionRail.count
                            orientation: "horizontal"
                            onActivated: (index) => {
                                if (index >= 0 && index < section.railModel.length)
                                    root.openEntry(section.modelData.kind, section.railModel[index])
                            }
                        }
                    }
                    Item { width: 1; height: 40 }
                }
            }
            Item { width: 1; height: 60 }
        }
    }

    // ---- the house page chrome, reproduced from SagaUniversePage.qml:348-394 ----
    // The mock draws only a back arrow, but the mock is a page-BODY mock and depicts no
    // window chrome at all — that is not a conflict with the house block. This page fills
    // the window, so without this row there is no way to minimise, unmaximise or close the
    // app while standing on it; all seven bespoke universe pages carry it, which is why the
    // three signals were declared in the first place. ChromeScrim belongs to the same block:
    // light glyphs over a bright banner need the top scrim to stay legible.
    ChromeScrim { z: 16 }
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
}
