// BiblioBook — the book "dust-jacket" detail page. Owner: A2. OUR OWN design (NOT the manga series
// view): the cover as a physical object · the tagline as the hero · a drop-capped synopsis · an
// "Editions" panel. Opens as a layer over the Biblio world (Main.qml bookLayer). `book` is a full
// Apple object from BiblioApi.fullBook.
//
// The Editions rows are a STUB until the libgen "delivery" layer is ported (TB2 had it; Colosseum
// doesn't yet). Metadata + layout are real; the download list is a preview.

import QtQuick
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi

Item {
    id: detail
    property var book: ({})
    property Item backdrop
    property var editions: []
    property bool edLoading: false

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    Theme { id: theme }
    MouseArea { anchors.fill: parent }                 // swallow clicks to the world beneath
    // SOLID page (doctrine: books = page solid, frame OS) — a calm dark reading ground so the busy
    // world page never bleeds through and the long-form text stays legible.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // raised illuminated initial: oversize the first letter inline (QML has no CSS float drop-cap)
    function dropCapHtml(s) {
        var t = String(s || "");
        if (t.length === 0) return "";
        var first = t.charAt(0);
        var rest = t.substring(1);
        return '<span style="font-family:' + theme.display + '; font-size:62px; color:#f7f7f5;">'
             + first + '</span>' + rest;
    }

    // ── editions: live LibGen search for this book (recreates TB2's scraper) ──
    onBookChanged: detail.loadEditions()
    function loadEditions() {
        if (!detail.book || !detail.book.title) return
        detail.edLoading = true
        detail.editions = []
        BiblioApi.searchLibgen(detail.book.title, detail.book.author, function(eds) {
            detail.editions = eds
            detail.edLoading = false
        })
    }
    function edMeta(ed) {
        var p = []
        if (ed.year) p.push(ed.year)
        if (ed.language) p.push(ed.language)
        return p.length ? "   ·   " + p.join("   ·   ") : ""
    }

    // ── top bar ────────────────────────────────────────────────────────────
    Glass {
        id: bar
        backdrop: detail.backdrop
        x: theme.margin; y: 22
        width: detail.width - theme.margin * 2
        height: 64; radius: 16

        Row {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 22
            Text {
                text: "‹ Back"; color: backMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    id: backMa; anchors.fill: parent; anchors.margins: -10
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: detail.backRequested()
                }
            }
            Text {
                text: "Biblio"; color: theme.ink; font.family: theme.display; font.pixelSize: 20
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Repeater {
                model: [ { g: "—", a: "min" }, { g: "⏻", a: "pow" } ]   // fullscreen-only: no maximize
                delegate: Rectangle {
                    required property var modelData
                    width: 30; height: 30; radius: 8
                    color: sysMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.g; color: theme.inkDimmer; font.pixelSize: 14 }
                    MouseArea {
                        id: sysMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.a === "min") detail.minimizeRequested()
                            else if (modelData.a === "pow") detail.closeRequested()
                        }
                    }
                }
            }
        }
    }

    // ── scrollable content ─────────────────────────────────────────────────
    Flickable {
        id: page
        anchors.left: parent.left; anchors.right: parent.right
        y: 108; height: detail.height - 108
        contentWidth: width
        contentHeight: body.implicitHeight + 70
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: body
            x: theme.margin
            width: detail.width - theme.margin * 2
            implicitHeight: Math.max(coverCol.implicitHeight, textCol.implicitHeight) + 36

            // ── cover column ──
            Column {
                id: coverCol
                width: 268
                topPadding: 16
                spacing: 28

                // the book as a physical object: soft shadow + cover + spine + page edge
                Item {
                    width: 268; height: 402

                    Rectangle {                       // page edge (right)
                        anchors.right: parent.right; anchors.rightMargin: -5
                        y: 5; width: 7; height: parent.height - 10; radius: 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "#d3cdbe" }
                            GradientStop { position: 1; color: "#a8a294" }
                        }
                    }
                    Image {
                        id: coverImg
                        anchors.fill: parent
                        source: (detail.book && detail.book.cover) ? detail.book.cover : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true; cache: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Qt.rgba(0, 0, 0, 0.7)
                            shadowBlur: 1.0
                            shadowVerticalOffset: 26
                            shadowHorizontalOffset: 0
                            autoPaddingEnabled: true
                        }
                    }
                    Rectangle {                       // base tint while the cover loads
                        anchors.fill: coverImg; z: -1; radius: 3
                        color: (detail.book && detail.book.c1) ? detail.book.c1 : "#14131a"
                    }
                    Rectangle {                       // spine (left)
                        anchors.left: parent.left; width: 11; height: parent.height; radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.5) }
                            GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.05) }
                            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                        }
                    }
                }

                Column {                              // actions
                    width: 268; spacing: 12
                    Rectangle {
                        width: parent.width; height: 50; radius: 13; color: theme.gold
                        Text {
                            anchors.centerIn: parent; text: "Read"; color: "#241a05"
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                    Rectangle {
                        width: parent.width; height: 50; radius: 13
                        color: libMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent; text: "+ Library"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea { id: libMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // ── text column ──
            Column {
                id: textCol
                anchors.left: coverCol.right; anchors.leftMargin: 64
                anchors.right: parent.right
                topPadding: 18
                spacing: 0

                Text {                                // eyebrow
                    text: (detail.book && detail.book.genreLine ? detail.book.genreLine : "").toUpperCase()
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { width: 1; height: 14 }
                Text {                                // title
                    text: detail.book && detail.book.title ? detail.book.title : ""
                    color: theme.ink; font.family: theme.display; font.pixelSize: 54
                    width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.02
                }
                Item { width: 1; height: 20 }
                Text {                                // tagline — the hero
                    visible: text.length > 0
                    text: detail.book && detail.book.tagline ? "“" + detail.book.tagline + "”" : ""
                    color: theme.ink; opacity: 0.92
                    font.family: theme.display; font.italic: true; font.pixelSize: 28
                    width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.3
                }
                Item { width: 1; height: 30 }
                Item {                                // hairline rule with a gold tick
                    width: parent.width; height: 3
                    Rectangle {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 1
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: theme.edge }
                            GradientStop { position: 0.7; color: "transparent" }
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.top: parent.top; width: 34; height: 3; radius: 2; color: theme.gold }
                }
                Item { width: 1; height: 26 }
                Text {                                // synopsis with a raised initial
                    width: Math.min(parent.width, 640)
                    textFormat: Text.RichText
                    text: detail.dropCapHtml(detail.book ? detail.book.synopsis : "")
                    color: theme.inkDim; font.family: theme.display; font.pixelSize: 17
                    wrapMode: Text.WordWrap; lineHeight: 1.7
                }

                Item { width: 1; height: 40 }
                // ── Editions — live from LibGen (recreates TB2's scraper); click opens the download ──
                Text {
                    text: "EDITIONS  ·  LIBGEN" + (detail.edLoading ? "  ·  SEARCHING…"
                          : (detail.editions.length > 0 ? "  ·  " + detail.editions.length : "  ·  NONE"))
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { width: 1; height: 12 }
                Glass {
                    backdrop: detail.backdrop
                    width: Math.min(parent.width, 640); radius: 14
                    height: edCol.implicitHeight
                    Column {
                        id: edCol
                        width: parent.width

                        Item {                              // loading / empty state
                            visible: detail.edLoading || detail.editions.length === 0
                            width: parent.width; height: 52
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: detail.edLoading ? "Searching LibGen…" : "No editions found on LibGen"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                        }

                        Repeater {
                            model: detail.editions
                            delegate: Item {
                                required property var modelData
                                required property int index
                                width: parent.width; height: 52
                                Rectangle { anchors.fill: parent; color: edMa.containsMouse ? Qt.rgba(1,1,1,0.06)
                                    : (modelData.best ? Qt.rgba(0.94,0.77,0.29,0.06) : "transparent") }
                                Rectangle { visible: index > 0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                                Row {
                                    anchors.left: parent.left; anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 16
                                    Rectangle {
                                        width: 54; height: 24; radius: 7; color: "transparent"
                                        border.width: 1
                                        border.color: modelData.best ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { anchors.centerIn: parent; text: (modelData.format || "?").toUpperCase()
                                            color: modelData.best ? theme.gold : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.8 }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "<b>" + (modelData.size || "") + "</b>" + detail.edMeta(modelData)
                                        textFormat: Text.RichText
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    }
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "↓"; color: edMa.containsMouse ? theme.gold : theme.inkDimmer; font.pixelSize: 16
                                }
                                MouseArea { id: edMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(modelData.detailUrl) }
                            }
                        }
                    }
                }
            }
        }
    }
}
