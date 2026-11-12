import QtQuick

// Square record face with a 67% photo label; the host owns the name caption.
Item {
    id: root
    property url artworkSource: ""
    property string title: ""
    property string displayFont: "Fraunces"
    readonly property bool includesTitle: false
    readonly property string initials: title.trim().split(/\s+/).slice(0, 2)
        .map(function(word) { return word.charAt(0); }).join("").toLocaleUpperCase()
    implicitWidth: 196
    implicitHeight: width

    Item {
        id: record
        width: root.width
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#09090d"
        }
        VinylArtwork {
            id: photo
            objectName: "vinylArtistArtwork"
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
            objectName: "vinylArtistInitials"
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
}
