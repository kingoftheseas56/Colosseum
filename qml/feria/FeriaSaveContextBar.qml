import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import "../account" as AccountUi

// A small reserved chrome row. Its parent must place the website below it.
Item {
    id: root
    objectName: "feriaSaveContextBar"
    property var tools: null
    readonly property var snapshot: tools ? tools.contextSnapshot : null
    readonly property bool open: snapshot !== null
    implicitHeight: open ? content.implicitHeight + 12 : 0
    height: implicitHeight
    visible: open
    component ContextButton: AccountUi.AccountButton {
        implicitWidth: Math.max(72, contentItem.implicitWidth + 18)
        implicitHeight: 32
        Accessible.name: text
    }
    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.margins: 6
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ContextButton {
                objectName: "feriaContextSavePage"
                text: qsTr("Save page")
                enabled: root.tools && root.tools.canSavePage
                onClicked: root.tools.savePage(formatChoice.currentValue)
            }
            ComboBox {
                id: formatChoice
                objectName: "feriaSavePageFormat"
                implicitWidth: 150; implicitHeight: 32
                textRole: "text"; valueRole: "value"; currentIndex: 0
                model: [
                    {text: "Web archive", value: WebEngineDownloadRequest.MimeHtmlSaveFormat},
                    {text: "HTML only", value: WebEngineDownloadRequest.SingleHtmlSaveFormat},
                    {text: "Complete HTML", value: WebEngineDownloadRequest.CompleteHtmlSaveFormat}
                ]
                Accessible.name: qsTr("Page save format")
            }
            ContextButton {
                objectName: "feriaContextSaveResource"
                readonly property string kind: root.tools && root.tools.canSaveImage ? "image"
                    : root.tools && root.tools.canSaveMedia ? "media" : "link"
                text: kind === "image" ? qsTr("Save image") : kind === "media" ? qsTr("Save media") : qsTr("Save link")
                visible: root.snapshot && (root.snapshot.linkUrl.length > 0 || root.snapshot.mediaType !== ContextMenuRequest.MediaTypeNone)
                enabled: root.tools && (root.tools.canSaveLink || root.tools.canSaveImage || root.tools.canSaveMedia)
                onClicked: root.tools.saveFromContext(root.snapshot.contextId, kind)
            }
            ContextButton {
                objectName: "feriaContextOpenLink"
                text: qsTr("Open link")
                visible: root.snapshot && root.snapshot.linkUrl.length > 0
                enabled: root.snapshot && root.tools.admittedSaveUrl(root.snapshot.linkUrl)
                onClicked: root.tools.contextNativeAction(root.snapshot.contextId, "openLink")
            }
            ContextButton {
                objectName: "feriaContextCopy"
                text: root.snapshot && root.snapshot.linkUrl.length ? qsTr("Copy link")
                    : root.snapshot && root.snapshot.mediaType === ContextMenuRequest.MediaTypeImage
                        ? qsTr("Copy image") : qsTr("Copy media link")
                visible: root.snapshot && (root.snapshot.linkUrl.length || root.snapshot.mediaUrl.length)
                onClicked: root.tools.contextNativeAction(root.snapshot.contextId,
                    root.snapshot.linkUrl.length ? "copyLink"
                    : root.snapshot.mediaType === ContextMenuRequest.MediaTypeImage ? "copyImage" : "copyMediaLink")
            }
            Item { Layout.fillWidth: true }
            ContextButton {
                objectName: "feriaContextDismiss"
                text: qsTr("Dismiss")
                onClicked: root.tools.invalidateSaveContext("dismissed")
            }
        }
        Text {
            Layout.fillWidth: true
            color: "#b7bfce"; font.pixelSize: 11
            text: root.snapshot && !root.snapshot.mainFrameOnly
                ? qsTr("Frame-specific saving is unavailable on this engine.")
                : qsTr("Native Save uses Downloads. Protected streams and canvas export are unavailable.")
            wrapMode: Text.WordWrap
        }
    }
}
