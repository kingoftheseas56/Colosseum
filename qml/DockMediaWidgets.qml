pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: widgets
    objectName: "dockMediaWidgets"
    property bool mediaAvailable: true
    property bool reducedMotion: false
    signal wallpaperRequested()
    signal chooseFilesRequested()
    signal filesDropped(var urls)

    Theme { id: theme }

    Item {
        id: wallpaper
        width: Math.max(0, (parent.width - 24) * .57)
        height: parent.height
        property int current: 0
        readonly property var images: ["captured-motion.jpg", "cold-ripple.jpg", "sagas/dune.jpg"]

        // The images are cropped into the wallpaper surface itself.
        component Preview: RoundedPosterImage {
            required property string modelData
            required property int index
            anchors.fill: parent
            sources: [Qt.resolvedUrl("../assets/wallpaper/" + modelData)]
            radius: 13
            revealDuration: 0
            opacity: index === wallpaper.current ? 1 : 0
            visible: opacity > 0
            z: index === wallpaper.current ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: widgets.reducedMotion ? 0 : 950 }
            }
        }
        Repeater { model: wallpaper.images; delegate: Preview {} }
        HoverHandler { id: wallpaperHover }
        KeyboardAction {
            id: wallpaperInput
            objectName: "dockWallpaperArtwork"
            anchors.fill: parent
            z: 2
            accessibleName: qsTr("Change wallpaper")
            tooltipText: accessibleName
            focusRadius: 13
            onTriggered: widgets.wallpaperRequested()
        }
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            z: 2
            width: 30; height: 30; radius: 15
            color: "#a0181821"
            Text { anchors.centerIn: parent; text: "↗"; color: theme.ink; font.pixelSize: 16 }
        }
        Timer {
            running: wallpaper.visible && !widgets.reducedMotion && !wallpaperHover.hovered && !wallpaperInput.activeFocus
            repeat: true
            interval: 6500
            onTriggered: wallpaper.current = (wallpaper.current + 1) % wallpaper.images.length
        }
    }

    Item {
        id: media
        anchors.left: wallpaper.right
        anchors.leftMargin: 24
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Column {
            anchors.centerIn: parent
            spacing: 17
            opacity: widgets.mediaAvailable ? 1 : .5
            Item {
                width: Math.min(media.width, 200)
                height: 111
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: 144
                    height: 19
                    radius: 10
                    color: drop.containsDrag ? "#48f0c44a" : "#1cf0c44a"
                    border.color: "#55f0c44a"
                }
                Repeater {
                    model: [
                        { icon: "comic-book.svg", angle: -15, dx: -29, dz: 0 },
                        { icon: "book-library.svg", angle: 15, dx: 29, dz: 0 },
                        { icon: "projector-theatre.svg", angle: 0, dx: 0, dz: 1 }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        width: 57; height: 78
                        x: (parent.width - width) / 2 + modelData.dx
                        y: modelData.dz ? 1 : 11
                        z: modelData.dz
                        rotation: modelData.angle
                        radius: 7
                        color: modelData.dz ? "#302e38" : "#22212b"
                        border.color: modelData.dz ? "#a3f0c44a" : "#44ffffff"
                        Image {
                            width: 24; height: 24
                            anchors.centerIn: parent
                            sourceSize: Qt.size(48, 48)
                            source: "../assets/icons/" + parent.modelData.icon
                        }
                    }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Drop a file")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 19
            }
        }
        KeyboardAction {
            objectName: "dockDropArtwork"
            anchors.fill: parent
            enabled: widgets.mediaAvailable
            accessibleName: qsTr("Drop or choose files")
            tooltipText: qsTr("Choose files")
            focusRadius: 13
            onTriggered: widgets.chooseFilesRequested()
        }
        DropArea {
            id: drop
            anchors.fill: parent
            enabled: widgets.mediaAvailable
            keys: ["text/uri-list"]
            onDropped: event => {
                event.accepted = event.hasUrls && event.urls.length > 0
                if (event.accepted) widgets.filesDropped(event.urls)
            }
        }
    }
}
