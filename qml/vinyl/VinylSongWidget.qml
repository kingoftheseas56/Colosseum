import QtQuick

// The glass label owns song title/artist; the host retains reasons and availability.
Item {
    id: root
    property url artworkSource: ""
    property string title: ""
    property string artist: ""
    property string displayFont: "Fraunces"
    property string uiFont: "Segoe UI"
    readonly property bool includesTitle: true
    readonly property bool includesSubtitle: true
    readonly property real unit: width / 280
    readonly property real inset: Math.max(5, 8 * unit)
    readonly property real coverSize: Math.max(0, width - 2 * inset)
    readonly property real titleAreaHeight: Math.ceil(titleMetrics.lineSpacing * titleCopy.lineHeight * 2)
    readonly property real artistRowHeight: Math.ceil(artistMetrics.lineSpacing)
    readonly property real labelHeight: Math.max(60, 76 * unit,
        labelCopy.implicitHeight + Math.max(16, 24 * unit))
    implicitWidth: 196
    implicitHeight: coverSize + labelHeight + 2 * inset

    Rectangle {
        anchors.fill: parent
        radius: Math.max(4, 7 * root.unit)
        color: "transparent"
        border.width: 1
        border.color: "#393b3c"
    }
    Rectangle {
        x: root.inset - 1; y: root.inset - 1
        width: root.coverSize + 2
        height: root.coverSize + 2
        color: "#0c0d0e"
    }
    VinylArtwork {
        objectName: "vinylSongArtwork"
        x: root.inset; y: root.inset
        width: root.coverSize; height: width
        source: root.artworkSource
        crop: false
        backgroundColor: "#242225"
    }
    Rectangle {
        id: paper
        objectName: "vinylSongPaper"
        x: root.inset
        y: root.inset + root.coverSize
        width: root.coverSize
        height: root.labelHeight
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: "#c8171a20" }
            GradientStop { position: 0.16; color: "#b620242d" }
            GradientStop { position: 0.52; color: "#ad252a33" }
            GradientStop { position: 0.86; color: "#b21c2029" }
            GradientStop { position: 1; color: "#cc14171d" }
        }
        Rectangle { width: parent.width; height: 1; color: "#52f7f8fa" }
        Rectangle { y: 1; width: parent.width; height: 1; color: "#22ffffff" }
        Rectangle { y: parent.height - height; width: parent.width; height: 1; color: "#24ffffff" }
        Column {
            id: labelCopy
            objectName: "vinylSongLabelCopy"
            anchors.centerIn: parent
            width: parent.width - Math.max(16, 26 * root.unit)
            spacing: Math.max(3, 6 * root.unit)
            Text {
                id: titleCopy
                objectName: "vinylSongTitle"
                width: parent.width
                height: root.titleAreaHeight
                text: root.title
                textFormat: Text.PlainText
                color: "#f7f7f5"
                font.family: root.displayFont
                font.pixelSize: Math.max(16, 24 * root.unit)
                font.weight: Font.Medium
                font.letterSpacing: -0.35 * root.unit
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.08
            }
            Text {
                id: artistCopy
                objectName: "vinylSongArtist"
                width: parent.width
                height: root.artistRowHeight
                text: root.artist.toLocaleUpperCase()
                textFormat: Text.PlainText
                color: "#c9c8d0"
                font.family: root.uiFont
                font.pixelSize: Math.max(10, 11 * root.unit)
                font.weight: Font.DemiBold
                font.letterSpacing: Math.max(0.7, 1.4 * root.unit)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }
    FontMetrics { id: titleMetrics; font: titleCopy.font }
    FontMetrics { id: artistMetrics; font: artistCopy.font }
    Rectangle {
        x: 0; y: root.inset
        width: root.inset
        height: Math.max(0, root.height - 2 * root.inset)
        gradient: Gradient {
            GradientStop { position: 0; color: "#25292b" }
            GradientStop { position: 0.28; color: "#666a6b" }
            GradientStop { position: 0.5; color: "#343839" }
            GradientStop { position: 0.76; color: "#171a1b" }
            GradientStop { position: 1; color: "#383b3c" }
        }
    }
    Rectangle {
        x: root.width - width; y: root.inset
        width: root.inset
        height: Math.max(0, root.height - 2 * root.inset)
        gradient: Gradient {
            GradientStop { position: 0; color: "#25292b" }
            GradientStop { position: 0.28; color: "#666a6b" }
            GradientStop { position: 0.5; color: "#343839" }
            GradientStop { position: 0.76; color: "#171a1b" }
            GradientStop { position: 1; color: "#383b3c" }
        }
    }
    Repeater {
        model: 2
        delegate: Rectangle {
            required property int index
            x: Math.max(3, 5 * root.unit)
            y: index === 0 ? 1 : root.height - height - 1
            width: root.width - 2 * x
            height: Math.max(4, 7 * root.unit)
            radius: Math.max(2, 3 * root.unit)
            gradient: Gradient {
                GradientStop { position: 0; color: "#626664" }
                GradientStop { position: 0.17; color: "#c5c8c4" }
                GradientStop { position: 0.32; color: "#6e7270" }
                GradientStop { position: 0.65; color: "#303536" }
                GradientStop { position: 0.82; color: "#626563" }
                GradientStop { position: 1; color: "#151819" }
            }
        }
    }
}
