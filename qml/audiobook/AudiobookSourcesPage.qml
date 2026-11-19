import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

FocusScope {
    id: page
    objectName: "audiobookSourcesPage"
    focus: true

    property string workTitle
    property string workAuthor
    property bool showTankorent: false
    property var providerStates: ({})
    property var recordingStatus: key => "none"

    signal backRequested()
    signal extensionsRequested()
    signal retryRequested(string provider)
    signal sourcePicked(var row)

    Keys.onEscapePressed: page.backRequested()
    Keys.onBackPressed: page.backRequested()
    StackView.onActivated: back.focusControl(Qt.TabFocusReason)

    Theme { id: theme }

    function statusText(provider) {
        switch (provider.state) {
        case "searching": return qsTr("Searching…")
        case "disabled": return qsTr("Disabled in Extensions")
        case "empty": return qsTr("No matches found")
        case "error": return provider.error === "timeout" ? qsTr("Search timed out") : qsTr("Search unavailable")
        case "results": return provider.error ? qsTr("Some results were unavailable") : ""
        default: return ""
        }
    }

    function rowDetails(row) {
        const parts = []
        if (row.narrators && row.narrators.length) parts.push(qsTr("Narrated by %1").arg(row.narrators.join(", ")))
        if (row.format) parts.push(String(row.format).toUpperCase())
        const count = Number(row.fileCount)
        if (count > 0) parts.push(count === 1 ? qsTr("1 file") : qsTr("%1 files").arg(count))
        if (row.size) parts.push(String(row.size))
        if (row.bundle) {
            const books = Number(row.bundleCount)
            parts.push(books === 1 ? qsTr("Bundle · 1 book")
                : books > 1 ? qsTr("Bundle · %1 books").arg(books) : qsTr("Bundle"))
        }
        if (row.recommended) parts.push(qsTr("Recommended"))
        if (row.reason) parts.push(String(row.reason))
        return parts.join(" · ")
    }

    readonly property int sideMargin: width < 640 ? 16 : 54

    RowLayout {
        id: toolbar
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: page.sideMargin
        anchors.rightMargin: page.sideMargin
        spacing: 16

        BackAction {
            id: back
            objectName: "audiobookSourcesBack"
            label: qsTr("Back")
            onTriggered: page.backRequested()
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Sources")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: theme.fontSize(27)
        }
        Button {
            objectName: "audiobookSourcesExtensions"
            visible: Object.values(page.providerStates).some(provider => provider.state === "disabled")
            text: qsTr("Open Extensions")
            onClicked: page.extensionsRequested()
        }
    }

    Flickable {
        id: scroll
        objectName: "audiobookSourcesScroll"
        anchors.top: toolbar.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.left: toolbar.left
        anchors.right: toolbar.right
        contentWidth: width
        contentHeight: sections.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: sections
            width: scroll.width - 6
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: page.workTitle
                color: theme.ink
                font.family: theme.display
                font.pixelSize: theme.fontSize(page.width < 640 ? 30 : 43)
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                visible: text.length > 0
                text: page.workAuthor
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.fontSize(15)
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }

            Repeater {
                model: [
                    { id: "abb", name: qsTr("AudioBookBay") },
                    { id: "librivox", name: qsTr("LibriVox") },
                    { id: "tankorent", name: qsTr("Tankorent") }
                ]
                delegate: ColumnLayout {
                    id: section
                    required property var modelData
                    objectName: "audiobookSources_" + modelData.id
                    readonly property var provider: page.providerStates[modelData.id] || ({ state: "", rows: [] })
                    visible: modelData.id !== "tankorent" || page.showTankorent
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    spacing: 7

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.edge }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: section.modelData.name
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: theme.fontSize(22)
                        }
                        Button {
                            visible: section.provider.state === "error"
                            text: qsTr("Retry")
                            onClicked: page.retryRequested(section.modelData.id)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        objectName: "audiobookSourcesStatus_" + section.modelData.id
                        text: page.statusText(section.provider)
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(14)
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: section.provider.rows
                        delegate: KeyboardAction {
                            id: option
                            required property int index
                            required property var modelData
                            objectName: "audiobookSourceOption_" + section.modelData.id + "_" + index
                            readonly property string status: modelData.request
                                ? page.recordingStatus(modelData.request.recordingKey) : ""
                            readonly property string action: !modelData.request ? qsTr("Unavailable")
                                : status === "done" ? qsTr("Listen") : status === "failed" ? qsTr("Retry")
                                : enabled ? qsTr("Download") : qsTr("Downloading…")
                            enabled: !!modelData.request && (status === "none" || status === "failed" || status === "done")
                            spaceActivates: true
                            accessibleName: modelData.title + " · " + action
                            accessibleDescription: description.text
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.max(64, row.implicitHeight + 20)
                            onTriggered: page.sourcePicked(modelData)
                            onActiveFocusChanged: if (activeFocus) {
                                const top = mapToItem(scroll.contentItem, 0, 0).y
                                if (top < scroll.contentY) scroll.contentY = top
                                else if (top + height > scroll.contentY + scroll.height)
                                    scroll.contentY = top + height - scroll.height
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                color: option.interactionActive ? theme.glassHi : theme.glassTint
                                border.color: theme.edge
                            }
                            RowLayout {
                                id: row
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: option.modelData.title || qsTr("Audiobook source")
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: theme.fontSize(15)
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.Wrap
                                        textFormat: Text.PlainText
                                    }
                                    Text {
                                        id: description
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: page.rowDetails(option.modelData)
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: theme.fontSize(13)
                                        wrapMode: Text.Wrap
                                        textFormat: Text.PlainText
                                    }
                                }
                                Text {
                                    text: option.action
                                    color: option.enabled ? theme.gold : theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: theme.fontSize(13)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
