// DiscoverPreview — Discover's right pane: mirrors the selected wall poster.
// Stage-1 actions: + Library (LibraryButton, world "theatre") and ▶ Show (opens the
// detail page — the same door as a second click). Watched-eye lands with Stage 2;
// Trailer rides the yt-dlp arc (never a dead button). meta = Cinemeta-shaped card
// (mapCinemeta output or raw addon meta): { id, type, title|caption|name, cover|poster,
// description?, releaseInfo?|year?, imdbRating?, runtime? }.
import QtQuick

Rectangle {
    id: pane
    property var meta: null
    signal showRequested(var item)

    radius: 16
    color: Qt.rgba(0.03, 0.035, 0.055, 0.72)
    border.width: 1; border.color: theme.edge
    clip: true

    Theme { id: theme }

    readonly property string title: meta ? (meta.title || meta.caption || meta.name || "") : ""
    readonly property string cover: meta ? (meta.cover || meta.poster || "") : ""
    readonly property string year: meta ? String(meta.releaseInfo || meta.year || "") : ""
    readonly property string rating: meta && meta.imdbRating ? String(meta.imdbRating) : ""
    readonly property string runtime: meta && meta.runtime ? String(meta.runtime) : ""
    readonly property string blurb: meta ? String(meta.description || meta.synopsis || "") : ""
    readonly property var genres: (meta && meta.genres && meta.genres.length) ? meta.genres : []

    // ---- art (top ~45%): cover, dark-scrimmed, title overlaid ----
    Item {
        id: art
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: Math.round(pane.height * 0.45)
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#2a2140" }
                GradientStop { position: 1; color: "#0d0a18" }
            }
        }
        Image {
            anchors.fill: parent
            source: pane.cover
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.45; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(0.02, 0.02, 0.04, 0.92) }
            }
        }
        Text {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: 22
            text: pane.title
            color: theme.ink
            font.family: theme.display; font.pixelSize: 30; font.weight: Font.Bold
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
    }

    // ---- body ----
    Column {
        anchors.top: art.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: actions.top
        anchors.margins: 22; anchors.bottomMargin: 12
        spacing: 12
        Row {
            spacing: 16
            visible: pane.runtime.length || pane.year.length || pane.rating.length
            Text { visible: pane.runtime.length; text: pane.runtime
                   color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text { visible: pane.year.length; text: pane.year
                   color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
            Row {
                visible: pane.rating.length; spacing: 4
                Text { text: "★"; color: theme.gold; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                Text { text: pane.rating; color: theme.ink; font.family: theme.ui
                       font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
        }
        // genre chips (mock .pv-genres) — dim bordered tags from the meta's genres
        Flow {
            width: parent.width
            visible: pane.genres.length > 0
            spacing: 6
            Repeater {
                model: pane.genres
                Rectangle {
                    required property var modelData
                    height: 22; radius: 7
                    width: chipTxt.implicitWidth + 18
                    color: "transparent"
                    border.width: 1; border.color: theme.edge
                    Text {
                        id: chipTxt
                        anchors.centerIn: parent
                        text: modelData
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                    }
                }
            }
        }
        Text {
            width: parent.width
            text: pane.blurb
            visible: pane.blurb.length > 0
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 13
            lineHeight: 1.4
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 8
        }
    }

    // ---- actions ----
    Row {
        id: actions
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.margins: 22
        spacing: 10
        LibraryButton {
            world: "theatre"
            entry: pane.meta ? { "id": String(pane.meta.id || ""),
                                 "type": pane.meta.type === "movie" ? "movie" : "series",
                                 "title": pane.title, "cover": pane.cover, "payload": ({}) } : null
        }
        Rectangle {
            width: showRow.implicitWidth + 36; height: 42; radius: 11
            color: theme.gold
            Row {
                id: showRow
                anchors.centerIn: parent; spacing: 8
                Text { text: "▶"; color: "#17120a"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Show"; color: "#17120a"; font.family: theme.ui
                       font.pixelSize: 15; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: if (pane.meta) pane.showRequested(pane.meta)
            }
        }
    }

    // empty state — nothing selected/loaded yet
    Text {
        visible: !pane.meta
        anchors.centerIn: parent
        text: "Pick something from the wall"
        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14
    }
}
