// ComicArchiveBoard — the GetComics Archives door: the live publisher/franchise
// taxonomy (ComicsApi.explore) as a full page. Each box opens ComicArchiveIndex,
// exactly as the old inline world-page mosaic did. When xoxo became the primary
// comics feed (peer-sources spec 2026-07-09), the GetComics taxonomy moved here —
// one click off the world page's "GetComics Archives" tile. GetComics loses nothing.

import QtQuick
import QtQuick.Controls
import "ComicsApi.js" as ComicsApi

Item {
    id: page
    property Item backdrop
    signal boxRequested(var box)     // → win.openComicArchive (existing route)
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    property var boxes: []
    property bool loading: true

    Theme { id: theme }

    Component.onCompleted: ComicsApi.explore(function(b) {
        page.boxes = b || []
        page.loading = false
    })

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
        ScrollBar.vertical: HouseScrollBar { flick: flick }

        Column {
            id: col
            width: parent.width
            spacing: 0

            Item { width: 1; height: 96 }

            Column {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                spacing: 10
                Text {
                    text: "GetComics · Tankoban"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                    font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                }
                Text {
                    text: "GetComics Archives"
                    color: theme.ink; font.family: theme.display; font.pixelSize: 48
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Publishers & franchises — whole-archive releases (TPBs, omnibuses, runs)"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                }
            }

            Item { width: 1; height: 26 }

            GenreMosaic {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                title: ""
                genres: page.boxes
                navigable: false
                onGenreClicked: (i) => page.boxRequested(page.boxes[i])
            }

            Item { width: 1; height: 48 }
        }
    }

    ScrollGlide { flick: flick }

    Text {
        anchors.centerIn: parent
        visible: page.loading
        text: "Loading…"
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
    }
}
