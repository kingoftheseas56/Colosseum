import QtQuick
import ".."
import "../vinyl" as Vinyl
import "AudiobookBookPageData.js" as BookData

// The page for one audiobook (Slice F): the work, its recordings (narrations),
// and one primary action. It is Vinyl's album page driven through options; no
// Vinyl file is copied. Claude wires the signals into Biblio.
Item {
    id: root
    objectName: "audiobookBookPage"

    required property Item backdrop
    // slice C's work(id) shape
    property var work: ({})
    // [{recordingKey, narrators, runtime_min, language, abridgement, release_year,
    //   format, source, sourceMatch, local, bundle}]
    property var recordings: []
    // {recordingKey: {chapters: [...]}} for downloaded recordings
    property var local: ({})
    // {recordingKey: "none" | "done" | "failed" | {state: "active", progress: 0..1}}
    property var downloadState: ({})

    signal listenRequested(string recordingKey)
    signal downloadRequested(string recordingKey)
    signal backRequested()
    signal authorRequested(string name)
    signal seriesRequested(string seriesId)

    readonly property var built: BookData.build(work, recordings, local)
    readonly property var shownEdition: pageModel.shownEdition
    readonly property var primary: BookData.primaryAction(shownEdition, downloadState)
    property alias albumPage: album

    Theme {
        id: theme
    }

    // Everything the Vinyl album page reads, answered from the book data.
    QtObject {
        id: pageModel
        property string selectedId: ""
        readonly property var editions: root.built.editions
        readonly property var shownEdition: edition(selectedId)
        readonly property string shownEditionId: shownEdition.id
        readonly property string shownAlbumId: shownEdition.id
        readonly property var tracks: shownEdition.discs[0] || []
        readonly property int shownDisc: 0
        readonly property bool saved: false
        readonly property real duration: 0
        readonly property real position: 0
        readonly property var credits: []
        readonly property var videos: []
        readonly property var moreAlbums: []
        readonly property var information: root.built.information
        readonly property var others: root.built.others
        signal editionSelected()
        signal artistRequested(string name)
        function edition(id) {
            var list = root.built.editions;
            for (var i = 0; i < list.length; ++i)
                if (list[i].id === id)
                    return list[i];
            return list[0] || root.built.fallback;
        }
        function artwork(file) {
            return file ? file : "";
        }
        function totalTracks(e) {
            return e.discs.reduce(function (n, d) {
                return n + d.length;
            }, 0);
        }
        function formatTime(seconds) {
            var s = Math.round(seconds);
            var h = Math.floor(s / 3600);
            var m = Math.floor((s % 3600) / 60);
            var sec = s % 60;
            return h > 0 ? h + ":" + ("0" + m).slice(-2) + ":" + ("0" + sec).slice(-2)
                : m + ":" + ("0" + sec).slice(-2);
        }
        function trackArtists(index) {
            return "";
        }
        function isPlayingTrack(index) {
            return false;
        }
        function sectionVisible(key) {
            if (key === "about") return !!shownEdition.about;
            if (key === "editions") return editions.length > 0;
            if (key === "information") return information.length > 0;
            if (key === "extra") return others.length > 0;
            return false;
        }
        function selectEdition(id) {
            if (!editions.some(function (e) { return e.id === id; }))
                return;
            selectedId = id;
            editionSelected();
        }
        function selectDisc(index) {
        }
        function playTrack(index) {
            root.listenRequested(shownEdition.id);
        }
        function playShownAlbum() {
            root.listenRequested(shownEdition.id);
        }
        function shuffle() {
        }
        function toggleSaved() {
        }
    }

    KeyboardAction {
        id: backAction
        objectName: "audiobookBack"
        x: Math.max(54, (root.width - 1280) / 2)
        y: 12
        width: backLabel.implicitWidth + 24
        height: 34
        accessibleName: "Back"
        onTriggered: root.backRequested()
        Text {
            id: backLabel
            anchors.centerIn: parent
            text: "‹  Back"
            color: backAction.interactionActive ? theme.ink : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
        }
    }

    Vinyl.VinylAlbumPage {
        id: album
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 56
            bottom: parent.bottom
        }
        pageData: pageModel
        backdrop: root.backdrop
        // The mock gives the cover about a third of the column so the action stays on screen.
        coverMaxSize: 340
        // No Vinyl-only controls on a book.
        shuffleVisible: false
        saveVisible: false
        collectionsVisible: false
        factsVisible: false
        discSwitchEnabled: false
        queueActionsEnabled: false
        primaryActionLabel: root.primary.label
        primaryActionEnabled: root.primary.enabled
        metaLines: {
            var lines = [];
            var series = root.built.series;
            if (series.length > 0)
                lines.push({
                    key: "series",
                    text: series[0].line,
                    actionable: series[0].id !== undefined && series[0].id !== null && String(series[0].id) !== "",
                    strong: false
                });
            var narrators = root.shownEdition.narratorLine;
            if (narrators)
                lines.push({ key: "narrator", text: "Narrated by " + narrators, actionable: false, strong: true });
            return lines;
        }
        trackListHeading: root.shownEdition.hasChapters ? "Chapters" : ""
        trackAside: root.shownEdition.hasChapters ? null : recordingDetails
        editionsTitle: "Recordings"
        editionsNoun: "recordings"
        editionDetailFromData: true
        editionsSortByYear: false
        extraSectionTitle: "Other downloads"
        extraSectionComponent: pageModel.others.length > 0 ? otherDownloads : null
        sidebarWorldName: "Audiobooks"
        onPrimaryActionRequested: {
            if (root.primary.kind === "listen")
                root.listenRequested(root.shownEdition.id);
            else if (root.primary.kind === "download")
                root.downloadRequested(root.shownEdition.id);
        }
        onMetaLineRequested: key => {
            if (key === "series" && root.built.series.length > 0)
                root.seriesRequested(String(root.built.series[0].id));
        }
        Connections {
            target: pageModel
            function onArtistRequested(name) {
                root.authorRequested(name);
            }
        }
    }

    // Before a recording is downloaded its facts stand where the chapters will be.
    Component {
        id: recordingDetails
        Column {
            id: details
            objectName: "audiobookRecordingDetails"
            width: parent ? parent.width : 0
            spacing: 18
            readonly property var edition: root.shownEdition
            Text {
                visible: details.edition.facts.length > 0 || details.edition.hasSource
                text: "This recording"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 27
            }
            Flow {
                visible: details.edition.facts.length > 0
                width: parent.width
                spacing: 36
                Repeater {
                    model: details.edition.facts
                    delegate: Column {
                        required property var modelData
                        objectName: "audiobookFact_" + modelData[0]
                        spacing: 6
                        Text {
                            text: parent.modelData[1]
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                        }
                        Text {
                            text: parent.modelData[2]
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 17
                        }
                    }
                }
            }
            Rectangle {
                objectName: "audiobookSourceRow"
                visible: details.edition.hasSource
                width: Math.min(parent.width, 560)
                height: visible ? sourceColumn.implicitHeight + 32 : 0
                radius: 13
                color: Qt.rgba(1, 1, 1, 0.045)
                border.color: theme.edge
                Column {
                    id: sourceColumn
                    x: 20
                    y: 16
                    width: parent.width - 40
                    spacing: 4
                    Text {
                        objectName: "audiobookSourceName"
                        width: parent.width
                        text: details.edition.source
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 16
                        elide: Text.ElideRight
                    }
                    Text {
                        objectName: "audiobookSourceMatch"
                        visible: details.edition.probable
                        width: parent.width
                        text: "Probable match for this recording"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                }
            }
            Text {
                visible: details.edition.id !== ""
                width: parent.width
                text: "Chapter information comes from the local recording once it is downloaded."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "audiobookNoRecordings"
                visible: details.edition.id === ""
                width: parent.width
                text: pageModel.others.length > 0
                    ? "No recording of this book is matched yet."
                    : "No recording of this book is known yet."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 15
                wrapMode: Text.WordWrap
            }
        }
    }

    // Sources that are not a recording of this book: unmatched files and bundles.
    Component {
        id: otherDownloads
        Column {
            objectName: "audiobookOtherDownloads"
            width: parent ? parent.width : 0
            spacing: 12
            Repeater {
                model: pageModel.others
                delegate: KeyboardAction {
                    id: otherRow
                    required property var modelData
                    focusOnPointer: false
                    objectName: "audiobookOther_" + modelData.id
                    width: parent.width
                    height: 66
                    accessibleName: modelData.title + ". " + modelData.kind
                    onTriggered: root.downloadRequested(modelData.id)
                    Rectangle {
                        anchors.fill: parent
                        radius: 12
                        color: otherRow.interactionActive ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.045)
                        border.color: theme.edge
                    }
                    Column {
                        x: 20
                        width: parent.width - 40
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text {
                            width: parent.width
                            text: otherRow.modelData.title
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                        Text {
                            objectName: "audiobookOtherKind_" + otherRow.modelData.id
                            width: parent.width
                            text: [otherRow.modelData.kind, otherRow.modelData.source].filter(Boolean).join(" · ")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
