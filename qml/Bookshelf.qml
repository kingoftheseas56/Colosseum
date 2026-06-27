// Bookshelf — the Tankoban HOME mode-intro widget: one glass "bar" split into halves — a MANGA fan
// (left) and a COMICS fan (right), sitting back to back under the centered "Tankoban" title. Each fan
// is the "comic-strip / shelf" form: a spread hand of that medium's real covers. The corner labels
// ("Manga" left, "Comics" right) name each half. Covers are remote (AniList manga / iTunes comics) —
// async + disk-cached via the native launcher, exactly like PortraitTile.
//
// Glass = the panel chrome (doctrine: chrome is glass); the covers are SOLID content fanned on it.

import QtQuick

Glass {
    id: shelf

    property var mangaBooks: []
    property var comicsBooks: []
    property string heading: "Tankoban"
    property int coverW: 150
    property int coverH: 222

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

                // drop shadow (offset rounded rect behind the cover) — gives the fan depth
                Rectangle {
                    width: parent.width; height: parent.height
                    x: 4; y: 10; radius: 10
                    color: Qt.rgba(0, 0, 0, 0.42)
                }

                // the cover — solid art; the c1→c2 tint stands in until the remote cover loads
                Rectangle {
                    anchors.fill: parent
                    radius: 9; clip: true
                    gradient: Gradient {
                        GradientStop { position: 0; color: f.books[card.index] ? f.books[card.index].c1 : "#532f49" }
                        GradientStop { position: 1; color: f.books[card.index] ? f.books[card.index].c2 : "#1d121b" }
                    }
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)

                    Image {
                        anchors.fill: parent
                        source: f.books[card.index] ? f.books[card.index].cover : ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 300; sourceSize.height: 444
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                    // hover accent — the one gold touch
                    Rectangle {
                        anchors.fill: parent; radius: parent.radius; color: "transparent"
                        border.width: cma.containsMouse ? 2 : 0; border.color: theme.gold
                    }
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
