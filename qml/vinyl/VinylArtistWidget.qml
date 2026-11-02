import QtQuick

// v6 Kindred sounds: glass frame, full grooved record, 67% photo label.
Item {
    id: root
    property url artworkSource: ""
    property string title: ""
    property string displayFont: "Fraunces"
    readonly property bool includesTitle: true
    readonly property string initials: title.trim().split(/\s+/).slice(0, 2)
        .map(function(word) { return word.charAt(0); }).join("").toLocaleUpperCase()
    implicitWidth: 196
    implicitHeight: width * 1.084

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: root.width * 0.1
        color: Qt.rgba(1, 1, 1, 0.09)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16)
    }
    Item {
        id: record
        width: Math.min(root.width * 0.758, root.height * 0.72)
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.height * 0.06
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#09090d"
        }
        VinylArtwork {
            id: photo
            anchors.centerIn: parent
            width: record.width * 0.67
            height: width
            source: root.artworkSource
            crop: true
            showPlaceholder: false
            backgroundColor: "#262832"
        }
        // A shared opaque annulus covers the photo's corners. The 67% square fits
        // entirely within the record, so circular cropping needs no per-card FBO.
        Image {
            id: grooves
            objectName: "vinylRecordMaterial"
            anchors.fill: parent
            source: "assets/record-ring.svg"
            sourceSize.width: Math.ceil(width * 2)
            sourceSize.height: Math.ceil(height * 2)
            cache: true
            smooth: true
        }
        Rectangle {
            anchors.centerIn: parent
            width: record.width * 0.67 + 2
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(1, record.width * 0.012)
            border.color: "#c0000000"
        }
        Text {
            anchors.centerIn: parent
            width: record.width * 0.52
            visible: !photo.ready
            text: root.initials
            textFormat: Text.PlainText
            color: "#ddd5ca"
            font.family: root.displayFont
            font.pixelSize: record.width * 0.21
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
    Text {
        x: root.width * 0.05
        y: record.y + record.height + root.width * 0.08
        width: root.width * 0.9
        height: Math.max(0, root.height - y - root.width * 0.06)
        text: root.title
        textFormat: Text.PlainText
        color: "#f7f7f5"
        font.family: root.displayFont
        font.pixelSize: Math.max(16, root.width * 17 / 179)
        font.weight: Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }
}
