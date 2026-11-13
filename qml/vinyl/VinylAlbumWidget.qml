import QtQuick

// Presentation only. The owning VinylItemCard keeps metadata and all interaction.
Item {
    id: root

    property url artworkSource: ""
    property string title: ""

    implicitWidth: 196
    implicitHeight: width

    Item {
        id: jewel
        x: (root.width - width) / 2
        y: (root.height - height) / 2 - 1
        width: Math.max(0, Math.min(root.width - 4, (root.height - 6) * 1.10))
        height: width / 1.10

        readonly property real rim: width * 0.032
        readonly property real spineWidth: width * 0.079
        readonly property real coverSize: height - 2 * rim
        readonly property real coverX: width - rim - coverSize
        readonly property real corner: Math.max(1, width * 0.014)

        // The shallow back tray and offset bottom edge give the closed case thickness.
        Rectangle {
            x: 1
            y: 3
            width: jewel.width
            height: jewel.height
            radius: jewel.corner
            color: Qt.rgba(0, 0, 0, 0.34)
        }
        Rectangle {
            anchors.fill: parent
            radius: jewel.corner
            border.width: 1
            border.color: Qt.rgba(0.89, 0.94, 0.96, 0.36)
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0.70, 0.79, 0.82, 0.25) }
                GradientStop { position: 0.09; color: Qt.rgba(0.31, 0.39, 0.43, 0.19) }
                GradientStop { position: 0.72; color: Qt.rgba(0.17, 0.23, 0.26, 0.22) }
                GradientStop { position: 1; color: Qt.rgba(0.66, 0.74, 0.78, 0.30) }
            }
        }

        // The cover sits behind the clear lid; it never crops to fill the square.
        Rectangle {
            x: jewel.coverX - 1
            y: jewel.rim - 1
            width: jewel.coverSize + 2
            height: jewel.coverSize + 2
            color: "#111416"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.62)
        }
        VinylArtwork {
            id: cover
            x: jewel.coverX
            y: jewel.rim
            width: jewel.coverSize
            height: jewel.coverSize
            source: root.artworkSource
            crop: false
            backgroundColor: "#202326"
        }

        // A subdued lid reflection leaves the actual album artwork legible.
        Rectangle {
            x: jewel.coverX
            y: jewel.rim
            width: jewel.coverSize
            height: jewel.coverSize
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.065) }
                GradientStop { position: 0.20; color: Qt.rgba(1, 1, 1, 0.012) }
                GradientStop { position: 0.72; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(0.82, 0.90, 0.94, 0.025) }
            }
        }

        // Separate dark hinge spine, clear gutter and lid seam: visible even at 196 px.
        Rectangle {
            x: 2
            y: 2
            width: jewel.spineWidth
            height: jewel.height - 4
            radius: Math.max(1, jewel.corner - 1)
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#697276" }
                GradientStop { position: 0.12; color: "#30383c" }
                GradientStop { position: 0.38; color: "#192024" }
                GradientStop { position: 0.85; color: "#242c30" }
                GradientStop { position: 1; color: "#566166" }
            }
            border.width: 1
            border.color: Qt.rgba(0.86, 0.92, 0.95, 0.14)
        }
        Repeater {
            model: 5
            delegate: Rectangle {
                required property int index
                x: 4 + index * Math.max(0, (jewel.spineWidth - 5) / 5)
                y: jewel.rim + 2
                width: 1
                height: jewel.height - 2 * jewel.rim - 4
                color: index % 2 === 0 ? Qt.rgba(0.85, 0.91, 0.94, 0.14)
                                      : Qt.rgba(0, 0, 0, 0.38)
            }
        }
        Rectangle {
            x: jewel.spineWidth + 3
            y: 3
            width: Math.max(0, jewel.coverX - jewel.spineWidth - 5)
            height: jewel.height - 6
            color: Qt.rgba(0.73, 0.85, 0.90, 0.11)
            border.width: 1
            border.color: Qt.rgba(0.88, 0.94, 0.97, 0.13)
        }
        Rectangle {
            x: jewel.coverX - 2
            y: 3
            width: 1
            height: jewel.height - 6
            color: Qt.rgba(0.90, 0.95, 0.97, 0.28)
        }

        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                x: jewel.spineWidth - 1
                y: index === 0 ? jewel.rim * 1.35
                               : jewel.height - jewel.rim * 1.35 - height
                width: Math.max(5, jewel.rim * 1.45)
                height: Math.max(8, jewel.height * 0.065)
                radius: 1
                color: Qt.rgba(0.56, 0.66, 0.72, 0.30)
                border.width: 1
                border.color: Qt.rgba(0.91, 0.96, 0.98, 0.30)
                Rectangle {
                    x: 2
                    y: 1
                    width: 1
                    height: parent.height - 2
                    color: Qt.rgba(1, 1, 1, 0.26)
                }
            }
        }

        // Double bevels and small retaining tabs are part of the case, not a selection ring.
        Rectangle {
            x: 3
            y: 2
            width: jewel.width - 6
            height: 1
            color: Qt.rgba(0.96, 0.99, 1, 0.52)
        }
        Rectangle {
            x: jewel.coverX + 2
            y: jewel.rim - 3
            width: jewel.coverSize - 4
            height: 1
            color: Qt.rgba(0.91, 0.97, 1, 0.20)
        }
        Rectangle {
            x: jewel.width - 3
            y: 3
            width: 1
            height: jewel.height - 6
            color: Qt.rgba(0.97, 0.99, 1, 0.40)
        }
        Rectangle {
            x: jewel.coverX - 1
            y: jewel.height - 4
            width: jewel.width - jewel.coverX - 2
            height: 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.40) }
                GradientStop { position: 1; color: Qt.rgba(0.92, 0.97, 1, 0.44) }
            }
        }
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                x: jewel.coverX + jewel.coverSize * (index === 0 ? 0.19 : 0.71)
                y: jewel.height - jewel.rim + 1
                width: jewel.coverSize * 0.10
                height: Math.max(1, jewel.rim - 3)
                radius: 1
                color: Qt.rgba(0.88, 0.94, 0.97, 0.15)
                border.width: 1
                border.color: Qt.rgba(0.96, 0.99, 1, 0.23)
            }
        }
    }
}
