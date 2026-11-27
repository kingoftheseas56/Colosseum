pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import ".."

Glass {
    id: root
    objectName: "vinylHomeWidget"

    property bool inViewport: true
    property bool reducedMotion: false
    property bool loadingPresentation: false
    property bool cueRequested: false
    property url localArtworkDirectory: ""
    signal clicked()
    signal focusEntered(Item item)

    readonly property bool cued: visible && inViewport
        && (loadingPresentation ? cueRequested : action.hovered || action.activeFocus)
    readonly property bool moving: cued && !reducedMotion
    readonly property real layoutBlend: Math.max(0, Math.min(1, (width - 560) / 620))
    readonly property real layoutScale: Math.min(1, width / 560)
    readonly property var sleeves: [
        { cover: "Music118/v4/dd/5c/e6/dd5ce621-f7d2-f767-7a08-e7a7eaa7870b/00602537526994.rgb.jpg", color: "#68423e" },
        { cover: "Music124/v4/4d/13/ba/4d13bac3-d3d5-7581-2c74-034219eadf2b/081227970949.jpg", color: "#8b816f" },
        { cover: "Music115/v4/e8/43/5f/e8435ffa-b6b9-b171-40ab-4ff3959ab661/886443919266.jpg", color: "#353940" },
        { cover: "Music126/v4/dd/50/c7/dd50c790-99ac-d3d0-5ab8-e3891fb8fd52/634904032463.png", color: "#72513e" },
        { cover: "Music211/v4/48/53/43/485343e3-dd6a-0034-faec-f4b6403f8108/13UMGIM63890.rgb.jpg", color: "#485846" },
        { cover: "Music115/v4/a8/2e/b4/a82eb490-f30a-a321-461a-0383c88fec95/15UMGIM23316.rgb.jpg", color: "#564560" },
        { cover: "Music221/v4/fd/4a/77/fd4a77db-0ebc-d043-41a2-f32fa1bb0fb4/dj.qrikkdwj.jpg", color: "#31313c" }
    ]
    property bool artworkRequested: false

    function dimension(compact: real, wide: real): real {
        return (compact + (wide - compact) * layoutBlend) * layoutScale
    }

    function requestArtwork() {
        if (visible && inViewport)
            artworkRequested = true
    }

    function coverUrl(index: int): url {
        if (localArtworkDirectory.toString().length)
            return localArtworkDirectory.toString() + index + ".jpg"
        return artworkRequested ? "https://is1-ssl.mzstatic.com/image/thumb/"
                                  + sleeves[index].cover + "/352x352bb.jpg" : ""
    }

    onInViewportChanged: requestArtwork()
    onVisibleChanged: requestArtwork()
    Component.onCompleted: requestArtwork()

    implicitHeight: dimension(340, 400)

    Theme { id: theme }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.dimension(20, 27)
        visible: !root.loadingPresentation
        text: qsTr("Vinyl")
        color: theme.ink
        font.family: theme.display
        font.pixelSize: Math.max(20, Math.round(root.dimension(30, 33)))
        Accessible.ignored: true
    }

    Item {
        id: deck
        objectName: "vinylHomeDeck"
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.dimension(92, 105)
        width: root.dimension(524, 934)
        height: root.dimension(187, 244)

        RectangularShadow {
            anchors.fill: parent
            radius: 17
            blur: 19
            offset.y: 9
            color: "#95000000"
        }

        Rectangle {
            x: root.dimension(31, 31)
            y: deck.height - 5
            width: root.dimension(53, 53)
            height: 10
            radius: 4
            color: "#121316"
        }
        Rectangle {
            x: deck.width - width - root.dimension(31, 31)
            y: deck.height - 5
            width: root.dimension(53, 53)
            height: 10
            radius: 4
            color: "#121316"
        }
        Rectangle {
            anchors.fill: parent
            radius: 17
            border.color: "#4f5158"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#35373d" }
                GradientStop { position: 0.34; color: "#292b30" }
                GradientStop { position: 0.69; color: "#202126" }
                GradientStop { position: 1; color: "#27282d" }
            }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            height: 13
            bottomLeftRadius: 16
            bottomRightRadius: 16
            gradient: Gradient {
                GradientStop { position: 0; color: "#141519" }
                GradientStop { position: 0.29; color: "#27292e" }
                GradientStop { position: 1; color: "#17181c" }
            }
        }

        VinylTurntable {
            x: root.dimension(8, 54)
            y: root.dimension(12, 7)
            scale: root.dimension(0.69, 1)
            transformOrigin: Item.TopLeft
            artwork: root.coverUrl(3)
            cued: root.cued
            spinning: root.moving
            animateArm: root.visible && root.inViewport && !root.reducedMotion
            stylusColor: theme.gold
        }

        Rectangle {
            x: root.dimension(234, 450)
            y: root.dimension(126, 191)
            width: root.dimension(271, 440)
            height: 10 * root.layoutScale
            radius: 4
            border.color: "#45474e"
            gradient: Gradient {
                GradientStop { position: 0; color: "#464850" }
                GradientStop { position: 0.4; color: "#24262c" }
                GradientStop { position: 1; color: "#16171b" }
            }
        }
        Item {
            x: root.dimension(235, 452)
            y: root.dimension(18, 15)
            width: 446
            height: 193
            scale: root.dimension(0.61, 1)
            transformOrigin: Item.TopLeft

            Repeater {
                model: root.sleeves
                delegate: Rectangle {
                    id: sleeve
                    required property int index
                    required property var modelData
                    readonly property int offset: index - 3
                    readonly property int distance: Math.abs(offset)
                    x: [33, 68, 103, 145, 193, 228, 263][index]
                    y: [4, 15, 19, 25][distance] - (root.moving ? 5 - distance : 0)
                    z: 4 - distance
                    width: distance === 0 ? 172 : 154 - distance
                    height: width
                    rotation: offset * 5.33
                    transformOrigin: Item.Bottom
                    radius: 3
                    color: "#141518"
                    border.color: "#3f4044"
                    Behavior on y {
                        enabled: root.visible && root.inViewport && !root.reducedMotion
                        NumberAnimation {
                            duration: 440
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: [0.22, 0.68, 0.26, 1, 1, 1]
                        }
                    }

                    Rectangle {
                        x: 2; y: 3
                        width: sleeve.width + 1; height: sleeve.height + 2
                        radius: 4; color: "#90000000"; z: -1
                    }
                    VinylArtwork {
                        objectName: sleeve.offset === 0 ? "vinylHomeFeaturedSleeve" : ""
                        anchors.fill: parent
                        anchors.margins: 1
                        source: root.coverUrl(sleeve.index)
                        backgroundColor: sleeve.modelData.color
                        showPlaceholder: sleeve.offset === 0
                        crop: true
                    }
                }
            }
        }

        Column {
            x: root.dimension(376, 683) - width / 2
            y: root.dimension(143, 207)
            width: 220 * root.layoutScale
            Accessible.ignored: true
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: "In Rainbows"
                color: theme.ink
                font.family: theme.ui
                font.weight: Font.DemiBold
                font.pixelSize: Math.max(10, theme.fontSize(13) * root.layoutScale)
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: "Radiohead"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: Math.max(9, theme.fontSize(11) * root.layoutScale)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: root.radius - 2
        color: "transparent"
        border.width: 2
        border.color: theme.ink
        visible: action.activeFocus
        Accessible.ignored: true
    }

    KeyboardAction {
        id: action
        objectName: "vinylHomeOpenAction"
        anchors.fill: parent
        enabled: !root.loadingPresentation
        accessibleName: qsTr("Open Vinyl")
        accessibleDescription: qsTr("Featured album: In Rainbows by Radiohead.")
        focusOnPointer: false
        showFocusFrame: false
        spaceActivates: true
        onActiveFocusChanged: if (activeFocus) root.focusEntered(action)
        onTriggered: root.clicked()
    }
}
