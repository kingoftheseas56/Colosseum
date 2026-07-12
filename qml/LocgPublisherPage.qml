// LocgPublisherPage — the comics explore page (publisher axis): one LOCG publisher shelf
// (Marvel, DC, Image...) as a paginated series grid. Comics axis is publisher, not genre —
// genre was dropped, LOCG carries no keyless genre; ratified 2026-07-09. A tile opens the LOCG
// series detail (routing wired in a later task). Load-more walks pages until the
// server stops returning a full page (hasMore = items.length >= 100).

import QtQuick
import QtQuick.Controls
import "LocgApi.js" as Locg

Item {
    id: page
    property Item backdrop
    property var box: ({})               // {id, label} from a publisher-shelf request
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
    property string sortMode: "new"      // "new" (as fetched) | "az" (title A–Z)

    // view-only reorder — the fetch/overflow order is untouched. A–Z uses a natural
    // compare so "Batman 2" sorts before "Batman 10".
    readonly property var shownItems: {
        if (sortMode !== "az") return items
        return items.slice().sort(function(a, b) { return natCmp(a.title, b.title) })
    }
    function natCmp(a, b) {
        var ax = String(a).toLowerCase().match(/(\d+)|(\D+)/g) || []
        var bx = String(b).toLowerCase().match(/(\d+)|(\D+)/g) || []
        for (var i = 0; i < Math.max(ax.length, bx.length); i++) {
            var av = ax[i], bv = bx[i]
            if (av === undefined) return -1
            if (bv === undefined) return 1
            if (/^\d/.test(av) && /^\d/.test(bv)) { var d = Number(av) - Number(bv); if (d) return d }
            else if (av !== bv) return av < bv ? -1 : 1
        }
        return 0
    }

    Theme { id: theme }

    Component.onCompleted: fetchMore()
    onBoxChanged: { items = []; nextPage = 1; hasMore = true; lastFirstId = ""; loading = true; cooldownMs = 0; fetchMore() }

    function fetchMore() {
        if (fetching || !hasMore || !box.id) return
        fetching = true
        Locg.publisherItems(box.id, nextPage, function(r, meta) {
            fetching = false
            page.loading = false
            page.cooldownMs = (meta && meta.blocked) ? (meta.retryInMs || 0) : 0
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
        sourceName: "Catalogue"
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
        ScrollBar.vertical: HouseScrollBar { flick: flick }   // gold sliver, same as every page
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
                    text: "LOCG · Tankoban"
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
                // sort control (Newest / A–Z) — view-only reorder
                Row {
                    spacing: 8
                    topPadding: 6
                    Repeater {
                        model: [{ k: "new", l: "Newest" }, { k: "az", l: "A–Z" }]
                        delegate: Rectangle {
                            required property var modelData
                            height: 28; radius: 14
                            width: pillT.implicitWidth + 30
                            property bool on: page.sortMode === modelData.k
                            color: on ? Qt.rgba(0.94, 0.77, 0.29, 0.14) : theme.glassTint
                            border.width: 1
                            border.color: on ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.edge
                            Text { id: pillT; anchors.centerIn: parent; text: modelData.l
                                color: parent.on ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: page.sortMode = modelData.k }
                        }
                    }
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
                    model: page.shownItems
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
