import QtQuick
import ".."

Item {
    id: root
    required property var artistData
    signal createPlaylistRequested()
    signal itemFocused(var item)
    readonly property bool sourcedContext: artistData.context.sources.length > 0
    readonly property bool portraitReady: portrait.status === Image.Ready
    implicitHeight: Math.max(224, identity.implicitHeight + 32)
    height: implicitHeight
    Theme { id: theme }

    Item {
        anchors.right: parent.right; width: parent.width * 0.64; height: parent.height
        clip: true
        Image {
            id: portrait
            objectName: "vinylArtistPortrait"
            anchors.fill: parent
            source: root.artistData.artwork(root.artistData.media.portrait.cover)
            sourceSize.width: 1440
            fillMode: Image.PreserveAspectCrop
            horizontalAlignment: Image.AlignHCenter
            asynchronous: true
            opacity: root.portraitReady ? 0.85 : 0
        }
        Rectangle {
            anchors.fill: parent
            visible: root.portraitReady
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#ff06070b" }
                GradientStop { position: 0.48; color: "#3006070b" }
                GradientStop { position: 1; color: "#0006070b" }
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: root.portraitReady
            gradient: Gradient {
                GradientStop { position: 0; color: "#3006070b" }
                GradientStop { position: 0.72; color: "#0006070b" }
                GradientStop { position: 1; color: "#ee06070b" }
            }
        }
        RoundedPosterImage {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: Math.min(220, parent.width * 0.62); height: width
            radius: 10; rotation: 4
            visible: !root.portraitReady && root.artistData.record.releases.length > 0
            sources: root.artistData.record.releases.length ? [root.artistData.artwork(root.artistData.record.releases[0].album.cover)] : []
        }
    }
    Column {
        id: identity
        y: 16; width: root.width * (root.width < 720 ? 0.66 : 0.58)
        spacing: 12
        Text {
            width: parent.width
            text: root.sourcedContext ? root.artistData.context.roles.toUpperCase() : "ARTIST"
            textFormat: Text.PlainText; color: theme.gold; font.family: theme.ui
            font.pixelSize: 10; font.letterSpacing: 1.8; wrapMode: Text.WordWrap
        }
        Text {
            objectName: "vinylArtistName"; width: parent.width
            text: root.artistData.record.name; textFormat: Text.PlainText
            color: theme.ink; font.family: theme.display
            font.pixelSize: root.width < 900 ? 48 : 68; wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width; visible: text.length > 0
            text: root.sourcedContext ? root.artistData.context.blurb : ""
            textFormat: Text.PlainText; color: theme.inkDim; font.family: theme.ui
            font.pixelSize: 13; lineHeight: 1.3; wrapMode: Text.WordWrap
            maximumLineCount: 3; elide: Text.ElideRight
        }
        Flow {
            width: parent.width; spacing: 12
            KeyboardAction {
                id: save
                objectName: "vinylArtistSave"; width: 120; height: 36
                accessibleName: root.artistData.saved ? "Remove saved artist" : "Save artist"
                onTriggered: root.artistData.toggleSaved()
                onActiveFocusChanged: if (activeFocus) root.itemFocused(save)
                Rectangle { anchors.fill: parent; radius: 18; color: parent.interactionActive ? theme.glassHi : theme.glassTint; border.color: root.artistData.saved ? theme.gold : theme.edge }
                Text { anchors.centerIn: parent; text: root.artistData.saved ? "✓  Saved" : "+  Save artist"; color: root.artistData.saved ? theme.gold : theme.ink; font.family: theme.ui; font.pixelSize: 12 }
            }
            KeyboardAction {
                id: create
                objectName: "vinylArtistCreatePlaylist"; width: 128; height: 36
                visible: root.artistData.songs.length > 0; accessibleName: "Create playlist"
                onTriggered: root.createPlaylistRequested()
                onActiveFocusChanged: if (activeFocus) root.itemFocused(create)
                Text { anchors.centerIn: parent; text: "+ Create playlist"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12 }
            }
        }
    }
}
