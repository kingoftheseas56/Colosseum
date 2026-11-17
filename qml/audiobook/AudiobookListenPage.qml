import QtQuick
import QtQuick.Controls
import ".."
import "../vinyl"
import "AudiobookListenPageData.js" as Data

// Vinyl’s song room hosts the audiobook controls and chapter list.
Item {
    id: root
    objectName: "audiobookListenPage"
    required property var session
    // title, authors[], narrators[], cover, description, seriesLine. Every field is optional.
    property var book: ({})
    signal backRequested()

    readonly property alias pageData: pd
    property bool chaptersOpen: false
    readonly property bool chaptersBesideRoom: width >= 1120 && chaptersPanel.visible
    readonly property int chapterIndex: session && session.ready && hasChapters ? session.chapterIndex : -1
    readonly property var chapterList: session && session.chapterModel ? session.chapterModel : []
    readonly property string bookTitle: Data.text(book && book.title)
    readonly property string description: Data.text(book && book.description).trim()
    readonly property string seriesLine: Data.text(book && book.seriesLine).trim()
    readonly property string narratorLine: Data.list(book && book.narrators).join(" · ")
    readonly property bool hasNotes: description.length > 0 || seriesLine.length > 0
    readonly property bool hasChapters: chapterList.length > 0

    onHasChaptersChanged: if (!hasChapters) chaptersOpen = false

    Theme { id: theme }

    QtObject {
        id: pd
        readonly property var playingTrack: ({ title: root.bookTitle, artists: playingEdition.artists })
        readonly property var playingEdition: Data.edition(root.book)
        function artwork(file) { return Data.coverUrl(file) }

        // Vinyl's instantiated song-room children still read these values.
        readonly property bool lyricsShown: false
        readonly property bool autoNotes: false
        readonly property bool videoShown: false
        readonly property bool hasLyrics: false
        readonly property string lyricsSource: ""
        readonly property string lyricsDocumentId: ""
        readonly property string lyricsStatus: ""
        readonly property bool timedLyrics: false
        readonly property var lyrics: []
        readonly property int currentLineIndex: -1
        readonly property var notes: ({})
        readonly property int activeNoteLine: -1
        readonly property int manualNoteLine: -1
        readonly property string annotationsStatus: ""
        readonly property string readingContextKey: ""
    }

    VinylBackdrop {
        anchors.fill: parent
        source: Data.coverUrl(root.book && root.book.cover)
    }

    VinylSongPage {
        id: songPage
        objectName: "audiobookListenSong"
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: bar.top
                  topMargin: 8; bottomMargin: 12
                  rightMargin: root.chaptersBesideRoom ? 110 + Math.max(0, 1280 - root.width) : 0 }
        pageData: pd
        readersEnabled: false
        songActionsEnabled: false
        albumLinkEnabled: false
        onBackRequested: root.backRequested()
    }

    Item {
        id: slots
        x: songPage.x
        y: songPage.y
        width: songPage.width
        height: songPage.height

        // Narrator line, only when the book supplies narrators.
        Text {
            objectName: "audiobookNarrators"
            visible: root.narratorLine.length > 0
            text: qsTr("Narrated by %1").arg(root.narratorLine)
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            width: Math.min(420, slots.width - 40)
            x: (slots.width - width) / 2
            y: songPage.artworkTop + songPage.cardHeight + 84
        }

        // Description and series: the notes column's place. Absent fields leave it hidden.
        Rectangle {
            id: notesPanel
            objectName: "audiobookNotes"
            visible: root.hasNotes && !chaptersPanel.visible && songPage.readerWidth > 0
            x: songPage.notesAreaX
            y: songPage.artworkTop
            width: Math.min(songPage.readerWidth, 311)
            height: Math.max(0, slots.height - y - 8)
            radius: 14
            color: theme.glassTint
            border.color: theme.edge
            clip: true
            Flickable {
                anchors.fill: parent
                anchors.margins: 14
                contentHeight: notesColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: notesColumn
                    width: parent.width - 8
                    spacing: 10
                    Text {
                        objectName: "audiobookSeriesLine"
                        visible: root.seriesLine.length > 0
                        width: parent.width
                        text: root.seriesLine
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        objectName: "audiobookDescription"
                        visible: root.description.length > 0
                        width: parent.width
                        text: root.description
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        lineHeight: 1.25
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    AudiobookControlBar {
        id: bar
        anchors.horizontalCenter: songPage.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.min(126, Math.max(18, root.height - 510))
        width: Math.min(root.chaptersBesideRoom ? 685 : implicitWidth, Math.max(0, root.width - 2 * theme.margin))
        height: implicitHeight
        session: root.session
        compact: false
        onChaptersRequested: if (root.hasChapters) root.chaptersOpen = !root.chaptersOpen
    }

    Rectangle {
        id: chaptersPanel
        objectName: "audiobookChapters"
        visible: root.chaptersOpen && root.hasChapters
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: songPage.y + songPage.artworkTop + 3
        anchors.rightMargin: 32
        anchors.bottomMargin: 55
        width: Math.min(311, root.width - 64)
        radius: 19
        color: "#f2141720"
        border.color: "#36ffffff"
        clip: true
        Keys.onEscapePressed: root.chaptersOpen = false
        onVisibleChanged: {
            if (visible && root.chapterIndex >= 0)
                chapterView.positionViewAtIndex(root.chapterIndex, ListView.Center)
        }

        Column {
            x: 22
            y: 22
            width: parent.width - 84
            spacing: 5
            Text {
                width: parent.width
                text: root.bookTitle.toUpperCase()
                elide: Text.ElideRight
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 10
                font.letterSpacing: 1.2
            }
            Text {
                text: qsTr("Chapters")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 26
            }
        }

        KeyboardAction {
            id: closeChapters
            objectName: "audiobookCloseChapters"
            x: parent.width - width - 17
            y: 23
            width: 32
            height: 32
            focusRadius: 12
            accessibleName: qsTr("Close chapters")
            tooltipText: accessibleName
            onTriggered: root.chaptersOpen = false

            Rectangle {
                anchors.fill: parent
                radius: 12
                color: closeChapters.interactionActive ? theme.glassHi : theme.glassTint
            }
            Text {
                anchors.centerIn: parent
                text: "×"
                color: theme.ink
                font.pixelSize: 20
            }
        }

        Rectangle {
            x: 1
            y: 92
            width: parent.width - 2
            height: 1
            color: theme.edge
        }

        ListView {
            id: chapterView
            objectName: "audiobookChapterList"
            x: 11; y: 105
            width: parent.width - 22
            height: Math.max(0, parent.height - y - 12)
            clip: true
            model: root.chapterList
            boundsBehavior: Flickable.StopAtBounds
            Connections {
                target: root
                function onChapterIndexChanged() {
                    if (chaptersPanel.visible && root.chapterIndex >= 0) chapterView.positionViewAtIndex(root.chapterIndex, ListView.Contain)
                }
            }
            ScrollBar.vertical: ScrollBar { }
            delegate: KeyboardAction {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: index === root.chapterIndex
                objectName: "audiobookChapter_" + index
                width: chapterView.width
                height: 44
                focusOnPointer: false
                focusRadius: 8
                accessibleName: Data.text(modelData.label)
                accessibleDescription: current ? qsTr("Playing now") : ""
                enabled: root.session.ready
                onTriggered: {
                    // Selecting the active file is a no-op unless playback has ended.
                    if (current && modelData.kind === "file" && !root.session.completed)
                        return
                    root.session.markJumpOrigin()
                    root.session.goToChapter(index)
                }
                Rectangle {
                    objectName: "audiobookChapterHighlight_" + row.index
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: 10
                    color: row.current ? "#26f0c44a" : (row.interactionActive ? theme.glassTint : "transparent")

                    Rectangle {
                        visible: row.current
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: parent.height - 12
                        radius: 2
                        color: theme.gold
                    }
                }
                Text {
                    x: 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.index + 1
                    color: row.current ? theme.gold : theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.bold: row.current
                }
                Text {
                    anchors { left: parent.left; leftMargin: 56; right: stamp.left; rightMargin: 8
                              verticalCenter: parent.verticalCenter }
                    text: Data.text(row.modelData.label)
                    color: row.current ? theme.gold : theme.ink
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.bold: row.current
                    elide: Text.ElideRight
                }
                Text {
                    id: stamp
                    objectName: "audiobookChapterTime_" + row.index
                    visible: row.current || (row.modelData.kind === "seek" && Number(row.modelData.time) > 0)
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    text: row.current ? qsTr("NOW") : Data.formatTime(row.modelData.time)
                    color: row.current ? theme.gold : theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                }
            }
        }
    }
}
