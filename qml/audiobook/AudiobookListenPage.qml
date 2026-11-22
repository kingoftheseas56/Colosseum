import QtQuick
import QtQuick.Controls
import ".."
import "../collections"
import "../vinyl"
import "../CollectionsReferences.js" as CollectionsReferences
import "AudiobookListenPageData.js" as Data
import "AudiobookChapterLabels.js" as ChapterLabels

Item {
    id: root
    objectName: "audiobookListenPage"
    required property var session
    property var book: ({})
    property bool ebookAvailable: false
    property bool saved: false
    property bool saveAvailable: true
    property bool androidHost: Qt.platform.os === "android"
    // Inset from the top of the viewport to the content below the shell's top bar.
    property real contentTop: 138
    property bool chaptersOpen: false
    property var listeningJump: null
    readonly property string listeningJumpOffer: listeningJump
        ? qsTr("Jump to %1, where you're reading?").arg(listeningJump.label) : ""
    signal backRequested()
    signal readAlongRequested()
    signal saveToggled()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal quitRequested()

    readonly property bool playbackReady: !!session && session.ready
    readonly property var chapterList: session && session.chapterModel ? session.chapterModel : []
    readonly property bool hasChapters: chapterList.length > 0
    readonly property int chapterIndex: playbackReady && hasChapters ? session.chapterIndex : -1
    readonly property string bookTitle: Data.text(book && book.title)
    readonly property string authorLine: Data.list(book && (book.authors || book.author)).join(" · ")
    readonly property string narratorLine: Data.list(book && book.narrators).join(" & ")
    readonly property string seriesLine: Data.text(book && book.seriesLine)
    readonly property url coverSource: Data.coverUrl(book && book.cover)
    readonly property real sideInset: Math.min(theme.margin, width * 0.075)
    readonly property bool stackedActions: width < 600
    readonly property real posterSize: Math.max(0, Math.min(290, width - 2 * sideInset,
        bar.y - contentTop - (stackedActions ? 132 : 96) - (seriesLine.length ? 14 : 0)))

    onHasChaptersChanged: if (!hasChapters) chaptersOpen = false
    function acceptListeningJump() {
        const jump = listeningJump
        listeningJump = null
        if (!jump || !playbackReady || jump.index < 0 || jump.index >= chapterList.length
                || jump.index === session.chapterIndex) return
        session.markJumpOrigin()
        session.goToChapterKeepState(jump.index)
    }

    Theme { id: theme }

    component Glyph: Image {
        property string path
        property color ink: theme.inkDim
        sourceSize: Qt.size(24, 24)
        source: path ? "data:image/svg+xml," + encodeURIComponent(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="'
            + ink + '" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="'
            + path + '"/></svg>') : ""
    }

    component PageAction: KeyboardAction {
        id: action
        property string label: ""
        property string path: ""
        property string symbol: ""
        property bool selected: false
        width: label.length ? caption.implicitWidth + 48 : 34
        height: 34
        opacity: enabled ? 1 : 0.34
        focusOnPointer: false
        spaceActivates: true

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: action.interactionActive || action.selected ? theme.glassHi : theme.glassTint
            border.color: theme.edge
        }
        Row {
            anchors.centerIn: parent
            spacing: 7
            VinylControlIcon {
                visible: action.symbol.length > 0
                width: 17
                height: 17
                anchors.verticalCenter: parent.verticalCenter
                symbol: action.symbol
                color: action.selected ? theme.gold : theme.inkDim
            }
            Glyph {
                visible: action.path.length > 0
                width: 17
                height: 17
                anchors.verticalCenter: parent.verticalCenter
                path: action.path
            }
            Text {
                id: caption
                visible: action.label.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: action.label
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
            }
        }
    }

    VinylBackdrop {
        anchors.fill: parent
        source: root.coverSource
    }

    HouseWindowControls {
        objectName: "audiobookListenWindowControls"
        objectPrefix: "audiobookListenWindow"
        androidHost: root.androidHost
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: root.sideInset
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onQuitRequested: root.quitRequested()
    }

    BackAction {
        objectName: "audiobookBack"
        x: root.sideInset
        y: root.contentTop + (root.stackedActions ? -46 : 1)
        width: Math.max(92, implicitWidth)
        height: 44
        labelSize: 13
        label: qsTr("Back")
        focusOnPointer: false
        onTriggered: root.backRequested()
    }

    AddToCollectionsButton {
        objectName: "audiobookAddToCollections"
        x: root.width - width - root.sideInset
        y: root.contentTop + (root.stackedActions ? -41 : 5)
        reference: CollectionsReferences.book(Object.assign({}, root.book,
            { author: root.authorLine, isAudiobook: true }))
        iconOnly: true
        hoverDescriptionEnabled: false
    }

    RoundedPosterImage {
        id: poster
        objectName: "audiobookPoster"
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.contentTop
        width: root.posterSize
        height: width
        radius: 18
        sources: root.coverSource.toString() ? [root.coverSource] : []
        placeholderTitle: root.bookTitle
    }

    PageAction {
        id: chaptersButton
        objectName: "audiobookChaptersButton"
        x: root.stackedActions ? root.width / 2 - width - 4 : poster.x - width - 44
        y: root.stackedActions ? identity.y + identity.height + 8 : poster.y
        label: qsTr("Chapters")
        path: "M9 6h11M9 12h11M9 18h11M4 6h.2M4 12h.2M4 18h.2"
        accessibleName: label
        accessibleDescription: root.chaptersOpen ? qsTr("Expanded") : qsTr("Collapsed")
        enabled: root.hasChapters
        selected: root.chaptersOpen
        onTriggered: root.chaptersOpen = !root.chaptersOpen
    }

    PageAction {
        objectName: "audiobookReadAlong"
        visible: !root.book.bundle
        x: root.stackedActions ? root.width / 2 + 4 : poster.x + poster.width + 44
        y: chaptersButton.y
        label: qsTr("Read along")
        path: "M12 6c-2-2-5-2-9-2v15c4 0 7 0 9 2 2-2 5-2 9-2V4c-4 0-7 0-9 2ZM12 6v15"
        accessibleName: label
        enabled: root.ebookAvailable
        onTriggered: root.readAlongRequested()
    }

    PageAction {
        id: saveButton
        objectName: "audiobookSave"
        x: poster.x
        y: poster.y + poster.height + 12
        visible: root.saveAvailable
        symbol: root.saved ? "saved" : "save"
        selected: root.saved
        accessibleName: root.saved ? qsTr("Remove saved audiobook") : qsTr("Save audiobook")
        tooltipText: accessibleName
        onTriggered: root.saveToggled()
    }

    Column {
        id: identity
        anchors.horizontalCenter: parent.horizontalCenter
        y: (root.saveAvailable ? saveButton.y + saveButton.height : poster.y + poster.height) + 8
        width: Math.min(420, root.width - 2 * root.sideInset)

        Text {
            objectName: "audiobookIdentity"
            width: parent.width
            text: [root.authorLine, root.bookTitle].filter(value => value.length > 0).join(" · ")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            objectName: "audiobookNarrators"
            visible: root.narratorLine.length > 0
            width: parent.width
            text: qsTr("Narrated by %1").arg(root.narratorLine)
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 11
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            objectName: "audiobookPlayerSeries"
            visible: root.seriesLine.length > 0
            width: parent.width
            text: root.seriesLine
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 11
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    Rectangle {
        id: chaptersPanel
        objectName: "audiobookChapters"
        visible: root.chaptersOpen && root.hasChapters
        x: Math.min(44, root.sideInset)
        y: poster.y + (root.stackedActions ? 0 : 42)
        width: root.stackedActions ? Math.min(250, root.width - 2 * x)
            : Math.max(0, Math.min(250, poster.x - x - 8))
        height: Math.max(0, (root.stackedActions ? chaptersButton.y - 8 : bar.y - 18) - y)
        radius: 10
        color: root.stackedActions ? "#f01a1b23" : "transparent"
        Keys.onEscapePressed: {
            root.chaptersOpen = false
            chaptersButton.forceActiveFocus(Qt.ShortcutFocusReason)
        }
        onVisibleChanged: {
            if (visible && root.chapterIndex >= 0) {
                const rowsAbove = Math.round(chapterView.height / (2 * chapterView.rowHeight))
                chapterView.positionViewAtIndex(Math.max(0, root.chapterIndex - rowsAbove), ListView.Beginning)
            }
        }

        ListView {
            id: chapterView
            readonly property int rowHeight: 34
            objectName: "audiobookChapterList"
            anchors.fill: parent
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
                width: Math.max(0, chapterView.width - 8)
                height: chapterView.rowHeight
                focusOnPointer: false
                focusRadius: 8
                accessibleName: ChapterLabels.display(modelData, index, qsTr("Chapter %1"))
                accessibleDescription: current ? qsTr("Playing now")
                    : modelData.kind === "seek" && Number(modelData.time) > 0
                      ? qsTr("Starts at %1").arg(Data.formatTime(modelData.time)) : ""
                enabled: root.playbackReady
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
                    radius: 8
                    color: row.current || row.interactionActive ? "#18ffffff" : "transparent"
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: marker.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.accessibleName
                    color: row.current ? theme.ink : theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 15
                    font.weight: row.current ? Font.DemiBold : Font.Normal
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
                Glyph {
                    id: marker
                    objectName: "audiobookChapterMarker_" + row.index
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.current
                    width: 14
                    height: 14
                    ink: theme.gold
                    path: "M5 10v4m5-8v12m5-10v8m5-6v4"
                }
            }
        }
    }

    AudiobookJumpOffer {
        objectName: "audiobookListeningJumpOffer"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bar.top
        anchors.bottomMargin: 26
        width: Math.min(implicitWidth, parent.width - 32)
        label: root.listeningJumpOffer
        z: 2
        onAccepted: root.acceptListeningJump()
        onDismissed: root.listeningJump = null
    }

    AudiobookControlBar {
        id: bar
        objectName: "audiobookListenBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sideInset
        anchors.rightMargin: root.sideInset
        anchors.bottomMargin: 18
        session: root.session
        wide: true
        playbackAvailable: root.playbackReady
        bookTitle: root.bookTitle
        coverSource: root.coverSource
    }
}
