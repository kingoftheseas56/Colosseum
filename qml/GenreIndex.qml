// GenreIndex — the "Explore" genre directory for the Tankoban / manga lane. Recreates MyAnimeList's
// `manga.php` genre index in the house glass: the four grouped sections (Genres · Explicit Genres ·
// Themes · Demographics) as a cover mosaic — each genre its own art, name + count over it. Approved
// mock: mocks/genre-index.html. Data: GenreIndexApi.js (live counts from Jikan + baked covers). A tile
// emits genrePicked(name) → the host opens that genre's GenrePage.
//
// PROTOTYPE harness:  native\build-msvc\colosseum.exe qml\_indexcheck.qml
import QtQuick
import QtQuick.Controls
import "GenreIndexApi.js" as Api

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors GenrePage / the world-page layers)
    property Item backdrop: null
    property bool includeExplicit: true          // locked in (the mature group stays, softened)
    // Task 9: global Explicit Content preference (Main.qml binds it on this standalone layer).
    // Drives the "Explicit Genres" section's visibility — Erotica/Hentai tiles hide when the
    // preference is off. Ecchi/Mature stay in their ordinary sections (visible either way).
    property bool showExplicitContent: false
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal genrePicked(string name)              // a tile → host opens GenrePage(name)

    Theme { id: theme }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => { if (!event.accepted) pageScrollKeys.handle(event) }

    property var groups: []
    property bool loading: true
    property int totalGenres: {
        var t = 0;
        for (var i = 0; i < groups.length; i++) t += groups[i].genres.length;
        return t;
    }

    function fmtCount(n) {
        if (n >= 10000) return Math.round(n / 1000) + "k";
        if (n >= 1000)  return (n / 1000).toFixed(1) + "k";
        return "" + n;
    }
    function reload() {
        root.loading = true;
        // Task 9: the explicit section (Erotica/Hentai only) shows only when the host allows it
        // AND the user has opted in via the global preference. Every ordinary genre — including
        // Ecchi, Mature Readers, horror, violent work — stays visible in its own section either way.
        var showExplicitSection = root.includeExplicit && root.showExplicitContent;
        Api.loadMangaGroups(showExplicitSection, function(g) { if (g) root.groups = g; root.loading = false; });
    }
    Component.onCompleted: reload()
    onShowExplicitContentChanged: reload()

    // ---- the page's own wallpaper (it's a layer over the shell) ----
    Item {
        id: wall
        anchors.fill: parent
        // Live shell wallpaper (the user's pick), mirrored from the shell's wall item.
        // The bundled default only paints when no backdrop was injected (harness runs).
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.86) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 50
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header ----
            Text { text: "TANKOBAN · MANGA"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Explore Genres"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Text {
                topPadding: 14; textFormat: Text.StyledText
                font.family: theme.display; font.italic: true; font.pixelSize: 18
                color: theme.inkDim
                text: root.loading ? "Loading the directory…"
                      : "<b><font color='#f7f7f5'>" + root.totalGenres
                        + "</font></b> genres, four ways in — by genre, theme, and who they're for."
            }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }   // the gold accent mark

            // ---- grouped sections ----
            Repeater {
                model: root.groups
                delegate: Column {
                    id: section
                    required property var modelData
                    width: col.width
                    topPadding: 38
                    spacing: 16

                    // group header: name + count badge + subtitle
                    Row {
                        spacing: 12
                        Text { text: section.modelData.group; color: theme.ink
                               font.family: theme.display; font.pixelSize: 25; font.letterSpacing: -0.2
                               anchors.verticalCenter: parent.verticalCenter }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            height: 20; radius: 10; width: gcl.implicitWidth + 18
                            color: Qt.rgba(0.94,0.77,0.29,0.14); border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.4)
                            Text { id: gcl; anchors.centerIn: parent; text: section.modelData.genres.length
                                   color: theme.gold; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.Bold }
                        }
                        Text { text: Api.groupSub(section.modelData.group); color: theme.inkDimmer
                               font.family: theme.ui; font.pixelSize: 13; font.italic: true
                               anchors.verticalCenter: parent.verticalCenter }
                    }

                    // the cover mosaic for this group
                    Grid {
                        id: mosaic
                        property int currentIndex: section.modelData.genres.length > 0 ? 0 : -1
                        width: parent.width
                        columns: Math.max(3, Math.floor(width / 248))
                        focusPolicy: section.modelData.genres.length > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => mosaicKeys.handle(event)
                        KeyboardCollectionController {
                            id: mosaicKeys
                            view: mosaic
                            orientation: "grid"
                            columns: Math.max(1, mosaic.columns)
                            count: section.modelData.genres.length
                            pageStep: Math.max(1, mosaic.columns * 3)
                            positionIndexFn: function(index) {
                                const cell = tileRepeater.itemAt(index)
                                if (!cell) return
                                const p = cell.mapToItem(page.contentItem, 0, 0)
                                if (p.y < page.contentY + 56) page.contentY = Math.max(0, p.y - 56)
                                else if (p.y + cell.height > page.contentY + page.height - 24)
                                    page.contentY = Math.min(Math.max(0, page.contentHeight - page.height), p.y + cell.height - page.height + 24)
                            }
                            onActivated: (index) => root.genrePicked(section.modelData.genres[index].name)
                        }
                        columnSpacing: 14; rowSpacing: 14
                        readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

                        Repeater {
                            id: tileRepeater
                            model: section.modelData.genres
                            delegate: Rectangle {
                                id: tile
                                required property var modelData
                                required property int index
                                width: mosaic.cellW; height: 104; radius: 13; clip: true
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: tile.modelData.c1 || "#33445d" }
                                    GradientStop { position: 1; color: tile.modelData.c2 || "#0c1118" }
                                }
                                readonly property bool keyboardSelected: mosaic.activeFocus && mosaic.currentIndex === tile.index
                                border.width: keyboardSelected ? 2 : 1
                                border.color: tHov.hovered || keyboardSelected ? theme.gold : theme.edge
                                scale: tHov.hovered ? 1.025 : 1.0
                                Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

                                Image {
                                    anchors.fill: parent; source: tile.modelData.cover || ""
                                    fillMode: Image.PreserveAspectCrop; verticalAlignment: Image.AlignTop
                                    cache: true; asynchronous: true
                                    opacity: status === Image.Ready ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 220 } }
                                }
                                // legibility wash (darken the whole tile a touch + a stronger bottom for the name)
                                Rectangle { anchors.fill: parent; color: Qt.rgba(0,0,0,0.40) }
                                Rectangle {
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    height: 58
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: "transparent" }
                                        GradientStop { position: 1; color: Qt.rgba(0,0,0,0.72) }
                                    }
                                }
                                Text {
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    anchors.leftMargin: 13; anchors.rightMargin: 13; anchors.bottomMargin: 11
                                    text: tile.modelData.name; color: "#ffffff"
                                    font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                    style: Text.Raised; styleColor: Qt.rgba(0,0,0,0.6)
                                }
                                Text {
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 9
                                    text: root.fmtCount(tile.modelData.count)
                                    color: Qt.rgba(1,1,1,0.86)
                                    font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                                    style: Text.Raised; styleColor: Qt.rgba(0,0,0,0.7)
                                }
                                HoverHandler { id: tHov }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.genrePicked(tile.modelData.name)
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 40 }
        }
    }

    ScrollGlide { id: pageGlide; flick: page }
    KeyboardScrollController {
        id: pageScrollKeys; flick: page; glide: pageGlide; arrowScrolling: false
    }

    // ---- fixed back / system controls (mirrors GenrePage) ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        BackAction {
            variant: "capsule"; tip: "Back"
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            onTriggered: root.backRequested()
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Search"; focusRadius: 4; onTriggered: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Minimize"; focusRadius: 4; onTriggered: root.minimizeRequested() } }
            Image { source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Toggle fullscreen"; focusRadius: 4; onTriggered: root.fullscreenRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Close"; focusRadius: 4; onTriggered: root.closeRequested() } }
        }
    }
}
