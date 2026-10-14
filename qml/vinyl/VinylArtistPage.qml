import QtQuick
import QtQuick.Controls
import ".."

Flickable {
    id: root
    objectName: "vinylArtistPage"
    required property var artistData
    required property var worldData
    signal albumRequested(var album)
    signal songRequested(var song)
    signal artistRequested(string name)
    property real gutter: width < 1100 ? 36 : 54
    readonly property real bodyWidth: width - gutter * 2
    readonly property int columns: bodyWidth < 1100 ? 4 : 6
    contentWidth: width
    contentHeight: body.height + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Theme { id: theme }
    ScrollGlide { flick: root }
    ScrollBar.vertical: HouseScrollBar { flick: root }

    Column {
        id: body
        x: root.gutter
        width: root.bodyWidth
        spacing: 24
        Item {
            width: parent.width
            height: root.width < 1400 ? 160 : 208
            Column {
                width: parent.width * 0.63
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                Text {
                    text: "ARTIST"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.letterSpacing: 3
                }
                Text {
                    objectName: "vinylArtistName"
                    width: parent.width
                    text: root.artistData.record.name
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: root.width < 1100 ? 44 : 58
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Row {
                    spacing: 20
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.artistData.record.releases.length > 0
                        text: root.artistData.record.releases.length + (root.artistData.record.releases.length === 1 ? " release  ·  " : " releases  ·  ")
                              + root.artistData.record.songs.length + (root.artistData.record.songs.length === 1 ? " song" : " songs")
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                    KeyboardAction {
                        objectName: "vinylArtistSave"
                        width: 112
                        height: 34
                        accessibleName: root.artistData.saved ? "Remove saved artist" : "Save artist"
                        onTriggered: root.artistData.toggleSaved()
                        Rectangle {
                            anchors.fill: parent
                            radius: 17
                            color: parent.interactionActive ? theme.glassHi : theme.glassTint
                            border.color: root.artistData.saved ? theme.gold : theme.edge
                        }
                        Text {
                            anchors.centerIn: parent
                            text: root.artistData.saved ? "✓  Saved" : "+  Save artist"
                            color: root.artistData.saved ? theme.gold : theme.ink
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                }
            }
            // The artist's work is the portrait: three records, no artist photo.
            Item {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.33
                height: parent.height - 12
                Repeater {
                    model: root.artistData.record.releases.slice(0, 3).reverse()
                    delegate: Image {
                        required property var modelData
                        required property int index
                        width: Math.min(parent.height - 24, parent.width * 0.58)
                        height: width
                        x: parent.width - width - (2 - index) * 42
                        y: 12 + (2 - index) * 4
                        rotation: (index - 2) * -7
                        source: root.worldData.artwork(modelData.album.cover)
                        sourceSize.width: 420
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                    }
                }
            }
        }
        Column {
            width: parent.width
            spacing: 20
            visible: root.artistData.record.releases.length > 0
            Row {
                width: parent.width
                height: 36
                Text {
                    width: parent.width - filters.width - sort.width - 24
                    text: "Discography"
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 28
                }
                Row {
                    id: filters
                    spacing: 6
                    Repeater {
                        model: root.artistData.filters
                        delegate: KeyboardAction {
                            required property var modelData
                            objectName: "vinylArtistFilter_" + modelData.key
                            width: filterText.implicitWidth + 24
                            height: 34
                            accessibleName: modelData.label
                            onTriggered: root.artistData.filter = modelData.key
                            Rectangle {
                                anchors.fill: parent
                                radius: 17
                                color: root.artistData.filter === modelData.key ? theme.glassHi : "transparent"
                                border.color: root.artistData.filter === modelData.key ? theme.edge : "transparent"
                            }
                            Text {
                                id: filterText
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.artistData.filter === modelData.key ? theme.gold : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 12
                            }
                        }
                    }
                }
                KeyboardAction {
                    id: sort
                    objectName: "vinylArtistSort"
                    width: 126
                    height: 34
                    accessibleName: root.artistData.newestFirst ? "Sort oldest first" : "Sort newest first"
                    onTriggered: root.artistData.newestFirst = !root.artistData.newestFirst
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.artistData.newestFirst ? "Newest first ↓" : "Oldest first ↑"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
            }
            Grid {
                width: parent.width
                columns: root.columns
                columnSpacing: 22
                rowSpacing: 22
                Repeater {
                    model: root.artistData.releases
                    delegate: KeyboardAction {
                        required property var modelData
                        required property int index
                        objectName: "vinylArtistAlbum_" + index
                        width: (root.bodyWidth - (root.columns - 1) * 22) / root.columns
                        height: width + 66
                        accessibleName: modelData.album.title + ", " + modelData.album.year
                        onTriggered: root.albumRequested(modelData.album)
                        RoundedPosterImage {
                            objectName: "vinylArtistAlbumArt_" + index
                            width: parent.width
                            height: width
                            radius: 10
                            sources: [root.worldData.artwork(modelData.album.cover)]
                            hovered: parent.interactionActive
                        }
                        Text {
                            y: parent.width + 12
                            width: parent.width
                            text: modelData.album.title
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            y: parent.width + 36
                            text: modelData.album.year + (modelData.credit === "track" ? "  ·  Appears on" : "")
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
        Column {
            width: parent.width
            spacing: 14
            visible: root.artistData.songs.length > 0
            Text {
                text: "Songs"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 28
            }
            Repeater {
                model: root.artistData.songs
                delegate: Row {
                    required property var modelData
                    required property int index
                    width: parent.width
                    height: 54
                    spacing: 18
                    KeyboardAction {
                        objectName: "vinylArtistSong_" + index
                        width: parent.width * 0.59
                        height: parent.height
                        accessibleName: "Play " + modelData.track.title
                        onTriggered: root.songRequested(modelData)
                        Rectangle { anchors.fill: parent; radius: 6; color: theme.glassTint; visible: parent.interactionActive }
                        Text {
                            x: 8; anchors.verticalCenter: parent.verticalCenter
                            text: index + 1; color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12
                        }
                        Column {
                            x: 40; width: parent.width - 52; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                            Text {
                                width: parent.width; text: modelData.track.title; elide: Text.ElideRight
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                            }
                            Text {
                                width: parent.width; text: (modelData.track.artists || []).join(" · "); elide: Text.ElideRight
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                            }
                        }
                    }
                    KeyboardAction {
                        objectName: "vinylArtistSongAlbum_" + index
                        width: parent.width * 0.31
                        height: parent.height
                        accessibleName: "Open " + modelData.album.title
                        onTriggered: root.albumRequested(modelData.album)
                        Text {
                            width: parent.width; anchors.verticalCenter: parent.verticalCenter
                            text: modelData.album.title; elide: Text.ElideRight
                            color: parent.interactionActive ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.floor(modelData.track.seconds / 60) + ":" + ("0" + modelData.track.seconds % 60).slice(-2)
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    }
                }
            }
            KeyboardAction {
                objectName: "vinylArtistAllSongs"
                width: 150; height: 34
                visible: root.artistData.record.songs.length > 6
                accessibleName: root.artistData.allSongs ? "Show fewer songs" : "Show all songs"
                onTriggered: root.artistData.allSongs = !root.artistData.allSongs
                Text {
                    anchors.verticalCenter: parent.verticalCenter; text: parent.accessibleName
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                }
            }
        }
        Column {
            width: parent.width
            spacing: 14
            visible: root.artistData.record.collaborators.length > 0
            Text {
                text: "Shared credits"
                color: theme.ink; font.family: theme.display; font.pixelSize: 28
            }
            Flow {
                width: parent.width; spacing: 10
                Repeater {
                    model: root.artistData.record.collaborators
                    delegate: KeyboardAction {
                        required property string modelData
                        objectName: "vinylArtistCredit_" + modelData
                        width: creditText.implicitWidth + 28; height: 38
                        accessibleName: modelData
                        onTriggered: root.artistRequested(modelData)
                        Rectangle { anchors.fill: parent; radius: 19; color: parent.interactionActive ? theme.glassHi : theme.glassTint }
                        Text {
                            id: creditText
                            anchors.centerIn: parent; text: modelData
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }
}
