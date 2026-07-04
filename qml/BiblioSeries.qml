// BiblioSeries - the series page. Owner: A2. Opens from a SERIES stack (genre browse / search).
// Source = the offline SeriesIndex bridge (biblio_series.db, ~190k series books from the Goodreads
// graph). seriesEntries(name) returns the roster IN READING ORDER with per-book rating/year/cover.
// Clicking a book looks it up on Apple and opens its BiblioBook detail (download-fed reading model).
// Supersedes the old FictionDB series scraping.

import QtQuick
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi

Item {
    id: ser
    property string series: ""
    property string author: ""
    property Item backdrop

    signal backRequested()
    signal bookRequested(var book)
    signal minimizeRequested()
    signal closeRequested()

    property var entries: []          // deduped, reading-ordered: {position,title,rating,year,cover}
    readonly property real avgRating: {
        var s = 0, n = 0
        for (var i = 0; i < entries.length; i++) { var r = entries[i].rating || 0; if (r > 0) { s += r; n++ } }
        return n > 0 ? s / n : 0
    }
    onSeriesChanged: ser.load()
    Component.onCompleted: ser.load()

    // Collapse the index's edition-noise: many books survive as multiple same-position rows
    // (US/UK titles, etc.). Keep one per position (best-rated), then sort by reading position -
    // turns a messy 30-entry "Harry Potter" into its clean run.
    function load() {
        if (!ser.series || typeof SeriesIndex === "undefined") { ser.entries = []; return }
        var raw = SeriesIndex.seriesEntries(ser.series)
        var byPos = {}, order = []
        for (var i = 0; i < raw.length; i++) {
            var e = raw[i], p = String(e.position)
            if (!(p in byPos)) { byPos[p] = e; order.push(p) }
            else if ((e.rating || 0) > (byPos[p].rating || 0)) byPos[p] = e
        }
        order.sort(function(a, b) { return parseFloat(a) - parseFloat(b) })
        ser.entries = order.map(function(p) { return byPos[p] })
    }

    function openByTitle(title, author) {
        if (!title) return
        if (typeof SeriesIndex !== "undefined" && SeriesIndex.bookDetail) {
            var canonicalBook = SeriesIndex.bookDetail(title, author || ser.author || "")
            if (canonicalBook && canonicalBook.title) {
                ser.bookRequested(canonicalBook)
                return
            }
        }
        BiblioApi.lookupBook(title, author || ser.author || "", function(b) { if (b) ser.bookRequested(b) })
    }
    function tint(i) {
        var pal = ["#5a2f45", "#6b2f45", "#7a3a4f", "#5a3550", "#3f5868", "#7a5a2f", "#2f5a55", "#4a3550"]
        return pal[i % pal.length]
    }

    MouseArea { anchors.fill: parent }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
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

    Theme { id: theme }

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
            topPadding: 14
            spacing: 0

            // ── hero: fanned covers + name + author + facts ──
            Item {
                width: parent.width; height: 250

                Item {                                   // the fan
                    id: fan
                    width: 250; height: 230
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: Math.min(4, ser.entries.length)
                        delegate: Item {
                            required property int index
                            width: 150; height: 225
                            transformOrigin: Item.Bottom
                            x: index * 44
                            y: (index === 0 || index === 3) ? 8 : -2
                            rotation: [-13, -4.5, 4, 13][index]
                            z: (index === 1 || index === 2) ? 3 : 2
                            Rectangle {
                                anchors.fill: parent; radius: 8; clip: true
                                gradient: Gradient {
                                    GradientStop { position: 0; color: ser.tint(index) }
                                    GradientStop { position: 1; color: "#180c14" }
                                }
                                layer.enabled: true
                                layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0,0,0,0.75)
                                    shadowBlur: 1.0; shadowVerticalOffset: 12; autoPaddingEnabled: true }
                                Image { anchors.fill: parent
                                    source: ser.entries[index] && ser.entries[index].cover ? ser.entries[index].cover : ""
                                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                Text {
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    anchors.margins: 12
                                    text: ser.entries[index] ? ser.entries[index].title : ""
                                    color: Qt.rgba(1,1,1,0.92); font.family: theme.display; font.pixelSize: 13
                                    wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                Column {
                    anchors.left: fan.right; anchors.leftMargin: 44
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Text { text: "SERIES"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold; font.letterSpacing: 2 }
                    Item { width: 1; height: 12 }
                    Text { width: parent.width; text: ser.series
                        color: theme.ink; font.family: theme.display; font.pixelSize: 50; font.letterSpacing: -0.6
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                    Item { width: 1; height: 8 }
                    Text { text: ser.author; visible: ser.author.length > 0
                        color: theme.inkDim; font.family: theme.display; font.italic: true; font.pixelSize: 20 }
                    Item { width: 1; height: 18 }
                    Row {
                        spacing: 16
                        Text { text: ser.entries.length + " books"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
                        Text { visible: ser.avgRating > 0; text: "·"; color: theme.inkDimmer; font.pixelSize: 14 }
                        Row { visible: ser.avgRating > 0; spacing: 5
                            Text { text: "★"; color: theme.gold; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: ser.avgRating.toFixed(2) + " avg"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }

            Item { width: 1; height: 36 }
            Text { text: "READING ORDER"; color: theme.inkDimmer; font.family: theme.ui
                font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
            Item { width: 1; height: 10 }

            // ── roster: one row per book, in order ──
            Column {
                width: parent.width
                Repeater {
                    model: ser.entries
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: parent.width; height: 78; radius: 12
                        color: rowMa.containsMouse ? theme.glassTint : "transparent"
                        readonly property bool novella: String(modelData.position).indexOf(".") >= 0

                        Text {                            // position
                            x: 8; width: 54; anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignHCenter
                            text: "#" + modelData.position
                            color: novella ? theme.gold : theme.inkDimmer
                            font.family: theme.display; font.pixelSize: novella ? 16 : 22
                        }
                        Rectangle {                       // mini cover
                            x: 70; width: 44; height: 64; radius: 5; clip: true
                            anchors.verticalCenter: parent.verticalCenter
                            gradient: Gradient {
                                GradientStop { position: 0; color: ser.tint(index) }
                                GradientStop { position: 1; color: "#160d14" }
                            }
                            Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                        }
                        Column {
                            x: 132; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 132 - 150; spacing: 3
                            Text { width: parent.width; text: modelData.title ? modelData.title : ""
                                color: theme.ink; font.family: theme.display; font.pixelSize: 18
                                elide: Text.ElideRight; maximumLineCount: 1 }
                            Text { text: novella ? "Novella" + (modelData.year ? "  ·  " + modelData.year : "")
                                                 : (modelData.year ? "" + modelData.year : "")
                                color: novella ? theme.gold : theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                        }
                        Row {
                            anchors.right: parent.right; anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter; spacing: 18
                            Row { visible: (modelData.rating || 0) > 0; spacing: 5
                                anchors.verticalCenter: parent.verticalCenter
                                Text { text: "★"; color: theme.gold; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: (modelData.rating || 0).toFixed(2); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter }
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: getTxt.width + 28; height: 32; radius: 8
                                color: getMa.containsMouse ? theme.glassHi : "transparent"
                                border.width: 1; border.color: theme.edge
                                Text { id: getTxt; anchors.centerIn: parent; text: "Download"
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                                MouseArea { id: getMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: ser.openByTitle(modelData.title, modelData.author || ser.author) }
                            }
                        }
                        MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true; z: -1
                            cursorShape: Qt.PointingHandCursor; onClicked: ser.openByTitle(modelData.title, modelData.author || ser.author) }
                    }
                }
            }

            Text {
                visible: ser.entries.length === 0
                text: "No books found for this series."; color: theme.inkDimmer
                font.family: theme.display; font.italic: true; font.pixelSize: 16; topPadding: 30
            }
        }
    }
}
