// ComicArchiveIndex — the SERIES ARCHIVES under an explore box (Tankoban mode).
// Clicking "Marvel Comics" or "Batman" on the world's explore mosaic used to dump
// the raw release feed (Hemanth, 2026-07-04: "just individual cbr/cbz uploads, not
// archive pages"). This page is the missing middle layer: a live poster grid of the
// series archives ACTIVE under that box — aggregated from the co-tags of its newest
// 200 posts (ComicsApi.archiveIndex), each card a real /tag/ archive that opens the
// ComicSeries shelf. "All N releases ›" still reaches the raw feed, one level down.
// (This file began as the parked ComicGenrePage; repurposed when genre died on the
// board — the curated-genre concept lives only in git history / option B now.)

import QtQuick
import QtQuick.Controls
import "ComicsApi.js" as Api

Item {
    id: page
    property Item backdrop
    property string boxTitle: ""
    property string tagSlug: ""
    property int    tagId: 0
    property int    boxCount: 0        // the box tag's own release count (for "All N releases")
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal westernPicked(var data)         // a series card → host opens its ComicSeries shelf
    signal allReleasesRequested(var data)  // "All N releases ›" → host opens the box's raw shelf

    // --- resolved series: [{title, tag, tagId, count, freq, cover}] ---
    property var series: []
    property bool loading: true
    property int _gen: 0               // stale-reply guard across box switches

    Theme { id: theme }

    onTagIdChanged: resolve()
    Component.onCompleted: if (tagId > 0) resolve()

    function resolve() {
        loading = true
        series = []
        _gen += 1
        var gen = _gen
        if (tagId <= 0) { loading = false; return }
        Api.archiveIndex(tagId, function(list) {
            if (gen !== page._gen) return
            page.series = list || []
            page.loading = false
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
    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: page.backRequested()
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
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.fullscreenRequested()
            }
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
        ScrollBar.vertical: HouseScrollBar { flick: flick }
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: col
            width: flick.width
            spacing: 0

            Item { width: 1; height: 96 }

            // header — INLINE metadata (bright count, dim medium) + the raw-feed door
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
                    text: page.boxTitle
                    color: theme.ink; font.family: theme.display; font.pixelSize: 48
                    font.weight: Font.DemiBold
                }
                Row {
                    spacing: 11
                    Text { text: page.series.length
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "series archives in the latest releases"; color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                    // the raw feed stays reachable — one honest level down
                    Text {
                        text: "All " + (page.boxCount > 0 ? page.boxCount + " " : "") + "releases ›"
                        color: allMa.containsMouse ? theme.gold : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea { id: allMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.allReleasesRequested({ western: true, tag: page.tagSlug,
                                                                   tagId: page.tagId, title: page.boxTitle }) }
                    }
                }
            }

            Item { width: 1; height: 26 }

            // the series-archive grid
            Grid {
                id: grid
                x: theme.margin
                width: parent.width - 2 * theme.margin
                columns: 6
                columnSpacing: 18; rowSpacing: 24
                readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

                Repeater {
                    model: page.series
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
                                onClicked: page.westernPicked({ western: true, tag: tile.modelData.tag,
                                                                tagId: tile.modelData.tagId, title: tile.modelData.title })
                            }
                        }
                        Column {
                            width: parent.width
                            spacing: 3
                            Text {
                                width: parent.width
                                text: tile.modelData.title
                                color: tileMa.containsMouse ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13
                                elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                width: parent.width
                                text: tile.modelData.count + " releases"
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }

            Text {
                visible: !page.loading && page.series.length === 0
                x: theme.margin
                text: "No series archives surfaced under this box."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                topPadding: 12
            }

            Item { width: 1; height: 70 }
        }
    }

    ScrollGlide { flick: flick }

    // ---- loading state ----
    Column {
        visible: page.loading
        anchors.centerIn: parent
        spacing: 14
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.boxTitle
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Reading the archives…"
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
        }
    }
}
