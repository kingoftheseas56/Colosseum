import QtQuick
import QtQuick.Effects
import ".."

Item {
    id: root
    objectName: "vinylBackdrop"
    property bool wordsShown: false
    property bool tallFixture: false
    property url source
    property var contentStore: typeof Content !== "undefined" ? Content : null
    readonly property string fallbackSource: {
        if (root.contentStore) {
            const remote = (root.contentStore.generation,
                            root.contentStore.assetUrl("assets/wallpaper/cold-ripple.jpg"))
            if (remote) return remote
        }
        return Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg")
    }
    property bool artistSkinActive: false
    property url artistLayerASource: ""
    property url artistLayerBSource: ""
    property real artistLayerAFocalX: 0.5
    property real artistLayerAFocalY: 0.4
    property real artistLayerBFocalX: 0.5
    property real artistLayerBFocalY: 0.4
    property int artistActiveLayer: 0
    property int crossfadeDuration: 1350
    property bool reducedMotion: false
    readonly property bool artistLayerAReady: skinImgA.status === Image.Ready && skinImgA.source === artistLayerASource
    readonly property bool artistLayerBReady: skinImgB.status === Image.Ready && skinImgB.source === artistLayerBSource

    Theme {
        id: theme
    }
    Rectangle {
        anchors.fill: parent
        color: "#05060a"
    }

    // Standard Album / Song / World single-source backdrop
    Item {
        id: standardBackdrop
        anchors.fill: parent
        visible: !root.artistSkinActive
        Image {
            id: art
            anchors.fill: parent
            anchors.margins: -60
            source: root.source || root.fallbackSource
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: art
            blurEnabled: true
            blur: 1
            blurMax: root.tallFixture ? 24 : 64
            blurMultiplier: 0.5
            brightness: 0
            saturation: root.tallFixture ? -0.12 : -0.24
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: Qt.rgba(theme.biblioWashBottom.r, theme.biblioWashBottom.g, theme.biblioWashBottom.b, root.wordsShown ? 0.60 : 0.42)
                }
                GradientStop {
                    position: 1
                    color: Qt.rgba(theme.biblioWashBottom.r, theme.biblioWashBottom.g, theme.biblioWashBottom.b, root.wordsShown ? 0.84 : 0.76)
                }
            }
        }
    }

    // Synchronized dual-buffered full-viewport blurred artist skin (.page-skin parity).
    // Blur radius remains fixed during the 1.35s opacity transition.
    Item {
        id: artistSkin
        objectName: "vinylBackdropArtistSkin"
        anchors.fill: parent
        visible: root.artistSkinActive

        Item {
            id: skinSourceA
            anchors.fill: parent
            anchors.margins: -72
            clip: true
            visible: false
            Image {
                id: skinImgA
                objectName: "vinylBackdropSkinImgA"
                readonly property real imageRatio: implicitHeight > 0 ? implicitWidth / implicitHeight : 16 / 9
                width: Math.max(parent.width, parent.height * imageRatio)
                height: Math.max(parent.height, parent.width / imageRatio)
                x: (parent.width - width) * root.artistLayerAFocalX
                y: (parent.height - height) * root.artistLayerAFocalY
                source: root.artistLayerASource || root.source || root.fallbackSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: false
            }
        }
        MultiEffect {
            id: skinBlurA
            objectName: "vinylBackdropSkinBlurA"
            anchors.fill: parent
            source: skinSourceA
            blurEnabled: true
            blur: 1.0
            blurMax: 64
            blurMultiplier: 0.65
            brightness: 0.02
            saturation: 0.14
            opacity: root.artistActiveLayer === 0 ? 1.0 : 0.0
            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : root.crossfadeDuration
                    easing.type: Easing.InOutQuad
                }
            }
        }

        Item {
            id: skinSourceB
            anchors.fill: parent
            anchors.margins: -72
            clip: true
            visible: false
            Image {
                id: skinImgB
                objectName: "vinylBackdropSkinImgB"
                readonly property real imageRatio: implicitHeight > 0 ? implicitWidth / implicitHeight : 16 / 9
                width: Math.max(parent.width, parent.height * imageRatio)
                height: Math.max(parent.height, parent.width / imageRatio)
                x: (parent.width - width) * root.artistLayerBFocalX
                y: (parent.height - height) * root.artistLayerBFocalY
                source: root.artistLayerBSource || root.artistLayerASource || root.source || root.fallbackSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: false
            }
        }
        MultiEffect {
            id: skinBlurB
            objectName: "vinylBackdropSkinBlurB"
            anchors.fill: parent
            source: skinSourceB
            blurEnabled: true
            blur: 1.0
            blurMax: 64
            blurMultiplier: 0.65
            brightness: 0.02
            saturation: 0.14
            opacity: root.artistActiveLayer === 1 ? 1.0 : 0.0
            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : root.crossfadeDuration
                    easing.type: Easing.InOutQuad
                }
            }
        }

        // Approved HTML .page-skin::after wash: preserves rich visible blurred artist photo
        // behind the entire scrollable page while keeping dark contrast grounded.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.016, 0.020, 0.031, 0.10) }
                GradientStop { position: 0.45; color: Qt.rgba(0.016, 0.020, 0.031, 0.24) }
                GradientStop { position: 1.0; color: Qt.rgba(0.016, 0.020, 0.031, 0.38) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(0.016, 0.020, 0.031, 0.32) }
                GradientStop { position: 0.56; color: Qt.rgba(0.016, 0.020, 0.031, 0.14) }
                GradientStop { position: 1.0; color: Qt.rgba(0.016, 0.020, 0.031, 0.28) }
            }
        }
    }
}
