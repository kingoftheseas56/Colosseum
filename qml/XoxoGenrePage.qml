// XoxoGenrePage — one xoxo genre/shelf box (superhero, horror, Popular...) as a
// paginated series grid. Peer of ComicArchiveIndex (GetComics). A tile opens the
// xoxo issue list (XoxoSeries). Load-more walks pages until the overflow quirk shows
// up: xoxo repeats content past the real end, so a repeated first id = true end.

import QtQuick
import "XoxoApi.js" as Xoxo

Item {
    id: page
    property Item backdrop
    property var box: ({})               // {id, label} from xoxoGenreRequested
    signal seriesRequested(var data)     // {id, title, cover}
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    property var items: []
    property int nextPage: 1
    property bool hasMore: true
    property bool fetching: false
    property string lastFirstId: ""      // overflow guard: repeated first id = true end
    property bool loading: true
    property int cooldownMs: 0           // >0 → source rate-limited, banner shows countdown

    Theme { id: theme }

    Component.onCompleted: fetchMore()
    onBoxChanged: { items = []; nextPage = 1; hasMore = true; lastFirstId = ""; loading = true; cooldownMs = 0; fetchMore() }

    function fetchMore() {
        if (fetching || !hasMore || !box.id) return
        fetching = true
        Xoxo.exploreItems(box.id, nextPage, function(r, meta) {
            fetching = false
            page.loading = false
            page.cooldownMs = (meta && meta.blocked) ? meta.retryInMs : 0
            if (meta && meta.blocked) return   // banner explains the quiet; don't burn hasMore
            if (!r || r.items.length === 0) { page.hasMore = false; return }
            if (r.items[0].id === page.lastFirstId) { page.hasMore = false; return }  // overflow repeat
            page.lastFirstId = r.items[0].id
            page.items = page.items.concat(r.items)
            page.nextPage += 1
            page.hasMore = r.hasMore
        })
    }

    // ===================== visual tree =====================
    MouseArea { anchors.fill: parent }                      // absorb clicks from the world below

    // source cooldown banner (rate-limited): honest state, auto-retries the next page
    SourceCooldownBanner {
        z: 40
        anchors.top: parent.top; anchors.topMargin: 84
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - theme.margin * 2, 460)
        retryInMs: page.cooldownMs
        onRetry: page.fetchMore()
    }

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

    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: page.backRequested()
    }

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
        onContentYChanged: {
            if (contentY + height > contentHeight - 400) page.fetchMore()   // near the end → load more
        }

        Column {
            id: col
            width: flick.width
            spacing: 0

            Item { width: 1; height: 96 }

            Column {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                spacing: 10
                Text {
                    text: "XOXO · Tankoban"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                    font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                }
                Text {
                    text: page.box.label || ""
                    color: theme.ink; font.family: theme.display; font.pixelSize: 48
                    font.weight: Font.DemiBold
                }
                Text {
                    text: page.items.length + " series" + (page.hasMore ? " so far" : "")
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                }
            }

            Item { width: 1; height: 26 }

            Grid {
                id: grid
                x: theme.margin
                width: parent.width - 2 * theme.margin
                columns: 6
                columnSpacing: 18; rowSpacing: 24
                readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

                Repeater {
                    model: page.items
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
                                source: tile.modelData.cover || ""
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true; cache: true
                                sourceSize.width: 400
                            }
                            MouseArea {
                                id: tileMa
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.seriesRequested({ id: tile.modelData.id,
                                                                  title: tile.modelData.title,
                                                                  cover: tile.modelData.cover })
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

            Item { width: 1; height: 48 }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: page.loading
        text: "Loading…"
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
    }
}
