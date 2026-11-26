// ReadingDesk: Home's two Apple Books chart picks and Biblio entry points.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "BiblioApi.js" as BiblioApi
import "Catalog.js" as Catalog

Glass {
    id: desk
    objectName: "biblioHomeReadingDesk"

    property string heading: qsTr("Biblio")
    property var bookDispatcher: BiblioApi.loadBiblio
    property var audioDispatcher: BiblioApi.loadAudiobookChart
    property var chart: []
    property var audioChart: []
    property bool booksLoading: true
    property bool audioLoading: true
    property var bookCancel: null
    property var audioCancel: null
    readonly property bool compactLayout: width < 640
    readonly property int sideMargin: compactLayout ? 12 : 30
    readonly property int verticalMargin: compactLayout ? 16 : 22

    signal clicked()
    signal genrePicked(string name)
    signal audiobookPicked(var book)
    signal audiobookBrowseRequested()

    radius: 18
    implicitHeight: Math.max(compactLayout ? 0 : 400, content.implicitHeight + 2 * verticalMargin)
    height: implicitHeight

    Theme { id: theme }

    Component.onCompleted: {
        bookCancel = bookDispatcher(function(result, error) {
            desk.chart = !error && result && result.top ? result.top : []
            desk.booksLoading = false
        })
        audioCancel = audioDispatcher(function(result, error) {
            desk.audioChart = !error && result ? result : []
            desk.audioLoading = false
        })
    }
    Component.onDestruction: {
        if (typeof bookCancel === "function") bookCancel()
        if (typeof audioCancel === "function") audioCancel()
    }

    function tintAt(index, fallback) {
        return chart.length > index && chart[index].c1 ? chart[index].c1 : fallback
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: desk.sideMargin
        anchors.rightMargin: desk.sideMargin
        anchors.topMargin: desk.verticalMargin
        spacing: 4

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: desk.heading
            font.family: theme.display
            font.pixelSize: 33
            color: theme.ink

            KeyboardAction {
                anchors.fill: parent
                accessibleName: qsTr("Open Biblio")
                onTriggered: desk.clicked()
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Apple Books · US charts")
            font.family: theme.ui
            font.pixelSize: 12
            color: theme.inkDim
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            columns: desk.compactLayout ? 2 : 3
            columnSpacing: desk.compactLayout ? 8 : 21
            rowSpacing: 15

            ChartPick {
                objectName: "biblioHomeBooks"
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignTop
                entry: desk.chart.length ? desk.chart[0] : null
                loading: desk.booksLoading
                onActivated: desk.clicked()
            }

            ChartPick {
                objectName: "biblioHomeAudiobooks"
                Layout.row: 0
                Layout.column: desk.compactLayout ? 1 : 2
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignTop
                audio: true
                entry: desk.audioChart.length ? desk.audioChart[0] : null
                loading: desk.audioLoading
                onActivated: if (entry) desk.audiobookPicked(entry)
                onBrowseRequested: desk.audiobookBrowseRequested()
            }

            ColumnLayout {
                Layout.row: desk.compactLayout ? 1 : 0
                Layout.column: desk.compactLayout ? 0 : 1
                Layout.columnSpan: desk.compactLayout ? 2 : 1
                Layout.fillWidth: false
                Layout.preferredWidth: desk.compactLayout
                    ? Math.max(0, Math.min(desk.width - 2 * desk.sideMargin, 320)) : 227
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                spacing: 8

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Explore by genre")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: desk.compactLayout ? 17 : 22
                }

                Flow {
                    id: genreFlow
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: Catalog.biblioGenres.slice(0, 4)

                        delegate: Rectangle {
                            id: genreChip
                            required property var modelData
                            width: Math.min(genreFlow.width, genreLabel.implicitWidth + 18)
                            height: desk.compactLayout ? 38 : 30
                            radius: height / 2
                            color: Qt.rgba(1, 1, 1, genreAction.hovered ? 0.12 : 0.06)
                            border.width: 1
                            border.color: genreAction.interactionActive ? theme.gold : theme.edge

                            Text {
                                id: genreLabel
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - 12)
                                text: genreChip.modelData.name
                                elide: Text.ElideRight
                                font.family: theme.ui
                                font.pixelSize: 11
                                color: theme.ink
                            }

                            KeyboardAction {
                                id: genreAction
                                anchors.fill: parent
                                focusRadius: genreChip.radius
                                accessibleName: qsTr("Explore %1").arg(genreChip.modelData.name)
                                onTriggered: desk.genrePicked(genreChip.modelData.name)
                            }
                        }
                    }
                }
            }
        }
    }

    component ChartPick: ColumnLayout {
        id: pick
        property var entry: null
        property bool loading: false
        property bool audio: false
        signal activated()
        signal browseRequested()
        spacing: 4

        Item {
            id: artwork
            Layout.fillWidth: true
            Layout.preferredHeight: desk.compactLayout ? 132 : 197
            readonly property real coverWidth: Math.max(0, Math.min(width * (pick.audio ? 0.78 : 0.68),
                desk.compactLayout ? (pick.audio ? 94 : 72) : (pick.audio ? 142 : 111)))

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: Math.min(parent.width, artwork.coverWidth + (pick.audio ? 30 : 56))
                height: 8
                radius: height / 2
                color: Qt.rgba(0, 0, 0, 0.32)
            }

            Repeater {
                model: pick.audio ? 0 : 3

                delegate: Rectangle {
                    required property int index
                    anchors.horizontalCenter: artwork.horizontalCenter
                    anchors.bottom: artwork.bottom
                    anchors.bottomMargin: 4 + index * (desk.compactLayout ? 8 : 10)
                    width: Math.min(artwork.width - 4, artwork.coverWidth + 51 - index * 6)
                    height: desk.compactLayout ? 10 : 13
                    radius: 3
                    rotation: index % 2 ? 2 : -2
                    color: desk.tintAt(4 - index, ["#68493a", "#544357", "#435c52"][index])
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.16)
                }
            }

            Rectangle {
                visible: pick.audio && (pick.entry !== null)
                anchors.verticalCenter: cover.verticalCenter
                anchors.horizontalCenter: artwork.horizontalCenter
                anchors.horizontalCenterOffset: artwork.coverWidth * 0.23
                width: artwork.coverWidth * 0.92
                height: width
                radius: width / 2
                border.width: 1
                border.color: "#bfc8d9"
                rotation: -24
                gradient: Gradient {
                    GradientStop { position: 0; color: "#8790ab" }
                    GradientStop { position: 0.30; color: "#d2ddeb" }
                    GradientStop { position: 0.58; color: "#a394b8" }
                    GradientStop { position: 0.82; color: "#c5c6dc" }
                    GradientStop { position: 1; color: "#69738d" }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.84
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: "#50f4ecff"
                }
            }

            Rectangle {
                id: cover
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.horizontalCenterOffset: pick.audio ? -width * 0.14 : 0
                anchors.bottom: parent.bottom
                anchors.bottomMargin: pick.audio ? 12 : desk.compactLayout ? 24 : 29
                width: artwork.coverWidth
                height: pick.audio ? width : width * 1.5
                radius: 5
                clip: true
                rotation: -4
                color: pick.audio ? "#393b51" : desk.tintAt(0, "#514032")

                Image {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    source: pick.audio ? "../assets/icons/music.svg" : "../assets/icons/book-library.svg"
                    fillMode: Image.PreserveAspectFit
                    visible: coverImage.status !== Image.Ready
                    opacity: 0.7
                }

                Image {
                    id: coverImage
                    anchors.fill: parent
                    source: pick.entry && pick.entry.cover ? pick.entry.cover : ""
                    asynchronous: true
                    sourceSize.width: pick.audio ? 400 : 300
                    sourceSize.height: pick.audio ? 400 : 450
                    fillMode: pick.audio ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                }
            }

            KeyboardAction {
                objectName: pick.audio ? "biblioHomeAudiobookCover" : "biblioHomeBookCover"
                anchors.fill: parent
                enabled: !pick.audio || pick.entry !== null
                accessibleName: pick.audio && pick.entry ? qsTr("Open audiobook %1").arg(pick.entry.title)
                                                         : qsTr("Open Biblio")
                onTriggered: pick.activated()
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: pick.audio ? qsTr("No. 1 · US audiobook chart") : qsTr("No. 1 · US ebook chart")
            visible: pick.entry !== null
            font.family: theme.ui
            font.pixelSize: desk.compactLayout ? 11 : 12
            font.weight: Font.DemiBold
            color: theme.gold
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: pick.entry ? pick.entry.title
                             : pick.loading ? qsTr("Loading chart…")
                                            : pick.audio ? qsTr("Audiobook chart unavailable")
                                                         : qsTr("Ebook chart unavailable")
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
            font.family: theme.display
            font.pixelSize: desk.compactLayout ? 15 : 19
            color: theme.ink
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0
            text: pick.entry && pick.entry.author ? pick.entry.author : ""
            elide: Text.ElideRight
            font.family: theme.ui
            font.pixelSize: 12
            color: theme.inkDim
        }

        Rectangle {
            id: browse
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 3
            implicitWidth: browseLabel.implicitWidth + 18
            implicitHeight: desk.compactLayout ? 44 : 32
            visible: pick.audio
            radius: height / 2
            color: Qt.rgba(0, 0, 0, browseAction.hovered ? 0.26 : 0.15)
            border.width: 1
            border.color: browseAction.interactionActive ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.48)

            Text {
                id: browseLabel
                anchors.centerIn: parent
                text: qsTr("Browse audiobooks")
                font.family: theme.ui
                font.pixelSize: desk.compactLayout ? 11 : 12
                color: theme.ink
            }

            KeyboardAction {
                id: browseAction
                objectName: pick.audio ? "biblioHomeBrowseAudiobooks" : ""
                anchors.fill: parent
                focusRadius: browse.radius
                accessibleName: browseLabel.text
                onTriggered: pick.browseRequested()
            }
        }
    }
}
