// BiblioSeries — the series detail page. Owner: A2. Opens from a SERIES card in search. Shows the
// series as a hero (cover + name + author + count) and its books as a grid; clicking a book looks it
// up on Apple and opens its BiblioBook detail. `group` is a FictionDB series group from BiblioApi.
// v1 lists the books found in the search grouping; fetching the FULL series roster is a follow-up.

import QtQuick
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi

Item {
    id: ser
    property var group: ({})
    property Item backdrop

    signal backRequested()
    signal bookRequested(var book)
    signal minimizeRequested()
    signal closeRequested()

    property var books: []
    property bool loading: false
    onGroupChanged: ser.loadRoster()

    Theme { id: theme }
    MouseArea { anchors.fill: parent }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    function openByTitle(title) {
        if (!title) return
        BiblioApi.lookupBook(title, function(b) { if (b) ser.bookRequested(b) })
    }

    // Show the books we already have instantly, then swap in the FULL series roster (every book,
    // in order) once FictionDB's series page + the missing covers come back.
    function loadRoster() {
        ser.books = (ser.group && ser.group.books) ? ser.group.books : []
        if (!ser.group || !ser.group.seriesId) return
        ser.loading = true
        BiblioApi.loadFullSeries(ser.group.seriesId, ser.group.books, function(res) {
            if (res.books && res.books.length > 0) ser.books = res.books
            ser.loading = false
        })
    }

    // ── top bar ──
    Glass {
        id: bar
        backdrop: ser.backdrop
        x: theme.margin; y: 22
        width: ser.width - theme.margin * 2; height: 64; radius: 16
        Row {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter; spacing: 22
            Text {
                text: "‹ Back"; color: backMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -10
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ser.backRequested() }
            }
            Text { text: "Biblio"; color: theme.ink; font.family: theme.display; font.pixelSize: 20
                anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Repeater {
                model: [ { g: "—", a: "min" }, { g: "⏻", a: "pow" } ]
                delegate: Rectangle {
                    required property var modelData
                    width: 30; height: 30; radius: 8
                    color: sysMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.g; color: theme.inkDimmer; font.pixelSize: 14 }
                    MouseArea { id: sysMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (modelData.a === "min") ser.minimizeRequested(); else ser.closeRequested() } }
                }
            }
        }
    }

    // ── content ──
    Flickable {
        id: page
        anchors.left: parent.left; anchors.right: parent.right
        y: 108; height: ser.height - 108
        contentWidth: width
        contentHeight: content.implicitHeight + 60
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            x: theme.margin
            width: ser.width - theme.margin * 2
            topPadding: 16
            spacing: 0

            // ── hero ──
            Item {
                width: parent.width; height: 220

                Item {                                   // series cover as an object
                    id: hero; width: 130; height: 195
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    Image {
                        id: heroImg; anchors.fill: parent
                        source: (ser.group && ser.group.cover) ? ser.group.cover : ""
                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                        layer.enabled: true
                        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0,0,0,0.7)
                            shadowBlur: 1.0; shadowVerticalOffset: 18; autoPaddingEnabled: true }
                    }
                    Rectangle { anchors.left: parent.left; width: 9; height: parent.height; radius: 2
                        gradient: Gradient { orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Qt.rgba(0,0,0,0.5) }
                            GradientStop { position: 0.6; color: Qt.rgba(0,0,0,0.05) }
                            GradientStop { position: 1; color: Qt.rgba(1,1,1,0.08) } } }
                }
                Column {
                    anchors.left: hero.right; anchors.leftMargin: 36
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Text { text: "SERIES"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                    Item { width: 1; height: 12 }
                    Text { width: parent.width
                        text: (ser.group && ser.group.seriesName) ? ser.group.seriesName : ""
                        color: theme.ink; font.family: theme.display; font.pixelSize: 46
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                    Item { width: 1; height: 12 }
                    Text { text: ((ser.group && ser.group.author) ? ser.group.author : "")
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15 }
                    Item { width: 1; height: 6 }
                    Text { text: ser.books.length + " books in this series" + (ser.loading ? "   ·   loading…" : "")
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                }
            }

            Item { width: 1; height: 40 }
            Text { text: "BOOKS IN THIS SERIES"; color: theme.inkDimmer; font.family: theme.ui
                font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.6 }
            Item { width: 1; height: 18 }

            // ── member books grid ──
            Grid {
                id: grid
                width: parent.width; columns: 6
                columnSpacing: 22; rowSpacing: 26
                property real cellW: (width - columnSpacing * (columns - 1)) / columns
                Repeater {
                    model: ser.books
                    delegate: Column {
                        required property var modelData
                        width: grid.cellW; spacing: 9
                        Rectangle {
                            width: parent.width; height: width * 1.5; radius: 8; clip: true; color: "#14131a"
                            Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                            Rectangle {                       // position chip ("Book N")
                                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
                                visible: modelData.position > 0
                                radius: 6; height: 20; width: posTxt.width + 14; color: Qt.rgba(0, 0, 0, 0.66)
                                Text { id: posTxt; anchors.centerIn: parent; text: "BOOK " + modelData.position
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 0.6 }
                            }
                            scale: bMa.containsMouse ? 1.03 : 1.0
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                            MouseArea { id: bMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: ser.openByTitle(modelData.title) }
                        }
                        Text { width: parent.width; text: modelData.title ? modelData.title : ""
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                            elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
