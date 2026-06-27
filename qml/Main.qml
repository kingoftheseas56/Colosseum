// Colosseum — HOME (v1, on the proven spine)
// Fullscreen-exclusive frameless OS surface: persistent wallpaper + frosted-glass chrome.
//   Top bar (clock·pills·system) → Universe hero → unified Continue row → per-medium trending rows.
// Mock data only (no Universe data engine yet). Glass = proven material (see Glass.qml).
// Run:  C:/Qt/6.11.1/mingw_64/bin/qml.exe qml/Main.qml      (Esc / Ctrl+Q to quit)

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Catalog.js" as Catalog

Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#05060a"
    title: "Colosseum"

    // Esc: leave a world page if one is open, otherwise quit the shell. Ctrl+Q always quits.
    Shortcut { sequences: ["Escape"]; onActivated: worldStack.current !== "" ? win.closeWorld() : Qt.quit() }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: Qt.quit() }

    // Minimize the OS surface to the taskbar — "get it off my screen" WITHOUT quitting (the shell keeps
    // running, art stays warm). A frameless fullscreen window has no normal frame to land in, so when
    // Windows restores it from the taskbar we snap it straight back to fullscreen — never a stray bare
    // rectangle stuck with no titlebar to grab.
    function minimizeShell() { win.showMinimized() }
    onVisibilityChanged: if (win.visibility === Window.Windowed) win.visibility = Window.FullScreen

    // ---- navigation: open a medium's world page over the persistent wallpaper ----
    // Each visited mode keeps ONE live Loader (created on first entry, never destroyed); navigating
    // Home or between modes just toggles visibility. So returning to a mode shows the already-loaded
    // world with its covers INTACT instead of re-downloading them. Real mode pages route to their
    // own QML; unbuilt modes fall back to DemoWorld.qml.
    function worldSourceFor(medium) {
        if (medium === "Tankoban") return "TankobanWorld.qml"
        if (medium === "Theatre") return "TheatreWorld.qml"
        return "DemoWorld.qml"
    }
    function openWorld(medium) {
        var found = false
        for (var i = 0; i < openModes.count; i++)
            if (openModes.get(i).mode === medium) { found = true; break }
        if (!found) openModes.append({ mode: medium })   // first visit → create its keep-alive Loader
        worldStack.current = medium
        topbar.visible = false
        page.visible = false
    }
    function closeWorld() {
        worldStack.current = ""                           // hide all worlds; none destroyed
        topbar.visible = true
        page.visible = true
    }

    // ---- design tokens (the skin: glass is the constant; gold is sparing) ----
    Theme { id: theme }

    // =====================================================================
    // BACKDROP — the persistent wallpaper everything composites over.
    // =====================================================================
    Item {
        id: wall
        anchors.fill: parent
        // Real OS wallpaper — a placeholder PICK (Windows 11 "Captured Motion"; its translucent
        // glass-ribbon motif echoes our material, and it's dark enough for the glass to read).
        // Swap from the parked personalization gallery later. Glass composites over WHATEVER sits in
        // `wall`, so the Image "just works" — and it pops against the chrome instead of reading as an app.
        Image {
            anchors.fill: parent
            source: "../assets/wallpaper/captured-motion.jpg"
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        // gentle global vignette so chrome + text read against the wallpaper, bright or dark
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.34) }
                GradientStop { position: 0.5; color: Qt.rgba(0,0,0,0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.46) }
            }
        }
    }

    // ---- reusable: a clickable row header (the nav-in to a world). Continue isn't a world,
    //      so it opts out with navigable:false (no chevron, no click). ----
    component RowHeader: Item {
        id: rh
        property string title
        property bool navigable: true
        signal clicked()
        implicitWidth: rhRow.implicitWidth
        implicitHeight: rhRow.implicitHeight
        Row {
            id: rhRow
            spacing: 8
            Text {
                text: rh.title
                color: (rh.navigable && rhMa.containsMouse) ? theme.ink : theme.inkDim
                font.family: theme.display; font.pixelSize: 23
            }
            Text {
                text: "›"
                visible: rh.navigable
                color: theme.gold; font.pixelSize: 22
                opacity: rhMa.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }
        MouseArea {
            id: rhMa; anchors.fill: parent
            hoverEnabled: rh.navigable
            cursorShape: rh.navigable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (rh.navigable) rh.clicked()
        }
    }

    // ---- reusable: a unified Continue card (glass chrome, solid art slot) ----
    component ContinueCard: Glass {
        backdrop: wall
        width: 340; height: 148; radius: 14
        property string badge
        property string title
        property string sub
        property real progress: 0
        property color art: "#333"
        Row {
            anchors.fill: parent
            Rectangle {   // art slot (solid: banner/cover)
                width: 112; height: parent.height
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(art, 1.25) }
                    GradientStop { position: 1; color: Qt.darker(art, 1.4) }
                }
            }
            Item {
                width: parent.width - 112; height: parent.height
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 15; spacing: 0
                    Text {
                        text: badge; color: theme.gold
                        font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.3
                        Layout.alignment: Qt.AlignLeft
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: title; color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Text { text: sub; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; topPadding: 4; bottomPadding: 8 }
                    Rectangle {
                        Layout.fillWidth: true; height: 4; radius: 2; color: Qt.rgba(1,1,1,0.2)
                        Rectangle { width: parent.width * progress; height: parent.height; radius: 2; color: theme.gold }
                    }
                }
            }
        }
    }

    // (PortraitTile · Pill · SysIcon · the top bar now live in shared sibling files:
    //  PortraitTile.qml and TopBar.qml — reused by the world-page template.)

    // =====================================================================
    // FOREGROUND
    // =====================================================================

    // ---- 1. TOP BAR (fixed, glass over wallpaper) — shared shell chrome.
    //      activeMedium "" → HOME: no pill selected (the no-selection rule). Tapping a pill
    //      enters that world. ----
    TopBar {
        id: topbar
        backdrop: wall
        activeMedium: ""
        x: theme.margin; y: 30
        width: win.width - theme.margin * 2
        onMediumSelected: (medium) => win.openWorld(medium)
        onMinimizeClicked: win.minimizeShell()
        onPowerClicked: Qt.quit()
    }

    // ---- pinned top bar is above; everything below SCROLLS (vertical wheel/drag) ----
    Flickable {
        id: page
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: win.height - 96
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 40
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: theme.margin
            width: win.width - theme.margin * 2
            topPadding: 10
            spacing: 30

            // ---- 2. UNIVERSE HERO ----
            Glass {
                id: hero
                backdrop: wall
                track: page.contentY
                width: parent.width; height: 320; radius: 20
                tint: 0.06

                // placeholder "banner art" — color comes from artwork, pops against the glass
                Rectangle {
                    anchors.fill: parent; radius: hero.radius; clip: true
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.rgba(0.10,0.08,0.06,0.95) }
                        GradientStop { position: 0.5; color: Qt.rgba(0.16,0.12,0.08,0.55) }
                        GradientStop { position: 1; color: Qt.rgba(0.22,0.16,0.10,0.12) }
                    }
                    Text {
                        text: "DUNE"; color: Qt.rgba(1,0.92,0.75,0.08)
                        font.family: theme.display; font.pixelSize: 200; font.bold: true
                        anchors.right: parent.right; anchors.rightMargin: 40; anchors.top: parent.top; anchors.topMargin: -30
                    }
                }

                Column {
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 42
                    spacing: 12
                    Text { text: "UNIVERSE"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
                    Text { text: "Dune"; color: theme.ink; font.family: theme.display; font.pixelSize: 56 }
                    Text {
                        text: "Frank Herbert's world, end to end — novels, films, the graphic novel."
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; width: 460; wrapMode: Text.WordWrap
                    }
                    Row {
                        spacing: 10
                        Repeater {
                            model: [
                                { t: "6 Novels", ic: "books" },
                                { t: "2 Films", ic: "movies" },
                                { t: "Graphic Novel", ic: "comics" }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                radius: 999; height: 32
                                width: chipRow.implicitWidth + 26
                                color: Qt.rgba(1,1,1,0.10); border.width: 1; border.color: Qt.rgba(1,1,1,0.16)
                                Row {
                                    id: chipRow; anchors.centerIn: parent; spacing: 7
                                    Kirigami.Icon { width: 14; height: 14; isMask: true; color: theme.gold
                                        source: "../assets/icons/" + modelData.ic + ".svg"
                                        anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: modelData.t; color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                        anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                        }
                    }
                    Row {
                        spacing: 12; topPadding: 4
                        Rectangle {
                            radius: 11; height: 42; width: ctaP.implicitWidth + 36; color: theme.gold
                            Text { id: ctaP; anchors.centerIn: parent; text: "Continue — Part Two"; color: "#1a1408"
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                        }
                        Rectangle {
                            radius: 11; height: 42; width: ctaS.implicitWidth + 36
                            color: Qt.rgba(1,1,1,0.10); border.width: 1; border.color: Qt.rgba(1,1,1,0.18)
                            Text { id: ctaS; anchors.centerIn: parent; text: "Explore the universe"; color: theme.ink
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.Medium }
                        }
                    }
                }
                Row {
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 30
                    spacing: 8
                    Repeater {
                        model: 5
                        delegate: Rectangle {
                            required property int index
                            width: index === 0 ? 22 : 8; height: 8; radius: 4
                            color: index === 0 ? theme.gold : Qt.rgba(1,1,1,0.3)
                        }
                    }
                }
            }

            // ---- 3. CONTINUE (one unified row, all mediums mixed; scrolls horizontally) ----
            Column {
                width: parent.width
                spacing: 14
                RowHeader { title: "Continue"; navigable: false }   // unified resume row, not a world
                Flickable {
                    id: contFlick
                    width: parent.width; height: 148
                    contentWidth: contRow.width; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: contRow
                        spacing: 18
                        ContinueCard { track: page.contentY + contFlick.contentX; badge: "VIDEO"; title: "Dune: Part Two"; sub: "1h 42m left"; progress: 0.62; art: "#2c4256" }
                        ContinueCard { track: page.contentY + contFlick.contentX; badge: "MANGA"; title: "One Piece"; sub: "Ch. 1090"; progress: 0.45; art: "#532f49" }
                        ContinueCard { track: page.contentY + contFlick.contentX; badge: "BOOK";  title: "Dune"; sub: "Frank Herbert · 34%"; progress: 0.34; art: "#5a4a28" }
                        ContinueCard { track: page.contentY + contFlick.contentX; badge: "COMIC"; title: "Invincible"; sub: "Issue #12"; progress: 0.70; art: "#6a4a32" }
                    }
                }
            }

            // ---- 4. MODE-INTRO WIDGETS — the board that introduces each app AND shows what's inside.
            //      First prototype: Tankoban as a BOOKSHELF (manga covers standing on a shelf ledge).
            //      The other modes get their own widget forms next; this is the shape to react to.
            Bookshelf {
                backdrop: wall
                track: page.contentY
                width: parent.width
                mangaBooks: Catalog.topManga
                comicsBooks: Catalog.topComics
                onClicked: win.openWorld("Tankoban")
                onBookClicked: win.openWorld("Tankoban")
            }

            TheatreMarquee {
                backdrop: wall
                track: page.contentY
                width: parent.width
                featured: Catalog.theatreFeatured
                continueItems: Catalog.theatreContinue
                onClicked: win.openWorld("Theatre")
                onMovieClicked: win.openWorld("Theatre")
            }

            Item { width: 1; height: 16 }   // bottom breathing room
        }
    }

    // ---- world pages: one keep-alive Loader PER visited mode, stacked over the home on the SAME
    //      wallpaper. worldStack.current picks which is visible; "" = home. Kept alive so covers
    //      don't re-fetch on return (the home's top bar + scroll hide while a world is up). ----
    ListModel { id: openModes }
    Item {
        id: worldStack
        anchors.fill: parent
        property string current: ""                      // "" = home; else the visible mode
        Repeater {
            model: openModes
            delegate: Loader {
                required property string mode
                anchors.fill: parent
                visible: worldStack.current === mode
                active: true
                source: win.worldSourceFor(mode)
                onLoaded: {
                    item.medium = mode
                    item.backdrop = wall
                    item.homeRequested.connect(win.closeWorld)
                    item.mediumSelected.connect(win.openWorld)
                    item.minimizeClicked.connect(win.minimizeShell)
                    item.powerClicked.connect(function() { Qt.quit() })
                }
            }
        }
    }

    // ---- OS-style boot loader: prefetch covers, then fade away to reveal the shell with art warm ----
    BootSplash {
        id: boot
        anchors.fill: parent
        z: 1000
        onFinished: bootFade.start()
        NumberAnimation { id: bootFade; target: boot; property: "opacity"; to: 0; duration: 400
            onFinished: boot.visible = false }
    }
}
