pragma ComponentBehavior: Bound
import QtQuick

// The near-opaque graphite corners cover the crop; no layer or artwork processing.
Item {
    id: art
    property url artworkSource: ""
    property var artworkSources: []
    property string title: ""
    property bool circular: false
    property string displayFont: "Fraunces"
    readonly property string initials: title.trim().split(/\s+/).slice(0, 2)
        .map(word => word.charAt(0)).join("").toUpperCase()
    readonly property bool collage: artworkSources.length > 0
    implicitWidth: 196
    implicitHeight: width

    component Cover: Image {
        fillMode: Image.PreserveAspectCrop
        clip: true
        asynchronous: true
        cache: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        visible: status === Image.Ready && corners.status === BorderImage.Ready
    }

    Cover {
        id: first
        objectName: "vinylArtworkImage"
        width: art.collage ? art.width / 2 : art.width
        height: art.collage ? art.height / 2 : art.height
        source: art.collage ? art.artworkSources[0] : art.artworkSource
    }
    Repeater {
        model: art.collage ? Math.min(3, art.artworkSources.length - 1) : 0
        delegate: Cover {
            required property int index
            objectName: "vinylMixArtwork_" + (index + 1)
            x: index === 1 ? 0 : width
            y: index === 0 ? 0 : height
            width: art.width / 2
            height: art.height / 2
            source: art.artworkSources[index + 1] || ""
        }
    }
    Text {
        objectName: art.circular ? "vinylArtistInitials" : "vinylArtworkPlaceholder"
        anchors.centerIn: parent
        visible: !art.collage && !first.visible
        text: art.circular && art.initials ? art.initials : "♪"
        color: "#9a99a5"
        font.family: art.displayFont
        font.pixelSize: Math.round(Math.min(art.width, art.height) * 0.23)
    }
    BorderImage {
        id: corners
        objectName: art.circular ? "vinylArtistRing" : "vinylArtworkCorners"
        anchors.fill: parent
        z: 1
        visible: art.circular || first.visible || art.collage
        source: art.circular ? "../../assets/icons/vinyl-tile-portrait-ring.svg"
                             : "../../assets/icons/vinyl-tile-corners.svg"
        border.left: art.circular ? 0 : 13
        border.right: border.left
        border.top: border.left
        border.bottom: border.left
        asynchronous: true
        cache: true
    }
}
