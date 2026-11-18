import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Popup {
    id: picker
    objectName: "audiobookSourcePicker"

    property var rows: []
    property string workTitle: ""
    signal sourcePicked(var row)

    readonly property bool wide: parent && parent.width >= 900
    readonly property int edgeInset: wide ? 54 : 16
    readonly property int bottomGap: wide ? 72 : 16

    width: parent ? Math.max(0, Math.min(650, parent.width - edgeInset * 2)) : 0
    height: parent ? Math.min(sheet.implicitHeight + padding * 2, Math.floor(parent.height * 0.6)) : 0
    x: parent ? (wide ? parent.width - width - edgeInset : (parent.width - width) / 2) : 0
    y: parent ? parent.height - height - bottomGap : 0
    padding: 16
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        list.contentY = 0
        closeAction.forceActiveFocus(Qt.PopupFocusReason)
    }

    Theme { id: theme }

    function describe(row) {
        const parts = []
        if (row.format)
            parts.push(String(row.format).toUpperCase())
        const count = Number(row.fileCount)
        if (Number.isInteger(count) && count > 0)
            parts.push(count === 1 ? qsTr("1 file") : qsTr("%1 files").arg(count))
        if (row.size)
            parts.push(String(row.size))
        if (row.narrators && row.narrators.length)
            parts.push(qsTr("narrated by %1").arg(row.narrators.join(", ")))
        return parts.join(" · ")
    }

    function bundleLabel(row) {
        const count = Number(row.bundleCount)
        if (Number.isInteger(count) && count > 0)
            return count === 1 ? qsTr("Bundle · 1 book") : qsTr("Bundle · %1 books").arg(count)
        return qsTr("Bundle")
    }

    Overlay.modal: Rectangle { color: "#50060a12" }
    background: Rectangle {
        radius: 16
        color: "#171923"
        border.color: theme.edge
    }

    contentItem: ColumnLayout {
        id: sheet
        spacing: 10
        Keys.onBackPressed: picker.close()

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Choose an AudioBookBay upload")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: theme.fontSize(21)
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                }
                Text {
                    objectName: "audiobookSourceWorkTitle"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: picker.workTitle
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.fontSize(14)
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
            KeyboardAction {
                id: closeAction
                objectName: "audiobookSourceClose"
                Layout.preferredWidth: Math.max(52, closeLabel.implicitWidth + 20)
                Layout.preferredHeight: 44
                accessibleName: qsTr("Close source picker")
                spaceActivates: true
                onTriggered: picker.close()
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: closeAction.hovered ? theme.glassHi : theme.glassTint
                    border.color: theme.edge
                    Text {
                        id: closeLabel
                        anchors.centerIn: parent
                        text: qsTr("Close")
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(13)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.edge
        }

        Text {
            objectName: "audiobookSourceEmpty"
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            visible: picker.rows.length === 0
            text: qsTr("No AudioBookBay uploads found.")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(14)
            wrapMode: Text.WordWrap
        }

        Flickable {
            id: list
            objectName: "audiobookSourceOptions"
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            visible: picker.rows.length > 0
            contentWidth: width
            contentHeight: options.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: options
                width: list.width - (list.contentHeight > list.height ? 10 : 0)
                Repeater {
                    model: picker.rows
                    delegate: KeyboardAction {
                        id: option
                        required property int index
                        required property var modelData
                        objectName: "audiobookSourceOption_" + index
                        width: options.width
                        height: Math.max(64, sourceDetails.implicitHeight + 20)
                        enabled: !!modelData.request
                        spaceActivates: true
                        accessibleName: sourceTitle.text
                        accessibleDescription: [recommendation.visible ? recommendation.text : "", metadata.text,
                            bundle.text, reason.text].filter(Boolean).join(" · ")
                        onTriggered: {
                            picker.sourcePicked(modelData)
                            picker.close()
                        }
                        onActiveFocusChanged: {
                            if (activeFocus && y < list.contentY)
                                list.contentY = y
                            else if (activeFocus && y + height > list.contentY + list.height)
                                list.contentY = y + height - list.height
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: option.interactionActive ? theme.glassTint : "transparent"
                        }
                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12

                            ColumnLayout {
                                id: sourceDetails
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        id: sourceTitle
                                        Layout.fillWidth: true
                                        text: option.modelData.title || qsTr("AudioBookBay upload")
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: theme.fontSize(14)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                    }
                                    Text {
                                        id: recommendation
                                        objectName: "audiobookSourceRecommendation_" + option.index
                                        visible: !!option.modelData.recommended
                                        text: qsTr("Recommended")
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: theme.fontSize(12)
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Text {
                                    id: metadata
                                    objectName: "audiobookSourceSummary" + option.index
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: picker.describe(option.modelData)
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: theme.fontSize(13)
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                }
                                Text {
                                    id: bundle
                                    objectName: "audiobookSourceBundle" + option.index
                                    visible: text.length > 0
                                    text: option.modelData.bundle ? picker.bundleLabel(option.modelData) : ""
                                    color: theme.gold
                                    font.family: theme.ui
                                    font.pixelSize: theme.fontSize(12)
                                }
                                Text {
                                    id: reason
                                    objectName: "audiobookSourceReason" + option.index
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: option.modelData.reason || (!option.enabled ? qsTr("Unavailable") : "")
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: theme.fontSize(12)
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                }
                            }
                            Rectangle {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                visible: option.enabled
                                radius: 18
                                color: theme.gold
                                Text {
                                    anchors.centerIn: parent
                                    text: "↓"
                                    color: "#1a1306"
                                    font.family: theme.ui
                                    font.pixelSize: theme.fontSize(16)
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: theme.edge
                            visible: option.index < picker.rows.length - 1
                        }
                    }
                }
            }
        }
    }
}
