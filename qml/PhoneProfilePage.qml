// PhoneProfilePage.qml — the page behind the Android shell's Profile tab (Slice S3, decision 19).
//
// A plain list. Each row opens a page the app already has; nothing new is built behind a row.
// The shell owns the routes: this page only says which row was tapped.
import QtQuick

Item {
    id: page

    // [{ key, label, note }] in display order, supplied by the shell
    property var rows: []
    property real contentBottomInset: 0
    signal rowRequested(string key)

    Theme { id: theme }

    // An opaque floor: the page lies over whatever world or title page was open beneath it.
    Rectangle { anchors.fill: parent; color: "#07080c" }
    MouseArea { anchors.fill: parent }   // taps never fall through to the page beneath

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.bottomMargin: page.contentBottomInset
        contentWidth: width
        contentHeight: column.height + 48
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column
            x: 20
            y: 36
            width: Math.min(flick.width - 40, 640)
            spacing: 0

            Text {
                text: "PROFILE"
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.5
            }
            Item { width: 1; height: 8 }
            Text {
                width: parent.width
                text: "Your Colosseum"
                color: theme.ink
                wrapMode: Text.WordWrap
                font.family: theme.display; font.pixelSize: 30; font.weight: Font.DemiBold
            }
            Item { width: 1; height: 22 }

            Repeater {
                model: page.rows
                delegate: Item {
                    id: row
                    required property var modelData
                    objectName: "profileRow_" + row.modelData.key
                    width: column.width
                    height: Math.max(60, labels.implicitHeight + 24)

                    Rectangle {
                        anchors.fill: parent
                        radius: 14
                        color: rowMa.pressed ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
                    }
                    Column {
                        id: labels
                        anchors.left: parent.left; anchors.leftMargin: 14
                        anchors.right: chevron.left; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            width: parent.width
                            text: row.modelData.label
                            color: theme.ink
                            wrapMode: Text.WordWrap
                            font.family: theme.ui; font.pixelSize: 17; font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: row.modelData.note || ""
                            color: theme.inkDim
                            wrapMode: Text.WordWrap
                            font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                    // a chevron drawn from two bars, so no font has to supply the glyph
                    Item {
                        id: chevron
                        anchors.right: parent.right; anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10; height: 16
                        Rectangle { x: 1; y: 3; width: 9; height: 2; radius: 1; rotation: 45
                                    transformOrigin: Item.Left; color: theme.inkDim; antialiasing: true }
                        Rectangle { x: 1; y: 12; width: 9; height: 2; radius: 1; rotation: -45
                                    transformOrigin: Item.Left; color: theme.inkDim; antialiasing: true }
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.leftMargin: 14
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.08)
                    }
                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        onClicked: page.rowRequested(row.modelData.key)
                    }
                }
            }
        }
    }
}
