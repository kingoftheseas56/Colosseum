import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

FocusScope {
    id: page
    objectName: "audiobookSourcesPage"
    focus: true

    property string workTitle
    property string workAuthor
    property url artwork
    property Item backdrop: null
    property real contentBottomInset: 0
    property bool showTankorent: false
    property string backTip: qsTr("Back to audiobook details")
    property string selectionError
    property var providerStates: ({})
    property var recordingStatus: key => "none"

    readonly property bool narrow: width < 640
    readonly property int sideMargin: narrow ? 16 : theme.margin
    readonly property int headerInset: narrow ? 14 : 26
    readonly property int releaseHeight: narrow ? 104 : (compactHeight ? 116 : 150)
    readonly property real contentHeightBudget: height - contentBottomInset
    readonly property bool compactHeight: contentHeightBudget - 300 < 20 + 26 + 52 + 2 * 44 + 150
    readonly property int bannerHeight: narrow ? Math.min(188, Math.max(126, contentHeightBudget - 220))
        : Math.min(300, Math.max(88, contentHeightBudget - (compactHeight ? 276 : 0)))
    readonly property bool hasDisabledProvider: providerStates.abb?.state === "disabled"
        || (showTankorent && providerStates.tankorent?.state === "disabled")
    readonly property int releaseCount: (providerStates.abb?.rows?.length || 0)
        + (showTankorent ? (providerStates.tankorent?.rows?.length || 0) : 0)

    signal backRequested()
    signal extensionsRequested()
    signal retryRequested(string provider)
    signal sourcePicked(var row)

    Keys.onEscapePressed: page.backRequested()
    Keys.onBackPressed: page.backRequested()
    Keys.onPressed: keyboardScroll.handle(event)
    Keys.onReleased: keyboardScroll.handleRelease(event)
    StackView.onActivated: back.focusControl(Qt.TabFocusReason)

    Theme { id: theme }

    function statusText(provider) {
        if (provider.message) return provider.message
        switch (provider.state) {
        case "searching": return qsTr("Searching…")
        case "disabled": return qsTr("Disabled in Extensions")
        case "empty": return qsTr("No matches found")
        case "error": return provider.error === "timeout" ? qsTr("Search timed out") : qsTr("Search unavailable")
        case "results": return provider.error ? qsTr("Some results were unavailable") : ""
        default: return ""
        }
    }

    function rowDetails(row, providerId) {
        const parts = []
        if (row.narrators && row.narrators.length) parts.push(qsTr("Narrated by %1").arg(row.narrators.join(", ")))
        if (row.format) parts.push(String(row.format).toUpperCase())
        const count = Number(row.fileCount)
        if (count > 0) parts.push(count === 1 ? qsTr("1 file") : qsTr("%1 files").arg(count))
        if (row.size) parts.push(String(row.size))
        if (providerId !== "abb" && row.seeders !== undefined)
            parts.push(row.seeders === 1 ? qsTr("1 seeder") : qsTr("%1 seeders").arg(row.seeders))
        if (row.bundle) {
            const books = Number(row.bundleCount)
            parts.push(books === 1 ? qsTr("Bundle · 1 book")
                : books > 1 ? qsTr("Bundle · %1 books").arg(books) : qsTr("Bundle"))
        }
        if (row.reason) parts.push(String(row.reason))
        return parts.join(" · ")
    }

    component HouseButton: Button {
        id: control
        leftPadding: 14
        rightPadding: 14
        background: Rectangle {
            radius: 9
            color: control.down || control.hovered ? theme.glassHi : theme.glassTint
            border.color: control.activeFocus ? theme.gold : theme.edge
        }
        contentItem: Text {
            text: control.text
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: theme.fontSize(13)
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle { anchors.fill: parent; color: "black" }

    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        visible: page.backdrop !== null
        opacity: 0.5
    }

    Item {
        id: banner
        objectName: "audiobookSourcesBanner"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: page.bannerHeight

        Image {
            objectName: "audiobookSourcesArtwork"
            anchors.fill: parent
            source: page.artwork
            sourceSize.width: Math.ceil(width)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.25) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.5) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.92) }
            }
        }
    }

    Rectangle {
        anchors.top: banner.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: Qt.rgba(0, 0, 0, 0.9)
    }

    BackAction {
        id: back
        objectName: "audiobookSourcesBack"
        x: page.sideMargin
        y: page.compactHeight || page.narrow ? 12 : 30
        z: 1
        tip: page.backTip
        onTriggered: page.backRequested()
    }

    HouseButton {
        id: extensionsAction
        objectName: "audiobookSourcesExtensions"
        anchors.right: parent.right
        anchors.rightMargin: page.sideMargin
        anchors.verticalCenter: back.verticalCenter
        z: 1
        visible: page.hasDisabledProvider
        text: qsTr("Open Extensions")
        height: 38
        onClicked: page.extensionsRequested()
    }

    Column {
        id: titleBlock
        objectName: "audiobookSourcesTitleBlock"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: page.compactHeight && !page.narrow ? back.x + back.width + 16 : page.sideMargin
        anchors.rightMargin: page.compactHeight && !page.narrow && extensionsAction.visible
            ? page.sideMargin + extensionsAction.width + 16 : page.sideMargin
        anchors.bottom: banner.bottom
        anchors.bottomMargin: page.compactHeight || page.narrow ? 12 : 26
        spacing: page.compactHeight || page.narrow ? 4 : 12

        Text {
            width: parent.width
            text: qsTr("AUDIOBOOK SOURCES")
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: theme.fontSize(12)
            font.letterSpacing: page.narrow ? 2 : 4
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: page.workTitle
            color: theme.ink
            font.family: theme.display
            font.pixelSize: theme.fontSize(page.narrow ? 28 : page.compactHeight ? 34 : 56)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: page.workAuthor
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(14)
            font.letterSpacing: 1
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
    }

    Glass {
        id: table
        objectName: "audiobookSourcesTable"
        backdrop: page.backdrop
        anchors.top: banner.bottom
        anchors.topMargin: page.compactHeight ? 10 : 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.contentBottomInset + (page.compactHeight ? 12 : 26)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: page.sideMargin
        anchors.rightMargin: page.sideMargin
        radius: 18

        Item {
            id: tableHead
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: page.compactHeight ? 44 : 52

            Text {
                anchors.left: parent.left
                anchors.leftMargin: page.headerInset
                anchors.verticalCenter: parent.verticalCenter
                text: page.releaseCount === 0 ? qsTr("Releases")
                    : page.releaseCount === 1 ? qsTr("1 release") : qsTr("%1 releases").arg(page.releaseCount)
                color: theme.ink
                font.family: theme.display
                font.pixelSize: theme.fontSize(16)
                font.weight: Font.DemiBold
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
        }

        Flickable {
            id: scroll
            objectName: "audiobookSourcesScroll"
            anchors.top: tableHead.bottom
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            contentWidth: width
            contentHeight: sections.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: HouseScrollBar { flick: scroll }

            ColumnLayout {
                id: sections
                width: scroll.width
                spacing: 0

                Repeater {
                    model: [
                        { id: "abb", name: qsTr("AudioBookBay") },
                        { id: "tankorent", name: qsTr("Tankorent") }
                    ]
                    delegate: ColumnLayout {
                        id: section
                        required property var modelData
                        objectName: "audiobookSources_" + modelData.id
                        readonly property var provider: page.providerStates[modelData.id] || ({ state: "", rows: [] })
                        visible: modelData.id !== "tankorent" || page.showTankorent
                        Layout.fillWidth: true
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumHeight: 44
                            Layout.leftMargin: page.headerInset
                            Layout.rightMargin: page.headerInset
                            spacing: 10

                            Text {
                                text: section.modelData.name
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: theme.fontSize(15)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                objectName: "audiobookSourcesStatus_" + section.modelData.id
                                text: page.selectionError || page.statusText(section.provider)
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: theme.fontSize(13)
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.Wrap
                            }
                            HouseButton {
                                visible: section.provider.state === "error"
                                text: qsTr("Retry")
                                onClicked: page.retryRequested(section.modelData.id)
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.edge }

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
                                    : status === "downloaded" ? qsTr("Downloaded")
                                    : status === "done" ? qsTr("Listen") : status === "failed" ? qsTr("Retry")
                                    : enabled ? qsTr("Download") : qsTr("Downloading…")
                                readonly property string sourceName: modelData.sourceLabel || section.modelData.name
                                enabled: !!modelData.request && (status === "none" || status === "failed" || status === "done")
                                spaceActivates: true
                                accessibleName: qsTr("%1 · %2 · %3").arg(sourceName).arg(modelData.title || "").arg(action)
                                accessibleDescription: [description.text, confidence.text].filter(Boolean).join(" · ")
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(page.releaseHeight, row.implicitHeight + 24)
                                onTriggered: page.sourcePicked(modelData)
                                onActiveFocusChanged: if (activeFocus) {
                                    const yInList = mapToItem(scroll.contentItem, 0, 0).y
                                    if (yInList < scroll.contentY) scroll.contentY = yInList
                                    else if (yInList + height > scroll.contentY + scroll.height)
                                        scroll.contentY = yInList + height - scroll.height
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: option.interactionActive ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                                    border.color: option.activeFocus ? theme.gold : "transparent"
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                                RowLayout {
                                    id: row
                                    anchors.fill: parent
                                    anchors.leftMargin: page.headerInset
                                    anchors.rightMargin: page.headerInset
                                    spacing: page.narrow ? 12 : 24

                                    Rectangle {
                                        visible: !page.narrow
                                        Layout.preferredWidth: 54
                                        Layout.preferredHeight: 54
                                        radius: 12
                                        color: Qt.rgba(1, 1, 1, 0.05)
                                        border.color: theme.edge

                                        Text {
                                            anchors.centerIn: parent
                                            text: option.sourceName.charAt(0).toUpperCase()
                                            color: theme.ink
                                            font.family: theme.display
                                            font.pixelSize: theme.fontSize(24)
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Row {
                                            Layout.fillWidth: true
                                            spacing: 10
                                            Text {
                                                width: Math.min(implicitWidth, Math.max(0, parent.width
                                                    - (confidence.visible ? confidence.implicitWidth + parent.spacing : 0)))
                                                text: option.sourceName
                                                color: theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: theme.fontSize(15)
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                                textFormat: Text.PlainText
                                            }
                                            Text {
                                                id: confidence
                                                visible: text.length > 0
                                                text: option.modelData.recommended ? qsTr("Recommended") : ""
                                                color: theme.gold
                                                font.family: theme.ui
                                                font.pixelSize: theme.fontSize(12)
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: option.modelData.title || qsTr("Audiobook source")
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: theme.fontSize(14)
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            textFormat: Text.PlainText
                                        }
                                        Text {
                                            id: description
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            text: page.rowDetails(option.modelData, section.modelData.id)
                                            color: theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: theme.fontSize(12)
                                            wrapMode: Text.Wrap
                                            textFormat: Text.PlainText
                                        }
                                    }

                                    Rectangle {
                                        visible: option.enabled
                                        Layout.preferredWidth: page.narrow ? 44 : 56
                                        Layout.preferredHeight: page.narrow ? 44 : 56
                                        radius: width / 2
                                        color: theme.gold
                                        Accessible.ignored: true
                                        Image {
                                            anchors.centerIn: parent
                                            width: 20
                                            height: 20
                                            sourceSize: Qt.size(40, 40)
                                            readonly property string glyph: option.status === "done"
                                                ? '<path d="M8 5v14l11-7z"/>'
                                                : option.status === "failed"
                                                    ? '<path d="M20 7v5h-5m5 0a8 8 0 1 0-2.5 5.8"/>'
                                                    : '<path d="M12 3v12m-5-5 5 5 5-5M4 17v4h16v-4"/>'
                                            source: "data:image/svg+xml;utf8," + encodeURIComponent(
                                                '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#1a1306" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
                                                + glyph + '</svg>')
                                        }
                                    }
                                    Text {
                                        visible: !option.enabled
                                        text: option.action
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: theme.fontSize(12)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        ScrollGlide { id: glide; flick: scroll }
    }

    KeyboardScrollController { id: keyboardScroll; flick: scroll; glide: glide }
}
