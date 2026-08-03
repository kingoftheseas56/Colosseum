// Bookshelf — the Tankoban HOME mode-intro widget: one glass "bar" split into halves — a MANGA fan
// (left) and a COMICS fan (right), sitting back to back under the centered "Tankoban" title. Each fan
// is the "comic-strip / shelf" form: a spread hand of that medium's real covers. The corner labels
// ("Manga" left, "Comics" right) name each half. Covers are remote (AniList manga / iTunes comics) —
// async + disk-cached via the native launcher, exactly like PortraitTile.
//
// Glass = the panel chrome (doctrine: chrome is glass); the covers are SOLID content fanned on it.

import QtQuick
import QtQuick.Effects
import "CatalogueVisualMetrics.js" as Metrics

Glass {
    id: shelf

    property var mangaBooks: []
    property var comicsBooks: []
    property string heading: "Tankoban"
    // gallery geometry — read from the frozen tokens so the fan reads as one family with the
    // shelves below it (PortraitTile / ContinueTile world variant). 148x222 portrait.
    property int coverW: Metrics.gallery.posterWidth
    property int coverH: Math.round(Metrics.gallery.posterWidth * Metrics.gallery.posterRatio)
    readonly property real _coverRadius: Metrics.gallery.posterRadius

    signal clicked()                                  // title → open the world
    signal bookClicked(string medium, int index)      // a single cover → open that title

    radius: 18
    height: 400

    Theme { id: theme }

    // ---- a fanned spread of one medium's covers (the "comic-strip / shelf") ----
    component Fan: Item {
        id: f
        property var books: []
        readonly property int count: Math.min(5, books.length)
        signal activated(int index)

        width: 560; height: shelf.coverH + 72

        // the fan opens a little wider on hover (a spread hand)
        HoverHandler { id: fh }
        property real spread: fh.hovered ? 98 : 72
        Behavior on spread { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Repeater {
            model: f.count
            delegate: Item {
                id: card
                required property int index
                readonly property real off: index - (f.count - 1) / 2.0

                width: shelf.coverW; height: shelf.coverH
                x: f.width / 2 - width / 2 + off * f.spread
                y: f.height / 2 - height / 2 + Math.abs(off) * 11
                rotation: off * 8
                z: 10 - Math.round(Math.abs(off))      // center cover sits on top

                // two cheap offset depth plates behind the cover (flat rounded rects; no blur, no FBO),
                // matching the gallery posters + ContinueTile world variant. Deepen slightly on hover.
                Rectangle {
                    x: 0; y: 3; width: card.width; height: card.height; radius: shelf._coverRadius + 1
                    color: Qt.rgba(0, 0, 0, cma.containsMouse ? 0.42 : 0.28)
                    Behavior on color { ColorAnimation { duration: 220 } }
                }
                Rectangle {
                    x: -2; y: cma.containsMouse ? 11 : 7; width: card.width + 4; height: card.height
                    radius: shelf._coverRadius + 3
                    color: Qt.rgba(0, 0, 0, cma.containsMouse ? 0.20 : 0.10)
                    Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: 220 } }
                }

                // the cover — solid art rendered ONLY through the single rounded MultiEffect mask, so
                // the corners are a GENUINE crop (the old rectangular clip let art paint over the
                // border). The c1→c2 tint stands in until the remote cover loads (and stays on failure).
                Item {
                    id: coverContent
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: coverMask
                        maskThresholdMin: 0.5      // crisp 50% cutoff on the AA'd rounded-rect mask texture
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: f.books[card.index] ? f.books[card.index].c1 : "#532f49" }
                            GradientStop { position: 1; color: f.books[card.index] ? f.books[card.index].c2 : "#1d121b" }
                        }
                    }
                    Image {
                        anchors.fill: parent
                        source: f.books[card.index] ? f.books[card.index].cover : ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        smooth: true; mipmap: true  // mipmap only on the bounded decoded image
                        sourceSize.width: Math.ceil(card.width * 2)
                        sourceSize.height: Math.ceil(card.height * 2)
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                }
                // stable rounded mask source (no animation) — a texture provider, not drawn directly.
                Item {
                    id: coverMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    Rectangle { anchors.fill: parent; radius: shelf._coverRadius; color: "black" }
                }
                // resting inset edge: 1px white 8%; becomes 2px soft gold on hover (the shared gallery edge).
                Rectangle {
                    anchors.fill: parent; radius: shelf._coverRadius; color: "transparent"
                    border.width: cma.containsMouse ? 2 : 1
                    border.color: cma.containsMouse ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
                                                    : Qt.rgba(1, 1, 1, 0.08)
                    Behavior on border.color { ColorAnimation { duration: 220 } }
                }

                MouseArea {
                    id: cma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: f.activated(card.index)
                }
            }
        }
    }

    // ---- main title (centered) ----
    Text {
        anchors.top: parent.top; anchors.topMargin: 28
        anchors.horizontalCenter: parent.horizontalCenter
        text: shelf.heading; color: theme.ink
        font.family: theme.display; font.pixelSize: 33
        MouseArea {
            anchors.fill: parent; anchors.margins: -12
            cursorShape: Qt.PointingHandCursor; onClicked: shelf.clicked()
        }
    }
    // ---- half labels in the outer corners ----
    Text {
        anchors.left: parent.left; anchors.leftMargin: 46
        anchors.top: parent.top; anchors.topMargin: 36
        text: "Manga"; color: theme.inkDim
        font.family: theme.display; font.italic: true; font.pixelSize: 22
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 46
        anchors.top: parent.top; anchors.topMargin: 36
        text: "Comics"; color: theme.inkDim
        font.family: theme.display; font.italic: true; font.pixelSize: 22
    }

    // ---- the two fans, back to back under the title ----
    Fan {
        books: shelf.mangaBooks
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: -shelf.width * 0.21
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 30
        onActivated: (i) => shelf.bookClicked("manga", i)
    }
    Fan {
        books: shelf.comicsBooks
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: shelf.width * 0.21
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 30
        onActivated: (i) => shelf.bookClicked("comics", i)
    }
}
