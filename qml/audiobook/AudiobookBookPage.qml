import QtQuick
import QtQuick.Controls
import ".."
import "../vinyl" as Vinyl
import "AudiobookBookPageData.js" as BookData

FocusScope {
    id: root
    objectName: "audiobookBookPage"
    property Item backdrop: null
    property var work: ({})
    // Enriched catalogue recordings may supply canonical recordingKey/local state.
    property var recordings: work.recordings || []
    // Catalogue series(id).works for the first membership in work.series.
    property var seriesWorks: []
    property var local: ({})
    // {recordingKey (or workId): "none"|"done"|"failed"|{state, progress: 0..1}}
    property var downloadState: ({})
    property bool canSave: false
    property bool saved: false
    property string selectedDownloadKey: ""
    signal downloadRequested(string workId)
    signal listenRequested(string recordingKey)
    signal seriesBookRequested(string workId)
    signal authorRequested(string personId, string name)
    signal saveRequested()
    signal backRequested()

    readonly property var built: BookData.build(work, seriesWorks, recordings)
    readonly property var shownRecording: BookData.recording(built, local, downloadState)
    readonly property string coverUrl: built.cover || shownRecording.cover || ""
    readonly property var actionRecording: selectedDownloadKey
        ? { key: selectedDownloadKey, local: false } : shownRecording
    readonly property var primary: BookData.primaryAction(built, actionRecording, local, downloadState)
    readonly property bool hasSeries: built.seriesBooks.length > 0
    readonly property bool compact: width < 1050
    readonly property real inset: Math.max(0, (width - 1280) / 2)
    function focusBackControl() { back.focusControl(Qt.TabFocusReason); }
    Keys.onEscapePressed: root.backRequested()
    onWorkChanged: scroll.contentY = 0

    Theme { id: theme }
    BackAction {
        id: back
        objectName: "audiobookBack"
        x: root.inset + 54; y: 23
        label: qsTr("Back")
        labelSize: theme.fontSize(13)
        tip: qsTr("Back to Audiobooks")
        idleColor: theme.inkDim
        onTriggered: root.backRequested()
    }
    Flickable {
        id: scroll
        objectName: "audiobookScroll"
        x: 0; y: 88
        width: root.width; height: Math.max(0, root.height - y)
        contentWidth: width
        contentHeight: Math.max(height, Math.max(artColumn.y + artColumn.height,
            root.hasSeries ? shelf.y + shelf.height : details.y + details.height) + (root.compact ? 100 : 24))
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: artColumn
            x: root.compact ? 24 : root.inset + (root.hasSeries ? 78 : 82)
            y: root.compact ? 24 : (root.hasSeries ? 37 : 65)
            width: root.compact ? Math.min(298, root.width - 48) : (root.hasSeries ? 298 : 319)
            spacing: 23
            Item {
                objectName: "audiobookCover"
                width: artColumn.width; height: width
                RoundedPosterImage {
                    anchors.fill: parent
                    sources: root.coverUrl ? [root.coverUrl] : []
                    placeholderTitle: root.built.title
                    radius: 13
                }
                Rectangle { anchors.fill: parent; radius: 13; color: "transparent"; border.color: theme.edge }
                Rectangle {
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 11
                    width: 36; height: 36; radius: 10
                    color: "#d607090d"; border.color: theme.edge
                    Image {
                        anchors.centerIn: parent
                        width: 18; height: 18
                        source: "../../assets/icons/reader2/headphones.svg"
                        sourceSize: Qt.size(width, height)
                    }
                }
            }
            Column {
                width: artColumn.width
                spacing: 9
                Row {
                    id: actions
                    width: parent.width
                    spacing: root.canSave ? 9 : 0
                    KeyboardAction {
                        id: primaryAction
                        objectName: "audiobookPrimaryAction"
                        width: actions.width - (root.canSave ? saveAction.width + actions.spacing : 0)
                        height: 50
                        accessibleName: root.primary.label
                        enabled: root.primary.enabled
                        focusPrimary: true
                        onTriggered: {
                            if (root.primary.kind === "listen") root.listenRequested(root.actionRecording.key)
                            else if (root.primary.kind === "download") root.downloadRequested(root.built.workId)
                        }
                        Rectangle {
                            anchors.fill: parent; radius: 11
                            color: primaryAction.interactionActive ? "#f6d16c" : theme.gold
                            border.color: "#f6d776"
                        }
                        Row {
                            anchors.centerIn: parent
                            readonly property int iconSpace:
                                root.primary.kind === "listen" || root.primary.kind === "download" ? 28 : 0
                            width: Math.min(primaryLabel.implicitWidth + iconSpace, parent.width - 16)
                            spacing: 10
                            Vinyl.VinylControlIcon {
                                width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter
                                visible: root.primary.kind === "listen"
                                symbol: "play"; color: "#181408"
                            }
                            Image {
                                width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter
                                visible: root.primary.kind === "download"
                                source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#181408" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3v12m-5-5 5 5 5-5M4 17v4h16v-4"/></svg>')
                                sourceSize: Qt.size(width, height)
                            }
                            Text {
                                id: primaryLabel
                                width: Math.max(0, parent.width - parent.iconSpace)
                                text: root.primary.label
                                color: "#181408"
                                font.family: theme.ui; font.pixelSize: theme.fontSize(15); font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                    KeyboardAction {
                        id: saveAction
                        objectName: "audiobookSave"
                        visible: root.canSave
                        width: 50; height: 50
                        accessibleName: root.saved ? qsTr("Remove from Library") : qsTr("Save to Library")
                        focusRadius: 11
                        onTriggered: root.saveRequested()
                        Rectangle {
                            anchors.fill: parent
                            radius: 11
                            color: saveAction.interactionActive ? "#30ffffff" : "#12ffffff"
                            border.color: theme.edge
                        }
                        Text {
                            anchors.centerIn: parent
                            text: root.saved ? "♥" : "♡"
                            color: root.saved ? theme.gold : theme.ink
                            font.pixelSize: 24
                        }
                    }
                }
                ProgressBar {
                    id: downloadProgress
                    objectName: "audiobookDownloadProgress"
                    width: parent.width; height: visible ? 4 : 0
                    visible: root.primary.kind === "progress" && root.primary.progress >= 0
                    padding: 0
                    value: Math.max(0, root.primary.progress)
                    background: Rectangle { radius: 2; color: theme.edge }
                    contentItem: Item {
                        Rectangle {
                            width: parent.width * downloadProgress.position
                            height: parent.height
                            radius: 2
                            color: theme.gold
                        }
                    }
                }
                Text {
                    width: parent.width
                    objectName: "audiobookLocalStatus"
                    topPadding: theme.fontSize(11)
                    text: root.primary.kind === "listen" ? qsTr("Downloaded · Ready to listen")
                        : root.primary.kind === "progress" ? "" : qsTr("Not downloaded")
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                }
            }
        }
        Column {
            id: details
            x: root.compact ? 24 : root.inset + (root.hasSeries ? 431 : 466)
            y: root.compact ? artColumn.y + artColumn.height + 32 : (root.hasSeries ? 32 : 69)
            width: Math.max(0, root.width - x - (root.compact ? 24 : root.inset + 67))
            spacing: 0
            Text {
                width: parent.width
                text: root.built.seriesName ? (root.built.seriesNumber
                    ? qsTr("%1 · Book %2").arg(root.built.seriesName).arg(root.built.seriesNumber)
                    : root.built.seriesName).toUpperCase() : qsTr("STANDALONE AUDIOBOOK")
                color: theme.gold
                font.family: theme.ui; font.pixelSize: theme.fontSize(11)
                font.weight: Font.DemiBold; font.letterSpacing: 2.75
                elide: Text.ElideRight
                bottomPadding: 11
            }
            Text {
                objectName: "audiobookTitle"
                width: parent.width
                text: root.built.title
                color: theme.ink
                font.family: theme.display
                font.pixelSize: theme.fontSize(root.compact ? 40 : root.hasSeries ? 57 : 60)
                height: Math.ceil(font.pixelSize * 1.13) + 9
                font.letterSpacing: -1.7
                elide: Text.ElideRight
                bottomPadding: 9
            }
            Text {
                id: author
                objectName: "audiobookAuthor"
                width: parent.width
                visible: root.built.authors !== ""
                readonly property var linkedAuthors: root.built.authorPeople.filter(person => person.id)
                textFormat: linkedAuthors.length ? Text.RichText : Text.PlainText
                text: !visible ? "" : qsTr("By %1").arg(linkedAuthors.length
                    ? BookData.authorMarkup(root.built.authorPeople, String(theme.ink)) : root.built.authors)
                color: theme.ink
                font.family: theme.ui; font.pixelSize: theme.fontSize(20); font.weight: Font.Medium
                wrapMode: Text.WordWrap
                onLinkActivated: (link) => {
                    const person = linkedAuthors.find(entry => encodeURIComponent(entry.id) === link)
                    if (person) root.authorRequested(person.id, person.name)
                }
                HoverHandler { cursorShape: author.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
            Row {
                objectName: "audiobookNarratorRow"
                visible: root.shownRecording.narrators !== ""
                topPadding: root.hasSeries ? 19 : 33
                spacing: 14
                width: parent.width
                Rectangle {
                    y: 3
                    width: 35; height: 35; radius: 12
                    color: "#12ffffff"; border.color: theme.edge
                    Image {
                        anchors.centerIn: parent
                        width: 18; height: 18
                        source: "../../assets/icons/reader2/headphones.svg"
                        sourceSize: Qt.size(width, height)
                    }
                }
                Column {
                    width: parent.width - 49
                    spacing: 3
                    Text {
                        objectName: "audiobookNarratorLabel"
                        text: qsTr("NARRATED BY")
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: theme.fontSize(12); font.letterSpacing: 1.14
                    }
                    Text {
                        objectName: "audiobookNarrator"
                        width: parent.width
                        text: root.shownRecording.narrators
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: theme.fontSize(19)
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Column {
                objectName: "audiobookStory"
                visible: root.built.synopsis !== ""
                topPadding: root.hasSeries ? 18 : 35
                width: parent.width
                spacing: 9
                Text {
                    text: qsTr("The story")
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: theme.fontSize(23)
                }
                Text {
                    width: parent.width
                    text: root.built.synopsis
                    color: "#d1d0d5"
                    font.family: theme.ui; font.pixelSize: theme.fontSize(root.hasSeries ? 15 : 16)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: font.pixelSize * (root.hasSeries ? 1.56 : 1.67)
                    wrapMode: Text.WordWrap
                }
            }
            Column {
                id: facts
                objectName: "audiobookFacts"
                visible: root.shownRecording.facts.length > 0
                width: parent.width
                topPadding: root.hasSeries ? 18 : 37
                spacing: 15
                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Flow {
                    width: parent.width
                    spacing: 31
                    Repeater {
                        model: root.shownRecording.facts
                        delegate: Column {
                            required property var modelData
                            width: Math.max(124, factValue.implicitWidth)
                            spacing: 5
                            Text {
                                text: parent.modelData.label.toUpperCase()
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: theme.fontSize(11); font.letterSpacing: 0.88
                            }
                            Text {
                                id: factValue
                                objectName: "audiobookFact_" + parent.modelData.key
                                text: parent.modelData.text
                                color: theme.ink
                                font.family: theme.ui; font.pixelSize: theme.fontSize(14)
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            id: shelf
            objectName: "audiobookSeriesShelf"
            visible: root.hasSeries
            x: root.compact ? 24 : root.inset + 416
            y: Math.max(root.compact ? 0 : 372, details.y + details.height + 19)
            width: Math.max(0, root.width - x - (root.compact ? 24 : root.inset + 49))
            height: seriesScroll.y + seriesScroll.height + 8
            radius: 17
            color: "#7a141822"; border.color: "#21ffffff"
            Row {
                id: shelfHeading
                x: 18; y: 11
                spacing: 15
                Text {
                    text: root.built.seriesName
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: theme.fontSize(20)
                }
                Text {
                    visible: !root.compact
                    anchors.baseline: parent.children[0].baseline
                    text: qsTr("In reading order · select a cover to open its audiobook page")
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: theme.fontSize(11)
                }
            }
            Flickable {
                id: seriesScroll
                objectName: "audiobookSeriesScroll"
                x: 18; y: shelfHeading.y + shelfHeading.height + 4
                width: parent.width - 36
                height: 126
                contentWidth: seriesRow.width
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.horizontal: ScrollBar {
                    policy: seriesScroll.contentWidth > seriesScroll.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
                Row {
                    id: seriesRow
                    spacing: 11
                    Repeater {
                        model: root.built.seriesBooks
                        delegate: KeyboardAction {
                            id: card
                            required property var modelData
                            objectName: "audiobookSeries_" + modelData.workId
                            width: root.compact ? 98 : Math.max(98, Math.floor((seriesScroll.width - 66) / 7))
                            height: 116
                            accessibleName: [modelData.number ? qsTr("Book %1").arg(modelData.number) : "",
                                modelData.title, modelData.current ? qsTr("Current audiobook") : ""].filter(Boolean).join(", ")
                            onTriggered: root.seriesBookRequested(modelData.workId)
                            onActiveFocusChanged: {
                                if (!activeFocus) return
                                if (x < seriesScroll.contentX) seriesScroll.contentX = x
                                else if (x + width > seriesScroll.contentX + seriesScroll.width)
                                    seriesScroll.contentX = x + width - seriesScroll.width
                            }
                            RoundedPosterImage {
                                x: (parent.width - width) / 2
                                width: 86; height: 86
                                readonly property string coverUrl: (card.modelData.current ? root.coverUrl : "")
                                    || card.modelData.cover
                                sources: coverUrl ? [coverUrl] : []
                                placeholderTitle: card.modelData.title
                                radius: 8
                            }
                            Rectangle {
                                x: (parent.width - width) / 2
                                width: 86; height: 86; radius: 8
                                color: "transparent"
                                border.color: card.modelData.current || card.interactionActive ? theme.gold : theme.edge
                                border.width: card.modelData.current ? 2 : 1
                            }
                            Rectangle {
                                x: (parent.width - 86) / 2 + 5; y: 5
                                visible: card.modelData.number !== ""
                                width: Math.max(22, numberText.implicitWidth + 10); height: 19; radius: 5
                                color: card.modelData.current ? theme.gold : "#e006080e"
                                Text {
                                    id: numberText
                                    objectName: "audiobookNumber_" + card.modelData.workId
                                    anchors.centerIn: parent
                                    text: card.modelData.number
                                    color: card.modelData.current ? "#181406" : theme.ink
                                    font.family: theme.ui; font.pixelSize: theme.fontSize(10); font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {
                                objectName: "audiobookCurrent_" + card.modelData.workId
                                x: (parent.width + 86) / 2 - width; y: 71
                                visible: card.modelData.current
                                width: currentLabel.implicitWidth + 8; height: 14; radius: 4
                                color: theme.gold
                                Text {
                                    id: currentLabel
                                    anchors.centerIn: parent
                                    text: qsTr("CURRENT")
                                    color: "#1d1608"
                                    font.family: theme.ui; font.pixelSize: theme.fontSize(8); font.weight: Font.Bold
                                }
                            }
                            Text {
                                y: 91
                                width: parent.width; height: 26
                                text: card.modelData.title
                                color: card.modelData.current ? "#f8d777" : "#dedde2"
                                font.family: theme.ui; font.pixelSize: theme.fontSize(11)
                                font.weight: card.modelData.current ? Font.DemiBold : Font.Normal
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
