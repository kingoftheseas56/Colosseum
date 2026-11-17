import QtQuick
import QtQuick.Controls
import ".."
import "../vinyl"
import "AudiobookListenPageData.js" as Data

// The audio-only listening page: Vinyl's song page and player bar, driven by the app-wide
// AudiobookSession. The chapter list takes the lyrics column's place; the book's description and
// series line take the notes column's. Reader audiobook controls (skip, speed, sleep) ride in the
// bar's tool area. Hosts wire `backRequested`; routing and Biblio are not this file's business.
Item {
    id: root
    objectName: "audiobookListenPage"
    required property var session
    // title, authors[], narrators[], cover, description, seriesLine. Every field is optional.
    property var book: ({})
    signal backRequested()

    readonly property alias pageData: pd
    readonly property int chapterIndex: Data.chapterIndex(session)
    readonly property var chapterList: session && session.chapterModel ? session.chapterModel : []
    readonly property string description: Data.text(book && book.description).trim()
    readonly property string seriesLine: Data.text(book && book.seriesLine).trim()
    readonly property string narratorLine: Data.list(book && book.narrators).join(" · ")
    readonly property bool hasNotes: description.length > 0 || seriesLine.length > 0
    readonly property bool hasChapters: chapterList.length > 0

    Theme { id: theme }

    QtObject {
        id: pd
        readonly property var playingTrack: ({ title: Data.chapterTitle(root.session, root.book, root.chapterIndex),
                                               available: true, artists: Data.list(root.book && root.book.authors) })
        readonly property var playingEdition: Data.edition(root.book)
        readonly property string playingEditionId: Data.text(root.book && root.book.id)
        readonly property int playingIndex: root.chapterIndex
        readonly property real position: root.session ? Number(root.session.position) || 0 : 0
        readonly property real duration: root.session ? Number(root.session.duration) || 0 : 0
        readonly property bool paused: root.session ? root.session.paused === true : true
        readonly property bool loaded: !!(root.session && root.session.ready)
        readonly property string sourceState: loaded ? "playing" : "idle"
        readonly property bool canNext: root.chapterIndex >= 0 && root.chapterIndex < root.chapterList.length - 1
        readonly property bool canPrevious: root.chapterIndex > 0
        function seek(t) { root.session.seekTo(t) }
        function togglePlay() { root.session.togglePlay() }
        function next(delta) {
            var target = root.chapterIndex + (delta < 0 ? -1 : 1)
            if (root.chapterIndex >= 0 && target >= 0 && target < root.chapterList.length)
                root.session.goToChapter(target)
        }
        function formatTime(seconds) { return Data.formatTime(seconds) }
        function artwork(file) { return Data.coverUrl(file) }
        function toggleVideo() {}
        function toggleLyrics() {}
        function queueRequested() {}
        function noteWindow(line) { return ({}) }
        // Vinyl-only fields, all off (see AudiobookListenPageData.offFields).
        readonly property bool lyricsShown: false
        readonly property bool autoNotes: false
        readonly property bool videoShown: false
        readonly property bool hasLyrics: false
        readonly property bool hasVideo: false
        readonly property string lyricsSource: ""
        readonly property string lyricsDocumentId: ""
        readonly property string lyricsStatus: ""
        readonly property string lyricsError: ""
        readonly property bool timedLyrics: false
        readonly property var lyrics: []
        readonly property int currentLineIndex: -1
        readonly property var notes: ({})
        readonly property int activeNoteLine: -1
        readonly property int manualNoteLine: -1
        readonly property string annotationsStatus: ""
        readonly property int annotationCount: 0
        readonly property string geniusSongId: ""
        readonly property string readingContextKey: ""
        readonly property bool personalEnabled: false
        readonly property bool songIsSaved: false
        readonly property string sourceKind: ""
        readonly property bool torrentAvailable: false
        readonly property bool torrentBusy: false
        readonly property string torrentError: ""
        readonly property bool videoBusy: false
        readonly property string videoError: ""
        readonly property bool queueEnabled: false
    }

    VinylBackdrop {
        id: backdrop
        anchors.fill: parent
        source: Data.coverUrl(root.book && root.book.cover)
    }

    VinylSongPage {
        id: songPage
        objectName: "audiobookListenSong"
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: bar.top
                  topMargin: 8; bottomMargin: 12 }
        pageData: pd
        readersEnabled: false
        songActionsEnabled: false
        albumLinkEnabled: false
        artistNavigationEnabled: false
        onBackRequested: root.backRequested()
    }

    // Everything below sits over the song page in the slots its lyrics/notes columns use.
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
            width: songPage.centerWidth
            x: (slots.width - width) / 2
            y: Math.max(0, songPage.readingTop - 26)
        }

        // Chapters: the lyrics column's place.
        Rectangle {
            id: chaptersPanel
            objectName: "audiobookChapters"
            visible: root.hasChapters && songPage.readerWidth > 0
            x: songPage.sideInset
            y: songPage.readingTop
            width: songPage.readerWidth
            height: songPage.readingHeight
            radius: 14
            color: theme.glassTint
            border.color: theme.edge
            clip: true
            Text {
                x: 14; y: 10
                text: qsTr("Chapters")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
                font.bold: true
            }
            ListView {
                id: chapterView
                objectName: "audiobookChapterList"
                x: 4; y: 34
                width: parent.width - 8
                height: parent.height - 38
                clip: true
                model: root.chapterList
                boundsBehavior: Flickable.StopAtBounds
                Connections {
                    target: root
                    function onChapterIndexChanged() {
                        if (root.chapterIndex >= 0) chapterView.positionViewAtIndex(root.chapterIndex, ListView.Contain)
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
                    height: 36
                    focusOnPointer: false
                    focusRadius: 8
                    accessibleName: Data.text(modelData.label)
                    accessibleDescription: current ? qsTr("Playing now") : ""
                    onTriggered: root.session.goToChapter(index)
                    Rectangle {
                        objectName: "audiobookChapterHighlight_" + row.index
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 8
                        color: row.current ? theme.glassHi : (row.interactionActive ? theme.glassTint : "transparent")
                    }
                    Text {
                        anchors { left: parent.left; leftMargin: 10; right: stamp.left; rightMargin: 8
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
                        // Only a chapter with a real start time (single-file books) gets one.
                        visible: row.modelData.kind === "seek" && Number(row.modelData.time) > 0
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        text: Data.formatTime(row.modelData.time)
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                    }
                }
            }
        }

        // Description and series: the notes column's place. Absent fields leave it hidden.
        Rectangle {
            id: notesPanel
            objectName: "audiobookNotes"
            visible: root.hasNotes && songPage.readerWidth > 0
            x: songPage.notesAreaX
            y: songPage.readingTop
            width: songPage.readerWidth
            height: songPage.readingHeight
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

    VinylPlayerBar {
        id: bar
        x: theme.margin
        y: root.height - height - 18
        width: root.width - theme.margin * 2
        pageData: pd
        backdrop: backdrop
        page: "song"
        videoToolVisible: false
        queueToolVisible: false
        sourcePickerVisible: false
    }

    // Reader audiobook controls the Vinyl bar lacks, in the bar's tool area.
    Row {
        id: audioTools
        objectName: "audiobookTools"
        x: bar.x + bar.toolsLeft
        y: bar.y + bar.toolsTop + 3
        z: 5
        spacing: 6
        component Tool: KeyboardAction {
            id: tool
            property string label
            property bool lit: false
            focusOnPointer: false
            tooltipText: accessibleName
            width: Math.max(38, caption.implicitWidth + 20)
            height: 34
            opacity: enabled ? 1 : 0.34
            Rectangle {
                anchors.fill: parent
                radius: 10
                color: tool.interactionActive || tool.lit ? theme.glassHi : theme.glassTint
                border.color: theme.edge
            }
            Text {
                id: caption
                anchors.centerIn: parent
                text: tool.label
                color: tool.lit ? theme.gold : theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
            }
        }
        Tool {
            objectName: "audiobookSkipBack"
            label: "−" + Data.SKIP_SECONDS
            enabled: pd.loaded
            accessibleName: qsTr("Back %1 seconds").arg(Data.SKIP_SECONDS)
            onTriggered: root.session.seekRel(-Data.SKIP_SECONDS)
        }
        Tool {
            objectName: "audiobookSkipForward"
            label: "+" + Data.SKIP_SECONDS
            enabled: pd.loaded
            accessibleName: qsTr("Forward %1 seconds").arg(Data.SKIP_SECONDS)
            onTriggered: root.session.seekRel(Data.SKIP_SECONDS)
        }
        Tool {
            objectName: "audiobookSpeed"
            label: Data.speedLabel(root.session ? root.session.speed : 1)
            accessibleName: qsTr("Playback speed %1").arg(label)
            onTriggered: root.session.setRate(Data.nextSpeed(root.session.speed))
        }
        Tool {
            objectName: "audiobookSleep"
            label: Data.sleepLabel(root.session ? root.session.sleepMinutes : 0)
            lit: !!root.session && root.session.sleepMinutes > 0
            accessibleName: root.session && root.session.sleepMinutes > 0
                ? qsTr("Sleep timer %1 minutes").arg(root.session.sleepMinutes) : qsTr("Sleep timer off")
            onTriggered: root.session.sleepMinutes = Data.nextSleep(root.session.sleepMinutes)
        }
    }
}
