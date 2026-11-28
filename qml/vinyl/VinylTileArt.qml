pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import ".."

// Mask only the artwork so the background stays visible outside its silhouette.
Item {
    id: art
    property url artworkSource: ""
    property var artworkSources: []
    property string title: ""
    property bool circular: false
    property bool artworkPreviouslyReady: false
    signal artworkReady(url source, size decodeSize)
    property string displayFont: theme.display
    property color surfaceColor: circular ? theme.vaultSurface : "transparent"
    property real cornerRadius: 10
    readonly property bool softwareRendering: art.GraphicsInfo.api === GraphicsInfo.Software
    readonly property real artRadius: circular ? width / 2 : cornerRadius
    Theme { id: theme }
    readonly property string initials: title.trim().split(/\s+/).slice(0, 2)
        .map(word => word.charAt(0)).join("").toUpperCase()
    readonly property bool collage: artworkSources.length > 0
    readonly property bool artworkSettled: first.status === Image.Ready || first.status === Image.Error
    implicitWidth: 196
    implicitHeight: width

    component Cover: Image {
        fillMode: Image.PreserveAspectCrop
        clip: true
        asynchronous: true
        cache: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        visible: status === Image.Ready
    }

    Rectangle {
        id: cropMask
        anchors.fill: parent
        radius: art.artRadius
        visible: false
        layer.enabled: covers.layer.enabled
    }
    Item {
        id: covers
        anchors.fill: parent
        // The software backend cannot run ShaderEffects; keep artwork visible there.
        layer.enabled: !art.softwareRendering && art.visible && (art.collage || first.visible)
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: cropMask
            autoPaddingEnabled: false
        }
        Cover {
            id: first
            objectName: "vinylArtworkImage"
            width: art.collage ? art.width / 2 : art.width
            height: art.collage ? art.height / 2 : art.height
            source: art.collage ? art.artworkSources[0] : art.artworkSource
            // Keep the same portrait through a resize, never the outgoing
            // artist's pixels while a recycled delegate loads a different URL.
            property string decodedSource: ""
            readonly property bool samePortrait: art.circular && !art.collage && decodedSource.length > 0
                && decodedSource === source.toString()
            retainWhileLoading: samePortrait
            visible: status === Image.Ready || (status === Image.Loading && samePortrait)
            onStatusChanged: if (status === Image.Ready && art.circular && !art.collage) {
                decodedSource = source.toString();
                art.artworkReady(source, sourceSize);
            }
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
    }
    Rectangle {
        objectName: "vinylArtworkFallback"
        anchors.fill: parent
        visible: art.surfaceColor.a > 0 && !first.visible && !art.collage
        radius: art.artRadius
        color: art.surfaceColor
        border.width: 0
    }
    Text {
        objectName: art.circular ? "vinylArtistInitials" : "vinylArtworkPlaceholder"
        anchors.centerIn: parent
        visible: !art.collage && !first.visible && !first.samePortrait && !art.artworkPreviouslyReady
        text: art.circular && art.initials ? art.initials : "♪"
        color: theme.inkDimmer
        font.family: art.displayFont
        font.pixelSize: Math.round(Math.min(art.width, art.height) * 0.23)
    }
    Rectangle {
        objectName: "vinylPortraitEdge"
        anchors.fill: parent
        visible: art.circular
        radius: art.artRadius
        color: "transparent"
        border.width: 1
        border.color: theme.edge
    }
}
