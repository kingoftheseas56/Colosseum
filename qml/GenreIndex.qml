// GenreIndex — the "Explore" genre directory for the Tankoban / manga lane. Recreates MyAnimeList's
// `manga.php` genre index in the house glass: the four grouped sections (Genres · Explicit Genres ·
// Themes · Demographics) as a cover mosaic — each genre its own art, name + count over it. Approved
// mock: mocks/genre-index.html. Data: GenreIndexApi.js (live counts from Jikan + baked covers). A tile
// emits genrePicked(name) → the host opens that genre's GenrePage.
//
// PROTOTYPE harness:  native\build-msvc\colosseum.exe qml\_indexcheck.qml
import QtQuick
import "GenreIndexApi.js" as Api

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors GenrePage / the world-page layers)
    property Item backdrop: null
    property bool includeExplicit: true          // locked in (the mature group stays, softened)
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal genrePicked(string name)              // a tile → host opens GenrePage(name)

    Theme { id: theme }

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
        Api.loadMangaGroups(root.includeExplicit, function(g) { if (g) root.groups = g; root.loading = false; });
    }
    Component.onCompleted: reload()

    // ---- the page's own wallpaper (it's a layer over the shell) ----
    Item {
        id: wall
        anchors.fill: parent
        Image { anchors.fill: parent; source: "../assets/wallpaper/captured-motion.jpg"
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
                        width: parent.width
                        columns: Math.max(3, Math.floor(width / 248))
                        columnSpacing: 14; rowSpacing: 14
                        readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

                        Repeater {
                            model: section.modelData.genres
                            delegate: Rectangle {
                                id: tile
                                required property var modelData
                                width: mosaic.cellW; height: 104; radius: 13; clip: true
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: tile.modelData.c1 || "#33445d" }
                                    GradientStop { position: 1; color: tile.modelData.c2 || "#0c1118" }
                                }
                                border.width: 1
                                border.color: tHov.hovered ? theme.gold : theme.edge
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

    // ---- fixed back / system controls (mirrors GenrePage) ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        Rectangle {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            width: 42; height: 34; radius: 17
            color: backMa.hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(0,0,0,0.40)
            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 22 }
            HoverHandler { id: backMa }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
}
