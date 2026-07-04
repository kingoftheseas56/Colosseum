// ComicGenrePage — the western-comics genre page (Tankoban mode). A genre tile on the
// world's comics mosaic opens this: a poster grid of the genre's starter shelf
// (Catalog.comicGenreSeries — curated v1, same editorial status as the mosaic itself).
// Every title is resolved LIVE against GetComics at open (ranked tag search), so a
// title with no releases there simply never appears — nothing curated can rot into a
// dead link. Posters come from iTunes (session-cached). Clicking a tile opens the
// ComicSeries shelf. NOT the manga GenrePage (that file is AniList's and contract-locked).

import QtQuick
import "Catalog.js" as Catalog
import "ComicsApi.js" as Api

Item {
    id: page
    property Item backdrop
    property string genreName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal westernPicked(var data)     // { western: true, tag, tagId, title } → host opens ComicSeries

    // --- resolved tiles: [{title, tag, tagId, count, poster}] ---
    property var tiles: []
    property bool loading: true
    property int _gen: 0               // stale-reply guard across genre switches

    Theme { id: theme }

    onGenreNameChanged: resolve()
    Component.onCompleted: if (genreName.length) resolve()

    function resolve() {
        loading = true
        tiles = []
        _gen += 1
        var gen = _gen
        var titles = (Catalog.comicGenreSeries[genreName] || [])
        if (!titles.length) { loading = false; return }
        var out = [], pending = titles.length
        titles.forEach(function(t) {
            Api.searchSeries(t, function(tags) {
                if (gen !== page._gen) return                    // genre changed mid-flight
                if (tags && tags.length) {
                    var best = tags[0]
                    out.push({ title: best.title, tag: best.tag, tagId: best.tagId,
                               count: best.count, poster: "" })
                }
                pending -= 1
                if (pending === 0) {
                    page.tiles = out
                    page.loading = false
                    // posters trail in (cached per session; iTunes rate-limits bursts)
                    out.forEach(function(tile, idx) {
                        Api.posterFor(tile.title + " comic", function(art) {
                            if (gen !== page._gen || !art.length) return
                            var copy = page.tiles.slice()
                            if (copy[idx]) { copy[idx].poster = art; page.tiles = copy }
                        })
                    })
                }
            })
        })
    }

    // ===================== visual tree =====================
    MouseArea { anchors.fill: parent }                      // absorb clicks from the world below

    Rectangle { anchors.fill: parent; color: "#07080c" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.06, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.9) }
        }
    }

    ChromeScrim { z: 16 }

    // ---- ‹ Back ----
    Item {
        x: theme.margin; y: 28; width: backRow.implicitWidth + 16; height: 34; z: 20
        Row {
            id: backRow; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 26; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } } }
        }
        MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: page.backRequested() }
    }

    // ---- window controls (minimize / power) ----
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested() }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: col
            width: flick.width
            spacing: 0

            Item { width: 1; height: 96 }

            // header — INLINE metadata (bright count, dim medium)
            Column {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                spacing: 10
                Text {
                    text: "Western Comics · Tankoban"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                    font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                }
                Text {
                    text: page.genreName
                    color: theme.ink; font.family: theme.display; font.pixelSize: 48
                    font.weight: Font.DemiBold
                }
                Row {
                    spacing: 11
                    Text { text: page.tiles.length
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "series on the shelf"; color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "GetComics"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter }
                }
            }

            Item { width: 1; height: 26 }

            // the poster grid
            Grid {
                id: grid
                x: theme.margin
                width: parent.width - 2 * theme.margin
                columns: 6
                columnSpacing: 18; rowSpacing: 24
                readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

                Repeater {
                    model: page.tiles
                    delegate: Column {
                        id: tile
                        required property var modelData
                        width: grid.cellW
                        spacing: 8
                        Item {
                            width: parent.width; height: width * 1.5
                            Rectangle {
                                anchors.fill: parent; radius: 10
                                color: "#161821"
                                border.width: 1
                                border.color: tileMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : Qt.rgba(1, 1, 1, 0.1)
                                Text {
                                    anchors.centerIn: parent
                                    visible: art.status !== Image.Ready
                                    width: parent.width - 16
                                    horizontalAlignment: Text.AlignHCenter
                                    text: tile.modelData.title
                                    wrapMode: Text.WordWrap
                                    color: theme.inkDim; font.family: theme.display; font.pixelSize: 15
                                }
                            }
                            Image {
                                id: art
                                anchors.fill: parent; anchors.margins: 1
                                source: tile.modelData.poster || ""
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true; cache: true
                                sourceSize.width: 400
                            }
                            MouseArea {
                                id: tileMa
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.westernPicked({ western: true, tag: tile.modelData.tag,
                                                                tagId: tile.modelData.tagId, title: tile.modelData.title })
                            }
                        }
                        Text {
                            width: parent.width
                            text: tile.modelData.title
                            color: tileMa.containsMouse ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 13
                            elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            // curated shelf came up entirely dry on GetComics (unlikely; honest state anyway)
            Text {
                visible: !page.loading && page.tiles.length === 0
                x: theme.margin
                text: "Nothing on this shelf reached GetComics — the curated picks found no releases."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                topPadding: 12
            }

            Item { width: 1; height: 70 }
        }
    }

    // ---- loading state ----
    Column {
        visible: page.loading
        anchors.centerIn: parent
        spacing: 14
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.genreName
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Checking the shelf against GetComics…"
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
        }
    }
}
