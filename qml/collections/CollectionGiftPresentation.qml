pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ".."

Rectangle {
    id: gift
    property var presentation: ({})
    property string collectionName: ""
    property bool opened: false
    property bool initiallyOpened: false
    property bool preview: false
    readonly property string preset: String(presentation.preset || "midnight")
    readonly property color ink: preset === "paper" ? "#332d25" : "#f1eee6"
    readonly property color accent: preset === "sea" ? "#9fcac6" : preset === "paper" ? "#805c34" : "#e0bd79"
    implicitHeight: body.implicitHeight + 48
    radius: 14
    color: preset === "paper" ? "#e8dfce" : preset === "sea" ? "#153a40" : "#202432"
    border.color: accent
    border.width: 1
    Theme { id: theme }
    onPresentationChanged: opened = preview || initiallyOpened
    onPreviewChanged: opened = preview || initiallyOpened
    onInitiallyOpenedChanged: if (initiallyOpened) opened = true
    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 24
        spacing: 15
        Text {
            Layout.fillWidth: true
            text: gift.preview ? qsTr("A preview of your gift") : qsTr("A collection, just for you")
            color: gift.accent
            font.family: theme.ui
            font.pixelSize: 12
            font.letterSpacing: 1.1
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: gift.collectionName
            color: gift.ink
            font.family: theme.display
            font.pixelSize: 28
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        CollectionActionButton {
            objectName: gift.preview ? "previewCollectionGift" : "openCollectionGift"
            visible: !gift.opened && !gift.preview
            text: qsTr("Open your gift")
            emphasis: "primary"
            onClicked: { gift.opened = true; gift.forceActiveFocus(Qt.OtherFocusReason) }
        }
        Image {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.min(240, width * 0.6) : 0
            visible: (gift.opened || gift.preview) && !!gift.presentation.cover_data
            source: visible ? gift.presentation.cover_data : ""
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 1024
            sourceSize.height: 1024
            Accessible.name: qsTr("Personal gift cover")
        }
        Text {
            Layout.fillWidth: true
            visible: gift.opened || gift.preview
            text: gift.presentation.dedication || qsTr("A few things worth sharing.")
            color: gift.ink
            font.family: theme.ui
            font.pixelSize: 16
            lineHeight: 1.3
            textFormat: Text.PlainText
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Text {
            Layout.fillWidth: true
            visible: (gift.opened || gift.preview) && !!gift.presentation.signature
            text: gift.presentation.signature || ""
            color: gift.accent
            font.family: theme.display
            font.pixelSize: 18
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
    }
}
