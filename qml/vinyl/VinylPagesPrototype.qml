import QtQuick
import QtQuick.Controls
import ".."

Window {
    id: root
    objectName: "vinylPrototypeWindow"
    title: "Colosseum · Vinyl prototype"
    width: 1280
    height: 720
    visible: true
    property string page: "album"
    property bool ready: false
    property bool developerStrip: true
    property alias pageData: pageData
    // Lanista resolves QQuickItems rather than Window/QObject roots.
    Item {
        objectName: "vinylPrototype"
        anchors.fill: parent
        property alias ready: root.ready
        property alias page: root.page
    }
    Theme {
        id: theme
    }
    FontLoader {
        source: "../../assets/fonts/Fraunces-Regular.ttf"
    }
    VinylPageData {
        id: pageData
        artworkRoot: Qt.resolvedUrl("../../tests/fixtures/vinyl-pages")
    }
    VinylBackdrop {
        id: backdrop
        anchors.fill: parent
        source: pageData.artwork(root.page === "album" ? pageData.shownEdition.cover : (pageData.playingEdition.backdrop || pageData.playingEdition.cover))
        wordsShown: root.page === "song" && pageData.lyricsShown
        tallFixture: root.page === "song" && pageData.playingEdition.id === "mounamela"
    }
    Item {
        id: header
        x: theme.margin
        y: 30
        width: root.width - theme.margin * 2
        height: 55
        Text {
            x: 68
            text: Qt.formatTime(new Date(), "hh:mm")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 32
        }
        Text {
            x: 68
            y: 38
            text: Qt.formatDate(new Date(), "dddd, d MMMM")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
        }
        KeyboardAction {
            width: 55
            height: 45
            visible: root.page === "album"
            accessibleName: "Open song"
            onTriggered: root.page = "song"
            Text {
                anchors.centerIn: parent
                text: "‹ Vinyl"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
            }
        }
        Glass {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 520
            height: 46
            y: 5
            radius: 24
            backdrop: backdrop
            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: ["Tankoban", "Biblio", "Theatre", "Vinyl", "Universes", "Feria"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 80
                        height: 36
                        radius: 18
                        color: modelData === "Vinyl" ? theme.gold : Qt.rgba(0, 0, 0, 0)
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData
                            color: parent.modelData === "Vinyl" ? theme.biblioWashBottom : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                }
            }
        }
    }
    VinylAlbumPage {
        id: album
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 110
            bottom: bar.top
            bottomMargin: 20
        }
        pageData: pageData
        backdrop: backdrop
        visible: root.page === "album"
        onVideoRequested: track => {
            pageData.playTrack(track);
            pageData.videoShown = true;
            root.page = "song";
        }
    }
    VinylSongPage {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 108
            bottom: bar.top
            bottomMargin: 42
        }
        visible: root.page === "song"
        pageData: pageData
        onBackRequested: {
            root.page = "album";
            album.forceActiveFocus();
        }
        onAlbumRequested: {
            pageData.selectEdition(pageData.playingEditionId);
            root.page = "album";
        }
    }
    VinylCreditsPopup {
        id: creditsPopup
        parent: root.contentItem
        x: root.width - width - theme.margin
        y: bar.y - height - 12
        pageData: pageData
        backdrop: backdrop
        onClosed: bar.forceActiveFocus()
    }
    VinylPlayerBar {
        id: bar
        x: theme.margin
        y: root.height - height - 18
        width: root.width - theme.margin * 2
        pageData: pageData
        backdrop: backdrop
        page: root.page
        onSongRequested: root.page = "song"
        onCreditsRequested: creditsPopup.opened ? creditsPopup.close() : creditsPopup.open()
    }
    Flow {
        id: dev
        visible: root.developerStrip
        x: 8
        y: 2
        width: root.width - 16
        spacing: 3
        z: 100
        Repeater {
            model: [["PageAlbum", "Album"], ["PageSong", "Song"], ["FixtureThriller", "Thriller"], ["FixtureWithoutMe", "Without Me"], ["FixtureMounamela", "Mounamela"], ["SourcePlaying", "Playing"], ["SourceFinding", "Finding"], ["SourceRetrying", "Retrying"], ["Freeze", "Freeze"], ["Size720", "1280×720"], ["Size900", "1600×900"], ["Clean", "Hide controls"]]
            delegate: Button {
                required property var modelData
                objectName: "vinylDev" + modelData[0]
                text: modelData[1]
                height: 22
                font.pixelSize: 10
                onClicked: {
                    var action = modelData[0];
                    if (action === "PageAlbum")
                        root.page = "album";
                    else if (action === "PageSong")
                        root.page = "song";
                    else if (action === "FixtureThriller")
                        pageData.setFixture("thriller");
                    else if (action === "FixtureWithoutMe")
                        pageData.setFixture("without-me");
                    else if (action === "FixtureMounamela")
                        pageData.setFixture("mounamela");
                    else if (action === "SourcePlaying")
                        pageData.setSourceState("playing");
                    else if (action === "SourceFinding")
                        pageData.setSourceState("finding");
                    else if (action === "SourceRetrying")
                        pageData.setSourceState("retrying");
                    else if (action === "Freeze")
                        pageData.frozen = !pageData.frozen;
                    else if (action === "Size720") {
                        root.width = 1280;
                        root.height = 720;
                    } else if (action === "Size900") {
                        root.width = 1600;
                        root.height = 900;
                    } else
                        root.developerStrip = false;
                }
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: root.developerStrip = !root.developerStrip
    }
    onFrameSwapped: ready = true
}
